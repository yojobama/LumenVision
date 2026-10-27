"""Scores rknn_eval detections against COCO val2017, and makes the float reference from the ONNX with onnxruntime.

    python rknn_eval_score.py score DETECTIONS.txt PREP_DIR COCO_DIR
    python rknn_eval_score.py onnx MODEL.onnx PREP_DIR OUT.txt      (same decode as rknn_eval.cpp, in float32 on the CPU)

PREP_DIR is rknn_eval_prep.py's output. Prints one JSON line: mAP50-95, mAP50, mAP small/medium/large.
"""
import json
import sys
from pathlib import Path

import numpy as np


def coco_category_ids(annotations):
    return sorted(c["id"] for c in annotations["categories"])


def score(detections_path, prep, coco):
    from pycocotools.coco import COCO
    from pycocotools.cocoeval import COCOeval

    index = json.loads((Path(prep) / "eval_index.json").read_text())["images"]
    gt = COCO(str(Path(coco) / "annotations/instances_val2017.json"))
    categories = sorted(gt.getCatIds())
    results = []
    for line in Path(detections_path).read_text().splitlines():
        n, cls, s, x1, y1, x2, y2 = line.split()
        info = index[int(n)]
        r, px, py = info["ratio"], info["padx"], info["pady"]
        x1 = min(max((float(x1) - px) / r, 0), info["w"])
        x2 = min(max((float(x2) - px) / r, 0), info["w"])
        y1 = min(max((float(y1) - py) / r, 0), info["h"])
        y2 = min(max((float(y2) - py) / r, 0), info["h"])
        results.append({"image_id": info["id"], "category_id": categories[int(cls)], "bbox": [x1, y1, x2 - x1, y2 - y1], "score": float(s)})
    dt = gt.loadRes(results)
    ev = COCOeval(gt, dt, "bbox")
    ev.params.imgIds = [i["id"] for i in index]
    ev.evaluate()
    ev.accumulate()
    ev.summarize()
    s = ev.stats
    print(json.dumps({"detections": Path(detections_path).name, "images": len(index), "mAP50_95": round(s[0], 4), "mAP50": round(s[1], 4),
                      "mAP_small": round(s[3], 4), "mAP_medium": round(s[4], 4), "mAP_large": round(s[5], 4)}))


def onnx_reference(model, prep, out, conf=0.001, iou=0.7):
    import onnxruntime as ort
    import torch
    import torchvision

    meta = json.loads((Path(prep) / "eval_index.json").read_text())
    size = meta["size"]
    session = ort.InferenceSession(model, providers=["CPUExecutionProvider"])
    name = session.get_inputs()[0].name
    frame = size * size * 3
    with open(Path(prep) / "eval.raw", "rb") as raw, open(out, "w") as f:
        for n in range(len(meta["images"])):
            rgb = np.frombuffer(raw.read(frame), np.uint8).reshape(1, size, size, 3)
            x = rgb.transpose(0, 3, 1, 2).astype(np.float32) / 255.0
            outputs = session.run(None, {name: x})
            boxes, scores, classes = [], [], []
            for s in range(0, len(outputs), 3):
                box, cls = outputs[s][0], outputs[s + 1][0]
                bins = box.shape[0] // 4
                gh, gw = box.shape[1:]
                stride = size / gh
                logits = box.reshape(4, bins, gh * gw)
                e = np.exp(logits - logits.max(1, keepdims=True))
                dist = (e * np.arange(bins)[None, :, None]).sum(1) / e.sum(1)
                cx = (np.arange(gh * gw) % gw) + 0.5
                cy = (np.arange(gh * gw) // gw) + 0.5
                xyxy = np.stack([cx - dist[0], cy - dist[1], cx + dist[2], cy + dist[3]], 1) * stride
                c = cls.reshape(cls.shape[0], -1)
                cell, k = np.nonzero(c.T > conf)
                boxes.append(xyxy[cell])
                scores.append(c[k, cell])
                classes.append(k)
            b = torch.from_numpy(np.concatenate(boxes)).float()
            sc = torch.from_numpy(np.concatenate(scores)).float()
            cl = torch.from_numpy(np.concatenate(classes))
            if len(sc) > 30000:
                top = sc.topk(30000).indices
                b, sc, cl = b[top], sc[top], cl[top]
            keep = torchvision.ops.batched_nms(b, sc, cl, iou)[:300]
            for i in keep.tolist():
                x1, y1, x2, y2 = b[i].tolist()
                f.write(f"{n} {int(cl[i])} {float(sc[i]):.5f} {x1:.2f} {y1:.2f} {x2:.2f} {y2:.2f}\n")


if __name__ == "__main__":
    if sys.argv[1] == "score":
        score(sys.argv[2], sys.argv[3], sys.argv[4])
    elif sys.argv[1] == "onnx":
        onnx_reference(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(__doc__)
