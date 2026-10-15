#pragma once

#include <cstdint>

namespace chroma {

struct DecodeTelemetry {
  std::uint64_t frame_requests{0};
  std::uint64_t frames_decoded{0};
  std::uint64_t frames_converted{0};
  std::uint64_t native_bytes_estimated{0};
  std::uint64_t rgba8_bytes_written{0};
  std::uint64_t linear_rgba_bytes_written{0};
  std::int64_t decode_nanoseconds{0};
  std::int64_t conversion_nanoseconds{0};

  DecodeTelemetry& operator+=(const DecodeTelemetry& other) noexcept {
    frame_requests += other.frame_requests;
    frames_decoded += other.frames_decoded;
    frames_converted += other.frames_converted;
    native_bytes_estimated += other.native_bytes_estimated;
    rgba8_bytes_written += other.rgba8_bytes_written;
    linear_rgba_bytes_written += other.linear_rgba_bytes_written;
    decode_nanoseconds += other.decode_nanoseconds;
    conversion_nanoseconds += other.conversion_nanoseconds;
    return *this;
  }

  friend bool operator==(const DecodeTelemetry&, const DecodeTelemetry&) = default;
};

struct EncodeTelemetry {
  std::uint64_t frames_converted{0};
  std::uint64_t linear_rgba_bytes_read{0};
  std::uint64_t rgb24_bytes_written{0};
  std::uint64_t yuv420_bytes_written{0};
  std::int64_t linear_to_rgb_nanoseconds{0};
  std::int64_t rgb_to_yuv_nanoseconds{0};
  std::int64_t codec_mux_nanoseconds{0};

  EncodeTelemetry& operator+=(const EncodeTelemetry& other) noexcept {
    frames_converted += other.frames_converted;
    linear_rgba_bytes_read += other.linear_rgba_bytes_read;
    rgb24_bytes_written += other.rgb24_bytes_written;
    yuv420_bytes_written += other.yuv420_bytes_written;
    linear_to_rgb_nanoseconds += other.linear_to_rgb_nanoseconds;
    rgb_to_yuv_nanoseconds += other.rgb_to_yuv_nanoseconds;
    codec_mux_nanoseconds += other.codec_mux_nanoseconds;
    return *this;
  }

  friend bool operator==(const EncodeTelemetry&, const EncodeTelemetry&) = default;
};

}  // namespace chroma
