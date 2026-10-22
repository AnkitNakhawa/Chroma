# Bounded CPU parallelism

Development `main` parallelizes independent work within a frame. This feature is
newer than the published v0.1.0 archives. It does not change composition JSON or
the stable C ABI. CLI, C and Python renders all use the same implementation.

By default, Chroma uses at most four participants: the calling thread and up to
three shared background workers, capped by reported hardware concurrency. One
process-wide pool is reused across frames and engine instances. It has no job
queue: a competing render executes its pass inline when the pool is busy. Nested
requests also execute inline. A synchronous barrier finishes all partitions
before the next layer/pass, buffer release, or return to the caller.

Configure the limit at process startup, before using Chroma:

```sh
CHROMA_CPU_THREADS=1 chroma render scene.json --output serial.mp4
CHROMA_CPU_THREADS=4 chroma render scene.json --output parallel.mp4
CHROMA_CPU_THREADS=2 chroma hardware
```

Values 1 through 4 are accepted and capped by available hardware concurrency;
invalid values fall back to one participant. Configuration is read once per
process. `chroma hardware` reports `configured_cpu_thread_limit`; this is a
configured upper bound, not a guarantee that worker creation succeeds or every
pass uses all participants. Worker startup failure falls back to serial work.

The bound covers Chroma's additional workers. Application-owned caller threads,
FFmpeg's codec threads, and separate processes are outside it. For multiple
simultaneous render processes, set an appropriate per-process limit in the job
scheduler. A shared pool prevents worker growth across engine instances but does
not limit how many render calls an application can launch.

## Work and correctness

The current parallel paths cover:

- Bulk linear RGB → sRGB8 output conversion.
- Uniform solid blending and native media fallback blending.
- Eligible fused source transforms/compositing.
- Surface placement, affine resampling and shape rasterization by independent rows.

Small work stays inline; the dispatch threshold is approximately 131,072 pixels
per participant (row partitions use a width-derived grain). Each worker receives
a disjoint contiguous range. Sample order within a pixel and layer order are
unchanged. Text shaping/glyph order, decode, most fallback effects and codec/mux
work are not newly parallelized. No floating-point reduction is shared across
workers, and no frames or full-size scratch surfaces are queued for future work.
The existing default floating-point environment remains the supported contract.

Progress callbacks remain on the calling thread. Existing cancellation checks
remain between synchronous passes/frames; callbacks are not invoked from worker
kernels. Callback failures and cancellation retain existing output-publication
behavior. If a worker kernel throws, all participants finish before the exception
is rethrown on the caller; the pool remains usable. Errors are not reported while
workers still hold references to local buffers.

`chroma_parallel_tests` exercises exact partition coverage, nested calls,
exception recovery, concurrent callers, bounded active tasks, pool reuse and
bulk conversion against the scalar oracle. CTest runs it with limits one and
four. Native composition tests include large frames and compare optimized
serial/parallel execution bit-for-bit. The normal quality gate and C API
progress/cancellation/thread tests remain enabled. The concurrency test also
passed a local ThreadSanitizer run; it is not a newly mandatory sanitizer CI job.

See [benchmark reproduction](https://github.com/AnkitNakhawa/Chroma/blob/main/benchmarks/CPU_PARALLEL.md) and the
[showcase measurements](https://github.com/AnkitNakhawa/Chroma/blob/main/docs/benchmarks/2026-10-22-cpu-parallel.md).
