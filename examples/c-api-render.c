/* Plain C99 consumer: no C++ compiler or dependency headers are required. */
#include <chroma/chroma.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

struct render_observer { int show_progress; int64_t cancel_after; };
static int32_t CHROMA_CALL observe(const chroma_render_progress* p, void* user) {
  struct render_observer* observer = (struct render_observer*)user;
  if (observer->show_progress)
    fprintf(stderr, "phase=%u frames=%lld/%lld audio=%lld/%lld\n", p->phase,
            (long long)p->frames_encoded, (long long)p->total_frames,
            (long long)p->audio_samples_encoded, (long long)p->total_audio_samples);
  return observer->cancel_after > 0 && p->phase == CHROMA_RENDER_RENDERING &&
         p->frames_encoded >= observer->cancel_after;
}
int main(int argc, char** argv) {
  chroma_engine_options options = CHROMA_ENGINE_OPTIONS_INIT;
  chroma_engine* engine = NULL;
  chroma_composition* composition = NULL;
  chroma_string* diagnostics = NULL;
  chroma_string* profile = NULL;
  chroma_frame* frame = NULL;
  chroma_frame_info info;
  int result = 1, index;
  struct render_observer observer = {0, 0};
  chroma_video_render_options render_options = CHROMA_VIDEO_RENDER_OPTIONS_INIT;
  chroma_status status;
  if (argc < 3) {
    fprintf(stderr, "usage: c-api-render composition.json output.mp4 [--no-cache] [--progress] [--cancel-after frames]\n");
    return 2;
  }
  for (index = 3; index < argc; ++index) {
    if (strcmp(argv[index], "--no-cache") == 0) {
      options.image_cache_budget_bytes = 0;
      options.text_cache_budget_bytes = 0;
    } else if (strcmp(argv[index], "--progress") == 0) {
      observer.show_progress = 1;
    } else if (strcmp(argv[index], "--cancel-after") == 0 && index + 1 < argc) {
      char* end;
      long long count;
      ++index; errno = 0; count = strtoll(argv[index], &end, 10);
      if (errno || end == argv[index] || *end || count <= 0) return 2;
      observer.cancel_after = (int64_t)count;
    } else return 2;
  }
  if (chroma_engine_create(&options, &engine) != CHROMA_OK) goto done;
  if (chroma_composition_load_file(argv[1], &composition, &diagnostics) != CHROMA_OK) goto done;
  if (chroma_engine_render_frame(engine, composition, 0, &frame) != CHROMA_OK) goto done;
  if (chroma_frame_get_info(frame, &info, sizeof(info)) != CHROMA_OK) goto done;
  if (info.pixel_format != CHROMA_PIXEL_RGBA_F32_LINEAR_PREMULTIPLIED || !chroma_frame_data(frame)) goto done;
  if (observer.show_progress || observer.cancel_after) {
    render_options.progress = observe;
    render_options.user_data = &observer;
    status = chroma_engine_render_video_with_options(engine, composition, argv[2], &render_options, &profile);
  } else {
    status = chroma_engine_render_video(engine, composition, argv[2], &profile);
  }
  if (status == CHROMA_ERROR_CANCELLED) { result = 130; goto done; }
  if (status != CHROMA_OK) goto done;
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
