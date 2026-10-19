#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "chroma/core/composition.hpp"
#include "chroma/core/cache.hpp"
#include "chroma/core/render.hpp"
#include "chroma/media/telemetry.hpp"

namespace chroma {

enum class VideoRenderPhase : std::uint32_t { preparing = 0, rendering = 1, finalizing = 2, completed = 3 };
struct VideoRenderProgress {
  VideoRenderPhase phase;
  std::int64_t frames_encoded;
  std::int64_t total_frames;
  std::int64_t audio_samples_encoded;
  std::int64_t total_audio_samples;
  std::int64_t elapsed_nanoseconds;
};
struct VideoRenderControl {
  // Called on the rendering thread. Return true to cancel, except at completed.
  std::function<bool(const VideoRenderProgress&)> progress;
  // Polled at safe boundaries; may read an atomic token or signal flag.
  std::function<bool()> cancelled;
};
class RenderCancelled : public std::runtime_error {
 public:
  RenderCancelled() : std::runtime_error("render cancelled") {}
};

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
  CpuGraphicsStats graphics{};
};

[[nodiscard]] VideoRenderResult render_video_cpu(const Composition& composition,
                                                 const std::filesystem::path& output_path,
                                                 CpuCacheConfig cache_config = {},
                                                 CpuExecutionMode execution_mode = CpuExecutionMode::fused,
                                                 const VideoRenderControl& control = {});
[[nodiscard]] std::string video_render_profile_json(const VideoRenderResult& result);
[[nodiscard]] std::string video_benchmark_json(
    const std::vector<VideoRenderResult>& iterations);

}  // namespace chroma
