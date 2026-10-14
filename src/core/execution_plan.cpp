#include "chroma/core/execution_plan.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace chroma {
namespace {

nlohmann::json color_json(const Color& color) {
  return nlohmann::json::array({color.red, color.green, color.blue, color.alpha});
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

    const auto& solid = std::get<CompositeSolidOperation>(operation);
    if (solid.source_id.empty() || solid.source_id.size() > 128) {
      throw std::invalid_argument("execution plan source id is invalid");
    }
    if (!valid_color(solid.color)) {
      throw std::invalid_argument("execution plan solid color is invalid");
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
