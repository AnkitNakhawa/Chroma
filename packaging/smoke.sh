#!/bin/sh
# Run by /bin/sh in a base OS image: no Python, compiler, CMake or FFmpeg install.
set -eu
package=$(cd "$1" && pwd)
fixture=$(cd "$2" && pwd)
mkdir -p "$3"
result=$(cd "$3" && pwd)
clean() {
  env -i PATH=/usr/bin:/bin HOME="$result" LC_ALL=C "$@"
}
clean "$package/bin/chroma" validate "$fixture/smoke.json"
clean "$package/bin/chroma" render "$fixture/smoke.json" --output "$result/cli.mp4" --progress json > "$result/progress.ndjson"
grep -q '"phase":"completed","frames_encoded":36' "$result/progress.ndjson"
clean "$package/libexec/chroma/chroma_c_example" "$fixture/smoke.json" "$result/c-api.mp4" --progress
# Exercise cancellation against the actual bundled C library and its new symbols.
printf 'original\n' > "$result/cancelled.mp4"
status=0
clean "$package/libexec/chroma/chroma_c_example" "$fixture/smoke.json" "$result/cancelled.mp4" --cancel-after 2 || status=$?
test "$status" -eq 130
grep -qx original "$result/cancelled.mp4"
for leftover in "$result"/.chroma-render-*; do
  test ! -e "$leftover"
done
# A separately compiled CMake find_package() consumer, relocated with the bundle.
clean "$package/bin/chroma-sdk-consumer"
clean "$package/bin/chroma" frame "$fixture/tiny.json" --frame 0 --output "$result/tiny.ppm"
if clean "$package/bin/chroma" render "$fixture/tiny.json" --output "$result/tiny.mp4" 2> "$result/tiny-error.txt"; then
  echo 'Expected OpenH264 to reject sub-16-pixel output' >&2
  exit 1
fi
grep -q 'at least 16 pixels' "$result/tiny-error.txt"
test ! -e "$result/tiny.mp4"
printf 'keep-existing-output\n' > "$result/tiny.mp4"
if clean "$package/bin/chroma" render "$fixture/tiny.json" --output "$result/tiny.mp4" 2> "$result/tiny-error.txt"; then
  exit 1
fi
grep -qx 'keep-existing-output' "$result/tiny.mp4"
probe="$package/libexec/chroma/ffprobe"
ffmpeg="$package/libexec/chroma/ffmpeg"
for mode in cli c-api; do
  clean "$probe" -v error -select_streams v:0 -count_frames \
    -show_entries stream=codec_name,width,height,r_frame_rate,nb_read_frames \
    -of default=noprint_wrappers=1 "$result/$mode.mp4" > "$result/$mode.video.txt"
  for expected in codec_name=h264 width=160 height=90 r_frame_rate=30000/1001 nb_read_frames=36; do
    grep -qx "$expected" "$result/$mode.video.txt"
  done
  clean "$probe" -v error -select_streams a:0 \
    -show_entries stream=codec_name,sample_rate,channels -of default=noprint_wrappers=1 \
    "$result/$mode.mp4" > "$result/$mode.audio.txt"
  for expected in codec_name=aac sample_rate=48000 channels=2; do
    grep -qx "$expected" "$result/$mode.audio.txt"
  done
  clean "$ffmpeg" -v error -y -i "$result/$mode.mp4" -map 0:v:0 -f rawvideo -pix_fmt rgba "$result/$mode.rgba"
  clean "$ffmpeg" -v error -y -i "$result/$mode.mp4" -map 0:a:0 -f s16le "$result/$mode.pcm"
done
cmp "$result/cli.rgba" "$result/c-api.rgba"
cmp "$result/cli.pcm" "$result/c-api.pcm"
test -s "$result/cli.rgba"
test -s "$result/cli.pcm"
printf '%s\n' 'PASS: relocated CLI, C SDK, H.264/AAC, text/image/footage/transforms/audio, identical decoded CLI/C output'
