#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "chroma/media/audio.hpp"
#include "chroma/media/media.hpp"

namespace {
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "line " << __LINE__ << ": " << #expression << '\n'; ++failures; } } while (false)
template<class F> void throws(F operation) {
  bool rejected = false;
  try { operation(); } catch (const std::exception&) { rejected = true; }
  CHECK(rejected);
}

struct TemporaryDirectory {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
    ("chroma-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { std::filesystem::create_directory(path); }
  ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

// PCM16 fixture with a source-time marker: first half 0.25, second half 0.5.
void write_wave(const std::filesystem::path& path, int rate, int channels) {
  std::ofstream out(path, std::ios::binary);
  auto integer = [&](unsigned value, int bytes) {
    for (int byte = 0; byte < bytes; ++byte) out.put(static_cast<char>((value >> (byte * 8)) & 255));
  };
  const auto bytes = static_cast<unsigned>(rate * 2 * channels * 2);
  out.write("RIFF", 4); integer(36 + bytes, 4); out.write("WAVEfmt ", 8);
  integer(16, 4); integer(1, 2); integer(static_cast<unsigned>(channels), 2);
  integer(static_cast<unsigned>(rate), 4); integer(static_cast<unsigned>(rate * channels * 2), 4);
  integer(static_cast<unsigned>(channels * 2), 2); integer(16, 2);
  out.write("data", 4); integer(bytes, 4);
  for (int sample = 0; sample < rate * 2; ++sample)
    for (int channel = 0; channel < channels; ++channel)
      integer(static_cast<unsigned>(sample < rate ? 8192 : (channel == 0 ? 16384 : 8192)), 2);
  if (!out) throw std::runtime_error("failed to create audio fixture");
}

chroma::Composition composition(const std::filesystem::path& path,
                                std::vector<chroma::AudioTrack> tracks,
                                chroma::FrameRate rate = chroma::FrameRate(chroma::Rational(30)),
                                int frames = 90) {
  return chroma::Composition(1, 16, 16, rate,
    chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(frames)),
    {}, {}, {}, {}, {{"sound", path}}, std::move(tracks));
}
chroma::AudioTrack track(const char* id, int start, int end, int source = 0, double gain = 1) {
  return {id, chroma::FrameRange(chroma::FrameIndex(start), chroma::FrameIndex(end)),
          "sound", chroma::FrameIndex(source), gain};
}
std::vector<float> collect(chroma::AudioMixer& mixer, int block = 997) {
  std::vector<float> result;
  while (mixer.position() < mixer.sample_count()) {
    auto next = mixer.read(static_cast<int>(std::min<std::int64_t>(block, mixer.sample_count() - mixer.position())));
    result.insert(result.end(), next.begin(), next.end());
  }
  return result;
}
void mixer_tests(const std::filesystem::path& wave) {
  const auto input = composition(wave, {track("trim", 15, 45, 30, 0.5), track("overlap", 30, 60)});
  chroma::AudioMixer mixer(input);
  CHECK(mixer.sample_count() == 144000);
  const auto output = collect(mixer);
  auto value = [&](int sample, int channel = 0) { return output[static_cast<std::size_t>(sample * 2 + channel)]; };
  CHECK(value(0) == 0);
  CHECK(value(23999) == 0);
  CHECK(value(24000) == 0.25F); // Trim skips the source's first second, then gain halves it.
  CHECK(value(24000, 1) == 0.125F);
  CHECK(value(48000) == 0.5F); // Two sources overlap.
  CHECK(value(71999) == 0.5F);
  CHECK(value(72000) == 0.25F); // First track ends exclusively.
  CHECK(value(95999) == 0.25F);
  CHECK(value(96000) == 0);
  CHECK(value(143999) == 0);
  chroma::AudioMixer different_blocks(input);
  CHECK(collect(different_blocks, 4096) == output);
  throws([&] { static_cast<void>(mixer.read(1)); });
  throws([&] { static_cast<void>(mixer.read(-1)); });
  throws([&] { static_cast<void>(mixer.read(65537)); });
  CHECK(mixer.read(0).empty());

  chroma::AudioMixer clipping(composition(wave, {track("a", 0, 30, 30, 2), track("b", 0, 30, 30, 2)}));
  const auto clipped = clipping.read(100);
  CHECK(clipped[0] == 1.0F);
  CHECK(clipped[1] == 1.0F);
  chroma::AudioMixer muted(composition(wave, {track("mute", 0, 30, 0, 0)}));
  const auto silence = collect(muted);
  CHECK(std::all_of(silence.begin(), silence.end(), [](float v) { return v == 0; }));
  chroma::AudioMixer short_source(composition(wave, {track("too-long", 0, 90)}));
  throws([&] { static_cast<void>(collect(short_source)); });
  // A direct C++ composition may have a nonzero timeline origin.
  chroma::Composition offset(1, 16, 16, chroma::FrameRate(chroma::Rational(30)),
    chroma::FrameRange(chroma::FrameIndex(30), chroma::FrameIndex(60)), {}, {}, {}, {},
    {{"sound", wave}}, {track("offset", 30, 60, 30, 0.5)});
  chroma::AudioMixer offset_mixer(offset);
  CHECK(offset_mixer.sample_count() == 48000);
  CHECK(offset_mixer.read(1)[0] == 0.25F);
  chroma::AudioMixer missing(composition(wave.parent_path() / "absent.wav", {track("bad", 0, 30)}));
  throws([&] { static_cast<void>(missing.read(1)); });
}
void resampling_tests(const std::filesystem::path& wave) {
  const auto rate = chroma::FrameRate(chroma::Rational(30000, 1001));
  chroma::AudioMixer mixer(composition(wave, {track("resample", 1, 31)}, rate, 60));
  const auto output = collect(mixer);
  CHECK(mixer.sample_count() == 96096);
  CHECK(output[1601 * 2] == 0);
  CHECK(std::abs(output[1700 * 2] - 0.25F / std::sqrt(2.0F)) < 0.001F);
  CHECK(output[1700 * 2] == output[1700 * 2 + 1]);
  CHECK(output[49650 * 2] == 0);
  // End-to-end resampling flush: consume the entire two-second source.
  chroma::AudioMixer flush(composition(wave, {track("full", 0, 60)}, chroma::FrameRate(chroma::Rational(30)), 60));
  CHECK(collect(flush).size() == 192000);
}
}  // namespace

int main() {
  chroma::configure_media_logging(chroma::MediaLogLevel::error);
  CHECK(chroma::audio_sample_at(chroma::FrameRate(chroma::Rational(30000, 1001)), chroma::FrameIndex(1)) == 1602);
  CHECK(chroma::audio_sample_at(chroma::FrameRate(chroma::Rational(30000, 1001)), chroma::FrameIndex(60)) == 96096);
  CHECK(chroma::audio_sample_at(chroma::FrameRate(chroma::Rational(96000)), chroma::FrameIndex(1)) == 1);
  TemporaryDirectory temporary;
  if (chroma::media_available()) {
    const auto stereo = temporary.path / "stereo.wav";
    const auto mono = temporary.path / "mono.wav";
    write_wave(stereo, 48000, 2);
    write_wave(mono, 44100, 1);
    mixer_tests(stereo);
    resampling_tests(mono);
  } else {
    throws([&] { chroma::AudioMixer mixer(composition("missing.wav", {})); });
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
