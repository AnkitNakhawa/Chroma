#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "chroma/core/composition.hpp"
#include "chroma/core/execution_plan.hpp"
#include "chroma/core/render.hpp"
#include "chroma/media/decode.hpp"
#include "chroma/media/encode.hpp"
#include "chroma/media/media.hpp"

namespace {

constexpr int usage_error = 2;
constexpr int invalid_composition = 3;
constexpr int input_error = 4;

std::optional<std::string> read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return std::nullopt;
  }
  return buffer.str();
}

void print_usage(std::ostream& output) {
  output << "Usage:\n"
         << "  chroma --version\n"
         << "  chroma validate <composition.json> [--json]\n"
         << "  chroma inspect <composition.json>\n"
         << "  chroma probe <media-file>\n"
         << "  chroma decode <media-file> --frame <index> --output <frame.ppm>\n"
         << "  chroma render <composition.json> --output <video.mp4>\n"
         << "  chroma benchmark <composition.json> --iterations <count> --output <report.json>\n"
         << "  chroma inspect-plan <composition.json> --frame <index>\n"
         << "  chroma frame <composition.json> --frame <index> --output <frame.ppm>\n";
}

void print_diagnostics(const std::vector<chroma::Diagnostic>& diagnostics, bool json) {
  if (json) {
    std::cout << chroma::diagnostics_json(diagnostics) << '\n';
    return;
  }
  for (const auto& diagnostic : diagnostics) {
    std::cerr << diagnostic.code << " at " << (diagnostic.path.empty() ? "/" : diagnostic.path)
              << ": " << diagnostic.message << '\n';
  }
}

int parse_file(const char* path, chroma::CompositionParseResult& result) {
  const auto source = read_file(path);
  if (!source) {
    std::cerr << "unable to read composition: " << path << '\n';
    return input_error;
  }
  result = chroma::parse_composition(*source, std::filesystem::path(path).parent_path());
  return 0;
}

std::optional<chroma::FrameIndex> parse_frame_index(const char* value) {
  try {
    std::size_t consumed = 0;
    const auto frame = std::stoll(value, &consumed);
    if (consumed != std::string_view(value).size()) {
      return std::nullopt;
    }
    return chroma::FrameIndex(frame);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

int main(int argc, char** argv) {
  chroma::configure_media_logging(chroma::MediaLogLevel::error);

  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "chroma 0.1.0\n";
    return 0;
  }

  if (argc >= 3 && std::string_view(argv[1]) == "validate") {
    const bool json = argc == 4 && std::string_view(argv[3]) == "--json";
    if (argc > 4 || (argc == 4 && !json)) {
      print_usage(std::cerr);
      return usage_error;
    }
    chroma::CompositionParseResult result;
    if (const auto status = parse_file(argv[2], result); status != 0) {
      return status;
    }
    if (!result.valid()) {
      print_diagnostics(result.diagnostics, json);
      return invalid_composition;
    }
    std::cout << (json ? "[]\n" : "composition is valid\n");
    return 0;
  }

  if (argc == 3 && std::string_view(argv[1]) == "inspect") {
    chroma::CompositionParseResult result;
    if (const auto status = parse_file(argv[2], result); status != 0) {
      return status;
    }
    if (!result.valid()) {
      print_diagnostics(result.diagnostics, false);
      return invalid_composition;
    }
    const auto& composition = *result.composition;
    std::cout << "schema: " << composition.schema_version() << '\n'
              << "canvas: " << composition.width() << 'x' << composition.height() << '\n'
              << "frame rate: " << composition.frame_rate().value().numerator() << '/'
              << composition.frame_rate().value().denominator() << '\n'
              << "duration: " << composition.range().length() << " frames\n"
              << "layers: " << composition.layers().size() << '\n';
    return 0;
  }

  if (argc == 3 && std::string_view(argv[1]) == "probe") {
    try {
      std::cout << chroma::media_info_json(chroma::probe_media(argv[2])) << '\n';
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "probe failed: " << error.what() << '\n';
      return input_error;
    }
  }

  if (argc == 7 && std::string_view(argv[1]) == "decode" &&
      std::string_view(argv[3]) == "--frame" && std::string_view(argv[5]) == "--output") {
    const auto frame = parse_frame_index(argv[4]);
    if (!frame) {
      std::cerr << "invalid frame index: " << argv[4] << '\n';
      return usage_error;
    }
    try {
      const auto info = chroma::probe_media(argv[2]);
      if (!info.frame_rate) {
        std::cerr << "decode failed: media does not declare a usable frame rate\n";
        return input_error;
      }
      const auto time = chroma::FrameRate(*info.frame_rate).time_at(*frame);
      const auto decoded = chroma::decode_video_frame(argv[2], time);
      std::ofstream output(argv[6], std::ios::binary);
      if (!output) {
        std::cerr << "unable to open output: " << argv[6] << '\n';
        return input_error;
      }
      chroma::write_ppm(decoded.surface, output);
      std::cout << "decoded frame " << frame->value() << " at "
                << decoded.presentation_time.numerator() << '/'
                << decoded.presentation_time.denominator() << " seconds to " << argv[6] << '\n';
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "decode failed: " << error.what() << '\n';
      return input_error;
    }
  }

  if (argc == 5 && std::string_view(argv[1]) == "render" &&
      std::string_view(argv[3]) == "--output") {
    chroma::CompositionParseResult result;
    if (const auto parse_status = parse_file(argv[2], result); parse_status != 0) {
      return parse_status;
    }
    if (!result.valid()) {
      print_diagnostics(result.diagnostics, false);
      return invalid_composition;
    }
    try {
      const auto rendered = chroma::render_video_cpu(*result.composition, argv[4]);
      std::cout << "rendered " << rendered.frames_encoded << " frames to " << argv[4] << '\n';
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "render failed: " << error.what() << '\n';
      return input_error;
    }
  }

  if (argc == 7 && std::string_view(argv[1]) == "benchmark" &&
      std::string_view(argv[3]) == "--iterations" &&
      std::string_view(argv[5]) == "--output") {
    chroma::CompositionParseResult result;
    if (const auto parse_status = parse_file(argv[2], result); parse_status != 0) {
      return parse_status;
    }
    if (!result.valid()) {
      print_diagnostics(result.diagnostics, false);
      return invalid_composition;
    }

    std::int64_t iteration_count = 0;
    try {
      std::size_t consumed = 0;
      iteration_count = std::stoll(argv[4], &consumed);
      if (consumed != std::string_view(argv[4]).size() || iteration_count <= 0 ||
          iteration_count > 100) {
        throw std::invalid_argument("iteration count outside supported range");
      }
    } catch (const std::exception&) {
      std::cerr << "invalid iteration count: " << argv[4] << " (expected 1-100)\n";
      return usage_error;
    }

    try {
      std::vector<chroma::VideoRenderResult> iterations;
      iterations.reserve(static_cast<std::size_t>(iteration_count));
      const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
      for (std::int64_t index = 0; index < iteration_count; ++index) {
        const auto temporary =
            std::filesystem::temp_directory_path() /
            ("chroma-benchmark-" + std::to_string(unique) + "-" + std::to_string(index) +
             ".mp4");
        try {
          iterations.push_back(chroma::render_video_cpu(*result.composition, temporary));
        } catch (...) {
          std::error_code cleanup_error;
          std::filesystem::remove(temporary, cleanup_error);
          throw;
        }
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
      }

      std::ofstream output(argv[6], std::ios::binary);
      if (!output) {
        std::cerr << "unable to open benchmark report: " << argv[6] << '\n';
        return input_error;
      }
      output << chroma::video_benchmark_json(iterations) << '\n';
      if (!output) {
        std::cerr << "unable to write benchmark report: " << argv[6] << '\n';
        return input_error;
      }
      std::cout << "completed " << iteration_count << " benchmark iteration(s); report: "
                << argv[6] << '\n';
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "benchmark failed: " << error.what() << '\n';
      return input_error;
    }
  }

  if (argc == 5 && std::string_view(argv[1]) == "inspect-plan" &&
      std::string_view(argv[3]) == "--frame") {
    chroma::CompositionParseResult result;
    if (const auto status = parse_file(argv[2], result); status != 0) {
      return status;
    }
    if (!result.valid()) {
      print_diagnostics(result.diagnostics, false);
      return invalid_composition;
    }
    const auto frame = parse_frame_index(argv[4]);
    if (!frame) {
      std::cerr << "invalid frame index: " << argv[4] << '\n';
      return usage_error;
    }
    try {
      std::cout << chroma::execution_plan_json(chroma::compile_frame(*result.composition, *frame))
                << '\n';
    } catch (const std::exception& error) {
      std::cerr << "plan compilation failed: " << error.what() << '\n';
      return input_error;
    }
    return 0;
  }

  if (argc == 7 && std::string_view(argv[1]) == "frame" &&
      std::string_view(argv[3]) == "--frame" && std::string_view(argv[5]) == "--output") {
    chroma::CompositionParseResult result;
    if (const auto status = parse_file(argv[2], result); status != 0) {
      return status;
    }
    if (!result.valid()) {
      print_diagnostics(result.diagnostics, false);
      return invalid_composition;
    }

    const auto frame = parse_frame_index(argv[4]);
    if (!frame) {
      std::cerr << "invalid frame index: " << argv[4] << '\n';
      return usage_error;
    }

    try {
      const auto surface = chroma::render_frame_cpu(*result.composition, *frame);
      std::ofstream output(argv[6], std::ios::binary);
      if (!output) {
        std::cerr << "unable to open output: " << argv[6] << '\n';
        return input_error;
      }
      chroma::write_ppm(surface, output);
    } catch (const std::exception& error) {
      std::cerr << "render failed: " << error.what() << '\n';
      return input_error;
    }
    std::cout << "rendered frame " << frame->value() << " to " << argv[6] << '\n';
    return 0;
  }

  print_usage(argc == 1 ? std::cout : std::cerr);
  return argc == 1 ? 0 : usage_error;
}
