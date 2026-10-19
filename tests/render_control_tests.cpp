#include "chroma/media/encode.hpp"
#include "chroma/media/media.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; ++failures; } } while(false)
std::string contents(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
}
int main(int argc, char** argv) {
  if (!chroma::media_available()) return 0;
  if (argc != 2) return 1;
  const std::filesystem::path root(argv[1]);
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  const auto output = root / "existing.mp4";
  const auto parsed = chroma::parse_composition(R"({"schema_version":1,"canvas":{"width":32,"height":18,"frame_rate":{"numerator":30000,"denominator":1001},"duration_frames":8},"layers":[]})");
  CHECK(parsed.valid());
  if (!parsed.valid()) return 1;
  const auto& composition = *parsed.composition;
  for (const auto phase : {chroma::VideoRenderPhase::preparing, chroma::VideoRenderPhase::rendering,
                           chroma::VideoRenderPhase::finalizing}) {
    for (bool existing : {false, true}) {
      std::filesystem::remove(output);
      if (existing) std::ofstream(output) << "original";
      bool cancelled = false;
      chroma::VideoRenderControl control;
      control.progress = [&](const auto& progress) { return progress.phase == phase; };
      try { (void)chroma::render_video_cpu(composition, output, {}, chroma::CpuExecutionMode::fused, control); }
      catch (const chroma::RenderCancelled&) { cancelled = true; }
      CHECK(cancelled);
      CHECK(existing ? contents(output) == "original" : !std::filesystem::exists(output));
      CHECK(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == (existing ? 1 : 0));
    }
  }
  bool cancelled = false;
  chroma::VideoRenderControl requested;
  requested.cancelled = [] { return true; };
  try { (void)chroma::render_video_cpu(composition, output, {}, chroma::CpuExecutionMode::fused, requested); }
  catch (const chroma::RenderCancelled&) { cancelled = true; }
  CHECK(cancelled && contents(output) == "original");
  // Observer failures before commit must preserve existing files too.
  chroma::VideoRenderControl broken;
  broken.progress = [](const auto& p) -> bool {
    if (p.phase == chroma::VideoRenderPhase::rendering) throw std::runtime_error("observer failed");
    return false;
  };
  bool failed = false;
  try { (void)chroma::render_video_cpu(composition, output, {}, chroma::CpuExecutionMode::fused, broken); }
  catch (const std::runtime_error& e) { failed = std::string(e.what()) == "observer failed"; }
  CHECK(failed && contents(output) == "original");
  std::vector<chroma::VideoRenderProgress> events;
  chroma::VideoRenderControl control;
  control.progress = [&](const auto& p) {
    CHECK(p.total_frames == 8 && p.total_audio_samples == 0);
    if (!events.empty()) {
      CHECK(p.frames_encoded >= events.back().frames_encoded);
      CHECK(p.elapsed_nanoseconds >= events.back().elapsed_nanoseconds);
    }
    events.push_back(p);
    if (p.phase == chroma::VideoRenderPhase::completed) {
      CHECK(contents(output) != "original" && std::filesystem::file_size(output) > 0);
      return true; // Too late to cancel: a committed output must remain success.
    }
    return false;
  };
  const auto result = chroma::render_video_cpu(composition, output, {}, chroma::CpuExecutionMode::fused, control);
  CHECK(result.frames_encoded == 8);
  CHECK(events.size() == 11 && events.front().frames_encoded == 0 && events.back().frames_encoded == 8);
  CHECK(events.front().phase == chroma::VideoRenderPhase::preparing);
  CHECK(events.back().phase == chroma::VideoRenderPhase::completed);
  CHECK(std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}) == 1);
  std::filesystem::remove_all(root);
  return failures ? 1 : 0;
}
