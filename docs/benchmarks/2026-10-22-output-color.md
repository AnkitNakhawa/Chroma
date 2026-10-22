# Exact output color conversion — 2026-10-22

On the unchanged 18-second 1920×1080/30 showcase, the lookup conversion renders
in **20.81 s** versus **27.75 s** with the retained scalar conversion:
**1.33× throughput**, **25.0% less wall time**. The output conversion stage itself
falls from a mean **9.470 s to 2.700 s** (3.51×). This is a CPU optimization.

## Implementation

A 127 KiB immutable lookup table groups adjacent positive float32 bit patterns.
Buckets whose endpoints quantize to the same byte return that byte; buckets
crossing rounding boundaries use the original `pow`/`lround` implementation.
No interpolation changes the sRGB curve or quantization. A bulk RGB conversion
loop accesses the table once per frame. See the
[reproduction and correctness notes](../../benchmarks/OUTPUT_COLOR.md).

## Controlled measurement

- Apple M5, 16 GiB, macOS, Release; developer FFmpeg configuration, not the
  OpenH264 release bundle. Code checkpoint `32274b5`.
- Both binaries built from the same source/configuration; only
  `CHROMA_REFERENCE_OUTPUT_CONVERSION` differs. Both use bounded graphics.
- Same frozen composition SHA
  `a8faa46b091597b7952bc49799497872a2ef1ed09ff791bb8e8324e826182cb6`,
  assets, caches, media fusion, H.264/AAC settings, 540 video frames and 864,000
  audio sample frames. Asset preparation excluded.
- One discarded full encode warm-up per mode (lookup 30.16 s, scalar 27.32 s),
  then three fresh-process samples per mode in alternating order. No concurrent
  Chroma tests/renders. Warmup timings show transient system variation; measured
  ranges do not overlap. This is a local experiment, not a general guarantee.

| Conversion | Measured wall times | Median | Throughput | Median peak RSS |
|---|---|---:|---:|---:|
| Scalar | 27.43, 27.85, 27.75 s | 27.75 s | 19.46 fps | 789.7 MiB |
| Lookup | 20.81, 20.91, 20.71 s | 20.81 s | 25.95 fps | 798.2 MiB |

Memory usage did not improve. Rendering still takes about 1.16× the 18-second
video duration; it is closer to real time but remains below 30 fps.

| Mean leaf stage | Scalar | Lookup |
|---|---:|---:|
| Output linear RGB → sRGB8 | 9.470 s | 2.700 s |
| Composition | 9.880 s | 9.849 s |
| Decode | 2.930 s | 2.881 s |
| Decode conversion | 3.049 s | 3.018 s |
| RGB → YUV | 0.963 s | 0.984 s |
| Codec/mux | 0.681 s | 0.694 s |
| Audio | 0.322 s | 0.320 s |

Enclosing `execute`, `encode` and `total` timings overlap these stages and must
not be added again. Unchanged stages remain similar; composition is now the
largest measured cost. Do not multiply this ratio by historical ratios measured
in separate runs to claim a new controlled end-to-end comparison.

## Quality and regression checks

The exhaustive scalar comparison passed for 117,440,513 float32 values covering
all quantization boundaries, with additional extreme/bulk layout checks. All
71 media-enabled CTests passed, including the existing native pixel and encoded
video quality gate; the core-only conversion test passed as well.

Full-showcase scalar/lookup delivery renders preserve identical decoded video
frames and timestamps and identical decoded audio PCM. Stream dimensions,
H.264/yuv420p format, frame count, rate, duration, AAC sample rate/channels and
audio duration are checked independently. Raw benchmark and verification evidence
is in [the results](2026-10-22-output-color-results.json).
