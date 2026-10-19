/* Frozen ABI-v1 client: do not update this header snapshot to make regressions pass. */
#include "chroma_v1.h"
#include <stddef.h>
#include <string.h>

typedef char options_size_is_24[(sizeof(chroma_engine_options) == 24) ? 1 : -1];
typedef char info_size_is_48[(sizeof(chroma_composition_info) == 48) ? 1 : -1];
typedef char frame_size_is_32[(sizeof(chroma_frame_info) == 32) ? 1 : -1];
typedef char budget_offset_is_8[(offsetof(chroma_engine_options, image_cache_budget_bytes) == 8) ? 1 : -1];
typedef char rate_offset_is_16[(offsetof(chroma_composition_info, frame_rate_numerator) == 16) ? 1 : -1];

int main(void) {
  const char source[] = "{\"schema_version\":1,\"canvas\":{\"width\":2,\"height\":2,\"frame_rate\":{\"numerator\":30,\"denominator\":1},\"duration_frames\":1},\"layers\":[]}";
  chroma_engine_options options = CHROMA_ENGINE_OPTIONS_INIT;
  chroma_engine* engine = NULL;
  chroma_composition* composition = NULL;
  chroma_string* diagnostics = NULL;
  chroma_frame* frame = NULL;
  chroma_frame_info info;
  chroma_composition_info composition_info;
  chroma_string* usage = NULL;
  int result = 1;
  if (chroma_abi_version() != CHROMA_ABI_VERSION) goto done;
  if (chroma_engine_create(&options, &engine) != CHROMA_OK) goto done;
  if (chroma_engine_cache_usage(engine, &usage) != CHROMA_OK) goto done;
  if (!strstr(chroma_string_data(usage), "\"budget_bytes\": 67108864")) goto done;
  if (chroma_composition_parse(source, sizeof(source) - 1, NULL, &composition, &diagnostics) != CHROMA_OK) goto done;
  if (strcmp(chroma_string_data(diagnostics), "[]") != 0) goto done;
  if (chroma_composition_get_info(composition, &composition_info, sizeof(composition_info)) != CHROMA_OK) goto done;
  if (composition_info.width != 2 || composition_info.frame_rate_numerator != 30) goto done;
  if (chroma_engine_render_frame(engine, composition, 0, &frame) != CHROMA_OK) goto done;
  if (chroma_frame_get_info(frame, &info, sizeof(info)) != CHROMA_OK) goto done;
  if (info.data_bytes != 64 || info.row_stride_bytes != 32 || chroma_frame_data(frame)[3] != 0.0f) goto done;
  result = 0;
done:
  chroma_frame_destroy(frame);
  chroma_string_destroy(diagnostics);
  chroma_string_destroy(usage);
  chroma_composition_destroy(composition);
  chroma_engine_destroy(engine);
  return result;
}
