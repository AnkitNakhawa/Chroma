# Motion studies: an 18-second 1080p showcase

Four editorial scenes combine ocean footage, native shaped text, animated cards,
source-over transitions, and an original 100 BPM electronic cue. Output is 540
frames at 1920×1080, 30 fps, H.264 video with 48 kHz stereo AAC audio.

Build Chroma in **Release** mode with FFmpeg, HarfBuzz and FreeType enabled. Prepare
assets and emit the composition (Python standard library; FFmpeg with libx264):

```sh
python3 examples/showcase/generate.py --ffmpeg ffmpeg
./build-release/chroma validate artifacts/showcase/composition.json
./build-release/chroma render artifacts/showcase/composition.json \
  --output artifacts/showcase/showcase.mp4 \
  --cache-report artifacts/showcase/render-profile.json
```

The first preparation downloads source footage and fonts; subsequent preparations
reuse checksum-verified downloads. Generated media stays outside Git under
`artifacts/showcase/`. `assets.json` records source URLs, checksums and score peak.
FFmpeg normalizes footage to native-size 1080p/30 before Chroma renders it. Chroma
performs every layout, text rasterization, animation, transition and final mux;
the footage normalization and original-score synthesis are asset preparation.

## Credits and licensing

- Ocean footage: **Nino Souza / Pexels**,
  https://www.pexels.com/video/drone-shot-of-ocean-waves-at-the-seashore-4193133/
  Used under the [Pexels License](https://www.pexels.com/license/). It is downloaded
  from Pexels, not bundled or relicensed under this repository's Apache license.
- Lato Regular and Bold: Łukasz Dziedzic and contributors, **SIL Open Font License
  1.1**. Preparation downloads the fonts and their `OFL.txt` from Google Fonts;
  the license stays beside the fonts. Source hashes reject unexpected changes.
- Music: original deterministic synthesis in `generate.py`; no sampled recordings.
  Composition/generation code and the original score use this repository's
  Apache-2.0 license. The footage is illustrative, without implied endorsement.

## Validation and benchmark

CI validates the complete composition through the actual CLI, exact timeline and
original-score WAV properties without network access or stock assets. To measure
full renders, first render once as a discarded warm-up, close other workloads,
and run:

```sh
python3 examples/showcase/benchmark.py --chroma build-release/chroma \
  --composition artifacts/showcase/composition.json \
  --output artifacts/showcase/benchmark --runs 3
```

The runner serializes alternating cache-enabled/disabled pairs, starts a fresh
process for each render, and preserves per-stage profiles, wall time, peak RSS,
logs and the source composition hash. The benchmark CLI removes temporary MP4s
after measuring their complete render/encode/mux lifecycle. It measures current
prepared-resource caching, not the cumulative effect of all historical changes.
Raw JSON is written after every completed sample so interrupted runs are usable.
Stock downloads/preparation are excluded. Neither CUDA acceleration nor a
performance advantage over FFmpeg/Remotion is implied.

Verify the delivery independently with FFmpeg/ffprobe and the native renderer:

```sh
python3 examples/showcase/verify.py --chroma build-release/chroma \
  --composition artifacts/showcase/composition.json \
  --video artifacts/showcase/showcase.mp4 \
  --output artifacts/showcase/verification.json
```

The verifier decodes all 540 video frames, checks every presentation timestamp,
checks exact 18-second stream durations and decoded stereo sample count, measures
audio level and the fade tail, compares four cache-enabled/disabled native scene
frames byte-for-byte, and compares those frames with the lossy encoded output
(mean absolute RGB error under 3/255). It records hashes and measured results.
