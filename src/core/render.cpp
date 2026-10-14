#include "chroma/core/render.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace chroma {
namespace {

std::size_t checked_pixel_count(std::int32_t width, std::int32_t height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("surface dimensions must be positive");
  }
  const auto unsigned_width = static_cast<std::uint64_t>(width);
  const auto unsigned_height = static_cast<std::uint64_t>(height);
  const auto count = unsigned_width * unsigned_height;
  if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() / sizeof(Pixel))) {
    throw std::overflow_error("surface dimensions exceed addressable memory");
  }
  return static_cast<std::size_t>(count);
}

float linear_to_srgb(float value) noexcept {
  value = std::clamp(value, 0.0F, 1.0F);
  if (value <= 0.0031308F) {
    return value * 12.92F;
  }
  return 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

unsigned char to_byte(float value) noexcept {
  return static_cast<unsigned char>(std::lround(linear_to_srgb(value) * 255.0F));
}

}  // namespace

Surface::Surface(std::int32_t width, std::int32_t height, Pixel fill)
    : width_(width), height_(height), pixels_(checked_pixel_count(width, height), fill) {}

const Pixel& Surface::pixel(std::int32_t x, std::int32_t y) const {
  return pixels_.at(offset(x, y));
}

Pixel& Surface::pixel(std::int32_t x, std::int32_t y) {
  return pixels_.at(offset(x, y));
}

std::size_t Surface::offset(std::int32_t x, std::int32_t y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    throw std::out_of_range("pixel coordinate is outside the surface");
  }
  return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
         static_cast<std::size_t>(x);
}

Pixel composite_over(Pixel source, Pixel destination) noexcept {
  const auto inverse_source_alpha = 1.0F - source.alpha;
  return Pixel{.red = source.red + destination.red * inverse_source_alpha,
               .green = source.green + destination.green * inverse_source_alpha,
               .blue = source.blue + destination.blue * inverse_source_alpha,
               .alpha = source.alpha + destination.alpha * inverse_source_alpha};
}

Surface render_frame_cpu(const Composition& composition, FrameIndex frame) {
  if (!composition.range().contains(frame)) {
    throw std::out_of_range("requested frame is outside the composition");
  }

  Pixel result{};
  for (const auto& layer : composition.layers()) {
    if (!layer.range.contains(frame)) {
      continue;
    }
    const auto alpha = static_cast<float>(layer.color.alpha);
    const Pixel source{.red = static_cast<float>(layer.color.red) * alpha,
                       .green = static_cast<float>(layer.color.green) * alpha,
                       .blue = static_cast<float>(layer.color.blue) * alpha,
                       .alpha = alpha};
    result = composite_over(source, result);
  }
  return Surface(composition.width(), composition.height(), result);
}

void write_ppm(const Surface& surface, std::ostream& output) {
  output << "P6\n" << surface.width() << ' ' << surface.height() << "\n255\n";
  for (const auto& pixel : surface.pixels()) {
    const char bytes[] = {static_cast<char>(to_byte(pixel.red)),
                          static_cast<char>(to_byte(pixel.green)),
                          static_cast<char>(to_byte(pixel.blue))};
    output.write(bytes, 3);
  }
  if (!output) {
    throw std::runtime_error("failed to write PPM image");
  }
}

}  // namespace chroma
