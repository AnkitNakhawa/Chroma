#include "chroma/core/execution_plan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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

bool valid_shape_geometry(const ShapeGeometry& geometry) noexcept {
  return shape_kind_name(geometry.kind) != "unknown" && std::isfinite(geometry.x) &&
         std::isfinite(geometry.y) && std::isfinite(geometry.width) &&
         std::isfinite(geometry.height) && geometry.width > 0.0 && geometry.height > 0.0 &&
         std::isfinite(geometry.corner_radius) && geometry.corner_radius >= 0.0;
}

nlohmann::json placement_json(const SourcePlacement& placement) {
  return {{"fit", fit_mode_name(placement.fit)},
          {"position", {placement.position_x, placement.position_y}}};
}

nlohmann::json shape_geometry_json(const ShapeGeometry& geometry) {
  return {{"kind", shape_kind_name(geometry.kind)},
          {"bounds", {geometry.x, geometry.y, geometry.width, geometry.height}},
          {"corner_radius", geometry.corner_radius}};
}

OptimizedEffectProgram compile_effects(const EffectProgram& program, std::int64_t local_frame,
                                       std::optional<double> final_opacity = std::nullopt) {
  std::vector<Effect> effects;
  effects.reserve(program.effects().size() + static_cast<std::size_t>(final_opacity.has_value()));
  for (const auto& effect : program.effects()) {
    if (const auto* animated = std::get_if<AnimatedOpacityEffect>(&effect)) {
      effects.emplace_back(
          OpacityEffect{.opacity = evaluate_animated_opacity(*animated, local_frame)});
    } else {
      effects.push_back(effect);
    }
  }
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
               std::is_same_v<Type, AnimatedOpacityEffect> ||
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

BarrierReason barrier_reason(const Effect& effect) noexcept {
  switch (effect_traits(effect).domain) {
    case EffectDomain::neighborhood: return BarrierReason::neighborhood_effect;
    case EffectDomain::temporal: return BarrierReason::temporal_effect;
    case EffectDomain::external: return BarrierReason::external_effect;
    case EffectDomain::coordinate:
    case EffectDomain::pointwise: return BarrierReason::none;
  }
  return BarrierReason::external_effect;
}

bool is_temporary_resource(std::string_view resource) noexcept {
  return resource.starts_with("temporary:");
}

CacheOpportunity classify_cache_opportunity(std::string source_id, std::string source_node,
                                            bool frame_variant, bool static_resource,
                                            const EffectProgram& program,
                                            std::uint64_t surface_bytes) {
  CacheOpportunity opportunity{.source_id = std::move(source_id),
                               .decision = CacheDecision::recompute,
                               .reason = CacheReason::cheap_static_subgraph,
                               .nodes = {},
                               .estimated_storage_bytes = 0,
                               .storage_size_known = true};
  opportunity.nodes.push_back(std::move(source_node));
  bool temporal = false;
  bool nondeterministic = false;
  bool expensive = false;
  for (const auto& effect : program.effects()) {
    opportunity.nodes.emplace_back(effect_name(effect));
    const auto traits = effect_traits(effect);
    temporal = temporal || traits.domain == EffectDomain::temporal;
    nondeterministic = nondeterministic || !traits.deterministic;
    expensive = expensive || traits.domain == EffectDomain::neighborhood ||
                traits.domain == EffectDomain::external;
  }

  if (frame_variant) {
    opportunity.decision = CacheDecision::not_cacheable;
    opportunity.reason = CacheReason::frame_variant_source;
  } else if (temporal) {
    opportunity.decision = CacheDecision::not_cacheable;
    opportunity.reason = CacheReason::temporal_dependency;
  } else if (nondeterministic) {
    opportunity.decision = CacheDecision::not_cacheable;
    opportunity.reason = CacheReason::nondeterministic_effect;
  } else if (static_resource) {
    opportunity.decision = CacheDecision::cache_candidate;
    opportunity.reason = CacheReason::static_resource;
    opportunity.estimated_storage_bytes = 0;
    opportunity.storage_size_known = false;
  } else if (expensive) {
    opportunity.decision = CacheDecision::cache_candidate;
    opportunity.reason = CacheReason::expensive_static_subgraph;
    opportunity.estimated_storage_bytes = surface_bytes;
  }
  return opportunity;
}

void allocate_temporary_buffers(ExecutionPlanAnalysis& analysis) {
  std::unordered_map<std::string, std::size_t> allocation_by_resource;
  for (const auto& pass : analysis.passes) {
    if (is_temporary_resource(pass.write_resource)) {
      allocation_by_resource.emplace(pass.write_resource,
                                     analysis.temporary_allocations.size());
      analysis.temporary_allocations.push_back(
          TemporaryAllocation{.logical_resource = pass.write_resource,
                              .physical_buffer = 0,
                              .first_pass = pass.index,
                              .last_pass = pass.index});
    }
    for (const auto& resource : pass.read_resources) {
      const auto found = allocation_by_resource.find(resource);
      if (found != allocation_by_resource.end()) {
        analysis.temporary_allocations[found->second].last_pass = pass.index;
      }
    }
  }

  std::vector<std::size_t> buffer_last_use;
  for (auto& allocation : analysis.temporary_allocations) {
    auto buffer = buffer_last_use.size();
    for (std::size_t candidate = 0; candidate < buffer_last_use.size(); ++candidate) {
      if (buffer_last_use[candidate] < allocation.first_pass) {
        buffer = candidate;
        break;
      }
    }
    if (buffer == buffer_last_use.size()) {
      buffer_last_use.push_back(allocation.last_pass);
    } else {
      buffer_last_use[buffer] = allocation.last_pass;
    }
    allocation.physical_buffer = buffer;
  }
  analysis.physical_temporary_buffers = buffer_last_use.size();
  analysis.physical_temporary_bytes =
      static_cast<std::uint64_t>(analysis.physical_temporary_buffers) * analysis.surface_bytes;
}

}  // namespace

std::string_view render_pass_kind_name(RenderPassKind kind) noexcept {
  switch (kind) {
    case RenderPassKind::clear: return "clear";
    case RenderPassKind::fusion_candidate: return "fusion_candidate";
    case RenderPassKind::barrier: return "barrier";
  }
  return "unknown";
}

std::string_view barrier_reason_name(BarrierReason reason) noexcept {
  switch (reason) {
    case BarrierReason::none: return "none";
    case BarrierReason::surface_initialization: return "surface_initialization";
    case BarrierReason::neighborhood_effect: return "neighborhood_effect";
    case BarrierReason::temporal_effect: return "temporal_effect";
    case BarrierReason::external_effect: return "external_effect";
  }
  return "unknown";
}

std::string_view cache_decision_name(CacheDecision decision) noexcept {
  switch (decision) {
    case CacheDecision::not_cacheable: return "not_cacheable";
    case CacheDecision::recompute: return "recompute";
    case CacheDecision::cache_candidate: return "cache_candidate";
  }
  return "unknown";
}

std::string_view cache_reason_name(CacheReason reason) noexcept {
  switch (reason) {
    case CacheReason::frame_variant_source: return "frame_variant_source";
    case CacheReason::temporal_dependency: return "temporal_dependency";
    case CacheReason::nondeterministic_effect: return "nondeterministic_effect";
    case CacheReason::cheap_static_subgraph: return "cheap_static_subgraph";
    case CacheReason::expensive_static_subgraph: return "expensive_static_subgraph";
    case CacheReason::static_resource: return "static_resource";
  }
  return "unknown";
}

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
    if (const auto* video = std::get_if<CompositeVideoFrameOperation>(&operation)) {
      if (video->asset_path.empty() || video->source_time < Rational(0) ||
          !valid_placement(video->placement) || !valid_blend_mode(video->blend_mode)) {
        throw std::invalid_argument("execution plan video operation is invalid");
      }
      continue;
    }
    if (const auto* image = std::get_if<CompositeImageOperation>(&operation)) {
      if (image->asset_path.empty() || !valid_placement(image->placement) ||
          !valid_blend_mode(image->blend_mode)) {
        throw std::invalid_argument("execution plan image operation is invalid");
      }
      continue;
    }
    if (const auto* shape = std::get_if<CompositeShapeOperation>(&operation)) {
      if (!valid_shape_geometry(shape->geometry) || !valid_color(shape->fill) ||
          (shape->stroke && (!valid_color(shape->stroke->color) ||
                             !std::isfinite(shape->stroke->width) ||
                             shape->stroke->width <= 0.0)) ||
          !valid_blend_mode(shape->blend_mode)) {
        throw std::invalid_argument("execution plan shape operation is invalid");
      }
      continue;
    }
    const auto& text = std::get<CompositeTextOperation>(operation);
    if (text.font_path.empty() || text.text.empty() || text.text.size() > 16'384 ||
        !std::isfinite(text.font_size) || text.font_size <= 0.0 || text.font_size > 4096.0 ||
        !std::isfinite(text.x) || !std::isfinite(text.y) || !valid_color(text.color) ||
        !valid_blend_mode(text.blend_mode)) {
      throw std::invalid_argument("execution plan text operation is invalid");
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
        const auto local_frame = frame.value() - solid->range.start().value();
        const auto optimized = compile_effects(solid->effects, local_frame);
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

    if (const auto* video = std::get_if<VideoLayer>(&layer_variant)) {
      if (!video->range.contains(frame)) {
        continue;
      }
      const auto* asset = composition.find_video_asset(video->asset_id);
      if (asset == nullptr) {
        throw std::logic_error("validated video layer references a missing asset");
      }
      const auto source_frame = checked_source_frame(*video, frame);
      const auto local_frame = frame.value() - video->range.start().value();
      const auto optimized = compile_effects(video->effects, local_frame, video->opacity);
      if (definitely_transparent(optimized.program(), false)) {
        continue;
      }
      operations.emplace_back(CompositeVideoFrameOperation{
          .source_id = video->id,
          .asset_path = asset->path,
          .source_time = composition.frame_rate().time_at(FrameIndex(source_frame)),
          .placement = video->placement,
          .effects = optimized.program(),
          .effect_rewrite_count = optimized.rewrite_count(),
          .blend_mode = video->blend_mode});
      continue;
    }

    if (const auto* image = std::get_if<ImageLayer>(&layer_variant)) {
      if (!image->range.contains(frame)) {
        continue;
      }
      const auto* asset = composition.find_image_asset(image->asset_id);
      if (asset == nullptr) {
        throw std::logic_error("validated image layer references a missing asset");
      }
      const auto local_frame = frame.value() - image->range.start().value();
      const auto optimized = compile_effects(image->effects, local_frame);
      if (definitely_transparent(optimized.program(), false)) {
        continue;
      }
      operations.emplace_back(CompositeImageOperation{
          .source_id = image->id,
          .asset_path = asset->path,
          .placement = image->placement,
          .effects = optimized.program(),
          .effect_rewrite_count = optimized.rewrite_count(),
          .blend_mode = image->blend_mode});
      continue;
    }

    if (const auto* shape = std::get_if<ShapeLayer>(&layer_variant)) {
      if (!shape->range.contains(frame)) {
        continue;
      }
      const auto local_frame = frame.value() - shape->range.start().value();
      const auto optimized = compile_effects(shape->effects, local_frame);
      const auto transparent_paint =
          shape->fill.alpha == 0.0 && (!shape->stroke || shape->stroke->color.alpha == 0.0);
      if (definitely_transparent(optimized.program(), transparent_paint)) {
        continue;
      }
      operations.emplace_back(CompositeShapeOperation{
          .source_id = shape->id,
          .geometry = shape->geometry,
          .fill = shape->fill,
          .stroke = shape->stroke,
          .effects = optimized.program(),
          .effect_rewrite_count = optimized.rewrite_count(),
          .blend_mode = shape->blend_mode});
      continue;
    }

    const auto& text = std::get<TextLayer>(layer_variant);
    if (text.range.contains(frame)) {
      const auto* font = composition.find_font_asset(text.font_asset_id);
      if (font == nullptr) {
        throw std::logic_error("validated text layer references a missing font asset");
      }
      const auto local_frame = frame.value() - text.range.start().value();
      const auto optimized = compile_effects(text.effects, local_frame);
      if (definitely_transparent(optimized.program(), text.color.alpha == 0.0)) {
        continue;
      }
      operations.emplace_back(CompositeTextOperation{
          .source_id = text.id,
          .font_path = font->path,
          .text = text.text,
          .font_size = text.font_size,
          .x = text.x,
          .y = text.y,
          .color = text.color,
          .effects = optimized.program(),
          .effect_rewrite_count = optimized.rewrite_count(),
          .blend_mode = text.blend_mode});
    }
  }

  return ExecutionPlan(composition.width(), composition.height(), frame, std::move(operations));
}

ExecutionPlanAnalysis analyze_execution_plan(const ExecutionPlan& plan) {
  ExecutionPlanAnalysis analysis;
  analysis.surface_bytes = static_cast<std::uint64_t>(plan.width()) *
                           static_cast<std::uint64_t>(plan.height()) * 4U * sizeof(float);
  std::size_t temporary_index = 0;
  std::unordered_map<std::string, std::size_t> prepared_resource_by_key;

  const auto record_prepared_resource = [&](PreparedResourceKind kind,
                                            const std::string& source_id,
                                            std::string request_key) {
    const auto found = prepared_resource_by_key.find(request_key);
    if (found != prepared_resource_by_key.end()) {
      analysis.prepared_resources[found->second].source_ids.push_back(source_id);
      return;
    }
    prepared_resource_by_key.emplace(request_key, analysis.prepared_resources.size());
    analysis.prepared_resources.push_back(
        PreparedResourcePlan{.kind = kind,
                             .identity = prepared_resource_identity(request_key),
                             .source_ids = {source_id},
                             .estimated_storage_bytes = 0,
                             .storage_size_known = false});
  };

  const auto next_temporary = [&]() {
    ++analysis.logical_temporary_surfaces;
    return std::string("temporary:") + std::to_string(temporary_index++);
  };
  const auto append_pass = [&](RenderPassKind kind, BarrierReason barrier,
                               const std::string& source_id, bool gpu_fusible,
                               std::size_t rewrite_count, std::vector<std::string> nodes,
                               std::vector<std::string> reads, std::string write) {
    const auto read_count = static_cast<std::uint64_t>(reads.size());
    analysis.estimated_bytes_read += read_count * analysis.surface_bytes;
    analysis.estimated_bytes_written += analysis.surface_bytes;

    std::unordered_set<std::string> live_resources{"canvas"};
    live_resources.insert(reads.begin(), reads.end());
    live_resources.insert(write);
    const auto live_bytes = static_cast<std::uint64_t>(live_resources.size()) *
                            analysis.surface_bytes;
    analysis.peak_live_bytes = std::max(analysis.peak_live_bytes, live_bytes);
    analysis.passes.push_back(PlannedRenderPass{
        .index = analysis.passes.size(),
        .kind = kind,
        .barrier = barrier,
        .source_id = source_id,
        .gpu_fusible = gpu_fusible,
        .rewrite_count = rewrite_count,
        .nodes = std::move(nodes),
        .read_resources = std::move(reads),
        .write_resource = std::move(write)});
  };

  append_pass(RenderPassKind::clear, BarrierReason::surface_initialization, "", false, 0,
              {"clear"}, {}, "canvas");

  for (const auto& operation : plan.operations()) {
    if (std::holds_alternative<ClearOperation>(operation)) {
      continue;
    }

    std::string source_id;
    std::vector<std::string> current_nodes;
    std::vector<std::string> current_reads;
    std::vector<Effect> effects;
    std::size_t pending_rewrites = 0;
    BlendMode blend_mode = BlendMode::source_over;
    std::string source_node;
    bool frame_variant = false;
    bool static_resource = false;
    if (const auto* solid = std::get_if<CompositeSolidOperation>(&operation)) {
      source_id = solid->source_id;
      source_node = "solid_source";
      current_nodes.push_back(source_node);
      effects = solid->effects.effects();
      pending_rewrites = solid->effect_rewrite_count;
      blend_mode = solid->blend_mode;
    } else if (const auto* video = std::get_if<CompositeVideoFrameOperation>(&operation)) {
      source_id = video->source_id;
      source_node = "video_sample";
      frame_variant = true;
      current_nodes.push_back(source_node);
      current_nodes.push_back("place_" + std::string(fit_mode_name(video->placement.fit)));
      current_reads.push_back("source:" + source_id);
      effects = video->effects.effects();
      pending_rewrites = video->effect_rewrite_count;
      blend_mode = video->blend_mode;
    } else if (const auto* image = std::get_if<CompositeImageOperation>(&operation)) {
      source_id = image->source_id;
      source_node = "image_sample";
      static_resource = true;
      current_nodes.push_back(source_node);
      current_nodes.push_back("place_" + std::string(fit_mode_name(image->placement.fit)));
      current_reads.push_back("source:" + source_id);
      effects = image->effects.effects();
      pending_rewrites = image->effect_rewrite_count;
      blend_mode = image->blend_mode;
      record_prepared_resource(PreparedResourceKind::image, source_id,
                               prepared_image_request_key(image->asset_path));
    } else if (const auto* shape = std::get_if<CompositeShapeOperation>(&operation)) {
      source_id = shape->source_id;
      source_node = "shape_" + std::string(shape_kind_name(shape->geometry.kind));
      current_nodes.push_back(source_node);
      effects = shape->effects.effects();
      pending_rewrites = shape->effect_rewrite_count;
      blend_mode = shape->blend_mode;
    } else {
      const auto& text = std::get<CompositeTextOperation>(operation);
      source_id = text.source_id;
      source_node = "text_raster";
      static_resource = true;
      current_nodes.push_back(source_node);
      current_reads.push_back("source:" + source_id);
      effects = text.effects.effects();
      pending_rewrites = text.effect_rewrite_count;
      blend_mode = text.blend_mode;
      record_prepared_resource(
          PreparedResourceKind::text, source_id,
          prepared_text_request_key(text.font_path, text.text, text.font_size));
    }

    const auto optimized = optimize_effect_program(EffectProgram(std::move(effects)));
    analysis.cache_opportunities.push_back(classify_cache_opportunity(
        source_id, source_node, frame_variant, static_resource, optimized.program(),
        analysis.surface_bytes));
    pending_rewrites += optimized.rewrite_count();
    analysis.total_rewrite_count += pending_rewrites;
    for (const auto& group : optimized.fusion_groups()) {
      if (group.gpu_fusible) {
        for (auto index = group.begin; index < group.end; ++index) {
          current_nodes.emplace_back(effect_name(optimized.program().effects()[index]));
        }
        continue;
      }

      if (!current_nodes.empty()) {
        auto temporary = next_temporary();
        append_pass(RenderPassKind::fusion_candidate, BarrierReason::none, source_id, true,
                    pending_rewrites, std::move(current_nodes), std::move(current_reads),
                    temporary);
        pending_rewrites = 0;
        current_reads = {std::move(temporary)};
      }

      for (auto index = group.begin; index < group.end; ++index) {
        const auto& effect = optimized.program().effects()[index];
        auto temporary = next_temporary();
        auto barrier_reads = current_reads;
        if (const auto* temporal = std::get_if<TemporalSampleEffect>(&effect)) {
          for (auto offset = -temporal->radius; offset <= temporal->radius; ++offset) {
            if (offset != 0) {
              barrier_reads.push_back("temporal:" + source_id + ":" +
                                      std::to_string(offset));
            }
          }
        }
        append_pass(RenderPassKind::barrier, barrier_reason(effect), source_id, false,
                    pending_rewrites, {std::string(effect_name(effect))},
                    std::move(barrier_reads),
                    temporary);
        pending_rewrites = 0;
        current_reads = {std::move(temporary)};
      }
    }

    current_nodes.emplace_back(blend_mode_name(blend_mode));
    if (std::find(current_reads.begin(), current_reads.end(), "canvas") ==
        current_reads.end()) {
      current_reads.emplace_back("canvas");
    }
    append_pass(RenderPassKind::fusion_candidate, BarrierReason::none, source_id, true,
                pending_rewrites, std::move(current_nodes), std::move(current_reads),
                "canvas");
  }
  allocate_temporary_buffers(analysis);
  return analysis;
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
    if (const auto* video = std::get_if<CompositeVideoFrameOperation>(&operation)) {
      operations.push_back(
          {{"op", "composite_video_frame"},
           {"source_id", video->source_id},
           {"asset_path", video->asset_path.string()},
           {"source_time", {{"numerator", video->source_time.numerator()},
                            {"denominator", video->source_time.denominator()}}},
           {"placement", placement_json(video->placement)},
           {"effects", effects_json(video->effects)},
           {"effect_rewrite_count", video->effect_rewrite_count},
           {"blend_mode", blend_mode_name(video->blend_mode)}});
      continue;
    }
    if (const auto* image = std::get_if<CompositeImageOperation>(&operation)) {
      operations.push_back({{"op", "composite_image"},
                            {"source_id", image->source_id},
                            {"asset_path", image->asset_path.string()},
                            {"placement", placement_json(image->placement)},
                            {"effects", effects_json(image->effects)},
                            {"effect_rewrite_count", image->effect_rewrite_count},
                            {"blend_mode", blend_mode_name(image->blend_mode)}});
      continue;
    }
    if (const auto* shape = std::get_if<CompositeShapeOperation>(&operation)) {
      nlohmann::json stroke = nullptr;
      if (shape->stroke) {
        stroke = {{"color", color_json(shape->stroke->color)},
                  {"width", shape->stroke->width}};
      }
      operations.push_back({{"op", "composite_shape"},
                            {"source_id", shape->source_id},
                            {"geometry", shape_geometry_json(shape->geometry)},
                            {"fill", color_json(shape->fill)},
                            {"stroke", std::move(stroke)},
                            {"effects", effects_json(shape->effects)},
                            {"effect_rewrite_count", shape->effect_rewrite_count},
                            {"blend_mode", blend_mode_name(shape->blend_mode)}});
      continue;
    }
    const auto& text = std::get<CompositeTextOperation>(operation);
    operations.push_back({{"op", "composite_text"},
                          {"source_id", text.source_id},
                          {"font_path", text.font_path.string()},
                          {"text", text.text},
                          {"font_size", text.font_size},
                          {"position", {text.x, text.y}},
                          {"color", color_json(text.color)},
                          {"effects", effects_json(text.effects)},
                          {"effect_rewrite_count", text.effect_rewrite_count},
                          {"blend_mode", blend_mode_name(text.blend_mode)}});
  }

  return nlohmann::json{{"frame", plan.frame().value()},
                        {"surface", {{"width", plan.width()}, {"height", plan.height()}}},
                        {"operations", std::move(operations)}}
      .dump(2);
}

std::string optimized_execution_plan_json(const ExecutionPlan& plan) {
  const auto analysis = analyze_execution_plan(plan);
  nlohmann::json passes = nlohmann::json::array();
  for (const auto& pass : analysis.passes) {
    passes.push_back({{"pass", pass.index},
                      {"kind", render_pass_kind_name(pass.kind)},
                      {"source_id", pass.source_id},
                      {"gpu_fusible", pass.gpu_fusible},
                      {"barrier", barrier_reason_name(pass.barrier)},
                      {"rewrite_count", pass.rewrite_count},
                      {"nodes", pass.nodes},
                      {"resources",
                       {{"reads", pass.read_resources}, {"writes", pass.write_resource}}},
                      {"estimated_bytes_read",
                       static_cast<std::uint64_t>(pass.read_resources.size()) *
                           analysis.surface_bytes},
                      {"estimated_bytes_written", analysis.surface_bytes}});
  }

  nlohmann::json allocations = nlohmann::json::array();
  for (const auto& allocation : analysis.temporary_allocations) {
    allocations.push_back({{"logical_resource", allocation.logical_resource},
                           {"physical_buffer", allocation.physical_buffer},
                           {"first_pass", allocation.first_pass},
                           {"last_pass", allocation.last_pass}});
  }

  nlohmann::json cache_opportunities = nlohmann::json::array();
  std::size_t cache_candidate_count = 0;
  for (const auto& opportunity : analysis.cache_opportunities) {
    if (opportunity.decision == CacheDecision::cache_candidate) {
      ++cache_candidate_count;
    }
    cache_opportunities.push_back(
        {{"source_id", opportunity.source_id},
         {"decision", cache_decision_name(opportunity.decision)},
         {"reason", cache_reason_name(opportunity.reason)},
         {"nodes", opportunity.nodes},
         {"estimated_storage_bytes",
          opportunity.storage_size_known
              ? nlohmann::json(opportunity.estimated_storage_bytes)
              : nlohmann::json(nullptr)}});
  }

  nlohmann::json prepared_resources = nlohmann::json::array();
  for (const auto& resource : analysis.prepared_resources) {
    prepared_resources.push_back(
        {{"kind", prepared_resource_kind_name(resource.kind)},
         {"identity", resource.identity},
         {"source_ids", resource.source_ids},
         {"estimated_storage_bytes",
          resource.storage_size_known ? nlohmann::json(resource.estimated_storage_bytes)
                                      : nlohmann::json(nullptr)}});
  }

  return nlohmann::json{{"frame", plan.frame().value()},
                        {"surface", {{"width", plan.width()}, {"height", plan.height()}}},
                        {"status", "planning_only"},
                        {"summary",
                         {{"pass_count", analysis.passes.size()},
                          {"logical_temporary_surfaces",
                           analysis.logical_temporary_surfaces},
                          {"physical_temporary_buffers",
                           analysis.physical_temporary_buffers},
                          {"surface_bytes", analysis.surface_bytes},
                          {"physical_temporary_bytes",
                           analysis.physical_temporary_bytes},
                          {"estimated_bytes_read", analysis.estimated_bytes_read},
                          {"estimated_bytes_written", analysis.estimated_bytes_written},
                          {"peak_live_bytes", analysis.peak_live_bytes},
                          {"total_rewrite_count", analysis.total_rewrite_count},
                          {"cache_candidate_count", cache_candidate_count},
                          {"prepared_resource_count", analysis.prepared_resources.size()}}},
                        {"passes", std::move(passes)},
                        {"temporary_allocations", std::move(allocations)},
                        {"cache_opportunities", std::move(cache_opportunities)},
                        {"prepared_resources", std::move(prepared_resources)}}
      .dump(2);
}

}  // namespace chroma
