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
- **The decode is always to BGR**, but the apriltag detector only needs grey. `cv::IMREAD_GRAYSCALE` skips chroma upsampling + colour conversion entirely (~40% less work) — and currently the BGR→grey conversion is paid *again* downstream (§2).
- **The RK3588 has a hardware JPEG decoder (MPP).** Hardware 1080p MJPEG decode is ~2–3ms. *(Update: implemented and confirmed working on real hardware - see [MppJpegDecoder.cpp](../LumenCore/MppJpegDecoder.cpp), used by `V4l2CameraBackend::Grab()` with a software `cv::imdecode` fallback.)*
- The decode happens **after** `captureTimeUs` is stamped, so all 12ms count toward `latencyMs` (§5 has a related timestamping issue).

## 2. Double colour conversion per frame

[ApriltagDetector.cpp](../LumenCore/ApriltagDetector.cpp) `Process()`:

1. Camera decodes MJPEG → BGR (12ms, §1).
2. `result.frame->AsGray()` converts BGR → GRAY with `cv::cvtColor` ([Frame.cpp](../LumenCore/Frame.cpp#L46-L56)) — another ~2–3ms at 1080p, single-threaded.
3. If a preview sink is bound (`wantsFrame`), the BGR frame already in hand is **copied in full** into a pooled buffer (~3–4ms at 1080p) for annotation.

The grey image the detector actually uses is derived from a full-colour decode that was only needed for the optional preview. Fix: decode MJPEG directly to greyscale when no frame consumer needs colour (the `FrameConsumer` registry already exists for exactly this kind of decision), or keep the compressed JPEG in `Frame` and decode lazily in `AsGray()`/`AsBgr()`.

## 3. Per-frame disk + console I/O in the detector hot path ⚠️

[ApriltagDetector.cpp](../LumenCore/ApriltagDetector.cpp) `Process()` calls, **every frame, per detector**:

```cpp
m_Logger->EnterLog("detecting apriltags using backend=" + m_Backend->Name());
```

[Logger.cpp](../LumenCore/Logger.cpp#L18-L24) `EnterLog` then does, per call:

- heap-allocates a `Log`, takes a **process-wide recursive mutex shared by every component**,
- calls `FlushLogs()`, which **opens `LumenVision.log` with a fresh `std::ofstream`, appends, and closes it** — 2+ syscalls plus SD-card/eMMC write latency, per frame,
- writes to `std::cout` with `std::endl` (**flush** per frame) — blocking if stdout is a slow pipe (journald/SSH session).

At 30fps this is 30 file open/write/close cycles per second per detector, serialised against every other thread that logs anything, on SD-card-class storage. This is a prime suspect for the jitter between the theoretical ~30ms and the observed 60ms. (Incidental bug: `FlushLogs()` writes `m_Logs.back()` then pops — the file receives entries in **reverse** order.)

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
- detector thread: grey conversion + GPU submit/wait + copy/annotate,
- vkapriltag CPU tail pool (unpinned, lands wherever),
- WebRTCSink's H264 encoder ([WebRTCSink.cpp](../LumenCore/WebRTCSink.cpp#L172-L240)) — if this is x264 `ultrafast` at 1080p it's ~20–35ms/frame of pure A76 time; if it's `h264_rkmpp` it's nearly free. *Verify on-device which encoder the profile selects.*

12ms (decode) + ~9ms (detect) + ~3ms (grey) + ~4ms (copy/annotate) ≈ 28ms of critical path is already at the edge of the 33ms frame budget; any encoder contention or A55 straggler pushes a frame over and the next frame starts late — this is exactly how ~30ms of work becomes a 60ms reading.

**Fix:** verify the preview uses `h264_rkmpp` (RK3588 VPU) rather than x264; consider partitioning instead of blanket pinning (e.g. capture+encode on 2 A76s, detector on the other 2), or leave the encoder thread unpinned.

## 7. MJPEG preview path is doubly wasteful (fallback only, but notable)

- [MjpegSink.cpp](../LumenCore/MjpegSink.cpp): `cv::imencode` per frame (~10–15ms at 1080p) → **base64-encodes** the JPEG (+33% bytes, ~1–2ms) → SWIG-marshals a string → [MjpegStreamModule.cs](../Server/HttpModules/MjpegStreamModule.cs#L45-L52) immediately `Convert.FromBase64String`s it back. The base64 round trip buys nothing.
- The server polls the sink at a fixed `PollInterval = 100ms` — preview is hard-capped at 10fps and gains up to 100ms of additional latency independent of the actual frame rate. (Preview-only; doesn't affect `latencyMs`.)

---

## Expected budget after fixes (1080p30, vkapriltag)

| Stage | Now | After |
|---|---|---|
| MJPEG decode | 12ms (CPU, colour) | ~2–3ms (MPP HW) or ~5–7ms (turbo, direct-to-gray) |
| BGR→grey | ~2–3ms | 0 (decode-to-gray) |
| Per-frame logging | 0.5–5ms+ jitter | ~0 |
| vkapriltag serial detect | ~9ms | ~9ms (or ~6.5ms w/ decimation 3–4; FramePipeline for throughput) |
| Pose + JSON | ~1ms | ~1ms |
| Annotate copy (preview only) | ~3–4ms | unchanged |
| **Total critical path** | **~30ms + contention → 60ms observed** | **~13–18ms** |

Plus removing encoder/contention jitter (§4, §6) is what converts "30ms average, 60ms typical reading" into a stable sub-20ms.

## Prioritised action list

1. **Remove/level-gate the per-frame `EnterLog`** in `ApriltagDetector::Process`; batch `Logger` flushes off the hot path. *(smallest change, removes jitter)*
2. **Decode MJPEG directly to grey** for detector-only pipelines (`IMREAD_GRAYSCALE`); longer term, RK MPP hardware decode.
3. **Set vkapriltag `cpu_threads = 4`** and pin the library's `WorkerPool` threads to the performance cores.
4. **Verify the WebRTC encoder is `h264_rkmpp`**, not x264; if x264, switch or unpin/downscale the preview.
5. **Stamp `captureTimeUs` from `buf.timestamp`** and drain-to-newest on DQBUF.
6. Consider **decimation 3–4** at 1080p if detection range allows (GPU phase is 86% pixel-proportional per the library's measurements).
7. Consider **`FramePipeline`** overlap if throughput (not single-frame latency) becomes the binding constraint.
8. MJPEG fallback: drop the base64 round trip (raw bytes over SWIG), tie the server poll to frame-ready notification instead of 100ms polling.
9. Instrument per-stage timings (decode / grey / detect / pose / publish) and run with `APRILTAG_VK_TIMESTAMPS=1` to confirm the split on-device.

## vkapriltag 1.7.0 findings on the Orange Pi 5 Plus (Mali-G610)

- `VK_EXT_external_memory_host` is not exposed by the Mali driver (`external_memory_dma_buf` is), so host-pointer import is
  inactive on the board and every frame takes the direct-write upload: 0.12-0.17 ms of a ~3.5 ms frame at 1280x800
  (`pipeline_total` median 3.55 ms, GPU 2.1 ms, quad decode 0.35 ms, tag decode 1.1 ms). Host import is active on the RX 9060 XT
  development GPU, with corners identical to the upload path.
- dma-buf import (`ImportDmaBufFrame`) is available on the Mali. It would only save the upload plus the CPU grey conversion, and
  needs a dma-buf per frame from the V4L2/MPP path (the library reads an NV12 buffer's Y plane as a packed 8-bit frame), so it
  is left as a follow-up that needs a camera attached to measure.
- RKNN on the same board, bus.jpg, ms/frame: yolov8n fused fp16 45, DFL fp16 44, DFL int8 20; yolo11n fused fp16 51,
  DFL fp16 50, DFL int8 26.

## Where the time goes: stage profiler measurements (Orange Pi 5 Plus, 2026-10-18)

Every node records per-stage timings over its last ~240 results (`StageProfile`; `GET /api/profile`, `POST /api/profile/reset`, the
Inspector's Timing section). "age" is the end-to-end latency a node publishes at (capture timestamp to published result), "wait" is how long the
newest frame sat before the sink started on it, "process" is the sink's work. Measured on the live pipeline: one 720p MJPEG 30 fps camera feeding
a Vulkan AprilTag sink (decimation 1, ultra-fast refine, 8 threads), a CPU AprilTag sink (8 threads) and a YOLO11 RKNN sink at once.

| Node | age | = camera | + wait | + work | the work |
| --- | --- | --- | --- | --- | --- |
| Camera | 15-16 ms | | | | hardware JPEG decode 3.9 ms; the rest is the USB transfer of the frame |
| Vulkan AprilTag | 29-30 ms | 15.5 | 0.1 | 13.6 | GPU 8.7 (upload 0.13, readback 0.03), CPU quad fit 3.9, tag decode 0.4, grey 0.3 |
| RKNN YOLO11 | 34 ms | 15.5 | 0.3 | 18.2 | NPU run 16.1, letterbox 0.7, output fetch 0.8, convert 0.2, decode+NMS 0.1 |
| CPU AprilTag | 69 ms | 15.5 | 17 | 36 | libapriltag detect 36 > the 33 ms frame interval, so frames queue |

- **The framework adds nothing.** Age is camera + wait + work to within 0.2 ms for every node: no hidden queueing between nodes, JSON and
  publishing cost 0.02 ms. Latency is the camera's USB transfer plus each node's own compute.
- **Thread priority was inconsistent (fixed).** A thread inherits its creator's nice value, and the NPU runtime raises the thread that loads a
  model to nice -19 without being asked (the service holds no capabilities, so it cannot do that itself; a standalone process that merely loads
  librknnrt shows the same). So everything started at boot ran at -19 and anything started later from a web request (an ASP.NET thread, nice 0)
  got a small share of the saturated fast cores. Stopping and starting a sink, or rebuilding the Vulkan sink with Apply Tuning, made it
  30-60% slower until the service restarted (Vulkan 30 -> 38-48 ms, its start wait 0.2 -> 3.5 ms; RKNN 34 -> 41 ms). Renicing that one
  thread to -19 restored it (RKNN 40.5 -> 35.0 ms); `Nice=-19` in the unit puts every thread at -19, and then restarting all three sinks and
  rebuilding the Vulkan sink leave the numbers unchanged.
- **Decimation is the lever for the Vulkan sink.** Decimation 2 halves its work (GPU 8.7 -> 4.0 ms, CPU tail 3.9 -> 1.8): age 29 -> 22.6 ms.
  It costs detection range (a tag needs about twice the pixels), so it was left at the user's setting.
- **Worker threads matter a little for the Vulkan sink's CPU tail.** Quad fitting takes 6.8 ms with 2 threads, 4.9 with 4, 4.5 with 6 and
  4.2 with 8 (frame age 32.5, 30.6, 29.2 and 30.3 ms; measured with `Nice=-19`, so the rebuilds are comparable). More than 4 gains under 1 ms.
- **CPU governor `performance` vs `ondemand`**: no difference once the cores are busy (they sit at their maximum frequency under this load) - but a
  graph with a single detector leaves the big cores lightly loaded, and then `ondemand` clocks them down between frames: with only the Vulkan
  detector bound, `detect` took 9.7 ms (p95 11.0) and the camera decode 2.5 ms, against 4.3 ms (p95 4.5) and 1.3 ms with the big cores on
  `performance` (Orange Pi 5 Plus, 1280x720 at 30 fps). The package's udev rule now sets `performance` on cpu4-7.
- **NPU core masks** (`rknn_set_core_mask`, three cores) gave no gain for the deployed YOLO11 model (25.6 vs 25.2 ms per frame with the system
  loaded) and 20% for a YOLOv8n; not adopted.
- The remaining frame age is the camera: exposure is already 2 ms, the 12 ms before decode is the USB transfer.

## On-device verification checklist (pending SSH access)

SSH access could not be established during this analysis (password rejected), so these remain to be confirmed on the target:

- [ ] `cv::getBuildInformation()` → is OpenCV built with libjpeg-turbo / NEON?
- [ ] Which H264 encoder does the active profile use (`h264_rkmpp` vs `libx264`)?
- [ ] `htop`/`sched` during a run: are vkapriltag WorkerPool threads on A55s? A76 saturation?
- [ ] `APRILTAG_VK_TIMESTAMPS=1` per-dispatch breakdown at 1920×1080.
- [ ] Size/flush behaviour of `LumenVision.log` on the rootfs; measure one `EnterLog` call.
- [ ] `v4l2-ctl --get-fmt-video` + buffer occupancy to confirm queue depth behaviour.
