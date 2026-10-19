#include <cstring>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
#include "chroma/core/render.hpp"

namespace {
int failures = 0;
void require(bool value, const char* message) {
  if (!value) { ++failures; std::cerr << message << '\n'; }
}

chroma::CpuGraphicsStats compare(const chroma::CompositeShapeOperation& shape, bool bounded = true) {
  const chroma::ExecutionPlan plan(48, 37, chroma::FrameIndex(0), {
      chroma::ClearOperation{{.red=.12, .green=.43, .blue=.21, .alpha=.63}}, shape});
  chroma::CpuRenderContext fast, reference;
  const auto actual = fast.execute(plan);
  const auto expected = reference.execute(plan, chroma::CpuExecutionMode::unbounded_graphics);
  require(std::memcmp(actual.pixels().data(), expected.pixels().data(), actual.size()*sizeof(chroma::Pixel)) == 0,
          "bounded shape must be bitwise equal to full-canvas reference");
  const auto stats = fast.last_execution_stats().graphics;
  require((stats.bounded_operations == 1) == bounded, "wrong bounded/fallback dispatch");
  return stats;
}

void shapes() {
  using namespace chroma;
  const std::vector<EffectProgram> programs{
      EffectProgram{}, EffectProgram({OpacityEffect{.37}}),
      EffectProgram({Transform2DEffect{1,0,.325,0,1,-.72}, OpacityEffect{.63}}),
      EffectProgram({Transform2DEffect{.7,-.5,12,.5,.7,-4}}),
      EffectProgram({Transform2DEffect{-1.5,.1,42,.2,.8,4}, CropEffect{-5,4,38,24}, OpacityEffect{.83}}),
      EffectProgram({OpacityEffect{.21}, Transform2DEffect{1,.2,-6,.1,1,3}, OpacityEffect{.37}}),
      EffectProgram({Transform2DEffect{1,0,-40,0,1,0}, Transform2DEffect{1,0,40,0,1,0}}),
      EffectProgram({CropEffect{4,3,18,19}, Transform2DEffect{1,0,5.5,0,1,-1.25}}),
      EffectProgram({Transform2DEffect{0,0,10,0,0,8}}),
      EffectProgram({Transform2DEffect{1e200,1e200,0,1e200,1e200,0}}),
      EffectProgram({Transform2DEffect{1e8,0,-1e8,0,1e-6,5}})};
  for (auto kind : {ShapeKind::rectangle, ShapeKind::rounded_rectangle, ShapeKind::ellipse})
    for (auto mode : {BlendMode::source_over, BlendMode::plus, BlendMode::multiply, BlendMode::screen})
      for (auto position : {std::pair{-2.75,3.125}, std::pair{42.2,31.8}, std::pair{-80.,-30.}, std::pair{8.,7.}})
        for (const auto& program : programs)
          compare({.source_id="shape", .geometry={kind,position.first,position.second,13.25,10.75,3.2},
                   .fill={.red=.8,.green=.3,.blue=.6,.alpha=.71},
                   .stroke=ShapeStroke{{.red=.2,.green=.7,.blue=.4,.alpha=.43},1.3},
                   .effects=program,.blend_mode=mode});
  const auto stats = compare({.source_id="small",.geometry={ShapeKind::rectangle,4,5,5,4,0},
                              .fill={.red=1,.alpha=1}});
  require(stats.source_pixels==20 && stats.composite_pixels==20, "small shape should touch only its bounds");
  auto color=identity_color_matrix(); color.values[19]=.3;
  for (const auto& program : {EffectProgram({GaussianBlurEffect{1.2}}), EffectProgram({color}),
                             EffectProgram({MaskEffect{MaskShape::ellipse,0,0,20,20,true}})})
    compare({.source_id="fallback",.geometry={ShapeKind::ellipse,2,3,12,8,0},
             .fill={.red=.7,.alpha=.8},.effects=program},false);
  std::mt19937 random(42);
  std::uniform_real_distribution<double> coord(-25,60), linear(-2,2);
  for(int i=0;i<200;++i) {
    compare({.source_id="random",.geometry={ShapeKind::ellipse,coord(random),coord(random),8.7,12.9,0},
             .fill={.red=.7,.green=.3,.blue=.9,.alpha=.67},
             .effects=EffectProgram({Transform2DEffect{linear(random),linear(random),coord(random),
                                                      linear(random),linear(random),coord(random)},OpacityEffect{.63}})});
  }
}
}
int main() { shapes(); return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }
