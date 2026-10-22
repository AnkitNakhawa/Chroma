# Constant composition passes — 2026-10-22

On the same frozen 18-second 1080p showcase, median rendering time falls from
**21.34 s to 19.91 s**: **1.072× throughput**, **6.7% less wall time**. Mean
composition time drops from **10.057 s to 8.780 s**, a **12.7% reduction**.
This is a measured incremental CPU gain; it is smaller than the earlier bounded
text/shape and color-conversion gains.

## Controlled comparison

Apple M5, 16 GiB, macOS, Release, developer FFmpeg configuration (not the
OpenH264 release bundle), implementation checkpoint `02436d4`. Both binaries use
the same source/configuration except `CHROMA_REFERENCE_COMPOSITION_PASSES`.
Both retain bounded graphics, output color lookup, media fusion, identical cache
budgets and H.264/AAC settings. Each encodes 540 video frames and 864,000 audio
sample frames. The frozen composition SHA is
`a8faa46b091597b7952bc49799497872a2ef1ed09ff791bb8e8324e826182cb6`.

One full encoded warm-up per mode was discarded (optimized 20.01 s, materialized
20.31 s), then three fresh-process encodes per mode were alternated. Asset
preparation is excluded. No concurrent Chroma test/render ran during timing.
These are local measurements with ordinary system variation, not general
performance guarantees. Do not multiply ratios from separately measured
historical runs to claim a new controlled comparison.

| Mode | Wall samples | Median | Throughput | Median peak RSS |
|---|---|---:|---:|---:|
| Materialized passes | 21.05, 21.34, 21.52 s | 21.34 s | 25.30 fps | 826.0 MiB |
| Optimized passes | 19.68, 19.91, 20.53 s | 19.91 s | 27.12 fps | 827.7 MiB |

Peak process memory did not improve. Rendering remains about 1.11× the video
duration, below real-time 1080p/30.

| Mean leaf stage | Materialized | Optimized |
|---|---:|---:|
| Composition | 10.057 s | 8.780 s |
| Decode | 3.017 s | 3.018 s |
| Decode conversion | 3.048 s | 3.077 s |
| Output color conversion | 2.825 s | 2.842 s |
| RGB → YUV | 0.989 s | 0.986 s |
| Codec/mux | 0.677 s | 0.655 s |
| Audio | 0.328 s | 0.323 s |

Nested `execute`, `encode` and `total` fields overlap these leaf stages and must
not be added again. Unchanged stages are broadly stable; composition supplies
the measured gain.

## Avoided work

All optimized profiles record the same reductions:

- 1,119,744,000 initialization pixel writes avoided.
- 617,932,800 constant-prefix blend slots avoided.
- Full-canvas solid sources covering 80,870,400 pixel slots replaced with
  one-pixel sources.

These are work counters, not unique pixel totals, allocation bytes or RSS savings.
The change initializes each result directly from the clear/constant prefix and
evaluates uniform solid opacity once instead of across a temporary canvas.
Spatial effects retain the original path. It does not remove all full-frame
work or introduce scratch pooling. See the [implementation scope and reproduction
commands](../../benchmarks/COMPOSITION_PASSES.md).

## Quality

All 72 media-enabled CTests passed, including the existing native/encoded video
quality gate and 301 new bitwise composition cases. The new test also passed in
the core-only build. The frozen showcase verification additionally requires:

- Exact native float RGBA equality for all 540 frames (1,119,744,000 pixels).
- Identical decoded video frames and presentation timestamps.
- Identical intended audio PCM, with stream format, counts, rates and durations
  independently checked.

Raw benchmark profiles, hashes and verification evidence are in
[the results](2026-10-22-composition-passes-results.json).
