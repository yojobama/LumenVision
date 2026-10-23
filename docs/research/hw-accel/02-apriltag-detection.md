# 02 - AprilTag detection: greyscale frame to published tag poses (RK3588)

Research notes. Scope: every stage from a greyscale frame to published poses
(decimate/threshold/segment/quad-fit/decode/refine/pose/multi-tag/overlay),
CPU vs GPU split in the Vulkan backend, pipelining, threads/affinity, NEON,
ROI tracking, pose solve, overlay drawing.

## Status

Complete (2026-10-24). Code read: ApriltagDetector, Cpu/VkApriltagBackend, IApriltagBackend, ConstrainedPnp,
TagOverlay, StageProfile, Frame, CpuAffinity, ISink/ISource frame-consumer registry, WebRTCSink frame path,
V4L2 grab path; vkapriltag QuadDecode, TagDecoder, WorkerPool, FramePipeline, PoseEstimator, PERFORMANCE.md,
research/00-SUMMARY + 01/02; upstream libapriltag apriltag_pose.c and workerpool.c. Nothing measured on hardware
by this agent - all gains are estimates or citations of in-tree measurements, marked as such.

## Ranked recommendations (this codebase)

| # | Change | Where | Expected gain on RK3588 (per frame, sink latency) | Effort / risk | Confidence |
| --- | --- | --- | --- | --- | --- |
| 1 | Replace libapriltag `estimate_tag_pose_orthogonal_iteration` with vkapriltag's `PoseEstimator::EstimateBoth` (already linked); recompute H from undistorted corners (F5, F6) | ApriltagDetector.cpp:449-509 | ~0.9 ms per visible tag -> ~0.02 ms (58x, measured on the Orange Pi 5 by the library); 2-4 tags = 1.8-3.6 ms | Low; tolerance-equal not bit-equal; validate with tools/validate_pose | High |
| 2 | Fix `CpuAffinity` tiering so both A76 clusters count as performance cores even when their PVTM-binned max frequencies differ (F9) | CpuAffinity.cpp:20-34 | If triggered: doubles the cores available to every pinned thread incl. detector + workers; else 0 | Trivial | High (mechanism), unknown (whether this board triggers) |
| 3 | Expose the blob-count knobs (min_white_black_diff, min_tag_pixels/min_cluster_pixels) in ApriltagTuning and sweep them on a real field scene (F3) | VkApriltagBackend.cpp:141-150, IApriltagBackend.h | Live CPU quad fit is 3.9 ms (d1) vs 0.29 ms on the bench: clutter-driven. Plausibly 1-3 ms of CPU tail + some GPU (labelling/sort/extents) | Low code; recall must be checked at range | Medium |
| 4 | Make annotation cheap when a viewer exists: (a) skip the extra copy when the BGR view is private (GRAY8 source); (b) emit NV12 (Y = grey plane, UV = 128) and draw in Y+UV, removing GRAY2BGR, copy and the downstream BGR->NV12; or (c) draw in the browser from the JSON (F8). Viewer-gating itself is agent 04's F3 | ApriltagDetector.cpp:385-598, Frame.cpp, WebRTCSink | (a) ~0.3-1 ms at 720p, ~1-2 ms at 1080p; (b)/(c) removes ~all of annotate-copy plus the sink's conversion | (a) ~10 lines; (b) ~100 lines; (c) UI work | High (flow), Medium (ms) |
| 5 | Pin vkapriltag/libapriltag worker pools explicitly (don't rely on inheritance from whichever thread built the backend); name the threads (F9, F14) | VkApriltagBackend ctor / WorkerPool | Removes A55 stragglers for API-created sinks; library measured +6% fps from A76 pinning | Low | Medium |
| 6 | WorkerPool: complete a batch when all *items* are done, not when every worker has checked in (F10); same issue in libapriltag v3.4.5 (+ sched_yield) | vkapriltag WorkerPool.cpp:81-102 | 0 on an idle board; up to a CFS slice (~1-3 ms) per ParallelFor under the live multi-sink load | Medium (lifetime of fn_), library change | Medium |
| 7 | Field-map-predicted ROI atlas at d1 + full frame at d2 (dual resolution), using the layout, intrinsics and odometry seed LumenCore already has (F11) | ApriltagDetector + a second fixed-size VkApriltagBackend | d1 range at ~d2 cost: est. 13.6 -> 8-9 ms at 720p, more at 1080p+ | Medium-high (300-500 lines, recall testing) | Medium |
| 8 | Allow decimation 3 at 1280-wide frames by cropping to a multiple of d (F15) | VkApriltagBackend::ResolveDecimation + detector crop | d3 vs d2 at 720p: est. GPU ~4.0 -> ~2.5 ms, tail ~1.8 -> ~1 ms | Small; range trade | Medium |
| 9 | Build flags: `-mcpu=cortex-a76.cortex-a55` for RK3588 builds; `-fno-math-errno` (or inline hypotf) for QuadDecode (F4, F16) | CMake | 0-15% of quad fit; 0-5% of other CPU code | Trivial; breaks armv8.0 targets unless per-target | Low-Medium |
| 10 | CPU backend (fallback): nthreads = pinned core count or let it use all 8 cores; bump libapriltag past v3.4.5 (F14) | CpuApriltagBackend / install-deps | Stops a CPU sink oversubscribing the A76s used by the Vulkan sink | Low | Medium |
| 11 | Multi-tag: SQPnP (as PhotonVision) and/or warm start from the previous pose; analytic Jacobian in ConstrainedPnp (F7) | ApriltagDetector.cpp:287-328, ConstrainedPnp.cpp | Sub-ms; mainly robustness | Low | Medium |

Not recommended: FramePipeline at 30-60 fps (adds a frame of latency, F13), GPU quad fit (F12), NPU/RGA/VOP2 for any
detection or overlay stage, hand-written NEON for the tail.

## Findings

### Baseline (already measured / already done - do not re-propose)

From docs/PERFORMANCE_ANALYSIS.md (Orange Pi 5 Plus, 2026-10-18, 720p MJPEG 30 fps, Vulkan sink decimation 1,
ultra-fast refine, 8 threads): Vulkan AprilTag work 13.6 ms = GPU 8.7 (upload 0.13, readback 0.03) + CPU quad fit 3.9
+ tag decode 0.4 + grey 0.3. Age 29-30 ms, of which camera 15.5 ms (USB transfer ~12 ms + MPP decode 3.9 ms).
Decimation 2: GPU 8.7 -> 4.0 ms, CPU tail 3.9 -> 1.8 ms (age 29 -> 22.6). Quad-fit threads: 2 -> 6.8 ms, 4 -> 4.9,
6 -> 4.5, 8 -> 4.2 ms. Already done: MPP HW JPEG decode, decode-to-grey, Debug-level per-frame log, cpu_threads
default 4 (VkApriltagBackend.cpp:150), V4L2 buf.timestamp + drain-to-newest, Nice=-19, RecordSink HW encoder,
host-pointer import path (inactive on Mali - no VK_EXT_external_memory_host), per-stage profiler.
Explicitly deferred: core pinning of library worker threads, FramePipeline adoption, dma-buf import.

### F1. Vulkan backend still composes GPU + CPU tail serially; FramePipeline unused
- Claim: `VkApriltagBackend::Detect` calls `GpuDetector::Detect` (blocking), then `QuadDecode::Decode`, then
  `TagDecoder::Decode` on the sink thread (LumenCore/VkApriltagBackend.cpp:218-233). No overlap of GPU frame N
  with CPU tail of frame N-1.
- Evidence: code above; PERFORMANCE_ANALYSIS.md section 4 cites library numbers 5.00 ms serial vs 3.52 ms pipelined
  at 1280x800 d2 on this SoC.
- Confidence: high (code read).
- (gain/effort: see recommendations; latency trade-off discussed in F-pipelining below)

### F2. Overlay path does two full-frame passes when the source is GRAY8, and draws at full resolution
- Claim: with decode-to-grey active, the camera frame is GRAY8. When a preview consumer is attached
  (`HasActiveFrameConsumer()`), `result.frame->AsBgr()` runs `cv::cvtColor(GRAY2BGR)` into a pooled buffer
  (LumenCore/Frame.cpp:110-113), and then ApriltagDetector copies that BGR frame again into another pooled buffer
  (`sourceBgr.copyTo(colouredFrame)`, LumenCore/ApriltagDetector.cpp:391-395) before drawing. So 1 read of W*H +
  2 writes of 3*W*H + 1 read of 3*W*H bytes per frame, purely for a preview. At 1080p that is ~2 MB in, ~12 MB
  out/in; at 720p ~0.9 MB + ~5.5 MB.
- The copy is needed only when the BGR view is shared (cached `bgrView` may be read by another sink). When the
  frame was GRAY8, the `bgrView` is a fresh per-frame conversion; drawing straight into a private GRAY2BGR output
  (cvtColor into the pooled buffer that becomes the output Frame) removes the copy entirely.
- Drawing: per tag 4 `cv::line` (thickness 2, non-AA) + `DrawTagPose` 12 projected points, 11 `cv::line` with
  `LINE_AA` (LumenCore/TagOverlay.cpp:33-40) + `cv::Rodrigues` + `cv::projectPoints`, and `cv::putText` with
  FONT_HERSHEY_SCRIPT_SIMPLEX (the most stroke-heavy Hershey font) + `getTextSize`. Cost scales with tags and line
  length, not frame area; for a handful of tags this is tens of microseconds (needs on-device confirmation via the
  existing `annotate-draw` stage). The dominating cost is the full-frame convert+copy (`annotate-copy` stage).
- Confidence: high on the data flow (code read); medium on cost magnitudes (estimate: ~0.3-1 ms for GRAY2BGR +
  copy at 720p on an A76 with NEON memcpy at several GB/s; ~1-2.5 ms at 1080p; PERFORMANCE_ANALYSIS.md section 2
  estimated 3-4 ms for a 1080p BGR copy).


### F3. Live LumenVision numbers are 2-4x the library's own bench numbers: the gap is scene clutter + d1, not the GPU
- Library bench (vkapriltag PERFORMANCE.md section 3/8h, 1280x800 corpus, d2, 1 tag, 75 candidate quads, service
  stopped, governors performance): GPU 2.12-2.67 ms, quad_decode 0.29 ms, tag_decode 0.72 ms, total 3.1-3.7 ms.
- LumenVision live (PERFORMANCE_ANALYSIS.md, 1280x720 camera, d1): GPU 8.7 ms, quad fit 3.9 ms; d2: GPU 4.0, CPU
  tail 1.8 ms. So at the same d2 and fewer pixels the live GPU is ~1.5-1.9x the bench and the CPU quad fit ~6x.
- Interpretation: a real camera scene has far more blobs (texture, edges) than the synthetic corpus, and the live
  run shared the A76s and the DDR bus with a CPU libapriltag sink (8 threads, 36 ms/frame) and an RKNN sink. Both
  GPU (labelling/sort/extents scale with blob/point count) and CPU quad fit (O(points)) scale with clutter.
- Consequence: the knobs that cut blob count before the CPU tail (min_white_black_diff, min_tag_pixels /
  min_cluster_pixels, aspect/fill gates) are worth more on the live system than the bench suggests - and LumenCore
  does not expose them (VkApriltagBackend.cpp:141-150 only sets width/height/decimation/tag_width/borders/cpu_threads;
  DetectorConfig defaults: min_white_black_diff 5, min_cluster_pixels 24, min_tag_pixels 0 - GpuDetector.h:29-58).
- Confidence: medium (inference from two different measurement setups; needs a same-scene A/B).
- Test on the Pi: run the live sink with `APRILTAG_VK_MIN_TAG_PX=<n>` (env override exists in the library) and,
  after exposing it, min_white_black_diff 5/10/15/20; read `vk-gpu`, `vk-quads`, `vk-tags` from /api/profile and the
  candidate-quad count; check recall at range on a tag at the far end of the field.

### F4. The CPU quad fit calls libm `hypotf` per boundary point - same class of cost as the `modf` that cost 25%
- Claim: `QuadDecode` peaks path computes `FitLineError` for every point of every blob (QuadDecode.cpp:313-319), and
  `FitLineError` calls `std::hypotf` (QuadDecode.cpp:79); `FitLine` calls it twice (94, 118). With kDp default
  (DESIGN.md:187, GpuDetector.h:58) the per-point loop runs only for the ~8% of blobs that fall back to peaks, but
  every blob still pays the prefix-sum pass (QuadDecode.cpp:279-295) and DP's three linear passes in double.
- The library already measured that glibc aarch64 libm calls dominate such loops: replacing `modf` with `trunc`
  gave tag_decode -25% on the Orange Pi vs -4.7% on x86 (PERFORMANCE.md section 4).
- VERIFIED: glibc's `hypotf` (sysdeps/ieee754/flt-32/e_hypotf.c, GLIBC_2.35 version,
  https://github.com/bminor/glibc/blob/master/sysdeps/ieee754/flt-32/e_hypotf.c) is
  `(float) sqrt((double)x*x + (double)y*y)` plus inf/nan handling and an errno=ERANGE check. So an inline
  `static_cast<float>(std::sqrt(double(a)*a + double(b)*b))` is bit-identical for finite inputs, and compiles to
  `fmul/fmadd/fsqrt` - no PLT call, no errno. The compiler cannot inline `hypotf` itself because of errno unless
  built with `-fno-math-errno`. (Ubuntu 22.04+/Debian 12 ship glibc >= 2.35; older glibc used a different
  algorithm - check `ldd --version` on the image.)
- Caveat on size: with the kDp default, the per-point hypotf loop only runs for blobs that fall back to peaks
  (~8% at d2 per DESIGN.md:187); every accepted DP quad still pays 4 `FitLine` = 8 hypotf. Probably a 5-15% win
  on quad fit, not 25%. Cheapest experiment: rebuild vkapriltag with `-fno-math-errno` for QuadDecode.cpp only.
- Gain: bounded by the share of quad-fit time in hypotf; the quad fit is 3.9 ms (d1) / ~1.8 ms (d2) live, so even
  10-20% is 0.2-0.8 ms. Confidence: low-medium (needs perf on the Pi).
- Test: `perf record -g` on the service during a live run, look for `__hypotf`/`__ieee754_hypotf` share under
  `quad.fit_blob`; A/B with an inlined replacement and the library's bit-identity gate.

### F5. Per-tag pose uses upstream's matd-based solver at ~0.9 ms/tag; the submodule ships a 58x faster drop-in
- Claim: ApriltagDetector calls libapriltag `estimate_tag_pose_orthogonal_iteration(..., poseIterations=50)` for
  every detection on the sink thread, serially (LumenCore/ApriltagDetector.cpp:468-473; default
  `poseIterations = 50`, IApriltagBackend.h:68). Upstream's loop runs all n_steps with no convergence exit and
  allocates through `matd_op` string-parsed expressions, `malloc` and `matd_svd` every step
  (third_party/vkapriltag/apriltags_vulkan/build/_deps/apriltag-src/apriltag_pose.c:43-117), twice per tag (both
  ambiguity solutions, lines 510 and 514).
- Evidence of cost: vkapriltag README.md:365-369 measured on the Orange Pi 5: upstream `estimate_tag_pose` 0.916 ms
  per tag vs the library's `apriltag_vulkan::PoseEstimator` 0.016 ms per tag (58x), same algorithm (homography seed,
  orthogonal iteration, Schweighofer-Pinz second minimum), fixed-size 3x3 maths, early exit at tol 1e-8
  (PoseEstimator.h:44-91). `EstimateBoth()` returns both solutions + errors, exactly what LumenCore needs for
  `altPose`/`poseAmbiguity`. It is already compiled and linked (LumenCore/CMakeLists.txt:226 links
  vkapriltag::vkapriltag) but unused by LumenCore.
- Gain on RK3588: ~0.9 ms per visible tag on the critical path -> ~0.02 ms. With 2-4 tags in view (typical FRC
  2024-2026 field views) 1.8-3.6 ms saved per frame, more with 6-8 tags (a CPU-only build also benefits). The
  2026-10-18 profile shows little pose time only because the live scene/calibration did not exercise it (13.3 of the
  13.6 ms work is accounted for by gpu+quads+tags+grey) - UNVERIFIED how many tags/whether calibrated.
- Confidence: high (two measurements in-tree + code read).
- Effort/risk: low-medium. ~40 lines in ApriltagDetector; PoseEstimator takes no distortion, so keep LumenCore's
  undistortPoints step and pass undistorted corners plus a homography recomputed from them (see F6). Results are
  tolerance-equal, not bit-equal, to upstream (DESIGN.md:401-406: early exit, double-precision scale). Gate with the
  library's `tools/validate_pose` ladder and an A/B of published poses on recorded frames.
- Test on the Pi: tag board with 4-8 tags at 1-5 m, calibrated camera; compare the `pose` stage in /api/profile
  before/after, and the pose deltas (expect < 1e-6 m / rad when both converge).

### F6. Pose seed comes from the distorted homography (minor correctness/convergence issue)
- Claim: LumenCore copies the detection, overwrites `p[]` with undistorted corners, but leaves `det->H` (computed by
  the decoder from distorted corners) (ApriltagDetector.cpp:451-466). Upstream seeds orthogonal iteration from
  `info->det->H` (apriltag_pose.c:458). So the seed and the corners disagree whenever distortion is non-zero. With
  50 fixed iterations it usually converges anyway; with early exit or fewer iterations (poseIterations is a tuning
  knob) a worse seed can mean more iterations or the wrong ambiguity basin (DESIGN.md:406 notes even a 1e-7 seed
  perturbation can switch basins occasionally).
- Fix: recompute H from the 4 undistorted corners (libapriltag `homography_compute2` / a 4-point DLT, microseconds).
- Confidence: high on the code fact, low on the practical impact (depends on lens distortion; wide-angle FRC
  cameras such as OV9281 2.8 mm lenses have strong barrel distortion).

### F7. Multi-tag solve: `cv::solvePnP` default (SOLVEPNP_ITERATIVE, no initial guess) - cost and robustness
- Claim: SolveMultiTagPnP calls `cv::solvePnP(objectPoints, imagePoints, K, dist, rvec, tvec)` with the default
  flag (ApriltagDetector.cpp:295) on all corners of tags with field poses; then `projectPoints` for the error.
  SOLVEPNP_ITERATIVE without a guess initialises with DLT (non-planar points, >= 6 points) or a homography
  (planar, i.e. all tags on one wall) followed by Levenberg-Marquardt (OpenCV docs). For 8-32 points this costs on
  the order of 0.05-0.3 ms on an A76 - UNVERIFIED on the Pi. This matches PhotonVision's MultiTag approach in
  spirit (PhotonVision uses SQPnP for multi-tag in recent versions - verify).
- Not a large time sink; the interest is robustness (SQPnP is globally optimal and non-iterative-seed-free) and the
  option to warm-start from the previous frame's pose (`useExtrinsicGuess=true`), which reduces LM iterations.
- The constrained solve (ConstrainedPnp.cpp) uses a numeric central-difference Jacobian: 6 residual evaluations per
  iteration, each running `cv::Rodrigues` + `cv::projectPoints` (with Mat allocation), up to 60 iterations and up to 12
  damping attempts (ConstrainedPnp.cpp:70-128). For 3 unknowns and <= ~32 points it is still sub-millisecond
  (estimate), but it allocates heavily; an analytic Jacobian or `cv::projectPoints` with its jacobian output would
  cut evaluations 6x. Low priority.
- Confidence: medium (code read; costs estimated).
- PhotonVision, with multi-target on, skips per-tag camera-to-target solves by default and derives them from the
  multi-tag result (https://docs.photonvision.org/en/latest/docs/apriltag-pipelines/multitag.html). LumenCore has the
  `singleTagPose` knob for the same; after F5 the per-tag solve costs ~16 us, so this stops mattering.

### F8. Annotation: once viewer-gating exists, the remaining cost is the colour frame itself - avoid making one
- Cross-reference: agent 04 (04-encode-stream-record.md F3) verified that `wantsFrame` is "a frame sink is toggled
  on", not "someone is watching", so annotation runs with zero viewers. Gating on real viewers is their
  recommendation; not repeated here. This finding is about the cost *when someone is watching*.
- Today's per-frame chain for an annotated Vulkan-sink preview of a grey (MPP decode-to-grey) camera:
  GRAY8 -> `AsBgr()` GRAY2BGR full-res (Frame.cpp:110-113) -> pooled copy (ApriltagDetector.cpp:391-395) -> draw ->
  WebRTCSink `cv::resize` INTER_AREA if scaleDivisor > 1 (WebRTCSink.cpp:271-273) -> RGA BGR->NV12 (+ memcpys per
  04 F4) -> encoder. Bytes touched per 720p frame: ~0.9 MB grey read, 2.8 MB BGR write, 2.8 MB read + 2.8 MB write
  (copy), 2.8 MB read for resize/RGA, NV12 1.4 MB. ~13 MB of DDR traffic and 2-3 full-frame CPU passes on the
  detector's critical path for a debug view.
- Options, cheapest/best first:
  1. **Client-side overlay (browser).** The detector already publishes corners, centre, id and pose JSON. Draw the
     quad outline, id and axes in a `<canvas>`/SVG over the `<video>` in the web UI and forward the *unannotated*
     grey frame (the same `Frame`, zero-copy). Detector frame work drops to ~0. Sync: WebRTC video is delayed by
     encode + jitter buffer (~30-100 ms) relative to JSON; align by capture timestamp using
     `HTMLVideoElement.requestVideoFrameCallback` metadata (`rtpTimestamp`/`captureTime`) or buffer JSON by
     timestamp. Risk: UI work; overlay could lag/lead slightly if not synced.
  2. **Draw in NV12 directly, no BGR.** For a GRAY8 source, build the output as NV12: Y = the grey plane (one
     memcpy, 0.9 MB at 720p, or zero-copy if the encoder path accepts a separate Y pointer), UV = memset(128) (0.46 MB).
     Draw each line twice: `cv::line` on the Y plane (CV_8UC1) with the colour's luma, and on the half-resolution UV
     plane viewed as CV_8UC2 with Scalar(U,V) at half coordinates. `putText` likewise. Then WebRTCSink skips its
     BGR->NV12 conversion (RGA or sws) entirely. Estimated detector cost at 720p: ~0.1-0.2 ms (memcpy+memset at
     ~5-10 GB/s on an A76) vs the current GRAY2BGR + copy (~0.5-1.5 ms, UNVERIFIED - read `annotate-copy` in
     /api/profile). Also removes the RGA step downstream (agent 04 F4). Needs `FrameFormat::NV12` support in
     WebRTCSink's encode path (Frame already knows NV12, Frame.cpp:69-73).
  3. **Draw after downscale.** If the consumer's scaleDivisor is d > 1, the detector could produce the frame at
     1/d (downscale the grey plane first with `cv::resize` INTER_AREA on 1 byte/px, or RGA), then convert/draw with
     coordinates scaled by 1/d: d=2 cuts every later pass 4x. Requires the detector to know the consumer's scale
     (add it to the FrameConsumer registry next to requiresColor).
  4. Avoid the double pass even in the BGR case: when the source is GRAY8 the `bgrView` is produced for this frame
     only; cvtColor straight into the pooled output buffer and skip the copy (keeping the copy only when the BGR
     view could be shared with another consumer, i.e. a BGR24 source).
- Drawing primitives themselves: per tag 4 `cv::line` (thickness 2), 11 `LINE_AA` lines from `DrawTagPose`
  (TagOverlay.cpp:34-40, plus `cv::Rodrigues` + `cv::projectPoints` with distortion), and `putText` with
  FONT_HERSHEY_SCRIPT_SIMPLEX (the most stroke-heavy Hershey face). Cost is O(line length x thickness), independent
  of frame area: estimated tens of us per tag (UNVERIFIED - the `annotate-draw` stage records it per tag). Cheaper
  but cosmetic: FONT_HERSHEY_SIMPLEX, LINE_8 for the axes. Not worth optimising before options 1-3.
- Rejected for this stage: RGA (it can blit/fill/blend rectangles and alpha-composite a full ARGB layer, but has no
  line primitive, and compositing a full-frame overlay layer costs more than drawing a few lines with the CPU);
  Vulkan drawing (needs a colour image on the GPU and a readback, far more than the work saved); RK3588 VOP2 hardware
  overlay planes (display-output only - they compose to HDMI/DSI, not into an encoder input).
- Confidence: high on data flow; medium on magnitudes. Effort: option 4 ~10 lines; option 2 ~100 lines
  (detector + WebRTCSink NV12 input); option 1 UI work + a timestamp in the JSON (already has captureTimeUs).

### F9. Threads and affinity: library workers inherit whatever affinity/nice the building thread had
- Linux `pthread_create` threads inherit the creator's CPU affinity mask and nice value (sched_setaffinity(2),
  pthread_create(3) man pages). vkapriltag's `WorkerPool` threads (QuadDecode "quad-worker" and TagDecoder
  "tag-worker", each `cpu_threads - 1` workers) are created in their constructors and never pin themselves (no
  affinity calls anywhere in library/src - grep).
- So the workers' placement depends on which thread built the backend: built in `Process()` on the sink thread
  (frame size unknown, e.g. every restored sink: SinkManager.cs RestoreApriltagSink passes 0,0) -> inherits the A76 pin
  from ISink::ProcessingThreadLoop (ISink.cpp:71); built in the constructor when the API passed a frame size
  (`AddApriltagSinkWithBackend` with width/height) or on Apply Tuning from an ASP.NET thread -> unpinned, free to
  land on the A55s. This is the same inheritance mechanism PERFORMANCE_ANALYSIS.md found for nice -19.
- Two pools of 3 extra workers each at the default 4 threads = 6 parked threads per Vulkan sink; they never run
  concurrently (quads then tags), so this is only wake-up cost, not contention.
- RK3588 A76 cores are two clusters (cpu4-5, cpu6-7) with separate cpufreq policies; `CpuAffinity` picks only the
  highest `cpuinfo_max_freq` tier (CpuAffinity.cpp:20-34). If the two A76 clusters report different maxima (board
  binning; UNVERIFIED for this board), "performance cores" would be just 2 cores. Check:
  `cat /sys/devices/system/cpu/cpu{4..7}/cpufreq/cpuinfo_max_freq`.
  **Evidence that this happens on real RK3588 boards:** each A76 cluster gets its own PVTM bin and the upstream OPP
  table masks 2256/2304/2352/2400 MHz per bin (linux-rockchip "Add OPP data for CPU cores on RK3588",
  https://lists.infradead.org/pipermail/linux-rockchip/2024-May/046927.html); a ROCK 5B showed policy4 topping at
  2304000 and policy6 at 2352000, a Khadas Edge2 2256/2304 (Radxa forum "ROCK 5B Debug Party",
  https://forum.radxa.com/t/rock-5b-debug-party-invitation/10483). So on a board whose clusters bin differently,
  `CpuAffinity` pins *every* capture and sink thread (and, by inheritance, vkapriltag/libapriltag workers) to
  only 2 cores, leaving 2 A76s idle. Fix: group by core type (MIDR / `/sys/devices/system/cpu/cpuN/regs/identification/midr_el1`
  or `cpu_capacity`), or treat any frequency within ~10% of the maximum as the same tier. HIGH impact if it
  triggers, trivial fix. (Shared with agent 05's scope; flagged here because the detector is the biggest consumer.)
- Measured scaling (PERFORMANCE_ANALYSIS.md): quad fit 6.8/4.9/4.5/4.2 ms at 2/4/6/8 threads. 4 -> 8 gains only
  0.7 ms, which is what one would expect if threads 5-8 are A55s (~2-2.5x slower per thread for this scalar double
  code) or time-sharing A76s with other sinks.
- Recommendation: pin pools explicitly (WorkerPool option or set the affinity of the building thread before
  constructing the backend, then restore); keep 4 threads on A76s; consider giving the NON-critical work (overlay,
  JSON, NT publish, WebRTC/record sinks) to the A55s instead of pinning every sink to the A76s - that frees A76
  time for the detector's critical path. Overlap with agent 05's thread-model scope: only the AprilTag-specific part
  is here.
- Confidence: high on the inheritance fact; medium on its live impact (depends on how sinks were created).
- Test: the pool threads have no OS name (`prof::SetThreadName` only labels the profiler buffer,
  Profiler.cpp:131-135; no pthread_setname_np), so list them with `ps -eLo pid,tid,psr,ni,comm -p <pid>` and
  `taskset -p <tid>` for each; compare a sink created via the API vs one restored at boot. A one-line
  `pthread_setname_np` in WorkerMain would make this and `perf`/`htop` attribution easy.

### F10. WorkerPool join waits for *every* worker to wake, even ones that get no work - a straggler tax under load
- Claim: `ParallelFor` sets `outstanding_ = workers_.size()` and the caller blocks until each worker has woken,
  drained (possibly zero items) and decremented (WorkerPool.cpp:81-102, 63-68). So each `ParallelFor` (two per
  frame: quads, tags) completes no earlier than the *slowest worker's wake-up*, even when the caller and the other
  workers already finished all items.
- Why it matters on the live system: the A76s are shared by capture threads, every pinned sink thread, the CPU
  AprilTag sink's 8 libapriltag workers and RKNN post-processing (PERFORMANCE_ANALYSIS.md 2026-10-18 setup). A
  runnable-but-not-running worker can wait a CFS slice (~0.75-3 ms with default sched_latency/min_granularity on 4
  busy cores) before it gets the CPU just to find nothing left. The library's own tracer even has a scope for it
  (`pool.join_wait`, WorkerPool.cpp:96) and a "how late it woke" arg (line 40).
- Plausibly part of why the live quad fit (3.9 ms) is so far above the bench (0.29 ms) and why 4->8 threads gains
  little. UNVERIFIED: needs the tracer or `perf sched` on the Pi.
- Fix (library, ~30 lines): count completed *items* instead of checked-in workers; the caller returns when
  `done_items == count`; late workers must not dereference `fn_` of a finished batch (guard with a batch generation
  check under the mutex, or keep `fn` alive in a shared_ptr per batch). Alternatively use fewer, pinned threads
  so they are never descheduled (F9).
- Gain: 0 to several ms per frame under contention, ~0 on an idle board. Confidence: medium (mechanism verified in
  code, impact unmeasured).
- Test: `perf sched record -- sleep 10` during a live run; `perf sched latency` for the worker tids; or build with the
  library tracer and look at `pool.join_wait` on the Pi with all three sinks running vs only the Vulkan sink.

### F11. ROI search predicted from the field map: LumenCore already has every input; can be done without library changes
- Prior art (do not redo): vkapriltag research/02-algorithmic-speedups.md:148-234 and 00-SUMMARY D2/D3 already
  established (a) aligned axis-aligned crops are bit-identical per found tag (crop origin multiple of 4*d source
  px, >= 1 threshold tile margin, refine/decode on full-res pixels), (b) EagleEye (FRC 2025-26, Pi 5) measured ROI-only
  frames 3.48 ms vs 20.6 ms full-frame (~6x), predicting ROIs by projecting *all field tags* from the last camera
  pose (catches tags entering view), with same-frame full-frame fallback when all ROIs are empty
  (https://www.chiefdelphi.com/t/eagleeye-photonvision-alternative-with-120-apriltag-fps-on-raspi-5/523976,
  https://github.com/Scythe-Engineering/EagleEye-Vision-System), (c) atlas packing with constant grey gutters
  (threshold to 127, never joins) so the GPU fixed cost (~560 us) is paid once.
- New here (LumenCore integration): ApriltagDetector already holds the field layout (`m_FieldLayout`), camera
  intrinsics + distortion, the multi-tag camera pose of the previous frame, and even an odometry seed
  (`SetConstrainedSeed`: robot x, y, yaw + robotToCamera, ApriltagDetector.cpp:83-91), which predicts *this* frame's
  pose better than last frame's vision pose. So ROI prediction = project the 4 corners of every layout tag that faces
  the camera with `cv::projectPoints` (32 tags x 4 corners, ~20 us), pad, align, merge.
- Library-free implementation: build a second `VkApriltagBackend`/GpuDetector at a fixed atlas size (e.g. 640x640
  or 1024x512 at d1), memcpy the crops into it (each row a memcpy; 4 crops of 200x200 = 160 KB, ~20 us), run Detect
  on the atlas, offset the corners back. Decode/refine sample the atlas, which holds the same pixels, so results
  equal a full-frame d1 run per tag given enough margin (refine's search radius + 1 tile). The pose step then uses
  frame coordinates as today.
- Best FRC use: **dual resolution**: full frame at d2 every frame (finds near/large and new tags; GPU 4.0 ms + CPU
  1.8 ms live at 720p) + d1 atlas only around predicted far tags. Gets d1 range at ~d2 cost: live d1 = 13.6 ms work
  vs estimated d2 + small atlas ~ 4.0 + 1.8 + (0.56 + 3.3 us/kpx * 410 kpx ~ 1.9) + small CPU tail ~ 8-9 ms, about a
  third off the detector's latency (estimate from the in-tree cost model, UNVERIFIED on live scenes). On a 1080p or
  1600x1200 camera the saving grows with frame area.
- Risks: recall for tags outside predictions (mitigated by the d2 full-frame pass), duplicates from the two passes
  (reconcile by id, prefer the d1 detection), more code paths to test.
- Confidence: medium (prior art solid; gains are model estimates). Effort: medium (~300-500 lines in LumenCore).
- Test: record a match-like drive with FrameRecord/RecordSink, replay through ImageFileSource/VideoFileSource,
  compare detected IDs and corner deltas (expect < 1e-6 px for tags found in both) and the `age` stage.

### F12. Moving the quad fit to the GPU: only interesting at decimation 1, not a priority
- At d1 live the CPU quad fit is 3.9 ms vs GPU 8.7 ms; at d2 it is ~1.4 ms of the 1.8 ms CPU tail. frc971's CUDA
  detector does peak NMS + quad fit on the GPU (research/01-fiducial-implementations.md:105), but with fp32 maths
  that is not bit-exact. Mali-G610 exposes no native fp64 (UNVERIFIED for libmali g24p0; Valhall has no fp64
  units), so a bit-exact port of the int64/double moments is impractical, and the Mali is already the slower half.
- A cheaper middle ground already exists: the DP corner seeding (default kDp) removes the C(10,4) search. What
  remains on the CPU per blob is the prefix-moment pass plus 3 linear passes - all memory-bound over the readback
  buffer. Reducing *blob count* (F3 knobs, F11 ROIs) shrinks it more safely than porting it.
- Verdict: rejected for now; revisit only if d1 full-frame stays the deployed mode after F3/F11.

### F13. FramePipeline (GPU N || CPU N-1) does not help this pipeline's latency; keep serial for 30-60 fps cameras
- The library is explicit: throughput, not latency, +1 frame of delay (PERFORMANCE.md section 5:463-483); neutral at
  640x400 on the Pi due to DDR contention; 21% regression on Mali on an older tree. At 30 fps the camera interval
  (33 ms) far exceeds the detector's 13.6 ms (d1) / ~6 ms (d2) work, so the detector is never the throughput limit;
  adopting it would add ~33 ms of age to every result. Only useful for >= 90-120 fps cameras where work > interval.
- What *does* overlap usefully for latency is CPU work that does not depend on this frame's detections during the
  ~8.7 ms fence wait: the annotation frame preparation (F8 option 2/4) and publishing of the previous frame's
  derived outputs. With F8 implemented there is little left, so this is a minor point.
- Confidence: high.

### F14. CPU backend (fallback): 8 libapriltag workers squeezed onto the pinned A76s, with sched_yield per task
- libapriltag (pinned v3.4.5) creates its workerpool lazily inside `apriltag_detector_detect` (apriltag.c:1113-1115),
  i.e. on the A76-pinned sink thread, so all `nthreads` workers inherit the A76 mask. With the measured setting of
  8 threads that is 2x oversubscription of 4 cores (or 4x if F9's 2-core case applies), and every worker calls
  `sched_yield()` after claiming each task (common/workerpool.c:84) and the pool waits for all workers to check in
  (end_count) - the same straggler pattern as F10.
- The live CPU sink measured 36 ms/frame at 720p with 8 threads (PERFORMANCE_ANALYSIS.md 2026-10-18), over the
  33 ms interval. Recommendation: for the CPU backend either nthreads = number of pinned cores (4), or explicitly
  give its workers all 8 cores (libapriltag's stages are data-parallel and A55s contribute throughput). The vkapriltag
  research already recommends bumping libapriltag past v3.4.5 to drop the sched_yield (00-SUMMARY C7).
- Gain: UNVERIFIED; expect a CPU sink at 4 pinned threads to be no slower than at 8 and to stop stealing A76 time from
  the Vulkan sink. Confidence: medium. Test: thread sweep 2/4/6/8 with the Vulkan sink running, watch both sinks'
  `age`.

### F15. Decimation 3 is impossible at 1280x720/1280x800 only because the GPU wants exact divisibility
- `ResolveDecimation` steps the request down until both dimensions divide (VkApriltagBackend.cpp:13-21): 1280 % 3 != 0,
  so a request for 3 silently becomes 2 at 720p and 800p (it works at 1920x1080 and 1600x1200). Upstream libapriltag
  floors instead (the last partial block is dropped).
- The detector could build the GPU pipeline for floor(W/d)*d x floor(H/d)*d (1278x720 at d3) and feed a cropped
  view: losing <= 2 border columns costs nothing for tags. On the Mali the frame is uploaded by copy anyway (no
  host import, PERFORMANCE_ANALYSIS.md vkapriltag 1.7.0 findings), so the non-contiguous view only costs the existing
  repack (VkApriltagBackend.cpp:206-213, ~0.15 ms memcpy at 720p) unless the upload learns a row stride.
- Why it matters: d3 sits between d2 (range) and d4 (2.1x faster than d2, PERFORMANCE.md section 3). GPU estimate
  at 720p d3 vs d2: decimated pixels 102k vs 230k; with the measured live d1->d2 scaling (8.7 -> 4.0 ms) d3 would be
  ~2.3-2.6 ms GPU, CPU tail ~1 ms. UNVERIFIED. Combined with the FRC tag size (6.5 in = 165 mm), range loss vs d2
  should be measured (a 36h11 tag needs ~10 decimated px across - UNVERIFIED rule of thumb).
- Effort: small (detector-side crop). Confidence: high on the mechanism.

### F16. NEON / compiler flags: nothing targets the A76; cheap, small, broad
- No `-mcpu`/`-march`/`-mtune` anywhere in LumenCore's or vkapriltag's CMake, or in scripts/install-deps.sh (grep), so
  aarch64 builds use GCC's generic armv8-a baseline: no inline LSE atomics (outline-atomics helpers instead), no
  armv8.2 fp16/dotprod/rcpc, generic scheduling. `-mcpu=cortex-a76.cortex-a55` (GCC big.LITTLE tuning) is valid on
  both RK3588 core types.
- Where it could matter in this stage: the WorkerPool atomics/mutex hand-offs (LSE), the scalar double loops in
  QuadDecode/RefineEdges (scheduling only; the library measured refine as memory-latency bound, so expect little),
  OpenCV's draw/convert code is already runtime-dispatched NEON. Expected gain: 0-5% of the CPU tail - UNVERIFIED.
- Risk: binaries no longer run on armv8.0 cores (Raspberry Pi 4 / A72). If the .deb is meant for those too, build
  per-target or use GCC `target_clones` on the few hot functions.
- Hand-written NEON for the CPU tail: rejected for now - the exact refine is scalar double with a fixed operation
  order (2 double lanes on NEON, PERFORMANCE.md 8e), the quad fit's prefix sums are a serial dependency chain, and
  the threshold/decimate stages already run on the GPU. The live CPU tail is dominated by *how many* blobs/quads
  reach it (F3), not by per-item arithmetic.
- Combine with `-fno-math-errno` on QuadDecode.cpp (F4) - that one has a concrete mechanism.

## Ideas rejected (and why)

- **FramePipeline for latency** (F13): +1 frame (33 ms at 30 fps) of age to save ~30% throughput the camera does not
  need. Keep for >= 90 fps cameras only.
- **NPU (RKNN) for any detection stage or as an ROI proposer**: YOLO-class models cost 13-26 ms on this NPU
  (PERFORMANCE_ANALYSIS.md RKNN numbers; vkapriltag 00-SUMMARY section 2), more than the whole Vulkan GPU phase at d2.
- **GPU quad fit** (F12): Mali is the slower half already, fp64/int64 exactness hard on Valhall; reduce blob count
  instead.
- **RGA / VOP2 overlay planes / Vulkan for drawing the annotations** (F8): no line primitive (RGA), display-only
  (VOP2), readback cost (Vulkan). Client-side or NV12 drawing is cheaper.
- **Spinning on the fence / moving GPU IRQs / Mali always_on**: measured no effect by the library (PERFORMANCE.md 8e,
  8i).
- **KLT optical-flow corner tracking between detections**: corners not ID-verified or exact; vkapriltag research
  rejected it (02-algorithmic-speedups.md:185). Field-map ROIs (F11) give the speed without the unverified poses.
- **Hand NEON of refine/quad fit** (F16): wrong bottleneck.
- **More CPU-tail threads (>4)**: measured < 1 ms gain 4 -> 8 (PERFORMANCE_ANALYSIS.md); the extra threads land on
  A55s or contend.

## Open questions

- How many tags, and was the camera calibrated, during the 2026-10-18 live profile? (decides whether F5's pose cost
  was simply absent there). Read the `pose` stage (per-tag samples) and `tags+solve` with 4+ tags in view.
- Do policy4 and policy6 report the same `cpuinfo_max_freq` on the deployed Orange Pi 5 Plus / 5 / 5 Max boards?
  (F9: if not, everything is pinned to 2 cores.)
- Live candidate-quad and blob counts per frame at d1 vs d2 on a real field scene (F3) - the library's validator
  prints them; LumenCore does not surface them.
- `pool.join_wait` / `perf sched latency` for the vkapriltag workers on the loaded board (F10).
- Mali-G610 (libmali g24p0) `shaderFloat64` - not found in public gpuinfo data (F12; low importance).
- Does the deployed image's glibc >= 2.35 (F4 bit-identity argument)?
- `annotate-copy` and `annotate-draw` live values at 720p/1080p with a viewer attached (F8 magnitudes).
- Multi-tag `cv::solvePnP` ITERATIVE vs SQPnP (PhotonVision uses SQPNP for multi-tag and IPPE_SQUARE for single tags,
  https://docs.photonvision.org/en/latest/docs/apriltag-pipelines/multitag.html and photonlibpy estimateCamPosePNP)
  - timing on the A76 and agreement on recorded frames (F7).
