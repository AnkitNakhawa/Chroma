#include <cstring>
#include <iostream>
#include <random>
#include "chroma/core/render.hpp"

int main() {
  std::mt19937 random(91238);
  auto value = [&] { return static_cast<double>(random() % 10001) / 10000.0; };
  const chroma::BlendMode modes[] = {chroma::BlendMode::source_over, chroma::BlendMode::plus,
                                    chroma::BlendMode::multiply, chroma::BlendMode::screen};
  chroma::CpuRenderContext optimized, reference;
  for (int trial = 0; trial < 300; ++trial) {
    std::vector<chroma::RenderOperation> operations;
    operations.emplace_back(chroma::ClearOperation{{value(), value(), value(), value()}});
    for (int layer = 0; layer < 6; ++layer) {
      std::vector<chroma::Effect> effects;
      // Repeated factors deliberately preserve intermediate float rounding.
      if ((trial + layer) % 2) effects.emplace_back(chroma::OpacityEffect{value()});
      if ((trial + layer) % 3) effects.emplace_back(chroma::OpacityEffect{value()});
      if (layer == 2 && trial % 2) effects.emplace_back(chroma::CropEffect{-2, 1, 9, 5});
      if (layer == 3 && trial % 3 == 0) effects.emplace_back(chroma::Transform2DEffect{1, 0, .3, 0, 1, -.7});
      operations.emplace_back(chroma::CompositeSolidOperation{.source_id="solid",
          .color={value(), value(), value(), layer % 2 ? value() : 1.0},
          .effects=chroma::EffectProgram(std::move(effects)), .blend_mode=modes[(trial+layer)%4]});
    }
    const chroma::ExecutionPlan plan(trial < 6 ? 701 : 13, trial < 6 ? 509 : 9, chroma::FrameIndex(trial), std::move(operations));
    const auto actual = optimized.execute(plan);
    const auto serial = reference.execute(plan, chroma::CpuExecutionMode::serial);
    if (std::memcmp(actual.pixels().data(),serial.pixels().data(),actual.size()*sizeof(chroma::Pixel))) return 1;
    const auto expected = reference.execute(plan, chroma::CpuExecutionMode::unoptimized_composition);
    if (std::memcmp(actual.pixels().data(), expected.pixels().data(), actual.size()*sizeof(chroma::Pixel))) {
      std::cerr << "composition mismatch in trial " << trial << '\n'; return 1;
    }
  }
  // A clear-only plan, including signed zero, still uses exactly the original fill.
  const chroma::ExecutionPlan clear(3, 2, chroma::FrameIndex(0), {chroma::ClearOperation{{-0.0, 0, 0, 0}}});
  const auto a=optimized.execute(clear);
  const auto b=reference.execute(clear, chroma::CpuExecutionMode::unoptimized_composition);
  if (std::memcmp(a.pixels().data(),b.pixels().data(),a.size()*sizeof(chroma::Pixel))) return 1;
  std::cout << "301 bitwise composition cases passed\n";
}
