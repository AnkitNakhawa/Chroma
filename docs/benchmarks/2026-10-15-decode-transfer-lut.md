# Decode transfer lookup benchmark — 2026-10-15

This checkpoint replaces repeated sRGB transfer-function evaluation for decoded RGBA8 samples
with a precomputed 256-entry table. Because each input channel is already one byte, the table
contains every possible input and produces pixel-exact output relative to the retained reference
converter.

## Environment and method

- Host: Apple M3 Pro, 18 GiB RAM, macOS 15.6 / Darwin 24.6.0
- Build: CMake `Release`, FFmpeg and HarfBuzz/FreeType enabled
- Workload: 640×360, 120-frame video-plus-solid benchmark
- Baseline revision: `4a06d33`
- Samples: 16 renders per variant after discarded warm-ups

| Variant | Throughput | Mean total | Decode conversion | Decode | Composition |
|---|---:|---:|---:|---:|---:|
| 256-entry lookup | 312.7 fps | 383.74 ms | 76.51 ms | 30.64 ms | 38.03 ms |
| Per-channel reference | 262.0 fps | 458.08 ms | 158.70 ms | 30.42 ms | 37.54 ms |

The lookup improved end-to-end throughput by 19.4%, reduced total time by 16.2%, and reduced
decode conversion time by 51.8%. Unit coverage decodes the same frame through both modes and
requires exact equality for every linear-premultiplied output pixel.

This optimization does not remove a representation or reduce the reported byte traffic: FFmpeg
still writes RGBA8 and Chroma still materializes linear RGBA floats. It only makes the required
8-bit transfer conversion cheaper. The retained reference mode remains a correctness oracle.

The next major CPU opportunity is the encode side, where linear-to-sRGB byte conversion remains
the dominant measured stage. Any approximation there needs an explicit pixel/video tolerance;
unlike decode's 8-bit input, arbitrary linear floats do not have only 256 possible values.
