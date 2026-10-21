# Progress and cancellation

`chroma render` stays synchronous. Its default output remains the existing human
success line, without progress. SIGINT (Ctrl+C) and SIGTERM cancel safely even
when progress is disabled.

```sh
chroma render scene.json --output result.mp4 --progress text
chroma render scene.json --output result.mp4 --progress json > progress.ndjson
```

`--progress none|text|json` may be combined with cache budgets, cache reports and
graphics modes, and is accepted only by `render`. Duplicate/invalid modes fail
before rendering. Text progress goes to stderr and is throttled to roughly four
updates per second, plus phase changes. JSON goes to stdout, one flushed object
per event; FFmpeg diagnostics/errors stay on stderr. Successful JSON renders do
not append the human success line.

```json
{"event":"progress","phase":"rendering","frames_encoded":12,"total_frames":36,"audio_samples_encoded":18432,"total_audio_samples":57658,"elapsed_nanoseconds":210000000}
```

Phases are `preparing`, `rendering`, `finalizing`, `completed`. A render reports
zero counters at preparation, one event per submitted video frame (after its
audio blocks), then finalization and completion. Frame/audio counters and elapsed
steady-clock nanoseconds are monotonic. Audio uses stereo sample frames, without
AAC padding; jobs without audio tracks report zero. A frame count at its total means all video
frames were submitted; codecs and the container still need finalization. Treat
`completed`, emitted after publication, as the success notification.

On cancellation, JSON appends `{"event":"cancelled"}` with no `completed` event;
stderr prints `render cancelled`. Exit status is 130 for SIGINT or a closed JSON
progress pipe, and 143 for SIGTERM. Invalid arguments remain 2, composition errors
3, and other render failures 4. On other failures, use stderr and exit status;
there is no success event. A closed JSON consumer triggers cooperative cleanup
when detected before completion. A signal arriving after publication may be too
late to cancel: a committed job returns success. Ancillary cache-report I/O can
still fail after video completion; `completed` describes the video, not that report.

Cancellation is cooperative: checks occur between frames, after frame execution,
between audio blocks and around finalization. An in-flight decode, filter, audio
read, encoder or filesystem operation can delay a request. SIGKILL does not run
cleanup. There is no fixed cancellation-latency guarantee.

Video is encoded in an exclusively reserved `.chroma-render-*` sibling directory.
After successful trailer writing and file close, it is atomically renamed over
the destination. Before that point, errors/cancellation leave an existing output
unchanged; a new destination remains absent. Temporary media is removed during
normal unwinding. SIGKILL, process crashes, or cleanup filesystem errors can leave
that directory behind. The destination parent must exist and be writable. This
is atomic publication on supported POSIX filesystems, not a power-loss durability
promise. Replacement uses a new inode and replaces a destination symlink itself.

The [C API](c-api.md#progress-and-cooperative-cancellation-additive-abi-v1) exposes
the same phases and counters through `chroma_engine_render_video_with_options`.
Callbacks can request cancellation by returning nonzero; atomic cancellation
tokens also accept requests from other threads. Legacy function signatures and
ABI-v1 layout/symbol tests are unchanged.

### Preparing inputs

Full-video rendering now runs [input preflight](preflight.md) after the initial
`preparing` notification and before creating temporary output. Failed preflight
emits no rendering/completion events and preserves an existing destination.
Cancellation is checked between asset/packet inspections. Probe time is included
in setup time; the existing frame/audio progress counters remain unchanged.


For terminal JSON output metadata, timings, memory/cache accounting and failure
diagnostics, see [structured render reports](render-reports.md). This additive
feature is available on development main and newer package builds.
