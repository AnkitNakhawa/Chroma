/* Real C99 caller: terminal reports remain owned even on non-OK job status. */
#include <chroma/chroma.h>
#include <stdio.h>
#include <string.h>
static int errors;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,chroma_last_error()); ++errors; } } while(0)
static int32_t CHROMA_CALL cancel(const chroma_render_progress* p,void* unused) {
  (void)unused; return p->phase==CHROMA_RENDER_PREPARING;
}
int main(int argc,char** argv) {
  const char* source="{\"schema_version\":1,\"canvas\":{\"width\":32,\"height\":18,\"frame_rate\":{\"numerator\":30,\"denominator\":1},\"duration_frames\":6},\"layers\":[{\"id\":\"bg\",\"type\":\"solid\",\"start_frame\":0,\"end_frame\":6,\"color\":[0.1,0.2,0.3,1]}]}";
  chroma_engine* engine=NULL; chroma_composition* scene=NULL;
  chroma_string* report=NULL; chroma_cancel_token* token=NULL;
  chroma_video_render_options options=CHROMA_VIDEO_RENDER_OPTIONS_INIT;
  if(argc!=2) return 2;
  CHECK(chroma_capabilities() & CHROMA_CAP_RENDER_REPORT);
  CHECK(chroma_engine_create(NULL,&engine)==CHROMA_OK);
  CHECK(chroma_composition_parse(source,strlen(source),NULL,&scene,NULL)==CHROMA_OK);
  CHECK(chroma_engine_render_video_report(engine,scene,argv[1],NULL,NULL)==CHROMA_ERROR_INVALID_ARGUMENT);
  options.abi_version=999;
  CHECK(chroma_engine_render_video_report(engine,scene,argv[1],&options,&report)==CHROMA_ERROR_UNSUPPORTED_ABI);
  CHECK(report==NULL);
  options.abi_version=CHROMA_ABI_VERSION;
  if(!(chroma_capabilities() & CHROMA_CAP_FFMPEG)) {
    CHECK(chroma_engine_render_video_report(engine,scene,argv[1],NULL,&report)==CHROMA_ERROR_UNAVAILABLE);
    CHECK(report && strstr(chroma_string_data(report),"render.unavailable"));
    chroma_string_destroy(report);
  } else {
    CHECK(chroma_engine_render_video_report(engine,scene,argv[1],NULL,&report)==CHROMA_OK);
    CHECK(report && strstr(chroma_string_data(report),"\"status\": \"succeeded\""));
    CHECK(strstr(chroma_string_data(report),"\"published\": true"));
    chroma_string_destroy(report); report=NULL;
    options.progress=cancel;
    CHECK(chroma_engine_render_video_report(engine,scene,argv[1],&options,&report)==CHROMA_ERROR_CANCELLED);
    CHECK(report && strstr(chroma_string_data(report),"\"status\": \"cancelled\""));
    CHECK(strstr(chroma_string_data(report),"\"published\": false"));
    chroma_string_destroy(report); report=NULL;
    CHECK(chroma_cancel_token_create(&token)==CHROMA_OK);
    chroma_cancel_token_request(token); options.cancel_token=token; options.progress=NULL;
    CHECK(chroma_engine_render_video_report(engine,scene,argv[1],&options,&report)==CHROMA_ERROR_CANCELLED);
    CHECK(report && chroma_string_size(report)>0); chroma_string_destroy(report);
    chroma_cancel_token_destroy(token);
    remove(argv[1]);
  }
  chroma_composition_destroy(scene); chroma_engine_destroy(engine);
  return errors ? 1 : 0;
}
