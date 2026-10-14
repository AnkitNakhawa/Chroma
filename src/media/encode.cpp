#include "chroma/media/encode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "chroma/core/render.hpp"

#ifndef CHROMA_HAS_FFMPEG
#define CHROMA_HAS_FFMPEG 0
#endif

#if CHROMA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
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
  void operator()(AVFormatContext* value) const noexcept {
    if (value == nullptr) {
      return;
    }
    if (!(value->oformat->flags & AVFMT_NOFILE) && value->pb != nullptr) {
      avio_closep(&value->pb);
    }
    avformat_free_context(value);
  }
};

struct CodecDeleter {
  void operator()(AVCodecContext* value) const noexcept { avcodec_free_context(&value); }
};

struct FrameDeleter {
  void operator()(AVFrame* value) const noexcept { av_frame_free(&value); }
};

struct PacketDeleter {
  void operator()(AVPacket* value) const noexcept { av_packet_free(&value); }
};

struct ScaleDeleter {
  void operator()(SwsContext* value) const noexcept { sws_freeContext(value); }
};

using FormatPointer = std::unique_ptr<AVFormatContext, FormatDeleter>;
using CodecPointer = std::unique_ptr<AVCodecContext, CodecDeleter>;
using FramePointer = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPointer = std::unique_ptr<AVPacket, PacketDeleter>;
using ScalePointer = std::unique_ptr<SwsContext, ScaleDeleter>;

std::uint8_t linear_to_byte(float value) noexcept {
  value = std::clamp(value, 0.0F, 1.0F);
  const auto srgb = value <= 0.0031308F
                        ? value * 12.92F
                        : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
  return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}

void write_available_packets(AVCodecContext* codec, AVFormatContext* format, AVStream* stream,
                             AVPacket* packet) {
  while (true) {
    const auto status = avcodec_receive_packet(codec, packet);
    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
      return;
    }
    if (status < 0) {
      throw std::runtime_error("video encode failed: " + ffmpeg_error(status));
    }
    av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
    packet->stream_index = stream->index;
    const auto write_status = av_interleaved_write_frame(format, packet);
    av_packet_unref(packet);
    if (write_status < 0) {
      throw std::runtime_error("unable to mux encoded video packet: " +
                               ffmpeg_error(write_status));
    }
  }
}

void submit_frame(AVCodecContext* codec, AVFormatContext* format, AVStream* stream,
                  AVPacket* packet, AVFrame* frame) {
  auto status = avcodec_send_frame(codec, frame);
  if (status == AVERROR(EAGAIN)) {
    write_available_packets(codec, format, stream, packet);
    status = avcodec_send_frame(codec, frame);
  }
  if (status < 0 && !(frame == nullptr && status == AVERROR_EOF)) {
    throw std::runtime_error("unable to submit video frame to encoder: " +
                             ffmpeg_error(status));
  }
  write_available_packets(codec, format, stream, packet);
}
#endif

}  // namespace

VideoRenderResult render_video_cpu(const Composition& composition,
                                   const std::filesystem::path& output_path) {
#if CHROMA_HAS_FFMPEG
  if (output_path.empty()) {
    throw std::invalid_argument("video output path must not be empty");
  }
  if (composition.width() % 2 != 0 || composition.height() % 2 != 0) {
    throw std::invalid_argument("H.264 output currently requires even canvas dimensions");
  }
  const auto& rate = composition.frame_rate().value();
  if (rate.numerator() > std::numeric_limits<int>::max() ||
      rate.denominator() > std::numeric_limits<int>::max()) {
    throw std::invalid_argument("frame rate exceeds FFmpeg encoder limits");
  }

  AVFormatContext* raw_format = nullptr;
  const auto output_string = output_path.string();
  auto status =
      avformat_alloc_output_context2(&raw_format, nullptr, nullptr, output_string.c_str());
  if (status < 0 || raw_format == nullptr) {
    throw std::runtime_error("unable to select output container for '" + output_string + "'");
  }
  FormatPointer format(raw_format);

  const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
  if (encoder == nullptr) {
    encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  }
  if (encoder == nullptr) {
    throw std::runtime_error("this FFmpeg build does not provide an H.264 encoder");
  }

  auto* stream = avformat_new_stream(format.get(), nullptr);
  if (stream == nullptr) {
    throw std::runtime_error("unable to create output video stream");
  }
  CodecPointer codec(avcodec_alloc_context3(encoder));
  if (!codec) {
    throw std::runtime_error("unable to allocate video encoder context");
  }
  codec->codec_id = encoder->id;
  codec->codec_type = AVMEDIA_TYPE_VIDEO;
  codec->width = composition.width();
  codec->height = composition.height();
  codec->time_base = AVRational{static_cast<int>(rate.denominator()),
                                static_cast<int>(rate.numerator())};
  codec->framerate = AVRational{static_cast<int>(rate.numerator()),
                                static_cast<int>(rate.denominator())};
  codec->pix_fmt = AV_PIX_FMT_YUV420P;
  codec->bit_rate = 4'000'000;
  codec->gop_size = 60;
  codec->max_b_frames = 2;
  if (format->oformat->flags & AVFMT_GLOBALHEADER) {
    codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
  if (codec->priv_data != nullptr) {
    av_opt_set(codec->priv_data, "preset", "medium", 0);
    av_opt_set(codec->priv_data, "crf", "18", 0);
  }
  status = avcodec_open2(codec.get(), encoder, nullptr);
  if (status < 0) {
    throw std::runtime_error("unable to open H.264 encoder: " + ffmpeg_error(status));
  }
  status = avcodec_parameters_from_context(stream->codecpar, codec.get());
  if (status < 0) {
    throw std::runtime_error("unable to configure output stream: " + ffmpeg_error(status));
  }
  stream->time_base = codec->time_base;
  stream->avg_frame_rate = codec->framerate;

  if (!(format->oformat->flags & AVFMT_NOFILE)) {
    status = avio_open(&format->pb, output_string.c_str(), AVIO_FLAG_WRITE);
    if (status < 0) {
      throw std::runtime_error("unable to open video output: " + ffmpeg_error(status));
    }
  }
  status = avformat_write_header(format.get(), nullptr);
  if (status < 0) {
    throw std::runtime_error("unable to write video header: " + ffmpeg_error(status));
  }

  FramePointer frame(av_frame_alloc());
  PacketPointer packet(av_packet_alloc());
  if (!frame || !packet) {
    throw std::runtime_error("unable to allocate video encoder buffers");
  }
  frame->format = codec->pix_fmt;
  frame->width = codec->width;
  frame->height = codec->height;
  status = av_frame_get_buffer(frame.get(), 32);
  if (status < 0) {
    throw std::runtime_error("unable to allocate encoded frame storage: " +
                             ffmpeg_error(status));
  }

  ScalePointer scale(sws_getContext(codec->width, codec->height, AV_PIX_FMT_RGB24,
                                    codec->width, codec->height, codec->pix_fmt, SWS_BILINEAR,
                                    nullptr, nullptr, nullptr));
  if (!scale) {
    throw std::runtime_error("unable to create encoder color conversion context");
  }
  const auto row_bytes = static_cast<std::size_t>(codec->width) * 3U;
  std::vector<std::uint8_t> rgb(row_bytes * static_cast<std::size_t>(codec->height));

  std::int64_t encoded = 0;
  for (auto index = composition.range().start().value();
       index < composition.range().end().value(); ++index) {
    const auto surface = render_frame_cpu(composition, FrameIndex(index));
    for (std::size_t pixel_index = 0; pixel_index < surface.size(); ++pixel_index) {
      const auto& pixel = surface.pixels()[pixel_index];
      rgb[pixel_index * 3U] = linear_to_byte(pixel.red);
      rgb[pixel_index * 3U + 1U] = linear_to_byte(pixel.green);
      rgb[pixel_index * 3U + 2U] = linear_to_byte(pixel.blue);
    }

    status = av_frame_make_writable(frame.get());
    if (status < 0) {
      throw std::runtime_error("encoded frame buffer is not writable: " +
                               ffmpeg_error(status));
    }
    const std::uint8_t* input_data[] = {rgb.data(), nullptr, nullptr, nullptr};
    const int input_lines[] = {static_cast<int>(row_bytes), 0, 0, 0};
    const auto rows = sws_scale(scale.get(), input_data, input_lines, 0, codec->height,
                                frame->data, frame->linesize);
    if (rows != codec->height) {
      throw std::runtime_error("encoder color conversion returned an incomplete frame");
    }
    frame->pts = encoded;
    frame->duration = 1;
    submit_frame(codec.get(), format.get(), stream, packet.get(), frame.get());
    ++encoded;
  }

  submit_frame(codec.get(), format.get(), stream, packet.get(), nullptr);
  stream->duration = av_rescale_q(encoded, codec->time_base, stream->time_base);
  status = av_write_trailer(format.get());
  if (status < 0) {
    throw std::runtime_error("unable to finalize video output: " + ffmpeg_error(status));
  }
  return VideoRenderResult{.frames_encoded = encoded};
#else
  static_cast<void>(composition);
  static_cast<void>(output_path);
  throw std::runtime_error("Chroma was built without FFmpeg media support");
#endif
}

}  // namespace chroma
