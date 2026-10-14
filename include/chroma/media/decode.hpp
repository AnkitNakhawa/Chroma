#pragma once

#include <filesystem>

#include "chroma/core/rational.hpp"
#include "chroma/core/render.hpp"

namespace chroma {

struct DecodedVideoFrame {
  Surface surface;
  Rational presentation_time;
};

[[nodiscard]] DecodedVideoFrame decode_video_frame(const std::filesystem::path& path,
                                                    Rational time);

}  // namespace chroma
