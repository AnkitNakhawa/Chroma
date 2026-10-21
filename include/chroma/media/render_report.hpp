#pragma once
#include <memory>
#include <string>
#include "chroma/media/encode.hpp"

namespace chroma {
// One synchronous job. Measurements unavailable after failure serialize as null.
class VideoRenderReport {
 public:
  explicit VideoRenderReport(std::filesystem::path output, CpuCacheConfig config = {});
  ~VideoRenderReport();
  VideoRenderReport(const VideoRenderReport&) = delete;
  VideoRenderReport& operator=(const VideoRenderReport&) = delete;
  VideoRenderResult render(const Composition& composition,
      CpuExecutionMode mode = CpuExecutionMode::fused, const VideoRenderControl& control = {});
  void diagnostic(const std::string& code, const std::string& message,
                  const std::string& path, const std::string& suggested_action,
                  const std::string& phase = {});
  [[nodiscard]] std::string json() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace chroma
