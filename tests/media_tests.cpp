#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "chroma/media/media.hpp"
#include "chroma/media/decode.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": check failed: " << expression << '\n';
    ++failures;
  }
}

template <typename Exception, typename Operation>
void check_throws(Operation operation, std::string_view expression, int line) {
  try {
    operation();
    std::cerr << "line " << line << ": expected exception: " << expression << '\n';
    ++failures;
  } catch (const Exception&) {
  } catch (...) {
    std::cerr << "line " << line << ": wrong exception type: " << expression << '\n';
    ++failures;
  }
}

#define CHECK(expression) check((expression), #expression, __LINE__)
#define CHECK_THROWS_AS(expression, exception_type) \
  check_throws<exception_type>([&] { static_cast<void>(expression); }, #expression, __LINE__)

void metadata_json_is_machine_readable() {
  const chroma::MediaInfo info{.width = 320,
                               .height = 180,
                               .time_base = chroma::Rational(1, 15360),
                               .frame_rate = chroma::Rational(30),
                               .duration = chroma::Rational(2),
                               .frame_count = 60,
                               .codec = "h264",
                               .pixel_format = "yuv420p"};
  const auto json = chroma::media_info_json(info);
  CHECK(json.find("\"width\": 320") != std::string::npos);
  CHECK(json.find("\"numerator\": 30") != std::string::npos);
  CHECK(json.find("\"codec\": \"h264\"") != std::string::npos);
}

void missing_media_is_rejected() {
  CHECK_THROWS_AS(chroma::probe_media("this-file-does-not-exist.mp4"), std::runtime_error);
}

void fixture_can_be_probed_and_decoded(const char* path) {
  const auto info = chroma::probe_media(path);
  CHECK(info.width == 64);
  CHECK(info.height == 36);
  CHECK(info.frame_rate == chroma::Rational(30));
  CHECK(info.duration == chroma::Rational(2));

  const auto start = chroma::decode_video_frame(path, chroma::Rational(0));
  const auto middle = chroma::decode_video_frame(path, chroma::Rational(1));
  const auto middle_again = chroma::decode_video_frame(path, chroma::Rational(1));
  CHECK(start.surface.width() == 64);
  CHECK(start.surface.height() == 36);
  CHECK(start.presentation_time == chroma::Rational(0));
  CHECK(middle.presentation_time == chroma::Rational(1));
  CHECK(start.surface.pixels() != middle.surface.pixels());
  CHECK(middle.surface.pixels() == middle_again.surface.pixels());
}

}  // namespace

int main(int argc, char** argv) {
  metadata_json_is_machine_readable();
  missing_media_is_rejected();
  if (chroma::media_available() && argc == 2) {
    fixture_can_be_probed_and_decoded(argv[1]);
  }
  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "media tests passed (FFmpeg "
            << (chroma::media_available() ? "enabled" : "disabled") << ")\n";
  return EXIT_SUCCESS;
}
