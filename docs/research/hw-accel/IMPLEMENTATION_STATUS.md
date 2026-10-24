# Hardware offload and optimisation: what was done and what was measured

The research notes in this folder (`01`-`05`) ranked what could be offloaded to the RK3588's accelerators or otherwise sped up. This file records what was
implemented from them, what was measured on an Orange Pi 5 Plus, and what was deliberately left. Baselines are in `baselines/` (`scripts/bench-pi.sh`
writes them: node stage timings plus CPU, NPU, GPU and DDR load and temperatures, sampled once a second).

Test setup: one UVC camera at 1280x720, 30 fps MJPEG; a Vulkan AprilTag detector bound to it (no tag in view, so pose cost is not in these
numbers); for the NPU rows also a YOLO11 `.rknn` detector (640x640 int8) on the same camera.

## Measured on the board

| Stage | before (dev62) | after | change |
|---|---|---|---|
| Camera decode (grey, hardware JPEG) | 2.14 ms mean, 2.71 p95 | 1.64 ms mean, 1.68 p95 (1.44 in the decoder block, 0.19 copying out) | cacheable MPP output buffer, synced around the CPU read |
| Vulkan AprilTag `detect` | 6.59 ms mean, 9.01 p95 | 6.07 ms mean, 7.20 p95 | worker pools pinned to the performance cores, OpenCV pool placed there too |
| WebRTC preview, `h264_rkmpp` encode | 5.47 ms | 2.27 ms | the encoder runs in low-delay mode (otherwise it keeps up to four frames in flight) |
| WebRTC sink total per frame | 7.86 ms | 4.62 ms | same |
| MJPEG fallback sink | 1268 JPEGs in 40 s (30 fps) | about a third of that | it encodes at the poll rate (the server asks about 10 times a second) and stops a second after the last request |
| YOLO11 letterbox + convert | 0.70 + 0.21 ms | 0.44 + 0.14 ms | resized straight into a reused canvas |
| YOLO11 NPU run | 15.6 ms | 15.55 ms | unchanged: it is Rockchip's own figure for this model on one core |
| Annotated frame while nobody watches | drawn every frame | not drawn | a preview only counts as a consumer while a viewer is connected |

Also verified on the board: two browsers can watch one WebRTC sink at once (the second one joins with a forced IDR frame and the first keeps playing);
a recording is a fragmented MP4 (`moov` at byte 32, first `moof` at byte 850 while the file is still being written, so a power cut keeps everything up to
the last keyframe); each RKNN detector is given its own NPU core in turn and the runtime and driver versions are logged (`RKNN runtime 2.5.0, driver 0.9.8`);
the server runs workstation GC (it ran Server GC with a thread per core).

## Implemented, verified by tests only (no board test possible here)

- Tag poses come from vkapriltag's allocation-free `PoseEstimator`, seeded from a homography of the undistorted corners. A test checks the pose against
  libapriltag's own `estimate_tag_pose`, with and without lens distortion (1e-4 m / 1e-4 rotation). The published research figure is 0.916 ms against 0.016 ms
  per tag on an Orange Pi 5 for the solve itself; there was no tag in view on the board.
- SGBM stereo matches at half resolution by default (about 8x less work; a test shows the same disparity as full resolution).
- The stereo node crops at the origin of the valid region (it cropped at the image corner while sizing the crop from the region), rectifies only the crop, and only
  draws its output frame when a bound node wants it; depth fusion shifts the principal point by the crop origin and fuses a detection with the depth grid of the
  pair it was made on instead of the newest one.
- Each CPU cluster's frequency is reported next to the accelerators (a cluster far below its maximum is how throttling shows).
- Core detection uses `cpu_capacity` (any core within 10% of the fastest), so differently binned A76 clusters stay one set. On the board all four report the same.
- WebRTC takes keyframe requests (PLI/FIR) and timestamps frames from the camera's capture time.

## Found on the way

- A camera source whose device cannot be opened (the board's camera had moved to another USB port) looped on an immediate "camera is closed" error: its capture
  thread held a performance core at 100% and wrote about 40 MB of error lines, rotating the useful logs away. It now logs once and retries every two seconds, and
  restores the last mode when the device opens again. The `LenovoCamera0` source on the test board still points at the old port.
- `ServerGarbageCollection` (not `ServerGarbageCollector`) is the MSBuild property for the GC mode.

## Left out on purpose

| Item | Why |
|---|---|
| `CPUAffinity` for the .NET process | Only safe once every native worker pool is pinned explicitly; with workstation GC the server no longer starts a thread per core, so the gain is small |
| NPU load from devfreq instead of `sudo cat` | the devfreq load file reads 100% on an idle NPU on this board |
| Rate-limiting the logger | the logger's contract (and a test) is that every call reaches the file; the actual flood was the camera loop above |
| SDP profile/level matching the bitstream | no evidence of a decoder rejecting the stream; revisit if one does |
| Pinning libdatachannel (v0.24.6) and the RKNN runtime (v2.5.0) in `install-deps.sh` | editing that script invalidates the CI dependency cache and forces a full rebuild of OpenCV, ffmpeg-rockchip and MPP; do it when a full rebuild is acceptable |
| MJPEG raw bytes instead of base64 | the string crosses the SWIG boundary |
| RKNN zero-copy input and int8 decoding | at most about 1 ms of an 17 ms frame, and not testable without the board loop |
| Blob-filter thresholds as tuning options | `LUMEN_VK_MIN_WHITE_BLACK_DIFF`, `LUMEN_VK_MIN_CLUSTER_PIXELS` and `LUMEN_VK_MIN_TAG_PIXELS` read the three values for experiments; a sweep needs a real field scene and the detection range judged by eye |
| Rectangular model input (640x384) | needs the team's model converted again; `scripts/convert-yolo-rknn.py` is written for it but has not been run (rknn-toolkit2 is x86-64 only) |
| Phase 6: zero-copy NV12 DMA-BUF frames, RGA for the recording and WebRTC paths, MIPI cameras, predicted-region tag search | the measured pipeline is already at 1.6 ms decode and 6 ms detect, and the camera's own 14 ms age (USB transfer and exposure) dominates latency; each is its own design step if CPU load or latency needs it |
