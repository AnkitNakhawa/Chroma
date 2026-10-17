#include <cstdlib>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "chroma/core/effect.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": check failed: " << expression << '\n';
    ++failures;
  }
}

template <typename Exception, typename Operation>
void check_throws(Operation operation, std::string_view expression, int line) {
  try {
    operation();
    std::cerr << "line " << line << ": expected exception: " << expression << '\n';
    ++failures;
  } catch (const Exception&) {
  } catch (...) {
    std::cerr << "line " << line << ": wrong exception type: " << expression << '\n';
    ++failures;
  }
}

#define CHECK(expression) check((expression), #expression, __LINE__)
#define CHECK_THROWS_AS(expression, exception_type) \
  check_throws<exception_type>([&] { static_cast<void>(expression); }, #expression, __LINE__)

void traits_describe_fusion_boundaries() {
  const chroma::Effect transform = chroma::Transform2DEffect{};
  const chroma::Effect opacity = chroma::OpacityEffect{.opacity = 0.5};
  const chroma::Effect mask = chroma::MaskEffect{
      .shape = chroma::MaskShape::ellipse, .x = 0.0, .y = 0.0, .width = 10.0, .height = 10.0};
  const chroma::Effect blur = chroma::GaussianBlurEffect{.radius = 2.0};
  const chroma::Effect temporal = chroma::TemporalSampleEffect{.radius = 2};
  const chroma::Effect external =
      chroma::ExternalEffect{.implementation = "openfx.example", .deterministic = false};

  CHECK(chroma::effect_traits(transform).domain == chroma::EffectDomain::coordinate);
  CHECK(chroma::effect_traits(transform).gpu_fusible);
  CHECK(chroma::effect_traits(opacity).domain == chroma::EffectDomain::pointwise);
  CHECK(chroma::effect_traits(opacity).gpu_fusible);
  CHECK(chroma::effect_traits(mask).domain == chroma::EffectDomain::pointwise);
  CHECK(chroma::effect_traits(mask).gpu_fusible);
  CHECK(chroma::effect_traits(blur).domain == chroma::EffectDomain::neighborhood);
  CHECK(chroma::effect_traits(blur).spatial_radius == 6);
  CHECK(!chroma::effect_traits(blur).gpu_fusible);
  CHECK(chroma::effect_traits(temporal).temporal_radius == 2);
  CHECK(!chroma::effect_traits(external).deterministic);
}

void optimizer_removes_identities_and_combines_adjacent_effects() {
  const chroma::EffectProgram program({
      chroma::Transform2DEffect{},
      chroma::OpacityEffect{.opacity = 1.0},
      chroma::OpacityEffect{.opacity = 0.5},
      chroma::OpacityEffect{.opacity = 0.25},
      chroma::GaussianBlurEffect{.radius = 0.0},
  });
  const auto optimized = chroma::optimize_effect_program(program);
  CHECK(optimized.program().effects().size() == 1);
  CHECK(optimized.rewrite_count() == 4);
  const auto* opacity = std::get_if<chroma::OpacityEffect>(&optimized.program().effects()[0]);
  CHECK(opacity != nullptr);
  if (opacity != nullptr) {
    CHECK(opacity->opacity == 0.125);
  }
  CHECK(optimized.fusion_groups().size() == 1);
  CHECK(optimized.fusion_groups()[0] ==
        (chroma::FusionGroup{.begin = 0, .end = 1, .gpu_fusible = true}));
}

void transforms_compose_in_program_order() {
  const chroma::EffectProgram program({
      chroma::Transform2DEffect{.m02 = 3.0, .m12 = 4.0},
      chroma::Transform2DEffect{.m00 = 2.0, .m11 = 2.0},
  });
  const auto optimized = chroma::optimize_effect_program(program);
  CHECK(optimized.program().effects().size() == 1);
  const auto* transform =
      std::get_if<chroma::Transform2DEffect>(&optimized.program().effects()[0]);
  CHECK(transform != nullptr);
  if (transform != nullptr) {
    CHECK(transform->m00 == 2.0);
    CHECK(transform->m11 == 2.0);
    CHECK(transform->m02 == 6.0);
    CHECK(transform->m12 == 8.0);
  }
}

void barriers_partition_fusible_effects() {
  const chroma::EffectProgram program({
      chroma::Transform2DEffect{.m02 = 1.0},
      chroma::OpacityEffect{.opacity = 0.5},
      chroma::GaussianBlurEffect{.radius = 2.0},
      chroma::identity_color_matrix(),
      chroma::CropEffect{.x = 0, .y = 0, .width = 10, .height = 10},
      chroma::ExternalEffect{.implementation = "example", .deterministic = true},
  });
  const auto optimized = chroma::optimize_effect_program(program);
  CHECK(optimized.program().effects().size() == 5);
  CHECK(optimized.fusion_groups().size() == 4);
  if (optimized.fusion_groups().size() == 4) {
    CHECK(optimized.fusion_groups()[0] ==
          (chroma::FusionGroup{.begin = 0, .end = 2, .gpu_fusible = true}));
    CHECK(optimized.fusion_groups()[1] ==
          (chroma::FusionGroup{.begin = 2, .end = 3, .gpu_fusible = false}));
    CHECK(optimized.fusion_groups()[2] ==
          (chroma::FusionGroup{.begin = 3, .end = 4, .gpu_fusible = true}));
    CHECK(optimized.fusion_groups()[3] ==
          (chroma::FusionGroup{.begin = 4, .end = 5, .gpu_fusible = false}));
  }
}

void invalid_effects_are_rejected() {
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::OpacityEffect{.opacity = 2.0}}),
                  std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::CropEffect{.x = 0, .y = 0,
                                                            .width = 0, .height = 1}}),
                  std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::TemporalSampleEffect{.radius = 0}}),
                  std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::MaskEffect{
                      .shape = chroma::MaskShape::rectangle,
                      .x = 0.0,
                      .y = 0.0,
                      .width = 0.0,
                      .height = 1.0}}),
                  std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::ExternalEffect{.implementation = "",
                                                                .deterministic = true}}),
                  std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::OptimizedEffectProgram(
          chroma::EffectProgram({chroma::OpacityEffect{.opacity = 0.5}}),
          {chroma::FusionGroup{.begin = 0, .end = 1, .gpu_fusible = false}}, 0),
      std::invalid_argument);
}

void inspection_is_stable_and_includes_groups() {
  const auto optimized = chroma::optimize_effect_program(chroma::EffectProgram({
      chroma::OpacityEffect{.opacity = 0.5},
      chroma::GaussianBlurEffect{.radius = 1.0},
  }));
  const auto first = chroma::optimized_effect_program_json(optimized);
  const auto second = chroma::optimized_effect_program_json(optimized);
  CHECK(first == second);
  CHECK(first.find("fusion_groups") != std::string::npos);
  CHECK(first.find("gaussian_blur") != std::string::npos);
}

void easing_and_animated_opacity_are_deterministic() {
  CHECK(chroma::evaluate_easing(chroma::EasingCurve::linear, 0.25) == 0.25);
  CHECK(chroma::evaluate_easing(chroma::EasingCurve::ease_in, 0.5) == 0.125);
  CHECK(chroma::evaluate_easing(chroma::EasingCurve::ease_out, 0.5) == 0.875);
  CHECK(chroma::evaluate_easing(chroma::EasingCurve::ease_in_out, 0.5) == 0.5);
  CHECK(chroma::evaluate_easing(chroma::EasingCurve::ease_out, -1.0) == 0.0);
  CHECK(chroma::evaluate_easing(chroma::EasingCurve::ease_in, 2.0) == 1.0);

  const chroma::AnimatedOpacityEffect animation{
      .keyframes = {{.frame = 3, .value = 0.0, .easing = chroma::EasingCurve::ease_out},
                    {.frame = 7, .value = 1.0, .easing = chroma::EasingCurve::linear}}};
  CHECK(chroma::evaluate_animated_opacity(animation, 0) == 0.0);
  CHECK(chroma::evaluate_animated_opacity(animation, 3) == 0.0);
  CHECK(chroma::evaluate_animated_opacity(animation, 5) == 0.875);
  CHECK(chroma::evaluate_animated_opacity(animation, 7) == 1.0);
  CHECK(chroma::evaluate_animated_opacity(animation, 20) == 1.0);
  CHECK(!chroma::effect_traits(chroma::Effect{animation}).gpu_fusible);
}

void invalid_animations_are_rejected() {
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedOpacityEffect{}}),
                  std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::EffectProgram({chroma::AnimatedOpacityEffect{
          .keyframes = {{.frame = 1, .value = 0.0}, {.frame = 1, .value = 1.0}}}}),
      std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::EffectProgram({chroma::AnimatedOpacityEffect{
          .keyframes = {{.frame = 0, .value = std::nan("")}}}}),
      std::invalid_argument);
}

void animated_transforms_evaluate_and_validate() {
  const chroma::AnimatedTranslationEffect translation{
      .keyframes = {{3, {0.0, -4.0}, chroma::EasingCurve::ease_out}, {7, {8.0, 4.0}}}};
  CHECK(chroma::evaluate_animated_transform(translation, 0).m12 == -4.0);
  const auto moved = chroma::evaluate_animated_transform(translation, 5);
  CHECK(moved.m02 == 7.0);
  CHECK(moved.m12 == 3.0);
  CHECK(chroma::evaluate_animated_transform(translation, 20).m02 == 8.0);
  const chroma::AnimatedScaleEffect scale{.keyframes = {{0, {1, 1}}, {4, {-1, 3}}},
                                         .origin = {2, 3}};
  const auto scaled = chroma::evaluate_animated_transform(scale, 2);
  CHECK(scaled.m00 == 0.0); // Singular scales retain the existing transparent semantics.
  CHECK(scaled.m11 == 2.0);
  CHECK(scaled.m02 == 2.0);
  CHECK(scaled.m12 == -3.0);
  CHECK(chroma::evaluate_animated_transform(scale, 4).m00 == -1.0);
  const chroma::AnimatedRotationEffect rotation{.keyframes = {{0, 0}, {8, 720}},
                                               .origin = {2, 3}};
  const auto rotated = chroma::evaluate_animated_transform(rotation, 1);
  CHECK(rotated.m00 == 0.0 && rotated.m01 == -1.0 && rotated.m10 == 1.0);
  CHECK(rotated.m02 == 5.0 && rotated.m12 == 1.0);
  CHECK(rotated.m00 * 2 + rotated.m01 * 3 + rotated.m02 == 2);
  CHECK(rotated.m10 * 2 + rotated.m11 * 3 + rotated.m12 == 3);
  CHECK(chroma::evaluate_animated_transform(rotation, 4) == chroma::Transform2DEffect{});
  CHECK(chroma::evaluate_animated_transform(rotation, 8) == chroma::Transform2DEffect{});
  const chroma::AnimatedRotationEffect reverse{.keyframes = {{0, -90}}};
  CHECK(chroma::evaluate_animated_transform(reverse, 0).m10 == -1.0);
  for (const auto& effect : std::vector<chroma::Effect>{translation, scale, rotation}) {
    const auto traits = chroma::effect_traits(effect);
    CHECK(traits.domain == chroma::EffectDomain::coordinate);
    CHECK(traits.deterministic && !traits.gpu_fusible);
    const auto json = chroma::effect_program_json(chroma::EffectProgram({effect}));
    CHECK(json.find("keyframes") != std::string::npos);
    CHECK(json == chroma::effect_program_json(chroma::EffectProgram({effect})));
  }
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedTranslationEffect{}}), std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedScaleEffect{
      .keyframes = {{0, {1, std::nan("")}}}}}), std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedRotationEffect{
      .keyframes = {{0, 0}, {0, 90}}}}), std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedRotationEffect{
      .keyframes = {{-1, 0}}}}), std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedRotationEffect{
      .keyframes = {{0, 0}}, .origin = {std::nan(""), 0}}}), std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedTranslationEffect{
      .keyframes = {{0, {0, 0}, static_cast<chroma::EasingCurve>(99)}}}}), std::invalid_argument);
  CHECK_THROWS_AS(chroma::EffectProgram({chroma::AnimatedRotationEffect{
      .keyframes = std::vector<chroma::ScalarKeyframe>(1025)}}), std::invalid_argument);
}

}  // namespace

int main() {
  traits_describe_fusion_boundaries();
  optimizer_removes_identities_and_combines_adjacent_effects();
  transforms_compose_in_program_order();
  barriers_partition_fusible_effects();
  invalid_effects_are_rejected();
  inspection_is_stable_and_includes_groups();
  easing_and_animated_opacity_are_deterministic();
  invalid_animations_are_rejected();
  animated_transforms_evaluate_and_validate();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "effect tests passed\n";
  return EXIT_SUCCESS;
}
