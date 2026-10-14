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
       "end_frame": 40, "source_start_frame": 3, "opacity": 0.75}
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
       "end_frame": 10, "opacity": 2}
    ]
  }
  )json";
  const auto result = chroma::parse_composition(source);
  CHECK(!result.valid());
  CHECK(has_code(result, "layer.unknown_asset"));
  CHECK(has_code(result, "layer.invalid_opacity"));
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

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "composition tests passed\n";
  return EXIT_SUCCESS;
}
