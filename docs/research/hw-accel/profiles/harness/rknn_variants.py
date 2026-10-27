"""Converts one ONNX into .rknn variants that differ only in rknn-toolkit2 options, for rknn_perf (speed) and rknn_eval (accuracy).

    python rknn_variants.py MODEL.onnx PREP_DIR OUT_DIR [VARIANT ...]

PREP_DIR is rknn_eval_prep.py's output (calib_jpg.txt, calib_npy.txt). Each variant builds in its own process, since the toolkit keeps
state between builds; a variant's log is OUT_DIR/NAME.log and a failed build is reported, not fatal.
"""
import json
import subprocess
import sys
import time
from pathlib import Path

# name: (config options beyond the script's mean/std/target, build options, calibration list, images taken from it)
VARIANTS = {
    "base_jpg200": ({}, {}, "calib_jpg.txt", 200),  # convert-yolo-rknn.py as it stands
    "lb200": ({}, {}, "calib_npy.txt", 200),  # calibration frames letterboxed like the backend's input
    "lb500": ({}, {}, "calib_npy.txt", 500),
    "lb50": ({}, {}, "calib_npy.txt", 50),
    # mmse costs ~0.7 s per calibration image per layer (single-threaded): 200 images would take ~6 h, 20 take ~40 min
    "mmse": ({"quantized_algorithm": "mmse"}, {}, "calib_npy.txt", 20),
    "kl": ({"quantized_algorithm": "kl_divergence"}, {}, "calib_npy.txt", 200),
    "gdq": ({"quantized_algorithm": "gdq"}, {}, "calib_npy.txt", 200),
    "layer": ({"quantized_method": "layer"}, {}, "calib_npy.txt", 200),
    "group32": ({"quantized_method": "group32"}, {}, "calib_npy.txt", 200),
    "auto_hybrid": ({}, {"auto_hybrid": True}, "calib_npy.txt", 200),
    "mmse_auto_hybrid": ({"quantized_algorithm": "mmse"}, {"auto_hybrid": True}, "calib_npy.txt", 20),
    "lb20": ({}, {}, "calib_npy.txt", 20),  # the same 20 frames without mmse, to separate the algorithm from the calibration size
    "w8a16": ({"quantized_dtype": "w8a16"}, {}, "calib_npy.txt", 200),
    "w16a16i_dfp": ({"quantized_dtype": "w16a16i_dfp"}, {}, "calib_npy.txt", 200),
    "w4a16": ({"quantized_dtype": "w4a16"}, {}, "calib_npy.txt", 200),
    "single_core": ({"single_core_mode": True}, {}, "calib_npy.txt", 200),
    "flash_attention": ({"enable_flash_attention": True}, {}, "calib_npy.txt", 200),
    "compress": ({"compress_weight": True}, {}, "calib_npy.txt", 200),
    "pruning": ({"model_pruning": True}, {}, "calib_npy.txt", 200),
    "remove_reshape": ({"remove_reshape": True}, {}, "calib_npy.txt", 200),
    "opt2": ({"optimization_level": 2}, {}, "calib_npy.txt", 200),
    # compiler switches 2.5.0 accepts as undocumented config keywords (they appear in the build log's option line)
    "sram": ({"sram_prefer": True}, {}, "calib_npy.txt", 200),
    "nbuf": ({"nbuf_prefer": True}, {}, "calib_npy.txt", 200),
    "core0_only": ({"enable_core0_only_mode": True}, {}, "calib_npy.txt", 200),
    "no_first_conv": ({"disable_first_conv": True}, {}, "calib_npy.txt", 200),
    "const_share": ({"enable_const_share": True}, {}, "calib_npy.txt", 200),
    "fp16": ({}, {"do_quantization": False}, None, 0),
    "fp16_flash_attention": ({"enable_flash_attention": True}, {"do_quantization": False}, None, 0),
}


def build_one(onnx, prep, out, name):
    from rknn.api import RKNN

    config, build, calib, count = VARIANTS[name]
    rknn = RKNN(verbose=False)
    rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]], target_platform="rk3588", **config)
    if rknn.load_onnx(model=onnx) != 0:
        sys.exit("load_onnx failed")
    dataset = None
    if calib:
        dataset = str(Path(out) / f"{name}.dataset.txt")
        lines = (Path(prep) / calib).read_text().splitlines()[:count]
        Path(dataset).write_text("\n".join(lines) + "\n")
    options = {"do_quantization": True, "dataset": dataset, **build}
    if rknn.build(**options) != 0:
        sys.exit("build failed")
    if rknn.export_rknn(str(Path(out) / f"{name}.rknn")) != 0:
        sys.exit("export failed")
    rknn.release()


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--one":
        build_one(*sys.argv[2:6])
        return
    onnx, prep, out = sys.argv[1:4]
    names = sys.argv[4:] or list(VARIANTS)
    Path(out).mkdir(parents=True, exist_ok=True)
    for name in names:
        t0 = time.time()
        with open(Path(out) / f"{name}.log", "w") as log:
            code = subprocess.call([sys.executable, __file__, "--one", onnx, prep, out, name], stdout=log, stderr=subprocess.STDOUT)
        rknn = Path(out) / f"{name}.rknn"
        print(json.dumps({"variant": name, "ok": code == 0 and rknn.exists(), "seconds": round(time.time() - t0),
                          "bytes": rknn.stat().st_size if rknn.exists() else 0}), flush=True)


if __name__ == "__main__":
    main()
