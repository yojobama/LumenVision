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

`/calibration` is its own tab: pick a camera (or a stereo pair, or one side-by-side camera) and a board, then
start a session. Its wizard (`/calibration/camera/:sessionId`, `/calibration/stereo/:sessionId`) shows the
live overlay, captures with a coverage heatmap showing where the board has and hasn't been seen
in-frame, and runs with a pass/fail light against the real gate (`epipolarRms` < 0.5px for stereo,
not `stereoRms`; a configurable RMS gate for mono). The tab also lists running sessions (to resume
or stop them), each camera's calibration status, and every saved calibration.

`/match` is a read-only, per-camera table (FPS, latency, detector binding, NT4 connection state)
plus CPU/RAM/disk/temperature — meant to be legible across a pit during a match, not for editing
anything.

## Tests

- **C++ (`tests/`, Catch2):** configure with `-DLUMEN_BUILD_TESTS=ON` (the `ci-*` presets do) and build the
  `LumenCoreTests` target. The calibration tests (`[calibration]`) drive `CameraCalibrator` and
  `StereoCalibrator` with synthetic checkerboard renders; the AprilTag tests (`[apriltag]`) include a
  Vulkan-versus-CPU agreement check in every refine mode, which needs a Vulkan device. The target links
  against the shared LumenCore library; on Windows `LUMEN_BUILD_TESTS` makes LumenCore export all its
  symbols so the test executable can link (production builds do not), and the native dependency
  folders must be on `PATH`.
- **C# (`Server.Tests/`, xUnit):** `dotnet test Server.Tests` after building LumenCore for your preset
  (pass `-p:LumenCorePreset=` as for `Server.csproj`). It covers calibration result persistence,
  calibration session error paths, the legacy-record handling in `DB.Load` and the AprilTag refine-mode
  persistence and defaults; on Windows the
  native dependency folders must be on `PATH`, as when running the Server.

## Known gaps

See `docs/history/IMPLEMENTATION_PLAN.md`'s phase status lines for the authoritative historical
record, and `ROADMAP.md` for what's planned next. Currently: RKNN object detection runs but still
takes ONNX-style BGR input rather than the NPU's native NV12 (ROADMAP.md C1); a stored calibration
is applied to an AprilTag pipeline profile when it is activated, but not yet to a standalone
`ApriltagSink` restored after a restart; and WebRTC has been verified by
compiling/linking against the real libraries, not
against an actual browser ICE handshake in this environment.
