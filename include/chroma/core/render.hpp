#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <vector>

#include "chroma/media/telemetry.hpp"
#include "chroma/core/cache.hpp"

#include "chroma/core/execution_plan.hpp"

namespace chroma {

struct Pixel {
  float red;
  float green;
  float blue;
  float alpha;

  friend bool operator==(const Pixel&, const Pixel&) = default;
};

class Surface {
 public:
  Surface(std::int32_t width, std::int32_t height, Pixel fill = {});

  [[nodiscard]] std::int32_t width() const noexcept { return width_; }
  [[nodiscard]] std::int32_t height() const noexcept { return height_; }
  [[nodiscard]] std::size_t size() const noexcept { return pixels_.size(); }
  [[nodiscard]] const Pixel& pixel(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] Pixel& pixel(std::int32_t x, std::int32_t y);
  [[nodiscard]] const std::vector<Pixel>& pixels() const noexcept { return pixels_; }
  [[nodiscard]] std::vector<Pixel>& pixels() noexcept { return pixels_; }

 private:
  [[nodiscard]] std::size_t offset(std::int32_t x, std::int32_t y) const;

  std::int32_t width_;
  std::int32_t height_;
  std::vector<Pixel> pixels_;
};

enum class CpuExecutionMode {
  fused,
  reference,
  // Preserve video/image fusion, but use the original full-canvas graphics path.
  unbounded_graphics,
};

struct CpuGraphicsStats {
  std::uint64_t bounded_operations{0};
  std::uint64_t full_frame_operations{0};
  std::uint64_t source_pixels{0};
  std::uint64_t transform_pixels{0};
  std::uint64_t composite_pixels{0};

  CpuGraphicsStats& operator+=(const CpuGraphicsStats& other) noexcept {
    bounded_operations += other.bounded_operations;
    full_frame_operations += other.full_frame_operations;
    source_pixels += other.source_pixels;
    transform_pixels += other.transform_pixels;
    composite_pixels += other.composite_pixels;
    return *this;
  }
  friend bool operator==(const CpuGraphicsStats&, const CpuGraphicsStats&) = default;
};

struct CpuExecutionStats {
  CpuGraphicsStats graphics;
  std::size_t fused_operations{0};
  std::size_t reference_operations{0};
  std::size_t full_frame_intermediates_avoided{0};
  std::size_t full_frame_intermediates_materialized{0};
  DecodeTelemetry decode;

  friend bool operator==(const CpuExecutionStats&, const CpuExecutionStats&) = default;
};

class CpuRenderContext {
 public:
  CpuRenderContext();
  explicit CpuRenderContext(CpuCacheConfig config);
  ~CpuRenderContext();

  CpuRenderContext(CpuRenderContext&&) noexcept;
  CpuRenderContext& operator=(CpuRenderContext&&) noexcept;
  CpuRenderContext(const CpuRenderContext&) = delete;
  CpuRenderContext& operator=(const CpuRenderContext&) = delete;

  void set_cache_budgets(CpuCacheConfig config);
  void clear_prepared_caches();
  [[nodiscard]] CpuCacheUsage cache_usage() const noexcept;

  [[nodiscard]] Surface execute(const ExecutionPlan& plan,
                                CpuExecutionMode mode = CpuExecutionMode::fused);
  [[nodiscard]] const CpuExecutionStats& last_execution_stats() const noexcept {
    return last_execution_stats_;
  }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  CpuExecutionStats last_execution_stats_;
};

[[nodiscard]] Pixel composite_over(Pixel source, Pixel destination) noexcept;
[[nodiscard]] Pixel composite(Pixel source, Pixel destination, BlendMode mode) noexcept;
[[nodiscard]] Surface place_surface_cpu(Surface source, std::int32_t width, std::int32_t height,
                                        const SourcePlacement& placement);
[[nodiscard]] Surface apply_effect_program_cpu(Surface source, const EffectProgram& program);
[[nodiscard]] Surface execute_cpu(const ExecutionPlan& plan);
[[nodiscard]] Surface render_frame_cpu(const Composition& composition, FrameIndex frame);
void write_ppm(const Surface& surface, std::ostream& output);

}  // namespace chroma
