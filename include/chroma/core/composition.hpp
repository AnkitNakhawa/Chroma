#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

#include "chroma/core/effect.hpp"
#include "chroma/core/timeline.hpp"

namespace chroma {

inline constexpr std::uint64_t max_canvas_pixels = 67'108'864;

struct Diagnostic {
  std::string code;
  std::string path;
  std::string message;

  friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

struct Color {
  double red;
  double green;
  double blue;
  double alpha;

  friend bool operator==(const Color&, const Color&) = default;
};

enum class BlendMode {
  source_over,
  multiply,
  screen,
  plus,
};

[[nodiscard]] std::string_view blend_mode_name(BlendMode mode) noexcept;

enum class FitMode {
  none,
  contain,
  cover,
  fill,
};

[[nodiscard]] std::string_view fit_mode_name(FitMode mode) noexcept;

struct SourcePlacement {
  FitMode fit{FitMode::none};
  double position_x{0.5};
  double position_y{0.5};

  friend bool operator==(const SourcePlacement&, const SourcePlacement&) = default;
};

struct SolidLayer {
  std::string id;
  FrameRange range;
  Color color;
  EffectProgram effects;
  BlendMode blend_mode{BlendMode::source_over};
};

struct VideoAsset {
  std::string id;
  std::filesystem::path path;
};

struct ImageAsset {
  std::string id;
  std::filesystem::path path;
};

struct FontAsset {
  std::string id;
  std::filesystem::path path;
};

struct VideoLayer {
  std::string id;
  FrameRange range;
  std::string asset_id;
  FrameIndex source_start;
  double opacity;
  SourcePlacement placement;
  EffectProgram effects;
  BlendMode blend_mode{BlendMode::source_over};
};

struct ImageLayer {
  std::string id;
  FrameRange range;
  std::string asset_id;
  SourcePlacement placement;
  EffectProgram effects;
  BlendMode blend_mode{BlendMode::source_over};
};

enum class ShapeKind {
  rectangle,
  rounded_rectangle,
  ellipse,
};

[[nodiscard]] std::string_view shape_kind_name(ShapeKind kind) noexcept;

struct ShapeGeometry {
  ShapeKind kind{ShapeKind::rectangle};
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};
  double corner_radius{0.0};

  friend bool operator==(const ShapeGeometry&, const ShapeGeometry&) = default;
};

struct ShapeStroke {
  Color color;
  double width;

  friend bool operator==(const ShapeStroke&, const ShapeStroke&) = default;
};

struct ShapeLayer {
  std::string id;
  FrameRange range;
  ShapeGeometry geometry;
  Color fill;
  std::optional<ShapeStroke> stroke;
  EffectProgram effects;
  BlendMode blend_mode{BlendMode::source_over};
};

struct TextLayer {
  std::string id;
  FrameRange range;
  std::string font_asset_id;
  std::string text;
  double font_size;
  double x;
  double y;
  Color color;
  EffectProgram effects;
  BlendMode blend_mode{BlendMode::source_over};
};

using CompositionLayer =
    std::variant<SolidLayer, VideoLayer, ImageLayer, ShapeLayer, TextLayer>;

class Composition {
 public:
  Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
              FrameRate frame_rate, FrameRange range, std::vector<SolidLayer> layers);
  Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
              FrameRate frame_rate, FrameRange range, std::vector<VideoAsset> assets,
              std::vector<CompositionLayer> layers);
  Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
              FrameRate frame_rate, FrameRange range, std::vector<VideoAsset> video_assets,
              std::vector<ImageAsset> image_assets, std::vector<CompositionLayer> layers);
  Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
              FrameRate frame_rate, FrameRange range, std::vector<VideoAsset> video_assets,
              std::vector<ImageAsset> image_assets, std::vector<FontAsset> font_assets,
              std::vector<CompositionLayer> layers);

  [[nodiscard]] std::int32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] std::int32_t width() const noexcept { return width_; }
  [[nodiscard]] std::int32_t height() const noexcept { return height_; }
  [[nodiscard]] const FrameRate& frame_rate() const noexcept { return frame_rate_; }
  [[nodiscard]] const FrameRange& range() const noexcept { return range_; }
  [[nodiscard]] const std::vector<VideoAsset>& assets() const noexcept { return assets_; }
  [[nodiscard]] const std::vector<ImageAsset>& image_assets() const noexcept {
    return image_assets_;
  }
  [[nodiscard]] const std::vector<FontAsset>& font_assets() const noexcept {
    return font_assets_;
  }
  [[nodiscard]] const std::vector<CompositionLayer>& layers() const noexcept { return layers_; }
  [[nodiscard]] const VideoAsset* find_video_asset(std::string_view id) const noexcept;
  [[nodiscard]] const ImageAsset* find_image_asset(std::string_view id) const noexcept;
  [[nodiscard]] const FontAsset* find_font_asset(std::string_view id) const noexcept;

 private:
  std::int32_t schema_version_;
  std::int32_t width_;
  std::int32_t height_;
  FrameRate frame_rate_;
  FrameRange range_;
  std::vector<VideoAsset> assets_;
  std::vector<ImageAsset> image_assets_;
  std::vector<FontAsset> font_assets_;
  std::vector<CompositionLayer> layers_;
};

struct CompositionParseResult {
  std::optional<Composition> composition;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool valid() const noexcept {
    return composition.has_value() && diagnostics.empty();
  }
};

[[nodiscard]] CompositionParseResult parse_composition(
    std::string_view source, const std::filesystem::path& base_directory = {});
[[nodiscard]] std::string diagnostics_json(const std::vector<Diagnostic>& diagnostics);

}  // namespace chroma
