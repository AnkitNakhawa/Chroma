#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
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
  BlendMode blend_mode{BlendMode::source_over};

  friend bool operator==(const CompositeSolidOperation&, const CompositeSolidOperation&) = default;
};

struct CompositeVideoFrameOperation {
  std::string source_id;
  std::filesystem::path asset_path;
  Rational source_time;
  EffectProgram effects;
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

[[nodiscard]] ExecutionPlan compile_frame(const Composition& composition, FrameIndex frame);
[[nodiscard]] std::string execution_plan_json(const ExecutionPlan& plan);
[[nodiscard]] std::string optimized_execution_plan_json(const ExecutionPlan& plan);

}  // namespace chroma
