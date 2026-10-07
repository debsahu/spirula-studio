# RoMa v2 on `src/nn` (`src/roma/model/`)

The RoMa v2 dense matcher (Edstedt et al., <https://github.com/Parskatt/RoMaV2>,
MIT) on the inference layer. It uses no PyTorch and no converter: the authors'
`romav2.0.1.pt` is read in process by `nn::TorchCheckpoint`, after the consent
gate in `roma/model/Fetch.h`. The densify host stage in `src/roma/*.cpp` is
separate and lives in `docs/notes/densify.md`.

Status: the DINOv3 backbone, the VGG19-BN fine features and the coarse matcher
(the multi-view transformer, the similarity, the match embedding and the DPT
head) match upstream PyTorch on the same bytes. The refiners are not here yet.
`roma::Model::coarse()` returns the stride-4 warp and overlap logit.

## Rules

- **Vulkan only**, built with `SS_BUILD_SAM` as `ss_roma`, globbing this
  directory only.
- **The DINOv3 parts are composed from the architecture, not translated.**
  RoMa v2's `vit/*.py` and `dpt.py` carry Meta's copyright under the DINOv3
  License Agreement. `Backbone.cpp`, `Matcher.cpp` and `Dpt.cpp` were written
  from the published architecture and from the checkpoint's tensor shapes, as
  `loma/model/Dino.cpp` was for DINOv2.
- **The weights are never bundled or mirrored** (`Fetch.h`, `no_mirror`).

## Five conventions you cannot guess

1. **The matcher's RoPE is bf16 even in an fp32 run.** The multi-view ViT
   builds its sin/cos in bf16 (`pos_embed_rope_dtype="bf16"`), casts q and k to
   bf16, and rounds after every eager op of `x*cos + rotate_half(x)*sin`.
   `shaders/roma.slang` reproduces that bit for bit, and its table is
   bit-identical to torch's. Rotating in fp32 instead moves the coarse warp by
   0.27 px at the median, 27x the bar.
2. **The bf16 rounding amplifies everything upstream of it.** PyTorch on MPS,
   compared with PyTorch on the CPU, both fp32 on the same bytes, misses the
   plan's fp32 P-2 bars on its own (table below). Parity is therefore read two
   ways:
   - with the rounding on, against twice the worst of three torch-vs-torch
     runs;
   - with the rounding off on both sides (`SS_ROMA_ROPE_F32=1` against
     `--ref rope32`), against the plan's bars unchanged.
3. **The backbone's RoPE is rotate-half and `nn::rope` pairs neighbours.** The
   q and k rows of every fused qkv are permuted at load (`permute_qk_rows`).
   `q.k` does not change under one permutation applied to both, so no kernel
   changed. The class and storage tokens are not rotated.
4. **RoMa reads blocks 11 and 17 of 24**, each through the final LayerNorm, so
   blocks 18 to 23 are neither loaded nor run.
5. **The DPT's residual units use in-place ReLU.** The skip therefore adds
   relu(x), not x. Every bilinear resize in the head is `align_corners=True`.

Two findings about the file: every qkv bias and every `bias_mask` in
`romav2.0.1.pt` is exactly zero. The K-bias fold is therefore inert, and no
parity test can see whether it is applied. The backbone's bf16 values convert
to f16 exactly, except 4.6e-4 of them that fall below f16's normal range. The
matcher's fp32 matrices stay fp32: in f16 they moved the logit by 4e-2, twice
the bar.

## Parity

`tools/roma/compare_torch.py` runs upstream RoMaV2 at `95c9968` on the CPU, in
fp32, with every autocast off (including the one `dpt.py` hard-codes). It is
fed the `[0, 1]` images that `roma_model_test` dumped. Three 640² pairs were
measured on an M4 Max (fp32 GEMM, no tensor cores):

- **C**: RoMa's toronto demo pair.
- **A**: two consecutive basement cube faces.
- **B**: two aerial equirect frames cut to 90° faces.

Results with the rounding on (`SS_ROMA_F32_WEIGHTS=1`; `f16` differs from it
only in the backbone's storage):

| | C | A | B | bar |
|---|---|---|---|---|
| P-1 DINOv3 taps, max rel L2 | 1.6e-6 | 2.1e-6 | 1.9e-6 | 1e-4 |
| VGG19-BN taps, max rel L2 | 2.8e-6 | 4.7e-6 | 3.2e-6 | 1e-4 |
| P-2 warp EPE p50 px | 0.0086 | 0.0133 | 0.0073 | 2x floor |
| P-2 warp EPE p99 px | 0.041 | 0.47 | 0.12 | 2x floor |
| P-2 overlap logit, max abs | 0.011 | 0.10 | 0.13 | 2x floor |
| *floor: torch MPS vs CPU, logit* | *0.010* | *0.107* | *0.143* | |

Results with the rounding off on both sides:

| | C | A | B | plan's fp32 bar |
|---|---|---|---|---|
| P-2 EPE p50 px | 6.2e-5 | 1.3e-4 | 9.7e-5 | 0.01 |
| P-2 EPE p99 px | 1.1e-3 | 2.9e-3 | 2.0e-3 | 0.1 |
| P-2 logit | 9.0e-5 | 4.2e-4 | 5.8e-4 | 1e-3 |

```bash
SS_ROMA_F32_WEIGHTS=1 SS_ROMA_DUMP=/tmp/d ./build/roma_model_test --pair A.png B.png
uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32 --floor F1.json F2.json F3.json
```

## Memory and speed

The arena is planned before a pass runs. `roma_model_test` holds every stage
(backbone, fine features, transformer, similarity, DPT head) to its own plan,
because the overall maximum is bound by the DPT head at every size and would
hide an under-counted term. The overall peak at each size:

| size | 320 | 512 | 640 | 800 | 1024 | 1280 |
|---|---|---|---|---|---|---|
| arena peak | 111 MB | 248 MB | 387 MB | 605 MB | 992 MB | 1550 MB |
| coarse pair, M4 Max | 0.22 s | 0.54 s | 0.89 s | 1.55 s | 3.2 s | 6.5 s |

Weights take 0.94 GB on the device, or 1.39 GB under `SS_ROMA_F32_WEIGHTS=1`.
A coarse pair runs both backbones and the matcher.
