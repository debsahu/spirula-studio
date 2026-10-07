# WS-6 evaluation tooling (geometry only, no training)

Scripts that produced the WS-6 numbers in the plan (section 10.5). Written for one session: paths into
the session scratchpad (`/private/tmp/claude-502/.../scratchpad/{ws4,ws6,rv2spike,romav2}`) and the
basement data are hard-coded, so this is a record of what was run, not a portable harness. The densify
runs themselves are the product CLI; `src/roma/model/tests/roma_match_pairs.cpp` is the one C++ tool.

| script | what |
|---|---|
| `parity.py`, `parity2.py` | native `.rwm` vs PyTorch `.rwm`: EPE, certainty, and the pose-aware stratification (Sampson error against the COLMAP poses, independent of both matchers) |
| `epi.py` | Sampson error of a warp against a COLMAP model (face conventions copied from `CameraMath.cpp`) |
| `mutants_parity.py` | mutation check of the parity harness (shift, swapped channels, flipped certainty, identity) |
| `torch_dump.py` | upstream RoMa v2 on exported views, float32 `.rwm`, with the module-level device patched so CPU is CPU |
| `chamfer.py` | P-5 style symmetric nearest-neighbour distance between two clouds |
| `ws4_eval_basement.py`, `integrity.py` | WS-4's G-3b/c/e evaluation, unchanged (copied) |
| `score2.py`, `make_half.py` | independent anchors (50/50 split of the sparse points, half B scores) and the stairs-band measures |
| `g3d.py` | G-3d held-out reprojection, geometric and photometric, each with its shuffle null |
| `outside.py`, `render_outside.py` | classification of the points outside the sparse model's box |
| `render_blind.py`, `render_scene.py` | blind per-arm renders with a randomised name map; whole-scene views |
| `cmp_eval.py`, `floor.py`, `tab2.py`, `tabg.py`, `sumc.py`, `pairdiff.py` | table printers |
| `run_matrix.sh`, `run_dump_arm.sh`, `build_*.sh` | the run drivers and the serialised builds |

The consent-skipping checkpoint hook that WS-6 used for its native runs lives only on branch `ws/6-eval`
(commit `6bc4bb28`). It must never be merged: the product consent flow is `ensure_checkpoint`.
