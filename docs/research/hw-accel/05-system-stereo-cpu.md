# 05 - Whole-system, stereo and remaining CPU stages on RK3588

Research agent 5 of 5. Scope: thread model and big.LITTLE pinning, governors/frequency pinning,
DDR bandwidth/frequency, per-stage profile data, OpenCV build flags, SGBM stereo offload/speed-up,
depth fusion, NT publishing/serialisation, logging/IO, .NET server interference, thermal/power,
kernel/BSP choices, benchmarking methodology on the Orange Pi.

Read-only on the repo except this file.

## Status

- [x] Context docs read (README, ROADMAP, PERFORMANCE_ANALYSIS, STEREO_IMPLEMENTATION_PLAN)
- [x] Code read (CpuAffinity, ISource/ISink, FramePool, StageProfile, stereo, fusion, NT sink, Logger, SystemMonitor, Server monitors)
- [x] scripts/packaging tuning reviewed (unit, udev devfreq rule, journald, OpenCV flags)
- [x] Web research (PVTM per-cluster OPP, DMC governor, thermal, power, NT flush, .NET GC, rkmon paths, mainline status, OpenCV dispatch)
- [x] Ranked recommendations
- Status: COMPLETE (2026-10-24). Numbers marked UNVERIFIED are analytic estimates awaiting on-device measurement.

## Findings

### Already done (verified in code/docs, do NOT re-propose)

- Per-frame AprilTag log line is now `LogLevel::Debug` and dropped before lock/alloc/IO unless `LUMEN_LOG_DEBUG=1`
  (LumenCore/Logger.cpp:14-21,55; ApriltagDetector.cpp:374). Logger keeps the file open (Logger.cpp:128-136), no stdout.
- MPP hardware JPEG decode (MppJpegDecoder.cpp), decode-to-grey, V4L2 drain-to-newest + driver timestamps
  (docs/PERFORMANCE_ANALYSIS.md status line, §1, §5).
- All ISource capture threads and ISink processing threads pinned to the fastest-cpufreq tier
  (CpuAffinity.cpp:16-35, ISource.cpp:157, ISink.cpp:71). No per-node partitioning, no RT priority.
- `Nice=-19` in the unit (PERFORMANCE_ANALYSIS.md "Thread priority was inconsistent (fixed)").
- Per-node StageProfile (age / wait / process + named stages, 240-sample window, `GET /api/profile`)
  (StageProfile.h:13-43, ISink.cpp:99-108, ISource.cpp:121-125).
- Governor `performance` vs `ondemand` measured: "no difference once the cores are busy" (PERFORMANCE_ANALYSIS.md §stage profiler).
- NPU core mask tried, no gain for YOLO11 (same section).
- FramePool: 4 KiB-aligned, never-freed malloc buffers for GPU host import (FramePool.cpp:68-125). Not dma-heap backed.
- Dashboard shows GPU / DMC / NPU devfreq freq, governor, load (README.md:312).
- Stereo: grey-then-remap, CV_16SC2 fixed-point maps built once, crop to block multiple
  (StereoDepthNode.cpp:150-151, 226-233). SGBM uses MODE_SGBM_3WAY (SgbmStereoBackend.cpp:18).
- NT: one Flush() per Process batch (NetworkTablesSink.cpp:498); framework overhead measured 0.02 ms for JSON+publish
  (PERFORMANCE_ANALYSIS.md "The framework adds nothing").

### F1. SGBM runs at full rectified resolution although the output is a 16x16 block grid (biggest stereo CPU lever)

- Claim: `SgbmStereoBackend::Compute` runs `StereoSGBM::compute` on the full-res rectified pair, then box-averages
  the per-pixel CV_16S disparity into 16x16 blocks (SgbmStereoBackend.cpp:21-61). SGBM cost scales ~ W*H*D
  (D = numDisparities, which itself scales with width). Halving resolution cuts W*H by 4 and D by 2 => ~8x less work,
  while the 16x16 output grid becomes an 8x8 grid on the half-res image (same physical coverage). Disparity precision
  per block is still sub-pixel after averaging ~64 pixels.
- Evidence: code above; numDisparities derived as ceil(dNear - dFar)+16 (StereoDepthNode.cpp:51-52) => for the plan's
  640x480, fx 457, B 0.15 m, 0.75-8 m example: minDisp 8, numDisp = ceil(91-8)+16 = 99 -> rounded to 112.
- Confidence: high on the scaling (SGBM is O(W*H*D) per OpenCV docs); gain estimate medium.
- Estimated gain on RK3588 (analytic, UNVERIFIED - no published SGBM timings on A76/RK3588 were found): 640x480 x D=112 =
  34.4 M cost cells; SGBM_3WAY does a cost computation plus 3 aggregation passes per cell in int16 NEON (8 lanes). At a
  plausible 15-25 cycles/cell/8 lanes and 4 A76 @ 2.3 GHz the full-res pass lands at roughly 30-80 ms - over the 33 ms budget
  by itself. At 320x240 x D=56 (4.3 M cells, 8x fewer) expect ~4-10 ms. This is the single largest CPU saving available for
  the stereo node, and makes SGBM a viable fallback/accuracy reference rather than a toy.
- Also: rectLeft/rectRight/colour Mats are freshly allocated every pair (StereoDepthNode.cpp:228-233, 285-310); make them members
  so `cv::remap` reuses the buffers (glibc's dynamic mmap threshold mostly hides this, but it is free to fix). 
- Effort/risk: S. Add a `downscale` factor (pyrDown or INTER_AREA resize of both rectified images, or better: build
  rectify maps directly at the reduced size so remap produces the small image in one pass), divide min/numDisparities
  by the factor, multiply disparities back. Risk: loses thin structures; irrelevant for block-granular output.
- Test on the Orange Pi: StereoDepthNode already reports `rectifyMs`/`extractMs` in its JSON (StereoDepthNode.cpp:314-319)
  and the node's StageProfile `process`; compare full vs half res on a recorded pair, plus validFraction / medianDepth.

### F2. Stereo rectification does extra full-frame work that can be removed

- (a) Remap is over the whole source frame, then cropped (StereoDepthNode.cpp:229-233). Use map ROI views
  (`m_MapLx(cropRect)`) so remap only computes the cropped region. Small gain (crop is usually most of the frame with alpha=0).
- (b) Crop origin ignores the valid ROI's x/y: `cv::Rect(0, 0, m_CropW, m_CropH)` although roi = roiL & roiR
  (StereoDepthNode.cpp:156-167 vs 232-233). With alpha=0 the ROI is usually ~full frame, so mostly harmless, but it is
  a correctness bug when ROI.x/y > 0 (black border can enter the matcher, which the comment says must never happen).
- (c) `STEREO_FRAME_RECTIFIED_LEFT` output remaps the BGR left frame again (3 channels) and `clone()`s it every pair
  (StereoDepthNode.cpp:285-288; overlay 306-307) even if the downstream detector only wants grey. `ISource::HasActiveColorFrameConsumer()`
  exists (ISource.cpp:83-91) and could select: grey-only consumer => publish the already-computed `rectLeft` (zero extra cost).
  DEPTH_COLORMAP/OVERLAY should likewise be skipped when `HasActiveFrameConsumer()` is false (headless robot).
  Est. gain: 3-ch remap 640x480 ~1-2 ms + clone ~0.2 ms + colormap/resize ~0.5 ms per pair (UNVERIFIED; measure).
- (d) Could fold rectification into the source-side decode: MPP JPEG decode gives NV12, Y plane is grey; RGA cannot do
  arbitrary remap (only affine/resize/rotate), so remap stays CPU or moves to GPU (OpenCL/Vulkan). At 640x480 grey,
  NEON remap is ~0.3-0.6 ms per eye (UNVERIFIED), so GPU offload is not worth it at that size.
- Confidence: high on code facts, medium on ms numbers. Effort: S.

### F2b. Stereo offload options, ranked for this codebase

| Option | Where | Latency (640x480) | CPU cost | Effort | Verdict |
|---|---|---|---|---|---|
| codec-stereo `rkmpp_hwenc` (exists) | VEPU580 encode + libavcodec P-frame decode on the calling thread | 3.0 ms measured (STEREO_IMPLEMENTATION_PLAN.md §0) | ~1-1.5 ms CPU: 2 luma memcpys into MPP buffers + single-thread CAVLC P-frame decode (`thread_count` default 1; cs_backend_rkmpp_hwenc.c:457-477) | done | primary path; accuracy unmeasured on FRC scenes |
| SGBM_3WAY at half res (F1) | A76 NEON, OpenCV pool | ~4-10 ms est. | 4 cores busy for that time | S | do it; best accuracy fallback |
| StereoBM (NEON, `cv::StereoBM`) at half/full res | A76 | est. 2-5x faster than SGBM | moderate | S | worth a 1-hour A/B since output is block-averaged anyway; worse on low texture |
| Vulkan/compute SGM on Mali-G610 | GPU | unknown; 2014 Mali-T604 OpenCL census+multires got 40 fps @640x480 / 60 disp (https://etd.adm.unipi.it/t/etd-07042014-105839) | ~0 CPU | L (new shaders; vkapriltag infra reusable) | only if stereo becomes a headline feature; GPU already shared with AprilTag |
| NPU stereo network (RKNN) | NPU | unknown; no RK3588 stereo-net benchmarks found; NPUs struggle with cost-volume / 3D conv / grid_sample ops and on-chip memory at higher res (Snapdragon study needed tiling: https://scholarx.skku.edu/item/4a9eac89-1200-4703-928d-0760f5d63044) | ~0 CPU | XL | reject for now; NPU is also the YOLO engine |
| OpenCL T-API (`cv::UMat`) SGBM | GPU | OpenCV has no OpenCL SGBM kernel (only StereoBM has an OCL path, from memory - verify) | | S | not useful for SGBM |

- Hidden contention: codec-stereo and h264_rkmpp (WebRTCSink/RecordSink prefer it: Manager.cpp:1738-1741) share the two
  VEPU580 cores via mpp_service; a 1080p preview encode (~5-10 ms of VEPU per frame, UNVERIFIED) can queue ahead of a
  stereo pair. Test exactly as the plan's §10.6 item 5 says: stereo `extractMs` p95 with and without a WebRTC viewer.
- Avoidable copies on the hwenc path: codec-stereo memcpy's each rectified luma into an `mpp_buffer` (cs_backend_rkmpp_hwenc.c:283-288).
  If `cv::remap` wrote straight into MPP-owned (dma-buf) buffers - i.e. a dma-heap-backed FramePool and a codec-stereo API that
  accepts external buffers (`mpp_buffer_import`) - that memcpy disappears. ~0.1 ms at 640x480; only worth it at 1080p.

### F3. DepthFusion is cheap but not time-matched

- Fusion work is O(boxes x blocks) on a ~40x30 grid plus nlohmann::json edits: microseconds (DepthFusionNode.cpp:16-82).
  Not a perf target. But `GetLastDepthGrid` returns the most recent grid regardless of the detection's captureTimeUs
  (DepthFusionNode.cpp:94), copying the vector under a mutex. For accuracy (not perf) the fusion should check
  |detection.captureTimeUs - depth.captureTimeUs| and/or keep a 2-3 entry ring of grids keyed by capture time.
- Since the detector runs on the depth node's RECTIFIED_LEFT output, detection is always >= 1 stereo frame behind, so
  the grid it fuses with is usually the NEXT pair (newer than the box) - up to 33 ms mismatch at 30 fps.
- Confidence: high (code). Gain: accuracy, not ms.

### F4. Logger is still synchronous for Info/Warning/Error: flush + stat syscall per call

- Every non-debug `EnterLog` takes a process-wide recursive mutex, writes, `flush()`es and then `fs::file_size()`s the
  file for rotation (Logger.cpp:47-60, 139-146, 79-84). Fine for rare events, but several error paths fire per frame
  (e.g. CameraSource.cpp:150 "camera grab failed", StereoDepthNode "no valid calibration attached yet" per pair,
  WebRTCSink sendFrame failure). An error storm = 30-90 synchronous write+fsync-less flush+stat per second on SD/eMMC
  serialised against every logging thread. 156 non-debug EnterLog call sites.
- Fix: rate-limit identical messages (log first N, then "repeated K times" once per second), and move the file write to a
  background thread with a bounded queue. Confidence high; gain only matters in failure modes (prevents
  failure-induced latency cascades). Effort S.

### F5. SystemMonitor CPU usage is measured over ~0 s (bug) and there is no per-cluster / throttle view

- `m_GetCPUUsage(m_ReadCPUData(), m_ReadCPUData())` reads /proc/stat twice back-to-back (SystemMonitor.cpp:36), so the delta
  spans microseconds: the reported CPU% is noise (and argument evaluation order is unspecified). Should keep the previous
  sample across loop iterations. Only aggregate CPU; no per-core load, per-cluster frequency, or thermal-throttle state.
  Temperature prefers `soc-thermal` (SystemMonitor.cpp:151) - RK3588 also exposes bigcore0/bigcore1/littlecore/center/gpu/npu
  zones (to verify on device), the A76 zones run hotter.
- Matters for benchmarking/regression detection, not for latency. Effort S.

### F6. Thread model: one flat "A76 set" for everything, OpenCV's own pool unmanaged, no thread names

- Facts: every ISource capture thread and every ISink thread pins itself to all fast cores (CpuAffinity.cpp:48-60).
  No `cv::setNumThreads`, no `pthread_setname_np`, no `sched_setscheduler`/SCHED_FIFO anywhere in LumenCore sources
  (grep, only object files matched). The service runs `Nice=-19`, no `CPUAffinity=`, no `CPUSchedulingPolicy=`
  (scripts/lumenvision.service). Library pools inherit the creating thread's affinity and nice on Linux:
  vkapriltag's WorkerPool is created in `VkApriltagBackend` ctor (VkApriltagBackend.cpp:150-156) which is built from
  `ApriltagDetector::Process` (ApriltagDetector.cpp:368, sink thread => inherits the A76 mask) - OK; but OpenCV's
  parallel_for pthread pool is process-global, lazily created by whichever thread first calls a parallel OpenCV
  function, sized to all 8 CPUs by default. If that first caller is a .NET request thread (calibration, snapshot,
  ImageFileSource etc.) the pool workers can run on the A55s and at nice 0 => SGBM_3WAY, remap, resize, cvtColor
  stripes straggle on 1.8 GHz A55s (critical path = slowest stripe). Also 8 OpenCV workers + vkapriltag 4 + libapriltag
  N + RKNN runtime threads all oversubscribe the same 4 A76 cores.
- Recommendation (cheap): (1) at LumenCore init call `cv::setNumThreads(4)` (or #fast cores) from a pinned thread so the
  pool is created with the A76 mask, (2) name every thread (`pthread_setname_np(“lv-src-<id>”, “lv-sink-<id>”)`, 15 chars) so
  `perf`, `top -H`, `/proc/<pid>/task/*/comm` attribute time per node, (3) optional per-node placement: give a node an
  affinity class {fast, little, any} - light sinks (NetworkTablesSink, DepthFusion, RecordSink muxing, MJPEG preview,
  SystemMonitor) belong on A55s, heavy ones on A76s.
- (4) Move the .NET side to the little cluster: `CPUAffinity=0-3` in the unit puts Kestrel, the GC and threadpool on the
  A55s by default; LumenCore's explicit `pthread_setaffinity_np` to 4-7 still works because a thread may widen its own mask
  inside the cgroup cpuset (sched_setaffinity(2): mask is intersected with the cpuset, not the parent's mask) [verify on
  device]. Library threads created later by unpinned threads (librknnrt, MPP, Mali driver threads) would then default to
  A55s, which is usually right for driver/wait threads but must be checked with `ps -eLo tid,psr,comm`.
- Confidence: high on facts; gain medium (removes straggler/jitter, p95 rather than mean). Effort S-M.

### F6b. LATENT BUG: CpuAffinity can pin every heavy thread to only 2 A76 cores on some RK3588 chips

- Claim: `DetectPerformanceCores()` groups CPUs by `cpuinfo_max_freq` and takes only the single highest tier
  (CpuAffinity.cpp:20-34). RK3588's two A76 clusters (policy4 = cpu4-5, policy6 = cpu6-7) get their OPP tables per
  cluster from PVTM/leakage binning, so their max frequencies can differ on the same chip: CNX Software reports one board at
  cpu4-5 = 2304 MHz, cpu6-7 = 2352 MHz (others 2304/2304, 2400/2400). On such a chip there are three tiers and the code picks
  only cpu6-7: every capture and sink thread (plus inherited library pools) is squeezed onto 2 cores while cpu4-5 idle.
- Evidence: https://www.cnx-software.com/2022/07/17/what-is-pvtm-or-why-your-rockchip-rk3588-cpu-may-not-reach-2-4-ghz/ ;
  per-policy layout policy0/4/6: https://bbs.21ic.com/icview-3436964-1-1.html
- Confidence: high that the code would misbehave on such a chip; UNVERIFIED whether the team's boards are affected
  (check `grep . /sys/devices/system/cpu/cpufreq/policy*/cpuinfo_max_freq` on each Pi; also `GET` a debug endpoint
  returning `GetPerformanceCoreIds()`).
- Estimated gain where it bites: up to 2x CPU capacity for the pipeline (the measured run used 3 heavy sinks on "4" cores).
- Fix (XS): classify by `/sys/devices/system/cpu/cpuN/cpu_capacity` (exported by arm64 topology from the DT
  capacity-dmips-mhz; A76=1024, A55~=530 on RK3588 - verify), or by MIDR part number in /proc/cpuinfo (0xd0b A76, 0xd05 A55),
  or treat every CPU within ~15% of the top max frequency as a performance core. Test: a unit test feeding fake sysfs values
  (2304/2352/1800) to the detection function.

### F7. .NET server GC: ASP.NET Core projects default to Server GC

- `Microsoft.NET.Sdk.Web` sets `ServerGarbageCollector=true` by default; Server.csproj does not override it
  (Server/Server.csproj:1-40, only InvariantGlobalization). Server GC creates one heap + one GC thread per CPU (8 here),
  the GC threads are affinitised and high priority; .NET 9+/10 enables DATAS by default which shrinks heap count
  dynamically (to verify for net10.0). Native LumenCore threads are not suspended by a GC, but GC threads compete for
  the same A76 cores while marking. Cheap fix: `<ServerGarbageCollector>false</ServerGarbageCollector>` (workstation,
  concurrent GC, one background GC thread) or `DOTNET_GCHeapAffinitizeMask=0x0F` + `DOTNET_GCHeapCount=2` to keep GC on the
  A55s. Also consider `TieredPGO`/ReadyToRun only for startup - not latency relevant.
- Background services are low-rate (StateChannel 1 s tick, NetworkTablesControlService 100 ms, LinuxResourceMonitor 2 s,
  AcceleratorMonitor - Server/*.cs grep), MJPEG preview polls at 100 ms. So .NET steady-state CPU is small; the risk is
  GC pauses/threads and request bursts (WebUI open, MJPEG preview, snapshot/zip export) landing on the A76s.
- Confidence medium. Microsoft docs (https://learn.microsoft.com/en-us/dotnet/core/runtime-config/garbage-collector):
  runtime default is workstation, but the Web SDK turns Server GC on (VERIFY on device: `grep System.GC
  /opt/lumenvision/Server.runtimeconfig.json`); Server GC threads are affinitised by default (`System.GC.NoAffinitize=false`),
  `GCHeapCount` affinitises n heaps to the *first n processors* (cpu0..n-1 = the A55s on RK3588 - a happy accident),
  `GCHeapAffinitizeMask`/`GCHeapAffinitizeRanges` work on Linux; DATAS is on by default since .NET 9 and starts with one heap.
  Concrete: `<ServerGarbageCollection>false</ServerGarbageCollection>` in Server.csproj, or keep server GC with
  `DOTNET_GCHeapAffinitizeRanges=0-3` in the unit. Gain: p99 jitter only. Effort XS.

### F7b. NT4 publishing: already near-optimal; no action beyond hygiene

- One `Flush()` per batch (NetworkTablesSink.cpp:498). In current ntcore, `NetworkClient::Flush()` fires a libuv async that
  runs `HandleLocal()` + `SendOutgoing(now, flush=true)` - no client-side throttle in that code path; without Flush the local
  queue is read on a 100 ms timer (`m_readLocalTimer->Start(100,100)`), so the Flush is load-bearing and must stay.
  Source: https://raw.githubusercontent.com/wpilibsuite/allwpilib/main/ntcore/src/main/native/cpp/NetworkClient.cpp ;
  WPILib doc says flushes closer than the minimum period are deferred (not dropped):
  https://github.wpilib.org/allwpilib/docs/release/cpp/group__ntcore__network__cfunc.html . Outgoing data is held until time
  sync exists (ClientImpl.cpp `if (!m_haveTimeOffset) return;`).
- Measured JSON + publish = 0.02 ms (PERFORMANCE_ANALYSIS.md). Hygiene only: `.graph`, `.version`, `.status` are Put every
  tick (NetworkTablesSink.cpp:504-523); NT suppresses unchanged values, but `.status` changes every tick (uptime) and is
  JSON-dumped per result batch => at 3 sinks x 30 fps that is ~90 status messages/s on the wire. Rate-limit `.status`/
  `heartbeat` to e.g. 10 Hz. Gain: bandwidth on the robot radio, not latency. Confidence high. Effort XS.

### F9. DDR (DMC) governor: `performance` is right for latency; keep it, but know the cost

- Already done: udev rule pins `dmc`, GPU (fb000000.gpu) and NPU (fdab0000.npu) devfreq governors to `performance`
  (scripts/build-deb.sh:77-78, install-deps.sh:811-818, applied in postinst).
- Evidence it matters: with `dmc_ondemand` the Rock 5B DRAM sat at 528 MHz most of the time and Geekbench lost ~10%;
  `dmc_ondemand` saves ~0.5-0.6 W idle; DRAM at fixed 2112 MHz measured ~0.7 W above a 1.02 W base (5 V USB-C).
  RK3588 DMC range 528-2112 MHz (LPDDR4X; LPDDR5 boards higher). `upthreshold` default 40 under
  /sys/devices/platform/dmc/devfreq/dmc/. Source: tkaiser et al., https://forum.radxa.com/t/rock-5b-debug-party-invitation/10483?page=24
  and /10483/458. Caveat from a kernel commit: `perf stat` DDR cycle counter on RK3588 LPDDR4X reads half the real frequency;
  MB/s counters are correct (https://git.zx2c4.com/laptop-kernel/log/drivers/devfreq?showmsg=1).
- Implication for this pipeline: camera DMA (USB), MPP JPEG decode, RGA, NPU (DDR-bound weights/activations), GPU and the
  CPU all share the one memory controller; with dmc pinned high, contention is bounded. Remaining lever is *traffic*, i.e.
  avoiding full-frame copies (BGR clones, 3-ch remaps in F2c, upload copies) - which the other agents' dma-buf work covers.
- The only remaining question is power/thermal on the robot (F10). Confidence: medium (forum measurements, other board).

### F10. Thermal and power on an FRC robot

- Bare RK3588 boards throttle under sustained all-core load: big cores dropped to 2256 MHz at ~88.6 C after ~2 min without a
  cooler; with an ICE Tower fan no throttling over 1 h (https://magazinmehatronika.com/en/52pi-ice-tower-for-orange-pi-5-review);
  52Pi measured 20-27 C lower with fan (https://cnx-software.com/?p=113116). Armbian: >85 C and throttling within 30-60 s
  reported anecdotally (https://blog.armbian.com/stay-cool/). The vendor BSP trip for the soc zone is 85 C (to verify in the DT
  on device: `cat /sys/class/thermal/thermal_zone*/trip_point_*_temp`).
- In this pipeline, 3 heavy sinks keep the A76s at max frequency continuously (PERFORMANCE_ANALYSIS.md), plus GPU and NPU at
  pinned max clocks => exactly the sustained-load case. Throttling turns into latency regressions mid-match with no software
  change; inside a robot belly pan with no airflow it is likely without active cooling.
- Recommendations: (1) require an active cooler/heatsink in the README hardware section; (2) surface per-zone temps
  (bigcore0/bigcore1/littlecore/center/gpu/npu/soc) plus `scaling_cur_freq` vs `scaling_max_freq` per policy and a
  "throttled" flag on the Match page and in NT `.status` so a drive team sees it; (3) StageProfile already gives latency - log
  it alongside temperature for post-match analysis; (4) optionally cap devfreq for GPU/NPU to the second-highest OPP to cut
  power if a node does not need it (measure). Confidence medium. Effort S.
- Power: RK3588 boards are 5 V (Orange Pi 5) or 5-20 V USB-PD / 12 V DC (5 Plus/Max) - on FRC the supply must ride through
  battery sag (brownout at ~6.8 V on the roboRIO). Measured board draw (CPU-only loads): Orange Pi 5 Plus 2-3 W idle, 6-8 W full
  load at 5 V (https://the-diy-life.com/orange-pi-5-plus-test-review/); Orange Pi 5 3.3 W idle / 7.3 W full; OPi 5 Ultra ~9 W peak
  under CPU+memory stress. No published NPU/GPU-loaded figures; with GPU+NPU+VPU busy and devfreq pinned to max, expect ~10-12 W
  (UNVERIFIED) => 2-2.5 A at 5 V plus cameras. PhotonVision's docs list a Pololu S13V30F5 (3 A) regulator for the Orange Pi 5
  (https://docs.photonvision.org/en/latest/docs/quick-start/common-setups.html); teams report OP5 + cameras at 2-3 A and USB-C
  cable gauge as a brownout cause (https://www.chiefdelphi.com/t/how-do-you-power-co-processor-for-vision/509362).
  => A 3 A regulator is marginal for a fully loaded LumenVision board with 2 USB cameras; recommend >= 5 A continuous and a
  short, heavy-gauge lead, and document it. The pinned-max devfreq governors (F9) add ~0.5-1 W; worth it for latency, but it is
  a knob if a team hits power limits.

### F11. The devfreq udev rule may not match the GPU node on the 6.1 vendor kernel the image ships

- The rule is `SUBSYSTEM=="devfreq", KERNEL=="fb000000.gpu|dmc|fdab0000.npu", ATTR{governor}="performance"`
  (scripts/build-deb.sh:78). The image is Armbian Trixie *vendor* (scripts/build-image.sh:87 => Rockchip 6.1 BSP).
  rkmon (tested on Rock 5B+, Debian 12, 6.1 BSP) reads the GPU at `/sys/class/devfreq/fb000000.gpu-mali/`
  (https://pkg.go.dev/github.com/isac322/rkmon). udev's `|` alternatives are exact glob matches, so `fb000000.gpu` would not
  match `fb000000.gpu-mali`, leaving the GPU on `simple_ondemand` while DMC/NPU are pinned.
- Confidence: medium-low (node name differs between kernels/boards; UNVERIFIED on the team's Orange Pi 5 Plus image).
  Check: `ls /sys/class/devfreq; cat /sys/class/devfreq/*/governor` - the Dashboard's AcceleratorMonitor already shows it.
- Fix (XS): `KERNEL=="fb000000.gpu*|dmc|fdab0000.npu"`. Gain if it applies: the Vulkan AprilTag GPU stage (8.7 ms at 720p d1)
  runs at whatever OPP simple_ondemand picks; Mali-G610 tops at ~1 GHz, ondemand ramp-up adds ms-scale latency at 30 fps duty
  cycles (UNVERIFIED magnitude; measure GPU stage with governor performance vs simple_ondemand).

### F12. AcceleratorMonitor spawns `sudo -n cat` every 2 s

- Server/AcceleratorMonitor.cs:21,35,108-115 + PrivilegedCommand.cs:32-40: NPU per-core load is read via `sudo -n cat
  /sys/kernel/debug/rknpu/load` every 2 s. Each call = .NET fork/exec of sudo + PAM session + sudo's own journal lines
  (typically "COMMAND=/usr/bin/cat" plus pam_unix session open/close) => ~1.5 journal entries/s, which churns the 20 MB journal cap
  (journald-lumenvision.conf) and evicts useful logs, plus a few ms of CPU per spawn (UNVERIFIED cost; fork of a large .NET process).
- Fix: read the aggregate `/sys/class/devfreq/fdab0000.npu/load` (no root, per rkmon), or create a tiny root helper that keeps
  the file open (or a systemd unit that bind-mounts/chmods the debugfs node), or poll only while the dashboard is open.
  Also add `Defaults:lumen !syslog`/`!pam_session` style sudoers options if sudo stays. Confidence high on the mechanism. Effort XS.

### F13. Kernel/BSP: stay on the Rockchip 6.1 vendor kernel for production; track mainline for later

- The image is Armbian Trixie vendor (6.1 BSP) + libmali g24p0 Vulkan (scripts/build-image.sh:2-3,87,105). Everything this
  pipeline accelerates depends on vendor-only interfaces: rknpu + librknnrt (RknnDetectionBackend), `/dev/mpp_service` (MPP
  JPEG decode, codec-stereo rkmpp_hwenc, h264_rkmpp), RGA3 via librga/`/dev/rga`, libmali Vulkan (vkapriltag findings in
  PERFORMANCE_ANALYSIS.md).
- Mainline status: "Rocket" DRM_ACCEL NPU driver landed for Linux 6.18 with Mesa 25.3 Teflon (TFLite delegate, not RKNN API)
  (https://www.phoronix.com/news/Rockchip-NPU-Linux-Mesa ; FOSDEM 2026 slides
  https://fosdem.org/2026/events/attachments/KLFW73-no-line-like-mainline-rockchip/slides/267550/no-line-l_vrjwxmj.pdf);
  GPU = panthor + PanVK; VPU = V4L2 stateless (no MPP API, no VEPU H.264 encode MV path codec-stereo needs); RGA3 not upstream.
  Moving to mainline would mean rewriting RKNN, MPP, RGA and codec-stereo backends. Not recommended for this season.
- What mainline does offer for *measurement*: rockchip-dfi perf PMU (`perf stat -a -e rockchip_ddr/...`) gives real DDR
  bandwidth (RK3588 cycle count needs x2 except LPDDR5; MB/s correct)
  (https://lists.infradead.org/pipermail/linux-rockchip/2023-June/039118.html). On the vendor 6.1 kernel use
  `/sys/class/devfreq/dmc/load` ("busy%@freq") instead. A one-off mainline boot of the same board is a reasonable way to
  measure the pipeline's DDR bandwidth (CPU-only paths) if contention becomes a question.
- Confidence: high on the dependency facts (from the repo), medium on mainline status details.

### F14. Benchmarking methodology on the Orange Pi (what to run, what to read)

Existing: StageProfile per node (`GET /api/profile`, reset via `POST /api/profile/reset`), AcceleratorMonitor (devfreq
freq/governor/load + rknpu load), SystemMonitor (CPU%, temp, RAM). Gaps and a proposed protocol:

1. Fixed workload: replay a recorded clip through `VideoFileSource`/`ImageFileSource` (exists) so runs are comparable; for
   stereo, a recorded L/R pair set. Warm up 10 s, then `POST /api/profile/reset`, measure 60 s, read p50/p95/max per stage.
   Report p95, not mean - the pipeline's problems are jitter.
2. Per-thread CPU and placement: after naming threads (F6), `top -H -p $(pidof Server)` or
   `ps -eLo tid,psr,pcpu,ni,cls,comm -p <pid>`; `pidstat -t -p <pid> 1`. `psr` shows A55 (0-3) vs A76 (4-7) placement.
3. Scheduling latency: `perf sched record -a -- sleep 10; perf sched latency --sort max` or
   `trace-cmd record -e sched_switch -e sched_wakeup -P <pid>` - quantifies run-queue wait that StageProfile's "wait" only
   sees at node granularity. On big.LITTLE use the per-PMU events: `perf stat -e armv8_cortex_a76/cycles/,armv8_cortex_a55/cycles/`
   (PMU names to verify with `ls /sys/bus/event_source/devices`).
4. Hot spots: `perf record -g -p <pid> -- sleep 20` with LumenCore built RelWithDebInfo (`-g -fno-omit-frame-pointer`);
   for OpenCV hot spots this shows whether NEON paths are taken (function names like `*_NEON`/`opt_*`).
5. Accelerator counters (rkmon's paths, https://pkg.go.dev/github.com/isac322/rkmon): GPU `/sys/class/devfreq/fb000000.gpu*/{load,cur_freq}`,
   NPU `/sys/class/devfreq/fdab0000.npu/load` and per-core `/sys/kernel/debug/rknpu/load` (root), RGA `/sys/kernel/debug/rkrga/load`
   (root), VPU `/proc/mpp_service/load` (root; set `load_interval`) and `/proc/mpp_service/*/task_count`, DMC
   `/sys/class/devfreq/dmc/load`, throttling `/sys/class/thermal/cooling_device*/cur_state` and per-zone temps. Tools: rkmon
   (Go TUI, MIT) or rktop (Rust) give a tegrastats-like view (https://docs.rs/crate/rktop/0.1.4).
6. Thermal-soak run: 15 min continuous at match load with the robot's actual enclosure; log temps + per-policy cur_freq +
   p95 age every 10 s; a pass criterion is "no cooling_device state change, p95 age drift < 10%".
7. Power: inline USB-C/DC meter at the board input under the soak workload; note brownout behaviour (see F10).
8. Ship it as `lumen-bench` (ROADMAP C5): a script that does 1-7 and prints a markdown table.
- Confidence high on tool availability; specific sysfs names vary by kernel (verify each on device).

### F15. MJPEG fallback preview encodes every frame on an A76 although the server reads it at <= 10 fps

- `MjpegSink::Process` runs `AsBgr()` + optional resize + software `cv::imencode` + base64 on every result (MjpegSink.cpp:61-80);
  the server polls every 100 ms (MjpegStreamModule.cs:14,85) and base64-decodes again (PERFORMANCE_ANALYSIS.md item 7 still open).
  The sink thread is pinned to the A76s like every sink. At 1080p a libjpeg-turbo encode is ~10-15 ms (PERFORMANCE_ANALYSIS.md §7),
  so a camera at 30 fps burns ~30-45% of one A76 to deliver 10 fps, contending with detectors. Only while an MJPEG viewer is open
  (MjpegPreviewJanitor reaps idle ones), and WebRTC is the primary path - so a fallback-only cost, but it hits exactly when someone
  is debugging on a weak network.
- Fix (S): encode lazily - store the latest Frame (refcounted, no copy) and encode in `GetLatestJpeg*()` only when a newer frame
  arrived since the last encode (caps encode rate at the poll rate, 3x saving at 30 fps), run the sink on the A55 class (F6), and
  drop base64 (raw bytes over SWIG). Hardware JPEG encode via MPP is another agent's area. Confidence high.

### F8. OpenCV is built with near-default flags (install-deps.sh:235-250)

- Flags: Release, contrib, FFMPEG on, no GTK/highgui/python/java. Nothing about `CPU_BASELINE`/`CPU_DISPATCH`,
  `WITH_TBB`, `WITH_OPENMP`, `WITH_OPENCL`, `ENABLE_LTO`, `-mcpu`. libtbb-dev is apt-installed (install-deps.sh:218-222)
  but `WITH_TBB` is not set (OpenCV default is OFF => parallel backend = pthreads).
- aarch64 defaults (verified in opencv 4.x cmake/OpenCVCompilerOptimizations.cmake,
  https://raw.githubusercontent.com/opencv/opencv/4.x/cmake/OpenCVCompilerOptimizations.cmake): `CPU_BASELINE=DETECT`
  (whatever the native compiler enables by default => NEON;FP16 for plain armv8-a), `CPU_DISPATCH="NEON_FP16;NEON_BF16;NEON_DOTPROD"`.
  5.0 is assumed to match (UNVERIFIED).
  Cortex-A76 supports dotprod and FP16 arith (ARMv8.2-A). So most hot kernels already get NEON; the dispatched dotprod/fp16
  paths are mostly DNN-related, which LumenCore does not use (RKNN/ONNX do inference).
- Verify on device: `cv::getBuildInformation()` (Manager could expose it through /api/about) - check "CPU/HW features:
  Baseline NEON FP16, Dispatched NEON_DOTPROD NEON_FP16 NEON_BF16", "Parallel framework: pthreads", "OpenCL: YES/NO",
  "JPEG: libjpeg-turbo". Rebuilding with `-DCPU_BASELINE=NEON,FP16,NEON_DOTPROD -DCMAKE_CXX_FLAGS=-mcpu=cortex-a76`
  would break running on A55-only boards? (A55 also supports dotprod/fp16 - ARMv8.2 - so RK3588/RK3566 both OK; Pi 4/A72
  would not.) Expected gain for SGBM/remap/cvtColor: 0-10% (UNVERIFIED) - low priority versus F1.
- OpenCL (T-API) is irrelevant unless code moves to `cv::UMat`; Mali-G610 OpenCL exists via libmali (vendor BSP) and Rusticl
  on Panfrost (mainline, newer). Not recommended as the path for SGBM (see F9).

## Ranked recommendations (this codebase)

Ranked by expected gain per unit effort; "gain" is on the Orange Pi 5 Plus pipeline described in PERFORMANCE_ANALYSIS.md.

| # | Change | Where | Expected gain | Effort | Confidence |
|---|---|---|---|---|---|
| 1 | Fix performance-core detection to include both A76 clusters (classify by `cpu_capacity`/MIDR, or within 15% of max freq) | LumenCore/CpuAffinity.cpp:20-34 | Up to 2x pipeline CPU on chips whose A76 clusters bin differently (2304 vs 2352 MHz); 0 otherwise. Check every board first | XS | high (mechanism), board-dependent |
| 2 | SGBM at reduced resolution (rectify maps built at /2, min/numDisparities /2, disparities x2) | SgbmStereoBackend, StereoDepthNode.cpp:51-52,226-233 | ~8x less SGBM work: est. 30-80 ms -> 4-10 ms at 640x480 | S | high scaling / medium ms |
| 3 | Size and place OpenCV's thread pool: `cv::setNumThreads(#fast cores)` called first from a pinned thread at Manager init | Manager ctor | Removes A55 stragglers in remap/SGBM/resize/cvtColor stripes and 8-thread oversubscription of 4 cores; p95 gain, est. 10-30% on OpenCV-heavy stages | XS | medium |
| 4 | Stereo node: skip colour/colormap/overlay work when no frame consumer; emit grey RECTIFIED_LEFT when the consumer is grey-only; remap only the crop ROI; fix crop origin | StereoDepthNode.cpp:156-167,229-233,285-310 | ~1-3 ms per pair + correctness (crop at roi.x/y) | S | high |
| 5 | Name threads (`pthread_setname_np`) and add a node placement class {fast, little}; light sinks (NT, DepthFusion, MJPEG, Record mux) on A55s | ISource/ISink + CpuAffinity | Frees A76 time for detectors; makes perf/top attribution possible | S | medium |
| 6 | .NET side off the A76s: `CPUAffinity=0-3` in the unit (LumenCore threads re-pin themselves to 4-7) and Server GC -> workstation or `DOTNET_GCHeapAffinitizeRanges=0-3` | scripts/lumenvision.service, Server.csproj | p99 jitter from GC/request bursts; verify library threads' placement after | XS | medium |
| 7 | Widen devfreq udev match to `fb000000.gpu*` (6.1 BSP may name it `fb000000.gpu-mali`) | scripts/build-deb.sh:78, install-deps.sh:812 | If the GPU is on simple_ondemand today: lower/steadier Vulkan AprilTag GPU stage (8.7 ms) | XS | medium-low (verify node name) |
| 8 | MJPEG fallback: encode lazily at the poll rate, raw bytes not base64, sink on A55s | MjpegSink.cpp:61-80, MjpegStreamModule.cs | ~2/3 of 30-45% of an A76 while an MJPEG viewer is open | S | high |
| 9 | Thermal/throttle visibility: per-zone temps, per-policy cur/max freq, cooling_device state on Dashboard/Match and NT `.status`; fix SystemMonitor CPU% (two back-to-back /proc/stat reads) | SystemMonitor.cpp:36,151; AcceleratorMonitor.cs | Detects mid-match throttling (A76 2.4 -> ~2.26 GHz and below at ~88 C without cooling) | S | high |
| 10 | Replace the `sudo -n cat` NPU-load poll every 2 s with the devfreq aggregate or a long-lived reader | AcceleratorMonitor.cs:108-115 | Removes 1 fork/exec + ~3 journal lines every 2 s | XS | high |
| 11 | Logger: rate-limit repeated messages, async writer | Logger.cpp:47-60,139-146 | Prevents error storms (30-90 sync write+flush+stat/s) from adding latency | S | high (only matters in faults) |
| 12 | DepthFusion: time-match detection and depth grid (ring of 2-3 grids by captureTimeUs) | DepthFusionNode.cpp:94 | Accuracy (up to 1 frame / 33 ms mismatch), not ms | S | high |
| 13 | Hardware docs: active cooling required; >= 5 A 5 V regulator, short heavy lead | README | Avoids throttling and brownout resets on the robot | XS | medium |
| 14 | `lumen-bench` protocol (F14) incl. thermal soak and VEPU contention test (stereo extractMs p95 with/without a WebRTC viewer) | tools/ | Makes every item above measurable | M | high |
| 15 | Rate-limit `.status`/`heartbeat` NT puts to ~10 Hz | NetworkTablesSink.cpp:500-523 | Radio bandwidth only | XS | high |

Not recommended now: GPU (Vulkan) SGM, NPU stereo nets, mainline kernel, SCHED_FIFO/isolcpus, TBB/jemalloc (see below).

## Ideas rejected (and why)

- **CPU governor `performance`**: already measured, no difference under this load (PERFORMANCE_ANALYSIS.md). Only revisit for
  light-load cases (e.g. a single Vulkan AprilTag node where the A76 may idle between frames and schedutil/ondemand ramps);
  cheap A/B, but low expected value.
- **isolcpus / nohz_full**: the service is the only workload on the board; isolation would remove the scheduler's ability to
  balance LumenCore's own threads (isolcpus disables load balancing on those CPUs, every thread would need manual placement).
  The measured "wait" stage is 0.1-0.3 ms, so scheduling latency is not the bottleneck. Partitioning via affinity (F6) gets
  the benefit without the fragility.
- **SCHED_FIFO for pipeline threads**: needs CAP_SYS_NICE (service holds none; PERFORMANCE_ANALYSIS.md), a runaway FIFO thread
  on 4 cores can starve the NT sink and the web server, and the measured wait is already sub-ms. Possible narrow use later:
  SCHED_FIFO prio 1-10 for the V4L2 capture thread only (it sleeps in DQBUF), via `AmbientCapabilities=CAP_SYS_NICE`.
- **Switching OpenCV to TBB/OpenMP**: OpenCV's pthreads backend already hands out stripes dynamically; on 4 equal A76 cores TBB's
  work stealing buys nothing measurable, and adds a dependency. The real problem is pool placement/size (F6), not the backend.
- **jemalloc/mimalloc**: full-frame buffers are pooled (FramePool), remaining per-frame allocations are few and large enough
  that glibc's dynamic mmap threshold keeps them in-heap after the first free; no allocator-bound symptom in the profile.
  Re-evaluate only if `perf` shows malloc/page-fault time.
- **Mainline kernel (panthor/rocket/V4L2 codecs)**: loses MPP, RGA3, RKNN API and the VEPU MV path (F13).
- **NPU stereo network**: no RK3588 benchmarks, operator-support risk, NPU already hosts YOLO (F2b).
- **Moving the uvcvideo URB copy / xHCI IRQ to A76s**: uvcvideo copies payloads in an unbound high-priority workqueue
  (process-context decode, https://lkml.iu.edu/hypermail/linux/kernel/1806.0/01871.html); for MJPEG at 720p30 that is ~100 KB
  per frame (~tens of us). Only worth looking at for raw YUYV/high-res cameras.
- **NT4 batching changes**: already one Flush per batch, 0.02 ms measured (F7b).

## Open questions

All to be answered on the Orange Pi; none could be checked from this Windows checkout.

1. `grep . /sys/devices/system/cpu/cpufreq/policy*/cpuinfo_max_freq` and `cat /sys/devices/system/cpu/cpu*/cpu_capacity` on
   every team board: are policy4 and policy6 equal? (decides how urgent #1 is).
2. `ls /sys/class/devfreq; cat /sys/class/devfreq/*/governor` - is the GPU node `fb000000.gpu` or `fb000000.gpu-mali`, and is it
   actually on `performance`? (#7)
3. `grep System.GC /opt/lumenvision/Server.runtimeconfig.json` - is Server GC on? How many GC threads (`ps -eLo comm | grep -c .NET\ Server\ GC`)?
4. SGBM ms at 640x480 full vs half res on the A76s (StereoDepthNode JSON `extractMs`), plus StereoBM for comparison, on a
   recorded FRC stereo pair; also codec-stereo `rkmpp_hwenc` accuracy vs SGBM on the same pairs (plan §10.6 item 4).
5. `cv::getBuildInformation()` on the board: baseline/dispatch, parallel framework, OpenCL, JPEG library.
6. Who creates OpenCV's pthread pool first in a normal boot (thread affinity of the `opencv`-created threads: `ps -eLo tid,psr,comm`).
7. Thermal trip points and cooling devices on the vendor 6.1 DT; soak test inside the robot enclosure.
8. Board input power under full GPU+NPU+VPU+CPU load (no published figure).
9. VEPU contention: stereo `extractMs` p95 with a 1080p h264_rkmpp WebRTC viewer running.
10. Whether `sched_setaffinity` widening beyond a systemd `CPUAffinity=` mask works under the unit's cgroup (it should, since
    `CPUAffinity=` sets the initial mask, not a cpuset; `AllowedCPUs=` would be a hard cpuset limit - do not use that one).
