# FlexGlue (`src/flexglue/`)

FlexGlue ([cvg/FlexGlue](https://github.com/cvg/FlexGlue), MIT code) on the
inference layer: a DINOv3 ConvNeXt-B descriptor that turns each image into a
`[64, 64, 256]` map, and a sparse matcher that matches two images' keypoints
through those maps and scores whether the pair is covisible at all.

The configuration is COLMAP's: the image squashed to 512x512, fp32, mutual
nearest neighbours between keypoints and predicted points, early-stop checks
after layers 2, 4, 6 and 8, `min_covisibility 0.5`, `early_stop_decay 0.5`.
`tools/flexglue_ref/ARCHITECTURE.md` has every shape and constant.

## Weights and licences

`flexglue.pth` from the authors' release, read by `nn::TorchCheckpoint` by
name. It bundles Meta's DINOv3 ConvNeXt-B, so loading it needs the `dinov3`
licence (registered once, by `spirula::license::register_dinov3()`, which RoMa
v2 shares) and the `flexglue` MIT licence. Never mirrored, never bundled.

## Layout

```
FlexGlue.h            Model: describe() an image, match() two
model/Model.cpp       the forward pass, next to descriptor.py and flexglue.py
model/Fetch.{h,cpp}   the checkpoint URL + SHA-256, licence registration
shaders/flexglue.slang  the 1.5-entmax window readout, the offset head's
                        binned expectation, the 4-D mutual nearest neighbour
tests/flexglue_test.cpp parity against a tools/flexglue_ref/ pair
```

## In SfM

`spirula sfm --matcher flexglue` with any `--features`, SIFT included: FlexGlue
reads only the keypoint locations. It reads the pixels too, so `auto` hands it
its image directory and `match` needs `--flexglue-images`. Every flag has
COLMAP's default; `--flexglue-min-covisibility` is the pair filter (0 keeps
every pair) and `--flexglue-early-stop-decay` how early a clear non-pair stops.
The dense features of the last `--flexglue-cache` images stay resident (4 MB
each). A pair the focal or gyro sample matched is not matched again; those
results are held until reused, within 256 MB.

Layer 1's self block (input projection, self attention, FFN) reads one image
only, so `match()` keeps its output, 8 MB, for the 64 most recently used live
images (`SS_FLEXGLUE_SELF_CACHE=N`, 0 off) and starts a hit at the cross block.
With both images hit it saves 50-57 ms of a ~930 ms pair (Apple M5 Pro, six
`--bench 5` runs).
It is bit-identical to the uncached path (`flexglue_test`'s self-cache checks;
`--warm-cache` dumps the hit path for `compare.py`).

## Parity

`flexglue_test` runs every pair in `tools/flexglue_ref/fixtures/` twice, as the
reference did (`nostop`, then `colmap`), against the fixed bars of
`tools/flexglue_ref/README.md`. `SS_FLEXGLUE_DUMP=<dir> flexglue_test --pair
NAME` writes every stage for `compare.py check`, which also applies the
torch-vs-torch floors. Mutants the gates kill are listed in the commit that
added them. One sampling path cannot be pinned: `f_match` is read at a mean of
cell centres, which never reaches the outer half-cell, so its border padding
is unobservable.

## Speed

Apple Silicon through MoltenVK has no cooperative matrix, so this is the fp32
path: about 1.1 s a pair at 1.6k x 1.5k keypoints, 0.74 s of it attention (nine
layers of 4096 x 4096 x 8 heads, self and cross, both images, 1.2 TFLOP) at
1.65 TFLOP/s. COLMAP's ONNX graphs on the same machine under onnxruntime: 3.0 s
(CPU provider) and 3.6 s (CoreML). Tried and measured slower: a
query-per-thread attention kernel, fp16 attention operands, and f16 weights
(which also cost the descriptor 4e-4 relative L2). An early stop after layer 4
costs 0.5 s.
