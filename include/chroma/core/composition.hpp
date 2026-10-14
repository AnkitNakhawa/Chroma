#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chroma/core/timeline.hpp"

namespace chroma {

struct Diagnostic {
  std::string code;
  std::string path;
  std::string message;

  friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

struct Color {
  double red;
  double green;
  double blue;
  double alpha;

  friend bool operator==(const Color&, const Color&) = default;
};

struct SolidLayer {
  std::string id;
  FrameRange range;
  Color color;
};

class Composition {
 public:
  Composition(std::int32_t schema_version, std::int32_t width, std::int32_t height,
              FrameRate frame_rate, FrameRange range, std::vector<SolidLayer> layers);

  [[nodiscard]] std::int32_t schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] std::int32_t width() const noexcept { return width_; }
  [[nodiscard]] std::int32_t height() const noexcept { return height_; }
  [[nodiscard]] const FrameRate& frame_rate() const noexcept { return frame_rate_; }
  [[nodiscard]] const FrameRange& range() const noexcept { return range_; }
  [[nodiscard]] const std::vector<SolidLayer>& layers() const noexcept { return layers_; }

 private:
  std::int32_t schema_version_;
  std::int32_t width_;
  std::int32_t height_;
  FrameRate frame_rate_;
  FrameRange range_;
  std::vector<SolidLayer> layers_;
};

struct CompositionParseResult {
  std::optional<Composition> composition;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool valid() const noexcept {
    return composition.has_value() && diagnostics.empty();
  }
};

[[nodiscard]] CompositionParseResult parse_composition(std::string_view source);
[[nodiscard]] std::string diagnostics_json(const std::vector<Diagnostic>& diagnostics);

}  // namespace chroma
