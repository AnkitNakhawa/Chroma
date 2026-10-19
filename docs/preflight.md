# Input preflight

Preflight checks real inputs before a full-video render starts. It complements
structural `validate`: parsing checks the composition schema, references,
keyframes and layer/track timeline bounds; preflight opens files and checks
backend support, fonts, media and source trims. It collects independent problems
so you can fix them together.

## CLI

```sh
chroma validate scene.json --json
chroma preflight scene.json
chroma preflight scene.json --json
chroma preflight scene.json --inputs-only --json
chroma render scene.json --output result.mp4 --progress json
```

`preflight` defaults to checking inputs plus the current H.264/AAC video-output
prerequisites. `--inputs-only` skips encoder, output frame-rate and canvas-size
constraints; it still checks every declared input and the full source timelines.
It is useful for compositions intended for individual-frame rendering. Ordinary
`validate` remains offline and does not require asset files or media/text backends.

Exit codes: 0 means ready (possibly with warnings), 2 means invalid command
arguments, 3 means a structurally invalid composition, and 4 means unreadable
composition/input files or failed preflight. Standalone CLI preflight responds to
SIGINT/SIGTERM cooperatively with exit 130/143. JSON output is a single report on
stdout, with FFmpeg logging on stderr. Parse/file failures also produce reports.

```json
{
  "schema_version": 1,
  "ready": false,
  "assets_checked": 2,
  "diagnostics": [
    {
      "severity": "error",
      "code": "asset.missing",
      "path": "/assets",
      "message": "file does not exist: assets/music.wav",
      "asset_id": "music",
      "asset_path": "assets/music.wav"
    }
  ]
}
```

`ready` means there are no error diagnostics. It is not a guarantee against later
I/O, codec or output failures. `assets_checked` counts declared assets (zero when
structural parsing failed). Each diagnostic has severity (`error` or `warning`),
code, path and message. Asset-related diagnostics also identify `asset_id` and
`asset_path`, after composition-base resolution; paths can remain relative.
Asset diagnostics point to `/assets`, because typed compositions group assets by
type rather than retaining original JSON indices. Layer/track diagnostics retain
exact paths such as `/layers/2/source_start_frame`.

## Checks and severity

- **Files:** local regular files must exist, be readable and nonempty. Broken
  symlinks, directories and embedded NUL path bytes are rejected. Valid symlinks
  to regular files work.
- **Fonts:** the selected face must open through the same FreeType/HarfBuzz path
  as rendering. A small probe checks rasterization at every requested font size.
  Missing text-backend support is an error when a font is referenced.
- **Media:** FFmpeg must find the appropriate video/image or audio stream and
  open its decoder. Preflight decodes the first frame/block and checks dimensions,
  CPU pixel conversion or 48 kHz stereo resampler support. Audio-only files used
  as video/images, video-only files used as audio, corrupt headers/initial packets,
  unsupported codecs and empty streams are rejected. Source dimensions are
  limited to 67,108,864 pixels, matching the native surface budget.
- **Color:** explicitly tagged PQ/HLG HDR sources are rejected because the current
  renderer does not tone-map them. This is an SDR path, not production color
  management. Untagged HDR or unusual color profiles may need external inspection.
- **Source ranges:** video checks the last requested source frame against known
  media duration. The last sample time must be strictly before duration. Audio
  checks exact rounded 48 kHz sample ranges, including overflow and zero-sample
  timelines. A 48 kHz PCM WAVE trim can end exactly at source EOF. Source overflow
  is reported even when the asset itself is missing or incompatible.
- **Output prerequisites:** video mode checks FFmpeg/H.264 availability, even
  dimensions, codec frame-rate limits, OpenH264's 16-pixel minimum and AAC
  availability when audio is declared. It does not test writing the output path
  or fully initialize every encoder/device.

An asset referenced by any declared layer or audio track is required, including
late, transparent or off-canvas layers. Its failures are errors. Unused declared
assets are inspected too, but their problems are warnings. This conservative
policy catches intended content that would otherwise fail later, without blocking
jobs on unused declarations.

Unknown video/audio duration produces `media.duration_unknown`. For compressed or
resampled audio, a trim beyond metadata duration produces
`timeline.audio_duration_estimate`: duration metadata and decoder padding are not
an exact count of available 48 kHz samples. These warnings do not block a render;
runtime decoding remains authoritative. Review warnings before starting long jobs.

Representative error codes include `asset.missing`, `asset.unreadable`,
`asset.not_regular`, `asset.empty`, `asset.invalid_path`, `font.incompatible`,
`font.backend_unavailable`, `media.incompatible`, `media.backend_unavailable`,
`media.hdr_unsupported`, `timeline.video_out_of_range`,
`timeline.video_range_overflow`, `timeline.video_time_overflow`,
`timeline.audio_out_of_range`, `timeline.audio_range_overflow`,
`timeline.audio_empty`, and `output.*` prerequisite errors.

## Automatic rendering behavior

Full-video rendering through the CLI, C++ API, C API or Python runs preflight
before creating its temporary output directory, opening output media or encoding
frames. The existing `preparing` progress notification runs first, so callers can
cancel before any input inspection. Cancellation is checked between assets and
media packets; a blocking filesystem/demux/decoder call can finish before the
request is observed.

Failed preflight preserves existing output and emits no `rendering`, `finalizing`
or `completed` event. C++ throws `PreflightError`, with the full report available
through `report()`. CLI/C/Python render failures include an aggregate error message;
the C status remains `CHROMA_ERROR_RENDER`. Use explicit preflight to inspect the
full structured report, including warnings. Successful automatic preflight does
not separately print warnings or alter progress-event counts.

Individual `frame`/`render_frame` calls keep their existing behavior; explicit
inputs-only preflight checks the whole composition before batch frame extraction.
Preflight uses independent, temporary decoder/font resources. It does not populate
an engine's prepared image/text caches or change native pixels, audio or encoder
settings. Header/first-frame work adds to render setup time; execution telemetry
continues counting the actual rendering decoders, not these inspection probes.

## Python

```python
from chroma_render import Engine

with Engine() as engine, engine.load_composition("scene.json") as scene:
    report = scene.preflight()  # engine.preflight(scene) is equivalent
    for diagnostic in report["diagnostics"]:
        print(diagnostic["severity"], diagnostic["code"], diagnostic["message"])
    if report["ready"]:
        engine.render(scene, "output.mp4")
```

`for_video=False` selects inputs-only checks. Reports are Python-owned dictionaries
and stay usable after native handles close. Preflight requires
`Capabilities.PREFLIGHT`; older ABI-v1 libraries produce `LibraryLoadError` for
this method. Handle ownership and engine synchronization follow the existing
[Python API contract](python.md).

## C ABI v1

```c
chroma_string* report = NULL;
chroma_status status = chroma_composition_preflight(
    composition, CHROMA_PREFLIGHT_VIDEO_OUTPUT, &report);
if (status == CHROMA_OK) {
    /* Parse the owned report JSON and inspect its "ready" and diagnostics. */
    puts(chroma_string_data(report));
}
chroma_string_destroy(report);
```

Use flags=0 for inputs-only checks. Inspection returns `CHROMA_OK` with a report
even when `ready` is false; operation failures use the usual native statuses.
Composition and report output pointer are required, unknown flags are rejected,
and the output pointer is cleared on failure. The additive capability bit
`CHROMA_CAP_PREFLIGHT` and function preserve all frozen ABI-v1 layouts/symbols.

## Limits

This is a first-frame/metadata preflight, not a full source scan. Later corruption,
midstream resolution/audio-format changes, discontinuous audio timestamps,
inaccurate metadata, missing font glyphs, filesystem changes after checking and
output-space/permission/device failures can still fail at runtime. A passing
preflight does not certify an entire file or make mutable assets immutable.
Runtime checks and atomic output publication remain active.
