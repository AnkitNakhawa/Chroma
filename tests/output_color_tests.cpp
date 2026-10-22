#include "chroma/core/output_color.hpp"
#include <bit>
#include <array>
#include "chroma/core/render.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

int main() {
  // Below 2^-14 the linear branch is positive and strictly below half a byte;
  // all such values round to zero. Exhaust every float32 above that bound to 1.
  // This covers every quantization transition and catches libm/compiler changes
  // violating the monotonic bucket assumption on each CI platform.
  std::uint64_t count = 0;
  for (std::uint32_t bits = 0x38800000U; bits <= 0x3f800000U; ++bits) {
    const float value = std::bit_cast<float>(bits);
    const auto expected = chroma::linear_to_srgb8_reference(value);
    if (chroma::linear_to_srgb8(value) != expected) {
      std::cerr << "conversion mismatch at float bits " << bits << '\n';
      return 1;
    }
    ++count;
  }
  for (float value : {0.0F, -0.0F, -1.0F, 2.0F,
      std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::min(),
      std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity()}) {
    if (chroma::linear_to_srgb8(value) != chroma::linear_to_srgb8_reference(value)) return 1;
  }
  std::array<chroma::Pixel, 4096> pixels{};
  std::array<std::uint8_t, 4096 * 3> rgb{};
  std::uint32_t seed = 12345;
  for (auto& pixel : pixels) {
    seed = seed * 1664525U + 1013904223U;
    pixel = {.red = std::bit_cast<float>(seed & 0x7f7fffffU),
             .green = -std::bit_cast<float>(seed & 0x7f7fffffU),
             .blue = static_cast<float>(seed % 65537) / 65536.0F,
             .alpha = std::numeric_limits<float>::quiet_NaN()};
  }
  chroma::convert_output_rgb8(pixels.data(), pixels.size(), rgb.data());
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    if (rgb[i*3] != chroma::linear_to_srgb8_reference(pixels[i].red) ||
        rgb[i*3+1] != chroma::linear_to_srgb8_reference(pixels[i].green) ||
        rgb[i*3+2] != chroma::linear_to_srgb8_reference(pixels[i].blue)) return 1;
  }
  std::cout << count << " float32 values matched the scalar oracle\n";
}
