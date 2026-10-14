#include "chroma/media/decode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef CHROMA_HAS_FFMPEG
#define CHROMA_HAS_FFMPEG 0
#endif

#if CHROMA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}
#endif

namespace chroma {
namespace {

#if CHROMA_HAS_FFMPEG
std::string ffmpeg_error(int status) {
  char buffer[AV_ERROR_MAX_STRING_SIZE]{};
  if (av_strerror(status, buffer, sizeof(buffer)) < 0) {
    return "unknown FFmpeg error";
  }
  return buffer;
}

struct FormatDeleter {
  void operator()(AVFormatContext* value) const noexcept { avformat_close_input(&value); }
};

struct CodecDeleter {
  void operator()(AVCodecContext* value) const noexcept { avcodec_free_context(&value); }
};

struct PacketDeleter {
  void operator()(AVPacket* value) const noexcept { av_packet_free(&value); }
};

struct FrameDeleter {
  void operator()(AVFrame* value) const noexcept { av_frame_free(&value); }
};

struct ScaleDeleter {
  void operator()(SwsContext* value) const noexcept { sws_freeContext(value); }
};

using FormatPointer = std::unique_ptr<AVFormatContext, FormatDeleter>;
using CodecPointer = std::unique_ptr<AVCodecContext, CodecDeleter>;
using PacketPointer = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePointer = std::unique_ptr<AVFrame, FrameDeleter>;
using ScalePointer = std::unique_ptr<SwsContext, ScaleDeleter>;

std::int64_t floor_rescale(Rational time, AVRational time_base) {
  using Wide = __int128_t;
  if (time < Rational(0) || time_base.num <= 0 || time_base.den <= 0) {
    throw std::invalid_argument("media time and stream time base must be non-negative");
  }
  const auto numerator = static_cast<Wide>(time.numerator()) *
                         static_cast<Wide>(time_base.den);
  const auto denominator = static_cast<Wide>(time.denominator()) *
                           static_cast<Wide>(time_base.num);
  const auto result = numerator / denominator;
  if (result > static_cast<Wide>(std::numeric_limits<std::int64_t>::max())) {
    throw std::overflow_error("requested media timestamp is too large");
  }
  return static_cast<std::int64_t>(result);
}

std::int64_t checked_add(std::int64_t lhs, std::int64_t rhs) {
  using Wide = __int128_t;
  const auto result = static_cast<Wide>(lhs) + static_cast<Wide>(rhs);
  if (result < static_cast<Wide>(std::numeric_limits<std::int64_t>::min()) ||
      result > static_cast<Wide>(std::numeric_limits<std::int64_t>::max())) {
    throw std::overflow_error("requested media timestamp is too large");
  }
  return static_cast<std::int64_t>(result);
}

float srgb_to_linear(float value) noexcept {
  value = std::clamp(value, 0.0F, 1.0F);
  if (value <= 0.04045F) {
    return value / 12.92F;
  }
  return std::pow((value + 0.055F) / 1.055F, 2.4F);
}

Surface convert_frame(const AVFrame& frame) {
  if (frame.width <= 0 || frame.height <= 0) {
    throw std::runtime_error("decoder produced a frame with invalid dimensions");
  }
  auto* raw_scale = sws_getContext(
      frame.width, frame.height, static_cast<AVPixelFormat>(frame.format), frame.width,
      frame.height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (raw_scale == nullptr) {
    throw std::runtime_error("unable to create FFmpeg pixel conversion context");
  }
  ScalePointer scale(raw_scale);

  const auto row_bytes = static_cast<std::size_t>(frame.width) * 4U;
  std::vector<std::uint8_t> rgba(row_bytes * static_cast<std::size_t>(frame.height));
  std::uint8_t* output_data[] = {rgba.data(), nullptr, nullptr, nullptr};
  const int output_lines[] = {static_cast<int>(row_bytes), 0, 0, 0};
  const auto rows = sws_scale(scale.get(), frame.data, frame.linesize, 0, frame.height,
                              output_data, output_lines);
  if (rows != frame.height) {
    throw std::runtime_error("FFmpeg pixel conversion returned an incomplete frame");
  }

  Surface surface(frame.width, frame.height);
  for (std::int32_t y = 0; y < frame.height; ++y) {
    for (std::int32_t x = 0; x < frame.width; ++x) {
      const auto offset = static_cast<std::size_t>(y) * row_bytes +
                          static_cast<std::size_t>(x) * 4U;
      const auto alpha = static_cast<float>(rgba[offset + 3]) / 255.0F;
      surface.pixel(x, y) =
          Pixel{.red = srgb_to_linear(static_cast<float>(rgba[offset]) / 255.0F) * alpha,
                .green = srgb_to_linear(static_cast<float>(rgba[offset + 1]) / 255.0F) * alpha,
                .blue = srgb_to_linear(static_cast<float>(rgba[offset + 2]) / 255.0F) * alpha,
                .alpha = alpha};
    }
  }
  return surface;
}

Rational timestamp_time(std::int64_t timestamp, AVRational time_base,
                        std::int64_t stream_start) {
  return Rational(time_base.num, time_base.den).scaled_by(timestamp - stream_start);
}
#endif

}  // namespace

DecodedVideoFrame decode_video_frame(const std::filesystem::path& path, Rational time) {
#if CHROMA_HAS_FFMPEG
  if (time < Rational(0)) {
    throw std::invalid_argument("requested media time must be non-negative");
  }

  AVFormatContext* raw_format = nullptr;
  const auto path_string = path.string();
  auto status = avformat_open_input(&raw_format, path_string.c_str(), nullptr, nullptr);
  if (status < 0) {
    throw std::runtime_error("unable to open media '" + path_string + "': " +
                             ffmpeg_error(status));
  }
  FormatPointer format(raw_format);
  status = avformat_find_stream_info(format.get(), nullptr);
  if (status < 0) {
    throw std::runtime_error("unable to read media streams: " + ffmpeg_error(status));
  }

  const auto stream_index =
      av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (stream_index < 0) {
    throw std::runtime_error("media does not contain a decodable video stream");
  }
  auto* stream = format->streams[stream_index];
  const auto* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
  if (decoder == nullptr) {
    throw std::runtime_error("no decoder is available for the selected video stream");
  }

  CodecPointer codec(avcodec_alloc_context3(decoder));
  if (!codec) {
    throw std::runtime_error("unable to allocate FFmpeg decoder context");
  }
  status = avcodec_parameters_to_context(codec.get(), stream->codecpar);
  if (status < 0) {
    throw std::runtime_error("unable to initialize decoder parameters: " +
                             ffmpeg_error(status));
  }
  status = avcodec_open2(codec.get(), decoder, nullptr);
  if (status < 0) {
    throw std::runtime_error("unable to open video decoder: " + ffmpeg_error(status));
  }

  const auto stream_start = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
  const auto target = checked_add(stream_start, floor_rescale(time, stream->time_base));
  status = av_seek_frame(format.get(), stream_index, target, AVSEEK_FLAG_BACKWARD);
  if (status < 0) {
    throw std::runtime_error("unable to seek video stream: " + ffmpeg_error(status));
  }
  avcodec_flush_buffers(codec.get());

  PacketPointer packet(av_packet_alloc());
  FramePointer frame(av_frame_alloc());
  if (!packet || !frame) {
    throw std::runtime_error("unable to allocate FFmpeg decode buffers");
  }

  FramePointer selected;
  bool finished = false;
  const auto receive_frames = [&] {
    while (!finished) {
      const auto receive_status = avcodec_receive_frame(codec.get(), frame.get());
      if (receive_status == AVERROR(EAGAIN) || receive_status == AVERROR_EOF) {
        return;
      }
      if (receive_status < 0) {
        throw std::runtime_error("video decode failed: " + ffmpeg_error(receive_status));
      }
      const auto timestamp = frame->best_effort_timestamp;
      if (timestamp == AV_NOPTS_VALUE || timestamp <= target || !selected) {
        selected.reset(av_frame_clone(frame.get()));
        if (!selected) {
          throw std::runtime_error("unable to retain decoded video frame");
        }
      }
      if (timestamp != AV_NOPTS_VALUE && timestamp > target) {
        finished = true;
      }
      av_frame_unref(frame.get());
    }
  };

  while (!finished && (status = av_read_frame(format.get(), packet.get())) >= 0) {
    if (packet->stream_index == stream_index) {
      auto send_status = avcodec_send_packet(codec.get(), packet.get());
      if (send_status == AVERROR(EAGAIN)) {
        receive_frames();
        send_status = avcodec_send_packet(codec.get(), packet.get());
      }
      if (send_status < 0) {
        throw std::runtime_error("unable to submit video packet: " + ffmpeg_error(send_status));
      }
      receive_frames();
    }
    av_packet_unref(packet.get());
  }
  if (!finished) {
    if (status != AVERROR_EOF) {
      throw std::runtime_error("unable to read video packet: " + ffmpeg_error(status));
    }
    auto flush_status = avcodec_send_packet(codec.get(), nullptr);
    if (flush_status == AVERROR(EAGAIN)) {
      receive_frames();
      flush_status = avcodec_send_packet(codec.get(), nullptr);
    }
    if (flush_status < 0 && flush_status != AVERROR_EOF) {
      throw std::runtime_error("unable to flush video decoder: " + ffmpeg_error(flush_status));
    }
    receive_frames();
  }
  if (!selected) {
    throw std::out_of_range("requested time is outside the decodable video range");
  }

  const auto selected_timestamp = selected->best_effort_timestamp;
  return DecodedVideoFrame{
      .surface = convert_frame(*selected),
      .presentation_time = selected_timestamp == AV_NOPTS_VALUE
                               ? time
                               : timestamp_time(selected_timestamp, stream->time_base,
                                                stream_start)};
#else
  static_cast<void>(path);
  static_cast<void>(time);
  throw std::runtime_error("Chroma was built without FFmpeg media support");
#endif
}

}  // namespace chroma
