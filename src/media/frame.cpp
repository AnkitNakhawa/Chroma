#include "chroma/media/frame.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace chroma {
namespace {

std::size_t expected_plane_count(FramePixelFormat format) noexcept {
  switch (format) {
    case FramePixelFormat::linear_rgba_f32:
    case FramePixelFormat::rgba8_srgb:
    case FramePixelFormat::rgb24_srgb:
      return 1;
    case FramePixelFormat::yuv420p8:
      return 3;
    case FramePixelFormat::nv12_8:
      return 2;
  }
  return 0;
}

}  // namespace

ResidentFrame::ResidentFrame(ResidentFrameDescriptor descriptor,
                             std::vector<ResidentFramePlane> planes,
                             std::shared_ptr<const void> lifetime)
    : descriptor_(descriptor), planes_(std::move(planes)), lifetime_(std::move(lifetime)) {
  if (descriptor_.width <= 0 || descriptor_.height <= 0) {
    throw std::invalid_argument("resident frame dimensions must be positive");
  }
  if (!lifetime_) {
    throw std::invalid_argument("resident frame requires an owning lifetime token");
  }
  if (planes_.size() != expected_plane_count(descriptor_.pixel_format)) {
    throw std::invalid_argument("resident frame plane count does not match its pixel format");
  }
  for (const auto& plane : planes_) {
    if (plane.address == 0 || plane.byte_size == 0 || plane.row_bytes == 0 ||
        plane.row_stride_bytes == 0 || plane.row_count == 0 ||
        plane.row_stride_bytes == std::numeric_limits<std::int64_t>::min()) {
      throw std::invalid_argument("resident frame planes must describe non-empty storage");
    }
    const auto stride = static_cast<std::uint64_t>(
        plane.row_stride_bytes < 0 ? -plane.row_stride_bytes : plane.row_stride_bytes);
    if (plane.row_bytes > stride) {
      throw std::invalid_argument("resident frame active row bytes exceed its stride");
    }
    const auto preceding_rows = static_cast<std::uint64_t>(plane.row_count - 1U);
    if (preceding_rows > (std::numeric_limits<std::uint64_t>::max() - plane.row_bytes) / stride ||
        preceding_rows * stride + plane.row_bytes > plane.byte_size) {
      throw std::invalid_argument("resident frame plane storage is smaller than its layout");
    }
    if (plane.byte_size > std::numeric_limits<std::uint64_t>::max() - total_bytes_) {
      throw std::overflow_error("resident frame byte size overflows");
    }
    total_bytes_ += plane.byte_size;
  }
}

std::string_view frame_memory_domain_name(FrameMemoryDomain domain) noexcept {
  switch (domain) {
    case FrameMemoryDomain::host:
      return "host";
    case FrameMemoryDomain::accelerator:
      return "accelerator";
  }
  return "unknown";
}

std::string_view frame_pixel_format_name(FramePixelFormat format) noexcept {
  switch (format) {
    case FramePixelFormat::linear_rgba_f32:
      return "linear_rgba_f32";
    case FramePixelFormat::rgba8_srgb:
      return "rgba8_srgb";
    case FramePixelFormat::rgb24_srgb:
      return "rgb24_srgb";
    case FramePixelFormat::yuv420p8:
      return "yuv420p8";
    case FramePixelFormat::nv12_8:
      return "nv12_8";
  }
  return "unknown";
}

}  // namespace chroma
