#include "chroma/core/execution_plan.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

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

bool valid_blend_mode(BlendMode mode) noexcept {
  return blend_mode_name(mode) != "unknown";
}

bool valid_placement(const SourcePlacement& placement) noexcept {
  return fit_mode_name(placement.fit) != "unknown" &&
         std::isfinite(placement.position_x) && std::isfinite(placement.position_y) &&
         placement.position_x >= 0.0 && placement.position_x <= 1.0 &&
         placement.position_y >= 0.0 && placement.position_y <= 1.0;
}

nlohmann::json placement_json(const SourcePlacement& placement) {
  return {{"fit", fit_mode_name(placement.fit)},
          {"position", {placement.position_x, placement.position_y}}};
}

OptimizedEffectProgram compile_effects(const EffectProgram& program,
                                       std::optional<double> final_opacity = std::nullopt) {
  auto effects = program.effects();
  if (final_opacity && *final_opacity != 1.0) {
    effects.emplace_back(OpacityEffect{.opacity = *final_opacity});
  }
  return optimize_effect_program(EffectProgram(std::move(effects)));
}

bool preserves_transparency(const Effect& effect) noexcept {
  return std::visit(
      [](const auto& typed) noexcept {
        using Type = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Type, ColorMatrixEffect>) {
          return typed.values[4] == 0.0 && typed.values[9] == 0.0 &&
                 typed.values[14] == 0.0 && typed.values[19] == 0.0;
        }
        return std::is_same_v<Type, Transform2DEffect> || std::is_same_v<Type, CropEffect> ||
               std::is_same_v<Type, OpacityEffect> ||
               std::is_same_v<Type, GaussianBlurEffect>;
      },
      effect);
}

bool definitely_transparent(const EffectProgram& program, bool source_is_transparent) noexcept {
  auto transparent = source_is_transparent;
  for (const auto& effect : program.effects()) {
    if (const auto* opacity = std::get_if<OpacityEffect>(&effect);
        opacity != nullptr && opacity->opacity == 0.0) {
      transparent = true;
      continue;
    }
    if (transparent && !preserves_transparency(effect)) {
      transparent = false;
    }
  }
  return transparent;
}

nlohmann::json effects_json(const EffectProgram& program) {
  return nlohmann::json::parse(effect_program_json(program));
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
      if (!valid_color(solid->color) || !valid_blend_mode(solid->blend_mode)) {
        throw std::invalid_argument("execution plan solid operation is invalid");
      }
      continue;
    }
    const auto& video = std::get<CompositeVideoFrameOperation>(operation);
    if (video.asset_path.empty() || video.source_time < Rational(0) ||
        !valid_placement(video.placement) ||
        !valid_blend_mode(video.blend_mode)) {
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
        const auto optimized = compile_effects(solid->effects);
        if (definitely_transparent(optimized.program(), solid->color.alpha == 0.0)) {
          continue;
        }
        if (solid->color.alpha == 1.0 && optimized.program().effects().empty() &&
            solid->blend_mode == BlendMode::source_over) {
          operations.resize(1);
        }
        operations.emplace_back(CompositeSolidOperation{.source_id = solid->id,
                                                        .color = solid->color,
                                                        .effects = optimized.program(),
                                                        .effect_rewrite_count =
                                                            optimized.rewrite_count(),
                                                        .blend_mode = solid->blend_mode});
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
      const auto optimized = compile_effects(video.effects, video.opacity);
      if (definitely_transparent(optimized.program(), false)) {
        continue;
      }
      operations.emplace_back(CompositeVideoFrameOperation{
          .source_id = video.id,
          .asset_path = asset->path,
          .source_time = composition.frame_rate().time_at(FrameIndex(source_frame)),
          .placement = video.placement,
          .effects = optimized.program(),
          .effect_rewrite_count = optimized.rewrite_count(),
          .blend_mode = video.blend_mode});
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
                            {"color", color_json(solid->color)},
                            {"effects", effects_json(solid->effects)},
                            {"effect_rewrite_count", solid->effect_rewrite_count},
                            {"blend_mode", blend_mode_name(solid->blend_mode)}});
      continue;
    }
    const auto& video = std::get<CompositeVideoFrameOperation>(operation);
    operations.push_back(
        {{"op", "composite_video_frame"},
         {"source_id", video.source_id},
         {"asset_path", video.asset_path.string()},
         {"source_time", {{"numerator", video.source_time.numerator()},
                          {"denominator", video.source_time.denominator()}}},
         {"placement", placement_json(video.placement)},
         {"effects", effects_json(video.effects)},
         {"effect_rewrite_count", video.effect_rewrite_count},
         {"blend_mode", blend_mode_name(video.blend_mode)}});
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
    std::size_t compiled_rewrite_count = 0;
    std::string placement_node;
    if (const auto* solid = std::get_if<CompositeSolidOperation>(&operation)) {
      source_id = solid->source_id;
      source_node = "solid_source";
      effects = solid->effects.effects();
      compiled_rewrite_count = solid->effect_rewrite_count;
    } else {
      const auto& video = std::get<CompositeVideoFrameOperation>(operation);
      source_id = video.source_id;
      source_node = "video_sample";
      placement_node = "place_" + std::string(fit_mode_name(video.placement.fit));
      effects = video.effects.effects();
      compiled_rewrite_count = video.effect_rewrite_count;
    }

    const auto optimized = optimize_effect_program(EffectProgram(std::move(effects)));
    nlohmann::json nodes = nlohmann::json::array({source_node});
    if (!placement_node.empty()) {
      nodes.push_back(placement_node);
    }
    for (const auto& effect : optimized.program().effects()) {
      nodes.push_back(effect_name(effect));
    }
    const auto blend_mode = std::visit(
        [](const auto& typed_operation) {
          if constexpr (requires { typed_operation.blend_mode; }) {
            return typed_operation.blend_mode;
          }
          return BlendMode::source_over;
        },
        operation);
    nodes.push_back(blend_mode_name(blend_mode));

    nlohmann::json effect_groups = nlohmann::json::array();
    bool entire_chain_fusible = true;
    for (const auto& group : optimized.fusion_groups()) {
      entire_chain_fusible = entire_chain_fusible && group.gpu_fusible;
      effect_groups.push_back({{"begin", group.begin},
                               {"end", group.end},
                               {"gpu_fusible", group.gpu_fusible}});
    }

    // Source sampling, simple effects, and blending are one candidate pass until a
    // non-fusible effect forces the future GPU compiler to split the chain.
    passes.push_back({{"pass", pass_index++},
                      {"kind", entire_chain_fusible ? "fusion_candidate"
                                                     : "multi_pass_required"},
                      {"source_id", source_id},
                      {"gpu_fusible", entire_chain_fusible},
                      {"rewrite_count", compiled_rewrite_count + optimized.rewrite_count()},
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
