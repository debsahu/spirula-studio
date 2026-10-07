"""Writes src/nn/tests/data/*.pt, the torch.save fixtures nn/tests/torch_pickle_test.cpp
reads. Dev-time only; the outputs are committed so a checkout needs no torch.

    uv run --with torch python tools/make_torch_fixtures.py
"""
import collections
import pathlib

import torch

out = pathlib.Path(__file__).resolve().parent.parent / "src/nn/tests/data"
out.mkdir(parents=True, exist_ok=True)

# The two-tensor fixture: one f32 and one bf16 (0.1 is not representable, so
# the f32 widening of its bf16 rounding is 0.10009765625, not 0.1).
two = collections.OrderedDict()
two["a.weight"] = (torch.arange(6, dtype=torch.float32) * 0.5 + 0.25).reshape(2, 3)
two["b.bias"] = torch.tensor([1.5, -2.25, 0.1, 65536.0, -0.0], dtype=torch.bfloat16)
torch.save(two, out / "two_tensors.pt")

# Everything else a real state dict can hold that the reader must get right: a
# view into a shared storage with a storage offset, a bool buffer, an int64, an
# f16, a 0-d tensor, an empty tensor, and a {"state_dict": ...} wrapper.
base = torch.arange(24, dtype=torch.float32)
sd = collections.OrderedDict()
sd["view.tail"] = base[10:16].reshape(2, 3)
sd["view.head"] = base[:4]
sd["mask"] = torch.tensor([True, False, True])
sd["idx"] = torch.tensor([1, -2, 3_000_000_000], dtype=torch.int64)
sd["half"] = torch.tensor([0.5, -1.0, 3.0], dtype=torch.float16)
sd["scalar"] = torch.tensor(7.25)
sd["empty"] = torch.zeros(0, 4)
torch.save({"state_dict": sd}, out / "wrapped_misc.pt")

# Not contiguous: the reader must refuse rather than read it as if it were.
torch.save(collections.OrderedDict(t=torch.arange(6.0).reshape(2, 3).t()),
           out / "transposed.pt")


# A pickle whose REDUCE would call os.system if anything unpickled it. The
# reader has to refuse it by name and never get as far as calling anything.
import os


class Evil:
    def __reduce__(self):
        return (os.system, ("true",))


torch.save(collections.OrderedDict(t=torch.zeros(2), e=Evil()), out / "evil_reduce.pt")


# --table <romav2.0.1.pt>: one line per tensor, in file order, "name dtype shape
# sum abs_sum" (shape x-joined, "-" for 0-d; sums in float64).
import sys

if len(sys.argv) == 3 and sys.argv[1] == "--table":
    names = {torch.float32: "F32", torch.bfloat16: "BF16", torch.float16: "F16",
             torch.int64: "I64", torch.bool: "BOOL", torch.float64: "F64",
             torch.int32: "I32", torch.uint8: "U8"}
    sd = torch.load(sys.argv[2], map_location="cpu", weights_only=True)
    if "state_dict" in sd and not torch.is_tensor(sd["state_dict"]):
        sd = sd["state_dict"]
    with open(out / "romav2_0_1_tensors.txt", "w") as f:
        for k, t in sd.items():
            d = names[t.dtype]
            v = t.to(torch.float64) if t.dtype != torch.bool else t.to(torch.float64)
            shape = "x".join(map(str, t.shape)) or "-"
            f.write(f"{k} {d} {shape} {v.sum().item():.17g} {v.abs().sum().item():.17g}\n")
    print(len(sd), "tensors")
