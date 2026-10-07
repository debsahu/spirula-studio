#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["torch>=2.4", "numpy"]
# ///
"""The kDown / kUp goldens of src/roma/model/tests/roma_unit_test.cpp.

F.interpolate(bicubic, antialias=True, align_corners=False) of the test's LCG
bytes, which RoMaV2.match() applies to its 8-bit input. Prints the two C++
arrays; with --check, compares them with the ones in the test (exit 1 on a
difference).

    uv run tools/roma/make_unit_goldens.py [--check]
"""
import re
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

CASES = {"kDown": (13, 11, 5, 4, 7), "kUp": (6, 5, 11, 9, 11)}   # w, h, ow, oh, seed
TEST = Path(__file__).resolve().parents[2] / "src/roma/model/tests/roma_unit_test.cpp"


def lcg(n, seed):
    out = np.empty(n, np.uint8)
    x = seed
    for i in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out[i] = (x >> 16) & 255
    return out


def golden(w, h, ow, oh, seed):
    img = lcg(w * h * 3, seed).reshape(h, w, 3)
    t = torch.from_numpy(img).permute(2, 0, 1)[None].float() / 255
    o = F.interpolate(t, size=(oh, ow), mode="bicubic", antialias=True, align_corners=False)
    return o[0].permute(1, 2, 0).reshape(-1).numpy()


def main():
    check = "--check" in sys.argv[1:]
    bad = 0
    for name, args in CASES.items():
        g = golden(*args)
        if check:
            m = re.search(r"const float " + name + r"\[\] = \{([^}]*)\}", TEST.read_text())
            want = np.array([float(v.strip().rstrip("f")) for v in m.group(1).split(",")], np.float32)
            d = float(np.abs(want - g).max()) if want.shape == g.shape else float("inf")
            print(f"{name}: {g.size} values, max |test - torch {torch.__version__}| {d:.2e}")
            bad += d > 1e-7
        else:
            w, h, ow, oh, seed = args
            body = ", ".join(f"{v:.9g}f" for v in g)
            print(f"const float {name}[] = {{{body}}};   // {w}x{h} -> {ow}x{oh}, seed {seed}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
