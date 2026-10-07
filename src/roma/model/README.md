# RoMa v2 on `src/nn` (`src/roma/model/`)

The RoMa v2 dense matcher (Edstedt et al., <https://github.com/Parskatt/RoMaV2>,
MIT) on the inference layer. It uses no PyTorch and no converter: the authors'
`romav2.0.1.pt` is read in process by `nn::TorchCheckpoint`, after the consent
gate in `roma/model/Fetch.h`. The densify host stage in `src/roma/*.cpp` is
separate and lives in `docs/notes/densify.md`.

Status: the whole of `RoMaV2.forward` -- the DINOv3 backbone, the VGG19-BN
fine features, the coarse matcher (multi-view transformer, similarity, match
embedding, DPT head) and the three refiners at one or two scales, in one or
both directions -- matches upstream PyTorch on the same bytes.
`roma::Model::match()` returns the refined warp and confidence;
`roma::RomaMatcher` is the `roma::Matcher` densify uses, with RoMa's presets.
`roma::Model::coarse()` still returns the stride-4 warp alone.

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

## The refiners and the full match

`Refiner.cpp` is upstream `refiner.py` and `local_correlation.py` (MIT): project
both VGG maps, sample B's at the previous warp, embed `scale * (warp - grid)`,
add a local correlation, run a depthwise trunk, add the heads' deltas. Four
things a port gets wrong:

1. **The local correlation reads B's projected map, not the warped one**
   (upstream fix #47; the Lichtfeld plugin's vendored copy predates it). The
   mutant reads 1.2 px at the P-3 median.
2. **Every refiner BatchNorm is folded into its depthwise conv at load**, and the
   reference must be run with each refiner `Block.enable_amp` off: it enters a
   bf16 autocast of its own that `--ref fp32` would otherwise keep.
3. **Before the hr scale the precision is zeroed**, logit kept
   (`zero_out_precision`). Without it the P-3 p99 reads 56 px.
4. **The local correlation has two paths.** `shaders/roma_local_corr.slang`'s fused
   kernel samples and dots in one pass; `SS_ROMA_LOCAL_CORR=unfused` is the
   reference's own shape, every window sample through `nn::grid_sample_points`.
   At `precise` the unfused window is 3.5 GB of arena (5.33 GB against 1.81 GB
   fused, M5 Pro), which is the term the plan's section 5.2 predicted. The two
   agree to 1e-5 px at the median.

Presets (`RomaMatcher.h`): `turbo` 320, `fast` 512, `base` 640, `precise`
800 + 1280 hr (upstream's `apply_setting`), and the plugin's `high`, 640 +
960 hr. A into B does not depend on whether B into A is computed, so
`match()` never computes B into A; `matchBoth()` does. A's backbone taps and
VGG maps stay on the device keyed by `MatchImage::name`, so a reference matched
against several neighbours runs its backbone once.

## Full-match parity (P-3)

`compare_torch.py --full` runs `RoMaV2.forward` at the dump's lr (and hr) sizes,
bidirectional when the dump has B into A, and reads every refiner stage and the
final warp and certainty. 15 cells (5 presets x 3 fixtures), both directions,
measured on the M4 Max:

- **Rounding on, against max(plan bar, 2x torch MPS-vs-CPU, 2x torch CPU on
  1e-6-perturbed inputs): 30 of 30 cell-weight pairs pass** (f32 and f16
  weights). Torch MPS against torch CPU misses the plan's fp32 P-3 bars itself
  in every cell, chiefly certainty max abs (2e-2 to 1e-1 against 2e-3).
- **Rounding off on both sides** (`SS_ROMA_ROPE_F32=1`, `--ref rope32`), against
  its own MPS floor: 10 of 15 pass. The five misses are single-pixel maxima:
  certainty max abs 2.8e-3 to 1.2e-2 against 2.0e-3 to 5.5e-3, and once the
  certain-pixel precision, 1.08e-2 against 1e-2. The C_high one was traced: an
  occlusion edge where the reference warp jumps 74 px between neighbours, at
  certainty 0.03, where a 1e-3 px input difference moves which side of the
  edge bilinear sampling lands on. The unfused path misses at the same pixel by
  the same amount, so it is not the fused kernel.
- **Where the reference is certain (> 0.5)**, our EPE against torch CPU tracks
  torch MPS against torch CPU in every cell, mostly within 2x: e.g. `precise`
  on the basement faces 0.0017 / 0.071 px (p50 / p99) against torch's own
  0.0018 / 0.079.

All-pixel EPE is dominated by pixels where the warp is ill-posed (toronto's
B into A is 1.5% certain), so on those cells the floor-widened p99 bar runs to
tens of pixels; the p50 bars stay at 0.01-0.022 px. Precision is gated over
reference-certain pixels only: elsewhere it is arbitrary and torch misses 1e-2
against itself (0.25 on toronto B into A).

```bash
SS_ROMA_F32_WEIGHTS=1 SS_ROMA_BENCH_BOTH=1 SS_ROMA_DUMP=/tmp/d \
    ./build/roma_model_test --bench precise --pair A.png B.png --repeat 0
uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32 --full
```

## Memory and speed of the full match

M5 Pro (24 GB, no cooperative matrix), basement cube faces, f16 backbone. A
pair is "cold" when A is new, "warm" when A's features are cached (densify's
case after a reference's first neighbour). The arena is the plan, which the
peak stays inside at every stage; A's cache is its own arena.

| preset | arena | A cache | cold s/pair | warm s/pair |
|---|---|---|---|---|
| turbo | 142 MB | 50 MB | 0.39 | 0.27 |
| fast | 310 MB | 129 MB | 0.97 | 0.68 |
| base | 466 MB | 202 MB | 1.63 | 1.16 |
| high | 993 MB | 625 MB | 2.52 | 1.81 |
| high, both directions | 1037 MB | 625 MB | 3.07 | 2.40 |
| precise | 1734 MB | 1069 MB | 4.78 | 3.43 |
| precise, both directions | 1813 MB | 1069 MB | 5.26 | 4.10 |

`precise` in both directions holds 2.88 GB of arena and cache beside 0.95 GB of
weights (1.40 GB with `SS_ROMA_F32_WEIGHTS=1`), against the plan's 4 GB gate.
The process peak footprint `/usr/bin/time -l` measured is 4.64 GB, which agrees.
PyTorch on MPS needs 17.1 GB for the same preset.
