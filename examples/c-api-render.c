/* Plain C99 consumer: no C++ compiler or dependency headers are required. */
#include <chroma/chroma.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  chroma_engine_options options = CHROMA_ENGINE_OPTIONS_INIT;
  chroma_engine* engine = NULL;
  chroma_composition* composition = NULL;
  chroma_string* diagnostics = NULL;
  chroma_string* profile = NULL;
  chroma_frame* frame = NULL;
  chroma_frame_info info;
  int result = 1;
  if (argc < 3 || argc > 4 || (argc == 4 && strcmp(argv[3], "--no-cache") != 0)) {
    fprintf(stderr, "usage: c-api-render composition.json output.mp4 [--no-cache]\n");
    return 2;
  }
  if (argc == 4) {
    options.image_cache_budget_bytes = 0;
    options.text_cache_budget_bytes = 0;
  }
  if (chroma_engine_create(&options, &engine) != CHROMA_OK) goto done;
  if (chroma_composition_load_file(argv[1], &composition, &diagnostics) != CHROMA_OK) goto done;
  if (chroma_engine_render_frame(engine, composition, 0, &frame) != CHROMA_OK) goto done;
  if (chroma_frame_get_info(frame, &info, sizeof(info)) != CHROMA_OK) goto done;
  if (info.pixel_format != CHROMA_PIXEL_RGBA_F32_LINEAR_PREMULTIPLIED || !chroma_frame_data(frame)) goto done;
  if (chroma_engine_render_video(engine, composition, argv[2], &profile) != CHROMA_OK) goto done;
  puts(chroma_string_data(profile));
  result = 0;
done:
  if (result) {
    fprintf(stderr, "%s\n", chroma_last_error());
    if (diagnostics) fprintf(stderr, "%s\n", chroma_string_data(diagnostics));
  }
  chroma_frame_destroy(frame);
  chroma_string_destroy(profile);
  chroma_string_destroy(diagnostics);
  chroma_composition_destroy(composition);
  chroma_engine_destroy(engine);
  return result;
}
