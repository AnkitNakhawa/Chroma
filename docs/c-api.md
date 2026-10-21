# Stable C API, ABI version 1

The supported binary boundary is `<chroma/chroma.h>` and the `chroma::c` CMake
target. It requires C99 or later, and is also valid C++. C++ containers, exceptions,
FFmpeg/FreeType handles, and internal render structures never cross this boundary.
Linux and macOS 64-bit builds are tested. Windows export annotations are present,
but Windows support is not validated by this release.

## Build and consume

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix /your/prefix --component CAPI
```

A consumer can enable only the C language:

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_renderer LANGUAGES C)
find_package(Chroma CONFIG REQUIRED)
add_executable(my_renderer main.c)
target_link_libraries(my_renderer PRIVATE chroma::c)
```

Configure with `-DCMAKE_PREFIX_PATH=/your/prefix`. The package installs the C
header, shared library, CMake config, and `share/chroma/examples/c-api-render.c`.
The example loads a composition file, requests a CPU frame, and encodes a video:

```sh
./build/chroma_c_example composition.json output.mp4
./build/chroma_c_example composition.json output.mp4 --no-cache
./build/chroma_c_example composition.json output.mp4 --progress
./build/chroma_c_example composition.json output.mp4 --cancel-after 30
```

The C++ implementation remains a private static core linked into the shared C
library; its headers/ABI are not the installed stable interface. `BUILD_SHARED_LIBS`
does not change these explicit target types. Runtime FFmpeg and text libraries
must remain available when those optional backends are enabled. The raw CAPI
component install does not bundle them; [portable release archives](../packaging/README.md) do. The install uses loader-relative lookup and external dependency
link paths. Moving the Chroma install does not move its separately installed
runtime dependencies. CTest installs, relocates, builds, and runs a C-only consumer.

## ABI compatibility

`chroma_abi_version()` returns `CHROMA_ABI_VERSION` (1), independently of the engine
release string returned by `chroma_version_string()`. The shared-library ABI major
is 1 (`libchroma.so.1` on Linux); Linux symbols also have the `CHROMA_1` version.
Existing v1 function signatures, numeric status/format/capability constants, and
structure prefixes/layouts are frozen. Breaking changes require a new ABI major;
additive functions are allowed. JSON composition/diagnostic/plan/profile formats
follow their separately documented schema and engine semantics: binary stability
does not freeze the experimental composition DSL or planning diagnostics.

`chroma_status` is explicitly `int32_t`, not a compiler-sized enum. Structure fields
use fixed-width integers. Initialize options with `CHROMA_ENGINE_OPTIONS_INIT`;
NULL options select the same defaults. `struct_size` must be at least the v1
prefix size and `abi_version` must be 1. A larger options struct is accepted and
its unknown tail ignored. Info getters take a caller-provided byte size, reject
undersized buffers without writing, fill the known prefix, report the known
`struct_size`, and preserve unknown tails. V1 options/info/frame-info sizes are
24/48/32 bytes on supported targets. Frozen-header and layout consumers plus a
symbol audit protect the published v1 boundary; the current header is not their
only source of truth.

## Ownership and errors

- Engines, immutable compositions, frames, and strings are opaque library-owned
  allocations. Every successful creation transfers a handle to the caller. Destroy
  it with the matching `chroma_*_destroy`, never `free` or a different runtime.
- Every destroy function accepts NULL. Foreign/stale handles, double destruction,
  invalid pointers, unreadable input buffers, and undersized *actual* allocations
  with falsely advertised sizes are caller errors; handles are not a sandbox.
- Input JSON and paths are copied/consumed during the call. JSON has an explicit
  byte length, must contain no raw NUL bytes, and is capped at 64 MiB. Paths are
  UTF-8 and NUL-terminated. `chroma_composition_load_file` resolves assets relative
  to the file's parent; `parse` accepts an optional explicit base directory.
- Output handle pointers are set to NULL before a fallible call. Validation failure
  returns `CHROMA_ERROR_INVALID_COMPOSITION`, a NULL composition, and optional owned
  diagnostics JSON. Valid input returns `[]` diagnostics if requested. Other errors
  do not leave partially constructed output handles.
- Returned strings are immutable, NUL-terminated UTF-8. Their reported byte length
  excludes the terminator; their borrowed data pointer lasts until string destroy.
  Strings and frames remain valid after their engine/composition is destroyed.
- All C entry points prevent C++ exceptions escaping. Status values distinguish
  invalid arguments, unsupported ABI, invalid compositions, range errors,
  unavailable backends, allocation failure, I/O failure, render failure, and
  unexpected internal exceptions. Inspect `chroma_last_error()` immediately after
  a failure. Its thread-local borrowed message is allocation-independent, capped
  at 2047 bytes, and valid until the next status-returning call on that thread.
  A successful status call clears it; getters/destroy functions do not.

An allocation error while creating a post-render profile may occur after the
media file has completed. Video rendering writes into a reserved sibling temporary directory and atomically
renames the closed, finalized file over the destination on success. Cancellation
and pre-publication failures preserve an existing destination and remove temporary
output. On POSIX filesystems, replacing an existing destination also replaces its
inode (including a destination symlink); permissions/ownership are those of the new
file. Publication is atomic, not a power-loss durability guarantee. SIGKILL, process
crashes, or filesystem errors during cleanup can leave temporary directories.

## Rendering and threading

`chroma_engine_render_frame` compiles the requested absolute composition frame
and uses the engine's persistent CPU context. Cache budgets, resizing, clearing,
and usage follow the [bounded cache contract](architecture.md#bounded-prepared-resource-retention).
Each engine has independent mutable caches and must be externally synchronized:
do not concurrently call or destroy the same engine. Independent engines can
render concurrently. Immutable compositions and returned frame/string data may
be read concurrently while their owner ensures they are not destroyed. Errors
are per-thread, not global.

A returned frame owns tightly packed IEEE-754 32-bit float RGBA, in linear working
color and premultiplied alpha, with the pixel format constant
`CHROMA_PIXEL_RGBA_F32_LINEAR_PREMULTIPLIED`. Rows are top-to-bottom with byte
stride `width * 4 * sizeof(float)`; pixel data is aligned for `float` and remains
unchanged until frame destruction. Read dimensions/format/byte count using the
sized info getter. These are working-color pixels, not display-ready sRGB bytes;
apply appropriate unpremultiplication/transfer conversion when presenting them.
The output is an owned copy, so later renders or cache eviction cannot invalidate it.

`chroma_engine_render_video` is synchronous and uses a fresh render-job context
with the engine's current cache budgets. Its optional owned profile includes
per-job cache and stage/audio counters. Engine frame-cache usage is unchanged by
video jobs. MP4 output inherits the core's H.264/AAC, even-dimension, color, and
audio timing restrictions. `CHROMA_CAP_FFMPEG` and `CHROMA_CAP_TEXT` report compiled
backend availability, not hardware readiness or guarantees that an encoder, font,
or asset is usable; runtime failures still return statuses and messages. Native
CPU frames and inspection remain available in core-only builds.

## Progress and cooperative cancellation (additive ABI v1)

`chroma_engine_render_video_with_options` adds optional controls without changing
any existing function signature, numeric constant, or frozen struct. Detect it
through `CHROMA_CAP_RENDER_CONTROL`; media encoding still requires
`CHROMA_CAP_FFMPEG`. Old clients continue using `chroma_engine_render_video`.
NULL render options select the old behavior. Initialize new options with
`CHROMA_VIDEO_RENDER_OPTIONS_INIT`; short structs or mismatched ABI versions fail,
and larger structs preserve/ignore unknown tails.

```c
static int32_t CHROMA_CALL progress(const chroma_render_progress* p, void* user) {
  (void)user;
  fprintf(stderr, "%lld/%lld frames\n", (long long)p->frames_encoded,
          (long long)p->total_frames);
  return 0; /* nonzero requests cancellation, except at COMPLETED */
}
/* engine, composition, path and profile are supplied by the caller */
chroma_video_render_options options = CHROMA_VIDEO_RENDER_OPTIONS_INIT;
options.progress = progress;
chroma_status status = chroma_engine_render_video_with_options(
    engine, composition, path, &options, &profile);
```

Progress runs synchronously on the render thread: PREPARING (zero counters), one
RENDERING event per submitted video frame after its audio blocks, FINALIZING
(before flushing delayed codec packets), and COMPLETED after the file is closed
and published. Frame/audio counts and monotonic-clock elapsed nanoseconds never
decrease; counts are relative to the composition start. Audio counters count
stereo sample frames, not channel samples or AAC padding. They are zero for
compositions without audio tracks. `frames_encoded` counts submitted frames, so reaching the
frame total does not imply the MP4 is finalized. COMPLETED reports final totals;
its return value/cancellation request is ignored because output is committed.
Errors/cancellation have a returned status, not a synthetic COMPLETED event.

The progress pointer is borrowed only during its callback. Keep callback data,
engine, composition and cancellation token alive until the render returns. Do not
throw across the C boundary or reenter/mutate/destroy the active engine from a
callback. Callback return values do not affect future jobs. Slow callbacks slow
rendering; returning nonzero cancels the current job.

For cancellation from another thread, create a `chroma_cancel_token`, pass it in
`options.cancel_token`, and call `chroma_cancel_token_request(token)` from that
thread. The caller provides its own worker-thread scheduling; rendering remains
synchronous. Requests are thread-safe and latched, so a pre-requested token cancels
before output setup, and reusing it continues to cancel. Create a fresh token for
a fresh cancellation lifetime. A token can cancel multiple jobs; destroy it only
after all users finish. These functions are not async-signal-safe.

Checks occur before setup, between frames, after frame execution, between audio
blocks and around finalization. Cancellation waits for an in-flight decode,
filter, audio read, encoder or filesystem operation; there is no fixed latency
promise and no forced interruption inside third-party code. Before publication,
cancellation returns `CHROMA_ERROR_CANCELLED`, NULL profile, and a useful last
error, with destination preservation and temporary cleanup. A request racing with
publication may arrive too late: a committed output returns success. Late profile
allocation errors remain possible after a completed media file.

See [CLI usage and signal behavior](render-control.md). Async job handles,
accelerator planes and language bindings remain outside this synchronous API.

### Input preflight

`CHROMA_CAP_PREFLIGHT` advertises the additive ABI-v1
`chroma_composition_preflight(composition, flags, &report)` entry point. It returns
owned JSON with readiness and structured diagnostics. Flags=0 checks inputs and
source timelines; `CHROMA_PREFLIGHT_VIDEO_OUTPUT` adds output prerequisites.
`CHROMA_OK` means inspection succeeded, including reports with `ready: false`.
Video rendering also preflights automatically; failures keep
`CHROMA_ERROR_RENDER` and preserve output. See [preflight semantics](preflight.md).


For terminal JSON output metadata, timings, memory/cache accounting and failure
diagnostics, see [structured render reports](render-reports.md). This additive
feature is available on development main and newer package builds.
