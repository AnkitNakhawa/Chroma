#include "chroma/media/media.hpp"

#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#ifndef CHROMA_HAS_FFMPEG
#define CHROMA_HAS_FFMPEG 0
#endif

#if CHROMA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
}
#endif

namespace chroma {
namespace {

nlohmann::json rational_json(const Rational& value) {
  return {{"numerator", value.numerator()}, {"denominator", value.denominator()}};
}

#if CHROMA_HAS_FFMPEG
std::string ffmpeg_error(int status) {
  char buffer[AV_ERROR_MAX_STRING_SIZE]{};
  if (av_strerror(status, buffer, sizeof(buffer)) < 0) {
    return "unknown FFmpeg error";
  }
  return buffer;
}

class FormatContext {
 public:
  FormatContext() = default;
  FormatContext(const FormatContext&) = delete;
  FormatContext& operator=(const FormatContext&) = delete;
  ~FormatContext() { avformat_close_input(&value_); }

  [[nodiscard]] AVFormatContext** output() noexcept { return &value_; }
  [[nodiscard]] AVFormatContext* get() const noexcept { return value_; }

 private:
  AVFormatContext* value_ = nullptr;
};

Rational from_av_rational(AVRational value) {
  return Rational(value.num, value.den);
}
#endif

}  // namespace

bool media_available() noexcept {
  return CHROMA_HAS_FFMPEG != 0;
}

void configure_media_logging(MediaLogLevel level) noexcept {
#if CHROMA_HAS_FFMPEG
  switch (level) {
    case MediaLogLevel::quiet:
      av_log_set_level(AV_LOG_QUIET);
      break;
    case MediaLogLevel::error:
      av_log_set_level(AV_LOG_ERROR);
      break;
    case MediaLogLevel::warning:
      av_log_set_level(AV_LOG_WARNING);
      break;
    case MediaLogLevel::info:
      av_log_set_level(AV_LOG_INFO);
      break;
  }
#else
  static_cast<void>(level);
#endif
}

MediaInfo probe_media(const std::filesystem::path& path) {
#if CHROMA_HAS_FFMPEG
  FormatContext format;
  const auto path_string = path.string();
  auto status = avformat_open_input(format.output(), path_string.c_str(), nullptr, nullptr);
  if (status < 0) {
    throw std::runtime_error("unable to open media '" + path_string + "': " +
                             ffmpeg_error(status));
  }
  status = avformat_find_stream_info(format.get(), nullptr);
  if (status < 0) {
    throw std::runtime_error("unable to read media streams: " + ffmpeg_error(status));
  }

  const auto stream_index =
      av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    throw std::runtime_error("media does not contain a decodable video stream");
  }
  auto* stream = format.get()->streams[stream_index];
  const auto* parameters = stream->codecpar;
  if (parameters->width <= 0 || parameters->height <= 0) {
    throw std::runtime_error("video stream has invalid dimensions");
  }

  std::optional<Rational> frame_rate;
  const auto guessed_rate = av_guess_frame_rate(format.get(), stream, nullptr);
  if (guessed_rate.num > 0 && guessed_rate.den > 0) {
    frame_rate.emplace(from_av_rational(guessed_rate));
  }

  std::optional<Rational> duration;
  if (stream->duration != AV_NOPTS_VALUE && stream->time_base.num > 0 &&
      stream->time_base.den > 0) {
    duration.emplace(from_av_rational(stream->time_base).scaled_by(stream->duration));
  } else if (format.get()->duration != AV_NOPTS_VALUE) {
    duration.emplace(format.get()->duration, AV_TIME_BASE);
  }

  std::optional<std::int64_t> frame_count;
  if (stream->nb_frames > 0) {
    frame_count.emplace(stream->nb_frames);
  }

  const auto* pixel_format_name =
      parameters->format >= 0
          ? av_get_pix_fmt_name(static_cast<AVPixelFormat>(parameters->format))
          : nullptr;
  return MediaInfo{.width = parameters->width,
                   .height = parameters->height,
                   .time_base = from_av_rational(stream->time_base),
                   .frame_rate = std::move(frame_rate),
                   .duration = std::move(duration),
                   .frame_count = frame_count,
                   .codec = avcodec_get_name(parameters->codec_id),
                   .pixel_format = pixel_format_name != nullptr ? pixel_format_name : "unknown"};
#else
  static_cast<void>(path);
  throw std::runtime_error("Chroma was built without FFmpeg media support");
#endif
}

std::string media_info_json(const MediaInfo& info) {
  nlohmann::json output{{"width", info.width},
                        {"height", info.height},
                        {"time_base", rational_json(info.time_base)},
                        {"codec", info.codec},
                        {"pixel_format", info.pixel_format}};
  output["frame_rate"] = info.frame_rate ? rational_json(*info.frame_rate) : nlohmann::json(nullptr);
  output["duration"] = info.duration ? rational_json(*info.duration) : nlohmann::json(nullptr);
  output["frame_count"] = info.frame_count ? nlohmann::json(*info.frame_count) : nlohmann::json(nullptr);
  return output.dump(2);
}

}  // namespace chroma
