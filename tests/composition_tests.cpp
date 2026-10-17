#include <cstdlib>
#include <string>
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

void shape_layers_are_normalized_and_validated() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 16, "height": 16,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 10},
    "layers": [
      {"id": "card", "type": "shape", "shape": "rounded_rectangle",
       "start_frame": 0, "end_frame": 10, "bounds": [1, 2, 12, 8],
       "corner_radius": 2, "fill": [0, 0, 1, 1],
       "stroke": {"color": [1, 0, 0, 1], "width": 1},
       "effects": [{"type": "opacity", "value": 0.75}]}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(result.valid());
  if (!result.composition) {
    return;
  }
  const auto* shape = std::get_if<chroma::ShapeLayer>(&result.composition->layers().front());
  CHECK(shape != nullptr);
  if (shape != nullptr) {
    CHECK(shape->geometry.kind == chroma::ShapeKind::rounded_rectangle);
    CHECK(shape->geometry.x == 1.0);
    CHECK(shape->geometry.height == 8.0);
    CHECK(shape->geometry.corner_radius == 2.0);
    CHECK(shape->stroke.has_value());
    CHECK(shape->stroke && shape->stroke->width == 1.0);
  }

  constexpr std::string_view invalid = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 16, "height": 16,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 1},
    "layers": [
      {"id": "bad", "type": "shape", "shape": "triangle",
       "start_frame": 0, "end_frame": 1, "bounds": [0, 0, 0, 4],
       "corner_radius": -1, "fill": [1, 1, 1, 1],
       "stroke": {"color": [0, 0, 0, 1], "width": 0}}
    ]
  }
  )json";
  const auto invalid_result = chroma::parse_composition(invalid);
  CHECK(!invalid_result.valid());
  CHECK(has_code(invalid_result, "layer.unsupported_shape"));
  CHECK(has_code(invalid_result, "layer.invalid_shape_bounds"));
  CHECK(has_code(invalid_result, "layer.invalid_corner_radius"));
  CHECK(has_code(invalid_result, "layer.invalid_stroke"));
}

void text_layers_are_normalized_and_validated() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 320, "height": 180,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 60},
    "assets": [{"id": "body", "type": "font", "path": "fonts/body.ttf"}],
    "layers": [
      {"id": "title", "type": "text", "font": "body", "text": "Hello",
       "font_size": 36, "position": [24, 72], "color": [1, 0.5, 0.2, 1],
       "start_frame": 0, "end_frame": 60,
       "effects": [{"type": "opacity", "value": 0.8}]}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source, "/project");
  CHECK(result.valid());
  if (!result.composition) {
    return;
  }
  CHECK(result.composition->font_assets().size() == 1);
  CHECK(result.composition->font_assets().front().path == "/project/fonts/body.ttf");
  const auto* text = std::get_if<chroma::TextLayer>(&result.composition->layers().front());
  CHECK(text != nullptr);
  if (text != nullptr) {
    CHECK(text->font_asset_id == "body");
    CHECK(text->text == "Hello");
    CHECK(text->font_size == 36.0);
    CHECK(text->x == 24.0);
    CHECK(text->y == 72.0);
  }

  constexpr std::string_view invalid = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 16, "height": 16,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 1},
    "layers": [
      {"id": "bad", "type": "text", "font": "missing", "text": "",
       "font_size": 0, "position": [0, 0], "color": [1, 1, 1, 1],
       "start_frame": 0, "end_frame": 1}
    ]
  }
  )json";
  const auto invalid_result = chroma::parse_composition(invalid);
  CHECK(!invalid_result.valid());
  CHECK(has_code(invalid_result, "layer.unknown_asset"));
  CHECK(has_code(invalid_result, "layer.invalid_text"));
  CHECK(has_code(invalid_result, "layer.invalid_font_size"));
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
          {"type": "gaussian_blur", "radius": 1.5},
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
    CHECK(layer->effects.effects().size() == 5);
    CHECK(std::holds_alternative<chroma::Transform2DEffect>(layer->effects.effects()[0]));
    CHECK(std::holds_alternative<chroma::CropEffect>(layer->effects.effects()[1]));
    CHECK(std::holds_alternative<chroma::OpacityEffect>(layer->effects.effects()[2]));
    CHECK(std::holds_alternative<chroma::GaussianBlurEffect>(layer->effects.effects()[3]));
    CHECK(std::holds_alternative<chroma::ColorMatrixEffect>(layer->effects.effects()[4]));
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
          {"type": "blur", "radius": 2},
          {"type": "gaussian_blur", "radius": -1}
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
  CHECK(has_code(result, "effect.invalid_blur"));
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

void animated_opacity_is_normalized_and_validated() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 8, "height": 8,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 20},
    "layers": [
      {"id": "animated", "type": "solid", "start_frame": 5, "end_frame": 15,
       "color": [1, 1, 1, 1],
       "effects": [{"type": "opacity", "keyframes": [
         {"frame": 0, "value": 0, "easing": "ease_out"},
         {"frame": 5, "value": 1}
       ]}]}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(result.valid());
  if (result.composition) {
    const auto& solid = std::get<chroma::SolidLayer>(result.composition->layers().front());
    const auto* animated =
        std::get_if<chroma::AnimatedOpacityEffect>(&solid.effects.effects().front());
    CHECK(animated != nullptr);
    if (animated != nullptr) {
      CHECK(animated->keyframes.size() == 2);
      CHECK(animated->keyframes.front().easing == chroma::EasingCurve::ease_out);
    }
  }

  constexpr std::string_view invalid = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 8, "height": 8,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 10},
    "layers": [
      {"id": "bad", "type": "solid", "start_frame": 0, "end_frame": 10,
       "color": [1, 1, 1, 1],
       "effects": [{"type": "opacity", "keyframes": [
         {"frame": 4, "value": 0}, {"frame": 4, "value": 2, "easing": "bounce"}
       ]}]}
    ]
  }
  )json";
  const auto invalid_result = chroma::parse_composition(invalid);
  CHECK(!invalid_result.valid());
  CHECK(has_code(invalid_result, "effect.invalid_keyframe_order"));
  CHECK(has_code(invalid_result, "effect.invalid_opacity"));
  CHECK(has_code(invalid_result, "effect.invalid_easing"));
}

void masks_are_normalized_and_validated() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 8, "height": 8,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 10},
    "layers": [
      {"id": "masked", "type": "solid", "start_frame": 0, "end_frame": 10,
       "color": [1, 1, 1, 1],
       "effects": [{"type": "mask", "shape": "ellipse",
         "bounds": [1, 2, 6, 4], "inverted": true}]}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(result.valid());
  if (result.composition) {
    const auto& solid = std::get<chroma::SolidLayer>(result.composition->layers().front());
    const auto* mask = std::get_if<chroma::MaskEffect>(&solid.effects.effects().front());
    CHECK(mask != nullptr);
    if (mask != nullptr) {
      CHECK(mask->shape == chroma::MaskShape::ellipse);
      CHECK(mask->width == 6.0);
      CHECK(mask->inverted);
    }
  }

  constexpr std::string_view invalid = R"json(
  {
    "schema_version": 1,
    "canvas": {"width": 8, "height": 8,
      "frame_rate": {"numerator": 30, "denominator": 1}, "duration_frames": 10},
    "layers": [
      {"id": "bad", "type": "solid", "start_frame": 0, "end_frame": 10,
       "color": [1, 1, 1, 1],
       "effects": [{"type": "mask", "shape": "triangle",
         "bounds": [0, 0, 0, 4], "inverted": 1}]}
    ]
  }
  )json";
  const auto invalid_result = chroma::parse_composition(invalid);
  CHECK(!invalid_result.valid());
  CHECK(has_code(invalid_result, "effect.invalid_mask_shape"));
  CHECK(has_code(invalid_result, "effect.invalid_mask_bounds"));
  CHECK(has_code(invalid_result, "schema.type"));
}

void audio_tracks_are_normalized_and_validated() {
  const std::string prefix = R"({"schema_version":1,"canvas":{"width":8,"height":8,
    "frame_rate":{"numerator":30000,"denominator":1001},"duration_frames":60},
    "layers":[],"assets":[{"id":"music","type":"audio","path":"music.wav"}],"audio_tracks":)";
  const auto valid = chroma::parse_composition(prefix + R"([{"id":"bed","asset":"music",
    "start_frame":10,"end_frame":50,"source_start_frame":5,"gain":0.5}]})", "/assets");
  CHECK(valid.valid());
  if (valid.composition) {
    CHECK(valid.composition->audio_assets().front().path == "/assets/music.wav");
    const auto& track = valid.composition->audio_tracks().front();
    CHECK(track.range.start().value() == 10);
    CHECK(track.source_start.value() == 5);
    CHECK(track.gain == 0.5);
  }
  const auto defaults = chroma::parse_composition(prefix + R"([{"id":"bed","asset":"music",
    "start_frame":0,"end_frame":60}]})");
  CHECK(defaults.valid());
  CHECK(defaults.composition->audio_tracks().front().gain == 1.0);
  const auto invalid = chroma::parse_composition(prefix + R"([{"id":"bad","asset":"absent",
    "start_frame":-1,"end_frame":61,"source_start_frame":-1,"gain":-1}]})");
  CHECK(has_code(invalid, "audio.unknown_asset"));
  CHECK(has_code(invalid, "audio.invalid_range"));
  CHECK(has_code(invalid, "audio.invalid_source_start"));
  CHECK(has_code(invalid, "audio.invalid_gain"));
  CHECK(!chroma::parse_composition(prefix + R"([{"id":"bed","asset":"music",
    "start_frame":0,"end_frame":60,"gain":"loud"}]})").valid());
  CHECK(!chroma::parse_composition(prefix + R"([{"id":"bed","asset":"music",
    "start_frame":0,"end_frame":60},{"id":"bed","asset":"music",
    "start_frame":0,"end_frame":60}]})").valid());
  CHECK(!chroma::parse_composition(prefix + "{} }").valid());
  bool rejected = false;
  try {
    chroma::Composition composition(1, 8, 8, chroma::FrameRate(chroma::Rational(30, 1)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(60)), {}, {}, {}, {},
      {{"music", "music.wav"}}, {{"bad", chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(61)), "music"}});
  } catch (const std::invalid_argument&) { rejected = true; }
  CHECK(rejected);
}

void animated_transform_documents_validate_and_normalize() {
  const std::string prefix = R"({"schema_version":1,"canvas":{"width":16,"height":16,
    "frame_rate":{"numerator":30,"denominator":1},"duration_frames":30},
    "layers":[{"id":"motion","type":"solid","start_frame":5,"end_frame":25,
    "color":[1,0,0,1],"effects":[)";
  auto parse = [&](const std::string& effects) { return chroma::parse_composition(prefix + effects + "]}]}"); };
  const auto valid = parse(R"({"type":"translation","keyframes":[
    {"frame":0,"value":[-2,3],"easing":"ease_out"},{"frame":10,"value":[4,8]}]},
    {"type":"scale","value":[-1,0],"origin":[8,8]},
    {"type":"rotation","keyframes":[{"frame":0,"value":0},{"frame":20,"value":720}],"origin":[8,8]})");
  CHECK(valid.valid());
  if (valid.composition) {
    const auto& effects = std::get<chroma::SolidLayer>(valid.composition->layers().front()).effects.effects();
    const auto& translation = std::get<chroma::AnimatedTranslationEffect>(effects[0]);
    CHECK(translation.keyframes[0].value[0] == -2);
    CHECK(translation.keyframes[0].easing == chroma::EasingCurve::ease_out);
    const auto& scale = std::get<chroma::AnimatedScaleEffect>(effects[1]);
    CHECK(scale.keyframes.size() == 1 && scale.keyframes[0].frame == 0);
    CHECK(scale.origin[0] == 8);
    CHECK(std::get<chroma::AnimatedRotationEffect>(effects[2]).keyframes.back().value == 720);
  }
  CHECK(parse(R"({"type":"translation","value":[0,0]},{"type":"scale","value":[1,1]},
    {"type":"rotation","value":90})").valid());
  CHECK(has_code(parse(R"({"type":"translation"})"), "effect.invalid_animation"));
  CHECK(has_code(parse(R"({"type":"rotation","value":90,"keyframes":[]})"), "effect.invalid_animation"));
  CHECK(has_code(parse(R"({"type":"rotation","keyframes":[]})"), "effect.invalid_keyframes"));
  CHECK(has_code(parse(R"({"type":"rotation","keyframes":[{"frame":-1,"value":0}]})"), "effect.invalid_keyframe_order"));
  CHECK(has_code(parse(R"({"type":"scale","keyframes":[{"frame":1,"value":[1,1]},
    {"frame":1,"value":[2,2]}]})"), "effect.invalid_keyframe_order"));
  CHECK(has_code(parse(R"({"type":"rotation","keyframes":[{"frame":0,"value":90,"easing":"bounce"}]})"), "effect.invalid_easing"));
  CHECK(has_code(parse(R"({"type":"translation","value":[0,0],"origin":[1,1]})"), "effect.unexpected_origin"));
  for (const auto& bad : {R"({"type":"scale","value":[1]})", R"({"type":"scale","value":2})",
       R"({"type":"rotation","value":[90,0]})", R"({"type":"rotation","value":90,"origin":[1]})",
       R"({"type":"translation","keyframes":{}})", R"({"type":"translation","keyframes":[0]})",
       R"({"type":"translation","keyframes":[{"frame":0,"value":[0,"bad"]}]})",
       R"({"type":"rotation","keyframes":[{"frame":0.5,"value":90}]})",
       R"({"type":"rotation","keyframes":[{"frame":0}]})"}) {
    CHECK(!parse(bad).valid());
  }
  std::string oversized = R"({"type":"rotation","keyframes":[)";
  for (int index = 0; index < 1025; ++index) {
    if (index != 0) oversized += ",";
    oversized += "{\"frame\":" + std::to_string(index) + ",\"value\":0}";
  }
  CHECK(has_code(parse(oversized + "]}"), "effect.invalid_keyframes"));
}

}  // namespace

int main() {
  valid_composition_is_normalized();
  audio_tracks_are_normalized_and_validated();
  animated_transform_documents_validate_and_normalize();
  invalid_json_is_reported();
  invalid_timeline_is_reported();
  duplicate_and_out_of_bounds_layers_are_reported();
  diagnostics_are_machine_readable();
  oversized_canvases_are_rejected_before_rendering();
  animated_opacity_is_normalized_and_validated();
  masks_are_normalized_and_validated();
  video_assets_and_layers_are_normalized();
  invalid_video_references_are_reported();
  image_assets_and_layers_are_normalized();
  image_layers_reject_video_assets();
  shape_layers_are_normalized_and_validated();
  text_layers_are_normalized_and_validated();
  effects_and_blend_modes_are_normalized();
  invalid_effects_and_blends_are_reported();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "composition tests passed\n";
  return EXIT_SUCCESS;
}
