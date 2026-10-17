#include "chroma/media/audio.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef CHROMA_HAS_FFMPEG
#define CHROMA_HAS_FFMPEG 0
#endif

#if CHROMA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}
#endif

namespace chroma {

std::int64_t audio_sample_at(const FrameRate& rate, FrameIndex frame) {
  const auto time = rate.time_at(frame).scaled_by(audio_sample_rate);
  const auto whole = time.numerator() / time.denominator();
  const auto remainder = time.numerator() % time.denominator();
  return whole + (remainder >= time.denominator() / 2 + time.denominator() % 2 ? 1 : 0);
}

#if CHROMA_HAS_FFMPEG
namespace {
void require_audio(int status, const char* operation) {
  if (status < 0) {
    char error[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(status, error, sizeof(error));
    throw std::runtime_error(std::string(operation) + ": " + error);
  }
}

// Decode from the beginning so compressed-source priming and resampler delay have
// the same semantics regardless of trim position. Only one decoded block is held.
class AudioSource {
 public:
  explicit AudioSource(const std::filesystem::path& path) {
    try {
      require_audio(avformat_open_input(&format_, path.string().c_str(), nullptr, nullptr), "open audio input");
      require_audio(avformat_find_stream_info(format_, nullptr), "inspect audio input");
      const AVCodec* decoder = nullptr;
      stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
      require_audio(stream_, "find audio stream");
      codec_ = avcodec_alloc_context3(decoder);
      if (!codec_) throw std::bad_alloc();
      require_audio(avcodec_parameters_to_context(codec_, format_->streams[stream_]->codecpar), "configure audio decoder");
      require_audio(avcodec_open2(codec_, decoder, nullptr), "open audio decoder");
      if (codec_->sample_rate <= 0 || codec_->ch_layout.nb_channels <= 0)
        throw std::runtime_error("audio input has invalid sample rate or channel layout");
      input_rate_ = codec_->sample_rate;
      input_format_ = codec_->sample_fmt;
      require_audio(av_channel_layout_copy(&input_layout_, &codec_->ch_layout), "copy input channel layout");
      AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
      require_audio(swr_alloc_set_opts2(&resampler_, &stereo, AV_SAMPLE_FMT_FLT, audio_sample_rate,
                                      &codec_->ch_layout, codec_->sample_fmt, codec_->sample_rate, 0, nullptr), "configure audio resampler");
      require_audio(swr_init(resampler_), "initialize audio resampler");
      packet_ = av_packet_alloc();
      frame_ = av_frame_alloc();
      if (!packet_ || !frame_) throw std::bad_alloc();
    } catch (...) { release(); throw; }
  }
  ~AudioSource() { release(); }
  AudioSource(const AudioSource&) = delete;
  AudioSource& operator=(const AudioSource&) = delete;

  void skip_to(std::int64_t target) {
    if (target < position_) throw std::logic_error("audio source cannot move backwards");
    while (position_ < target) {
      ensure();
      const auto available = static_cast<std::int64_t>((buffer_.size() - offset_) / 2);
      const auto count = std::min(target - position_, available);
      offset_ += static_cast<std::size_t>(count) * 2;
      position_ += count;
    }
  }
  void mix(double* destination, int count, double gain) {
    for (int sample = 0; sample < count; ++sample) {
      ensure();
      for (int channel = 0; channel < 2; ++channel)
        destination[sample * 2 + channel] += static_cast<double>(buffer_[offset_++]) * gain;
      ++position_;
    }
  }

 private:
  void release() noexcept {
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    swr_free(&resampler_);
    av_channel_layout_uninit(&input_layout_);
    avcodec_free_context(&codec_);
    avformat_close_input(&format_);
  }
  bool convert(const AVFrame* frame) {
    const auto input_count = frame ? frame->nb_samples : 0;
    const auto capacity = swr_get_out_samples(resampler_, input_count);
    require_audio(capacity, "size resampled audio");
    buffer_.resize(static_cast<std::size_t>(std::max(capacity, 1)) * 2);
    std::uint8_t* output[] = {reinterpret_cast<std::uint8_t*>(buffer_.data())};
    const auto count = swr_convert(resampler_, output, capacity,
                                  frame ? const_cast<const std::uint8_t**>(frame->extended_data) : nullptr,
                                  input_count);
    require_audio(count, "resample audio");
    buffer_.resize(static_cast<std::size_t>(count) * 2);
    offset_ = 0;
    return count != 0;
  }
  void ensure() {
    while (offset_ >= buffer_.size()) {
      if (decoded_eof_) {
        if (convert(nullptr)) return;
        throw std::runtime_error("audio trim extends beyond available source samples");
      }
      const auto status = avcodec_receive_frame(codec_, frame_);
      if (status == AVERROR_EOF) { decoded_eof_ = true; continue; }
      if (status != AVERROR(EAGAIN)) {
        require_audio(status, "decode audio frame");
        // Midstream format changes need an explicit new resampling contract.
        if (frame_->sample_rate != input_rate_ || frame_->format != input_format_ ||
            av_channel_layout_compare(&frame_->ch_layout, &input_layout_) != 0)
          throw std::runtime_error("audio format changed within the stream");
        if (frame_->best_effort_timestamp != AV_NOPTS_VALUE) {
          const auto base = format_->streams[stream_]->time_base;
          const auto timestamp = av_rescale_q(frame_->best_effort_timestamp, base, AVRational{1, input_rate_});
          if (!have_timestamp_) {
            timestamp_origin_ = timestamp - input_samples_;
            have_timestamp_ = true;
          }
          const auto tolerance = av_rescale_q_rnd(1, base, AVRational{1, input_rate_}, AV_ROUND_UP) + 1;
          if (static_cast<__int128_t>(timestamp) - timestamp_origin_ - input_samples_ > tolerance ||
              static_cast<__int128_t>(timestamp) - timestamp_origin_ - input_samples_ < -tolerance)
            throw std::runtime_error("discontinuous audio timestamps are not supported");
        }
        input_samples_ += frame_->nb_samples;
        const bool ready = convert(frame_);
        av_frame_unref(frame_);
        if (ready) return;
        continue;
      }
      if (sent_flush_) throw std::runtime_error("audio decoder stalled after flush");
      int read_status;
      do {
        av_packet_unref(packet_);
        read_status = av_read_frame(format_, packet_);
      } while (read_status >= 0 && packet_->stream_index != stream_);
      if (read_status == AVERROR_EOF) {
        require_audio(avcodec_send_packet(codec_, nullptr), "flush audio decoder");
        sent_flush_ = true;
      } else {
        require_audio(read_status, "read audio packet");
        require_audio(avcodec_send_packet(codec_, packet_), "submit audio packet");
        av_packet_unref(packet_);
      }
    }
  }
  AVFormatContext* format_ = nullptr;
  AVCodecContext* codec_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVFrame* frame_ = nullptr;
  SwrContext* resampler_ = nullptr;
  int stream_ = -1;
  int input_rate_ = 0;
  AVSampleFormat input_format_ = AV_SAMPLE_FMT_NONE;
  AVChannelLayout input_layout_{};
  bool have_timestamp_ = false;
  std::int64_t timestamp_origin_ = 0;
  std::int64_t input_samples_ = 0;
  bool sent_flush_ = false;
  bool decoded_eof_ = false;
  std::vector<float> buffer_;
  std::size_t offset_ = 0;
  std::int64_t position_ = 0;
};
}  // namespace
#endif

struct AudioMixer::Impl {
  std::int64_t count = 0;
  std::int64_t cursor = 0;
#if CHROMA_HAS_FFMPEG
  struct Track {
    std::filesystem::path path;
    std::int64_t start;
    std::int64_t end;
    std::int64_t source;
    double gain;
    std::unique_ptr<AudioSource> decoder;
  };
  std::vector<Track> tracks;
#endif
};

AudioMixer::AudioMixer(const Composition& composition) : impl_(std::make_unique<Impl>()) {
#if CHROMA_HAS_FFMPEG
  const auto& rate = composition.frame_rate();
  const auto origin = audio_sample_at(rate, composition.range().start());
  impl_->count = audio_sample_at(rate, composition.range().end()) - origin;
  for (const auto& track : composition.audio_tracks()) {
    const auto length = audio_sample_at(rate, track.range.end()) - audio_sample_at(rate, track.range.start());
    if (audio_sample_at(rate, track.source_start) > std::numeric_limits<std::int64_t>::max() - length)
      throw std::overflow_error("audio source trim sample range overflow");
    impl_->tracks.push_back({composition.find_audio_asset(track.asset_id)->path,
      audio_sample_at(rate, track.range.start()) - origin,
      audio_sample_at(rate, track.range.end()) - origin,
      audio_sample_at(rate, track.source_start), track.gain, nullptr});
  }
#else
  static_cast<void>(composition);
  throw std::runtime_error("Chroma was built without FFmpeg audio support");
#endif
}
AudioMixer::~AudioMixer() = default;
std::int64_t AudioMixer::sample_count() const noexcept { return impl_->count; }
std::int64_t AudioMixer::position() const noexcept { return impl_->cursor; }

std::vector<float> AudioMixer::read(int samples) {
  if (samples < 0 || samples > 65536 || samples > impl_->count - impl_->cursor)
    throw std::invalid_argument("audio read must be within the timeline and at most 65536 samples");
  std::vector<double> sum(static_cast<std::size_t>(samples) * 2, 0.0);
#if CHROMA_HAS_FFMPEG
  const auto end = impl_->cursor + samples;
  for (auto& track : impl_->tracks) {
    const auto begin = std::max(impl_->cursor, track.start);
    const auto finish = std::min(end, track.end);
    if (finish <= begin) continue;
    if (!track.decoder) track.decoder = std::make_unique<AudioSource>(track.path);
    track.decoder->skip_to(track.source + begin - track.start);
    track.decoder->mix(sum.data() + static_cast<std::size_t>(begin - impl_->cursor) * 2,
                       static_cast<int>(finish - begin), track.gain);
    if (finish == track.end) track.decoder.reset();
  }
#endif
  std::vector<float> output(sum.size());
  std::transform(sum.begin(), sum.end(), output.begin(), [](double value) {
    if (!std::isfinite(value)) throw std::runtime_error("audio input contains nonfinite samples");
    return static_cast<float>(std::clamp(value, -1.0, 1.0));
  });
  impl_->cursor += samples;
  return output;
}

}  // namespace chroma
