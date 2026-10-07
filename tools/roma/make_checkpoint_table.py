#!/usr/bin/env python3
"""The table roma_checkpoint_test reads: one line per tensor of romav2.0.1.pt, in
file order, "name dtype shape sum abs_sum" (shape x-joined, "-" for 0-d; sums in
float64), written to src/roma/model/tests/data/romav2_0_1_tensors.txt.

    uv run --with torch python tools/roma/make_checkpoint_table.py <romav2.0.1.pt>
"""
import pathlib
import sys

import torch

out = pathlib.Path(__file__).resolve().parents[2] / "src/roma/model/tests/data"
names = {torch.float32: "F32", torch.bfloat16: "BF16", torch.float16: "F16",
         torch.int64: "I64", torch.bool: "BOOL", torch.float64: "F64",
         torch.int32: "I32", torch.uint8: "U8"}
sd = torch.load(sys.argv[1], map_location="cpu", weights_only=True)
if "state_dict" in sd and not torch.is_tensor(sd["state_dict"]):
    sd = sd["state_dict"]
with open(out / "romav2_0_1_tensors.txt", "w") as f:
    for k, t in sd.items():
        v = t.to(torch.float64)
        shape = "x".join(map(str, t.shape)) or "-"
        f.write(f"{k} {names[t.dtype]} {shape} {v.sum().item():.17g} {v.abs().sum().item():.17g}\n")
print(len(sd), "tensors")
