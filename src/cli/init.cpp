#include "init.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace chroma::cli {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr int frames = 180;
constexpr double pi = 3.14159265358979323846;

struct Options {
  fs::path destination;
  std::string preset{"title-sequence"};
  std::string title{"Make an entrance."};
  std::string subtitle{"Your next story starts here."};
  std::array<double, 4> accent{0.35, 0.8, 0.72, 1};
  fs::path font, music;
  bool no_music{false};
};

void usage() {
  std::cout << "Usage: chroma init <directory> [--template title-sequence]\n"
      "       [--title <text>] [--subtitle <text>] [--accent '#RRGGBB']\n"
      "       [--font <file>] [--music <file> | --no-music]\n"
      "       chroma init --list-templates\n"
      "Creates a new project only; an existing destination is never overwritten.\n";
}

fs::path executable() {
#ifdef __APPLE__
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) throw std::runtime_error("cannot locate CLI executable");
  return fs::canonical(buffer.data());
#elif defined(__linux__)
  return fs::canonical("/proc/self/exe");
#elif defined(_WIN32)
  std::vector<wchar_t> buffer(32768);
  const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size == 0 || size >= buffer.size()) throw std::runtime_error("cannot locate CLI executable");
  return fs::canonical(std::wstring(buffer.data(), size));
#else
  throw std::runtime_error("template resource discovery is unsupported on this platform");
#endif
}

fs::path resources() {
  const auto folder = executable().parent_path();
  for (const auto& candidate : {folder / "share/chroma/templates", folder.parent_path() / "share/chroma/templates"}) {
    if (fs::is_regular_file(candidate / "Lato-Regular.ttf") && fs::is_regular_file(candidate / "OFL.txt")) return candidate;
  }
  throw std::runtime_error("template resources are missing; keep the complete Chroma SDK directory together");
}

void readable_file(const fs::path& path) {
  if (!fs::is_regular_file(path) || fs::file_size(path) == 0 || !std::ifstream(path, std::ios::binary))
    throw std::runtime_error("asset must be a readable, nonempty regular file: " + path.string());
}

void write(const fs::path& path, const std::string& content) {
  std::ofstream stream(path, std::ios::binary);
  stream.exceptions(std::ios::failbit | std::ios::badbit);
  stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  stream.close();
}

Json fade(int length, int delay = 0) {
  Json keys = Json::array({{{"frame",0},{"value",0},{"easing","ease_out"}}});
  if (delay > 0) keys.push_back({{"frame",delay},{"value",0},{"easing","ease_out"}});
  keys.push_back({{"frame",delay+16},{"value",1}});
  keys.push_back({{"frame",length-16},{"value",1}});
  keys.push_back({{"frame",length-1},{"value",0}});
  return {{"type","opacity"},{"keyframes",keys}};
}

Json motion(int length, int delay = 0) {
  return Json::array({fade(length, delay), {{"type", "translation"}, {"keyframes", Json::array({
    {{"frame", 0}, {"value", {0, 30}}, {"easing", "ease_out"}}, {{"frame", delay + 22}, {"value", {0, 0}}}})}}});
}

Json shape(const std::string& id, double x, double y, double w, double h, Json color, double radius = 20) {
  return {{"id", id}, {"type", "shape"}, {"shape", "rounded_rectangle"}, {"start_frame", 0}, {"end_frame", frames},
    {"bounds", {x,y,w,h}}, {"corner_radius", radius}, {"fill", color}};
}

Json text(const std::string& id, const std::string& content, double size, double x, double baseline,
          double width, Json color, int start = 0, int end = frames, std::string align = "left", int lines = 2) {
  return {{"id", id}, {"type", "text"}, {"font", "body"}, {"text", content}, {"font_size", size},
    {"position", {x,baseline}}, {"color", color}, {"start_frame", start}, {"end_frame", end},
    {"layout", {{"width", width}, {"wrap", "word"}, {"align", align}, {"line_spacing", 1.12},
                {"max_lines", lines}, {"overflow", "ellipsis"}}}, {"effects", motion(end - start)}};
}

Json composition(const Options& options) {
  Json doc = {{"schema_version",1}, {"canvas", {{"width",1280}, {"height",720},
    {"frame_rate", {{"numerator",30},{"denominator",1}}}, {"duration_frames",frames}}},
    {"assets",Json::array({{{"id","body"},{"type","font"},{"path","assets/font.ttf"}}})},
    {"layers",Json::array({{{"id","background"},{"type","solid"},{"start_frame",0},{"end_frame",frames},
      {"color",{0.018,0.025,0.045,1}}}})}};
  auto& layers = doc["layers"];
  auto halo = shape("halo", 900, -240, 650, 650, {0.06,0.12,0.15,1}, 320);
  halo["effects"] = Json::array({{{"type","translation"},{"keyframes",Json::array({
    {{"frame",0},{"value",{100,0}}}, {{"frame",179},{"value",{-100,90}}}})}}});
  layers.push_back(halo);
  auto rail = shape("accent-line", 580, 208, 120, 5, options.accent, 2);
  rail["effects"] = motion(frames);
  layers.push_back(rail);
  layers.push_back(text("eyebrow", "A NEW PERSPECTIVE", 18, 80, 178, 1120, options.accent, 0, frames, "center", 1));
  layers.push_back(text("title", options.title, 84, 80, 330, 1120, {0.95,0.97,1,1}, 8, frames, "center"));
  layers.push_back(text("subtitle", options.subtitle, 28, 140, 510, 1000, {0.6,0.67,0.76,1}, 24, frames, "center"));
  layers.push_back(text("footer", "CREATED WITH CHROMA", 15, 80, 650, 1120, {0.42,0.5,0.61,1}, 0, frames, "center", 1));
  if (!options.no_music) {
    doc["assets"].push_back({{"id","music"},{"type","audio"},{"path", options.music.empty() ? "assets/music.wav" : "assets/music" + options.music.extension().string()}});
    doc["audio_tracks"] = Json::array({{{"id","score"},{"asset","music"},{"start_frame",0},{"end_frame",frames},{"gain",0.6}}});
  }
  return doc;
}

// Original six-second cue: three soft chord changes with a resolved, faded tail.
void score(const fs::path& path) {
  constexpr std::uint32_t rate = 48000, count = rate * 6;
  std::ofstream stream(path, std::ios::binary);
  stream.exceptions(std::ios::failbit | std::ios::badbit);
  const auto number = [&](std::uint32_t value, int bytes) {
    for (int i=0; i<bytes; ++i) stream.put(static_cast<char>((value >> (i*8)) & 255));
  };
  stream.write("RIFF",4); number(36 + count*4,4); stream.write("WAVEfmt ",8); number(16,4);
  number(1,2); number(2,2); number(rate,4); number(rate*4,4); number(4,2); number(16,2);
  stream.write("data",4); number(count*4,4);
  const std::array<std::array<double,3>,3> chords{{{130.8128,164.8138,195.9977}, {110,130.8128,164.8138}, {130.8128,164.8138,195.9977}}};
  for (std::uint32_t i=0; i<count; ++i) {
    const double t = static_cast<double>(i) / rate;
    const double local = std::fmod(t,2.0);
    const double envelope = std::min(local / 0.12, 1.0) * std::min((2.0-local)/0.2,1.0)
      * std::min(t/0.25,1.0) * std::min((6.0-t)/0.6,1.0);
    double sample = 0;
    for (const double f : chords[static_cast<std::size_t>(t/2)]) sample += std::sin(2*pi*f*t) * 0.06;
    const auto pcm = static_cast<std::int16_t>(std::lround(sample*envelope*32767));
    number(static_cast<std::uint16_t>(pcm),2); number(static_cast<std::uint16_t>(pcm),2);
  }
  stream.close();
}

std::string project_readme(const Options& options) {
  return "# Your Chroma project\n\nTemplate: `" + options.preset + "` / 1280x720 / 30 fps / 6 seconds.\n\n"
    "Run these commands from this directory, using Chroma with media and text support:\n\n```sh\n"
    "chroma preflight scene.json\nchroma frame scene.json --frame 75 --output preview.ppm\n"
    "chroma render scene.json --output video.mp4 --progress text\n```\n\n"
    "Edit scene.json directly: title/subtitle text, colors, layer bounds and effect keyframes.\n"
    "Asset paths are relative to scene.json, so the complete project can be moved.\n"
    "Long titles wrap and ellipsize to fit their layout boxes. Ctrl-C cancels rendering safely.\n\n"
    "The included Lato font (when selected by default) retains its SIL Open Font License in assets/FONT-LICENSE.txt.\n"
    "The generated demo music is original Chroma material under assets/CHROMA-LICENSE.txt.\n"
    "Custom assets retain their original licenses. Supplied music must contain at least six seconds of audio.\n";
}

Options parse(int argc, char** argv) {
  if (argc < 1 || std::string_view(argv[0]).starts_with("--")) throw std::invalid_argument("a destination directory is required");
  Options options;
  options.destination = fs::absolute(argv[0]);
  std::map<std::string,bool> seen;
  for (int i=1; i<argc; ++i) {
    const std::string flag(argv[i]);
    if (seen[flag]) throw std::invalid_argument("duplicate option: " + flag);
    seen[flag] = true;
    if (flag == "--no-music") { options.no_music = true; continue; }
    if (i+1 >= argc) throw std::invalid_argument("missing value for " + flag);
    const std::string value(argv[++i]);
    if (value.empty()) throw std::invalid_argument("empty value for " + flag);
    if (flag == "--template") options.preset = value;
    else if (flag == "--title") options.title = value;
    else if (flag == "--subtitle") options.subtitle = value;
    else if (flag == "--font") options.font = fs::absolute(value);
    else if (flag == "--music") options.music = fs::absolute(value);
    else if (flag == "--accent") {
      if (value.size()!=7 || value[0]!='#') throw std::invalid_argument("accent must be #RRGGBB");
      const auto digit = [](char c) -> int {
        if(c>='0' && c<='9') return c-'0';
        if(c>='a' && c<='f') return c-'a'+10;
        if(c>='A' && c<='F') return c-'A'+10;
        throw std::invalid_argument("accent must be #RRGGBB");
      };
      for (std::size_t j=0;j<3;++j) options.accent[j]=static_cast<double>(digit(value[j*2+1])*16+digit(value[j*2+2]))/255;
    } else throw std::invalid_argument("unknown option: " + flag);
  }
  if (options.preset != "title-sequence") throw std::invalid_argument("unknown template: " + options.preset);
  if (options.no_music && !options.music.empty()) throw std::invalid_argument("--music and --no-music cannot be combined");
  return options;
}
} // namespace

int init_project(int argc, char** argv) {
  if (argc == 1 && std::string_view(argv[0]) == "--help") { usage(); return 0; }
  if (argc == 1 && std::string_view(argv[0]) == "--list-templates") {
    std::cout << "title-sequence  Animated editorial title, subtitle, and an original music cue\n"; return 0;
  }
  fs::path created;
  try {
    const auto options = parse(argc, argv);
    // Reject even dangling symlinks and empty directories. Never merge projects.
    if (fs::symlink_status(options.destination).type() != fs::file_type::not_found)
      throw std::runtime_error("destination already exists: " + options.destination.string());
    const auto source = resources();
    const auto font = options.font.empty() ? source/"Lato-Regular.ttf" : options.font;
    readable_file(font);
    if (!options.music.empty()) readable_file(options.music);
    const auto doc = composition(options).dump(2) + '\n';
    if (!fs::create_directory(options.destination)) throw std::runtime_error("destination already exists");
    created = options.destination;
    fs::create_directory(created/"assets");
    fs::copy_file(font, created/"assets/font.ttf");
    if (options.font.empty()) fs::copy_file(source/"OFL.txt",created/"assets/FONT-LICENSE.txt");
    fs::copy_file(source/"CHROMA-LICENSE.txt",created/"assets/CHROMA-LICENSE.txt");
    if (!options.no_music) {
      if (options.music.empty()) score(created/"assets/music.wav");
      else fs::copy_file(options.music,created/("assets/music" + options.music.extension().string()));
    }
    write(created/"scene.json",doc);
    write(created/"README.md",project_readme(options));
    write(created/".gitignore","video.mp4\npreview.ppm\n.chroma-cache/\n");
    created.clear();
    std::cout << "Created " << options.preset << " project: " << options.destination.string()
      << "\nFrom that directory, run:\n  chroma preflight scene.json\n  chroma render scene.json --output video.mp4 --progress text\n";
    return 0;
  } catch (const std::invalid_argument& error) {
    std::cerr << "init: " << error.what() << '\n'; usage(); return 2;
  } catch (const std::exception& error) {
    if (!created.empty()) { std::error_code ignored; fs::remove_all(created,ignored); }
    std::cerr << "init failed: " << error.what() << '\n'; return 4;
  }
}
} // namespace chroma::cli
