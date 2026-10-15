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

Video and image files are declared separately from their uses so plans can share decoders, prepared surfaces, and cache identities:

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
      "opacity": 1.0,
      "fit": "cover",
      "position": [0.5, 0.5]
    }
  ]
}
```

Static images use `"type": "image"` and may use any still-image format supported by the configured FFmpeg build:

```json
{
  "assets": [
    {"id": "logo", "type": "image", "path": "media/logo.png"}
  ],
  "layers": [
    {
      "id": "logo-layer",
      "type": "image",
      "asset": "logo",
      "start_frame": 0,
      "end_frame": 60,
      "fit": "contain",
      "position": [0.5, 0.5]
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
- Video and image `fit` support `none` (default), `contain`, `cover`, and `fill`. `position` is a normalized `[x, y]` alignment from `[0, 0]` (top-left) through `[1, 1]` (bottom-right) and defaults to `[0.5, 0.5]`.
- `contain` preserves the complete source and may leave transparent bars; `cover` preserves aspect ratio while cropping overflow; `fill` stretches to the canvas; `none` keeps native pixel size. Placement occurs before the ordered effect program.
- Asset existence and decodability are checked when media is probed or rendered, not during syntax-only composition validation.

## Basic effects and blending

Solid, image, and video layers may declare an ordered `effects` array and an optional `blend_mode`:

```json
{
  "id": "panel",
  "type": "solid",
  "start_frame": 0,
  "end_frame": 60,
  "color": [0.95, 0.28, 0.08, 1.0],
  "blend_mode": "screen",
  "effects": [
    {"type": "transform_2d", "matrix": [1, 0, 12, 0, 1, 8]},
    {"type": "crop", "x": 52, "y": 48, "width": 216, "height": 84},
    {"type": "opacity", "value": 0.82}
  ]
}
```

- Effects execute in declaration order.
- `transform_2d` uses the affine matrix `[m00, m01, m02, m10, m11, m12]`, maps source pixel centers into a same-sized destination, samples bilinearly, and treats pixels outside the source as transparent.
- `crop` retains pixels inside the half-open integer rectangle `[x, x + width) × [y, y + height)` and makes the remainder transparent.
- `opacity` accepts a value from 0 through 1 and scales premultiplied color and alpha.
- `color_matrix` requires 20 finite row-major values defining a 4×5 matrix over straight linear RGBA. The result is clamped and premultiplied again.
- Supported blend modes are `source_over` (default), `multiply`, `screen`, and `plus`.
- At most 64 effects may be declared on a layer in the experimental schema.
- Video layers retain the legacy top-level `opacity` field. When it differs from 1, it is applied after the ordered `effects` array.
- Blur, temporal, external/plugin, and animated effects are not accepted by the JSON schema yet.
