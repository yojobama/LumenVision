# LumenVision

A PhotonVision-style FRC vision coprocessor: AprilTag detection (CPU or Vulkan-accelerated),
YOLOv8/v11 object detection, camera calibration, WebRTC live view, NetworkTables 4 publishing,
and stereo depth — targeting an Orange Pi 5 / 5 Plus (RK3588) in production, with any Linux box
(WSL2 included) usable for development.

See `docs/history/IMPLEMENTATION_PLAN.md` for the design rationale behind the original phases
and `ROADMAP.md` for what's being worked on next (a robot-side vendordep, a CMake +
native-Windows build, side-by-side stereo cameras, explicit capture resolution, and better
hardware utilisation). This file is the quick-start.

## Architecture

- **`LumenCore/`** — the C++ core: sources (camera/video/image), sinks (AprilTag, object
  detection, calibration, NetworkTables, WebRTC), built as `libLumenCore.so`.
- **`Server/`** — a .NET 10 ASP.NET Core (Kestrel) web server, gluing the C++ library (via SWIG
  bindings) to the WebUI.
- **`webui/`** — the React/Vite WebUI.
- **`third_party/vkapriltag/`** — git submodule; Vulkan-compute AprilTag detection
  (`git submodule update --init` after cloning).
- **`third_party/codec-stereo/`** — git submodule; stereo depth via hardware video-encoder
  motion vectors (see `docs/history/STEREO_IMPLEMENTATION_PLAN.md`).

## Building

**No CMake, no Linux-native build (yet — see `ROADMAP.md`).** Visual Studio drives everything:
`LumenCore.vcxproj` builds inside WSL2 (`Debug|x64` / `Release|x64`, local dev) or remotely on
the Orange Pi over SSH via the Remote_GCC toolset (`Debug|ARM64` / `Release|ARM64`, the actual
target). `Server.csproj` consumes the built `.so` as a prebuilt artifact — it does not build
LumenCore itself, so `dotnet publish` works even from a machine with no C++ toolchain, as long
as the `.so` exists.

1. Copy `LumenCore/Local.props.example` to `LumenCore/Local.props` and fill in your WSL distro
   name and/or the Orange Pi's SSH details (gitignored — never commit a password there; Visual
   Studio's Connection Manager handles credentials separately).
2. Run the dependency script **on the machine that will compile**:
   - Inside WSL, for the `x64` configurations: `./scripts/install-deps.ps1 -Target wsl`
   - On the Orange Pi, for the `ARM64` configurations: `./scripts/install-deps.ps1 -Target pi`
     (or run `scripts/install-deps.sh` directly over SSH)
   - `install-deps.sh` builds OpenCV 5.0 and a few other dependencies from source; expect the
     first run to take an hour or more, especially on the Pi's ARM cores.
3. Open `LumenVision.sln` in Visual Studio, pick a configuration, build.
4. `git submodule update --init` before building with `LUMEN_WITH_VULKAN_APRILTAG` (on by
   default) — `third_party/vkapriltag` needs to be checked out.

### Feature flags

Set as MSBuild preprocessor defines in `LumenCore.vcxproj` (`LumenCommonDefines` /
`LumenPlatformDefines`), not build options — there's no CMake to hold them yet:

| Flag | Default | Notes |
|---|---|---|
| `LUMEN_WITH_ONNX` | on, all platforms | Stock ONNX Runtime, CPU execution provider only |
| `LUMEN_WITH_NT4` | on, all platforms | Needs `ntcore`/`wpiutil`/`wpinet` (install-deps.sh `--with-nt4`) |
| `LUMEN_WITH_WEBRTC` | on, all platforms | Needs `libdatachannel` (install-deps.sh `--with-webrtc`) |
| `LUMEN_WITH_VULKAN_APRILTAG` | on, all platforms | Needs the `vkapriltag` submodule + a Vulkan compute device; falls back to CPU automatically if none is found |
| `LUMEN_WITH_RKNN` | ARM64 only | The Orange Pi's NPU; `RknnDetectionBackend` runs object detection on it (native NV12 input and per-NPU-core pinning are still open, see ROADMAP.md C1) |
| `LUMEN_WITH_CODEC_STEREO` | on, all platforms | Needs the `codec-stereo` submodule (install-deps.sh builds it by default, unconditionally); `STEREO_BACKEND_SGBM` remains available without this flag |

## Deploying to the Orange Pi

`scripts/deploy.ps1` — publishes the server self-contained for `linux-arm64` (no .NET needed on
the Pi), builds the WebUI, copies both plus the Visual-Studio-built `libLumenCore.so` to
`/opt/lumenvision`, and installs/restarts the `lumenvision` systemd unit
(`scripts/lumenvision.service`). Run it from Windows after building the `ARM64`/`Release`
configuration in Visual Studio.

The unit `Conflicts=photonvision.service`, since both want exclusive access to the same camera
device(s) — see `ROADMAP.md`'s notes on running the two side by side for benchmarking.

## NetworkTables

NT4 publishing runs in C++ (`NetworkTablesSink`, against the real `ntcore`/`wpiutil`) — there is
no official WPILib C# binding, so this deliberately isn't a P/Invoke round-trip through the
server. Create one via `POST /api/networkTablesSink/createForTeam` (or `createForServer` for
bench testing against a local NT4 server), bind it to one or more detector nodes, and it
publishes each into its own subtable under the configured root table (default `/lumenvision`).

### Topics

Each camera publishes under `/<root>/<camera>/`, where `<camera>` is the camera's name in the UI (characters outside
`A-Za-z0-9_.-` become `_`; a detector's id also works as an alias). Every value is stamped with the frame's
**capture time**, not its arrival time, so feed that timestamp to your robot's pose estimator.

| Topic | Type | Meaning |
|---|---|---|
| `result` | raw | The binary result packet (schema v2, below): targets, multi-tag pose, constrained pose. Sent with "send all" so a reader's queue sees every frame. |
| `tags/*`, `multitag/*` | numbers | Flat per-tag and multi-tag values for dashboards. |
| `cameraIntrinsics`, `cameraDistortion` | double[] | The calibration in use (3x3 row-major; OpenCV distortion coefficients). |
| `config/pipelineIndex`, `config/driverMode`, `config/fpsLimit` | int / bool / int | Robot-written controls. `status/*` reports the value in effect. |
| `config/inputSnapshot`, `config/outputSnapshot` | any write | Save a raw or annotated snapshot. |
| `config/constrainedSeed`, `config/robotToCamera` | double[3], double[7] | Start the floor-constrained solve: seed `[x, y, yaw]` (layout frame) and the camera mount `[x, y, z, qw, qx, qy, qz]`. |
| `/<root>/config/ledMode`, `config/recording` | int / bool | Global LED (-1 default, 0 off, 1 on, 2 blink) and recording. |
| `/<root>/heartbeat`, `/<root>/.version` | double / string | Liveness and the coprocessor's version (compared with the vendordep's). |

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

### Robot code: the `photoncompat` vendordep

`photoncompat/` is a Java vendordep with PhotonLib-shaped classes (`org.lumenvision.photoncompat.compat.PhotonCamera`,
`PhotonPoseEstimator`, ...) and native ones (`LumenCamera`, `LumenPoseEstimator`). It covers `getLatestResult` and
`getAllUnreadResults`, driver mode, pipeline index, snapshots, LED, FPS limit, camera intrinsics, every pose strategy
(lowest ambiguity, closest to height/reference/last, average, coprocessor multi-tag with fallback, distance/trig solve and
constrained SolvePnP) and a version check. `MULTI_TAG_PNP_ON_RIO` is mapped to coprocessor multi-tag.

- **Install from a release:** add `https://github.com/yojobama/LumenVision/releases/latest/download/photoncompat.json`
  in the WPILib "Manage Vendor Libraries" dialog.
- **Install from a checkout:** `cd photoncompat && ./gradlew installIntoRobot` publishes to `photoncompat/build/maven-repo` and
  writes `robot/vendordeps/photoncompat.json` pointing at it (git-ignored). `robot/` is an example project: see
  `subsystems/VisionSubsystem.java`.
- **Simulation:** `org.lumenvision.photoncompat.sim` has `VisionSystemSim`, `PhotonCameraSim`, `SimCameraProperties`,
  `VisionTargetSim` and `TargetModel`. A simulated camera projects targets through a pinhole model and publishes the same
  topics as the coprocessor, so `PhotonCamera` and the estimators read it unchanged. Poses are ground truth (zero
  ambiguity) and pixel noise moves the reported corners only; targets do not occlude each other.
- **Hardware in the loop:** no code is needed. Point the real coprocessor at the simulation's NT server
  (`createForServer`) and the robot code sees real detections in simulation.
- **Tests:** `cd photoncompat && ./gradlew test` (an in-process NT server and ntcore's JNI, unpacked from WPILib's native zips).

## Calibration

Calibration is a *session*, not a graph node: a session owns a private calibrator and a live preview
stream, never appears in the graph or `data.json`, and ends when stopped (or after 15 minutes idle).
Use the WebUI's **Calibration** tab, or the REST API:

`POST /api/calibration/camera/start?sourceId=` (default 6x9 checkerboard, 25mm squares; pass
`boardType`, `rows`, `cols`, `squareSizeMeters` — and `markerSizeMeters` for ChArUco — for another
board) returns a session id and a preview sink id (`/stream/mjpeg?SinkID=`). Call
`POST /api/calibration/{id}/saveDetection` for each snapshot (need at least 4), then
`POST .../{id}/runCamera` to compute the result — calibration only happens when explicitly asked, never
implicitly. `POST .../{id}/stop` frees the session. Results persist to `calibrations.json`, keyed by
camera device path + resolution, and are listed by `GET /api/calibration/saved`.

An AprilTag pipeline profile picks up the source camera's saved calibration at its current
resolution when it is activated, so calibrate first and the profile gets real-world pose; a camera
with no calibration at its current resolution runs uncalibrated (the graph shows a "Calibration
stale" badge when a saved calibration is for a different resolution).

## AprilTag detection

`ApriltagSink` runs on the CPU (libapriltag) or on the GPU through the vkapriltag submodule (Vulkan compute,
pinned to tag `v1.6.0`); Vulkan falls back to CPU when no usable device exists. Pick the backend and tune it with
`PATCH /api/apriltagSink/backend` (or `refine*` parameters on `createWithBackend` and
`POST /api/source/profiles/apriltag`); `GET /api/apriltagSink/tuning` returns what is actually in effect.

- **Refine edges** sharpens corner positions. On the Vulkan backend its implementation is selectable with `refineMode`
  (`0` upstream, `1` exact — the default, bit-identical to upstream, `2` fast, `3` ultra-fast, which only refines quads
  that already decode unrefined); `refineModeSupported` is false on the CPU backend, which always runs upstream's.
  Setting the `APRILTAG_VK_REFINE` environment variable (`upstream|exact|fast|ultrafast`) overrides the mode inside the
  library, and the log warns when it differs from the request.
- Measured on the reference 1280x800 image (RX 9060 XT, Windows, 50 iterations, decimation 2): CPU 9.57 ms; Vulkan
  upstream 1.22 ms, exact 1.12 ms, fast 1.01 ms, ultra-fast 0.76 ms. `lumen-bench --refine-mode=<mode|all>` reproduces
  this on any machine.

## Object detection

Upload a YOLOv8 or YOLOv11 export via `POST /api/model/upload` (multipart: `model`, optional
`labels`, plus `variant`/`inputSize`/thresholds as form fields), then create a sink with
`POST /api/objectDetectionSink/create` referencing the returned model id. The backend (ONNX
Runtime vs RKNN/NPU) is picked automatically from the uploaded file's extension — `.onnx`
(`ultralytics export format=onnx`) runs on ONNX Runtime's CPU execution provider; `.rknn` runs on
the Pi's NPU via `RknnDetectionBackend`. A `.rknn` export is produced offline with
`rknn-toolkit2` on an x86 host — there's no on-device or in-repo converter yet.

## Live view

`WebRTCSink` streams any single frame-producing node (a raw camera, or a detector's annotated
output) over WebRTC (`libdatachannel` + ffmpeg `libx264`; hardware encoding via `h264_rkmpp` on
the Pi is a follow-up). Signalling is plain REST (`/api/webrtcSink/*`), not a persistent
WebSocket — LumenVision uses non-trickle ICE on its side, so one offer/answer/candidate exchange
over ordinary HTTP requests is enough.

## Stereo depth

See `docs/history/STEREO_IMPLEMENTATION_PLAN.md` for the full design (disparity-window
derivation, sign convention, accuracy expectations, and the risks around camera synchronization
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
plain C++ reference, not a bound source — the full depth grid is deliberately never serialized
through JSON). Each detection comes back with `distanceMeters`/`xMeters`/`yMeters` added.

The WebUI's "Stereo" page covers creating and binding stereo depth sinks, choosing a saved stereo
calibration from the Calibration tab.

## WebUI

`/graph` is the primary way to build a pipeline: sources and sinks are canvas nodes, dragging a
connection between them performs the real REST bind (with structurally invalid connections — e.g.
a second source onto a single-source sink — rejected before the drag completes), and the
right-hand inspector covers per-node parameters, a live WebRTC preview, the latest result JSON,
and pipeline profile switching. State (topology, per-node FPS/latency, device stats) is pushed
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
- A pipeline profile can keep its **own camera settings** (control values, transform, FPS limit): tick *own camera settings* on the profile.
  While that profile is active, camera edits belong to it, and the camera's own settings come back when another profile is activated.
- Input and output **snapshots** can be taken from the inspector or by a robot (`PhotonCamera.takeInputSnapshot()`); the `/snapshots`
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


## Tests

- **C++ (`tests/`, Catch2):** configure with `-DLUMEN_BUILD_TESTS=ON` (the `ci-*` presets do) and build the
  `LumenCoreTests` target. The calibration tests (`[calibration]`) drive `CameraCalibrator` and
  `StereoCalibrator` with synthetic checkerboard renders; the AprilTag tests (`[apriltag]`) include a
  Vulkan-versus-CPU agreement check in every refine mode, which needs a Vulkan device. The target links
  against the shared LumenCore library; on Windows `LUMEN_BUILD_TESTS` makes LumenCore export all its
  symbols so the test executable can link (production builds do not), and the native dependency
  folders must be on `PATH`. The `[constrained]`, `[frames]` and `[packet]` tests cover the floor-constrained solver, the
  WPILib frame conversions and the result packet's golden file.
- **C# (`Server.Tests/`, xUnit):** `dotnet test Server.Tests` after building LumenCore for your preset
  (pass `-p:LumenCorePreset=` as for `Server.csproj`). It covers calibration result persistence,
  calibration session error paths, the legacy-record handling in `DB.Load` and the AprilTag refine-mode
  persistence and defaults, and the NT control service; on Windows the
  native dependency folders must be on `PATH`, as when running the Server.

## Known gaps

See `docs/history/IMPLEMENTATION_PLAN.md`'s phase status lines for the authoritative historical
record, and `ROADMAP.md` for what's planned next. Currently: RKNN object detection runs but still
takes ONNX-style BGR input rather than the NPU's native NV12 (ROADMAP.md C1); a stored calibration
is applied to an AprilTag pipeline profile when it is activated, but not yet to a standalone
`ApriltagSink` restored after a restart; and WebRTC has been verified by
compiling/linking against the real libraries, not
against an actual browser ICE handshake in this environment.
