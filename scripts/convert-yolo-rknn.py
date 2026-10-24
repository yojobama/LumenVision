#!/usr/bin/env python3
"""Converts a YOLOv8 / YOLO11 ONNX export into an int8 .rknn model for the RK3588 NPU.

rknn-toolkit2 only runs on x86-64 Linux, so run this in a container or WSL, not on the board:

    docker run --rm -v "$PWD":/work -w /work <image with rknn-toolkit2> \\
        python3 scripts/convert-yolo-rknn.py --onnx yolov8n.onnx --out yolov8n-640x384.rknn \\
        --dataset calibration-frames/ --imgsz 384x640

The ONNX must come from Rockchip's split-head recipe (airockchip/ultralytics_yolov8, see its RKOPT_README.md): the decoded head is cut
into box / class / score-sum outputs for each FPN scale, which is the layout RknnDetectionBackend decodes. Export it at the size you
will run (HxW): for a 16:9 camera 384x640 skips the grey padding a square 640x640 input would carry (44% of the input at 720p),
and detections are unchanged because the picture's scale is the same.

Quantisation is only as good as its calibration: use 100-300 frames from the camera and scenes the robot will actually see, not
dataset images. The toolkit version should match the runtime on the board (see the log line RknnDetectionBackend prints at load); the
version used is written next to the model.

This script is not run by CI; check the printed accuracy analysis and the model on the board before relying on a conversion.
"""
import argparse
import json
import random
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp"}


def parse_size(text):
    height, width = text.lower().split("x")
    return int(height), int(width)


def calibration_list(dataset, samples):
    """The list of image paths RKNN calibrates on: a folder is sampled, a .txt file is used as given (one path per line)."""
    path = Path(dataset)
    if path.is_file():
        return [line.strip() for line in path.read_text().splitlines() if line.strip()]
    images = sorted(p for p in path.rglob("*") if p.suffix.lower() in IMAGE_SUFFIXES)
    if not images:
        sys.exit(f"no images found in {path}")
    random.Random(0).shuffle(images)
    return [str(p.resolve()) for p in images[:samples]]


def check_onnx(onnx_path, imgsz):
    """Refuses an export whose input size or head layout the backend cannot use (needs the onnx package; skipped without it)."""
    try:
        import onnx
    except ImportError:
        print("onnx is not installed: skipping the input and output checks")
        return
    model = onnx.load(onnx_path)
    dims = [d.dim_value for d in model.graph.input[0].type.tensor_type.shape.dim]
    outputs = len(model.graph.output)
    print(f"onnx input {dims}, {outputs} outputs")
    if len(dims) == 4 and (dims[2], dims[3]) != imgsz:
        sys.exit(f"--imgsz {imgsz[0]}x{imgsz[1]} does not match the ONNX input {dims[2]}x{dims[3]}: export the ONNX at that size")
    if outputs != 1 and outputs % 3 != 0:
        sys.exit(f"{outputs} outputs is neither a fused head nor box/class/score-sum triples per scale: use the Rockchip split-head export")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--onnx", required=True, help="the split-head YOLO ONNX export")
    parser.add_argument("--out", required=True, help="the .rknn file to write")
    parser.add_argument("--dataset", help="folder of calibration frames, or a .txt listing their paths (required for int8)")
    parser.add_argument("--imgsz", required=True, type=parse_size, help="model input size as HxW, e.g. 384x640")
    parser.add_argument("--samples", type=int, default=200, help="frames sampled from a dataset folder (default 200)")
    parser.add_argument("--fp16", action="store_true", help="skip quantisation (slower on the NPU, no calibration needed)")
    parser.add_argument("--analysis", action="store_true", help="run the toolkit's accuracy analysis on one calibration frame")
    parser.add_argument("--target", default="rk3588")
    args = parser.parse_args()

    if not args.fp16 and not args.dataset:
        sys.exit("--dataset is required for int8 (or pass --fp16)")
    check_onnx(args.onnx, args.imgsz)

    from rknn.api import RKNN

    images = [] if args.fp16 else calibration_list(args.dataset, args.samples)
    rknn = RKNN(verbose=False)
    # the graph takes RGB uint8 and divides by 255 itself, so the backend feeds raw pixels (RknnDetectionBackend: "no normalisation")
    rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]], target_platform=args.target)
    if rknn.load_onnx(model=args.onnx) != 0:
        sys.exit("load_onnx failed")

    with tempfile.TemporaryDirectory() as scratch:
        dataset_file = None
        if images:
            dataset_file = str(Path(scratch) / "dataset.txt")
            Path(dataset_file).write_text("\n".join(images) + "\n")
        if rknn.build(do_quantization=not args.fp16, dataset=dataset_file) != 0:
            sys.exit("build failed")
        if args.analysis and images:
            rknn.accuracy_analysis(inputs=[images[0]], output_dir=str(Path(args.out).with_suffix(".analysis")))

    if rknn.export_rknn(args.out) != 0:
        sys.exit("export_rknn failed")
    rknn.release()

    try:
        from importlib.metadata import version
        toolkit = version("rknn-toolkit2")
    except Exception:
        toolkit = "unknown"
    manifest = {
        "model": Path(args.out).name,
        "source": Path(args.onnx).name,
        "inputSize": {"height": args.imgsz[0], "width": args.imgsz[1]},
        "quantisation": "fp16" if args.fp16 else "int8",
        "calibrationFrames": len(images),
        "toolkitVersion": toolkit,
        "converted": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    Path(args.out).with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {args.out} and {Path(args.out).with_suffix('.json').name}")


if __name__ == "__main__":
    main()
