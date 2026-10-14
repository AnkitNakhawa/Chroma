#include "chroma/core/execution_plan.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <limits>

#include <nlohmann/json.hpp>

namespace chroma {
namespace {

nlohmann::json color_json(const Color& color) {
  return nlohmann::json::array({color.red, color.green, color.blue, color.alpha});
}

std::int64_t checked_source_frame(const VideoLayer& layer, FrameIndex frame) {
  using Wide = __int128_t;
  const auto value = static_cast<Wide>(layer.source_start.value()) +
                     static_cast<Wide>(frame.value()) -
                     static_cast<Wide>(layer.range.start().value());
  if (value < 0 || value > static_cast<Wide>(std::numeric_limits<std::int64_t>::max())) {
    throw std::overflow_error("video source frame is outside the supported range");
  }
  return static_cast<std::int64_t>(value);
}

bool valid_color(const Color& color) noexcept {
  const double components[] = {color.red, color.green, color.blue, color.alpha};
  for (const auto component : components) {
    if (!std::isfinite(component) || component < 0.0 || component > 1.0) {
      return false;
    }
  }
  return true;
}

}  // namespace

ExecutionPlan::ExecutionPlan(std::int32_t width, std::int32_t height, FrameIndex frame,
                             std::vector<RenderOperation> operations)
    : width_(width), height_(height), frame_(frame), operations_(std::move(operations)) {
  if (width <= 0 || height <= 0 || width > 32768 || height > 32768 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
          max_canvas_pixels) {
    throw std::invalid_argument("execution plan dimensions are invalid or too large");
  }
  if (operations_.empty() || !std::holds_alternative<ClearOperation>(operations_.front())) {
    throw std::invalid_argument("execution plan must begin with a clear operation");
  }
  for (std::size_t index = 0; index < operations_.size(); ++index) {
    const auto& operation = operations_[index];
    if (const auto* clear = std::get_if<ClearOperation>(&operation)) {
      if (index != 0) {
        throw std::invalid_argument("execution plan may only clear as its first operation");
      }
      if (!valid_color(clear->color)) {
        throw std::invalid_argument("execution plan clear color is invalid");
      }
      continue;
    }

    const auto& source_id = std::visit(
        [](const auto& typed_operation) -> const std::string& {
          if constexpr (requires { typed_operation.source_id; }) {
            return typed_operation.source_id;
          } else {
            throw std::logic_error("clear operation was handled before source validation");
          }
        },
        operation);
    if (source_id.empty() || source_id.size() > 128) {
      throw std::invalid_argument("execution plan source id is invalid");
    }
    if (const auto* solid = std::get_if<CompositeSolidOperation>(&operation)) {
      if (!valid_color(solid->color)) {
        throw std::invalid_argument("execution plan solid color is invalid");
      }
      continue;
    }
    const auto& video = std::get<CompositeVideoFrameOperation>(operation);
    if (video.asset_path.empty() || video.source_time < Rational(0) ||
        !std::isfinite(video.opacity) || video.opacity < 0.0 || video.opacity > 1.0) {
      throw std::invalid_argument("execution plan video operation is invalid");
    }
  }
}

ExecutionPlan compile_frame(const Composition& composition, FrameIndex frame) {
  if (!composition.range().contains(frame)) {
    throw std::out_of_range("requested frame is outside the composition");
  }

  std::vector<RenderOperation> operations;
  operations.reserve(composition.layers().size() + 1);
  operations.emplace_back(ClearOperation{.color = Color{0.0, 0.0, 0.0, 0.0}});
  for (const auto& layer_variant : composition.layers()) {
    if (const auto* solid = std::get_if<SolidLayer>(&layer_variant)) {
      if (solid->range.contains(frame)) {
        operations.emplace_back(
            CompositeSolidOperation{.source_id = solid->id, .color = solid->color});
      }
      continue;
    }

    const auto& video = std::get<VideoLayer>(layer_variant);
    if (video.range.contains(frame)) {
      const auto* asset = composition.find_video_asset(video.asset_id);
      if (asset == nullptr) {
        throw std::logic_error("validated video layer references a missing asset");
      }
      const auto source_frame = checked_source_frame(video, frame);
      operations.emplace_back(CompositeVideoFrameOperation{
          .source_id = video.id,
          .asset_path = asset->path,
          .source_time = composition.frame_rate().time_at(FrameIndex(source_frame)),
          .opacity = video.opacity});
    }
  }

  return ExecutionPlan(composition.width(), composition.height(), frame, std::move(operations));
}

std::string execution_plan_json(const ExecutionPlan& plan) {
  nlohmann::json operations = nlohmann::json::array();
  for (const auto& operation : plan.operations()) {
    if (const auto* clear = std::get_if<ClearOperation>(&operation)) {
      operations.push_back({{"op", "clear"}, {"color", color_json(clear->color)}});
      continue;
    }
    if (const auto* solid = std::get_if<CompositeSolidOperation>(&operation)) {
      operations.push_back({{"op", "composite_solid"},
                            {"source_id", solid->source_id},
                            {"color", color_json(solid->color)}});
      continue;
    }
    const auto& video = std::get<CompositeVideoFrameOperation>(operation);
    operations.push_back(
        {{"op", "composite_video_frame"},
         {"source_id", video.source_id},
         {"asset_path", video.asset_path.string()},
         {"source_time", {{"numerator", video.source_time.numerator()},
                          {"denominator", video.source_time.denominator()}}},
         {"opacity", video.opacity}});
  }

  return nlohmann::json{{"frame", plan.frame().value()},
                        {"surface", {{"width", plan.width()}, {"height", plan.height()}}},
                        {"operations", std::move(operations)}}
      .dump(2);
}

std::string optimized_execution_plan_json(const ExecutionPlan& plan) {
  nlohmann::json passes = nlohmann::json::array();
  std::size_t pass_index = 0;

  for (const auto& operation : plan.operations()) {
    if (std::holds_alternative<ClearOperation>(operation)) {
      passes.push_back({{"pass", pass_index++},
                        {"kind", "clear"},
                        {"gpu_fusible", false},
                        {"barrier", "surface_initialization"},
                        {"nodes", nlohmann::json::array({"clear"})}});
      continue;
    }

    std::string source_id;
    std::string source_node;
    std::vector<Effect> effects;
    if (const auto* solid = std::get_if<CompositeSolidOperation>(&operation)) {
      source_id = solid->source_id;
      source_node = "solid_source";
    } else {
      const auto& video = std::get<CompositeVideoFrameOperation>(operation);
      source_id = video.source_id;
      source_node = "video_sample";
      effects.emplace_back(OpacityEffect{.opacity = video.opacity});
    }

    const auto optimized = optimize_effect_program(EffectProgram(std::move(effects)));
    nlohmann::json nodes = nlohmann::json::array({source_node});
    for (const auto& effect : optimized.program().effects()) {
      nodes.push_back(effect_name(effect));
    }
    nodes.push_back("source_over");

    nlohmann::json effect_groups = nlohmann::json::array();
    for (const auto& group : optimized.fusion_groups()) {
      effect_groups.push_back({{"begin", group.begin},
                               {"end", group.end},
                               {"gpu_fusible", group.gpu_fusible}});
    }

    // Source sampling, simple effects, and source-over are one candidate pass until a
    // non-fusible effect forces the future GPU compiler to split the chain.
    passes.push_back({{"pass", pass_index++},
                      {"kind", "fusion_candidate"},
                      {"source_id", source_id},
                      {"gpu_fusible", true},
                      {"rewrite_count", optimized.rewrite_count()},
                      {"nodes", std::move(nodes)},
                      {"effect_groups", std::move(effect_groups)}});
  }

  return nlohmann::json{{"frame", plan.frame().value()},
                        {"surface", {{"width", plan.width()}, {"height", plan.height()}}},
                        {"status", "planning_only"},
                        {"passes", std::move(passes)}}
      .dump(2);
}

}  // namespace chroma
