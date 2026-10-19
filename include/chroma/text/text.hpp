#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "chroma/text/layout.hpp"

namespace chroma {

struct PreparedGlyph {
  std::int32_t x;
  std::int32_t y;
  std::int32_t width;
  std::int32_t height;
  std::vector<std::uint8_t> coverage;
};

struct PreparedTextLine {
  double advance;
  double x;
  double baseline;
};

struct PreparedText {
  std::vector<PreparedGlyph> glyphs;
  std::vector<PreparedTextLine> lines;
  bool truncated{false};
};

[[nodiscard]] bool text_support_available() noexcept;
[[nodiscard]] PreparedText prepare_text(const std::filesystem::path& font_path,
                                        std::string_view text, double font_size,
                                        const std::optional<TextLayout>& layout = std::nullopt);

}  // namespace chroma
