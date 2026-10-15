#include <cstdlib>
#include <iostream>
#include <string_view>
#include <variant>

#include "chroma/core/composition.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": check failed: " << expression << '\n';
    ++failures;
  }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

bool has_code(const chroma::CompositionParseResult& result, std::string_view code) {
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.code == code) {
      return true;
    }
  }
  return false;
}

constexpr std::string_view valid_source = R"json(
{
  "schema_version": 1,
  "canvas": {
    "width": 1920,
    "height": 1080,
    "frame_rate": {"numerator": 30000, "denominator": 1001},
    "duration_frames": 300
  },
  "layers": [
    {
      "id": "background",
      "type": "solid",
      "start_frame": 0,
      "end_frame": 300,
      "color": [0.1, 0.2, 0.3, 1.0]
    }
  ]
}
)json";

void valid_composition_is_normalized() {
  const auto result = chroma::parse_composition(valid_source);
  CHECK(result.valid());
  CHECK(result.composition.has_value());
  if (!result.composition) {
    return;
  }
  CHECK(result.composition->width() == 1920);
  CHECK(result.composition->height() == 1080);
  CHECK(result.composition->frame_rate().value() == chroma::Rational(30000, 1001));
  CHECK(result.composition->range().length() == 300);
  CHECK(result.composition->layers().size() == 1);
  CHECK(std::get<chroma::SolidLayer>(result.composition->layers().front()).id == "background");
}

void video_assets_and_layers_are_normalized() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 64,
      "height": 36,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 60
    },
    "assets": [
      {"id": "clip", "type": "video", "path": "media/clip.mkv"}
    ],
    "layers": [
      {"id": "video", "type": "video", "asset": "clip", "start_frame": 10,
       "end_frame": 40, "source_start_frame": 3, "opacity": 0.75,
       "fit": "cover", "position": [0.25, 0.75]}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source, "/project");
  CHECK(result.valid());
  if (!result.composition) {
    return;
  }
  CHECK(result.composition->assets().size() == 1);
  CHECK(result.composition->assets().front().path == "/project/media/clip.mkv");
  const auto* video = std::get_if<chroma::VideoLayer>(&result.composition->layers().front());
  CHECK(video != nullptr);
  if (video != nullptr) {
    CHECK(video->asset_id == "clip");
    CHECK(video->source_start == chroma::FrameIndex(3));
    CHECK(video->opacity == 0.75);
    CHECK(video->placement.fit == chroma::FitMode::cover);
    CHECK(video->placement.position_x == 0.25);
    CHECK(video->placement.position_y == 0.75);
  }
}

void invalid_video_references_are_reported() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 64,
      "height": 36,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 10
    },
    "layers": [
      {"id": "video", "type": "video", "asset": "missing", "start_frame": 0,
       "end_frame": 10, "opacity": 2, "fit": "stretch", "position": [-1, 2]}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(!result.valid());
  CHECK(has_code(result, "layer.unknown_asset"));
  CHECK(has_code(result, "layer.invalid_opacity"));
  CHECK(has_code(result, "layer.unsupported_fit"));
  CHECK(has_code(result, "layer.invalid_position"));
}

void image_assets_and_layers_are_normalized() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 64,
      "height": 36,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 60
    },
    "assets": [
      {"id": "logo", "type": "image", "path": "media/logo.png"}
    ],
    "layers": [
      {"id": "logo-layer", "type": "image", "asset": "logo", "start_frame": 0,
       "end_frame": 60, "fit": "contain", "position": [0.25, 0.75],
       "effects": [{"type": "opacity", "value": 0.5}], "blend_mode": "screen"}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source, "/project");
  CHECK(result.valid());
  if (!result.composition) {
    return;
  }
  CHECK(result.composition->image_assets().size() == 1);
  CHECK(result.composition->image_assets().front().path == "/project/media/logo.png");
  const auto* image = std::get_if<chroma::ImageLayer>(&result.composition->layers().front());
  CHECK(image != nullptr);
  if (image != nullptr) {
    CHECK(image->asset_id == "logo");
    CHECK(image->placement.fit == chroma::FitMode::contain);
    CHECK(image->placement.position_x == 0.25);
    CHECK(image->placement.position_y == 0.75);
    CHECK(image->effects.effects().size() == 1);
    CHECK(image->blend_mode == chroma::BlendMode::screen);
  }
}

void image_layers_reject_video_assets() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 1, "height": 1,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 1},
    "assets": [{"id": "clip", "type": "video", "path": "clip.mkv"}],
    "layers": [
      {"id": "wrong-kind", "type": "image", "asset": "clip",
       "start_frame": 0, "end_frame": 1}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(!result.valid());
  CHECK(has_code(result, "layer.unknown_asset"));
}

void effects_and_blend_modes_are_normalized() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 8,
      "height": 4,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 10
    },
    "layers": [
      {
        "id": "effected",
        "type": "solid",
        "start_frame": 0,
        "end_frame": 10,
        "color": [1, 0, 0, 1],
        "blend_mode": "multiply",
        "effects": [
          {"type": "transform_2d", "matrix": [1, 0, 2, 0, 1, 1]},
          {"type": "crop", "x": 2, "y": 1, "width": 4, "height": 2},
          {"type": "opacity", "value": 0.5},
          {"type": "color_matrix", "matrix": [
            1, 0, 0, 0, 0,
            0, 1, 0, 0, 0,
            0, 0, 1, 0, 0,
            0, 0, 0, 1, 0
          ]}
        ]
      }
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(result.valid());
  if (!result.composition) {
    return;
  }
  const auto* layer = std::get_if<chroma::SolidLayer>(&result.composition->layers().front());
  CHECK(layer != nullptr);
  if (layer != nullptr) {
    CHECK(layer->blend_mode == chroma::BlendMode::multiply);
    CHECK(layer->effects.effects().size() == 4);
    CHECK(std::holds_alternative<chroma::Transform2DEffect>(layer->effects.effects()[0]));
    CHECK(std::holds_alternative<chroma::CropEffect>(layer->effects.effects()[1]));
    CHECK(std::holds_alternative<chroma::OpacityEffect>(layer->effects.effects()[2]));
    CHECK(std::holds_alternative<chroma::ColorMatrixEffect>(layer->effects.effects()[3]));
  }
}

void invalid_effects_and_blends_are_reported() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 8,
      "height": 4,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 10
    },
    "layers": [
      {
        "id": "invalid",
        "type": "solid",
        "start_frame": 0,
        "end_frame": 10,
        "color": [1, 0, 0, 1],
        "blend_mode": "difference",
        "effects": [
          {"type": "transform_2d", "matrix": [1, 0]},
          {"type": "blur", "radius": 2}
        ]
      }
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(!result.valid());
  CHECK(has_code(result, "layer.unsupported_blend_mode"));
  CHECK(has_code(result, "effect.invalid_array"));
  CHECK(has_code(result, "effect.unsupported_type"));
}

void invalid_json_is_reported() {
  const auto result = chroma::parse_composition("{");
  CHECK(!result.valid());
  CHECK(has_code(result, "json.invalid"));
}

void invalid_timeline_is_reported() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 1920,
      "height": 1080,
      "frame_rate": {"numerator": 0, "denominator": 1},
      "duration_frames": 0
    },
    "layers": []
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(!result.valid());
  CHECK(has_code(result, "timeline.invalid_frame_rate"));
  CHECK(has_code(result, "timeline.invalid_duration"));
}

void duplicate_and_out_of_bounds_layers_are_reported() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 100,
      "height": 100,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 10
    },
    "layers": [
      {"id": "same", "type": "solid", "start_frame": 0, "end_frame": 5,
       "color": [0, 0, 0, 1]},
      {"id": "same", "type": "solid", "start_frame": 5, "end_frame": 11,
       "color": [1, 1, 1, 1]}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(!result.valid());
  CHECK(has_code(result, "layer.duplicate_id"));
  CHECK(has_code(result, "layer.out_of_bounds"));
}

void diagnostics_are_machine_readable() {
  const auto result = chroma::parse_composition("null");
  const auto json = chroma::diagnostics_json(result.diagnostics);
  CHECK(json.find("schema.root") != std::string::npos);
  CHECK(json.find("path") != std::string::npos);
}

void oversized_canvases_are_rejected_before_rendering() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 32768,
      "height": 32768,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 1
    },
    "layers": []
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(!result.valid());
  CHECK(has_code(result, "canvas.too_large"));
}

}  // namespace

int main() {
  valid_composition_is_normalized();
  invalid_json_is_reported();
  invalid_timeline_is_reported();
  duplicate_and_out_of_bounds_layers_are_reported();
  diagnostics_are_machine_readable();
  oversized_canvases_are_rejected_before_rendering();
  video_assets_and_layers_are_normalized();
  invalid_video_references_are_reported();
  image_assets_and_layers_are_normalized();
  image_layers_reject_video_assets();
  effects_and_blend_modes_are_normalized();
  invalid_effects_and_blends_are_reported();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "composition tests passed\n";
  return EXIT_SUCCESS;
}
