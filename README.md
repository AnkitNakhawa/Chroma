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

Post-baseline compiler spike. Chroma can parse and validate solid, static-image, native-shape, shaped-text, and video compositions; resolve assets; probe and decode media at exact rational times; compile typed per-frame plans; render through a persistent CPU execution context; write PPM frames; encode H.264/MP4 directly; and emit per-stage benchmark JSON. Image and video sources support `none`, `contain`, `cover`, and `fill` placement with normalized alignment. Native rectangles, rounded rectangles, and ellipses support fill and optional inside stroke. HarfBuzz shapes UTF-8 text and FreeType rasterizes glyph coverage into Chroma-owned prepared resources. Composition layers can request transform, crop, static or keyframed opacity, analytic rectangle/ellipse masks, color-matrix, and Gaussian-blur effects plus source-over, multiply, screen, and plus blending. Keyframed opacity, translation, scale, and rotation use layer-relative integer frames and deterministic cubic easing, then lower to the existing static effect IR before optimization; overlapping animated layers provide a native crossfade pattern. Gaussian blur is the first executable neighborhood barrier, preserving separate fusible groups on either side. Static images and prepared text are reused within a render context while Chroma retains ownership of effects and compositing. The CPU runtime now executes a conservative, profitable subset of native-size video/image sampling, one leading transform, crop, opacity, and compositing as one destination-pixel loop; unsupported or slower chains use the materializing reference path. The compiler makes conservative algebraic rewrites authoritative for execution, removes provably dead work, and produces a typed physical-pass analysis with fusion candidates, explicit barriers, logical intermediate resources, reusable physical-buffer assignments, full-frame byte-traffic estimates, peak-live memory, and cache opportunities. CUDA execution, production color management, persistent cross-job caching, parallel scheduling, and language bindings are not implemented yet.

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

Chroma's internal resident-frame contract describes host or accelerator memory, pixel format, color semantics, plane addresses/strides, and a shared lifetime token without exposing FFmpeg or CUDA types. The contract is currently infrastructure for the upcoming zero-copy path; the CPU renderer still materializes its linear-float `Surface`.

HarfBuzz/FreeType text support is also discovered through `pkg-config` and can be disabled independently with `-DCHROMA_ENABLE_TEXT=OFF`. Composition parsing and plan inspection remain available, while CPU execution of a text operation fails explicitly in that configuration.

Inspect a media file without exposing FFmpeg types through Chroma's public API:

```sh
./build/chroma probe input.mp4
```

Inspect local CUDA/NVDEC/NVENC readiness without assuming that an FFmpeg build implies usable hardware:

```sh
./build/chroma hardware
```

The command separately reports CUDA device-type discovery, device creation, H.264 hardware-frame decode support, NVENC availability, and whether Chroma's accelerated pipeline is implemented. The last field remains false until an end-to-end GPU-resident path is validated on supported Linux/NVIDIA hardware.

Decode a source frame using the media stream's nominal frame rate:

```sh
./build/chroma decode input.mp4 --frame 120 --output frame-120.ppm
```

Render a complete composition directly to H.264/MP4 without intermediate image files:

```sh
./build/chroma render composition.json --output result.mp4
```

The current encoder path is CPU-only, uses 8-bit YUV420P, requires even canvas dimensions, and optionally mixes declared audio tracks to 48 kHz stereo AAC. Audio supports frame-based source trimming and placement, linear gain, overlapping tracks, and silence gaps.

Render the audio example after supplying a two-second `examples/sound.wav`:

```sh
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=2" examples/sound.wav
./build/chroma render examples/audio.json --output audio.mp4
```

See [audio track semantics](docs/composition-format.md#audio-tracks) for timing,
resampling, clipping, source-length errors, and current format restrictions.
Benchmark reports include audio sample counts and separate audio processing time.

CPU decode uses a pixel-exact 256-entry sRGB-to-linear lookup for RGBA8 input; `CpuDecodeConversionMode::reference` retains the original transfer-function implementation as a correctness oracle.

Run the reproducible Release CPU benchmark:

```sh
./benchmarks/run_cpu.sh build 5
```

The report separates setup, plan compilation, decode, decode conversion, composition, linear-float-to-RGB, RGB-to-YUV, codec/mux, and total throughput. It also reports decoded/converted frame counts and estimated bytes crossing each representation boundary. Results are machine-specific and are not presented as general performance claims.

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

Image and shaped-text preparation share deterministic request-key construction. Runtime maps use the full length-prefixed key; optimized-plan diagnostics expose a compact `fnv1a64:` identifier and deduplicate identical preparation requests across layers. The compact identifier is diagnostic—not a collision-proof persistent cache key—and currently incorporates normalized paths rather than asset contents. Cross-job content hashing, disk storage, and persistent-cache eviction remain future work.

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

Animate translation, scale, and rotation with fixed origins:

```sh
./build/chroma render examples/animated-transforms.json --output motion.mp4
```

See [transform animation semantics](docs/composition-format.md#translation-scale-and-rotation)
for effect order, units, origins, reflections, and multiple rotation turns.

### Prepared image and text cache budgets

Render contexts retain prepared images and text with independent least-recently-used
caches: 64 MiB for images and 16 MiB for text by default. Budgets count retained
resource/key storage; they do not cap total process memory or temporary decoding.
Zero disables retention, and oversized resources still render without being cached.

```sh
./build/chroma render composition.json --output result.mp4 \
  --image-cache-bytes 33554432 --text-cache-bytes 8388608 \
  --cache-report cache.json
./build/chroma benchmark composition.json --iterations 3 --output benchmark.json \
  --image-cache-bytes 33554432 --text-cache-bytes 8388608
```

Append these budget flags to `frame`, `render`, or `benchmark`; `frame` and `render`
also accept `--cache-report`. Reports expose budgets, resident/peak bytes, entries,
hits, misses, insertions, evictions, bypasses, and preparation failures for each
cache. Benchmark snapshots appear at `iterations[i].cache`; each iteration starts
with a fresh context. A render report captures usage before job cleanup.

C++ callers can pass `CpuCacheConfig` to `CpuRenderContext` or as the third
argument to `render_video_cpu`. Contexts also expose `set_cache_budgets`,
`clear_prepared_caches`, and `cache_usage`. See [retention and accounting
semantics](docs/architecture.md#bounded-prepared-resource-retention) for details.

### Stable C interface

ABI v1 is available through `<chroma/chroma.h>` and the shared `chroma::c` target.
It exposes validation/diagnostics, metadata and plan inspection, owned float-RGBA
frames, synchronous H.264/AAC MP4 rendering, capability queries, and cache controls.

```sh
cmake --install build --prefix /your/prefix --component CAPI
./build/chroma_c_example composition.json result.mp4
```

A C-only application can use `find_package(Chroma CONFIG REQUIRED)` and link
`chroma::c`. See [the ABI, ownership, and installation contract](docs/c-api.md) and
[the C99 example](examples/c-api-render.c). Async jobs and cancellation callbacks
are not part of this initial interface.

### 1080p showcase

[Motion studies](examples/showcase/README.md) is a reproducible 18-second editorial
film combining coastal footage, animated typography/cards, transitions, and an
original electronic score. The example includes asset preparation, attribution,
offline tests, and a full-resolution cache A/B benchmark runner.

### Bounded-region graphics performance

Eligible text/shape layers now rasterize, transform and composite their affected
regions, with full-frame fallbacks for unsupported effects. On the unchanged
18-second 1080p showcase, a controlled three-pair CPU benchmark measured a median
**26.32 s versus 88.88 s** using the retained full-frame path: **3.38× throughput**
with bitwise-equal native frames and a byte-identical MP4. This is a workload-specific
result; peak process memory did not improve. See [raw evidence and methodology](docs/benchmarks/2026-10-18-bounded-graphics.md).

Use `--graphics-mode bounded|full-frame` on `frame`, `render`, or `benchmark` for
a same-binary comparison; the default is `bounded`. Both modes retain identical
media fusion and cache settings.
