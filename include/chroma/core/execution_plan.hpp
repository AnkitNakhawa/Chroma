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

using RenderOperation =
    std::variant<ClearOperation, CompositeSolidOperation, CompositeVideoFrameOperation>;

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

[[nodiscard]] std::string_view render_pass_kind_name(RenderPassKind kind) noexcept;
[[nodiscard]] std::string_view barrier_reason_name(BarrierReason reason) noexcept;

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

struct ExecutionPlanAnalysis {
  std::vector<PlannedRenderPass> passes;
  std::size_t logical_temporary_surfaces{0};
  std::uint64_t surface_bytes{0};
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
