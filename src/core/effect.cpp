#include "chroma/core/effect.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace chroma {
namespace {

constexpr double identity_tolerance = 1e-12;

bool approximately(double left, double right) noexcept {
  return std::abs(left - right) <= identity_tolerance;
}

bool finite_transform(const Transform2DEffect& effect) noexcept {
  const double values[] = {effect.m00, effect.m01, effect.m02,
                           effect.m10, effect.m11, effect.m12};
  for (const auto value : values) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

bool is_identity(const Transform2DEffect& effect) noexcept {
  return approximately(effect.m00, 1.0) && approximately(effect.m01, 0.0) &&
         approximately(effect.m02, 0.0) && approximately(effect.m10, 0.0) &&
         approximately(effect.m11, 1.0) && approximately(effect.m12, 0.0);
}

bool is_identity(const ColorMatrixEffect& effect) noexcept {
  const auto identity = identity_color_matrix();
  for (std::size_t index = 0; index < effect.values.size(); ++index) {
    if (!approximately(effect.values[index], identity.values[index])) {
      return false;
    }
  }
  return true;
}

Transform2DEffect compose(const Transform2DEffect& first,
                          const Transform2DEffect& second) noexcept {
  // Applying first and then second is second * first.
  return Transform2DEffect{
      .m00 = second.m00 * first.m00 + second.m01 * first.m10,
      .m01 = second.m00 * first.m01 + second.m01 * first.m11,
      .m02 = second.m00 * first.m02 + second.m01 * first.m12 + second.m02,
      .m10 = second.m10 * first.m00 + second.m11 * first.m10,
      .m11 = second.m10 * first.m01 + second.m11 * first.m11,
      .m12 = second.m10 * first.m02 + second.m11 * first.m12 + second.m12};
}

ColorMatrixEffect compose(const ColorMatrixEffect& first,
                          const ColorMatrixEffect& second) noexcept {
  ColorMatrixEffect result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      double value = 0.0;
      for (std::size_t inner = 0; inner < 4; ++inner) {
        value += second.values[row * 5 + inner] * first.values[inner * 5 + column];
      }
      result.values[row * 5 + column] = value;
    }
    double bias = second.values[row * 5 + 4];
    for (std::size_t inner = 0; inner < 4; ++inner) {
      bias += second.values[row * 5 + inner] * first.values[inner * 5 + 4];
    }
    result.values[row * 5 + 4] = bias;
  }
  return result;
}

void validate_effect(const Effect& effect) {
  std::visit(
      [](const auto& typed) {
        using Type = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Type, Transform2DEffect>) {
          if (!finite_transform(typed)) {
            throw std::invalid_argument("transform effect values must be finite");
          }
        } else if constexpr (std::is_same_v<Type, CropEffect>) {
          if (typed.width <= 0 || typed.height <= 0) {
            throw std::invalid_argument("crop effect dimensions must be positive");
          }
        } else if constexpr (std::is_same_v<Type, OpacityEffect>) {
          if (!std::isfinite(typed.opacity) || typed.opacity < 0.0 || typed.opacity > 1.0) {
            throw std::invalid_argument("opacity effect must be between zero and one");
          }
        } else if constexpr (std::is_same_v<Type, AnimatedOpacityEffect>) {
          if (typed.keyframes.empty() || typed.keyframes.size() > 1024) {
            throw std::invalid_argument("animated opacity must contain 1 to 1024 keyframes");
          }
          std::int64_t previous_frame = -1;
          for (const auto& keyframe : typed.keyframes) {
            if (keyframe.frame < 0 || keyframe.frame <= previous_frame) {
              throw std::invalid_argument(
                  "animated opacity keyframe frames must be nonnegative and strictly increasing");
            }
            if (!std::isfinite(keyframe.value) || keyframe.value < 0.0 ||
                keyframe.value > 1.0 || easing_curve_name(keyframe.easing) == "unknown") {
              throw std::invalid_argument("animated opacity keyframe is invalid");
            }
            previous_frame = keyframe.frame;
          }
        } else if constexpr (std::is_same_v<Type, MaskEffect>) {
          if (mask_shape_name(typed.shape) == "unknown" || !std::isfinite(typed.x) ||
              !std::isfinite(typed.y) || !std::isfinite(typed.width) ||
              !std::isfinite(typed.height) || typed.width <= 0.0 || typed.height <= 0.0) {
            throw std::invalid_argument("mask effect geometry is invalid");
          }
        } else if constexpr (std::is_same_v<Type, ColorMatrixEffect>) {
          for (const auto value : typed.values) {
            if (!std::isfinite(value)) {
              throw std::invalid_argument("color matrix effect values must be finite");
            }
          }
        } else if constexpr (std::is_same_v<Type, GaussianBlurEffect>) {
          if (!std::isfinite(typed.radius) || typed.radius < 0.0 || typed.radius > 4096.0) {
            throw std::invalid_argument("blur radius is outside the supported range");
          }
        } else if constexpr (std::is_same_v<Type, TemporalSampleEffect>) {
          if (typed.radius <= 0 || typed.radius > 1024) {
            throw std::invalid_argument("temporal radius is outside the supported range");
          }
        } else if constexpr (std::is_same_v<Type, ExternalEffect>) {
          if (typed.implementation.empty() || typed.implementation.size() > 128) {
            throw std::invalid_argument("external effect implementation id is invalid");
          }
        }
      },
      effect);
}

nlohmann::json effect_json(const Effect& effect) {
  auto output = nlohmann::json{{"op", effect_name(effect)},
                               {"domain", effect_domain_name(effect_traits(effect).domain)}};
  std::visit(
      [&output](const auto& typed) {
        using Type = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Type, Transform2DEffect>) {
          output["matrix"] = {typed.m00, typed.m01, typed.m02,
                              typed.m10, typed.m11, typed.m12};
        } else if constexpr (std::is_same_v<Type, CropEffect>) {
          output["rect"] = {{"x", typed.x}, {"y", typed.y},
                            {"width", typed.width}, {"height", typed.height}};
        } else if constexpr (std::is_same_v<Type, OpacityEffect>) {
          output["opacity"] = typed.opacity;
        } else if constexpr (std::is_same_v<Type, AnimatedOpacityEffect>) {
          output["keyframes"] = nlohmann::json::array();
          for (const auto& keyframe : typed.keyframes) {
            output["keyframes"].push_back({{"frame", keyframe.frame},
                                            {"value", keyframe.value},
                                            {"easing", easing_curve_name(keyframe.easing)}});
          }
        } else if constexpr (std::is_same_v<Type, MaskEffect>) {
          output["shape"] = mask_shape_name(typed.shape);
          output["bounds"] = {typed.x, typed.y, typed.width, typed.height};
          output["inverted"] = typed.inverted;
        } else if constexpr (std::is_same_v<Type, ColorMatrixEffect>) {
          output["matrix"] = typed.values;
        } else if constexpr (std::is_same_v<Type, GaussianBlurEffect>) {
          output["radius"] = typed.radius;
        } else if constexpr (std::is_same_v<Type, TemporalSampleEffect>) {
          output["radius"] = typed.radius;
        } else if constexpr (std::is_same_v<Type, ExternalEffect>) {
          output["implementation"] = typed.implementation;
        }
      },
      effect);
  return output;
}

}  // namespace

EffectProgram::EffectProgram(std::vector<Effect> effects) : effects_(std::move(effects)) {
  for (const auto& effect : effects_) {
    validate_effect(effect);
  }
}

OptimizedEffectProgram::OptimizedEffectProgram(EffectProgram program,
                                               std::vector<FusionGroup> fusion_groups,
                                               std::size_t rewrite_count)
    : program_(std::move(program)),
      fusion_groups_(std::move(fusion_groups)),
      rewrite_count_(rewrite_count) {
  std::size_t expected_begin = 0;
  for (const auto& group : fusion_groups_) {
    if (group.begin != expected_begin || group.end <= group.begin ||
        group.end > program_.effects().size()) {
      throw std::invalid_argument("fusion groups must form a contiguous effect partition");
    }
    if (!group.gpu_fusible && group.end != group.begin + 1) {
      throw std::invalid_argument("non-fusible effects must occupy individual fusion groups");
    }
    for (auto index = group.begin; index < group.end; ++index) {
      if (effect_traits(program_.effects()[index]).gpu_fusible != group.gpu_fusible) {
        throw std::invalid_argument("fusion group classification does not match effect traits");
      }
    }
    expected_begin = group.end;
  }
  if (expected_begin != program_.effects().size()) {
    throw std::invalid_argument("fusion groups must cover the optimized effect program");
  }
}

EffectTraits effect_traits(const Effect& effect) {
  return std::visit(
      [](const auto& typed) -> EffectTraits {
        using Type = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Type, Transform2DEffect> ||
                      std::is_same_v<Type, CropEffect>) {
          return {.domain = EffectDomain::coordinate,
                  .spatial_radius = 0,
                  .temporal_radius = 0,
                  .gpu_fusible = true,
                  .deterministic = true};
        } else if constexpr (std::is_same_v<Type, OpacityEffect> ||
                             std::is_same_v<Type, MaskEffect> ||
                             std::is_same_v<Type, ColorMatrixEffect>) {
          return {.domain = EffectDomain::pointwise,
                  .spatial_radius = 0,
                  .temporal_radius = 0,
                  .gpu_fusible = true,
                  .deterministic = true};
        } else if constexpr (std::is_same_v<Type, AnimatedOpacityEffect>) {
          // A local frame is required before a backend can execute this node.
          // Per-frame compilation lowers it to a normal fusible opacity effect.
          return {.domain = EffectDomain::pointwise,
                  .spatial_radius = 0,
                  .temporal_radius = 0,
                  .gpu_fusible = false,
                  .deterministic = true};
        } else if constexpr (std::is_same_v<Type, GaussianBlurEffect>) {
          return {.domain = EffectDomain::neighborhood,
                  .spatial_radius = static_cast<std::int32_t>(std::ceil(typed.radius * 3.0)),
                  .temporal_radius = 0,
                  .gpu_fusible = false,
                  .deterministic = true};
        } else if constexpr (std::is_same_v<Type, TemporalSampleEffect>) {
          return {.domain = EffectDomain::temporal,
                  .spatial_radius = 0,
                  .temporal_radius = typed.radius,
                  .gpu_fusible = false,
                  .deterministic = true};
        } else {
          return {.domain = EffectDomain::external,
                  .spatial_radius = 0,
                  .temporal_radius = 0,
                  .gpu_fusible = false,
                  .deterministic = typed.deterministic};
        }
      },
      effect);
}

std::string_view effect_name(const Effect& effect) noexcept {
  return std::visit(
      [](const auto& typed) -> std::string_view {
        using Type = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Type, Transform2DEffect>) return "transform_2d";
        if constexpr (std::is_same_v<Type, CropEffect>) return "crop";
        if constexpr (std::is_same_v<Type, OpacityEffect>) return "opacity";
        if constexpr (std::is_same_v<Type, AnimatedOpacityEffect>) return "animated_opacity";
        if constexpr (std::is_same_v<Type, MaskEffect>) return "mask";
        if constexpr (std::is_same_v<Type, ColorMatrixEffect>) return "color_matrix";
        if constexpr (std::is_same_v<Type, GaussianBlurEffect>) return "gaussian_blur";
        if constexpr (std::is_same_v<Type, TemporalSampleEffect>) return "temporal_sample";
        return "external";
      },
      effect);
}

std::string_view easing_curve_name(EasingCurve easing) noexcept {
  switch (easing) {
    case EasingCurve::linear: return "linear";
    case EasingCurve::ease_in: return "ease_in";
    case EasingCurve::ease_out: return "ease_out";
    case EasingCurve::ease_in_out: return "ease_in_out";
  }
  return "unknown";
}

std::string_view mask_shape_name(MaskShape shape) noexcept {
  switch (shape) {
    case MaskShape::rectangle: return "rectangle";
    case MaskShape::ellipse: return "ellipse";
  }
  return "unknown";
}

double evaluate_easing(EasingCurve easing, double progress) noexcept {
  const auto value = std::clamp(progress, 0.0, 1.0);
  switch (easing) {
    case EasingCurve::linear: return value;
    case EasingCurve::ease_in: return value * value * value;
    case EasingCurve::ease_out: {
      const auto inverse = 1.0 - value;
      return 1.0 - inverse * inverse * inverse;
    }
    case EasingCurve::ease_in_out:
      if (value < 0.5) {
        return 4.0 * value * value * value;
      }
      return 1.0 - std::pow(-2.0 * value + 2.0, 3.0) / 2.0;
  }
  return value;
}

double evaluate_animated_opacity(const AnimatedOpacityEffect& effect,
                                 std::int64_t local_frame) noexcept {
  if (effect.keyframes.empty()) {
    return 1.0;
  }
  if (local_frame <= effect.keyframes.front().frame) {
    return effect.keyframes.front().value;
  }
  if (local_frame >= effect.keyframes.back().frame) {
    return effect.keyframes.back().value;
  }
  const auto next = std::upper_bound(
      effect.keyframes.begin(), effect.keyframes.end(), local_frame,
      [](std::int64_t frame, const ScalarKeyframe& keyframe) { return frame < keyframe.frame; });
  const auto& right = *next;
  const auto& left = *(next - 1);
  const auto progress = static_cast<double>(local_frame - left.frame) /
                        static_cast<double>(right.frame - left.frame);
  const auto eased = evaluate_easing(left.easing, progress);
  return left.value + (right.value - left.value) * eased;
}

std::string_view effect_domain_name(EffectDomain domain) noexcept {
  switch (domain) {
    case EffectDomain::coordinate: return "coordinate";
    case EffectDomain::pointwise: return "pointwise";
    case EffectDomain::neighborhood: return "neighborhood";
    case EffectDomain::temporal: return "temporal";
    case EffectDomain::external: return "external";
  }
  return "unknown";
}

OptimizedEffectProgram optimize_effect_program(const EffectProgram& program) {
  std::vector<Effect> optimized;
  optimized.reserve(program.effects().size());
  std::size_t rewrite_count = 0;

  for (const auto& effect : program.effects()) {
    if (const auto* transform = std::get_if<Transform2DEffect>(&effect)) {
      if (is_identity(*transform)) {
        ++rewrite_count;
        continue;
      }
      if (!optimized.empty()) {
        if (const auto* previous = std::get_if<Transform2DEffect>(&optimized.back())) {
          const auto combined = compose(*previous, *transform);
          optimized.back() = combined;
          ++rewrite_count;
          if (is_identity(combined)) {
            optimized.pop_back();
            ++rewrite_count;
          }
          continue;
        }
      }
    } else if (const auto* opacity = std::get_if<OpacityEffect>(&effect)) {
      if (approximately(opacity->opacity, 1.0)) {
        ++rewrite_count;
        continue;
      }
      if (!optimized.empty()) {
        if (const auto* previous = std::get_if<OpacityEffect>(&optimized.back())) {
          const auto combined = OpacityEffect{previous->opacity * opacity->opacity};
          optimized.back() = combined;
          ++rewrite_count;
          continue;
        }
      }
    } else if (const auto* matrix = std::get_if<ColorMatrixEffect>(&effect)) {
      if (is_identity(*matrix)) {
        ++rewrite_count;
        continue;
      }
      if (!optimized.empty()) {
        if (const auto* previous = std::get_if<ColorMatrixEffect>(&optimized.back())) {
          const auto combined = compose(*previous, *matrix);
          optimized.back() = combined;
          ++rewrite_count;
          if (is_identity(combined)) {
            optimized.pop_back();
            ++rewrite_count;
          }
          continue;
        }
      }
    } else if (const auto* blur = std::get_if<GaussianBlurEffect>(&effect);
               blur != nullptr && approximately(blur->radius, 0.0)) {
      ++rewrite_count;
      continue;
    }
    optimized.push_back(effect);
  }

  std::vector<FusionGroup> groups;
  std::size_t index = 0;
  while (index < optimized.size()) {
    const auto fusible = effect_traits(optimized[index]).gpu_fusible;
    const auto begin = index;
    ++index;
    if (fusible) {
      while (index < optimized.size() && effect_traits(optimized[index]).gpu_fusible) {
        ++index;
      }
    }
    groups.push_back(FusionGroup{.begin = begin, .end = index, .gpu_fusible = fusible});
  }

  return OptimizedEffectProgram(EffectProgram(std::move(optimized)), std::move(groups),
                                rewrite_count);
}

std::string optimized_effect_program_json(const OptimizedEffectProgram& program) {
  nlohmann::json effects = nlohmann::json::array();
  for (const auto& effect : program.program().effects()) {
    effects.push_back(effect_json(effect));
  }
  nlohmann::json groups = nlohmann::json::array();
  for (const auto& group : program.fusion_groups()) {
    groups.push_back({{"begin", group.begin},
                      {"end", group.end},
                      {"gpu_fusible", group.gpu_fusible}});
  }
  return nlohmann::json{{"rewrite_count", program.rewrite_count()},
                        {"effects", std::move(effects)},
                        {"fusion_groups", std::move(groups)}}
      .dump(2);
}

std::string effect_program_json(const EffectProgram& program) {
  nlohmann::json effects = nlohmann::json::array();
  for (const auto& effect : program.effects()) {
    effects.push_back(effect_json(effect));
  }
  return effects.dump(2);
}

ColorMatrixEffect identity_color_matrix() noexcept {
  return ColorMatrixEffect{.values = {1.0, 0.0, 0.0, 0.0, 0.0,
                                      0.0, 1.0, 0.0, 0.0, 0.0,
                                      0.0, 0.0, 1.0, 0.0, 0.0,
                                      0.0, 0.0, 0.0, 1.0, 0.0}};
}

}  // namespace chroma
