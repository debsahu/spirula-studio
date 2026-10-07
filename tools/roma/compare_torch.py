#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#   "torch>=2.4", "torchvision", "numpy", "einops", "pillow",
#   "romav2 @ git+https://github.com/Parskatt/RoMaV2.git@95c9968145c8906b7b59383258e9f73b02853d89",
# ]
# ///
"""Compare src/roma/model/ against upstream RoMa v2 on the same bytes.

roma_model_test writes the [0, 1] images it ran on beside every stage
(SS_ROMA_DUMP=<dir>), so the resampler and the image decoder are outside the
numbers. This feeds those bytes to upstream RoMaV2 at the pinned commit and
prints one row per stage with its bar:

    SS_ROMA_DUMP=/tmp/d ./build/roma_model_test --require-model --pair A.png B.png &&
    uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32

The dump must carry the manifest.json of a run that finished with status 0,
and only the files that run wrote are read: a stale or failed dump is refused.

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
import hashlib
import json
import os
import sys
import tempfile

import numpy as np

# The checkpoint upstream downloads, which is the one src/roma/model/Fetch.h pins.
CKPT_FILE = "romav2.0.1.pt"
CKPT_SHA256 = "1557dec0d21b62366465f7ff4d5fdf228cc695d0582e196ad2b80e05230828b7"

# The floor is the worst of this many draws of torch against itself: one on a
# second device and the rest perturbed. Fewer is too few to call a worst case.
MIN_FLOOR_DRAWS = 4

# The bf16 RoPE makes torch miss the coarse and full-match bars against itself,
# so each is widened to FLOOR_FACTOR x the floor --floor-device measures (README);
# --ref rope32, with the rounding off on both sides, is held to them unwidened.
FLOOR_FACTOR = 2.0
BARS = {
    "f16": dict(taps=2e-3, coarse_p50=0.05, coarse_p99=0.5, coarse_logit=2e-2,
                full_p50=0.05, full_p99=0.5, full_cert=2e-2, full_cross=0.005, full_prec=1e-2),
    "f32": dict(taps=1e-4, coarse_p50=0.01, coarse_p99=0.1, coarse_logit=1e-3,
                full_p50=0.01, full_p99=0.1, full_cert=2e-3, full_cross=0.0005, full_prec=1e-2),
}


MANIFESTS = {}


def require_manifest(dump):
    """The files the run that wrote `dump` listed, after checking it ended well."""
    path = os.path.join(dump, "manifest.json")
    if not os.path.exists(path):
        sys.exit(f"{dump}: no manifest.json; not a dump this tree's roma_model_test wrote")
    with open(path) as f:
        m = json.load(f)
    if not m.get("finished"):
        sys.exit(f"{dump}: the run that wrote it never finished (crashed or still running)")
    if m.get("exit_status") != 0:
        sys.exit(f"{dump}: the run that wrote it exited {m.get('exit_status')}")
    MANIFESTS[os.path.abspath(dump)] = set(m["files"])
    return m


def check_settings(m, weights, ref):
    """The run's own record of what it ran with must agree with how it is scored."""
    for key in ("f16_weights", "rope_rounds", "local_corr_fused"):
        if not isinstance(m.get(key), bool):
            sys.exit(f"manifest.json records no {key}: not a dump of this tree's "
                     "roma_model_test, so the --weights label cannot be checked")
    if m["f16_weights"] != (weights == "f16"):
        sys.exit(f"--weights {weights}, but the run held the backbone in "
                 f"{'f16' if m['f16_weights'] else 'f32'} (manifest.json f16_weights)")
    if m["rope_rounds"] != (ref != "rope32"):
        sys.exit(f"--ref {ref}, but the run's matcher RoPE "
                 f"{'rounds to bf16' if m['rope_rounds'] else 'is fp32'} "
                 "(manifest.json rope_rounds; SS_ROMA_ROPE_F32=1 makes it fp32)")


def write_manifest(dump, files, nonce):
    with open(os.path.join(dump, "manifest.json"), "w") as f:
        json.dump(dict(nonce=nonce, finished=True, exit_status=0, files=sorted(files)), f)


def load(dump, name):
    files = MANIFESTS.get(os.path.abspath(dump))
    if files is None:
        sys.exit(f"internal: {dump} read before require_manifest()")
    p = os.path.join(dump, name + ".npy")
    return np.load(p) if name in files and os.path.exists(p) else None


def rel_l2(a, b):
    a = a.astype(np.float64).ravel()
    b = b.astype(np.float64).ravel()
    return float(np.linalg.norm(a - b) / max(np.linalg.norm(b), 1e-30))


_CKPT_OK = False


def verify_checkpoint():
    """The weights upstream loaded are the ones the C++ side pins by SHA-256."""
    global _CKPT_OK
    if _CKPT_OK:
        return
    import torch
    path = os.path.join(torch.hub.get_dir(), "checkpoints", CKPT_FILE)
    if not os.path.exists(path):
        sys.exit(f"{path}: upstream RoMaV2 loaded its weights from somewhere else; "
                 "cannot check them against the pinned checkpoint")
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    if h.hexdigest() != CKPT_SHA256:
        sys.exit(f"{path}: sha256 {h.hexdigest()}, not the pinned {CKPT_SHA256}")
    _CKPT_OK = True


def build_model(ref, device):
    import torch
    import romav2.dpt
    import romav2.features
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
        # dpt.py and the VGG in features.py enter torch.autocast(bf16)
        # unconditionally (the latter for "cuda"); route each module-level
        # `torch` through a proxy whose autocast does nothing.
        class _NoAutocast:
            def __getattr__(self, k):
                return getattr(torch, k)

            @staticmethod
            def autocast(*a, **k):
                return contextlib.nullcontext()

        romav2.dpt.torch = _NoAutocast()
        romav2.features.torch = _NoAutocast()
    model = RoMaV2(cfg)
    verify_checkpoint()
    if not amp:
        # Every refiner Block enters autocast(bf16) unless told not to.
        for refiner in model.refiners.values():
            for m in refiner.modules():
                if hasattr(m, "enable_amp"):
                    m.enable_amp = False
    if ref == "rope32":
        rope = model.matcher.mv_vit.rope_embed
        rope.dtype = torch.float32
        rope.periods.data = rope.periods.data.float()
    model.eval()
    return model


def run_reference(model, img_A, img_B, taps=None):
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
        if taps is not None:
            f_A, f_B = taps
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


def run_full(model, img_A, img_B, img_A_hr, img_B_hr, bidirectional):
    """RoMaV2.forward on the dumped bytes; every refiner call, keyed by our names."""
    import torch

    out = {}
    calls = {k: [] for k in model.refiners.keys()}
    hooks = []
    for k, refiner in model.refiners.items():
        def h(_m, _a, _kw, o, k=k):
            calls[k].append({n: t.detach().float().cpu().numpy()[0].copy() for n, t in o.items()})
        hooks.append(refiner.register_forward_hook(h, with_kwargs=True))
    H, W = img_A.shape[-2:]
    model.H_lr, model.W_lr = H, W
    model.H_hr, model.W_hr = (None, None) if img_A_hr is None else img_A_hr.shape[-2:]
    model.bidirectional = bidirectional
    with torch.inference_mode():
        p = model(img_A, img_B, img_A_hr=img_A_hr, img_B_hr=img_B_hr)
    for h in hooks:
        h.remove()
    m = p["matcher"]
    out["dpt_out_AB"] = torch.cat((m["warp_AB"], m["confidence_AB"]), -1)[0].float().cpu().numpy()
    if bidirectional:
        out["dpt_out_BA"] = torch.cat((m["warp_BA"], m["confidence_BA"]), -1)[0] \
            .float().cpu().numpy()
    dirs = ["AB", "BA"] if bidirectional else ["AB"]
    for k, lst in calls.items():
        per_stage = len(dirs)
        for i, o in enumerate(lst):
            stage = "lr" if i < per_stage else "hr"
            tag = f"{stage}_s{k}_{dirs[i % per_stage]}"
            out["refine_warp_" + tag] = o["warp"]
            out["refine_conf_" + tag] = o["confidence"]
    return out


def epe_px(ours, ref):
    h, w = ref.shape[:2]
    d = (ours[..., :2] - ref[..., :2]).astype(np.float64) * np.array([w / 2, h / 2])
    return np.sqrt((d ** 2).sum(-1)).ravel()


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x.astype(np.float64)))


def reference_full(a, dump, img, device):
    """RoMaV2.forward on the dumped lr (and hr) bytes, bidirectional when the dump is."""
    import torch
    A_hr, B_hr = load(dump, "input_A_hr"), load(dump, "input_B_hr")
    to = lambda x: torch.from_numpy(x).permute(2, 0, 1)[None].float().to(device)
    hr = (None, None) if A_hr is None else (to(A_hr), to(B_hr))
    if a.perturb:
        g = torch.Generator().manual_seed(a.perturb_seed + 1)
        hr = tuple(None if x is None else
                   x + a.perturb * torch.randn(x.shape, generator=g).to(x) for x in hr)
    bidir = load(dump, "refine_warp_lr_s1_BA") is not None
    model = build_model(a.ref, device)
    return run_full(model, img[0].to(device), img[1].to(device), hr[0], hr[1], bidir)


def compare_full(a, dump, ref, bars, floor_of, row, missing):
    """The final warp and certainty, with every refiner stage for bisecting."""
    bidir = "dpt_out_BA" in ref

    def bar(stage, metric, key):
        fl = floor_of.get((stage, metric))
        return bars[key] if fl is None else max(bars[key], FLOOR_FACTOR * fl)

    stages = [k for k in ref if k.startswith("refine_warp_")]
    final = {}
    for d in (["AB", "BA"] if bidir else ["AB"]):
        oc = load(dump, "dpt_out_" + d)
        if oc is None:
            missing("dpt_out_" + d)
        else:
            e = epe_px(oc, ref["dpt_out_" + d])
            row("coarse " + d, "EPE p50 px", float(np.percentile(e, 50)), None)
            row("coarse " + d, "logit max abs",
                float(np.abs(oc[..., 2] - ref["dpt_out_" + d][..., 2]).max()), None)
        last = [k for k in stages if k.endswith(d)]
        last = sorted(last, key=lambda k: (k.split("_")[2] == "hr", -int(k.split("_")[3][1:])))
        for k in last:
            tag = k[len("refine_warp_"):]
            ow, oc_ = load(dump, k), load(dump, "refine_conf_" + tag)
            if ow is None or oc_ is None:
                missing(k)
                continue
            e = epe_px(ow, ref[k])
            rc = ref["refine_conf_" + tag]
            row("refine " + tag, "EPE p50 px", float(np.percentile(e, 50)), None)
            row("refine " + tag, "EPE p99 px", float(np.percentile(e, 99)), None)
            row("refine " + tag, "cert max abs",
                float(np.abs(sigmoid(oc_[..., 0]) - sigmoid(rc[..., 0])).max()), None)
            final[d] = (tag, ow, oc_, ref[k], rc)
    for d, (tag, ow, oc_, rw, rc) in final.items():
        st = "full match " + d
        e = epe_px(ow, rw)
        row(st, "EPE p50 px", float(np.percentile(e, 50)), bar(st, "EPE p50 px", "full_p50"))
        row(st, "EPE p99 px", float(np.percentile(e, 99)), bar(st, "EPE p99 px", "full_p99"))
        row(st, "EPE max px", float(e.max()), None)
        co, cr = sigmoid(oc_[..., 0]), sigmoid(rc[..., 0])
        # Where the reference is unsure the warp is ill-posed and amplifies any
        # rounding; not gated, for reading a gated miss.
        sure = (cr > 0.5).ravel()
        if sure.any():
            row(st, "EPE p50 px, ref cert>0.5", float(np.percentile(e[sure], 50)),
                bar(st, "EPE p50 px, ref cert>0.5", "full_p50"))
            row(st, "EPE p99 px, ref cert>0.5", float(np.percentile(e[sure], 99)),
                bar(st, "EPE p99 px, ref cert>0.5", "full_p99"))
        row(st, "certainty max abs", float(np.abs(co - cr).max()),
            bar(st, "certainty max abs", "full_cert"))
        cross = np.zeros(co.shape, bool)
        for t in (0.2, 0.9):
            cross |= (co > t) != (cr > t)
        row(st, "cert crosses 0.2/0.9", float(cross.mean()),
            bar(st, "cert crosses 0.2/0.9", "full_cross"))
        # Precision (p00, p10, p11) is in match px^-2, relative to max(|ref|, 1),
        # gated at p99 where the reference is certain: elsewhere it is arbitrary,
        # and a max is one pixel on an occlusion edge (README).
        dp = np.abs(oc_[..., 1:] - rc[..., 1:]).astype(np.float64)
        rel = (dp / np.maximum(np.abs(rc[..., 1:]), 1.0)).reshape(-1, 3).max(-1)
        row(st, "precision max abs", float(dp.max()), None)
        row(st, "precision max rel", float(rel.max()), None)
        if sure.any():
            row(st, "prec max rel, cert>0.5", float(rel[sure].max()), None)
            row(st, "prec p99 rel, cert>0.5", float(np.percentile(rel[sure], 99)),
                bar(st, "prec p99 rel, cert>0.5", "full_prec"))
        else:
            missing(st + " has no certain pixel")
        row(st, "reference certain > 0.5", float((cr > 0.5).mean()), None)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", required=True, help="SS_ROMA_DUMP directory of a roma_model_test run")
    ap.add_argument("--weights", choices=["f16", "f32"], required=True,
                    help="what roma_model_test held the backbone in (SS_ROMA_F32_WEIGHTS)")
    ap.add_argument("--ref", choices=["fp32", "bf16", "rope32"], default="fp32")
    ap.add_argument("--save-ref", help="also write the reference stages here as .npy")
    ap.add_argument("--json", help="write the table here")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--device", default="cpu", help="torch device for the reference")
    ap.add_argument("--our-taps", action="store_true",
                    help="feed OUR backbone taps to the torch matcher: the coarse warp of the matcher "
                         "alone, with the backbone's rounding taken out of it")
    ap.add_argument("--floor-device", default="auto",
                    help="second torch device whose reference, scored as ours, is the floor "
                         "(auto: MPS, else CUDA; none: the bars unwidened)")
    ap.add_argument("--floor-from", help="reuse the floor a previous --json of the same inputs, "
                    "--ref and mode recorded, instead of measuring it again")
    ap.add_argument("--floor-perturb-draws", type=int, default=MIN_FLOOR_DRAWS - 1,
                    help="also reference draws on inputs + N(0, --floor-perturb), one seed "
                         "each, on --device; the floor is the max over every draw")
    ap.add_argument("--floor-perturb", type=float, default=1e-6)
    ap.add_argument("--perturb", type=float, default=0.0,
                    help="add N(0, s) to the inputs: the reference's own noise floor")
    ap.add_argument("--perturb-seed", type=int, default=0)
    ap.add_argument("--full", action="store_true",
                    help="the dump is of Model::match(): run RoMaV2.forward at its lr "
                         "(and hr) sizes, bidirectional when the dump has B->A, and compare "
                         "every refiner stage and the final warp and certainty")
    a = ap.parse_args()

    import torch
    torch.set_float32_matmul_precision("highest")
    if a.threads:
        torch.set_num_threads(a.threads)

    manifest = require_manifest(a.dump)
    check_settings(manifest, a.weights, a.ref)
    A, B = load(a.dump, "input_A"), load(a.dump, "input_B")
    if A is None or B is None:
        sys.exit(f"{a.dump}: no input_A.npy / input_B.npy in its manifest")
    img_A = torch.from_numpy(A).permute(2, 0, 1)[None].float()
    img_B = torch.from_numpy(B).permute(2, 0, 1)[None].float()
    if a.perturb:
        g = torch.Generator().manual_seed(a.perturb_seed)
        img_A = img_A + a.perturb * torch.randn(img_A.shape, generator=g)
        img_B = img_B + a.perturb * torch.randn(img_B.shape, generator=g)
    taps = None
    if a.our_taps:
        def t(name):
            v = load(a.dump, name)
            if v is None:
                sys.exit(f"{a.dump}: no {name}.npy")
            return torch.from_numpy(v)[None].float()
        taps = ([t("dino_tap11_A"), t("dino_tap17_A")], [t("dino_tap11_B"), t("dino_tap17_B")])

    def reference(device):
        if a.full:
            return reference_full(a, a.dump, (img_A, img_B), device)
        tp = None if taps is None else [[x.to(device) for x in side] for side in taps]
        return run_reference(build_model(a.ref, device), img_A.to(device), img_B.to(device), tp)

    # Which bytes this table is about.
    hr_bytes = b"".join(x.tobytes() for x in (load(a.dump, "input_A_hr"),
                                              load(a.dump, "input_B_hr")) if x is not None)
    input_sha = hashlib.sha256(A.tobytes() + B.tobytes() + hr_bytes).hexdigest()[:16]
    bars = dict(BARS[a.weights])
    compare = compare_full if a.full else compare_coarse

    ref = reference(a.device)
    if a.save_ref:
        save_reference(a.save_ref, ref, a.dump, "torch-" + a.device)

    # The floor: the same reference on a second torch device, scored as if it
    # were ours. Every coarse and full-match bar is then max(bar, FLOOR_FACTOR x floor).
    floor_dev = None if a.floor_from else resolve_floor_device(a.floor_device, a.device)
    if floor_dev and 1 + a.floor_perturb_draws < MIN_FLOOR_DRAWS:
        sys.exit(f"--floor-perturb-draws {a.floor_perturb_draws}: the floor is the worst of "
                 f"{1 + a.floor_perturb_draws} draws, and fewer than {MIN_FLOOR_DRAWS} is "
                 "too few to call a worst case")
    floor_of, floor_info = {}, None
    if a.floor_from:
        with open(a.floor_from) as f:
            prev = json.load(f)
        if (prev.get("input_sha"), prev.get("ref"), prev.get("full")) != (input_sha, a.ref, a.full):
            sys.exit(f"{a.floor_from}: floor of inputs {prev.get('input_sha')} ref "
                     f"{prev.get('ref')} full {prev.get('full')}, not this run's")
        floor_info = prev["floor"]
        if not floor_info or floor_info.get("n_draws", 0) < MIN_FLOOR_DRAWS:
            sys.exit(f"{a.floor_from}: its floor is the worst of "
                     f"{(floor_info or {}).get('n_draws', 0)} draws, not {MIN_FLOOR_DRAWS}")
        floor_of = {(r_["stage"], r_["metric"]): r_["value"] for r_ in floor_info["rows"]}
    draws = []   # (label, reference): each scored against `ref` as if it were ours
    if floor_dev:
        draws.append(("torch-" + floor_dev, reference(floor_dev)))
    if floor_dev:
        for seed in range(a.floor_perturb_draws):
            draws.append((f"torch-{a.device}+N(0,{a.floor_perturb:g}) seed {seed}",
                          perturbed_reference(a, img_A, img_B, a.floor_perturb, 1000 + seed)))
    if draws:
        per_draw = []
        for label, fref in draws:
            with tempfile.TemporaryDirectory() as tmp:
                save_reference(tmp, fref, a.dump, label)
                require_manifest(tmp)
                frows = []
                compare(a, tmp, ref, bars, {}, collect(frows), lambda name: None)
            per_draw.append(dict(draw=label, rows=frows))
        # The floor of a row is the worst draw.
        for d in per_draw:
            for r_ in d["rows"]:
                k = (r_["stage"], r_["metric"])
                floor_of[k] = max(floor_of.get(k, 0.0), r_["value"])
        frows = [dict(stage=k[0], metric=k[1], value=v) for k, v in floor_of.items()]
        floor_info = dict(device=floor_dev or "none", against=a.device, torch=torch.__version__,
                          factor=FLOOR_FACTOR, n_draws=len(per_draw), draws=per_draw,
                          rows=frows)

    rows = []
    compare(a, a.dump, ref, bars, floor_of, collect(rows), missing_into(rows))
    return report(a, rows, input_sha, manifest, floor_info)


def perturbed_reference(a, img_A, img_B, sigma, seed):
    """The reference on inputs + N(0, sigma) (hr too): one draw of its own noise floor."""
    import torch
    g = torch.Generator().manual_seed(seed)
    pa = img_A + sigma * torch.randn(img_A.shape, generator=g)
    pb = img_B + sigma * torch.randn(img_B.shape, generator=g)
    if not a.full:
        return run_reference(build_model(a.ref, a.device), pa.to(a.device), pb.to(a.device), None)
    saved = (a.perturb, a.perturb_seed)
    a.perturb, a.perturb_seed = sigma, seed   # reference_full perturbs the hr pair from these
    try:
        return reference_full(a, a.dump, (pa, pb), a.device)
    finally:
        a.perturb, a.perturb_seed = saved


def collect(rows):
    def row(stage, metric, value, bar):
        ok = None if bar is None else bool(value <= bar)
        rows.append(dict(stage=stage, metric=metric, value=value, bar=bar, ok=ok))
    return row


def missing_into(rows):
    def missing(name):
        rows.append(dict(stage=name, metric="missing from our dump", value=float("nan"),
                         bar=None, ok=False))
    return missing


def resolve_floor_device(choice, main_device):
    import torch
    if choice == "none":
        return None
    if choice == "auto":
        for d, ok in (("mps", torch.backends.mps.is_available()),
                      ("cuda", torch.cuda.is_available())):
            if ok and d != main_device:
                return d
        sys.exit("--floor-device auto: no second torch device (MPS or CUDA) to measure the "
                 "floor on; pass --floor-device none to hold the coarse and full-match rows to the bars unwidened")
    return choice


def save_reference(path, ref, dump, nonce):
    """A reference written as a dump, with inputs and manifest, so it can stand as ours."""
    os.makedirs(path, exist_ok=True)
    files = set(ref)
    for k, v in ref.items():
        np.save(os.path.join(path, k + ".npy"), v)
    for k in ("input_A", "input_B", "input_A_hr", "input_B_hr"):
        v = load(dump, k)
        if v is not None:
            np.save(os.path.join(path, k + ".npy"), v)
            files.add(k)
    write_manifest(path, files, nonce)


def compare_coarse(a, dump, ref, bars, floor_of, row, missing):
    """The backbone and VGG taps, and the coarse warp."""
    def p2_bar(stage, metric, key):
        fl = floor_of.get((stage, metric))
        return bars[key] if fl is None else max(bars[key], FLOOR_FACTOR * fl)

    A = load(dump, "input_A")
    # The matcher's bf16 table must be bit-exact: one flipped rounding there is
    # amplified by the bf16 rotation it feeds.
    for name, bar in (("rope_backbone", 1e-6),
                      ("rope_matcher", 1e-6 if a.ref == "rope32" else 0.0)):
        ours = load(dump, name)
        if ours is None:
            missing(name)
            continue
        row(name, "max abs", float(np.abs(ours - ref[name]).max()), bar)

    p1 = []
    for name in ("dino_tap11_A", "dino_tap17_A", "dino_tap11_B", "dino_tap17_B"):
        ours = load(dump, name)
        if ours is None:
            missing(name)
            continue
        e = rel_l2(ours, ref[name])
        p1.append(e)
        row(name, "rel L2", e, None)
    if p1:
        row("backbone taps", "max rel L2", max(p1), bars["taps"])

    # The refiners' VGG maps share no rounding with the transformers, so they
    # are held to the backbone-tap bar.
    vgg = []
    for name in ("vgg_s1_A", "vgg_s2_A", "vgg_s4_A", "vgg_s1_B", "vgg_s2_B", "vgg_s4_B"):
        ours = load(dump, name)
        if ours is None:
            missing(name)
            continue
        vgg.append(rel_l2(ours, ref[name]))
        row(name, "rel L2", vgg[-1], None)
    if vgg:
        row("VGG19-BN taps", "max rel L2", max(vgg), bars["taps"])

    # Stages between the gates, for bisecting a failure.
    for name in ("mv_A", "mv_B", "sim_AB", "dpt_l1", "dpt_l2", "dpt_l3", "dpt_l4",
                 "dpt_rn4", "dpt_rn3", "dpt_rn2", "dpt_rn1", "dpt_conv1"):
        ours = load(dump, name)
        if ours is None:
            missing(name)
            continue
        row(name, "rel L2", rel_l2(ours, ref[name]), None)

    ours = load(dump, "dpt_out_AB")
    if ours is None:
        missing("dpt_out_AB")
    else:
        r = ref["dpt_out_AB"]
        H, W = A.shape[:2]
        # Warp is normalized [-1, 1]; one unit is half the image, so px at the
        # 640 match resolution is the delta times (W/2, H/2).
        d = (ours[..., :2] - r[..., :2]) * np.array([W / 2, H / 2])
        epe = np.sqrt((d.astype(np.float64) ** 2).sum(-1)).ravel()
        row("coarse warp", "EPE p50 px", float(np.percentile(epe, 50)),
            p2_bar("coarse warp", "EPE p50 px", "coarse_p50"))
        row("coarse warp", "EPE p99 px", float(np.percentile(epe, 99)),
            p2_bar("coarse warp", "EPE p99 px", "coarse_p99"))
        row("coarse warp", "EPE max px", float(epe.max()), None)
        row("coarse overlap logit", "max abs", float(np.abs(ours[..., 2] - r[..., 2]).max()),
            p2_bar("coarse overlap logit", "max abs", "coarse_logit"))



def report(a, rows, input_sha, manifest, floor_info):
    failed = any(r_["ok"] is False for r_ in rows)
    print(f"ref={a.ref} weights={a.weights} dump={a.dump} inputs={input_sha}"
          + (" (matcher fed our taps)" if a.our_taps else "")
          + (" (full match)" if a.full else "")
          + (f" floor: worst of {floor_info['n_draws']} draws (device {floor_info['device']}, "
             f"perturbed {a.floor_perturb:g}) vs {floor_info['against']}, bars >= "
             f"{FLOOR_FACTOR:g}x" if floor_info else " floor: none, bars unwidened"))
    floor_of = {(r_["stage"], r_["metric"]): r_["value"]
                for r_ in (floor_info["rows"] if floor_info else [])}
    for r_ in rows:
        bar = "" if r_["bar"] is None else f"<= {r_['bar']:.3g}"
        flag = "" if r_["ok"] is None else ("PASS" if r_["ok"] else "FAIL")
        fl = floor_of.get((r_["stage"], r_["metric"]))
        r_["floor"] = fl
        extra = "" if fl is None or r_["bar"] is None else f"  (floor {fl:.3g})"
        print(f"  {r_['stage']:<24} {r_['metric']:<24} {r_['value']:.3e} {bar:<12} {flag}{extra}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(dict(ref=a.ref, weights=a.weights, our_taps=a.our_taps, full=a.full,
                           input_sha=input_sha, dump_nonce=manifest.get("nonce"),
                           ours=dict(f16_weights=manifest["f16_weights"],
                                     rope_rounds=manifest["rope_rounds"],
                                     local_corr_fused=manifest["local_corr_fused"],
                                     notes=manifest.get("notes", {})),
                           floor=floor_info, rows=rows), f, indent=1)
    print("FAIL" if failed else "PASS")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
