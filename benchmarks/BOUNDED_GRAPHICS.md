# Bounded graphics verification and benchmark

Build Release with tests, FFmpeg and text support. Reuse the exact showcase and
assets from `examples/showcase/README.md` without regenerating or editing them.

```sh
./build-release/chroma_graphics_equivalence \
  artifacts/showcase/composition.json artifacts/bounded-region/equivalence.json
python3 benchmarks/bounded_graphics.py --chroma build-release/chroma \
  --composition artifacts/showcase/composition.json \
  --output artifacts/bounded-region/benchmark --runs 3
```

The C++ verifier compares every float RGBA byte in every frame, using independent
persistent contexts/decoders for bounded and full-frame graphics. It is deliberately
not itself an automatic CTest: the original full 1080p corpus requires separately
downloaded stock assets. [CI quality gates](../docs/quality-gates.md) reuse this
verifier on a generated full-timeline proxy, fractional-rate and full-HD corpus. It fails without
writing a successful report on any mismatch. The comparison does not encode;
its elapsed time is validation time, not benchmark evidence.

The Python runner validates the frozen composition hash and prepared assets,
discards one complete audio/video encode per variant, then alternates three or
more fresh-process encodes per variant, with no concurrency. Media fusion, text
cache budgets, timeline, assets and encoder settings remain identical. Wall time,
peak process RSS, stage profiles and graphics counters are recorded after each
sample. Close other workloads during measurement. Temporary benchmark MP4s are
removed by the CLI after complete encode/mux; render a separate delivery copy to
compare decoded content with the original showcase.

The `--graphics-mode bounded|full-frame` option also works on `frame` and `render`.
The full-frame mode is a retained correctness/performance control, not a change to
composition semantics. Neither variant runs CUDA or a competing renderer.
