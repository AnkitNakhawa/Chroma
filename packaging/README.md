# Portable Chroma releases

Release archives contain the CLI (`bin/chroma`), shared C ABI v1 library and header,
relocatable CMake config (`find_package(Chroma 0.1 CONFIG REQUIRED)` / `chroma::c`),
a prebuilt plain-C render example, and private FFmpeg/ffprobe tools.
Extract the entire archive together; no Homebrew or FFmpeg installation is needed.
Fonts and input media remain application assets: reference their paths in your composition.

Targets: Linux x86_64 (glibc 2.35+, Ubuntu 22.04/24.04), macOS 14+ arm64 and x86_64.
Linux retains the OS C/C++ runtimes (glibc, libstdc++6, libgcc_s); these are part of
the tested base Ubuntu images. macOS retains only Apple system libraries.
The macOS archives are ad-hoc signed, not Developer ID signed or notarized.
Downloaded archives may be subject to macOS Gatekeeper; these are developer SDK
releases, not a signed macOS application installer.

For download, binary-only checksum verification, and a first video, follow the
[quickstart](../docs/quickstart.md). The checksum manifest lists both binary and
corresponding-source archives; checking the entire manifest requires both files.

C consumers need a C99 compiler only, not C++ or FFmpeg headers:

```sh
cc share/chroma/examples/c-api-render.c -Iinclude -Llib -lchroma \
  -Wl,-rpath,"$PWD/lib" -o c-api-render
./c-api-render share/chroma/examples/solid.json c-api.mp4
```

## Dependency policy and corresponding source

Chroma is Apache-2.0. Bundled FFmpeg is built under LGPL-2.1-or-later, with GPL and
nonfree components disabled, using the BSD-licensed OpenH264 encoder. It does not
include libx264. Release H.264 output requires even dimensions of at least 16×16;
native frame rendering still supports smaller canvases. Other bundled libraries: HarfBuzz (MIT), FreeType (FreeType License),
zlib (zlib license), and header-only nlohmann/json (MIT). FreeType is used under its
FreeType License, including the required acknowledgement: portions of this software
are copyright © The FreeType Project (www.freetype.org). All rights reserved.
See `share/chroma/licenses` for upstream license texts and notices, and
`share/chroma/release.json` for exact versions, source URLs, hashes and build revision.
OpenH264 is built from source; Cisco's binary-download patent license does not apply
to this build. Codec patent permissions, where required, are separate from copyright licenses.

Each binary archive is accompanied by a `-sources.tar.gz` archive containing all
exact upstream dependency sources, Chroma source at the recorded revision and
packaging recipes. Keep these companions available when redistributing. FFmpeg is
linked dynamically; recipients can rebuild and replace these shared libraries with
ABI-compatible modified versions. No DRM or signature enforcement blocks replacement
(ad-hoc sign modified Mach-O files as needed on macOS). No GPL libraries are bundled.

## Building

Prerequisites: Python 3.12+, CMake 3.25+, pkg-config, make, C/C++20 compilers,
Linux `patchelf`/binutils, or macOS command-line developer tools. No media development
packages are needed. Linux release builds run in Ubuntu 22.04 to set the glibc floor.

```sh
python3 packaging/build-dependencies.py --work artifacts/release --jobs 2
export PKG_CONFIG_LIBDIR="$PWD/artifacts/release/prefix/lib/pkgconfig"
export PKG_CONFIG_PATH=
cmake -S . -B build-package -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/artifacts/release/prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCHROMA_RELOCATABLE_INSTALL=ON \
  -DCHROMA_REQUIRE_QUALITY_TESTS=ON
cmake --build build-package --parallel 2
ctest --test-dir build-package --output-on-failure
python3 -m pip wheel --no-deps ./python --wheel-dir artifacts/python-wheel
python3 packaging/bundle.py --build build-package --work artifacts/release --python-wheel artifacts/python-wheel/*.whl
```

Put the controlled prefix's `bin` directory first on PATH before configuring to run
quality checks with the exact bundled FFmpeg. Provide a system test font (e.g.
fonts-dejavu-core on Ubuntu). On macOS set `SDKROOT` and
`MACOSX_DEPLOYMENT_TARGET=14.0` for both dependency and Chroma builds.
Use a fresh work directory after changing the lock or build flags; recipe fingerprints
reject stale prefixes and allow resuming an interrupted identical build. Downloads are SHA-256 verified before use.
To rebuild offline from the source companion, extract `sources/chroma.tar`, copy the
upstream archives to `artifacts/release/sources/`, then run the same commands.

CI builds all three targets, checks native/encoded quality, audits dependency paths,
relocates archives to paths containing spaces and exercises CLI and C rendering with
an empty loader environment. Linux smoke tests run in unmodified Ubuntu containers;
macOS smoke tests run on a fresh runner without the build prefix. Consumer compilation
is tested separately from runtime execution. Tag `v0.1.0` (matching CMake's version)
to publish after every build and smoke job passes; main pushes create CI artifacts only.

## Python wrapper

Archives include `share/chroma/python/chroma_render/` and a pure Python wheel.
Install the wheel offline with `python3 -m pip install --no-index --no-deps /path/to/chroma/share/chroma/python/*.whl`,
then set `CHROMA_LIBRARY_PATH` to the SDK's `lib/libchroma.so` (Linux) or
`lib/libchroma.dylib` (macOS). Alternatively set `PYTHONPATH` to the SDK's
`share/chroma/python` directory for automatic SDK-relative library discovery.
See `share/chroma/docs/python.md`. Python 3.10+ is required.
Release CI installs the wheel into a fresh venv with no network or Python
dependencies and compares decoded Python/CLI video and audio output.

Release smoke checks run explicit input preflight, exercise early missing-input
failures without output replacement, and call the new C preflight entry point
through the installed Python wheel. See `share/chroma/docs/preflight.md`.
