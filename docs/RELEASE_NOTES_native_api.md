# Native node API: what changed and how to move over

This release replaces the PhotonVision-shaped parts of the coprocessor with generic node operations, and moves the PhotonVision names into
a compatibility package. Coprocessor and robot library must be updated together: the topic layout is now **protocol 4** and the library
ignores a coprocessor that reports another one.

## Coprocessor
- **Pipeline profiles are gone.** A saved configuration that still has them is converted when it loads: each profile becomes a detector
  node bound to its camera, the active one keeps running, the others are saved stopped, and the active profile's camera settings become the
  camera's own. The original is kept as `data.json.before-nodes`. Per-profile camera settings of the non-active profiles are not kept.
- **Driver mode is gone.** Stop the detector (`config/enabled` = false); the camera keeps streaming.
- **Nodes publish under their own names** (made unique with the id). `/<root>/.graph` lists every node and its table; `/<root>/.protocol` is 4.
- **Per-node controls:** `config/enabled`, `config/snapshot`, `config/fpsLimit`. Removed: `config/pipelineIndex`, `config/driverMode`,
  `config/inputSnapshot`, `config/outputSnapshot`, the `tags/*` and `multitag/*` topics. The result packet is unchanged.
- A NetworkTablesSink publishes every detector node. Snapshots over REST are `POST /api/snapshot/take?NodeID=`.
- The LED has an auto mode (-1): lit while any detector runs.

## Robot library (`photoncompat-java`)
- `org.lumenvision.lib`: `LumenCoprocessor`, `LumenNode`, `LumenDetector`, `LumenGraph`, `LumenPoseEstimator` (coprocessor multi-tag and constrained solves).
- `org.lumenvision.photoncompat`: PhotonLib's names. Replace `org.photonvision` imports with these. `setPipelineIndex` runs the camera's
  i-th detector (or `withPipelines("a", "b")`), driver mode stops them all, `takeInputSnapshot` saves the camera's frame.
- `LumenCamera`, `LumenCoprocessorControl`'s driver-mode/pipeline calls and the Lumen pose-strategy classes are removed.
