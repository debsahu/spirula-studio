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
