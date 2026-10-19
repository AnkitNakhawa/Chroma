#include "chroma/chroma.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "chroma/core/composition.hpp"
#include "chroma/core/execution_plan.hpp"
#include "chroma/core/render.hpp"
#include "chroma/media/media.hpp"
#include "chroma/text/text.hpp"

struct chroma_engine {
  chroma::CpuCacheConfig config;
  chroma::CpuRenderContext context;
  explicit chroma_engine(chroma::CpuCacheConfig value) : config(value), context(value) {}
};
struct chroma_composition {
  chroma::Composition value;
};
struct chroma_string {
  std::string value;
};

namespace {
thread_local char error_message[2048]{};
chroma_status fail(chroma_status status, const char* message) noexcept {
  std::snprintf(error_message, sizeof(error_message), "%s", message);
  return status;
}
template <typename Operation>
chroma_status boundary(Operation operation, chroma_status fallback = CHROMA_ERROR_INTERNAL) noexcept {
  error_message[0] = '\0';
  try { return operation(); }
  catch (const std::bad_alloc&) { return fail(CHROMA_ERROR_OUT_OF_MEMORY, "allocation failed"); }
  catch (const std::invalid_argument& error) { return fail(CHROMA_ERROR_INVALID_ARGUMENT, error.what()); }
  catch (const std::out_of_range& error) { return fail(CHROMA_ERROR_OUT_OF_RANGE, error.what()); }
  catch (const std::overflow_error& error) { return fail(CHROMA_ERROR_OUT_OF_RANGE, error.what()); }
  catch (const std::filesystem::filesystem_error& error) { return fail(CHROMA_ERROR_IO, error.what()); }
  catch (const std::exception& error) { return fail(fallback, error.what()); }
  catch (...) { return fail(CHROMA_ERROR_INTERNAL, "unknown internal exception"); }
}
std::filesystem::path path_from_utf8(const char* path) {
  if (!path) return {};
  std::u8string utf8;
  for (const auto byte : std::string_view(path))
    utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
  return std::filesystem::path(utf8);
}
}  // namespace

extern "C" {
uint32_t CHROMA_CALL chroma_abi_version(void) noexcept { return CHROMA_ABI_VERSION; }
const char* CHROMA_CALL chroma_version_string(void) noexcept { return CHROMA_VERSION; }
uint64_t CHROMA_CALL chroma_capabilities(void) noexcept {
  return CHROMA_CAP_CPU_RENDER | (chroma::media_available() ? CHROMA_CAP_FFMPEG : UINT64_C(0)) |
         (chroma::text_support_available() ? CHROMA_CAP_TEXT : UINT64_C(0));
}
const char* CHROMA_CALL chroma_last_error(void) noexcept { return error_message; }

chroma_status CHROMA_CALL chroma_engine_create(const chroma_engine_options* options,
                                               chroma_engine** out_engine) noexcept {
  if (out_engine) *out_engine = nullptr;
  return boundary([&]() -> chroma_status {
    if (!out_engine) return fail(CHROMA_ERROR_INVALID_ARGUMENT, "out_engine is required");
    chroma::CpuCacheConfig config;
    if (options) {
      if (options->struct_size < sizeof(chroma_engine_options))
        return fail(CHROMA_ERROR_INVALID_ARGUMENT, "engine options struct is too small");
      if (options->abi_version != CHROMA_ABI_VERSION)
        return fail(CHROMA_ERROR_UNSUPPORTED_ABI, "unsupported C ABI version");
      config = {options->image_cache_budget_bytes, options->text_cache_budget_bytes};
    }
    *out_engine = new chroma_engine(config);
    return CHROMA_OK;
  });
}
void CHROMA_CALL chroma_engine_destroy(chroma_engine* engine) noexcept { delete engine; }
chroma_status CHROMA_CALL chroma_engine_set_cache_budgets(chroma_engine* engine,
    uint64_t image_bytes, uint64_t text_bytes) noexcept {
  return boundary([&]() -> chroma_status {
    if (!engine) return fail(CHROMA_ERROR_INVALID_ARGUMENT, "engine is required");
    const chroma::CpuCacheConfig config{image_bytes, text_bytes};
    engine->context.set_cache_budgets(config);
    engine->config = config;
    return CHROMA_OK;
  });
}
chroma_status CHROMA_CALL chroma_engine_clear_caches(chroma_engine* engine) noexcept {
  return boundary([&]() -> chroma_status {
    if (!engine) return fail(CHROMA_ERROR_INVALID_ARGUMENT, "engine is required");
    engine->context.clear_prepared_caches();
    return CHROMA_OK;
  });
}
chroma_status CHROMA_CALL chroma_engine_cache_usage(chroma_engine* engine, chroma_string** out_json) noexcept {
  if (out_json) *out_json = nullptr;
  return boundary([&]() -> chroma_status {
    if (!engine || !out_json) return fail(CHROMA_ERROR_INVALID_ARGUMENT, "engine and out_json are required");
    *out_json = new chroma_string{chroma::cpu_cache_usage_json(engine->context.cache_usage())};
    return CHROMA_OK;
  });
}
chroma_status CHROMA_CALL chroma_composition_parse(const char* json, uint64_t json_bytes,
    const char* base_directory, chroma_composition** out_composition, chroma_string** out_diagnostics) noexcept {
  if (out_composition) *out_composition = nullptr;
  if (out_diagnostics) *out_diagnostics = nullptr;
  return boundary([&]() -> chroma_status {
    if (!json || !out_composition || json_bytes > CHROMA_MAX_JSON_BYTES)
      return fail(CHROMA_ERROR_INVALID_ARGUMENT, "JSON buffer, valid size, and out_composition are required");
    const std::string_view source(json, static_cast<std::size_t>(json_bytes));
    if (source.find('\0') != std::string_view::npos)
      return fail(CHROMA_ERROR_INVALID_ARGUMENT, "JSON buffer contains NUL bytes");
    auto parsed = chroma::parse_composition(source, path_from_utf8(base_directory));
    std::unique_ptr<chroma_string> diagnostics;
    if (out_diagnostics) diagnostics = std::make_unique<chroma_string>(chroma_string{chroma::diagnostics_json(parsed.diagnostics)});
    if (!parsed.valid()) {
      if (out_diagnostics) *out_diagnostics = diagnostics.release();
      return fail(CHROMA_ERROR_INVALID_COMPOSITION, "composition validation failed; inspect diagnostics JSON");
    }
    auto composition = std::make_unique<chroma_composition>(chroma_composition{std::move(*parsed.composition)});
    if (out_diagnostics) *out_diagnostics = diagnostics.release();
    *out_composition = composition.release();
    return CHROMA_OK;
  });
}
void CHROMA_CALL chroma_composition_destroy(chroma_composition* composition) noexcept { delete composition; }
chroma_status CHROMA_CALL chroma_composition_get_info(const chroma_composition* composition,
    chroma_composition_info* out_info, uint64_t out_size) noexcept {
  return boundary([&]() -> chroma_status {
    if (!composition || !out_info || out_size < sizeof(chroma_composition_info))
      return fail(CHROMA_ERROR_INVALID_ARGUMENT, "composition and full-sized info buffer are required");
    const auto& value = composition->value;
    *out_info = {sizeof(chroma_composition_info), value.width(), value.height(), value.schema_version(),
                value.frame_rate().value().numerator(), value.frame_rate().value().denominator(),
                value.range().start().value(), value.range().length()};
    return CHROMA_OK;
  });
}
chroma_status CHROMA_CALL chroma_composition_inspect_plan(const chroma_composition* composition,
    int64_t frame, uint32_t optimized, chroma_string** out_json) noexcept {
  if (out_json) *out_json = nullptr;
  return boundary([&]() -> chroma_status {
    if (!composition || !out_json || optimized > 1 || frame < 0)
      return fail(CHROMA_ERROR_INVALID_ARGUMENT, "composition, valid frame/mode, and out_json are required");
    const auto plan = chroma::compile_frame(composition->value, chroma::FrameIndex(frame));
    *out_json = new chroma_string{optimized ? chroma::optimized_execution_plan_json(plan) : chroma::execution_plan_json(plan)};
    return CHROMA_OK;
  });
}
const char* CHROMA_CALL chroma_string_data(const chroma_string* string) noexcept { return string ? string->value.c_str() : nullptr; }
uint64_t CHROMA_CALL chroma_string_size(const chroma_string* string) noexcept { return string ? string->value.size() : 0; }
void CHROMA_CALL chroma_string_destroy(chroma_string* string) noexcept { delete string; }
}
