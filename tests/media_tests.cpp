#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chroma/media/media.hpp"
#include "chroma/media/decode.hpp"
#include "chroma/media/encode.hpp"
#include "chroma/media/frame.hpp"

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

void resident_frame_contract_validates_layout_and_lifetime() {
  auto storage = std::make_shared<std::vector<std::uint8_t>>(64U);
  std::weak_ptr<std::vector<std::uint8_t>> weak_storage = storage;
  {
    const chroma::ResidentFrame frame(
        {.width = 2,
         .height = 2,
         .memory_domain = chroma::FrameMemoryDomain::host,
         .pixel_format = chroma::FramePixelFormat::linear_rgba_f32,
         .color = {.range = chroma::FrameColorRange::full,
                   .transfer = chroma::FrameColorTransfer::linear,
                   .matrix = chroma::FrameColorMatrix::identity}},
        {{.address = reinterpret_cast<std::uintptr_t>(storage->data()),
          .byte_size = storage->size(),
          .row_stride_bytes = 32,
          .row_count = 2}},
        storage);
    storage.reset();
    CHECK(!weak_storage.expired());
    CHECK(frame.total_bytes() == 64);
    CHECK(frame.planes().size() == 1);
    CHECK(chroma::frame_memory_domain_name(frame.descriptor().memory_domain) == "host");
    CHECK(chroma::frame_pixel_format_name(frame.descriptor().pixel_format) ==
          "linear_rgba_f32");
  }
  CHECK(weak_storage.expired());

  const auto owner = std::make_shared<int>(1);
  CHECK_THROWS_AS(
      chroma::ResidentFrame(
          {.width = 2,
           .height = 2,
           .memory_domain = chroma::FrameMemoryDomain::host,
           .pixel_format = chroma::FramePixelFormat::yuv420p8,
           .color = {}},
          {{.address = 1, .byte_size = 4, .row_stride_bytes = 2, .row_count = 2}}, owner),
      std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::ResidentFrame(
          {.width = 2,
           .height = 2,
           .memory_domain = chroma::FrameMemoryDomain::host,
           .pixel_format = chroma::FramePixelFormat::rgba8_srgb,
           .color = {}},
          {{.address = 1, .byte_size = 16, .row_stride_bytes = 8, .row_count = 2}},
          std::shared_ptr<const void>{}),
      std::invalid_argument);
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
  CHECK(start.telemetry.frame_requests == 1);
  CHECK(start.telemetry.frames_decoded > 0);
  CHECK(start.telemetry.frames_converted == 1);
  CHECK(start.telemetry.native_bytes_estimated > 0);
  CHECK(start.telemetry.rgba8_bytes_written == 64U * 36U * 4U);
  CHECK(start.telemetry.linear_rgba_bytes_written ==
        64U * 36U * sizeof(chroma::Pixel));
  CHECK(start.telemetry.decode_nanoseconds >= 0);
  CHECK(start.telemetry.conversion_nanoseconds >= 0);

  chroma::VideoDecoder decoder(path);
  const auto sequential_start = decoder.frame_at(chroma::Rational(0));
  const auto sequential_middle = decoder.frame_at(chroma::Rational(1));
  const auto rewound = decoder.frame_at(chroma::Rational(0));
  CHECK(sequential_start.surface.pixels() == start.surface.pixels());
  CHECK(sequential_middle.surface.pixels() == middle.surface.pixels());
  CHECK(rewound.surface.pixels() == start.surface.pixels());
  CHECK_THROWS_AS(decoder.frame_at(chroma::Rational(2)), std::out_of_range);
}

void render_profiles_are_machine_readable() {
  const chroma::VideoRenderResult result{.frames_encoded = 60,
                                         .setup_nanoseconds = 1'000'000,
                                         .compile_nanoseconds = 2'000'000,
                                         .execute_nanoseconds = 3'000'000,
                                         .encode_nanoseconds = 4'000'000,
                                         .total_nanoseconds = 10'000'000,
                                         .composition_nanoseconds = 500'000,
                                         .decode = {.frame_requests = 60,
                                                    .frames_decoded = 61,
                                                    .frames_converted = 60,
                                                    .native_bytes_estimated = 100,
                                                    .rgba8_bytes_written = 200,
                                                    .linear_rgba_bytes_written = 300,
                                                    .decode_nanoseconds = 1'000'000,
                                                    .conversion_nanoseconds = 1'500'000},
                                         .encode = {.frames_converted = 60,
                                                    .linear_rgba_bytes_read = 400,
                                                    .rgb24_bytes_written = 500,
                                                    .yuv420_bytes_written = 600,
                                                    .linear_to_rgb_nanoseconds = 1'250'000,
                                                    .rgb_to_yuv_nanoseconds = 750'000,
                                                    .codec_mux_nanoseconds = 2'000'000}};
  const auto profile = chroma::video_render_profile_json(result);
  const auto benchmark = chroma::video_benchmark_json({result, result});
  CHECK(profile.find("frames_per_second") != std::string::npos);
  CHECK(profile.find("\"total\": 10.0") != std::string::npos);
  CHECK(profile.find("\"decode_requests\": 60") != std::string::npos);
  CHECK(profile.find("\"decode_conversion\": 1.5") != std::string::npos);
  CHECK(profile.find("\"encode_conversion\": 2.0") != std::string::npos);
  CHECK(profile.find("\"decoded_native_estimate\": 100") != std::string::npos);
  CHECK(benchmark.find("\"iteration_count\": 2") != std::string::npos);
  CHECK(benchmark.find("total_median") != std::string::npos);
  CHECK(benchmark.find("telemetry_totals") != std::string::npos);
  CHECK(benchmark.find("decode_conversion_mean") != std::string::npos);
}

void successful_render_reports_conversion_telemetry(const char* fixture_path) {
  const auto directory = std::filesystem::path(fixture_path).parent_path();
  const auto output = directory / "telemetry-render.mp4";
  std::error_code cleanup_error;
  std::filesystem::remove(output, cleanup_error);

  const chroma::Composition composition(
      1, 64, 36, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(2)),
      {chroma::VideoAsset{.id = "fixture", .path = fixture_path}},
      {chroma::VideoLayer{
          .id = "video",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(2)),
          .asset_id = "fixture",
          .source_start = chroma::FrameIndex(0),
          .opacity = 1.0}});
  const auto result = chroma::render_video_cpu(composition, output);
  CHECK(result.frames_encoded == 2);
  CHECK(result.decode.frame_requests == 2);
  CHECK(result.decode.frames_decoded >= 2);
  CHECK(result.decode.frames_converted == 2);
  CHECK(result.decode.native_bytes_estimated > 0);
  CHECK(result.decode.rgba8_bytes_written == 2U * 64U * 36U * 4U);
  CHECK(result.decode.linear_rgba_bytes_written ==
        2U * 64U * 36U * sizeof(chroma::Pixel));
  CHECK(result.encode.frames_converted == 2);
  CHECK(result.encode.linear_rgba_bytes_read ==
        2U * 64U * 36U * sizeof(chroma::Pixel));
  CHECK(result.encode.rgb24_bytes_written == 2U * 64U * 36U * 3U);
  CHECK(result.encode.yuv420_bytes_written == 2U * 64U * 36U * 3U / 2U);
  CHECK(result.composition_nanoseconds >= 0);
  CHECK(result.decode.decode_nanoseconds >= 0);
  CHECK(result.decode.conversion_nanoseconds >= 0);
  CHECK(result.encode.linear_to_rgb_nanoseconds >= 0);
  CHECK(result.encode.rgb_to_yuv_nanoseconds >= 0);
  CHECK(result.encode.codec_mux_nanoseconds >= 0);
  std::filesystem::remove(output, cleanup_error);
}

void failed_renders_remove_partial_outputs(const char* fixture_path) {
  const auto directory = std::filesystem::path(fixture_path).parent_path();
  const auto output = directory / "failed-render.mp4";
  std::error_code cleanup_error;
  std::filesystem::remove(output, cleanup_error);

  const chroma::Composition composition(
      1, 64, 36, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
      {chroma::VideoAsset{.id = "missing", .path = directory / "missing-video.mkv"}},
      {chroma::VideoLayer{
          .id = "video",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
          .asset_id = "missing",
          .source_start = chroma::FrameIndex(0),
          .opacity = 1.0}});
  CHECK_THROWS_AS(chroma::render_video_cpu(composition, output), std::runtime_error);
  CHECK(!std::filesystem::exists(output));
}

}  // namespace

int main(int argc, char** argv) {
  metadata_json_is_machine_readable();
  missing_media_is_rejected();
  resident_frame_contract_validates_layout_and_lifetime();
  render_profiles_are_machine_readable();
  if (chroma::media_available() && argc == 2) {
    fixture_can_be_probed_and_decoded(argv[1]);
    successful_render_reports_conversion_telemetry(argv[1]);
    failed_renders_remove_partial_outputs(argv[1]);
  }
  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "media tests passed (FFmpeg "
            << (chroma::media_available() ? "enabled" : "disabled") << ")\n";
  return EXIT_SUCCESS;
}
