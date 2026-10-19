#include "chroma/core/preflight.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "chroma/media/audio.hpp"
#include "chroma/media/encode.hpp"
#include "chroma/media/media.hpp"
#include "chroma/text/text.hpp"
#include <nlohmann/json.hpp>

#ifndef CHROMA_HAS_FFMPEG
#define CHROMA_HAS_FFMPEG 0
#endif
#if CHROMA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

namespace chroma {
namespace {
std::string summary(const PreflightReport& report) {
  std::string message = "input preflight failed";
  for (const auto& diagnostic : report.diagnostics) {
    if (diagnostic.severity != "error") continue;
    message += "; " + diagnostic.code + " at " + diagnostic.path;
    if (!diagnostic.asset_id.empty()) message += " (asset '" + diagnostic.asset_id + "')";
    message += ": " + diagnostic.message;
  }
  return message;
}
}

bool PreflightReport::ready() const noexcept {
  return std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) { return item.severity == "error"; });
}
PreflightError::PreflightError(PreflightReport report)
    : std::runtime_error(summary(report)), report_(std::move(report)) {}

PreflightReport preflight_inputs(const Composition& composition, const PreflightOptions& options) {
  PreflightReport report;
  const auto cancelled = [&] { if (options.cancelled && options.cancelled()) throw RenderCancelled(); };
  const auto add = [&](std::string code, std::string path, std::string message,
                       std::string id = {}, std::string file = {}, bool warning = false) {
    report.diagnostics.push_back({warning ? "warning" : "error", std::move(code), std::move(path),
                                  std::move(message), std::move(id), std::move(file)});
  };
  cancelled();
  if (options.for_video) {
    if (!media_available()) add("output.backend_unavailable", "/canvas", "FFmpeg encoding support is unavailable");
    if (composition.width() % 2 || composition.height() % 2)
      add("output.even_dimensions", "/canvas", "H.264 output requires even canvas dimensions");
    const auto& rate = composition.frame_rate().value();
    if (rate.numerator() > std::numeric_limits<int>::max() || rate.denominator() > std::numeric_limits<int>::max())
      add("output.frame_rate_range", "/canvas/frame_rate", "frame rate exceeds FFmpeg encoder limits");
#if CHROMA_HAS_FFMPEG
    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) add("output.video_encoder_unavailable", "/canvas", "no H.264 encoder is available");
    else if (std::string_view(encoder->name) == "libopenh264" && (composition.width() < 16 || composition.height() < 16))
      add("output.encoder_dimensions", "/canvas", "OpenH264 output requires canvas dimensions of at least 16 pixels");
    if (!composition.audio_tracks().empty() && !avcodec_find_encoder_by_name("aac"))
      add("output.audio_encoder_unavailable", "/audio_tracks", "no native AAC encoder is available");
#endif
  }

  std::unordered_set<std::string> used;
  std::unordered_map<std::string, std::vector<double>> font_sizes;
  for (const auto& layer : composition.layers()) {
    if (const auto* video = std::get_if<VideoLayer>(&layer)) used.insert(video->asset_id);
    else if (const auto* image = std::get_if<ImageLayer>(&layer)) used.insert(image->asset_id);
    else if (const auto* text = std::get_if<TextLayer>(&layer)) {
      used.insert(text->font_asset_id);
      auto& sizes = font_sizes[text->font_asset_id];
      if (std::find(sizes.begin(), sizes.end(), text->font_size) == sizes.end()) sizes.push_back(text->font_size);
    }
  }
  for (const auto& track : composition.audio_tracks()) used.insert(track.asset_id);
  std::unordered_map<std::string, InputMediaInfo> media;
  const auto asset = [&](const auto& item, bool font, InputMediaKind kind) {
    cancelled();
    ++report.assets_checked;
    const bool unused = !used.contains(item.id);
    const auto problem = [&](std::string code, std::string message, bool warning = false) {
      add(std::move(code), "/assets", std::move(message), item.id, item.path.string(), warning || unused);
    };
    if (item.path.native().find('\0') != std::filesystem::path::string_type::npos) {
      problem("asset.invalid_path", "asset paths must not contain NUL bytes"); return;
    }
    std::error_code error;
    const auto status = std::filesystem::status(item.path, error);
    if ((!error || error == std::errc::no_such_file_or_directory) && !std::filesystem::exists(status)) {
      problem("asset.missing", "file does not exist: " + item.path.string()); return;
    }
    if (error) { problem("asset.unreadable", "cannot inspect file: " + error.message()); return; }
    if (!std::filesystem::is_regular_file(status)) { problem("asset.not_regular", "asset must be a regular file"); return; }
    std::ifstream readable(item.path, std::ios::binary);
    if (!readable) { problem("asset.unreadable", "file cannot be opened for reading"); return; }
    if (readable.peek() == std::ifstream::traits_type::eof()) { problem("asset.empty", "file is empty or cannot be read"); return; }
    if (font ? !text_support_available() : !media_available()) {
      problem(font ? "font.backend_unavailable" : "media.backend_unavailable",
              font ? "HarfBuzz/FreeType support is unavailable" : "FFmpeg decoding support is unavailable"); return;
    }
    try {
      if (font) {
        auto sizes = font_sizes[item.id];
        if (sizes.empty()) sizes.push_back(12);
        for (const auto size : sizes) {
          cancelled();
          static_cast<void>(prepare_text(item.path, " ", size));
        }
      } else {
        auto info = inspect_media_input(item.path, kind, cancelled);
        if (info.hdr) problem("media.hdr_unsupported", "HDR PQ/HLG transfer requires tone mapping; the current renderer supports SDR inputs");
        if (kind != InputMediaKind::image && !info.duration) problem("media.duration_unknown", "source duration is unknown; trim bounds require runtime checks", true);
        media.emplace(item.id, std::move(info));
      }
    } catch (const RenderCancelled&) { throw; }
      catch (const std::bad_alloc&) { throw; }
      catch (const std::exception& exception) { problem(font ? "font.incompatible" : "media.incompatible", exception.what()); }
  };
  for (const auto& item : composition.assets()) asset(item, false, InputMediaKind::video);
  for (const auto& item : composition.image_assets()) asset(item, false, InputMediaKind::image);
  for (const auto& item : composition.font_assets()) asset(item, true, InputMediaKind::image);
  for (const auto& item : composition.audio_assets()) asset(item, false, InputMediaKind::audio);

  for (std::size_t index = 0; index < composition.layers().size(); ++index) {
    cancelled();
    const auto* layer = std::get_if<VideoLayer>(&composition.layers()[index]);
    if (!layer) continue;
    const auto path = "/layers/" + std::to_string(index) + "/source_start_frame";
    const auto file = composition.find_video_asset(layer->asset_id)->path.string();
    const auto length = layer->range.length();
    if (layer->source_start.value() > std::numeric_limits<std::int64_t>::max() - (length - 1)) {
      add("timeline.video_range_overflow", path, "source frame range overflows int64", layer->asset_id, file); continue;
    }
    try {
      const auto last = composition.frame_rate().time_at(FrameIndex(layer->source_start.value() + length - 1));
      const auto found = media.find(layer->asset_id);
      if (found != media.end() && found->second.duration && last >= *found->second.duration)
        add("timeline.video_out_of_range", path, "last requested source frame is outside the declared media duration", layer->asset_id, file);
    } catch (const std::overflow_error& error) { add("timeline.video_time_overflow", path, error.what(), layer->asset_id, file); }
  }
  if (!composition.audio_tracks().empty()) {
    try {
      if (audio_sample_at(composition.frame_rate(), composition.range().end()) -
          audio_sample_at(composition.frame_rate(), composition.range().start()) <= 0)
        add("timeline.audio_empty", "/canvas/duration_frames", "composition rounds to zero audio samples");
    } catch (const std::overflow_error& error) { add("timeline.audio_range_overflow", "/canvas", error.what()); }
  }
  for (std::size_t index = 0; index < composition.audio_tracks().size(); ++index) {
    cancelled();
    const auto& track = composition.audio_tracks()[index];
    const auto path = "/audio_tracks/" + std::to_string(index) + "/source_start_frame";
    const auto file = composition.find_audio_asset(track.asset_id)->path.string();
    try {
      const auto length = audio_sample_at(composition.frame_rate(), track.range.end()) - audio_sample_at(composition.frame_rate(), track.range.start());
      const auto start = audio_sample_at(composition.frame_rate(), track.source_start);
      if (length <= 0) { add("timeline.audio_empty", path, "track rounds to zero audio samples", track.asset_id, file); continue; }
      if (start > std::numeric_limits<std::int64_t>::max() - length) throw std::overflow_error("audio source sample range overflows int64");
      const auto found = media.find(track.asset_id);
      if (found != media.end() && found->second.duration && Rational(start + length, audio_sample_rate) > *found->second.duration) {
        const bool estimate = !found->second.exact_audio_duration;
        add(estimate ? "timeline.audio_duration_estimate" : "timeline.audio_out_of_range", path,
            estimate ? "requested audio trim exceeds estimated duration; runtime decoding will verify the sample range" :
                       "requested audio trim exceeds the available 48 kHz PCM samples", track.asset_id, file, estimate);
      }
    } catch (const std::overflow_error& error) { add("timeline.audio_range_overflow", path, error.what(), track.asset_id, file); }
  }
  cancelled();
  return report;
}

std::string preflight_report_json(const PreflightReport& report) {
  auto diagnostics = nlohmann::json::array();
  for (const auto& item : report.diagnostics) {
    nlohmann::json value{{"severity", item.severity}, {"code", item.code}, {"path", item.path}, {"message", item.message}};
    if (!item.asset_id.empty()) value["asset_id"] = item.asset_id;
    if (!item.asset_path.empty()) value["asset_path"] = item.asset_path;
    diagnostics.push_back(std::move(value));
  }
  return nlohmann::json{{"schema_version", 1}, {"ready", report.ready()},
    {"assets_checked", report.assets_checked}, {"diagnostics", std::move(diagnostics)}}.dump(2);
}
}  // namespace chroma
