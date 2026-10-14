#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "chroma/core/composition.hpp"
#include "chroma/core/render.hpp"

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
  result = chroma::parse_composition(*source);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
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

    std::int64_t frame_number = 0;
    try {
      std::size_t consumed = 0;
      frame_number = std::stoll(argv[4], &consumed);
      if (consumed != std::string_view(argv[4]).size()) {
        throw std::invalid_argument("trailing characters");
      }
    } catch (const std::exception&) {
      std::cerr << "invalid frame index: " << argv[4] << '\n';
      return usage_error;
    }

    try {
      const auto surface =
          chroma::render_frame_cpu(*result.composition, chroma::FrameIndex(frame_number));
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
    std::cout << "rendered frame " << frame_number << " to " << argv[6] << '\n';
    return 0;
  }

  print_usage(argc == 1 ? std::cout : std::cerr);
  return argc == 1 ? 0 : usage_error;
}
