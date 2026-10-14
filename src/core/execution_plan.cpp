#include "chroma/core/execution_plan.hpp"

#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace chroma {
namespace {

nlohmann::json color_json(const Color& color) {
  return nlohmann::json::array({color.red, color.green, color.blue, color.alpha});
}

}  // namespace

ExecutionPlan::ExecutionPlan(std::int32_t width, std::int32_t height, FrameIndex frame,
                             std::vector<RenderOperation> operations)
    : width_(width), height_(height), frame_(frame), operations_(std::move(operations)) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("execution plan dimensions must be positive");
  }
  if (operations_.empty() || !std::holds_alternative<ClearOperation>(operations_.front())) {
    throw std::invalid_argument("execution plan must begin with a clear operation");
  }
}

ExecutionPlan compile_frame(const Composition& composition, FrameIndex frame) {
  if (!composition.range().contains(frame)) {
    throw std::out_of_range("requested frame is outside the composition");
  }

  std::vector<RenderOperation> operations;
  operations.reserve(composition.layers().size() + 1);
  operations.emplace_back(ClearOperation{.color = Color{0.0, 0.0, 0.0, 0.0}});
  for (const auto& layer : composition.layers()) {
    if (layer.range.contains(frame)) {
      operations.emplace_back(
          CompositeSolidOperation{.source_id = layer.id, .color = layer.color});
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
    const auto& solid = std::get<CompositeSolidOperation>(operation);
    operations.push_back({{"op", "composite_solid"},
                          {"source_id", solid.source_id},
                          {"color", color_json(solid.color)}});
  }

  return nlohmann::json{{"frame", plan.frame().value()},
                        {"surface", {{"width", plan.width()}, {"height", plan.height()}}},
                        {"operations", std::move(operations)}}
      .dump(2);
}

}  // namespace chroma
