#include "chroma/text/text.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#ifndef CHROMA_HAS_TEXT
#define CHROMA_HAS_TEXT 0
#endif

#if CHROMA_HAS_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>
#endif

namespace chroma {

bool text_support_available() noexcept {
  return CHROMA_HAS_TEXT != 0;
}

PreparedText prepare_text(const std::filesystem::path& font_path, std::string_view text,
                          double font_size, const std::optional<TextLayout>& layout) {
#if CHROMA_HAS_TEXT
  if (font_path.empty() || text.empty() || !std::isfinite(font_size) || font_size <= 0.0 ||
      font_size > 4096.0 || text.size() > 16384 || (layout && !valid_text_layout(*layout))) {
    throw std::invalid_argument("text preparation parameters are invalid");
  }

  FT_Library raw_library = nullptr;
  if (FT_Init_FreeType(&raw_library) != 0) {
    throw std::runtime_error("unable to initialize FreeType");
  }
  const auto library_deleter = [](FT_Library library) { FT_Done_FreeType(library); };
  std::unique_ptr<std::remove_pointer_t<FT_Library>, decltype(library_deleter)> library(
      raw_library, library_deleter);

  FT_Face raw_face = nullptr;
  const auto path = font_path.string();
  if (FT_New_Face(library.get(), path.c_str(), 0, &raw_face) != 0) {
    throw std::runtime_error("unable to load font '" + path + "'");
  }
  const auto face_deleter = [](FT_Face face) { FT_Done_Face(face); };
  std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(face_deleter)> face(raw_face,
                                                                               face_deleter);
  const auto character_size = static_cast<FT_F26Dot6>(std::llround(font_size * 64.0));
  if (FT_Set_Char_Size(face.get(), 0, character_size, 72, 72) != 0) {
    throw std::runtime_error("unable to configure font size");
  }

  auto* raw_font = hb_ft_font_create_referenced(face.get());
  if (raw_font == nullptr) {
    throw std::runtime_error("unable to create HarfBuzz font");
  }
  const auto font_deleter = [](hb_font_t* font) { hb_font_destroy(font); };
  std::unique_ptr<hb_font_t, decltype(font_deleter)> font(raw_font, font_deleter);
  auto* raw_buffer = hb_buffer_create();
  if (raw_buffer == nullptr || !hb_buffer_allocation_successful(raw_buffer)) {
    if (raw_buffer != nullptr) {
      hb_buffer_destroy(raw_buffer);
    }
    throw std::runtime_error("unable to create HarfBuzz buffer");
  }
  const auto buffer_deleter = [](hb_buffer_t* buffer) { hb_buffer_destroy(buffer); };
  std::unique_ptr<hb_buffer_t, decltype(buffer_deleter)> buffer(raw_buffer, buffer_deleter);
  if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::length_error("text is too large to shape");
  }
  struct Shaped {
    PreparedText prepared;
    double advance{0};
    std::vector<std::size_t> boundaries;
  };
  auto shape = [&](std::string_view value, bool raster,
                   const hb_segment_properties_t* properties = nullptr) {
    hb_buffer_clear_contents(buffer.get());
    hb_buffer_add_utf8(buffer.get(), value.data(), static_cast<int>(value.size()), 0,
                       static_cast<int>(value.size()));
    if (properties) hb_buffer_set_segment_properties(buffer.get(), properties);
    else hb_buffer_guess_segment_properties(buffer.get());
    hb_shape(font.get(), buffer.get(), nullptr, 0);
    if (!hb_buffer_allocation_successful(buffer.get())) throw std::bad_alloc();
    unsigned int glyph_count = 0;
    const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &glyph_count);
    const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &glyph_count);
    Shaped shaped;
    auto& prepared = shaped.prepared;
    if (raster) prepared.glyphs.reserve(glyph_count);
    shaped.boundaries = {0, value.size()};
    std::int64_t pen_x = 0;
    std::int64_t pen_y = 0;
    for (unsigned int index = 0; index < glyph_count; ++index) {
      shaped.boundaries.push_back(infos[index].cluster);
      if (raster) {
        if (FT_Load_Glyph(face.get(), infos[index].codepoint, FT_LOAD_DEFAULT) != 0 ||
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
          throw std::runtime_error("unable to rasterize shaped glyph");
        }
        const auto& bitmap = face->glyph->bitmap;
        const auto width = static_cast<std::int32_t>(bitmap.width);
        const auto height = static_cast<std::int32_t>(bitmap.rows);
        const auto glyph_x = (pen_x + positions[index].x_offset) / 64 +
                             face->glyph->bitmap_left;
        const auto glyph_y = -(pen_y + positions[index].y_offset) / 64 -
                             face->glyph->bitmap_top;
        if (glyph_x < std::numeric_limits<std::int32_t>::min() ||
            glyph_x > std::numeric_limits<std::int32_t>::max() ||
            glyph_y < std::numeric_limits<std::int32_t>::min() ||
            glyph_y > std::numeric_limits<std::int32_t>::max()) {
          throw std::overflow_error("shaped glyph position exceeds the supported range");
        }
        PreparedGlyph glyph{
            .x = static_cast<std::int32_t>(glyph_x),
            .y = static_cast<std::int32_t>(glyph_y),
            .width = width,
            .height = height,
            .coverage = {}};
        glyph.coverage.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        for (std::int32_t row = 0; row < height; ++row) {
          const auto source_row = bitmap.pitch >= 0 ? row : height - 1 - row;
          const auto* source = bitmap.buffer +
                               static_cast<std::ptrdiff_t>(source_row) *
                                   static_cast<std::ptrdiff_t>(std::abs(bitmap.pitch));
          for (std::int32_t column = 0; column < width; ++column) {
            std::uint8_t coverage = 0;
            if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
              coverage = source[column];
            } else if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
              coverage = (source[column / 8] & (0x80U >> (column % 8))) != 0 ? 255U : 0U;
            } else if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
              coverage = source[static_cast<std::size_t>(column) * 4U + 3U];
            } else {
              throw std::runtime_error("font produced an unsupported glyph bitmap format");
            }
            glyph.coverage[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(column)] = coverage;
          }
        }
        prepared.glyphs.push_back(std::move(glyph));
      }
      pen_x += positions[index].x_advance;
      pen_y += positions[index].y_advance;
    }
    shaped.advance = std::abs(static_cast<double>(pen_x) / 64.0);
    std::sort(shaped.boundaries.begin(), shaped.boundaries.end());
    shaped.boundaries.erase(std::unique(shaped.boundaries.begin(), shaped.boundaries.end()), shaped.boundaries.end());
    return shaped;
  };
  if (!layout) return shape(text, true).prepared;  // Preserve legacy pixels exactly.

  struct Line { std::string text; hb_segment_properties_t properties; };
  std::vector<Line> lines;
  const auto whitespace = [](unsigned char ch) { return ch == ' ' || ch == '\t'; };
  auto trimmed = [&](std::string_view value) {
    while (!value.empty() && whitespace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
  };
  std::size_t paragraph_start = 0;
  while (paragraph_start <= text.size()) {
    auto end = text.find('\n', paragraph_start);
    if (end == std::string_view::npos) end = text.size();
    auto paragraph = text.substr(paragraph_start, end - paragraph_start);
    if (!paragraph.empty() && paragraph.back() == '\r') paragraph.remove_suffix(1);
    // Tabs are normalized to four spaces in layout mode.
    std::string normalized;
    for (const char ch : paragraph) {
      if (ch == '\t') normalized += "    ";
      else normalized += ch;
    }
    paragraph = normalized;
    const auto paragraph_shape = shape(paragraph, false);
    hb_segment_properties_t properties{};
    hb_buffer_get_segment_properties(buffer.get(), &properties);
    if (paragraph.empty()) lines.push_back({"", properties});
    if (!paragraph.empty() && (!layout->width || layout->wrap == TextWrap::none ||
                               paragraph_shape.advance <= *layout->width)) {
      lines.push_back({std::string(paragraph), properties});
    } else if (!paragraph.empty()) {
      const auto& boundaries = paragraph_shape.boundaries;
      std::size_t start = 0;
      const auto before = lines.size();
      while (start < paragraph.size()) {
        const auto first = std::upper_bound(boundaries.begin(), boundaries.end(), start);
        const auto available = static_cast<std::size_t>(boundaries.end() - first);
        const auto fits = [&](std::size_t count) {
          return shape(trimmed(paragraph.substr(start, first[count-1] - start)), false,
                       &properties).advance <= *layout->width;
        };
        // Exponential search keeps narrow boxes linear in paragraph length,
        // rather than repeatedly shaping the entire unconsumed suffix.
        std::size_t low = 1, high = 1;
        if (fits(1)) {
          while (high < available && fits(high)) {
            low = high;
            high = std::min(available, high * 2);
          }
          if (fits(high)) low = high;
          while (low < high) {
            const auto middle = (low + high + 1) / 2;
            if (fits(middle)) low = middle;
            else high = middle - 1;
          }
        }
        auto count = first[low-1] - start;
        // Prefer a whitespace boundary; otherwise break only at a shaped
        // grapheme cluster. An indivisible cluster may exceed the box width.
        for (auto boundary = first + static_cast<std::ptrdiff_t>(low); boundary != first;) {
          --boundary;
          if (whitespace(static_cast<unsigned char>(paragraph[*boundary-1]))) {
            count = *boundary - start;
            break;
          }
        }
        const auto line = trimmed(paragraph.substr(start, count));
        if (!line.empty()) lines.push_back({std::string(line), properties});
        start += count;
        auto next = std::upper_bound(boundaries.begin(), boundaries.end(), start);
        while (next != boundaries.end() && std::all_of(paragraph.begin() + static_cast<std::ptrdiff_t>(start),
               paragraph.begin() + static_cast<std::ptrdiff_t>(*next), whitespace)) {
          start = *next++;
        }
      }
      if (lines.size() == before) lines.push_back({"", properties});
    }
    if (end == text.size()) break;
    paragraph_start = end + 1;
  }

  const double ascent = static_cast<double>(face->size->metrics.ascender) / 64.0;
  const double descent = -static_cast<double>(face->size->metrics.descender) / 64.0;
  const double spacing = font_size * layout->line_spacing;
  std::size_t limit = lines.size();
  if (layout->max_lines) limit = std::min(limit, static_cast<std::size_t>(*layout->max_lines));
  if (layout->overflow != TextOverflow::visible) {
    if (layout->height) {
      const double fitting_count = spacing == 0 ? static_cast<double>(lines.size()) :
          layout->overflow == TextOverflow::clip ? std::ceil(*layout->height / spacing) :
          (*layout->height < ascent + descent ? 1.0 :
           std::floor((*layout->height - ascent - descent) / spacing) + 1.0);
      // Extremely small positive spacing can overflow the floating-point
      // quotient. Clamp to the finite line count BEFORE converting to size_t.
      const auto fitting = static_cast<std::size_t>(std::min(
          static_cast<double>(lines.size()), fitting_count));
      limit = std::min(limit, fitting);
    }
  }
  PreparedText result;
  result.truncated = limit < lines.size();
  for (std::size_t index = 0; index < limit; ++index) {
    auto& line = lines[index];
    auto measured = shape(line.text, false, &line.properties);
    if (layout->overflow == TextOverflow::ellipsis &&
        ((index + 1 == limit && result.truncated) || measured.advance > *layout->width)) {
      const std::string ellipsis = "\xe2\x80\xa6";
      const auto boundaries = measured.boundaries;
      std::size_t low = 0, high = boundaries.size() - 1;
      while (low < high) {
        const auto middle = (low + high + 1) / 2;
        const auto candidate = std::string(trimmed(std::string_view(line.text).substr(0, boundaries[middle]))) + ellipsis;
        if (shape(candidate, false, &line.properties).advance <= *layout->width) low = middle;
        else high = middle - 1;
      }
      line.text = std::string(trimmed(std::string_view(line.text).substr(0, boundaries[low]))) + ellipsis;
      if (shape(line.text, false, &line.properties).advance > *layout->width) line.text.clear();
      result.truncated = true;
    }
    auto shaped = shape(line.text, true, &line.properties);
    double x = 0;
    if (layout->align == TextAlign::center) x = (*layout->width - shaped.advance) / 2;
    if (layout->align == TextAlign::right) x = *layout->width - shaped.advance;
    const double baseline = static_cast<double>(index) * spacing;
    result.lines.push_back({shaped.advance, x, baseline});
    for (auto& glyph : shaped.prepared.glyphs) {
      const auto gx = static_cast<std::int64_t>(glyph.x) + static_cast<std::int64_t>(std::llround(x));
      const auto gy = static_cast<std::int64_t>(glyph.y) + static_cast<std::int64_t>(std::llround(baseline));
      if (gx < INT32_MIN || gx > INT32_MAX || gy < INT32_MIN || gy > INT32_MAX)
        throw std::overflow_error("text layout position exceeds supported range");
      glyph.x = static_cast<std::int32_t>(gx);
      glyph.y = static_cast<std::int32_t>(gy);
      if (layout->overflow != TextOverflow::visible) {
        const auto left = layout->width ? std::max<std::int64_t>(gx, 0) : gx;
        const auto right = layout->width ? std::min<std::int64_t>(gx + glyph.width, static_cast<std::int64_t>(std::floor(*layout->width))) : gx + glyph.width;
        const auto top = layout->height ? std::max<std::int64_t>(gy, static_cast<std::int64_t>(std::ceil(-ascent))) : gy;
        const auto bottom = layout->height ? std::min<std::int64_t>(gy + glyph.height, static_cast<std::int64_t>(std::floor(-ascent + *layout->height))) : gy + glyph.height;
        if (right <= left || bottom <= top) continue;
        std::vector<std::uint8_t> coverage(static_cast<std::size_t>((right-left)*(bottom-top)));
        for (auto row = top; row < bottom; ++row)
          std::copy_n(glyph.coverage.begin() + (row-gy)*glyph.width + (left-gx), right-left,
                      coverage.begin() + (row-top)*(right-left));
        glyph.x = static_cast<std::int32_t>(left); glyph.y = static_cast<std::int32_t>(top);
        glyph.width = static_cast<std::int32_t>(right-left); glyph.height = static_cast<std::int32_t>(bottom-top);
        glyph.coverage = std::move(coverage);
      }
      result.glyphs.push_back(std::move(glyph));
    }
  }
  return result;
#else
  static_cast<void>(font_path);
  static_cast<void>(text);
  static_cast<void>(font_size);
  static_cast<void>(layout);
  throw std::runtime_error("Chroma was built without HarfBuzz/FreeType text support");
#endif
}

}  // namespace chroma
