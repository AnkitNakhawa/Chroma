#include "chroma/core/rational.hpp"

#include <limits>
#include <stdexcept>

namespace chroma {
namespace {

using Wide = __int128_t;

std::uint64_t magnitude(std::int64_t value) noexcept {
  if (value >= 0) {
    return static_cast<std::uint64_t>(value);
  }
  return static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

std::uint64_t greatest_common_divisor(std::uint64_t lhs, std::uint64_t rhs) noexcept {
  while (rhs != 0U) {
    const auto remainder = lhs % rhs;
    lhs = rhs;
    rhs = remainder;
  }
  return lhs;
}

std::int64_t narrow(Wide value) {
  constexpr auto minimum = static_cast<Wide>(std::numeric_limits<std::int64_t>::min());
  constexpr auto maximum = static_cast<Wide>(std::numeric_limits<std::int64_t>::max());
  if (value < minimum || value > maximum) {
    throw std::overflow_error("rational arithmetic overflow");
  }
  return static_cast<std::int64_t>(value);
}

}  // namespace

Rational::Rational(std::int64_t numerator, std::int64_t denominator)
    : numerator_(numerator), denominator_(denominator) {
  if (denominator_ == 0) {
    throw std::invalid_argument("rational denominator must not be zero");
  }

  if (numerator_ == 0) {
    denominator_ = 1;
    return;
  }

  if (denominator_ < 0) {
    if (denominator_ == std::numeric_limits<std::int64_t>::min() ||
        numerator_ == std::numeric_limits<std::int64_t>::min()) {
      throw std::overflow_error("rational sign normalization overflow");
    }
    numerator_ = -numerator_;
    denominator_ = -denominator_;
  }

  const auto divisor = greatest_common_divisor(magnitude(numerator_), magnitude(denominator_));
  numerator_ /= static_cast<std::int64_t>(divisor);
  denominator_ /= static_cast<std::int64_t>(divisor);
}

double Rational::to_double() const noexcept {
  return static_cast<double>(numerator_) / static_cast<double>(denominator_);
}

Rational Rational::scaled_by(std::int64_t factor) const {
  if (factor == 0 || numerator_ == 0) {
    return Rational(0);
  }

  const auto divisor = greatest_common_divisor(magnitude(factor), magnitude(denominator_));
  const auto reduced_factor = factor / static_cast<std::int64_t>(divisor);
  const auto reduced_denominator = denominator_ / static_cast<std::int64_t>(divisor);
  return Rational(narrow(static_cast<Wide>(numerator_) * static_cast<Wide>(reduced_factor)),
                  reduced_denominator);
}

std::strong_ordering operator<=>(const Rational& lhs, const Rational& rhs) noexcept {
  const auto left = static_cast<Wide>(lhs.numerator_) * static_cast<Wide>(rhs.denominator_);
  const auto right = static_cast<Wide>(rhs.numerator_) * static_cast<Wide>(lhs.denominator_);
  if (left < right) {
    return std::strong_ordering::less;
  }
  if (left > right) {
    return std::strong_ordering::greater;
  }
  return std::strong_ordering::equal;
}

}  // namespace chroma
