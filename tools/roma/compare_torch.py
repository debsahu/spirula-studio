#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "torch>=2.4", "torchvision", "numpy", "einops", "pillow",
#   "romav2 @ git+https://github.com/Parskatt/RoMaV2.git@95c9968145c8906b7b59383258e9f73b02853d89",
# ]
# ///
"""Compare src/roma/'s matcher against upstream RoMa v2 on the same bytes.

roma_test writes the [0, 1] images it ran on beside every stage
(SS_ROMA_DUMP=<dir>), so the resampler and the image decoder are outside the
numbers. This feeds those bytes to upstream RoMaV2 at the pinned commit and
prints one row per stage with its bar:

    SS_ROMA_F32_WEIGHTS=1 SS_ROMA_DUMP=/tmp/d ./build/roma_test --pair A.png B.png
    uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32

`--ref fp32` (the default) is the parity reference: every autocast disabled,
including the one dpt.py hard-codes, so what remains is the model's own
arithmetic. The multi-view transformer's RoPE is bf16 BY CONSTRUCTION
(`pos_embed_rope_dtype="bf16"`, q and k are cast to it), so it stays bf16 here
too, and our side emulates it. `--ref bf16` runs the model as shipped, for
context only; `--ref rope32` swaps that RoPE to fp32, which is how much the
emulation is worth.

Exit status is 1 when any gated row misses its bar.
"""
import argparse
import contextlib
import json
import os
import sys

import numpy as np

CKPT_URL = "https://github.com/Parskatt/RoMaV2/releases/download/v2.0.1/romav2.0.1.pt"

# Plan section 9.1, P-1 and P-2, at the two weight precisions.
BARS = {
    "f16": dict(p1=2e-3, p2_p50=0.05, p2_p99=0.5, p2_logit=2e-2),
    "f32": dict(p1=1e-4, p2_p50=0.01, p2_p99=0.1, p2_logit=1e-3),
}


def load(dump, name):
    p = os.path.join(dump, name + ".npy")
    return np.load(p) if os.path.exists(p) else None


def rel_l2(a, b):
    a = a.astype(np.float64).ravel()
    b = b.astype(np.float64).ravel()
    return float(np.linalg.norm(a - b) / max(np.linalg.norm(b), 1e-30))


def build_model(ref, device):
    import torch
    import romav2.dpt
    # romav2.device picks MPS or CUDA at import; every module bound that name
    # at its own import, so each copy is repointed.
    dev = torch.device(device)
    for name, mod in list(sys.modules.items()):
        if name.startswith("romav2") and isinstance(getattr(mod, "device", None),
                                                    torch.device):
            mod.device = dev
    from romav2 import RoMaV2
    from romav2.features import Descriptor
    from romav2.matcher import Matcher

    amp = ref == "bf16"
    cfg = RoMaV2.Cfg(descriptor=Descriptor.Cfg(enable_amp=amp),
                     matcher=Matcher.Cfg(enable_amp=amp), setting="base")
    if not amp:
        # dpt.py enters torch.autocast(bf16) unconditionally; route its
        # module-level `torch` through a proxy whose autocast does nothing.
        class _NoAutocast:
            def __getattr__(self, k):
                return getattr(torch, k)

            @staticmethod
            def autocast(*a, **k):
                return contextlib.nullcontext()

        romav2.dpt.torch = _NoAutocast()
    model = RoMaV2(cfg)
    if ref == "rope32":
        rope = model.matcher.mv_vit.rope_embed
        rope.dtype = torch.float32
        rope.periods.data = rope.periods.data.float()
    model.eval()
    return model


def run_reference(model, img_A, img_B):
    """Every stage our side dumps, from upstream modules, keyed by our names."""
    import torch

    out = {}
    hooks = []

    def keep(name):
        def h(_m, _i, o):
            # Cloned: a later in-place ReLU would rewrite a shared buffer.
            out[name] = o.detach().permute(0, 2, 3, 1)[0].float().cpu().numpy().copy()
        return h

    head = model.matcher.head
    for i in range(4):
        hooks.append(head.resize_layers[i].register_forward_hook(keep(f"dpt_l{i + 1}")))
    for i in range(1, 5):
        hooks.append(getattr(head.scratch, f"refinenet{i}").register_forward_hook(
            keep(f"dpt_rn{i}")))
    hooks.append(head.scratch.output_conv1.register_forward_hook(keep("dpt_conv1")))

    def mv_hook(_m, _i, o):
        x = o["x_norm_patchtokens"].detach().float().cpu().numpy()[0].copy()
        out["mv_tokens"] = x
    hooks.append(model.matcher.mv_vit.register_forward_hook(mv_hook))

    with torch.inference_mode():
        f_A = model.f(img_A)
        f_B = model.f(img_B)
        for tag, f in (("A", f_A), ("B", f_B)):
            out[f"dino_tap11_{tag}"] = f[0][0].float().cpu().numpy().copy()
            out[f"dino_tap17_{tag}"] = f[1][0].float().cpu().numpy().copy()
        for tag, img in (("A", img_A), ("B", img_B)):
            v = model.refiner_features(img)
            for s, t in v.items():
                out[f"vgg_s{s}_{tag}"] = t[0].float().cpu().numpy().copy()
        m = model.matcher(list(f_A), list(f_B), img_A=img_A, img_B=img_B,
                          bidirectional=False)
    for h in hooks:
        h.remove()
    h, w = out["dino_tap11_A"].shape[:2]
    for name, rope in (("rope_backbone", model.f.rope_embed),
                       ("rope_matcher", model.matcher.mv_vit.rope_embed)):
        with torch.inference_mode():
            sin, cos = rope(H=h, W=w)
        k = sin.shape[-1] // 2
        out[name] = torch.stack((cos[:, :k], sin[:, :k]), -1).float().cpu().numpy()
    mv = out.pop("mv_tokens").reshape(2, h, w, -1)
    out["mv_A"], out["mv_B"] = mv[0], mv[1]
    out["sim_AB"] = m["attn_AB_logits"][0].reshape(h * w, h * w).float().cpu().numpy()
    out["dpt_out_AB"] = torch.cat((m["warp_AB"], m["confidence_AB"]), -1)[0] \
        .float().cpu().numpy()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", required=True, help="SS_ROMA_DUMP directory of a roma_test run")
    ap.add_argument("--weights", choices=["f16", "f32"], required=True,
                    help="what roma_test held the backbone in (SS_ROMA_F32_WEIGHTS)")
    ap.add_argument("--ref", choices=["fp32", "bf16", "rope32"], default="fp32")
    ap.add_argument("--save-ref", help="also write the reference stages here as .npy")
    ap.add_argument("--json", help="write the table here")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--device", default="cpu", help="torch device for the reference")
    ap.add_argument("--perturb", type=float, default=0.0,
                    help="add N(0, s) to the inputs: the reference's own noise floor")
    a = ap.parse_args()

    import torch
    torch.set_float32_matmul_precision("highest")
    if a.threads:
        torch.set_num_threads(a.threads)

    A, B = load(a.dump, "input_A"), load(a.dump, "input_B")
    if A is None or B is None:
        sys.exit(f"{a.dump}: no input_A.npy / input_B.npy; was SS_ROMA_DUMP set?")
    img_A = torch.from_numpy(A).permute(2, 0, 1)[None].float().to(a.device)
    img_B = torch.from_numpy(B).permute(2, 0, 1)[None].float().to(a.device)

    if a.perturb:
        g = torch.Generator().manual_seed(0)
        img_A = img_A + a.perturb * torch.randn(img_A.shape, generator=g).to(img_A)
        img_B = img_B + a.perturb * torch.randn(img_B.shape, generator=g).to(img_B)
    model = build_model(a.ref, a.device)
    ref = run_reference(model, img_A, img_B)
    if a.save_ref:
        os.makedirs(a.save_ref, exist_ok=True)
        for k, v in ref.items():
            np.save(os.path.join(a.save_ref, k + ".npy"), v)

    bars = BARS[a.weights]
    rows, failed = [], False

    def row(stage, metric, value, bar):
        nonlocal failed
        ok = None if bar is None else bool(value <= bar)
        if ok is False:
            failed = True
        rows.append(dict(stage=stage, metric=metric, value=value, bar=bar, ok=ok))

    def missing(name):
        nonlocal failed
        failed = True
        rows.append(dict(stage=name, metric="missing from our dump", value=float("nan"),
                         bar=None, ok=False))

    # The matcher's bf16 table must be bit-exact: one flipped rounding there is
    # amplified by the bf16 rotation it feeds.
    for name, bar in (("rope_backbone", 1e-6), ("rope_matcher", 0.0)):
        ours = load(a.dump, name)
        if ours is None:
            missing(name)
            continue
        row(name, "max abs", float(np.abs(ours - ref[name]).max()), bar)

    p1 = []
    for name in ("dino_tap11_A", "dino_tap17_A", "dino_tap11_B", "dino_tap17_B"):
        ours = load(a.dump, name)
        if ours is None:
            missing(name)
            continue
        e = rel_l2(ours, ref[name])
        p1.append(e)
        row(name, "rel L2", e, None)
    if p1:
        row("P-1 DINOv3 taps", "max rel L2", max(p1), bars["p1"])

    # Not gated by the plan; reported so a regression in either is visible.
    for name in ("vgg_s1_A", "vgg_s2_A", "vgg_s4_A", "vgg_s1_B", "vgg_s2_B", "vgg_s4_B",
                 "mv_A", "mv_B", "sim_AB", "dpt_l1", "dpt_l2", "dpt_l3", "dpt_l4",
                 "dpt_rn4", "dpt_rn3", "dpt_rn2", "dpt_rn1", "dpt_conv1"):
        ours = load(a.dump, name)
        if ours is None:
            missing(name)
            continue
        row(name, "rel L2", rel_l2(ours, ref[name]), None)

    ours = load(a.dump, "dpt_out_AB")
    if ours is None:
        missing("dpt_out_AB")
    else:
        r = ref["dpt_out_AB"]
        H, W = A.shape[:2]
        # Warp is normalized [-1, 1]; one unit is half the image, so px at the
        # 640 match resolution is the delta times (W/2, H/2).
        d = (ours[..., :2] - r[..., :2]) * np.array([W / 2, H / 2])
        epe = np.sqrt((d.astype(np.float64) ** 2).sum(-1)).ravel()
        row("P-2 coarse warp", "EPE p50 px", float(np.percentile(epe, 50)), bars["p2_p50"])
        row("P-2 coarse warp", "EPE p99 px", float(np.percentile(epe, 99)), bars["p2_p99"])
        row("P-2 coarse warp", "EPE max px", float(epe.max()), None)
        row("P-2 overlap logit", "max abs",
            float(np.abs(ours[..., 2] - r[..., 2]).max()), bars["p2_logit"])

    print(f"ref={a.ref} weights={a.weights} dump={a.dump}")
    for r_ in rows:
        bar = "" if r_["bar"] is None else f"<= {r_['bar']:.3g}"
        flag = "" if r_["ok"] is None else ("PASS" if r_["ok"] else "FAIL")
        print(f"  {r_['stage']:<20} {r_['metric']:<22} {r_['value']:.3e} {bar:<12} {flag}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(dict(ref=a.ref, weights=a.weights, rows=rows), f, indent=1)
    print("FAIL" if failed else "PASS")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
