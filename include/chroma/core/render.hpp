#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

#include "chroma/core/composition.hpp"

namespace chroma {

struct Pixel {
  float red;
  float green;
  float blue;
  float alpha;

  friend bool operator==(const Pixel&, const Pixel&) = default;
};

class Surface {
 public:
  Surface(std::int32_t width, std::int32_t height, Pixel fill = {});

  [[nodiscard]] std::int32_t width() const noexcept { return width_; }
  [[nodiscard]] std::int32_t height() const noexcept { return height_; }
  [[nodiscard]] std::size_t size() const noexcept { return pixels_.size(); }
  [[nodiscard]] const Pixel& pixel(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] Pixel& pixel(std::int32_t x, std::int32_t y);
  [[nodiscard]] const std::vector<Pixel>& pixels() const noexcept { return pixels_; }

 private:
  [[nodiscard]] std::size_t offset(std::int32_t x, std::int32_t y) const;

  std::int32_t width_;
  std::int32_t height_;
  std::vector<Pixel> pixels_;
};

[[nodiscard]] Pixel composite_over(Pixel source, Pixel destination) noexcept;
[[nodiscard]] Surface render_frame_cpu(const Composition& composition, FrameIndex frame);
void write_ppm(const Surface& surface, std::ostream& output);

}  // namespace chroma
