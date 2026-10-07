#!/usr/bin/env python3
"""Fixture for gate P-4: the Lichtfeld densification plugin's own host stage,
run on upstream RoMa v2 matches, with every input and decision written out.

    python3 reference/python/roma_plugin_parity.py --plugin <clone> \
        --model <cube dataset>/sparse/0 --images <cube dataset>/images \
        --pairs pairs.json --out <fixture dir>
    roma_plugin_parity_test <fixture dir>

The plugin (GPL-3.0-or-later, github.com/shadygm/Lichtfeld-Densification-Plugin)
is imported from a clone and run unmodified apart from what it needs to run
outside LichtFeld: a stub `lichtfeld` log module and a matcher that calls
upstream RoMaV2's match() per pair (the plugin's vendored copy adds a
match_from_features() upstream does not have). `pairs.json` is a list of
[reference, neighbour] view names. The sampler is wrapped only to record the
indices it returns; the C++ side is handed the same ones, since numpy's draws
cannot be reproduced.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import tempfile
import types
from pathlib import Path

import numpy as np


def qR(q):
    w, x, y, z = q
    return np.array([[1 - 2 * y * y - 2 * z * z, 2 * x * y - 2 * w * z, 2 * x * z + 2 * w * y],
                     [2 * x * y + 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z - 2 * w * x],
                     [2 * x * z - 2 * w * y, 2 * y * z + 2 * w * x, 1 - 2 * x * x - 2 * y * y]])


def write_rwm(path, warp, cert):
    h, w = cert.shape
    with open(path, "wb") as f:
        f.write(b"RWM1" + struct.pack("<iii", w, h, 0))
        f.write(np.ascontiguousarray(warp, dtype="<f4").tobytes())
        f.write(np.ascontiguousarray(cert, dtype="<f4").tobytes())


def write_text(out: Path) -> None:
    """The fixture's JSON as the whitespace tables the C++ test reads."""
    meta = json.loads((out / "meta.json").read_text())
    with open(out / "views.txt", "w") as f:
        for n, v in meta["views"].items():
            # The plugin holds R, t and K as float32 (CameraRecord): so does the C++ side.
            f32 = lambda a: [float(x) for x in np.asarray(a, np.float32).reshape(-1)]
            vals = [v["w"], v["h"], *f32(v["K"]), *f32(v["R"]), *f32(v["t"])]
            f.write(n + " " + " ".join(repr(float(x)) if not isinstance(x, int) else str(x) for x in vals) + "\n")
    cfg = dict(meta["config"], w_match=meta["w_match"], h_match=meta["h_match"])
    (out / "config.txt").write_text("".join(f"{k} {float(v)}\n" for k, v in cfg.items() if not isinstance(v, str)))
    (out / "refs.txt").write_text("".join(" ".join([r["ref"], *r["nbrs"]]) + "\n" for r in meta["refs"]))
    for r in meta["refs"]:
        d = out / r["ref"]
        tr = json.loads((d / "tracks.json").read_text())
        (d / "tracks.txt").write_text("".join(f"{len(t)} " + " ".join(f"{n} {x!r} {y!r}" for n, x, y in t) + "\n" for t in tr))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--plugin", type=Path, required=True)
    ap.add_argument("--model", type=Path, required=True, help="text COLMAP model of PINHOLE views")
    ap.add_argument("--images", type=Path, required=True, help="RGBA views; alpha = keep")
    ap.add_argument("--pairs", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--device", default="mps")
    ap.add_argument("--f64-dlt", action="store_true",
                    help="run the plugin's DLT in float64, to tell its float32 rounding from a defect")
    args = ap.parse_args()

    class _Log:
        def info(self, m): print("[plugin]", m, flush=True)
        warn = error = info
        def debug(self, m): pass
    lf = types.ModuleType("lichtfeld"); lf.log = _Log(); sys.modules["lichtfeld"] = lf
    sys.path.insert(0, str(args.plugin))
    import torch
    from PIL import Image
    Image.MAX_IMAGE_PIXELS = None
    import core.pipeline as PL
    from core.camera_models import CameraRecord
    from core.config import DensePipelineConfig
    from romav2 import RoMaV2

    pairs = json.loads(args.pairs.read_text())
    refs = list(dict.fromkeys(p[0] for p in pairs))
    names = list(dict.fromkeys([p[0] for p in pairs] + [p[1] for p in pairs]))
    cam = [l.split() for l in (args.model / "cameras.txt").read_text().splitlines() if l and l[0] != "#"][0]
    assert cam[1] == "PINHOLE", cam
    W, H = int(cam[2]), int(cam[3])
    fx, fy, cx, cy = map(float, cam[4:8])
    K = np.array([[fx, 0, cx], [0, fy, cy], [0, 0, 1]], np.float32)
    poses = {}
    for l in (args.model / "images.txt").read_text().splitlines():
        s = l.split()
        if not l or l[0] == "#" or len(s) < 10 or not s[9].endswith((".png", ".jpg")):
            continue
        stem = s[9].rsplit(".", 1)[0]
        if stem in names:
            poses[stem] = (qR([float(v) for v in s[1:5]]), np.array([float(v) for v in s[5:8]]))
    missing = [n for n in names if n not in poses]
    assert not missing, missing

    tmp = Path(tempfile.mkdtemp())
    recs = []
    for i, n in enumerate(names):
        im = Image.open(args.images / f"{n}.png")
        a = np.asarray(im.getchannel("A")) if im.mode == "RGBA" else np.full((H, W), 255, np.uint8)
        Image.fromarray(np.where(a > 0, 255, 0).astype(np.uint8)).save(tmp / f"{n}.png")
        Image.fromarray(np.asarray(im.convert("RGB"))).save(tmp / f"{n}_rgb.png")
        R, t = poses[n]
        R = R.astype(np.float32); t = t.astype(np.float32).reshape(3, 1)
        recs.append(CameraRecord(uid=i, image_path=str(tmp / f"{n}_rgb.png"), mask_path=str(tmp / f"{n}.png"),
                                 width=W, height=H, K=K, R=R, t=t, P=K @ np.concatenate([R, t], 1),
                                 C=(-R.T @ t).reshape(3)))
    idx = {n: i for i, n in enumerate(names)}
    nn_table = [[] for _ in recs]
    for a, b in pairs:
        nn_table[idx[a]].append(idx[b])
    nns = max(len(r) for r in nn_table)

    model = RoMaV2(RoMaV2.Cfg(compile=False))
    model.apply_setting("base")
    model.to(torch.device(args.device)).eval()
    torch.set_float32_matmul_precision("highest")
    raw = {}

    class Matcher:
        sample_thresh = 0.9
        w_resized = h_resized = model.W_lr

        def match_grids_batch(self, imA, imBs):
            out = []
            for imB in imBs:
                with torch.inference_mode():
                    p = model.match(imA, imB)
                warp = p["warp_AB"][0].float().cpu()
                cert = p["overlap_AB"][0, ..., 0].float().cpu()
                h, w = cert.shape
                yy = torch.linspace(-1 + 1 / h, 1 - 1 / h, h)
                xx = torch.linspace(-1 + 1 / w, 1 - 1 / w, w)
                yy, xx = torch.meshgrid(yy, xx, indexing="ij")
                out.append((torch.cat([torch.stack([xx, yy], -1), warp], -1).contiguous(), cert.contiguous()))
                raw.setdefault("last", []).append((warp.numpy(), cert.numpy()))
            return out

    cfg = DensePipelineConfig(output_path=str(tmp / "x.ply"), roma_setting="base", nns_per_ref=nns)
    np.random.seed(cfg.seed)
    cams = PL._build_camera_lookup(recs)
    pack = PL._PackContext(cameras=cams, nn_table=nn_table, nns_per_ref=nns, w_match=Matcher.w_resized,
                           h_match=Matcher.h_resized)
    tri = PL._TriangulationContext(cameras=cams, config=cfg, matcher_sample_cap=Matcher.sample_thresh,
                                   w_match=Matcher.w_resized, h_match=Matcher.h_resized)
    if args.f64_dlt:
        def dlt64(P1, P2, uv1, uv2):
            P1, P2, uv1, uv2 = (np.asarray(a, np.float64) for a in (P1, P2, uv1, uv2))
            A = np.stack([uv1[:, 0:1] * P1[2] - P1[0], uv1[:, 1:2] * P1[2] - P1[1],
                          uv2[:, 0:1] * P2[2] - P2[0], uv2[:, 1:2] * P2[2] - P2[1]], axis=1)
            if A.shape[0] == 0:
                return np.zeros((0, 4))
            Xh = np.linalg.svd(A)[2][:, -1, :]
            w = np.where(np.abs(Xh[:, 3:4]) < 1e-12, 1e-12, Xh[:, 3:4])
            return Xh / w
        PL.dlt_triangulate_batch = dlt64
    # Which samples each neighbour leaves a candidate for: one recording line in a
    # copy of _triangulate_ref, so the C++ side can tell a threshold edge (a
    # different candidate set) from arithmetic.
    import inspect
    src = inspect.getsource(PL._triangulate_ref)
    hook = "        e = err[keep].astype(np.float32)\n"
    assert src.count(hook) == 1
    src = src.replace(hook, hook + "        _CANDIDATES.append((int(nbr_id), kept_idxs.copy(), Xw.copy(), e.copy()))\n")
    PL._CANDIDATES = []
    exec(compile(src, PL.__file__, "exec"), PL.__dict__)
    sampler = PL.select_samples_with_coverage
    recorded = {}

    def recording(*a, **k):
        s = sampler(*a, **k)
        recorded["sel"] = np.asarray(s, np.int64)
        return s
    PL.select_samples_with_coverage = recording

    args.out.mkdir(parents=True, exist_ok=True)
    meta = {"w_match": Matcher.w_resized, "h_match": Matcher.h_resized,
            "config": {k: v for k, v in cfg.__dict__.items() if isinstance(v, (int, float, str, bool))},
            "views": {n: {"w": W, "h": H, "K": [fx, fy, cx, cy], "R": poses[n][0].tolist(),
                          "t": poses[n][1].tolist()} for n in names},
            "refs": []}
    for r in refs:
        raw["last"] = []
        PL._CANDIDATES.clear()
        packed = PL._pack_reference_batch(idx[r], pack)
        matched, _ = PL._collect_reference_matches(packed, Matcher(), cfg, 0, None)
        out = PL._triangulate_ref(matched, tri)
        d = args.out / r
        d.mkdir(exist_ok=True)
        nb = [names[u] for u in packed.nn_ids]
        for k, n in enumerate(nb):
            write_rwm(d / f"raw_{k}.rwm", *raw["last"][k])
            np.save(d / f"maskB_{k}.npy", packed.nn_masks[k].astype(np.uint8))
            np.save(d / f"cert_{k}.npy", matched.cert_list_cpu[k].numpy().astype(np.float32))
        np.save(d / "maskA.npy", packed.maskA_np.astype(np.uint8))
        for nbr_id, kept, cx, ce in PL._CANDIDATES:
            k = packed.nn_ids.index(nbr_id)
            np.save(d / f"cand_{k}.npy", np.asarray(kept, np.int64))
            np.save(d / f"candX_{k}.npy", np.asarray(cx, np.float32))
            np.save(d / f"candE_{k}.npy", np.asarray(ce, np.float32))
        np.save(d / "imA.npy", packed.imA_np.astype(np.uint8))
        np.save(d / "sel.npy", recorded["sel"])
        if out is None:
            xyz, err, rgb, tracks = np.zeros((0, 3), np.float32), np.zeros(0, np.float32), np.zeros((0, 3), np.float32), []
        else:
            xyz, err, rgb, tracks = out.xyz, out.err, out.rgb, out.tracks
        np.save(d / "xyz.npy", np.asarray(xyz, np.float32))
        np.save(d / "err.npy", np.asarray(err, np.float32))
        np.save(d / "rgb.npy", np.asarray(rgb, np.float32))
        (d / "tracks.json").write_text(json.dumps([[[names[int(i)], float(x), float(y)] for i, x, y in t] for t in tracks]))
        meta["refs"].append({"ref": r, "nbrs": nb, "points": int(len(xyz)), "samples": int(len(recorded["sel"]))})
        print(r, "samples", len(recorded["sel"]), "points", len(xyz), flush=True)
    (args.out / "meta.json").write_text(json.dumps(meta, indent=1))
    write_text(args.out)


if __name__ == "__main__":
    main()
