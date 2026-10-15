#include "chroma/core/composition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

namespace chroma {
namespace {

using Json = nlohmann::json;

void add_diagnostic(std::vector<Diagnostic>& diagnostics, std::string code, std::string path,
                    std::string message) {
  diagnostics.push_back(
      Diagnostic{.code = std::move(code), .path = std::move(path), .message = std::move(message)});
}

const Json* required_member(const Json& object, std::string_view key, std::string_view path,
                            std::vector<Diagnostic>& diagnostics) {
  const auto iterator = object.find(key);
  if (iterator == object.end()) {
    add_diagnostic(diagnostics, "schema.missing", std::string(path) + "/" + std::string(key),
                   "required field is missing");
    return nullptr;
  }
  return &*iterator;
}

const Json* optional_member(const Json& object, std::string_view key) {
  const auto iterator = object.find(key);
  return iterator == object.end() ? nullptr : &*iterator;
}

std::optional<std::int64_t> integer_value(const Json* value, std::string_view path,
                                          std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (!value->is_number_integer() && !value->is_number_unsigned()) {
    add_diagnostic(diagnostics, "schema.type", std::string(path), "expected an integer");
    return std::nullopt;
  }

  if (value->is_number_unsigned()) {
    const auto number = value->get<std::uint64_t>();
    if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      add_diagnostic(diagnostics, "schema.range", std::string(path),
                     "integer exceeds the supported range");
      return std::nullopt;
    }
    return static_cast<std::int64_t>(number);
  }
  return value->get<std::int64_t>();
}

std::optional<std::string> string_value(const Json* value, std::string_view path,
                                        std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (!value->is_string()) {
    add_diagnostic(diagnostics, "schema.type", std::string(path), "expected a string");
    return std::nullopt;
  }
  return value->get<std::string>();
}

std::optional<double> number_value(const Json* value, std::string_view path,
                                   std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (!value->is_number()) {
    add_diagnostic(diagnostics, "schema.type", std::string(path), "expected a number");
    return std::nullopt;
  }
  const auto number = value->get<double>();
  if (!std::isfinite(number)) {
    add_diagnostic(diagnostics, "schema.range", std::string(path), "number must be finite");
    return std::nullopt;
  }
  return number;
}

std::vector<CompositionLayer> wrap_solid_layers(std::vector<SolidLayer> layers) {
  std::vector<CompositionLayer> output;
  output.reserve(layers.size());
  for (auto& layer : layers) {
    output.emplace_back(std::move(layer));
  }
  return output;
}

std::optional<Color> parse_color(const Json* value, std::string_view path,
                                 std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (!value->is_array() || value->size() != 4) {
    add_diagnostic(diagnostics, "schema.color", std::string(path),
                   "expected four numeric RGBA components");
    return std::nullopt;
  }

  Color color{};
  double* components[] = {&color.red, &color.green, &color.blue, &color.alpha};
  for (std::size_t index = 0; index < value->size(); ++index) {
    const auto& component = (*value)[index];
    const auto component_path = std::string(path) + "/" + std::to_string(index);
    if (!component.is_number()) {
      add_diagnostic(diagnostics, "schema.type", component_path, "expected a number");
      return std::nullopt;
    }
    const auto number = component.get<double>();
    if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
      add_diagnostic(diagnostics, "schema.range", component_path,
                     "color component must be between 0 and 1");
      return std::nullopt;
    }
    *components[index] = number;
  }
  return color;
}

bool valid_blend_mode(BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::source_over:
    case BlendMode::multiply:
    case BlendMode::screen:
    case BlendMode::plus:
      return true;
  }
  return false;
}

bool valid_fit_mode(FitMode mode) noexcept {
  switch (mode) {
    case FitMode::none:
    case FitMode::contain:
    case FitMode::cover:
    case FitMode::fill:
      return true;
  }
  return false;
}

bool valid_shape_kind(ShapeKind kind) noexcept {
  switch (kind) {
    case ShapeKind::rectangle:
    case ShapeKind::rounded_rectangle:
    case ShapeKind::ellipse: return true;
  }
  return false;
}

std::optional<BlendMode> parse_blend_mode(const Json* value, std::string_view path,
                                          std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return BlendMode::source_over;
  }
  const auto name = string_value(value, path, diagnostics);
  if (!name) {
    return std::nullopt;
  }
  if (*name == "source_over") return BlendMode::source_over;
  if (*name == "multiply") return BlendMode::multiply;
  if (*name == "screen") return BlendMode::screen;
  if (*name == "plus") return BlendMode::plus;
  add_diagnostic(diagnostics, "layer.unsupported_blend_mode", std::string(path),
                 "supported blend modes are source_over, multiply, screen, and plus");
  return std::nullopt;
}

std::optional<FitMode> parse_fit_mode(const Json* value, std::string_view path,
                                      std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return FitMode::none;
  }
  const auto name = string_value(value, path, diagnostics);
  if (!name) {
    return std::nullopt;
  }
  if (*name == "none") return FitMode::none;
  if (*name == "contain") return FitMode::contain;
  if (*name == "cover") return FitMode::cover;
  if (*name == "fill") return FitMode::fill;
  add_diagnostic(diagnostics, "layer.unsupported_fit", std::string(path),
                 "supported fit modes are none, contain, cover, and fill");
  return std::nullopt;
}

std::optional<ShapeKind> parse_shape_kind(const Json* value, std::string_view path,
                                          std::vector<Diagnostic>& diagnostics) {
  const auto name = string_value(value, path, diagnostics);
  if (!name) {
    return std::nullopt;
  }
  if (*name == "rectangle") return ShapeKind::rectangle;
  if (*name == "rounded_rectangle") return ShapeKind::rounded_rectangle;
  if (*name == "ellipse") return ShapeKind::ellipse;
  add_diagnostic(diagnostics, "layer.unsupported_shape", std::string(path),
                 "supported shapes are rectangle, rounded_rectangle, and ellipse");
  return std::nullopt;
}

std::optional<std::optional<ShapeStroke>> parse_shape_stroke(
    const Json* value, std::string_view path, std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return std::optional<ShapeStroke>{};
  }
  if (!value->is_object()) {
    add_diagnostic(diagnostics, "schema.type", std::string(path), "expected an object");
    return std::nullopt;
  }
  const auto color = parse_color(required_member(*value, "color", path, diagnostics),
                                 std::string(path) + "/color", diagnostics);
  const auto width = number_value(required_member(*value, "width", path, diagnostics),
                                  std::string(path) + "/width", diagnostics);
  if (width && *width <= 0.0) {
    add_diagnostic(diagnostics, "layer.invalid_stroke", std::string(path) + "/width",
                   "stroke width must be positive");
  }
  if (!color || !width || *width <= 0.0) {
    return std::nullopt;
  }
  return std::optional<ShapeStroke>(ShapeStroke{.color = *color, .width = *width});
}

std::optional<SourcePlacement> parse_placement(const Json& layer, std::string_view path,
                                               std::vector<Diagnostic>& diagnostics) {
  auto fit = parse_fit_mode(optional_member(layer, "fit"), std::string(path) + "/fit",
                            diagnostics);
  SourcePlacement placement;
  if (fit) {
    placement.fit = *fit;
  }
  const auto* position = optional_member(layer, "position");
  if (position == nullptr) {
    return fit ? std::optional<SourcePlacement>(placement) : std::nullopt;
  }
  if (!position->is_array() || position->size() != 2) {
    add_diagnostic(diagnostics, "layer.invalid_position", std::string(path) + "/position",
                   "position must contain exactly two normalized numbers");
    return std::nullopt;
  }
  const auto x = number_value(&(*position)[0], std::string(path) + "/position/0", diagnostics);
  const auto y = number_value(&(*position)[1], std::string(path) + "/position/1", diagnostics);
  if (!x || !y) {
    return std::nullopt;
  }
  if (*x < 0.0 || *x > 1.0 || *y < 0.0 || *y > 1.0) {
    add_diagnostic(diagnostics, "layer.invalid_position", std::string(path) + "/position",
                   "position values must be between 0 and 1");
    return std::nullopt;
  }
  placement.position_x = *x;
  placement.position_y = *y;
  return fit ? std::optional<SourcePlacement>(placement) : std::nullopt;
}

template <std::size_t Size>
std::optional<std::array<double, Size>> parse_number_array(
    const Json* value, std::string_view path, std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return std::nullopt;
  }
  if (!value->is_array() || value->size() != Size) {
    add_diagnostic(diagnostics, "effect.invalid_array", std::string(path),
                   "expected exactly " + std::to_string(Size) + " numeric values");
    return std::nullopt;
  }
  std::array<double, Size> output{};
  for (std::size_t index = 0; index < Size; ++index) {
    const auto item_path = std::string(path) + "/" + std::to_string(index);
    const auto number = number_value(&(*value)[index], item_path, diagnostics);
    if (!number) {
      return std::nullopt;
    }
    output[index] = *number;
  }
  return output;
}

std::optional<std::int32_t> effect_integer(const Json* value, std::string_view path,
                                           std::vector<Diagnostic>& diagnostics) {
  const auto integer = integer_value(value, path, diagnostics);
  if (!integer) {
    return std::nullopt;
  }
  if (*integer < std::numeric_limits<std::int32_t>::min() ||
      *integer > std::numeric_limits<std::int32_t>::max()) {
    add_diagnostic(diagnostics, "effect.integer_range", std::string(path),
                   "effect integer is outside the supported 32-bit range");
    return std::nullopt;
  }
  return static_cast<std::int32_t>(*integer);
}

std::optional<EffectProgram> parse_effects(const Json* value, std::string_view path,
                                           std::vector<Diagnostic>& diagnostics) {
  if (value == nullptr) {
    return EffectProgram{};
  }
  if (!value->is_array()) {
    add_diagnostic(diagnostics, "schema.type", std::string(path), "expected an array");
    return std::nullopt;
  }
  if (value->size() > 64) {
    add_diagnostic(diagnostics, "effect.too_many", std::string(path),
                   "a layer may contain at most 64 effects");
    return std::nullopt;
  }

  std::vector<Effect> effects;
  effects.reserve(value->size());
  bool valid = true;
  for (std::size_t index = 0; index < value->size(); ++index) {
    const auto& effect = (*value)[index];
    const auto base = std::string(path) + "/" + std::to_string(index);
    if (!effect.is_object()) {
      add_diagnostic(diagnostics, "schema.type", base, "expected an object");
      valid = false;
      continue;
    }
    const auto type = string_value(required_member(effect, "type", base, diagnostics),
                                   base + "/type", diagnostics);
    if (!type) {
      valid = false;
      continue;
    }
    if (*type == "transform_2d") {
      const auto matrix = parse_number_array<6>(
          required_member(effect, "matrix", base, diagnostics), base + "/matrix", diagnostics);
      if (matrix) {
        effects.emplace_back(Transform2DEffect{.m00 = (*matrix)[0],
                                               .m01 = (*matrix)[1],
                                               .m02 = (*matrix)[2],
                                               .m10 = (*matrix)[3],
                                               .m11 = (*matrix)[4],
                                               .m12 = (*matrix)[5]});
      } else {
        valid = false;
      }
      continue;
    }
    if (*type == "crop") {
      const auto x = effect_integer(required_member(effect, "x", base, diagnostics),
                                    base + "/x", diagnostics);
      const auto y = effect_integer(required_member(effect, "y", base, diagnostics),
                                    base + "/y", diagnostics);
      const auto width = effect_integer(required_member(effect, "width", base, diagnostics),
                                        base + "/width", diagnostics);
      const auto height = effect_integer(required_member(effect, "height", base, diagnostics),
                                         base + "/height", diagnostics);
      if (x && y && width && height && *width > 0 && *height > 0) {
        effects.emplace_back(CropEffect{.x = *x, .y = *y, .width = *width, .height = *height});
      } else {
        if (width && height && (*width <= 0 || *height <= 0)) {
          add_diagnostic(diagnostics, "effect.invalid_crop", base,
                         "crop width and height must be positive");
        }
        valid = false;
      }
      continue;
    }
    if (*type == "opacity") {
      const auto opacity = number_value(required_member(effect, "value", base, diagnostics),
                                        base + "/value", diagnostics);
      if (opacity && *opacity >= 0.0 && *opacity <= 1.0) {
        effects.emplace_back(OpacityEffect{.opacity = *opacity});
      } else {
        if (opacity) {
          add_diagnostic(diagnostics, "effect.invalid_opacity", base + "/value",
                         "opacity must be between 0 and 1");
        }
        valid = false;
      }
      continue;
    }
    if (*type == "color_matrix") {
      const auto matrix = parse_number_array<20>(
          required_member(effect, "matrix", base, diagnostics), base + "/matrix", diagnostics);
      if (matrix) {
        effects.emplace_back(ColorMatrixEffect{.values = *matrix});
      } else {
        valid = false;
      }
      continue;
    }
    add_diagnostic(diagnostics, "effect.unsupported_type", base + "/type",
                   "supported effects are transform_2d, crop, opacity, and color_matrix");
    valid = false;
  }

  if (!valid) {
    return std::nullopt;
  }
  return EffectProgram(std::move(effects));
}

}  // namespace

std::string_view blend_mode_name(BlendMode mode) noexcept {
  switch (mode) {
    case BlendMode::source_over: return "source_over";
    case BlendMode::multiply: return "multiply";
    case BlendMode::screen: return "screen";
    case BlendMode::plus: return "plus";
  }
  return "unknown";
}

std::string_view fit_mode_name(FitMode mode) noexcept {
  switch (mode) {
    case FitMode::none: return "none";
    case FitMode::contain: return "contain";
    case FitMode::cover: return "cover";
    case FitMode::fill: return "fill";
  }
  return "unknown";
}

std::string_view shape_kind_name(ShapeKind kind) noexcept {
  switch (kind) {
    case ShapeKind::rectangle: return "rectangle";
    case ShapeKind::rounded_rectangle: return "rounded_rectangle";
    case ShapeKind::ellipse: return "ellipse";
  }
  return "unknown";
}

Composition::Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
                         FrameRate frame_rate, FrameRange range,
                         std::vector<SolidLayer> layers)
    : Composition(schema_version, width, height, std::move(frame_rate), range, {},
                  wrap_solid_layers(std::move(layers))) {}

Composition::Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
                         FrameRate frame_rate, FrameRange range,
                         std::vector<VideoAsset> assets, std::vector<CompositionLayer> layers)
    : Composition(schema_version, width, height, std::move(frame_rate), range,
                  std::move(assets), {}, std::move(layers)) {}

Composition::Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
                         FrameRate frame_rate, FrameRange range,
                         std::vector<VideoAsset> video_assets,
                         std::vector<ImageAsset> image_assets,
                         std::vector<CompositionLayer> layers)
    : Composition(schema_version, width, height, std::move(frame_rate), range,
                  std::move(video_assets), std::move(image_assets), {}, std::move(layers)) {}

Composition::Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
                         FrameRate frame_rate, FrameRange range,
                         std::vector<VideoAsset> video_assets,
                         std::vector<ImageAsset> image_assets,
                         std::vector<FontAsset> font_assets,
                         std::vector<CompositionLayer> layers)
    : schema_version_(schema_version),
      width_(width),
      height_(height),
      frame_rate_(std::move(frame_rate)),
      range_(range),
      assets_(std::move(video_assets)),
      image_assets_(std::move(image_assets)),
      font_assets_(std::move(font_assets)),
      layers_(std::move(layers)) {
  if (schema_version_ != 1) {
    throw std::invalid_argument("unsupported composition schema version");
  }
  if (width_ <= 0 || height_ <= 0 || width_ > 32768 || height_ > 32768 ||
      static_cast<std::uint64_t>(width_) * static_cast<std::uint64_t>(height_) >
          max_canvas_pixels) {
    throw std::invalid_argument("composition canvas dimensions are invalid or too large");
  }
  if (range_.empty()) {
    throw std::invalid_argument("composition range must not be empty");
  }

  std::unordered_set<std::string> asset_ids;
  for (const auto& asset : assets_) {
    if (asset.id.empty() || asset.id.size() > 128 || !asset_ids.insert(asset.id).second ||
        asset.path.empty()) {
      throw std::invalid_argument("composition video assets must have unique ids and paths");
    }
  }
  for (const auto& asset : image_assets_) {
    if (asset.id.empty() || asset.id.size() > 128 || !asset_ids.insert(asset.id).second ||
        asset.path.empty()) {
      throw std::invalid_argument("composition assets must have unique ids and paths");
    }
  }
  for (const auto& asset : font_assets_) {
    if (asset.id.empty() || asset.id.size() > 128 || !asset_ids.insert(asset.id).second ||
        asset.path.empty()) {
      throw std::invalid_argument("composition assets must have unique ids and paths");
    }
  }

  std::unordered_set<std::string> layer_ids;
  for (const auto& layer_variant : layers_) {
    const auto& id = std::visit([](const auto& layer) -> const std::string& { return layer.id; },
                                layer_variant);
    const auto& layer_range =
        std::visit([](const auto& layer) -> const FrameRange& { return layer.range; },
                   layer_variant);
    if (id.empty() || id.size() > 128 || !layer_ids.insert(id).second) {
      throw std::invalid_argument("composition layer ids must be non-empty and unique");
    }
    if (layer_range.empty() || layer_range.start() < range_.start() ||
        layer_range.end() > range_.end()) {
      throw std::invalid_argument("composition layer range is outside the composition");
    }

    if (const auto* solid = std::get_if<SolidLayer>(&layer_variant)) {
      const double components[] = {solid->color.red, solid->color.green, solid->color.blue,
                                   solid->color.alpha};
      for (const auto component : components) {
        if (!std::isfinite(component) || component < 0.0 || component > 1.0) {
          throw std::invalid_argument(
              "composition layer color components must be between 0 and 1");
        }
      }
      if (!valid_blend_mode(solid->blend_mode)) {
        throw std::invalid_argument("composition solid layer blend mode is invalid");
      }
      continue;
    }

    const auto validate_placement = [](const SourcePlacement& placement) {
      return valid_fit_mode(placement.fit) && std::isfinite(placement.position_x) &&
             std::isfinite(placement.position_y) && placement.position_x >= 0.0 &&
             placement.position_x <= 1.0 && placement.position_y >= 0.0 &&
             placement.position_y <= 1.0;
    };
    if (const auto* video = std::get_if<VideoLayer>(&layer_variant)) {
      if (find_video_asset(video->asset_id) == nullptr) {
        throw std::invalid_argument("composition video layer references an unknown asset");
      }
      if (!std::isfinite(video->opacity) || video->opacity < 0.0 || video->opacity > 1.0) {
        throw std::invalid_argument("composition video opacity must be between 0 and 1");
      }
      if (!validate_placement(video->placement)) {
        throw std::invalid_argument("composition video placement is invalid");
      }
      if (!valid_blend_mode(video->blend_mode)) {
        throw std::invalid_argument("composition video layer blend mode is invalid");
      }
      continue;
    }

    if (const auto* image = std::get_if<ImageLayer>(&layer_variant)) {
      if (find_image_asset(image->asset_id) == nullptr) {
        throw std::invalid_argument("composition image layer references an unknown asset");
      }
      if (!validate_placement(image->placement)) {
        throw std::invalid_argument("composition image placement is invalid");
      }
      if (!valid_blend_mode(image->blend_mode)) {
        throw std::invalid_argument("composition image layer blend mode is invalid");
      }
      continue;
    }

    const auto valid_color = [](const Color& color) {
      const double components[] = {color.red, color.green, color.blue, color.alpha};
      return std::all_of(std::begin(components), std::end(components), [](double component) {
        return std::isfinite(component) && component >= 0.0 && component <= 1.0;
      });
    };
    if (const auto* shape = std::get_if<ShapeLayer>(&layer_variant)) {
      const auto& geometry = shape->geometry;
      if (!valid_shape_kind(geometry.kind) || !std::isfinite(geometry.x) ||
          !std::isfinite(geometry.y) || !std::isfinite(geometry.width) ||
          !std::isfinite(geometry.height) || geometry.width <= 0.0 || geometry.height <= 0.0 ||
          !std::isfinite(geometry.corner_radius) || geometry.corner_radius < 0.0) {
        throw std::invalid_argument("composition shape geometry is invalid");
      }
      if (!valid_color(shape->fill) ||
          (shape->stroke && (!valid_color(shape->stroke->color) ||
                             !std::isfinite(shape->stroke->width) ||
                             shape->stroke->width <= 0.0))) {
        throw std::invalid_argument("composition shape paint is invalid");
      }
      if (!valid_blend_mode(shape->blend_mode)) {
        throw std::invalid_argument("composition shape layer blend mode is invalid");
      }
      continue;
    }

    const auto& text = std::get<TextLayer>(layer_variant);
    if (find_font_asset(text.font_asset_id) == nullptr || text.text.empty() ||
        text.text.size() > 16'384 || !std::isfinite(text.font_size) || text.font_size <= 0.0 ||
        text.font_size > 4096.0 || !std::isfinite(text.x) || !std::isfinite(text.y) ||
        !valid_color(text.color) || !valid_blend_mode(text.blend_mode)) {
      throw std::invalid_argument("composition text layer is invalid");
    }
  }
}

const VideoAsset* Composition::find_video_asset(std::string_view id) const noexcept {
  for (const auto& asset : assets_) {
    if (asset.id == id) {
      return &asset;
    }
  }
  return nullptr;
}

const ImageAsset* Composition::find_image_asset(std::string_view id) const noexcept {
  for (const auto& asset : image_assets_) {
    if (asset.id == id) {
      return &asset;
    }
  }
  return nullptr;
}

const FontAsset* Composition::find_font_asset(std::string_view id) const noexcept {
  for (const auto& asset : font_assets_) {
    if (asset.id == id) {
      return &asset;
    }
  }
  return nullptr;
}

CompositionParseResult parse_composition(std::string_view source,
                                         const std::filesystem::path& base_directory) {
  CompositionParseResult result;
  const auto document = Json::parse(source, nullptr, false);
  if (document.is_discarded()) {
    add_diagnostic(result.diagnostics, "json.invalid", "", "input is not valid JSON");
    return result;
  }
  if (!document.is_object()) {
    add_diagnostic(result.diagnostics, "schema.root", "", "composition root must be an object");
    return result;
  }

  const auto schema_version = integer_value(
      required_member(document, "schema_version", "", result.diagnostics), "/schema_version",
      result.diagnostics);
  if (schema_version && *schema_version != 1) {
    add_diagnostic(result.diagnostics, "schema.unsupported_version", "/schema_version",
                   "only experimental schema version 1 is supported");
  }

  const Json* canvas = required_member(document, "canvas", "", result.diagnostics);
  if (canvas != nullptr && !canvas->is_object()) {
    add_diagnostic(result.diagnostics, "schema.type", "/canvas", "expected an object");
    canvas = nullptr;
  }

  std::optional<std::int64_t> width;
  std::optional<std::int64_t> height;
  std::optional<std::int64_t> duration;
  std::optional<std::int64_t> rate_numerator;
  std::optional<std::int64_t> rate_denominator;

  if (canvas != nullptr) {
    width = integer_value(required_member(*canvas, "width", "/canvas", result.diagnostics),
                          "/canvas/width", result.diagnostics);
    height = integer_value(required_member(*canvas, "height", "/canvas", result.diagnostics),
                           "/canvas/height", result.diagnostics);
    duration = integer_value(
        required_member(*canvas, "duration_frames", "/canvas", result.diagnostics),
        "/canvas/duration_frames", result.diagnostics);

    const Json* frame_rate =
        required_member(*canvas, "frame_rate", "/canvas", result.diagnostics);
    if (frame_rate != nullptr && !frame_rate->is_object()) {
      add_diagnostic(result.diagnostics, "schema.type", "/canvas/frame_rate",
                     "expected an object");
    } else if (frame_rate != nullptr) {
      rate_numerator = integer_value(
          required_member(*frame_rate, "numerator", "/canvas/frame_rate", result.diagnostics),
          "/canvas/frame_rate/numerator", result.diagnostics);
      rate_denominator = integer_value(
          required_member(*frame_rate, "denominator", "/canvas/frame_rate", result.diagnostics),
          "/canvas/frame_rate/denominator", result.diagnostics);
    }
  }

  if (width && (*width <= 0 || *width > 32768)) {
    add_diagnostic(result.diagnostics, "canvas.invalid_width", "/canvas/width",
                   "width must be between 1 and 32768 pixels");
  }
  if (height && (*height <= 0 || *height > 32768)) {
    add_diagnostic(result.diagnostics, "canvas.invalid_height", "/canvas/height",
                   "height must be between 1 and 32768 pixels");
  }
  if (width && height && *width > 0 && *height > 0 &&
      static_cast<std::uint64_t>(*width) * static_cast<std::uint64_t>(*height) >
          max_canvas_pixels) {
    add_diagnostic(result.diagnostics, "canvas.too_large", "/canvas",
                   "canvas exceeds the initial CPU pixel budget");
  }
  if (duration && *duration <= 0) {
    add_diagnostic(result.diagnostics, "timeline.invalid_duration", "/canvas/duration_frames",
                   "duration must contain at least one frame");
  }
  if (rate_numerator && *rate_numerator <= 0) {
    add_diagnostic(result.diagnostics, "timeline.invalid_frame_rate",
                   "/canvas/frame_rate/numerator", "frame-rate numerator must be positive");
  }
  if (rate_denominator && *rate_denominator <= 0) {
    add_diagnostic(result.diagnostics, "timeline.invalid_frame_rate",
                   "/canvas/frame_rate/denominator", "frame-rate denominator must be positive");
  }

  std::vector<VideoAsset> assets;
  std::vector<ImageAsset> image_assets;
  std::vector<FontAsset> font_assets;
  std::unordered_set<std::string> asset_ids;
  std::unordered_set<std::string> video_asset_ids;
  std::unordered_set<std::string> image_asset_ids;
  std::unordered_set<std::string> font_asset_ids;
  const Json* asset_array = optional_member(document, "assets");
  if (asset_array != nullptr && !asset_array->is_array()) {
    add_diagnostic(result.diagnostics, "schema.type", "/assets", "expected an array");
  } else if (asset_array != nullptr) {
    for (std::size_t index = 0; index < asset_array->size(); ++index) {
      const auto& asset = (*asset_array)[index];
      const auto base = std::string("/assets/") + std::to_string(index);
      if (!asset.is_object()) {
        add_diagnostic(result.diagnostics, "schema.type", base, "expected an object");
        continue;
      }
      auto id = string_value(required_member(asset, "id", base, result.diagnostics), base + "/id",
                             result.diagnostics);
      auto type = string_value(required_member(asset, "type", base, result.diagnostics),
                               base + "/type", result.diagnostics);
      auto path = string_value(required_member(asset, "path", base, result.diagnostics),
                               base + "/path", result.diagnostics);
      if (id && (id->empty() || id->size() > 128)) {
        add_diagnostic(result.diagnostics, "asset.invalid_id", base + "/id",
                       "asset id must contain between 1 and 128 characters");
      } else if (id && !asset_ids.insert(*id).second) {
        add_diagnostic(result.diagnostics, "asset.duplicate_id", base + "/id",
                       "asset id must be unique");
      }
      if (type && *type != "video" && *type != "image" && *type != "font") {
        add_diagnostic(result.diagnostics, "asset.unsupported_type", base + "/type",
                       "supported asset types are video, image, and font");
      }
      if (path && path->empty()) {
        add_diagnostic(result.diagnostics, "asset.invalid_path", base + "/path",
                       "asset path must not be empty");
      }
      if (id && type && path && !id->empty() && id->size() <= 128 &&
          (*type == "video" || *type == "image" || *type == "font") && !path->empty()) {
        auto resolved_path = std::filesystem::path(*path);
        if (resolved_path.is_relative() && !base_directory.empty()) {
          resolved_path = base_directory / resolved_path;
        }
        if (*type == "video") {
          video_asset_ids.insert(*id);
          assets.push_back(
              VideoAsset{.id = std::move(*id), .path = resolved_path.lexically_normal()});
        } else if (*type == "image") {
          image_asset_ids.insert(*id);
          image_assets.push_back(
              ImageAsset{.id = std::move(*id), .path = resolved_path.lexically_normal()});
        } else {
          font_asset_ids.insert(*id);
          font_assets.push_back(
              FontAsset{.id = std::move(*id), .path = resolved_path.lexically_normal()});
        }
      }
    }
  }

  std::vector<CompositionLayer> layers;
  std::unordered_set<std::string> layer_ids;
  const Json* layer_array = required_member(document, "layers", "", result.diagnostics);
  if (layer_array != nullptr && !layer_array->is_array()) {
    add_diagnostic(result.diagnostics, "schema.type", "/layers", "expected an array");
  } else if (layer_array != nullptr) {
    for (std::size_t index = 0; index < layer_array->size(); ++index) {
      const auto& layer = (*layer_array)[index];
      const auto base = std::string("/layers/") + std::to_string(index);
      if (!layer.is_object()) {
        add_diagnostic(result.diagnostics, "schema.type", base, "expected an object");
        continue;
      }

      auto id = string_value(required_member(layer, "id", base, result.diagnostics), base + "/id",
                             result.diagnostics);
      auto type = string_value(required_member(layer, "type", base, result.diagnostics),
                               base + "/type", result.diagnostics);
      auto start = integer_value(required_member(layer, "start_frame", base, result.diagnostics),
                                 base + "/start_frame", result.diagnostics);
      auto end = integer_value(required_member(layer, "end_frame", base, result.diagnostics),
                               base + "/end_frame", result.diagnostics);
      auto effects = parse_effects(optional_member(layer, "effects"), base + "/effects",
                                   result.diagnostics);
      auto blend_mode = parse_blend_mode(optional_member(layer, "blend_mode"),
                                         base + "/blend_mode", result.diagnostics);

      if (id && (id->empty() || id->size() > 128)) {
        add_diagnostic(result.diagnostics, "layer.invalid_id", base + "/id",
                       "layer id must contain between 1 and 128 characters");
      } else if (id && !layer_ids.insert(*id).second) {
        add_diagnostic(result.diagnostics, "layer.duplicate_id", base + "/id",
                       "layer id must be unique");
      }
      if (type && *type != "solid" && *type != "video" && *type != "image" &&
          *type != "shape" && *type != "text") {
        add_diagnostic(result.diagnostics, "layer.unsupported_type", base + "/type",
                       "supported layer types are solid, video, image, shape, and text");
      }
      if (start && end && (*start < 0 || *end <= *start)) {
        add_diagnostic(result.diagnostics, "layer.invalid_range", base,
                       "layer range must be non-negative and non-empty");
      } else if (start && end && duration && *end > *duration) {
        add_diagnostic(result.diagnostics, "layer.out_of_bounds", base + "/end_frame",
                       "layer ends after the composition");
      }

      const auto common_valid = id && type && start && end && !id->empty() &&
                                id->size() <= 128 && *start >= 0 && *end > *start &&
                                (!duration || *end <= *duration);
      if (type && *type == "solid") {
        auto color = parse_color(required_member(layer, "color", base, result.diagnostics),
                                 base + "/color", result.diagnostics);
        if (common_valid && color && effects && blend_mode) {
          layers.emplace_back(
              SolidLayer{.id = std::move(*id),
                         .range = FrameRange(FrameIndex(*start), FrameIndex(*end)),
                         .color = *color,
                         .effects = std::move(*effects),
                         .blend_mode = *blend_mode});
        }
      } else if (type && *type == "video") {
        auto asset = string_value(required_member(layer, "asset", base, result.diagnostics),
                                  base + "/asset", result.diagnostics);
        auto source_start = integer_value(optional_member(layer, "source_start_frame"),
                                          base + "/source_start_frame", result.diagnostics)
                                .value_or(0);
        auto opacity = number_value(optional_member(layer, "opacity"), base + "/opacity",
                                    result.diagnostics)
                           .value_or(1.0);
        auto placement = parse_placement(layer, base, result.diagnostics);
        if (asset && !video_asset_ids.contains(*asset)) {
          add_diagnostic(result.diagnostics, "layer.unknown_asset", base + "/asset",
                         "video layer references an unknown asset");
        }
        if (source_start < 0) {
          add_diagnostic(result.diagnostics, "layer.invalid_source_start",
                         base + "/source_start_frame",
                         "source start frame must be non-negative");
        }
        if (opacity < 0.0 || opacity > 1.0) {
          add_diagnostic(result.diagnostics, "layer.invalid_opacity", base + "/opacity",
                         "opacity must be between 0 and 1");
        }
        if (common_valid && asset && video_asset_ids.contains(*asset) && source_start >= 0 &&
            opacity >= 0.0 && opacity <= 1.0 && placement && effects && blend_mode) {
          layers.emplace_back(
              VideoLayer{.id = std::move(*id),
                         .range = FrameRange(FrameIndex(*start), FrameIndex(*end)),
                         .asset_id = std::move(*asset),
                         .source_start = FrameIndex(source_start),
                         .opacity = opacity,
                         .placement = *placement,
                         .effects = std::move(*effects),
                         .blend_mode = *blend_mode});
        }
      } else if (type && *type == "image") {
        auto asset = string_value(required_member(layer, "asset", base, result.diagnostics),
                                  base + "/asset", result.diagnostics);
        auto placement = parse_placement(layer, base, result.diagnostics);
        if (asset && !image_asset_ids.contains(*asset)) {
          add_diagnostic(result.diagnostics, "layer.unknown_asset", base + "/asset",
                         "image layer references an unknown asset");
        }
        if (common_valid && asset && image_asset_ids.contains(*asset) && placement && effects &&
            blend_mode) {
          layers.emplace_back(
              ImageLayer{.id = std::move(*id),
                         .range = FrameRange(FrameIndex(*start), FrameIndex(*end)),
                         .asset_id = std::move(*asset),
                         .placement = *placement,
                         .effects = std::move(*effects),
                         .blend_mode = *blend_mode});
        }
      } else if (type && *type == "shape") {
        const auto kind = parse_shape_kind(
            required_member(layer, "shape", base, result.diagnostics), base + "/shape",
            result.diagnostics);
        const auto bounds = parse_number_array<4>(
            required_member(layer, "bounds", base, result.diagnostics), base + "/bounds",
            result.diagnostics);
        const auto fill = parse_color(required_member(layer, "fill", base, result.diagnostics),
                                      base + "/fill", result.diagnostics);
        const auto stroke = parse_shape_stroke(optional_member(layer, "stroke"),
                                               base + "/stroke", result.diagnostics);
        const auto corner_radius =
            number_value(optional_member(layer, "corner_radius"), base + "/corner_radius",
                         result.diagnostics)
                .value_or(0.0);
        if (bounds && ((*bounds)[2] <= 0.0 || (*bounds)[3] <= 0.0)) {
          add_diagnostic(result.diagnostics, "layer.invalid_shape_bounds", base + "/bounds",
                         "shape width and height must be positive");
        }
        if (corner_radius < 0.0) {
          add_diagnostic(result.diagnostics, "layer.invalid_corner_radius",
                         base + "/corner_radius", "corner radius must be non-negative");
        }
        if (common_valid && kind && bounds && (*bounds)[2] > 0.0 && (*bounds)[3] > 0.0 &&
            fill && stroke && corner_radius >= 0.0 && effects && blend_mode) {
          layers.emplace_back(
              ShapeLayer{.id = std::move(*id),
                         .range = FrameRange(FrameIndex(*start), FrameIndex(*end)),
                         .geometry = ShapeGeometry{.kind = *kind,
                                                   .x = (*bounds)[0],
                                                   .y = (*bounds)[1],
                                                   .width = (*bounds)[2],
                                                   .height = (*bounds)[3],
                                                   .corner_radius = corner_radius},
                         .fill = *fill,
                         .stroke = std::move(*stroke),
                         .effects = std::move(*effects),
                         .blend_mode = *blend_mode});
        }
      } else if (type && *type == "text") {
        auto font = string_value(required_member(layer, "font", base, result.diagnostics),
                                 base + "/font", result.diagnostics);
        auto text = string_value(required_member(layer, "text", base, result.diagnostics),
                                 base + "/text", result.diagnostics);
        const auto font_size = number_value(
            required_member(layer, "font_size", base, result.diagnostics),
            base + "/font_size", result.diagnostics);
        const auto position = parse_number_array<2>(
            required_member(layer, "position", base, result.diagnostics), base + "/position",
            result.diagnostics);
        const auto color = parse_color(required_member(layer, "color", base, result.diagnostics),
                                       base + "/color", result.diagnostics);
        if (font && !font_asset_ids.contains(*font)) {
          add_diagnostic(result.diagnostics, "layer.unknown_asset", base + "/font",
                         "text layer references an unknown font asset");
        }
        if (text && (text->empty() || text->size() > 16'384)) {
          add_diagnostic(result.diagnostics, "layer.invalid_text", base + "/text",
                         "text must contain between 1 and 16384 UTF-8 bytes");
        }
        if (font_size && (*font_size <= 0.0 || *font_size > 4096.0)) {
          add_diagnostic(result.diagnostics, "layer.invalid_font_size", base + "/font_size",
                         "font size must be greater than 0 and at most 4096 pixels");
        }
        if (common_valid && font && font_asset_ids.contains(*font) && text && !text->empty() &&
            text->size() <= 16'384 && font_size && *font_size > 0.0 && *font_size <= 4096.0 &&
            position && color && effects && blend_mode) {
          layers.emplace_back(
              TextLayer{.id = std::move(*id),
                        .range = FrameRange(FrameIndex(*start), FrameIndex(*end)),
                        .font_asset_id = std::move(*font),
                        .text = std::move(*text),
                        .font_size = *font_size,
                        .x = (*position)[0],
                        .y = (*position)[1],
                        .color = *color,
                        .effects = std::move(*effects),
                        .blend_mode = *blend_mode});
        }
      }
    }
  }

  if (!result.diagnostics.empty() || !schema_version || !width || !height || !duration ||
      !rate_numerator || !rate_denominator) {
    return result;
  }

  result.composition.emplace(
      static_cast<std::int32_t>(*schema_version), static_cast<std::int32_t>(*width),
      static_cast<std::int32_t>(*height), FrameRate(Rational(*rate_numerator, *rate_denominator)),
      FrameRange(FrameIndex(0), FrameIndex(*duration)), std::move(assets),
      std::move(image_assets), std::move(font_assets), std::move(layers));
  return result;
}

std::string diagnostics_json(const std::vector<Diagnostic>& diagnostics) {
  Json output = Json::array();
  for (const auto& diagnostic : diagnostics) {
    output.push_back({{"code", diagnostic.code},
                      {"path", diagnostic.path},
                      {"message", diagnostic.message}});
  }
  return output.dump(2);
}

}  // namespace chroma
