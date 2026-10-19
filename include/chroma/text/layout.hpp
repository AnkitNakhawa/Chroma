#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace chroma {

enum class TextWrap { word, none };
enum class TextAlign { left, center, right };
enum class TextOverflow { visible, clip, ellipsis };

struct TextLayout {
  std::optional<double> width;
  std::optional<double> height;
  TextWrap wrap{TextWrap::word};
  TextAlign align{TextAlign::left};
  double line_spacing{1.2};  // Baseline advance as a multiple of font size.
  std::optional<std::int32_t> max_lines;
  TextOverflow overflow{TextOverflow::visible};

  friend bool operator==(const TextLayout&, const TextLayout&) = default;
};

[[nodiscard]] inline bool valid_text_layout(const TextLayout& layout) noexcept {
  const auto dimension = [](std::optional<double> value) {
    return !value || (std::isfinite(*value) && *value > 0 && *value <= 65536);
  };
  return dimension(layout.width) && dimension(layout.height) &&
    std::isfinite(layout.line_spacing) && layout.line_spacing > 0 && layout.line_spacing <= 100 &&
    (!layout.max_lines || (*layout.max_lines > 0 && *layout.max_lines <= 16384)) &&
    (layout.wrap == TextWrap::word || layout.wrap == TextWrap::none) &&
    (layout.align == TextAlign::left || layout.align == TextAlign::center || layout.align == TextAlign::right) &&
    (layout.overflow == TextOverflow::visible || layout.overflow == TextOverflow::clip ||
     layout.overflow == TextOverflow::ellipsis) &&
    (layout.align == TextAlign::left || layout.width) &&
    (layout.overflow != TextOverflow::ellipsis || layout.width);
}

}  // namespace chroma
