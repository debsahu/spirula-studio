# Sun ghost removal

When the sun is inside a fisheye lens, that lens records stray light the scene
never had. The visible part is **ghosts**: internal reflections between the
lens surfaces, the sensor stack and any filter, each a defocused image of the
aperture clipped by a rectangular stop -- a soft-edged rounded rectangle of
nearly flat brightness near the line from the optical centre through the sun.
A splat trained on such frames bakes the ghost into the scene as a floating
pale patch that moves with the camera.

`app/FlareRemoval.{h,cpp}` finds and subtracts them. It is a port of OpenOSV's
WP-FLARE removal (https://github.com/Kemerd/OpenOSV, commit
`e169fe1dd7b1e2da7cd0a52734393dc155670722`, `src/osv/render/Flare.cpp` and the
`[WP-FLARE]` functions of `osv_kernel.h`), Apache-2.0; attribution is in
`LICENSES/NOTICE-OpenOSV.txt`. OpenOSV's own write-up (`docs/research/FLARE.md`
there) has the characterisation of the ghosts, the literature and the patent
notes this port relies on.

## Using it

Off by default everywhere.

| where | switch |
|---|---|
| GUI, dataset preparation, advanced options | "Remove sun ghosts (fisheye clips)" (preset key `flare_removal`) |
| `spirula sam flare <images> --curve dlogm\|bt709` | a folder of frames already on disk, in place |
| `spirula sam extract <video> --flare-removal` | after the built-in decoder writes the frames; refused with `--model`, because that masks in the same pass |

`sam extract` needs the in-process video decoder, which MoltenVK does not
expose, so on macOS the GUI (through ffmpeg) and `sam flare` are the routes.

The GUI applies it only to fisheye inputs (`.osv`, `.insv`, packed-lens
`.insp` / `.lrv`), never to a 360 packing turned into views: the ghost search
runs along the sun line through the lens's own optical centre, which a
reprojected view does not have. Toggling it moves the frames stamp, so the
frames are extracted again rather than half-treated.

## Where it runs

On the frames on disk, right after extraction and before masking, the colour
record, SfM, geometry or training read them (`DatasetPrep::run` calls
`remove_flare` straight after `extract_video` for each input).

**Relative to D-Log M it runs in linear light, before the trainer's decode and
independent of it**, as OpenOSV does: each code value goes through the
per-channel curve to the lens's *native* linear light (OpenOSV's
`osvCodeToLinear`: the D-Log M curve for a D-Log M clip, the BT.709 camera
curve otherwise; no primaries matrix, no exposure), the ghosts are fitted and
subtracted there, and the result is encoded back to the same code values. The
file stays a D-Log M frame, and `--image-color-log` decodes it later exactly
as before. Ghosts are additive in linear light and are not in log code, which
is why the subtraction cannot happen on the codes.

Which curve: from the clip's `djmd` record (`sfm::video_color`). D-Log M uses
the D-Log M curve; Normal uses BT.709; another DJI log profile is skipped with
a log line; an unknown or unrecorded mode is read as
BT.709 and logged as an assumption.

## What it does, per frame

1. A factor-4 working image of native linear RGB (960 x 960 for a 3840 x 3840
   lens), each pixel the mean of a 2 x 2 sample grid in its block.
2. **Sun**: the largest region within 92% of the frame maximum, which must be
   compact (aspect <= 1.8, fill >= 0.5), with the maximum at least 6x the
   median. Otherwise there is no sun and the frame is not touched.
3. **Seeds**: relative band-pass bumps over 3% within +-20 degrees of the sun
   line (either side of the axis), beyond 3 sun radii, on low texture only.
4. **Fit**: Levenberg-Marquardt over a rotated rounded rectangle, the
   background and amplitude solved linearly at each step; then a caustic rim
   and a brightness tilt at that geometry.
5. **Gates**: aspect <= 3, not on a fit bound, plateau >= 4% of the
   background, and >= 50% of the footprint's variance explained. At most 4
   ghosts.
6. **Subtraction** at full resolution, only within each ghost's reach, with
   `f(x) = x - g x^3 / (x^3 + g^3)`: never negative, never below 0.47 x, never
   reversing a gradient. Pixels the ghosts add no light to keep their exact
   code values.

The file is rewritten (through a temporary and a rename) only when a ghost was
removed: a 16-bit PNG through `save_png16`, an 8-bit PNG through stb, a JPEG
at quality 95. **A rewritten JPEG is re-encoded as a whole**, so its pixels
outside the ghosts move by JPEG generation loss; PNG frames change only inside
the ghosts. `.spirula-flare` beside the frames records each file as it is
done (name, size, time), so an interrupted or resumed run never subtracts
twice.

## Not ported

- **The veil estimate** (OpenOSV's `estimateVeil`): OFF in OpenOSV too, because
  estimating flare from the two lenses' overlap is claimed by active GoPro
  patents (US11330208B2 and related, per OpenOSV's notes).
- The seam cost hook, temporal smoothing and the per-sun-position model cache:
  they serve OpenOSV's stitcher and playback. Here every frame is analysed on
  its own.
- The CUDA sampler. **CPU only, measured**: on one 3840 x 3840 16-bit D-Log M
  fisheye frame (M5 Pro), the downsample takes 3.5-3.9 ms and the analysis
  2 ms with no sun, 83 ms with a bright window taken for the sun and six
  candidates fitted; the PNG decode is 300-330 ms of the ~0.4 s total. The
  per-pixel work is two orders below the file I/O, so a Vulkan kernel would
  buy nothing measurable. The GUI and `sam flare` run four frames at a time
  (~0.55 GB peak for two frames).

## Assumptions

- **The lens is centred with its image circle touching the frame edges**
  (`centred_lens`). True of the Osmo 360's 3840 x 3840 tracks by inspection;
  not calibrated. The circle only bounds the search (97% for the sun, 95% for
  seeds), and the corridor tolerates the offset of an uncalibrated centre.
- The defaults are OpenOSV's, tuned on its one sample clip (dual 3000 x 3000
  D-Log M, the sun in one lens, shot through an ND filter).

## Checks

`flare_removal_test` (synthetic lens, the cases of OpenOSV's
`tests/unit/test_flare.cpp`): the ghost shape and its reach, the soft
subtraction's bounds, recovery of a planted ghost (centre +-0.3 px, extents
+-0.5 px, angle +-0.03 rad, amplitude +-8%) and of its rim and tilt, refusal
without a sun, on texture and off the sun line, the mirrored side searched,
>85% of the ghost's light removed, the linear-light downsample, the curve round
trips, a frame and a file changed only inside the ghost. Each test names the
wrong implementation it catches; all were run against it.
`frame_bits_test` T8 runs the pass through `DatasetPrep::run` on a real
two-track clip and checks a resumed run skips it.

On real frames of an Osmo 360 D-Log M clip (indoors, no sun; one frame with a
large bright window, one with a window at the lens rim taken for the sun and
six candidates tried): no frame changed, files byte-identical.

OpenOSV's sample clip is not published, so its before/after could only be
approximated from the 8-bit display crop in its `research/flare/`
(`sun_view_before_after.jpg`). On the "before" half, with the sun gate lowered
to 2x the median (a tone-mapped display image has lost the sun's range) and
the cloud bank cropped away, the port found the pill ghost at 83 x 50 px,
explaining 0.86 of its footprint's variance (OpenOSV reports 0.86 for the same
ghost), and its result sits 0.97 display levels from OpenOSV's "after" over the
ghost's box, against 3.30 for the untreated frame.
