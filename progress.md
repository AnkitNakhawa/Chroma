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
