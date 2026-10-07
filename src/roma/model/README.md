# RoMa v2 on `src/nn` (`src/roma/model/`)

The RoMa v2 dense matcher (Edstedt et al., <https://github.com/Parskatt/RoMaV2>,
MIT) on the inference layer. It uses no PyTorch and no converter: the authors'
`romav2.0.1.pt` is read in process by `nn::TorchCheckpoint`, after the consent
gate in `roma/model/Fetch.h`. The densify host stage in `src/roma/*.cpp` is
separate and lives in `docs/notes/densify.md`. Dated measurements (parity
tables, arena sizes, timings) are in `docs/notes/roma-model-validation.md`.

The whole of `RoMaV2.forward` -- the DINOv3 backbone, the VGG19-BN fine
features, the coarse matcher (multi-view transformer, similarity, match
embedding, DPT head) and the three refiners at one or two scales, in one or
both directions -- matches upstream PyTorch on the same bytes, within the bars
below. `roma::Model::match()` returns the refined warp and confidence;
`roma::RomaMatcher` is the `roma::Matcher` densify uses, with RoMa's presets.
`roma::Model::coarse()` returns the stride-4 warp alone. The output is
bit-identical from run to run (`budget_slicing_test`).

## Rules

- **Vulkan only**, built with `SS_BUILD_SAM` as `ss_roma` (`cmake/SsRoma.cmake`),
  globbing this directory only.
- **Nothing here is a line-by-line translation of the reference's code.**
  RoMa v2's `vit/*.py` carry Meta's copyright under the DINOv3 License
  Agreement. `dpt.py` carries Meta's copyright too, but its header points to
  the repository's LICENSE, which is MIT. `Backbone.cpp`, `Matcher.cpp` and
  `Dpt.cpp` were written from the published architecture and the checkpoint's
  tensor shapes, as `loma/model/Dino.cpp` was for DINOv2. `Rope.cpp` and
  `shaders/roma.slang` were written from the reference's behaviour (its
  coordinate recipe, dtypes and per-op bf16 rounding) and checked against its
  output. `Refiner.cpp` follows `refiner.py` and `local_correlation.py`, which
  are MIT.
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
   fp32 coarse-warp bars on its own (docs/notes/roma-model-validation.md). Parity is therefore read two
   ways:
   - **With the rounding on**, each coarse-warp bar is max(bar, 2 x the floor).
     The floor is that MPS-vs-CPU run, which `compare_torch.py` computes itself
     (`--floor-device auto`) and records, row by row, in its `--json`. The
     factor 2 is a judgement, not a measurement.
   - **With the rounding off** on both sides (`SS_ROMA_ROPE_F32=1` against
     `--ref rope32 --floor-device none`), against the bars unchanged.
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
fp32, with every autocast off (including the ones `dpt.py` and the VGG in
`features.py` hard-code). It is fed the `[0, 1]` images that `roma_model_test`
dumped. It reads only the files listed in the dump's `manifest.json`, and only
from a run that finished with status 0: a stale or failed dump is refused, not
scored. The manifest also records the run's `f16_weights`, `rope_rounds` and
`local_corr_fused`, and the script refuses a `--weights` or `--ref` label that
disagrees with them. It checks the torch-side checkpoint against the SHA-256
`Fetch.h` pins.

Bars (`BARS` in the script): backbone and VGG taps, max relative L2 1e-4 (f32)
or 2e-3 (f16); coarse warp EPE p50 0.01 px, p99 0.1 px, overlap logit 1e-3 (f32;
0.05, 0.5 and 2e-2 for f16). With the rounding on, a bar is max(bar, 2 x the
floor), where the floor is the worst of four draws of torch against itself: torch
on a second device, and torch on the CPU with the inputs perturbed by N(0, 1e-6),
three seeds. The script refuses a floor of fewer than four draws.

```bash
SS_ROMA_DUMP=/tmp/d ./build/roma_model_test --require-model --pair A.png B.png &&
uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32 --json /tmp/d.json
SS_ROMA_ROPE_F32=1 SS_ROMA_DUMP=/tmp/r ./build/roma_model_test --require-model --pair A.png B.png &&
uv run tools/roma/compare_torch.py --dump /tmp/r --weights f32 --ref rope32 --floor-device none
```

The first command needs a second torch device (MPS or CUDA) for the floor.
Without one it stops and says so, rather than scoring against bars that torch
itself misses. `--rect W H` runs a single coarse() on a W x H pair. The
`--ref bf16` reference (DINOv3 in bf16, every autocast on) is context only: this
port computes in fp32 where that reference rounds to bf16.

### Full match

`compare_torch.py --full` runs `RoMaV2.forward` at the dump's lr (and hr) sizes,
bidirectional when the dump has B into A, and reads every refiner stage and the
final warp and certainty. Gated: EPE p50 and p99 over all pixels and over pixels
the reference is certain of (> 0.5), certainty max abs, the share crossing 0.2 or
0.9, and the p99 of the certain-pixel precision error relative to max(|ref|, 1).
Precision is read where the reference is certain because elsewhere it is
arbitrary (torch misses 1e-2 against itself there), and at p99 rather than the
max because the max is one occlusion-edge pixel.

```bash
SS_ROMA_BENCH_BOTH=1 SS_ROMA_DUMP=/tmp/d ./build/roma_model_test --bench precise \
    --pair A.png B.png --repeat 0
uv run tools/roma/compare_torch.py --dump /tmp/d --weights f32 --full
```

## Memory

The arena is planned before a pass runs, and `coarse()` and `match()` fail if
the pass used more than the plan. `roma_model_test` holds every stage (backbone,
fine features, transformer, similarity, DPT head, each refiner) to its own plan
and asserts how many stages each call logs, because the overall maximum is bound
by the DPT head at every size and would hide an under-counted term. Measured
sizes and timings are in the validation note. The 4 GB gate is the arena plus A's
cache for `precise` in both directions.

## The refiners and the full match

`Refiner.cpp` is upstream `refiner.py` and `local_correlation.py` (MIT): project
both VGG maps, sample B's at the previous warp, embed `scale * (warp - grid)`,
add a local correlation, run a depthwise trunk, add the heads' deltas. Four
things a port gets wrong:

1. **The local correlation reads B's projected map, not the warped one**
   (upstream fix #47; the Lichtfeld plugin's vendored copy predates it). The
   mutant reads 1.2 px at the full-match median.
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
   fused, M5 Pro), The two
   agree to 1e-5 px at the median.

Presets (`RomaMatcher.h`): `turbo` 320, `fast` 512, `base` 640, `precise`
800 + 1280 hr (upstream's `apply_setting`), and the plugin's `high`, 640 +
960 hr. A into B does not depend on whether B into A is computed, so
`match()` never computes B into A; `matchBoth()` does. A's backbone taps and
VGG maps stay on the device keyed by its own bytes and sizes, so a reference matched
against several neighbours runs its backbone once.
