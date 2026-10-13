#include "chroma/core/timeline.hpp"

#include <limits>
#include <stdexcept>

namespace chroma {
namespace {

using Wide = __int128_t;

std::int64_t narrow_nonnegative(Wide value) {
  if (value < 0 || value > static_cast<Wide>(std::numeric_limits<std::int64_t>::max())) {
    throw std::overflow_error("frame index overflow");
  }
  return static_cast<std::int64_t>(value);
}

}  // namespace

FrameIndex::FrameIndex(std::int64_t value) : value_(value) {
  if (value_ < 0) {
    throw std::invalid_argument("frame index must be non-negative");
  }
}

FrameRange::FrameRange(FrameIndex start, FrameIndex end) : start_(start), end_(end) {
  if (end_ < start_) {
    throw std::invalid_argument("frame range end must not precede start");
  }
}

std::int64_t FrameRange::length() const noexcept {
  return end_.value() - start_.value();
}

bool FrameRange::empty() const noexcept {
  return start_ == end_;
}

bool FrameRange::contains(FrameIndex frame) const noexcept {
  return frame >= start_ && frame < end_;
}

FrameRate::FrameRate(Rational frames_per_second) : frames_per_second_(frames_per_second) {
  if (frames_per_second_ <= Rational(0)) {
    throw std::invalid_argument("frame rate must be positive");
  }
}

Rational FrameRate::time_at(FrameIndex frame) const {
  return Rational(frames_per_second_.denominator(), frames_per_second_.numerator())
      .scaled_by(frame.value());
}

FrameIndex FrameRate::frame_at(Rational time, FrameRounding rounding) const {
  if (time < Rational(0)) {
    throw std::invalid_argument("timeline time must be non-negative");
  }

  const auto numerator = static_cast<Wide>(time.numerator()) *
                         static_cast<Wide>(frames_per_second_.numerator());
  const auto denominator = static_cast<Wide>(time.denominator()) *
                           static_cast<Wide>(frames_per_second_.denominator());

  Wide result = numerator / denominator;
  const auto remainder = numerator % denominator;
  if (rounding == FrameRounding::ceil && remainder != 0) {
    ++result;
  } else if (rounding == FrameRounding::nearest && remainder * 2 >= denominator) {
    ++result;
  }

  return FrameIndex(narrow_nonnegative(result));
}

}  // namespace chroma
