#pragma once

#include <compare>
#include <cstdint>

namespace chroma {

class Rational {
 public:
  Rational(std::int64_t numerator, std::int64_t denominator = 1);

  [[nodiscard]] std::int64_t numerator() const noexcept { return numerator_; }
  [[nodiscard]] std::int64_t denominator() const noexcept { return denominator_; }
  [[nodiscard]] double to_double() const noexcept;
  [[nodiscard]] Rational scaled_by(std::int64_t factor) const;

  friend bool operator==(const Rational&, const Rational&) = default;
  friend std::strong_ordering operator<=>(const Rational& lhs, const Rational& rhs) noexcept;

 private:
  std::int64_t numerator_;
  std::int64_t denominator_;
};

}  // namespace chroma
