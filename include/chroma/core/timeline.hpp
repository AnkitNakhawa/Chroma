#pragma once

#include <cstdint>

#include "chroma/core/rational.hpp"

namespace chroma {

class FrameIndex {
 public:
  explicit FrameIndex(std::int64_t value);

  [[nodiscard]] std::int64_t value() const noexcept { return value_; }
  friend bool operator==(const FrameIndex&, const FrameIndex&) = default;
  friend auto operator<=>(const FrameIndex&, const FrameIndex&) = default;

 private:
  std::int64_t value_;
};

class FrameRange {
 public:
  FrameRange(FrameIndex start, FrameIndex end);

  [[nodiscard]] FrameIndex start() const noexcept { return start_; }
  [[nodiscard]] FrameIndex end() const noexcept { return end_; }
  [[nodiscard]] std::int64_t length() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] bool contains(FrameIndex frame) const noexcept;

 private:
  FrameIndex start_;
  FrameIndex end_;
};

enum class FrameRounding {
  floor,
  ceil,
  nearest,
};

class FrameRate {
 public:
  explicit FrameRate(Rational frames_per_second);

  [[nodiscard]] const Rational& value() const noexcept { return frames_per_second_; }
  [[nodiscard]] Rational time_at(FrameIndex frame) const;
  [[nodiscard]] FrameIndex frame_at(Rational time, FrameRounding rounding) const;

 private:
  Rational frames_per_second_;
};

}  // namespace chroma
