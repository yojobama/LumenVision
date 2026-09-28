# Performance Analysis — FHD MJPEG 30fps → vkapriltag → 60ms latency

**Date:** 2026-09-26
**Target:** Orange Pi 5 Plus (RK3588: 4× Cortex-A76 + 4× Cortex-A55, Mali-G610), image `v0.0.0-imagetest13`
**Measured symptoms:** ~12ms MJPEG decode per 1920×1080 frame; ~60ms end-to-end `latencyMs` for a vkapriltag pipeline (should be ≤ ~30ms, ideally ~20ms).

**Status (2026-09-26, same day):** items 1-6 addressed, verified against the real board where the
analysis itself needed correcting - #1's "no libjpeg-turbo" theory was wrong (2.1.5 is present;
the fix is decode-to-gray instead, item 2), and a real, previously-unsuspected contention source
was found live on the board (RecordSink hardcoded software libx264 even with a working hardware
encoder). Core pinning (item 6's second half) is explicitly deferred, not done. Item 7 (MJPEG
fallback base64) stays open - lowest priority, doesn't affect `latencyMs`. See the git log for the
exact commits (per-frame log level-gating, RecordSink hardware encoder, MJPEG/YUYV decode-to-gray
plus a new hardware JPEG-decode path via rockchip_mpp, vkapriltag thread-count default, honest
V4L2 timestamps + drain-to-newest).

`latencyMs` ([NetworkTablesSink.cpp](../LumenCore/NetworkTablesSink.cpp#L464-L471)) measures `captureTimeUs → producedTimeUs`, i.e. everything from V4L2 dequeue to the detector publishing. It does **not** include NT4 network time — the 60ms is entirely in-process. Below is every cost found in that window, in pipeline order.

---

## 1. Where the 12ms MJPEG decode goes

[V4l2CameraBackend.cpp](../LumenCore/V4l2CameraBackend.cpp#L300-L315) — `Grab()` decodes MJPEG with `cv::imdecode(..., cv::IMREAD_COLOR)` on the capture thread, every frame, unconditionally.

Findings:

- **12ms for 1080p MJPEG is consistent with OpenCV built without libjpeg-turbo** (or a non-SIMD build). With libjpeg-turbo on an A76 this should be ~5–8ms. *Verify on-device:* `ldd` / `cv::getBuildInformation()` for `JPEG: libjpeg-turbo`.
- **The decode is always to BGR**, but the apriltag detector only needs gray. `cv::IMREAD_GRAYSCALE` skips chroma upsampling + colour conversion entirely (~40% less work) — and currently the BGR→gray conversion is paid *again* downstream (§2).
- **The RK3588 has a hardware JPEG decoder (MPP).** Hardware 1080p MJPEG decode is ~2–3ms. *(Update: implemented and confirmed working on real hardware - see [MppJpegDecoder.cpp](../LumenCore/MppJpegDecoder.cpp), used by `V4l2CameraBackend::Grab()` with a software `cv::imdecode` fallback.)*
- The decode happens **after** `captureTimeUs` is stamped, so all 12ms count toward `latencyMs` (§5 has a related timestamping issue).

## 2. Double colour conversion per frame

[ApriltagDetector.cpp](../LumenCore/ApriltagDetector.cpp) `Process()`:

1. Camera decodes MJPEG → BGR (12ms, §1).
2. `result.frame->AsGray()` converts BGR → GRAY with `cv::cvtColor` ([Frame.cpp](../LumenCore/Frame.cpp#L46-L56)) — another ~2–3ms at 1080p, single-threaded.
3. If a preview sink is bound (`wantsFrame`), the already-have BGR is **copied in full** into a pooled buffer (~3–4ms at 1080p) for annotation.

The gray image the detector actually uses is derived from a full-colour decode that was only needed for the optional preview. Fix: decode MJPEG directly to grayscale when no frame consumer needs colour (the `FrameConsumer` registry already exists for exactly this kind of decision), or keep the compressed JPEG in `Frame` and decode lazily in `AsGray()`/`AsBgr()`.

## 3. Per-frame disk + console I/O in the detector hot path ⚠️

[ApriltagDetector.cpp](../LumenCore/ApriltagDetector.cpp) `Process()` calls, **every frame, per detector**:

```cpp
m_Logger->EnterLog("detecting apriltags using backend=" + m_Backend->Name());
```

[Logger.cpp](../LumenCore/Logger.cpp#L18-L24) `EnterLog` then does, per call:

- heap-allocates a `Log`, takes a **process-wide recursive mutex shared by every component**,
- calls `FlushLogs()`, which **opens `LumenVision.log` with a fresh `std::ofstream`, appends, and closes it** — 2+ syscalls plus SD-card/eMMC write latency, per frame,
- writes to `std::cout` with `std::endl` (**flush** per frame) — blocking if stdout is a slow pipe (journald/SSH session).

At 30fps this is 30 file open/write/close cycles per second per detector, serialized against every other thread that logs anything, on SD-card-class storage. This is a prime suspect for the jitter between the theoretical ~30ms and the observed 60ms. (Incidental bug: `FlushLogs()` writes `m_Logs.back()` then pops — the file receives entries in **reverse** order.)

**Fix:** delete or level-gate the per-frame log line; make `Logger` queue entries and flush on a background thread or every N messages; drop the per-call `std::endl` flush.

## 4. vkapriltag is run serially, with the wrong thread count for big.LITTLE

[VkApriltagBackend.cpp](../LumenCore/VkApriltagBackend.cpp) `Detect()`:

```cpp
m_GpuDetector->Detect(...);                        // GPU, blocking (fence waits, 4 submits)
auto quads = m_QuadDecode->Decode(...);            // CPU tail
return m_TagDecoder->Decode(quads, ...);           // CPU tail
```

- The library's own measurements on this exact SoC ([PERFORMANCE.md](../third_party/vkapriltag/apriltags_vulkan/PERFORMANCE.md)): **5.00ms serial vs 3.52ms with `FramePipeline`** at 1280×800 decimation 2. `FramePipeline` (GPU frame N overlapping CPU tail of frame N−1) exists in the library ([FramePipeline.h](../third_party/vkapriltag/apriltags_vulkan/library/include/vkapriltag/FramePipeline.h)) but is **not used** — VkApriltagBackend composes the three stages serially.
- Scaling the library's model to 1920×1080: GPU ≈ 560µs + 3.3µs/1k px ≈ **7.4ms** + ~1.3ms CPU tail ≈ **9ms serial**. So detection itself should be ~9ms, not 60.
- **Threading mismatch:** `config.cpu_threads` defaults to `hardware_concurrency()` = **8**, so the QuadDecode/TagDecoder worker pool spawns 8 threads. LumenCore pins *its own* capture/sink threads to the 4 A76 cores ([CpuAffinity.cpp](../LumenCore/CpuAffinity.cpp)), but the library's pool threads are **unpinned** — the scheduler is free to park them on the 4 slow A55s (~2.5× slower) or timeshare the A76s against the pinned capture-decode thread, the detector thread, and the H264 encoder thread. Straggler A55 scheduling on the CPU tail directly lengthens the critical path.

**Fix:** pass `nthreads=4` for the vkapriltag backend; pin the library's worker threads to the A76 set (or at least exclude A55s); adopt `FramePipeline` for the ~30% throughput overlap win (note: pipelining adds one frame of latency in exchange for throughput — for lowest latency, keep serial but fix threading; for headroom at 30fps+, pipeline).

## 5. V4L2 queue can hide stale frames; timestamps are optimistic

[V4l2CameraBackend.cpp](../LumenCore/V4l2CameraBackend.cpp):

- `BUFFER_COUNT = 4` driver buffers. If any downstream stage ever runs slower than 33ms, up to 3 frames (~100ms at 30fps) queue in the driver, and `DQBUF` returns the **oldest**. The pipeline never drains the queue to the newest frame.
- `captureTimeUs` is stamped at `DQBUF` return time, **not** from `buf.timestamp` (the driver's CLOCK_MONOTONIC capture instant). This makes `latencyMs` *understate* real latency whenever frames queue — the 60ms you see can co-exist with an older frame actually being processed.

**Fix:** stamp from `buf.timestamp` (with a one-time offset mapping to the wall clock); when a newer frame is already queued, DQBUF-and-discard until the queue is drained (classic low-latency V4L2 pattern); consider `BUFFER_COUNT = 2`.

## 6. Everything heavy is pinned to the same 4 cores

`CpuAffinity::PinCurrentThreadToPerformanceCores()` runs on **every** capture thread and **every** sink processing thread. On a previewing vkapriltag pipeline the 4 A76s simultaneously carry:

- capture thread: 12ms/frame MJPEG decode,
- detector thread: gray conversion + GPU submit/wait + copy/annotate,
- vkapriltag CPU tail pool (unpinned, lands wherever),
- WebRTCSink's H264 encoder ([WebRTCSink.cpp](../LumenCore/WebRTCSink.cpp#L172-L240)) — if this is x264 `ultrafast` at 1080p it's ~20–35ms/frame of pure A76 time; if it's `h264_rkmpp` it's nearly free. *Verify on-device which encoder the profile selects.*

12ms (decode) + ~9ms (detect) + ~3ms (gray) + ~4ms (copy/annotate) ≈ 28ms of critical path is already at the edge of the 33ms frame budget; any encoder contention or A55 straggler pushes a frame over and the next frame starts late — this is exactly how ~30ms of work becomes a 60ms reading.

**Fix:** verify the preview uses `h264_rkmpp` (RK3588 VPU) rather than x264; consider partitioning instead of blanket pinning (e.g. capture+encode on 2 A76s, detector on the other 2), or leave the encoder thread unpinned.

## 7. MJPEG preview path is doubly wasteful (fallback only, but notable)

- [MjpegSink.cpp](../LumenCore/MjpegSink.cpp): `cv::imencode` per frame (~10–15ms at 1080p) → **base64-encodes** the JPEG (+33% bytes, ~1–2ms) → SWIG-marshals a string → [MjpegStreamModule.cs](../Server/HttpModules/MjpegStreamModule.cs#L45-L52) immediately `Convert.FromBase64String`s it back. The base64 round trip buys nothing.
- The server polls the sink at a fixed `PollInterval = 100ms` — preview is hard-capped at 10fps and gains up to 100ms of additional latency independent of the actual frame rate. (Preview-only; doesn't affect `latencyMs`.)

---

## Expected budget after fixes (1080p30, vkapriltag)

| Stage | Now | After |
|---|---|---|
| MJPEG decode | 12ms (CPU, colour) | ~2–3ms (MPP HW) or ~5–7ms (turbo, direct-to-gray) |
| BGR→gray | ~2–3ms | 0 (decode-to-gray) |
| Per-frame logging | 0.5–5ms+ jitter | ~0 |
| vkapriltag serial detect | ~9ms | ~9ms (or ~6.5ms w/ decimation 3–4; FramePipeline for throughput) |
| Pose + JSON | ~1ms | ~1ms |
| Annotate copy (preview only) | ~3–4ms | unchanged |
| **Total critical path** | **~30ms + contention → 60ms observed** | **~13–18ms** |

Plus removing encoder/contention jitter (§4, §6) is what converts "30ms average, 60ms typical reading" into a stable sub-20ms.

## Prioritized action list

1. **Remove/level-gate the per-frame `EnterLog`** in `ApriltagDetector::Process`; batch `Logger` flushes off the hot path. *(smallest change, removes jitter)*
2. **Decode MJPEG directly to gray** for detector-only pipelines (`IMREAD_GRAYSCALE`); longer term, RK MPP hardware decode.
3. **Set vkapriltag `cpu_threads = 4`** and pin the library's `WorkerPool` threads to the performance cores.
4. **Verify the WebRTC encoder is `h264_rkmpp`**, not x264; if x264, switch or unpin/downscale the preview.
5. **Stamp `captureTimeUs` from `buf.timestamp`** and drain-to-newest on DQBUF.
6. Consider **decimation 3–4** at 1080p if detection range allows (GPU phase is 86% pixel-proportional per the library's measurements).
7. Consider **`FramePipeline`** overlap if throughput (not single-frame latency) becomes the binding constraint.
8. MJPEG fallback: drop the base64 round trip (raw bytes over SWIG), tie the server poll to frame-ready notification instead of 100ms polling.
9. Instrument per-stage timings (decode / gray / detect / pose / publish) and run with `APRILTAG_VK_TIMESTAMPS=1` to confirm the split on-device.

## On-device verification checklist (pending SSH access)

SSH access could not be established during this analysis (password rejected), so these remain to be confirmed on the target:

- [ ] `cv::getBuildInformation()` → is OpenCV built with libjpeg-turbo / NEON?
- [ ] Which H264 encoder does the active profile use (`h264_rkmpp` vs `libx264`)?
- [ ] `htop`/`sched` during a run: are vkapriltag WorkerPool threads on A55s? A76 saturation?
- [ ] `APRILTAG_VK_TIMESTAMPS=1` per-dispatch breakdown at 1920×1080.
- [ ] Size/flush behaviour of `LumenVision.log` on the rootfs; measure one `EnterLog` call.
- [ ] `v4l2-ctl --get-fmt-video` + buffer occupancy to confirm queue depth behaviour.
