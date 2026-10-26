# Sanitisers and profilers on the whole application: what they found

The two earlier rounds of work (`IMPLEMENTATION_STATUS.md`) were measured with the application's own stage timers. This round ran real tools over the application:
AddressSanitizer and UBSan, ThreadSanitizer, and `perf` on the Orange Pi 5 Plus, and followed what they found. Raw data is in `profiles/`.

## How it was run

- **Host (x64, WSL):** `cmake --preset ci-linux-x64 -DLUMEN_SANITIZE=address,undefined` (or `=thread`) instruments every target, fetched dependencies included. The native
  suite (126 test cases) runs clean under both. `tests/lsan.supp` and `tests/tsan.supp` list the prebuilt libraries the sanitisers cannot see into.
- **Board (arm64):** the RKNN, RGA and MPP code only compiles there and the board has no development packages, so `.github/workflows/sanitize-arm64.yml` builds LumenCore with
  ASan+UBSan on an arm runner, runs the suite, and uploads the instrumented library and test binary. The hardware tests (`[hitl]`: V4L2 capture, MPP decode, Vulkan, the
  RKNN int8 and fp16 models) were then run on the board with them. The instrumented library was also swapped into the running service (`LD_PRELOAD` of `libasan`, release
  library backed up and restored afterwards) and the heavy graph (B below), three camera create/delete cycles, a recording with the source and the sink deleted in each order
  and a recording across four camera mode changes were run against it: no sanitiser report, no kernel message, the service never restarted, and the dma-buf objects returned to zero.
- **Profiles:** `perf record -F 499 -g` on the service for 60 s on two graphs. **A**: one camera (1280x720 at 30 fps) with a Vulkan AprilTag detector (ultrafast refine, margin 4)
  and a YOLO11 detector. **B**: A plus the stereo camera at 2560x720 at 60 fps split into halves (two more Vulkan detectors, one more YOLO), two recordings and two MJPEG tiles
  polled at 10 fps. The graph was held steady while the profiler ran.

## Sanitiser findings

| Found by | Where | What | Status |
|---|---|---|---|
| ASan (leak) | `VkApriltagBackend` constructor | The libapriltag detector and tag family (36 MB for the largest) were never released when Vulkan setup threw, e.g. no usable GPU; a destructor does not run for a constructor that threw | fixed, test added |
| TSan | `~Manager` | Destroyed running sinks and sources without stopping them. The base-class destructors only join after the derived members are gone, so a node's thread could call into a half-destroyed object | fixed: it runs `Shutdown()` first |
| TSan | `SystemMonitor` | The stop flag was a plain `bool` shared by two threads | fixed: atomic |
| ASan on the board | tests and `lumen-bench` | The test target took some `LUMEN_WITH_*` defines but not `LUMEN_WITH_RGA`, so `RknnDetectionBackend` was smaller in the test than in the library and a stack-built one overflowed (`stack-buffer-overflow` in the RGA member's constructor). The shipped service builds these objects inside the library and was not affected | fixed: both take every define the library was built with |
| UBSan on the board | `MppJpegDecoder` | MPP's documented "type OR flag bits" value does not fit a C++ enum | check off for that one function |
| (noise) | OpenCV's worker pool, ntcore's message queue, the Vulkan loader, onnxruntime, driver `dlopen` allocations | uninstrumented or process-lifetime, not our code | suppressed, with the reason in the suppression files |

After the fixes the host suite is clean under ASan+UBSan and under TSan, and the board's hardware suite (11 passed, 1 skipped for PhotonVision's own model, which is not installed)
is clean under ASan+UBSan.

## Where the CPU goes

Workload A, shares of the service's CPU: the Vulkan AprilTag library's CPU quad fit about 60%, the YOLO path 17% (the RGA letterbox call itself 8%, about half a millisecond a frame,
the same as the CPU path it replaced), JPEG decode 3%, camera grab 3%. Allocation (`malloc`/`free`) is under 1% in both graphs, so a heap profile would not have found anything.

Workload B had four items worth acting on:

| Item | Share of service CPU | Fix | Measured |
|---|---|---|---|
| `cv::LUT`, the full-range to video-range squeeze in the recorders | 8% | NEON `tbl` lookups of the same table (identical bytes, checked for all 256 values) | luma of 1280x720 0.67 ms to 0.25 ms on an A76 core (3.4x faster on an A55); stereo recorder `convert` 2.0-2.3 ms to 1.35 ms |
| Overlay conversion, copy and drawing for every frame while a tile was watched, although an MJPEG sink encodes at most every 80 ms | about 8-10% | `ISink::WantsFrameNow()`: detectors draw only for a frame a preview will use; WebRTC is also paced to its stream rate | together with the line above: service 2.13-2.15 cores to 1.56-1.58 (-27%), performance cores 48% busy to 35% |
| OpenCV's worker threads spinning on `sched_yield` after every job | 7-8% | `OPENCV_THREAD_POOL_ACTIVE_WAIT_WORKER=0` in the service unit | 2.13 to 1.81 cores on the old code; 1.56 to 1.53 once the parallel `cv::LUT` that kept waking the pool was gone |
| `hypotf` in the Vulkan AprilTag library's quad fit | 14% of graph A | not changed here, see below | |

Frame ages, detect times and the MJPEG tile frame counts are unchanged by the changes above (two runs each, `profiles/heavy-graph-*.jsonl`).

## For the vkapriltag maintainer (not applied: the submodule stays at its pinned commit)

`QuadDecode.cpp` calls `std::hypotf` in the line fit (lines 79, 94 and 118 at v1.7.0), once or twice per candidate window. glibc's `hypotf` costs 7.5 ns a call on an A76 core
and 34.6 ns on an A55; `sqrtf(x * x + y * y)` costs 0.9 ns and 5.3 ns, and a `double` square root rounded to `float` 3.1 ns and 10.2 ns. For the magnitudes the fit sees
(moment differences up to a few 1e9) the worst relative error against a `double` hypot is 1.1e-7 for the float form and 5.9e-8 for the double form (under half an ulp).
It is 14% of the service's samples on graph A, so replacing the three calls would take about a tenth off the detector's CPU. `profiles/hypot_bench.cpp` reproduces the numbers.
**Update:** vkapriltag 1.8.0 evaluates the line-fit hypotenuse in double and rounds once (the form below), among other CPU-tail work; the measured effect of the upgrade is in `IMPLEMENTATION_STATUS.md`.
The proposal as made:

```diff
-  const float hypot_cached = std::hypotf(static_cast<float>(Cxx - Cyy), static_cast<float>(2 * Cxy));
+  const float hx = static_cast<float>(Cxx - Cyy), hy = static_cast<float>(2 * Cxy);
+  const float hypot_cached = static_cast<float>(std::sqrt(static_cast<double>(hx) * hx + static_cast<double>(hy) * hy));
```

(the same form at the other two sites). The rest of the quad fit (`ReadMomentsWindow` 14%, the lambda itself 31% of graph A's samples) is the library's own algorithm, and the
blob-filter options already exposed on the Vulkan sink (`minWhiteBlackDiff`, `minClusterPixels`, `minTagPixels`) are the lever for scenes with a lot of clutter.

## Not done, and why

- The kernel `perf_event_paranoid` and `kptr_restrict` settings were left alone (`sudo perf` does not need them changed).
- ThreadSanitizer was only run on the host suite, not on the board (the board's service has two real cameras and the Mali driver, none of which TSan can see into usefully).
- The single `rkvenc` "fd failed" kernel message seen once at the end of an earlier two-camera test run (an encode task submitted with a buffer descriptor that had just been
  closed, during a teardown with recorders bound) did not recur in the ASan run, a re-run of the whole test, or deliberate source/sink/mode-change orders. It is unexplained.
- GPU time (`vk-gpu:submit+wait`) is not visible to `perf`; the earlier measurements stand: with three Vulkan detectors the Mali GPU is the saturating resource, not the CPU.
