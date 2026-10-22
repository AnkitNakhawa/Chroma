#include "chroma/media/encode.hpp"

#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "chroma/core/output_color.hpp"

#include "chroma/core/render.hpp"
#include "chroma/core/preflight.hpp"
#include "chroma/media/audio.hpp"

#include <nlohmann/json.hpp>

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
using Clock = std::chrono::steady_clock;

std::int64_t elapsed_nanoseconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}
#endif

double milliseconds(std::int64_t nanoseconds) {
  return static_cast<double>(nanoseconds) / 1'000'000.0;
}

double frames_per_second(std::int64_t frames, std::int64_t nanoseconds) {
  if (nanoseconds <= 0) {
    return 0.0;
  }
  return static_cast<double>(frames) * 1'000'000'000.0 /
         static_cast<double>(nanoseconds);
}

nlohmann::json profile_json(const VideoRenderResult& result) {
  return {{"frames_encoded", result.frames_encoded},
          {"graphics", {{"bounded_operations", result.graphics.bounded_operations},
                        {"full_frame_operations", result.graphics.full_frame_operations},
                        {"source_pixels", result.graphics.source_pixels},
                        {"transform_pixels", result.graphics.transform_pixels},
                        {"composite_pixels", result.graphics.composite_pixels}}},
          {"cache", nlohmann::json::parse(cpu_cache_usage_json(result.cache))},
          {"audio_samples_encoded", result.audio_samples_encoded},
          {"frames_per_second",
           frames_per_second(result.frames_encoded, result.total_nanoseconds)},
          {"counts",
           {{"decode_requests", result.decode.frame_requests},
            {"frames_decoded", result.decode.frames_decoded},
            {"decode_conversions", result.decode.frames_converted},
            {"encode_conversions", result.encode.frames_converted}}},
          {"bytes",
           {{"decoded_native_estimate", result.decode.native_bytes_estimated},
            {"decode_rgba8_written", result.decode.rgba8_bytes_written},
            {"decode_linear_rgba_written", result.decode.linear_rgba_bytes_written},
            {"encode_linear_rgba_read", result.encode.linear_rgba_bytes_read},
            {"encode_rgb24_written", result.encode.rgb24_bytes_written},
            {"encode_yuv420_written", result.encode.yuv420_bytes_written}}},
          {"milliseconds",
           {{"audio", milliseconds(result.audio_nanoseconds)},
            {"setup", milliseconds(result.setup_nanoseconds)},
            {"compile", milliseconds(result.compile_nanoseconds)},
            {"decode", milliseconds(result.decode.decode_nanoseconds)},
            {"decode_conversion", milliseconds(result.decode.conversion_nanoseconds)},
            {"composition", milliseconds(result.composition_nanoseconds)},
            {"execute", milliseconds(result.execute_nanoseconds)},
            {"linear_to_rgb", milliseconds(result.encode.linear_to_rgb_nanoseconds)},
            {"rgb_to_yuv", milliseconds(result.encode.rgb_to_yuv_nanoseconds)},
            {"encode_conversion",
             milliseconds(result.encode.linear_to_rgb_nanoseconds +
                          result.encode.rgb_to_yuv_nanoseconds)},
            {"codec_mux", milliseconds(result.encode.codec_mux_nanoseconds)},
            {"encode", milliseconds(result.encode_nanoseconds)},
            {"total", milliseconds(result.total_nanoseconds)}}}};
}

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

// Reserve a private sibling directory exclusively, then atomically publish its
// file. Failure/cancellation never truncates an existing destination.
class OutputFileGuard {
 public:
  explicit OutputFileGuard(std::filesystem::path path) : path_(std::move(path)) {
    static std::atomic<std::uint64_t> sequence{0};
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
      directory_ = path_.parent_path() / (".chroma-render-" + std::to_string(random()) +
                    "-" + std::to_string(sequence.fetch_add(1)));
      if (std::filesystem::create_directory(directory_)) return;
    }
    throw std::runtime_error("unable to reserve temporary output directory");
  }
  ~OutputFileGuard() {
    std::error_code error;
    std::filesystem::remove_all(directory_, error);
  }
  std::filesystem::path temporary() const { return directory_ / "video"; }
  void commit() { std::filesystem::rename(temporary(), path_); }
 private:
  std::filesystem::path path_, directory_;
};

void check_cancelled(const VideoRenderControl& control) {
  if (control.cancelled && control.cancelled()) throw RenderCancelled();
}

using FormatPointer = std::unique_ptr<AVFormatContext, FormatDeleter>;
using CodecPointer = std::unique_ptr<AVCodecContext, CodecDeleter>;
using FramePointer = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPointer = std::unique_ptr<AVPacket, PacketDeleter>;
using ScalePointer = std::unique_ptr<SwsContext, ScaleDeleter>;

#ifdef CHROMA_REFERENCE_OUTPUT_CONVERSION
std::uint8_t linear_to_byte(float value) noexcept {
  value = std::clamp(value, 0.0F, 1.0F);
  const auto srgb = value <= 0.0031308F
                        ? value * 12.92F
                        : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
  return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}
#endif

void write_available_packets(AVCodecContext* codec, AVFormatContext* format, AVStream* stream,
                             AVPacket* packet, std::int64_t sample_limit = -1) {
  while (true) {
    const auto status = avcodec_receive_packet(codec, packet);
    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
      return;
    }
    if (status < 0) {
      throw std::runtime_error("media encode failed: " + ffmpeg_error(status));
    }
    if (sample_limit >= 0 && packet->pts != AV_NOPTS_VALUE) {
      if (packet->pts >= sample_limit) { av_packet_unref(packet); continue; }
      packet->duration = std::min(packet->duration, sample_limit - packet->pts);
    }
    av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
    packet->stream_index = stream->index;
    const auto write_status = av_interleaved_write_frame(format, packet);
    av_packet_unref(packet);
    if (write_status < 0) {
      throw std::runtime_error("unable to mux encoded media packet: " +
                               ffmpeg_error(write_status));
    }
  }
}

void submit_frame(AVCodecContext* codec, AVFormatContext* format, AVStream* stream,
                  AVPacket* packet, AVFrame* frame, std::int64_t sample_limit = -1) {
  auto status = avcodec_send_frame(codec, frame);
  if (status == AVERROR(EAGAIN)) {
    write_available_packets(codec, format, stream, packet, sample_limit);
    status = avcodec_send_frame(codec, frame);
  }
  if (status < 0 && !(frame == nullptr && status == AVERROR_EOF)) {
    throw std::runtime_error("unable to submit media frame to encoder: " +
                             ffmpeg_error(status));
  }
  write_available_packets(codec, format, stream, packet, sample_limit);
}
// Native AAC uses planar float stereo. Keep its codec/frame state job-local and
// feed full codec blocks except for the final partial block. Mux packet durations
// are capped at the actual sample count, excluding AAC tail padding.
class AudioOutput {
 public:
  AudioOutput(const Composition& composition, AVFormatContext* format)
      : mixer_(composition), format_(format) {
    if (mixer_.sample_count() <= 0)
      throw std::invalid_argument("audio composition must span at least one output sample");
    const auto* encoder = avcodec_find_encoder_by_name("aac");
    if (!encoder) throw std::runtime_error("this FFmpeg build does not provide the native AAC encoder");
    codec_.reset(avcodec_alloc_context3(encoder));
    if (!codec_) throw std::bad_alloc();
    codec_->sample_rate = audio_sample_rate;
    codec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    codec_->time_base = AVRational{1, audio_sample_rate};
    codec_->bit_rate = 192000;
    av_channel_layout_default(&codec_->ch_layout, audio_channels);
    if (format->oformat->flags & AVFMT_GLOBALHEADER) codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    auto status = avcodec_open2(codec_.get(), encoder, nullptr);
    if (status < 0) throw std::runtime_error("unable to open AAC encoder: " + ffmpeg_error(status));
    if (codec_->frame_size <= 0) throw std::runtime_error("AAC encoder has invalid frame size");
    stream_ = avformat_new_stream(format, nullptr);
    if (!stream_) throw std::bad_alloc();
    stream_->time_base = codec_->time_base;
    status = avcodec_parameters_from_context(stream_->codecpar, codec_.get());
    if (status < 0) throw std::runtime_error("unable to configure audio stream: " + ffmpeg_error(status));
    frame_.reset(av_frame_alloc());
    packet_.reset(av_packet_alloc());
    if (!frame_ || !packet_) throw std::bad_alloc();
    frame_->format = codec_->sample_fmt;
    frame_->sample_rate = audio_sample_rate;
    frame_->nb_samples = codec_->frame_size;
    status = av_channel_layout_copy(&frame_->ch_layout, &codec_->ch_layout);
    if (status < 0) throw std::runtime_error("unable to configure audio frame: " + ffmpeg_error(status));
    status = av_frame_get_buffer(frame_.get(), 0);
    if (status < 0) throw std::runtime_error("unable to allocate audio frame: " + ffmpeg_error(status));
  }
  void through(std::int64_t boundary, bool final, const VideoRenderControl& control) {
    while (mixer_.position() < boundary) {
      check_cancelled(control);
      const auto remaining = mixer_.sample_count() - mixer_.position();
      const auto count = static_cast<int>(std::min<std::int64_t>(codec_->frame_size, remaining));
      if (!final && count > boundary - mixer_.position()) break;
      const auto start = Clock::now();
      const auto pts = mixer_.position();
      const auto samples = mixer_.read(count);
      const auto status = av_frame_make_writable(frame_.get());
      if (status < 0) throw std::runtime_error("audio frame is not writable: " + ffmpeg_error(status));
      frame_->nb_samples = count;
      frame_->pts = pts;
      frame_->duration = count;
      for (int channel = 0; channel < audio_channels; ++channel) {
        auto* output = reinterpret_cast<float*>(frame_->data[channel]);
        for (int sample = 0; sample < count; ++sample)
          output[sample] = samples[static_cast<std::size_t>(sample * audio_channels + channel)];
      }
      submit_frame(codec_.get(), format_, stream_, packet_.get(), frame_.get(), mixer_.sample_count());
      nanoseconds_ += elapsed_nanoseconds(start, Clock::now());
    }
  }
  void finish(const VideoRenderControl& control) {
    through(mixer_.sample_count(), true, control);
    check_cancelled(control);
    const auto start = Clock::now();
    submit_frame(codec_.get(), format_, stream_, packet_.get(), nullptr, mixer_.sample_count());
    stream_->duration = av_rescale_q(mixer_.sample_count(), codec_->time_base, stream_->time_base);
    nanoseconds_ += elapsed_nanoseconds(start, Clock::now());
  }
  std::int64_t samples() const noexcept { return mixer_.position(); }
  std::int64_t nanoseconds() const noexcept { return nanoseconds_; }
 private:
  AudioMixer mixer_;
  AVFormatContext* format_;
  CodecPointer codec_;
  AVStream* stream_ = nullptr;
  FramePointer frame_;
  PacketPointer packet_;
  std::int64_t nanoseconds_ = 0;
};
#endif

}  // namespace

VideoRenderResult render_video_cpu(const Composition& composition,
                                   const std::filesystem::path& output_path, CpuCacheConfig cache_config, CpuExecutionMode execution_mode, const VideoRenderControl& control) {
#if CHROMA_HAS_FFMPEG
  const auto total_start = Clock::now();
  const auto total_frames = composition.range().length();
  const auto total_audio_samples = composition.audio_tracks().empty() ? 0 :
      audio_sample_at(composition.frame_rate(), composition.range().end()) -
      audio_sample_at(composition.frame_rate(), composition.range().start());
  auto report = [&](VideoRenderPhase phase, std::int64_t frames, std::int64_t samples) {
    check_cancelled(control);
    const VideoRenderProgress progress{phase, frames, total_frames, samples, total_audio_samples,
                                      elapsed_nanoseconds(total_start, Clock::now())};
    if (control.progress && control.progress(progress)) throw RenderCancelled();
    check_cancelled(control);
  };
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

  const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
  if (encoder == nullptr) {
    encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  }
  if (encoder == nullptr) {
    throw std::runtime_error("this FFmpeg build does not provide an H.264 encoder");
  }

  if (std::string_view(encoder->name) == "libopenh264" &&
      (composition.width() < 16 || composition.height() < 16)) {
    throw std::invalid_argument("OpenH264 output requires canvas dimensions of at least 16 pixels");
  }

  report(VideoRenderPhase::preparing, 0, 0);
  auto inputs = preflight_inputs(composition, {.for_video=true, .cancelled=control.cancelled});
  if (control.preflight) control.preflight(inputs);
  if (!inputs.ready()) throw PreflightError(std::move(inputs));
  AVFormatContext* raw_format = nullptr;
  const auto output_string = output_path.string();
  OutputFileGuard output_guard(output_path);
  auto status =
      avformat_alloc_output_context2(&raw_format, nullptr, nullptr, output_string.c_str());
  if (status < 0 || raw_format == nullptr) {
    throw std::runtime_error("unable to select output container for '" + output_string + "'");
  }
  FormatPointer format(raw_format);

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

  std::unique_ptr<AudioOutput> audio;
  if (!composition.audio_tracks().empty()) {
    audio = std::make_unique<AudioOutput>(composition, format.get());
    // Older MOV/MP4 muxers round edit-list duration to their default 1 kHz
    // movie clock, truncating short AAC tracks despite exact packet durations.
    // Use the audio sample clock when the container exposes this option.
    if (format->priv_data != nullptr &&
        av_opt_find(format->priv_data, "movie_timescale", nullptr, 0, 0) != nullptr) {
      status = av_opt_set_int(format->priv_data, "movie_timescale", audio_sample_rate, 0);
      if (status < 0)
        throw std::runtime_error("unable to configure sample-accurate MP4 timing: " + ffmpeg_error(status));
    }
  }

  if (!(format->oformat->flags & AVFMT_NOFILE)) {
    check_cancelled(control);
    status = avio_open(&format->pb, output_guard.temporary().string().c_str(), AVIO_FLAG_WRITE);
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
  const auto setup_end = Clock::now();

  std::int64_t encoded = 0;
  std::int64_t compile_nanoseconds = 0;
  std::int64_t execute_nanoseconds = 0;
  std::int64_t encode_nanoseconds = 0;
  std::int64_t composition_nanoseconds = 0;
  DecodeTelemetry decode_telemetry;
  EncodeTelemetry encode_telemetry;
  CpuGraphicsStats graphics;
  CpuRenderContext render_context(cache_config);
  for (auto index = composition.range().start().value();
       index < composition.range().end().value(); ++index) {
    check_cancelled(control);
    const auto compile_start = Clock::now();
    const auto plan = compile_frame(composition, FrameIndex(index));
    const auto compile_end = Clock::now();
    const auto surface = render_context.execute(plan, execution_mode);
    const auto execute_end = Clock::now();
    compile_nanoseconds += elapsed_nanoseconds(compile_start, compile_end);
    const auto frame_execute_nanoseconds = elapsed_nanoseconds(compile_end, execute_end);
    execute_nanoseconds += frame_execute_nanoseconds;
    const auto& execution_stats = render_context.last_execution_stats();
    decode_telemetry += execution_stats.decode;
    graphics += execution_stats.graphics;
    const auto frame_decode_nanoseconds = execution_stats.decode.decode_nanoseconds +
                                          execution_stats.decode.conversion_nanoseconds;
    composition_nanoseconds += std::max<std::int64_t>(
        0, frame_execute_nanoseconds - frame_decode_nanoseconds);

    check_cancelled(control);
    const auto encode_start = Clock::now();
#ifdef CHROMA_REFERENCE_OUTPUT_CONVERSION
    for (std::size_t pixel_index = 0; pixel_index < surface.size(); ++pixel_index) {
      const auto& pixel = surface.pixels()[pixel_index];
      rgb[pixel_index * 3U] = linear_to_byte(pixel.red);
      rgb[pixel_index * 3U + 1U] = linear_to_byte(pixel.green);
      rgb[pixel_index * 3U + 2U] = linear_to_byte(pixel.blue);
    }
#else
    convert_output_rgb8(surface.pixels().data(), surface.size(), rgb.data());
#endif
    const auto linear_to_rgb_end = Clock::now();
    encode_telemetry.linear_to_rgb_nanoseconds +=
        elapsed_nanoseconds(encode_start, linear_to_rgb_end);

    status = av_frame_make_writable(frame.get());
    if (status < 0) {
      throw std::runtime_error("encoded frame buffer is not writable: " +
                               ffmpeg_error(status));
    }
    const std::uint8_t* input_data[] = {rgb.data(), nullptr, nullptr, nullptr};
    const int input_lines[] = {static_cast<int>(row_bytes), 0, 0, 0};
    const auto rgb_to_yuv_start = Clock::now();
    const auto rows = sws_scale(scale.get(), input_data, input_lines, 0, codec->height,
                                frame->data, frame->linesize);
    const auto rgb_to_yuv_end = Clock::now();
    if (rows != codec->height) {
      throw std::runtime_error("encoder color conversion returned an incomplete frame");
    }
    ++encode_telemetry.frames_converted;
    const auto pixel_count = static_cast<std::uint64_t>(codec->width) *
                             static_cast<std::uint64_t>(codec->height);
    encode_telemetry.linear_rgba_bytes_read += pixel_count * sizeof(Pixel);
    encode_telemetry.rgb24_bytes_written += pixel_count * 3U;
    encode_telemetry.yuv420_bytes_written += pixel_count * 3U / 2U;
    encode_telemetry.rgb_to_yuv_nanoseconds +=
        elapsed_nanoseconds(rgb_to_yuv_start, rgb_to_yuv_end);
    frame->pts = encoded;
    frame->duration = 1;
    submit_frame(codec.get(), format.get(), stream, packet.get(), frame.get());
    ++encoded;
    encode_nanoseconds += elapsed_nanoseconds(encode_start, Clock::now());
    if (audio) {
      const auto boundary = audio_sample_at(composition.frame_rate(), FrameIndex(index + 1)) -
                            audio_sample_at(composition.frame_rate(), composition.range().start());
      audio->through(boundary, false, control);
    }
    report(VideoRenderPhase::rendering, encoded, audio ? audio->samples() : 0);
  }

  report(VideoRenderPhase::finalizing, encoded, audio ? audio->samples() : 0);
  const auto audio_before_finalize = audio ? audio->nanoseconds() : 0;
  const auto finalize_start = Clock::now();
  submit_frame(codec.get(), format.get(), stream, packet.get(), nullptr);
  stream->duration = av_rescale_q(encoded, codec->time_base, stream->time_base);
  if (audio) audio->finish(control);
  check_cancelled(control);
  status = av_write_trailer(format.get());
  if (status < 0) {
    throw std::runtime_error("unable to finalize video output: " + ffmpeg_error(status));
  }
  if (format->pb) {
    status = avio_closep(&format->pb);
    if (status < 0) throw std::runtime_error("unable to close video output: " + ffmpeg_error(status));
  }
  check_cancelled(control);
  output_guard.commit();
  // Publication is the point of no return. Completion is notification only:
  // cancellation/observer exceptions cannot turn a committed file into failure.
  if (control.progress) {
    try {
      control.progress({VideoRenderPhase::completed, encoded, total_frames,
                        audio ? audio->samples() : 0, total_audio_samples,
                        elapsed_nanoseconds(total_start, Clock::now())});
    } catch (...) {}
  }
  const auto total_end = Clock::now();
  encode_nanoseconds += elapsed_nanoseconds(finalize_start, total_end);
  // Audio work inside finalization is reported separately from video encoding.
  if (audio) encode_nanoseconds -= audio->nanoseconds() - audio_before_finalize;
  encode_telemetry.codec_mux_nanoseconds = std::max<std::int64_t>(
      0, encode_nanoseconds - encode_telemetry.linear_to_rgb_nanoseconds -
             encode_telemetry.rgb_to_yuv_nanoseconds);
  return VideoRenderResult{.frames_encoded = encoded,
                           .setup_nanoseconds = elapsed_nanoseconds(total_start, setup_end),
                           .compile_nanoseconds = compile_nanoseconds,
                           .execute_nanoseconds = execute_nanoseconds,
                           .encode_nanoseconds = encode_nanoseconds,
                           .total_nanoseconds = elapsed_nanoseconds(total_start, total_end),
                           .composition_nanoseconds = composition_nanoseconds,
                           .decode = decode_telemetry,
                           .encode = encode_telemetry,
                           .audio_samples_encoded = audio ? audio->samples() : 0,
                           .audio_nanoseconds = audio ? audio->nanoseconds() : 0,
                           .cache = render_context.cache_usage(),
                           .graphics = graphics};
#else
  static_cast<void>(composition);
  static_cast<void>(output_path);
  static_cast<void>(cache_config);
  static_cast<void>(execution_mode);
  static_cast<void>(control);
  throw std::runtime_error("Chroma was built without FFmpeg media support");
#endif
}

std::string video_render_profile_json(const VideoRenderResult& result) {
  return profile_json(result).dump(2);
}

std::string video_benchmark_json(const std::vector<VideoRenderResult>& iterations) {
  if (iterations.empty()) {
    throw std::invalid_argument("benchmark requires at least one iteration");
  }

  std::vector<std::int64_t> totals;
  totals.reserve(iterations.size());
  std::int64_t total_frames = 0;
  std::int64_t total_time = 0;
  std::int64_t total_setup = 0;
  std::int64_t total_compile = 0;
  std::int64_t total_execute = 0;
  std::int64_t total_encode = 0;
  std::int64_t total_composition = 0;
  std::int64_t total_audio = 0;
  std::int64_t total_audio_samples = 0;
  DecodeTelemetry total_decode;
  EncodeTelemetry total_encode_telemetry;
  nlohmann::json runs = nlohmann::json::array();
  for (const auto& iteration : iterations) {
    totals.push_back(iteration.total_nanoseconds);
    total_frames += iteration.frames_encoded;
    total_time += iteration.total_nanoseconds;
    total_setup += iteration.setup_nanoseconds;
    total_compile += iteration.compile_nanoseconds;
    total_execute += iteration.execute_nanoseconds;
    total_encode += iteration.encode_nanoseconds;
    total_composition += iteration.composition_nanoseconds;
    total_audio += iteration.audio_nanoseconds;
    total_audio_samples += iteration.audio_samples_encoded;
    total_decode += iteration.decode;
    total_encode_telemetry += iteration.encode;
    runs.push_back(profile_json(iteration));
  }
  std::sort(totals.begin(), totals.end());
  const auto count = static_cast<double>(iterations.size());
  return nlohmann::json{
      {"backend", "cpu"},
      {"iteration_count", iterations.size()},
      {"telemetry_totals",
       {{"counts",
         {{"audio_samples_encoded", total_audio_samples},
          {"decode_requests", total_decode.frame_requests},
          {"frames_decoded", total_decode.frames_decoded},
          {"decode_conversions", total_decode.frames_converted},
          {"encode_conversions", total_encode_telemetry.frames_converted}}},
        {"bytes",
         {{"decoded_native_estimate", total_decode.native_bytes_estimated},
          {"decode_rgba8_written", total_decode.rgba8_bytes_written},
          {"decode_linear_rgba_written", total_decode.linear_rgba_bytes_written},
          {"encode_linear_rgba_read", total_encode_telemetry.linear_rgba_bytes_read},
          {"encode_rgb24_written", total_encode_telemetry.rgb24_bytes_written},
          {"encode_yuv420_written", total_encode_telemetry.yuv420_bytes_written}}}}},
      {"summary",
       {{"frames_per_second", frames_per_second(total_frames, total_time)},
        {"milliseconds",
         {{"audio_mean", milliseconds(static_cast<std::int64_t>(total_audio / count))},
          {"setup_mean", milliseconds(static_cast<std::int64_t>(total_setup / count))},
          {"compile_mean", milliseconds(static_cast<std::int64_t>(total_compile / count))},
          {"decode_mean",
           milliseconds(static_cast<std::int64_t>(total_decode.decode_nanoseconds / count))},
          {"decode_conversion_mean",
           milliseconds(
               static_cast<std::int64_t>(total_decode.conversion_nanoseconds / count))},
          {"composition_mean",
           milliseconds(static_cast<std::int64_t>(total_composition / count))},
          {"execute_mean", milliseconds(static_cast<std::int64_t>(total_execute / count))},
          {"linear_to_rgb_mean",
           milliseconds(static_cast<std::int64_t>(
               total_encode_telemetry.linear_to_rgb_nanoseconds / count))},
          {"rgb_to_yuv_mean",
           milliseconds(static_cast<std::int64_t>(
               total_encode_telemetry.rgb_to_yuv_nanoseconds / count))},
          {"encode_conversion_mean",
           milliseconds(static_cast<std::int64_t>(
               (total_encode_telemetry.linear_to_rgb_nanoseconds +
                total_encode_telemetry.rgb_to_yuv_nanoseconds) /
               count))},
          {"codec_mux_mean",
           milliseconds(static_cast<std::int64_t>(
               total_encode_telemetry.codec_mux_nanoseconds / count))},
          {"encode_mean", milliseconds(static_cast<std::int64_t>(total_encode / count))},
          {"total_mean", milliseconds(static_cast<std::int64_t>(total_time / count))},
          {"total_min", milliseconds(totals.front())},
          {"total_median", milliseconds(totals[totals.size() / 2])}}}}},
      {"iterations", std::move(runs)}}
      .dump(2);
}

}  // namespace chroma
