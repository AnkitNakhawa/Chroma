#include <algorithm>
#include <cstdlib>
#include <chrono>
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

void text_cache_budgets_preserve_pixels_and_bound_retention(const std::filesystem::path& font_path) {
  const auto directory = std::filesystem::temp_directory_path() /
    ("chroma-text-budget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(directory);
  const auto a = directory / "a.ttf", b = directory / "b.ttf", c = directory / "c.ttf";
  for (const auto& destination : {a, b, c}) std::filesystem::copy_file(font_path, destination);
  auto plan = [&](const std::filesystem::path& source) {
    const chroma::Composition composition(1, 160, 64, chroma::FrameRate(chroma::Rational(30)),
      chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
      std::vector<chroma::VideoAsset>{}, std::vector<chroma::ImageAsset>{}, {{"font", source}},
      {chroma::TextLayer{.id = "title", .range = chroma::FrameRange(chroma::FrameIndex(0), chroma::FrameIndex(1)),
        .font_asset_id = "font", .text = "Hello", .font_size = 32, .x = 8, .y = 44, .color = {1, 0, 0, 1}}});
    return chroma::compile_frame(composition, chroma::FrameIndex(0));
  };
  const auto pa = plan(a), pb = plan(b), pc = plan(c);
  chroma::CpuRenderContext sizing;
  const auto expected = sizing.execute(pa);
  const auto bytes = sizing.cache_usage().text.resident_bytes;
  CHECK(bytes > 0 && sizing.cache_usage().images.entries == 0);
  chroma::CpuRenderContext bounded({0, bytes * 2});
  static_cast<void>(bounded.execute(pa));
  static_cast<void>(bounded.execute(pb));
  static_cast<void>(bounded.execute(pa));
  static_cast<void>(bounded.execute(pc));
  CHECK(bounded.cache_usage().text.evictions == 1 && bounded.cache_usage().text.entries == 2);
  CHECK(bounded.cache_usage().text.resident_bytes == bytes * 2);
  std::filesystem::remove(a);
  CHECK(bounded.execute(pa).pixels() == expected.pixels());
  std::filesystem::remove(b);
  bool failed = false;
  try { static_cast<void>(bounded.execute(pb)); } catch (const std::runtime_error&) { failed = true; }
  CHECK(failed);
  CHECK(bounded.cache_usage().text.load_failures == 1);
  CHECK(bounded.cache_usage().text.evictions == 1 && bounded.cache_usage().text.hits == 2);
  bounded.set_cache_budgets({0, 0});
  CHECK(bounded.cache_usage().text.entries == 0 && bounded.cache_usage().text.resident_bytes == 0);
  chroma::CpuRenderContext disabled({0, 0});
  CHECK(disabled.execute(pc).pixels() == expected.pixels());
  CHECK(disabled.execute(pc).pixels() == expected.pixels());
  CHECK(disabled.cache_usage().text.misses == 2 && disabled.cache_usage().text.bypasses == 2);
  chroma::CpuRenderContext oversized({0, bytes - 1});
  CHECK(oversized.execute(pc).pixels() == expected.pixels());
  CHECK(oversized.cache_usage().text.bypasses == 1 && oversized.cache_usage().text.entries == 0);
  bounded.set_cache_budgets({0, bytes});
  CHECK(bounded.execute(pc).pixels() == expected.pixels());
  bounded.clear_prepared_caches();
  CHECK(bounded.cache_usage().text.resident_bytes == 0 && bounded.cache_usage().text.entries == 0);
  CHECK(bounded.cache_usage().text.peak_resident_bytes == bytes * 2);
  std::filesystem::remove_all(directory);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2) {
    shaped_text_is_prepared_cached_and_rendered(argv[1]);
    text_cache_budgets_preserve_pixels_and_bound_retention(argv[1]);
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
