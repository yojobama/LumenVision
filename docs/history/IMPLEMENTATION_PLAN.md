# FRCV — Implementation Plan

> Written when this project was called FRCV. Left in its original FRCV-branded prose rather
> than rewritten - see `docs/RENAME.md` for the old->new identifier table and why.

Target hardware: **Orange Pi 5 / 5 Plus (RK3588, aarch64)** running `ubuntu-rockchip` v2.4.0
(Ubuntu 24.04 Server arm64), using the image's bundled `libmali` blob for Vulkan.
Development on **Windows + Visual Studio**,
compiling into **WSL2 (`Ubuntu`)** for the inner loop and **remotely onto the Orange Pi** for
target builds — build, deploy and debug all driven from Visual Studio. There is deliberately
**no Linux-native/CMake build**; MSBuild (`FRCVLib.vcxproj` + `Server.csproj`) stays the single
build system.

---

## 0. Current state (assessment)

What exists and works:

- `FRCVLib` — C++ core: `Manager` façade, `ISource`/`ISink` threaded pipeline,
  `CameraSource`, `VideoFileSource`, `ImageFileSource`, `ApriltagDetector`
  (CPU, apriltag lib, tag36h11 + pose), `CameraCalibrator` (OpenCV checkerboard),
  `RecordSink`, `Logger`, `SystemMonitor`.
- `Server` — .NET 10 EmbedIO REST API, SWIG-generated C# bindings (`Server/FRCVCore`),
  JSON file DB (`DB.cs`), source/sink controllers.
- `reactproject1` — React + Vite + Tailwind WebUI.

Blocking problems that must be fixed before any of the requested features land:

| # | Problem | Impact |
|---|---|---|
| B1 | **Only two build configurations actually work.** `Debug\|x64` (WSL2 `Ubuntu`) and `Debug\|ARM64` are configured; every `Release`, `LocalCheck`, `x86` and `ARM` configuration has no `ConfigurationType`, `PlatformToolset` or link settings. And `Debug\|ARM64` uses the **WSL2** toolset pointed at `Ubuntu-24.04` — that is not the Orange Pi, and on an x64 Windows host it is not even an aarch64 build. | There is no working path to the target device. Kills feature 1's real purpose. |
| B2 | `Debug\|ARM64` include path has a typo (`/usr/loca/include`), lacks `CppLanguageStandard=c++20`, and `AdditionalDependencies` is empty on both configs. | The ARM64 configuration cannot compile the existing C++20 code even once pointed at the Pi. |
| B3 | **Sink results are not readable.** `Manager::GetSinkResult` returns `nullptr` as a `std::string` (UB/crash); `GetAllSinkResults` returns `ISink::GetStatus()`, which is hardcoded to `""`. | No detections can reach NT4 (feature 4), the WebUI (6) or the calibration UI (7). |
| B4 | **Legacy preview-image path is a dead end.** `Manager::GetPreviewImage` throws `"Preview images are not exposed by the current ISink interface"`; `ISink::m_PreviewFrame` is never written; `Image8U` marshals a raw buffer across P/Invoke. | Superseded by the source-and-sink node model (see §Architecture). To be deleted, not repaired. |
| B5 | **`ISink::ProcessingThreadLoop` busy-waits** on `GetCurrentFrameCount()` with no sleep or condvar. | One core pegged per sink on an 8-core OPi5. Unusable in production. |
| B6 | **All WebRTC C# code is commented out**, duplicated across `Server/WebRTCStreamingService.cs` and `Server/Services/WebRTCStreamingService.cs`; `WebRTCSinkController` returns mock responses and is not registered in `Program.cs`. | Feature 5 is greenfield. |
| B7 | **Controllers not registered**: `CameraCalibrationSinkController`, `ObjectDetectionSinkController`, `RecordingSinkController`, `WebRTCSinkController` exist but are missing from `Program.cs`. | Calibration API unreachable (feature 7). |
| B8 | **Calibration model is incomplete**: `CameraCalibrationResult` carries only `fx, fy, cx, cy, rms` — **no distortion coefficients**, no resolution, no camera identity. Board geometry is hardcoded (`6x9`, 25 mm). No persistence. | Pose estimates are wrong on any lens with real distortion. Confirmed for fixing. |
| B9 | Object detection is a stub — `Manager::CreateObjectDetectionSink` logs and returns. The old `RknnSink`/`ONNXSink`/`Yolov11`/`VkApriltagSink` were deleted in commit `49a3a2a`. | Features 2 and 3 are re-implementations (old code recoverable from git as reference). |
| B10 | **Orphaned source files**: `Frame`, `FramePool`, `FrameSpec`, `PreProcessor`, `EndpointBase`, `WebRTCEndpoint`, `VideoFileRecorder` are not all in the vcxproj item groups, and `EndpointBase`/`WebRTCEndpoint` are entirely commented out — yet `Frame` is still referenced by `swig.i` and by `Manager::m_CalibrationImages`. | Dead weight that breaks SWIG generation and confuses the build. |
| B11 | Build outputs (`Server/bin`, `obj`, `libFRCVLib.so`, generated `swig_wrap.cxx`) are committed to git. | Merge noise, stale binaries shipped. |
| B12 | `Server` binds `http://localhost:8175` only; `ApiService.ts` hardcodes a `localhost` base URL. | Unreachable from the driver station over the network. |

**Ordering consequence:** phases 1–2 are prerequisites for *every* requested feature.

---

## Architecture: the node model

Per the design decision, the "source-and-sink" shape already used by `ApriltagDetector` and
`CameraCalibrator` becomes **the** model, and the legacy `GetStatus()` / preview-image path is
removed rather than fixed.

**Every pipeline element is a node.** A node implements `ISource` (produces a `SourceResult`),
`ISink` (consumes bound sources), or both:

| Node | ISource | ISink | Notes |
|---|:--:|:--:|---|
| `CameraSource`, `VideoFileSource`, `ImageFileSource` | ✔ | | frame producers |
| `ApriltagDetector` (CPU / Vulkan backend) | ✔ | ✔ | JSON detections + annotated frame |
| `ObjectDetectionSink` (RKNN / ONNX backend) | ✔ | ✔ | JSON detections + annotated frame |
| `CameraCalibrator` | ✔ | ✔ | corner overlay frame + calibration state JSON |
| `NetworkTablesSink` | | ✔ | terminal — publishes to NT4 |
| `WebRTCSink` | | ✔ | terminal — encodes and streams frames |
| `RecordSink` | | ✔ | terminal — writes video |

Consequences, all of which are work items in phase 2:

1. **`ISink::GetStatus()` and `ISink::m_PreviewFrame` are deleted.** So are
   `Manager::EnableSinkPreview` / `DisableSinkPreview` / `GetPreviewImage`, the `Image8U`
   struct, and `GetAllSinkStatus` / `GetSinkStatusById`.
2. **`SourceResult` is the single result currency.** It already carries an optional JSON and an
   optional `cv::Mat`; extend it with `frameNumber`, `captureTimeUs` and `producedTimeUs` so
   end-to-end latency is measurable (NT4 latency compensation depends on this).
3. **Frames never cross into C#.** Any visual output is a node: `WebRTCSink` for live view,
   `RecordSink` for capture. This is what makes the P/Invoke boundary cheap — the C# server only
   ever moves small JSON strings and control calls.
4. **The C# server is glue, not logic.** It owns: REST/WebSocket surface, persistence (`DB.cs`),
   pipeline topology, WebRTC *signalling* relay, and serving the WebUI. It owns no image data
   and no detection logic.
5. **Manager's read surface becomes two calls**:
   - `std::string GetNodeResult(int id)` → the node's latest JSON via
     `ISource::GetLatestResult(requireFrame=false, requireJson=true)`; `"{}"` when the node is
     not an `ISource` or has produced nothing. Never `nullptr`.
   - `std::string GetPipelineState()` → one JSON document describing every node:
     `{id, type, isSource, isSink, running, fps, latencyMs, boundSources:[...], error}`.
     Built by `Manager` from the node registry, replacing per-sink `GetStatus()`.

---

## Phase 1 — Build, deploy and debug from Visual Studio  *(feature 1)*

**Goal:** F5 in Visual Studio builds `FRCVLib` inside WSL2 for local work, and a second
configuration cross-builds onto the Orange Pi over SSH, deploys the server + WebUI, and attaches
a debugger — with a single dependency script that prepares either machine.

1. **Consolidate the vcxproj configurations (B1).** Keep exactly four, delete `x86`, `ARM` and
   `LocalCheck`:

   | Configuration | Toolset | Machine |
   |---|---|---|
   | `Debug\|x64` | `WSL2_1_0`, `WSLPath=Ubuntu` | dev inner loop |
   | `Release\|x64` | `WSL2_1_0`, `WSLPath=Ubuntu` | dev perf checks |
   | `Debug\|ARM64` | `Remote_GCC_1_0` | Orange Pi over SSH, gdbserver debugging |
   | `Release\|ARM64` | `Remote_GCC_1_0` | Orange Pi, deployment build |

   The ARM64 configurations get a Visual Studio **Connection Manager** entry for the Pi
   (`RemoteTarget`, `RemoteProjectDirectory`, `RemoteRootDir`); `RemoteRootDir` is currently
   hardcoded to `/home/john/projects` on `Debug|x64` — move it to a user-overridable property
   sheet (`Local.props`, gitignored) so it is not per-developer state in the repo.
2. **Fix the compile/link settings (B2).** Per configuration: correct `/usr/loca/include` →
   `/usr/local/include`, set `CppLanguageStandard=c++20` on all four, and populate
   `LibraryDependencies` completely (`apriltag`, `opencv_*`, `avcodec`/`avformat`/`avutil`/
   `swscale`, plus the phase-specific ones below).
3. **Feature flags as preprocessor defines**, since there is no CMake to hold options:
   `FRCV_WITH_ONNX` (all configs), `FRCV_WITH_NT4` (all), `FRCV_WITH_WEBRTC` (all),
   `FRCV_WITH_VULKAN_APRILTAG` (all, with a runtime capability probe),
   `FRCV_WITH_RKNN` (**ARM64 only**). Every optional backend compiles out cleanly when its flag
   is absent, so the WSL x64 configuration never needs the RKNN SDK.
4. **Clean up the item groups (B10).** Add every file that is actually compiled; delete the dead
   ones — `EndpointBase`, `WebRTCEndpoint`, `UDPServer`, `PreProcessor`, `VideoFileRecorder`, and
   `Frame`/`FramePool`/`FrameSpec` (the pipeline moved to `cv::Mat` in commit `dc20863`). Deleting
   `Frame` also requires removing its `%template` entries from `swig.i` and the unused
   `Manager::m_CalibrationImages` member.
5. **`scripts/install-deps.sh`** — the feature-1 deliverable. Idempotent, `set -euo pipefail`.
   Runs *on the machine that compiles*: inside WSL `Ubuntu`, or on the Orange Pi over SSH.
   Both targets are **Ubuntu 24.04**, which keeps the package list nearly identical — the Pi runs
   [`ubuntu-rockchip` v2.4.0](https://github.com/Joshua-Riek/ubuntu-rockchip/releases/tag/v2.4.0)
   (`ubuntu-24.04-preinstalled-server-arm64-orangepi-5-plus`). The script detects arch and installs:
   - **Visual Studio remote toolchain requirements** (the part that is easy to forget):
     `openssh-server g++ gdb gdbserver make ninja-build zip tar rsync`, and enables `sshd`.
     The Pi image is a **server** image, so this is the only way in — verify SSH before anything else.
   - Base: `git pkg-config curl unzip swig`
   - CV: **build OpenCV 5.0 from source with the `opencv_contrib` modules** (ArUco, needed for
     phase 7's ChArUco support) into `/usr/local`, rather than the distro's 4.6 packages —
     confirmed decision. Cache the build (checked-out tag + build dir) so `--check` reruns don't
     rebuild it; this step dominates first-run install time on the Pi (expect 1–2+ hours on the
     RK3588's 8 cores) so the script should support `-j` tuning and resuming a partial build.
   - AprilTag: build `AprilRobotics/apriltag` from source into `/usr/local` (distro packages
     do not reliably ship `apriltag_pose.h`)
   - JSON: `nlohmann-json3-dev`
   - FFmpeg dev: `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev` — on aarch64, from
     the `rockchip-multimedia` PPA that `ubuntu-rockchip` already enables, so `h264_rkmpp` is
     present without building FFmpeg (verify with `ffmpeg -encoders | grep rkmpp`; see phase 6)
   - Vulkan (phase 5): `libvulkan-dev vulkan-tools glslang-tools spirv-tools`. On aarch64 the ICD
     is the image's bundled **`libmali`** blob, not mesa/panvk — see phase 5 for the verification
     step and for pinning `VK_ICD_FILENAMES` if a panvk ICD is also present.
   - WebRTC (phase 6): `libssl-dev libsrtp2-dev`, then build `libdatachannel` from source
   - NT4 (phase 3): fetch `ntcore` + `wpiutil` headers/`.so` from `frcmaven.wpi.edu`
     (`linuxarm64` / `linuxx86-64`)
   - ONNX Runtime: the **official prebuilt release** tarball for the arch (default CPU build)
   - **aarch64 only**: `librknnrt.so` + `rknn_api.h` from `rknn-toolkit2`, with a version check
     against the kernel's `rknpu` driver (`cat /sys/kernel/debug/rknpu/version`, or dmesg) —
     mismatched runtime/driver versions fail at `rknn_init` with an unhelpful error.
   - **No .NET install needed on the Pi**: the server is published `--self-contained`
     (phase 1, item 7), which sidesteps the fact that Ubuntu 24.04's feed has no .NET 10 package.
   - `--check` mode that verifies and reports what is missing, so a broken remote build has an
     obvious first diagnostic step.
6. **`scripts/install-deps.ps1`** (thin Windows helper): verifies WSL and the `Ubuntu` distro
   exist, checks that `swig.exe` is on the Windows PATH (the `Server.csproj` pre-build step needs
   it), and re-invokes the bash script inside WSL. Optionally runs it on the Pi over SSH.
7. **Deployment from Visual Studio.** `scripts/deploy.ps1`, wired as a post-build step on the
   `Release|ARM64` configuration:
   - `dotnet publish Server -r linux-arm64 --self-contained` on Windows,
   - `npm run build` in `reactproject1` → `Server/wwwroot`,
   - copy the published server + the remotely built `libFRCVLib.so` to the Pi,
   - restart the `frcv` systemd service.
8. **Debugging.** C++ on the Pi via the Remote_GCC configuration's gdbserver (VS handles this
   natively). C# via VS *Attach to Process* over SSH (`vsdbg`) against the running service —
   document both, plus the requirement that the deployed `.so` matches the debugged binary.
9. **`.gitignore`** for `bin/`, `obj/`, `node_modules/`, `*.so`, `swig_wrap.cxx`;
   `git rm -r --cached` the tracked build output (B11).

**Exit criteria:** on a freshly imaged Orange Pi and a fresh WSL `Ubuntu`, one script each, then
Visual Studio builds, deploys, runs and breakpoints both configurations.

---

## Phase 2 — Core library: adopt the node model  *(prerequisite for features 4–7)*

1. **Introduce `INode` and rename to the node scheme.** Today `ApriltagDetector` and
   `CameraCalibrator` inherit both `ISource` and `ISink`, and *each base separately* declares
   `m_ID`, `GetID()`, `Toggle()`, `GetToggleStatus()` and its own `pthread_t`. So a dual-role node
   currently carries **two ids, two toggle states and two threads**, and `GetID()`/`Toggle()` are
   ambiguous at the call site. Fix the model and the naming in one pass:
   - Add `class INode` holding `m_Id`, `GetType()`, and a single enabled state; `ISource` and
     `ISink` inherit it **virtually** and keep only their own worker thread. One node, one id, one
     `Enable()/Disable()`, with the source-side capture thread suppressed as today via
     `m_DoNotLoadCaptureThread`.
   - Rename the aggregate/user-facing surface: `Manager::m_Sources`/`m_Sinks` → one `m_Nodes`
     registry; `GetAllSinks`/`GetAllSources` → `GetAllNodes()`; `BindSourceToSink(src, sink)` →
     `BindNode(upstreamId, downstreamId)`; `UnbindSourceFromSink` → `UnbindNode`;
     `Start/StopSinkById` and `Start/StopSourceById` → `Start/StopNode`.
   - C#: `SinkManager` + `SourceManager` → `NodeManager`; `Sink.cs` + `Source.cs` → `Node.cs`;
     controllers collapse into `NodeController` plus per-type creation controllers.
   - `DB.cs`: `{sinks:[], sources:[]}` → `{nodes:[], bindings:[]}`, with a one-shot migration of
     any existing `data.json`.
   - TypeScript: `types/index.ts`, `useAppData` and `ApiService` follow the same rename.
   `ISource`/`ISink` keep their names as *capability* interfaces — that is what they now are.
2. **Delete the legacy status/preview surface (B4).** Remove `ISink::GetStatus`,
   `ISink::m_PreviewFrame`, `Manager::EnableSinkPreview` / `DisableSinkPreview` /
   `GetPreviewImage`, `Image8U`, `GetAllSinkStatus`, `GetSinkStatusById`, and their C# wrappers
   and REST routes.
3. **Implement the two-call read surface (B3).** `Manager::GetNodeResult(int)` and
   `Manager::GetPipelineState()` exactly as specified in §Architecture. Every node that produces
   results already calls `SetLatestResult`; nothing new is needed on the producer side beyond the
   `SourceResult` timestamp fields.
4. **Extend `SourceResult`** with `frameNumber`, `captureTimeUs`, `producedTimeUs`, and have each
   node stamp them. `CameraSource` stamps capture time; every downstream node propagates the
   original capture time so latency is measured end to end, not per stage.
5. **Kill the busy-wait (B5).** Replace the spin loop in `ISink::ProcessingThreadLoop` with a
   condition variable signalled by `ISource::SetLatestResult`, with a timeout so `Toggle(false)`
   stays responsive. Also fix `ISink::Toggle`: `m_Thread` is uninitialised before the first start
   and `m_ShouldTerminate` is read unsynchronised → make it `std::atomic<bool>`.
6. **Node lifecycle.** Add `Manager::DeleteNode(int)` (the C# layer already exposes delete
   routes with nothing behind them), and fix `UnbindSourceFromSink` to line up with
   `ISink::UnbindSource(sourceID)` — the current signatures do not match.
7. **Node type introspection.** `GetType()` returning a stable string
   (`"camera" | "videoFile" | "imageFile" | "apriltag" | "objectDetection" | "calibration" |`
   `"networkTables" | "webrtc" | "record"`) so the server and WebUI stop inferring type from the DB.
8. **Per-node counters** (FPS, last latency, error string) feeding `GetPipelineState()`.
9. **Binding validation.** `ISink::BindSource` currently accepts anything up to `m_MaxSources`;
   add capability checks (a `NetworkTablesSink` needs a JSON-producing source, a `WebRTCSink`
   needs a frame-producing one) and return a reason string on failure so the UI can explain it.
10. Revive `UnitTests` (currently orphaned from the build): result egress, binding rules,
   `ApriltagDetection`, plus the phase-4/5 tests below.

---

## Phase 3 — NT4 publishing (feature 4)

**Where NT4 lives:** WPILib's official implementation is `ntcore` (C++/Java); there is no
official C# NT4 binding. So `NetworkTablesSink` is a **C++ terminal sink** inside `FRCVLib`,
bound to detector nodes like any other sink — consistent with the node model, and it keeps
detections from making a round trip through C#.

1. `install-deps.sh` fetches `ntcore` + `wpiutil` (headers + `.so`) for the host arch;
   the vcxproj links them under `FRCV_WITH_NT4`.
2. **`NetworkTablesSink : public ISink`** (`FRCVLib/NetworkTablesSink.{h,cpp}`):
   - Config: team number **or** explicit server address/port, client identity, root table
     (default `/FRCV`).
   - Starts an NT4 client (`nt::NetworkTableInstance::StartClient4`), reconnects automatically,
     reports connection state through `GetPipelineState()`.
   - `maxSources` > 1: one NT sink can publish several detector nodes, each into its own subtable
     keyed by node id/name.
   - Payloads by source node type:
     - AprilTag: `tags/ids` (int[]), per-tag pose as a `Pose3d` struct topic (ntcore struct
       serialisation), plus `decisionMargin`, `poseError`, `latencyMs`, `timestampUs`.
     - Object detection: `objects/classIds`, `confidences`, `boxes` (double[4N]), `latencyMs`.
     - Always: heartbeat + `deviceStatus` (CPU / temperature / memory from `SystemMonitor`).
   - Timestamps published in NT server time (`nt::Now()` + `GetServerTimeOffset`) using
     `SourceResult::captureTimeUs`, so the robot can latency-compensate. This is the part teams
     actually need.
3. `Manager::CreateNetworkTablesSink(config)` + SWIG exposure.
4. C# `NetworkTablesController` (`/api/nt/*`): configure team number/server, start/stop, read
   connection status; persisted in `DB.cs`.
5. Test against a local `ntcore` server and Glass/Shuffleboard.

---

## Phase 4 — Object detection: YOLOv8 **and** YOLOv11, RKNN + ONNX (feature 2)

The deleted code (`RknnSink.h`, `ONNXSink.h`, `Yolov11.cpp`, `ONNX_YOLO11.hpp` at `49a3a2a^`)
is useful reference but assumed the old `FilterBase` pipeline — reimplement against the node model.

1. **Model variant is user-selected, not inferred.** Per the requirement, uploading weights in the
   WebUI includes a **model-family dropdown** (`YOLOv8` / `YOLOv11`), stored in the model manifest
   and passed down as a `YoloVariant` enum. Both are anchor-free with the same
   `[1, 4+numClasses, 8400]` head, so they share one decoder; the enum drives export/layout
   validation, label handling, and future-proofs the decoder for genuine head differences rather
   than guessing from the filename.
2. **Backend abstraction** — `FRCVLib/detection/IDetectionBackend.h`:
   ```cpp
   enum class YoloVariant { YOLOv8, YOLOv11 };
   struct DetectionBackendConfig {
       std::string modelPath, labelsPath;
       YoloVariant variant;
       float confThreshold, nmsThreshold;
       int inputW, inputH;
   };
   class IDetectionBackend {
   public:
       virtual bool Load(const DetectionBackendConfig&) = 0;
       virtual std::vector<ObjectDetection> Infer(const cv::Mat& bgr) = 0;
       virtual std::string Name() const = 0;
   };
   ```
3. **Shared pre/post-processing** — one implementation, backend-independent, unit-tested:
   - letterbox resize with scale/pad bookkeeping;
   - **anchor-free head decode**: `[1, 4+numClasses, 8400]`, *no objectness channel* — this
     differs from YOLOv5 and is the usual source of garbage boxes; DFL decode when the export
     emits raw distributions; xywh→xyxy; un-letterbox; class-wise NMS;
   - for RKNN INT8, per-output zero-point/scale dequantisation before decode.
4. **`RknnBackend`** (`FRCV_WITH_RKNN`, ARM64 configurations only): `rknn_init` from a `.rknn`
   file, `rknn_query` for I/O attrs, zero-copy I/O (`rknn_create_mem` / `rknn_set_io_mem`) to
   avoid a per-frame memcpy, and **NPU core round-robin** (`rknn_set_core_mask` across RK3588's
   three NPU cores) so multiple cameras scale. Document the conversion path
   (`ultralytics export format=onnx opset=12` → `rknn-toolkit2` on x86 → `.rknn`) and add
   `scripts/convert-yolo-rknn.py`, which takes the same v8/v11 selector.
5. **`OnnxRuntimeBackend`** (`FRCV_WITH_ONNX`): the **stock ONNX Runtime release** — CPU
   execution provider only. MIGraphX and OpenVINO are explicitly out of scope, which removes the
   custom-ORT-build problem entirely. This backend is the development/x86 path and the portable
   fallback; RKNN is the production path on the Pi.
6. **`ObjectDetectionSink : public ISink, public ISource`** — mirrors `ApriltagDetector`: emits
   JSON detections plus an annotated frame; owns an `IDetectionBackend`; backend chosen at
   creation (`RKNN` | `ONNX`).
7. **Model management**: a `models/` directory with a per-model manifest (`model.json`: path,
   backend, **variant**, input size, labels, thresholds), a C# `ModelController` (`/api/models/*`)
   for upload/list/delete, and the WebUI upload form carrying the family dropdown.
8. Replace `Manager::CreateObjectDetectionSink(ObjectDetectionProvider)` with
   `CreateObjectDetectionSink(const DetectionSinkConfig&)`; keep the provider enum as
   `{ RKNN, ONNX }` and expose both it and `YoloVariant` through SWIG.
9. Benchmarks: log per-backend inference milliseconds; target ≥30 FPS at 640×640 on the RK3588 NPU.

---

## Phase 5 — `vkapriltag` second detector (feature 3) — ✅ DONE, verified on real hardware (2026-08-22)

`IApriltagBackend`/`CpuApriltagBackend`/`VkApriltagBackend` implemented; the SONAME collision
between vkapriltag's patched apriltag fetch and any other apriltag build was found, understood,
and resolved (see git history on `develop`). Validated with vkapriltag's own
`apriltag_vulkan_validate` tool against its sample image on the real Orange Pi Mali G610: same
tag ID as the CPU reference, 0.58px corner RMS agreement, whole GPU pipeline (16.8ms) already
faster than the CPU reference (21ms) at 1280x800.

Repo: <https://github.com/yojobama/vkapriltag> (Vulkan-compute AprilTag detection).
A `VkApriltagSink` previously existed in-tree (deleted at `49a3a2a`) — its Vulkan boilerplate
is reusable.

0. **Prove headless Vulkan on the Pi before writing any code.** The GPU is Mali G610 driven by the
   image's bundled **`libmali`** blob (not panfrost/panvk). Over SSH on the server image, with no
   display server running:
   - confirm the ICD is registered (`ls /usr/share/vulkan/icd.d/`) and that
     `vulkaninfo --summary` reports the Mali device with a queue family exposing
     `VK_QUEUE_COMPUTE_BIT`;
   - if a panvk/lavapipe ICD is also installed, pin the blob with `VK_ICD_FILENAMES` (and set it in
     the `frcv.service` unit from phase 9) so the wrong driver is never selected silently;
   - if the installed `libmali` variant is a `-x11`/`-wayland-gbm` build that refuses to
     initialise headless, try the GBM-only variant before concluding anything.

   This is the single largest risk in the plan: everything else here is contingent on a working
   compute queue. If it cannot be made to work headless, `vkapriltag` becomes a
   development-machine-only backend and the CPU detector stays the production path on the Pi —
   which the backend interface in item 3 makes a configuration change, not a rewrite.
1. **Read the upstream repo**: confirm its build layout, public API surface, SPIR-V shader
   build/install story, and how complete detection + pose actually is. Since it is your repo, add
   whatever it lacks upstream rather than vendoring a fork.
2. Add as a **git submodule** under `third_party/vkapriltag`. With no CMake in play, its sources
   are either (a) added directly to `FRCVLib.vcxproj` item groups, or (b) built once on each
   target by `install-deps.sh` and linked as a `.so` — **(b) is preferred**, so the submodule
   keeps its own build and the vcxproj only links it. Shaders compile to SPIR-V during that step
   and install beside the library; the runtime resolves them via a configurable path.
3. **Refactor `ApriltagDetector` to a backend interface** (same shape as phase 4):
   ```cpp
   enum class ApriltagBackend { CPU, VULKAN };
   class IApriltagBackend {
   public:
       virtual std::vector<ApriltagDetection> Detect(const cv::Mat& gray) = 0;
   };
   ```
   `CpuApriltagBackend` wraps today's `apriltag_detector_*` code; `VkApriltagBackend` wraps the
   submodule. Pose estimation, JSON emission and frame annotation stay **shared**, so both
   backends emit identical schemas and the node type stays `"apriltag"` with a `backend` field.
4. `Manager::CreateApriltagDetector(..., ApriltagBackend)` overloads + SWIG, plus a `backend`
   selector in the create API and the WebUI.
5. Runtime capability probe: with no Vulkan device or no compute queue, log and fall back to CPU
   instead of crashing — WSL2 and headless x86 frequently lack one.
6. **Equivalence test**: run both backends over the same image corpus in `UnitTests`, assert
   matching tag IDs and corner positions within tolerance, and report per-backend FPS. This is the
   acceptance gate — a faster detector that disagrees with the CPU one is useless.

---

## Phase 6 — WebRTC streaming (feature 5) — ✅ core sink done (2026-08-22), MJPEG fallback pending

`WebRTCSink` implemented against libdatachannel + ffmpeg (libx264, `h264_rkmpp` on the Pi is a
follow-up once confirmed available), non-trickle ICE, signalling wired through Manager as plain
strings (no `rtc::` types reach swig.i) and a real REST `WebRTCSinkController`. Verified by
compiling/linking against the real libdatachannel + ffmpeg and a clean `dotnet build` generating
correct SWIG bindings; **not yet verified against a real browser handshake** (no browser
available in this environment) - that check is still owed before relying on this in practice.
MJPEG fallback sink (item 5 below) not yet implemented.

**`WebRTCSink` is a terminal sink in C++** (`libdatachannel`), bound to any frame-producing node.
This follows directly from the node model: frames never leave C++, so the previous C# design
(marshalling `Image8U` across P/Invoke at 30 FPS and synthesising a fake H.264 stream) is dropped
entirely. The C# server keeps only **signalling**.

1. **`WebRTCSink : public ISink`** (`FRCV_WITH_WEBRTC`). "Different stages" comes for free: bind
   it to a `CameraSource` for the raw feed, or to an `ApriltagDetector` / `ObjectDetectionSink`
   for the annotated feed — one sink class, any stage, several instances at once.
2. **Encoding**: FFmpeg `libavcodec` with hardware encoders where available — `h264_rkmpp`
   (RK3588 VPU) on the Orange Pi, `libx264 -preset ultrafast -tune zerolatency` as the portable
   fallback on WSL/x86. `ubuntu-rockchip` already enables the `rockchip-multimedia` PPA, so the
   rkmpp-enabled FFmpeg comes from apt rather than a source build — confirm with
   `ffmpeg -encoders | grep rkmpp` and check the user is in the `video`/`render` groups for
   `/dev/mpp_service` access. Encoded NALUs go into `rtc::H264PacketizationHandler` + `rtc::Track`.
3. **Signalling**: a WebSocket endpoint on the C# server (`/ws/webrtc`) brokering offer/answer/ICE
   between browser and the C++ sink, exposed through
   `Manager::WebRTCCreateOffer/SetAnswer/AddIceCandidate/Close` (SWIG-friendly strings in and out).
   Replace the mock `WebRTCSinkController`; delete both commented `WebRTCStreamingService.cs`
   files (B6).
4. Local network only → no TURN; STUN configurable (default none — empty ICE servers works on a
   field LAN). Per-stream bitrate/resolution/FPS, and a hard cap on concurrent viewers.
5. **MJPEG fallback** as a second terminal sink (`MjpegSink`, served through the C# server at
   `/api/stream/mjpeg?nodeId=N`). Works in every browser and is invaluable when WebRTC negotiation
   breaks at competition.
6. Frontend: rework `WebRTCStream.tsx` against the real signalling protocol; keep
   `webrtc-test.html` as a standalone debug page under `Server/wwwroot/`.

---

## Phase 7 — Calibration, end to end (feature 7) — ✅ items 1-5 done (2026-08-22), item 6 (WebUI) pending

Distortion coefficients (item 1, confirmed by the user), configurable board geometry including
ChArUco (item 2 - OpenCV 5.0 moved ArUco/ChArUco into the core `objdetect` module, no contrib
build needed), the calibrator control surface (item 3: snapshot count/remove/clear, explicit
`RunCalibration()`), and basic persistence keyed by camera path + resolution (item 4, though
**not yet auto-applied** when a matching camera source is recreated - `CalibrationManager` only
covers save/list/lookup so far) are implemented and verified via full compile+link+SWIG-generation
checks. `CameraCalibrationSinkController` (item 5) now exposes the whole surface. The calibration
wizard WebUI screen (item 6) has not been started - that's phase 8/WebUI work.

1. **Fix the data model (B8) — confirmed.** Extend `CameraCalibrationResult` with
   `distCoeffs[5..8]` (k1, k2, p1, p2, k3…), `imageWidth`/`imageHeight`, `boardSpec`,
   `sampleCount`, `perViewErrors`, `calibratedAtUnixMs`, and a `cameraId` tying it to the camera
   hardware path/serial. `CameraCalibrator::GetCalibrationResult` must return the distortion
   vector `cv::calibrateCamera` already produces but currently discards, and **`ApriltagDetector`
   must undistort detection corners before `estimate_tag_pose`** — without this the pose output
   is simply wrong on any real lens. Propagate the new fields through SWIG and `DB.cs`.
2. **Make board geometry configurable**: pattern type (checkerboard / ChArUco), rows, cols, square
   size in metres — replacing the hardcoded `CHECKERBOARD_WIDTH` / `0.025f`. ChArUco is worth
   supporting: it tolerates partial views and is what PhotonVision users expect.
3. **Calibrator control surface**: `SaveBoardDetection()` already exists; add `GetSnapshotCount()`,
   `RemoveSnapshot(i)`, `ClearSnapshots()`, and an explicit `RunCalibration()` so the UI decides
   when `cv::calibrateCamera` runs — plus coverage feedback (which regions of the frame have
   samples) and per-view reprojection error so bad snapshots can be dropped. All of this surfaces
   through the calibrator node's `SourceResult` JSON, and its annotated frame (corner overlay)
   streams through a `WebRTCSink` like any other node — no special preview path.
4. **Persistence**: store results in `DB.cs` keyed by camera + resolution; auto-apply the stored
   calibration when a camera source is created; import/export JSON, and read PhotonVision-format
   calibration files for migration.
5. **Server**: register `CameraCalibrationSinkController` in `Program.cs` (B7) and complete it —
   create calibrator, bind camera, capture snapshot, list/delete snapshots, run calibration,
   view/save/export result, apply to an AprilTag node.
6. **WebUI**: a calibration wizard — pick camera → pick board spec → live view (WebRTC) with corner
   overlay and coverage heat-map → capture N snapshots → run → show RMS with a
   good/acceptable/bad verdict → save and apply. This is the highest-value screen in the product;
   build it properly.

---

## Phase 8 — Server & WebUI refactor (feature 6) — server item 5 done (2026-08-22); rest pending

`DB.Load()` now restores sink->source bindings (was silently dropped before, despite being
persisted) and auto-starts every AprilTag/object-detection/NetworkTables sink on load - not
CameraCalibrationSink (interactive, operator-driven) or WebRTCSink (its processing thread runs
the encoder on every frame regardless of whether a peer is connected, so starting it before any
client has asked for a stream is pure waste). Fixed a related real bug found while wiring this
up: `ISource`/`ISink::Toggle(true)` had no guard against being called while already running - it
would spawn a second capture/processing thread without stopping the first. Now idempotent.

The node-oriented API reshape (items 1-4, 6-7) is **not started**. The WebUI gap called out below
(no way to create Object Detection/NetworkTables/WebRTC sinks) is now **closed** (2026-08-22):
the "Add Sink" dialog's type dropdown grew three new options with their real fields (a model
picker + inline upload for Object Detection, team-number/server-address for NetworkTables,
bitrate/fps/encoder for WebRTC), wired straight to the real `ObjectDetectionSinkController`/
`NetworkTablesSinkController`/`WebRTCSinkController`/`ModelController` endpoints - no mocked
responses. Also removed "Recording" and "Stereo Vision" from the dropdown: both were already
unreachable dead options (`RecordingSinkController.Create` is a bare `throw new
NotImplementedException()` and isn't even registered in `Program.cs`; no `StereoSink`
implementation exists anywhere beyond a forward declaration), so keeping them selectable was
worse than removing them. AprilTag Detection and Camera Calibration (the latter previously also
silently broken - `handleAddSink` rejected every type except a literal `'ApriltagSink'` string)
now both work too.

Verified for real, end to end, through the actual browser against the Orange Pi - and this is
where three separate server-crashing bugs surfaced, all of the same underlying class: **swig.i had
no `%exception` block anywhere**, so any C++ exception thrown by application code (not just a
SWIG-recognized case) crosses the P/Invoke boundary completely uncaught and calls
`std::terminate()`, killing the *entire server process* over what is often a perfectly ordinary,
expected failure:
1. `WebRTCSink::AddIceCandidate`/`SetAnswer` crashed the server the first time a real ICE
   candidate raced ahead of the answer being set (a real, common ordering race: browsers start
   firing `onicecandidate` as soon as `setLocalDescription()` is called, which is before the
   answer has been POSTed). Fixed by catching inside `WebRTCSink.cpp` itself, and by having
   `WebRTCStream.tsx` buffer candidates client-side until the answer POST actually completes.
2. `/webrtcSink/offer`'s `Task<string>` return crashed the *browser's* JSON parsing (not the
   server) with "Bad control character in string literal" - EmbedIO's default string
   auto-serialization doesn't escape the literal `\r\n` that SDP is full of. Fixed by writing the
   offer as a raw `text/plain` body (`HttpContext.SendStringAsync`) instead of relying on
   auto-JSON-wrapping a plain string return value.
3. Uploading a model that isn't valid ONNX (confirmed with a dummy file) crashed the server via
   `Manager::CreateObjectDetectionSink`'s `throw std::runtime_error("failed to load detection
   model...")` - textbook expected input validation failure, not a bug, yet fatal to the whole
   process.

Rather than patch each throw site, added a global `%exception` block to `swig.i` (using
`swig.i`'s already-generated-but-never-invoked `SWIGExceptionHelper`/
`SWIGRegisterExceptionCallbacks` C# marshaling machinery - it existed in every generated wrapper
all along, just never triggered without a `%exception` block). Every C++ exception across the
whole library is now converted into a real, catchable `System.ApplicationException` instead of
terminating the process. Verified: re-ran the exact bad-model-upload request post-fix and got a
clean `500` with the exception message, server still running and responsive immediately after.

**Follow-up redesign after real user feedback** (2026-08-22, same day): WebRTC and NetworkTables
as standalone addable "Sink Types" turned out to be the wrong shape entirely - both are really
attributes of *another* node's output, not first-class processing pipelines a user creates on
purpose. Redesigned per feedback:
- AprilTag Detection gained the Tag Size and CPU/Vulkan Backend fields it was missing, now wired
  to `/apriltagSink/createWithBackend` instead of the parameterless `/apriltagSink/create`.
- WebRTC and NetworkTables were removed from the "Add Sink" dropdown again. In their place, every
  node that can produce a frame or JSON result - a raw Source card, or an AprilTag/Object
  Detection/Camera Calibration Sink card - now has a "Live Preview" toggle, and detector Sinks
  additionally get a "Publish to NT4" toggle. Toggling either auto-creates (once) a dedicated
  WebRTCSink/NetworkTablesSink bound to that node, reusing NT4 connection details (team number or
  server address, root table) configured once in Settings rather than re-entered per node. These
  auto-created sinks are filtered out of the Sinks page/Dashboard sink lists entirely - they're
  plumbing, not something the user directly manages.
- This surfaced the actual root cause of a real bug report ("WebRTC preview didn't work against
  the AprilTag detector"): `SourceManager`'s C# source list only ever tracked "real" sources
  (camera/video/image) - a dual-role node like `ApriltagDetector` (registered as both a sink AND
  a source natively, see `Manager.cpp`'s `m_Sources.emplace` alongside `m_Sinks.emplace`) could
  never be selected as a bind target, and `SinkManager.BindSourceToSink` would
  NullReferenceException trying to construct `sink.Source` from a lookup that always returned
  null for such an id. Fixed by adding `SourceType.SinkOutput` and having `BindSourceToSink`
  construct a synthetic `Source` for a dual-role sink id instead of only ever checking
  `SourceManager`'s own list; the WebUI's bind picker (and the new preview/publish toggles) now
  offer these nodes too.
- Verified for real against actual Orange Pi hardware with a live USB webcam connected
  (`webcam_HD_2MP_WEBCAM` at `/dev/video0`): created a real camera source, watched its live feed
  over WebRTC in the browser; created an AprilTag sink with the Vulkan backend explicitly
  selected (confirmed via `/apriltagSink/backend` returning `"Vulkan (vkapriltag)"`), bound it to
  the camera, and got a working Live Preview of *the detector's own annotated output* - the exact
  scenario that was broken before. Toggled Publish to NT4 on the same sink and confirmed via
  `/networkTablesSink/status` that a real NT4 client was created with the configured team number.
  Also caught and fixed two smaller bugs this surfaced: the Tag Size field's default (0.1651)
  wasn't a multiple of its `step="0.001"`, which silently blocked submission (browsers reject
  non-conforming values with a UI hint but no visible field error) - changed to `step="any"`; and
  the bind picker offered a sink as a valid bind target for *itself*.

**Server — the glue layer**

1. Register every controller in `Program.cs` (B7); add `NetworkTablesController`,
   `ModelController`, `CalibrationController`, and the real `WebRTCSinkController`.
2. ~~Bind to `http://*:8175` (B12) and serve the built WebUI from `wwwroot`~~ — **done, verified
   on the real Orange Pi** (2026-08-22). `Program.cs` now binds `http://*:8175` (was `localhost`)
   and adds `.WithStaticFolder("/", wwwroot, false)` after `WithWebApi`, so `/api/*` still wins
   the route match. `Server.csproj` copies `reactproject1/dist/**` into `wwwroot/**` at build
   time (guarded on `Exists()`, same pattern as the `libFRCVLib.so` check, so a machine that
   hasn't run `npm run build` yet still builds and runs Server fine — just API-only). The
   frontend's three hardcoded `http://localhost:8175` references (`ApiService.ts`,
   `WebRTCStream.tsx` ×2, plus a cosmetic default in `App.tsx`'s settings panel) are now
   `window.location.origin`-relative, since the server always serves its own UI - a page loaded
   from the Pi's real IP was otherwise still trying to call back into `localhost` from the
   browser, which resolves to the browser's own machine, not the Pi. Verified by opening
   `http://192.168.55.138:8175/` in a real browser: `index.html` and JS/CSS assets loaded (`200`),
   and the dashboard's own polling (`source/getAll`, `sink/getAll`, `device/cpuUsage` etc, all
   through the relative base URL) hit the server log as real `200 OK`s with zero exceptions.
3. **Reshape the API around nodes**, matching the library: `/api/nodes` (list with type,
   bindings, state), `/api/nodes/{id}` (delete, start/stop), `/api/nodes/{id}/bind`,
   and typed creation endpoints per node type. **No compatibility shim** — the old
   source/sink routes are deleted and the WebUI moves over in the same change.
4. **Live state push**: a `/ws/state` WebSocket broadcasting `Manager::GetPipelineState()` plus
   per-node results, replacing the polling in `useAppData.ts`.
5. Make `DB.cs` the single source of truth for pipeline topology and **restore the pipeline on
   startup** (recreate nodes and bindings from JSON). Today the DB is loaded but the `Manager`
   starts empty, so nothing survives a reboot — and the robot power-cycles.
6. Structured errors (`{code, message}`), consistent HTTP statuses, request logging into the
   existing `Logger`, and a `/api/logs` tail endpoint backed by it.
7. Config file (`frcv.config.json`): team number, NT server, port, default pipelines, log level —
   editable from the UI.

**WebUI**

1. Split the 926-line `App.tsx` into routed pages: **Dashboard**, **Cameras/Sources**,
   **Pipelines**, **Calibration**, **Models**, **NetworkTables**, **Settings**, **Logs**.
   Add a router; keep Tailwind.
2. Regenerate `ApiService.ts` against the node API — consider generating types from an OpenAPI
   document to stop the client/server drift the current code comments already complain about.
3. Replace polling with the `/ws/state` socket; show live FPS / latency / temperature per node.
4. **Pipeline editor** as the centrepiece: add a node, choose type **and backend**
   (AprilTag CPU/Vulkan, detection RKNN/ONNX), bind it to upstream nodes, tune thresholds live,
   and watch the WebRTC view of any stage side by side with the raw camera.
5. **Model upload** with the YOLOv8/YOLOv11 dropdown, labels file, input size and thresholds.
6. NT4 panel: team number, connection state, live view of published values.
7. Production build step (`npm run build` → `Server/wwwroot`) wired into `scripts/deploy.ps1`.

---

## Phase 9 — Runtime packaging on the Orange Pi — ✅ done (2026-08-22)

`scripts/frcv.service` and `scripts/deploy.ps1` written (publish self-contained linux-arm64,
build the WebUI, copy the VS-built `libFRCVLib.so`, install/restart the systemd unit).
`avahi-daemon` added to `install-deps.sh`. Top-level `README.md` written. **Not yet run for
real** - `deploy.ps1` has not been executed against the actual Pi in this session, only
reviewed; the systemd unit has not been installed/started there either.

1. `systemd` unit `frcv.service`: restart-always, journald logging, starts on boot, launches the
   self-contained published server with `LD_LIBRARY_PATH` covering `libFRCVLib.so` and its
   dependencies, `VK_ICD_FILENAMES` pinned to the `libmali` ICD (phase 5, item 0), and
   supplementary groups for `video`/`render` so the VPU and NPU device nodes are reachable.
2. mDNS hostname (`frcv.local`) via avahi, so teams do not chase IP addresses.
3. `scripts/deploy.ps1` (phase 1) handles push + restart; add a `--rollback` that keeps the
   previous published directory.
4. `README.md` rewrite: Visual Studio setup (WSL + Connection Manager), the dependency script,
   per-configuration feature flags, NT4 setup, calibration guide.

---

## Suggested execution order

```
Phase 1 (VS build/deploy) ──┬─> Phase 2 (node model) ──┬─> Phase 3 (NT4 sink)
                            │                          ├─> Phase 4 (YOLOv8/v11 RKNN+ONNX)
                            │                          ├─> Phase 5 (vkapriltag backend)
                            │                          ├─> Phase 6 (WebRTC sink)
                            │                          └─> Phase 7 (calibration)
                            └──────────────────────────────> Phase 8 (server + WebUI) ─> Phase 9
```

Phases 3–7 are independent of one another and can be interleaved. Phase 8 should track them
incrementally rather than waiting for all of them to land.

---

## Settled decisions

1. **Build system** — MSBuild only. Visual Studio compiles into WSL2 (`Ubuntu`) for the inner loop
   and remotely onto the Orange Pi for target builds, then deploys and debugs from the IDE.
   No CMake, no Linux-native build.
2. **Architecture** — the source-and-sink node model is normative; the legacy `GetStatus()` /
   preview-image path is deleted. The C# server is glue between the C++ library and the WebUI.
   NT4 and WebRTC are both nodes (terminal sinks).
3. **Naming** — rename to "node" throughout C++, C# and TypeScript (phase 2, item 1).
4. **Calibration** — distortion coefficients are added and consumed by AprilTag pose estimation.
5. **ONNX** — stock prebuilt ONNX Runtime, CPU execution provider. MIGraphX and OpenVINO are out
   of scope.
6. **Model families** — YOLOv8 and YOLOv11 both supported, selected by a dropdown at model upload.
7. **Target OS** — `ubuntu-rockchip` v2.4.0, Ubuntu 24.04 Server arm64, Orange Pi 5 Plus image.
8. **GPU** — the image's bundled `libmali` blob provides Vulkan; use it rather than panfrost/panvk.
9. **API** — phase 8 may reshape the REST surface around nodes; no backwards-compatibility shim.

---

## Remaining risks

1. ~~**Headless Vulkan on `libmali`**~~ — **resolved, verified on the real board** (2026-08-21).
   `vulkaninfo` against the GBM+Vulkan ICD (`libmali-valhall-g610-g6p0-wayland-gbm-vulkan.so`,
   auto-registered by `install-deps.sh`) reports device `Mali-LODX`, and its queue family exposes
   `QUEUE_GRAPHICS_BIT | QUEUE_COMPUTE_BIT | QUEUE_TRANSFER_BIT` — with no display server running
   (`DISPLAY` unset, surface enumeration skipped as expected on a headless box). `vkapriltag` is
   clear to proceed on phase 5's original plan.
2. **RKNN driver/runtime version skew** *(phase 4)* — confirmed the two version numbers involved
   on the real board: kernel driver reports `RKNPU driver: v0.9.7`
   (`/sys/kernel/debug/rknpu/version`), installed `librknnrt.so` reports
   `librknnrt version: 2.3.2`. These are different versioning schemes (kernel ABI vs. userspace
   SDK release), not a simple string-equality check, so having both numbers doesn't by itself
   prove compatibility — that can only be confirmed by actually calling `rknn_init()` against a
   real `.rknn` model, which is phase 4 work. If it fails there with an opaque error, this pairing
   is the first thing to revisit.
3. **Board variant** — the chosen image is the **Orange Pi 5 Plus** build, while the project brief
   says Orange Pi 5. The two boards differ in device tree, PCIe/network layout and USB topology,
   and the images are not interchangeable. The bench hardware confirmed reachable at
   192.168.55.138 identifies as `Linux ubuntu 6.1.0-1025-rockchip ... aarch64` — consistent with
   either board; worth confirming which one physically is on the bench before the first deploy,
   though nothing else in this plan depends on the answer.
4. **Build verification is real, not simulated.** As of 2026-08-21, `FRCVLib` has been compiled
   and linked — with `FRCV_WITH_ONNX`, `FRCV_WITH_NT4`, and (on the Pi) `FRCV_WITH_RKNN` all
   enabled — against the actual installed OpenCV 5.0.0, apriltag, ntcore/wpiutil/wpinet, and ONNX
   Runtime on both the WSL2 dev box and the Orange Pi itself, with `ldd` confirming zero missing
   shared library dependencies on either machine. `dotnet build` of `Server.csproj` also succeeds
   from a clean `FRCVCore/`, confirming the SWIG regeneration path (including the new NT4 methods
   and the `distCoeffs` field) works end to end. This was direct verification via SSH against the
   real target, not a stand-in for Visual Studio's own WSL2/Remote_GCC build — that should still
   be exercised from Visual Studio itself before relying on it day to day.
5. ~~**Deploying to the Orange Pi from Visual Studio threw `DllNotFoundException` for
   `libFRCVLib`**~~ — **root-caused and fixed, verified on the real board** (2026-08-22).
   `Server.csproj` always defaulted `FRCVLibPlatform` to `x64` regardless of which architecture
   was actually being deployed, and the `.sln` maps *every* `Server` solution configuration
   (including `Debug|ARM64`) down to Server's own `Debug|Any CPU` — there was no signal at all
   telling it to bundle an ARM64 build. Since only a WSL2-built x86-64 `libFRCVLib.so` existed,
   that's what got bundled and pushed to the aarch64 Orange Pi, where an x86-64 ELF cannot be
   `dlopen()`'d — exactly the reported exception. Fixed by deriving `FRCVLibPlatform` from
   `$(RuntimeIdentifier)` (`linux-arm64` → `ARM64`, set by VS's SSH remote target) and by adding a
   build-time `Error` when the resolved `libFRCVLib.so` doesn't exist, so this class of mistake
   fails the build instead of surfacing as a runtime crash on the deployed device.
   `FRCVLib.vcxproj.user` also had `Debug|x64`'s WSL debugger flavour pointed at the Orange Pi's IP
   (a leftover from before the ARM64/`Remote_GCC` configuration existed) and no debug settings at
   all for `Debug|ARM64`/`Release|ARM64` — both corrected.

   Investigating this also surfaced that **the Orange Pi had never actually been provisioned by
   `install-deps.sh`**: it still had apt's unpatched `libapriltag3t64` (3.3.0) installed instead
   of the patched v3.4.5 vkapriltag needs, `libdatachannel` was entirely absent, and no .NET
   runtime was installed at all. Ran `install-deps.sh --skip-opencv --with-webrtc --with-nt4` for
   real on the board (OpenCV 5, ntcore/wpiutil and ONNX Runtime were already present from an
   earlier partial run) and installed the .NET 10 ASP.NET Core runtime via `dotnet-install.sh`.
   Built `libFRCVLib.so` for real aarch64 on the Pi (confirmed via `file`: `ELF 64-bit LSB shared
   object, ARM aarch64`), copied it back to `FRCVLib\bin\ARM64\Debug\` so `Server.csproj`'s fixed
   `FRCVLibPlatform` resolution has something real to find, then deployed the whole `Server`
   output to the board and ran it there directly (not simulated). Server started with no
   exceptions; created an `ImageFileSource` + an `ApriltagSink` requesting the **Vulkan** backend,
   bound and enabled it, and got back a correct real detection (tag id 585, real corner/center
   coordinates) with the backend confirmed as `"Vulkan (vkapriltag)"` — the first time the GPU
   AprilTag path has been exercised end-to-end through the actual C#/REST stack on real silicon,
   not just via the standalone `apriltag_vulkan_validate` tool.
