#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "chroma/core/render.hpp"
#include "chroma/text/text.hpp"

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

#define CHECK(expression) check((expression), #expression, __LINE__)
#define CHECK_THROWS_AS(expression, exception_type) \
  check_throws<exception_type>([&] { static_cast<void>(expression); }, #expression, __LINE__)

void disabled_backend_fails_explicitly() {
  CHECK(!chroma::text_support_available());
  CHECK_THROWS_AS(chroma::prepare_text("missing.ttf", "text", 16.0), std::runtime_error);
}

void shaped_text_is_prepared_cached_and_rendered(const std::filesystem::path& font_path) {
  CHECK(chroma::text_support_available());
  const auto shaped = chroma::prepare_text(font_path, "ffi", 32.0);
  CHECK(!shaped.glyphs.empty());
  CHECK(shaped.glyphs.size() <= 3);
  bool has_coverage = false;
  for (const auto& glyph : shaped.glyphs) {
    has_coverage = has_coverage ||
                   std::any_of(glyph.coverage.begin(), glyph.coverage.end(),
                               [](std::uint8_t value) { return value != 0; });
  }
  CHECK(has_coverage);

  const auto cached_font =
      std::filesystem::temp_directory_path() / "chroma-text-cache-test.ttf";
  std::filesystem::copy_file(font_path, cached_font,
                             std::filesystem::copy_options::overwrite_existing);
  const chroma::Composition composition(
      1, 160, 64, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(2)),
      std::vector<chroma::VideoAsset>{}, std::vector<chroma::ImageAsset>{},
      {chroma::FontAsset{.id = "body", .path = cached_font}},
      {chroma::TextLayer{
          .id = "title",
          .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(2)),
          .font_asset_id = "body",
          .text = "Hello",
          .font_size = 32.0,
          .x = 8.0,
          .y = 44.0,
          .color = {.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0},
          .effects = chroma::EffectProgram({chroma::OpacityEffect{.opacity = 0.5}})}});
  chroma::CpuRenderContext context;
  const auto first = context.execute(chroma::compile_frame(composition, chroma::FrameIndex(0)));
  std::filesystem::remove(cached_font);
  const auto second = context.execute(chroma::compile_frame(composition, chroma::FrameIndex(1)));
  CHECK(first.pixels() == second.pixels());
  const auto maximum_alpha =
      std::max_element(first.pixels().begin(), first.pixels().end(),
                       [](const chroma::Pixel& left, const chroma::Pixel& right) {
                         return left.alpha < right.alpha;
                       });
  CHECK(maximum_alpha != first.pixels().end());
  if (maximum_alpha != first.pixels().end()) {
    CHECK(maximum_alpha->alpha > 0.4F);
    CHECK(maximum_alpha->alpha <= 0.5F);
    CHECK(maximum_alpha->red == maximum_alpha->alpha);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2) {
    shaped_text_is_prepared_cached_and_rendered(argv[1]);
  } else if (chroma::text_support_available()) {
    CHECK(chroma::text_support_available());
  } else {
    disabled_backend_fails_explicitly();
  }

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "text tests passed\n";
  return EXIT_SUCCESS;
}
