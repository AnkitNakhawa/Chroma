// Native float verifier shared by offline corpora and generated CI fixtures.
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "chroma/core/render.hpp"

nlohmann::json counters(const chroma::CpuGraphicsStats& s) {
  return {{"solid_surface_pixels_avoided",s.solid_surface_pixels_avoided},
          {"initialization_pixels_avoided",s.initialization_pixels_avoided},
          {"constant_composite_pixels_avoided",s.constant_composite_pixels_avoided},
          {"bounded_operations",s.bounded_operations},{"full_frame_operations",s.full_frame_operations},
          {"source_pixels",s.source_pixels},{"transform_pixels",s.transform_pixels},{"composite_pixels",s.composite_pixels}};
}
int main(int argc,char** argv) {
  try {
    if (argc!=3 && argc!=5)
      throw std::invalid_argument("usage: chroma_graphics_equivalence composition.json report.json [--reference-mode full-frame|reference|composition]");
    auto reference_mode = chroma::CpuExecutionMode::unbounded_graphics;
    if (argc==5) {
      if (std::string(argv[3])!="--reference-mode") throw std::invalid_argument("unknown verifier option");
      const std::string mode(argv[4]);
      if (mode!="full-frame" && mode!="reference" && mode!="composition") throw std::invalid_argument("invalid reference mode");
      if (mode=="reference") reference_mode=chroma::CpuExecutionMode::reference;
      if (mode=="composition") reference_mode=chroma::CpuExecutionMode::unoptimized_composition;
    }
    std::ifstream input(argv[1]);
    if (!input) throw std::runtime_error("unable to open composition");
    std::ostringstream source; source<<input.rdbuf();
    auto parsed=chroma::parse_composition(source.str(),std::filesystem::absolute(argv[1]).parent_path());
    if (!parsed.valid()) throw std::runtime_error(chroma::diagnostics_json(parsed.diagnostics));
    chroma::CpuRenderContext bounded,full_frame;
    chroma::CpuGraphicsStats bounded_stats,full_stats;
    std::uint64_t frames=0,pixels=0;
    const auto started=std::chrono::steady_clock::now();
    for(auto i=parsed.composition->range().start().value();i<parsed.composition->range().end().value();++i) {
      const auto plan=chroma::compile_frame(*parsed.composition,chroma::FrameIndex(i));
      const auto actual=bounded.execute(plan);
      const auto expected=full_frame.execute(plan,reference_mode);
      if (std::memcmp(actual.pixels().data(),expected.pixels().data(),actual.size()*sizeof(chroma::Pixel))!=0)
        throw std::runtime_error("bitwise pixel mismatch at frame "+std::to_string(i));
      bounded_stats+=bounded.last_execution_stats().graphics;
      full_stats+=full_frame.last_execution_stats().graphics;
      ++frames; pixels+=actual.size();
      if(frames%90==0) std::cerr<<"verified "<<frames<<" frames\n";
    }
    nlohmann::json report{{"frames",frames},{"pixels_compared",pixels},{"comparison","bitwise float RGBA"},
        {"reference_mode",reference_mode==chroma::CpuExecutionMode::unoptimized_composition ? "composition" : reference_mode==chroma::CpuExecutionMode::reference ? "reference" : "full-frame"},
        {"bounded",counters(bounded_stats)},{"full_frame",counters(full_stats)},
        {"verification_seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()}};
    std::ofstream output(argv[2]); output<<report.dump(2)<<'\n';
    output.close(); if(!output) throw std::runtime_error("unable to write verification report");
  } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
