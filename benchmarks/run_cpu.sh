#!/bin/sh
set -eu

build_directory=${1:-build}
iterations=${2:-5}

cmake --build "$build_directory" --target chroma_benchmark_fixture
mkdir -p benchmarks/results
"$build_directory/chroma" benchmark \
  "$build_directory/benchmarks/video-composition.json" \
  --iterations "$iterations" \
  --output benchmarks/results/cpu-video-640x360.json

echo "Benchmark report: benchmarks/results/cpu-video-640x360.json"
