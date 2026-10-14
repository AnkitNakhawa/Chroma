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
          .opacity = 0.75}});
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(13));
  CHECK(plan.operations().size() == 2);
  const auto* video =
      std::get_if<chroma::CompositeVideoFrameOperation>(&plan.operations().back());
  CHECK(video != nullptr);
  if (video != nullptr) {
    CHECK(video->source_id == "video");
    CHECK(video->asset_path == "clip.mkv");
    CHECK(video->source_time == chroma::Rational(1, 5));
    CHECK(video->opacity == 0.75);
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
  const auto json = chroma::optimized_execution_plan_json(plan);
  CHECK(json.find("planning_only") != std::string::npos);
  CHECK(json.find("fusion_candidate") != std::string::npos);
  CHECK(json.find("video_sample") != std::string::npos);
  CHECK(json.find("opacity") == std::string::npos);
  CHECK(json.find("source_over") != std::string::npos);
  CHECK(json.find("\"rewrite_count\": 1") != std::string::npos);
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
  optimized_plan_exposes_rewrites_without_changing_execution();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "execution plan tests passed\n";
  return EXIT_SUCCESS;
}
