#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "chroma/core/composition.hpp"
#include "chroma/media/telemetry.hpp"

namespace chroma {

struct VideoRenderResult {
  std::int64_t frames_encoded;
  std::int64_t setup_nanoseconds;
  std::int64_t compile_nanoseconds;
  std::int64_t execute_nanoseconds;
  std::int64_t encode_nanoseconds;
  std::int64_t total_nanoseconds;
  std::int64_t composition_nanoseconds;
  DecodeTelemetry decode;
  EncodeTelemetry encode;
};

[[nodiscard]] VideoRenderResult render_video_cpu(const Composition& composition,
                                                 const std::filesystem::path& output_path);
[[nodiscard]] std::string video_render_profile_json(const VideoRenderResult& result);
[[nodiscard]] std::string video_benchmark_json(
    const std::vector<VideoRenderResult>& iterations);

}  // namespace chroma
