# CPU benchmark fixture

The initial pre-CUDA corpus contains a deterministic four-second, 640x360, 30 fps video with a time-bounded translucent solid overlay. CMake generates the lossless source asset and composition under the build directory; generated media and result JSON are not committed.

Use a Release build and run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./benchmarks/run_cpu.sh build 5
```

The JSON report includes every iteration plus mean setup, plan compilation, execution, encode/mux, total latency, and aggregate frames per second. These numbers describe only the current CPU implementation on the current machine. They are not a comparison with Remotion or proof of Chroma's performance thesis.

For an approximate plain-FFmpeg comparison on the same generated source:

```sh
/usr/bin/time -p ffmpeg -y -hide_banner -loglevel error \
  -i build/benchmarks/color-segments-640x360.mkv \
  -vf "drawbox=x=0:y=0:w=iw:h=ih:color=green@0.2:t=fill:enable='between(t,1,3)'" \
  -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p \
  /tmp/chroma-ffmpeg-baseline.mp4
```

This is useful as a lower-level systems baseline, but its color math is not yet a pixel-equivalent conformance comparison with Chroma's linear-light compositing.
