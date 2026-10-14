#pragma once

#include <cstdint>
#include <filesystem>

#include "chroma/core/composition.hpp"

namespace chroma {

struct VideoRenderResult {
  std::int64_t frames_encoded;
};

[[nodiscard]] VideoRenderResult render_video_cpu(const Composition& composition,
                                                 const std::filesystem::path& output_path);

}  // namespace chroma
