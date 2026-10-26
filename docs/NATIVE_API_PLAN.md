# Native API and PhotonVision compatibility split: implementation plan

Goal: the coprocessor and the Java library expose LumenVision's own model (a graph of named nodes that can be enabled,
disabled, bound and asked for a frame), and PhotonVision's model (cameras with indexed pipelines, driver mode, input/output
snapshots, LED modes) exists only in a `photoncompat` Java package built on top of it.

Every phase leaves the tree green and shippable, is committed and pushed to the working branch on its own, and has a gate
that must pass before the next phase starts. Phases 1 and 2 are additive; removals start in phase 3, once the new
compatibility layer already runs on the new primitives.

## Decisions to settle before phase 1

| # | Question | Recommendation |
|---|----------|----------------|
| D1 | Branch | `feature/native-api`, cut from `feature/vkapriltag-1.7.0` (or from `main` once that merges). Never `main`. |
| D2 | How `photoncompat` resolves a pipeline index to nodes | Explicit in robot code (`new PhotonCamera("front").withPipelines("front-tags", "front-notes")`); without it, pipeline *i* is the *i*-th detector bound to the camera, by node id. |
| D3 | Per-pipeline camera overrides (`CameraOverrides`: exposure, controls, transform, FPS limit per profile) | Drop. Migration folds the active profile's overrides into the camera's own settings and logs any it discards. Check the Pi's `data.json` first; if a team relies on them, keep them as a camera-controls preset instead. |
| D4 | The best-target NT scalars (`hasTargets`, `targetYaw`, `targetPitch`, `targetArea`, `targetPose`) | Keep: dashboards and teams without the vendordep read them. Drop the legacy `tags/*` arrays and `multitag/*` scalars. |
| D5 | Compatibility between versions | A protocol break: the new vendordep refuses a coprocessor without the new `.protocol` topic, and the new coprocessor stops honouring the old config topics. Release both together with a note. |
| D6 | Java artefacts | One artefact, two packages: `org.lumenvision.lib` (native) and `org.lumenvision.photoncompat` (PhotonVision-shaped, plus `.sim`). One vendordep JSON. |

## Phase 0: baseline and safety net

Work:
- Record the baseline: `LumenCoreTests.exe "~[hitl]"` (102 cases), `dotnet test Server.Tests` (247), `npx tsc --noEmit` and `npm run build` in `webui`,
  `./gradlew test` in `lumenlib`, `./gradlew build` in `robot`.
- Pull the Pi's live `data.json` and save a copy as a test fixture (`Server.Tests/fixtures/data-with-profiles.json`), plus a synthetic one that
  covers every profile field (both detector kinds, driver mode on, camera overrides, a field layout, an active index other than 0).
- Write characterisation tests for what robot code can do today, at the Java level against an in-process NT server and a scripted fake
  coprocessor: `PhotonCamera` pipeline switch, driver mode on/off, input/output snapshot requests, `setFPSLimit`, `setLED`, results,
  multi-tag and constrained poses. These describe *behaviour* (what the robot observes), not topic names, so they survive the
  rewrite with only their fake coprocessor changing.

Gate 0: all baseline suites pass; the new characterisation tests pass against the current code; fixtures committed.

## Phase 1: generic node controls in the core and server (additive)

Work:
- **Persisted enabled state per node.** `Sink.Enabled` / `Source.Enabled` in `DB.cs` (default true, so existing configs load unchanged), restored
  on load; REST `PATCH node/enabled`.
- **NT per node:** `<root>/<node>/config/enabled` (bool) and `status/enabled`; `config/snapshot` (any write saves that node's latest frame); `config/fpsLimit`
  applied to the node itself (`ISource::SetFpsLimit`) rather than resolved to a camera. Topics are keyed by the node's *name* when it has
  one (see phase 5), its id otherwise; for now, under the same table names the sink already uses.
- **Snapshots by node:** `SnapshotService.Save(nodeId)` for any node with a frame; the input/output kinds become a call on the camera or
  detector node.
- **Discovery:** a read-only `<root>/.graph` JSON topic (nodes: id, name, kind, enabled, bound sources) and `<root>/.protocol` (integer, starts at 3),
  republished on change and every few seconds, so robot code can resolve names without REST.
- All existing topics and routes keep working.

Tests:
- Native: `tests/test_networktables_nt4_e2e.cpp` cases for `config/enabled`, `config/snapshot`, `config/fpsLimit` per node and the `.graph` payload.
- Server: enabled flag round-trips through `DB.Save`/`Load`; a disabled detector stays disabled after restart; NT enable/disable of
  a node toggles it within one tick; snapshot by node id for a camera, a detector and a crop.

Gate 1: all suites green (counts ≥ baseline + new); the phase 0 characterisation tests still pass unchanged; on the Pi (CI .deb, offline
installer) toggling a detector over NT stops and restarts its results, and the camera keeps streaming.

## Phase 2: Java library split

Work:
- New native package `org.lumenvision.lib`:
  - `LumenCoprocessor` (connection, `.version`/`.protocol` handshake, `.graph`, recording, LED);
  - `LumenNode` (by name: `setEnabled`, `isEnabled`, `takeSnapshot`, `setFpsLimit`);
  - `LumenDetector extends LumenNode` (results: `getLatestResult`, `getAllUnreadResults`, multi-tag, constrained seed, depth fields, intrinsics);
  - result types keep the packet's data under LumenVision's own names; `LumenUtils` keeps only what is not already in wpimath.
- `org.lumenvision.photoncompat` reimplemented on `lib`:
  - `PhotonCamera(name)` resolves the camera and its detectors from `.graph`; `setPipelineIndex(i)` enables pipeline *i*'s nodes and disables the others (D2);
  - `setDriverMode(true)` disables the camera's detectors and remembers which were on, `false` restores them; `getDriverMode` reads it back from `.graph`;
  - `takeInputSnapshot` / `takeOutputSnapshot` snapshot the camera / the enabled detector; `setFPSLimit` sets it on the enabled detector; `setLED` maps `VisionLEDMode`;
  - `PhotonPipelineResult`, `PhotonTrackedTarget`, `PhotonPoseEstimator` with photonlib's eight `PoseStrategy` values, `PhotonUtils`, `EstimatedRobotPose`, and `sim/`.
- Remove the photonlib-shaped classes from the root package (`LumenCamera`, `LumenPoseStrategy`, `LumenLedMode`, `LumenCoprocessorControl`'s REST
  driver mode / pipeline calls). Move the pose-strategy logic into `photoncompat`; the native estimator offers the coprocessor's multi-tag
  and constrained solves only.
- Update `robot/` (the example) to the native API, and add a second example (or test) that compiles against `photoncompat` only.

Tests:
- Port `LumenCameraTest`, `LumenResultPacketTest`, `LumenPoseEstimatorTest`, `LumenUtilsTest` and `sim/VisionSystemSimTest` to their new homes; new
  `LumenNodeTest` (enable/snapshot/fps writes, `.graph` resolution, protocol refusal) and `PhotonCameraCompatTest`.
- The phase 0 characterisation tests, with their fake coprocessor switched to the phase 1 topics: same assertions, all passing.

Gate 2: `./gradlew test` green; both robot examples build; on the Pi, a robot program on the `photoncompat` API (run in WPILib simulation against
the Pi's NT server) switches pipelines, toggles driver mode and takes both snapshots; the native API does the same through `LumenNode`.

## Phase 3: replace pipeline profiles with nodes

Work:
- One-time migration in `DB.Load`: each profile becomes its own detector node bound to the camera, named `<camera>-<profile>`; the active profile's
  node is enabled and keeps the old `ActiveDetectionSinkId` (so downstream bindings survive), the others are disabled; each profile's field layout moves
  to its node; camera overrides per D3. Write a backup of the pre-migration `data.json` next to it.
- Remove `PipelineProfile.cs`, `CameraOverrides`, `Source.Profiles` / `ActiveProfileIndex` / `ActiveDetectionSinkId`, `PipelineProfileController.cs`, and the
  profile paths in `SourceManager.cs` (`ActivateProfile`, `ApplyCameraSettings` overrides layering, `SetProfileCameraOverrides`, profile field layouts),
  `SinkManager.cs` (`CreateOrReplaceDetectionSinkForProfile`, the active-profile lookups), `DB.cs` (profile reactivation) and `SnapshotService.cs`.
- Web UI: remove the profile UI (`Inspector.tsx`, `BottomStrip.tsx`, `LeftRail.tsx`, `ApiService.ts`, `types/index.ts`, `model.ts`); `MatchPage.tsx` finds
  a camera's detector from the bindings; add an enable/disable switch on every node in the graph and inspector.
- NT `config/pipelineIndex` and `status/pipelineIndex` stop being honoured (removed outright in phase 5).

Tests:
- Migration tests on both phase 0 fixtures: node count, names, enabled flags, bindings, field-layout paths, camera settings, backup written; loading the
  migrated file a second time changes nothing.
- Remove the profile tests (list them in the commit message); everything else unchanged.

Gate 3: all suites green; no references remain (`git grep -E "PipelineProfile|ActiveProfileIndex|ActiveDetectionSinkId|CameraOverrides"` is empty
outside the migration code); install on the Pi over the current config, and verify in Chrome that the graph shows the migrated detectors with the right one
enabled, results flow, and the layout survives a restart; the phase 2 robot program still switches pipelines.

## Phase 4: remove driver mode

Work:
- Remove `SetDriverMode`/`GetDriverMode` and `m_DriverMode` from `ApriltagDetector` and `ObjectDetectionSink`, `Manager.h/.cpp`, the SWIG surface,
  `SinkController.cs`'s `sink/driverMode` routes, `SinkManager.cs`'s carry-over on rebuild/duplicate, and the Inspector toggle.
- Migration: a detector saved with driver mode on is loaded disabled.

Tests: delete `tests/test_driver_mode.cpp` and the driver-mode lines in `DuplicateTests.cs`; add a migration case; the `photoncompat` driver-mode
characterisation test still passes (it now goes through node enable).

Gate 4: all suites green; `git grep -i drivermode` hits only `photoncompat` and the migration; in Chrome, disabling a detector leaves the camera's own
preview streaming.

## Phase 5: NT control surface and naming

Work:
- Topics are published under each node's own name (sanitised, unique; id when unnamed). Remove camera aliasing from `NetworkTablesControlService.cs`
  (`ComputeCameraAliases`, `RefreshAliases`, `ResolveCamera`, `PublishCameraStatus`, the `AppliedAliases` / `PublishedStatus` / `ForgetSink` caches) and
  `NetworkTablesSink`'s alias maps in favour of a plain name table.
- Remove the old config topics: `pipelineIndex`, `driverMode`, camera-scoped `fpsLimit`, `inputSnapshot`, `outputSnapshot`, and their `PollConfigRequests`
  fields and `PublishNodeStatus` arguments. What remains: per node `enabled`, `snapshot`, `fpsLimit`, `constrainedSeed`, `robotToCamera`; root `recording`, `ledMode`.
- LED: a native `Off/On/Blink` enum plus "auto" (on while any detector is enabled), replacing PhotonVision's numbering; `photoncompat` maps `VisionLEDMode`.
- Snapshot REST and the `/snapshots` page work by node; snapshot files are named after the node.
- Bump `.protocol`; the vendordep refuses older coprocessors (D5).

Tests: `NtControlTests.cs` rewritten for the reduced surface; the NT4 e2e cases for renamed topics; a test that a write to a removed topic is ignored
and logged once; `SnapshotTests.cs` by node.

Gate 5: all suites green; `NetworkTablesControlService.cs` has no camera resolution left; the phase 2 robot programs (native and compat) pass on the Pi.

## Phase 6: one encoding per result

Work:
- Drop the `tags/ids,x,y,z,r0..r8` arrays and the `multitag/*` scalars from `NetworkTablesSink::PublishSourceResult`; keep the `result` packet, the `raw`
  JSON for object detection, `latencyMs`, `fps`, intrinsics, and the best-target scalars (D4). Update `sim/SimResultEncoder.java` to match.
- The packet layout is unchanged (schema 2): `photoncompat` still needs skew, area and min-area-rect corners.

Tests: NT4 e2e asserts the removed topics are absent and the packet is unchanged byte-for-byte for a fixed input; `VisionSystemSimTest` green.

Gate 6: all suites green; AdvantageScope (or `nt4` inspection) on the Pi shows only the documented topics per node.

## Phase 7: documentation, release and end-to-end check

Work:
- README: NT topic table, robot-code section (native API first, `photoncompat` as the migration path), snapshots, LED.
- ROADMAP Phase E: rewrite E0/E2/E3 to the native-first design; record the removals.
- `release.yml` and `vendordep-template.json` for the renamed packages; release notes with the protocol break and the migration.

Gate 7 (done): CI release build green; the `.deb` installs with the offline installer on the Pi over a config that still has profiles, and
migrates it; full Chrome pass over Graph, Inspector, Match, Snapshots, Stereo and Settings; both robot examples against the Pi; every suite green,
with the final test counts and the removed tests listed in the release notes.

## Progress

| Phase | State | Notes |
|-------|-------|-------|
| 0 | done | Baseline recorded (native 102, server 247, Java 44 plus 2 skipped hardware tests); fixtures in `Server.Tests/fixtures`; the Pi's saved config has no pipeline profiles. |
| 1 | done | Native 103, server 255. Deviation: `Sink.Enabled` is stored as a `DisabledNodes` list in `data.json` (sinks are rebuilt in many places and would lose a per-record flag); sources have no enabled state; per-node `fpsLimit` is left for phase 5. Pi check (CI build `0.0.0-dev56`, offline installer): stopping and starting a detector over REST stops and resumes its results while the camera and another detector keep running, and `DisabledNodes` is written; snapshots by node id work for a camera and a detector. The NT half could not be run against the Pi from this machine (the Pi is on another subnet and the PC's firewall blocks inbound 5810), so NT behaviour is covered by the native end-to-end and server tests. |
| 2 | done | Java 49 tests (3 skipped hardware). `org.lumenvision.lib` and `org.lumenvision.photoncompat` split as planned; compat `setPipelineIndex` still uses the old `config/pipelineIndex` topic until phase 3. |
| 3 | done | Option 1: a NetworkTablesSink binds every detector node itself. `NodeMigration` rewrites a saved config once (original kept as `data.json.before-nodes`). Checked on the Pi (build `0.0.0-dev58`): a config with two profiles gave one new stopped node beside the running active one, and the graph and dashboard showed them. |
| 4 | done | Driver mode is gone from the core, server, NT and UI. |
| 5 | done | Tables are node names (unique with the id), snapshots and fps limits apply per node, `inputSnapshot`/`outputSnapshot`/camera aliasing are gone, LED has an auto mode, protocol 4. |
| 6 | done | The `tags/*` arrays and `multitag/*` scalars are gone; the packet is unchanged. |
| 7 | done except the NT link | README and ROADMAP updated; a full Chrome pass was done on the Graph and Dashboard pages through a local dev proxy (the Chrome machine cannot reach the Pi directly). The NT-over-network checks and the Java examples against the Pi still need a machine the Pi can reach. |
