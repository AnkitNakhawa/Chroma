#include "chroma/core/preflight.hpp"
#include "chroma/media/encode.hpp"
#include "chroma/media/media.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int failures = 0;
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " << #value << '\n'; ++failures; } } while (false)
chroma::Composition scene(const std::string& body) {
  auto parsed = chroma::parse_composition(std::string(R"({"schema_version":1,"canvas":{"width":32,"height":18,
    "frame_rate":{"numerator":30,"denominator":1},"duration_frames":60},)") + body + "}");
  if (!parsed.composition) throw std::runtime_error(chroma::diagnostics_json(parsed.diagnostics));
  return std::move(*parsed.composition);
}
bool has(const chroma::PreflightReport& report, const std::string& code, const std::string& severity="error") {
  for (const auto& item : report.diagnostics) if (item.code == code && item.severity == severity) return true;
  return false;
}
}
int main() {
  const auto directory = std::filesystem::temp_directory_path() /
      ("chroma-preflight-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(directory);
  const auto source = scene(R"("layers":[])");
  CHECK(chroma::preflight_inputs(source, {.for_video=false}).ready());
  const auto video = chroma::preflight_inputs(source);
  if (!chroma::media_available()) CHECK(has(video, "output.backend_unavailable"));
  const auto used = scene(R"("assets":[{"id":"image","type":"image","path":"/chroma/nonexistent.png"}],
    "layers":[{"id":"image","type":"image","asset":"image","start_frame":0,"end_frame":60}])");
  const auto report = chroma::preflight_inputs(used, {.for_video=false});
  CHECK(!report.ready() && report.assets_checked == 1 && has(report,"asset.missing"));
  CHECK(report.diagnostics.front().asset_id == "image");
  CHECK(chroma::preflight_report_json(report).find("\"ready\": false") != std::string::npos);
  const auto unused = scene(R"("assets":[{"id":"image","type":"image","path":"/chroma/nonexistent.png"}],"layers":[])");
  const auto warning = chroma::preflight_inputs(unused, {.for_video=false});
  CHECK(warning.ready() && has(warning,"asset.missing","warning"));
  const auto before = std::filesystem::current_path();
  std::filesystem::current_path(directory);
  {
    std::ofstream("empty.ttf");
    std::ofstream("bad.ttf") << "not a font";
    const auto font = [&](std::string path) {
      return scene("\"assets\":[{\"id\":\"font\",\"type\":\"font\",\"path\":\"" + path +
        R"("}],"layers":[{"id":"title","type":"text","font":"font","text":"Hello","font_size":24,
        "position":[0,24],"color":[1,1,1,1],"start_frame":0,"end_frame":60}])");
    };
    CHECK(has(chroma::preflight_inputs(font("empty.ttf"), {.for_video=false}),"asset.empty"));
    CHECK(has(chroma::preflight_inputs(font("."), {.for_video=false}),"asset.not_regular"));
    CHECK(!chroma::preflight_inputs(font("bad.ttf"), {.for_video=false}).ready());
    const auto overflow = scene(R"("assets":[{"id":"video","type":"video","path":"bad.ttf"}],
      "layers":[{"id":"clip","type":"video","asset":"video","source_start_frame":9223372036854775807,
      "start_frame":0,"end_frame":60}])");
    CHECK(has(chroma::preflight_inputs(overflow, {.for_video=false}), "timeline.video_range_overflow"));
  }
  bool cancelled = false;
  try { static_cast<void>(chroma::preflight_inputs(source, {.for_video=false,.cancelled=[] { return true; }})); }
  catch (const chroma::RenderCancelled&) { cancelled = true; }
  CHECK(cancelled);
  chroma::PreflightError error(report);
  CHECK(std::string(error.what()).find("asset.missing") != std::string::npos && !error.report().ready());
  std::filesystem::current_path(before);
  std::filesystem::remove_all(directory);
  return failures ? 1 : 0;
}
