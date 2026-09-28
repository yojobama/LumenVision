# FRCV → LumenVision rename record

This project was called **FRCV** until 2026-09-21, when it was renamed to **LumenVision**. This
page is the old→new identifier table, so a `git log -S FRCV` or an old issue/commit referencing
the previous name stays findable. The narrative planning docs in `docs/history/` are left in
their original FRCV-branded prose rather than search-and-replaced — rewriting them would make
them read as if the project was always called LumenVision, which is a worse record than leaving
them alone with a banner pointing here.

| Category | Old | New |
|---|---|---|
| Product name | FRCV | LumenVision |
| C++ library directory | `FRCV/FRCVLib/` | `LumenCore/` |
| C++ library / SWIG module | `libFRCVLib` | `LumenCore` (→ `LumenCore.dll` on Windows, `libLumenCore.so` on Linux — one `DllImport` string resolves both) |
| Solution file | `FRCV.sln` | `LumenVision.sln` |
| Generated C# bindings directory | `Server/FRCVCore/` | `Server/Interop/` |
| WebUI directory | `FRCV/reactproject1/` | `webui/` |
| npm package | `frcv-dashboard` | `lumenvision-ui` |
| Robot-side project directory | `RobotSideJava/` | `robot/` |
| Robot-side Java package | `frcv` (+ `frcv.managers`, `frcv.sinks`) | `org.lumenvision.lib` (+ `.managers`, `.sinks`) |
| Robot-side Java classes | `FRCVDevice`, `FRCVSink` | `LumenDevice`, `LumenSink` |
| Feature-flag macros | `FRCV_WITH_ONNX`, `FRCV_WITH_NT4`, `FRCV_WITH_WEBRTC`, `FRCV_WITH_VULKAN_APRILTAG`, `FRCV_WITH_CODEC_STEREO`, `FRCV_WITH_RKNN` | `LUMEN_WITH_*` (same six) |
| MSBuild feature-flag properties | `FrcvCommonDefines` / `FrcvPlatformDefines` | `LumenCommonDefines` / `LumenPlatformDefines` |
| Server.csproj native-library properties | `FRCVLibPlatform` / `FRCVLibConfiguration` / `FRCVLibBuildDir` | `LumenCorePlatform` / `LumenCoreConfiguration` / `LumenCoreBuildDir` |
| NT4 root table default | `FRCV` | `lumenvision` |
| NT4 client identity default | `FRCV` (shared with the root table — a real bug: two coprocessors on one robot would silently register the same NT4 client identity) | `lumenvision` (now an independent literal, not copied from the root table) |
| Browser `localStorage` settings key | `frcvSettings` | `lumenSettings` (no migration; old settings are not carried over) |
| systemd unit | `frcv.service` | `lumenvision.service` |
| systemd user/group | `frcv` | `lumen` |
| Install directory | `/opt/frcv` | `/opt/lumenvision` |
| mDNS | `frcv.local` (never actually implemented) | not a hostname change — the bench Pi keeps the PhotonVision image's `photonvision.local` identity; an avahi alias is planned instead |
| Dependency cache env var | `FRCV_BUILD_ROOT` | `LUMEN_BUILD_ROOT` (default cache `~/.lumen-build`; the old variable and `~/.frcv-build` are no longer read) |
| Log file | `FRCVLog.txt` | `LumenVision.log` |
| RTP track label (WebRTC) | `frcv-video` | `lumen-video` |
| ONNX Runtime logging tag | `FRCV` | `LumenVision` |
| Rider solution directory | `.idea.FRCV` | `.idea.LumenVision` |
| Java/Maven group (planned, Phase 7) | — | `org.lumenvision` |

Not renamed, and why: `calibrations.json`, `stereoCalibrations.json` and `data.json` were
checked and never carried the name in the first place — nothing to change there. The submodules
`third_party/vkapriltag` and `third_party/codec-stereo` keep their existing upstream repository
names and URLs (`github.com/yojobama/vkapriltag`, `github.com/yojobama/codec-stereo`) — only
their path within this repo moved (flattened out of `FRCV/third_party/`), via a full
deinit/re-add rather than a `git mv`, since a plain `git mv` on a submodule updates
`.gitmodules`' path but not the submodule's own registered name or its `.git/modules` storage
directory.
