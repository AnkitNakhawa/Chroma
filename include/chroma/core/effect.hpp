#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace chroma {

enum class EffectDomain {
  coordinate,
  pointwise,
  neighborhood,
  temporal,
  external,
};

struct EffectTraits {
  EffectDomain domain;
  std::int32_t spatial_radius;
  std::int32_t temporal_radius;
  bool gpu_fusible;
  bool deterministic;

  friend bool operator==(const EffectTraits&, const EffectTraits&) = default;
};

// Affine transforms use column vectors. Effects are applied in program order.
struct Transform2DEffect {
  double m00{1.0};
  double m01{0.0};
  double m02{0.0};
  double m10{0.0};
  double m11{1.0};
  double m12{0.0};

  friend bool operator==(const Transform2DEffect&, const Transform2DEffect&) = default;
};

struct CropEffect {
  std::int32_t x;
  std::int32_t y;
  std::int32_t width;
  std::int32_t height;

  friend bool operator==(const CropEffect&, const CropEffect&) = default;
};

struct OpacityEffect {
  double opacity;

  friend bool operator==(const OpacityEffect&, const OpacityEffect&) = default;
};

enum class EasingCurve {
  linear,
  ease_in,
  ease_out,
  ease_in_out,
};

struct ScalarKeyframe {
  std::int64_t frame;
  double value;
  EasingCurve easing{EasingCurve::linear};

  friend bool operator==(const ScalarKeyframe&, const ScalarKeyframe&) = default;
};

// Keyframe frames are offsets from the owning layer's start frame. The easing
// on a keyframe controls interpolation from that keyframe to the next one.
struct AnimatedOpacityEffect {
  std::vector<ScalarKeyframe> keyframes;

  friend bool operator==(const AnimatedOpacityEffect&, const AnimatedOpacityEffect&) = default;
};

enum class MaskShape {
  rectangle,
  ellipse,
};

struct MaskEffect {
  MaskShape shape{MaskShape::rectangle};
  double x;
  double y;
  double width;
  double height;
  bool inverted{false};

  friend bool operator==(const MaskEffect&, const MaskEffect&) = default;
};

// Row-major 4x5 matrix applied to unpremultiplied RGBA plus a constant column.
struct ColorMatrixEffect {
  std::array<double, 20> values;

  friend bool operator==(const ColorMatrixEffect&, const ColorMatrixEffect&) = default;
};

struct GaussianBlurEffect {
  double radius;

  friend bool operator==(const GaussianBlurEffect&, const GaussianBlurEffect&) = default;
};

struct TemporalSampleEffect {
  std::int32_t radius;

  friend bool operator==(const TemporalSampleEffect&, const TemporalSampleEffect&) = default;
};

struct ExternalEffect {
  std::string implementation;
  bool deterministic;

  friend bool operator==(const ExternalEffect&, const ExternalEffect&) = default;
};

using Effect = std::variant<Transform2DEffect, CropEffect, OpacityEffect, AnimatedOpacityEffect,
                            MaskEffect, ColorMatrixEffect, GaussianBlurEffect,
                            TemporalSampleEffect, ExternalEffect>;

class EffectProgram {
 public:
  EffectProgram() = default;
  explicit EffectProgram(std::vector<Effect> effects);

  [[nodiscard]] const std::vector<Effect>& effects() const noexcept { return effects_; }

  friend bool operator==(const EffectProgram&, const EffectProgram&) = default;

 private:
  std::vector<Effect> effects_;
};

struct FusionGroup {
  std::size_t begin;
  std::size_t end;
  bool gpu_fusible;

  friend bool operator==(const FusionGroup&, const FusionGroup&) = default;
};

class OptimizedEffectProgram {
 public:
  OptimizedEffectProgram(EffectProgram program, std::vector<FusionGroup> fusion_groups,
                         std::size_t rewrite_count);

  [[nodiscard]] const EffectProgram& program() const noexcept { return program_; }
  [[nodiscard]] const std::vector<FusionGroup>& fusion_groups() const noexcept {
    return fusion_groups_;
  }
  [[nodiscard]] std::size_t rewrite_count() const noexcept { return rewrite_count_; }

  friend bool operator==(const OptimizedEffectProgram&, const OptimizedEffectProgram&) = default;

 private:
  EffectProgram program_;
  std::vector<FusionGroup> fusion_groups_;
  std::size_t rewrite_count_;
};

[[nodiscard]] EffectTraits effect_traits(const Effect& effect);
[[nodiscard]] std::string_view effect_name(const Effect& effect) noexcept;
[[nodiscard]] std::string_view effect_domain_name(EffectDomain domain) noexcept;
[[nodiscard]] std::string_view easing_curve_name(EasingCurve easing) noexcept;
[[nodiscard]] std::string_view mask_shape_name(MaskShape shape) noexcept;
[[nodiscard]] double evaluate_easing(EasingCurve easing, double progress) noexcept;
[[nodiscard]] double evaluate_animated_opacity(const AnimatedOpacityEffect& effect,
                                               std::int64_t local_frame) noexcept;
[[nodiscard]] OptimizedEffectProgram optimize_effect_program(const EffectProgram& program);
[[nodiscard]] std::string effect_program_json(const EffectProgram& program);
[[nodiscard]] std::string optimized_effect_program_json(const OptimizedEffectProgram& program);
[[nodiscard]] ColorMatrixEffect identity_color_matrix() noexcept;

}  // namespace chroma
