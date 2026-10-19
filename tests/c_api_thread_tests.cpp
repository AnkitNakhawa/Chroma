// Uses only the public C API; checks TLS errors and concurrent immutable reads.
#include "chroma/chroma.h"
#include <atomic>
#include <cstring>
#include <string>
#include <thread>

int main() {
  const char source[] = R"({"schema_version":1,"canvas":{"width":2,"height":2,"frame_rate":{"numerator":30,"denominator":1},"duration_frames":1},"layers":[{"id":"red","type":"solid","start_frame":0,"end_frame":1,"color":[1,0,0,0.5]}]})";
  chroma_composition* composition = nullptr;
  if (chroma_composition_parse(source, sizeof(source) - 1, nullptr, &composition, nullptr) != CHROMA_OK) return 1;
  if (chroma_engine_create(nullptr, nullptr) != CHROMA_ERROR_INVALID_ARGUMENT) return 1;
  const std::string parent_error(chroma_last_error());
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::atomic<int> failures{0};
  auto work = [&](bool parse_error) {
    if (parse_error) {
      chroma_composition* invalid = nullptr;
      if (chroma_composition_parse("{", 1, nullptr, &invalid, nullptr) != CHROMA_ERROR_INVALID_COMPOSITION) ++failures;
    } else {
      if (chroma_engine_clear_caches(nullptr) != CHROMA_ERROR_INVALID_ARGUMENT) ++failures;
    }
    const std::string own_error(chroma_last_error());
    if (own_error.empty() || own_error == parent_error) ++failures;
    ++ready;
    while (!go.load()) std::this_thread::yield();
    if (own_error != chroma_last_error()) ++failures;
    chroma_engine* engine = nullptr;
    chroma_frame* frame = nullptr;
    if (chroma_engine_create(nullptr, &engine) != CHROMA_OK) { ++failures; return; }
    if (chroma_last_error()[0] != '\0') ++failures;
    for (int index = 0; index < 20; ++index) {
      if (chroma_engine_render_frame(engine, composition, 0, &frame) != CHROMA_OK) { ++failures; break; }
      const auto* pixel = chroma_frame_data(frame);
      if (!pixel || pixel[0] != 0.5f || pixel[1] != 0.0f || pixel[3] != 0.5f) ++failures;
      chroma_frame_destroy(frame);
      frame = nullptr;
    }
    chroma_frame_destroy(frame);
    chroma_engine_destroy(engine);
  };
  std::thread first(work, true), second(work, false);
  while (ready.load() != 2) std::this_thread::yield();
  if (parent_error != chroma_last_error()) ++failures;
  go = true;
  first.join();
  second.join();
  if (parent_error != chroma_last_error()) ++failures;
  chroma_composition_destroy(composition);
  return failures.load() ? 1 : 0;
}
