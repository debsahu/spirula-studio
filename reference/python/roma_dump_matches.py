#!/usr/bin/env python3
"""Match the pairs `spirula densify --export-pairs` wrote, with upstream RoMa v2.

    spirula densify <dataset> --export-pairs <dir>
    python3 reference/python/roma_dump_matches.py <dir> [--setting base] [--encoding 1]
    spirula densify <dataset> --matches <dir>/matches

Writes <dir>/matches/<A>__<B>.rwm per line of <dir>/pairs.txt, in the format
src/roma/DumpMatcher.h reads: RoMa's warp_AB and overlap_AB, A -> B, at its
output resolution. This is what the densify host stage was validated on before
the Vulkan matcher existed, and what that matcher is compared against.

Requires torch and RoMaV2 (github.com/Parskatt/RoMaV2, MIT) installed from
source; the checkpoint carries Meta's DINOv3 weights under the DINOv3 License.
"""
from __future__ import annotations

import argparse
import struct
import time
from pathlib import Path

import numpy as np
import torch
from PIL import Image


def write_rwm(path: Path, warp: np.ndarray, cert: np.ndarray, encoding: int) -> None:
    h, w = cert.shape
    with open(path, "wb") as f:
        f.write(b"RWM1")
        f.write(struct.pack("<iii", w, h, encoding))
        if encoding == 0:
            f.write(np.ascontiguousarray(warp, dtype="<f4").tobytes())
            f.write(np.ascontiguousarray(cert, dtype="<f4").tobytes())
        else:
            q = np.round(np.clip(warp, -1, 1) * 32767).astype("<i2")
            c = np.round(np.clip(cert, 0, 1) * 65535).astype("<u2")
            f.write(q.tobytes())
            f.write(c.tobytes())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("export_dir", type=Path)
    ap.add_argument("--setting", default="base")
    ap.add_argument("--encoding", type=int, default=1, choices=(0, 1))
    ap.add_argument("--device", default="mps" if torch.backends.mps.is_available() else "cpu")
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    from romav2 import RoMaV2

    out = args.out or args.export_dir / "matches"
    out.mkdir(parents=True, exist_ok=True)
    pairs = [l.split() for l in (args.export_dir / "pairs.txt").read_text().splitlines() if l.strip()]
    torch.set_float32_matmul_precision("highest")
    model = RoMaV2(RoMaV2.Cfg(compile=False))
    model.apply_setting(args.setting)
    model.to(torch.device(args.device)).eval()
    t0, done = time.time(), 0
    for a, b in pairs:
        dst = out / f"{a}__{b}.rwm"
        if dst.exists():
            continue
        ima = Image.open(args.export_dir / "views" / f"{a}.png").convert("RGB")
        imb = Image.open(args.export_dir / "views" / f"{b}.png").convert("RGB")
        with torch.inference_mode():
            p = model.match(ima, imb)
        warp = p["warp_AB"][0].float().cpu().numpy()
        cert = p["overlap_AB"][0, ..., 0].float().cpu().numpy()
        tmp = dst.with_suffix(".part")
        write_rwm(tmp, warp, cert, args.encoding)
        tmp.rename(dst)
        done += 1
        if done % 50 == 0:
            print(f"{done}/{len(pairs)} pairs, {(time.time() - t0) / done:.2f} s/pair", flush=True)
    print(f"done: {done} matched, {len(pairs)} pairs, {time.time() - t0:.0f} s", flush=True)


if __name__ == "__main__":
    main()
