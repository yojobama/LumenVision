"""Splits COCO val2017 into a calibration pool and an evaluation set, and letterboxes the evaluation images for rknn_eval on the board.

    python rknn_eval_prep.py COCO_DIR OUT_DIR [--size 640] [--calib 500] [--seed 0]

COCO_DIR holds val2017/ and annotations/instances_val2017.json. Writes to OUT_DIR:
  eval.raw          the evaluation images, letterboxed like RknnDetectionBackend (aspect kept, centred, 114 grey, bilinear), RGB uint8 NHWC
  eval_index.json   per raw image: COCO image id, ratio and padding (to map the detections back)
  calib_jpg.txt     the calibration pool as the original JPEG paths (what convert-yolo-rknn.py passes the toolkit today)
  calib_npy/ + calib_npy.txt   the same images letterboxed to the model input, as .npy
"""
import argparse
import json
import random
from pathlib import Path

import cv2
import numpy as np


def letterbox(bgr, size):
    h, w = bgr.shape[:2]
    ratio = min(size / h, size / w)
    nw, nh = round(w * ratio), round(h * ratio)
    padx, pady = (size - nw) // 2, (size - nh) // 2
    canvas = np.full((size, size, 3), 114, np.uint8)
    canvas[pady:pady + nh, padx:padx + nw] = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_LINEAR)
    return cv2.cvtColor(canvas, cv2.COLOR_BGR2RGB), ratio, padx, pady


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("coco")
    parser.add_argument("out")
    parser.add_argument("--size", type=int, default=640)
    parser.add_argument("--calib", type=int, default=500)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    coco, out = Path(args.coco), Path(args.out)
    (out / "calib_npy").mkdir(parents=True, exist_ok=True)

    images = json.loads((coco / "annotations/instances_val2017.json").read_text())["images"]
    images.sort(key=lambda i: i["id"])
    random.Random(args.seed).shuffle(images)
    calib, evaluation = images[:args.calib], sorted(images[args.calib:], key=lambda i: i["id"])

    (out / "calib_jpg.txt").write_text("".join(f"{(coco / 'val2017' / i['file_name']).resolve()}\n" for i in calib))
    npy_paths = []
    for i in calib:
        rgb, _, _, _ = letterbox(cv2.imread(str(coco / "val2017" / i["file_name"])), args.size)
        path = (out / "calib_npy" / f"{i['id']}.npy").resolve()
        np.save(path, rgb.transpose(2, 0, 1)[None])  # the toolkit wants .npy calibration inputs as NCHW
        npy_paths.append(f"{path}\n")
    (out / "calib_npy.txt").write_text("".join(npy_paths))

    index = []
    with open(out / "eval.raw", "wb") as raw:
        for i in evaluation:
            rgb, ratio, padx, pady = letterbox(cv2.imread(str(coco / "val2017" / i["file_name"])), args.size)
            raw.write(rgb.tobytes())
            index.append({"id": i["id"], "ratio": ratio, "padx": padx, "pady": pady, "w": i["width"], "h": i["height"]})
    (out / "eval_index.json").write_text(json.dumps({"size": args.size, "images": index}))
    print(f"{len(calib)} calibration images, {len(evaluation)} evaluation images")


if __name__ == "__main__":
    main()
