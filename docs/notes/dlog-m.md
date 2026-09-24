# D-Log M input

DJI's D-Log M is a log encoding: the camera stores `log(light)` so that a
10-bit file keeps highlights an ordinary Rec.709 clip would clip. Trained as
if it were an ordinary photo, a log clip gives a flat, washed-out model with
the wrong exposure. `--image-color-log dlogm-osmo360` decodes it back to
scene light first.

## What the flag does

```
--image-color-log dlogm-osmo360     # the training images are D-Log M from an Osmo 360
--point-color-log none | off | dlogm-osmo360
```

The decode takes a code value to scene-linear light (mid grey 0.18) and
through the Osmo 360's primaries matrix to **linear Rec.2020**. That output is
fixed, so the flag also fixes the other two halves of the image side:

| resolved | value | set explicitly |
|---|---|---|
| `--image-color-is-linear` | on | `1` agrees; `0` gives way (plain sRGB, below) unless the gamut says Rec.2020 |
| `--image-color-gamut` | `Rec.2020` | `Rec.2020` agrees; `Rec.709` gives way unless linear is on; anything else is refused |
| `--image-color-transfer` | unchanged (`srgb` unless set) | allowed |

"Plain sRGB" -- gamut unset or `Rec.709`, and not linear -- is what an
ordinary photo is, and it is what the `hdr` preset states for its input. The
log flag is the more specific statement, so it wins over it silently:
`spirula train hdr --image-color-log dlogm-osmo360` decodes the images to
linear Rec.2020 and keeps the preset's linear ACEScg splats. A statement that
names another space (`ACEScg` input, linear Rec.709, display-encoded Rec.2020)
contradicts the decode and is refused.

After the decode, the existing conversion in `docs/notes/color-transfer.md`
runs unchanged: `display = TONE(M · c)` with `M` the Rec.2020 -> Rec.709
matrix. The splat side follows the image side as always, so a run trained
this way stores **linear Rec.2020** splats, which the GUI viewport and
`viewer/` both display correctly.

**For delivery through a viewer that expects sRGB splats**, pass
`--splat-color-gamut Rec.709 --splat-color-is-linear 0`. The images are
still decoded; only the splats' storage changes.

`--point-color-log` is the seed cloud's curve. Unset (`none`) follows the
images, which is right for a cloud the SfM sampled from the same log frames:
without the decode those seeds start about 1.15 stops too bright (code 0.4 is
0.18 decoded, 0.4 read as linear). A log point side is forced to linear
Rec.2020 the same way the image side is.

`off` says the colours are **ordinary sRGB**, for a cloud made from other
footage: the point side then stops following the (decoded) image side and is
read exactly as it would be in a run with no log flag at all, unless
`--point-color-is-linear` / `--point-color-gamut` say otherwise.

An `--init-ply` DC goes through the point conversion too. For a PLY trained by
an earlier D-Log M run with default splats (linear Rec.2020), pass
`--point-color-log off --point-color-is-linear 1 --point-color-gamut Rec.2020`.
One point setting covers both sources, so `--init-ply-add-points` cannot mix
such a PLY with log SfM points; seed from one or the other.

## Where it runs

- **Training images:** on the device, in `_engine_color_space_apply_to_gt`,
  after upload and before `working_to_display`, on both the plain and the
  warped upload path. The kernel is `input_curve_decode_forward`
  (`ImageColorOps.cu`, and `input_curve_decode_fwd` in
  `pixel_wise_render.slang`); the math is `src/shaders/dlogm.slang`.
- **Host mirrors:** `src/core/DlogM.h` holds the same constants for the seeds
  (`PointToSplat`) and for the mean-luma features (`_engine_color_space_gt_pixel`,
  which `luminance_normalization` and `background_match_luminance` read).
  `gt_decode_dlogm` holds the device to both.
- **Not decoded:** SfM, masking and `spirula geometry` load frames through
  `stbi_load` and see the flat log images. Features and masks on flat footage
  are likely somewhat worse (not measured).
- **Image compare:** the GT pane shows the decoded GT. The "source file" pane
  decodes a log file into the space the render pane shows (the splat working
  space, or display values), so the pair compares light with light
  (`source_pixel_for_compare`, `TrainerCore.cpp`).

`InputCurve` (`core/ColorSpace.h`) is its own axis, deliberately not a value of
`colorspace::Transfer`: `Transfer` is an *output* curve, and its numbering is
shared with the viewport's tone menu and the web viewer's `uTransfer`.

## Bit depth: what 8-bit frames cost

The Osmo 360 records 10-bit HEVC, but both frame extractors write 8-bit
(the built-in decoder packs `uint8`; the ffmpeg path writes JPEG). Measured
on the curve over 2M uniform codes in [0.05, 0.95], decoded then sRGB-encoded:

| extraction | error in stops, RMS / max | sRGB display error in 8-bit levels, RMS / max | largest display gap between adjacent codes |
|---|---|---|---|
| 8-bit | 0.013 / 0.086 | 0.53 / **1.6** | 2.1 levels |
| 10-bit (or a 16-bit PNG of it) | 0.004 / 0.025 | 0.16 / 0.48 | 0.6 levels |

An 8-bit sRGB JPEG quantizes at 0.29 RMS / 0.5 max levels, so 8-bit D-Log M
is about 3x coarser. On one real 3840² Osmo 360 frame, encoded to D-Log M
codes and decoded by the trainer's own GT upload, the 8-bit error came to 0.49
RMS / 1.3 at p99.9 / 3.9 max display levels (16-bit codes: 0.002 RMS). The max
exceeds the table's because the table stops at code 0.95 and uses grey only:
on the grey axis the gap between adjacent 8-bit codes reaches 3.6 levels in the
highlights above code 0.5, where display values pass 1.0. On footage shot in
D-Log M (a frame of `..._0130_D.OSV`, 230,400 pixels) 8-bit codes cost 0.47
RMS / 1.77 at p99.9 / 2.73 max display levels.

### 16-bit frames

`PrepJob::frame_bits` (the GUI's "Video frame depth"; preset key `frame_bits`)
is 0 = auto, 8 or 16. Auto is 16 for a clip whose djmd reads D-Log M and 8
for everything else, so a Normal clip costs nothing extra. A 16-bit input
always goes through ffmpeg, whatever the decoder setting: the built-in path
packs `uint8`. The candidates stay JPEG and the sharpest of each group is
chosen on **track 0 alone**; every track is then decoded again and only those
candidates are written, as 16-bit PNG named by candidate index. Both lenses
hold the same stems, so the capture is recorded lockstep.

The conversion is ffmpeg's `colorspace` filter with a 12-bit 4:4:4
intermediate, then `rgb48be`. swscale's own 10-bit to RGB48 conversion is not
used: on grey it is a 0.9962 gain (Y=940 lands on 65283, not 65535), and on
saturated chroma it misses BT.709 by up to 8.2% of full scale. With the filter
Y=64/502/940 land on 0/32768/65520 and colour stays within 0.22%.

Cost, measured on the Osmo 360's 3840² fisheye frames: about 32-37 MB per
frame against 0.6 MB for JPEG, and a second decode of every track. A 9 s clip
at 2 fps (36 frames) wrote 1.3 GB in 78 s. The prep log states the estimate
before the second pass. Keep `cache_images = disk` for such a dataset: `cpu`
holds every frame decoded, 88.5 MB each.

## Telling a D-Log M clip from a normal one

From the clip's metadata, never from its pixels. The Osmo 360 records the mode
in the `.OSV` file's `djmd` metadata track: in sample 0, top-level field 2
(stream meta), then field 4, then field 1 is the colour mode -- **19 is D-Log
M**, 0 is Normal (an empty field 4 decodes to 0). The video stream's own tags
say `bt709` either way. `sfm::video_color` (`src/sfm/core/Telemetry.cpp`) reads
it; five real clips read as recorded (three D-Log M, two Normal).

What it returns:

| what the file carries | mode |
|---|---|
| `dvtm_oq101.proto`, colour mode 19 | D-Log M |
| `dvtm_oq101.proto`, colour mode 0 or an empty field 4 | Normal |
| `dvtm_oq101.proto`, any other colour mode (D-Log 2, D-Log2 22, HLG 9...) | log, unsupported |
| `dvtm_oq101.proto`, field 4 missing, of the wrong wire type, unparseable, or holding anything but a varint field 1 | unknown |
| any other DJI layout (Avata 360, ...) | unknown |
| a `djmd` track whose first 8 samples hold no readable clip header | unknown |
| no `djmd` track | not recorded |

Only an empty field 4 is proto3's unwritten 0. Any other shape the Osmo path
was not written against is unknown, never Normal. The Avata 360
(`dvtm_AVATA360.proto`) has `fov_type` at StreamMeta field 4, where the Osmo has
the colour mode, so reading it the Osmo way would find a 0 that means nothing.

### The record

Extracted frames carry no mode, so the GUI's dataset preparation writes each
input's mode to `.spirula-color` in the dataset root (`src/data/DatasetColor.h`),
one line per input: `<mode> <code> <layout or -> <file name>`, where mode is
`dlogm`, `normal`, `unsupported-log`, `unknown` or `unrecorded` (photos, a
non-DJI video). Each unknown input is logged with its layout as it is read.

- It is written to `.spirula-color.tmp` and renamed into place. If that fails,
  the old record is removed and the failure logged. If even the removal fails,
  preparation stops and names the file, because a stale record would decode
  every frame the wrong way.
- A line that does not parse, and a record that is there but cannot be read,
  both read as an unknown input.
- When the inputs change, the frames of inputs no longer in the job are removed
  from the dataset's `images/`, so nothing is trained that the record does not
  describe. A folder an input is read from is never removed.
- The same prune runs when `images/` holds frames but `.spirula-frames` is
  missing, as after an interrupted prep. Those frames are not called stale, so a
  resume still keeps what the interrupted run finished; a lone input keeps its
  root frames and its camera folders (`cam<k>/`, or its own photo subfolders).
- A stale entry that is a directory symlink is unlinked and logged with its
  target. The link is never followed, so what it points at is never touched.
  Anything that cannot be removed is logged.
- Clear project removes the record.
- `spirula sam extract` writes no record, and warns when the video is D-Log M,
  in an unsupported log profile, or unreadable. A Normal clip stays silent.

### What training does with it

`--image-color-log` defaults to `auto`, which `TrainerSession::load_dataset`
settles from that record and logs:

| record | `auto` becomes |
|---|---|
| every input D-Log M | `dlogm-osmo360` |
| D-Log M beside anything else (Normal, another profile, unknown, photos) | refused: split the dataset, or set the flag only if every input is D-Log M |
| any input in another DJI log profile | refused: prepare without it, or set `none` to train it undecoded |
| an `unknown` input, no D-Log M | no curve, and a line naming the input and asking for the flag (for the Avata 360: "Avata 360 colour mode not readable yet") |
| every input Normal | no curve |
| no record | no curve, nothing logged |

Any explicit value, `none` included, is kept and logged as set. The seed
points follow the images as before.

`config.json` keeps what was asked for under `image_color_log` (`auto`), and
the curve it settled on under `image_color_log_resolved` (`dlogm-osmo360` or
`none`). That key is not a flag: a resume reads it and never re-detects, while
a preset or a batch row made from the file ignores it, so one dataset's answer
does not follow the preset to the next dataset. A `null` `image_color_log`, as
older files have, is an explicit `none`.

## Where the numbers come from

The curve and the matrix are OpenOSV's (Apache-2.0,
https://github.com/Kemerd/OpenOSV, read at commit
`3a39776272efb5dfdc1d29711ae746e855383084`), ported as 16 constants:

- `kDlogMOsmo360`: `lin = mgs · (t < cut ? t·slope + intercept : t·slope2)`,
  `t = 2^(scale·code + yShift) + xShift`, `cut = intercept / (slope2 − slope)`
  (the branches meet at code 0.1252; the slope kinks there, the value does
  not). Anchors: code 0.40 -> 0.18000, 0.714 -> 0.95775, 1.0 -> 3.76470.
  Input is full-range R'G'B' from narrow-range BT.709 Y'CbCr.
- `kNativeToRec2020_Osmo360`: rows sum to 1 (white stays white),
  determinant 0.873349, all three implied primaries have positive luminance.

Both are OpenOSV's least-squares fits to DJI's publicly distributed Osmo 360
D-Log M -> Rec.709 LUT: the curve to its 33 neutral-axis entries, the matrix
to all 35937. So "scene-linear" here means linear under that model of DJI's
rendering, not a radiometric calibration of the sensor; DJI ships the same LUT
for the Pocket 3. The curve's residual against the LUT is 0.016 RMS (HLG code)
above code 0.24, and its toe below 0.24 is loosely constrained. The matrix
cannot reproduce DJI's gamut mapping of saturated colours.

Not ported, on purpose: OpenOSV's Pocket 3 curve and matrix (a different
camera, a matrix with a negative-luminance blue primary, and taken from an
unlicensed repository), its older `kDlogMDjiRefit`, its Rec.709 "look", and
its tests' table of measured DJI LUT values. The tests here check the curve's
own anchors instead.

### OpenOSV NOTICE

The parts of OpenOSV's NOTICE that pertain to this port are in
`LICENSES/NOTICE-OpenOSV.txt`, verbatim, with a short preface. It includes
the NOTICE's bullet on the `kDlogMDjiRefit` curve, which is not ported,
because the Osmo 360 bullet's "of the same form" refers back to it. The files
that carry the port -- `src/core/DlogM.h` and `src/shaders/dlogm.slang` -- say
so in an SPDX header, and the licence text is `LICENSES/Apache-2.0.txt`.
`tools/package_macos.sh` copies `LICENSE` and `LICENSES/` into the app bundle
(`Contents/Resources`), so the macOS DMG carries both (Apache-2.0 §4(a), §4(d)).
That also covers the Apache-2.0 code the tree already had (`shaders/ppisp.slang`,
`shaders/harmonics.slang`), which shipped without the licence text before.

## Tests

| test | holds |
|---|---|
| `dlogm_osmo360` | the curve's anchors on both branches, continuity at the cut, the round trip (2e-5), the matrix's row sums, determinant and layout, and code -> Rec.709 through spirula's own Rec.2020 matrix |
| `color_resolution_test` | what `resolve_color` makes of the two flags, the refusals, the `hdr` preset, the transfer left alone, the seed colours (including `off` against a run with no flag), and the compare panel's source decode |
| `gt_decode_dlogm` | the device decode against the host curve and the mean-luma mirror, the misuse guard, and that `engine_reset` clears it |
| `dlogm_session_test` | the flag through `TrainerSession::setup_engine` and one real step: the decode is armed, the uploaded GT is decoded, and the brightness match measures decoded light |

None of them uses D-Log M footage: none was available. What was checked
instead is ordinary footage encoded to D-Log M codes with the inverse curve.
One real 3840² Osmo 360 frame through the trainer's GT upload comes back at
0.002 display levels RMS from 16-bit codes (0.49 from 8-bit). A 69-photo
scene at 1/8 resolution, encoded to 8-bit codes and trained for 3000 steps with
the flag, lands its decoded eval GT within 51.8 dB of the original photos and
its renders at 21.3-21.8 dB against them, next to 20.8 dB for the same scene
trained from the photos directly; without the flag, 15.7 dB. Those are short
runs on one scene: they show the decode is wired end to end, not that D-Log M
trains better or worse than a normal clip.
