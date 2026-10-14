#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "chroma/core/rational.hpp"

namespace chroma {

struct MediaInfo {
  std::int32_t width;
  std::int32_t height;
  Rational time_base;
  std::optional<Rational> frame_rate;
  std::optional<Rational> duration;
  std::optional<std::int64_t> frame_count;
  std::string codec;
  std::string pixel_format;
};

[[nodiscard]] bool media_available() noexcept;
[[nodiscard]] MediaInfo probe_media(const std::filesystem::path& path);
[[nodiscard]] std::string media_info_json(const MediaInfo& info);

}  // namespace chroma
