# Install Chroma and render a video

This guide uses the v0.1.0 developer release. The CLI includes its native media
libraries; you need only the matching operating system, a shell, curl, and tar.
The optional Python section requires Python 3.10+ with pip and venv support.

## 1. Download and verify

Run these commands in a new directory. The platform selector supports Linux
x86_64 (glibc 2.35+, tested on Ubuntu 22.04/24.04) and macOS 14+ on Apple Silicon
or Intel. Linux ARM and Windows are not packaged.

```sh
set -eu
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) platform=linux-x86_64 ;;
  Darwin-arm64) platform=darwin-arm64 ;;
  Darwin-x86_64) platform=darwin-x86_64 ;;
  *) echo 'No Chroma v0.1.0 package for this platform' >&2; exit 1 ;;
esac
archive="chroma-0.1.0-$platform.tar.gz"
manifest="chroma-0.1.0-$platform.sha256"
base=https://github.com/AnkitNakhawa/Chroma/releases/download/v0.1.0
curl --fail --location --output "$archive" "$base/$archive"
curl --fail --location --output "$manifest" "$base/$manifest"
# The manifest also lists the optional corresponding-source archive.
# Verify the binary archive that we downloaded, without requiring that source archive.
awk -v name="$archive" '$2 == name { print; found++ } END { if (found != 1) exit 1 }' "$manifest" > binary.sha256
case "$platform" in
  linux-*) sha256sum -c binary.sha256 ;;
  darwin-*) shasum -a 256 -c binary.sha256 ;;
esac
tar -xzf "$archive"
export CHROMA_HOME="$PWD/chroma-0.1.0-$platform"
"$CHROMA_HOME/bin/chroma" --version
```

Keep the whole extracted directory together, including `lib` and `libexec`.
You can move it, then update `CHROMA_HOME` to its new absolute path. No global
installation, sudo, Homebrew, or separate FFmpeg installation is needed.
macOS packages are ad-hoc signed developer SDK archives, without Developer ID
signing or notarization; Gatekeeper may prevent a downloaded package from running.

## 2. Render the included animation

Copy the example so you can edit it without changing the installed SDK:

```sh
cp "$CHROMA_HOME/share/chroma/examples/quickstart.json" scene.json
"$CHROMA_HOME/bin/chroma" preflight scene.json
"$CHROMA_HOME/bin/chroma" render scene.json --output first-video.mp4 --progress text
```

Open `first-video.mp4` in your video player. It is a three-second, 1280×720,
30 fps H.264 animation of three colored cards. It needs no fonts or input media
and intentionally has no audio. The package also includes the smaller
`animated-transforms.json` example for a faster first render.

Edit the canvas, colors, shape bounds, and effect keyframes in `scene.json`.
The [composition format](composition-format.md) describes the schema. Times use
integer frames, and layer end frames are exclusive. For text, images, footage,
and music, supply your own assets and reference paths relative to the JSON file.
See [text layout](composition-format.md#text-layout) and
[input preflight](preflight.md) before adding media. MP4 dimensions must be even
and at least 16×16 in these packages.

Progress is displayed on stderr. For automation, use `--progress json` for
newline-delimited events on stdout. Ctrl-C cancels a render; a cancelled or
failed render does not replace an existing output file. See
[render controls](render-control.md) for exit codes and API cancellation.

## 3. Optional: render with Python

Use Python matching the archive architecture. The wheel is inside the native
archive and is installed offline; it does not download the native renderer.
`chroma-render` is not currently published on PyPI.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --no-index --no-deps "$CHROMA_HOME"/share/chroma/python/*.whl
case "$(uname -s)" in
  Darwin) export CHROMA_LIBRARY_PATH="$CHROMA_HOME/lib/libchroma.dylib" ;;
  Linux) export CHROMA_LIBRARY_PATH="$CHROMA_HOME/lib/libchroma.so" ;;
esac
python - <<'PY'
from chroma_render import Engine

with Engine() as engine:
    with engine.load_composition("scene.json") as scene:
        report = scene.preflight(for_video=True)
        if not report["ready"]:
            raise RuntimeError(report)
        engine.render(scene, "python-video.mp4", progress=lambda p: print(
            p.phase.name, p.frames_encoded, "/", p.total_frames))
PY
```

For cooperative cancellation from another thread, pass a `CancellationToken` to
`engine.render` and call `token.cancel()`. A callback can also return `True` to
cancel. See the [Python API guide](python.md) for complete examples and ownership
rules. Native C consumers can use the header, library, CMake config, and example
in the archive; see the [C API guide](c-api.md).

## Troubleshooting

- **Unsupported architecture or loader error:** use the archive matching your OS
  and CPU, keep its directory together, and check the minimum OS above. Python
  and the native library must have matching architectures.
- **Missing media or font:** run `preflight scene.json --json` and check each
  reported asset path. `validate` checks structure only; it does not open assets.
- **Python cannot find Chroma:** set `CHROMA_LIBRARY_PATH` to the absolute library
  path shown above. Installing the wheel alone does not install native codecs.

[Release notes](releases/v0.1.0.md) describe the supported scope.
[The packaging guide](../packaging/README.md) covers source archives and licenses.


For reusable starter layouts, see [project scaffolding](projects.md). The
`chroma init` command is newer than v0.1.0 and requires development main or a
newer package; the installation instructions above target v0.1.0.
