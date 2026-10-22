#include "parallel.hpp"
#include "chroma/core/output_color.hpp"
#include "chroma/core/render.hpp"
#include <atomic>
#include <barrier>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

int main() {
  for (std::size_t count : {0U,1U,7U,1003U,10000U}) {
    std::vector<unsigned> values(count);
    chroma::detail::parallel_for(count,1,[&](auto begin,auto end) {
      for (auto i=begin;i<end;++i) ++values[i];
      // Nested requests must run without deadlocking.
      chroma::detail::parallel_for(100,1,[](auto,auto) {});
    });
    for (auto value : values) if (value!=1) return 1;
  }
  bool threw=false;
  try { chroma::detail::parallel_for(10000,1,[](auto,auto) { throw std::runtime_error("task"); }); }
  catch (const std::runtime_error&) { threw=true; }
  if (!threw) return 1;
  std::atomic<unsigned> partitions{0};
  chroma::detail::parallel_for(10000,1,[&](auto,auto) { ++partitions; });
  if (partitions!=chroma::detail::cpu_thread_limit()) return 1;
  std::atomic<unsigned> calls{0};
  chroma::detail::parallel_for(10000,1,[&](auto begin,auto end) { calls+=static_cast<unsigned>(end-begin); });
  if (calls!=10000) return 1; // Exception recovery and completion barrier.

  std::barrier start(8);
  std::vector<std::thread> callers;
  std::atomic<unsigned> active{0}, peak{0};
  for (int i=0;i<8;++i) callers.emplace_back([&] {
    start.arrive_and_wait();
    for (int job=0;job<100;++job) {
      chroma::detail::parallel_for(1000,1,[&](auto begin,auto end) {
        const auto current=++active;
        auto old=peak.load(); while (old<current && !peak.compare_exchange_weak(old,current)) {}
        calls+=static_cast<unsigned>(end-begin);
        --active;
      });
    }
  });
  for (auto& caller : callers) caller.join();
  if (calls!=810000 || peak>8+chroma::detail::cpu_thread_limit()-1) return 1;
  std::vector<chroma::Pixel> pixels(1170077);
  std::vector<std::uint8_t> rgb(pixels.size()*3);
  for (std::size_t i=0;i<pixels.size();++i) pixels[i]={static_cast<float>(i%10001)/10000.0F, -0.1F, 1.1F, 0};
  chroma::convert_output_rgb8(pixels.data(),pixels.size(),rgb.data());
  for (std::size_t i=0;i<pixels.size();++i)
    if (rgb[i*3]!=chroma::linear_to_srgb8_reference(pixels[i].red) || rgb[i*3+1]!=0 || rgb[i*3+2]!=255) return 1;
  std::cout << "exact partitioning, nesting, exceptions, reuse and concurrent boundedness passed\n";
}
