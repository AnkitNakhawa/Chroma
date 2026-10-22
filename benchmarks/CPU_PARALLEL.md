# CPU parallelism benchmark

Reuse the frozen composition and prepared assets from `examples/showcase/README.md`.
Use a Release build with FFmpeg/text, output lookup and optimized composition
passes enabled. The earlier scalar/materializing build flags must remain OFF.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DCHROMA_REFERENCE_OUTPUT_CONVERSION=OFF -DCHROMA_REFERENCE_COMPOSITION_PASSES=OFF
cmake --build build-release --target chroma_cli chroma_graphics_equivalence --parallel
python3 benchmarks/output_color.py --experiment cpu-parallel \
  --reference build-release/chroma --optimized build-release/chroma \
  --composition artifacts/showcase/composition.json \
  --output artifacts/cpu-parallel/controlled --runs 3
CHROMA_CPU_THREADS=4 build-release/chroma_graphics_equivalence \
  artifacts/showcase/composition.json artifacts/cpu-parallel/native.json --reference-mode serial
python3 benchmarks/output_color_verify.py --experiment cpu-parallel \
  --reference build-release/chroma --optimized build-release/chroma \
  --composition artifacts/showcase/composition.json \
  --ffmpeg /absolute/path/to/ffmpeg --ffprobe /absolute/path/to/ffprobe \
  --output artifacts/cpu-parallel/media
```

The runner sets `CHROMA_CPU_THREADS=1` or `4` in each child process. Hardware may
cap four lower; inspect `chroma hardware` on the benchmark host. It validates the
frozen source/asset hashes, discards a full encode per mode, then alternates at
least three complete fresh-process encodes per mode. All 540 frames and 864,000
audio sample frames must be encoded. Profiles, executable hashes, wall time and
process peak RSS are retained. Do not run another Chroma render/test concurrently.

For an actual pre-change versus post-change comparison, retain the previous
Release executable and matching runtime libraries. Use `--experiment
cpu-before-after`, passing that saved executable as `--reference` and the current
one as `--optimized`. The previous implementation ignores the new environment
variable. Record both executable hashes and previous/new source revisions.
Do not present a same-executable one/four-thread ratio as a comparison against
the previous implementation. Inline dispatch and pointer capture can change
compiler optimization even with one thread.

The native verifier's `serial` mode uses the same optimized renderer on the
calling thread and compares all float RGBA bytes with an independently decoded
parallel context. The media verifier checks complete decoded video frame hashes,
presentation timestamps, intended audio PCM, stream formats/counts/rates and
durations. It accepts `cpu-before-after` too. Verification time is excluded from
benchmark measurements. MP4 container bytes need not be identical.
