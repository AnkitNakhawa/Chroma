# Python API

The `chroma-render` distribution imports as `chroma_render` and requires Python
3.10+ on 64-bit Linux or macOS. It uses standard-library `ctypes` to call C ABI v1;
it has no runtime Python dependencies and requires no Python extension compiler.
The native renderer, codecs, and quality settings are shared with the CLI/C API.

## Install

Extract the matching Chroma release archive and keep its directory together.
Each archive includes a wheel in `share/chroma/python/`:

```sh
python3 -m pip install /path/to/chroma/share/chroma/python/*.whl
export CHROMA_LIBRARY_PATH=/path/to/chroma/lib/libchroma.dylib # macOS
# Linux: /path/to/chroma/lib/libchroma.so
```

From source, use `python3 -m pip install ./python` and point to your built library.
The wheel contains only Python code and does not download the native renderer.
Nothing is currently published to PyPI. You can also import the archived source
without installing: set `PYTHONPATH=/path/to/chroma/share/chroma/python`;
in this layout the wrapper discovers the archive's library automatically.

Library selection order is `Engine(library_path=...)`, `CHROMA_LIBRARY_PATH`,
the enclosing SDK's `lib` directory, then the system loader's `find_library`.
An explicit invalid path fails instead of silently choosing another library.
`LibraryLoadError` reports load, missing-symbol, and ABI mismatch failures.
Use `engine.version`, `engine.library_path`, and `engine.capabilities` to inspect
what loaded. Render controls require `Capabilities.RENDER_CONTROL`; base
operations also work with earlier ABI-v1 libraries without that capability.

## Rendering and progress

```python
from chroma_render import Engine

with Engine() as engine:
    with engine.load_composition("scene.json") as scene:
        info = scene.info  # dimensions, rational frame rate, timeline range
        profile = engine.render(scene, "output.mp4", progress=lambda p: print(
            p.phase.name, p.frames_encoded, "/", p.total_frames))
```

`parse_composition` accepts JSON text, UTF-8 bytes, or a dictionary; pass
`base_directory=` for relative asset paths. `load_composition` resolves assets
relative to the composition file. `scene.inspect_plan(frame, optimized=True)`
returns a Python dictionary. `render` returns the native profile dictionary.

Progress callbacks run synchronously on the rendering thread. Each argument is
an immutable, independently owned `RenderProgress` snapshot with `phase`,
`frames_encoded`, `total_frames`, `audio_samples_encoded`, `total_audio_samples`,
and `elapsed_nanoseconds`. Phases are `RenderPhase.PREPARING`, `RENDERING`,
`FINALIZING`, and `COMPLETED`. Audio counters count stereo sample frames,
excluding encoder padding; no-audio renders report zero. Full frame counts do
not imply the file has finished finalizing. Keep callbacks short; dispatch UI
updates to your application's UI thread.

## Cancellation and callback errors

Returning `True` from a callback cancels. Return `None` or `False` to continue;
other return types raise `TypeError`. A callback cancellation raises
`RenderCancelledError`, whose `status` is `Status.CANCELLED`.

For cancellation from another thread, retain a token throughout the render:

```python
from concurrent.futures import ThreadPoolExecutor
from chroma_render import Engine, RenderCancelledError

with Engine() as engine, engine.load_composition("scene.json") as scene:
    with engine.cancellation_token() as token, ThreadPoolExecutor(1) as workers:
        future = workers.submit(engine.render, scene, "output.mp4", cancel_token=token)
        # In a UI, call token.cancel() from the Cancel button handler.
        token.cancel()
        try:
            future.result()
        except RenderCancelledError:
            print("Cancelled")
```

`CancellationToken(library_path=...)` can also be constructed directly. Tokens
are thread-safe and latched; create a new token for a new uncancelled operation.
Native calls release the Python GIL, allowing another thread to request
cancellation. Checks are cooperative: a decode, effect, encoder call, or I/O
operation already running can finish before cancellation takes effect.

Python callback exceptions, including `KeyboardInterrupt` and `SystemExit`, are
caught at the C boundary. The native operation cleans up before the wrapper
re-raises the original exception. On current libraries a Python callback bridge
is active even without a user callback, allowing Python interrupts at native
progress checkpoints. Signal delivery and cancellation remain cooperative;
there is no new process-wide signal handler.

Cancellation and failures before publication preserve an existing destination
and remove temporary output. Publication is atomic after finalization. A token
request racing publication may arrive too late. `COMPLETED` runs **after**
publication: returning `True` there does not cancel; an exception there is
re-raised but the completed file remains. See [render control](render-control.md)
for native cancellation and output guarantees.

## Frames, caches, and lifetimes

```python
with Engine(image_cache_bytes=32 << 20, text_cache_bytes=8 << 20) as engine:
    with engine.load_composition("scene.json") as scene:
        frame = engine.render_frame(scene, 0)
        rgba = frame.pixels  # flat read-only native-endian float32 memoryview
    engine.set_cache_budgets(16 << 20, 4 << 20)
    print(engine.cache_usage)  # native budgets, usage, hits, misses, eviction counters
    engine.clear_caches()
```

Frames contain linear, premultiplied RGBA, matching the C API. `frame.data` is a
Python-owned `bytes` copy; `frame.pixels`, `width`, `height`, and
`row_stride_bytes` remain valid after native handles close. Optional NumPy users
can call `numpy.frombuffer(frame.data, dtype=numpy.float32).reshape(h, w, 4)`.
The copy makes ownership safe and has a memory/time cost for frame extraction;
MP4 rendering stays entirely native.

Engines, compositions, and tokens own native handles. Use `with` or `close()`
for deterministic release; garbage collection is a fallback. Close is idempotent.
Using closed handles fails. Closing an active handle, using one engine
concurrently, or reentering that engine from its callback raises `RuntimeError`.
Calling `token.cancel()` while rendering is supported. Independent engines can
run concurrently and share an immutable composition from the same loaded
library. Compositions remain usable after their creating engine closes.

`ChromaError.status` preserves native status codes. Invalid compositions raise
`InvalidCompositionError` with structured `.diagnostics`. Invalid Python
argument types/ranges raise `TypeError`/`ValueError` before ctypes can truncate
paths or wrap integers. FFmpeg/text availability follows the loaded library;
unavailable rendering raises a native `Status.UNAVAILABLE` error.

## Development checks

`ctest` includes real shared-library Python integration tests. CI checks Python
3.10 and 3.14, wheel installation in a fresh virtual environment, and relocated
release archives on Linux and both macOS architectures. Clean runtime tests
compare Python, CLI, and C output as decoded RGBA/PCM and exercise cancellation
without installed compilers, FFmpeg, or third-party Python packages.

When testing an AddressSanitizer-instrumented library inside Python, preload the
compiler's ASan runtime into a compatible interpreter (`LD_PRELOAD` on Linux or
`DYLD_INSERT_LIBRARIES` on macOS). Apple's system Python can reject sanitizer
loading; use a separately installed Python. Set `ASAN_OPTIONS=detect_leaks=0`
to exclude the interpreter's unrelated shutdown allocations. You can disable
only the ctypes CTest with `-DCHROMA_TEST_PYTHON_WRAPPER=OFF` when running native
sanitizer tests, then run the Python integration script with the proper host
and runtime preload. Ordinary builds register the test with Python 3.10+.
