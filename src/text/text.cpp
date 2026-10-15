#include "chroma/text/text.hpp"

#include <cmath>
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
                          double font_size) {
#if CHROMA_HAS_TEXT
  if (font_path.empty() || text.empty() || !std::isfinite(font_size) || font_size <= 0.0 ||
      font_size > 4096.0) {
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
  hb_buffer_add_utf8(buffer.get(), text.data(), static_cast<int>(text.size()), 0,
                     static_cast<int>(text.size()));
  hb_buffer_guess_segment_properties(buffer.get());
  hb_shape(font.get(), buffer.get(), nullptr, 0);

  unsigned int glyph_count = 0;
  const auto* infos = hb_buffer_get_glyph_infos(buffer.get(), &glyph_count);
  const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &glyph_count);
  PreparedText prepared;
  prepared.glyphs.reserve(glyph_count);
  std::int64_t pen_x = 0;
  std::int64_t pen_y = 0;
  for (unsigned int index = 0; index < glyph_count; ++index) {
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
    pen_x += positions[index].x_advance;
    pen_y += positions[index].y_advance;
  }
  return prepared;
#else
  static_cast<void>(font_path);
  static_cast<void>(text);
  static_cast<void>(font_size);
  throw std::runtime_error("Chroma was built without HarfBuzz/FreeType text support");
#endif
}

}  // namespace chroma
