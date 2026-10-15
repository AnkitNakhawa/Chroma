#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "chroma/core/execution_plan.hpp"

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

chroma::Composition sample_composition() {
  return chroma::Composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
           .id = "background",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0}},
       chroma::SolidLayer{
           .id = "future",
           .range = chroma::FrameRange(chroma::FrameIndex(5), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 0.0, .green = 1.0, .blue = 0.0, .alpha = 1.0}},
       chroma::SolidLayer{
           .id = "foreground",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(5)),
           .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 0.5}}});
}

void compilation_eliminates_inactive_layers_and_preserves_order() {
  const auto plan = chroma::compile_frame(sample_composition(), chroma::FrameIndex(2));
  CHECK(plan.width() == 4);
  CHECK(plan.height() == 2);
  CHECK(plan.frame() == chroma::FrameIndex(2));
  CHECK(plan.operations().size() == 3);
  CHECK(std::holds_alternative<chroma::ClearOperation>(plan.operations()[0]));

  const auto* first = std::get_if<chroma::CompositeSolidOperation>(&plan.operations()[1]);
  const auto* second = std::get_if<chroma::CompositeSolidOperation>(&plan.operations()[2]);
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  if (first != nullptr && second != nullptr) {
    CHECK(first->source_id == "background");
    CHECK(second->source_id == "foreground");
  }
}

void equivalent_inputs_produce_equivalent_plans() {
  const auto first = chroma::compile_frame(sample_composition(), chroma::FrameIndex(6));
  const auto second = chroma::compile_frame(sample_composition(), chroma::FrameIndex(6));
  CHECK(first == second);
}

void invalid_frames_fail_during_compilation() {
  CHECK_THROWS_AS(chroma::compile_frame(sample_composition(), chroma::FrameIndex(10)),
                  std::out_of_range);
}

void plan_json_retains_source_identity() {
  const auto json =
      chroma::execution_plan_json(chroma::compile_frame(sample_composition(), chroma::FrameIndex(2)));
  CHECK(json.find("composite_solid") != std::string::npos);
  CHECK(json.find("background") != std::string::npos);
  CHECK(json.find("foreground") != std::string::npos);
  CHECK(json.find("future") == std::string::npos);
}

void plans_require_an_initial_clear() {
  CHECK_THROWS_AS(chroma::ExecutionPlan(1, 1, chroma::FrameIndex(0), {}),
                  std::invalid_argument);
}

void plans_reject_unsafe_dimensions() {
  const std::vector<chroma::RenderOperation> operations = {
      chroma::ClearOperation{.color = chroma::Color{0.0, 0.0, 0.0, 0.0}}};
  CHECK_THROWS_AS(chroma::ExecutionPlan(0, 1, chroma::FrameIndex(0), operations),
                  std::invalid_argument);
  CHECK_THROWS_AS(chroma::ExecutionPlan(32768, 32768, chroma::FrameIndex(0), operations),
                  std::invalid_argument);
}

void plans_reject_invalid_operations() {
  CHECK_THROWS_AS(
      chroma::ExecutionPlan(
          1, 1, chroma::FrameIndex(0),
          {chroma::ClearOperation{.color = chroma::Color{0.0, 0.0, 0.0, 0.0}},
           chroma::ClearOperation{.color = chroma::Color{0.0, 0.0, 0.0, 0.0}}}),
      std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::ExecutionPlan(
          1, 1, chroma::FrameIndex(0),
          {chroma::ClearOperation{.color = chroma::Color{0.0, 0.0, 0.0, 0.0}},
           chroma::CompositeSolidOperation{
               .source_id = "", .color = chroma::Color{1.0, 0.0, 0.0, 1.0}}}),
      std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::ExecutionPlan(
          1, 1, chroma::FrameIndex(0),
          {chroma::ClearOperation{.color = chroma::Color{0.0, 0.0, 0.0, 0.0}},
           chroma::CompositeSolidOperation{
               .source_id = "bad-color", .color = chroma::Color{2.0, 0.0, 0.0, 1.0}}}),
      std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::ExecutionPlan(
          1, 1, chroma::FrameIndex(0),
          {chroma::ClearOperation{.color = chroma::Color{0.0, 0.0, 0.0, 0.0}},
           chroma::CompositeSolidOperation{
               .source_id = "bad-blend",
               .color = chroma::Color{1.0, 0.0, 0.0, 1.0},
               .blend_mode = static_cast<chroma::BlendMode>(99)}}),
      std::invalid_argument);
  CHECK_THROWS_AS(
      chroma::ExecutionPlan(
          1, 1, chroma::FrameIndex(0),
          {chroma::ClearOperation{.color = chroma::Color{0.0, 0.0, 0.0, 0.0}},
           chroma::CompositeVideoFrameOperation{
               .source_id = "bad-placement",
               .asset_path = "clip.mkv",
               .source_time = chroma::Rational(0),
               .placement = {.fit = chroma::FitMode::cover, .position_x = -1.0}}}),
      std::invalid_argument);
}

void video_layers_compile_to_exact_source_times() {
  const chroma::Composition composition(
      1, 64, 36, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(60)),
      {chroma::VideoAsset{.id = "clip", .path = "clip.mkv"}},
      {chroma::VideoLayer{
          .id = "video",
          .range = chroma::FrameRange(chroma::FrameIndex(10), chroma::FrameIndex(40)),
          .asset_id = "clip",
          .source_start = chroma::FrameIndex(3),
          .opacity = 0.75,
          .placement = {.fit = chroma::FitMode::contain,
                        .position_x = 0.25,
                        .position_y = 0.75}}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(13));
  CHECK(plan.operations().size() == 2);
  const auto* video =
      std::get_if<chroma::CompositeVideoFrameOperation>(&plan.operations().back());
  CHECK(video != nullptr);
  if (video != nullptr) {
    CHECK(video->source_id == "video");
    CHECK(video->asset_path == "clip.mkv");
    CHECK(video->source_time == chroma::Rational(1, 5));
    CHECK(video->placement.fit == chroma::FitMode::contain);
    CHECK(video->placement.position_x == 0.25);
    CHECK(video->placement.position_y == 0.75);
    CHECK(video->effects.effects().size() == 1);
    const auto* opacity = std::get_if<chroma::OpacityEffect>(&video->effects.effects().front());
    CHECK(opacity != nullptr);
    if (opacity != nullptr) {
      CHECK(opacity->opacity == 0.75);
    }
  }
  const auto json = chroma::execution_plan_json(plan);
  CHECK(json.find("\"fit\": \"contain\"") != std::string::npos);
  CHECK(json.find("0.25") != std::string::npos);
  const auto optimized_json = chroma::optimized_execution_plan_json(plan);
  CHECK(optimized_json.find("place_contain") != std::string::npos);
}

void layer_effects_and_blends_lower_into_the_plan() {
  const chroma::Composition composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
          .id = "effected",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0},
          .effects = chroma::EffectProgram({chroma::OpacityEffect{.opacity = 0.5}}),
          .blend_mode = chroma::BlendMode::screen}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  const auto* solid = std::get_if<chroma::CompositeSolidOperation>(&plan.operations().back());
  CHECK(solid != nullptr);
  if (solid != nullptr) {
    CHECK(solid->effects.effects().size() == 1);
    CHECK(solid->blend_mode == chroma::BlendMode::screen);
  }
  const auto json = chroma::execution_plan_json(plan);
  CHECK(json.find("\"opacity\"") != std::string::npos);
  CHECK(json.find("\"screen\"") != std::string::npos);
}

void compilation_optimizes_effects_and_records_rewrites() {
  const chroma::Composition composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
          .id = "effected",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 0.5},
          .effects = chroma::EffectProgram({chroma::OpacityEffect{.opacity = 0.5},
                                            chroma::OpacityEffect{.opacity = 0.25}})}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  const auto* solid = std::get_if<chroma::CompositeSolidOperation>(&plan.operations().back());
  CHECK(solid != nullptr);
  if (solid != nullptr) {
    CHECK(solid->effects.effects().size() == 1);
    CHECK(solid->effect_rewrite_count == 1);
    const auto* opacity = std::get_if<chroma::OpacityEffect>(&solid->effects.effects().front());
    CHECK(opacity != nullptr);
    if (opacity != nullptr) {
      CHECK(opacity->opacity == 0.125);
    }
  }
  const auto json = chroma::execution_plan_json(plan);
  CHECK(json.find("\"effect_rewrite_count\": 1") != std::string::npos);
  const auto optimized_json = chroma::optimized_execution_plan_json(plan);
  CHECK(optimized_json.find("\"rewrite_count\": 1") != std::string::npos);
}

void compilation_eliminates_definitely_transparent_layers() {
  const chroma::Composition composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
          .id = "invisible",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0},
          .effects = chroma::EffectProgram({chroma::OpacityEffect{.opacity = 0.0}})}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  CHECK(plan.operations().size() == 1);
  CHECK(std::holds_alternative<chroma::ClearOperation>(plan.operations().front()));

  const chroma::Composition video_composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::VideoAsset{.id = "clip", .path = "unused.mkv"}},
      {chroma::VideoLayer{
          .id = "invisible-video",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .asset_id = "clip",
          .source_start = chroma::FrameIndex(0),
          .opacity = 0.0}});
  const auto video_plan = chroma::compile_frame(video_composition, chroma::FrameIndex(0));
  CHECK(video_plan.operations().size() == 1);
}

void compilation_keeps_layers_that_can_restore_alpha() {
  auto matrix = chroma::identity_color_matrix();
  matrix.values[19] = 1.0;
  const chroma::Composition composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
          .id = "restored",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0},
          .effects = chroma::EffectProgram({chroma::OpacityEffect{.opacity = 0.0}, matrix})}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  CHECK(plan.operations().size() == 2);
  CHECK(std::holds_alternative<chroma::CompositeSolidOperation>(plan.operations().back()));
}

void opaque_full_canvas_solids_cull_hidden_layers() {
  const chroma::Composition composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
           .id = "hidden",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 0.5}},
       chroma::SolidLayer{
           .id = "cover",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0}}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  CHECK(plan.operations().size() == 2);
  const auto* cover = std::get_if<chroma::CompositeSolidOperation>(&plan.operations().back());
  CHECK(cover != nullptr);
  if (cover != nullptr) {
    CHECK(cover->source_id == "cover");
  }
}

void optimized_plan_exposes_rewrites_without_changing_execution() {
  const chroma::Composition composition(
      1, 64, 36, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::VideoAsset{.id = "clip", .path = "clip.mkv"}},
      {chroma::VideoLayer{
          .id = "video",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .asset_id = "clip",
          .source_start = chroma::FrameIndex(0),
          .opacity = 1.0}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  const auto analysis = chroma::analyze_execution_plan(plan);
  CHECK(analysis.passes.size() == 2);
  CHECK(analysis.logical_temporary_surfaces == 0);
  CHECK(analysis.surface_bytes == 36'864);
  CHECK(analysis.estimated_bytes_read == 73'728);
  CHECK(analysis.estimated_bytes_written == 73'728);
  CHECK(analysis.peak_live_bytes == 73'728);
  const auto json = chroma::optimized_execution_plan_json(plan);
  CHECK(json.find("planning_only") != std::string::npos);
  CHECK(json.find("fusion_candidate") != std::string::npos);
  CHECK(json.find("video_sample") != std::string::npos);
  CHECK(json.find("opacity") == std::string::npos);
  CHECK(json.find("source_over") != std::string::npos);
  CHECK(json.find("\"rewrite_count\": 0") != std::string::npos);
}

void physical_planner_splits_barriers_and_tracks_resources() {
  const chroma::Composition composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
          .id = "blurred",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0},
          .effects = chroma::EffectProgram(
              {chroma::OpacityEffect{.opacity = 0.5},
               chroma::GaussianBlurEffect{.radius = 2.0},
               chroma::CropEffect{.x = 0, .y = 0, .width = 4, .height = 2},
               chroma::ExternalEffect{.implementation = "example", .deterministic = true}})}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  const auto analysis = chroma::analyze_execution_plan(plan);
  CHECK(analysis.passes.size() == 6);
  CHECK(analysis.logical_temporary_surfaces == 4);
  CHECK(analysis.surface_bytes == 128);
  CHECK(analysis.estimated_bytes_read == 640);
  CHECK(analysis.estimated_bytes_written == 768);
  CHECK(analysis.peak_live_bytes == 384);
  if (analysis.passes.size() == 6) {
    CHECK(analysis.passes[1].nodes ==
          (std::vector<std::string>{"solid_source", "opacity"}));
    CHECK(analysis.passes[2].kind == chroma::RenderPassKind::barrier);
    CHECK(analysis.passes[2].barrier == chroma::BarrierReason::neighborhood_effect);
    CHECK(analysis.passes[2].nodes == (std::vector<std::string>{"gaussian_blur"}));
    CHECK(analysis.passes[3].nodes == (std::vector<std::string>{"crop"}));
    CHECK(analysis.passes[4].kind == chroma::RenderPassKind::barrier);
    CHECK(analysis.passes[4].barrier == chroma::BarrierReason::external_effect);
    CHECK(analysis.passes[4].nodes == (std::vector<std::string>{"external"}));
    CHECK(analysis.passes[5].nodes == (std::vector<std::string>{"source_over"}));
    CHECK(analysis.passes[1].write_resource == analysis.passes[2].read_resources.front());
    CHECK(analysis.passes[4].write_resource == analysis.passes[5].read_resources.front());
  }
  CHECK(analysis == chroma::analyze_execution_plan(plan));

  const auto json = chroma::optimized_execution_plan_json(plan);
  CHECK(json.find("\"kind\": \"barrier\"") != std::string::npos);
  CHECK(json.find("neighborhood_effect") != std::string::npos);
  CHECK(json.find("external_effect") != std::string::npos);
  CHECK(json.find("logical_temporary_surfaces") != std::string::npos);
  CHECK(json.find("peak_live_bytes") != std::string::npos);

  const chroma::Composition temporal_composition(
      1, 4, 2, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
          .id = "temporal",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          .color = chroma::Color{.red = 1.0, .green = 1.0, .blue = 1.0, .alpha = 1.0},
          .effects = chroma::EffectProgram({chroma::TemporalSampleEffect{.radius = 1}})}});
  const auto temporal_analysis = chroma::analyze_execution_plan(
      chroma::compile_frame(temporal_composition, chroma::FrameIndex(0)));
  CHECK(temporal_analysis.passes.size() == 4);
  if (temporal_analysis.passes.size() == 4) {
    CHECK(temporal_analysis.passes[2].barrier == chroma::BarrierReason::temporal_effect);
    CHECK(temporal_analysis.passes[2].read_resources.size() == 3);
    CHECK(temporal_analysis.passes[2].read_resources[1] == "temporal:temporal:-1");
    CHECK(temporal_analysis.passes[2].read_resources[2] == "temporal:temporal:1");
  }
}

}  // namespace

int main() {
  compilation_eliminates_inactive_layers_and_preserves_order();
  equivalent_inputs_produce_equivalent_plans();
  invalid_frames_fail_during_compilation();
  plan_json_retains_source_identity();
  plans_require_an_initial_clear();
  plans_reject_unsafe_dimensions();
  plans_reject_invalid_operations();
  video_layers_compile_to_exact_source_times();
  layer_effects_and_blends_lower_into_the_plan();
  compilation_optimizes_effects_and_records_rewrites();
  compilation_eliminates_definitely_transparent_layers();
  compilation_keeps_layers_that_can_restore_alpha();
  opaque_full_canvas_solids_cull_hidden_layers();
  optimized_plan_exposes_rewrites_without_changing_execution();
  physical_planner_splits_barriers_and_tracks_resources();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "execution plan tests passed\n";
  return EXIT_SUCCESS;
}
