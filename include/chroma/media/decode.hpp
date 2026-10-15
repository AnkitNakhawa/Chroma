#pragma once

#include <filesystem>
#include <memory>

#include "chroma/core/rational.hpp"
#include "chroma/core/render.hpp"
#include "chroma/media/telemetry.hpp"

namespace chroma {

enum class CpuDecodeConversionMode {
  lookup_table,
  reference,
};

struct DecodedVideoFrame {
  Surface surface;
  Rational presentation_time;
  DecodeTelemetry telemetry;
};

class VideoDecoder {
 public:
  explicit VideoDecoder(
      const std::filesystem::path& path,
      CpuDecodeConversionMode conversion_mode = CpuDecodeConversionMode::lookup_table);
  ~VideoDecoder();

  VideoDecoder(VideoDecoder&&) noexcept;
  VideoDecoder& operator=(VideoDecoder&&) noexcept;
  VideoDecoder(const VideoDecoder&) = delete;
  VideoDecoder& operator=(const VideoDecoder&) = delete;

  [[nodiscard]] DecodedVideoFrame frame_at(Rational time);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] DecodedVideoFrame decode_video_frame(const std::filesystem::path& path,
                                                    Rational time,
                                                    CpuDecodeConversionMode conversion_mode =
                                                        CpuDecodeConversionMode::lookup_table);
[[nodiscard]] Surface decode_image(const std::filesystem::path& path);

}  // namespace chroma
