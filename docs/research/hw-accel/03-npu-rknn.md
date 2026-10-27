# 03 - NPU / RKNN / object detection offload (RK3588)

Research agent notes. Area: NPU and object detection in LumenVision (LumenCore RKNN + ONNX backends).

## Status

- [x] Read context docs (README, ROADMAP C1/C3, PERFORMANCE_ANALYSIS, IMPLEMENTATION_PLAN phase 4)
- [x] Read detection code (RknnDetectionBackend, OnnxDetectionBackend, YoloPostProcess, IDetectionBackend, ObjectDetectionSink, Frame, MppJpegDecoder)
- [x] Web research (rknpu2 zero-copy, core masks, quantisation, model zoo export, benchmarks, alternatives)
- [x] Ranked recommendations (end of file)
- Complete. Unverified items are marked "est." / "unverified"; nothing was run on a Pi.

## Baseline: what the code does today (verified in code)

RknnDetectionBackend::Infer (LumenCore/RknnDetectionBackend.cpp:117-196), per frame:
1. `ObjectDetectionSink::Process` calls `result.frame->AsBgr()` (ObjectDetectionSink.cpp:38). With the MPP JPEG path the decoder
   already did NV12 -> BGR on the CPU (`cv::cvtColor(nv12View, dst, COLOR_YUV2BGR_NV12)`, MppJpegDecoder.cpp:183), so BGR is free here.
2. `YoloPostProcess::Letterbox` on CPU: `cv::resize` + `cv::copyMakeBorder` (YoloPostProcess.cpp:9-34), new allocations each frame.
3. `cv::cvtColor(BGR2RGB)` on CPU into a fresh Mat (RknnDetectionBackend.cpp:134-135).
4. `rknn_inputs_set` with `pass_through=0`, uint8 NHWC (lines 138-146): the runtime copies + converts the buffer into the NPU's
   native layout on the CPU every frame (non-zero-copy API).
5. `rknn_run(ctx, nullptr)` synchronous, blocks the sink thread (line 148). No `rknn_set_core_mask` call at all (default
   RKNN_NPU_CORE_AUTO), one context per sink.
6. `rknn_outputs_get` with `want_float=1` (lines 151-157): runtime dequantises every int8 output element to float32 on the CPU
   and allocates/copies.
7. Decode on CPU in float: DFL softmax per side for every cell above threshold; class max over every cell
   (YoloPostProcess.cpp:135-214); a full pre-pass over all class scores to sniff probabilities vs logits (lines 150-155);
   the third output of each scale (score_sum, intended for early-out) is ignored (RknnDetectionBackend.cpp:73-74).
8. `cv::dnn::NMSBoxesBatched`.

Measured (docs/PERFORMANCE_ANALYSIS.md:140-172, Orange Pi 5 Plus, 720p MJPEG, three sinks running):
- RKNN YOLO11 sink work 18.2 ms = NPU run 16.1, letterbox 0.7, output fetch 0.8, convert 0.2, decode+NMS 0.1. Age 34 ms.
- Isolated bus.jpg ms/frame: yolov8n fused fp16 45, DFL fp16 44, DFL int8 20; yolo11n fused fp16 51, DFL fp16 50, DFL int8 26.
- Core mask (three cores) tried: no gain for YOLO11 (25.6 vs 25.2 ms), 20% for yolov8n; "not adopted".
- NPU devfreq governor already pinned to `performance` by udev rule (scripts/install-deps.sh:811, build-deb.sh:77).
- Driver `RKNPU v0.9.7`, runtime `librknnrt 2.3.2` (IMPLEMENTATION_PLAN.md:655-662).

=> Pre/post on the CPU is already only ~2 ms of 18; the NPU run itself (16 ms) dominates. The big lever is the model/graph
and how the NPU is used, not the CPU glue. (The 16.1 ms "rknn-run" includes nothing else: inputs_set is lapped separately,
but the lap list in the profile table does not show `rknn-set-input`; open question.)

ONNX path (OnnxDetectionBackend.cpp): CPU EP only, `SetIntraOpNumThreads(1)` (line 26) - single-threaded on purpose or not,
so yolov8n would be ~150-300 ms/frame on one A76 (unverified estimate). Float32 preprocessing via cvtColor+convertTo+split.

No converter script is in the repo (README.md:191-193, ROADMAP C1 "still missing"); the HITL test refers to models produced by
out-of-repo scripts (tests/test_rknn_detection_hitl.cpp:47-53). lumen-bench skips RKNN (tools/lumen-bench/main.cpp:186-187).

## Findings

### F1. The NPU run is already at Rockchip's reference speed; the model choice is the big single-frame lever
- Claim: LumenVision's live 16.1 ms `rknn-run` for YOLO11 matches rknn_model_zoo's published single-core inference figure for
  yolo11n int8 640 (64.0 fps = 15.6 ms). There is no hidden runtime inefficiency to remove inside `rknn_run`; single-frame latency
  falls only by changing the model (family, size, input resolution) or splitting it across cores.
- Evidence: rknn_model_zoo README benchmark, column "RK3588 @single_core", inference only, max NPU frequency
  (https://github.com/airockchip/rknn_model_zoo): yolov8n 76.3 fps (13.1 ms), yolo11n 64.0 (15.6 ms), yolov10n 63.9,
  yolov6n 108.8 (9.2 ms), yolov5n 84.6, yolov7-tiny 73.6, yolov8s 38.6 (25.9 ms), yolo11s 34.6 (28.9 ms), yolox_s 37.6,
  ppyoloe_s 39.7, yolov8m 16.3, yolo_world_v2s 20.6. All 640x640 INT8. RT-DETR has no RK3588 row.
  Repo: docs/PERFORMANCE_ANALYSIS.md:154 (NPU run 16.1 ms).
- Confidence: high (published table; mapping of the scraped column to RK3588 checked by the "/" pattern).
- Gain: yolo11n -> yolov8n saves ~2.5 ms (16%); -> yolov6n saves ~6.4 ms (40%) at some accuracy cost (yolov6n COCO mAP 37.5
  vs yolov8n 37.3 vs yolo11n 39.5 per their upstream READMEs - unverified here). yolo11's C2PSA attention block is the usual
  reason it is slower on NPU than v8 despite similar FLOPs (unverified attribution).
- Effort: zero code (all three are already-decoded DFL-or-not heads; yolov6 has a different head, see F-later).
- Test: the existing `LUMEN_RKNN_MODEL_DIR` HITL test + per-stage profile with each model.

### F2. RKNN runtime API surface the backend does not use (from rknn_api.h, rknn-toolkit2 master)
Source: https://raw.githubusercontent.com/airockchip/rknn-toolkit2/master/rknpu2/runtime/Linux/librknn_api/include/rknn_api.h
- Core masks: `RKNN_NPU_CORE_AUTO=0` ("run on NPU core randomly"), `_0=1`, `_1=2`, `_2=4`, `_0_1`, `_0_1_2`, `_ALL=0xffff`.
  The backend never calls `rknn_set_core_mask` (RknnDetectionBackend.cpp: no call) -> every sink is AUTO.
- `rknn_dup_context(&in, &out)`: second context for the same model sharing weights (for one-context-per-core pools).
  `RKNN_FLAG_SHARE_WEIGHT_MEM` (0x20) same purpose at init.
- Zero-copy I/O: `rknn_create_mem`, `rknn_create_mem_from_fd(ctx, fd, virt, size, offset)` (wrap an external DMA-BUF, e.g. an
  MPP or RGA output buffer), `rknn_set_io_mem`, `rknn_mem_sync`. Queries `RKNN_QUERY_NATIVE_INPUT_ATTR` (NC1HWC2 layout) and
  `RKNN_QUERY_NATIVE_NHWC_OUTPUT_ATTR`.
- Async: `rknn_run_extend.non_block` + `rknn_wait`; `RKNN_FLAG_ASYNC_MASK` (returns the previous frame's result; "in
  multi-threaded mode you do not need to turn this mode on"). Fences: `RKNN_FLAG_FENCE_IN_OUTSIDE/OUT_OUTSIDE`.
- `RKNN_FLAG_DISABLE_PROC_HIGH_PRIORITY` (0x2000): "default nice -19, this flag can disable default priority". This is the
  root cause of the nice -19 inheritance documented in PERFORMANCE_ANALYSIS.md:159-165 (now worked around with `Nice=-19`).
  Worth knowing: the runtime renices the *calling process/thread*; with the flag set the service can control priorities itself.
- `RKNN_FLAG_ENABLE_SRAM` (0x800) / `SHARE_SRAM`: use the RK3588 NPU's on-chip SRAM for internal buffers (needs kernel driver +
  device-tree SRAM node; unverified on the ubuntu-rockchip 0.9.7 driver). Could cut DDR traffic for internal tensors.
- `RKNN_FLAG_PRIOR_HIGH/MEDIUM/LOW`: per-context NPU scheduling priority, useful when a tag-assist model and a game-piece model
  share a core.
- `rknn_set_batch_core_num`: for batch>1 models, spread batch items across cores.
- Confidence: high (header text). Gains quantified in later findings.

### F3. Post-processing in the int8 domain (rknn_model_zoo recipe) - small gain, ~0.7-0.9 ms/frame
- Claim: the model zoo's C post-process never dequantises the whole output: it reads raw int8 tensors (`want_float=0`), converts
  the float threshold once per branch (`qnt_f32_to_affine`), skips a cell when `score_sum_tensor[offset] < score_sum_thres_i8`
  before scanning 80 classes, and dequantises only the surviving cells' DFL bins and best score.
- Evidence: https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yolov8/cpp/postprocess.cc (`process_i8`,
  `compute_dfl`, `qnt_f32_to_affine`). LumenVision instead sets `want_float=1` (RknnDetectionBackend.cpp:154) - the runtime
  dequantises all ~8400 x (64+80+1) = 1.22 M elements to float (4.9 MB written) - then does a full extra pass over all class
  scores to sniff probability-vs-logit (YoloPostProcess.cpp:150-155) and ignores score_sum.
- Measured cost today: "output fetch 0.8 ms" + "decode+NMS 0.1 ms" (PERFORMANCE_ANALYSIS.md:154). So the ceiling is ~0.9 ms
  (~5% of the 18.2 ms sink work), plus reduced DDR traffic and A76 time that AprilTag's CPU tail could use.
- Also: the probability-vs-logit sniff can be decided once at Load (it's a property of the export), not per frame.
- Confidence: high on mechanism, medium on the exact saving (dequant of 1.2 M int8 with NEON is ~0.3-0.6 ms on an A76; estimate).
- Going further: bind output buffers too (`rknn_create_mem` per output + `rknn_set_io_mem` with the output attr's type left as
  INT8 and fmt NCHW), so `rknn_outputs_get`'s copy disappears and the decoder reads `mem->virt_addr` directly (sync
  FROM_DEVICE if cacheable). Rockchip's demo does the same with float outputs (rknn_create_mem_with_rga_demo.cpp, F6).
- Effort: low-medium (a `DecodeDflMultiScaleI8` overload in YoloPostProcess with int8 + scale/zp per output; unit-testable on x64
  by quantising the existing float fixtures). Risk: low; keep the float path for fp16 models.
- Test: compare detections int8-path vs float-path on bus.jpg and a game-piece clip; profile `rknn-get-outputs` + decode.

### F4. Quantisation accuracy: int8 with a tiny calibration set costs several mAP points
- Claim: PhotonVision's own RK3588 int8 benchmark (COCO val 5000 images, calibrated with 20 random val images) reports mAP50-95
  YOLOv5 0.224, YOLOv5u 0.275, YOLOv8 0.305, YOLO11 0.325 and total times 15.0 / 20.3 / 17.3 / 23.0 ms. Ultralytics fp32 COCO
  mAP for v8n is 0.373 and 11n 0.395, so the int8 conversion as PhotonVision does it loses ~7 points (if these are the n
  models at 640, which the page does not state - unverified).
- Evidence: https://docs.photonvision.org/en/latest/docs/benchmarks/rknn-model-benchmarks.html ; PhotonVision OPi docs say only
  640x640 quantised YOLOv5/v8/v11 are supported (https://docs.photonvision.org/en/latest/docs/objectDetection/opi.html).
- Relevance: LumenVision's int8-fused refusal (RknnDetectionBackend.cpp:68-71) and HITL thresholds (0.6 for PV's model,
  tests/test_rknn_detection_hitl.cpp:19-20) already reflect int8 noise. A documented converter with a proper calibration set
  (100-500 *in-domain* FRC images) and optional hybrid quantisation is where accuracy is won back (see F-hybrid).
- Confidence: medium (numbers high; the n/640 inference is mine).
- Measured since (NPU_TOOLKIT_2_5.md): YOLO11n converted with Rockchip's split head and this repo's script loses only 0.6 mAP to int8
  (38.76 against 39.35 fp32, COCO, 4500 held-out images, on the board), and 20 calibration frames do as well as 500. Hybrid quantisation
  and the other quantisation algorithms do not win any of that back. PhotonVision's loss comes from its conversion, not from int8 itself.

### F5. Correction to ROADMAP C1 / README: rknpu2 on RK3588 has no NV12 input format
- Claim: ROADMAP.md:245 and README.md:58/340 say "RKNN takes NV12 input natively". The RK3588 runtime API has no YUV/NV12
  tensor format: `rknn_tensor_format` is only NCHW / NHWC / NC1HWC2 / UNDEFINED, and the header never mentions yuv/nv12. The
  only conversion the runtime does is type/layout (uint8 NHWC -> native) in `rknn_inputs_set` with pass_through=0.
- Evidence: rknn_api.h (rknn-toolkit2 master, URL in F2) - no yuv/nv12 lines; `pass_through` comment: TRUE passes the buffer
  "directly to the input node", FALSE "converted using the listed type and fmt". Rockchip's own video demos do MPP (NV12) ->
  RGA (YUV420SP->RGB888 + resize) -> RKNN (https://gitcode.csdn.net/6a27d8de10ee7a33f279bc9a.html, secondary source).
  (YUV input existed on RKNPU1 / RV1106-class parts; unverified, not relevant to RK3588.)
- Consequence: the "NV12 straight to the NPU" item really means "NV12 DMA-BUF -> RGA (CSC + scale + letterbox) -> an RKNN input
  buffer created with `rknn_create_mem`/`rknn_create_mem_from_fd` -> `rknn_set_io_mem`". A riskier alternative is to bake
  YUV->RGB into the graph (two inputs Y [H,W,1] and UV [H/2,W/2,2], a Resize on UV and a 1x1 conv with bias - YUV->RGB is
  affine, so it is exactly one conv), which the NPU can execute; but int8 quantisation of that first layer and the
  letterbox geometry make it a research item, not a recommendation.
- Confidence: high (header). Effort to fix docs: trivial.

### F6. Zero-copy input via RGA -> rknn_create_mem (saves ~1-1.5 ms CPU and latency per frame)
- Claim: today the CPU does resize+pad (0.7 ms), BGR->RGB (0.2 ms) and `rknn_inputs_set` does another copy/convert +
  cache flush of 1.2 MB (not shown in the profile table; estimate 0.2-0.5 ms). With `rknn_query(RKNN_QUERY_NATIVE_INPUT_ATTR)`
  + `rknn_create_mem(ctx, size_with_stride)` + `rknn_set_io_mem` once at Load, and RGA (`improcess`/`imresize` + `imfill` for
  the 114-grey border, `imcvtcolor` BGR->RGB or NV12->RGB) writing straight into `mem->fd` each frame, the CPU never touches
  the input pixels. For a 3-channel uint8 NHWC input the native layout on RK3588 is NHWC with width stride alignment
  (640 is already aligned), so no NC1HWC2 conversion is needed (medium confidence, per rknpu2 zero-copy demo patterns).
- Evidence: API in F2; costs from PERFORMANCE_ANALYSIS.md:154. RGA already linked in the build (LumenCore/RgaColorConverter.*,
  used only by WebRTCSink). Rockchip's own reference for exactly this flow:
  https://raw.githubusercontent.com/airockchip/rknn-toolkit2/master/rknpu2/examples/rknn_api_demo/src/rknn_create_mem_with_rga_demo.cpp
  - sets `input_attrs[0].type = RKNN_TENSOR_UINT8`, `fmt = RKNN_TENSOR_NHWC` (comment: "npu only support NHWC in zero copy
  mode"; uint8 lets the NPU fuse normalisation+quantisation), `rknn_create_mem(ctx, size_with_stride)`, wraps it for RGA with
  `wrapbuffer_fd_t(mem->fd, model_w, model_h, wstride /*align 8*/, hstride, RK_FORMAT_RGB_888)`, `imresize(src, dst)`, then
  `rknn_set_io_mem` for input and outputs, then `rknn_run`. (The demo's source is a virtual-address RGB image; with an MPP
  NV12 DMA-BUF it would be `wrapbuffer_fd(..., RK_FORMAT_YCbCr_420_SP)` and RGA does CSC+scale in the same pass.)
  Letterbox = resize into a sub-rect of the dst (`improcess` with dst rect / `imresize` on a `dst_rect`) + `imfill` of the
  border once (the border never changes between frames if the buffer is reused - fill it once at Load).
- Gain: ~1.2-1.5 ms of an 18.2 ms work time (~7%), more on 1080p sources where `cv::resize` from 1920x1080 costs more (estimate
  1.5-2.5 ms). Larger if the source NV12 DMA-BUF from MPP can feed RGA directly (skips the decoder's CPU NV12->BGR, which is
  inside the 3.9 ms "hardware JPEG decode"), but that is only free if no other node needs BGR - coordinate with the
  MPP/RGA agents.
- Risks: RGA2 core can only address <4 GB (use dma32 heap or let librga pick RGA3); RGA scaling filter differs from
  cv::resize INTER_LINEAR - negligible for detection; must `rknn_mem_sync(TO_DEVICE)` only if buffer is cacheable and the CPU
  wrote it (RGA output is device-coherent if non-cacheable).
- Effort: medium (1-2 days incl. fallback to the current CPU path). Test: HITL bus.jpg equality (boxes within 2 px) and the
  stage profile on the Pi.

### F6b. CPU-only variant of F6 (no RGA dependency) - most of the gain for ~50 lines
- Allocate the input once with `rknn_create_mem(ctx, size_with_stride)` (type UINT8, fmt NHWC, `rknn_set_io_mem`), wrap
  `mem->virt_addr` as a cv::Mat (step = w_stride*3), fill the 114-grey border once at Load (the letterbox geometry is fixed for
  a given source size; refill only when the source size changes), and per frame do `cv::resize(bgr, roiOfMem)` +
  `cv::cvtColor(roi, roi, BGR2RGB)` in place (or skip the swap, below), then `rknn_mem_sync(TO_DEVICE)` if cacheable.
- Removes: `copyMakeBorder` (a full 1.2 MB copy), the fresh-Mat allocations (resized, padded, rgb), and the runtime's
  inputs_set copy/convert. Estimated 0.5-1 ms of the ~1.2-1.5 ms.
- BGR->RGB can be removed entirely by permuting the first Conv's input-channel weights in the ONNX before conversion (exact; the
  calibration images must then be fed as BGR). rknn-toolkit2 has no `reorder_channel` (toolkit1 had); unverified whether a
  config flag replaces it - graph surgery is safe regardless. Saves ~0.2 ms.
- Confidence: medium-high. Effort: low. Risk: low (same pixels). Test: HITL bus.jpg identical detections.

### F7. Match the model's input aspect to the camera: 640x384 instead of 640x640 (~35-40% less NPU time, no accuracy loss)
- Claim: a 1280x720 (16:9) frame letterboxed into 640x640 puts 640x360 of image and 280 rows (44%) of grey padding through the
  NPU. Exporting the same weights with a rectangular static input (ultralytics `export(format="onnx", imgsz=(384, 640))`,
  i.e. HxW, both multiples of 32) and converting that ONNX gives a model that sees exactly the same pixels at the same scale
  with 40% fewer of them. YOLO is fully convolutional, and ultralytics' own `rect=True` validation does exactly this, so
  accuracy is unchanged (a 4:3 camera -> 640x480, 25% less).
- Evidence (code): the backend already takes H and W separately from the model (RknnDetectionBackend.cpp:55-61), Letterbox
  handles non-square targets (YoloPostProcess.cpp:9-34), the DFL decoder derives stride from width and uses gridH/gridW per
  scale (RknnDetectionBackend.cpp:88-92) - so no code change is needed beyond the converter and the UI's single
  `inputSize` field (Manager.cpp:1395-1396 sets W=H=inputSize, then the model's own dims override it for RKNN; ONNX also
  reads dims from the model, OnnxDetectionBackend.cpp:55-61).
- Gain estimate (unverified, needs a Pi run): NPU conv work scales with pixels; with ~1-2 ms of per-layer fixed overhead,
  yolo11n 16.1 ms -> ~10-11 ms; yolov8n 13.1 -> ~8.5 ms. Also shrinks output decode by 40%.
- Further resolution trade-off for FRC game pieces (large, high-contrast objects): 512x288 or 416x256 (`imgsz=(256,416)`) would
  roughly halve again (estimate 4-6 ms) at the cost of small/distant objects; worth offering as a model-zoo choice but needs a
  per-season recall test. No published RK3588 320-vs-640 timing table found (searched; Radxa forum only anecdotal:
  https://forum.radxa.com/t/use-yolov8-in-rk3588-npu/15838?page=7).
- Confidence: high on mechanism/accuracy, medium on the exact ms.
- Effort: low (converter script option + docs + UI showing WxH). Test: convert yolov8n at 640x640 and 384x640, run HITL
  bus.jpg (which is 810x1080 portrait -> would need 640x480 portrait; use a 16:9 game-piece clip) and compare `rknn-run`.

### F8. Runtime/toolkit version: 2.3.2 -> 2.5.0 is out (release notes mention multi-output CPU load and dma-buf fixes)
- Claim: rknn-toolkit2 v2.5.0 (released 20 Oct, presumably 2026; latest) lists "Optimize CPU load for full runtime for
  multi-input/output models" (the DFL export has 9 outputs), "Fix runtime issues: dma-buf/fd and error-path memory leaks"
  (relevant once F6 uses `rknn_create_mem_from_fd`), and graph-optimisation/fusion improvements. Releases: v2.3.2 (09 Apr),
  v2.3.0, v2.2.0, v2.1.0 (Flash Attention only RK3562/RK3576), v2.0.0-beta0 (SDPA for transformers).
- Evidence: https://github.com/airockchip/rknn-toolkit2/releases/tag/v2.5.0 ; https://github.com/airockchip/rknn-toolkit2/releases
- Caveat: install-deps.sh fetches `RKNN_TOOLKIT2_REF=master` (scripts/install-deps.sh:682), so the runtime version on a
  fresh install drifts with upstream; models must be converted with a toolkit no newer than the runtime. Pin the ref and record
  the toolkit version used in the model manifest (`rknn_query(RKNN_QUERY_SDK_VERSION)` gives api+drv versions at Load - log it).
- Gain: unknown (no figures in notes); likely <1 ms on output fetch. Confidence: medium. Effort: low (re-convert + bump).
- Test: same model converted with 2.3.2 and 2.5.0, compare `rknn-run` and `rknn-get-outputs` laps.

### F9. Three NPU cores: use them for isolation and throughput, not for single-model latency of a nano model
- Claim: (a) `RKNN_NPU_CORE_0_1_2` splits one model's layers across cores; for nano models the per-layer sync overhead eats
  most of the gain - the repo's own measurement: yolo11n 25.6 vs 25.2 ms (none), yolov8n ~20% (PERFORMANCE_ANALYSIS.md:171).
  (b) Separate contexts pinned to separate cores scale throughput almost linearly: leafqycc/rknn-multi-threaded (yolov5s,
  Python) ~27 fps with 1 thread, ~49 with 2, ~65 with 3, little beyond 3 (https://github.com/leafqycc/rknn-multi-threaded);
  rknn_model_zoo numbers are all `@single_core`, i.e. 2 of the 6 TOPS. (c) `RKNN_NPU_CORE_AUTO` "run on NPU core randomly"
  (rknn_api.h) - with two detector sinks (two cameras, or tag-assist + game-piece) nothing guarantees they land on different
  cores, so they can serialise.
- What this means for LumenVision:
  1. Per-sink core mask setting (`rknn_set_core_mask` after `rknn_init`), default: assign each new RKNN sink the least-used
     single core (0, 1, 2 round-robin) so up to three detectors never contend; allow `0_1_2` for a lone YOLOv8-family sink
     where it measured +20% (~13 -> ~10.5 ms; unverified for v8n at 640x384).
  2. Throughput mode for cameras faster than 1/latency (e.g. OV9281 global-shutter at 100-120 fps, or two 60 fps cameras):
     a pool of 2-3 contexts (`rknn_dup_context` or `RKNN_FLAG_SHARE_WEIGHT_MEM` to share weights) each pinned to a core,
     frames dispatched round-robin, results re-ordered by `captureTimeUs`. Per-frame latency unchanged (~16 ms), throughput
     ~2.5-3x (60 -> ~150-180 fps for yolo11n at 640; estimate from (b)). Needs the sink to have >1 frame in flight (today each
     sink processes one frame at a time on its own thread, newest-frame semantics; ObjectDetectionSink.cpp:31-110 is a
     straight synchronous loop; ISink(maxSources=1), ISink.h:21).
  3. For a single 30-60 fps camera, throughput mode buys nothing: the NPU already has 16 ms per 33 ms frame. Don't build it
     until there's a >60 fps use case.
- Confidence: high (a, c), medium (b numbers are yolov5s Python). Effort: (1) low, (2) medium-high.
- Test: two RKNN sinks on two cameras with AUTO vs pinned; watch per-core load in /sys/kernel/debug/rknpu/load (already
  surfaced by Server/AcceleratorMonitor.cs:56) and each sink's `rknn-run` lap.

### F10. Pipelining CPU pre/post with NPU inference inside one sink: small, mostly superseded by F6/F3
- Claim: with pre (letterbox+convert+set-input ~1-1.5 ms) and post (fetch+decode ~0.9 ms) on the CPU and 16 ms on the NPU,
  overlapping frame N+1's preprocessing with frame N's inference saves at most ~2 ms of *throughput* and nothing on *latency*;
  after F6 (RGA writes the input) and F3 (int8 decode), the CPU part is <0.5 ms and the overlap is worth nothing.
  `RKNN_FLAG_ASYNC_MASK` returns the *previous* frame's output - adds a full frame of latency; wrong trade for FRC.
- Confidence: high. Recommendation: do not implement.

### F11. Power / thermal: no published RK3588 NPU detection wattage; measure it
- Searched; found only estimates ("NPU active: +2-3 W", tinycomputers.io, explicitly not measured:
  https://tinycomputers.io/posts/rockchip-rk3588-npu-benchmarks.md) and an OPi 5 Max review saying the NPU holds 100% at its
  1 GHz max under sustained inference (https://tinycomputers.io/posts/rk3588-orange-pi-5-max-review.md). Unverified.
- Repo context: the NPU devfreq governor is forced to `performance` (scripts/install-deps.sh:811), so the NPU never down-clocks
  except by thermal cooling devices. Running 3 cores flat out (F9 throughput mode) roughly triples NPU power; on a passively
  cooled OPi 5 in a robot this could push the SoC towards the ~80-85 C throttle point and slow the *CPU* AprilTag path too.
- Energy angle: a smaller input (F7) and int8 cut NPU energy per frame proportionally; a frame-rate cap for the detector
  (fps limit already exists per node, NATIVE_API_PLAN.md:69) is the cheapest thermal control.
- Test: 10-min run, log `/sys/class/thermal/thermal_zone*/{type,temp}` (npu_thermal, soc, bigcore*), 
  `/sys/class/devfreq/fdab0000.npu/cur_freq`, and USB-C inline power meter, for: idle, 1 sink yolo11n 30 fps, 1 sink 640x384,
  3-context throughput mode. Confidence: low (no data).

### F12. Model family choice on RK3588 (accuracy vs NPU ms), and what NOT to use
- rknn_model_zoo single-core int8 640 (F1): yolov6n 9.2 ms, yolov8n 13.1, yolov5n 11.8, yolov10n 15.6, yolo11n 15.6,
  yolov8s 25.9, yolo11s 28.9, yolox_s 26.6, ppyoloe_s 25.2.
- Ultralytics' own RKNN export path (`format='rknn'`, keeps the post-processing in-graph, default calibration coco8 = 8
  images) measured on Rock 5B: YOLO26n INT8 41.2 ms, FP16 58.5 ms; YOLO26s INT8 61.9 ms (inference only)
  (https://docs.ultralytics.com/integrations/rockchip-rknn/). That is ~2.6x slower than the model-zoo-recipe yolo11n (15.6 ms)
  - the in-graph decode and a single fused output are the usual cause (Rockchip: post-processing in the model is
  "unfriendly for quantization" and DFL "slowdown the inference speed on NPU",
  https://github.com/airockchip/ultralytics_yolov8/blob/main/RKOPT_README.md). Also, per Ultralytics staff, "RKNN doesn't
  support the NMS-free head", so YOLO26/YOLOv10 end-to-end still need CPU NMS on RKNN
  (https://community.ultralytics.com/t/yolo26n-rknn-inference-speed-on-rk3588-orange-pi-5-plus-segmentation-alternatives/2131).
- Implication for LumenVision: tell users to convert with the rknn_model_zoo recipe (9-output DFL-split, int8) rather than
  Ultralytics' one-click `format=rknn`; the one-click output is the fused single-head layout, which the backend accepts only
  as fp16 (RknnDetectionBackend.cpp:68-71) and which the repo measured at 45-51 ms (PERFORMANCE_ANALYSIS.md:140).
- YOLO26 support: its head is DFL-free (reg_max=1), so neither of the backend's layouts match an int8 split export; adding a
  "box[4,H,W] + cls[nc,H,W]" per-scale layout to the decoder is small, but there's no evidence YOLO26n is faster than v8n on
  this NPU, so it's low priority until someone measures it with a model-zoo-style split export.
- Confidence: high for numbers; medium for "fused head is the cause" (consistent with the repo's own 45 vs 20 ms v8n
  measurement fused-fp16 vs DFL-int8, which mixes precision and layout).

### F13. Other tasks the NPU can take (beyond axis-aligned boxes), with RK3588 single-core int8 640 numbers
- Column order re-verified from the raw README (https://raw.githubusercontent.com/airockchip/rknn_model_zoo/main/README.md):
  RK3566/68, RK3562, **RK3588 (single core)**, RK3576, RV1126B, ... so F1's figures stand. Model zoo version 2.5.0, requires
  RKNPU2 SDK >= 2.5.0.
- yolov8n-obb 77.1 fps (13.0 ms) - oriented boxes: game pieces whose yaw matters (e.g. elongated pieces) get an orientation
  for free at the same cost as plain v8n.
- yolov8n-seg 63.2 fps (15.8 ms) - masks: a ground-contact point / footprint for ground-plane ranging that is better than a
  box's bottom edge. Mask post-process (32 protos x 160x160 matmul) adds CPU time (RKNN matmul API could do it; unverified).
- yolov8n-pose 58.3 fps (17.2 ms) - keypoints: e.g. robot bumper corners for opponent localisation (ROADMAP's later
  "avoid other robots").
- Frigate's RK3588 table (3 NPU cores): yolox_nano int8 14 ms, yolox_tiny 18 ms, yolonas_s fp16 25 ms, yolov9-t fp16 35 ms
  (https://docs.frigate.video/configuration/object_detectors/) - none beat the model-zoo yolov8n recipe.
- No depth or super-resolution models in the model zoo; mobilesam encoder tiny 448 fp16 is 10.6 fps; clip image 6.3 fps -
  transformer-heavy models are an order of magnitude slower on this NPU. Monocular depth (Depth-Anything-S class) is therefore
  not real-time here (inference from the clip/SAM figures; no direct benchmark found). Rejected for the per-frame path.
- Effort per new task type: medium (decoder + UI); OBB is the cheapest addition (same DFL head + one angle channel).
- Confidence: high on numbers; usefulness for FRC is judgement.

### F14. The missing converter is the biggest accuracy lever: in-domain calibration, rect input, quantisation options
- Today there is no converter in the repo (README.md:191-193; ROADMAP C1 "Still missing"); users must produce .rknn
  themselves, so speed/accuracy depend on whatever recipe they find (PhotonVision's 20-image calibration, Ultralytics'
  `format=rknn` with coco8 = 8 images by default, or model zoo's `coco_subset_20.txt`).
- Model zoo's convert.py is minimal: `rknn.config(mean_values=[[0,0,0]], std_values=[[255,255,255]], target_platform=...)`,
  `rknn.build(do_quantization=True, dataset='.../coco_subset_20.txt')`
  (https://raw.githubusercontent.com/airockchip/rknn_model_zoo/main/examples/yolov8/python/convert.py).
- Toolkit knobs that exist (seen in real conversion scripts, e.g.
  https://huggingface.co/happyme531/MeloTTS-RKNN2/blob/main/convert_rknn.py: `quantized_dtype='w8a8'`,
  `quantized_algorithm='normal'`, `quantized_method='channel'`, `quantized_hybrid_level=0`, `optimization_level=3`; an
  `quantize_algorithm_mmse` example ships with the toolkit,
  https://huggingface.co/csukuangfj/rknn-toolkit2-v2.1.0-2024-08-08/tree/main/rknn-toolkit2/examples/functions/quantize_algorithm_mmse).
  Plus `rknn.accuracy_analysis(...)` (per-layer cosine similarity vs float) and hybrid quantisation (`hybrid_quantization_step1/2`:
  keep chosen layers, e.g. the last convs of the box branch, in fp16). Exact semantics unverified from the PDF guide.
- Recommendation: `scripts/convert-yolo-rknn.py` (x86 host, Docker image with rknn-toolkit2 pinned to the runtime version):
  input .pt or .onnx, `--imgsz HxW` (F7), `--calib <dir of 100-300 frames from the team's own camera/field>`,
  `--dtype i8|fp`, `--algo normal|mmse`, applies the model-zoo head split (airockchip/ultralytics_yolov8 export, or equivalent
  ONNX graph surgery cutting after the two per-scale convs and adding ReduceSum+Clip score_sum), optional first-conv channel
  swap (F6b), writes the toolkit version + layout + imgsz into the model manifest, and runs `accuracy_analysis` on a few
  frames, failing loudly if output cosine < ~0.99 (threshold to tune).
- Gain: accuracy, not ms - recovering a few mAP points lets the user raise the confidence threshold (fewer false positives) or
  drop to a smaller/faster input. Confidence: medium (accuracy recovery size unmeasured). Effort: medium (2-3 days + Docker).

### F15. ONNX Runtime CPU path on the Pi: irrelevant for performance, but it is single-threaded
- `options.SetIntraOpNumThreads(1)` (OnnxDetectionBackend.cpp:26): ORT uses one core. Radxa's RK3588 example with a custom
  YOLO11n: PyTorch CPU FP32 268.8 ms, **ONNX CPU FP32 112.08 ms**, NPU FP16 (Ultralytics one-click export) 64.3 ms, NPU INT8
  (model-zoo style, post-process + DFL removed) 18.74 ms (https://docs.radxa.com/en/som/cm/cm5/radxa-os/app-dev/ai/rknn-custom-yolo;
  thread count not stated) - i.e. ONNX ~9 fps vs NPU int8 ~53 fps. (Also corroborates F12: the one-click fp16 export is 3.4x
  slower than the split int8 model.) Even with 4 A76 threads the CPU path is ~10x slower than the NPU and would
  steal the cores the AprilTag CPU tail needs.
- Recommendation: keep ONNX as the dev/x86 path; on the Pi, have the UI warn (or refuse) when an `.onnx` model is attached on a
  machine with RKNN available, and offer the converter (F14). If the CPU path must be usable, raise intra-op threads to 2-4 and
  pin to A76s (shared concern with the CPU-affinity agent) - but do not invest further.
- Confidence: high. Effort: trivial.

### F16. Profiling tools to use before optimising further
- `RKNN_FLAG_COLLECT_PERF_MASK` + `rknn_query(RKNN_QUERY_PERF_DETAIL)` gives per-layer NPU time (flag "will reduce the frame
  rate", so only in a diagnostic mode); `RKNN_LOG_LEVEL=4` prints per-layer MAC utilisation and bandwidth occupation
  (rknpu2 README, https://github.com/rockchip-linux/rknpu2/blob/master/README.md). Use these to find (a) layers falling back to
  CPU (yolo11's attention block is the suspect for its 2.5 ms deficit vs v8n), and (b) whether the model is DDR-bound.
- DDR contention: the live NPU run (16.1 ms with Vulkan AprilTag + CPU AprilTag + MJPEG decode running) vs the model zoo's
  isolated 15.6 ms suggests contention costs ~3% today (inference; same model assumed). A CSDN estimate of 10-15 GB/s for yolov8n
  at full NPU load and "30-50% latency under contention" (https://gitcode.csdn.net/6a20efc710ee7a33f27773a8.html) is not borne out
  by the repo's own numbers. Low priority.
- lumen-bench has no RKNN case (tools/lumen-bench/main.cpp:186-187) - add one so model/input-size choices (F1, F7) are quotable.

### F17. AprilTag help from the NPU: only as an ROI proposer, research-grade
- Learned fiducial pipelines exist: DeepTag (https://arxiv.org/abs/2105.13731), DeepArUco++ (YOLOv8 marker detection + CNN
  corner refinement + CNN decode, aimed at extreme lighting; https://alphaxiv.org/overview/2411.05552v1), and a 2026 edge-SLAM
  paper that keeps classical AprilTag for detection and regresses 16 keypoints from 256x256 crops
  (https://arxiv.org/pdf/2608.09620). No embedded RK3588 timings found.
- The only plausible LumenVision use: a small int8 detector (yolov8n at 640x384, ~9-10 ms on one NPU core, F7) trained to find
  tag *regions* (incl. motion-blurred / far tags), feeding ROIs to vkapriltag/libapriltag at full resolution while the global
  pass runs at decimation 2 (Vulkan sink: decimation 2 = 29 -> 22.6 ms age, PERFORMANCE_ANALYSIS.md:166). Net: may recover
  long-range detection at decimation-2 cost, but adds a model to train/maintain per tag family and a second dependency on NPU
  timing. Corners and pose still come from the classical detector (sub-pixel accuracy matters for pose).
- Confidence: low (no measurements). Recommendation: park; revisit only if long-range tag detection becomes the bottleneck.

## Ideas rejected (and why)

- **Async mode (`RKNN_FLAG_ASYNC_MASK`)**: returns the previous frame's result -> +1 frame latency. Wrong trade for FRC (F10).
- **Pipelining pre/post with inference inside one sink**: <=2 ms throughput, 0 latency; superseded by zero-copy (F10).
- **Single model across 3 cores (`0_1_2`) as the default**: the repo measured no gain for yolo11n, +20% for v8n
  (PERFORMANCE_ANALYSIS.md:171); it also consumes all three cores so a second detector would contend. Offer it as an option
  for a lone v8-family sink only (F9).
- **"Feed NV12 straight to the NPU"**: not supported by the RK3588 runtime API (F5); use RGA into an rknn_create_mem buffer.
- **Ultralytics one-click `format=rknn` models as the recommended path**: in-graph decode, fused output, 8-image calibration;
  ~2.6x slower than the model-zoo recipe and only usable here as fp16 (F12).
- **Transformers (RT-DETR, Depth-Anything, SAM) on the per-frame path**: no RK3588 RT-DETR benchmark found; SAM-tiny encoder
  10.6 fps and CLIP image 6.3 fps on one core indicate transformer models are an order of magnitude too slow (F13).
- **Super-resolution on the NPU**: no model-zoo SR model; making a 720p camera look like 1080p does not add real detail for
  detection/pose and would add 10s of ms. Rejected.
- **Investing in the ONNX CPU path on the Pi**: ~10-15x slower than the NPU even if multi-threaded (F15).
- **Moving NMS into the graph**: RKNN does not support the NMS-free/end2end head (F12), and decode+NMS is already 0.1 ms on
  CPU (PERFORMANCE_ANALYSIS.md:154) - nothing to gain.
- **NEON-optimising the DFL decode**: decode+NMS is 0.1 ms; the cost is the runtime's dequantisation (F3), not the decoder.

## Ranked recommendations for this codebase

Baseline (PERFORMANCE_ANALYSIS.md:154): YOLO11 RKNN sink, 720p camera, work 18.2 ms (NPU 16.1), frame age 34 ms.

| # | Change | Where | Expected gain (RK3588) | Effort / risk |
|---|---|---|---|---|
| 1 | Rectangular model input matching the camera aspect: 640x384 for 16:9, 640x480 for 4:3 (F7) | converter + docs + UI shows WxH; backend already supports it | NPU 16.1 -> ~10-11 ms (est.), age 34 -> ~28 ms; same accuracy | low / low |
| 2 | Default/recommended model yolov8n instead of yolo11n (F1, F12); offer `0_1_2` core mask for a lone v8n sink (+20% measured in-repo) | docs, converter defaults, per-sink setting | -2.5 ms at 640 (13.1 vs 15.6 ms), ~-1.5 ms at 640x384; -20% more with 0_1_2; costs ~2 mAP (fp) | low / low (accuracy check per season) |
| 3 | `scripts/convert-yolo-rknn.py` in a pinned Docker toolkit: model-zoo head split, int8, in-domain calibration (100-300 team frames), `--imgsz HxW`, optional BGR first-conv swap, accuracy_analysis gate, version in manifest (F14, F8) | new script + docs | enabler for 1/2; recovers int8 accuracy (PV's 20-image calibration int8 sits ~7 mAP under fp32, F4) | medium / low |
| 4 | Zero-copy input: `rknn_create_mem` + `rknn_set_io_mem` once at Load; letterbox written straight into it - CPU (F6b) first, then RGA from the source buffer (F6) | RknnDetectionBackend, YoloPostProcess::Letterbox | -0.5..-1 ms (CPU variant), -1.2..-1.5 ms (RGA; more for 1080p sources); frees A76 time | low (CPU) / medium (RGA) |
| 5 | Int8-domain decode with score_sum early-out, bound output buffers, logit/probability decided at Load (F3) | YoloPostProcess, RknnDetectionBackend | -0.7..-0.9 ms | low-medium / low |
| 6 | Per-sink NPU core assignment (round-robin single cores; `rknn_set_core_mask`), optional `RKNN_FLAG_PRIOR_*` (F9, F2) | RknnDetectionBackend + Manager | none for one sink; prevents 2 detectors serialising on one core under AUTO (up to ~2x on the slower one) | low / low |
| 7 | Bump/pin runtime to rknn-toolkit2 2.5.0 (multi-output CPU-load and dma-buf fixes), log `RKNN_QUERY_SDK_VERSION` (api+drv) at Load, stop fetching `master` (F8) | install-deps.sh, backend Load | unknown, likely <1 ms; removes version drift | low / medium (re-convert models; driver 0.9.7 compat) |
| 8 | lumen-bench RKNN case + diagnostic mode with `RKNN_FLAG_COLLECT_PERF_MASK`/`RKNN_QUERY_PERF_DETAIL` (F16) | tools/lumen-bench | measurement enabler for 1/2 | low / none |
| 9 | Fix ROADMAP C1 / README "NV12 natively" claims (F5) | docs | correctness | trivial |
| 10 | ONNX on the Pi: warn when an .onnx model is attached and RKNN is available; if kept, 2-4 intra-op threads (F15) | UI/Server, OnnxDetectionBackend | avoids a 9 fps trap | trivial |
| 11 | Throughput mode: 2-3 contexts (`rknn_dup_context`), one per core, round-robin frames, in-order results - only for >60 fps cameras or many streams (F9) | ObjectDetectionSink threading | 2.5-3x throughput, 0 latency gain, ~3x NPU power | medium-high / medium |
| 12 | New heads: yolov8n-obb (13.0 ms) for game-piece yaw; yolov8n-seg (15.8 ms) for footprint ranging (F13) | decoder + UI | features at nano cost | medium |

Combined 1+2+4+5 on a 16:9 camera: work ~18.2 -> ~9-10 ms, frame age ~34 -> ~25 ms (estimate; NPU numbers for rect input
need a Pi run). Measure power/thermals before enabling 2 (0_1_2) or 11 (F11).

## Open questions

- Is the 16.1 ms "rknn-run" from int8 DFL yolo11n? bus.jpg isolated DFL int8 yolo11n = 26 ms total, live = 18.2 ms work; which
  model is deployed (input size? 640 or smaller?).
- The profile table lists no `rknn-set-input` lap though the code records one: was it folded into "convert", or ~0?
- Actual NPU time at 640x384 / 512x288 vs 640x640 for v8n and 11n (no published table; F7 estimates are pixel-proportional
  with a fixed overhead guess).
- Does `RKNN_NPU_CORE_0_1_2` still give +20% for v8n at a smaller rect input (less work per layer -> more relative sync cost)?
- NPU/SoC temperature and board power for 1 vs 3 busy NPU cores on a passively cooled OPi 5 in a robot (F11).
- Does the ubuntu-rockchip kernel's RKNPU v0.9.7 driver work with librknnrt 2.5.0, and is NPU SRAM (`RKNN_FLAG_ENABLE_SRAM`,
  `RKNN_FLAG_MEMORY_TRY_ALLOC_SRAM`) exposed by its device tree? Gain from SRAM unknown.
- How much of the int8 accuracy loss (F4) is calibration-set quality vs per-layer sensitivity? `rknn.accuracy_analysis` on a
  team-domain set would answer it.
- yolo11n's 2.5 ms deficit vs v8n: which layers (C2PSA attention?) - check with RKNN_QUERY_PERF_DETAIL.
