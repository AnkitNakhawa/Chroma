#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chroma/core/render.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": check failed: " << expression << '\n';
    ++failures;
  }
}

template <typename Exception, typename Operation>
void check_throws(Operation operation, std::string_view expression, int line) {
  try {
    operation();
    std::cerr << "line " << line << ": expected exception: " << expression << '\n';
    ++failures;
  } catch (const Exception&) {
  } catch (...) {
    std::cerr << "line " << line << ": wrong exception type: " << expression << '\n';
    ++failures;
  }
}

bool near(float lhs, float rhs) {
  return std::abs(lhs - rhs) < 0.00001F;
}

#define CHECK(expression) check((expression), #expression, __LINE__)
#define CHECK_THROWS_AS(expression, exception_type) \
  check_throws<exception_type>([&] { static_cast<void>(expression); }, #expression, __LINE__)

chroma::Composition composition_with(std::vector<chroma::SolidLayer> layers) {
  return chroma::Composition(1, 2, 1, chroma::FrameRate(chroma::Rational(30)),
                             chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
                             std::move(layers));
}

void surface_checks_coordinates() {
  chroma::Surface surface(2, 3);
  CHECK(surface.size() == 6);
  surface.pixel(1, 2) = chroma::Pixel{.red = 1.0F, .alpha = 1.0F};
  CHECK(surface.pixel(1, 2).red == 1.0F);
  CHECK_THROWS_AS(surface.pixel(2, 0), std::out_of_range);
  CHECK_THROWS_AS(chroma::Surface(0, 1), std::invalid_argument);
  CHECK_THROWS_AS(chroma::Surface(32768, 32768), std::length_error);
}

void inactive_layers_are_transparent() {
  auto composition = composition_with({chroma::SolidLayer{
      .id = "future",
      .range = chroma::FrameRange(chroma::FrameIndex(5), chroma::FrameIndex(10)),
      .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0}}});
  const auto surface = chroma::render_frame_cpu(composition, chroma::FrameIndex(4));
  CHECK(surface.pixel(0, 0) == chroma::Pixel{});
}

void layers_composite_in_document_order() {
  auto composition = composition_with(
      {chroma::SolidLayer{
           .id = "blue",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0}},
       chroma::SolidLayer{
           .id = "red",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 0.5}}});

  const auto pixel = chroma::render_frame_cpu(composition, chroma::FrameIndex(0)).pixel(0, 0);
  CHECK(near(pixel.red, 0.5F));
  CHECK(near(pixel.green, 0.0F));
  CHECK(near(pixel.blue, 0.5F));
  CHECK(near(pixel.alpha, 1.0F));
}

void ppm_output_is_stable() {
  const chroma::Surface surface(1, 1,
                                chroma::Pixel{.red = 1.0F, .green = 0.0F, .blue = 0.0F,
                                              .alpha = 1.0F});
  std::ostringstream output(std::ios::out | std::ios::binary);
  chroma::write_ppm(surface, output);
  const std::string expected = std::string("P6\n1 1\n255\n") +
                               std::string({static_cast<char>(255), 0, 0});
  CHECK(output.str() == expected);
}

void frames_outside_the_composition_are_rejected() {
  const auto composition = composition_with({});
  CHECK_THROWS_AS(chroma::render_frame_cpu(composition, chroma::FrameIndex(10)),
                  std::out_of_range);
}

void compiled_plan_execution_matches_the_render_convenience_api() {
  const auto composition = composition_with(
      {chroma::SolidLayer{
           .id = "blue",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0}},
       chroma::SolidLayer{
           .id = "red",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 0.5}}});
  const auto frame = chroma::FrameIndex(3);
  const auto direct = chroma::render_frame_cpu(composition, frame);
  const auto compiled = chroma::execute_cpu(chroma::compile_frame(composition, frame));
  CHECK(direct.width() == compiled.width());
  CHECK(direct.height() == compiled.height());
  CHECK(direct.pixels() == compiled.pixels());
}

void direct_compositions_enforce_invariants() {
  CHECK_THROWS_AS(
      chroma::Composition(
          1, 2, 1, chroma::FrameRate(chroma::Rational(30)),
          chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
          {chroma::SolidLayer{
              .id = "bad-color",
              .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
              .color = chroma::Color{.red = 2.0, .green = 0.0, .blue = 0.0, .alpha = 1.0}}}),
      std::invalid_argument);
}

void cpu_effects_apply_in_program_order() {
  chroma::Surface surface(3, 1);
  surface.pixel(0, 0) =
      chroma::Pixel{.red = 1.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
  const auto shifted = chroma::apply_effect_program_cpu(
      std::move(surface),
      chroma::EffectProgram({chroma::Transform2DEffect{.m02 = 1.0},
                             chroma::CropEffect{.x = 1, .y = 0, .width = 1, .height = 1},
                             chroma::OpacityEffect{.opacity = 0.5}}));
  CHECK(shifted.pixel(0, 0) == chroma::Pixel{});
  CHECK(near(shifted.pixel(1, 0).red, 0.5F));
  CHECK(near(shifted.pixel(1, 0).alpha, 0.5F));
  CHECK(shifted.pixel(2, 0) == chroma::Pixel{});
}

void color_matrix_operates_on_straight_linear_rgba() {
  const chroma::ColorMatrixEffect red_to_green{
      .values = {0.0, 0.0, 0.0, 0.0, 0.0,
                 1.0, 0.0, 0.0, 0.0, 0.0,
                 0.0, 0.0, 0.0, 0.0, 0.0,
                 0.0, 0.0, 0.0, 1.0, 0.0}};
  const chroma::Surface surface(
      1, 1, chroma::Pixel{.red = 0.5F, .green = 0.0F, .blue = 0.0F, .alpha = 0.5F});
  const auto converted = chroma::apply_effect_program_cpu(
      surface, chroma::EffectProgram({red_to_green}));
  CHECK(near(converted.pixel(0, 0).red, 0.0F));
  CHECK(near(converted.pixel(0, 0).green, 0.5F));
  CHECK(near(converted.pixel(0, 0).blue, 0.0F));
  CHECK(near(converted.pixel(0, 0).alpha, 0.5F));
}

void singular_transforms_produce_transparency() {
  const chroma::Surface surface(
      1, 1, chroma::Pixel{.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F});
  const auto transformed = chroma::apply_effect_program_cpu(
      surface, chroma::EffectProgram({chroma::Transform2DEffect{.m00 = 0.0, .m11 = 0.0}}));
  CHECK(transformed.pixel(0, 0) == chroma::Pixel{});
}

void source_placement_modes_map_into_the_canvas() {
  const auto red = chroma::Pixel{.red = 1.0F, .alpha = 1.0F};

  const auto contained = chroma::place_surface_cpu(
      chroma::Surface(2, 1, red), 4, 4,
      chroma::SourcePlacement{.fit = chroma::FitMode::contain});
  CHECK(contained.pixel(0, 0) == chroma::Pixel{});
  CHECK(contained.pixel(0, 1) == red);
  CHECK(contained.pixel(3, 2) == red);
  CHECK(contained.pixel(3, 3) == chroma::Pixel{});

  const auto covered = chroma::place_surface_cpu(
      chroma::Surface(2, 1, red), 2, 2,
      chroma::SourcePlacement{.fit = chroma::FitMode::cover});
  CHECK(covered.pixel(0, 0) == red);
  CHECK(covered.pixel(1, 1) == red);

  chroma::Surface crop_source(4, 1, red);
  const auto blue = chroma::Pixel{.blue = 1.0F, .alpha = 1.0F};
  crop_source.pixel(3, 0) = blue;
  const auto cover_left = chroma::place_surface_cpu(
      crop_source, 1, 1,
      chroma::SourcePlacement{.fit = chroma::FitMode::cover,
                              .position_x = 0.0,
                              .position_y = 0.5});
  const auto cover_right = chroma::place_surface_cpu(
      std::move(crop_source), 1, 1,
      chroma::SourcePlacement{.fit = chroma::FitMode::cover,
                              .position_x = 1.0,
                              .position_y = 0.5});
  CHECK(cover_left.pixel(0, 0) == red);
  CHECK(cover_right.pixel(0, 0) == blue);

  const auto filled = chroma::place_surface_cpu(
      chroma::Surface(1, 1, red), 3, 2,
      chroma::SourcePlacement{.fit = chroma::FitMode::fill});
  CHECK(filled.pixel(0, 0) == red);
  CHECK(filled.pixel(2, 1) == red);

  const auto native = chroma::place_surface_cpu(
      chroma::Surface(1, 1, red), 3, 1,
      chroma::SourcePlacement{.fit = chroma::FitMode::none,
                              .position_x = 1.0,
                              .position_y = 0.5});
  CHECK(native.pixel(0, 0) == chroma::Pixel{});
  CHECK(native.pixel(1, 0) == chroma::Pixel{});
  CHECK(native.pixel(2, 0) == red);

  CHECK_THROWS_AS(
      chroma::place_surface_cpu(
          chroma::Surface(1, 1, red), 1, 1,
          chroma::SourcePlacement{.fit = chroma::FitMode::contain,
                                  .position_x = 2.0,
                                  .position_y = 0.5}),
      std::invalid_argument);
}

void unavailable_cpu_effects_fail_explicitly() {
  CHECK_THROWS_AS(
      chroma::apply_effect_program_cpu(
          chroma::Surface(1, 1),
          chroma::EffectProgram({chroma::GaussianBlurEffect{.radius = 1.0}})),
      std::runtime_error);
}

void blend_modes_follow_premultiplied_source_over_semantics() {
  const auto red = chroma::Pixel{.red = 1.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
  const auto blue = chroma::Pixel{.red = 0.0F, .green = 0.0F, .blue = 1.0F, .alpha = 1.0F};
  const auto multiplied = chroma::composite(red, blue, chroma::BlendMode::multiply);
  CHECK(multiplied == (chroma::Pixel{.red = 0.0F, .green = 0.0F,
                                     .blue = 0.0F, .alpha = 1.0F}));
  const auto screened = chroma::composite(red, blue, chroma::BlendMode::screen);
  CHECK(screened == (chroma::Pixel{.red = 1.0F, .green = 0.0F,
                                   .blue = 1.0F, .alpha = 1.0F}));
  const auto added = chroma::composite(red, blue, chroma::BlendMode::plus);
  CHECK(added == (chroma::Pixel{.red = 1.0F, .green = 0.0F,
                                .blue = 1.0F, .alpha = 1.0F}));
}

void composition_effects_execute_through_the_frame_plan() {
  const auto composition = composition_with(
      {chroma::SolidLayer{
           .id = "background",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0}},
       chroma::SolidLayer{
           .id = "effected",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0},
           .effects = chroma::EffectProgram(
               {chroma::CropEffect{.x = 1, .y = 0, .width = 1, .height = 1},
                chroma::OpacityEffect{.opacity = 0.5},
                chroma::OpacityEffect{.opacity = 0.5}})}});
  const auto surface = chroma::render_frame_cpu(composition, chroma::FrameIndex(0));
  CHECK(surface.pixel(0, 0) ==
        (chroma::Pixel{.red = 0.0F, .green = 0.0F, .blue = 1.0F, .alpha = 1.0F}));
  CHECK(near(surface.pixel(1, 0).red, 0.25F));
  CHECK(near(surface.pixel(1, 0).green, 0.0F));
  CHECK(near(surface.pixel(1, 0).blue, 0.75F));
  CHECK(near(surface.pixel(1, 0).alpha, 1.0F));
}

void parsed_effects_execute_end_to_end() {
  constexpr std::string_view source = R"json(
  {
    "schema_version": 1,
    "canvas": {
      "width": 2,
      "height": 1,
      "frame_rate": {"numerator": 30, "denominator": 1},
      "duration_frames": 1
    },
    "layers": [
      {"id": "background", "type": "solid", "start_frame": 0, "end_frame": 1,
       "color": [0, 0, 1, 1]},
      {"id": "foreground", "type": "solid", "start_frame": 0, "end_frame": 1,
       "color": [1, 0, 0, 1],
       "effects": [
         {"type": "crop", "x": 1, "y": 0, "width": 1, "height": 1},
         {"type": "opacity", "value": 0.5}
       ]}
    ]
  }
  )json";
  const auto parsed = chroma::parse_composition(source);
  CHECK(parsed.valid());
  if (!parsed.composition) {
    return;
  }
  const auto surface = chroma::render_frame_cpu(*parsed.composition, chroma::FrameIndex(0));
  CHECK(surface.pixel(0, 0) ==
        (chroma::Pixel{.red = 0.0F, .green = 0.0F, .blue = 1.0F, .alpha = 1.0F}));
  CHECK(near(surface.pixel(1, 0).red, 0.5F));
  CHECK(near(surface.pixel(1, 0).blue, 0.5F));
}

void video_layers_decode_into_the_cpu_plan(const char* path) {
  const chroma::Composition composition(
      1, 64, 36, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(60)),
      {chroma::VideoAsset{.id = "clip", .path = path}},
      {chroma::VideoLayer{
          .id = "video",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(60)),
          .asset_id = "clip",
          .source_start = chroma::FrameIndex(0),
          .opacity = 1.0}});
  const auto blue = chroma::render_frame_cpu(composition, chroma::FrameIndex(0)).pixel(0, 0);
  const auto red = chroma::render_frame_cpu(composition, chroma::FrameIndex(30)).pixel(0, 0);
  CHECK(blue.blue > 0.8F);
  CHECK(blue.red < 0.05F);
  CHECK(red.red > 0.8F);
  CHECK(red.blue < 0.05F);

  const chroma::Composition square_composition(
      1, 32, 32, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
      {chroma::VideoAsset{.id = "clip", .path = path}},
      {chroma::VideoLayer{
          .id = "contained-video",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
          .asset_id = "clip",
          .source_start = chroma::FrameIndex(0),
          .opacity = 1.0,
          .placement = {.fit = chroma::FitMode::contain}}});
  const auto contained =
      chroma::render_frame_cpu(square_composition, chroma::FrameIndex(0));
  CHECK(contained.pixel(16, 0) == chroma::Pixel{});
  CHECK(contained.pixel(16, 16).blue > 0.8F);
}

}  // namespace

int main(int argc, char** argv) {
  surface_checks_coordinates();
  inactive_layers_are_transparent();
  layers_composite_in_document_order();
  ppm_output_is_stable();
  frames_outside_the_composition_are_rejected();
  compiled_plan_execution_matches_the_render_convenience_api();
  direct_compositions_enforce_invariants();
  cpu_effects_apply_in_program_order();
  color_matrix_operates_on_straight_linear_rgba();
  singular_transforms_produce_transparency();
  source_placement_modes_map_into_the_canvas();
  unavailable_cpu_effects_fail_explicitly();
  blend_modes_follow_premultiplied_source_over_semantics();
  composition_effects_execute_through_the_frame_plan();
  parsed_effects_execute_end_to_end();
  if (argc == 2) {
    video_layers_decode_into_the_cpu_plan(argv[1]);
  }

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "render tests passed\n";
  return EXIT_SUCCESS;
}
