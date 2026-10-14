#include "chroma/core/render.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

#include "chroma/media/decode.hpp"

namespace chroma {
namespace {

std::size_t checked_pixel_count(std::int32_t width, std::int32_t height) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("surface dimensions must be positive");
  }
  const auto unsigned_width = static_cast<std::uint64_t>(width);
  const auto unsigned_height = static_cast<std::uint64_t>(height);
  const auto count = unsigned_width * unsigned_height;
  if (count > max_canvas_pixels) {
    throw std::length_error("surface exceeds the configured pixel budget");
  }
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

Pixel premultiply(const Color& color) noexcept {
  const auto alpha = static_cast<float>(color.alpha);
  return Pixel{.red = static_cast<float>(color.red) * alpha,
               .green = static_cast<float>(color.green) * alpha,
               .blue = static_cast<float>(color.blue) * alpha,
               .alpha = alpha};
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

struct CpuRenderContext::Impl {
  std::unordered_map<std::string, std::unique_ptr<VideoDecoder>> decoders;
};

CpuRenderContext::CpuRenderContext() : impl_(std::make_unique<Impl>()) {}
CpuRenderContext::~CpuRenderContext() = default;
CpuRenderContext::CpuRenderContext(CpuRenderContext&&) noexcept = default;
CpuRenderContext& CpuRenderContext::operator=(CpuRenderContext&&) noexcept = default;

Surface CpuRenderContext::execute(const ExecutionPlan& plan) {
  Surface result(plan.width(), plan.height());
  for (const auto& operation : plan.operations()) {
    if (const auto* clear = std::get_if<ClearOperation>(&operation)) {
      std::fill(result.pixels().begin(), result.pixels().end(), premultiply(clear->color));
      continue;
    }
    if (const auto* solid = std::get_if<CompositeSolidOperation>(&operation)) {
      const auto source = premultiply(solid->color);
      for (auto& destination : result.pixels()) {
        destination = composite_over(source, destination);
      }
      continue;
    }

    const auto& video = std::get<CompositeVideoFrameOperation>(operation);
    const auto key = video.asset_path.generic_string();
    auto& decoder = impl_->decoders[key];
    if (!decoder) {
      decoder = std::make_unique<VideoDecoder>(video.asset_path);
    }
    const auto decoded = decoder->frame_at(video.source_time);
    if (decoded.surface.width() != result.width() ||
        decoded.surface.height() != result.height()) {
      throw std::runtime_error("video layer dimensions must match the composition canvas");
    }
    const auto opacity = static_cast<float>(video.opacity);
    for (std::size_t index = 0; index < result.size(); ++index) {
      auto source = decoded.surface.pixels()[index];
      source.red *= opacity;
      source.green *= opacity;
      source.blue *= opacity;
      source.alpha *= opacity;
      result.pixels()[index] = composite_over(source, result.pixels()[index]);
    }
  }
  return result;
}

Surface execute_cpu(const ExecutionPlan& plan) {
  CpuRenderContext context;
  return context.execute(plan);
}

Surface render_frame_cpu(const Composition& composition, FrameIndex frame) {
  return execute_cpu(compile_frame(composition, frame));
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
