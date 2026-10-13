#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "chroma/core/rational.hpp"
#include "chroma/core/timeline.hpp"

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, int line) {
  if (!condition) {
    std::cerr << "line " << line << ": check failed: " << expression << '\n';
    ++failures;
  }
}

template <typename Exception>
void check_throws(const std::function<void()>& operation, std::string_view expression, int line) {
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

void rational_normalization() {
  CHECK(chroma::Rational(2, 4) == chroma::Rational(1, 2));
  CHECK(chroma::Rational(-2, -4) == chroma::Rational(1, 2));
  CHECK(chroma::Rational(2, -4) == chroma::Rational(-1, 2));
  CHECK(chroma::Rational(0, 99) == chroma::Rational(0, 1));
  CHECK(chroma::Rational(1, 3) < chroma::Rational(1, 2));
  CHECK_THROWS_AS(chroma::Rational(1, 0), std::invalid_argument);
}

void rational_scaling_reduces_before_multiplication() {
  const auto half = chroma::Rational(1, 2);
  CHECK(half.scaled_by(10) == chroma::Rational(5));
  CHECK(chroma::Rational(3, 10).scaled_by(5) == chroma::Rational(3, 2));
}

void frame_ranges_are_half_open() {
  const chroma::FrameRange range(chroma::FrameIndex(30), chroma::FrameIndex(60));
  CHECK(range.length() == 30);
  CHECK(range.contains(chroma::FrameIndex(30)));
  CHECK(range.contains(chroma::FrameIndex(59)));
  CHECK(!range.contains(chroma::FrameIndex(60)));
  CHECK_THROWS_AS(chroma::FrameRange(chroma::FrameIndex(2), chroma::FrameIndex(1)),
                  std::invalid_argument);
}

void frame_rate_conversion_is_exact() {
  const chroma::FrameRate ntsc(chroma::Rational(30000, 1001));
  CHECK(ntsc.time_at(chroma::FrameIndex(30000)) == chroma::Rational(1001));
  CHECK(ntsc.frame_at(chroma::Rational(1001), chroma::FrameRounding::floor) ==
        chroma::FrameIndex(30000));

  const chroma::FrameRate thirty(chroma::Rational(30));
  CHECK(thirty.frame_at(chroma::Rational(3, 2), chroma::FrameRounding::floor) ==
        chroma::FrameIndex(45));
}

void conversion_rounding_is_explicit() {
  const chroma::FrameRate rate(chroma::Rational(30));
  const auto just_after_one_frame = chroma::Rational(34, 1000);
  CHECK(rate.frame_at(just_after_one_frame, chroma::FrameRounding::floor) ==
        chroma::FrameIndex(1));
  CHECK(rate.frame_at(just_after_one_frame, chroma::FrameRounding::ceil) ==
        chroma::FrameIndex(2));
  CHECK(rate.frame_at(just_after_one_frame, chroma::FrameRounding::nearest) ==
        chroma::FrameIndex(1));
}

}  // namespace

int main() {
  rational_normalization();
  rational_scaling_reduces_before_multiplication();
  frame_ranges_are_half_open();
  frame_rate_conversion_is_exact();
  conversion_rounding_is_explicit();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "timeline tests passed\n";
  return EXIT_SUCCESS;
}
