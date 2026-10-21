# Composition Format

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

The [machine-readable schema](../schemas/composition-v1.schema.json) covers structural validation. See the [agent validation guide](composition-schema.md) for offline discovery and semantic checks.

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

Shape layers use canvas-space `[x, y, width, height]` bounds. Supported shape names are `rectangle`, `rounded_rectangle`, and `ellipse`. Fill is required; stroke is optional and is drawn inside the shape boundary:

```json
{
  "id": "card",
  "type": "shape",
  "shape": "rounded_rectangle",
  "start_frame": 0,
  "end_frame": 60,
  "bounds": [40, 30, 240, 120],
  "corner_radius": 18,
  "fill": [0.1, 0.2, 0.8, 1.0],
  "stroke": {"color": [1, 1, 1, 1], "width": 2}
}
```

Text layers reference a `font` asset. `position` is the canvas-space text baseline, `font_size` is measured in pixels, and HarfBuzz determines glyph order and placement before FreeType rasterization:

```json
{
  "assets": [
    {"id": "body", "type": "font", "path": "fonts/Inter-Regular.ttf"}
  ],
  "layers": [
    {
      "id": "title",
      "type": "text",
      "font": "body",
      "text": "Hello Chroma",
      "font_size": 40,
      "position": [32, 72],
      "color": [1, 1, 1, 1],
      "start_frame": 0,
      "end_frame": 60
    }
  ]
}
```


### Text layout

Existing text layers keep their original single-run shaping and baseline positioning
when `layout` is absent. Add `"layout": {}` to opt into multiline layout, including
explicit LF/CRLF line breaks and tab normalization (four spaces per tab).
`position: [x, y]` still means the first-line baseline. Each following baseline is
`font_size * line_spacing` pixels below the previous one, rounded to whole glyph pixels.

```json
{
  "id": "description", "type": "text", "font": "body",
  "text": "A longer description that wraps and ends gracefully when space runs out.",
  "font_size": 32, "position": [48, 100], "color": [1, 1, 1, 1],
  "start_frame": 0, "end_frame": 90,
  "layout": {
    "width": 420, "height": 150,
    "wrap": "word", "align": "left", "line_spacing": 1.4,
    "max_lines": 3, "overflow": "ellipsis"
  }
}
```

| Field | Default | Meaning |
|---|---|---|
| `width` | Unbounded | Positive box width in pixels, at most 65536; required for center/right alignment and ellipsis |
| `height` | Unbounded | Positive box height in pixels, at most 65536 |
| `wrap` | `word` | `word` automatically wraps when width is given; `none` keeps only explicit line breaks |
| `align` | `left` | Physical `left`, `center`, or `right` alignment within width, measured using shaped advances |
| `line_spacing` | `1.2` | Positive baseline-distance multiplier of font size, at most 100; values below 1 allow overlapping lines |
| `max_lines` | Unbounded | Integer 1–16384; caps emitted lines in every overflow mode |
| `overflow` | `visible` | `visible`, `clip`, or `ellipsis` as described below |

The box's left edge is `position.x`. Its top is `position.y` minus the selected
font's scaled ascender; height extends down from that top. Alignment uses logical
line advance, so ink overhangs can extend beyond the box in visible mode. Center
and right shifts are rounded to whole pixels. Alignment is physical, independent
of the paragraph's script direction.

Wrapping measures HarfBuzz-shaped text, prefers ASCII space/tab word boundaries,
and reshapes each resulting line. Trailing break spaces and leading spaces after
automatic breaks are removed; spaces not used as break delimiters are preserved. Long
words fall back to shaped grapheme-cluster boundaries, preserving combining
characters and ligatures. A single cluster can exceed a narrow width. Empty and
trailing explicit lines count toward line limits. Full Unicode line-breaking,
hyphenation, mixed-direction Unicode bidi layout, justification, vertical alignment,
and font fallback are outside this version. Existing single-direction HarfBuzz
shaping (including Arabic) remains available; direction/script are kept consistent
within each paragraph.

- `visible` emits wrapped lines without clipping to width/height. Height is ignored;
  an explicit `max_lines` still discards later lines without an ellipsis.
- `clip` intersects glyph coverage with the specified width/height before effects.
  Partially visible lines at the bottom are clipped, and later lines are discarded.
  With only `max_lines`, it drops later lines without imposing a height rectangle.
- `ellipsis` replaces the logical end of the last retained line with U+2026 when
  lines are omitted by height/max_lines. With `wrap: "none"`, each over-wide line
  is shortened independently. It removes whole shaped clusters and reshapes until
  the prefix plus ellipsis fits. If the ellipsis itself is too wide, the line is
  empty. Height normally retains lines whose font metrics fit fully, with one
  clipped line allowed for a box shorter than the font. Ink is clipped to width/height.

Clipping occurs in the text source coordinate system. Subsequent transforms move
that clipped source; blur can spread its coverage beyond the original layout box.
Use a crop effect after blur when you also need a fixed final effect boundary.
Width/height clipping uses whole coverage pixels contained inside the box.

All layout fields participate in prepared-text cache identity. Color, position,
and animation remain independent of text preparation, preserving reuse across
layers. Invalid types, ranges, enum strings, dependencies, and unknown layout
fields produce diagnostics at `/layers/N/layout/FIELD`. Direct C++ compositions
and execution plans enforce the same constraints. CLI, C API, and Python use
this same JSON contract, with no C ABI changes.

See [the animated 1080p layout example](../examples/text-layout.json); supply
`examples/fonts/Inter-Regular.ttf` or change the font asset to your own font.


## Basic effects and blending

Solid, image, shape, text, and video layers may declare an ordered `effects` array and an optional `blend_mode`:

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
- `opacity` accepts either a static `value` from 0 through 1 or a `keyframes` array. It scales premultiplied color and alpha.
- `gaussian_blur` accepts a `radius` from 0 through 4096. Radius is the Gaussian standard deviation in canvas pixels; samples outside the surface are transparent.
- `mask` accepts `shape` (`rectangle` or `ellipse`), canvas-space `bounds` (`[x, y, width, height]`), and optional boolean `inverted`. The deterministic CPU reference uses 4×4 coverage sampling at mask edges.
- Opacity keyframes use nonnegative, strictly increasing integer `frame` offsets relative to the layer's `start_frame`. Values before the first and after the last keyframe clamp to the nearest endpoint.
- A keyframe's optional `easing` controls its segment to the next keyframe. Supported curves are `linear` (the default), `ease_in`, `ease_out`, and `ease_in_out` using deterministic cubic equations.
- `color_matrix` requires 20 finite row-major values defining a 4×5 matrix over straight linear RGBA. The result is clamped and premultiplied again.
- Supported blend modes are `source_over` (default), `multiply`, `screen`, and `plus`.
- At most 64 effects may be declared on a layer in the experimental schema.
- Video layers retain the legacy top-level `opacity` field. When it differs from 1, it is applied after the ordered `effects` array.
- Temporal and external/plugin effects are not accepted by the JSON schema yet. Animated opacity resolves to a static opacity node for each compiled frame before algebraic optimization and fusion planning. Gaussian blur is a neighborhood barrier, so fusible effects on either side remain separate groups.

For example, this fades in during the first 12 frames of a layer:

```json
{"type": "opacity", "keyframes": [
  {"frame": 0, "value": 0.0, "easing": "ease_out"},
  {"frame": 12, "value": 1.0}
]}
```

### Translation, scale, and rotation

`translation`, `scale`, and `rotation` accept exactly one of a static `value` or
1–1024 `keyframes`. Translation values are `[x, y]` offsets in canvas pixels;
scale values are independent `[x, y]` factors; rotation values are degrees.
Keyframe frames and easing follow the same layer-relative rules as opacity,
including endpoint clamping and easing on the outgoing segment.

```json
{"type": "translation", "keyframes": [
  {"frame": 0, "value": [-100, 0], "easing": "ease_out"},
  {"frame": 30, "value": [0, 0]}
]}
```

```json
{"type": "scale", "origin": [160, 90], "keyframes": [
  {"frame": 0, "value": [0.5, 0.5]},
  {"frame": 30, "value": [1, 1]}
]}
```

```json
{"type": "rotation", "origin": [160, 90], "keyframes": [
  {"frame": 0, "value": 0},
  {"frame": 60, "value": 720}
]}
```

Scale and rotation accept a fixed canvas-space `origin`, defaulting to `[0, 0]`.
Positive rotation is clockwise in the downward-positive canvas Y coordinate
system. Angles interpolate as written, without shortest-path wrapping: 0 → 720
makes two full turns, while 350 → 10 rotates backwards through 340 degrees.
Scale factors may be zero (the singular transform renders transparent) or
negative (a reflection). All values and origins must be finite; an evaluated
matrix that exceeds finite floating-point range fails during compilation.

Effects apply in declaration order after source placement. To resize and turn a
layer around its original center, then move it, declare scale, rotation, then
translation. These operations transform the same canvas-sized source surface as
`transform_2d`; they do not infer a layer's bounding box or anchor. Translation
does not accept an origin. Per-frame compilation resolves each animation to an
affine matrix before existing identity removal, adjacent matrix composition, and
fusion planning. The CPU reference and eligible fused source paths execute the
same lowered matrix. Inspection retains `effects_frame_varying` for multi-keyframe
programs after lowering, so cache diagnostics do not suggest reusing animated
effect output; static image/text preparation remains reusable. See
`examples/animated-transforms.json`.

Simple transitions are compositions of existing primitives rather than a separate execution type. For a source-over crossfade between opaque layers, keep the outgoing layer opaque underneath and animate the incoming layer's opacity from 0 to 1; this preserves full output alpha while interpolating the colors. See `examples/crossfade.json`.

## Audio tracks

Audio is independent of visual layer order. Declare `{"id":"music","type":"audio",
"path":"music.wav"}` in `assets`, then add an optional top-level array:

```json
"audio_tracks": [
  {"id":"bed","asset":"music","start_frame":0,"end_frame":120,
   "source_start_frame":30,"gain":0.5}
]
```

Track ranges are half-open and must be inside the composition. Source trimming starts
at `source_start_frame` (default 0), measured at the **composition** frame rate,
including for audio embedded in video files. The track's range determines the trim
length. Gain is a finite linear multiplier from 0 through 16 (default 1); 0 mutes.
Audio asset IDs share the existing asset namespace; track IDs are unique within
`audio_tracks`. Missing tracks preserve video-only output.

The sequential CPU mixer resamples inputs through libswresample to 48 kHz stereo
float PCM. Each frame boundary maps independently to the nearest sample (ties up),
so fractional frame rates do not accumulate drift. Source time zero is the first
decoded sample, independent of the container's timestamp origin. Discontinuous
source timestamps and midstream format changes fail explicitly. Trimming decodes
from the beginning to preserve decoder priming and resampler state; deep trims
therefore have a startup cost. Input audio is not taken automatically from video
layers: declare the file again as an audio asset when its sound is wanted.

Samples outside active tracks are silence. Tracks sum in declaration order using
double precision; the final mix saturates to [-1, 1], without automatic gain
normalization. A trim extending beyond decoded source samples is an error,
including muted tracks. Decode and mix storage is block-based rather than sized
to the entire composition; each overlapping track owns its own decoder.

`render` adds an AAC stream when at least one track is declared, muxed alongside
H.264 video in MP4. The stream's presentation duration matches the composition to
the nearest 48 kHz sample. Encoder priming is represented by MP4 metadata; generic
raw PCM decoders can expose up to one extra trailing AAC block. AAC is lossy, so
encoded-output comparisons use documented tolerances rather than exact equality.
The mixer itself is deterministic and tested with exact PCM fixtures. Benchmark
JSON reports `audio_samples_encoded` and `milliseconds.audio` separately from
video encoding/conversion work. Audio integration CTest coverage additionally
requires Python 3 and the FFmpeg/ffprobe command-line tools.
