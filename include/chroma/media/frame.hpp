#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace chroma {

enum class FrameMemoryDomain {
  host,
  accelerator,
};

enum class FramePixelFormat {
  linear_rgba_f32,
  rgba8_srgb,
  rgb24_srgb,
  yuv420p8,
  nv12_8,
};

enum class FrameColorRange {
  unknown,
  full,
  limited,
};

enum class FrameColorTransfer {
  unknown,
  linear,
  srgb,
  bt709,
};

enum class FrameColorMatrix {
  unknown,
  identity,
  bt601,
  bt709,
  bt2020_ncl,
};

struct FrameColorDescription {
  FrameColorRange range{FrameColorRange::unknown};
  FrameColorTransfer transfer{FrameColorTransfer::unknown};
  FrameColorMatrix matrix{FrameColorMatrix::unknown};

  friend bool operator==(const FrameColorDescription&, const FrameColorDescription&) = default;
};

struct ResidentFrameDescriptor {
  std::int32_t width;
  std::int32_t height;
  FrameMemoryDomain memory_domain;
  FramePixelFormat pixel_format;
  FrameColorDescription color;

  friend bool operator==(const ResidentFrameDescriptor&, const ResidentFrameDescriptor&) =
      default;
};

struct ResidentFramePlane {
  std::uintptr_t address;
  std::uint64_t byte_size;
  std::int64_t row_stride_bytes;
  std::uint32_t row_count;

  friend bool operator==(const ResidentFramePlane&, const ResidentFramePlane&) = default;
};

class ResidentFrame {
 public:
  ResidentFrame(ResidentFrameDescriptor descriptor, std::vector<ResidentFramePlane> planes,
                std::shared_ptr<const void> lifetime);

  [[nodiscard]] const ResidentFrameDescriptor& descriptor() const noexcept {
    return descriptor_;
  }
  [[nodiscard]] const std::vector<ResidentFramePlane>& planes() const noexcept {
    return planes_;
  }
  [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_bytes_; }
  [[nodiscard]] long lifetime_use_count() const noexcept { return lifetime_.use_count(); }

 private:
  ResidentFrameDescriptor descriptor_;
  std::vector<ResidentFramePlane> planes_;
  std::shared_ptr<const void> lifetime_;
  std::uint64_t total_bytes_{0};
};

[[nodiscard]] std::string_view frame_memory_domain_name(FrameMemoryDomain domain) noexcept;
[[nodiscard]] std::string_view frame_pixel_format_name(FramePixelFormat format) noexcept;

}  // namespace chroma
