# Bounded-region graphics on the unchanged 1080p showcase — 2026-10-18

The 18-second 1920×1080/30 showcase now renders in a median **26.32
seconds**, versus **88.88 seconds** with the retained full-frame
text/shape implementation: **3.38× higher throughput**,
**70.4% less wall time**. All 540 native frames
match bit-for-bit across **1,119,744,000 float RGBA pixels**.

## What changed

Chroma rasterizes eligible text/shapes into clipped canvas-coordinate regions,
then transforms and blends only their conservative affected bounds. It retains
intermediate clipping, original sample arithmetic, 4×4 shape coverage and glyph
order. Transform/crop/opacity programs use the region path; blur, masks,
alpha-creating color matrices and other unsupported effects retain the full-frame
path. Extreme or ill-conditioned transforms use conservative full-canvas traversal.
HarfBuzz/FreeType still prepare text; FFmpeg still decodes, converts, encodes and
muxes. This experiment changes Chroma graphics execution, not those libraries.

## Controlled comparison

- Apple M5 / 16 GiB, macOS 26.6 (25G72), AppleClang 21, FFmpeg 9.0.2.
- CMake Release, FFmpeg and HarfBuzz/FreeType enabled; CPU execution only.
- Code revision `e46a959`; the original full-frame graphics implementation is
  retained behind `--graphics-mode full-frame`. Both variants use the **same
  executable**, compiler settings, media fusion, prepared-resource cache budgets,
  assets, timeline and H.264/AAC encoder settings. This isolates region execution.
- Frozen composition SHA-256:
  `a8faa46b091597b7952bc49799497872a2ef1ed09ff791bb8e8324e826182cb6`.
  This is exactly the composition from the
  [original showcase benchmark](2026-10-18-showcase.md), with the same source
  footage, fonts, original score, 540 frames and 864,000 audio sample frames.
  Asset download/normalization/score synthesis are excluded from both variants.
- One complete encoded warm-up per mode was discarded. Three measured fresh-process
  full encodes per mode followed, in bounded/full, full/bounded, bounded/full order.
  No concurrent render/test workload was launched during the measurement batch.
  The filesystem is warm; prepared caches start empty in every process.
- Wall time and peak process RSS come from `/usr/bin/time -l`; stage times/counters
  come from the renderer. These are local non-isolated measurements, not population
  estimates. Raw samples and executable/asset hashes are preserved.

## End-to-end measurements

| Mode | Median wall | Throughput from median | Wall range | Median peak RSS | Maximum peak RSS |
|---|---:|---:|---:|---:|---:|
| full-frame | 88.88 s | 6.08 fps | 86.94–90.22 s | 770.2 MiB | 815.0 MiB |
| bounded | 26.32 s | 20.52 fps | 25.40–26.75 s | 811.1 MiB | 825.0 MiB |

The timing ranges do not overlap in this six-sample run. The bounded median is
**1.46× the film's duration**: substantially faster, but still
below real-time 1080p/30. There is **no demonstrated peak-memory improvement**:
the bounded mode's observed RSS is somewhat higher. These region-work reductions
do not bound media/codec state, solid-layer surfaces, temporary allocations,
allocator retention or total process memory.

## Mean stage times

| Leaf stage | Full-frame | Bounded |
|---|---:|---:|
| composition | 71.845 s | 9.335 s |
| decode | 2.833 s | 2.796 s |
| decode_conversion | 2.721 s | 2.742 s |
| linear_to_rgb | 9.021 s | 9.092 s |
| rgb_to_yuv | 0.915 s | 0.915 s |
| codec_mux | 0.692 s | 0.658 s |
| audio | 0.315 s | 0.307 s |
| compile | 0.004 s | 0.003 s |

Composition is the main reduction. Decode, media conversions, the final full-frame
RGB/YUV conversion and encoder work still occur. `execute`, `encode` and `total`
in raw reports enclose these leaf timings and must not be added again. Variations
in unchanged stages reflect normal measurement noise; they are not separate
optimizations claimed by this change.

## Graphics work and exact equivalence

| Graphics destination slots | Full-frame | Bounded | Reduction |
|---|---:|---:|---:|
| source_pixels | 10,720,512,000 | 250,213,698 | 97.67% |
| transform_pixels | 1,654,732,800 | 39,120,011 | 97.64% |
| composite_pixels | 10,720,512,000 | 252,371,462 | 97.65% |

Both variants execute the same 5,170 text/shape operations. The bounded showcase
has zero full-frame graphics fallbacks. All six measured profiles have exactly
the work counts above, plus the same text-cache usage: 30 insertions/misses,
3,985 hits and 397,756 retained accounted bytes. These are destination-slot work
counters, not GPU transfer bytes or estimates of whole-process memory. Planner
full-canvas cost estimates remain conservative; runtime counters show the savings.

The offline C++ verifier executes every frame with independent persistent contexts
and compares the entire float RGBA buffers using `memcmp`. It preserves video/image
fusion in both modes, so only graphics execution differs. Its approximately
93-second combined validation runtime is not benchmark evidence. CI additionally
covers all shape/blend types, fractional and clipped geometry, strokes, sequential
and singular transforms, extreme arithmetic, 200 seeded affine cases, Unicode
shaping/negative bearings, cache hits, effect fallbacks and CLI dispatch/telemetry.

The separate encoded delivery is **byte-for-byte identical to the original MP4**,
including video, audio and container data (SHA-256
`4928a7273aff19d07ad456625f47c26edeb9a1905a6d49907197137f492f95ca`).
Its decoded video frame checksums also match the original verification exactly;
recorded media validation accompanies the result. The delivery verifier
also checks all timestamps, stream durations, decoded audio and four native/encoded
scene comparisons.

## Reproduce and inspect evidence

Follow [the verification/benchmark instructions](../../benchmarks/BOUNDED_GRAPHICS.md).
Keep the existing showcase unchanged; the runner refuses a different composition
hash or prepared assets that disagree with the manifest.

- [All measured samples, timings, work counters and RSS](2026-10-18-bounded-graphics-results.json)
- [Every-frame bitwise float equivalence](2026-10-18-bounded-graphics-equivalence.json)
- [Encoded delivery comparison and validation](2026-10-18-bounded-graphics-delivery.json)

This is an end-to-end improvement on this exact workload against Chroma's own
retained pre-optimization graphics path. It is not an FFmpeg/Remotion comparison,
a GPU result, or evidence of the same factor on every composition.
