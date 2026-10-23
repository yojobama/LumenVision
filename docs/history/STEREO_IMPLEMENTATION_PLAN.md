# FRCV — Stereo depth (Phase 10)

> Written when this project was called FRCV. Left in its original FRCV-branded prose rather
> than rewritten - see `docs/RENAME.md` for the old->new identifier table and why.

**Status:** planned, nothing implemented
**Depends on:** `codec-stereo` (https://github.com/yojobama/codec-stereo) for disparity,
OpenCV for stereo calibration and rectification
**Reads with:** `IMPLEMENTATION_PLAN.md` (phases 0–9); this document is phase 10 and follows the
same conventions — the node model is normative, MSBuild only, no CMake in FRCV's own build,
nothing heavy reaches `swig.i`.

---

## 0. What codec-stereo is, and what it deliberately isn't

`codec-stereo` computes **block-granular disparity** by encoding the left frame as an I-frame and
the right as a single P-frame referencing it, then reading the encoder's motion vectors back and
reinterpreting `dx` as horizontal disparity. It is a latency play: it runs the search on the
VEPU/ASIC silicon that already exists on the RK3588 rather than spending CPU on a dedicated
matcher.

Two facts from its own design doc drive most of this plan:

1. **"Stereo rectification and calibration are out of scope for this library and are assumed to
   be handled upstream (OpenCV `stereoRectify` or equivalent). `codec-stereo` consumes
   already-rectified frames."** — that split is exactly the one this plan implements. Everything
   in §10.2 and §10.3 up to `cs_extract()` is ours.
2. **It is not competitive with SGBM on accuracy, and says so.** Middlebury *Motorcycle*, block
   16×16, block-aggregated so the comparison is fair:

   | | bad-2.0% | RMSE | density |
   |---|---|---|---|
   | `lavc_sw` | 55.9% | 32.4 | 50.5% |
   | `ref_sad` | 50.1% | 47.6 | 99.8% |
   | `cv::StereoSGBM` (block-avg) | **31.5%** | **25.3** | 89.2% |

   Middlebury Motorcycle is a hard scene (2964×2000, ndisp=270, fine detail). An FRC scene at
   640×480 with a 15 cm baseline has an order of magnitude less disparity range, so it should do
   materially better — but that is an **assumption to measure on our own rig, not a fact**. §10.3
   therefore ships an SGBM backend behind the same interface, specifically so the comparison can
   be run on real FRC hardware with real cameras instead of argued about.

Measured latency on the Orange Pi 5 Plus, `rkmpp_hwenc`, steady state: **3.0 ms @ 640×480,
15.0 ms @ 1080p**, close to linear in pixels with a ~1 ms floor. That is the number that makes
this worth doing at all — SGBM at 640×480 on the Pi's A76 cores would cost tens of ms of CPU
that AprilTag and YOLO need.

### Backend availability per FRCV target

| FRCV configuration | codec-stereo backend | Notes |
|---|---|---|
| `Debug/Release\|x64` (WSL2 dev) | `lavc_sw` | ~65 ms @1080p on a Ryzen; fine for the inner loop |
| `Debug/Release\|ARM64` (Orange Pi) | `rkmpp_hwenc` | the production path; 15 ms @1080p, 3 ms @640×480 |
| — | `rkmpp` | **do not use.** Its `KEY_MOTION_INFO` readback has confirmed silicon defects (only the top ~half of rows written; odd 16px columns stuck at a placeholder MV). `rkmpp_hwenc` sidesteps it entirely. |
| — | `d3d12_vme` | Windows-only, and never compiled upstream. Irrelevant to this project. |
| — | `ref_sad` | validation control only, 468–967 ms. Never select it in production. |

Pin the backend by name (`cs_config::backend_override`) per platform rather than relying on
`cs_init()`'s auto-probe, precisely so `rkmpp` can never be picked up by accident.

---

## 1. Prerequisites — three real blockers in FRCV, not in codec-stereo

These must land before any stereo node can work correctly. None is stereo-specific; all three are
latent gaps in the current node plumbing that stereo is simply the first consumer to expose.

### P0. `SourceResult` has no timestamp

`SourceResult.h` carries `json`, `frame`, `sourceId` — nothing else. Phase 2 item 2 of
`IMPLEMENTATION_PLAN.md` already calls for `frameNumber`, `captureTimeUs` and `producedTimeUs`;
that work was never done. **Stereo cannot be built without `captureTimeUs`** — pairing a left and
a right frame is meaningless without knowing how far apart in time they were captured.

*Work:* add the three fields to `SourceResult`, stamp `captureTimeUs` in `ISource::CaptureFrame`
implementations (`CameraFrameSource` immediately after `capture.read()` returns) and
`producedTimeUs` in `ISource::SetLatestResult`. Small, and it pays off for NT4 latency
compensation too.

### P1. `ISink::Process()` is not given both sources

`ISink::ProcessingThreadLoop` builds its `sources` vector from **only those bound sources whose
frame count changed since the last pass**, then calls `Process()` if the vector is non-empty. Two
free-running cameras will routinely deliver one at a time. A stereo node handed a lone left frame
can do nothing with it.

*Work:* handle this **inside the stereo nodes**, not by changing `ISink` — a per-`sourceId` "most
recent frame" slot plus a pairing rule (run when both slots are populated and
`|captureTimeUs_L − captureTimeUs_R| ≤ maxSkewUs`, then clear both). Keeping it local avoids
disturbing every existing sink for one node's needs, and the pairing policy is genuinely
stereo-specific.

### P2. Source binding has no left/right role

`Manager::BindSourceToSink(sourceId, sinkId)` and `ISink::BindSource` are order-of-binding only.
Getting left and right backwards flips the sign of every disparity, which presents as "no valid
blocks anywhere" (`min_disparity` gates out everything negative) rather than as an obvious error.

*Work:* add a narrow, explicit `Manager::BindStereoSources(int sinkId, int leftSourceId,
int rightSourceId)` rather than generalising the bind API with a role parameter. The stereo nodes
record the two source IDs and match incoming `SourceResult::sourceId` against them.

> There is already an orphaned `class StereoSink;` forward declaration at the top of `ISink.h`
> with no definition anywhere in the tree. Delete it or make it real; right now it's a dangling
> hint at exactly this work.

---

## 10.1 — Vendor and build `codec-stereo`

Mirror the `vkapriltag` precedent exactly: a git submodule under `third_party/`, built by
`install-deps.sh` with its own CMake, consumed by `FRCVLib.vcxproj` as a prebuilt static archive.
FRCV's own build stays MSBuild-only.

1. **Submodule.** `git submodule add https://github.com/yojobama/codec-stereo.git
   FRCV/third_party/codec-stereo`; add to `.gitmodules` next to `vkapriltag`.

2. **`scripts/install-deps.sh` — new `build_codec_stereo()`**, run by default (unlike
   `--with-webrtc` / `--with-nt4`, its dependencies are already installed for other reasons):

   ```sh
   cmake -S third_party/codec-stereo -B third_party/codec-stereo/build \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
     -DCS_BUILD_PIPELINE=ON  -DCS_BUILD_TOOLS=ON \
     -DCS_BUILD_TESTS=ON     -DCS_BUILD_HARNESS=OFF \
     -DCS_ENABLE_REF_SAD=ON  -DCS_ENABLE_LAVC=ON \
     $EXTRA          # Pi only: -DCS_ENABLE_RKMPP_HWENC=ON
   ```

   - **`-DCMAKE_POSITION_INDEPENDENT_CODE=ON` is required, not optional.** `codec_stereo` is a
     default-`STATIC` `add_library` and we link it into `libFRCVLib.so`; a non-PIC `.a` fails at
     link with `relocation R_X86_64_32S ... can not be used when making a shared object`. Its own
     CMakeLists doesn't set it, so we must.
   - `CS_BUILD_HARNESS=OFF` deliberately: the harness pkg-configs the *system* `opencv4`, and this
     project installs OpenCV 5 under `/usr/local`. Its own CMakeLists carries a comment about
     exactly that collision. We don't need the harness.
   - `CS_BUILD_TESTS=ON`, and run `ctest` from the install script — `test_calibration` is a cheap,
     genuine correctness check of the backend on the machine that will actually run it.
   - `CS_ENABLE_LAVC` needs `libavcodec` / `libavutil` / `libavformat` **with libx264 encode**.
     Verify once per machine with `ffmpeg -hide_banner -encoders | grep libx264`; Ubuntu's
     `libavcodec-dev` has it, a minimal ffmpeg build may not.
   - `CS_ENABLE_RKMPP_HWENC` needs `librockchip-mpp-dev` (pkg-config `rockchip_mpp`) on the Pi.

3. **`FRCVLib.vcxproj`** — both `ItemDefinitionGroup`s:
   - `AdditionalIncludeDirectories` += `../third_party/codec-stereo/include`
   - `AdditionalDependencies` += `../third_party/codec-stereo/build/libcodec_stereo.a`
   - `LibraryDependencies` += `m` (codec_stereo links it `PUBLIC`, and a static archive carries no
     link interface); ARM64 only: += `rockchip_mpp`. `pthread`, `avcodec`, `avutil` and `avformat`
     are already in the list.
   - `FrcvCommonDefines` += `FRCV_WITH_CODEC_STEREO`
   - the `PreBuildEvent` swig command line += `-DFRCV_WITH_CODEC_STEREO` — **easy to miss and it
     fails silently**: `swig.i` parses `Manager.h`, so any method inside a
     `#ifdef FRCV_WITH_CODEC_STEREO` block is simply absent from the generated C# if the flag
     isn't also passed there. The same trap is already documented for NT4 and WebRTC.

4. **`README.md`** — add `FRCV_WITH_CODEC_STEREO` to the feature-flag table and
   `third_party/codec-stereo` to the architecture list.

---

## 10.2 — Stereo calibration (OpenCV)

### `StereoCalibrationResult.h` — a new SWIG-safe header

Modelled on `CameraCalibrationResult.h`: plain `double`s and `std::vector<double>`, **no `cv::`
types whatsoever**, so `swig.i` can `%include` it the way it already does
`CameraCalibrationResult.h`.

```cpp
class StereoCalibrationResult {
public:
    CameraCalibrationResult left, right;   // per-eye intrinsics + distortion

    std::vector<double> R, T;              // right relative to left: 3x3 row-major, 3x1
    std::vector<double> E, F;              // 3x3 each
    std::vector<double> R1, R2;            // rectifying rotations, 3x3 each
    std::vector<double> P1, P2;            // rectified projections, 3x4 each
    std::vector<double> Q;                 // disparity-to-depth, 4x4

    double stereoRms   = 0.0;              // cv::stereoCalibrate's return value
    double epipolarRms = 0.0;              // mean |y_l - y_r| after rectification, in px
    double baselineMeters = 0.0;           // norm(T), == -P2[3] / P1[0]
    double rectifiedFx = 0.0;              // P1[0]
    double rectifiedCx = 0.0, rectifiedCy = 0.0;
    int imageWidth = 0, imageHeight = 0;

    bool IsValid() const { return !Q.empty() && baselineMeters > 0.0; }
};
```

`epipolarRms` is the load-bearing number, not `stereoRms`. codec-stereo invalidates any block
whose `|dy|` exceeds `cs_disparity_config::max_dy`, so residual vertical misalignment translates
directly into density collapse. **Gate at `epipolarRms < 0.5 px`** and surface it prominently in
the WebUI — a rig that fails this produces a mostly-empty depth map and no other symptom.

### `StereoCalibrator : public ISink, public ISource` (maxSources = 2)

Mirrors `CameraCalibrator`, with the pairing rule from P1 and the role map from P2.

- **Reuse, don't copy, the board detection.** `CameraCalibrator::ProcessCheckerboard` /
  `ProcessCharuco` are the logic we need, twice. Factor them into a small `BoardDetector` helper
  (`CalibrationBoardConfig` in, corners + object points out) that both `CameraCalibrator` and
  `StereoCalibrator` hold an instance of. That also keeps
  `<opencv2/objdetect/charuco_detector.hpp>` in one place, which keeps it away from `swig.i`.
- `SaveStereoDetection()` — saves a snapshot **only when the board was found in both eyes** of a
  time-paired frame. Return a distinguishable reason otherwise (left-only / right-only / skew too
  large) so the UI can tell the operator what to fix.
- **ChArUco caveat:** each eye can detect a *different subset* of corners. `cv::stereoCalibrate`
  needs per-view point lists corresponding index-for-index, so the two eyes' detections must be
  **intersected by corner ID** before being stored, with the object-point list rebuilt from that
  intersection. A plain checkerboard is all-or-nothing and has no such problem — recommend
  checkerboard for the first implementation and treat ChArUco as a follow-up.
- `RunCalibration()`:
  1. Per-eye intrinsics: prefer taking them from two already-run `CameraCalibrator` nodes and
     passing `cv::CALIB_FIX_INTRINSIC` (numerically far more stable, and reuses code that already
     works). Fall back to `cv::calibrateCamera` per eye over the stereo snapshots when no per-eye
     calibration exists.
  2. `cv::stereoCalibrate(...)` → `R, T, E, F, stereoRms`.
  3. `cv::stereoRectify(..., cv::CALIB_ZERO_DISPARITY, /*alpha=*/0, ...)` → `R1, R2, P1, P2, Q`
     plus both valid ROIs. `alpha=0` crops to the all-valid region, which is what we want:
     codec-stereo has no notion of an invalid border and would happily match garbage there.
  4. `baselineMeters = cv::norm(T)`; assert `|P2(0,3) / P1(0,0) + baselineMeters| < 1e-6` as a
     self-check that sign and units line up.
  5. `epipolarRms`: push the saved corner sets through `R1/P1` and `R2/P2` with
     `cv::undistortPoints` and take mean `|y_l − y_r|`.
  6. Minimum 8 paired snapshots (vs. `CameraCalibrator`'s 4 — stereo extrinsics have more DOF, and
     the same "1–3 views is actively misleading" argument applies harder).
- **Frame output:** side-by-side rectified pair with horizontal epipolar rules drawn across both,
  which makes a bad rectification instantly obvious over WebRTC. **JSON output:** pair count,
  per-eye found flags, last skew in µs, and the result once run.
- **Server side:** `StereoCalibrationManager` in C#, mirroring `CalibrationManager` but keyed by
  `(leftDevicePath, rightDevicePath, width, height)`, persisted to `stereoCalibrations.json`.

### OpenCV 5 module-rename check — do this first

`install-deps.sh` builds **OpenCV 5.0** into `/usr/local`, and codec-stereo's own CMakeLists
carries the note that *"OpenCV 5 renamed calib3d → calib"*. `CameraCalibrator.cpp` currently
includes `<opencv2/calib3d.hpp>` and links `opencv_calib3d`, and it builds — so today's include
path resolves a compatible layout. Before writing any of §10.2, run in WSL:

```sh
ls /usr/local/include/opencv*/opencv2/ | grep -E '^calib'
ls /usr/local/lib/libopencv_calib*
```

If it's genuinely OpenCV 5 with `opencv_calib`, then `stereoCalibrate` / `stereoRectify` /
`initUndistortRectifyMap` move headers and the vcxproj needs `opencv_calib` added to
`LibraryDependencies`. Cheap to check, annoying to discover mid-implementation.

---

## 10.3 — `StereoDepthNode` — rectify, extract, publish

`class StereoDepthNode : public ISink, public ISource` (maxSources = 2), same shape as
`ApriltagDetector` and `ObjectDetectionSink`.

### Backend abstraction

Follow the established `IApriltagBackend` / `IDetectionBackend` pattern exactly — including the
**plain, unscoped enum** (a scoped `enum class` becomes a broken opaque `SWIGTYPE_p_*` handle in
C#; this codebase has learnt that three times already), in its own header so the selector can be
`%include`d in `swig.i` independently:

```cpp
// StereoDepthBackendKind.h
enum StereoDepthBackendKind {
    STEREO_BACKEND_CODEC_AUTO,          // cs_init() auto-probe, minus rkmpp (see §0)
    STEREO_BACKEND_CODEC_LAVC,          // backend_override = "lavc_sw"
    STEREO_BACKEND_CODEC_RKMPP_HWENC,   // backend_override = "rkmpp_hwenc"
    STEREO_BACKEND_SGBM                 // cv::StereoSGBM, block-aggregated to the same grid
};

// IStereoDepthBackend.h
class IStereoDepthBackend {
public:
    virtual ~IStereoDepthBackend() = default;
    // rectified GRAY8 in; block-grid disparity out (CS_DISPARITY_INVALID for invalid cells)
    virtual bool Compute(const cv::Mat& rectLeft, const cv::Mat& rectRight,
                         std::vector<float>& disparityOut, int& cols, int& rows) = 0;
    virtual std::string Name() const = 0;
    virtual int BlockW() const = 0;
    virtual int BlockH() const = 0;
};
```

`CodecStereoBackend` (compiled under `FRCV_WITH_CODEC_STEREO`) and `SgbmStereoBackend` (always).
Requesting a codec backend that wasn't compiled in throws with a clear message, the way
`ObjectDetectionProvider::RKNN` does today. `SgbmStereoBackend` block-averages SGBM's per-pixel
output onto the same `cols × rows` grid so the two are trivially swappable and directly
comparable — that comparison is the whole point of shipping it.

### Per-frame path

1. **Pair** (P1 rule) → two `cv::Mat` BGR frames captured ≤ `maxSkewUs` apart.
2. **`cv::cvtColor(BGR2GRAY)` first, then `cv::remap`** — not the other way round. Remapping one
   channel instead of three is a straight 3× saving on the most expensive fixed cost in this path,
   and nothing downstream needs rectified colour (see the frame-output options below).
3. **Rectify** with maps built **once** at construction:
   `cv::initUndistortRectifyMap(K1, D1, R1, P1, size, CV_16SC2, map1x, map1y)` and the same for
   the right eye, then `cv::remap(..., cv::INTER_LINEAR)`. `CV_16SC2` fixed-point is the fast
   path; float maps are meaningfully slower for no accuracy that matters here. Rebuild the maps if
   the source resolution changes — a calibration is only valid at the exact resolution it was
   computed at, as `CameraCalibrationResult` already documents.
4. **Align dimensions** — crop the rectified pair to a multiple of the backend's native block
   (16 for `lavc_sw`, **32×16 for `rkmpp_hwenc`** — forced by hardware, not configurable). Crop
   inside the rectify ROI; never pad with black. A synthetic black border is a huge, perfectly
   matchable feature that will pull motion vectors towards it.
5. **Fill `cs_frame`** — `data[0] = mat.data`, `stride[0] = (int)mat.step`, `data[1] = nullptr`,
   `fmt = CS_PIX_FMT_GRAY8`. A `cv::Mat` ROI is not continuous but *is* correctly strided, so no
   copy is needed.
6. **`cs_extract()`** on the sink's own thread, then `cs_mv_field_to_disparity()` with
   `cs_disparity_config{ fx = result.rectifiedFx, baseline = result.baselineMeters, min_disparity,
   max_dy, max_cost }`, then `cs_disparity_to_depth()`.
   - The returned buffers are **owned by the backend and valid only until the next `cs_extract()`
     or `cs_destroy()`** — copy anything retained past the call.
   - **Use `cs_extract()` directly, not `cs_pipeline`.** The pipeline raises throughput, not
     per-pair latency, and FRC cares about latency. Revisit only if several stereo pairs end up
     running on one coprocessor.

### Configuring the disparity window — worked example

`disparity_offset` is described upstream as "load-bearing rather than a nicety": encoder motion
search is centred near zero, while real stereo disparity is one-sided and can be large. None of
the implemented backends can steer their search centre, so they physically pre-shift the right
image by `disparity_offset` and add it back afterwards. **We must compute it, or nearly every
block comes back invalid.**

Take a typical FRC camera at 640×480 with ~70° HFOV → `fx ≈ 457 px`, and a 15 cm baseline:

| Range Z | disparity `d = fx·B/Z` |
|---|---|
| 0.75 m | 91 px |
| 1 m | 69 px |
| 2 m | 34 px |
| 4 m | 17 px |
| 8 m | 8.6 px |

Over a 0.75–8 m working range, `d ∈ [8.6, 91]`: set `disparity_offset ≈ 50` and
`search_range_x ≥ 41`. That comfortably fits `rkmpp_hwenc`'s hard 9-bit MV ceiling (±63.75 px at
quarter-pel) — which it would **not** at 1080p with the same optics, since disparity scales with
width and 1080p over the same range needs ±123 px, past the ceiling. Saturated blocks come back
flagged `CS_BLK_CLAMPED` so it's detectable, but it argues for running stereo at 640×480 even when
other nodes run higher.

Expose this in the API as **`minDepthMeters` / `maxDepthMeters`** and derive `disparity_offset`
and `search_range_x` internally. Nobody should be typing pixel offsets into the WebUI, and the
derivation has to be redone whenever resolution or calibration changes anyway.

### Depth accuracy — set expectations up front

`ΔZ = Z² · Δd / (fx · B)`. At `fx = 457`, `B = 0.15 m`:

| Z | ΔZ at Δd = 0.5 px | ΔZ at Δd = 1 px |
|---|---|---|
| 1 m | 0.015 m | 0.03 m |
| 2 m | 0.058 m | 0.12 m |
| 4 m | 0.23 m | 0.47 m |
| 8 m | 0.93 m | 1.87 m |

Usable to ~4 m, mushy past that. Doubling the baseline to 30 cm halves every figure — **baseline
is the cheapest accuracy lever available and should be chosen deliberately**, bounded by how much
near field you're willing to lose to non-overlap.

### Sign convention — verify once, explicitly

codec-stereo's convention is "`dx` such that a LEFT point at `x` appears in RIGHT at `x+dx`", and
its own docs say plainly that whether that comes out positive for a given rig is *not knowable by
the library* and must be verified against a known-shift pair. Its own Middlebury harness got this
backwards on the first attempt and searched the wrong side of the image entirely.

*Work:* a `bool invertDisparitySign` config field, plus a startup self-check that synthesises a
known shift with `cs_shift_gray8()` from the first captured left frame and asserts the recovered
disparity has the expected sign. Log which way it resolved. Without this, the failure mode is
"every block invalid", which looks identical to a bad calibration, a bad exposure, or a dead
camera.

Related trap: `min_disparity`'s near-zero gate assumes positive-valid disparities. If cross-check
(`cs_disparity_cross_check`) is ever added, the backward R→L pass has *genuinely negative* valid
answers and must run with the gate disabled (`min_disparity = -1e6f`).

### Outputs

- **JSON** (`SourceResult::json`) — `{backend, cols, rows, blockW, blockH, validFraction,
  minDepthM, medianDepthM, extractMs, rectifyMs, skewUs}` plus an optional **coarse** depth grid,
  hard-capped in size. Never the full grid: 1080p at 16×16 is 8160 floats per frame, absurd over
  NT4 and not much better over the REST surface.
- **Frame** (`SourceResult::frame`) — one of three, selected by a plain enum
  `StereoFrameOutput { DEPTH_COLORMAP, RECTIFIED_LEFT, DEPTH_OVERLAY }`. `DEPTH_COLORMAP` is
  `cv::applyColorMap` over normalised disparity upscaled with `INTER_NEAREST` (or
  `cs_disparity_upsample`, a caller-invoked opt-in that is off by default upstream);
  `RECTIFIED_LEFT` exists so a detector can be bound downstream (§10.4) without rectifying twice.
  Either way `WebRTCSink` streams it for free — no new streaming work.
- **NT4** — `NetworkTablesSink` picks the JSON up for free. Publish summary stats and a small
  fixed set of ROI probes, not the grid.

---

## 10.4 — `DepthFusionNode` — the actual FRC payoff *(optional, high value)*

Object detection gives bearing but no range; stereo gives range. Fusing them gives "note at 2.4 m,
15° left", which is what a drive team can actually use.

`class DepthFusionNode : public ISink, public ISource` (maxSources = 2), bound to an
`ObjectDetectionSink` (or `ApriltagDetector`) **and** a `StereoDepthNode`. For each detection, take
the **median of valid disparities inside the bbox** (median, not mean — one background block
bleeding into the box wrecks a mean) and emit `distanceMeters`, plus `xMeters` / `yMeters` from the
pixel centre through `Q`.

The one thing to get right: **the detector must run on the rectified left image**, or its bbox
coordinates don't index into the disparity grid. That's what `StereoFrameOutput::RECTIFIED_LEFT` is
for — bind the detector to the depth node's frame output rather than to the raw camera. Costs one
extra node hop and zero extra rectification.

Sequence this after §10.3 is verified. It's the payoff, but it's worthless on top of a depth map
nobody has validated yet.

---

## 10.5 — Manager, SWIG, server, WebUI

**`Manager.h`** — primitives only, same discipline as NT4 and WebRTC (their headers pull in ntcore
/ libdatachannel C++ APIs and must never reach `swig.i`; `StereoCalibrator.h` will pull in
`charuco_detector.hpp` and is in exactly the same position):

```cpp
int  CreateStereoCalibrator(CalibrationBoardType boardType, int rows, int cols,
                            float squareSizeMeters, float markerSizeMeters, int arucoDictionaryId);
bool BindStereoSources(int sinkId, int leftSourceId, int rightSourceId);
bool SaveStereoCalibrationDetection(int calibratorId);
int  GetStereoCalibrationPairCount(int calibratorId);
StereoCalibrationResult RunStereoCalibration(int calibratorId);
StereoCalibrationResult GetStereoCalibrationResult(int calibratorId);

int    CreateStereoDepthNode(StereoDepthBackendKind backend, StereoCalibrationResult calibration,
                             double minDepthMeters, double maxDepthMeters,
                             int maxSkewUs, int frameOutput);
string GetStereoDepthBackendName(int sinkId);
```

Plus, in `swig.i`: `%include "StereoDepthBackendKind.h"` and `%include
"StereoCalibrationResult.h"` **explicitly** — a plain-enum or result header that `Manager.h` merely
`#include`s is not enough, as `YoloVariant`, `ApriltagBackendKind` and `CalibrationBoardType` each
demonstrated the hard way. `StereoCalibrationResult` also needs `%template(VectorDouble)
vector<double>`, which `swig.i` already declares.

**Server** — `Controllers/sinks/StereoCalibrationSinkController.cs` and
`StereoDepthSinkController.cs` following the existing controller shape; `SinkManager` node types
`"stereocalibrationsink"` / `"stereodepthsink"`; `DB.cs` persistence of the left/right source IDs
and the depth config so a stereo pair survives a restart; `StereoCalibrationManager` as in §10.2.

**WebUI** — a stereo pair page: pick two cameras → calibration wizard (capture pairs, per-pair
found/skew feedback, run, show **`epipolarRms` and `baselineMeters` prominently** with the 0.5 px
gate called out) → depth node config (backend, min/max depth, max skew, `max_dy`, `max_cost`, frame
output) → live view through the existing `WebRTCStream` component. Note the WebUI is already behind
on phases 3–7, so this queues behind that catch-up rather than jumping it.

---

## 10.6 — Verification

Matching this project's convention of distinguishing "compiled and linked" from "actually ran":

1. **Synthetic shift** *(UnitTests)* — `cs_shift_gray8()` a real camera frame by a known 16 px, push
   both through `CodecStereoBackend`, assert mean recovered disparity is 16.0 and validity is near
   100%. Upstream's own `test_calibration` does this, but running it through *our* wrapper is what
   proves our `cs_frame` filling, stride handling and sign convention.
2. **Rectification quality** — checkerboard through `StereoCalibrator`, assert
   `epipolarRms < 0.5 px`. Gate the rest of the testing on this.
3. **Metric accuracy** — board at tape-measured 1 / 2 / 4 m; compare median depth in the board ROI
   against truth and against the ΔZ table in §10.3. This is the number that decides whether the
   feature is useful.
4. **Backend comparison** — the same recorded pairs through `SgbmStereoBackend` and
   `CodecStereoBackend`; report bad-2.0%, RMSE, density and ms/frame on the Pi. Settles §0's open
   question on our own hardware.
5. **Latency on the real board**, including **VEPU contention**: `WebRTCSink`'s planned
   `h264_rkmpp` hardware encoding targets the *same two* RK3588 encoder cores `rkmpp_hwenc` uses.
   Measure stereo latency with and without a WebRTC stream running. If they fight, the fallback is
   software x264 for the stream, or capping stereo frame rate.
6. **Motion / sync** — swing an object laterally across the field of view at speed and watch depth
   stability. This is the test that exposes risk 1 below, and it will not show up on static targets.

---

## 10.7 — Risks and open questions

1. **Camera synchronisation — the largest risk, and not solvable in software.** Two independent USB
   UVC cameras free-run; capture skew can approach a full frame period (16–33 ms). An object moving
   laterally at 3 m/s at 2 m with `fx = 457` traverses ~11 px in 16 ms — a ~30% disparity error at
   that range, appearing as depth that swims whenever anything moves. Ranked options: **(a)** a
   hardware-synced stereo module (dual OV9281 behind one MIPI bridge, or an Arducam-style stereo HAT
   with a shared trigger) — the right answer if the hardware isn't bought yet; **(b)** two identical
   global-shutter cameras with fixed manual exposure and a tight `maxSkewUs` gate, accepting dropped
   pairs; **(c)** two rolling-shutter USB webcams — will produce a depth map, will not produce
   trustworthy depth on anything moving.
   *This needs a decision before hardware is ordered.*
2. **Rolling shutter** compounds (1): the two sensors scan at different phases, so even a perfectly
   time-aligned pair disagrees row by row on a moving scene. Global shutter is strongly recommended
   for stereo specifically.
3. **Block granularity.** 16 px blocks at 640×480 → a 40×30 grid. A game piece at 4 m subtending
   ~40 px covers 2–3 blocks. Adequate for *range to a detected object* (§10.4), useless for shape or
   for obstacle maps with any detail.
4. **codec-stereo's accuracy on our scenes is unmeasured.** §0's Middlebury numbers are a hard case,
   not our case, but they're the only data that exists. §10.6 item 4 is the mitigation, and
   `SgbmStereoBackend` is the escape hatch if the answer is bad — at which point the honest framing
   is "SGBM at reduced resolution on CPU", not "stereo doesn't work".
5. **VEPU contention with WebRTC** — see §10.6 item 5.
6. **One `cs_context` per node**, each with its own persistent MPP context and buffers. RK3588 has
   two encoder cores; a third concurrent stereo node will queue. Not a near-term problem (one stereo
   pair per coprocessor is the realistic deployment) but worth knowing before someone creates four.
7. **OpenCV 5's `calib3d` → `calib` rename** — cheap check in §10.2, annoying to hit mid-write.
8. **The `min_disparity` / negative-disparity subtlety** if cross-checking is ever added — §10.3.

---

## Suggested execution order

```
P0 (SourceResult timestamps) ─┬─> P1 (pairing) ──────┐
                              └─> P2 (stereo bind) ──┴─> 10.2 (calibration)
10.1 (build/vendor) ──────────────────────────────────┘        │
                                                               v
                                                      10.3 (depth node)
                                                               │
                                        ┌──────────────────────┼──────────────────────┐
                                   10.4 (fusion)        10.5 (server/UI)      10.6 (verification)
```

P0–P2 are small and unblock everything; 10.1 can proceed in parallel with them. **Do not start 10.3
before 10.2 passes its `epipolarRms < 0.5 px` gate** — an unrectified pair produces a
plausible-looking but meaningless depth map, and debugging codec-stereo's output against a bad
rectification is the single most likely way to lose a week on this feature.
