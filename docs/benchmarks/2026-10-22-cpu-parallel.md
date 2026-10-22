# Bounded CPU parallelism — 2026-10-22

On the unchanged 18-second 1080p showcase, the direct pre-change/post-change
comparison measured **20.60 s → 18.33 s** median render time: **1.124× throughput**,
**11.0% less wall time**. This is the headline comparison against the saved
previous implementation, not a ratio inferred from an artificially serial build.

The parallel range was **16.31–18.45 s**. Some samples rendered faster than the
video duration, but this batch's median remains slightly slower than real-time
1080p/30. Do not describe it as a guaranteed 30 fps renderer.

## Implementation and controls

Chroma now reuses a process-wide pool with at most three background workers plus
the caller. Independent pixel ranges/rows run in parallel; layer order and each
pixel's arithmetic remain sequential. Small work, nested requests and contended
pool requests run inline. There is no queued frame pipeline or per-frame worker
creation. Existing cache limits and returned-surface ownership are unchanged.

The paths cover output conversion, suitable solid/media blending, source
transforms, placement and shape rasterization. Decode, most fallback effects,
text glyph processing and codec/mux remain unchanged. See the
[runtime contract](../cpu-parallelism.md) and
[reproduction commands](../../benchmarks/CPU_PARALLEL.md).

Apple M5, 16 GiB, macOS, CMake Release, developer FFmpeg configuration (not the
OpenH264 release bundle). Current implementation checkpoint `49583d4`; the saved
previous executable corresponds to renderer checkpoint `02436d4` (subsequent
pre-parallel commits only added documentation/verification tooling). Its SHA was
verified against the earlier benchmark's recorded optimized executable hash.

Both builds use bounded graphics, optimized output color conversion, optimized
constant composition passes, the same cache budgets and H.264/AAC settings. The
composition SHA remains
`a8faa46b091597b7952bc49799497872a2ef1ed09ff791bb8e8324e826182cb6`.
All prepared assets match the frozen manifest. Each sample fully renders,
encodes and muxes 540 video frames and 864,000 audio sample frames. Preparation
is excluded. No other Chroma test/render ran concurrently with measurements.

## Actual before/after result

One discarded full encode per build, then three alternating fresh-process full
encodes per build. `CHROMA_CPU_THREADS=4` selects the current parallel build;
the old implementation does not recognize the variable. Warmups took 16.16 s
parallel and 19.83 s previous.

| Build | Measured wall times | Median | Throughput | Median peak RSS |
|---|---|---:|---:|---:|
| Previous implementation | 20.20, 21.92, 20.60 s | 20.60 s | 26.22 fps | 790.0 MiB |
| Bounded parallelism | 16.31, 18.45, 18.33 s | 18.33 s | 29.46 fps | 788.7 MiB |

The roughly 1 MiB RSS difference is not evidence of a meaningful memory reduction.
Worker count and queued work are bounded; total process memory is not capped by
that bound.

Mean leaf-stage times:

| Stage | Previous | Parallel |
|---|---:|---:|
| Composition | 9.154 s | 7.382 s |
| Output color conversion | 2.900 s | 1.025 s |
| Decode | 3.050 s | 3.150 s |
| Decode conversion | 3.305 s | 3.451 s |
| RGB → YUV | 1.007 s | 1.034 s |
| Codec/mux | 0.753 s | 0.876 s |
| Audio | 0.340 s | 0.360 s |

These are means, while the wall result is a median. `execute`, `encode` and
`total` enclose the leaf stages and must not be added again. Unchanged stage
variation and scheduling affect the total. These local measurements are not
isolated hardware estimates or a general performance guarantee.

## Same-executable one/four-thread control

A separate batch used the identical current executable, changing only
`CHROMA_CPU_THREADS=1` versus `4`. It also discarded one full encode per setting
and alternated three fresh-process samples per setting. Warmups took 17.45 s
parallel and 21.30 s serial.

| Setting | Wall samples | Median | Throughput |
|---|---|---:|---:|
| One participant | 23.68, 22.19, 21.71 s | 22.19 s | 24.34 fps |
| Four participants | 16.49, 16.27, 16.96 s | 16.49 s | 32.75 fps |

This control gives a 1.346× ratio within the current executable. It is **not**
the speedup against the previous implementation. Serial dispatch/code-generation
costs and normal measurement variation matter: the current single-thread control
was slower than the saved previous build in these separate batches. That remains
a performance limitation for single-thread use and deserves follow-up. Inline
serial kernels and local pointer copies avoid some callback overhead but do not
establish that one-thread performance is unchanged.

The parallel medians also differ between batches (18.33 s versus 16.49 s), which
shows why a single fastest run or a ratio between separate batches would be
misleading. Use the direct before/after batch above for the published gain.

## Quality and concurrency checks

- All 74 media-enabled CTests passed, including the native/encoded quality gate,
  progress, cancellation, C/Python wrappers and concurrent C API tests.
- Targeted worker/serial/composition tests passed in the core-only build.
- Worker tests cover exact task coverage, actual partition count, nested calls,
  exception propagation/recovery, reuse, concurrent callers and bounded active
  tasks. A local ThreadSanitizer build passed without a reported race.
- The full showcase native verifier compared all 540 frames, **1,119,744,000 float
  RGBA pixels**, bit-for-bit between current parallel and serial execution.
- Separate delivery renders compare the saved previous build against current
  parallel execution: all decoded video frames and presentation timestamps match,
  as does intended audio PCM. Stream format, dimensions, counts, rational rate,
  duration, AAC sample rate/channels and audio duration are independently checked.
- The relocated SDK passed clean-runtime CLI/C API rendering, cancellation,
  decoded CLI/C equality and offline template checks.
- The implementation checkpoint's Linux/macOS build and quality CI jobs passed.

Verification runtimes are excluded from benchmark timing. Raw profiles, executable
and asset hashes, native counters and decoded-media evidence are preserved in
[the results](2026-10-22-cpu-parallel-results.json).
