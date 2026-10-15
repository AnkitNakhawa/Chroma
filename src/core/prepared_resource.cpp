#include "chroma/core/prepared_resource.hpp"

#include <bit>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace chroma {
namespace {

void append_component(std::string& output, std::string_view component) {
  output += std::to_string(component.size());
  output.push_back(':');
  output.append(component);
}

std::string double_bits(double value) {
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16)
         << std::bit_cast<std::uint64_t>(value);
  return output.str();
}

}  // namespace

std::string_view prepared_resource_kind_name(PreparedResourceKind kind) noexcept {
  switch (kind) {
    case PreparedResourceKind::image: return "image";
    case PreparedResourceKind::text: return "text";
  }
  return "unknown";
}

std::string prepared_image_request_key(const std::filesystem::path& path) {
  std::string output = "chroma-prepared-image-v1|";
  append_component(output, path.lexically_normal().generic_string());
  return output;
}

std::string prepared_text_request_key(const std::filesystem::path& font_path,
                                      std::string_view text, double font_size) {
  std::string output = "chroma-prepared-text-v1|";
  append_component(output, font_path.lexically_normal().generic_string());
  append_component(output, double_bits(font_size));
  append_component(output, text);
  return output;
}

std::string prepared_resource_identity(std::string_view request_key) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : request_key) {
    hash ^= static_cast<std::uint8_t>(byte);
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

}  // namespace chroma
