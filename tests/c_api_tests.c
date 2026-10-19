#ifdef __cplusplus
#error "This consumer must compile as C, not C++"
#endif
#include "chroma/chroma.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #value, chroma_last_error()); ++failures; } } while (0)
static const char source[] = "{\"schema_version\":1,\"canvas\":{\"width\":8,\"height\":4,\"frame_rate\":{\"numerator\":30000,\"denominator\":1001},\"duration_frames\":60},\"layers\":[{\"id\":\"red\",\"type\":\"solid\",\"start_frame\":0,\"end_frame\":60,\"color\":[1,0,0,1]}]}";

int main(int argc, char** argv) {
  chroma_engine* engine = NULL;
  chroma_composition* composition = NULL;
  chroma_string* text = NULL;
  chroma_string* profile = NULL;
  chroma_frame* frame = NULL;
  chroma_frame* invalid_frame = NULL;
  chroma_frame_info frame_info;
  chroma_engine_options options = CHROMA_ENGINE_OPTIONS_INIT;
  chroma_composition_info info;
  CHECK(chroma_abi_version() == 1);
  CHECK(strlen(chroma_version_string()) > 0);
  CHECK((chroma_capabilities() & CHROMA_CAP_CPU_RENDER) != 0);
  CHECK(chroma_engine_create(NULL, NULL) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(strlen(chroma_last_error()) > 0);
  options.abi_version = 999;
  CHECK(chroma_engine_create(&options, &engine) == CHROMA_ERROR_UNSUPPORTED_ABI);
  CHECK(engine == NULL);
  options.abi_version = CHROMA_ABI_VERSION;
  options.struct_size = 0;
  CHECK(chroma_engine_create(&options, &engine) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(engine == NULL);
  {
    struct future_options { chroma_engine_options known; uint64_t tail; } future;
    chroma_engine* temporary = NULL;
    future.known = options;
    future.known.struct_size = sizeof(future);
    future.tail = UINT64_C(123);
    CHECK(chroma_engine_create(&future.known, &temporary) == CHROMA_OK);
    chroma_engine_destroy(temporary);
  }
  options.struct_size = sizeof(options);
  options.image_cache_budget_bytes = 0;
  options.text_cache_budget_bytes = 0;
  CHECK(chroma_engine_create(&options, &engine) == CHROMA_OK);
  CHECK(chroma_last_error()[0] == '\0');
  CHECK(chroma_engine_cache_usage(engine, &text) == CHROMA_OK);
  CHECK(strstr(chroma_string_data(text), "\"budget_bytes\": 0") != NULL);
  CHECK(chroma_string_size(text) == strlen(chroma_string_data(text)));
  chroma_string_destroy(text);
  text = NULL;
  CHECK(chroma_composition_parse(source, sizeof(source) - 1, NULL, &composition, &text) == CHROMA_OK);
  CHECK(strcmp(chroma_string_data(text), "[]") == 0);
  chroma_string_destroy(text);
  text = NULL;
  CHECK(chroma_composition_get_info(composition, &info, sizeof(info)) == CHROMA_OK);
  {
    struct future_info { chroma_composition_info known; uint64_t tail; } future;
    future.tail = UINT64_C(123);
    CHECK(chroma_composition_get_info(composition, &future.known, sizeof(future)) == CHROMA_OK);
    CHECK(future.tail == UINT64_C(123));
  }
  CHECK(info.width == 8 && info.height == 4 && info.duration_frames == 60);
  CHECK(info.frame_rate_numerator == 30000 && info.frame_rate_denominator == 1001);
  CHECK(chroma_composition_get_info(composition, &info, sizeof(info) - 1) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_composition_inspect_plan(composition, 0, 0, &text) == CHROMA_OK);
  CHECK(strstr(chroma_string_data(text), "composite_solid") != NULL);
  chroma_string_destroy(text);
  text = NULL;
  CHECK(chroma_composition_inspect_plan(composition, 60, 0, &text) == CHROMA_ERROR_OUT_OF_RANGE);
  CHECK(text == NULL);
  CHECK(chroma_composition_inspect_plan(composition, -1, 0, &text) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_composition_inspect_plan(composition, 0, 2, &text) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_engine_render_frame(engine, composition, 0, &frame) == CHROMA_OK);
  CHECK(chroma_frame_get_info(frame, &frame_info, sizeof(frame_info)) == CHROMA_OK);
  CHECK(frame_info.width == 8 && frame_info.height == 4);
  CHECK(frame_info.pixel_format == CHROMA_PIXEL_RGBA_F32_LINEAR_PREMULTIPLIED);
  CHECK(frame_info.row_stride_bytes == 8 * 4 * sizeof(float));
  CHECK(frame_info.data_bytes == 8 * 4 * 4 * sizeof(float));
  {
    unsigned index;
    const float* pixels = chroma_frame_data(frame);
    CHECK(pixels != NULL);
    for (index = 0; index < 32; ++index) {
      CHECK(pixels[index * 4] == 1.0f && pixels[index * 4 + 1] == 0.0f);
      CHECK(pixels[index * 4 + 2] == 0.0f && pixels[index * 4 + 3] == 1.0f);
    }
  }
  {
    char transient[sizeof(source)];
    chroma_composition* owned = NULL;
    chroma_frame* independent = NULL;
    memcpy(transient, source, sizeof(source));
    CHECK(chroma_composition_parse(transient, sizeof(source) - 1, NULL, &owned, NULL) == CHROMA_OK);
    memset(transient, 'x', sizeof(transient));
    CHECK(chroma_engine_render_frame(engine, owned, 0, &independent) == CHROMA_OK);
    CHECK(chroma_frame_data(independent)[0] == 1.0f);
    chroma_frame_destroy(independent);
    chroma_composition_destroy(owned);
  }
  CHECK(chroma_engine_render_frame(engine, composition, 60, &invalid_frame) == CHROMA_ERROR_OUT_OF_RANGE);
  CHECK(invalid_frame == NULL);
  CHECK(chroma_engine_render_frame(NULL, composition, 0, &invalid_frame) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_engine_render_frame(engine, composition, -1, &invalid_frame) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_engine_render_frame(engine, composition, 0, NULL) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_frame_get_info(frame, &frame_info, sizeof(frame_info) - 1) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_frame_get_info(NULL, &frame_info, sizeof(frame_info)) == CHROMA_ERROR_INVALID_ARGUMENT);
  if (argc == 2) {
    char input_path[4096];
    FILE* input;
    chroma_composition* loaded = NULL;
    snprintf(input_path, sizeof(input_path), "%s.json", argv[1]);
    input = fopen(input_path, "wb");
    CHECK(input != NULL);
    if (input) {
      CHECK(fwrite(source, 1, sizeof(source) - 1, input) == sizeof(source) - 1);
      CHECK(fclose(input) == 0);
      CHECK(chroma_composition_load_file(input_path, &loaded, NULL) == CHROMA_OK);
      chroma_composition_destroy(loaded);
      remove(input_path);
    }
    if ((chroma_capabilities() & CHROMA_CAP_FFMPEG) != 0) {
      CHECK(chroma_engine_render_video(engine, composition, argv[1], &profile) == CHROMA_OK);
      CHECK(strstr(chroma_string_data(profile), "\"frames_encoded\": 60") != NULL);
      CHECK(strstr(chroma_string_data(profile), "\"cache\"") != NULL);
      remove(argv[1]);
    } else {
      CHECK(chroma_engine_render_video(engine, composition, argv[1], &profile) == CHROMA_ERROR_UNAVAILABLE);
      CHECK(profile == NULL);
    }
  }
  CHECK(chroma_engine_render_video(engine, composition, "", NULL) == CHROMA_ERROR_INVALID_ARGUMENT);
  {
    chroma_composition* missing = NULL;
    CHECK(chroma_composition_load_file("/chroma/missing/composition.json", &missing, NULL) == CHROMA_ERROR_IO);
    CHECK(missing == NULL);
  }
  CHECK(chroma_engine_set_cache_budgets(engine, 1, 2) == CHROMA_OK);
  CHECK(chroma_engine_clear_caches(engine) == CHROMA_OK);
  chroma_engine_destroy(engine);
  chroma_composition_destroy(composition);
  CHECK(chroma_frame_data(frame)[0] == 1.0f); /* Independent frame lifetime. */
  chroma_frame_destroy(frame);
  if (profile) CHECK(chroma_string_size(profile) > 0); /* Independent string lifetime. */
  chroma_string_destroy(profile);
  composition = NULL;
  CHECK(chroma_composition_parse("{", 1, NULL, &composition, &text) == CHROMA_ERROR_INVALID_COMPOSITION);
  CHECK(composition == NULL && text != NULL);
  CHECK(strstr(chroma_string_data(text), "json") != NULL);
  chroma_string_destroy(text);
  text = NULL;
  CHECK(chroma_composition_parse(source, sizeof(source), NULL, &composition, &text) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_composition_parse(source, CHROMA_MAX_JSON_BYTES + 1, NULL, &composition, &text) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_composition_parse(NULL, 0, NULL, &composition, &text) == CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(chroma_engine_cache_usage(NULL, &text) == CHROMA_ERROR_INVALID_ARGUMENT && text == NULL);
  CHECK(chroma_string_data(NULL) == NULL && chroma_string_size(NULL) == 0);
  chroma_string_destroy(NULL);
  chroma_frame_destroy(NULL);
  CHECK(chroma_frame_data(NULL) == NULL);
  chroma_engine_destroy(NULL);
  chroma_composition_destroy(NULL);
  return failures ? 1 : 0;
}
