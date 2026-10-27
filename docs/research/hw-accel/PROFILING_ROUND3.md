# Third profiling round: the accelerators, the codecs, the server

Rounds one and two (`IMPLEMENTATION_STATUS.md`, `SANITISERS_AND_PROFILING.md`) measured the CPU side with the stage timers, `perf` and the sanitisers. This round
looked at what those could not see: the GPU's own counters, the NPU, the codec blocks, the RGA, scheduling between threads, behaviour under sustained heat, and
the .NET server. Measured on the Orange Pi 5 Plus with the Lenovo camera (1280x720 at 30 fps) and the stereo camera (2560x720 at 60 fps, split into halves),
vkapriltag 1.9.0, all Vulkan detectors on the ultrafast refine preset with probeMargin 4. Raw data in `profiles/round3/`, the harness in `profiles/harness/`.

## Tools added on the board

`bpftrace`, `trace-cmd`, `cpupower` (apt); Perfetto's `tracebox` 59.1; `dotnet-counters` and `dotnet-trace` on a private .NET 10 runtime in `~/.dotnet` (the service's own
runtime is self-contained); vkapriltag's Mali counter tools `hwcnt_run`/`hwcnt_timeline`, built against Arm's libGPUCounters at the commit vkapriltag pins; a small
RKNN timing harness (`profiles/harness/rknn_perf.cpp`). `perf`, `valgrind` and `heaptrack` were already there. No kernel security setting was changed.

## What was found, ranked by what it cost

| # | Finding | Measured cost | Where | Done |
|---|---|---|---|---|
| 1 | The MJPEG tiles' JPEGs were encoded on the CPU (libjpeg) after a BGR conversion of every camera frame, while the SoC's four JPEG encoder cores sat idle | libjpeg 32% of the service's samples on the encoder-heavy graph E, plus 7% NV12 to BGR | `MjpegSink` | Hardware encode through FFmpeg's `mjpeg_rkmpp`: NV12 frames go in from their planes, BGR frames expanded to 32-bit BGR into the encoder's input, CPU path kept as the fallback. Service on E 1.02 to 0.69 cores (-32%) |
| 2 | Every MJPEG frame crossed into .NET as a base64 string, became a UTF-16 string and a decoded array: large-object allocations | 58 MB/s allocated, 13.5 full (gen2) GCs a second, 148 ms of every second paused | `MjpegSink`, `MjpegStreamModule` | Raw bytes copied into a pooled, pinned buffer (`Manager::CopyMjpegFrame`), a sequence number for the stream endpoint. 1.1 MB/s, no gen2 collections, 1 ms/s paused; service on E 0.69 to 0.45 cores (-35%) |
| 3 | The cameras' NV12 frames (full range, from the JPEG decoder) were converted to RGB with video-range formulas (OpenCV, RGA's default) | every colour picture's contrast stretched ~16%: annotated previews, snapshots, WebRTC from BGR, the NPU's input | `Frame::AsBgr`, `MppJpegDecoder::Decode`, `RgaLetterbox` | Squeezed to video range first (the encoders' NEON tables, now `YuvRange`), RGA asked for its full-range mode; +0.35 ms per 1280x720 conversion where one is made. Found by the hardware JPEG encoder's test |
| 4 | GPU shaders are memory-latency bound | issue starvation 89-105% of shader-core cycles, every load/store read partial (100%), 44% of arithmetic instructions diverged | vkapriltag | Upstream; data below |
| 5 | Combining NPU cores for one model is slower than one core | deployed model 15.3 ms on one core, 18.9 ms on two or three | — | Confirms the current one core per detector |
| 6 | Zero-copy NPU input (`rknn_create_mem`) | 15.6 ms against 15.9 ms wall | — | Not worth doing (as found before) |

Together, on graph E (two recordings, four MJPEG tiles at 10 fps, a Vulkan detector): the service went from 1.01-1.02 cores to 0.44-0.46 (-56%), the performance cores from
20-21% busy to 10-11%, and each tile delivered more frames in the same 40 s (470-491 to 518-520), the GC pauses no longer delaying the requests. A tile's JPEG now takes
7.5-8 ms (1280x720) instead of 6 ms, but almost all of that is the hardware working, not the CPU; at a 100 ms tile cadence it does not show.

The full graph B (three Vulkan detectors, two YOLO detectors, two recordings, two MJPEG tiles) now runs at 0.94-0.96 cores of service CPU with the performance cores
22-23% busy (`profiles/round3/B_dev85.jsonl`). On the build of two days earlier (vkapriltag 1.7.0, none of this round's changes) the same graph took 1.55 cores; that
difference combines the two vkapriltag upgrades with the fixes above.

## Measured and found fine

- **Scheduling** (`perf sched latency`, graph B): our threads wait on average 0.01-0.1 ms from wake-up to running, at most about 1 ms, with one 16 ms outlier where a
  detector thread and the MPP decoder were delayed together. No systemic problem; the full Perfetto timeline was not needed to answer the question.
- **Thermal soak** (10 minutes of graph B, with active cooling): the hottest zone levels at 50 C; the A76 cores stay at 2352 MHz, the GPU and NPU at 1000 MHz and the DDR at
  2112 MHz throughout; the NPU's run stays 18.7-18.9 ms. A drift in one detector's `detect` (2.9 to 4.5 and back to 3.0 ms) follows the scene, not the temperature.
- **Codec blocks** on graph E: the JPEG decoder 14% loaded, H.264 encoder core 0 25% (both recordings on one core, the second core idle: headroom, not a bottleneck),
  the RGA idle, DDR controller 13%.
- **Allocation in the native code** stays under 1% of samples; a heap profile was not needed.
- **CPU stalls** (graph B, service): 1.13 instructions per cycle, back-end stalls 45% of cycles, front-end 10%; graph E (before the fixes, libjpeg-heavy) 1.40 instructions per cycle. The back-end stalls are the
  quad fit's memory reads and the OpenCV conversions, consistent with the earlier rounds.

## GPU counters (vkapriltag 1.9.0, Mali-G610 MP4 at 1000 MHz)

| | S1: one detector, decimation 1 | W: three detectors | W1: three, decimation 1 | B: full graph |
|---|---|---|---|---|
| GPU busy | 27% | 25% | 67% | 27% |
| shader cores busy, of GPU-active | 87% | 77% | 91% | 76% |
| issue starvation, of core-active | 89% | 93% | 89% | 106% |
| arithmetic issue | 41% | 34% | 41% | 32% |
| load/store issue | 56% | 48% | 56% | 45% |
| partial reads, of load/store reads | 100% | 100% | 100% | 100% |
| DRAM read + write | 2.9 GB/s | 1.7 GB/s | 7.5 GB/s | 1.7 GB/s |
| diverged, of arithmetic instructions | 45% | 44% | 44% | 44% |
| MMU TLB misses per 1000 L2 reads | 0.4 | 0.4 | 0.4 | 0.3 |

For vkapriltag's maintainer: the passes wait on memory far more than they compute, every load/store read is narrower than a cache line, and almost half the arithmetic
diverges within a warp. Wider (vector) loads and less data-dependent branching in the threshold, labelling and extents passes are where the GPU time is; the TLB
is not a factor. Three detectors at decimation 1 now fit (67% busy; it was saturated before 1.9.0).

## Not possible here

- **NPU per-layer profile**: `RKNN_FLAG_COLLECT_PERF_MASK` makes `rknn_init` crash (segmentation fault inside librknnrt 2.5.0, before any query), reproducibly.
- **JIT frames in `perf`** need `DOTNET_EnableWriteXorExecute=0` as well as `DOTNET_PerfMapEnabled=1` (the runtime executes JIT code from a second mapping); for this round
  `dotnet-counters` answered the question instead.
