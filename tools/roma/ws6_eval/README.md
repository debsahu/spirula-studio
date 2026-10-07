# WS-6 evaluation tooling (geometry only, no training)

Scripts that produced the WS-6 numbers in the plan (section 10.5). They are re-runnable: no script opens a
path relative to the current directory or names a machine-specific location. Every input is a flag, and
every flag has a default hung off the **data root**.

The densify runs themselves are the product CLI; `src/roma/model/tests/roma_match_pairs.cpp` is the one C++ tool.

## Data root and path rules (`evalcfg.py`)

* Root = `--root`, else env `SS_EVAL_ROOT`, else the nearest ancestor of this directory that contains
  `work/ws6_eval` (i.e. `<checkout>/work/ws6_eval`). All defaults below are `<root>/...`.
* A missing input stops the run before any work, with a message naming the path and the flag that sets it:
  `error: spike directory not found: /x/spike ... set it with --spike-dir (default <root>/spike, root = ...)`.
  There is no fallback to a second location.
* Run the scripts as `python3 <script>`; do not use `python3 -I` (sibling modules are found through the
  script's own directory). Needs numpy, scipy, PIL; `torch_dump.py` also needs torch and `romav2`.
* `--help` works on every Python script and shell driver.

## Where each input comes from

| input | flag | default | origin |
|---|---|---|---|
| spike dir: `roi.py`, `steps_roi.json`, `model.pkl`, `da360_seed_only.npy` (`views_steps.json` is kept beside them; no script reads it) | `--spike-dir` | `<root>/spike` | copied from the WS-3 RoMa v2 spike; small, lives under git-ignored `work/` |
| M3 cloud (DA360, same refs), COLMAP `points3D.txt` | `--m3` | `<root>/spike/m3_points3D.txt` | copied from the WS-4 M3 run |
| densify output dirs ("siblings": `points3D.bin`, `points3D_tracks.bin`, `images.bin`, `cameras.bin`, `densify.json`) | positional / `label=dir` | none (always given) | the product CLI; `<root>/clouds/*` keeps only `points3D.bin` + `densify.json`, so scripts that need tracks or images (`outside.py`, `ws4_eval_basement.py`, `integrity.py`) need the full sibling dir under the dataset's `sparse/` |
| source COLMAP model for poses / integrity (`images.bin`, `cameras.bin`) | positional `<source>`; `--poses-model` (g3d), `--model` (mutants) | `<root>/basement_ds/sparse/0` where a default exists | the dataset the siblings were densified from |
| match dirs (`.rwm`) | `--matches` (g3d), positional (parity, parity2, pairdiff) | `<root>/g3d_matches` (g3d) | `roma_match_pairs` / `torch_dump.py` output |
| exported views (`views/<name>.png`, `pairs.txt`) | `--export` (g3d), positional (torch_dump) | `<root>/export_all` | `spirula densify --export` |
| held-out face list | `--g3d-meta` | `<root>/g3d_export/meta.json` (key `held`) | the g3d export |
| half-B anchor ids | `--half-b-ids` | `<root>/halfB_ids.npy` | `make_half.py` (seed 7 reproduces the published file) |
| eval jsons | `--eval-dir` | `<root>/eval` | `ws4_eval_basement.py` |
| native / torch match dirs to mutate (`mutants_parity.py`) | `--native`, `--torch` | `<root>/native_m5_sub`, `<root>/matches` | guessed locations, pass them |
| densify binary (drivers) | `--bin` | none | the candidate build; changes per run |

## Running the battery

```
R=<root>   # optional; default resolves by itself
# 0. matches: native (roma_match_pairs, product CLI) or torch
python3 torch_dump.py <export_dir> <out_dir> --device cpu|mps
# 1. densify arms
./run_matrix.sh --bin BIN --work DIR --ds DS --dsh DSH --matches DIR --dsh-matches DIR [--seed 0]
./run_dump_arm.sh --bin BIN --ds DS --matches DIR --work DIR [--seeds "0 1 2"]
python3 make_half.py <source model> <dsh/sparse/0> 7        # makes the half-anchor dataset (refuses to overwrite)
# 2. match parity
python3 parity.py  <native_dir> <torch_dir> <out.json>
python3 parity2.py <native_dir> <torch_dir> <model_dir> <out.json>
python3 mutants_parity.py --native DIR --torch DIR --model DIR [--work-dir DIR]
python3 epi.py <model_dir> <pair.rwm> label=dir ...
python3 pairdiff.py <pair.rwm> label=dir ... --ref LABEL
python3 sumc.py <parity.json>
# 3. cloud evaluation (G-3b/c/e, integrity first: a failed integrity check voids the run)
python3 integrity.py <sibling> <source model>
python3 ws4_eval_basement.py <sibling> <source model> <out.json> [--no-m3] [--anchor-mod N]
python3 cmp_eval.py NAME ...        # <eval-dir>/NAME.json side by side
python3 floor.py [--arms dump native] [--seeds 0 1 2]
python3 score2.py label=<sibling|.npy|.txt> ... --out out.json [--half-b-ids F]   # label M = <spike>/da360_seed_only.npy, M3 = <m3>
python3 tab2.py <score2.json>
python3 g3d.py label=<sibling> ... --out out.json [--matches D --export D --g3d-meta F --poses-model D] [--limit-pairs N]
python3 tabg.py <g3d.json> ...
python3 chamfer.py <sibling> <sibling> ... [--diam 10.51]
python3 outside.py <sibling> <out.json>          # writes out.json and out.npz
# 4. renders
python3 render_outside.py <sibling> <outside.npz> <out.jpg>
python3 render_scene.py <outdir> label=path ...
python3 render_blind.py <outdir> label=path ...  # blind codes; open MAPPING_do_not_open_before_ranking.json only after ranking
# builds (serialised through /tmp/spirula-build.lock)
./build_ws6.sh <worktree> <logfile> <target> [target...]
./build_m4.sh  <ws_dir> [target...]              # on the M4 Max
```

`score2.py` and `g3d.py` take the M and M3 arms from the spike dir and `--m3`; `g3d.py` always includes both.
`g3d.py --limit-pairs N` evaluates only the first N pairs and records `limit_pairs` in the output; such a run
does not reproduce a published number.

The consent-skipping hook that WS-6 used for its native runs lives only on branch `ws/6-eval`
(commit `6bc4bb28`). It must never be merged: the product consent flow is `ensure_checkpoint`.
