#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "chroma/core/composition.hpp"
#include "chroma/core/effect.hpp"

namespace chroma {

struct ClearOperation {
  Color color;

  friend bool operator==(const ClearOperation&, const ClearOperation&) = default;
};

struct CompositeSolidOperation {
  std::string source_id;
  Color color;
  EffectProgram effects;
  std::size_t effect_rewrite_count{0};
  BlendMode blend_mode{BlendMode::source_over};

  friend bool operator==(const CompositeSolidOperation&, const CompositeSolidOperation&) = default;
};

struct CompositeVideoFrameOperation {
  std::string source_id;
  std::filesystem::path asset_path;
  Rational source_time;
  SourcePlacement placement;
  EffectProgram effects;
  std::size_t effect_rewrite_count{0};
  BlendMode blend_mode{BlendMode::source_over};

  friend bool operator==(const CompositeVideoFrameOperation&,
                         const CompositeVideoFrameOperation&) = default;
};

struct CompositeImageOperation {
  std::string source_id;
  std::filesystem::path asset_path;
  SourcePlacement placement;
  EffectProgram effects;
  std::size_t effect_rewrite_count{0};
  BlendMode blend_mode{BlendMode::source_over};

  friend bool operator==(const CompositeImageOperation&, const CompositeImageOperation&) = default;
};

struct CompositeShapeOperation {
  std::string source_id;
  ShapeGeometry geometry;
  Color fill;
  std::optional<ShapeStroke> stroke;
  EffectProgram effects;
  std::size_t effect_rewrite_count{0};
  BlendMode blend_mode{BlendMode::source_over};

  friend bool operator==(const CompositeShapeOperation&, const CompositeShapeOperation&) = default;
};

struct CompositeTextOperation {
  std::string source_id;
  std::filesystem::path font_path;
  std::string text;
  double font_size;
  double x;
  double y;
  Color color;
  EffectProgram effects;
  std::size_t effect_rewrite_count{0};
  BlendMode blend_mode{BlendMode::source_over};

  friend bool operator==(const CompositeTextOperation&, const CompositeTextOperation&) = default;
};

using RenderOperation = std::variant<ClearOperation, CompositeSolidOperation,
                                     CompositeVideoFrameOperation, CompositeImageOperation,
                                     CompositeShapeOperation, CompositeTextOperation>;

class ExecutionPlan {
 public:
  ExecutionPlan(std::int32_t width, std::int32_t height, FrameIndex frame,
                std::vector<RenderOperation> operations);

  [[nodiscard]] std::int32_t width() const noexcept { return width_; }
  [[nodiscard]] std::int32_t height() const noexcept { return height_; }
  [[nodiscard]] FrameIndex frame() const noexcept { return frame_; }
  [[nodiscard]] const std::vector<RenderOperation>& operations() const noexcept {
    return operations_;
  }

  friend bool operator==(const ExecutionPlan&, const ExecutionPlan&) = default;

 private:
  std::int32_t width_;
  std::int32_t height_;
  FrameIndex frame_;
  std::vector<RenderOperation> operations_;
};

enum class RenderPassKind {
  clear,
  fusion_candidate,
  barrier,
};

enum class BarrierReason {
  none,
  surface_initialization,
  neighborhood_effect,
  temporal_effect,
  external_effect,
};

enum class CacheDecision {
  not_cacheable,
  recompute,
  cache_candidate,
};

enum class CacheReason {
  frame_variant_source,
  temporal_dependency,
  nondeterministic_effect,
  cheap_static_subgraph,
  expensive_static_subgraph,
  static_resource,
};

[[nodiscard]] std::string_view render_pass_kind_name(RenderPassKind kind) noexcept;
[[nodiscard]] std::string_view barrier_reason_name(BarrierReason reason) noexcept;
[[nodiscard]] std::string_view cache_decision_name(CacheDecision decision) noexcept;
[[nodiscard]] std::string_view cache_reason_name(CacheReason reason) noexcept;

struct PlannedRenderPass {
  std::size_t index;
  RenderPassKind kind;
  BarrierReason barrier{BarrierReason::none};
  std::string source_id;
  bool gpu_fusible;
  std::size_t rewrite_count{0};
  std::vector<std::string> nodes;
  std::vector<std::string> read_resources;
  std::string write_resource;

  friend bool operator==(const PlannedRenderPass&, const PlannedRenderPass&) = default;
};

struct TemporaryAllocation {
  std::string logical_resource;
  std::size_t physical_buffer;
  std::size_t first_pass;
  std::size_t last_pass;

  friend bool operator==(const TemporaryAllocation&, const TemporaryAllocation&) = default;
};

struct CacheOpportunity {
  std::string source_id;
  CacheDecision decision;
  CacheReason reason;
  std::vector<std::string> nodes;
  std::uint64_t estimated_storage_bytes;

  friend bool operator==(const CacheOpportunity&, const CacheOpportunity&) = default;
};

struct ExecutionPlanAnalysis {
  std::vector<PlannedRenderPass> passes;
  std::vector<TemporaryAllocation> temporary_allocations;
  std::vector<CacheOpportunity> cache_opportunities;
  std::size_t logical_temporary_surfaces{0};
  std::size_t physical_temporary_buffers{0};
  std::uint64_t surface_bytes{0};
  std::uint64_t physical_temporary_bytes{0};
  std::uint64_t estimated_bytes_read{0};
  std::uint64_t estimated_bytes_written{0};
  std::uint64_t peak_live_bytes{0};
  std::size_t total_rewrite_count{0};

  friend bool operator==(const ExecutionPlanAnalysis&, const ExecutionPlanAnalysis&) = default;
};

[[nodiscard]] ExecutionPlan compile_frame(const Composition& composition, FrameIndex frame);
[[nodiscard]] ExecutionPlanAnalysis analyze_execution_plan(const ExecutionPlan& plan);
[[nodiscard]] std::string execution_plan_json(const ExecutionPlan& plan);
[[nodiscard]] std::string optimized_execution_plan_json(const ExecutionPlan& plan);

}  // namespace chroma
