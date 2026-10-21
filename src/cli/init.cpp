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
  fs::path font, music, image, footage;
  bool no_music{false};
};

void usage() {
  std::cout << "Usage: chroma init <directory> [--template <name>]\n"
      "       [--title <text>] [--subtitle <text>] [--accent '#RRGGBB']\n"
      "       [--image <file>] [--footage <file>]\n"
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
  if (options.preset == "title-sequence") {
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
  } else if (options.preset == "product-promo") {
    auto glow = shape("product-glow",740,80,460,560,{0.05,0.1,0.14,1},80);
    glow["effects"] = motion(frames);
    layers.push_back(glow);
    layers.push_back(text("eyebrow","MEET YOUR NEXT FAVORITE",18,80,130,580,options.accent,0,frames,"left",1));
    layers.push_back(text("title",options.title,68,80,250,580,{0.95,0.97,1,1},8,frames));
    layers.push_back(text("subtitle",options.subtitle,27,80,445,560,{0.62,0.69,0.78,1},24,frames));
    auto button = shape("cta-button",80,554,260,58,options.accent,29);
    button["effects"] = motion(frames,20);
    layers.push_back(button);
    layers.push_back(text("cta","MAKE IT YOURS",17,96,591,228,{0.015,0.035,0.05,1},20,frames,"center",1));
    if (!options.image.empty()) {
      doc["assets"].push_back({{"id","product-image"},{"type","image"},{"path","assets/product" + options.image.extension().string()}});
      layers.push_back({{"id","product-image"},{"type","image"},{"asset","product-image"},{"fit","contain"},
        {"start_frame",0},{"end_frame",frames},{"effects",Json::array({
          {{"type","scale"},{"origin",{0,0}},{"value",{0.3,0.3}}},
          {{"type","translation"},{"value",{778,245}}},fade(frames)})}});
    } else {
      auto device = shape("device",840,145,260,420,{0.025,0.04,0.07,1},38);
      device["stroke"] = {{"color",{0.35,0.44,0.55,1}},{"width",2}};
      device["effects"] = motion(frames);
      layers.push_back(device);
      auto screen = shape("screen",860,170,220,370,{0.06,0.095,0.14,1},24);
      screen["effects"] = motion(frames);
      layers.push_back(screen);
      layers.push_back(text("app-name","STUDIO",20,880,218,180,{0.94,0.97,1,1},0,frames,"left",1));
      auto artwork = shape("artwork",880,250,180,140,options.accent,18);
      artwork["effects"] = Json::array({fade(frames),{{"type","rotation"},{"origin",{970,320}},
        {"keyframes",Json::array({{{"frame",0},{"value",-5}},{{"frame",179},{"value",5}}})}}});
      layers.push_back(artwork);
      for(int i=0;i<3;++i) {
        auto line = shape("ui-line-"+std::to_string(i),880,425+i*25,180-i*35,7,{0.3,0.42,0.5,1},3);
        line["effects"] = motion(frames,12);
        layers.push_back(line);
      }
    }
    layers.push_back(text("footer","YOUR PRODUCT. BEAUTIFULLY PRESENTED.",14,80,670,1120,{0.42,0.5,0.61,1},0,frames,"left",1));
  } else {
    doc["assets"].push_back({{"id","footage"},{"type","video"},{"path",options.footage.empty() ? "assets/footage.mp4" : "assets/footage" + options.footage.extension().string()}});
    layers.push_back({{"id","footage"},{"type","video"},{"asset","footage"},{"fit","cover"},{"start_frame",0},{"end_frame",frames}});
    layers.push_back(shape("header-shade",0,0,1280,205,{0.01,0.02,0.035,0.78},0));
    layers.push_back(text("eyebrow","FIELD NOTES / 01",17,80,68,1120,options.accent,0,frames,"left",1));
    layers.push_back(text("title",options.title,42,80,132,1100,{0.95,0.98,1,1},0,frames));
    const std::array<std::string,3> captions{options.subtitle,"Make room for what matters.","Bring your story into focus."};
    for(int i=0;i<3;++i) {
      auto plate = shape("caption-plate-"+std::to_string(i+1),64,514,1152,142,{0.012,0.025,0.04,0.88},18);
      plate["start_frame"]=i*60; plate["end_frame"]=(i+1)*60; plate["effects"]=motion(60);
      layers.push_back(plate);
      auto rail = shape("caption-accent-"+std::to_string(i+1),86,540,4,85,options.accent,2);
      rail["start_frame"]=i*60; rail["end_frame"]=(i+1)*60; rail["effects"]=motion(60);
      layers.push_back(rail);
      layers.push_back(text("caption-"+std::to_string(i+1),captions[static_cast<std::size_t>(i)],36,112,575,1060,
        {0.96,0.98,1,1},i*60,(i+1)*60));
    }
  }
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
    "The default captioned-footage clip is original abstract Chroma demo media, also Apache-2.0.\n"
    "Custom assets retain their original licenses. Supplied music and footage need at least six seconds.\n\n"
    "For product-promo, the optional product-image layer fits the supplied image inside the hero card.\n"
    "For captioned-footage, edit caption-1, caption-2 and caption-3 text and frame ranges.\n"
    "These are manually authored captions, not automatic transcription. --subtitle sets the first cue.\n"
    "Footage audio is not imported automatically; the score is a separate audio track.\n"
    "init copies files without transcoding; run preflight to check custom asset compatibility.\n";
}

Options parse(int argc, char** argv) {
  if (argc < 1 || std::string_view(argv[0]).empty() || std::string_view(argv[0]).starts_with("--")) throw std::invalid_argument("a destination directory is required");
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
    else if (flag == "--image") options.image = fs::absolute(value);
    else if (flag == "--footage") options.footage = fs::absolute(value);
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
  if (options.preset != "title-sequence" && options.preset != "product-promo" && options.preset != "captioned-footage")
    throw std::invalid_argument("unknown template: " + options.preset);
  if (!options.image.empty() && options.preset != "product-promo") throw std::invalid_argument("--image requires product-promo");
  if (!options.footage.empty() && options.preset != "captioned-footage") throw std::invalid_argument("--footage requires captioned-footage");
  if (!seen.contains("--title")) {
    if(options.preset == "product-promo") options.title = "A little everyday magic.";
    if(options.preset == "captioned-footage") options.title = "Find your next perspective.";
  }
  if (!seen.contains("--subtitle")) {
    if(options.preset == "product-promo") options.subtitle = "Designed for the way you move.";
    if(options.preset == "captioned-footage") options.subtitle = "Small details. A different perspective.";
  }
  if (options.no_music && !options.music.empty()) throw std::invalid_argument("--music and --no-music cannot be combined");
  return options;
}
} // namespace

int init_project(int argc, char** argv) {
  if (argc == 1 && std::string_view(argv[0]) == "--help") { usage(); return 0; }
  if (argc == 1 && std::string_view(argv[0]) == "--list-templates") {
    std::cout << "title-sequence    Animated editorial title and subtitle\n"
              << "product-promo     Product card, image or native device mockup, and call to action\n"
              << "captioned-footage Footage with three editable, timed caption cards\n"; return 0;
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
    if (!options.image.empty()) readable_file(options.image);
    if (!options.footage.empty()) readable_file(options.footage);
    if (options.preset == "captioned-footage" && options.footage.empty()) readable_file(source/"demo-footage.mp4");
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
    if (!options.image.empty()) fs::copy_file(options.image,created/("assets/product" + options.image.extension().string()));
    if (options.preset == "captioned-footage") {
      const auto footage = options.footage.empty() ? source/"demo-footage.mp4" : options.footage;
      fs::copy_file(footage,created/("assets/footage" + footage.extension().string()));
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
