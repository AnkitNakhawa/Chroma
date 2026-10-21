# Structured render reports

Available on development `main` and its package artifacts; the published v0.1.0
archives predate this feature. Reports share schema version 1 across the CLI,
C API, and Python wrapper. Legacy rendering/profile APIs retain their contracts.

## CLI

```sh
chroma render scene.json --output video.mp4 --report render-report.json --progress json
```

`--report` takes a file path and may be combined with progress, cache budgets,
cache reports, and graphics modes. Stdout remains newline-delimited progress;
the terminal report is one JSON document in a separate file. Duplicate options
and `--report -` are usage errors. Omitting `--report` keeps normal CLI behavior.

Wait for the process to exit before reading the report. A progress `completed`
event means the video was published; the report file may still be writing.
Use a unique report path per job and check exit status as well as report status.

A complete report is atomically renamed over its destination. The report parent
must exist and be writable. The CLI reserves staging before rendering and refuses
report paths that alias the composition, output, cache report, or any input asset,
including symlinks and hard links. Ordinary failure/cancellation removes staging.

Exit statuses remain 0 for success, 2 for invalid arguments, 3 for invalid
composition, 4 for input/render/report I/O failure, 130 for SIGINT or a closed JSON
progress pipe, and 143 for SIGTERM. A syntactically valid request with a writable
report destination produces reports for unreadable/invalid compositions, failed
preflight, render failure, and cooperative cancellation. Usage errors and invalid
report destinations do not promise a report. SIGKILL, crashes, allocation failures,
and signals before cooperative rendering starts do not guarantee report delivery.

If report publication fails, the old report is preserved and the CLI exits 4.
A cache-report failure after successful video publication produces a failed job
report with `output.published: true` and a `job.failed` diagnostic. A report-write
failure cannot roll back a committed video. Inspect publication before retrying.

## Schema

| Field | Meaning |
| --- | --- |
| `schema_version`, `kind` | `1`, `chroma.render-report` |
| `status` | `succeeded`, `failed`, or `cancelled` |
| `phase` | Last observed phase; may also be `parsing` or `preflight` |
| `output.path` | Requested output path |
| `output.published` | Whether this job committed a new output; not whether an old file exists |
| `output.metadata` | On success: dimensions, frame count/rate, rational duration, codec and audio configuration; otherwise null |
| `output.size_bytes` | Published file size when stat succeeds; otherwise null |
| `progress` | Last observed frame/audio counters and elapsed nanoseconds; null if no event occurred |
| `profile` | Complete existing native render profile on success; otherwise null |
| `timings.wall_nanoseconds` | Time from report construction through terminal render/recorded failure, excluding final JSON publication |
| `timings.stages_complete` | Whether complete stage measurements are available |
| `timings.milliseconds` | Existing native stage timings, or null on failure/cancellation |
| `memory.process_peak_rss_bytes` | OS process lifetime high-water resident memory, or null on unsupported platforms |
| `memory.scope` | `process_lifetime`; this is not an isolated per-render peak |
| `cache` | Configured image/text budgets and complete usage on success; usage is null after failure/cancellation |
| `diagnostics` | Errors and preflight warnings, with severity, code, path, message, and suggested action |

Metadata is labeled `source: render_configuration`: it records the successful
renderer configuration rather than independently re-probing or quality-checking
the encoded file. Audio fields are null for jobs without audio tracks. Video is
H.264/yuv420p; present audio is AAC, 48 kHz, stereo.

Stage timings retain the [existing profile semantics](../benchmarks/README.md):
`execute` includes decode/conversion/composition, and `encode` includes conversion
and codec/mux work. Do not add overlapping timings together. Failed jobs keep
available progress but do not invent partial stage/cache measurements.

RSS is measured with `getrusage` on Linux/macOS and normalized to bytes. A long-lived
Python/server process may retain a high-water mark from an earlier job or concurrent
work. Cache counters describe retained prepared resources, not all allocator,
framebuffer, decoder, or encoder memory.

Preflight diagnostics preserve their existing codes and asset ID/path. Other
codes include `composition.unreadable`, `render.invalid_argument`, `output.io`,
`render.failed`, `render.cancelled`, `render.unavailable` (native API without media),
and `job.failed` (CLI setup/ancillary failure). Parse diagnostics keep their existing
codes. Use codes and severity for branching; messages and suggested actions are
human guidance. Generic failures do not imply that an automatic retry is safe.

## Python

```python
from chroma_render import Engine

with Engine() as engine, engine.load_composition("scene.json") as scene:
    report = engine.render_report(scene, "video.mp4")
    if report["status"] == "succeeded":
        print(report["output"]["metadata"])
    else:
        print(report["diagnostics"])
        print("Output committed:", report["output"]["published"])
```

Unlike `render`, `render_report` returns a dictionary for terminal render failure
and cancellation. Pass the same `progress=` and `cancel_token=` arguments as
`render`. Bad binding arguments and callback exceptions still raise; composition
loading/parsing continues to raise its existing structured exceptions. No report
file is written by this method. `render` still returns its old profile and raises
on failure/cancellation.

The loaded library must advertise `Capabilities.RENDER_REPORT` (bit 32). Older
ABI-v1 libraries still support the legacy wrapper APIs; requesting a terminal
report from them raises `LibraryLoadError` instead of fabricating measurements.

## C ABI v1

`chroma_engine_render_video_report` accepts the existing render options, including
progress and cancellation. `out_report` is required. It receives a caller-owned
`chroma_string` on successful renders, render failures, cancellation, and unavailable
media support. **Destroy the returned string even when status is non-OK.**
Invalid API arguments/ABI and allocation failures can leave it null.

```c
chroma_string *report = NULL;
chroma_status status = chroma_engine_render_video_report(
    engine, composition, "video.mp4", NULL, &report);
if (report) {
    puts(chroma_string_data(report));
    chroma_string_destroy(report);
}
/* Handle status, including CHROMA_ERROR_CANCELLED or CHROMA_ERROR_RENDER. */
```

The capability bit and entry point are additive. Existing struct layouts, symbols,
profile ownership, and error behavior remain unchanged. The JSON string is an
independent snapshot; the borrowed `chroma_string_data` pointer is valid until its
string is destroyed. Native API reports start after composition/API validation;
CLI reports additionally cover file loading and composition parsing.
