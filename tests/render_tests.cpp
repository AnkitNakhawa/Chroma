#include <cmath>
#include <cstdlib>
#include <filesystem>
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

void gaussian_blur_spreads_premultiplied_pixels_symmetrically() {
  chroma::Surface source(5, 5);
  source.pixel(2, 2) = chroma::Pixel{.red = 1.0F, .alpha = 1.0F};
  const auto blurred = chroma::apply_effect_program_cpu(
      std::move(source),
      chroma::EffectProgram({chroma::GaussianBlurEffect{.radius = 1.0}}));
  CHECK(blurred.pixel(2, 2).alpha > blurred.pixel(1, 2).alpha);
  CHECK(blurred.pixel(1, 2) == blurred.pixel(3, 2));
  CHECK(blurred.pixel(2, 1) == blurred.pixel(2, 3));
  CHECK(near(blurred.pixel(1, 2).alpha, blurred.pixel(2, 1).alpha));
  CHECK(near(blurred.pixel(1, 2).red, blurred.pixel(2, 1).red));
  CHECK(blurred.pixel(2, 2).red == blurred.pixel(2, 2).alpha);
  CHECK(blurred.pixel(0, 0).alpha > 0.0F);
}

void analytic_masks_apply_antialiased_premultiplied_coverage() {
  const auto white = chroma::Pixel{.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F};
  const auto rectangle = chroma::apply_effect_program_cpu(
      chroma::Surface(4, 4, white),
      chroma::EffectProgram({chroma::MaskEffect{.shape = chroma::MaskShape::rectangle,
                                                .x = 1.0,
                                                .y = 1.0,
                                                .width = 2.0,
                                                .height = 2.0}}));
  CHECK(rectangle.pixel(0, 0) == chroma::Pixel{});
  CHECK(rectangle.pixel(1, 1) == white);
  CHECK(rectangle.pixel(2, 2) == white);
  CHECK(rectangle.pixel(3, 3) == chroma::Pixel{});

  const auto ellipse = chroma::apply_effect_program_cpu(
      chroma::Surface(4, 4, white),
      chroma::EffectProgram({chroma::MaskEffect{.shape = chroma::MaskShape::ellipse,
                                                .x = 0.0,
                                                .y = 0.0,
                                                .width = 4.0,
                                                .height = 4.0}}));
  CHECK(ellipse.pixel(0, 0).alpha > 0.0F);
  CHECK(ellipse.pixel(0, 0).alpha < 1.0F);
  CHECK(ellipse.pixel(1, 1) == white);

  const auto inverted = chroma::apply_effect_program_cpu(
      chroma::Surface(4, 4, white),
      chroma::EffectProgram({chroma::MaskEffect{.shape = chroma::MaskShape::rectangle,
                                                .x = 1.0,
                                                .y = 1.0,
                                                .width = 2.0,
                                                .height = 2.0,
                                                .inverted = true}}));
  CHECK(inverted.pixel(0, 0) == white);
  CHECK(inverted.pixel(1, 1) == chroma::Pixel{});
}

void animated_opacity_builds_an_opaque_source_over_crossfade() {
  const chroma::Composition composition(
      1, 1, 1, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
      {chroma::SolidLayer{
           .id = "outgoing",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0}},
       chroma::SolidLayer{
           .id = "incoming",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(10)),
           .color = chroma::Color{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0},
           .effects = chroma::EffectProgram({chroma::AnimatedOpacityEffect{
               .keyframes = {{.frame = 0, .value = 0.0},
                             {.frame = 10, .value = 1.0}}}})}});
  const auto frame = chroma::render_frame_cpu(composition, chroma::FrameIndex(5));
  CHECK(near(frame.pixel(0, 0).red, 0.5F));
  CHECK(near(frame.pixel(0, 0).blue, 0.5F));
  CHECK(near(frame.pixel(0, 0).alpha, 1.0F));
}

void unavailable_cpu_effects_fail_explicitly() {
  CHECK_THROWS_AS(
      chroma::apply_effect_program_cpu(
          chroma::Surface(1, 1),
          chroma::EffectProgram({chroma::TemporalSampleEffect{.radius = 1}})),
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

void image_layers_decode_once_and_execute_effects(const char* path) {
  auto cached_path = std::filesystem::path(path);
  cached_path += ".cache-test.png";
  std::filesystem::copy_file(path, cached_path,
                             std::filesystem::copy_options::overwrite_existing);
  const chroma::Composition composition(
      1, 4, 4, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(2)),
      std::vector<chroma::VideoAsset>{},
      {chroma::ImageAsset{.id = "red", .path = cached_path}},
      {chroma::ImageLayer{
          .id = "image",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(2)),
          .asset_id = "red",
          .placement = {.fit = chroma::FitMode::contain},
          .effects = chroma::EffectProgram({chroma::OpacityEffect{.opacity = 0.5}})}});
  chroma::CpuRenderContext context;
  const auto first = context.execute(chroma::compile_frame(composition, chroma::FrameIndex(0)));
  std::filesystem::remove(cached_path);
  const auto second = context.execute(chroma::compile_frame(composition, chroma::FrameIndex(1)));
  CHECK(first.pixels() == second.pixels());
  CHECK(first.pixel(0, 0).alpha == 0.5F);
  CHECK(first.pixel(0, 0).red > 0.45F);
  CHECK(first.pixel(0, 0).green < 0.05F);
  CHECK(first.pixel(0, 0).blue < 0.05F);
}

chroma::ColorMatrixEffect test_color_matrix() {
  return chroma::ColorMatrixEffect{
      .values = {0.8, 0.1, 0.0, 0.0, 0.02,
                 0.0, 0.7, 0.2, 0.0, 0.01,
                 0.1, 0.0, 0.6, 0.0, 0.03,
                 0.0, 0.0, 0.0, 0.9, 0.05}};
}

chroma::Composition image_effect_composition(const char* path, std::int32_t width,
                                             std::int32_t height,
                                             chroma::SourcePlacement placement,
                                             chroma::EffectProgram effects,
                                             chroma::BlendMode blend_mode =
                                                 chroma::BlendMode::screen) {
  return chroma::Composition(
      1, width, height, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
      std::vector<chroma::VideoAsset>{},
      {chroma::ImageAsset{.id = "red", .path = path}},
      {chroma::SolidLayer{
           .id = "background",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
           .color = {.red = 0.0, .green = 0.0, .blue = 0.5, .alpha = 1.0}},
       chroma::ImageLayer{
           .id = "image",
           .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
           .asset_id = "red",
           .placement = placement,
           .effects = std::move(effects),
           .blend_mode = blend_mode}});
}

void fused_image_sampling_and_effects_match_the_reference_path(const char* path) {
  const auto composition = image_effect_composition(
      path, 23, 19,
      {.fit = chroma::FitMode::contain, .position_x = 0.2, .position_y = 0.8},
      chroma::EffectProgram({
          chroma::CropEffect{.x = 2, .y = 3, .width = 17, .height = 11},
          chroma::OpacityEffect{.opacity = 0.63}}));
  const auto plan = chroma::compile_frame(composition, chroma::FrameIndex(0));
  chroma::CpuRenderContext fused_context;
  chroma::CpuRenderContext reference_context;
  const auto fused = fused_context.execute(plan);
  const auto reference =
      reference_context.execute(plan, chroma::CpuExecutionMode::reference);
  CHECK(fused.pixels() == reference.pixels());
  CHECK(fused_context.last_execution_stats().fused_operations == 0);
  CHECK(fused_context.last_execution_stats().reference_operations == 1);
  CHECK(fused_context.last_execution_stats().full_frame_intermediates_materialized == 1);
  CHECK(reference_context.last_execution_stats().fused_operations == 0);
  CHECK(reference_context.last_execution_stats().reference_operations == 1);
}

void fused_transform_matches_reference_and_unsafe_order_falls_back(const char* path) {
  for (const auto blend_mode : {chroma::BlendMode::source_over, chroma::BlendMode::multiply,
                                chroma::BlendMode::screen, chroma::BlendMode::plus}) {
    const auto fused_composition = image_effect_composition(
        path, 32, 32, {.fit = chroma::FitMode::fill},
        chroma::EffectProgram({
            chroma::Transform2DEffect{.m00 = 0.95, .m01 = 0.1, .m02 = 0.25,
                                      .m10 = -0.05, .m11 = 0.9, .m12 = 1.5},
            chroma::CropEffect{.x = 1, .y = 2, .width = 28, .height = 26},
            chroma::OpacityEffect{.opacity = 0.7}}),
        blend_mode);
    const auto fused_plan =
        chroma::compile_frame(fused_composition, chroma::FrameIndex(0));
    chroma::CpuRenderContext fused_context;
    chroma::CpuRenderContext reference_context;
    const auto fused = fused_context.execute(fused_plan);
    const auto reference =
        reference_context.execute(fused_plan, chroma::CpuExecutionMode::reference);
    CHECK(fused.pixels() == reference.pixels());
    CHECK(fused_context.last_execution_stats().fused_operations == 1);
    CHECK(fused_context.last_execution_stats().full_frame_intermediates_avoided == 1);
  }

  const auto singular_composition = image_effect_composition(
      path, 32, 32, {.fit = chroma::FitMode::fill},
      chroma::EffectProgram({chroma::Transform2DEffect{.m00 = 0.0, .m11 = 0.0},
                             chroma::OpacityEffect{.opacity = 0.7}}));
  const auto singular_plan =
      chroma::compile_frame(singular_composition, chroma::FrameIndex(0));
  chroma::CpuRenderContext singular_context;
  chroma::CpuRenderContext singular_reference_context;
  const auto singular = singular_context.execute(singular_plan);
  const auto singular_reference =
      singular_reference_context.execute(singular_plan, chroma::CpuExecutionMode::reference);
  CHECK(singular.pixels() == singular_reference.pixels());
  CHECK(singular_context.last_execution_stats().fused_operations == 1);

  const auto fallback_composition = image_effect_composition(
      path, 32, 32, {.fit = chroma::FitMode::fill},
      chroma::EffectProgram({
          chroma::OpacityEffect{.opacity = 0.7},
          chroma::Transform2DEffect{.m02 = 1.0},
          chroma::CropEffect{.x = 2, .y = 2, .width = 28, .height = 28}}));
  const auto fallback_plan =
      chroma::compile_frame(fallback_composition, chroma::FrameIndex(0));
  chroma::CpuRenderContext fallback_context;
  chroma::CpuRenderContext fallback_reference_context;
  const auto fallback = fallback_context.execute(fallback_plan);
  const auto fallback_reference =
      fallback_reference_context.execute(fallback_plan, chroma::CpuExecutionMode::reference);
  CHECK(fallback.pixels() == fallback_reference.pixels());
  CHECK(fallback_context.last_execution_stats().fused_operations == 0);
  CHECK(fallback_context.last_execution_stats().reference_operations == 1);

  const auto color_composition = image_effect_composition(
      path, 32, 32, {.fit = chroma::FitMode::fill},
      chroma::EffectProgram({chroma::CropEffect{.x = 2, .y = 2, .width = 28, .height = 28},
                             test_color_matrix()}));
  const auto color_plan = chroma::compile_frame(color_composition, chroma::FrameIndex(0));
  chroma::CpuRenderContext color_context;
  chroma::CpuRenderContext color_reference_context;
  const auto color = color_context.execute(color_plan);
  const auto color_reference =
      color_reference_context.execute(color_plan, chroma::CpuExecutionMode::reference);
  CHECK(color.pixels() == color_reference.pixels());
  CHECK(color_context.last_execution_stats().fused_operations == 0);
  CHECK(color_context.last_execution_stats().reference_operations == 1);
}

chroma::Surface render_test_shape(chroma::ShapeGeometry geometry,
                                  std::optional<chroma::ShapeStroke> stroke = std::nullopt) {
  const chroma::Composition composition(
      1, 8, 8, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
      std::vector<chroma::VideoAsset>{}, std::vector<chroma::ImageAsset>{},
      {chroma::ShapeLayer{
          .id = "shape",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
          .geometry = geometry,
          .fill = {.red = 0.0, .green = 0.0, .blue = 1.0, .alpha = 1.0},
          .stroke = stroke}});
  return chroma::render_frame_cpu(composition, chroma::FrameIndex(0));
}

void native_shapes_render_fill_stroke_and_coverage() {
  const auto rectangle = render_test_shape(
      {.kind = chroma::ShapeKind::rectangle,
       .x = 1.0,
       .y = 1.0,
       .width = 6.0,
       .height = 6.0},
      chroma::ShapeStroke{
          .color = {.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0}, .width = 1.0});
  CHECK(rectangle.pixel(0, 0) == chroma::Pixel{});
  CHECK(near(rectangle.pixel(1, 1).red, 1.0F));
  CHECK(near(rectangle.pixel(1, 1).blue, 0.0F));
  CHECK(near(rectangle.pixel(3, 3).red, 0.0F));
  CHECK(near(rectangle.pixel(3, 3).blue, 1.0F));

  const auto rounded = render_test_shape({.kind = chroma::ShapeKind::rounded_rectangle,
                                          .x = 1.0,
                                          .y = 1.0,
                                          .width = 6.0,
                                          .height = 6.0,
                                          .corner_radius = 2.0});
  CHECK(rounded.pixel(1, 1).alpha > 0.0F);
  CHECK(rounded.pixel(1, 1).alpha < 1.0F);
  CHECK(near(rounded.pixel(3, 3).alpha, 1.0F));

  const auto ellipse = render_test_shape({.kind = chroma::ShapeKind::ellipse,
                                          .x = 1.0,
                                          .y = 1.0,
                                          .width = 6.0,
                                          .height = 6.0});
  CHECK(ellipse.pixel(1, 1).alpha < 1.0F);
  CHECK(near(ellipse.pixel(3, 3).alpha, 1.0F));
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
  gaussian_blur_spreads_premultiplied_pixels_symmetrically();
  analytic_masks_apply_antialiased_premultiplied_coverage();
  animated_opacity_builds_an_opaque_source_over_crossfade();
  unavailable_cpu_effects_fail_explicitly();
  blend_modes_follow_premultiplied_source_over_semantics();
  composition_effects_execute_through_the_frame_plan();
  parsed_effects_execute_end_to_end();
  native_shapes_render_fill_stroke_and_coverage();
  if (argc >= 2) {
    video_layers_decode_into_the_cpu_plan(argv[1]);
  }
  if (argc >= 3) {
    image_layers_decode_once_and_execute_effects(argv[2]);
    fused_image_sampling_and_effects_match_the_reference_path(argv[2]);
    fused_transform_matches_reference_and_unsafe_order_falls_back(argv[2]);
  }

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "render tests passed\n";
  return EXIT_SUCCESS;
}
