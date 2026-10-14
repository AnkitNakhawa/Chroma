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
2. [next] Implement transforms, crop/fit, opacity, color matrices, and blend modes in the CPU reference backend. Add algebraic simplification for identities, combined transforms, combined opacity/color operations, and dead nodes.
3. [pending] Partition the optimized graph into fusible groups and explicit barriers. Add pass-count, intermediate-surface, estimated-byte-traffic, and cache-decision diagnostics; prove the planner with tests before GPU code generation.
4. [pending] Add image layers plus text and basic shapes. Prefer a narrow text shaping/raster dependency, cache static raster results, and avoid adopting a general compositor dependency at this stage.
5. [pending] Build the first Linux/NVIDIA CUDA vertical slice: retain decoded surfaces on the GPU where supported and fuse sample/transform/color/opacity/composite for the benchmark-critical path. Compare output against the CPU reference backend.
6. [pending] Add keyframes/easing, simple masks, blur, and transitions. Use blur as the first neighborhood-effect barrier and validate fusion on both sides of it.
7. [pending] Add audio input, trim, gain, simple mixing, and muxing through FFmpeg to complete the useful basic-video path.
8. [pending] Publish benchmark comparisons against equivalent FFmpeg and GPU-configured Remotion workloads. Report end-to-end time, GPU/CPU transfer bytes, peak memory, kernel/pass count, and warm/cold behavior.

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
