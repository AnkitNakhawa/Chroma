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

}  // namespace

int main() {
  surface_checks_coordinates();
  inactive_layers_are_transparent();
  layers_composite_in_document_order();
  ppm_output_is_stable();
  frames_outside_the_composition_are_rejected();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "render tests passed\n";
  return EXIT_SUCCESS;
}
