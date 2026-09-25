#!/usr/bin/env python3
"""Measure the Guard++ face-recognition pipeline on labeled images.

Uses the same detector and embedder ONNX files as guard-inference
(inference/models), the same letterbox/decode rules, on CPU via onnxruntime.

Layout (one folder per person, the folder name is the identity):

    DIR/ali/1.jpg 2.jpg ...
    DIR/osama/1.jpg ...

What it reports, per variant (preprocessing x crop margin):
  * genuine vs impostor cosine-similarity distributions (needs >= 2 images of
    at least one person for genuine pairs, >= 2 people for impostor pairs)
  * separation: AUC, d-prime, EER
  * FAR / FRR at the current production threshold (core.json match_threshold)
  * the threshold that meets --target-far, and whether the data can resolve it
  * with --degrade: how a face's own similarity decays as its resolution drops
    (evidence for a minimum-face-size gate)

Nothing here picks a threshold for you: it prints what the data supports and
how much data would be needed to claim more.

    tools/face_eval.py data/faces --target-far 0.001 --degrade
"""
import argparse, json, sys
from itertools import combinations
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "inference" / "models"
IMG_EXT = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}

PREPROCESS = {
    # what guard-inference does today (trt_models.cpp: px / 255 -> [0, 1])
    "x/255": lambda rgb: rgb / 255.0,
    # facenet-pytorch fixed_image_standardization -> [-1, 1]
    "(x-127.5)/128": lambda rgb: (rgb - 127.5) / 128.0,
}


class Detector:
    """yolov8n-face, decoded exactly like TrtYoloDetector (letterbox top-left, /255, RGB)."""

    def __init__(self, conf=0.50, nms=0.45, size=640):
        self.s = ort.InferenceSession(str(MODELS / "yolov8n-face.onnx"), providers=["CPUExecutionProvider"])
        self.conf, self.nms, self.size = conf, nms, size

    def detect(self, bgr):
        h, w = bgr.shape[:2]
        r = min(self.size / w, self.size / h)
        nw, nh = round(w * r), round(h * r)
        canvas = np.full((self.size, self.size, 3), 114, np.uint8)
        canvas[:nh, :nw] = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_LINEAR)
        x = canvas[:, :, ::-1].astype(np.float32).transpose(2, 0, 1)[None] / 255.0
        out = self.s.run(None, {self.s.get_inputs()[0].name: x})[0][0]          # [5, N]
        cx, cy, bw, bh, conf = out
        keep = (conf >= self.conf) & (bw > 0) & (bh > 0)
        boxes = np.stack([(cx - bw / 2) / r, (cy - bh / 2) / r, bw / r, bh / r], 1)[keep]
        scores = conf[keep]
        idx = cv2.dnn.NMSBoxes(boxes.tolist(), scores.tolist(), self.conf, self.nms)
        return [(boxes[i].astype(int), float(scores[i])) for i in np.array(idx).flatten()]


class Embedder:
    def __init__(self):
        self.s = ort.InferenceSession(str(MODELS / "facenet_vggface2.onnx"), providers=["CPUExecutionProvider"])
        self.name = self.s.get_inputs()[0].name

    def embed(self, bgr_crop, pre):
        rgb = cv2.resize(bgr_crop, (160, 160), interpolation=cv2.INTER_AREA)[:, :, ::-1].astype(np.float32)
        x = PREPROCESS[pre](rgb).transpose(2, 0, 1)[None].astype(np.float32)
        e = self.s.run(None, {self.name: x})[0][0]
        return e / max(np.linalg.norm(e), 1e-9)


def crop(img, box, margin):
    x, y, w, h = box
    mx, my = int(w * margin), int(h * margin)
    x0, y0 = max(0, x - mx), max(0, y - my)
    x1, y1 = min(img.shape[1], x + w + mx), min(img.shape[0], y + h + my)
    return img[y0:y1, x0:x1]


def load_set(root, det):
    people, skipped = {}, []
    for pdir in sorted(p for p in Path(root).iterdir() if p.is_dir()):
        for f in sorted(pdir.iterdir()):
            if f.suffix.lower() not in IMG_EXT:
                continue
            img = cv2.imread(str(f))
            faces = det.detect(img) if img is not None else []
            if not faces:
                skipped.append(str(f)); continue
            box, conf = max(faces, key=lambda t: t[1])          # best face, like enrollment
            people.setdefault(pdir.name, []).append({"file": str(f), "img": img, "box": box, "conf": conf})
    return people, skipped


def auc(gen, imp):
    g, i = np.asarray(gen), np.asarray(imp)
    return float(((g[:, None] > i[None]).sum() + 0.5 * (g[:, None] == i[None]).sum()) / (len(g) * len(i)))


def eer(gen, imp):
    best = (1.0, None)
    for t in np.unique(np.concatenate([gen, imp])):
        far, frr = float(np.mean(imp >= t)), float(np.mean(gen < t))
        if abs(far - frr) < best[0]:
            best = (abs(far - frr), (far + frr) / 2, float(t))
    return best[1], best[2]


def analyse(people, emb, pre, margin, thr, target_far):
    E = {p: [emb.embed(crop(s["img"], s["box"], margin), pre) for s in v] for p, v in people.items()}
    gen = [float(a @ b) for p, v in E.items() for a, b in combinations(v, 2)]
    imp = [float(a @ b) for (p, va), (q, vb) in combinations(E.items(), 2) for a in va for b in vb]
    r = {"preprocess": pre, "margin": margin, "genuine_pairs": len(gen), "impostor_pairs": len(imp)}
    if imp:
        i = np.array(imp)
        r["impostor"] = {"mean": float(i.mean()), "max": float(i.max()), "p99": float(np.percentile(i, 99))}
        r["far_at_threshold"] = float(np.mean(i >= thr))
        # cannot resolve a FAR smaller than 1/n; report the honest bound instead
        r["target_far_resolvable"] = len(i) >= 1.0 / target_far
        r["threshold_for_target_far"] = float(np.quantile(i, 1 - target_far)) if r["target_far_resolvable"] else None
        if np.mean(i >= thr) == 0:
            r["far_upper_95_rule_of_three"] = min(1.0, 3.0 / len(i))
    if gen:
        g = np.array(gen)
        r["genuine"] = {"mean": float(g.mean()), "min": float(g.min())}
        r["frr_at_threshold"] = float(np.mean(g < thr))
    if gen and imp:
        r["auc"] = auc(gen, imp)
        sg, si = np.std(gen), np.std(imp)
        r["d_prime"] = float((np.mean(gen) - np.mean(imp)) / max(np.sqrt((sg**2 + si**2) / 2), 1e-9))
        r["eer"], r["eer_threshold"] = eer(np.array(gen), np.array(imp))
    return r, E


def degrade(people, emb, pre, margin, factors=(1, 2, 3, 4, 6, 8)):
    """Similarity of a face to a down-then-up-sampled copy of itself, by face size."""
    rows = []
    for p, v in people.items():
        for s in v:
            c = crop(s["img"], s["box"], margin)
            ref = emb.embed(c, pre)
            for f in factors:
                h, w = c.shape[:2]
                small = cv2.resize(c, (max(8, w // f), max(8, h // f)), interpolation=cv2.INTER_AREA)
                back = cv2.resize(small, (w, h), interpolation=cv2.INTER_LINEAR)
                rows.append({"person": p, "factor": f, "face_px": int(min(w, h) / f),
                             "sim_to_full_res": float(ref @ emb.embed(back, pre))})
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", help="folder with one sub-folder of images per person")
    ap.add_argument("--threshold", type=float, default=0.55, help="production match_threshold (core.json)")
    ap.add_argument("--target-far", type=float, default=0.001)
    ap.add_argument("--margins", type=float, nargs="+", default=[0.0, 0.15])
    ap.add_argument("--degrade", action="store_true", help="also measure similarity vs face resolution")
    ap.add_argument("--json", help="write full results here")
    a = ap.parse_args()

    det, emb = Detector(), Embedder()
    people, skipped = load_set(a.dir, det)
    n_img = sum(len(v) for v in people.values())
    print(f"{len(people)} people, {n_img} images with a detected face")
    for p, v in people.items():
        print(f"  {p}: {len(v)} image(s), face box min side {[int(min(s['box'][2:])) for s in v]} px")
    for s in skipped:
        print(f"  (no face found, skipped: {s})")
    if len(people) < 2:
        sys.exit("need at least 2 people for impostor pairs")

    results = {"people": {p: len(v) for p, v in people.items()}, "variants": [], "degrade": {}}
    print(f"\nproduction threshold {a.threshold}, target FAR {a.target_far}\n")
    for pre in PREPROCESS:
        for m in a.margins:
            r, _ = analyse(people, emb, pre, m, a.threshold, a.target_far)
            results["variants"].append(r)
            print(f"[{pre}  margin={m}]  genuine pairs={r['genuine_pairs']}  impostor pairs={r['impostor_pairs']}")
            if "impostor" in r:
                print(f"   impostor sim: mean {r['impostor']['mean']:.3f}  max {r['impostor']['max']:.3f}")
            if "genuine" in r:
                print(f"   genuine  sim: mean {r['genuine']['mean']:.3f}  min {r['genuine']['min']:.3f}")
            if "auc" in r:
                print(f"   AUC {r['auc']:.3f}  d' {r['d_prime']:.2f}  EER {r['eer']:.3f} @ {r['eer_threshold']:.3f}")
            if "frr_at_threshold" in r:
                print(f"   at {a.threshold}: FRR {r['frr_at_threshold']:.3f}  FAR {r.get('far_at_threshold', float('nan')):.3f}")
            if r.get("threshold_for_target_far") is not None:
                print(f"   threshold for FAR {a.target_far}: {r['threshold_for_target_far']:.3f}")
            elif "impostor" in r:
                print(f"   FAR {a.target_far} NOT resolvable: needs >= {int(1 / a.target_far)} impostor pairs, have {r['impostor_pairs']}")
            if not r["genuine_pairs"]:
                print("   no genuine pairs: add >= 2 images of the same person to measure FRR / separation")

    if a.degrade:
        print("\nResolution decay (similarity of a face to a down/up-sampled copy of itself):")
        for pre in PREPROCESS:
            rows = degrade(people, emb, pre, 0.0)
            results["degrade"][pre] = rows
            print(f"  [{pre}]  face px -> mean sim")
            for f in sorted({r['factor'] for r in rows}):
                rr = [r for r in rows if r["factor"] == f]
                print(f"     ~{int(np.mean([r['face_px'] for r in rr])):4d}px  {np.mean([r['sim_to_full_res'] for r in rr]):.3f}"
                      f"  (min {min(r['sim_to_full_res'] for r in rr):.3f})")
    if a.json:
        Path(a.json).write_text(json.dumps(results, indent=1))
        print(f"\nwrote {a.json}")


if __name__ == "__main__":
    main()
