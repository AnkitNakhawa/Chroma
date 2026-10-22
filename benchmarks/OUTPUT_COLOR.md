# Output color conversion controls

Build Release with FFmpeg/text enabled and retain the frozen showcase and assets
from `examples/showcase/README.md`. Use one build directory and compiler/codec
configuration; only toggle the scalar conversion control:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCHROMA_REFERENCE_OUTPUT_CONVERSION=ON
cmake --build build-release --target chroma_cli --parallel
mkdir -p artifacts/output-color
cp build-release/chroma artifacts/output-color/chroma-scalar
cmake -S . -B build-release -DCHROMA_REFERENCE_OUTPUT_CONVERSION=OFF
cmake --build build-release --target chroma_cli --parallel
cp build-release/chroma artifacts/output-color/chroma-lookup
python3 benchmarks/output_color.py \
  --reference artifacts/output-color/chroma-scalar \
  --optimized artifacts/output-color/chroma-lookup \
  --composition artifacts/showcase/composition.json \
  --output artifacts/output-color/benchmark --runs 3
python3 benchmarks/output_color_verify.py \
  --reference artifacts/output-color/chroma-scalar \
  --optimized artifacts/output-color/chroma-lookup \
  --composition artifacts/showcase/composition.json \
  --ffmpeg /absolute/path/to/ffmpeg --ffprobe /absolute/path/to/ffprobe \
  --output artifacts/output-color/media
```

The copied executables still require their build's runtime libraries. This is a
local controlled experiment, not a portable SDK packaging recipe.

The runner requires the unchanged showcase SHA and prepared asset manifest,
discards one complete encode per mode, then alternates three fresh-process
full encodes per mode. Bounded graphics, caches, codec settings and audio remain
identical. Do not run other render/test workloads concurrently. Wall/RSS and
stage times are recorded; nested timing fields must not be added together.

Verification renders separate delivery files, checks stream format, frame count,
rational rate, duration and timestamps, then requires identical decoded video
frame hashes and audio PCM. MP4 container bytes need not match.

`chroma_output_color_tests` runs in normal CTest/CI, including core-only builds.
It exhausts 117,440,513 float32 values from 2^-14 through 1 against the original
scalar formula. Below 2^-14 every nonnegative value rounds to zero; negatives
clamp to zero and values above 1 clamp to 255. Additional cases cover clamping,
subnormals, infinities and bulk RGB layout/alpha independence. The static table
assumes monotonic quantization between bucket endpoints, and this exhaustive
check guards that assumption for each tested compiler/libm platform. It does not
rely on a sampled approximate sRGB curve or alter rounding tolerances.

The table is immutable, initialized once with C++ thread-safe static initialization,
and occupies 130,050 bytes. Bucket endpoints quantizing to different bytes use the
original scalar computation. Conversion receives straight RGB values from the
existing surface contract, ignores alpha exactly as before, and retains scalar
handling of NaNs. The default floating-point environment and ordinary non-fast-math
build settings remain the supported contract. Native rendering, PPM conversion,
FFmpeg RGB/YUV settings and codec settings are unchanged.
