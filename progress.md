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
