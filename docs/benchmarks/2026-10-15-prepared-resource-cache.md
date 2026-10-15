# Prepared-resource cache benchmark — 2026-10-15

This checkpoint measures whether Chroma's job-scoped image and shaped-text reuse improves
render throughput. It does not measure GPU execution or runtime graph fusion; those are not
implemented.

## Environment

- Revision: `788cfa3`
- Comparison revision for video-only regression: `906a2e9`
- Host: Apple M3 Pro, 18 GiB RAM, macOS 15.6 / Darwin 24.6.0
- Build: CMake `Release`, FFmpeg enabled, HarfBuzz/FreeType enabled
- Each benchmark had a discarded warm-up before its measured runs.

## Prepared image/text reuse

The 160×90, 60-frame basic-graphics fixture contains video, a static image, a rounded shape,
and shaped text. The control build used the exact same revision and build settings but bypassed
the two job-scoped prepared-resource maps, forcing image decode and text shaping/rasterization
on every frame. Two 20-iteration batches were run for each variant in alternating order.

| Variant | Samples | Throughput | Mean total | Mean execution |
|---|---:|---:|---:|---:|
| Prepared-resource reuse | 40 | 1,197.4 fps | 50.11 ms | 37.03 ms |
| Reprepare every frame | 40 | 993.9 fps | 60.37 ms | 47.89 ms |

For this small static-resource-heavy fixture, reuse improved end-to-end throughput by 20.5%,
reduced mean total time by 17.0%, and reduced execution time by 22.7%. Frame 30 from both
variants had the same SHA-256 digest, `1333db54db2fdf47fb5e6345c3e86e8e08b9c3b56b68ac87b580e7c5acfee01c`.

This gain comes from avoiding repeated resource preparation. It is workload-specific and will
shrink when decode, large-frame compositing, or encoding dominates. The cache is currently
job-scoped and unbounded; a byte budget and eviction policy remain necessary.

## Video-only regression control

The unchanged 640×360, 120-frame video-plus-solid corpus was measured at the pre-graphics
revision and the current revision in alternating 20-iteration batches.

| Revision | Samples | Throughput | Mean total | Mean execution |
|---|---:|---:|---:|---:|
| `906a2e9` | 40 | 249.3 fps | 481.32 ms | 243.40 ms |
| `788cfa3` | 40 | 246.5 fps | 486.85 ms | 244.69 ms |

The current result is 1.1% slower, which is small enough to treat as effectively flat for this
local, non-isolated benchmark. There is no evidence of a general renderer speedup from the new
planner diagnostics or graphics support.

An approximate direct FFmpeg filter/encode command processed the same video-only corpus at
about 1,081 fps across 30 sequential renders. Chroma therefore remains roughly 4.4× slower on
this CPU path. The FFmpeg comparison is directional rather than pixel-equivalent because its
blend/color semantics are not identical to Chroma's linear-premultiplied pipeline.

## Conclusion

Prepared-resource reuse is a measured win for repeated static images and text. Runtime graph
fusion has not landed, and the broader video pipeline has not sped up. Future performance claims
should separately report resource preparation, execution, encoding, cold/warm behavior, and
equivalence tolerances.
