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
| `points3D_normals.ply` | when the depth source ran: binary PLY, float x y z nx ny nz and the uint `point3D_id`, one vertex per point; unit world normals from the depth source, `0 0 0` where none (COLMAP's `points3D.bin` has no normal field) |
| `densify.json` | every setting as resolved, the per-filter counts, the source files' SHA-256, and `reprojection` (below) |

**An empty cloud is an error**: nothing is written and the exit is non-zero, whatever
the source. A sibling sorts after its source, so an empty one could be picked and
trained from nothing.

**`reprojection`** is measured on the files as written: each point of `points3D.bin`
through its image's own camera and pose (`cameras.bin`, `images.bin`) at the pixels of
`points3D_tracks.bin` (`reprojectWritten`). It shares nothing with the triangulation and
covers the face-to-panorama mapping, which only happens at write time. `--check` gates it
(no invalid observation, p95 at most 2 source pixels). It found a real defect on its first
basement run: a depth point's track was the projection of the reference's own point, not of
the averaged one written, and reprojected at p95 49.5 px; after the fix, 0.0004 px. **For a
depth point the number is now tautological**: its track is defined as the projection of the
written point, so it can only be wrong through the face mapping or the writer. It says nothing
about whether a depth point is on the surface. For a matched point it is the triangulation
residual in source pixels.

An empty run under `--overwrite` also removes an older sibling of the same name, which would
otherwise be trained from as if it were this run's.

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
    median error of the run's own 3-or-more-image points **and no other image saw
    through it**: per source image, a 512-wide grid holds the nearest distance at which
    it observed a 3-image point, and a two-image point more than 5 % in front of that
    (the nearest over 3x3 cells, so a depth edge does not veto), in an image outside its
    own track, is dropped; a cell with no observation is no evidence. One image never. Then one
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
| max depth error | `max(2 %, 1 / (f sin(theta / 2)))`, `theta` the median reference-neighbour angle and `f` the median view focal in match pixels. A heuristic: the half angle is chosen so a pair at half the capture's median parallax survives one match pixel of error; it is not fitted |
| coarse warps | a warp smaller than the matcher's input (RoMa's stride-4 coarse match) scales the pixel thresholds into warp pixels, and the run says so |

## Ported from the Lichtfeld densification plugin

GPL-3.0-or-later, commit `ab0b04e3` (LICENSES/NOTICE-Lichtfeld-Densification-Plugin.txt).
`--plugin-exact` means that commit's behaviour and the parity test stays pinned to it.
Upstream has moved since: at v0.10.1 the certainty floor is a zeroing `where` rather than
the `clamp(min=)` reproduced here. A review of `ab0b04e3..v0.10.1` (2026-10-07) found none
of the defects listed below fixed upstream, so every departure stands.
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

## Depth source: MoGe-2 maps, and the hybrid (WS-4d, 2026-10-07)

`--source auto|roma|moge|hybrid`. Auto is `hybrid` when the dataset has depth maps and a
matcher is available, `moge` with depth maps only, else `roma`; the choice is printed. The
licence gate applies to RoMa only. The GUI has a "Points from" combo (dataset step and the
training screen's Add Dense Points row) that sends `--source`; Auto sends nothing and says which it will
pick for the dataset. Only moge skips the RoMa/DINOv3 licences and the checkpoint, so the readiness gate,
the Run button and a batch's up-front licence prompt all read the chosen source. The source is a preset
field and a recorded densify field (a record from before it counts as auto). Gate:
`tools/roma/densify_source_gate.py`.

**Depth maps** (`DepthSource.h`): the `spirula geometry` maps in `--depth-dir` (default
`depths/`), 16-bit, relative or millimetres (both linear in depth, so the fit absorbs which),
ray depth where geometry splits the lens into faces. 0 is no data and stays no data, so a
sky-blanked `depths_nosky/` works through `--depth-dir`. **Reuse**: maps present are kept. When
some are missing, the folder is geometry's own `depths/` **and the source was named**
(`--source moge` or `hybrid`), `spirula geometry --depth --no-normal` runs once for the missing
ones; the present maps are read-only while it runs, and `ensureDepths` refuses the run if any of
them changed size or time anyway. **`--source auto` never starts a MoGe run**, with no maps or
with some missing: it says how many images have none and uses what is there. The count reused /
computed / still missing is printed. Another folder is never extended (a sky-blanked copy
cannot be).

**Maps for other pictures** (review B1, 2026-10-07). Depth reuse makes a stale or shifted
`depths/` likely after a re-extract, and such maps are plausible depth: with the folder shifted
by 3 frames, 17 of 127 passed every fit gate and the run wrote 38,951 points, 53 % of them near
the real geometry (99.8 % normally), with exit 0. Three defences:
- **geometry's record**: `spirula geometry` now writes `depths/geometry_maps.tsv` (`app/DepthManifest.h`):
  per map, the image it was made for, a fingerprint of the image and of the map (size and
  SHA-256 of the first and last MiB, so a copied dataset keeps them), and whether it is ray
  depth. Densify refuses a recorded map that names another image, that changed since, or whose
  image changed since; the ray/z choice comes from the record instead of being inferred. Maps
  made before the record existed are not checked by it.
- **the map itself**: refused if it is not 16-bit, or not the camera's shape (aspect within 1 %).
- **the share**: if fewer than half of the matched (non-held-out) images have a usable map
  (`--min-depth-share`, 0 accepts any), a named depth source is an error, and `auto` drops the
  depth source with a warning and runs on matches alone.

**Fit**, per image, to its own sparse points: `1/d_true = a / d_raw + b` (disparity affine),
2-point RANSAC on relative depth error (10 %), Huber IRLS. Refused with fewer than 30 anchors,
20 inliers, half the anchors, a rank correlation under 0.5, or more than 5 % of the pixels lost
to a non-positive disparity.

**Normals**: from `--normal-dir` (default `normals/`, geometry's 8-bit encoding, black = none)
where an image has one, else from the fitted depth with the engine's stencil
(`(x+ - x-) x (y- - y+)`, `ScanDepth.cpp`). Every normal is faced to **its own pixel's ray**,
never by `n.z`: on a 90-degree face the `n.z` rule inverts most of a grazing wall
(`normals_face_their_own_ray` is that wall). A normal map is used only when its median cosine
against the image's own depth normals is at least 0.9, **measured before facing**: faced, any
field lands in the camera's hemisphere and an unrelated map scores about 0.5. Measured on the
basement (vitl): 21 geometry normal maps 0.995 to 0.999 (the convention matches: camera frame,
OpenCV axes); a real map of the panorama 7 frames away 0.57 to 0.97 (a handheld rig sees the same
room the same way up, so a misnamed map is not reliably caught; the convention is); a photo
passed off as a normal map -0.58 to 0.13. The count from files, from depth, and rejected is printed.

**Candidates** per reference view, at the samples of a `match_size` grid: the fitted point,
then a vote of every other usable image, each comparing its own fitted map at the point's
projection (relative): **agree** within `tol`, **see through** past `2 tol` in front, **hide a
copy** when the map is up to 25 % behind the point. Kept with at least 2 agreeing and more agreeing
than either other vote. `tol` is 2.5 x the median fit residual, within 1 to 5 % (4.77 % on the
basement); `--depth-tol`, `--depth-min-agree` and `--no-depth-vote` set them. **A voter must see
the point from at least the parallax bar (1.5 degrees) away from the reference**: an adjacent
frame shares the reference's monocular error and would agree with it whatever the truth. *Deviation*: the brief asked for a hard "seen through" veto; it deleted the true
stairs whenever one image's map was a doubled copy too deep, since that image sees through every
true point in front of it. A vote, not a veto. An **edge filter** drops a sample whose 5 x 5 map
neighbourhood has no-data or spreads more than `4 tol` (silhouettes against the void, grazing
floors). The point is the mean of the agreeing images' fitted points; its **track is where that
mean lands** in each agreeing image (the written-reprojection check found the earlier
reference-ray track at p95 49.5 px).

**Normal agreement** (`--normal-check <deg>|off`, default 55): a candidate whose normal is
further than that from the median of the agreeing images' normals at it is dropped. 55 is the
angle where the agreeing-pair angle density falls to the null's, the null being the same
image's normal at a random pixel (basement vitl, 32 M pairs, 5-degree bins, `densify.json`
`normals.agree_5deg` / `null_5deg`): the ratio is 5.6 at 0-5 degrees, 2.5 at 30-35, 1.09 at 50-55,
0.88 at 55-60. The null is Manhattan (13 % of random pairs at 85-90 degrees), which is why it
is not a uniform sphere.

**Hybrid** = the matches, plus depth points only where no neighbour's warp is certain for that
reference, and only where the fill agrees with the matched and sparse points near it: the
median relative residual of the fitted map at those points (+-8 match cells) within `tol`, and
the fill's normal within the normal threshold of the plane through those points within 6 % of
the range (+-24 cells; 6 or more points, flat: least spread under a tenth of the next). On the
basement the plane test sees 26,357 of 4.45 M fill samples. In the voxel select **a matched
point beats a depth point** (the depth's track is every agreeing image, not a measurement).
Depth points are **not free-space evidence**: an image's map too deep there would veto true
two-image matches in front of it. With the automatic cap the **fill gets its own budget**, a
quarter of the matches' (the matches alone fill 1 M on the basement, which left the first hybrid
with no fill at all); an explicit `--max-points` is one budget, matches first. Normals of every
written point that has one go to `points3D_normals.ply`.

**Doubled layers** (review S4): a fill is placed where *this* reference has no certain match, but
another reference's matches may already measure that surface, and a fill a centimetre off them is
a second layer the voxel select cannot merge. So after all references a depth point within
4 voxels (twice the sparse spacing, a choice) of any matched point is dropped
(`dropFillNearMatches`, counted as `fill_near_matches`).

### Measured: basement steps ROI (vitl maps, 2048 x 1024, 146 panoramas, holdout every 8)

| arm | stair points | anchor p50 / p90 mm | anchors within 5 cm | free-space violations | thickness p50 mm |
|---|---|---|---|---|---|
| moge | 30,700 | 51.8 / 429.7 | 0.487 | 1.13 % | 9.17 |
| moge, normal check off | 35,049 | 47.3 / 431.7 | 0.513 | 1.16 % | 9.68 |
| **hybrid** | **49,777** | **19.9 / 53.5** | **0.885** | 0.38 % | 9.24 |
| hybrid, normal check off | 50,776 | 20.2 / 52.8 | 0.885 | 0.44 % | 8.99 |
| roma | 40,744 | 20.9 / 55.8 | 0.879 | 0.31 % | 8.90 |
| DA360 seed (M) | 355,760 | 15.9 / 28.7 | 0.991 | 1.82 % | |
| DA360 3-view (M3) | 168,771 | 20.3 / | | 2.50 % | |
| sparse | 2,174 | (the anchors) | | | |

The vitb maps (1064 x 532) were worse for moge (p50 81.7 mm, within 5 cm 0.377). Side profile
(`render_profile`): DA360 shows the doubled stair line; hybrid and roma show one; **moge shows
none, because it has almost no stairs**: the MoGe stairs do not survive the vote at this
tolerance. So moge alone is not usable on this scene and the doubled-line gate is passed by
absence there, not by quality. The hybrid is the arm to use: 22 % more stair points than roma
and anchors as good or better, at 0.07 pp more violations. The normal check is within noise on
anchors in both arms; it lowers hybrid violations (0.44 -> 0.38 %) and raises plane thickness
(8.99 -> 9.24 mm): mixed, kept on as specified, open.

S-1 through each source (stand-in matches, synthetic maps with a per-image disparity-affine
error, noise and three images with the risers 5 % too deep): roma 83,808 points / 0.996 within
1 cm / riser 0.953 / 0 beyond; moge 75,985 / 0.997 / 0.912 / 0; hybrid 87,639 / 0.996 / 0.945 / 0.

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
- WS-4d mutation run (each fails the test named): normals faced by `n.z` (derived and
  `faceCamera`), normal maps ignored, depth computed with nothing missing, a present map
  rewritten, normal agreement off, hybrid plane test off, alignment off, vote off, sentinel
  taken as depth, hybrid fill everywhere, hidden-copy vote off, depth track at the reference
  ray, depth point winning a voxel, depth points as free-space evidence, fill sharing the cap,
  empty cloud written, face mapping turned the wrong way, sample `nonzero` clamp dropped,
  certainty threshold strict, mask before the plugin floor, neighbour certainty ignored.
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

### The visibility test (2026-10-07)

Same matches and build, `--min-track auto` with the free-space test against `3`:

| steps ROI | 3 | auto + visibility |
|---|---|---|
| points | 26,655 | 40,744 |
| sparse-anchor p50 / p90 mm | 24.2 / 334.4 | 20.9 / 55.8 |
| anchors within 5 cm | 0.668 | 0.879 |
| free-space violations | 0.51 % | **0.31 %** |
| outside the sparse box (whole scene) | 0.54 % | 1.44 % |

321,010 two-image points were seen through and dropped, 939,479 admitted (before the voxel
select and the 1 M cap). The violation rate stays below `3`, so auto stays the default. The
whole-scene outside-the-box share did not come down (1.36 % without the test, 1.44 % with).
S-1, geometric stand-in: 0 points beyond 5 cm, riser cover 0.953. S-1 on upstream RoMa:
64 beyond 5 cm (18 of them two-image), `3`: 46, `2`: 215; the rest are 3-or-more-image points
at silhouettes against the empty background.
