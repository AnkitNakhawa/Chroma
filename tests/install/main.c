#ifdef __cplusplus
#error "Installed consumer must compile as C"
#endif
#include <chroma/chroma.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  const char source[] = "{\"schema_version\":1,\"canvas\":{\"width\":2,\"height\":2,\"frame_rate\":{\"numerator\":30,\"denominator\":1},\"duration_frames\":1},\"layers\":[]}";
  chroma_engine_options options = CHROMA_ENGINE_OPTIONS_INIT;
  chroma_engine* engine = NULL;
  chroma_composition* composition = NULL;
  chroma_string* diagnostics = NULL;
  chroma_frame* frame = NULL;
  chroma_frame_info info;
  int result = 1;
  if (chroma_engine_create(&options, &engine) != CHROMA_OK) goto done;
  if (chroma_composition_parse(source, sizeof(source) - 1, NULL, &composition, &diagnostics) != CHROMA_OK) goto done;
  if (chroma_engine_render_frame(engine, composition, 0, &frame) != CHROMA_OK) goto done;
  if (chroma_frame_get_info(frame, &info, sizeof(info)) != CHROMA_OK) goto done;
  chroma_engine_destroy(engine);
  engine = NULL;
  chroma_composition_destroy(composition);
  composition = NULL;
  if (info.width != 2 || info.height != 2 || chroma_frame_data(frame)[3] != 0.0f) goto done;
  if (strcmp(chroma_string_data(diagnostics), "[]") != 0) goto done;
  result = 0;
done:
  if (result) fprintf(stderr, "%s\n", chroma_last_error());
  chroma_frame_destroy(frame);
  chroma_string_destroy(diagnostics);
  chroma_composition_destroy(composition);
  chroma_engine_destroy(engine);
  return result;
}
