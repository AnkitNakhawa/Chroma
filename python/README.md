# Chroma Python binding

Dependency-free Python 3.10+ bindings for Chroma's stable C API. The `chroma_render`
module provides composition loading, frame and MP4 rendering, cache controls,
progress callbacks and cooperative cancellation. Rendering runs in the native library.

Install this package with `python -m pip install ./python` from the repository,
or install the wheel included in a Chroma Linux/macOS release archive. Set
`CHROMA_LIBRARY_PATH` to the archive's `lib/libchroma.so` (Linux) or
`lib/libchroma.dylib` (macOS). Keep the archive together so its bundled dependencies
remain available. The wheel contains Python code; it requires the native SDK.

```python
from chroma_render import Engine

with Engine() as engine:
    with engine.load_composition("scene.json") as scene:
        profile = engine.render(scene, "output.mp4", progress=lambda p: print(
            p.phase.name, p.frames_encoded, "/", p.total_frames))
```

See [the Python guide](https://github.com/AnkitNakhawa/Chroma/blob/main/docs/python.md)
for cancellation, ownership, concurrency, errors, and frame access.
