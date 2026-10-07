# `spirula densify`: dense points for a solved model

A dense, multi-view-triangulated, coloured point cloud for a COLMAP model whose
poses must not move. The input is the model and its images; dense matches between
image pairs come from RoMa v2 (`src/roma/Matcher.h` is the seam). The output is a
**sibling model**, `sparse/<model>-roma/`:

| file | what |
|---|---|
| `cameras.bin` | the source's, copied byte for byte |
| `images.bin` | the source's, byte for byte **except every 2-D point's `point3D_id`, which is set to COLMAP's invalid id** (`detachPoints`). Kept, they would name source points that `points3D.bin` does not hold, and alias unrelated dense points (COLMAP saw a mean track length of 0.148 on such a sibling). Verified: poses by `sfm::checkFixedModel`, everything else by a compare that only the cleared ids may differ in |
| `gauge.txt`, `rigs.txt`, `rigs.bin`, `frames.bin` | copied when the source has them |
| `points3D.bin` | the dense cloud, tracks empty; `colmap model_analyzer` reads it as a consistent model with no observations |
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
   into the others' observations within the bar, counted in distinct source images),
   averaged with weight `1 / depth_per_px^2`, the inverse variance of the depth a pair
   fixes. When no two images agree the sample is dropped.
10. **Support**: the fused point is re-checked against the reference and each
    candidate's view. Tracks count **distinct source images**, so two faces of one
    neighbour panorama are one observation.
11. **Precision**: a point whose best pair moves its depth by more than
    `max_depth_error` (2 %) per match pixel is dropped: `1 / (f sin(parallax))`, with
    `f` the coarser of the two views' focal lengths in match pixels.
12. **Finalize** (minimum track, `--min-track auto`): a point seen in 3 or more images
    is kept; a two-image point only when its reprojection error is no worse than the
    median error of the run's own 3-or-more-image points; one image never. Then one
    point per voxel (the longest track, then the lower error), then a seeded cap.

Every automatic setting is printed, and recorded in `densify.json`:

| setting | auto |
|---|---|
| samples per view | `clamp(max_points / (0.16 x reference views), 2000, 50000)`; 0.16 is the measured yield (points per sample) on the basement |
| max points | `clamp(4 x sparse points, 1 M, 8 M)` |
| voxel | half the sparse points' median nearest-neighbour spacing |
| reprojection | 1 px at the match resolution |
| Sampson | 5 px^2 at the match resolution |
| parallax (`--parallax`, per point) | 1.5 degrees |
| neighbour prior (`--covis-min-angle`) | 1.5 degrees at the shared sparse points |
| min track | auto (above); 2 with a single neighbour |
| max depth error | `max(2 %, 1 / (f sin(theta / 2)))`, `theta` the median reference-neighbour angle and `f` the median view focal in match pixels |
| coarse warps | a warp smaller than the matcher's input (RoMa's stride-4 coarse match) scales the pixel thresholds into warp pixels, and the run says so |

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

The plan proposed letting a short track survive alone in its voxel past 2 degrees of
parallax. It was measured on S-1 (it kept 49 two-view floaters more than 5 cm off a
surface, against 0 without it) and removed.

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
Like for like since 2026-10-07: the C++ side reads the plugin's float32 R, t and K and
rounds `K [R|t]` and the camera centres to float32 as the plugin holds them. Certainty after
collect is bit-identical; point decisions agree on 99.996 %, candidate decisions on
99.9992 %, the count within +0.025 %. Each neighbour's candidate point: p50 2.0e-7 m, p99.9
1.6e-5 m, max 2.8e-4 m (scene 10.5 units; the bar is 1.05e-4), with the plugin's DLT in
float64 (`--f64-dlt`); with its float32 SVD, p99.9 2.7e-5, max 3.5e-4. The **fused** points
still miss 1e-5 x diameter: max 2.85 mm, p99.9 0.21 mm (were 5.35 / 0.47 with float64 poses).
The plugin's fusion of its own candidates reproduces its points to 7.6e-6, ours fused with
its error weights to 2.8e-4: the gap is the weight `1 / max(err, 1e-4)`, which turns a
residual that a DLT drives to rounding noise (candidate error differences of 7e-4 px here)
into the averaging weight. What is left per candidate is not explained; float32 rounding
order in the plugin's `K @ [R|t]` and torch's `linspace` are the untested suspects.

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
130 points beyond 5 cm against 30.

### The automatic minimum track (2026-10-07, reviewer's rule)

Same matches and the same build; `--min-track 3`, `auto` and `2`. Auto samples per view
is now 10,212 and the 1 M cap binds, so the point counts share a cap.

| steps ROI | 3 | **auto** | 2 | DA360 all frames | DA360 same refs |
|---|---|---|---|---|---|
| points | 26,655 | **41,232** | 44,682 | 355,760 | 168,771 |
| sparse-anchor p50 / p90 mm | 24.2 / 334.4 | **20.7 / 58.1** | 20.6 / 49.1 | 15.9 / 28.7 | 20.3 / 41.6 |
| anchors within 5 cm | 0.668 | **0.868** | 0.904 | 0.991 | 0.935 |
| free-space violations | 0.51 % | **0.35 %** | 0.41 % | 1.82 % | 2.49 % |
| outside the sparse box | 0.54 % | **1.36 %** | 1.82 % | 5.8 % | 3.7 % |

The two-image bar was 0.518 px (the median of 3-image points), and it admitted 1.26 M
two-image points before the voxel select and the cap. The steps-ROI floater rate did not
rise against `--min-track 3`, which was the condition for making it the default; the
whole-scene outside-the-box share did rise, 0.54 % to 1.36 %. The floater null (1 % synthetic
floaters added to DA360) reads +0.82 pp. On the real-RoMa S-1: riser cover 0.955, 70
points beyond 5 cm (`3`: 0.934 / 30). On the oracle S-1 one two-view outlier survives: a
random match on its epipolar line has a near-zero two-view error, which is the case the
rule cannot see.

### `max_depth_error` on a narrow capture

aerial0720 (`sparse_seg1_1200/0`, read only): the median angle between a reference and its
three most covisible neighbours is **3.17 degrees** (p10 1.85, p90 9.06, 450 pairs). A fixed
2 % keeps only pairs past ~9 degrees at f = 320 match pixels, nearly nothing there; the
automatic bar is 11.3 % there and 2.6 % on the basement (13.8 degrees).
