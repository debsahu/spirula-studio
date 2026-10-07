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
`roma::Model::coarse()` still returns the stride-4 warp alone. The output is
bit-identical from run to run (`budget_slicing_test`, and three runs per
fixture below).

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
   - **With the rounding on**, each P-2 bar is max(plan bar, 2 x the floor).
     The floor is that MPS-vs-CPU run, which `compare_torch.py` computes itself
     (`--floor-device auto`) and records, row by row, in its `--json`. The
     factor 2 is a judgement, not a measurement.
   - **With the rounding off** on both sides (`SS_ROMA_ROPE_F32=1` against
     `--ref rope32 --floor-device none`), against the plan's bars unchanged.
   - The same amplification made the output depend on how the submit budget
     sliced attention, until `nn::attention` decided its key-range split for
     the whole problem (`budget_slicing_test`).
3. **The backbone's RoPE is rotate-half and `nn::rope` pairs neighbours.** The
   q and k rows of every fused qkv are permuted at load (`permute_qk_rows`).
   `q.k` does not change under one permutation applied to both, so no kernel
   changed. The class and storage tokens are not rotated.
4. **RoMa reads blocks 11 and 17 of 24**, each through the final LayerNorm, so
   blocks 18 to 23 are neither loaded nor run.
5. **The DPT's residual units use in-place ReLU.** The skip therefore adds
   relu(x), not x. Every bilinear resize in the head is `align_corners=True`.

Two findings about the file. First, every qkv bias and every `bias_mask` in
`romav2.0.1.pt` is exactly zero. The K-bias fold is therefore inert, and no
parity run can see whether it is applied; `roma_unit_test`'s `qkv_bias_fold`
pins it on synthetic values instead. Second, the backbone's bf16 values convert
to f16 exactly, except 4.6e-4 of them that fall below f16's normal range.

**Weights are f16 only where tensor cores take them.** The backbone goes to
f16 only when the coop-matrix GEMM will run it. nn's two fp32-pipe f16 tiles
(chosen per process by timing) differ by about 1e-5, which the bf16 RoPE turns
into 0.65 px between runs, and f16 is no faster without tensor cores (894 vs
889 ms a pair on the M4 Max). The matcher's matrices are fp32 in the file and
stay fp32: in f16 they moved the logit by 4e-2, twice the bar.

## Parity

`tools/roma/compare_torch.py` runs upstream RoMaV2 at `95c9968` on the CPU, in
fp32, with every autocast off (including the one `dpt.py` hard-codes). It is
fed the `[0, 1]` images that `roma_model_test` dumped. It reads only the files
listed in the dump's `manifest.json`, and only from a run that finished with
status 0: a stale or failed dump is refused, not scored.

Four pairs were measured on an M4 Max (fp32 GEMM, no tensor cores), on
2026-10-07:

- **C**: RoMa's toronto demo pair, 640².
- **A**: two consecutive basement cube faces, 640².
- **B**: two aerial equirect frames cut to 90° faces, 640².
- **R**: C at 640x448. RoPE's two axes and the DPT's resize targets only
  separate when H != W.

Every pair was run three times, and `dpt_out_AB` was bit-identical each time.

Results with the rounding on (f32 weights):

| | C | A | B | R | bar |
|---|---|---|---|---|---|
| P-1 DINOv3 taps, max rel L2 | 1.6e-6 | 2.1e-6 | 1.8e-6 | 1.7e-6 | 1e-4 |
| VGG19-BN taps, max rel L2 | 2.8e-6 | 4.7e-6 | 3.2e-6 | 2.7e-6 | 1e-4 |
| P-2 warp EPE p50 px | 0.0086 | 0.0130 | 0.0065 | 0.0028 | 2x floor |
| P-2 warp EPE p99 px | 0.041 | 0.41 | 0.089 | 0.018 | 2x floor |
| P-2 overlap logit, max abs | 0.011 | 0.106 | 0.040 | 0.0098 | 2x floor |
| *floor (MPS vs CPU): p50 / p99 / logit* | *.0096 / .057 / .010* | *.0125 / .42 / .107* | *.0074 / .074 / .143* | *.0045 / .023 / .012* | |

Results with the rounding off on both sides (plan bars unwidened):

| | C | A | B | R | plan's fp32 bar |
|---|---|---|---|---|---|
| P-2 EPE p50 px | 6.4e-5 | 1.3e-4 | 9.7e-5 | 5.0e-5 | 0.01 |
| P-2 EPE p99 px | 1.1e-3 | 2.9e-3 | 2.0e-3 | 6.3e-4 | 0.1 |
| P-2 logit | 9.2e-5 | 4.2e-4 | 5.8e-4 | 9.7e-5 | 1e-3 |

With f16 backbone weights, measured before f16 was restricted to tensor-core
devices (the floor there was the worst of MPS and two perturbed runs, not MPS
alone):

| | C | A | B | f16 bar |
|---|---|---|---|---|
| P-1 DINOv3 taps, max rel L2 | 1.6e-6 | 2.1e-6 | 1.9e-6 | 2e-3 |
| P-2 EPE p50 / p99 px | 0.0093 / 0.046 | 0.013 / 0.51 | 0.0064 / 0.11 | 0.05 / 0.5, or 2x floor |
| P-2 logit | 0.013 | 0.085 | 0.134 | 2e-2, or 2x floor |

**Against the model as shipped** (`--ref bf16`: DINOv3 in bf16, every
autocast on), this port is an fp32 implementation and does not meet the
plan's bars. That is the price of computing in fp32 where the reference
rounds to bf16, not a parity result.

| | C | A | B | R |
|---|---|---|---|---|
| P-1 rel L2 | 9.6e-3 | 1.5e-2 | 1.2e-2 | 1.0e-2 |
| P-2 EPE p50 / p99 px | 0.16 / 0.61 | 0.30 / 9.9 | 0.24 / 2.1 | 0.20 / 0.73 |
| P-2 logit | 0.17 | 0.84 | 0.38 | 0.19 |

Mutants that P-2 kills on its own under the MPS floor (fixture C), each also
caught earlier by `roma_model_test`:

| mutant | P-2 EPE p50 | caught first by |
|---|---|---|
| bf16 truncation instead of round-to-even | 0.11 px | `rope_bf16` |
| one bf16 rounding skipped | 0.037 px | `rope_bf16` |
| DPT align_corners=False | 0.63 px | — |

```bash
SS_ROMA_DUMP=/tmp/d ./build/roma_model_test --require-model --pair A.png B.png &&
uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32 --json /tmp/d.json
SS_ROMA_ROPE_F32=1 SS_ROMA_DUMP=/tmp/r ./build/roma_model_test --require-model --pair A.png B.png &&
uv run tools/roma/compare_torch.py --dump /tmp/r --weights f32 --ref rope32 --floor-device none
```

The first command needs a second torch device (MPS or CUDA) for the floor.
Without one it stops and says so, rather than scoring against bars that torch
itself misses. `--rect W H` runs a single coarse() on a W x H pair.

## Memory and speed

The arena is planned before a pass runs. `roma_model_test` holds every stage
(backbone, fine features, transformer, similarity, DPT head) to its own plan,
because the overall maximum is bound by the DPT head at every size and would
hide an under-counted term. The overall peak at each size:

| size | 320 | 512 | 640 | 800 | 1024 | 1280 |
|---|---|---|---|---|---|---|
| arena peak | 111 MB | 248 MB | 387 MB | 605 MB | 992 MB | 1550 MB |
| coarse pair, M4 Max | 0.22 s | 0.54 s | 0.89 s | 1.55 s | 3.2 s | 6.5 s |
| coarse pair, M5 Pro | | | 1.39 s | 2.5 s | | |

Weights take 1.39 GB on the device where every matrix is fp32, which is every
device without tensor cores, and 0.94 GB where the backbone is f16. A coarse
pair runs both backbones and the matcher.

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
   (`zero_out_precision`). Without it only the certain-pixel precision row
   sees it, at 60x its bar.
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
VGG maps stay on the device keyed by its own bytes and sizes, so a reference matched
against several neighbours runs its backbone once.

## Full-match parity (P-3)

`compare_torch.py --full` runs `RoMaV2.forward` at the dump's lr (and hr) sizes,
bidirectional when the dump has B into A, and reads every refiner stage and the
final warp and certainty. Each gated row's bar is max(plan bar, 2x floor), where
the floor is the worst of four draws of torch against itself: torch on MPS, and
torch on the CPU with the inputs perturbed by N(0, 1e-6), three seeds (plan
section 9's n >= 3). Gated: EPE p50 and p99 over all pixels and over pixels the
reference is certain of (> 0.5), certainty max abs, the share crossing 0.2 or 0.9,
and the p99 of the certain-pixel precision error relative to max(|ref|, 1).

15 cells (5 presets x 3 fixtures, both directions), M4 Max, on the build after
WS-2's split-K fix. Two draws of every cell are bit-identical. Fixtures by SHA-256
of the source files: toronto `40270c22`/`a2c07550`, basement faces
`b30319e1`/`2a3e377f`, aerial `fcd77cf7`/`522ffa63`. "On" is as shipped, with the
matcher's bf16 RoPE rounding; "off" is `SS_ROMA_ROPE_F32=1` against
`--ref rope32`, each against its own floor. The MPS-only columns re-score the same
rows against 2x the MPS draw alone:

| cell | on: 4-draw | on: MPS-only | off: 4-draw | off: MPS-only | certain p50 AB, BA (ours/bar) | certain p99 AB, BA | prec p99 AB, BA |
|---|---|---|---|---|---|---|---|
| C turbo | pass | pass | pass | pass | 0.0018/0.011, 0.037/0.24 | 0.045/0.91, 0.57/3.2 | 0.014/0.055, 0.0044/0.02 |
| C fast | pass | pass | pass | pass | 0.0015/0.01, 0.029/0.061 | 0.06/0.19, 0.89/2.3 | 0.0069/0.017, 0.0039/0.012 |
| C base | pass | pass | pass | pass | 0.0031/0.01, 0.044/0.088 | 0.055/0.11, 2.5/3.9 | 0.015/0.029, 0.012/0.027 |
| C high | pass | **miss** | **miss** | **miss** | 0.0044/0.01, 0.11/0.13 | 0.68/1.1, 9.6/34 | 0.082/0.086, 0.035/0.11 |
| C precise | pass | pass | pass | **miss** | 0.001/0.01, 0.042/0.11 | 0.034/0.17, 2.5/8.9 | 0.007/0.03, 0.021/0.049 |
| A turbo | pass | pass | pass | pass | 0.0031/0.01, 0.0022/0.01 | 0.084/0.25, 0.043/0.11 | 0.03/0.069, 0.037/0.094 |
| A fast | pass | pass | pass | pass | 0.0011/0.01, 0.0008/0.01 | 0.017/0.1, 0.017/0.1 | 0.014/0.04, 0.022/0.056 |
| A base | pass | pass | pass | **miss** | 0.0012/0.01, 0.001/0.01 | 0.034/0.1, 0.028/0.1 | 0.012/0.043, 0.017/0.042 |
| A high | pass | **miss** | pass | pass | 0.0012/0.01, 0.00082/0.01 | 0.084/0.15, 0.047/0.12 | 0.018/0.046, 0.0093/0.024 |
| A precise | pass | pass | pass | **miss** | 0.0022/0.01, 0.0015/0.01 | 0.11/0.33, 0.062/0.17 | 0.023/0.059, 0.013/0.03 |
| B turbo | pass | pass | pass | pass | 0.00049/0.01, 0.00041/0.01 | 0.0079/0.1, 0.012/0.1 | 0.068/0.27, 0.047/0.16 |
| B fast | pass | pass | pass | pass | 0.00019/0.01, 0.00019/0.01 | 0.011/0.1, 0.019/0.1 | 0.029/0.067, 0.04/0.067 |
| B base | pass | pass | pass | pass | 0.00033/0.01, 0.00032/0.01 | 0.012/0.1, 0.0096/0.1 | 0.031/0.07, 0.052/0.13 |
| B high | pass | pass | pass | pass | 9e-05/0.01, 6.4e-05/0.01 | 0.016/0.1, 0.018/0.1 | 0.02/0.045, 0.0096/0.045 |
| B precise | pass | **miss** | pass | pass | 0.00017/0.01, 0.00012/0.01 | 0.014/0.1, 0.013/0.1 | 0.025/0.044, 0.018/0.045 |

What the table says:

- **As shipped, against the four-draw floor: 15 of 15 pass.** Against the MPS
  draw alone, three miss. All three are A into B or B into A certainty maxima
  or tails: C high (worst: certain-pixel p99 0.68 px against 0.12), A high,
  and B precise (certainty max abs 0.0605 against 0.0585).
- **C high is traced to the coarse matcher, not the refiners.** Our coarse
  warp already differs from torch's CPU reference 6.5x more than MPS does
  (EPE p50 0.0073 against 0.0011 px). That ratio then holds through all six
  refiner stages. One of the perturbed draws differs by the same amount
  (coarse 0.0036 px, certain p99 0.54 px). With the rounding off, C high A into
  B passes even the MPS-only bars. Read: the bf16 RoPE amplifies fp32
  differences upstream of the refiners (convention 2 above).
- **Rounding off, against the four-draw floor: 14 of 15 pass.** The miss is
  C high B into A, certain-pixel p99 0.788 against 0.784, in toronto's B into A,
  which is 1.5% certain. Against MPS alone, four miss: C high B into A by 2.4x
  (0.788 against 0.325), and C precise, A base and A precise by 4-13% at a
  certainty max or an all-pixel p99.
- **C turbo**, flagged in review: A into B certain-pixel precision max is
  0.0262. Its worst pixel has certainty 0.66, sits near no warp discontinuity,
  and has an EPE of 0.004 px. The three perturbed draws of torch reach
  0.042-0.051 on the same row. It is not an error of ours.

Why these rows: all-pixel EPE is dominated by pixels where the warp is
ill-posed. On toronto's B into A its p50 bar runs to 3.6 px and gates little,
while the certain-pixel rows stay at the plan's 0.01 / 0.1 px nearly
everywhere. Precision is read where the reference is certain: elsewhere it is
arbitrary, and torch misses 1e-2 against itself there. It is read at p99, not
the max, because the max is one pixel: the traced case was a single
occlusion-edge pixel at certainty 0.03. Every P-3 mutant below is caught at
p99 (rows in `mut3b*.log`), two of them only by that row.

| mutant | caught by |
|---|---|
| local correlation fed the warped map (pre-#47) | the EPE, certainty and precision rows, both directions |
| refiner grid_sample `align_corners=True` | the same, except A into B's all-pixel p99 |
| hr precision not zeroed | certain-pixel precision p99 only (2.8 against 0.046) |
| refiner input order swapped | the EPE, certainty and precision rows, both directions |
| softplus -> relu in the Cholesky head | certain-pixel precision p99 only (0.73 against 0.043) |
| softplus without its log1p tail | not at P-3; `refine_update_softplus_tail` |

Superseded: the 2026-10-07 matrix before this one ("30 of 30") read a floor of
two draws, MPS and one perturbed seed, on a build whose first match in a process
was not reproducible, and its f16 single-draw verdicts are void. The Macs now
hold the backbone in f32 (no cooperative matrix), so this run has no f16 arm.
`mut.log` of that round predates the final bars.

```bash
SS_ROMA_BENCH_BOTH=1 SS_ROMA_DUMP=/tmp/d ./build/roma_model_test --bench precise \
    --pair A.png B.png --repeat 0
uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32 --full
```

## Memory and speed of the full match

Basement cube faces. Neither Mac has cooperative matrices; the times below were
taken with the backbone in f16, before it went f32 on such devices (894 against
889 ms a pair, WS-2's measurement). A
pair is "cold" when A is new, "warm" when A's features are cached (densify's
case after a reference's first neighbour). The arena is the plan, which the
peak stays inside at every stage; A's cache is its own arena. Both Macs plan
the same bytes.

| preset | arena | A cache | M5 Pro cold / warm s/pair | M4 Max cold / warm s/pair |
|---|---|---|---|---|
| turbo | 142 MB | 50 MB | 0.39 / 0.27 | 0.28 / 0.19 |
| fast | 310 MB | 129 MB | 0.97 / 0.68 | 0.71 / 0.49 |
| base | 466 MB | 202 MB | 1.63 / 1.16 | 1.15 / 0.80 |
| high | 993 MB | 625 MB | 2.52 / 1.81 | 1.71 / 1.19 |
| high, both directions | 1037 MB | 625 MB | 3.07 / 2.40 | 2.19 / 1.67 |
| precise | 1734 MB | 1069 MB | 4.78 / 3.43 | 2.95 / 2.06 |
| precise, both directions | 1813 MB | 1069 MB | 5.26 / 4.10 | 3.66 / 2.77 |

**The 4 GB gate is the arena plus A's cache: `precise` in both directions holds
2.88 GB, on either Mac.** Weights are on top of that: 1.40 GB with the f32
backbone the Macs now load (0.95 GB in f16). So is host memory. The process
peak footprint `/usr/bin/time -l` reports is therefore larger: 5.09 GB on the
M5 Pro with the f32 backbone, 4.64 GB with the f16 one.
PyTorch on MPS needs 17.1 GB for the same preset.
