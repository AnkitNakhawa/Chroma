#include "chroma/core/render.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <memory>
#include <string>
#include <type_traits>
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

float clamp_unit(double value) noexcept {
  if (std::isnan(value) || value <= 0.0) {
    return 0.0F;
  }
  if (value >= 1.0) {
    return 1.0F;
  }
  return static_cast<float>(value);
}

Pixel sample_bilinear(const Surface& surface, double x, double y) noexcept {
  // x/y address pixel centers: (0, 0) is the center of the first pixel.
  if (!std::isfinite(x) || !std::isfinite(y) || x <= -1.0 || y <= -1.0 ||
      x >= static_cast<double>(surface.width()) ||
      y >= static_cast<double>(surface.height())) {
    return {};
  }
  const auto x0 = static_cast<std::int64_t>(std::floor(x));
  const auto y0 = static_cast<std::int64_t>(std::floor(y));
  const auto x_fraction = static_cast<float>(x - static_cast<double>(x0));
  const auto y_fraction = static_cast<float>(y - static_cast<double>(y0));

  const auto read = [&surface](std::int64_t sample_x, std::int64_t sample_y) noexcept {
    if (sample_x < 0 || sample_y < 0 || sample_x >= surface.width() ||
        sample_y >= surface.height()) {
      return Pixel{};
    }
    return surface.pixels()[static_cast<std::size_t>(sample_y) *
                                static_cast<std::size_t>(surface.width()) +
                            static_cast<std::size_t>(sample_x)];
  };

  const auto top_left = read(x0, y0);
  const auto top_right = read(x0 + 1, y0);
  const auto bottom_left = read(x0, y0 + 1);
  const auto bottom_right = read(x0 + 1, y0 + 1);
  const auto interpolate = [x_fraction, y_fraction](float top_left_value,
                                                    float top_right_value,
                                                    float bottom_left_value,
                                                    float bottom_right_value) noexcept {
    const auto top = top_left_value + (top_right_value - top_left_value) * x_fraction;
    const auto bottom =
        bottom_left_value + (bottom_right_value - bottom_left_value) * x_fraction;
    return top + (bottom - top) * y_fraction;
  };
  return Pixel{.red = interpolate(top_left.red, top_right.red, bottom_left.red, bottom_right.red),
               .green = interpolate(top_left.green, top_right.green, bottom_left.green,
                                    bottom_right.green),
               .blue = interpolate(top_left.blue, top_right.blue, bottom_left.blue,
                                   bottom_right.blue),
               .alpha = interpolate(top_left.alpha, top_right.alpha, bottom_left.alpha,
                                    bottom_right.alpha)};
}

Surface apply_transform(Surface source, const Transform2DEffect& transform) {
  Surface output(source.width(), source.height());
  const auto determinant = transform.m00 * transform.m11 - transform.m01 * transform.m10;
  if (std::abs(determinant) <= 1e-12) {
    return output;
  }

  const auto inverse00 = transform.m11 / determinant;
  const auto inverse01 = -transform.m01 / determinant;
  const auto inverse10 = -transform.m10 / determinant;
  const auto inverse11 = transform.m00 / determinant;
  for (std::int32_t y = 0; y < output.height(); ++y) {
    for (std::int32_t x = 0; x < output.width(); ++x) {
      const auto destination_x = static_cast<double>(x) + 0.5;
      const auto destination_y = static_cast<double>(y) + 0.5;
      const auto translated_x = destination_x - transform.m02;
      const auto translated_y = destination_y - transform.m12;
      const auto source_x = inverse00 * translated_x + inverse01 * translated_y - 0.5;
      const auto source_y = inverse10 * translated_x + inverse11 * translated_y - 0.5;
      output.pixel(x, y) = sample_bilinear(source, source_x, source_y);
    }
  }
  return output;
}

void apply_crop(Surface& surface, const CropEffect& crop) noexcept {
  const auto right = static_cast<std::int64_t>(crop.x) + crop.width;
  const auto bottom = static_cast<std::int64_t>(crop.y) + crop.height;
  for (std::int32_t y = 0; y < surface.height(); ++y) {
    for (std::int32_t x = 0; x < surface.width(); ++x) {
      if (static_cast<std::int64_t>(x) < crop.x || static_cast<std::int64_t>(x) >= right ||
          static_cast<std::int64_t>(y) < crop.y || static_cast<std::int64_t>(y) >= bottom) {
        surface.pixel(x, y) = {};
      }
    }
  }
}

void apply_opacity(Surface& surface, const OpacityEffect& opacity) noexcept {
  const auto factor = static_cast<float>(opacity.opacity);
  for (auto& pixel : surface.pixels()) {
    pixel.red *= factor;
    pixel.green *= factor;
    pixel.blue *= factor;
    pixel.alpha *= factor;
  }
}

void apply_color_matrix(Surface& surface, const ColorMatrixEffect& matrix) noexcept {
  for (auto& pixel : surface.pixels()) {
    const auto alpha = static_cast<double>(pixel.alpha);
    const auto reciprocal_alpha = alpha > 0.0 ? 1.0 / alpha : 0.0;
    const double straight[] = {static_cast<double>(pixel.red) * reciprocal_alpha,
                               static_cast<double>(pixel.green) * reciprocal_alpha,
                               static_cast<double>(pixel.blue) * reciprocal_alpha, alpha};
    double transformed[4]{};
    for (std::size_t row = 0; row < 4; ++row) {
      transformed[row] = matrix.values[row * 5 + 4];
      for (std::size_t column = 0; column < 4; ++column) {
        transformed[row] += matrix.values[row * 5 + column] * straight[column];
      }
    }
    const auto transformed_alpha = clamp_unit(transformed[3]);
    pixel = Pixel{.red = clamp_unit(transformed[0]) * transformed_alpha,
                  .green = clamp_unit(transformed[1]) * transformed_alpha,
                  .blue = clamp_unit(transformed[2]) * transformed_alpha,
                  .alpha = transformed_alpha};
  }
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

Pixel composite(Pixel source, Pixel destination, BlendMode mode) noexcept {
  if (mode == BlendMode::source_over) {
    return composite_over(source, destination);
  }
  if (mode == BlendMode::plus) {
    return Pixel{.red = std::min(source.red + destination.red, 1.0F),
                 .green = std::min(source.green + destination.green, 1.0F),
                 .blue = std::min(source.blue + destination.blue, 1.0F),
                 .alpha = std::min(source.alpha + destination.alpha, 1.0F)};
  }

  const auto source_alpha = std::clamp(source.alpha, 0.0F, 1.0F);
  const auto destination_alpha = std::clamp(destination.alpha, 0.0F, 1.0F);
  const auto source_straight = [source_alpha](float channel) noexcept {
    return source_alpha > 0.0F ? channel / source_alpha : 0.0F;
  };
  const auto destination_straight = [destination_alpha](float channel) noexcept {
    return destination_alpha > 0.0F ? channel / destination_alpha : 0.0F;
  };
  if (mode != BlendMode::multiply && mode != BlendMode::screen) {
    return composite_over(source, destination);
  }
  const auto blend_channel = [mode, source_alpha, destination_alpha, &source_straight,
                              &destination_straight](float source_channel,
                                                    float destination_channel) noexcept {
    const auto source_value = source_straight(source_channel);
    const auto destination_value = destination_straight(destination_channel);
    const auto blended = mode == BlendMode::multiply
                             ? source_value * destination_value
                             : source_value + destination_value - source_value * destination_value;
    return source_channel * (1.0F - destination_alpha) +
           destination_channel * (1.0F - source_alpha) +
           source_alpha * destination_alpha * blended;
  };
  return Pixel{.red = blend_channel(source.red, destination.red),
               .green = blend_channel(source.green, destination.green),
               .blue = blend_channel(source.blue, destination.blue),
               .alpha = source_alpha + destination_alpha * (1.0F - source_alpha)};
}

Surface apply_effect_program_cpu(Surface source, const EffectProgram& program) {
  for (const auto& effect : program.effects()) {
    std::visit(
        [&source](const auto& typed) {
          using Type = std::decay_t<decltype(typed)>;
          if constexpr (std::is_same_v<Type, Transform2DEffect>) {
            source = apply_transform(std::move(source), typed);
          } else if constexpr (std::is_same_v<Type, CropEffect>) {
            apply_crop(source, typed);
          } else if constexpr (std::is_same_v<Type, OpacityEffect>) {
            apply_opacity(source, typed);
          } else if constexpr (std::is_same_v<Type, ColorMatrixEffect>) {
            apply_color_matrix(source, typed);
          } else {
            throw std::runtime_error(std::string(effect_name(Effect{typed})) +
                                     " is not implemented by the CPU effect backend");
          }
        },
        effect);
  }
  return source;
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
      if (solid->effects.effects().empty()) {
        for (auto& destination : result.pixels()) {
          destination = composite(source, destination, solid->blend_mode);
        }
      } else {
        auto effected = apply_effect_program_cpu(
            Surface(result.width(), result.height(), source), solid->effects);
        for (std::size_t index = 0; index < result.size(); ++index) {
          result.pixels()[index] =
              composite(effected.pixels()[index], result.pixels()[index], solid->blend_mode);
        }
      }
      continue;
    }

    const auto& video = std::get<CompositeVideoFrameOperation>(operation);
    const auto key = video.source_id + '\n' + video.asset_path.generic_string();
    auto& decoder = impl_->decoders[key];
    if (!decoder) {
      decoder = std::make_unique<VideoDecoder>(video.asset_path);
    }
    auto decoded = decoder->frame_at(video.source_time);
    if (decoded.surface.width() != result.width() ||
        decoded.surface.height() != result.height()) {
      throw std::runtime_error("video layer dimensions must match the composition canvas");
    }
    auto source_surface =
        apply_effect_program_cpu(std::move(decoded.surface), video.effects);
    for (std::size_t index = 0; index < result.size(); ++index) {
      result.pixels()[index] =
          composite(source_surface.pixels()[index], result.pixels()[index], video.blend_mode);
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
