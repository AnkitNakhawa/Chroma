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

Early design. The next deliverable is a minimal technical spike that can render and benchmark a small composition through both the CPU and CUDA execution paths.
