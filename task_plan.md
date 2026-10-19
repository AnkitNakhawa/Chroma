# Chroma — Native Agent-First Video Engine Plan

## Goal
Design and validate an open-source, deterministic, agent-first video composition engine with a native rendering core and substantially lower render latency and memory use than browser-based pipelines for its supported feature set.

## Current product boundary
- Agents generate a declarative composition, not C++ source code.
- A headless native library and CLI validate, render, mix audio, and encode the composition.
- The first version targets deterministic 2D motion graphics rather than arbitrary HTML/CSS or full Remotion compatibility.
- The project will not build a preview platform, hosted service, visual editor, or cloud render product.
- Host tools may request individual frames or low-resolution drafts from the same render API, but presentation of those results belongs to the host.

## Phases
| Phase | Description | Status |
|---|---|---|
| 1 | Establish product goals, user workflows, scope, and measurable requirements | complete |
| 2 | Define system architecture and technology choices | complete |
| 3 | Specify the composition DSL, timing model, and compatibility contract | in_progress |
| 4 | Design the rendering, media, audio, preview, and caching pipelines | in_progress |
| 5 | Define MVP milestones, benchmark corpus, tests, packaging, and governance | pending |
| 6 | Build and benchmark a minimal technical spike | in_progress |

## Decision gates
- Do not choose a renderer architecture until the MVP feature boundary is explicit.
- Do not claim a performance advantage until measured against a GPU-configured Remotion baseline.
- Do not add arbitrary code execution to the initial composition format.
- Preview and final render must use the same authoritative semantics.
- Keep the core independently embeddable; integrations with Remotion, agent tools, editors, and SaaS products are adapters rather than core responsibilities.
- Treat GPU acceleration as part of v1 validation, not as a requirement for every node: retain a deterministic CPU reference backend and initially target Linux/NVIDIA CUDA for accelerated effect chains.

## Phase 1 outcome
- Initial user workflow: an agent or host application emits a versioned declarative composition plus an asset manifest; the engine validates it and renders requested frames or a final video.
- MVP scope: deterministic 2D motion graphics with text, vector shapes, images, video, audio, keyframes, easing, clipping, masks, and basic effects.
- Initial non-goals: arbitrary HTML/CSS, React compatibility, arbitrary runtime plugins, full nonlinear editor functionality, generative video models, full 3D, HDR, and universal codec support.
- Provisional performance gate: demonstrate a material end-to-end advantage over an optimized Remotion baseline on a published corpus, while preserving deterministic output and bounded resource use.
- Distribution boundary: native library, stable C API, CLI, and thin language bindings; no first-party editor or preview application.

## Phase 2 working decisions
- Repository and working project name: Chroma.
- Architectural layers: versioned composition document, normalized immutable IR, compiler/optimizer, execution plan, scheduler/cache, CPU and CUDA backends, FFmpeg media boundary, and CLI/C API adapters.
- v1 runs headlessly on 64-bit Linux and supports a complete CPU reference path plus selected NVIDIA/CUDA fast paths.
- Time is represented using integer frame indices and rational rates; public composition semantics do not depend on wall-clock timing.
- Rendering semantics use premultiplied alpha in a linear working color space; the physical pixel representation may differ by backend.
- CUDA, FFmpeg, Skia, and other dependency-specific types must not cross the public C API or composition schema.
- GPU acceleration is selected by capability and policy; compositions never name a vendor backend.
- Cache identities include normalized inputs, referenced asset identities, engine/schema versions, and backend/kernel identity where output equivalence is not guaranteed.
- Exact dependency selection, pixel storage formats, and effect-kernel strategy remain spike decisions rather than settled architecture.

## Initial implementation milestones
1. Build system, exact rational timeline primitives, and unit-test harness.
2. Experimental versioned composition parser, structured diagnostics, and `validate`/`inspect` CLI commands.
3. Minimal CPU surface and solid-layer frame renderer with golden tests.
4. Typed per-frame execution-plan compiler with dead-layer elimination and stable inspection output.
5. FFmpeg media probing and decode boundary behind an optional build feature.
6. Profiling events and a benchmark fixture.
7. Linux CUDA spike for a narrow decode/effect/composite/encode chain.

Milestones 1–4 now have working implementations and automated tests. The current CPU renderer executes the same typed frame plan exposed by `inspect-plan`; the legacy convenience entry point compiles before executing.

## Pre-CUDA implementation track

The current implementation session will complete and verify these checkpoints in order:

1. [complete] Optional FFmpeg build integration plus a structured `probe` command.
2. [complete] Exact rational mapping from composition frames to media timestamps and deterministic single-frame CPU decode.
3. [complete] Versioned video assets/layers, typed decode/composite plan operations, and CPU video compositing.
4. [complete] Direct encoded-video output without intermediate PPM files in the normal render path.
5. [complete] Per-stage profiling, a reproducible media fixture, and benchmark commands/results suitable for establishing a CPU baseline.
6. [complete] Documentation, failure-path tests, builds with FFmpeg disabled, and a final focused review.

CUDA remains explicitly out of scope for this track. The CPU/media baseline must be measured before GPU work begins.

The pre-CUDA track is complete. CUDA work should not begin by assuming compositing alone is the bottleneck: the measured CPU pipeline spends comparable time in execution/decode/conversion and RGB-to-YUV/encode, while plan compilation is negligible. The next design must target conversion/copy boundaries and codec-surface residency as first-class metrics.

## Post-baseline implementation strategy

Development now proceeds as vertical slices. Each slice must add a user-visible MVP capability, exercise a specific compiler optimization, retain a deterministic CPU reference implementation, and extend the benchmark corpus. This keeps the basic-video product and the graph-compiler differentiation advancing together.

### Focused MVP output

The first useful product target is template-style programmatic video: a background image or video, timed overlays, animated text and shapes, simple transitions, and an audio track, rendered directly to a common delivery format. Full VFX compatibility is not required for this gate.

### Ordered implementation slices

1. [complete] Define an immutable effect IR and effect-trait contract covering identity, pointwise, coordinate-transform, neighborhood, temporal, and external/barrier operations. Add stable optimized-plan inspection.
2. [complete] Implement transforms, crop/fit, opacity, color matrices, and blend modes in the CPU reference backend. Video source placement now supports explicit none/contain/cover/fill sampling and normalized alignment before the optimized effect chain. Optimized programs are authoritative for execution, provably transparent layers are removed, and opaque full-canvas solids cull hidden work.
3. [complete] Partition the optimized graph into fusible groups and explicit barriers. Add pass-count, intermediate-surface, estimated-byte-traffic, physical-buffer-reuse, and cache-decision diagnostics; prove the planner with tests before GPU code generation.
4. [complete] Add image layers plus text and basic shapes. Prefer a narrow text shaping/raster dependency, cache static raster results, and avoid adopting a general compositor dependency at this stage.
5. [pending] Build the first Linux/NVIDIA CUDA vertical slice: retain decoded surfaces on the GPU where supported and fuse sample/transform/color/opacity/composite for the benchmark-critical path. Compare output against the CPU reference backend.
6. [complete] Add keyframes/easing, simple masks, blur, and transitions. Keyframed opacity, cubic easing, analytic rectangle/ellipse masks, deterministic CPU Gaussian blur, and a crossfade composition pattern are implemented. Blur is exposed as two physical neighborhood passes, with fusion retained on both sides.
7. [complete] Add audio input, frame-based trim/placement, linear gain, deterministic streaming stereo mixing, and AAC/MP4 muxing through FFmpeg. Exact PCM and independent encoded-output tests cover overlap, silence, rational timing, priming/tail handling, and failure cleanup.
8. [pending] Publish benchmark comparisons against equivalent FFmpeg and GPU-configured Remotion workloads. Report end-to-end time, GPU/CPU transfer bytes, peak memory, kernel/pass count, and warm/cold behavior.

### Active fit/placement checkpoint

1. [complete] Add typed `none`, `contain`, `cover`, and `fill` source placement with normalized alignment.
2. [complete] Carry placement through JSON parsing and exact/optimized execution-plan inspection.
3. [complete] Implement deterministic CPU placement before the existing effect chain and blending.
4. [complete] Add parser, plan, pixel, and mismatched-video integration tests; update public documentation.

### Active physical pass-planner checkpoint

1. [complete] Define typed physical passes, barrier reasons, resource reads/writes, and aggregate cost diagnostics.
2. [complete] Split optimized effect chains at neighborhood, temporal, and external barriers while preserving fusible regions.
3. [complete] Estimate logical temporary surfaces, full-frame byte traffic, and peak live memory with explicit assumptions.
4. [complete] Expose stable JSON, add legal/illegal fusion and resource-lifetime tests, update docs, and exercise the diagnostic path through the CLI.

### Active reuse/cache checkpoint

1. [complete] Compute logical temporary lifetimes and map them onto reusable physical buffers without read/write aliasing.
2. [complete] Report physical temporary count/bytes and per-resource allocation intervals in typed and JSON diagnostics.
3. [complete] Classify deterministic static solid subgraphs as cache candidates, cheap constants as recompute, and frame-varying or temporal work as non-cacheable.
4. [complete] Add allocator overlap, ping-pong reuse, cache-safety, deterministic-output, and documentation coverage.

### Active basic-graphics checkpoint

Each item is an independent test-and-commit gate; work proceeds strictly in order.

1. [complete] Add FFmpeg-backed static image assets/layers, typed plans, CPU execution, placement/effects/blending, planner diagnostics, fixtures, and documentation.
2. [complete] Add native rectangle, rounded-rectangle, and ellipse layers with fill/stroke semantics, CPU reference rendering, optimizer integration, tests, and documentation.
3. [complete] Add optional HarfBuzz/FreeType shaped text, Chroma-owned glyph layout/raster resources, CPU reference rendering, graceful dependency-disabled behavior, tests, and documentation.
4. [complete] Consolidate static prepared-resource identities and cache planning across images and text, add the representative basic-graphics demo/bench fixture, test, review, and document the boundary.

### Active render-speed measurement checkpoint

1. [complete] Establish a clean Release build and rerun the existing 640×360 video corpus to detect performance regressions or gains against the recorded ~261 fps baseline.
2. [complete] Measure the representative video/image/shape/text workload with enough iterations to separate cold-start noise from steady-state throughput.
3. [complete] Isolate prepared image/text reuse with a controlled cache-disabled comparison so any claimed speedup is attributable rather than inferred.
4. [complete] Record the methodology, raw reports, result, and limitations; do not describe planner-only fusion diagnostics as runtime acceleration.

### Active authoritative fused-execution checkpoint

1. [complete] Implement an authoritative CPU fused path for the measured-profitable subset of source sampling, one leading coordinate transform, crop, opacity, and compositing without a full-frame transform intermediate. Color and placement-only fusion remain reference-only after negative measurements.
2. [complete] Preserve the existing reference path for barriers, unsupported/unsafe orderings, and unprofitable programs, with exact pixel-equivalence and fallback tests.
3. [complete] Benchmark the fused path against the prior renderer and report intermediate reduction plus end-to-end and execution-stage timing, including negative and regression controls.
4. [complete] Define the first CUDA kernel candidate independently from the remaining codec/color-conversion residency boundary; do not over-scope that conversion work into this CPU slice.

### Active codec/color residency checkpoint

Each item is a separate test-and-commit gate.

1. [complete] Instrument decoded/converted/encoded frame counts, estimated transfer bytes, and conversion time separately from decode, composition, and codec time; expose stable benchmark JSON and regression tests.
2. [complete] Introduce a dependency-neutral resident-frame descriptor and ownership contract without leaking FFmpeg or CUDA types across Chroma's core/public boundary.
3. [complete] Implement and benchmark the first safe CPU conversion reduction, retaining the current path as a correctness oracle and avoiding premature color-management claims.
4. [complete] Specify and scaffold the Linux/NVIDIA hardware-frame path, including runtime CUDA/device/decoder/NVENC readiness checks and explicit unimplemented status. Actual kernels remain gated on a capable Linux/NVIDIA environment and CPU-reference comparison.
5. [in_progress] Resume the user-visible MVP sequence: keyframes/easing and masks/blur/transitions are implemented; audio is now implemented; bounded image/text caches are now implemented; ABI-v1 synchronous C API and CMake installation are now implemented; broader distribution packaging and published comparison corpus remain.

### Dependency policy for these slices

- Keep FFmpeg as the media decode, encode, mux, and initial audio boundary.
- Use a specialized text shaping/raster stack when text work begins; do not reimplement font shaping.
- Defer OpenFX, frei0r, OpenCV, Skia-as-general-compositor, and broad FFmpeg-filter compatibility until the native effect IR and fusion boundary are proven.
- Evaluate libplacebo for reusable GPU color/scaling primitives, but do not let its internal graph replace Chroma's optimizer or introduce avoidable surface transfers.
- Treat any external effect invocation as an explicit optimizer barrier unless its semantics can be expressed in Chroma's native IR.

### Gates before broadening scope

- Usefulness: render a representative template video with video/image, animated text/graphics, transitions, and audio.
- Correctness: CPU reference and accelerated output agree within documented pixel/audio tolerances and timing remains deterministic.
- Optimizer: inspection proves fewer passes/intermediates for representative chains, with regression tests for illegal fusion.
- Performance: demonstrate a measured end-to-end advantage on at least one published representative workload before claiming the architecture is faster.

## Errors Encountered
| Error | Attempt | Resolution |
|---|---|---|
| Initial findings patch context mismatch | 1 | Inspected current headings and reapplied against the actual file structure. |
| Initial repository scaffold patch used the wrong table separator context | 1 | Split the patch and applied it against the current file contents. |
| Local CMake configure requested Ninja, but Ninja is not installed | 1 | Removed the incomplete generated build directory and used CMake's default generator. |
| AppleClang rejected `[[nodiscard]]` before friend comparison declarations | 1 | Removed the non-essential attributes from the friend operators. |
| A single patch attempted to delete and re-add the CLI file | 1 | Kept the existing file and applied a normal update in a separate patch. |
| FFmpeg integration patch targeted `CMakeLists.txt` twice in one patch | 1 | No files changed; consolidated all CMake edits into one update operation. |
| Decode test assumed generated `testsrc2` pixels changed at selected samples | 3 | Stopped sampling a non-guaranteed motion pattern; replaced it with deterministic one-second blue and red segments joined by FFmpeg. |
| First directly encoded 60-frame MP4 probed at about 1.966 seconds instead of 2 seconds | 1 | Set frame and stream durations explicitly; added a regression test proving 60 frames at 30 fps probe as exactly 2 seconds. |
| FFmpeg-disabled build warned about an unused timing helper | 1 | Compiled the steady-clock helper only when the FFmpeg render path is enabled. |
| End-of-media test did not throw for Matroska fixture | 1 | Matroska exposed duration only at container level; rescaled the container duration into the selected stream time base as a fallback. |
| Adding effect programs as defaulted aggregate members failed to compile | 1 | Replaced the explicit constructor's default argument with a real default constructor while keeping vector conversion explicit. |
| `clang-format` was unavailable on the development host | 1 | Kept the existing project style manually and verified whitespace with `git diff --check` plus compiler warnings. |
| Invalid fit prevented an independent invalid-position diagnostic | 1 | Parsed fit and position independently, then returned placement only when both were valid. |
| Legacy optimizer test expected one `multi_pass_required` marker | 1 | Replaced it with typed assertions over the new concrete fusion and barrier passes, resource edges, and cost totals. |
| Source inventory included a nonexistent `cmake/` directory | 1 | `rg` reported the missing optional path; continued with the actual repository directories and root `CMakeLists.txt`. |
| Combined image documentation patch assumed a stale composition-format heading | 1 | No files changed; inspected the current document and split the patch against exact headings. |
| Initial shape execution-plan patch assumed helper ordering that differed from the file | 1 | No files changed; inspected exact helper and variant branches, then split the patch into smaller updates. |
| Image viewer could not open the generated PPM shape frame | 1 | Kept PPM as the renderer output, converted the verification copy to PNG with FFmpeg for visual inspection. |
| Text cache test tried to place its temporary font beside a read-only system font | 1 | Moved the disposable font copy into the writable system temporary directory. |
| The first approximate FFmpeg timing loop used `rm -f` for temporary outputs and was rejected by the command safety layer | 1 | Reused one explicit `/tmp` output path without deleting it during the timing loop. |
| The four-batch video A/B command exceeded the tool's 30-second output window while its final batch continued | 1 | Confirmed the exact remaining process and waited for that already-running batch to finish before aggregating results. |
| The fused-render baseline aggregation expected nested `mean` fields that the one-iteration report does not contain | 1 | Inspected the report schema and selected the scalar summary fields directly. |
| The first per-pixel effect-variant dispatch made the transformed color workload slower | 1 | Specialized the legal kernel, measured individual chains, and narrowed the authoritative profitability gate rather than enabling all planner candidates. |
| Color-matrix and small placement fusion remained slower after hot-loop specialization | 1 | Kept both on the materializing reference path and documented the negative results; only the measured-positive leading-transform subset is authoritative. |
| The first decode-LUT baseline run configured the generated composition but had not built its media fixture target | 1 | Built `chroma_benchmark_fixture` in the detached baseline worktree before rerunning only the missing baseline batches. |

### Animated transform checkpoint

1. [complete] Add typed vector/scalar translation, scale, and rotation keyframes, easing, origins, and lowering to affine matrices before optimization.
2. [complete] Expose static values and keyframes in composition JSON, validate inputs, document semantics, and add a representative animated-card example.
3. [complete] Preserve animation dependencies in cache planning and verify exact CPU reference/fused equivalence across frames and blend modes, rational-rate MP4 output, geometry, and failure paths.

### Bounded prepared-resource cache checkpoint

1. [complete] Implement independent deterministic LRU caches with byte budgets, oversize bypass, lifetime-safe handles, eviction and failure accounting.
2. [complete] Bound image surfaces and shaped-text preparation in CPU contexts, expose budget changes/clear/usage, and prove pixel equivalence across cache policies.
3. [complete] Expose CLI budgets and render/benchmark usage reports; test repeated request sequences, exact counters, disabled/oversized behavior, job isolation, configuration errors, and dependency-disabled builds.

These are job-scoped prepared-source caches. Persistent content-addressed storage
and effect-output caching remain separate future milestones.

### Stable C API checkpoint

1. [complete] Define fixed-width, versioned/sized ABI types and opaque ownership, validation/diagnostics, metadata, plans, capability queries, cache controls, and exception-safe error boundaries.
2. [complete] Add owned CPU frames, UTF-8 file loading, synchronous MP4 rendering, C99 usage examples, and encoded-media integration tests.
3. [complete] Export only declared C symbols; install a relocatable CMake package; freeze a v1 header/symbol/layout consumer; verify thread-local errors and C-only installation in media-enabled and core-only builds.

Async job scheduling, cancellation/progress callbacks, and thin language bindings
remain separate milestones rather than unimplemented promises of the v1 ABI.

### 1080p showcase checkpoint

1. [complete] Author and reproduce an 18-second editorial film with footage, animated text/cards, transitions and original music; preserve asset attribution and checksums.
2. [complete] Validate all 540 presentation timestamps, exact audio/video durations, stereo audio content/fade, cache-equivalent native frames and encoded image quality; inspect scene and transition frames.
3. [complete] Publish six sequential full-resolution CPU measurements with per-stage timings, peak RSS, raw results and limitations. The cached median is 85.91 seconds versus 89.37 seconds uncached (~4% observed throughput gain with overlapping ranges). Equivalent FFmpeg/Remotion comparisons and GPU acceleration remain pending.
