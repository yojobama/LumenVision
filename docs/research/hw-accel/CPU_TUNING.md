# Building for the RK3588's cores: -mcpu=cortex-a76.cortex-a55

Question: does compiling for the board's actual CPUs make the service faster than the generic arm64 build?

Answer: no measurable difference. The tuned build's service CPU is within 1% of the generic build's on every workload (inside the run-to-run noise),
and the stage timings move in both directions by about as much as they vary between the left and right detectors. The `LUMEN_TARGET_CPU` CMake option
stays available but is off in release builds.

## What was built

- `LUMEN_TARGET_CPU` (root `CMakeLists.txt`) adds `-mcpu=<value>` to every target built in the tree: LumenCore, vkapriltag, codec-stereo and the tests.
  Prebuilt dependencies keep their own flags: OpenCV, FFmpeg, MPP, librknnrt and `libapriltag.so.3`, which is byte-identical in both packages.
- `cortex-a76.cortex-a55` is GCC's tuning for this big.LITTLE pair. It enables what both cores have (Armv8.2-A plus dot product, fp16 vector
  arithmetic, LSE atomics, RCPC, RDMA, CRC32) and schedules for the A76.
- The release workflow's manual dispatch takes it as the `target_cpu` input. dev86 was built with it; dev85 is the generic build of the same native
  source (only docs and scripts changed between the two).
- The flag took effect. `libLumenCore.so` went from 7 inline LSE atomics plus calls to the out-of-line atomic helpers to 434 inline LSE atomics and 217
  `ldapr` (RCPC) loads. No dot-product instructions were generated: nothing in the hot code is an integer dot product the compiler can find.

## Measurements

`ab_env.py`, alternating installs of the two packages, two rounds, 40 s per run. The Lenovo camera was not connected, so W1, B and E ran on the stereo
camera alone; both builds ran the same graph. Raw data: `profiles/cpu-tune/ab.jsonl`.

| Workload | Generic, cores | Tuned, cores | Change |
|---|---|---|---|
| W1 (two Vulkan detectors, decimation 1) | 1.113 (1.121, 1.105) | 1.102 (1.095, 1.109) | -1.0% |
| B (detectors, YOLO, recording) | 0.764 (0.762, 0.767) | 0.758 (0.755, 0.760) | -0.9% |
| E (encoders) | 0.267 (0.264, 0.270) | 0.269 (0.268, 0.270) | +0.7% |

Stage means (ms) show the same: `vk-quads` 2.25 against 2.22 (W1 left) and 0.62 against 0.59 (B left). `detect` is +3.4% on W1's left detector and -3.7% on
its right, and -6.7% on B's left and +4.8% on its right, which is the GPU queue's noise. NPU `infer`, `stereo decode`, `convert`, `encode` and `jpeg` are
unchanged within 0.05 ms.

## Why it does not help

A 20 s `perf record` of W1 on the tuned build puts 76% of the service's CPU samples in `libLumenCore.so`, which the flag did recompile.
64% of the samples are one function, vkapriltag's `FitQuadForBlob`, with `FitLine` and the quad decode adding 8%. That code is scalar double-precision
arithmetic and sorting. None of the instructions `-mcpu` enables help it, and GCC's default arm64 tuning already schedules well for an out-of-order core
like the A76. The atomics the flag inlines are not on a contended path. Speeding up the quad fit is an algorithm question for vkapriltag (for example
float maths, NEON over the points, or fewer candidate fits), not a compiler-flag one. The remaining CPU time sits in prebuilt libraries (libapriltag's
`quad_decode` 8%, libc `memcpy` 5%) that this flag does not touch.
