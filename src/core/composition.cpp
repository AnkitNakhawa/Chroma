#include "chroma/core/composition.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

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

}  // namespace

Composition::Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
                         FrameRate frame_rate, FrameRange range,
                         std::vector<SolidLayer> layers)
    : schema_version_(schema_version),
      width_(width),
      height_(height),
      frame_rate_(std::move(frame_rate)),
      range_(range),
      layers_(std::move(layers)) {}

CompositionParseResult parse_composition(std::string_view source) {
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

  std::vector<SolidLayer> layers;
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
      auto color = parse_color(required_member(layer, "color", base, result.diagnostics),
                               base + "/color", result.diagnostics);

      if (id && (id->empty() || id->size() > 128)) {
        add_diagnostic(result.diagnostics, "layer.invalid_id", base + "/id",
                       "layer id must contain between 1 and 128 characters");
      } else if (id && !layer_ids.insert(*id).second) {
        add_diagnostic(result.diagnostics, "layer.duplicate_id", base + "/id",
                       "layer id must be unique");
      }
      if (type && *type != "solid") {
        add_diagnostic(result.diagnostics, "layer.unsupported_type", base + "/type",
                       "only solid layers are supported by the initial spike");
      }
      if (start && end && (*start < 0 || *end <= *start)) {
        add_diagnostic(result.diagnostics, "layer.invalid_range", base,
                       "layer range must be non-negative and non-empty");
      } else if (start && end && duration && *end > *duration) {
        add_diagnostic(result.diagnostics, "layer.out_of_bounds", base + "/end_frame",
                       "layer ends after the composition");
      }

      if (id && type && start && end && color && !id->empty() && id->size() <= 128 &&
          *type == "solid" && *start >= 0 && *end > *start && (!duration || *end <= *duration)) {
        layers.push_back(SolidLayer{.id = std::move(*id),
                                    .range = FrameRange(FrameIndex(*start), FrameIndex(*end)),
                                    .color = *color});
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
      FrameRange(FrameIndex(0), FrameIndex(*duration)), std::move(layers));
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
