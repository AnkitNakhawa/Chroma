/* Real C99 consumer of the additive ABI-v1 render control API. */
#include <chroma/chroma.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,chroma_last_error()); ++failures; } } while(0)
struct observer {
  uint32_t cancel_phase;
  int64_t last_frames, last_audio, last_elapsed;
  int calls, completed;
};
static int32_t CHROMA_CALL progress(const chroma_render_progress* p, void* data) {
  struct observer* o = (struct observer*)data;
  CHECK(p->struct_size >= sizeof(*p));
  CHECK(p->total_frames == 6 && p->total_audio_samples == 9610);
  CHECK(p->frames_encoded >= o->last_frames && p->frames_encoded <= 6);
  CHECK(p->audio_samples_encoded >= o->last_audio && p->audio_samples_encoded <= 9610);
  CHECK(p->elapsed_nanoseconds >= o->last_elapsed);
  o->last_frames = p->frames_encoded; o->last_audio = p->audio_samples_encoded;
  o->last_elapsed = p->elapsed_nanoseconds; ++o->calls;
  /* A callback may set this thread's error state through an unrelated call;
     the enclosing successful render must still clear it. */
  CHECK(chroma_engine_create(NULL, NULL) == CHROMA_ERROR_INVALID_ARGUMENT);
  if (p->phase == CHROMA_RENDER_COMPLETED) {
    ++o->completed;
    CHECK(p->frames_encoded == 6 && p->audio_samples_encoded == 9610);
    return 1; /* Must not cancel a committed output. */
  }
  return p->phase == o->cancel_phase && (p->phase != CHROMA_RENDER_RENDERING || p->frames_encoded == 3);
}
static void little(FILE* f, uint32_t n, int bytes) {
  int i; for (i=0;i<bytes;++i) fputc((int)((n >> (8*i)) & 255), f);
}
static void wave(const char* path) {
  FILE* f = fopen(path,"wb"); int i;
  CHECK(f != NULL); if (!f) return;
  fwrite("RIFF",1,4,f); little(f,192036,4); fwrite("WAVEfmt ",1,8,f); little(f,16,4);
  little(f,1,2); little(f,2,2); little(f,48000,4); little(f,192000,4); little(f,4,2); little(f,16,2);
  fwrite("data",1,4,f); little(f,192000,4);
  for(i=0;i<96000;++i) little(f,1000,2);
  CHECK(fclose(f)==0);
}
static void marker(const char* path) { FILE* f=fopen(path,"wb"); CHECK(f!=NULL); if(f){fputs("original",f);fclose(f);} }
static int preserved(const char* path) {
  char bytes[16]={0}; FILE* f=fopen(path,"rb"); if(!f)return 0;
  fread(bytes,1,15,f); fclose(f); return strcmp(bytes,"original")==0;
}
int main(int argc,char** argv) {
  const char source[]="{\"schema_version\":1,\"canvas\":{\"width\":32,\"height\":18,\"frame_rate\":{\"numerator\":30000,\"denominator\":1001},\"duration_frames\":6},\"assets\":[{\"id\":\"tone\",\"type\":\"audio\",\"path\":\"tone.wav\"}],\"layers\":[],\"audio_tracks\":[{\"id\":\"sound\",\"asset\":\"tone\",\"start_frame\":0,\"end_frame\":6}]}";
  chroma_engine* engine=NULL; chroma_composition* comp=NULL; chroma_cancel_token* token=NULL;
  chroma_video_render_options options=CHROMA_VIDEO_RENDER_OPTIONS_INIT;
  chroma_string* profile=NULL;
  char output[4096], audio[4096]; uint32_t phase;
  CHECK(argc==2); if(argc!=2)return 1;
  snprintf(output,sizeof(output),"%s/result.mp4",argv[1]); snprintf(audio,sizeof(audio),"%s/tone.wav",argv[1]);
  CHECK(chroma_cancel_token_create(NULL)==CHROMA_ERROR_INVALID_ARGUMENT);
  chroma_cancel_token_request(NULL); chroma_cancel_token_destroy(NULL);
  CHECK(chroma_cancel_token_create(&token)==CHROMA_OK);
  CHECK(chroma_engine_create(NULL,&engine)==CHROMA_OK);
  CHECK(chroma_composition_parse(source,sizeof(source)-1,argv[1],&comp,NULL)==CHROMA_OK);
  CHECK((chroma_capabilities() & CHROMA_CAP_RENDER_CONTROL)!=0);
  options.struct_size=1;
  CHECK(chroma_engine_render_video_with_options(engine,comp,output,&options,&profile)==CHROMA_ERROR_INVALID_ARGUMENT);
  CHECK(profile==NULL); options.struct_size=sizeof(options); options.abi_version=999;
  CHECK(chroma_engine_render_video_with_options(engine,comp,output,&options,NULL)==CHROMA_ERROR_UNSUPPORTED_ABI);
  options.abi_version=CHROMA_ABI_VERSION;
  if (!(chroma_capabilities() & CHROMA_CAP_FFMPEG)) {
    CHECK(chroma_engine_render_video_with_options(engine,comp,output,NULL,&profile)==CHROMA_ERROR_UNAVAILABLE);
  } else {
    wave(audio);
    for(phase=CHROMA_RENDER_PREPARING;phase<=CHROMA_RENDER_FINALIZING;++phase) {
      struct observer o={0}; o.cancel_phase=phase;
      marker(output); options.progress=progress; options.user_data=&o;
      CHECK(chroma_engine_render_video_with_options(engine,comp,output,&options,&profile)==CHROMA_ERROR_CANCELLED);
      CHECK(strstr(chroma_last_error(),"cancelled")!=NULL);
      CHECK(profile==NULL && preserved(output) && !o.completed);
    }
    {
      struct observer o={0}; o.cancel_phase=UINT32_MAX;
      struct future_options { chroma_video_render_options prefix; uint64_t tail; } future;
      options.user_data=&o; future.prefix=options; future.prefix.struct_size=sizeof(future); future.tail=123;
      CHECK(chroma_engine_render_video_with_options(engine,comp,output,&future.prefix,&profile)==CHROMA_OK);
      CHECK(profile!=NULL && o.completed==1 && o.calls==9 && !preserved(output));
      CHECK(chroma_last_error()[0]=='\0');
      CHECK(future.tail==123); chroma_string_destroy(profile); profile=NULL;
    }
    marker(output); options.progress=NULL; options.user_data=NULL; options.cancel_token=token;
    chroma_cancel_token_request(token);
    CHECK(chroma_engine_render_video_with_options(engine,comp,output,&options,&profile)==CHROMA_ERROR_CANCELLED);
    CHECK(profile==NULL && preserved(output));
    CHECK(chroma_engine_render_video_with_options(engine,comp,output,&options,NULL)==CHROMA_ERROR_CANCELLED);
    CHECK(preserved(output));
    CHECK(chroma_engine_render_video_with_options(engine,comp,output,NULL,&profile)==CHROMA_OK);
    chroma_string_destroy(profile); profile=NULL;
    remove(output); remove(audio);
  }
  chroma_cancel_token_destroy(token); chroma_composition_destroy(comp); chroma_engine_destroy(engine);
  return failures?1:0;
}
