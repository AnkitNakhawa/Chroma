# Chroma

Chroma is an experimental open-source, headless video composition engine for programmatic and agent-generated motion graphics.

The working thesis is that a constrained, declarative composition graph can render more predictably—and, for supported workloads, more efficiently—than generating and executing a browser application for every video.

Chroma is currently in the design and validation phase. No performance claims have been established yet.

## Intended shape

- A versioned declarative composition format
- A native C++ rendering core
- A stable C API and CLI
- A deterministic CPU reference backend
- An optional Linux/NVIDIA CUDA backend
- FFmpeg-based media decode, encode, audio, and muxing
- Whole-graph optimization and incremental content-addressed rendering
- Thin language bindings, beginning with Node.js

Chroma is not intended to be a visual editor, preview platform, hosted rendering service, Remotion clone, or general-purpose film VFX package.

## Current documents

- [Architecture](docs/architecture.md)
- [Experimental composition format](docs/composition-format.md)
- [Product and implementation plan](task_plan.md)
- [Research findings](findings.md)
- [Progress log](progress.md)

## Status

Post-baseline compiler spike. Chroma can parse and validate solid/video compositions, resolve assets, probe and decode media at exact rational times, compile typed per-frame plans, render through a persistent CPU execution context, write PPM frames, encode H.264/MP4 directly, and emit per-stage benchmark JSON. The core now includes an immutable effect program, explicit effect traits, conservative algebraic rewrites, and fusion-group planning. These optimized groups are inspection-only until the CPU effect executor and CUDA code generator land. Audio, text, composition-level transforms/effects, production color management, caching, parallel scheduling, language bindings, and CUDA execution are not implemented yet.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

FFmpeg media support is enabled automatically when its development libraries are discoverable through `pkg-config`. A core-only build remains supported:

```sh
cmake -S . -B build -DCHROMA_ENABLE_FFMPEG=OFF
```

Inspect a media file without exposing FFmpeg types through Chroma's public API:

```sh
./build/chroma probe input.mp4
```

Decode a source frame using the media stream's nominal frame rate:

```sh
./build/chroma decode input.mp4 --frame 120 --output frame-120.ppm
```

Render a complete composition directly to H.264/MP4 without intermediate image files:

```sh
./build/chroma render composition.json --output result.mp4
```

The current encoder path is CPU-only, uses 8-bit YUV420P, requires even canvas dimensions, and does not yet include audio.

Run the reproducible Release CPU benchmark:

```sh
./benchmarks/run_cpu.sh build 5
```

The report separates setup, plan compilation, execution/decode/compositing, encoding/muxing, and total throughput. Results are machine-specific and are not presented as general performance claims.

Render the current solid-layer example:

```sh
./build/chroma frame examples/solid.json --frame 0 --output frame.ppm
```

Inspect the executable plan for a frame:

```sh
./build/chroma inspect-plan examples/solid.json --frame 0
```

Inspect proposed rewrites and GPU fusion candidates separately:

```sh
./build/chroma inspect-optimized-plan examples/video.json --frame 0
```

Optimized-plan output currently reports `"status": "planning_only"`; rendering still executes the typed plan shown by `inspect-plan`.

The layered example exercises frame ranges and alpha compositing. Its red overlay is inactive at frame 0, active over the blue background at frame 30, and inactive again at frame 50:

```sh
./build/chroma inspect-plan examples/layered.json --frame 30
./build/chroma frame examples/layered.json --frame 30 --output frame-30.ppm
```

## License

Apache-2.0.
