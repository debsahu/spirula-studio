# `spirula densify`: dense points for a solved model

A dense, multi-view-triangulated, coloured point cloud for a COLMAP model whose
poses must not move. The input is the model and its images; dense matches between
image pairs come from RoMa v2 (`src/roma/Matcher.h` is the seam). The output is a
**sibling model**, `sparse/<model>-roma/`:

| file | what |
|---|---|
| `cameras.bin`, `images.bin` | the source's, copied **byte for byte** and verified (`sfm::checkFixedModel` plus a byte compare); a mismatch throws and leaves nothing behind |
| `gauge.txt`, `rigs.txt`, `rigs.bin`, `frames.bin` | copied when the source has them |
| `points3D.bin` | the dense cloud. Tracks are **empty**: `images.bin` is not touched, so there are no 2-D rows for a track to point at |
| `points3D_tracks.bin` | every observation: `"RTK1"`, u64 count, then per point u64 id, u32 n, n x (u32 image id, f32 x, f32 y) in the source image's pixels |
| `densify.json` | every setting as resolved, the per-filter counts, the source files' SHA-256 |

The trainer reads it with `--colmap-recon-dir sparse/0-roma`. The parser's
automatic pick never lands on it: models are sorted by image count, then by path,
`images.bin` is identical so the counts tie, and `sparse/0` sorts before
`sparse/0-roma` (`densify_autopick_test`).

## Pipeline

1. **Images**: registered images, sorted by name. `--holdout-every N` takes every
   N-th out entirely: it is never a reference and never a neighbour.
2. **Views**: an equirectangular image becomes six 90-degree pinhole cube faces
   (`camhost::equirect_face_axes`), with a native size of a quarter of the panorama
   width. Every other image is one view. The matcher gets each view resampled to its
   `inputSize()` square (a supersampled box filter), masked to black.
3. **References**: greedy maximum new-sparse-point coverage over the images. A lazy
   evaluation that is exact, because a score can only fall
   (`refs_by_visibility_is_greedy_coverage` checks it against the plain greedy).
   Farthest-first over poses when the model has no points.
4. **Neighbours**: `covis` (the default) takes the k images sharing the most sparse
   points whose centres subtend at least the parallax bar at the shared points'
   centroid; `pose` is the plugin's nearest flattened pose, padding covis when it
   is short. On a model whose `gauge.txt` says metric, neighbours farther apart than
   3x the median are refused.
5. **Face pairs**: a reference face pairs with the neighbour faces whose optical
   axes are within 60 degrees of its own. Two whole images always pair.
6. **Certainty** per pair: RoMa's, times the reference mask, times the neighbour's
   mask where the warp lands (`grid_sample` nearest, zeros outside).
7. **Samples** per reference view: 85 % drawn by certainty (capped at 0.9) without
   replacement, 15 % one per tile, 2 px border excluded (`Sample.cpp`).
8. **Per neighbour**: certainty floor, inside-the-image, Sampson on bearings
   (squared radians, rescaled from px^2 at the match resolution), DLT on bearings,
   reprojection through each view's own camera, cheirality, parallax.
9. **Fusion**: the largest mutually consistent set of candidates (each reprojects
   into the others' observations within the bar), averaged by 1/error. When no two
   neighbours agree the sample is dropped.
10. **Support**: the fused point is re-checked against the reference and each
    candidate's view. Tracks count **distinct source images**, so two faces of one
    neighbour panorama are one observation.
11. **Precision**: a point whose best pair moves its depth by more than
    `max_depth_error` (2 %) per match pixel is dropped: `1 / (f sin(parallax))`, with
    `f` the coarser of the two views' focal lengths in match pixels.
12. **Finalize**: points with fewer than `min_track` images are dropped (3 with 3 or
    more neighbours, else 2). Then one point per voxel (the longest track, then the
    lower error), then a seeded cap.

Every automatic setting is printed, and recorded in `densify.json`:

| setting | auto |
|---|---|
| samples per view | `clamp(max_points / reference views, 2000, 50000)` |
| max points | `clamp(4 x sparse points, 1 M, 8 M)` |
| voxel | half the sparse points' median nearest-neighbour spacing |
| reprojection | 1 px at the match resolution |
| Sampson | 5 px^2 at the match resolution |
| parallax | 1.5 degrees |
| min track | 3 images with 3 or more neighbours, else 2 |

## Ported from the Lichtfeld densification plugin

GPL-3.0-or-later, commit `ab0b04e` (LICENSES/NOTICE-Lichtfeld-Densification-Plugin.txt).
The matcher is not ported from the plugin: it is upstream RoMa v2 (`src/roma/model/`).

| plugin | here |
|---|---|
| `core/config.py` `DensePipelineConfig` | `roma::DensifyOptions` |
| `core/selection.py` `select_cameras_by_visibility`, `select_cameras_kcenters`, `nearest_neighbors` | `Select.cpp` `refsByVisibility`, `refsKCenters`, `neighboursByPose` |
| `core/sampling.py` `select_samples_with_coverage` | `Sample.cpp` `sampleWithCoverage` |
| `core/pipeline.py` `_collect_reference_matches` | `Densify.cpp` `collectCertainty` |
| `core/pipeline.py` `_triangulate_ref` | `Densify.cpp` `triangulateRef` |
| `core/geometry.py` DLT, reprojection, Sampson, cheirality, parallax | `Densify.cpp` (plugin mode), `sfm/geometry/` (default) |
| `densify.py` `_voxel_select_track_preserving`, `_apply_point_cap`, `_apply_track_filter` | `Densify.cpp` `voxelSelect`, `finalizePoints` |
| `core/writers.py` `write_sparse_model_bin` | not ported: it rewrites every camera as PINHOLE, which would corrupt an equirectangular or fisheye model. `DensifyRun.cpp` `writeSibling` copies the source's bytes instead |

### Where the default mode departs from the plugin (`--plugin-exact` reproduces it)

| plugin | default | why |
|---|---|---|
| pixel mapping `(u + 1) / 2 * (w - 1)` | `(u + 1) / 2 * w`, COLMAP's continuous coordinates | a half-pixel bias at the edges, times the camera-to-match scale (12 px at 15520 wide). Note: in COLMAP's convention, where pixel `j`'s centre is `j + 0.5`, the correct mapping has **no** `- 0.5` |
| thresholds in camera pixels (0.8 px, 5 px^2) | the same numbers at the **match** resolution | at 3840 px faces matched at 640, 0.8 px is 0.13 px of the matcher; on the basement spike that dropped 90 % of samples |
| two defaults: dataclass 0.8 px / argparse 1.5 px, 10000 / 12000 matches, 3 / 4 neighbours | one set (above) | |
| certainty `clamp(min=0.2)`, a floor that removes nothing | a true filter at 0.2, also per neighbour | a floor makes every unmasked pixel sampleable, and lets a neighbour that does not see the point triangulate with it |
| Sampson skipped for any non-pinhole pair | on bearings, never skipped | |
| float32 pixel DLT | bearing DLT in double | |
| every candidate averaged | the largest consistent set | measured on S-1 below |
| track length in views, kept at 1 | distinct source images, at least 3 | |
| no precision bar | `max_depth_error` 2 % | measured on S-1 below |
| voxel select off, after the cap | on (auto size), before the cap | |
| colour from the match-resolution, masked reference | bilinear from the full-resolution source image | |
| neighbours by nearest flattened pose | by covisibility with a parallax prior | the pose rule is blind to viewing direction |

The plan proposed letting a short track survive alone in its voxel past 2 degrees
of parallax. It is implemented (`DensifyOptions::lone_parallax_deg`) and **off**: on
the S-1 staircase with 3 % random matches and no noise, it kept 49 points more than
5 cm off a surface, every one of them a two-view floater, against 0 without it.

## In the GUI

- **New Dataset screen**: "Add dense points (RoMa v2)", after the depth-and-normals
  box. It is a step of the run (`Step::Densify`, between the reconstruction and
  geometry, `DensifyRunner.h`): a `spirula densify` child with the same plan row,
  record, redo button and batch check as geometry. Every override is 0 for "the
  tool chooses", which is also what the CLI does. A preset file carries the settings
  but never the source model, which belongs to one capture.
- **Training screen**: "Add Dense Points" is the generalised `RecomputePanel`
  (`Kind::Dense`), for a dataset that is already built. The **Model** combo under it
  lists every COLMAP model of the dataset with its image and point counts and sets
  `colmap_recon_dir`, so `sparse/0-roma` trains with one click; the dataset reloads
  and the preview's point count follows.
- **Licences**: the first run, or the "Get the RoMa v2 checkpoint" button, calls
  `GuiApp::request_licenses({"romav2", "dinov3"}, ...)`: the MIT text, then the DINOv3
  Agreement with its tick, then the download. Cancelling either fetches nothing and
  logs that no dense points were made. The child inherits the acceptance through
  `gui.conf`.
- **Presets**: the combo is sent as `--preset` only when `spirula densify --help` lists
  the flag, and is disabled with a tooltip until then.
- Gate: `tools/roma/densify_gui_gate.py` (`tools/guictl.py` on a 6-image fixture).

## Checks

- `roma_densify_test`: the stage on inputs whose answer is known, each test naming the
  wrong implementation it catches. Every named mutation was run and fails the test
  that names it.
- `roma_plugin_parity_test <fixture>`: gate P-4, against the plugin's own host stage on
  the same matches (`reference/python/roma_plugin_parity.py` writes the fixture).
- `densify_autopick_test`: gate H-3.
- `spirula densify --check`: S-1, the synthetic staircase end to end, through the
  plan, the run and the writer. It uses a geometric oracle by default; `--matches`
  runs it on real RoMa matches of the same scene (export the pairs with
  `spirula densify <check dir> --refs 1 --export-pairs`, match them with
  `reference/python/roma_dump_matches.py`).

## Measured (2026-10-07, upstream RoMa v2 `base` matches dumped on an M4 Max)

**P-4, against the plugin (54 basement face pairs, 18 reference faces, 163,422 samples).**
Certainty after collect: bit-identical. Point decisions agree on 99.994 % of samples, the
candidate decisions (sample x neighbour) on 99.998 %, and the count is within +0.025 %.
Per-candidate xyz p99.9 2.0e-5 m (scene 10.5 units). The **fused** points miss the
pre-registered 1e-5 x diameter bar (max 5.4 mm, p99.9 0.47 mm), and the fixture shows
why: the plugin's fusion of its *own* candidates reproduces its points to 7.6e-6, and ours
fused with *its* error weights to 3.2e-4. The weight `1 / max(err, 1e-4)` turns a
two-view residual that a DLT drives to rounding noise into the averaging weight, so a
millipixel of float32 noise moves a point by millimetres. The default mode weighs by depth
precision instead.

**S-1, the synthetic staircase.** With the geometric oracle (0.1 px noise at 640, 3 %
random matches), every gate passes: 99.8 % of points within 1 cm, 0 beyond 5 cm, riser cover
0.906. Upstream RoMa v2 on the rendered views passes 99.46 % within 1 cm and riser cover
0.934. It **misses** "zero beyond 5 cm" with 30 points (0.045 %). Unmasked, the same matches
put 4,059 points beyond 5 cm, 99.5 % of them within 4 px of the empty background: RoMa
fattens silhouettes into a textureless void, and masks (a sky mask on a real capture) are
what remove it.

**Basement, every 8th panorama held out, 102 references x 6 faces, 2,056 pairs, the steps ROI.**
At the automatic settings (2,000 samples per view, minimum track 3) the cloud has 200k points.
Three seeds give anchor p50 41.6, 42.0 and 43.3 mm. Those are coverage figures, not
accuracy: DA360 plus 1 cm of noise moves the p50 by 1-4 %, so this metric cannot see accuracy
at all. At 10,000 samples the anchor p50 is 24.7 mm (DA360, all frames: 15.9; DA360, same
references: 20.3). With `--min-track 2` the anchor p50 is 15.8 mm, and it is the only setting
that keeps the lower flight: a minimum track of 3 drops 87k of the 111k points in the ROI,
because with 3 neighbours, one of them held out, most of the stairs are seen by two panoramas.
At `--min-track 2` the free-space violation rate is 0.40 % against DA360's 1.88 %, and the
synthetic floater null reads +0.82 pp for 1 % floaters. The local-plane thickness is 7.6 mm
against 10.2 mm, and its 1 cm noise null moves it by only 1.5 mm. On S-1 the same change costs
130 points beyond 5 cm against 30. The default stays 3, as the plan has it, and this tradeoff
is the operator's call.
