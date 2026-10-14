# Experimental Composition Format

The current schema exists to exercise Chroma's validation and rendering boundaries. It is not yet a compatibility promise.

```json
{
  "schema_version": 1,
  "canvas": {
    "width": 1920,
    "height": 1080,
    "frame_rate": {"numerator": 30000, "denominator": 1001},
    "duration_frames": 300
  },
  "layers": [
    {
      "id": "background",
      "type": "solid",
      "start_frame": 0,
      "end_frame": 300,
      "color": [0.08, 0.12, 0.2, 1.0]
    }
  ]
}
```

Video files are declared separately from their uses so future plans can share probes, decoders, and cache identities:

```json
{
  "schema_version": 1,
  "canvas": {
    "width": 1920,
    "height": 1080,
    "frame_rate": {"numerator": 30, "denominator": 1},
    "duration_frames": 60
  },
  "assets": [
    {"id": "clip", "type": "video", "path": "media/clip.mp4"}
  ],
  "layers": [
    {
      "id": "video",
      "type": "video",
      "asset": "clip",
      "start_frame": 0,
      "end_frame": 60,
      "source_start_frame": 0,
      "opacity": 1.0
    }
  ]
}
```

## Current semantics

- Frame ranges are half-open: `start_frame` is included and `end_frame` is excluded.
- Frame-rate numerator and denominator are positive integers.
- Layer ids are non-empty, unique, and limited to 128 bytes in the experimental parser.
- Layers are listed back-to-front. Later layers composite over earlier layers.
- Solid colors are linear-light, straight-alpha RGBA values between 0 and 1. The CPU renderer converts them to premultiplied pixels before compositing.
- The canvas is currently bounded to 67,108,864 pixels to prevent accidental unbounded CPU allocations.
- PPM output has no alpha channel; premultiplied RGB is therefore effectively displayed over black.
- Asset paths are resolved relative to the composition file by the CLI. Parsing from memory accepts an explicit base directory.
- Video layers sample source time using the composition's exact rational frame rate. `source_start_frame` defaults to 0 and `opacity` defaults to 1.
- The current CPU spike requires decoded video dimensions to exactly match the canvas. Scaling and placement transforms have not landed yet.
- Asset existence and decodability are checked when media is probed or rendered, not during syntax-only composition validation.
- The internal effect IR is intentionally not exposed in this experimental JSON schema yet. Composition-level effect fields will be added only as their CPU reference semantics become executable and tested.
