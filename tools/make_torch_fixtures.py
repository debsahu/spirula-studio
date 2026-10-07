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


# ---- hostile files: written by hand, no torch, one defect each ------------
# nn/tests/torch_pickle_hostile_test.cpp refuses every one of these by name.
import struct
import zipfile

hostile = out / "hostile"
hostile.mkdir(exist_ok=True)


def _s(x):
    b = x.encode()
    return b"\x8c" + bytes([len(b)]) + b


def _glob(m, n):
    return b"c" + m.encode() + b"\n" + n.encode() + b"\n"


def _i(n):
    return b"J" + struct.pack("<i", n) if -(2**31) <= n < 2**31 else b"\x8a\x08" + struct.pack("<q", n)


def _tup(*xs):
    return b"(" + b"".join(xs) + b"t"


def _tensor(key, numel, off, shape, stride, cls="torch.FloatStorage"):
    pid = b"(" + _s("storage") + _glob(*cls.rsplit(".", 1)) + _s(key) + _s("cpu") + _i(numel) + b"t"
    return (_glob("torch._utils", "_rebuild_tensor_v2") + b"(" + pid + b"Q" + _i(off)
            + _tup(*[_i(x) for x in shape]) + _tup(*[_i(x) for x in stride]) + b"\x89"
            + _glob("collections", "OrderedDict") + b")R" + b"tR")


def _pkl(items, tail=b""):
    p = b"\x80\x02" + _glob("collections", "OrderedDict") + b")R("
    for k, v in items:
        p += _s(k) + v
    return p + b"u" + tail + b"."


def _zip(name, pk, storages, pkl_method=zipfile.ZIP_STORED, storage_method=zipfile.ZIP_STORED):
    with zipfile.ZipFile(hostile / name, "w") as z:
        z.writestr("a/data.pkl", pk, compress_type=pkl_method)
        for k, b in storages.items():
            z.writestr("a/data/" + k, b, compress_type=storage_method)


F4 = struct.pack("<4f", 1, 2, 3, 4)
_zip("stride_short.pt", _pkl([("w", _tensor("0", 4, 0, [4], []))]), {"0": F4})
_zip("off_overflow.pt", _pkl([("w", _tensor("0", 4, 2**63 - 1, [1], [1]))]), {"0": F4})
_zip("numel_wrap.pt", _pkl([("w", _tensor("0", 4, 0, [2**32, 2**32], [2**32, 1]))]), {"0": F4})
_zip("off_wrap_read.pt", _pkl([("w", _tensor("0", 4, 2**62 - 1, [4], [1]))]), {"0": F4})
_zip("wrap_unsigned.pt",
     _pkl([("w", _tensor("0", 2**62, 2**62 - 1, [1], [1], "torch.DoubleStorage"))]),
     {"0": struct.pack("<2d", 1.0, 2.0)})
_zip("storage_lies.pt", _pkl([("w", _tensor("0", 1000, 0, [1000], [1]))]), {"0": F4})
# Honest storage claim, tensor reaches one element past it.
_zip("storage_bounds.pt", _pkl([("w", _tensor("0", 4, 1, [4], [1]))]), {"0": F4})
_zip("dup_name.pt", _pkl([("w", _tensor("0", 4, 0, [4], [1])), ("w", _tensor("0", 4, 0, [4], [1]))]),
     {"0": F4})
# BUILD on a dict with a state that is not a dict.
_zip("build_kind.pt", _pkl([("w", _tensor("0", 4, 0, [4], [1]))], tail=b"N" + b"b"), {"0": F4})
_zip("unknown_opcode.pt", b"\x80\x02\xff.", {})
_zip("deflated_storage.pt", _pkl([("w", _tensor("0", 4, 0, [4], [1]))]), {"0": F4},
     storage_method=zipfile.ZIP_DEFLATED)
# two_tensors.pt with every storage cut to one byte: what a file swapped in after
# open() looks like to read_raw, whose bounds check the constructor cannot cover.
with zipfile.ZipFile(out / "two_tensors.pt") as src, zipfile.ZipFile(hostile / "shrunk_storage.pt", "w") as dst:
    for info in src.infolist():
        data = src.read(info.filename)
        dst.writestr(info.filename, data[:1] if "/data/" in info.filename else data)


# 2M nested TUPLE1: freed recursively it overflows the stack. Deflated: 2 KB.
_zip("deep.pt", b"\x80\x02N" + b"\x85" * 2_000_000 + b".", {}, zipfile.ZIP_DEFLATED)
# 50M `N`: 48 KB on disk, 5.6 GB of objects without a bound. Over the pickle cap.
_zip("amp.pt", b"\x80\x02" + b"N" * 50_000_000 + b".", {}, zipfile.ZIP_DEFLATED)
# 3M `N`: under the pickle cap, over the object cap.
_zip("many_objects.pt", b"\x80\x02" + b"N" * 3_000_000 + b".", {}, zipfile.ZIP_DEFLATED)

# Memo / stack amplification, none of it counted by the mk() object cap. Each is
# ~30 MB of pickle (under the 32 MiB cap), a few KB deflated.
# 6M MEMOIZE: each one a new distinct memo key, one std::map node apiece, 1 byte each
# (LONG_BINPUT with distinct keys is the same defect at 5 bytes, but does not deflate).
_zip("memo_keys.pt", b"\x80\x02N" + b"\x94" * 6_000_000 + b".", {}, zipfile.ZIP_DEFLATED)
# 15M BINGET of one memo entry: a stack slot each, no new object.
_zip("binget_stack.pt", b"\x80\x02Nq\x00" + b"h\x00" * 15_000_000 + b".", {}, zipfile.ZIP_DEFLATED)
# 6M LONG_BINGET of one memo entry.
_zip("long_binget_stack.pt", b"\x80\x02Nq\x00" + b"j\x00\x00\x00\x00" * 6_000_000 + b".",
     {}, zipfile.ZIP_DEFLATED)


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
