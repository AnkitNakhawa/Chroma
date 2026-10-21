# Chroma

Chroma is an open-source, headless video composition engine for programmatic motion graphics. Describe a timeline in JSON and render it through the CLI, C API, or Python wrapper.

**v0.1.0 is the first developer release.** It renders on the CPU and packages the native renderer and media libraries together. You do not need to install FFmpeg, Homebrew, or a C++ compiler to use the packaged CLI.

## Install and render your first video

Download the archive for your machine from [Chroma v0.1.0](https://github.com/AnkitNakhawa/Chroma/releases/tag/v0.1.0), extract the entire directory, then run:

```sh
# From inside the extracted chroma-0.1.0-<platform> directory:
./bin/chroma preflight share/chroma/examples/quickstart.json
./bin/chroma render share/chroma/examples/quickstart.json --output first-video.mp4 --progress text
```

This renders a three-second, 1280×720 animated card composition. It requires no external assets. See the **[quickstart](docs/quickstart.md)** for download commands, checksum verification, and Python installation.

| Platform | Archive | Minimum OS |
| --- | --- | --- |
| Linux x86_64 | [linux-x86_64](https://github.com/AnkitNakhawa/Chroma/releases/download/v0.1.0/chroma-0.1.0-linux-x86_64.tar.gz) | glibc 2.35, tested on Ubuntu 22.04/24.04 |
| macOS Apple Silicon | [darwin-arm64](https://github.com/AnkitNakhawa/Chroma/releases/download/v0.1.0/chroma-0.1.0-darwin-arm64.tar.gz) | macOS 14 |
| macOS Intel | [darwin-x86_64](https://github.com/AnkitNakhawa/Chroma/releases/download/v0.1.0/chroma-0.1.0-darwin-x86_64.tar.gz) | macOS 14 |

Keep the archive's `bin`, `lib`, `libexec`, and `share` directories together. macOS packages are ad-hoc signed developer SDKs, without Developer ID signing or notarization. Windows and Linux ARM packages are not included in this release.

## Create a project (development main)

The current `main` branch adds three offline project templates. This command is
newer than the published v0.1.0 archives; build main or use a newer package.

```sh
chroma init my-video --template product-promo --title "Meet your next favorite."
chroma preflight my-video/scene.json
chroma render my-video/scene.json --output my-video/video.mp4 --progress text
```

Choose `title-sequence`, `product-promo`, or `captioned-footage`. Each creates an
editable six-second 720p composition with a licensed font and original demo
music; captioned footage includes an original sample clip. Supply your own text,
colors, image, footage, or music through creation options. Existing destinations
are refused. See [project scaffolding](docs/projects.md).

## What it does

- Compose shapes, text, images, and video on an exact frame timeline; animate translation, scale, rotation, and opacity with easing.
- Wrap and align text, control line spacing, and clip or ellipsize overflow. Supply your own font files.
- Apply masks, cropping, blur, color matrices, blends, and transitions.
- Trim, adjust gain, and mix audio; encode H.264 video and AAC audio into MP4.
- Check assets, fonts, media compatibility, and timeline issues before rendering.
- Report progress and cancel renders through the CLI, C API, and Python. Failed or cancelled renders preserve existing destination files.
- Bound image and text cache memory with budgets, eviction, and usage counters.

The C library exposes ABI v1. The Python `chroma-render` wheel is included in each native archive and imports as `chroma_render`; it requires Python 3.10+ and the matching native library. It is not currently published on PyPI.

## Agent-readable render results (development main)

```sh
chroma render scene.json --output video.mp4 --report result.json --progress json
```

The report records publication status, output metadata, native stage timings,
process peak RSS, cache counters, and actionable diagnostics. Python offers
`engine.render_report(...)`; C ABI v1 adds `chroma_engine_render_video_report`.
See the [report contract](docs/render-reports.md), including failure/cancellation
and memory accounting. This feature is newer than the v0.1.0 release.

## Performance and quality

Chroma reduces intermediate work by rendering supported text and shapes inside their affected regions and by caching prepared assets. In the recorded 18-second 1080p showcase benchmark, bounded rendering took **26.32 seconds versus 88.88 seconds** for Chroma's full-frame reference path: **3.38× faster on that workload**. This is a comparison between two Chroma paths, not a comparison with other engines. Results depend on content and hardware.

CI checks pixel equivalence against the reference renderer and decoded video/audio quality. See [the quality gate](docs/quality-gates.md) and [benchmark records](benchmarks/) for methodology and results.

This release uses CPU rendering and 8-bit SDR H.264 output. GPU execution, production HDR color management, persistent cross-job caching, parallel render scheduling, and Node.js bindings remain future work. The bundled OpenH264 encoder requires even output dimensions of at least 16×16. Input preflight checks headers and initial decoded data; it is not a full media integrity scan.

## Documentation

- [Quickstart: install and render](docs/quickstart.md)
- [Project scaffolding and templates (development main)](docs/projects.md)
- [Composition format and text layout](docs/composition-format.md)
- [Machine-readable composition schema (development main)](docs/composition-schema.md)
- [Input preflight](docs/preflight.md)
- [Progress and cancellation](docs/render-control.md)
- [Structured render reports (development main)](docs/render-reports.md)
- [Python API](docs/python.md)
- [C API and ownership](docs/c-api.md)
- [Portable packaging, dependencies, and source archives](packaging/README.md)
- [Release notes](docs/releases/v0.1.0.md)

## Build from source

Use CMake 3.25+, a C++20 compiler, and pkg-config. Full media and text support require FFmpeg, FreeType, and HarfBuzz development libraries. CMake fetches nlohmann/json 3.11.3 if it is not installed. Check the configuration output: missing optional libraries disable their features.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/chroma render examples/quickstart.json --output first-video.mp4
```

The [packaging guide](packaging/README.md) describes the pinned, relocatable release build and clean-runtime tests. [Examples](examples/) include compositions and C/Python consumers; examples that reference media or fonts require those assets.

Chroma is licensed under [Apache-2.0](LICENSE). Release archives include notices and corresponding sources for bundled dependencies; see the [dependency policy](packaging/README.md#dependency-policy-and-corresponding-source).
