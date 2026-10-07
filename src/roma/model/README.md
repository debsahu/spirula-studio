# RoMa v2 on `src/nn` (`src/roma/model/`)

The RoMa v2 dense matcher (Edstedt et al., <https://github.com/Parskatt/RoMaV2>,
MIT) on the inference layer. It uses no PyTorch and no converter: the authors'
`romav2.0.1.pt` is read in process by `nn::TorchCheckpoint`, after the consent
gate in `roma/model/Fetch.h`. The densify host stage in `src/roma/*.cpp` is
separate and lives in `docs/notes/densify.md`.

Status: the DINOv3 backbone, the VGG19-BN fine features and the coarse matcher
(the multi-view transformer, the similarity, the match embedding and the DPT
head) match upstream PyTorch on the same bytes, and the output is bit-identical
from run to run. `roma::Model::coarse()` returns the stride-4 warp and overlap
logit. The refiners (`Refiner.cpp`, `Model::match()`) are gated separately, by
P-3 (`compare_torch.py --full`).

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
