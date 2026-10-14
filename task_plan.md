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
| 2 | Define system architecture and technology choices | in_progress |
| 3 | Specify the composition DSL, timing model, and compatibility contract | pending |
| 4 | Design the rendering, media, audio, preview, and caching pipelines | pending |
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
4. FFmpeg media probing and decode boundary behind an optional build feature.
5. Execution-plan skeleton, profiling events, and a benchmark fixture.
6. Linux CUDA spike for a narrow decode/effect/composite/encode chain.

## Errors Encountered
| Error | Attempt | Resolution |
|---|---|---|
| Initial findings patch context mismatch | 1 | Inspected current headings and reapplied against the actual file structure. |
| Initial repository scaffold patch used the wrong table separator context | 1 | Split the patch and applied it against the current file contents. |
| Local CMake configure requested Ninja, but Ninja is not installed | 1 | Removed the incomplete generated build directory and used CMake's default generator. |
| AppleClang rejected `[[nodiscard]]` before friend comparison declarations | 1 | Removed the non-essential attributes from the friend operators. |
| A single patch attempted to delete and re-add the CLI file | 1 | Kept the existing file and applied a normal update in a separate patch. |
