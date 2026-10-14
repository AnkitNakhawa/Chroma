#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "chroma/media/media.hpp"

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

}  // namespace

int main() {
  metadata_json_is_machine_readable();
  missing_media_is_rejected();
  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "media tests passed (FFmpeg "
            << (chroma::media_available() ? "enabled" : "disabled") << ")\n";
  return EXIT_SUCCESS;
}
