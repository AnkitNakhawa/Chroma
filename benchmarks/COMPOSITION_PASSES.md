# Constant composition passes

Use the frozen showcase and assets from `examples/showcase/README.md` and a
Release build with FFmpeg/text. Keep output color conversion enabled identically
in both builds; only toggle the composition control:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DCHROMA_REFERENCE_OUTPUT_CONVERSION=OFF -DCHROMA_REFERENCE_COMPOSITION_PASSES=ON
cmake --build build-release --target chroma_cli --parallel
mkdir -p artifacts/composition-passes
cp build-release/chroma artifacts/composition-passes/chroma-materialized
cmake -S . -B build-release -DCHROMA_REFERENCE_COMPOSITION_PASSES=OFF
cmake --build build-release --target chroma_cli chroma_graphics_equivalence --parallel
cp build-release/chroma artifacts/composition-passes/chroma-optimized
python3 benchmarks/output_color.py --experiment composition-passes \
  --reference artifacts/composition-passes/chroma-materialized \
  --optimized artifacts/composition-passes/chroma-optimized \
  --composition artifacts/showcase/composition.json \
  --output artifacts/composition-passes/benchmark --runs 3
build-release/chroma_graphics_equivalence artifacts/showcase/composition.json \
  artifacts/composition-passes/native-equivalence.json --reference-mode composition
python3 benchmarks/output_color_verify.py --experiment composition-passes \
  --reference artifacts/composition-passes/chroma-materialized \
  --optimized artifacts/composition-passes/chroma-optimized \
  --composition artifacts/showcase/composition.json \
  --ffmpeg /absolute/path/to/ffmpeg --ffprobe /absolute/path/to/ffprobe \
  --output artifacts/composition-passes/media
```

Copied executables still require their build's runtime libraries. The shared
benchmark runner defaults to the earlier output-color experiment; the explicit
experiment argument selects composition labels. It checks the frozen composition
hash and asset manifest, discards one full encode per build, and alternates three
fresh-process full encodes per build. Do not run other renders/tests concurrently.

The native verifier uses independent contexts and compares every float RGBA byte.
Its `composition` reference mode keeps bounded graphics and media fusion enabled
while retaining the old composition passes. The media verifier checks all decoded
video frame hashes, presentation timestamps, intended audio PCM and stream metadata.
Neither verification runtime is benchmark evidence.

`chroma_composition_pass_tests` runs in normal CTest, including core-only builds.
It checks 300 deterministic randomized plans across all four blend modes,
multiple opacity factors, nontransparent clears, opaque/translucent solids,
constant prefixes, later uniform solids, spatial-effect fallbacks, plus a
clear-only signed-zero case. Comparisons use `memcmp`, not a tolerance.

## Scope

- Initialize the result directly from its first clear, avoiding a separate clear
  traversal after constructing an already-filled vector.
- Collapse a consecutive constant solid prefix into the initial fill. Every
  original blend is evaluated in order; opaque layers are not simply substituted.
- Evaluate an opacity-only solid effect chain on a single pixel, retaining the
  original effect ordering and intermediate float rounding. Later solids still
  blend once over the destination, but do not allocate/effect a canvas-sized source.
- Keep spatial effects and other unsupported chains on the materializing path.
  Text/shape region behavior, video/image fusion and final color conversion stay
  unchanged. Returned frames retain independent ownership; there is no cross-frame
  scratch pool or unbounded retained allocation.

New additive graphics profile counters record `initialization_pixels_avoided`,
`constant_composite_pixels_avoided`, and `solid_surface_pixels_avoided`. The last
counts slots of would-be full-canvas solid sources now represented by a one-pixel
source; it is not an allocation-byte or RSS measurement. These counters describe
different work categories and should not be summed as unique pixels or memory.
