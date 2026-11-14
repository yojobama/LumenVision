# LumenVision

A PhotonVision-style FRC vision coprocessor for the Orange Pi 5 family (RK3588), with any Linux box (WSL2
included) or Windows machine usable for development. It does:

- **AprilTag detection** on the CPU (libapriltag) or the GPU (vkapriltag, Vulkan compute), with single-tag poses, a
  field-layout multi-tag solve and a floor-constrained solve;
- **YOLOv8/v11 object detection** on the NPU (RKNN) or ONNX Runtime, with optional **tracking** (stable ids and
  velocities for the detected objects);
- **camera and stereo calibration**, **stereo depth**, and **depth fusion** with detections;
- **recording** (segmented MP4 plus a per-frame telemetry sidecar), **WebRTC and MJPEG live view**, **snapshots**;
- **NetworkTables 4 publishing** and a robot-side Java vendordep (`lumenlib`) with a PhotonLib-compatible layer;
- a React **WebUI** for building the pipeline graph and managing the device (network, backups, updates, logs).

It ships as a `.deb` and as flashable images for the Orange Pi 5, 5 Plus, 5B, 5 Pro and 5 Max (see
[Releases](#releases-and-deploying-to-the-orange-pi)).

`docs/history/IMPLEMENTATION_PLAN.md` and `docs/history/STEREO_IMPLEMENTATION_PLAN.md` describe how the system was
first built, `docs/NATIVE_API_PLAN.md` and `docs/RELEASE_NOTES_native_api.md` the node API and the move away from
PhotonVision's pipeline model, `docs/RENAME.md` the FRCV to LumenVision rename, and `ROADMAP.md` the plan written when
the CMake and native-Windows work started (parts of it have since landed). Measurements on the board are in
`docs/research/hw-accel/IMPLEMENTATION_STATUS.md`. This file is the quick-start.

## Architecture

- **`LumenCore/`**: the C++ core: sources (camera, video, image, ROI/crop), sinks and detectors (AprilTag, object
  detection, stereo depth, depth fusion, recording, MJPEG, WebRTC, NetworkTables), built as `libLumenCore.so`
  (`LumenCore.dll` on Windows).
- **`Server/`**: a .NET 10 ASP.NET Core (Kestrel) server that gives the C++ library (through SWIG-generated bindings
  in `Server/Interop/`) a REST API, a `/ws/state` push channel and the WebUI. Its graph (sources, sinks, settings)
  is persisted on the device.
- **`webui/`**: the React/Vite WebUI.
- **`lumenlib/`** and **`robot/`**: the robot-side Java vendordep and an example robot project.
- **`third_party/vkapriltag/`**: git submodule, Vulkan-compute AprilTag detection (pinned to `v1.10.1`).
- **`third_party/codec-stereo/`**: git submodule, stereo depth from hardware video-encoder motion vectors.
- **`scripts/`**: dependency installer, `.deb` and board-image builders, the systemd unit, `deploy.ps1`.
- **`tools/`**: `lumen-bench` (AprilTag refine-mode benchmark) and `opencv-bench` (times the OpenCV calls LumenCore
  makes, so a version or a board can be compared).
- **`tests/`**, **`Server.Tests/`**: the C++ (Catch2) and C# (xUnit) suites.
- **`graph-profiles/`**, **`field-layouts/`**, **`models/`**: a shipped example graph, the WPILib field layouts (2022 to
  2026) and a place for object-detection models.

Clone with `git clone --recurse-submodules`, or run `git submodule update --init` afterwards.

## Building

LumenCore is built with CMake (3.25 or later) through the presets in `CMakePresets.json`; the Server is a normal .NET 10
project that builds LumenCore for you.

| Preset | For |
|---|---|
| `wsl-x64-debug`, `wsl-x64-release` | Linux x64, including WSL2 (local development) |
| `pi-arm64-debug`, `pi-arm64-release` | building on the Orange Pi itself (Rockchip RKNN and RGA on) |
| `windows-x64-debug`, `windows-x64-release`, `windows-x64-vs` | native Windows (MSVC) |
| `ci-linux-x64`, `ci-linux-arm64`, `ci-windows-x64` | what CI builds; `ci-linux-arm64` is what the `.deb` is made from |

1. **Dependencies.** Run `scripts/install-deps.sh` on the machine that compiles (Ubuntu 24.04 or Debian 13). It installs
   the system packages and builds OpenCV 5.1 (with opencv_contrib), FFmpeg and the other libraries that have no suitable
   package. Useful flags: `--with-nt4`, `--with-webrtc`, and on the Pi `--with-rknn --with-mpp --with-ffmpeg-rockchip`
   (`--with-bundled-ffmpeg` on x64, for the `.deb`); `--check` only reports what is missing. Expect the first run to take
   an hour or more, longer on the Pi's cores. From Windows, `scripts/install-deps.ps1 -Target wsl|pi` runs it over WSL or
   SSH. **LumenCore requires OpenCV 5.1 or later.** On native Windows the dependencies are built or unpacked by hand; copy
   `CMakeUserPresets.json.example` to `CMakeUserPresets.json` and point it at them (OpenCV, ONNX Runtime, WPILib, vcpkg).
2. **LumenCore:** `cmake --preset wsl-x64-release && cmake --build --preset wsl-x64-release`. The library lands in
   `out/build/<preset>/stage/<config>/`. `-DLUMEN_TARGET_CPU=cortex-a76.cortex-a55` tunes an arm64 build for the RK3588.
3. **Server:** `dotnet build Server -c Release` (or `dotnet publish -r linux-arm64`). It runs `cmake --build --preset`
   for the preset it derives from the runtime identifier (`-p:LumenCorePreset=<preset>` overrides), but only if that preset
   was configured already, and fails if the native library or the `Server/Interop` bindings (written by a CMake
   configure) are missing.
4. **WebUI:** `cd webui && npm ci && npm run dev` (proxies `/api` and `/ws` to a server on port 5800), or `npm run build`.

The server listens on port 5800 (an FRC team network passes 5800 to 5810).

### Feature flags

CMake options (`-DLUMEN_WITH_<NAME>=ON|OFF`); the presets set the platform ones. Disabled features still exist in the
generated C# API and throw when used, so the API is the same everywhere.

| Flag | Default | Notes |
|---|---|---|
| `LUMEN_WITH_ONNX` | on | ONNX Runtime object detection (CPU execution provider) |
| `LUMEN_WITH_NT4` | on | NetworkTables 4 publishing; needs `ntcore`/`wpiutil`/`wpinet` (`install-deps.sh --with-nt4`) |
| `LUMEN_WITH_WEBRTC` | on | WebRTC live view; needs `libdatachannel` (`--with-webrtc`) |
| `LUMEN_WITH_RECORD` | on | `RecordSink`: segmented MP4 recording with a telemetry sidecar (needs FFmpeg) |
| `LUMEN_WITH_VULKAN_APRILTAG` | on | Vulkan AprilTag backend (the `vkapriltag` submodule, a Vulkan compute device); falls back to the CPU backend if none is found |
| `LUMEN_WITH_CODEC_STEREO` | on | the `codec-stereo` depth backends (needs the submodule); `STEREO_BACKEND_SGBM` works without it |
| `LUMEN_WITH_RKNN` | off (aarch64 presets on) | the NPU object-detection backend |
| `LUMEN_WITH_RGA` | off (aarch64 presets on) | Rockchip RGA hardware scaling and colour conversion |

## Releases and deploying to the Orange Pi

Pushing a `v*` tag runs `.github/workflows/release.yml`, which builds on GitHub's arm64 and x64 runners and publishes a
GitHub Release with:

- `lumenvision-backend_<version>_arm64.deb` (and an `amd64` one without the Rockchip parts): the self-contained backend
  with its runtime libraries, the systemd unit and the sudoers rule the Settings page needs;
- `lumenvision-<board>_<version>.img.xz` for each Orange Pi 5, 5 Plus, 5B, 5 Pro and 5 Max: Armbian (Debian 13,
  vendor kernel) with the Mali G610 Vulkan driver and the package preinstalled; flash it and the board comes up as
  `lumenvision.local`;
- `lumenlib.json`, the robot vendordep (see below).

A manual `workflow_dispatch` run builds the same artefacts without publishing; it also uploads the arm64
`opencv-bench` and an optimised `LumenCoreTests`, for measuring on a board that has no compiler.

To update a running board, upload the `.deb` on the **Settings** page (it is checked, then installed by a systemd unit that
survives the server restarting), or `sudo dpkg -i lumenvision-backend_*_arm64.deb`. `scripts/deploy.ps1` is the older
route for a development board: it publishes the server self-contained for `linux-arm64`, builds the WebUI and copies both,
with a `libLumenCore.so` built on the Pi, to `/opt/lumenvision`. The unit `Conflicts=photonvision.service`, since both want
exclusive access to the same cameras.

## NetworkTables

NT4 publishing runs in C++ (`NetworkTablesSink`, against the real `ntcore`/`wpiutil`) — there is
no official WPILib C# binding, so this deliberately isn't a P/Invoke round-trip through the
server. Create one via `POST /api/networkTablesSink/createForTeam` (or `createForServer` for
bench testing against a local NT4 server), bind it to one or more detector nodes, and it
publishes each into its own subtable under the configured root table (default `/lumenvision`).

### Topics

Every camera and detector node publishes under `/<root>/<table>/`, where `<table>` is the node's name in the UI (characters
outside `A-Za-z0-9_.-` become `_`, and a repeated name gets `-<id>` appended; a node without a name uses its id). `/<root>/.graph`
lists every node with its `table`, kind, running state and input, so robot code finds nodes by name. `/<root>/.protocol` is the
topic-layout version (4); the vendordep refuses a coprocessor that speaks another one. Every value is stamped with the frame's
**capture time**, not its arrival time, so feed that timestamp to your robot's pose estimator.

| Topic | Type | Meaning |
|---|---|---|
| `result` | raw | The binary result packet (schema v2, below): targets, multi-tag pose, constrained pose. Sent with "send all" so a reader's queue sees every frame. |
| `hasTargets`, `targetYaw`, `targetPitch`, `targetArea`, `targetPose` | bool / numbers | The best target, flattened for dashboards. |
| `cameraIntrinsics`, `cameraDistortion` | double[] | The calibration in use (3x3 row-major; OpenCV distortion coefficients). |
| `config/enabled`, `config/fpsLimit` | bool / int | Start or stop the node (kept across restarts) and cap its published results per second. `status/*` reports the value in effect. |
| `config/recording` | bool | Record this node (annotated frames for a detector, raw for a camera); `status/recording` reports it. A recording left stopped stays stopped after a restart. |
| `config/snapshot` | any write | Save the node's latest frame (annotated for a detector, raw for a camera). |
| `config/constrainedSeed`, `config/robotToCamera` | double[3], double[7] | Start the floor-constrained solve: seed `[x, y, yaw]` (layout frame) and the camera mount `[x, y, z, qw, qx, qy, qz]`. |
| `/<root>/config/ledMode`, `config/recording` | int / bool | Global LED (-1 auto: lit while a detector runs, 0 off, 1 on, 2 blink) and recording. |
| `/<root>/heartbeat`, `/<root>/.version`, `/<root>/.protocol` | double / string / int | Liveness, the coprocessor's version (compared with the vendordep's) and its topic-layout version. |

### Frames and units

All poses are in WPILib frames: the camera is X forward / Y left / Z up, and a tag's frame is the one a field-layout tag pose
describes (X out of its face, Z up). A target's transforms are camera-to-tag; the multi-tag pose is the camera's pose in the
layout's own frame (relative to the layout file's origin, which the vendordep re-expresses for the current alliance origin).
**Yaw is positive to the left and pitch is positive up**, both in degrees.

### Result packet, schema v2

Big-endian. Header: `u16 version, u64 sequenceId, u32 latencyUs`, `u8 hasMultiTag [f64 t[3], f64 q[4], f32 reprojErr]`,
`u8 hasConstrained [f64 x, f64 y, f64 yaw, f32 reprojErr, u8 tagCount]`, `u8 idCount, u16 ids[]` (tags in the multi-tag
solve), `u16 targetCount`. Each target: `i16 fiducialId, i16 objectClassId, f32 confidence, f64 yaw, pitch, area, skew,
ambiguity`, best and alternate camera-to-tag as `f32 t[3], q[4]`, `f32` best and alternate reprojection error, `f32
corners[8]`, `f32 minAreaRect[8]`. `LumenCore/ResultPacket.h` is the specification; `tests/data/packet_v2.bin` locks it
between the C++ encoder and the Java decoder.

### Robot code: the `lumenlib` vendordep

`lumenlib/` is one Java artefact with two packages. `org.lumenvision.lib` is LumenVision's own API: `LumenCoprocessor`
(recording, the LED, the node graph), `LumenNode` (start, stop and snapshot any node by name), `LumenAprilTagDetector` (tags
with their poses, the multi-tag and constrained solves, the constrained-solve seed), `LumenObjectDetector` (objects with their
class, confidence and bounding box), both with calibration and the FPS limit, and `LumenPoseEstimator` (the coprocessor's
multi-tag and constrained solves as the robot's pose). A node opened as the wrong kind is reported as a driver-station warning. `org.lumenvision.photoncompat` maps PhotonLib's classes onto it (`PhotonCamera`, `PhotonPoseEstimator`, `PhotonUtils`,
the simulation), so a team migrating changes its imports:

- A PhotonVision pipeline is a detector node fed by the camera. `setPipelineIndex` runs that node and stops the camera's other
  detectors (pipelines are the camera's detectors in id order, or `new PhotonCamera("front").withPipelines("tags", "notes")`).
- Driver mode stops every detector of the camera; its video keeps streaming from the camera node.
- `takeInputSnapshot` saves the camera's own frame and `takeOutputSnapshot` the running detector's annotated one.
- Every pose strategy runs in `PhotonPoseEstimator`: the client-side ones from the targets and the field layout, the coprocessor
  ones (multi-tag, constrained SolvePnP) from the coprocessor. `MULTI_TAG_PNP_ON_RIO` is mapped to coprocessor multi-tag.

- **Install from a release:** add `https://github.com/yojobama/LumenVision/releases/latest/download/lumenlib.json`
  in the WPILib "Manage Vendor Libraries" dialog.
- **Install from a checkout:** `cd lumenlib && ./gradlew installIntoRobot` publishes to `lumenlib/build/maven-repo` and
  writes `robot/vendordeps/lumenlib.json` pointing at it (git-ignored). `robot/` is an example project: see
  `subsystems/VisionSubsystem.java` (the native API) and `PhotonCompatVisionSubsystem.java` (PhotonLib names).
- **Simulation:** `org.lumenvision.photoncompat.sim` has `VisionSystemSim`, `PhotonCameraSim`, `SimCameraProperties`,
  `VisionTargetSim` and `TargetModel`. A simulated camera projects targets through a pinhole model and publishes the same
  topics as the coprocessor, so `PhotonCamera` and the estimators read it unchanged. Poses are ground truth (zero
  ambiguity) and pixel noise moves the reported corners only; targets do not occlude each other.
- **Hardware in the loop:** no code is needed. Point the real coprocessor at the simulation's NT server
  (`createForServer`) and the robot code sees real detections in simulation.
- **Tests:** `cd lumenlib && ./gradlew test` (an in-process NT server and ntcore's JNI, unpacked from WPILib's native zips).

## Calibration

Calibration is a *session*, not a graph node: a session owns a private calibrator and a live preview
stream, never appears in the graph or `data.json`, and ends when stopped (or after 15 minutes idle).
Use the WebUI's **Calibration** tab, or the REST API:

`POST /api/calibration/camera/start?sourceId=` (default 6x9 checkerboard, 25mm squares; pass
`boardType`, `rows`, `cols`, `squareSizeMeters` — and `markerSizeMeters` for ChArUco — for another
board) returns a session id and a preview sink id (a WebRTC sink; the page falls back to an MJPEG one from
`POST /api/calibration/{id}/previewFallback` only if WebRTC cannot connect). Call
`POST /api/calibration/{id}/saveDetection` for each snapshot (need at least 4), then
`POST .../{id}/runCamera` to compute the result — calibration only happens when explicitly asked, never
implicitly. `POST .../{id}/stop` frees the session. Results persist to `calibrations.json`, keyed by
camera device path + resolution, and are listed by `GET /api/calibration/saved`.

An AprilTag detector bound to a camera picks up the camera's saved calibration at its current
resolution, so calibrate first and the detector gets real-world pose; a camera
with no calibration at its current resolution runs uncalibrated (the graph shows a "Calibration
stale" badge when a saved calibration is for a different resolution).

## AprilTag detection

`ApriltagSink` runs on the CPU (libapriltag) or on the GPU through the vkapriltag submodule (Vulkan compute,
pinned to tag `v1.10.1`); Vulkan falls back to CPU when no usable device exists. Pick the backend and tune it with
`PATCH /api/apriltagSink/backend` (or `refine*` parameters on `createWithBackend`); `GET /api/apriltagSink/tuning` returns what is actually in effect.

- **Refine edges** sharpens corner positions. On the Vulkan backend its implementation is selectable with `refineMode`
  (`0` upstream, `1` exact — the default, bit-identical to upstream, `2` fast, `3` ultra-fast, which only refines quads
  that already decode unrefined); `refineModeSupported` is false on the CPU backend, which always runs upstream's.
- **Refine margin cutoff** (`probeMargin`, grey levels, Vulkan only, `0` = off) skips edge refinement for quads whose decision
  margin before refinement is below it, which saves CPU time on frames full of non-tag shapes. `APRILTAG_VK_PROBE_MARGIN`
  overrides it; `GET /api/apriltagSink/tuning` reports the value in effect.
- **Frame ingest**: pooled grey frames are page-aligned so a GPU with `VK_EXT_external_memory_host` reads them in place
  (`ingestMode` is `host-import`; `LUMEN_APRILTAG_FRAME_IMPORT=0` forces the upload path). The Orange Pi's Mali driver does not
  offer that extension, but its unified memory already takes the frame as a direct write, so it reports `copy`.
  Setting the `APRILTAG_VK_REFINE` environment variable (`upstream|exact|fast|ultrafast`) overrides the mode inside the
  library, and the log warns when it differs from the request.
- **vkapriltag 1.10** turns its accuracy work on by default (a profile-fit edge refinement, retries for quads with a grey
  margin or fitted a cell too far out, a third corrected bit, per-family limits, early rejection of dark quads, block-averaging
  decimation, a noise-adaptive contrast floor) and is faster; each can be switched off with an environment variable
  (for example `APRILTAG_VK_BOX_DECIMATE=0`; see the library's CHANGELOG). Measurements are under [Performance](#performance).
  A tag that cannot be posed (a zero tag size, no calibration) is reported without a pose rather than stopping the detector.
- `lumen-bench --refine-mode=<mode|all>` (`tools/lumen-bench`) times the refine modes on any machine.

## Object detection

Upload a YOLOv8 or YOLOv11 export via `POST /api/model/upload` (multipart: `model`, optional
`labels`, plus `variant`/`inputSize`/thresholds as form fields), then create a sink with
`POST /api/objectDetectionSink/create` referencing the returned model id. The backend (ONNX
Runtime vs RKNN/NPU) is picked automatically from the uploaded file's extension: `.onnx`
(`ultralytics export format=onnx`) runs on ONNX Runtime's CPU execution provider; `.rknn` runs on
the Pi's NPU via `RknnDetectionBackend`, with the frame scaled and converted on the RGA. A `.rknn` export is produced offline,
on an x86 host, with `scripts/convert-yolo-rknn.py` (rknn-toolkit2 only runs there; the script's header has the container
command and the split-head ONNX export it needs). Confidence and overlap (NMS) cutoffs can be changed on a running
detector (`PATCH /api/objectDetectionSink/thresholds`).

### Tracking

Off by default. With it on (`PUT /api/objectDetectionSink/tracking?sinkId=&enabled=true`, or the *Track objects* switch
in the node's inspector) every detection also carries `trackId` and `vxPxPerS`/`vyPxPerS` (the box centre's velocity in
pixels per second) in the sink's JSON, in the recording sidecar and in the annotated frame (`#id`). The tracker is
SORT-style: each frame the tracks are moved by their velocity, detections are matched to them by the optimal assignment
(`cv::linearAssignment` on `1 - IoU`, so it needs OpenCV 5.1), matched tracks are corrected by an alpha-beta filter and
tracks unseen for `maxAgeMs` end. Time is the frames' capture time, so velocities are right at 30, 60 or 120 fps. An object
gets an id after `minHits` sightings in a row (until then `trackId` is -1, so a one-frame false positive takes none); ids are
never reused. Parameters: `minIoU` (0.1), `minHits` (3), `maxAgeMs` (500), `classAware` (true); they are saved with the sink and
copied with the node. The PhotonVision-compatible NetworkTables packet does not carry track ids. On the board tracking
costs about 17 microseconds a frame with a handful of objects.

## Recording

`RecordSink` records any frame-producing node (a raw camera, or a detector's annotated output) into a folder as
segments: `segment_000000.mp4`, `segment_000001.mp4`, ... plus a JSON-Lines **sidecar** per segment with one line per
frame (`frameNumber`, `captureTimeUs`, `producedTimeUs` and the node's JSON, so a recording of a detector carries its
detections and track ids). The MP4 is fragmented, so a power cut loses at most the last keyframe interval (2 s), and a new
file starts every `segmentSeconds` (300) on a keyframe; `maxFolderSizeBytes` / `maxFileCount` delete the oldest segments.
The encoder is the board's hardware one (`h264_rkmpp`) where there is one and `libx264` elsewhere, at 4000 kbps by default.

Frames are timed by their **capture time**, so a clip plays at the speed it was captured at whatever the camera's frame
rate (it is a variable-frame-rate file), and the encoder is opened with the rate it measures from the first frames, so the
bitrate means what it says: a 20 s recording at 60 fps on the board is a 20 s clip, 60 fps, about 10 MB.

`POST /api/recordSink/create` (then bind and toggle it like any sink), `GET /api/recordSink/{id}/segments`,
`GET /api/recordSink/{id}/download`; `POST /api/recordSink/node?NodeID=&enabled=` records one node without managing a sink,
and each node's `config/recording` topic does the same from the robot. A recording stopped on purpose stays stopped after a
restart. The sidecars also feed the tracker offline (`LUMEN_TRACK_REPLAY=<folder> LumenCoreTests "[.replay]"`).

## Live view

`WebRTCSink` streams any single frame-producing node (a raw camera, or a detector's annotated
output) over WebRTC (`libdatachannel`; FFmpeg `libx264`, or `h264_rkmpp` on the board's hardware encoder).
Signalling is plain REST (`/api/webrtcSink/*`), not a persistent
WebSocket: LumenVision uses non-trickle ICE on its side, so one offer/answer/candidate exchange
over ordinary HTTP requests is enough. `MjpegSink` (`/api/mjpegSink/*`) serves the same nodes as MJPEG for tiles and as the
fallback when WebRTC cannot connect; the board encodes JPEG in hardware too.

## Stereo depth

See `docs/history/STEREO_IMPLEMENTATION_PLAN.md` for the full design (disparity-window
derivation, sign convention, accuracy expectations, and the risks around camera synchronisation
worth reading before buying stereo hardware) and `ROADMAP.md` for the upcoming side-by-side
single-camera layout. The graph node is `StereoDepthSink`, bound to a left/right camera pair via
`PATCH /api/stereoDepthSink/{id}/bind` (`leftSourceId`/`rightSourceId`) — the ordinary
single-source `/api/sink/bind` doesn't apply here, since getting left/right backwards silently
flips the sign of every disparity.

Calibrate first, in a calibration session (see Calibration above): `POST /api/calibration/stereo/start`
with `leftSourceId`/`rightSourceId` (default 6x9 checkerboard, 25mm squares — ChArUco isn't
supported for stereo yet), or `POST /api/calibration/stereo/startSplit?sourceId=` for one
side-by-side stereo camera, which the session splits in half into left/right eyes. Call
`.../{id}/saveDetection` for each pair with the board visible to both eyes (need at least 8),
then `.../{id}/runStereo`. Check the returned `epipolarRms`, not `stereoRms` — gate real use at
< 0.5px, since that's what predicts whether the depth node will actually produce dense output.
Results persist to `stereoCalibrations.json`, keyed by both cameras' device paths + resolution
(a split camera is keyed by its own path for both eyes), and are listed by
`GET /api/calibration/savedStereo`.

Then `POST /api/stereoDepthSink/create` with a backend (`STEREO_BACKEND_SGBM` always available;
`STEREO_BACKEND_CODEC_LAVC`/`STEREO_BACKEND_CODEC_RKMPP_HWENC` need `LUMEN_WITH_CODEC_STEREO`,
on by default — see the feature-flag table above), a depth range, and a saved calibration result
(the request body), then bind the two cameras. `GET .../stats` returns the last pair's valid
fraction and median depth; the full per-block grid is in `/api/sink/getResult`'s JSON.

Optionally, `POST /api/depthFusionSink/create` fuses a detector's (AprilTag/object detection)
bounding boxes with a `StereoDepthSink`'s depth grid — bind the detector to the depth sink's own
rectified-left frame output (not a raw camera, or its bbox pixel coordinates won't index into
the same grid), then `PATCH .../attachDepthSource` to point it at the depth sink directly (a
plain C++ reference, not a bound source — the full depth grid is deliberately never serialised
through JSON). Each detection comes back with `distanceMeters`/`xMeters`/`yMeters` added.

The WebUI's "Stereo" page covers creating and binding stereo depth sinks, and choosing a saved stereo
calibration from the Calibration tab.

## WebUI

`/graph` is the primary way to build a pipeline: sources and sinks are canvas nodes, dragging a
connection between them performs the real REST bind (with structurally invalid connections — e.g.
a second source onto a single-source sink — rejected before the drag completes), and the
right-hand inspector covers per-node parameters, a live WebRTC preview, the latest result JSON,
and starting and stopping each node. State (topology, per-node FPS/latency, device stats) is pushed
over a `/ws/state` WebSocket, not polled.
**Graph shortcuts.** Select a node, then Ctrl+C / Ctrl+V copies it (Ctrl+Shift+V also keeps the copy's source binding), F2 renames
it in the inspector and Delete removes it. Copying runs on the server (`POST /api/sink/duplicate`, `POST /api/source/duplicate`):
detector nodes (AprilTag or object detection) and file sources can be copied; a physical camera cannot (a device opens once),
so give it another pipeline instead.

**Camera settings** (camera node inspector):

- *All camera controls* lists every control the device reports over V4L2 (brightness, white balance, exposure, ...), as sliders,
  switches, menus and buttons; values are saved per camera and reapplied at start. Windows (OpenCV) cameras only offer the exposure
  and gain fields.
- *Rotate, mirror and crop* reshapes the camera's frames (`GET/PATCH /api/cameraSource/{id}/transform`). Saved calibrations follow the
  transform automatically (the principal point, focal lengths and tangential distortion are remapped), but calibrate with the transform
  cleared: sessions refuse a transformed camera.
- Several detectors can be bound to one camera and switched by starting and stopping them; a configuration saved with the older
  per-camera pipeline profiles is converted when it loads (each profile becomes a detector node, the active one running, and the
  original file is kept as `data.json.before-nodes`).
- **Snapshots** of a camera or a detector can be taken from the inspector or by a robot (`PhotonCamera.takeInputSnapshot()`); the `/snapshots`
  page lists, downloads and deletes them.
- *Stream quality* (live view and MJPEG): bitrate, frame rate, a size divisor and JPEG quality, applied to the running stream and saved.

**Detector settings** (detector node inspector): besides threads, decimation and refine method, the AprilTag detector offers the tag
family (36h11, 16h5, 25h9, Standard41h12), blur, the corrected-bit limit, a decision-margin cutoff, pose iterations and switches for
multi-tag and single-tag poses. Blur is CPU-only (the GPU pipeline has no blur stage); everything else works on both backends. Tuning a
camera's active pipeline detector is saved into that pipeline. The WPILib field layouts for 2022-2026 are bundled
(`field-layouts/`, listed by `GET /api/fieldLayouts`); pick one or upload a JSON, per detector or per pipeline, to enable multi-tag
poses. Object detectors have confidence and overlap (NMS) cutoffs that can be changed while running, and a camera can hold object
detection pipelines next to AprilTag ones. `/models` uploads, renames and retunes models (changing a model's cutoffs retunes the detectors
running on it).

`/calibration` is its own tab: pick a camera (or a stereo pair, or one side-by-side camera) and a board, then
start a session. Its wizard (`/calibration/camera/:sessionId`, `/calibration/stereo/:sessionId`) shows the
live overlay, captures with a coverage heatmap showing where the board has and hasn't been seen
in-frame, and runs with a pass/fail light against the real gate (`epipolarRms` < 0.5px for stereo,
not `stereoRms`; a configurable RMS gate for mono). The tab also lists running sessions (to resume
or stop them), each camera's calibration status, and every saved calibration.

`/match` is a read-only, per-camera table (FPS, latency, detector binding, NT4 connection state)
plus CPU/RAM/disk/temperature — meant to be legible across a pit during a match, not for editing
anything.

## Device settings

The Settings page (`/settings`) manages the coprocessor itself. Everything is stored on the device, so every browser sees the same values:

- **NetworkTables and LED** (`GET/PUT /api/device/settings`, `settings.json`): how to reach the robot's NT4 server (team number or an address, root
  table, client identity) and the LED GPIO. Every NetworkTables sink connects from these settings, is rebuilt when they change, and is
  restored after a restart.
- **Network** (`/api/network`): hostname (`hostnamectl`, the board answers to `<name>.local`) and DHCP or a static IPv4 address through NetworkManager
  (`nmcli`). A static address that is not confirmed from the new address within a minute is undone, and so is one still unconfirmed when the server
  restarts, so a typo cannot lock the device out. Boards without NetworkManager show the settings as unsupported.
- **Backup and restore** (`/api/device/settings/export`, `/import`): one ZIP of the graph, settings, graph profiles, uploaded field layouts and
  calibrations (models and the log optional). An import validates the archive's manifest and every entry name, backs up what it replaces to
  `backups/` (the last five are kept), and restarts the server.
- **Restart, reboot, shut down, factory reset** (`/api/device/restart`, `reboot`, `shutdown`, `factoryReset`): a factory reset needs the typed phrase
  and can keep calibrations, models and media.
- **Update** (`/api/device/update/*`): upload a `lumenvision-backend` .deb built for the board (checked: ar archive, package name, architecture), then
  install it. The install runs as root in its own systemd unit, so it survives the server restarting, and its output is shown on the page.
- **Logs**: the server's own output, LumenCore's log and the store's log are gathered in one place (`GET /api/log/tail|entries|download` with `level` and
  `since` filters, `/ws/logs` live) and written to daily files under `logs/`. Press the backtick key anywhere in the UI for the log drawer.
  **Nothing is logged to stdout**, every log call is kept, and disk use is bounded: LumenCore's and the store's log files (`LumenVision.log`,
  `DBLog.txt`) rotate at 10 MB keeping 3 older copies each, a file that outgrew that before this existed is cut to its newest lines at start, and
  the server's `logs/` folder rolls into numbered parts at 10 MB, deletes the oldest files beyond a 50 MB budget and keeps 7 days. All four
  numbers are under Settings -> Logs, which also shows the disk use and can clear every log (`GET /api/log/usage`, `DELETE /api/log`). The package
  also caps the systemd journal at 20 MB.
- **Accelerators and version**: GPU, memory-controller and NPU frequency, governor and load on the Dashboard and Match pages (from devfreq; the NPU's
  load is read from `/sys/kernel/debug/rknpu/load` as root), and the server, LumenCore and operating system versions under About.

**Security.** These operations run as root. The package's postinst gives the `lumen` user passwordless sudo (`/etc/sudoers.d/lumenvision`, validated with
`visudo` before it is installed, removed on purge) and the server runs commands through `sudo -n` with each argument passed separately, never through a
shell. The HTTP API has no authentication, so **anyone who can reach the server's port can restart, reboot, re-address or update the board**. Keep it on the
robot network or put it behind something that authenticates.

## Performance

On the Orange Pi 5 Plus (RK3588, Mali-G610), with the harness in `docs/research/hw-accel/profiles/harness/` (Vulkan detectors with
the ultrafast refine preset, `probeMargin` 4), release `imagetest42` (vkapriltag 1.9.0, OpenCV 5.0) against the current
branch (vkapriltag 1.10.1, OpenCV 5.1):

| workload | detect time | service CPU | GPU load |
|---|---|---|---|
| 1 detector, decimation 2 | 3.1 to 1.7 ms | 0.11 to 0.07 cores | 6 to 4% |
| 1 detector, decimation 1 | 8.0 to 4.2 ms | 0.24 to 0.13 cores | 17 to 11% |
| 3 detectors, decimation 2 | 4.1-4.5 to 1.7-2.5 ms | 0.50 to 0.29 cores | 27 to 17% |
| 3 detectors, decimation 1 | 11-16 to 7.4-9.3 ms | 1.28 to 0.83 cores | 86 to 57% |

Against PhotonVision v2026.3.4 on the same board, cameras and scene (AprilTags at decimation 1, 1280x720@30): LumenVision's Vulkan detector uses 0.31 cores
against 2.29 and reports an 18 ms frame age against 38 ms; its CPU backend 1.67 cores; the NPU detector on the same YOLOv8n model 0.08 cores against 0.74
and 29 ms against 39 ms; at 2560x720@60 LumenVision delivers 60 fps where PhotonVision reported 30. The Vulkan path uses more memory (360 MB against 280 MB) and
3-10% of the GPU. Method, caveats and every number are in `docs/research/hw-accel/IMPLEMENTATION_STATUS.md`.

Accuracy (a synthetic corpus with ground truth, RX 9060 XT): corner error 0.209 to 0.189 px median at decimation 1, with the
same tags found and no wrong ids, and rotation error 6.1 to 5.1 degrees (libapriltag 6.4-6.6). OpenCV 5.1 makes
`calibrateCamera` 32% and `warpPerspective` 40% faster on x64. The write-ups, the raw runs and how to repeat them are in
`docs/research/hw-accel/IMPLEMENTATION_STATUS.md`; `scripts/bench-pi.sh` samples a running service's stage timers, CPU
frequencies and accelerator load.

## Tests

- **C++ (`tests/`, Catch2):** configure with `-DLUMEN_BUILD_TESTS=ON` (the `ci-*` presets do) and build the
  `LumenCoreTests` target; `ctest --preset ci-linux-x64` runs it. Tags select areas: `[calibration]` drives
  `CameraCalibrator` and `StereoCalibrator` with synthetic checkerboard renders; `[apriltag]` and `[pose]` cover the detectors
  (including a Vulkan-versus-CPU agreement check in every refine mode, which needs a Vulkan device); `[tracker]` covers the
  object tracker (unit tests, rendered footage with ground truth through a real `ObjectDetectionSink`, a real YOLO model on
  a panning photo, and replay of a recorded sidecar); `[record]` the recording sink's timing; `[constrained]`, `[frames]` and
  `[packet]` the floor-constrained solver, the WPILib frame conversions and the result packet's golden file. Tests tagged
  `[hitl]` need the board's hardware (Vulkan on the Mali, the NPU, the hardware codecs) and are skipped without it; hidden
  tests (`[.bench]`, `[.replay]`) are diagnostics run by name. The target links against the shared LumenCore library; on
  Windows `LUMEN_BUILD_TESTS` makes LumenCore export all its symbols so the test executable can link (production builds do
  not), and the native dependency folders must be on `PATH`. The sanitiser builds (`-DLUMEN_SANITIZE=address,undefined` or
  `thread`) and the arm64 sanitiser workflow are described in `docs/research/hw-accel/SANITISERS_AND_PROFILING.md`.
- **C# (`Server.Tests/`, xUnit):** `dotnet test Server.Tests` after building LumenCore for your preset
  (pass `-p:LumenCorePreset=` as for `Server.csproj`). It covers calibration result persistence and session error paths,
  the graph's load and restore paths, the AprilTag and object-detector settings (refine modes, tracking), model handling, the
  device-settings services and the NT control service; on Windows the native dependency folders must be on `PATH`, as when
  running the Server.
- **Java (`lumenlib/`):** `cd lumenlib && ./gradlew test`.
- **CI:** `.github/workflows/ci.yml` builds and tests Linux x64 on every push to `main` and every pull request.

## Known gaps

- The HTTP API has **no authentication** (see Security under Device settings): keep the board on the robot network.
- `.rknn` models are produced offline on an x86 host (`scripts/convert-yolo-rknn.py`); nothing converts on the board.
- Tracking is IoU-based: an object that moves farther than its own size between two frames (a very low frame rate or a very
  fast object) will not be matched.
- On native Windows the third-party libraries (OpenCV, ONNX Runtime, WPILib) are not fetched for you.
- A camera cannot be copied in the graph (a device opens once), and ChArUco boards are not supported for stereo calibration.
- The speed and accuracy figures above come from one board and one desktop GPU; `docs/research/hw-accel/` says what each
  was measured on and what was not.

See `docs/history/IMPLEMENTATION_PLAN.md`'s phase status lines for the historical record and `ROADMAP.md` for the plan
written at the start of the CMake and Windows work.
