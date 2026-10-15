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

Post-baseline compiler spike. Chroma can parse and validate solid, static-image, native-shape, shaped-text, and video compositions; resolve assets; probe and decode media at exact rational times; compile typed per-frame plans; render through a persistent CPU execution context; write PPM frames; encode H.264/MP4 directly; and emit per-stage benchmark JSON. Image and video sources support `none`, `contain`, `cover`, and `fill` placement with normalized alignment. Native rectangles, rounded rectangles, and ellipses support fill and optional inside stroke. HarfBuzz shapes UTF-8 text and FreeType rasterizes glyph coverage into Chroma-owned prepared resources. Composition layers can request transform, crop, opacity, and color-matrix effects plus source-over, multiply, screen, and plus blending. Static images and prepared text are reused within a render context while Chroma retains ownership of effects and compositing. The CPU runtime now executes a conservative, profitable subset of native-size video/image sampling, one leading transform, crop, opacity, and compositing as one destination-pixel loop; unsupported or slower chains use the materializing reference path. The compiler makes conservative algebraic rewrites authoritative for execution, removes provably dead work, and produces a typed physical-pass analysis with fusion candidates, explicit barriers, logical intermediate resources, reusable physical-buffer assignments, full-frame byte-traffic estimates, peak-live memory, and cache opportunities. CUDA execution, audio, production color management, persistent cross-job caching, parallel scheduling, and language bindings are not implemented yet.

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

HarfBuzz/FreeType text support is also discovered through `pkg-config` and can be disabled independently with `-DCHROMA_ENABLE_TEXT=OFF`. Composition parsing and plan inspection remain available, while CPU execution of a text operation fails explicitly in that configuration.

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

Optimized-plan output currently reports `"status": "planning_only"`; rendering still executes the typed plan shown by `inspect-plan`. A narrow CPU source-layer fusion path is selected independently at runtime when its exact-semantics and profitability rules pass.
Its cost summary assumes a linear-premultiplied RGBA working surface of four floats per pixel and full-canvas reads/writes. Logical temporaries use SSA-style names; non-overlapping lifetimes are mapped onto reusable physical buffer numbers. Cache diagnostics conservatively classify frame-varying, temporal, and nondeterministic work as non-cacheable, cheap static work as better to recompute, and expensive deterministic static work as a cache candidate. These are planning decisions only: the renderer does not yet allocate from this physical plan or persist cached surfaces.

The fused CPU source path is intentionally narrower than the planning diagnostics. It handles a native-size video/image source with one leading transform followed by crop and at most one opacity, then blends directly into the destination without a canvas-sized transform surface. Placement scaling, native no-effect copies, color matrices, multiple transforms or opacity nodes, crop after pointwise work, and all barrier effects use the reference executor. `CpuExecutionMode::reference` is available as a correctness oracle, and `CpuExecutionStats` reports fused/fallback operations plus avoided/materialized full-frame intermediates.

Image and shaped-text preparation share deterministic request-key construction. Runtime maps use the full length-prefixed key; optimized-plan diagnostics expose a compact `fnv1a64:` identifier and deduplicate identical preparation requests across layers. The compact identifier is diagnostic—not a collision-proof persistent cache key—and currently incorporates normalized paths rather than asset contents. Cross-job content hashing and eviction remain future work.

Render the basic effects example:

```sh
./build/chroma frame examples/effects.json --frame 0 --output effects.ppm
./build/chroma render examples/effects.json --output effects.mp4
./build/chroma inspect-optimized-plan examples/effects.json --frame 0
```

Static image assets use the same placement, effect, and blend pipeline as video, but decode once per CPU render context:

```json
{"id": "logo", "type": "image", "path": "logo.png"}
```

```json
{"id": "logo-layer", "type": "image", "asset": "logo", "start_frame": 0,
 "end_frame": 60, "fit": "contain", "position": [0.5, 0.5]}
```

Basic shapes remain native compiler nodes and accept canvas-space `[x, y, width, height]` bounds:

```json
{"id": "card", "type": "shape", "shape": "rounded_rectangle",
 "start_frame": 0, "end_frame": 60, "bounds": [40, 30, 240, 120],
 "corner_radius": 18, "fill": [0.1, 0.2, 0.8, 1],
 "stroke": {"color": [1, 1, 1, 1], "width": 2}}
```

Text declares a font asset and uses a canvas-space baseline position:

```json
{"id": "body", "type": "font", "path": "fonts/Inter-Regular.ttf"}
```

```json
{"id": "title", "type": "text", "font": "body", "text": "Hello Chroma",
 "font_size": 40, "position": [32, 72], "color": [1, 1, 1, 1],
 "start_frame": 0, "end_frame": 60}
```

The layered example exercises frame ranges and alpha compositing. Its red overlay is inactive at frame 0, active over the blue background at frame 30, and inactive again at frame 50:

```sh
./build/chroma inspect-plan examples/layered.json --frame 30
./build/chroma frame examples/layered.json --frame 30 --output frame-30.ppm
```

When tests run with FFmpeg and text support, CMake generates `build/fixtures/basic-graphics.json`. It combines video, image, rounded-shape, and shaped-text layers and is exercised through optimized-plan inspection, frame rendering, and the benchmark command.

## License

Apache-2.0.
