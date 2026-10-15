# Findings

## Market and technical context
- Remotion is a large developer ecosystem with roughly 62k GitHub stars and a broad monorepo spanning rendering, Studio, Player, cloud execution, media tools, templates, and agent skills.
- Remotion can use GPU-backed Chromium rendering and hardware video encoding, but React execution, DOM layout, orchestration, and portions of frame production remain outside an end-to-end GPU-native pipeline.
- FFmpeg already provides native decode/encode primitives; a new engine's main opportunity is a constrained retained scene graph, deterministic evaluation, cache reuse, fewer copies, and optional GPU-native compositing.
- Skia is a plausible rendering dependency. MLT and GStreamer demonstrate that general native media frameworks already exist, so the differentiator must be an agent-friendly deterministic composition model rather than generic video processing.

## Working product hypothesis
The project should be a headless native motion-graphics runtime for AI-generated declarative compositions. It should trade arbitrary web compatibility for speed, predictability, safety, and portable rendering. It is infrastructure that other products embed, not a preview platform or hosted video product.

## Unresolved questions
- Primary initial user: coding-agent user, SaaS video generator, or render-infrastructure team.
- First deployment target: local CLI or Linux server library.
- Exact integration wedge: standalone DSL renderer, Remotion sidecar/native layer, or both.
- Whether the native core should be C++ throughout or expose a C ABI to other language bindings.
- Initial GPU scope and whether CPU fallback is mandatory for every feature.

## VFX opportunity research

### How modern VFX is produced
- VFX is usually not "compiled into a video" in one step. A typical pipeline authors a scene or effect graph, prepares assets and shaders, optionally bakes simulations, renders frames and passes, composites those passes, applies color management, and only then encodes a delivery video.
- Some components really are compiled: C/C++ effect plugins, GPU shaders or compute kernels, and optimized graph execution plans. The scene or node graph itself is normally evaluated across frames rather than compiled directly into an MP4.
- 3D VFX uses geometry, materials, lighting, animation, particle/cloth/fluid/volume simulation, and raster or path-traced rendering. The output is commonly image sequences and auxiliary passes for later compositing.
- 2D compositing combines live-action plates, rendered passes, masks, transforms, tracking, keying, blur/glow, color operations, and text or graphics in a node graph.
- OpenFX is already a mature C/C++ ABI standard for individual 2D effects. A host supplies input clips and parameters and receives an output clip; the standard includes host suites for multithreading and GPU-oriented rendering support. Sources: https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectAPI.html and https://openfx.readthedocs.io/en/latest/Reference/ofxStructure.html

### Where the time goes
- In film-grade 3D, simulation and physically based rendering can dominate. Render time grows with frame count, resolution, samples, ray/path complexity, geometry, textures, volumes, and output passes.
- Sampling is an explicit quality/time tradeoff: more samples reduce noise but increase render time. Denoising can reduce the sample requirement, while GPU denoising has its own memory cost. Source: https://docs.blender.org/manual/id/5.1/render/cycles/render_settings/sampling.html
- Blender Cycles already supports GPU rendering, and RenderMan XPU is explicitly a hybrid CPU/GPU production path tracer. This weakens the case for a new general-purpose native path tracer. Sources: https://docs.blender.org/manual/en/4.4/render/cycles/introduction.html and https://renderman.pixar.com/product
- Unreal's Movie Render Queue documentation similarly exposes sample counts, warm-up frames, and high-resolution tiling; final-quality rendering remains a compute/quality tradeoff even in a real-time engine. Sources: https://dev.epicgames.com/documentation/en-us/unreal-engine/path-tracer-in-unreal-engine and https://dev.epicgames.com/documentation/unreal-engine/cinematic-rendering-image-quality-settings-in-unreal-engine?lang=en-US
- In 2D motion graphics, likely costs are media decode, per-pixel effects, compositing many layers, frame transfer between CPU and GPU, and final encoding. Browser and DOM orchestration can add overhead, but it is not necessarily the dominant cost in an effect-heavy composition.

### Opportunity assessment
- Poor wedge: build a new general-purpose renderer or path tracer. Existing engines have years of GPU, sampling, denoising, material, volume, and production integration work.
- Poor wedge: define another individual plugin API. OpenFX already fills that role, and opaque third-party plugins prevent many whole-graph optimizations.
- Better wedge: a headless, agent-first 2D effect and compositing graph that owns the whole graph and can optimize it globally.
- Candidate optimizations: constant folding, dead-node removal, kernel fusion, region-of-interest and tile evaluation, static-node caching, content-addressed incremental rerenders, parallel frame scheduling, and keeping intermediates resident on the GPU.
- Reuse rather than rebuild established layers: FFmpeg for decode and encode, OpenColorIO for color management, OpenImageIO or compatible image-sequence infrastructure, and optionally OpenFX adapters later.
- This fits the existing project boundary: it can be a C++ library and CLI called by Remotion or agents, without becoming an editor or preview platform.
- The clearest positioning is not "faster Hollywood VFX." It is "a native headless GPU compositor and effect graph for programmatic and agent-generated motion graphics."

### Existing infrastructure and competitive pressure
- OpenImageIO already provides a thread-safe tile-oriented image cache for accessing very large image sets with bounded memory and file handles. A new engine should integrate it or demonstrate a measurable reason not to. Source: https://openimageio.readthedocs.io/en/stable/imagecache.html
- Nuke already uses viewer, RAM buffer, playback, and disk caches, including automatically chosen intermediate cache points based partly on graph structure. Incremental graph caching alone is therefore not novel; the opportunity would be a simpler open-source, headless, deterministic implementation tailored to generated graphs. Source: https://learn.foundry.com/nuke/content/getting_started/managing_scripts/image_caching.html
- Nuke supports GPU-accelerated nodes and can retain data across compatible GPU-node chains before transferring it back to the CPU. This validates GPU residency and graph-chain optimization as important, but also confirms they are established techniques rather than a unique invention. Source: https://learn.foundry.com/nuke/current/content/misc/available_preferences_studio.html
- OpenColorIO is a mature, actively maintained color-management solution built for motion-picture VFX and animation, with C++ integration and ACES-compatible configurations. Color management should be adopted, not recreated. Source: https://opencolorio.readthedocs.io/en/main/

## Competitor landscape

### Developer-first video systems
- Remotion expresses each frame through React and offers CLI, server-side, cloud, and newer browser-side rendering. It wins on React flexibility and ecosystem; the proposed engine would trade arbitrary web compatibility for a constrained graph, native execution, deterministic resource use, and whole-graph optimization. Sources: https://www.remotion.dev/docs/the-fundamentals and https://www.remotion.dev/docs/render
- Revideo is an open-source TypeScript programmatic-video framework forked from Motion Canvas. Its renderer uses a headless browser and FFmpeg, making it architecturally close to the performance comparison we need. Sources: https://docs.re.video/ and https://docs.re.video/api/renderer/renderVideo/
- Motion Canvas uses a browser/editor runtime, sends rendered frames to Node.js, and uses FFmpeg to produce video. It is primarily a code-animation authoring system rather than a headless native effect compiler. Source: https://motion-canvas.io/docs/plugins/
- Creatomate and Shotstack already use declarative JSON-style representations and explicitly support programmatic or agent-created videos, but they are hosted commercial platforms with editors and rendering APIs. This proves that JSON plus agent authoring is not itself differentiated. Sources: https://creatomate.com/docs/api/quick-start/create-a-video-by-template and https://shotstack.io/docs/guide/

### Compositors and low-level engines
- Natron is the closest open-source architectural predecessor: an OpenFX host plus a demand-driven, multithreaded, tile-based render engine with RAM and disk caching. It is oriented around a desktop compositor and general OFX compatibility, not a small embeddable runtime for agent-generated motion graphics. Source: https://natron.readthedocs.io/en/rb-2.6/maintainers/overview.html
- FFmpeg supports complex multi-input and multi-output filter graphs and already covers a large portion of practical video filtering. It is powerful but its graph syntax and animation/graphics authoring model are not designed as an agent-friendly motion-design IR. Sources: https://www.ffmpeg.org/ffmpeg.html and https://www.ffmpeg.org/ffmpeg-filters.html
- OpenFX is shared by Natron, Nuke, DaVinci Resolve, and Fusion. Supporting it could provide compatibility, but making arbitrary plugins the core graph would restrict fusion, scheduling, and determinism. Source: https://natron.readthedocs.io/en/rb-2.6/maintainers/openfx-host.html

### Differentiation test
- "JSON for video" is not enough: Creatomate and Shotstack already do it.
- "Open-source node compositor" is not enough: Natron already does it.
- "Native filter graph" is not enough: FFmpeg already does it.
- "Video with code" is not enough: Remotion, Revideo, and Motion Canvas already do it.
- The defensible combination is: open-source + headless/embeddable + deterministic agent-friendly IR + native CPU/GPU execution + whole-graph optimization + incremental content-addressed rerendering.
- The project must prove meaningful performance or cost improvement on representative compositions; without benchmark evidence, it is only a smaller alternative with a less flexible authoring model.

## GPU strategy research
- A GPU is not required to validate the DSL, graph optimizer, deterministic renderer, cache, or bindings. It is required in v1 to validate the stronger performance thesis for effect-heavy workloads.
- Recommended v1 policy: GPU-enabled but not GPU-only. Maintain a CPU reference backend for correctness, CI, development without a GPU, and fallback; implement a deliberately narrow NVIDIA backend for the benchmark-critical nodes.
- Recommended deployment target: 64-bit Linux plus an NVIDIA GPU, packaged as a container. Do not make Metal a v1 requirement.
- CUDA is the pragmatic first compute backend because it aligns with common cloud NVIDIA instances and interoperates with NVDEC/NVENC. Google Cloud G2 exposes NVIDIA L4 GPUs; AWS documents G6 with L4 and G5 with A10G. Sources: https://docs.cloud.google.com/compute/docs/gpus/ and https://docs.aws.amazon.com/dlami/latest/devguide/gpu.html
- NVIDIA documents an end-to-end FFmpeg path that keeps decoded frames in CUDA memory and encodes through NVENC; supported encoders include H.264, HEVC, and AV1 depending on hardware. This is important because a GPU effects backend loses much of its value if frames are copied to the CPU between decode, effects, and encode. Source: https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/
- The initial reference cloud GPU should be NVIDIA L4, with compatibility testing on T4 and A10/A10G where possible. L4 is a suitable baseline for current cloud media workloads, while T4 coverage helps users with older or cheaper infrastructure.
- Vulkan compute is a plausible future portable backend: compute support is mandatory in Vulkan and supports headless compute. However, Vulkan plus cross-vendor video decode/encode interoperability would add substantial v1 engineering risk. Source: https://docs.vulkan.org/tutorial/latest/11_Compute_Shader.html
- Dawn/WebGPU Native could eventually provide D3D12, Metal, Vulkan, and OpenGL backends from one API, but it does not by itself solve native codec-surface interoperability or guarantee equivalent backend performance. Source: https://github.com/google/dawn
- Avoid hand-writing direct NVDEC/NVENC integrations initially. Use FFmpeg hardware frames and CUDA interop for the spike, then replace narrow pieces only if profiling justifies it.

## Repository implementation notes
- The first GitHub Actions run passed on both Ubuntu and macOS. GitHub warned that `actions/checkout@v4` used deprecated Node 20; the current upstream release discovered through the GitHub API was v7.0.1, so the workflow moved to the v7 major line.
- The current macOS development host exposes FFmpeg 8.0.1 through `pkg-config`: libavformat 62.3.100, libavcodec 62.11.100, libavutil 60.8.100, and libswscale 9.1.100. The implementation must remain compatible with CI environments that do not install these development libraries by keeping media support optional.
- FFmpeg container time bases often cannot represent a nominal frame time exactly: the 30 fps fixture stores frame 1 at 33/1000 seconds rather than 1/30. Chroma therefore exposes the actual decoded presentation time and selects the latest presented frame at or before the exact requested rational time.
- The current CPU decode spike converts decoded video through libswscale to 8-bit RGBA, then applies an sRGB transfer function into Chroma's linear float surface. This is deterministic for the test fixture but is not yet full metadata-driven color management for arbitrary BT.601/BT.709/BT.2020 inputs.
- The first native H.264 output visually preserved the intended blue/red frame sequence, but the selected stream duration was short by one frame until the encoder assigned explicit frame and stream durations. The regression suite now verifies frame count, frame rate, exact duration, deterministic decode, and representative colors.
- The first Release CPU profile on the 64x36 two-second fixture averaged 16.76 ms total for 60 frames (~3,581 fps): 0.008 ms compilation, 14.14 ms execution/decode/composite, 1.97 ms conversion/encode/mux, and 0.64 ms setup. This fixture is too small for a product performance claim, but it shows plan compilation is negligible and exposes per-frame decoder setup as the immediate CPU-path inefficiency.
- On the 640x360 four-second/120-frame corpus, Chroma averaged 658 ms (~182 fps), including ~426 ms execution and ~226 ms encoding. An approximately equivalent direct FFmpeg filter/encode command ran in a median 90 ms on the same host, so Chroma is currently about 7.2x slower. The major known architectural cause is reopening, seeking, and rebuilding decode/color-conversion state for every frame; persistent sequential decode is required before a credible CPU comparison.
- After adding job-scoped persistent sequential decoders, the same corpus improved to 463 ms mean (~259 fps): execution fell to ~234 ms while encoding remained ~226 ms. This is about a 30% total-time improvement, but still roughly 5.2x slower than the approximate FFmpeg baseline. The remaining gap is consistent with Chroma's multi-step YUV-to-RGBA8-to-linear-float-to-RGB8-to-YUV path and full-frame float compositing; this baseline argues for reducing conversions/copies before or alongside GPU work.
- The final reviewed pre-CUDA run, including cached libswscale state, measured 460 ms mean (~261 fps) on the 640x360/120-frame corpus: ~229 ms execution, ~224 ms encode, ~0.032 ms compile, and ~1.84 ms setup. Review also added known-duration bounds, per-layer decoder identities, and partial-output cleanup. These local numbers remain approximately 5x slower than the plain-FFmpeg comparison and are not generalized performance claims.

## Effects and optimizer direction

- Fit belongs in typed source sampling rather than as an opaque resize effect: source dimensions are only known at media execution today, and a future GPU backend should fuse placement with sampling, effects, and blending instead of materializing a resized intermediate.
- Physical pass diagnostics distinguish logical SSA-style intermediates from physical buffer allocations. A deterministic linear scan now reuses the lowest-numbered buffer only when its prior lifetime ends strictly before the next definition; strict inequality prevents a pass from reading and overwriting the same buffer.
- Cache classification is useful before cache execution exists: frame-varying video, temporal work, and nondeterministic effects are unsafe; cheap static solid graphs should be recomputed; expensive deterministic static graphs are candidates. Cache keys, persistence, eviction, and runtime integration remain separate work.
- Reusing mature C/C++ libraries is necessary but creates a risk that Chroma becomes a conversion-heavy wrapper. Chroma must own the semantic IR, whole-graph rewrites, fusion grouping, memory lifetime planning, residency decisions, caching, and scheduling.
- External libraries fall into three categories: specialized primitives that should be reused, backend implementations that preserve Chroma semantics, and black-box operations that form optimizer barriers.
- The first native fusible class should combine coordinate mapping, sampling, simple color math, opacity, and compositing. Neighborhood effects such as blur, temporal effects, fan-out, incompatible formats, and opaque external calls create natural fusion boundaries.
- Fusion is not automatically beneficial: oversized kernels can increase register pressure, occupancy loss, redundant sampling, and compilation cost. Initial fusion should use conservative deterministic rules and expose its decisions in plan inspection.
- The product and optimizer should advance through vertical slices: basic template-video features provide real graphs, while each slice validates simplification, static caching, fusion, barrier placement, surface reuse, or cross-frame scheduling.
- Broad plugin ecosystems should be deferred until the native optimizer is demonstrated. Otherwise compatibility requirements will define the core execution model before Chroma proves its differentiation.
- Static image decode is a resource-preparation boundary, not a compositor boundary: FFmpeg decodes once per normalized path in a CPU render context, while Chroma's plan retains image sampling, placement, effects, and blending as one visible fusion candidate. A regression test removes the file after the first frame and proves the second frame uses the retained surface.
