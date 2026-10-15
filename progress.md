# Progress

## 2026-10-13
- Pressure-tested the original performance-rewrite premise.
- Reviewed Remotion's current rendering, Chromium GPU, and hardware-encoding capabilities.
- Identified the stronger concept: an agent-first declarative video format executed by a native renderer, rather than agent-generated C++ or a full Remotion clone.
- Started Phase 1: high-level product requirements and scope.
- Completed Phase 1 by defining the product promise, primary workflow, MVP functional requirements, non-functional requirements, explicit non-goals, and success gates.
- Narrowed the product boundary: headless rendering library/CLI and bindings only. Removed the first-party preview platform, editor, hosted service, and local preview daemon from scope.
- Researched the VFX pipeline and separated film-grade simulation and path tracing from 2D compositing. Current evidence favors a headless agent-first 2D effect graph as a stronger wedge than a new general-purpose renderer.
- Chose a provisional v1 GPU direction: CPU reference backend plus a narrow Linux/NVIDIA CUDA backend, using FFmpeg hardware frames with NVDEC/NVENC and benchmarking primarily on cloud L4 hardware.
- Adopted Chroma as the working project name and initialized a local Git repository on `main`.
- Started Phase 2 and wrote the first architecture draft covering the frontend, immutable IR, compiler, execution plan, scheduler, CPU/CUDA backends, media boundary, color semantics, caching, public APIs, repository layout, and technical spike.
- Began the implementation spike with a CMake/C++20 project, exact rational and frame timeline primitives, a dependency-free unit-test harness, and a minimal CLI target.
- Added an experimental v1 composition parser for canvas and solid-layer documents, structured diagnostics, example input, and agent-facing validation support.
- Added the initial CPU render path with linear premultiplied pixels, document-order source-over compositing, PPM serialization, and focused surface/render tests.
- Rendered and visually inspected the 1920x1080 solid example; the generated frame matched the declared color and dimensions.
- Reviewed the implementation and hardened it by enforcing composition invariants for direct C++ callers, bounding CPU surface allocations, documenting layer/color semantics, and adding regression tests.
- Published the repository at https://github.com/AnkitNakhawa/Chroma and configured the local `main` branch to track `origin/main`.
- Added an Apache-2.0 license, Linux/macOS GitHub Actions CI, and reproducible local build instructions.
- Verified the first remote CI run passed on Ubuntu and macOS, then updated the checkout action from v4 to v7 to remove GitHub's Node 20 deprecation warning.
- Added the first typed execution-plan compiler boundary. It validates the requested frame, emits an explicit clear, removes inactive layers, preserves document-order compositing, retains source IDs for diagnostics, and has deterministic JSON inspection output.
- Migrated the CPU reference renderer onto the execution-plan boundary and retained `render_frame_cpu` as a compile-and-execute convenience API.
- Added `chroma inspect-plan <composition> --frame <index>` so agents and developers can inspect the exact optimized operations as deterministic JSON.
- Hardened direct execution-plan construction so invalid dimensions, colors, source IDs, and repeated clear operations fail before reaching a backend.
- Added and exercised a 60-frame layered demo composition. Chroma rendered every frame, the plan correctly activated the overlay only for frames 15–44, and FFmpeg assembled the output into a verified 2-second 320x180 H.264 demo at 30 fps.
- Started the full pre-CUDA implementation track: optional FFmpeg integration, probing, deterministic decode, video layers, direct encoding, profiling, and CPU benchmarks.
- Added an optional FFmpeg media boundary and `chroma probe`. Verified exact metadata against the layered H.264 demo and confirmed that both FFmpeg-enabled and explicitly disabled builds pass all eight tests.
- Added deterministic time-based CPU decode with backward seeking, presentation-order selection, actual timestamp reporting, libswscale conversion, and a `chroma decode` command. A generated blue/red lossless fixture verifies seeking, temporal selection, repeatability, and pixels; the decoded red frame was visually inspected. FFmpeg-enabled and disabled builds both pass.
- Extended the experimental composition model with relative-path video assets and ordered video layers. Frame compilation now emits typed video operations with exact source times, and the CPU executor decodes and composites them. Parser, plan, renderer, and CLI integration tests cover the full path.
- Added native sequential H.264/MP4 output through FFmpeg with no intermediate frame files. Fixed a one-frame mux-duration defect, added exact duration regression coverage, and visually verified the encoded blue/red sequence.
- Added internal stage timing and a JSON `benchmark` command. The first five-iteration Release run established a tiny-fixture CPU baseline and showed execution/decode dominates while compilation is negligible; results are saved under the ignored benchmark-results directory.
- Added a reproducible 640x360, 120-frame benchmark corpus. Chroma measured ~182 fps versus an approximately equivalent FFmpeg pipeline's ~1,333 fps median on the same host, honestly establishing that the current per-frame decoder lifecycle is about 7.2x slower and must be fixed before CUDA work.
- Added job-scoped persistent sequential decoders with lookahead and backward-seek reset. The 640x360 corpus improved to ~259 fps and reduced execution time by about 45%, though the full pipeline remains roughly 5.2x slower than the approximate FFmpeg baseline because of extra color conversions and float compositing.
- Completed the pre-CUDA focused review. Cached pixel-conversion state, isolated decoder sessions per layer, rejected requests beyond known media duration, removed partial outputs after render failures, and added regression tests for backward seeking, media bounds, and cleanup. The final local CPU corpus measured ~261 fps versus the approximate FFmpeg baseline's ~1,333 fps.

## 2026-10-14
- Defined the post-baseline development strategy around vertical slices that pair user-visible MVP capabilities with explicit graph-compiler optimizations.
- Narrowed the first useful output to template-style video with image/video backgrounds, animated text and graphics, simple transitions, audio, and direct encoding.
- Prioritized the effect IR, optimizer traits, algebraic rewrites, fusion partitioner, and inspection diagnostics before CUDA code generation.
- Adopted a conservative integration policy: retain FFmpeg at the media boundary, reuse specialized text/color primitives when needed, and defer broad effect/plugin integrations until Chroma's native optimizer is proven.
- Added the first immutable effect IR with validated transform, crop, opacity, color-matrix, blur, temporal, and external nodes plus explicit optimization traits.
- Added conservative identity removal, adjacent transform/opacity/color-matrix composition, fusion-barrier partitioning, stable effect JSON, and `inspect-optimized-plan`. The command is explicitly planning-only; the existing typed plan remains authoritative for execution.
- Added focused effect and plan tests. Both FFmpeg-enabled and core-only builds remain green after the compiler slice.
- Added CPU reference execution for transform, crop, opacity, and color-matrix effect programs, including transparent-edge bilinear sampling, singular-transform behavior, straight-alpha color math, and explicit failures for unsupported effect classes.

## 2026-10-15
- Exposed transform, crop, opacity, and color-matrix programs through the experimental composition JSON and typed execution plan.
- Added source-over, multiply, screen, and plus blending with premultiplied-alpha CPU semantics.
- Normalized legacy video opacity into the same effect chain while removing identity opacity before execution.
- Added parser/compiler/renderer seam tests, invalid-input diagnostics, fusion-barrier inspection coverage, and a reproducible effects composition.
- Rendered and visually inspected a 320×180 effects frame, then encoded and probed a 60-frame, 30 fps, exactly two-second H.264 demo.
- Made optimized effect programs authoritative in the typed execution plan while retaining rewrite counts in both exact and optimized inspection output.
- Added conservative dead-layer elimination for provably transparent effect chains and backward culling when an unaffected opaque full-canvas solid completely replaces earlier work.
- Added regression tests for combined opacity execution, rewrite diagnostics, transparent-layer safety, alpha-restoring color matrices, and opaque-cover culling.
- Added typed video source placement with `none`, `contain`, `cover`, and `fill` modes plus normalized alignment, carried through parsing and plan inspection.
- Added deterministic CPU bilinear placement before effects and blending, removing the requirement that decoded video dimensions match the canvas.
- Added parser diagnostics, per-mode pixel tests, and a real mismatched-dimensions video integration test; completed the CPU effects/placement implementation slice.
- Added a typed physical execution-plan analysis with fusion-candidate, clear, and barrier passes plus named resource reads/writes.
- Split neighborhood, temporal, and external effects into explicit barriers while preserving fusible regions on both sides; temporal barriers account for neighboring frame inputs.
- Added deterministic pass-count, logical-temporary, full-frame byte-traffic, peak-live-memory, and rewrite diagnostics to `inspect-optimized-plan`.
- Added lifetime-based physical temporary reuse with strict read/write non-aliasing, plus physical buffer counts, byte estimates, and allocation intervals.
- Added conservative cache opportunity diagnostics for frame-varying, temporal, nondeterministic, cheap static, and expensive deterministic subgraphs; cache execution and persistence remain unimplemented.
- Verified the reuse/cache checkpoint with 21/21 FFmpeg tests, 13/13 core-only tests, and 13/13 sanitizer tests; a focused review found no blocking correctness issues.
- Started the sequential basic-graphics track with four separate test-and-commit gates: images, native shapes, shaped text, and static prepared-resource planning/demo coverage.
- Completed static image assets/layers with typed parsing and plans, FFmpeg still-image decode, render-context surface reuse, placement/effects/blending, static-resource cache diagnostics, a real PNG fixture, and updated examples/docs. Verified 22/22 FFmpeg, 14/14 core-only, and 14/14 sanitizer tests.
- Completed native rectangle, rounded-rectangle, and ellipse layers with canvas-space bounds, fill, optional inside stroke, deterministic 4×4 CPU coverage, typed fusion-plan nodes, parser/plan/pixel tests, an example, and visual inspection. Verified 23/23 FFmpeg, 15/15 core-only, and 15/15 sanitizer tests.
- Completed optional HarfBuzz/FreeType text assets/layers with UTF-8 shaping, dependency-neutral prepared glyph coverage, render-context reuse, effects/blending, static-resource plan diagnostics, disabled-build behavior, tests, CI dependency setup, examples, and docs. Final verification passed 25/25 FFmpeg+text, 17/17 core+text, 17/17 sanitizer+text, and 17/17 text-disabled tests.
