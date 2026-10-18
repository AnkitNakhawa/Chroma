#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "chroma/core/composition.hpp"
#include "chroma/core/cache.hpp"
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
  std::int64_t audio_samples_encoded{0};
  std::int64_t audio_nanoseconds{0};
  CpuCacheUsage cache{};
};

[[nodiscard]] VideoRenderResult render_video_cpu(const Composition& composition,
                                                 const std::filesystem::path& output_path,
                                                 CpuCacheConfig cache_config = {});
[[nodiscard]] std::string video_render_profile_json(const VideoRenderResult& result);
[[nodiscard]] std::string video_benchmark_json(
    const std::vector<VideoRenderResult>& iterations);

}  // namespace chroma
