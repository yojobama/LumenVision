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

## Fifth round: vkapriltag 1.8.0 to 1.9.0

The submodule moved to the `v1.9.0` tag's commit (`0d537c8`). The release is GPU-side (six fewer dispatches per frame, labelling in one merge pass with run starts from a workgroup scan,
threshold and block passes folded together, wider loads on integrated GPUs, a larger-blob sort); the public API is unchanged and the library's output is stated to be identical.
Measured on the board with the same LumenVision build either side (`dev81` and `dev83`), 40 s per run after a 12 s warm-up, two runs per cell, the service as shipped. The Lenovo camera
was unplugged for this round, so the workloads use the stereo camera's halves (1280x720 each, at 60 fps) only; both versions were measured fresh, back to back, under those conditions.
All Vulkan sinks use the ultrafast refine preset with probeMargin 4. Raw runs: `profiles/vkapriltag-1.8.0-round2.jsonl`, `profiles/vkapriltag-1.9.0.jsonl` and the single-detector pair
`profiles/vkapriltag-single-1.8.0.jsonl`, `profiles/vkapriltag-single-1.9.0.jsonl`; the harness (workloads S and S1 added) is in `profiles/harness/`.

| Workload | | 1.8.0 | 1.9.0 | change |
|---|---|---|---|---|
| **S**: one detector on a 60 fps half, decimation 2 (the default) | `detect` | 3.02 ms | 2.60 | -14% |
| | GPU submit and wait | 2.27 ms | 1.89 | -17% |
| | GPU load | 11.5% | 10.0 | |
| **S1**: the same at decimation 1 | `detect` | 7.88 ms | 6.24 | -21% |
| | GPU submit and wait | 6.12 ms | 4.64 | -24% |
| | GPU load | 35% | 26.5 | -24% |
| | frame age at publish | 27.0 ms | 25.3 | -6% |
| **W**: both halves, decimation 2 | GPU load | 24% | 18.5 | -23% |
| | `detect` (left, right) | 4.55 / 3.72 ms | 3.79 / 3.44 | -17% / -8% |
| **W1**: both halves, decimation 1 | GPU load | 67.5% | 52 | -23% |
| | `detect` (left, right) | 9.68 / 12.09 ms | 9.34 / 7.74 | -3.5% / -36% |
| | frame age (left, right) | 29.0 / 31.4 ms | 28.5 / 26.9 | -2% / -14% |
| **B**: W plus a YOLO detector, two recordings and two MJPEG tiles | GPU load | 24.5% | 20.0 | -18% |
| | left `detect` | 4.77 ms | 3.88 | -19% |

The service's CPU is unchanged (within 2-5% in every workload): the release works on the GPU, which now spends 13-24% less time per frame. A single detector's `detect` gets 14-21% shorter;
when two detectors share the GPU at decimation 1 the second one gains most (the right half's `detect` 12.1 to 7.7 ms, its frame age 31.4 to 26.9 ms), because the GPU is no longer saturated.
The NPU run and its frame age, and decode, are unchanged. The per-run values agree closely within each version (for example S1's GPU wait 6.10 and 6.15 ms against 4.59 and 4.69 ms).

Correctness: the board's hardware suite with an ASan+UBSan build against 1.9.0 passes exactly as before (11 test cases passed, 1 skipped for PhotonVision's own model, 435 assertions, no sanitiser
report), including the CPU-against-Vulkan agreement test on a real tagged image. One behaviour to know about: 1.9.0 trusts a single merge pass of the labelling instead of running a second one to
check it converged; `APRILTAG_VK_UF_VERIFY=1` in the service's environment restores the check.

## Sixth round: the accelerators, the codecs and the server

See `PROFILING_ROUND3.md`. The MJPEG previews are encoded on the RK3588's JPEG encoder and handed to the server as bytes in a pooled buffer (an encoder-heavy graph went
from 1.02 to 0.45 cores; the server's 13 full GCs a second are gone), and the cameras' NV12 frames now convert to RGB as the full-range YUV they are (every colour
picture had its contrast stretched by about 16%). GPU counters, the NPU, scheduling and a 10-minute thermal soak are recorded there too.

## rknn-toolkit2 2.5.0 conversion options

See `NPU_TOOLKIT_2_5.md`. Every option of the 2.x toolkits that builds for the RK3588 was measured on the board with YOLO11n against COCO. These include the
quantisation algorithms, per-layer quantisation, automatic hybrid quantisation, single-core mode, weight compression and pruning, and the undocumented
2.5.0 compiler switches. None is faster or more accurate than the script's per-channel int8, which is only 0.6 mAP behind fp32. The script now pins those
options and records them in the model's manifest. The input size is the lever that matters: 384x640 runs in 9.5 ms on one NPU core against 15.8 ms.

## Building for the board's CPUs

See `CPU_TUNING.md`. `-DLUMEN_TARGET_CPU=cortex-a76.cortex-a55` builds the tree for the RK3588's cores. On the board it measured within 1% of the generic build on
every workload: the hot code is vkapriltag's scalar quad fit (64% of the service's CPU), which the new instructions do not help. The option stays off in release builds.

## 12-hour soak

See `SOAK_12H.md`. Every accelerator and pipeline loaded at once for 12 hours: no restart, no API error, no throttling, every stream at full rate,
the WebRTC preview connected throughout. It found two bugs, both fixed and re-checked in a 2-hour run. Recording tore its H.264 encoder down every
60 s segment, which corrupted MPP's frame pool, leaked encoder buffers (dma-buf +4.8 MB/h) and caused 9 bursts of failed encode jobs; the encoder
now lives for the whole recording and segments rotate on a forced keyframe. RGA jobs on memory above 4 GB failed on the RGA2 core; they are now
kept on the RGA3 cores.

## Sixth round: vkapriltag 1.9.0 to 1.10.1 (desktop GPU only; the board has not been measured)

The submodule moved to the `v1.10.1` tag (`956d502`). 1.10 turns its accuracy work on by default (profile-fit edge refinement, grey-margin and one-cell-out retries, third corrected bit, per-family
limits, dark and border-less quad rejection, box decimation, noise-adaptive contrast floor) and adds speed work (compacted labelling, a leaner tail, flag polling, a GPU keep-alive on integrated
GPUs). One behaviour change reached LumenCore: `PoseEstimator`'s constructor now throws `std::invalid_argument` for a zero tag size or non-positive focal lengths, which ended the detector
thread (the edge test "a detector with a zero tag size keeps running" caught it); `ApriltagDetector::SolveTagPose` now treats it as "no pose".

The board was not reachable for this round, so the numbers are from vkapriltag's own tools built from both tags on the development PC (AMD Radeon RX 9060 XT, Windows, OpenCV 5.0), each at its
defaults. They say nothing about the Mali; the library's own change log reports 2.549 / 1.230 ms to 1.058 / 0.743 ms (decimation 1 / 2) on the Orange Pi 5 Plus, which this round did not
reproduce. Because the defaults changed, an on-board run should compare 1.9.0 and 1.10.1 with the refine preset and `probeMargin` the service really uses, as the earlier rounds did.

Corpus (`profiles/vkapriltag-desktop/gen_corpus.py`): 170 synthetic 1280x800 frames of tag36h11 with exact ground-truth corners, 322 tags, varying tag size (16-320 px), tilt, yaw, blur,
noise, contrast, and 4-40 tags per frame. A tag counts as found when its ID is in the ground truth; corner error is the mean distance to the true corners (the ground truth shifted half a pixel
for the detector's pixel-corner convention).

| | 1.9.0 | 1.10.1 |
|---|---|---|
| tags found, decimation 1 / 2 (of 322) | 216 / 131 | 217 / 130 |
| wrong IDs | 0 / 0 | 0 / 0 |
| median corner error, decimation 1 / 2 | 0.209 / 0.221 px | 0.189 / 0.189 px |
| 95th percentile corner error, decimation 1 / 2 | 0.449 / 0.568 px | 0.396 / 0.485 px |
| median frame total, single tag, decimation 1 / 2 | 0.320 / 0.259 ms | 0.278 / 0.249 ms |
| median frame total, 4-40 tags, decimation 1 | 1.65 ms | 0.34-0.41 ms |
| median frame total, 4-40 tags, decimation 2 | 1.03-1.11 ms | 0.29-0.35 ms |

Timing is `--iterations 300` per frame, three alternating runs per version. The many-tag frames of 1.9.0 sit at one value (about 1.65 ms) however many tags there are, which points at a fixed
cost per frame (clock ramp or completion latency on repeated submissions) that 1.10's flag polling and keep-alive remove; whether the Mali has the same cost is not known from this.
Recall is the same on this corpus: the 1.10 change log's gain in tags found was measured on its own corpus, and here libapriltag itself finds fewer than either (209 / 104). Pose, from
`apriltag_synth_ground_truth` (rotation error against the true pose, mean over the frames both sides decode): vkapriltag 6.06 deg (1.9.0) to 5.09 deg (1.10.1), libapriltag 6.4-6.6 deg;
translation error 0.0039 to 0.0034 of the distance; corner RMS 0.82-0.96 px (libapriltag) against 0.50-0.69 px (1.10.1, which no longer tracks libapriltag's corners as 1.9.0 did, so the
mutual rotation delta between the two rises from 3.5 to 5.5 deg while both stay near the truth). The worst cases of about 120 deg are pose-ambiguity flips on tags under 20 px, in every
build. Library and LumenCore tests: the apriltag, pose, edge, multitag and overlay tests pass (1,061,446 assertions) in the ASan+UBSan WSL build against 1.10.1; the GPU-only tests were skipped
there (software Vulkan only).

## Seventh round: OpenCV 5.0.0 to 5.1.0 (x64 only; the board has not been measured)

`scripts/install-deps.sh` now builds OpenCV `5.1.0` (and opencv_contrib `5.1.0`) with the flags it used for 5.0; an existing 5.0 install is detected by `pkg-config --modversion` and rebuilt. The CI cache
key hashes the script, so it refreshes itself. The Windows `C:/lumen-deps` copy is hand-built and stays on 5.0 until it is rebuilt by hand. `find_package(OpenCV 5.0 ...)` keeps its minimum of 5.0 until code
that needs a 5.1 call (the object tracker's `cv::linearAssignment`) arrives.

Source changes: the `cv::Mat_<double>(r, c) << a, b, c` comma initializer, removed in 5.1 and already deprecated in 5.0, is replaced by `cv::Mat(cv::Matx<double, r, c>(...))` in `ApriltagDetector.cpp`,
`ObjectDetectionSink.cpp` and six test files (it compiles on both versions). Nothing else needed changing: `NMSBoxesBatched`, `StereoSGBM`, the calibration calls and the saturating `cvRound` family build and
behave as before.

Tests, WSL ASan+UBSan build, `LumenCoreTests "~[hitl]"`: 214 test cases, 208 passed and 6 skipped on 5.0 and on 5.1, linked against `libopencv_core.so.501` (assertion counts 1,007,449 and 995,937: some tests
loop for a fixed time, so the count varies with speed).

`tools/opencv-bench` times the OpenCV calls LumenCore makes (median of 400 or more runs, 3 alternating rounds, 12 threads, the same source built against each version, Ryzen desktop under WSL):

| call | 5.0.0 | 5.1.0 | change |
|---|---|---|---|
| `calibrateCamera`, 24 views of a 9x6 board (final RMS 0.409808 on both) | 32.45 ms | 22.18 ms | -32% |
| `warpPerspective` 1280x720 | 0.681 ms | 0.408 ms | -40% |
| `findChessboardCorners` 1280x720, 9x6 | 3.41 ms | 3.37 ms | -1% |
| `NMSBoxesBatched`, 400 boxes | 0.430 ms | 0.408 ms | -5% |
| `solvePnP` (IPPE square, 4 points / iterative, 8 points) | 0.0087 / 0.396 ms | 0.0087 / 0.408 ms | 0% / +3% |
| `cvtColor` BGR to gray / to I420 | 0.051 / 0.068 ms | 0.053 / 0.068 ms | +4% / 0% |
| `undistortPoints`, 1000 points | 0.051 ms | 0.055-0.067 ms | +7% to +30% |
| `StereoSGBM` 3-way, 640x480, 64 disparities | 8.2 ms | 9.1 ms | about +10% |
| `resize` 1280x720 to 640x360 | 0.054-0.093 ms | 0.053-0.094 ms | within noise |

The two slower rows (`undistortPoints`, `StereoSGBM`) are small in absolute terms and `StereoSGBM`'s runs overlap (7.8-9.3 ms against 9.1-9.3 ms); neither is large enough to matter at the rates the pipeline runs, but both are worth
a look on the board, where the 5.1 notes' Arm work (KleidiCV 26.09, vectorised imgproc kernels) is the part that could change. Whether the board's OpenCV build has KleidiCV enabled was not checked. Run
`tools/opencv-bench` there with both versions before drawing a conclusion.

## Eighth round: object tracking in ObjectDetectionSink (x64 only; the board has not been measured)

`ObjectDetectionSink` published each frame's boxes on their own, so nothing said that a box in one frame was the same ball as a box in the next. `LumenCore/ObjectTracker.{h,cpp}` adds a SORT-style tracker: each
frame the tracks move to where their velocity says they are, the detections are matched to them with the optimal assignment (`cv::linearAssignment`, new in OpenCV 5.1, on `1 - IoU`, a pair below `minIoU`
or of another class forbidden by an infinite cost), matched tracks are corrected by an alpha-beta filter, unmatched detections start tentative tracks, and tracks unseen for `maxAgeMs` end. Time is the
frames' capture time, so velocities are in pixels per second at 30, 60 or 120 fps. A track gets its id (monotonic per sink, never reused) after `minHits` sightings in a row; until then `trackId` is -1, so a
one-frame false positive takes none. Tracking is off by default; with it off the sink's output is unchanged. With it on each detection's JSON also carries `trackId`, `vxPxPerS` and `vyPxPerS` (web UI,
REST, the recording sidecar) and the annotated frame labels it `#id`. The PhotonVision-compatible NetworkTables packet is not touched.

Settings (`ObjectTrackingConfig`: `enabled`, `minIoU` 0.1, `minHits` 3, `maxAgeMs` 500, `classAware`) go through `Manager::Set/GetObjectDetectionTracking`, are validated, persisted on the sink and restored
with it and copied with the node (`GET/PUT /api/objectDetectionSink/tracking`, an Inspector section for an Object Detection node). `webui/src/api/generated.ts` was not regenerated (it comes from a running
server's OpenAPI document); the UI calls the route directly, as it does for the thresholds.

Tests, all on OpenCV 5.1.0 in the WSL ASan+UBSan build:
- `tests/test_object_tracker.cpp`: ids persist for a steady object; two identical objects crossing between frames keep their ids where a nearest-neighbour matcher swaps them; occlusions shorter and longer
  than `maxAgeMs`; other classes never match; confirmation after `minHits`; same velocity at 30/60/120 fps; resets on a size change or time running backwards; empty, NaN, zero-area, duplicate and repeated-time
  input; and `cv::linearAssignment` against an exhaustive search on 300 random rectangular matrices with forbidden pairs.
- `tests/test_object_tracker_footage.cpp`: rendered footage with exact ground truth goes through a real `ObjectDetectionSink` frame by frame (a detector that finds each ball by its colour, so overlapping
  balls stay two boxes). Two crossing balls: 0 id switches. A ball behind a bar: same track. Six bouncing balls with 5% missed detections and 5% false-positive speckles over 300 frames: 4 id switches, 2 tracks
  that were ever on two balls, 0 confirmed false positives (the thresholds in the test are 5 and 3). Velocity within 12% of the truth at 30, 60 and 120 fps. A real YOLOv8n on `bus.jpg` panned two pixels a
  frame for 50 frames: one id for the bus, three for the people (up to four visible).
- `tests/test_object_tracker_replay.cpp`: replays `RecordSink` sidecars (`.jsonl`) through the tracker with no model; a committed fixture is tested, and `LUMEN_TRACK_REPLAY=<folder> LumenCoreTests "[.replay]"`
  prints ids, ids a minute, mean track life and detections without an id for real recordings. This is the check to run on match footage once the board is back.
- `Server.Tests/ObjectTrackingTests.cs` (11 cases): defaults, set/get through to the native detector, persistence and old records, validation (400 and nothing changed), a non-detector sink, a copied node.
- A race test (settings changed from another thread while frames flow), run under ThreadSanitizer: 0 reports. `tests/tsan.supp` named OpenCV 5.0's sonames (`.so.500`), so on 5.1 (`.so.501`) every OpenCV worker-pool
  hand-over was reported again; the entries now match any soname.
- Whole non-hardware suite on 5.1 (ASan+UBSan): 231 test cases (214 before this work, plus 17), 225 passed, 6 skipped as before.

Speed (`LumenCoreTests "[.bench]"`, a Debug-configured test binary against the Release OpenCV 5.1, Ryzen desktop, 3 runs agreeing within 3%; the tracker's own code is therefore slower than a Release build's,
the solver is not):

| detections per frame | `Update` median / p99 | the assignment alone |
|---|---|---|
| 5 | 3.7 / 6 us | 1.0 us |
| 20 | 16 / 25-34 us | 7.6 us |
| 50 | 71 / 95-115 us | 51 us |
| 100 | 255 / 335-410 us | 231 us |
| 200 | 1.0 ms / 1.2-1.5 ms | 1.09 ms |

At the 5-30 objects a field camera sees, tracking costs tens of microseconds a frame, well under 1% of a 120 fps frame; it only reaches a millisecond at 200 objects, where the solver's cubic growth is the
cost. The sink reports the time as a `track` stage in `/api/profile`. On the board, measure it with `scripts/bench-pi.sh 60 tracking-on` on an Object Detection graph with tracking off and on, and build
`tools/opencv-bench` there for the OpenCV side; neither has been done.
