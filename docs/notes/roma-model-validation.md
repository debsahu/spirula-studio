# RoMa v2 model: validation record

Measurements behind `src/roma/model/`, dated and tied to the build that produced
them. The method and the bars are in `src/roma/model/README.md`; the scripts are
`tools/roma/compare_torch.py` (parity) and `roma_model_test` (stages, arena,
speed). Numbers here are history, not a contract: rerun the commands in the
README before relying on one.

## Coarse warp and backbone parity, 2026-10-07

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
| backbone taps, max rel L2 | 1.6e-6 | 2.1e-6 | 1.8e-6 | 1.7e-6 | 1e-4 |
| VGG19-BN taps, max rel L2 | 2.8e-6 | 4.7e-6 | 3.2e-6 | 2.7e-6 | 1e-4 |
| coarse warp EPE p50 px | 0.0086 | 0.0130 | 0.0065 | 0.0028 | 2x floor |
| coarse warp EPE p99 px | 0.041 | 0.41 | 0.089 | 0.018 | 2x floor |
| coarse overlap logit, max abs | 0.011 | 0.106 | 0.040 | 0.0098 | 2x floor |
| *floor (MPS vs CPU): p50 / p99 / logit* | *.0096 / .057 / .010* | *.0125 / .42 / .107* | *.0074 / .074 / .143* | *.0045 / .023 / .012* | |

Results with the rounding off on both sides (bars unwidened):

| | C | A | B | R | fp32 bar |
|---|---|---|---|---|---|
| coarse EPE p50 px | 6.4e-5 | 1.3e-4 | 9.7e-5 | 5.0e-5 | 0.01 |
| coarse EPE p99 px | 1.1e-3 | 2.9e-3 | 2.0e-3 | 6.3e-4 | 0.1 |
| coarse logit | 9.2e-5 | 4.2e-4 | 5.8e-4 | 9.7e-5 | 1e-3 |

With f16 backbone weights, measured before f16 was restricted to tensor-core
devices (the floor there was the worst of MPS and two perturbed runs, not MPS
alone):

| | C | A | B | f16 bar |
|---|---|---|---|---|
| backbone taps, max rel L2 | 1.6e-6 | 2.1e-6 | 1.9e-6 | 2e-3 |
| coarse EPE p50 / p99 px | 0.0093 / 0.046 | 0.013 / 0.51 | 0.0064 / 0.11 | 0.05 / 0.5, or 2x floor |
| coarse logit | 0.013 | 0.085 | 0.134 | 2e-2, or 2x floor |

**Against the model as shipped** (`--ref bf16`: DINOv3 in bf16, every
autocast on), this port is an fp32 implementation and does not meet the
bars. That is the price of computing in fp32 where the reference
rounds to bf16, not a parity result.

| | C | A | B | R |
|---|---|---|---|---|
| backbone taps rel L2 | 9.6e-3 | 1.5e-2 | 1.2e-2 | 1.0e-2 |
| coarse EPE p50 / p99 px | 0.16 / 0.61 | 0.30 / 9.9 | 0.24 / 2.1 | 0.20 / 0.73 |
| coarse logit | 0.17 | 0.84 | 0.38 | 0.19 |

Mutants the coarse-warp rows kill on their own under the MPS floor (fixture C), each also
caught earlier by `roma_model_test`:

| mutant | coarse EPE p50 | caught first by |
|---|---|---|
| bf16 truncation instead of round-to-even | 0.11 px | `rope_bf16` |
| one bf16 rounding skipped | 0.037 px | `rope_bf16` |
| DPT align_corners=False | 0.63 px | — |

## Arena and speed of the coarse pass

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

## Full-match parity, 2026-10-07

15 cells (5 presets x 3 fixtures, both directions), M4 Max, on the build after
the split-K fix. Two draws of every cell are bit-identical. Fixtures by SHA-256
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
while the certain-pixel rows stay at the 0.01 / 0.1 px nearly
everywhere. Precision is read where the reference is certain: elsewhere it is
arbitrary, and torch misses 1e-2 against itself there. It is read at p99, not
the max, because the max is one pixel: the traced case was a single
occlusion-edge pixel at certainty 0.03. Every full-match mutant below is caught at
p99 (rows in `mut3b*.log`), two of them only by that row.

| mutant | caught by |
|---|---|
| local correlation fed the warped map (pre-#47) | the EPE, certainty and precision rows, both directions |
| refiner grid_sample `align_corners=True` | the same, except A into B's all-pixel p99 |
| hr precision not zeroed | certain-pixel precision p99 only (2.8 against 0.046) |
| refiner input order swapped | the EPE, certainty and precision rows, both directions |
| softplus -> relu in the Cholesky head | certain-pixel precision p99 only (0.73 against 0.043) |
| softplus without its log1p tail | not at full match; `refine_update_softplus_tail` |

Superseded: the 2026-10-07 matrix before this one ("30 of 30") read a floor of
two draws, MPS and one perturbed seed, on a build whose first match in a process
was not reproducible, and its f16 single-draw verdicts are void. The Macs now
hold the backbone in f32 (no cooperative matrix), so this run has no f16 arm.
`mut.log` of that round predates the final bars.

## Arena and speed of the full match

Basement cube faces. Neither Mac has cooperative matrices; the times below were
taken with the backbone in f16, before it went f32 on such devices (894 against
889 ms a pair). A
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

