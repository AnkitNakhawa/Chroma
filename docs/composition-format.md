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

## Current semantics

- Frame ranges are half-open: `start_frame` is included and `end_frame` is excluded.
- Frame-rate numerator and denominator are positive integers.
- Layer ids are non-empty, unique, and limited to 128 bytes in the experimental parser.
- Layers are listed back-to-front. Later layers composite over earlier layers.
- Solid colors are linear-light, straight-alpha RGBA values between 0 and 1. The CPU renderer converts them to premultiplied pixels before compositing.
- The canvas is currently bounded to 67,108,864 pixels to prevent accidental unbounded CPU allocations.
- PPM output has no alpha channel; premultiplied RGB is therefore effectively displayed over black.
