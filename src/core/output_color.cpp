#include "chroma/core/output_color.hpp"
#include "chroma/core/render.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace chroma {
std::uint8_t linear_to_srgb8_reference(float value) noexcept {
  value = std::clamp(value, 0.0F, 1.0F);
  const auto srgb = value <= 0.0031308F
                        ? value * 12.92F
                        : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
  return static_cast<std::uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
}

namespace {
constexpr unsigned bucket_shift = 14;
constexpr std::uint16_t uncertain = 256;
// Positive float bit patterns are ordered. Each bucket covers 16384 adjacent
// float32 values. Buckets crossing a quantization boundary use the scalar oracle.
// No interpolation or approximate transfer function changes rounded output.
const auto& output_table() noexcept {
  static const auto table = [] {
    std::array<std::uint16_t, (0x3f800000U >> bucket_shift) + 1> result{};
    for (std::uint32_t i = 0; i < result.size(); ++i) {
      const auto low = i << bucket_shift;
      const auto high = std::min(low + (1U << bucket_shift) - 1U, 0x3f800000U);
      const auto a = linear_to_srgb8_reference(std::bit_cast<float>(low));
      const auto b = linear_to_srgb8_reference(std::bit_cast<float>(high));
      result[i] = a == b ? a : uncertain;
    }
    return result;
  }();
  return table;
}
}

namespace {
std::uint8_t convert(float value, const std::uint16_t* table) noexcept {
  if (value <= 0.0F) return 0;
  if (value >= 1.0F) return 255;
  // Preserve the scalar oracle's platform-specific behavior for NaNs as well.
  if (std::isnan(value)) return linear_to_srgb8_reference(value);
  const auto entry = table[std::bit_cast<std::uint32_t>(value) >> bucket_shift];
  return entry == uncertain ? linear_to_srgb8_reference(value) : static_cast<std::uint8_t>(entry);
}

}
std::uint8_t linear_to_srgb8(float value) noexcept {
  return convert(value, output_table().data());
}
void convert_output_rgb8(const Pixel* pixels, std::size_t count, std::uint8_t* rgb) noexcept {
  const auto* table = output_table().data();
  for (std::size_t index = 0; index < count; ++index) {
    rgb[index * 3] = convert(pixels[index].red, table);
    rgb[index * 3 + 1] = convert(pixels[index].green, table);
    rgb[index * 3 + 2] = convert(pixels[index].blue, table);
  }
}
}
