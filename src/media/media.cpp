#include "chroma/media/media.hpp"

#include <memory>
#include <cstring>
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
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
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

InputMediaInfo inspect_media_input(const std::filesystem::path& path, InputMediaKind kind,
                                   const std::function<void()>& checkpoint) {
#if CHROMA_HAS_FFMPEG
  FormatContext format;
  if (const auto status = avformat_open_input(format.output(), path.string().c_str(), nullptr, nullptr); status < 0)
    throw std::runtime_error("unable to open media: " + ffmpeg_error(status));
  if (const auto status = avformat_find_stream_info(format.get(), nullptr); status < 0)
    throw std::runtime_error("unable to inspect media streams: " + ffmpeg_error(status));
  const AVCodec* decoder = nullptr;
  const auto type = kind == InputMediaKind::audio ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO;
  const int index = av_find_best_stream(format.get(), type, -1, -1, &decoder, 0);
  if (index < 0 || !decoder) throw std::runtime_error(type == AVMEDIA_TYPE_AUDIO ?
      "no decodable audio stream" : "no decodable video/image stream");
  auto* stream = format.get()->streams[index];
  const auto deleter = [](AVCodecContext* codec) { avcodec_free_context(&codec); };
  std::unique_ptr<AVCodecContext, decltype(deleter)> codec(avcodec_alloc_context3(decoder), deleter);
  if (!codec) throw std::bad_alloc();
  if (const auto status = avcodec_parameters_to_context(codec.get(), stream->codecpar); status < 0)
    throw std::runtime_error("unable to configure decoder: " + ffmpeg_error(status));
  if (const auto status = avcodec_open2(codec.get(), decoder, nullptr); status < 0)
    throw std::runtime_error("unable to open decoder: " + ffmpeg_error(status));
  if (type == AVMEDIA_TYPE_VIDEO && codec->width > 0 && codec->height > 0 &&
      static_cast<std::uint64_t>(codec->width) * codec->height > 67108864)
    throw std::runtime_error("media dimensions exceed 67108864 pixels");
  const auto free_frame = [](AVFrame* value) { av_frame_free(&value); };
  const auto free_packet = [](AVPacket* value) { av_packet_free(&value); };
  std::unique_ptr<AVFrame, decltype(free_frame)> frame(av_frame_alloc(), free_frame);
  std::unique_ptr<AVPacket, decltype(free_packet)> packet(av_packet_alloc(), free_packet);
  if (!frame || !packet) throw std::bad_alloc();
  bool flushing = false;
  for (;;) {
    if (checkpoint) checkpoint();
    const auto status = avcodec_receive_frame(codec.get(), frame.get());
    if (status == 0) break;
    if (status == AVERROR_EOF) throw std::runtime_error("media contains no decodable frames/samples");
    if (status != AVERROR(EAGAIN)) throw std::runtime_error("unable to decode first frame/block: " + ffmpeg_error(status));
    if (flushing) throw std::runtime_error("decoder produced no frames/samples after EOF");
    int read;
    do {
      if (checkpoint) checkpoint();
      av_packet_unref(packet.get());
      read = av_read_frame(format.get(), packet.get());
    } while (read >= 0 && packet->stream_index != index);
    if (read < 0 && read != AVERROR_EOF) throw std::runtime_error("unable to read media packet: " + ffmpeg_error(read));
    flushing = read == AVERROR_EOF;
    const auto sent = avcodec_send_packet(codec.get(), flushing ? nullptr : packet.get());
    av_packet_unref(packet.get());
    if (sent < 0) throw std::runtime_error("unable to submit media packet: " + ffmpeg_error(sent));
  }
  if (type == AVMEDIA_TYPE_VIDEO) {
    if (frame->width <= 0 || frame->height <= 0 ||
        static_cast<std::uint64_t>(frame->width) * frame->height > 67108864)
      throw std::runtime_error("media dimensions are invalid or exceed 67108864 pixels");
    if (!sws_isSupportedInput(static_cast<AVPixelFormat>(frame->format)))
      throw std::runtime_error("pixel format is not supported by the CPU conversion path");
  } else {
    if (frame->sample_rate <= 0 || frame->ch_layout.nb_channels <= 0 ||
        !av_channel_layout_check(&frame->ch_layout))
      throw std::runtime_error("invalid audio sample rate or channel layout");
    if (frame->format != AV_SAMPLE_FMT_NONE) {
      SwrContext* raw = nullptr;
      const auto cleanup = [](SwrContext* value) { swr_free(&value); };
      AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
      const auto status = swr_alloc_set_opts2(&raw, &stereo, AV_SAMPLE_FMT_FLT, 48000,
          &frame->ch_layout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr);
      std::unique_ptr<SwrContext, decltype(cleanup)> resampler(raw, cleanup);
      if (status < 0 || !raw || swr_init(raw) < 0)
        throw std::runtime_error("audio cannot be converted to 48 kHz stereo");
    }
  }
  InputMediaInfo result;
  result.hdr = type == AVMEDIA_TYPE_VIDEO && (codec->color_trc == AVCOL_TRC_SMPTE2084 || codec->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
      frame->color_trc == AVCOL_TRC_SMPTE2084 || frame->color_trc == AVCOL_TRC_ARIB_STD_B67);
  if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0 &&
      stream->time_base.num > 0 && stream->time_base.den > 0) {
    result.duration = from_av_rational(stream->time_base).scaled_by(stream->duration);
  } else if (format.get()->duration != AV_NOPTS_VALUE && format.get()->duration > 0) {
    result.duration = Rational(format.get()->duration, AV_TIME_BASE);
  }
  const auto* name = avcodec_get_name(codec->codec_id);
  result.exact_audio_duration = type == AVMEDIA_TYPE_AUDIO && codec->sample_rate == 48000 &&
      name && std::strncmp(name, "pcm_", 4) == 0 && std::string_view(format.get()->iformat->name) == "wav" &&
      stream->duration != AV_NOPTS_VALUE && stream->duration > 0;
  return result;
#else
  static_cast<void>(path); static_cast<void>(kind); static_cast<void>(checkpoint);
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
