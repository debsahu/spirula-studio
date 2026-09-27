# Scene partitioning: train a reconstruction in parts and merge the models

`spirula partition` and the GUI's Partition panel (dataset screen, beside
"Open in Trainer" and "Edit Reconstruction") split one reconstruction into
parts that each fit a single training run, and merge the trained parts back
into one model. The code is portable host code in `data/ScenePartition.h`,
`data/LabelField.h`, `data/Region.h`, `data/RegionProgram.h`,
`core/GraphCut.h` and `checkpoint/SplatMerge.h`; the device test is
`shaders/region.slang` behind `kernels/densify/RegionWeight.cu`; the trainer
hooks are `--partition` / `--partition-part` and `--roi-region`
(`TrainerSession::apply_partition_config`, `setup_region`).

## Why not Voronoi over the cameras

The obvious split -- k-means on positions, Voronoi cells, crop by cell at the
merge -- already leaves surprisingly few visible seams, because a Gaussian's
look is set by nearby cameras. What survives comes from four places: Gaussians
straddling a cell boundary, sky and far geometry that every part reconstructs
on its own, per-image appearance drift between parts, and floaters that one
part grows in front of another part's cameras. The scheme here targets the
first and the last two directly; appearance drift is left for a later shared
initialization (see "Not done").

## The pipeline

1. **Covisibility graph** over the cameras (`build_covisibility`). One node per
   frame, edge weight = number of 3D points two frames both observe. Where the
   observations come from is the only thing that changes per dataset:
   - a COLMAP model (including this tool's own SfM output): its tracks, read by
     `read_sparse_stats`;
   - a Nerfstudio or Metashape dataset: the seed cloud projected into every
     frame with the host camera models (`camhost::ray_in_frame`), which is a
     covisibility proxy that ignores occlusion; at most `max_projected_points`
     points are projected and their observers are shared with the unsampled
     points beside them;
   - poses only: k nearest camera centres, weighted `1 + cos(view angle)`.
   `Auto` takes the first of these that works.
2. **Cut** (`graph::cut_labels`): recursive normalized-cut bisection by the
   Fiedler vector of the normalized Laplacian, the same code the bottom-up SfM
   mapper uses for its atoms, with three things the mapper does not want:
   each side of a bisection must hold at least 30% of the parent (a free
   normalized cut shaves off weakly attached clumps one at a time, which is
   what left a utias part in 437 pieces), a half the sweep left in pieces
   hands every piece but its largest to the other half, and a few passes of
   boundary refinement move a camera to the part it shares more with while
   the sizes stay within a quarter of the mean. Either exactly `--parts`
   parts (the costliest part is bisected until the count is reached) or as
   many as keep every part at or under `--max-images`. Parts under 2% of the
   cameras join their best-connected neighbour; an isolated camera joins the
   part of the nearest one. Labels are dense, largest part first, and the
   CLI says when a part is not one piece of the view graph.
   Frames are identified by the shortest tail of their path that is unique
   in the dataset, never the bare leaf: a rig has `cam0/00123.jpg` and
   `cam1/00123.jpg`.
3. **Point ownership**: a point belongs to the part most of its observers are
   in; ties go to the part whose camera centroid is nearer. Without tracks, to
   the nearest camera's part.
4. **Owned region** (`LabelField::build`): every point of space belongs to
   the nearest *seed* -- the cloud's points (strided to `--max-seeds`), each
   carrying the mean direction toward the cameras that observed it, plus every
   camera as a seed seen from everywhere. The metric is not Euclidean: the
   space *behind* a seed, relative to the side it was seen from, counts
   `kBehindWeight` (8) times farther, and a query that carries a normal facing
   away from a seed's viewing direction is `kOrientPenalty` (20) times farther
   (`shaders/region.slang`). So a wall photographed from one room does not
   claim the other room's air, and a splat on the wall's back -- seen only
   from the next room -- stays with that room's model. Density-adaptive and
   unbounded by construction: no grid, no resolution, and a far splat goes to
   the nearest thing that saw anything. Queried on the host through a BVH
   over the seeds, and on the device through the same layout.
5. **Ring**: an outside camera joins part k when at least `--ring` of the
   points it sees (and `--ring-min-points` of them) are owned by k. The ring
   sees the seam from outside, so both neighbouring parts learn it under the
   same supervision. This is the single change that matters most for seams.
6. **Seed points per part**: everything the part's core and ring cameras
   observe, plus what it owns. A ring camera whose image shows another part's
   region needs geometry there to explain the pixels, or it grows floaters
   inside the region it was borrowed for; the surplus is cropped at the merge.
7. **Training** (`--partition file --partition-part k`): after parsing, the
   trainer keeps the part's frames (matched by image leaf, as `SparseEdit`
   matches them) and its seed points, for both the train and the eval split.
   Nothing else changes: the run's output is in the dataset's frame as always,
   and its `scene_transform.json` records any centering or rescale.
8. **Merge** (`merge_partition_splats`): each part's splats go back through
   its run's `world_from_train`; each one's short axis is pointed at the
   nearest camera that trained it and the field is asked which part owns that
   oriented point; the winners get their SH padded to the highest degree
   present and are concatenated. Hard ownership, no feathering.
9. **Region of interest while training**: a partitioned run hands its part's
   label region to the engine (`engine_set_region`), and `--roi-region` hands
   any region JSON. At every refine step `region_weight_tensor` evaluates the
   compiled program at every splat centre -- normal oriented by the nearest
   training camera -- and a splat outside draws for relocation and growth
   with `--roi-outside-weight` (1e-4) instead of 1, in both the revised and
   the MCMC path. The model keeps what it has outside but stops growing there.
   `region_parity` holds the device test to the host mirror.

## The files

`partition.json` beside `partition.bin`, both written by `write_partition`:

- JSON: `format`, `version`, `dataset` (absolute), `source`, `options`,
  `num_parts`, `frame_names` (image leaves) and `frame_parts` (core label per
  frame), `parts[k].ring` (frame indices), `cut_fraction`, `binary`.
- BIN: `SSPT` v2, then the `LabelField` (`SSLF` v1: seeds [n,4] and BVH
  nodes [m,8] as floats), the camera centres [N,3], then u8 owner per seed
  point in file order, then per part a u32 count and u32 point indices.

The trainer needs the JSON's frame lists, the BIN's point tables and the
field (its region of interest); the merge needs the field and the centres. A run's `config.json` carries `partition` and
`partition_part`, which is how `find_partition_runs` pairs runs with parts
without any naming convention.

## Regions (`data/Region.h`, `data/RegionProgram.h`)

The ownership field is one `Region` among several: `BoxRegion` (oriented),
`SphereRegion`, `HalfSpaceRegion`, `MeshRegion` (closed mesh, ray parity over
a BVH), `LabelRegion` (one label of a `LabelField`) and `CsgRegion` (union,
intersection, difference, complement). Every kind serializes through
`region_to_json` / `region_from_json`, and `contains_many` answers a whole
splat array in parallel on the host. Every kind but the mesh also compiles
(`compile_region`) to a post-order program of float4 nodes that
`shaders/region.slang` evaluates on both backends in one kernel, with the
label field's seeds and BVH as two more float4 arrays uploaded once per run.
A region built from the editor's selection tools would go through the same
seam. Constants and layouts live in the shader; `data/LabelField.cpp` and
`data/RegionProgram.cpp` are its host mirrors and `region_parity` pins them.

## The GUI

The Partition button on the dataset screen opens the panel over the parsed
reconstruction and computes nothing until Compute is pressed. "Queue parts in
Batch" saves the partition, then asks for the run's preset, splat cap, SH
degree and step count in a dialog; Queue is the confirmation, and if the
batch list still holds rows that have not finished it asks whether to clear
them first. With "merge once all have trained" ticked the list gets a final
Merge row (`BatchStage::Merge`), which finds the parts' runs under the runs
folder by their `config.json` and writes `<dataset>_merged_<stamp>.ply`
there. Rows whose tasks all finished are left unticked when the queue ends;
"Clear done rows" and "Clear list" both confirm first.

## Not done, on purpose

- No seam-band joint refinement and no opacity feathering. Judge the hard
  ownership cut on real captures first; either is a small addition on top of
  the merge if the seams warrant it.
- No shared appearance initialization (a short capped global pass whose
  per-image appearance state every part starts from). Colour seams from
  exposure drift between parts are the one failure mode this design does not
  address.
- Projection covisibility ignores occlusion, so a wall between two rooms does
  not separate them the way tracks would. Tracks win whenever they exist.
- The region test on the device has no mesh leaf; a mesh region is host-only
  until a triangle BVH joins the program.
