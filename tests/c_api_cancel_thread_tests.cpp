#include <chroma/chroma.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <chrono>
struct State {
  std::mutex mutex;
  std::condition_variable wake;
  bool observed = false, requested = false;
};
int32_t CHROMA_CALL observe(const chroma_render_progress* p, void* user) {
  auto& s=*static_cast<State*>(user);
  if (p->phase != CHROMA_RENDER_RENDERING) return 0;
  std::unique_lock lock(s.mutex); s.observed=true; s.wake.notify_all();
  if(!s.wake.wait_for(lock,std::chrono::seconds(10),[&]{return s.requested;})) return 1;
  return 0; // The worker thread's token request must perform cancellation.
}
int main(int argc,char** argv) {
  if(!(chroma_capabilities() & CHROMA_CAP_FFMPEG))return 0;
  if(argc!=2)return 1;
  const char source[]=R"({"schema_version":1,"canvas":{"width":32,"height":18,"frame_rate":{"numerator":30,"denominator":1},"duration_frames":20},"layers":[]})";
  chroma_engine* engine=nullptr; chroma_composition* comp=nullptr; chroma_cancel_token* token=nullptr;
  if(chroma_engine_create(nullptr,&engine)!=CHROMA_OK || chroma_cancel_token_create(&token)!=CHROMA_OK ||
     chroma_composition_parse(source,sizeof(source)-1,nullptr,&comp,nullptr)!=CHROMA_OK)return 1;
  std::ofstream(argv[1])<<"preserved";
  State state;
  std::atomic<bool> handshake{false};
  std::thread worker([&]{
    std::unique_lock lock(state.mutex);
    handshake=state.wake.wait_for(lock,std::chrono::seconds(10),[&]{return state.observed;});
    chroma_cancel_token_request(token);
    state.requested=true; state.wake.notify_all();
  });
  chroma_video_render_options options=CHROMA_VIDEO_RENDER_OPTIONS_INIT;
  options.cancel_token=token; options.progress=observe; options.user_data=&state;
  chroma_string* profile=nullptr;
  const auto status=chroma_engine_render_video_with_options(engine,comp,argv[1],&options,&profile);
  worker.join();
  std::string contents; std::ifstream(argv[1])>>contents;
  const bool passed=handshake && status==CHROMA_ERROR_CANCELLED && !profile && contents=="preserved";
  chroma_cancel_token_destroy(token); chroma_composition_destroy(comp); chroma_engine_destroy(engine);
  std::filesystem::remove(argv[1]);
  return passed?0:1;
}
