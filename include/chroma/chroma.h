#ifndef CHROMA_CHROMA_H
#define CHROMA_CHROMA_H

#include <stdint.h>

#if defined(_WIN32)
#  define CHROMA_CALL __cdecl
#  if defined(CHROMA_C_BUILD)
#    define CHROMA_API __declspec(dllexport)
#  else
#    define CHROMA_API __declspec(dllimport)
#  endif
#else
#  define CHROMA_CALL
#  define CHROMA_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
#  define CHROMA_NOEXCEPT noexcept
extern "C" {
#else
#  define CHROMA_NOEXCEPT
#endif

#define CHROMA_ABI_VERSION UINT32_C(1)
#define CHROMA_MAX_JSON_BYTES UINT64_C(67108864)

typedef int32_t chroma_status;
#define CHROMA_OK INT32_C(0)
#define CHROMA_ERROR_INVALID_ARGUMENT INT32_C(1)
#define CHROMA_ERROR_UNSUPPORTED_ABI INT32_C(2)
#define CHROMA_ERROR_INVALID_COMPOSITION INT32_C(3)
#define CHROMA_ERROR_OUT_OF_RANGE INT32_C(4)
#define CHROMA_ERROR_UNAVAILABLE INT32_C(5)
#define CHROMA_ERROR_OUT_OF_MEMORY INT32_C(6)
#define CHROMA_ERROR_IO INT32_C(7)
#define CHROMA_ERROR_RENDER INT32_C(8)
#define CHROMA_ERROR_INTERNAL INT32_C(9)

#define CHROMA_CAP_CPU_RENDER UINT64_C(1)
#define CHROMA_CAP_FFMPEG UINT64_C(2)
#define CHROMA_CAP_TEXT UINT64_C(4)

typedef struct chroma_engine chroma_engine;
typedef struct chroma_composition chroma_composition;
typedef struct chroma_string chroma_string;
typedef struct chroma_frame chroma_frame;

typedef struct chroma_engine_options {
  uint32_t struct_size;
  uint32_t abi_version;
  uint64_t image_cache_budget_bytes;
  uint64_t text_cache_budget_bytes;
} chroma_engine_options;
#define CHROMA_ENGINE_OPTIONS_INIT { (uint32_t)sizeof(chroma_engine_options), CHROMA_ABI_VERSION, UINT64_C(67108864), UINT64_C(16777216) }

typedef struct chroma_composition_info {
  uint32_t struct_size;
  int32_t width;
  int32_t height;
  int32_t schema_version;
  int64_t frame_rate_numerator;
  int64_t frame_rate_denominator;
  int64_t start_frame;
  int64_t duration_frames;
} chroma_composition_info;

#define CHROMA_PIXEL_RGBA_F32_LINEAR_PREMULTIPLIED UINT32_C(1)
typedef struct chroma_frame_info {
  uint32_t struct_size;
  int32_t width;
  int32_t height;
  uint32_t pixel_format;
  uint64_t row_stride_bytes;
  uint64_t data_bytes;
} chroma_frame_info;

CHROMA_API uint32_t CHROMA_CALL chroma_abi_version(void) CHROMA_NOEXCEPT;
CHROMA_API const char* CHROMA_CALL chroma_version_string(void) CHROMA_NOEXCEPT;
CHROMA_API uint64_t CHROMA_CALL chroma_capabilities(void) CHROMA_NOEXCEPT;
/* Borrowed thread-local message. Valid until this thread's next status-returning call. */
CHROMA_API const char* CHROMA_CALL chroma_last_error(void) CHROMA_NOEXCEPT;

/* NULL options selects defaults. Larger option structs are accepted; unknown tails ignored. */
CHROMA_API chroma_status CHROMA_CALL chroma_engine_create(const chroma_engine_options* options,
                                                        chroma_engine** out_engine) CHROMA_NOEXCEPT;
CHROMA_API void CHROMA_CALL chroma_engine_destroy(chroma_engine* engine) CHROMA_NOEXCEPT;
CHROMA_API chroma_status CHROMA_CALL chroma_engine_set_cache_budgets(chroma_engine* engine,
    uint64_t image_bytes, uint64_t text_bytes) CHROMA_NOEXCEPT;
CHROMA_API chroma_status CHROMA_CALL chroma_engine_clear_caches(chroma_engine* engine) CHROMA_NOEXCEPT;
CHROMA_API chroma_status CHROMA_CALL chroma_engine_cache_usage(chroma_engine* engine,
    chroma_string** out_json) CHROMA_NOEXCEPT;

/* JSON is UTF-8, explicitly sized, without NUL bytes, at most CHROMA_MAX_JSON_BYTES.
 * base_directory is a UTF-8 NUL-terminated path (NULL means no base directory).
 * On validation failure out_composition is NULL and optional diagnostics owns JSON.
 * Compositions are immutable and independent of engines and input buffers. */
CHROMA_API chroma_status CHROMA_CALL chroma_composition_parse(const char* json, uint64_t json_bytes,
    const char* base_directory, chroma_composition** out_composition,
    chroma_string** out_diagnostics) CHROMA_NOEXCEPT;
/* Loads a UTF-8 path and resolves assets relative to the file's parent directory. */
CHROMA_API chroma_status CHROMA_CALL chroma_composition_load_file(const char* path,
    chroma_composition** out_composition, chroma_string** out_diagnostics) CHROMA_NOEXCEPT;
CHROMA_API void CHROMA_CALL chroma_composition_destroy(chroma_composition* composition) CHROMA_NOEXCEPT;
CHROMA_API chroma_status CHROMA_CALL chroma_composition_get_info(const chroma_composition* composition,
    chroma_composition_info* out_info, uint64_t out_size) CHROMA_NOEXCEPT;
CHROMA_API chroma_status CHROMA_CALL chroma_composition_inspect_plan(const chroma_composition* composition,
    int64_t frame, uint32_t optimized, chroma_string** out_json) CHROMA_NOEXCEPT;

/* Frame output owns a tightly packed float RGBA copy, independent of engine,
 * composition, caches, and subsequent renders. Data lives until frame_destroy. */
CHROMA_API chroma_status CHROMA_CALL chroma_engine_render_frame(chroma_engine* engine,
    const chroma_composition* composition, int64_t frame, chroma_frame** out_frame) CHROMA_NOEXCEPT;
CHROMA_API chroma_status CHROMA_CALL chroma_frame_get_info(const chroma_frame* frame,
    chroma_frame_info* out_info, uint64_t out_size) CHROMA_NOEXCEPT;
CHROMA_API const float* CHROMA_CALL chroma_frame_data(const chroma_frame* frame) CHROMA_NOEXCEPT;
CHROMA_API void CHROMA_CALL chroma_frame_destroy(chroma_frame* frame) CHROMA_NOEXCEPT;
/* Synchronous video job using engine budgets and a fresh per-job cache context.
 * Optional profile JSON is caller-owned; engine frame-cache usage is unchanged.
 * No asynchronous jobs, callbacks, or cancellation are provided by ABI v1. */
CHROMA_API chroma_status CHROMA_CALL chroma_engine_render_video(chroma_engine* engine,
    const chroma_composition* composition, const char* output_path,
    chroma_string** out_profile) CHROMA_NOEXCEPT;

/* Returned strings are owned by the caller; always destroy using this library.
 * Data is borrowed, NUL-terminated, immutable, and excludes the terminator in size.
 * All destroy functions accept NULL. Invalid/destroyed/foreign handles are caller errors. */
CHROMA_API const char* CHROMA_CALL chroma_string_data(const chroma_string* string) CHROMA_NOEXCEPT;
CHROMA_API uint64_t CHROMA_CALL chroma_string_size(const chroma_string* string) CHROMA_NOEXCEPT;
CHROMA_API void CHROMA_CALL chroma_string_destroy(chroma_string* string) CHROMA_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif
