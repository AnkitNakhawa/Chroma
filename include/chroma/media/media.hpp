#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
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

enum class InputMediaKind { video, image, audio };

struct InputMediaInfo {
  std::optional<Rational> duration;
  // Exact sample-count comparison is safe for 48 kHz PCM in WAVE. Other
  // audio durations are estimates and must not produce false hard failures.
  bool exact_audio_duration{false};
  bool hdr{false};
};

// Open the selected stream and decoder, and validate its conversion support.
// Inspect headers and the first decoded frame/block, not the entire source.
[[nodiscard]] InputMediaInfo inspect_media_input(const std::filesystem::path& path,
                                                 InputMediaKind kind,
                                                 const std::function<void()>& checkpoint = {});

enum class MediaLogLevel {
  quiet,
  error,
  warning,
  info,
};

[[nodiscard]] bool media_available() noexcept;
void configure_media_logging(MediaLogLevel level) noexcept;
[[nodiscard]] MediaInfo probe_media(const std::filesystem::path& path);
[[nodiscard]] std::string media_info_json(const MediaInfo& info);

}  // namespace chroma
