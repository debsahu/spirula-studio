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
| far isolated | on: margin 2 (metric) or 0.2 x the sparse box diagonal, radius 8 x spacing, at most 2 neighbours (below) |
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

`--source auto|roma|moge|hybrid`. **Auto is `roma`** whenever a matcher is available (consultant ruling,
2026-10-07, plan 13.3 C-4); `moge` only when no matcher can run (no licence, no checkpoint) and the
dataset has depth maps, said aloud with the reason; `hybrid` by explicit request only. With a matcher and
a `depths/` folder the run prints that the maps are left alone. Why: at an explicit cap hybrid *is* roma
with no fill (the matches fill the budget first), so equal-budget parity shows nothing; where a gain can
show (thinned ROI count, stairs-band planes, violations) it is a small loss; it costs 1.9x the memory
(9.56 GB against 5.14 GB peak on the M4 Max) and depends on maps of a model nobody measured here. Its
mechanism (depth fills where textureless surfaces starve the matcher) stands, and the basement is not
that scene. **Pre-registered test to reinstate `hybrid` as auto**: a scene with large textureless surfaces
(a painted interior wall run, or aerial0720's roofs) where, with the maps `spirula geometry`'s *default*
model writes, hybrid beats roma at a thinned equal ROI count on independent anchors by more than 3 pooled
sd (3 seeds) **and** its free-space violation rate is not worse by more than 3 sd. Until it runs,
hybrid is an option, not a default. Test not run: it needs that scene. The
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
with some missing: with no matcher it says how many images have none and uses what is there. The count reused /
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
  (`--min-depth-share`, 0 accepts any), a depth source is an error (auto reads the maps only
  when no matcher can run, so it is one too; with a matcher it never reads them).

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

**Retracted (review B2, 2026-10-07)**: the first version of this table said the hybrid beat roma
with 22 % more stair points. That was the cap: the hybrid wrote 1.25 M points (1 M matches and its
250 k fill budget) while roma and moge were capped at random to 1 M. It is not evidence for the
hybrid, and neither was its anchor score, half of whose anchors the depth fits had been fitted to.

Re-measured with no cap on any arm (`--max-points off`, the same 10,212 samples per view), so the
fraction of each cloud inside the ROI is comparable, and scored against **independent anchors**:
the 425 ROI sparse points with `id % 5 == 0`, which `--depth-fit-holdout 5` kept out of every depth
fit (roma never uses sparse points). The ROI holds a wall and a column besides the steps, so its
point count is not a stair count.

| arm | points | in ROI | ROI share | indep. anchor p50 / p90 mm | within 2 / 5 cm | free-space violations |
|---|---|---|---|---|---|---|
| roma | 1,875,846 | 76,414 | 4.07 % | 17.8 / 54.7 | 0.565 / 0.885 | 0.37 % |
| moge | 2,335,938 | 70,223 | 3.01 % | 48.6 / 441.1 | 0.268 / 0.508 | 1.23 % |
| hybrid | 2,350,933 | 102,743 | 4.37 % | 17.2 / 54.7 | 0.579 / 0.887 | 0.59 % |
| DA360 seed | 3,804,518 | 355,760 | 9.35 % | 15.7 / | / 0.993 | 1.85 % |
| DA360 3-view | 2,681,011 | 168,771 | 6.30 % | 20.1 / | / 0.944 | 2.53 % |

(anchor p50 is the distance from an anchor to the nearest point of the cloud: coverage of the
measured surface, not the accuracy of the points.) Against roma the hybrid adds 0.3 pp of ROI
share and 0.002 of anchors within 5 cm, inside what one arm moves between runs, and costs 0.22 pp
of free-space violations. The S4 guard dropped 1,138,170 of its 1,697,082 fill points as lying
within 4 voxels of a matched point. **No hybrid gain is shown on this scene.** moge alone is far
behind: its stairs mostly do not survive the vote.

**Decided 2026-10-07: `hybrid` is not the automatic default** (see the top of this section).
The equal-budget numbers are WS-6's (plan 10.5.3).

Side profile (`render_profile`): DA360 shows the doubled stair line; hybrid and roma one; moge
almost no stairs, so its passing the doubled-line gate is absence, not quality. The normal check
(capped runs): moge anchors 47.3 -> 51.8 mm, hybrid violations 0.44 -> 0.38 %; mixed, kept on.

**Maps of other pictures, measured**: `depths/` shifted 3 frames on the basement (the reviewer's
case, maps from before the record existed): `--source moge` now exits 1, "only 17 of 127 images
have a usable depth map (13 %, below 50 %)"; `--source auto` does not read the maps (a matcher is there).

S-1 through each source (stand-in matches, synthetic maps with a per-image disparity-affine
error, noise and three images with the risers 5 % too deep): roma 83,808 points / 0.996 within
1 cm / riser 0.953 / 0 beyond; moge 75,985 / 0.997 / 0.912 / 0; hybrid 87,639 / 0.996 / 0.945 / 0.

## Warp cache (2026-10-07)

A change to a filter or a threshold does not change what the matcher says about a pair, so
`spirula densify` keeps each pair's warp and certainty on disk and serves it back
(`src/roma/WarpCache.{h,cpp}`). It is a `roma::Matcher` decorator and touches nothing under
`src/nn/` or `src/roma/model/`. The idea comes from a prediction cache in the unmerged upstream
pull request #154 (D1odeKing); the code, the key and the tests are ours
(`LICENSES/NOTICE-spirula-studio-PR154.txt`).

- **Key**: SHA-256 over both images' pixel bytes and sizes **in order** (A to B is not B to A),
  the matcher's input size, and the matcher identity: preset and its resolutions, the checkpoint's
  SHA-256, the resolved precision (`f16_weights()`, which the cooperative-matrix probe decides),
  `matcher_rope_rounds()`, `local_corr_fused()`, `SS_NN_GEMM_KERNEL`, the device's selector, and a
  digest of every source under `src/nn/` and `src/roma/model/` that the build regenerates when one
  changes (`cmake/RomaModelDigest.cmake`). So a changed kernel, another GPU, `SS_ROMA_ROPE_F32=1`
  or a different checkpoint misses; a renamed or touched image does not. The images are the masked
  match-resolution views the matcher is handed, so a mask edit that changes any pixel misses.
- **Entry**: warp f32 and certainty f32, 12 B a pixel, an 80-byte header carrying the key and the
  seconds the matcher took, and a 64-bit checksum trailer; 4.9 MB at 640 px. A half-float warp
  would move a match 0.16 px at 640 and is not acceptable. **A uint16 certainty was built first
  and dropped**: it differs from the matcher's by up to 7.6e-6, and on the synthetic staircase
  through real RoMa that moved the cloud from 78,695 to 78,698 points, so a cached run was not the
  run it replaced. With float32 the cold, warm and `--cache off` clouds are byte-identical.
- **Verification**: an entry is decoded only if the magic, byte order, version, key, size range,
  counts, length, checksum, warp finiteness and certainty range all hold; one that does not is
  deleted and matched again (counted in `corrupt`). Writes go to a temporary beside the entry and
  are renamed in, so a crash leaves a stale temporary (swept after an hour), never a partial entry.
  A cache that cannot be written is reported and the run continues.
- **Where and how big**: `<dataset>/densify_cache/<2 hex>/<key>.rwc`, 16 GiB by default, least
  recently used first (a hit refreshes the file's time, so recency survives a restart). A budget lowered
  below what the folder holds trims it on open. The newest written entry is never evicted, so the
  directory can exceed the budget by one entry. **A pair set larger
  than the budget gets no hits on a repeat run** (a sequential scan over an LRU of smaller size
  evicts each entry just before its turn): the summary line shows `evicted`, and
  `--cache-budget` raises it. 2,056 pairs at `base` are about 10 GB; `high` is 9 MB a pair.
- **Controls**: `--cache auto|off|<dir>` (auto is the dataset folder), `--cache-budget auto|<size>`
  (`16G`, `500MiB`, `1.5G`; binary units), `--clear-cache` (deletes the entries, and only the
  entries, then runs; a file that is not an entry stays). Every choice is printed, the end of the run
  prints hits, misses, hit rate, the matcher time saved and what was written or evicted, and
  `densify.json` gains `warp_cache`. `--matches` and `--source moge` do not use it.
- **Time saved** is each hit's recorded matcher time less what the hit cost (hashing the images,
  reading and verifying the entry), so it is a measurement, not an estimate from a mean.
- **Not covered by the key**: the host stage's own settings, which is the point. Anything that
  changes what the matcher is handed (`--face-pairs`, the face size, the masks, a different
  neighbour set) changes the key through the pixels.
- **Interface**: `static_assert(sizeof(Warp) ...)` in `WarpCache.h` stops the build when `Warp` gains
  a field (the per-pixel precision the refinement needs), so the entry format cannot silently
  drop it; extend the entry and raise `kEntryVersion`.
- **Consent**: the cache is built from a constructed `RomaMatcher`, which needs a verified
  checkpoint and both licences, so a warm cache never lets an unaccepted run proceed.

**Measured (2026-10-07, M5 Pro, upstream-identical `base` preset, the real checkpoint).** The
runs below went through a test harness that loads the checkpoint by path, because the CLI's
licence gate could not be passed from the session; the CLI itself was run and refused, with a
warm cache beside it, without reading a pair or touching an entry.

| run | pairs | hits | misses | time | matcher time saved |
|---|---|---|---|---|---|
| basement, every 8th held out, cold | 2,056 | 0 | 2,056 | 5,352 s (matching 5,073 s) | 0 |
| the same, warm | 2,056 | 2,056 (100 %) | 0 | 262.6 s (loading 38.2 s) | 5,035 s |

Once the licence had been accepted on the machine, the real CLI (`spirula densify`, 59 pairs of
the staircase) gave: cold 2:02 with 3 hits (views that were masked to the same bytes), warm 3.2 s with
59 of 59 hits and 2:05 of matcher time saved, `--cache off` byte-identical to both,
`--cache-budget 100M` on a 262 MiB cache trimmed it to 21 entries on open, and `--clear-cache`
removed those 21 and ran cold.

The cache held 9.4 GiB (4.9 MB an entry). The cold run shared the GPU with other jobs on the
machine, so its absolute time is slower than an idle one; the saving is the recorded matcher time
of what the hits replaced, less what each hit cost. `points3D.bin`, `points3D_tracks.bin` and
`images.bin` of the two runs are byte-identical (1,000,000 points, the cap). On the synthetic
staircase (100 pairs, 91 distinct) the cache off, cold and warm outputs are byte-identical, the
warm run took 4.95 s against 184 s, and 9 pairs hit inside the cold run because two views had the
same masked bytes.

Mutation run of `warp_cache_test`, 58 mutants, every one failing the test that names it: the key made
from the image names with the pixels ignored (the stale name key), warp stored as half floats, certainty
stored as uint16, key without image B, without the last byte, with the names, symmetric in A and B,
without the sizes, without the identity or the input size, identity and size joined as text, each
of the ten identity fields left out in turn, checksum, key, length, finiteness, certainty range,
flags, version, byte order, size range and recorded time unchecked, entry written under its final
name, old temporaries never swept or live ones swept, a hit not refreshing recency, nothing evicted
or the newest evicted, `clear` removing the directory, lookup through the open-time index, unusable
output going through the cache, seconds saved dropped or taken from the hit, hits or misses not
counted, a damaged entry reported as a plain miss, a failed write throwing, an empty identity
accepted, entries ordered by name on reopen, a lowered budget not trimming on open, a hit's time
without the hashing, zero accepted as a size, decimal units.
A damaged entry left on disk is caught only by the direct `WarpCache::get` check, since a miss
writes over it.

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
- **Edit Dense Cloud** (`PointsDoc`, `src/roma/DensifyEdit.cpp`): the button beside
  "Edit Reconstruction" and the Model combo opens `sparse/<m>-roma` (or its edit, when
  there is one) in the point editor. Save writes `sparse/<m>-roma-edit/` and never the
  model it opened: `cameras.bin` and `images.bin` byte for byte, `points3D.bin`,
  `points3D_tracks.bin` and the normals file filtered to the points that stay, and a
  `densify.json` with `edited_from` (the first model, its name and checksums) and `edit`
  (what this save removed). Only points can go: a moved scene or a removed camera is
  refused, because the poses are the model. Nothing removed, or nothing left, writes
  nothing. Opening the edit and saving rewrites it. The edit is picked by neither the
  parser nor densify; pick it in the Model combo (`colmap_recon_dir`). Design after
  spirula-studio#154 (`LICENSES/NOTICE-spirula-studio-PR154.txt`).
- **Cloud checksums**: `densify.json` records SHA-256s of `points3D.bin` and
  `points3D_tracks.bin`. The step checks them when the child exits, and the Model combo
  marks each dense entry `[checksum ok]` or `[checksum mismatch]`.
- **Writer lock and publication** (`src/roma/Publish.h`): a file `.<m>-roma.lock` beside
  the model holds the writer's pid. `spirula densify` takes it before matching, the
  edit takes it before reading, and a live holder refuses the second writer with its pid;
  a dead holder's lock is taken over. A folder is written as `<out>.partial` and swapped
  in: an existing one is set aside as `<out>.old` first and dropped after, and
  `recoverPublish` puts it back if a crash left the swap half done. Neither suffix is
  listed as a model.
- **Live preview**: the dataset screen's densify step passes `--progress-dir`; the child
  writes `model.bin` snapshots (the cameras and a strided slice of at most 50 000 points
  of the cloud so far) through `CloudPreview`, at most one every 1.5 s and never more
  than one for each eight times a write takes. The model view the screen already has
  draws them, and the last one is the filtered cloud. A write that fails costs nothing.
- Gate: `tools/roma/densify_gui_gate.py` (`tools/guictl.py` on a 6-image fixture).
- Gate for the edit and the preview: `tools/roma/densify_gui_gate.py`; the preview is sampled
  during its run and the edit round trip is its last steps.

## Far isolated points (consultant C-6, 2026-10-07)

`--far-isolated auto|off` (default `auto`), default mode only (`--plugin-exact` never runs it). After the
voxel select and before the cap, a point is dropped when it is **far**: more than `margin` outside the
sparse points' p0.5-p99.5 box on some axis (max-norm), **and isolated**: at most 2 other points within
`radius`. The three values are resolved from the model, so the rule is scale-free:

| value | rule |
|---|---|
| box | per-axis p0.5 and p99.5 of the sparse points (numpy's linear interpolation) |
| margin | 2 on a model whose `gauge.txt` says metric, else 0.2 x the box diagonal (about 2 m on the basement; the 0.2 is the consultant's choice, not fitted) |
| radius | 8 x the sparse median nearest-neighbour spacing (10 cm on the basement) |
| neighbours | at most 2 |

Never run on a model of under 100 sparse points (no box to speak of). The run prints the margin,
radius and neighbour threshold with the count removed of the far points, and `densify.json` has a
`far_isolated` block (state, box, margin, radius, beyond, removed). Neighbours of a far point are
counted among every point within `margin - radius` of the box, not only among the far ones.

Why: the basement's `--min-track auto` cloud has 1.3 k points (0.13 %) more than 2 m beyond the box;
76.5 % are isolated and 13.3 % have DA360 support (arcs of two-image points at long range), against
95 to 97 % support and 3 to 6 % isolated in the 0 to 2 m zone, which is the stairwell and the landing the
box clips. The rule is the narrowest cut that removes the first class and nothing of the second. A
generic statistical outlier filter was rejected: it would also thin stair edges.

Checks: `far_isolated_drops_only_isolated_far_points` (an injected fixture: 50 isolated points 3
margins out, a 5 x 10 grid at 0.5 spacing as the coherent structure, a triple and a quad on either side
of the threshold, a far point whose neighbours lie inside the margin, a point 1.5 out on two axes that a
2-norm would drop), `far_isolated_runs_before_the_cap`, `far_filter_is_scale_free`,
`far_isolated_plan_states`. S-1 has no far points, so "removes 0 on S-1" proves nothing and is not a check.

## Cycle check and precision refinement (2026-10-07; both off by default)

Ported as designs from spirula-studio#154 (D1odeKing; `LICENSES/NOTICE-spirula-studio-PR154.txt`).

- **`--cycle auto|<px>|measure|off`.** This runs per neighbour, before Sampson. A's pixel goes through
  A -> B and back through B -> A (bilinear, align_corners=false), and the miss is measured in match pixels of A.
  `measure` drops nothing. It bins the miss (0.05 px) separately for samples that went on to be candidates
  and for samples a geometric filter rejected; `densify.json` "cycle" holds both histograms. A
  missing reverse refuses. B -> A comes from `RomaMatcher::matchBoth` only at two-scale presets,
  where it is cheaper: on the M5 Pro, one basement pair, AB+BA costs 1.38x AB at `high` and 2.01x at
  `base`. Elsewhere it takes a second `match()`, which the warp cache can serve. Each direction is its own cache entry.
- **`--refine auto|<sigmas>|off`.** This needs `Warp::precision`, RoMa's 2x2 information (p00, p01, p11)
  in 1/px² of the warp. It runs after the largest-consistent-set fusion and before every per-point filter,
  which then judge the refined point exactly as they judged the mean:
  - one observation per source image;
  - the point moves only along the reference ray, because the warp is defined at the reference pixel;
  - Huber IRLS by Gauss-Newton;
  - the threshold is in sigmas of the view's own residuals, median Mahalanobis at the mean / sqrt(2 ln 2).

  With no precision (encodings 0/1 dumps), the point stays at the mean and is counted.
- **`--dump-matches <dir>`** writes every warp matched, encoding 2 with precision. A pair matched
  both ways keeps its forward warp; reverses are written without precision.

**The gate.** Pre-registered in `docs/superpowers/plans/2026-10-07-integrate-pr154.md` §11, geometry only.
- Inputs: basement, 2,056 pairs, every 8th panorama held out. All arms read the same dumps, regenerated on the M5 Pro with precision and B -> A.
- Budget: `--max-points 1000000`, seeds 0/1/2. The floor is the baseline's sd.
- Harness and scorer: `tools/roma/eval_equal_budget.py` with WS-4's scorer.
- The run below is on the build with the far-isolated filter; one on the build before it gave the same verdicts.
- The cycle threshold: the pre-registered rule (99 % of geometric-pass candidates below T) **found no threshold**, since 6.85 % of them miss by more than 3.15 px. 1.3 px is post hoc, the Youden optimum against the geometric rejects, flat from 1.0 to 1.5 px.

| metric (mean of 3) | base (sd) | cycle 1.3 px | refine 1.345 | both |
|---|---|---|---|---|
| ROI free-space violations | 0.00360 (0.00022) | 0.00365 (+0.2 sd) | 0.00391 (+1.4) | 0.00399 (+1.8) |
| stairs-only violations | 0.00421 (0.00014) | 0.00408 (-1.0) | 0.00435 (+1.0) | 0.00444 (+1.6) |
| written reprojection p95, px | 5.207 (0.006) | 5.080 (**-19.8**) | 5.400 (+30.1) | 5.298 (+14.1) |
| local-plane bias p50, mm (void: +1 cm moves it -6 %) | 6.88 | 7.29 | 6.93 | 7.24 |
| local-plane thickness p50, mm | 9.03 (0.20) | 8.37 (-3.3) | 8.25 (-3.9) | 8.07 (-4.9) |
| ROI points | 40,706 (420) | 38,272 (**-5.8**) | 39,688 (-2.4) | 36,667 (-9.7) |
| ROI anchors within 5 cm, ROI-matched, delta | | +0.0005 | -0.010 | -0.016 |
| anchors seen by >= 2 held-out panoramas, c5 | 0.900 | 0.902 | 0.895 | 0.873 |
| outside-box share | 0.0141 | 0.0103 | 0.0113 | 0.0083 |

Nulls: 1 % floaters add 2.3 pp of violations, and shuffled depth reads 19.7x, so the violation rows stand. Plane bias is void, as WS-4 predicted.
S-1:
- Oracle: passes at every setting.
- Real RoMa (native dumps of the staircase), points beyond 5 cm / riser cover: base 47 / 0.955, cycle 9 / 0.951, refine 41 / 0.945, both 9 / 0.943.

**Verdict: both off by default.** The operator's rule needs violations and accuracy to improve beyond the floor without losing coverage.
- Neither feature moves violations.
- The cycle check improves the reprojection p95 and cuts S-1's far points 81 %. It costs 6 % of the ROI points and 0.004 of riser cover.
- Refinement worsens the reprojection p95, because it minimises precision-weighted residuals, not pixels. It also drops 19 % of fused points, through the track and depth-precision filters judging the refined point.

Two findings for whoever revisits this:
- Round trips on the basement are broad: candidate median about 0.4 px, and 27 % of candidates exceed 1 px. On the rendered staircase 97.7 % of candidates fall under 1.3 px.
- RoMa's precision is about 1.7-1.9x conservative in sigma here: the calibrated scale is 0.52 to 0.59.

## Checks

- `roma_densify_test`: the stage on inputs whose answer is known, each test naming the
  wrong implementation it catches. Every named mutation was run and fails the test
  that names it.
- `roma_plugin_parity_test <fixture>`: gate P-4, against the plugin's own host stage on
  the same matches (`reference/python/roma_plugin_parity.py` writes the fixture).
- `densify_autopick_test`: gate H-3.
- `warp_cache_test`: the cache (`WarpCache.h`). Each test names the mutation it catches.
- p23 mutation run, 2026-10-07 (each fails the test named; WS-4e's 41 re-run alongside, each still
  failing its test): cycle miss in camera pixels or over them, missing reverse passing, align_corners
  sampling, axes swapped, auto read as on, histogram before the filters or in camera pixels, reverse read
  at the reference pixel (`cycle_*`); precision as covariance, p00/p11 swapped, one observation per view,
  threshold in raw units, no Huber, identity precision when absent, track errors at the mean, refinement
  not moving, derivative sign, residuals in the view's own pixels (`refine_*`; the last is equivalent
  while views share one size, which only `refine_residuals_in_match_pixels` breaks); encoding 2 without
  precision, a reverse overwriting a forward dump, matchBoth ignored, the reverse from match(A, B), auto
  or explicit settings resolved wrongly (`dump_matches_reproduce_the_run`, `warp_files_round_trip`);
  precision from the logit or left empty (`warp_precision`), BA's precision from AB
  (`matcher_adapter_precision`); cache precision not stored, version not raised, matchBoth bypassing the
  cache or running both for one missing direction or keying B -> A as A -> B, NaN or short precision
  (`warp_cache_test`).
- Freeze mutation run, 2026-10-07 (each fails the test named; run by hand on the working tree, the rest
  of the suite passing): auto = hybrid with maps and auto never falling back to moge
  (`auto_source_is_roma_unless_no_matcher`; the GUI's auto = hybrid, `densify_gui_test`); far filter:
  isolation ignored, margin ignored, 2-norm distance, neighbours counted among far points only,
  fewer-than-2 and at-most-3 thresholds, self counted, cell scan limited on x, on y, on z
  (`far_isolated_drops_only_isolated_far_points`); filter after the cap, `--far-isolated off` ignored,
  plugin-exact filtering (`far_isolated_runs_before_the_cap`); box from extremes, margin not
  scale-free, radius 4 x, no 100-point floor (`far_filter_is_scale_free`); the plan ignoring
  `off` (`far_isolated_plan_states`). One mutant (cell scan limited on x) first survived: the fixture had
  no neighbours across a cell boundary; two squares straddling a cell corner were added.
- WS-4d review mutation run, 2026-10-07 (each fails the test named): the convention checked after
  facing, the seen-through vote off, the rank and inlier-share gates off, the flatness test off,
  hybrid's residual test off, the fill budget changed, the share gate off, a record's image, map
  print or image print unchecked, an 8-bit or wrongly shaped map read, maps writable while geometry
  runs, voters without parallax, fill next to matches kept, the fit hold-out ignored; the
  `nonzero` clamp dropped is a `logic_error` its test reports.
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
**P-4 gates `--plugin-exact` only.** The fused-point miss below is accepted as a documented deviation
(D-6). The default mode's fusion (`1 / depth_per_px^2`, the largest consistent set) has no external
reference at all: it is covered by S-1 and `roma_densify_test`'s named mutants, which is a different and
weaker kind of evidence than parity. Do not read "P-4 passes" as covering the shipped path.
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
