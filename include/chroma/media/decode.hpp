#pragma once

#include <filesystem>
#include <memory>

#include "chroma/core/rational.hpp"
#include "chroma/core/render.hpp"

namespace chroma {

struct DecodedVideoFrame {
  Surface surface;
  Rational presentation_time;
};

class VideoDecoder {
 public:
  explicit VideoDecoder(const std::filesystem::path& path);
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
                                                    Rational time);
[[nodiscard]] Surface decode_image(const std::filesystem::path& path);

}  // namespace chroma
