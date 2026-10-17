#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "chroma/core/composition.hpp"

namespace chroma {

inline constexpr int audio_sample_rate = 48000;
inline constexpr int audio_channels = 2;

// Exact rational frame-to-sample mapping, nearest sample with ties rounded up.
[[nodiscard]] std::int64_t audio_sample_at(const FrameRate& rate, FrameIndex frame);

// Sequential job-scoped mixer. Output is interleaved stereo float PCM; overlapping
// tracks sum in declaration order and saturate once after mixing. EOF inside a
// requested source trim is an error. No FFmpeg types cross this boundary.
class AudioMixer {
 public:
  explicit AudioMixer(const Composition& composition);
  ~AudioMixer();
  AudioMixer(const AudioMixer&) = delete;
  AudioMixer& operator=(const AudioMixer&) = delete;
  [[nodiscard]] std::int64_t sample_count() const noexcept;
  [[nodiscard]] std::int64_t position() const noexcept;
  [[nodiscard]] std::vector<float> read(int samples);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chroma
