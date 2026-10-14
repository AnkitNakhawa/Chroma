# Chroma Architecture

Status: initial design draft

## Product contract

Chroma accepts a versioned declarative composition plus referenced assets and produces a video, image sequence, or individual frame. The same composition semantics apply to draft frames and final renders.

The engine optimizes and executes a constrained graph. It does not execute arbitrary JavaScript, HTML, CSS, CUDA, or user-provided native code in v1.

## Primary flow

```text
Composition + assets
        |
        v
Parse and validate
        |
        v
Normalize into immutable IR
        |
        v
Analyze and optimize graph
        |
        v
Build execution plan
        |
        v
Schedule CPU/GPU/media work
        |
        v
Render + audio mix + encode
        |
        v
Video / frames + diagnostics
```

## Architectural layers

### 1. Composition frontend

Responsibilities:

- Parse a versioned JSON document.
- Validate structure, values, capabilities, and asset declarations.
- Produce stable machine-readable diagnostics for agents and applications.
- Reject arbitrary executable code.

The external schema describes intent, not an execution backend. A blur node never names CUDA, Vulkan, or a CPU implementation.

### 2. Normalized intermediate representation

The frontend lowers the document into an immutable typed graph.

Normalization includes:

- Converting user-facing time values to integer frame ranges against a rational frame rate.
- Resolving inherited transforms and defaults.
- Making layer ordering and blend semantics explicit.
- Resolving asset references into stable internal handles.
- Expanding shorthand animations into typed curves.

The IR is the authoritative semantic boundary. All frontends and language bindings must produce the same IR for equivalent compositions.

### 3. Compiler and optimizer

Initial passes:

1. Structural validation and cycle detection.
2. Time-range and visibility analysis.
3. Constant folding and static-subgraph detection.
4. Dead-node and fully occluded-node elimination where provably safe.
5. Region-of-interest propagation.
6. Effect-chain grouping and backend capability matching.
7. Resource lifetime and memory planning.
8. Cache-key construction.

Later passes may fuse compatible GPU operations, tile large surfaces, or select alternate algorithms based on device limits.

### 4. Execution plan

The compiler emits an explicit plan rather than letting graph nodes recursively render one another. A plan contains:

- Scheduled operations and dependencies.
- Frame or frame-range applicability.
- Required surfaces and their lifetimes.
- CPU/GPU placement and transfer boundaries.
- Static and incremental cache opportunities.
- Media decode and encode requirements.
- Diagnostic names that map work back to composition nodes.

Plans are internal and may change between engine versions. The public composition format is versioned separately.

The current spike implements the first narrow form of this boundary. Compiling a frame emits a typed, ordered list beginning with a transparent clear followed by one `CompositeSolid` operation for each active layer. Inactive layers are removed during compilation, and every emitted operation retains its composition layer ID. Plan construction enforces bounded surface dimensions, normalized colors, valid source IDs, and a single initial clear so malformed plans fail before execution. Surface lifetimes, backend placement, media operations, and dependencies will extend this representation as those features land.

Video layers now compile to `CompositeVideoFrame` operations containing the resolved asset path, exact source time, opacity, and diagnostic source ID. These operations remain in document order with solid layers. The CPU executor decodes and composites them; for now it rejects source dimensions that differ from the canvas rather than applying an implicit scaling policy.

### 5. Runtime and scheduler

The runtime owns:

- Worker pools and cancellation.
- Backpressure between decode, render, and encode stages.
- Bounded CPU and GPU memory budgets.
- Per-frame and per-node profiling.
- Cache reads and writes.
- Progress and structured failure reporting.

Parallelism is constrained by dependencies and memory budgets rather than an unbounded frames-in-flight setting.

### 6. Rendering backends

#### CPU reference backend

The CPU backend defines the reference behavior for every supported v1 node. It is used for correctness tests, ordinary CI, development without an NVIDIA GPU, and fallback.

#### CUDA backend

The first accelerated backend targets 64-bit Linux and NVIDIA GPUs. Only selected operations need CUDA implementations in v1:

- Scale and crop
- Color conversion and basic grading
- Opacity and common blend modes
- Masks
- Blur
- Layer compositing

Text shaping and glyph rasterization may initially run on the CPU, with glyph atlases uploaded for GPU composition.

Backend selection is capability-based. Unsupported operations may create a transfer boundary and fall back to the CPU, but the planner must expose those boundaries in profiling output.

### 7. Media and audio

FFmpeg libraries provide demuxing, decoding, encoding, audio resampling, and muxing. The CUDA path should use FFmpeg hardware frames so supported videos can travel through NVDEC, CUDA effects, and NVENC without unnecessary CPU readback.

The first implemented media boundary discovers libavformat, libavcodec, libavutil, and libswscale through `pkg-config`. It can be disabled at configure time, and all public metadata uses Chroma-owned strings, integers, optionals, and exact rationals. No FFmpeg structs or enum types cross the public header boundary. The `probe` command currently exposes the selected video stream's dimensions, codec, pixel format, time base, frame rate, duration, and reported frame count as JSON.

Single-frame decode accepts an exact rational time. The implementation seeks backward to a keyframe, decodes in presentation order, selects the latest frame presented at or before the request, and reports the actual presentation time. It converts through libswscale to RGBA and then into the CPU backend's linear premultiplied float surface. The current transfer conversion assumes sRGB-like RGB output; metadata-driven video color management remains a required follow-up before claiming broad fidelity.

Audio remains CPU-side in v1 and is scheduled independently from video until muxing.

The implemented CPU video writer renders surfaces in memory, converts linear premultiplied RGB over black to 8-bit RGB, converts to YUV420P with libswscale, encodes H.264, and muxes directly to the requested container. It assigns explicit frame and stream durations so rational frame rates survive muxing. The initial writer is sequential, requires even dimensions, and has no audio path yet.

During sequential renders, a CPU execution context owns one persistent decoder per asset path. Monotonically increasing requests retain demux, codec, decoded-frame lookahead, and conversion state; backward requests trigger an explicit seek and codec flush. The context lifetime is scoped to one render job, so it contains no shared mutable global cache.

### 8. Color and alpha semantics

v1 targets SDR output. Composition operations use a defined linear-light working space and premultiplied alpha. Input interpretation, conversion, blending, and output transforms must be specified rather than inherited accidentally from individual libraries.

Physical storage is backend-specific. CPU and GPU results may use tolerance-based equivalence for operations affected by floating-point or sampling differences.

### 9. Cache

The cache is content-addressed. A cache identity incorporates:

- Normalized node type and parameters
- Input-node identities
- Asset content identity or an explicit trusted identity
- Frame or frame-range identity
- Composition schema and engine semantic versions
- Relevant color and sampling settings
- Backend and kernel version when cross-backend equivalence is not guaranteed

The first implementation should prioritize static subtree caching and repeated-render reuse. Fine-grained tile caching should be added only after profiling demonstrates value.

## Public surfaces

### CLI

Proposed commands:

```text
chroma validate composition.json
chroma inspect composition.json
chroma inspect-plan composition.json --frame 120
chroma probe input.mp4
chroma decode input.mp4 --frame 120 --output frame.ppm
chroma render composition.json --output video.mp4
chroma frame composition.json --frame 120 --output frame.png
chroma benchmark composition.json
```

`inspect-plan` is implemented in the current spike and prints deterministic JSON for agent and developer inspection. Its structure is internal rather than a version-stable public contract. The render, benchmark, and encoded-video commands remain proposed.

### C API

The stable ABI should expose opaque handles for engine, composition, validation result, and render job. Callers supply configuration, cancellation, progress, and diagnostic callbacks. C++ and dependency-specific types do not cross the ABI.

### Language bindings

The first binding is expected to be Node.js. Bindings remain thin: validation, compilation, scheduling, and rendering behavior live in the core.

## Proposed repository layout

```text
chroma/
  CMakeLists.txt
  include/chroma/       Public C API
  src/core/             IR, validation, compiler, scheduler
  src/media/            FFmpeg integration
  src/backends/cpu/     Reference renderer
  src/backends/cuda/    Optional accelerated renderer
  src/cli/              Command-line interface
  bindings/node/        Thin Node.js binding
  schemas/              Versioned composition schemas
  tests/                Unit, conformance, and golden tests
  benchmarks/           Corpus and benchmark drivers
  docs/                 Architecture and decisions
```

## Important unresolved decisions

- Exact v1 schema and graph vocabulary.
- CPU raster and effect implementation: Skia, OpenImageIO primitives, custom kernels, or a measured combination.
- CUDA kernel authoring and fusion strategy.
- Internal surface formats and when lower-precision storage is acceptable.
- Text shaping, font discovery, font embedding, and reproducibility policy.
- Asset identity rules for local paths, URLs, streams, and caller-provided buffers.
- Cache storage format, eviction policy, and concurrency behavior.
- Packaging and redistribution constraints around FFmpeg and NVIDIA support.
- Exact deterministic-output contract across CPU models and GPU generations.

## First technical spike

The spike should render one representative graph:

```text
video decode
  -> crop and scale
  -> blur
  -> color adjustment
  -> animated text overlay
  -> composite
  -> H.264 encode
```

It must run through the CPU reference path and the CUDA path, emit per-stage timing and transfer counts, and compare output within a documented tolerance. It should be benchmarked against equivalent FFmpeg and GPU-configured Remotion compositions before expanding the feature set.
