# Rolling shutter

`--rolling-shutter` (default `auto`) is a finishing pass: it fits a shutter to
the reconstructed model, bundle-adjusts with it, and writes what it found to a
sidecar next to `images.bin`. Off leaves everything exactly as it was.

## The model

A rolling-shutter camera has a different pose for every scanline. This models
that as a per-image **twist** over the readout interval, in the camera frame:

```
shutter time   s   = ax*u + ay*v - 0.5          in [-0.5, 0.5]
pose at s          = exp(s*omega) . T_ref ,  t(s) = exp(s*omega) t_ref + s*v
```

Two consequences of putting `s = 0` at the image centre rather than at the
first row:

- the pose in `images.bin` is the **mid-row** pose, which is the best
  global-shutter approximation of the model. A reader that ignores the sidecar
  gets the least wrong answer available, not an endpoint.
- reversing the readout direction is a **negative readout time**, so the
  direction search is two hypotheses (vertical, horizontal), not four.

`(ax, ay)` is a general 2-vector, so a readout along any axis is representable;
only the two the search tries are ever produced.

The twist is camera-frame, which makes it Sim3-covariant exactly as `Pose::t`
is -- `omega` is invariant and `v` scales with the world (`transformTwist`).
Storing two endpoint poses instead would need both transformed, and would hide
which one the reconstruction's pose is.

## Why there is no implicit solve

A renderer must find where a world point *lands*, and where it lands decides
when it was seen, so projection is implicit and needs a fixed-point iteration.
Bundle adjustment has no such problem: the observation's row is **measured**, so
`s` is known in closed form and the residual is one pose composition before the
existing `applyPose`. `camera.slang`'s `applyShutter` is that composition;
Slang's autodiff produces the Jacobian, and no kernel was added.

## Why the readout is not a solver column

Free per-image velocities make rolling-shutter SfM degenerate -- pitch during
readout is a vertical image scale, which is `f_y` wearing a different name, and
vertical or forward velocity trades against scene depth (Albl et al., *Degeneracies
in Rolling Shutter Structure-from-Motion*, ECCV 2016). A handheld video is close
to that critical configuration by construction: one readout direction, and
neighbouring frames with nearly the same motion.

So the twist is not free. It is derived from the trajectory,

```
xi_i = (readout / dt_i) . log(T_{i+1} T_{i-1}^{-1})
```

with `dt_i` the interval the central difference actually spans, and the only
unknown is `readout` -- **one scalar per camera group**. Dividing by `dt_i` is
what makes a non-uniformly extracted capture work: the ratio, not the frame
count, is what the readout is a fraction of.

That scalar is found by a coarse grid plus refinement on the robust
reprojection cost (`searchReadout`), on the host, over a subsample of the
observations. It owns no column of the reduced system, so `kDofCompact/Mid/Wide`,
the pair-Schur tables and the preconditioner blocks are the global-shutter ones.
`SS_SFM_RS_CURVE=1` prints the whole sweep.

The twist is frozen for the duration of a solve and recomputed between them; the
pass alternates a rolling-shutter bundle adjustment with a re-fit of the readout.

## Timestamps

`dt_i` needs per-image times. They come from the **last digit run of the file
stem**, which is where every frame extractor here puts the frame number, and the
unit is therefore frames. A group whose names carry no number, or that has two
images sharing one, falls back to the position in the sequence and says so:
the readout is then a fraction of a uniform interval, and a subsampled capture
is under-corrected rather than wrong.

Measured on a 300-frame dense capture with a known shutter injected, recovering
it from every Nth frame only (truth 0.900 of a frame interval):

| estimated from | recovered | cost removed |
|---|---|---|
| every frame | 0.8997 | 93.9% |
| every 3rd | 0.9026 | 74.5% |
| every 6th | 0.9580 | 63.3% |
| every 12th | 0.8402 | 41.1% |

So subsampling degrades this gracefully; it does not break it.

## The mapper eats the evidence

**A shutter strong enough to matter is strong enough to make a global-shutter
mapper delete its own evidence**, and that is why the finishing pass finds
nothing on captures that obviously have one.

BS-RSC Scene1 ships paired global- and rolling-shutter images of the same scene,
which makes the mechanism measurable. On the rolling-shutter set at the default
`--max-error`, the reconstruction is clean (0.25 px over 54k observations) and
the fit reads zero -- because the surviving observations are only the rows where
`s ~ 0`:

| row band (of 768) | 0 | 96 | 192 | 288 | 384 | 480 | 576 | 672 |
|---|---|---|---|---|---|---|---|---|
| observations kept | 0% | 0% | 0% | 3% | 85% | 84% | 21% | 0% |

Every off-centre correspondence disagrees with a global-shutter pose, the
reprojection filter drops it, and what is left is the band the shutter does not
move. The structure is then triangulated from that band, so the points are wrong
in a way correlated with the shutter -- re-attaching the dropped features after
the fact does not recover it either (measured: a flat curve).

Loosening the filter keeps the evidence -- 27% of features triangulated becomes
62%, rows 96-576 instead of 384-576 -- and the signal comes back.

## When the trajectory twist is the wrong model

Even with the evidence kept, the *readout search* still reads ~0 on that capture:
a free per-image twist there is 5.9x larger than the trajectory implies and points
in an uncorrelated direction (cos -0.18). The frames are far enough apart that the
average velocity over a central difference says little about the velocity during
one readout -- exactly the case `--rs-per-image-twist` exists for.

So when the readout search is rejected, the fit falls back to scoring **which axis
a free per-image twist explains better**. That is a real discriminator:

| | vertical | horizontal |
|---|---|---|
| BS-RSC rolling shutter | **55.8%** | 26.6% |
| BS-RSC global shutter (same scene) | 15.0% | 19.7% |

It picks vertical on the rolling-shutter set, which is right, and stays under the
floor on the global-shutter one. Both thresholds are calibrated against that pair
and neither is generous: `min_gain` is 5% because a loosened filter lets a
global-shutter capture reach 2.1% by overfitting, and `free_twist_gain` is 25%
because the same capture reaches 20%.

**So a strongly rolling-shutter capture needs both** a loosened `--max-error` and
`--rs-per-image-twist`. Neither is the default, because both cost accuracy on the
overwhelming majority of captures that have no shutter worth correcting.

## The guard

`min_gain` (1%) is not optional. A capture with no shutter -- a tripod, a global
shutter sensor, or footage a phone already stabilized -- has no signal, and
without the guard the search fits the readout to noise. A fit that does not beat
global shutter by 1% of the robust cost is discarded and the model is left alone.

Every real capture tested here came back Global; see the README's table. That is
the guard working, not the pass failing: injecting a known shutter into the same
four models recovers it to within 1.6%.

## Per-image twists

`--rs-per-image-twist` gives every image its own twist instead of the one its
neighbours imply. It exists for the capture the trajectory estimate cannot
describe: bursty motion, or a sequence whose registered frames are far apart, so
that a central difference over the neighbours is not the velocity during the
readout.

It is **not** six more columns of the reduced camera system. With the poses and
points held, an image's twist is seen only by that image's observations, so it is
the exact conditional minimizer of a 6x6 normal equation -- block coordinate
descent, at no cost to `kDofCompact/Mid/Wide`, the pair-Schur tables or the
preconditioner (D75). The pass alternates: bundle-adjust with the current
twists, re-fit the readout, refine each image's twist, repeat.

The update is kept only when it lowers that image's robust cost, which makes the
decrease monotone. `--rs-twist-prior` damps toward the trajectory twist relative
to the data curvature, so it is dimensionless and treats `omega` and `v` alike.

**The prior is not optional.** On a synthetic capture whose true per-readout
velocity is deliberately not what its neighbours imply (a global-shutter bundle
adjustment leaves 5.610% of the camera spread and 9.23 deg):

| prior | rounds | camera centre | rotation | twist error |
|---|---|---|---|---|
| 0 | 1 | 5.874% | 9.62 deg | 98.9% -- worse than no twist at all |
| 0 | 8 | 2.050% | 4.32 deg | 49.4% |
| **0.5** | **6** | **0.944%** | **1.26 deg** | 64.2% |
| 2.0 | 6 | 1.552% | 1.44 deg | 71.8% |

Two things worth reading off it. A free twist (prior 0) is *worse than no twist*
after one round: with six unconstrained parameters per image it absorbs pose
error rather than the shutter, which is the degeneracy above arriving on
schedule. And the twist error is worst where the poses are best -- the pose is
what a reconstruction is judged on, and the prior buys it by refusing to let the
twist explain everything.

Six rounds, prior 0.5: 5.9x the camera-centre accuracy and 7.3x the rotation
accuracy of the trajectory twist alone. The 6x6 itself is exact -- given the true
poses and points it recovers the twist to 0.0% -- so everything above is the
alternation, not the solver.

## How small a readout is detectable

Injecting a known readout into a real model, letting a **global-shutter** bundle
adjustment absorb what it can (which is most of it), and then asking what a free
per-image twist could still explain -- 498 frames of a 1280x720 USB camera:

| injected | a free twist removes | coherent with the trajectory | fit detects it |
|---|---|---|---|
| none | 3.0% | 0.34 | no |
| 0.05 | 4.4% | 0.55 | yes |
| 0.20 | 12.9% | 0.74 | yes |
| 0.90 | 49.2% | 0.83 | yes |

The 3.0% at zero is the overfitting floor of six free parameters per image, not
signal, and the coherence column is what separates the two. Detection holds down
to 0.05 of a frame interval, so the guard is not what limits sensitivity.

The estimator is unbiased: injecting +-0.35 into a global-shutter capture (KITTI)
comes back at +-0.3504. Injecting the same into that USB camera comes back
**0.045 high at every injection from -0.9 to +0.9** -- an additive offset, which
is that camera's own readout: about 0.045 of a frame, ~1.5 ms at 30 fps. Real,
measurable, and far too fast to be worth correcting.

That is the useful calibration: a readout under ~0.05 of a frame interval is
below what is worth acting on, and the pass says so rather than claiming the
camera has no shutter.

## What is not handled

**Electronically stabilized video.** EIS applies its own per-row warp, so rows
are no longer lines of constant time and the linear-in-row model does not apply.
The guard rejects it rather than fitting it, which is the right outcome, but the
shutter is not recoverable from such footage at all -- capture with
stabilization off.

**Rendering, end to end.** The projection *math* is in, in the shared Slang both
backends compile: `shutter_camera` moves the camera to the shutter time a
Gaussian's centre is read out at, and the caller hands the result to
`projection_3dgs` as its pose.

That solve is where a renderer normally pays. `s = dot(axis, p(s)) - 0.5` is
implicit -- where a point lands decides when it was seen -- and 3DGRUT/gsplat
answer it with a ten-step fixed-point iteration. Over one readout `p` is
near-linear in `s`, so a central difference of `dp/ds` turns it into

```
s = (dot(axis, p0) - 0.5) / (1 - dot(axis, dp/ds))
```

one divide and one extra point projection, with the denominator guarding the case
where the feature outruns the scan.

**It lives outside `projection_3dgs` on purpose.** Putting the branch *inside*
that function cost ~1% of a training step even with the branch compiled out, and
grew `src/generated/primitive_3dgs.cuh` by 72%: projection is icache- and
register-bound, so merely having the shutter's locals in scope perturbed how the
hot function was emitted. Hoisting it leaves `projection_3dgs` byte-identical to
before (191 lines in, 191 out; only temporaries renumber) and the generated
header grows 4%.

Measured with the two builds **interleaved**, which is not optional: the machine
drifts several percent over an hour, and a non-interleaved comparison reads that
drift as a result.

Projection only matters once there are splats to project, so the run that counts
is `--cap-max 5000000 --min-init-fraction 1.0` -- 5M splats from step 0, held for
all 600 steps. Four rounds in each order, to catch within-round bias:

| | base | with the shutter |
|---|---|---|
| base first | 66.87 s | 65.63 s (-1.8%) |
| shutter first | 65.60 s | 64.73 s (-1.3%) |
| pooled | 66.23 s | 65.18 s (-1.6%) |

The shutter build is *faster*, in both orders. That is not a speedup the feature
earned -- it is code-layout variance between two binaries, which is the noise
floor of this comparison. What it does establish is that there is no regression
at the splat count where projection is the bottleneck.

(At 2000 steps of ordinary training, where the model starts small, the two are
61.00 s and 61.25 s -- also indistinguishable.) The binary grows 3.1 MB (2.2%).

On Vulkan a `kRollingShutter` specialization constant gates the call, so a
global-shutter pipeline has it folded out entirely rather than branched over.
It is declared **last** in each module, so a host `SpecList` that stops at
`kDistortion` leaves it 0 -- which is every pipeline today.

The data path is wired end to end:

```
rolling_shutter.bin  ->  ColmapParser      ->  ParsedDataset::rs_twists [N,8]
transforms.json keys ->  NerfstudioParser  ->        |
                                                     v
                             DataManager -> DecodedBatch::rs_twists_view
                                                     |
                        set_camera_params -> CameraTable::twists (device [C,8])
                                                     |
                    the six kernels that build a ProjCameraT -> shutter_camera
```

The axis is built from the RENDERED width and height, so a downscaled dataset
needs no adjustment, and the poses are handed over unscaled on both parser paths
(`train_frame_scale` is computed, not applied), so `v` needs no rescaling either.

Verified by writing a synthetic sidecar over a real 231-image capture and
sweeping the twist: PSNR falls monotonically (15.71 / 15.24 / 15.05 / 14.76 for
`omega_y` = 0 / 0.005 / 0.02 / 0.08). Applying a shutter the capture does not
have makes the fit steadily worse, which is the expected sign.

Two things that made this easy to get wrong, both worth remembering:

- Short training runs here are **not deterministic** -- the same configuration
  twice gave 12.2 and 14.9 dB. A single pair proves nothing; read a monotone
  trend over several points, or the noise will happily confirm anything.
- The engine reaches `set_camera_params` by more than one route. A dataset whose
  cameras are re-distorted takes a *different* branch from the plain one, and a
  twist wired into only one of them silently never arrives. `twists=(nil)` at the
  forward is the check that catches it.

And one that hangs rather than misbehaves: **a params struct that arms
`kRollingShutter` but never assigns `twists` leaves a null device address the
shader then dereferences**, which pins the GPU at 100% with no error. Every
struct that arms the constant must fill the pointer in the same function.

Gradients come back with respect to the *shuttered* pose, not the reference one;
they differ by the constant `exp(s*omega)` and nothing optimizes camera poses
under a rolling shutter yet. SH colour keeps the reference pose -- the view
direction moves under 0.05 rad over a readout.

**3DGUT and `split_batch` are in.** The eval3d rasterizer builds each pixel's
world-space ray from the pose at `s = dot(axis, pixel) - 0.5` -- no solve, the
row is known. In the backward the ray ORIGIN only moves by `-s * (R^T v)` to
first order, which is a block constant times a per-pixel scalar, so it costs no
extra shared memory in a kernel that is already tight on it. `split_batch` and
the mixed-resolution sub-batch path slice the `[C,8]` twist alongside `viewmats`.

Both are held to CUDA/Vulkan parity with a **non-zero** twist armed in
`render_parity` and `raster_bwd_parity`: the per-pixel ray shutter is written
twice, once in CUDA and once in Slang, and that is what pins them together.

**The fisheye/equirect warp path still refuses**, and the reason is not plumbing.
A split face is a pinhole view of a fisheye sensor, so the shutter time of a face
pixel is a function of the pixel it came from on the *source* sensor -- which is
not linear in face coordinates, and `rs_axis` can only express a linear one.
Fitting a plane to it costs:

| face | s range | linear-fit residual |
|---|---|---|
| centre, 60 deg | +-0.236 | 3.9% of range |
| centre, 90 deg | +-0.354 | 7.6% |
| side, 60 deg | +-0.458 | 13.4% |
| side, 90 deg | +-0.696 | **18.3%** |

An 18% error in `s` is 18% of the correction applied in the wrong place, smoothly
across the face -- exactly the shape that biases geometry rather than blurring
it. So the warp path needs the exact form instead: the rasterizer already has the
face ray, so rotating it into the source camera frame and projecting it through
`source_project` (camera_source.slang) gives the true source pixel and hence the
true `s`. That costs a per-camera payload of the face rotation, the source
intrinsics and model id, and it is the same reprojection the Gaussian-centre
solve in `shutter_camera` would need. Equirectangular needs none of it -- a 360
rig has no rolling shutter to correct.

Two more pieces are designed but unbuilt. The **covariance smear**: a Gaussian
spanning many rows is stretched, and to first order that is a rank-1 update to
the projection Jacobian, `A = (I + (g/H) e_v^T / (1 - g_v/H)) A_0` by
Sherman-Morrison -- same denominator as the `s` solve, and it keeps the output a
standard 2D conic, so 3DGS and Mip need no new format. And the **eval3d
rasterizer**, where 3DGUT's correctness actually lives: each pixel already builds
its own world-space ray, and the pixel's row is *known*, so there is no solve at
all -- interpolate the pose at `s = dot(axis, pixel) - 0.5` and hand
`transform_ray_o/d` the result.
