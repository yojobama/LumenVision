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

## Second round: zero-copy frames, the RGA, and the smaller items

Measured on the same board with a graph of one camera, a Vulkan AprilTag detector and a YOLO11 detector (no viewers). "ondemand" and "performance" are
the CPU governor of the big cores.

| | first round (dev66, ondemand) | now (performance governor) |
|---|---|---|
| Camera decode | 2.3 ms (1.4 in the decoder, 0.9 copying out as BGR for the detector that needs colour) | 1.5 ms (nothing is copied; the frame stays in the decoder's buffer) |
| AprilTag `detect` | 6.2 ms mean, 6.9 p95 | 4.4 ms mean, 4.7 p95 |
| AprilTag frame age at publish | 22.4 ms | 17.9 ms |
| YOLO11 frame age at publish | 31.4 ms | 30.9 ms (the NPU run itself is 15.4 ms either way) |
| Camera preview, WebRTC convert + encode | 2.0 (RGA from BGR) + 2.3, after a 0.9 ms BGR conversion | 1.35 + 3.0 straight from the NV12 planes |
| Camera recording, convert | 0.9 ms after a 0.9 ms BGR conversion | 1.6 ms from the NV12 planes |

- **Zero-copy NV12 frames.** The hardware JPEG decoder's output stays in its buffer as an NV12 `Frame` (two plane views plus an owner that returns the buffer
  when the last copy goes). The detector reads the luma plane as it is, `Frame::AsBgr()` converts once and only for a consumer that needs it, the encoders
  convert the planes straight to NV12 or YUV420P with lookup tables that squeeze the JPEG range into the video range (swscale took 5 ms for the same job),
  and a detector draws its overlay on one conversion of the picture. NV12 regions are cut from both planes when their edges are even. A reading of the
  luma plane no longer waits for another thread's BGR conversion of the same frame.
- **RGA.** The detector input (NV12 or BGR to RGB, scaled into the letterbox) is prepared in one RGA pass. It matches the CPU path to within about a pixel
  (the 14 and 10 detections of a test photo come out in the same places, confidences within 0.05) and takes 1.5 ms of hardware time against 1.0 ms of CPU
  time, so the NPU frame is about 0.35 ms later but the CPU is free (total load 2.9% against 4.2% of the board, AprilTag frame age 0.4 ms lower). The border is
  painted by the CPU, because a hardware fill can only run on the RGA2 core, which cannot reach memory above 4 GB on a 16 GB board. Frames whose scaled picture
  has an odd edge, and planes that do not sit in one buffer, are prepared on the CPU. `LUMEN_RGA_DISABLE=1` keeps everything on the CPU.
- **int8 outputs.** An int8 export is fetched as the NPU wrote it and decoded from the raw values: `get-outputs` 0.8 ms to 0.3 ms, and the detections are
  identical to the float path's (`LUMEN_RKNN_FLOAT_OUTPUT=1` brings that back).
- **Big-core governor.** With only a Vulkan detector bound, `ondemand` clocked the big cores down between frames: `detect` 9.7 ms (p95 11.0) and decode 2.5 ms,
  against 4.3 ms (p95 4.5) and 1.3 ms on `performance`. The package's udev rule now sets `performance` on cpu4-7 (the cost is a little idle power).
- **Blob filters** (edge contrast, smallest blob, smallest tag side) are tuning options of a Vulkan AprilTag sink: saved with it and set from the Inspector.
  Their effect depends on the scene (clutter makes the CPU quad fit slow), so the defaults are unchanged.
- **WebRTC** streams stay within constrained baseline at the smallest H.264 level that holds the macroblock rate, matching the SDP.
- **A camera that is gone** is retried every 2 s, backing off to 10 s.

Measured and not adopted: pinning the whole service to the A55 cores (`CPUAffinity=0-3`) changed nothing (AprilTag `detect` 4.38 ms against 4.45 ms, NPU
frame age 31.0 against 30.8 ms), and the .NET threads are idle enough not to matter.

## Left out on purpose

| Item | Why |
|---|---|
| NPU load from devfreq instead of `sudo cat` | the devfreq load file reads 100% on an idle NPU on this board |
| Rate-limiting the logger | the logger's contract (and a test) is that every call reaches the file; the actual flood was the camera loop |
| MJPEG raw bytes instead of base64 | base64 costs 0.6 ms per encoded frame at about 10 a second (0.6% of a core), and the string crosses the SWIG boundary |
| RKNN `rknn_create_mem` zero-copy input | the copy it saves is 0.16 ms (`set-input`) |
| Rectangular model input (640x384) | needs the team's model converted again; `scripts/convert-yolo-rknn.py` is written for it but has not been run (rknn-toolkit2 is x86-64 only) |
| MIPI cameras (multi-planar V4L2, `VIDIOC_EXPBUF`) | no MIPI camera to test with, and a rewrite of the capture backend's buffer handling is not something to ship untested |
| Predicted-region tag search, browser-side overlay | both need tags in view to validate, and the browser overlay changes what every detector preview shows |
| Direct dma-buf import of the luma plane into Vulkan | the upload it would replace is 0.1 to 0.2 ms |

## Third round: sanitisers and profilers

Measured and fixed with real tools rather than the stage timers; see `SANITISERS_AND_PROFILING.md`. On the heavy two-camera graph (two Vulkan detectors with MJPEG tiles, a
60 fps stereo camera, two recordings) the service went from 2.13 to 1.56 cores (-27%) and the performance cores from 48% to 35% busy, with the frame ages and detect times
unchanged: the recorders' range squeeze uses NEON table lookups, detectors draw their overlay only for frames a preview will use, WebRTC is paced to its stream rate, and
OpenCV's workers no longer spin between jobs. The sanitisers found a leak in the Vulkan backend's constructor, two thread-lifetime races and a test layout overflow, all fixed.

## Fourth round: vkapriltag 1.7.0 to 1.8.0

The submodule moved to the `v1.8.0` tag's commit (`5400c69`; its output is identical to 1.7.0's, the existing single-detector API is unchanged). Measured on the board with the
same build of LumenVision either side (only the library differs; `dev79` and `dev81`), service environment as shipped, 40 s per run after a 12 s warm-up, two runs per cell.
All Vulkan sinks use the ultrafast refine preset with probeMargin 4. The harness and the raw runs are in `profiles/harness/` and `profiles/vkapriltag-1.7.0.jsonl`, `profiles/vkapriltag-1.8.0.jsonl`.

| Workload (one camera at 1280x720 and 30 fps unless said) | | 1.7.0 | 1.8.0 | change |
|---|---|---|---|---|
| **V**: one Vulkan detector, decimation 2 (the default) | service CPU | 0.14 cores | 0.10 | -26% |
| | `detect` | 3.54 ms | 3.06 | -13.5% |
| | CPU quad fit (`vk-quads`) | 0.77 ms | 0.44 | -43% |
| **V1**: one detector, decimation 1 | service CPU | 0.35 cores | 0.23 | -34% |
| | `detect` | 11.13 ms | 8.95 | -19.6% |
| | GPU submit and wait | 8.09 ms | 6.95 | -14% |
| | CPU quad fit | 2.75 ms | 1.71 | -38% |
| | frame age at publish | 24.6 ms | 22.6 | -8% |
| **W**: three detectors (that camera and the stereo camera's two halves at 2560x720, 60 fps), decimation 2 | service CPU | 0.72 cores | 0.54 | -26% |
| | CPU quad fit | 0.83 / 0.98 ms | 0.47 / 0.61 | -43% / -38% |
| | `detect`, frame age | | | within the run-to-run scatter |
| **W1**: the same three at decimation 1 (the GPU is saturated, 95%, in both) | service CPU | 1.69 cores | 1.32 | -22% |
| | `detect` (camera, left, right half) | 19.2 / 21.5 / 21.5 ms | 16.4 / 17.1 / 17.1 | -15% / -21% / -21% |
| | frame age of the halves | 49.4 ms | 43.3 | -12% |
| **B**: W plus two YOLO detectors, two recordings, two MJPEG tiles | service CPU | 1.45 cores | 1.25 | -14% |

The CPU stage of the detector (the quad fit) is 36-43% cheaper in every workload, which is what the library's changes aim at (the line-fit hypotenuse in double precision, whose cost this
repository's profile had singled out in `SANITISERS_AND_PROFILING.md`, and its CPU-tail work); the service's CPU follows it. The decimation-1 GPU passes are 11-18% cheaper. At the default
decimation with several detectors the frame is bound by the camera's interval and the GPU is a third loaded, so the gain shows as CPU saved rather than latency. Decode, the NPU run and its frame age,
and the MJPEG tile frame counts are unchanged. The GPU submit-and-wait cells for the camera's own detector in W and B scatter by 1-3 ms between runs of the same version (it shares the GPU with the other
detectors), so no conclusion is drawn from them.

Correctness: the library's own change log calls its output identical, and the board's hardware suite agrees: with an ASan+UBSan build against 1.8.0, the Vulkan backend tests and the
CPU-against-Vulkan agreement test on a real tagged image pass exactly as before (11 test cases passed, 1 skipped for PhotonVision's own model, 435 assertions, no sanitiser report).
