#pragma once
#include <cstdint>
#include <cstddef>

namespace chroma {
struct Pixel;
void convert_output_rgb8(const Pixel* pixels, std::size_t count, std::uint8_t* rgb) noexcept;

// The retained scalar oracle defines the output quantization contract.
[[nodiscard]] std::uint8_t linear_to_srgb8_reference(float value) noexcept;
[[nodiscard]] std::uint8_t linear_to_srgb8(float value) noexcept;
}
