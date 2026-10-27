# rknn-toolkit2 2.5.0: do its conversion options help YOLO11 on the RK3588?

Question: rknn-toolkit2 2.5.0 (and 2.2 to 2.3.2 before it) added NPU operators, quantisation algorithms, automatic mixed precision and compiler switches.
`scripts/convert-yolo-rknn.py` uses none of them (per-channel int8, `normal` calibration, defaults otherwise). Can any of them make the converted
model more accurate or faster?

Answer: no. Every option that builds for the RK3588 was measured on the board for speed and COCO accuracy. None is faster, none is more accurate than
the script's current settings, and four are clearly worse. The only levers that move either number are ones the script already offers: the input size
(384x640 instead of 640x640 cuts NPU time by 40%) and fp16 (+0.6 mAP for +74% NPU time). The script now pins its quantisation options explicitly
and records them in the manifest, and its docstring carries these numbers.

## Method

- Model: stock YOLO11n (`yolo11n.pt`, Ultralytics v8.3.0 assets) as a stand-in for the AprilTag model, whose weights and dataset are not in the repo.
  Exported with Rockchip's `airockchip/ultralytics_yolo11` (commit `0692e92`) as the split head the backend decodes: per scale a 64-channel DFL box
  tensor, 80 sigmoid class scores and a clamped score sum; 640x640, opset 12.
- Toolkit: rknn-toolkit2 2.5.0 in its own WSL venv (Python 3.12, torch 2.4.0 CPU, numpy 1.26.4). Board runtime: librknnrt 2.5.0.
- Data: COCO val2017 split once with a fixed seed into a 500-image calibration pool and 4500 evaluation images (disjoint). The evaluation images are
  letterboxed like `RknnDetectionBackend` (aspect kept, centred, grey 114, bilinear) into one raw RGB file on the board.
- Accuracy: `rknn_eval` (C++, on the board, one NPU core) runs every evaluation image and decodes like Ultralytics' validator (multi-label above 0.001,
  class-wise NMS at 0.7, 300 per image); `pycocotools` scores the detections in WSL. The same decode on the fp32 ONNX (onnxruntime) gives
  39.35 mAP50-95 on these 4500 images, against the published 39.5 on all 5000, so the decode is right.
- Speed: `rknn_perf`, core 0, `rknn_inputs_set` path, 500 runs after warm-up. Speed-only switches ran in alternating rounds (3 x 500 runs each).
  Run-to-run spread is about ±0.05 ms. Calibration sampling alone moves mAP by about ±0.15 (20, 50, 200 and 500 frames span 38.68 to 38.96).
- Harness, in `profiles/harness/`: `rknn_eval_prep.py`, `rknn_variants.py`, `rknn_eval.cpp`, `rknn_eval_score.py`, `rknn_board_run.sh` and
  `rknn_speed_ab.sh`. Raw results are in `profiles/npu-toolkit/`.

## Results

Baseline: `base_jpg200` is the script as it was (200 calibration JPEGs, per-channel, `normal`). Unless noted, variants calibrate on 200 letterboxed frames.

| Variant | Option | mAP50-95 | mAP50 | NPU ms | Wall ms | Verdict |
|---|---|---|---|---|---|---|
| fp32 ONNX | (onnxruntime reference) | 39.35 | 55.25 | - | - | ceiling |
| fp16 | `do_quantization=False` | 39.35 | 55.22 | 27.36 | 40.56 | lossless, +74% NPU time |
| fp16 + flash attention | `enable_flash_attention=True` | 39.35 | 55.22 | 27.54 | 40.85 | no change |
| **base_jpg200** | the script as it was | **38.76** | **54.54** | **15.75** | **16.82** | baseline |
| lb200 / lb500 / lb50 / lb20 | letterboxed calibration, 200/500/50/20 frames | 38.68 / 38.72 / 38.80 / 38.96 | 54.4-54.6 | 15.74-15.86 | 16.5-16.7 | within noise |
| auto hybrid | `build(auto_hybrid=True)` (2.3.2+) | 38.75 | 54.44 | 18.15 | 18.97 | 15% slower, no gain |
| mmse (20 frames) | `quantized_algorithm="mmse"` | 38.14 | 53.83 | 15.73 | 16.53 | -0.8 against lb20 |
| mmse + auto hybrid (20) | both | 38.07 | 53.79 | 15.97 | 16.83 | worse |
| kl_divergence | `quantized_algorithm="kl_divergence"` | 37.67 | 53.22 | 15.86 | 16.67 | -1.0 |
| per-layer | `quantized_method="layer"` | 37.12 | 52.65 | 15.82 | 16.62 | -1.6 |
| single core | `single_core_mode=True` | 38.68 | 54.44 | 15.79 | 16.59 | same; file 9% smaller |
| weight compression | `compress_weight=True` | 38.68 | 54.44 | 15.71 | 16.48 | same |
| first conv off | `disable_first_conv=True` (2.5.0) | 38.68 | 54.44 | 15.76 | 16.56 | same |

Speed-only A/B (3 alternating rounds against lb200 at 15.77 ms; the same calibration, so the same accuracy except where measured above):
`model_pruning` 15.75, `remove_reshape` 15.77, `optimization_level=2` 15.75, `sram_prefer` 15.75, `nbuf_prefer` 15.76, `enable_core0_only_mode` 15.78,
`enable_const_share` 15.75, `single_core_mode` 15.77, `compress_weight` 15.84 (wall +0.6 ms), `disable_first_conv` 15.77. None is outside the noise.

Not available on the RK3588: `quantized_dtype` `w8a16` and `w4a16` are refused for rk3588 by the toolkit, and with them the `gdq` algorithm and
`group{N}` quantisation, which need one of those types. `w16a16i_dfp` builds and loads, but `rknn_run` fails on the first input: the runtime has no
int16 Concat ("Meet unsupported concat dtype ... fallback cpu failed" at `/model.2/Concat`). The 3.8 ms `rknn_perf` first printed for it timed the failed
calls; `rknn_perf` now stops when `rknn_run` fails.

### What the toolkit's newer operators change for YOLO11

The 2.5.0 changelog's operator work (Transpose, Reshape, ReduceMax and others, plus Softmax/MatMul in 2.2/2.3) matters when an operator would
otherwise fall back to the CPU. In this export every operator runs on the NPU: the int8 build log has no CPU fallbacks, and YOLO11's attention block
(C2PSA: MatMul, Softmax, Reshape, Transpose) is included. The DFL decode, which is what the NPU handles badly, is already outside the graph in
Rockchip's split head and runs in `YoloPostProcess` on the CPU. So there is nothing left for the new operators to take over.

### Hidden compiler switches

2.5.0 prints its compiler flags at build time (`op_group_sram_opt`, `op_group_nbuf_opt`, `enable_core0_only_mode`, `disable_first_conv`, ...).
The changelog's "enable core0 only" and "model conversion with/without first conv" are these. They are set through undocumented `config()` keywords
(`sram_prefer`, `nbuf_prefer`, `enable_core0_only_mode`, `disable_first_conv`, `enable_const_share`; `config()` lists the valid set when given an unknown
one). `compiler_control_file` takes a JSON file but does not set them. All of them were measured above and none changed the NPU time.

### Levers that do move the numbers

| Change | NPU ms | Wall ms | Note |
|---|---|---|---|
| 640x640 int8 (baseline) | 15.76 | 16.57 | |
| 384x640 int8 | 9.50 | 10.28 | -40%. For a 16:9 camera the picture is the same scale, so accuracy is unchanged; the backend already reads the model's input size |
| score-sum outputs removed | 15.63 | 16.39 | -0.9% (against 15.77 in the same rounds). The backend never reads them, but it accepts only box/class/sum triples, so taking this needs a decoder change: not worth it |
| fp16 | 27.36 | 40.56 | +0.6 mAP; the only accuracy gain found |

## Takeaways for the conversion script

1. Keep per-channel int8 with the `normal` algorithm. Every alternative measured was the same or worse, and mmse is also very slow to convert
   (about 0.7 s per calibration frame per layer, single-threaded: 200 frames would take about 6 hours, 20 frames took 9 minutes).
2. The size of the calibration set barely matters on COCO: 20 to 500 frames all landed within ±0.15 mAP. Letterboxing the frames first makes no
   difference either; the toolkit's own resize of the JPEGs is fine. In-domain frames are still the right choice for a custom model.
3. Use the 384x640 input for 16:9 cameras (-40% NPU time), as the script already recommends. The deployed AprilTag model is 640x640; re-exporting it at
   384x640 from its weights is the largest available NPU gain.
4. When accuracy matters more than rate, fp16 is lossless against fp32 at 27 ms per frame on one core.
5. Revisit the w8a16 and w16a16 types if a later toolkit and runtime support them on the RK3588. Today one is refused and the other does not run.
