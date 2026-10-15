#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace chroma {

enum class PreparedResourceKind {
  image,
  text,
};

[[nodiscard]] std::string_view prepared_resource_kind_name(PreparedResourceKind kind) noexcept;
[[nodiscard]] std::string prepared_image_request_key(const std::filesystem::path& path);
[[nodiscard]] std::string prepared_text_request_key(const std::filesystem::path& font_path,
                                                    std::string_view text, double font_size);
[[nodiscard]] std::string prepared_resource_identity(std::string_view request_key);

}  // namespace chroma
