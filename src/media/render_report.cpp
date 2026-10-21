#include "chroma/media/render_report.hpp"
#include "chroma/core/preflight.hpp"
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace chroma {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
Json rss() {
#if defined(__APPLE__) || defined(__linux__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF,&usage)!=0 || usage.ru_maxrss<0) return nullptr;
  auto bytes = static_cast<std::uint64_t>(usage.ru_maxrss);
#ifdef __linux__
  bytes *= 1024;
#endif
  return bytes;
#else
  return nullptr;
#endif
}
std::string phase_name(VideoRenderPhase phase) {
  switch(phase) {
    case VideoRenderPhase::preparing: return "preparing";
    case VideoRenderPhase::rendering: return "rendering";
    case VideoRenderPhase::finalizing: return "finalizing";
    case VideoRenderPhase::completed: return "completed";
  }
  return "unknown";
}
}
struct VideoRenderReport::Impl {
  std::filesystem::path output;
  CpuCacheConfig config;
  Clock::time_point start{Clock::now()};
  std::optional<Clock::time_point> end;
  Json value;
  Impl(std::filesystem::path path, CpuCacheConfig budgets) : output(std::move(path)),config(budgets) {
    value = {{"schema_version",1},{"kind","chroma.render-report"},{"status","failed"},{"phase","parsing"},
      {"output",{{"path",output.string()},{"published",false},{"metadata",nullptr},{"size_bytes",nullptr}}},
      {"progress",nullptr},{"profile",nullptr},{"diagnostics",Json::array()},
      {"cache",{{"image_budget_bytes",config.image_budget_bytes},{"text_budget_bytes",config.text_budget_bytes},{"usage",nullptr}}}};
  }
  void finish() { end=Clock::now(); }
  void add(const std::string& code, const std::string& message, const std::string& path,
           const std::string& action, const std::string& severity="error") {
    value["diagnostics"].push_back({{"severity",severity},{"code",code},{"path",path},
      {"message",message},{"suggested_action",action}});
  }
};
VideoRenderReport::VideoRenderReport(std::filesystem::path output, CpuCacheConfig config)
  : impl_(std::make_unique<Impl>(std::move(output),config)) {}
VideoRenderReport::~VideoRenderReport() = default;
void VideoRenderReport::diagnostic(const std::string& code,const std::string& message,
                                  const std::string& path,const std::string& action) {
  impl_->value["status"]="failed";
  impl_->add(code,message,path,action); impl_->finish();
}
VideoRenderResult VideoRenderReport::render(const Composition& composition, CpuExecutionMode mode,
                                          const VideoRenderControl& control) {
  auto& state=*impl_;
  state.value["phase"]="preparing";
  VideoRenderControl observed=control;
  observed.progress=[&](const VideoRenderProgress& p) {
    state.value["phase"]=phase_name(p.phase);
    state.value["progress"]={{"frames_encoded",p.frames_encoded},{"total_frames",p.total_frames},
      {"audio_samples_encoded",p.audio_samples_encoded},{"total_audio_samples",p.total_audio_samples},
      {"elapsed_nanoseconds",p.elapsed_nanoseconds}};
    return control.progress ? control.progress(p) : false;
  };
  observed.preflight=[&](const PreflightReport& report) {
    for(const auto& d:report.diagnostics) {
      state.add(d.code,d.message,d.path,"Inspect this asset and correct its path, format, or timeline range; rerun preflight.",d.severity);
      auto& diagnostic=state.value["diagnostics"].back();
      diagnostic["asset_id"]=d.asset_id; diagnostic["asset_path"]=d.asset_path;
    }
    if(control.preflight) control.preflight(report);
  };
  try {
    auto result=render_video_cpu(composition,state.output,state.config,mode,observed);
    state.value["status"]="succeeded";
    state.value["phase"]="completed";
    state.value["output"]["published"]=true;
    // Publication has committed. Ancillary stat errors cannot change its outcome.
    std::error_code error;
    const auto bytes=std::filesystem::file_size(state.output,error);
    if(!error) state.value["output"]["size_bytes"]=bytes;
    const auto duration=composition.frame_rate().time_at(FrameIndex(composition.range().length()));
    const auto& rate=composition.frame_rate().value();
    const bool audio=!composition.audio_tracks().empty();
    state.value["output"]["metadata"]={{"source","render_configuration"},{"width",composition.width()},
      {"height",composition.height()},{"frame_count",result.frames_encoded},{"frame_rate",{{"numerator",rate.numerator()},{"denominator",rate.denominator()}}},
      {"duration",{{"numerator",duration.numerator()},{"denominator",duration.denominator()}}},
      {"video_codec","h264"},{"pixel_format","yuv420p"},{"audio_codec",audio ? Json("aac") : Json(nullptr)},
      {"audio_sample_rate",audio ? Json(48000) : Json(nullptr)},{"audio_channels",audio ? Json(2) : Json(nullptr)}};
    state.value["profile"]=Json::parse(video_render_profile_json(result));
    state.value["cache"]["usage"]=state.value["profile"]["cache"];
    state.finish(); return result;
  } catch(const PreflightError&) {
    state.value["phase"]="preflight"; state.finish(); throw;
  } catch(const RenderCancelled& error) {
    state.value["status"]="cancelled";
    state.add("render.cancelled",error.what(),"/","Start a new render if the job is still wanted."); state.finish(); throw;
  } catch(const std::filesystem::filesystem_error& error) {
    state.add("output.io",error.what(),"/output","Check the destination directory, permissions, and available disk space."); state.finish(); throw;
  } catch(const std::invalid_argument& error) {
    state.add("render.invalid_argument",error.what(),"/","Check canvas dimensions, frame rate, and output arguments."); state.finish(); throw;
  } catch(const std::exception& error) {
    state.add("render.failed",error.what(),"/","Inspect the reported phase and message; check encoder support and input/output compatibility."); state.finish(); throw;
  }
}
std::string VideoRenderReport::json() const {
  const auto& state=*impl_;
  auto value=state.value;
  value["timings"]={{"wall_nanoseconds",std::chrono::duration_cast<std::chrono::nanoseconds>(state.end.value_or(Clock::now())-state.start).count()},
    {"stages_complete",!value["profile"].is_null()},
    {"milliseconds",value["profile"].is_null() ? Json(nullptr) : value["profile"]["milliseconds"]}};
  value["memory"]={{"process_peak_rss_bytes",rss()},{"scope","process_lifetime"},
    {"cache_accounting","retained resource storage, not total process memory"}};
  return value.dump(2);
}
} // namespace chroma
