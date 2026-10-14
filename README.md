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

Early technical spike. Chroma can currently parse and validate the experimental composition format, compile a requested frame into an explicit typed execution plan, render solid layers through the CPU reference backend, and write a PPM frame. Media, audio, text, effects, encoding, profiling, and CUDA execution are not implemented yet.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Render the current solid-layer example:

```sh
./build/chroma frame examples/solid.json --frame 0 --output frame.ppm
```

Inspect the optimized plan for a frame:

```sh
./build/chroma inspect-plan examples/solid.json --frame 0
```

The layered example exercises frame ranges and alpha compositing. Its red overlay is inactive at frame 0, active over the blue background at frame 30, and inactive again at frame 50:

```sh
./build/chroma inspect-plan examples/layered.json --frame 30
./build/chroma frame examples/layered.json --frame 30 --output frame-30.ppm
```

## License

Apache-2.0.
