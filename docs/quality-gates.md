# Continuous pixel and encoded-media quality checks

Every main-branch push and pull request runs dedicated **Quality (ubuntu-latest)**
and **Quality (macos-latest)** jobs. Both build Release with media/text enabled and
`CHROMA_REQUIRE_QUALITY_TESTS=ON`. Configuration fails if tests, Python, FFmpeg,
ffprobe, HarfBuzz/FreeType or a local test font are missing; CTest also fails if no
quality test is registered. The ordinary Debug/dependency-disabled jobs continue
running the existing focused renderer, media and API tests separately.

## Automatic offline corpus

| Case | Frames | Resolution/rate | Coverage |
|---|---:|---|---|
| Showcase proxy | 540 | 320×180, 30 fps | Complete 18-second showcase geometry, animation, scene transitions and original score |
| Fractional | 36 | 160×90, 30000/1001 fps | Moving video, image, shaped text, rounded/stroked card, blur fallback and stereo tone |
| Full HD | 12 | 1920×1080, 30 fps | Full-resolution sampling, text/shape regions, effects and encode |
| Text layout | 36 | 320×180, 30000/1001 fps | Word wrapping, alignment, line spacing, clipping/ellipsis, animated positions and stereo tone |

Footage and the small image are generated locally; fonts come from installed
system/dependency packages. CI never downloads stock footage, font assets or
external music during the test. This keeps the gate reproducible without
third-party asset availability. The original 540-frame **1080p stock-footage
showcase** remains the separately reproducible full benchmark/validation corpus;
CI does not claim to run that entire original media set on every push.

## Mandatory checks

- **Native pixels:** compare every byte of every float RGBA frame with the complete
  CPU reference, including media fusion differences: 58,579,200 pixels per job.
  Frame and pixel totals and active bounded/reference dispatch are checked.
- **Encoded equality:** decode every frame of both bounded and full-frame MP4s;
  frame checksums and decoded stereo PCM must match exactly within that build.
- **Format/timing:** H.264/yuv420p dimensions/rate/frame count, every presentation
  timestamp, AAC stereo/48 kHz sample count/start time, and video/audio durations must match.
  Fractional audio counts use exact integer frame/rate arithmetic.
- **Independent video fidelity:** compare decoded RGB against native reference
  snapshots at scene/transition/boundary frames. Every snapshot needs mean absolute
  RGB error **<3/255** and PSNR **≥30 dB**.
- **Independent audio fidelity:** compare decoded AAC against the generated source
  WAV with declared gain. Normalized full-scale RMSE must be **<0.015**, normalized
  waveform correlation **≥0.98**, and the output must be neither muted nor clipped.
- **Negative controls:** deliberately brighten/compress a real encode, mute its
  audio, truncate its video, shift its audio start time and substitute differing decoded video. Each relevant
  check must reject the mutation, or the overall gate fails.

Native comparisons are exact. The encoded fidelity limits acknowledge H.264/AAC
loss; they are fixed failure thresholds, not targets computed from the current
result. Matching the two encodes alone is insufficient: shared encoder degradation
can still fail the independent native/WAV checks. Cross-platform codec/font
versions may legitimately produce different hashes, so equality is evaluated
between modes within each job, not against a platform-specific hardcoded MP4 hash.
Reference-semantic changes and threshold changes need explicit review; the existing
analytic pixel/audio tests remain important because both paths share some code.

## Run and diagnose locally

```sh
cmake -S . -B build-quality -DCMAKE_BUILD_TYPE=Release \
  -DCHROMA_ENABLE_FFMPEG=ON -DCHROMA_ENABLE_TEXT=ON \
  -DCHROMA_REQUIRE_QUALITY_TESTS=ON
cmake --build build-quality --target chroma_cli chroma_graphics_equivalence --parallel 2
ctest --test-dir build-quality -L quality --output-on-failure --no-tests=error
```

`build-quality/quality/report.json` starts with `passed: false`, records progress
and metrics per case, and becomes true only after all checks and negative controls
pass. A failure preserves its traceback and the failing stage. Native comparison
reports, compositions, generated assets, MP4s and reference PPM snapshots remain
beside the report. Reports record tool/platform versions and the font fingerprint;
system font binaries are excluded from uploaded artifacts. CTest logs are in `build-quality/Testing/Temporary/`.

Both CI jobs upload these diagnostics as `quality-<runner OS>` artifacts on success
or failure, retained for seven days. Quality is a correctness gate; it intentionally
has no wall-time/fps assertion, so runner load cannot masquerade as a quality
regression. For the original full showcase check and controlled performance runs,
see [bounded graphics verification](../benchmarks/BOUNDED_GRAPHICS.md).

Focused text tests also compare laid-out glyphs with independently prepared single
lines, verify explicit/empty lines, combining clusters, long words, alignment shifts,
clipping and ellipsis, reject malformed layout options, and test cache isolation
for every field. Bounded-region tests compare layouts across transforms, blend
modes, off-canvas positions and effect fallbacks with exact native pixels.
Release smoke fixtures include layout text and compare decoded CLI/C/Python output.
