# The mask editor: hand corrections that survive a re-run

Phase 5 of [gui-editing-plan.md](gui-editing-plan.md), "The 2D half". It
lives in `src/app/gui/mask/`, a sibling of `edit/` and `render/`.

```
MaskLayer     what is on disk: mask_edits/<key>.{base,drop,keep}.png, index.json
MaskDoc       one frame in memory: base, two layers, composite, undo history
MaskWindow    the view: pane <-> mask mapping, the window texture's pixels
MaskSession   the frames of a dataset, the worker, the open frame
MaskPanel.cpp the window: canvas, tools, status, navigation
```

## Two levels

A stencil belongs to a camera (`app::FrameStencil`, edited in `SegmentPanel`,
applied by the run as `written = sam(frame) ∩ stencil(camera)`). A hand
correction belongs to a frame and has to be able to RESTORE a pixel the model
dropped as well as drop one it kept, which an intersection cannot express. So a
correction is a second level composed after the written mask, last wins:

    final = keep ? 255 : drop ? 0 : base        with keep ∧ drop = ∅

Polarity is the app's everywhere: 255 = keep.

## On disk

`<dataset>/mask_edits/` mirrors `masks/`. Per edited frame:

| file | holds |
|---|---|
| `<key>.base.png` | a BYTE COPY of `masks/<key>.png` as the run wrote it, made on the first save, never edited |
| `<key>.drop.png` | 255 where the user forced drop |
| `<key>.keep.png` | 255 where the user forced keep |

`index.json` records, per key, the FNV-1a 64 fingerprint of `.base.png` and of
the composite as last written into `masks/<key>.png`, both as 16-hex strings
(JSON numbers are doubles), the kept fraction and a UTC timestamp.

Why not inside `masks/`: "Masks again" deletes the whole tree
(`DatasetPrep.cpp`, `clear_generated`), and `sfm::MaskIndex` walks it and
claims stripped stems. Why not beside `masks/` wherever that is: a mask root
can sit inside the image tree, where a sibling is walked as images. The
dataset root is the one place nothing walks; a workspace that is itself inside
the photo folder is refused with a message.

Reverting a frame copies `.base.png` back over `masks/<key>.png` byte for
byte, then removes the three layer files and the index entry. If a layer
file cannot be removed, revert refuses: it reports the failure and keeps the
index entry standing rather than claiming success, because an orphaned
`.drop.png` with no entry pointing at it would be silently read as a live
correction the next time the frame opens. Reverting every edited frame at
once has the same shape as the re-masking pass below: one frame's removal
failure does not stop the rest, every frame in the index is still attempted,
and the ones that failed are named rather than folded into the count of
ones genuinely reverted.

## Re-masking

Five sites write masks. Instead of five hooks there is one pass at the end of
`DatasetPrep::run`'s masking stage over the frames `index.json` lists: a mask
whose fingerprint differs from the recorded composite fingerprint is a new
base, copied to `.base.png`, and the layers are re-composed over it. A mask
whose fingerprint matches is the composite written last time and is left
alone. A missing mask (a cancelled re-run) leaves the layers in place. A
frame the pass cannot re-base -- its layers no longer match the mask's size
-- does not stop the rest of the pass: every other frame `index.json` lists
is still attempted, and the ones that failed are named, not folded into the
count of ones that succeeded or left unreported. The same check runs when a
frame is opened in the editor. A `spirula sam` re-mask from the command line
is re-composited on the next open.

Known limitation: a crash between the composite's rename and the index
write leaves the composite looking regenerated, and the next open copies it
over the base. The window is two renames wide.

## Painting

A plain or Shift+left drag forces drop, Ctrl+left drag forces keep, and
Shift+Ctrl+left drag clears the correction back to the base: the 3D editor's
selection grammar (`EditSession::combine_now`) over the dropped set, with the
modifiers read on the frame the stroke completes, as that editor reads them.
Both held has no Intersect meaning on a layer and is Clear here. The right
button paints nothing; it only closes a polygon. The tools are `edit/EditTool` (box, ellipse,
lasso, polygon, brush) fed PANE pixels, so its gesture constants stay screen
sized; the finished stroke is mapped to displayed then stored mask pixels and
rasterised at the mask's size with `rasterize_shape`. Nothing in memory is
turned for EXIF: the window derivation and the stroke mapping both go through
one displayed-to-stored function.

Layers are byte planes rather than `gui::Selection`: a stroke touches only
its own rectangle, which is what keeps an 8K stroke inside its budget. Undo
stores the RLE of each layer's rectangle before and after, on the
`EditOp` / `EditDoc::run` pattern with the same 96-op / 256 MB caps.

The window texture holds the visible window in displayed mask pixels, at
most 4096 square, box filtered beyond; a committed stroke re-derives and
uploads only its rectangle with `glTexSubImage2D`.

## Measured floors

Filled in by the implementation (Task 9 of the plan). Machine, fixture size,
three repeats each, median.

| quantity | median | notes |
|---|---|---|
| load_rgb 8K JPEG | 78.1 ms | cold decode from disk each call |
| load_stencil 8K PNG | 44.6 ms | |
| encode_gray_png 8K mask | 226.1 ms | |
| rle_encode 8K plane | 10.8 ms | |
| stroke commit (CPU) 8K, 20 strokes | 1.1 ms | worst max across 3 runs 4.8 ms; bar 100 ms median / 250 ms max |
| history bytes after 20 strokes | 60039 bytes | identical in all 3 runs |
| derive 4096x3840 window (full re-derive) | 7.0 ms | |
| derive whole mask, step 2 | 8.3 ms | |
| MaskDoc::save 8K | 702.5 ms | |
| undo x20 + redo x20 | 9.3 ms | |
| resident memory, one 8K frame open | 387.3 MB above baseline | out-of-band `/usr/bin/time -l` peak RSS, not part of the committed bench; see note below; bar 600 MB |

M5 Pro, 18 cores, macOS 26.6.2, load average ~2.96/18 during measurement (quiet,
not idle). Fixture: 7680x3840, three synthetic frames. Three repeats of the
full test binary; the table above is the median of those three runs, taken
per line. Criterion #9's row is not produced by `bench_8k` -- the brief for
this task specifies no memory instrumentation -- it is `/usr/bin/time -l`'s
maximum resident set size (peak over the process lifetime, from `getrusage`,
not a snapshot at exit) for the whole `mask_doc_test` binary with
`SS_MASK_BENCH` set, minus the same measurement with it unset (baseline
~11.0 MB, three repeats each, both stable to within 2 MB). The delta spans
the moment inside `bench_8k` where the decoded RGB frame, all four mask
planes, and the largest window texture are simultaneously live, which is the
"one 8K frame open" case criterion #9 describes, but it also includes
whatever transient codec buffers happen to be resident at that instant.

Criterion 4 of the plan (stroke commit at 8K, median of 20 strokes of radius
100 px and 500 px length, bar 100 ms median / 250 ms max): CPU half measured
at 1.1 / 4.8 ms. PASS. The GL upload is added by the panel and reported live
in its status strip ("Last stroke"); see the in-app measurement below.

Criterion 9 of the plan (resident memory <= 600 MB above baseline with one 8K
frame open): measured at 387.3 MB above baseline. PASS, by the out-of-band
`/usr/bin/time -l` method described above, not by anything in `bench_8k`.

**Why the margin is ~90x and not ~2x** (carried from the Task 9 review). Plan
line 43 predicted a naive `gui::Selection` approach would pay "two 29.5 MB
passes per layer per stroke" -- a whole-image `combine()` on every stroke,
which at 8K would eat most of the 100 ms budget on its own and leave the
design with little headroom over its own bar. The shipped design (deviation
2, "Layers are byte planes rather than `gui::Selection`") pays only for its
own bounding box: a stroke touches its rectangle and nothing outside it, so
the cost scales with the stroke, not the frame. The ~90x margin measured
below is therefore structural -- a property of which design got built -- not
luck, and not slack available to spend on a future feature.

### Plan 2, Task 9: livewire floors at 8K and on a real 15520x7760 still

| quantity | value | notes |
|---|---|---|
| livewire build, 8K synthetic (7680x3840 -> grid 3840x1920 step 2) | 12.4 ms | median of 3; bar <= 300 ms; `builds()` = 1 in every run |
| livewire cursor move, 8K synthetic, 200 moves | p95 2.44 ms / max 4.81 ms | worst of 3, **unfiltered** -- the brief's post-warm-up filter kept 0/200 in every run, see note below |
| livewire build, real still 15520x7760 (grid 3880x1940 step 4) | 17.0 ms | median of 3; bar <= 300 ms; `builds()` = 1 in every run |
| livewire cursor move, real still, 200 moves | p95 0.92 ms / max 1.52 ms | worst of 3, unfiltered; same caveat |

M5 Pro, 18 cores, macOS 26.6.2, load average ~0.7-1.1/18 during measurement
(quiet). Fixture: `bench_8k`'s synthetic 7680x3840 frame, and the real
15520x7760 Osmo still
`ingest/osmo360/test/play_room/photo-monopod/CAM_20260810150859_0066_D.JPG`.
Three repeats of the full test binary; build is the median of the three,
p95/max are the worst (highest) of the three, per Step 3 of the task. Pops
(1,460,818 at 8K; 679,948 on the still) were identical across all 3 runs --
the search is deterministic -- and every one of the 200 `path_to` calls at
both sizes returned a non-empty path (`found 200/200`), which is how the
loop is known to be doing real Dijkstra expansion each move rather than
no-opping: `builds()` reading 1 only proves *that* number, not that the
moves that followed were genuine, and `pops()` and `found` are what supply
that second half.

**The brief's own warm-up filter cannot produce a sample at this speed, and
that is reported as a finding, not patched into a green number.** Its rule
is "skip a move while the *summed measured time of the timed moves so far
in this segment* is <= 200 ms"; with per-move cost in the 0.1-5 ms range,
the cumulative sum across all 200 moves in a segment never reaches 200 ms,
so the filter never opens and `ms` stays empty in every one of the 3 runs at
both sizes -- printing `p95 0.00 ms max 0.00 ms`, which reads as an easy
pass but is zero data, not a fast one. The bench was extended (still in
`mask_doc_test.cpp`, not a separate script) to also record every move
unfiltered as `ms_all`; because that is a strict superset of whatever the
filter would have kept, it can only be equal to or slower than the intended
number, never a friendlier substitute. `lw.build` was also changed from
`median_ms` (3 repeats) to one direct timed call, because 3 repeats left
`builds()` at 3 before a single cursor move, which would have made
criterion 5's "built once" unmeasurable by the same reasoning.

Livewire, criterion 5 (cost image at 8K, bar 300 ms, built once): 12.4 ms
(median of 3) at 3840 x 1920, `builds()` = 1 after 200 cursor moves, in
every run. **PASS.**

Criterion 6 (cursor response, bar p95 <= 16 ms after the first 200 ms of a
segment): the filtered measurement is 0 samples in all 3 runs at both
sizes (see above), so it cannot itself be read as pass or fail. Over all
200 moves unfiltered, worst of 3 runs: p95 2.44 ms, max 4.81 ms over 200
moves on the synthetic 8K frame (pops/move ~7,304); p95 0.92 ms, max
1.52 ms over 200 moves on the 15520 x 7760 still at step 4 (pops/move
~3,400). Both are more than 3x under the bar even at the max, let alone the
p95. **PASS**, by the unfiltered numbers -- the filtered protocol as
specified produced no data at either size on this machine, which is itself
worth recording: the implementation is fast enough that a 200-move segment
does not last as long as the warm-up window meant to exclude a slow first
move. The synthetic frame is a smooth gradient with almost no edges, which
is the search's worst case (the expanded region is a disc around the
anchor rather than a strip along an edge); the still is the realistic one,
and its lower pops/move despite a near-identical grid size (3880x1940 vs
3840x1920) is consistent with real edges giving Dijkstra a steeper cost
gradient to steer by. Memory: `Livewire::bytes()` = 21.1 MB at the 8K grid,
21.6 MB at the still's.

## Task 15: in-app and file-level verification

Everything below was measured against the running `spirula` GUI process (not
`bench_8k`), driven through `tools/guictl.py` under `SS_GUI_AUTOMATION=1
SS_GUI_OFFSCREEN=1` on the M5 Pro, macOS 26.6.2, with `native_dialogs=0` set
temporarily in `~/.config/spirula-studio/gui.conf` to reach the built-in
folder picker (restored to `1` afterward, as Tasks 13/14 did). Fixture:
`/tmp/spirula_mask_bench` (three synthetic 7680x3840 frames from Task 9's
`bench_8k`), restored to pristine after every measurement below (verified:
`masks/f0000.png` regenerated by re-running `mask_doc_test` with
`SS_MASK_BENCH` set after deleting it -- `synth_mask`/`synth_rgb` are pure
functions of `(w, h, seed)`, no RNG seeded from time, so the regenerated file
reproduced the original byte count exactly, 1,567,695 bytes; `masks/f0001.png`
was restored by copying its own recorded `.base.png` back over it; no
`mask_edits/` directory survives in the fixture root). Criterion #4 below was
re-measured in a follow-up pass (the corroborating-quantity fix below);
that pass used the in-app "Revert all" + "Done" before closing, and
`masks/f0000.png`'s MD5 afterward matched the checksum recorded right after
the first restoration exactly, confirming the fixture round-tripped clean a
second time.

### A pre-existing automation-tooling defect, found and fixed: macOS swaps Ctrl and Super

Dear ImGui's `AddKeyAnalogEvent()` swaps `Ctrl<->Super` whenever
`io.ConfigMacOSXBehaviors` is set (the default on an Apple build, verified by
reading `imgui.cpp:1850-1856`): a synthetic `ImGuiKey_LeftCtrl` event is
silently rewritten to `ImGuiKey_LeftSuper` before it reaches the app, so
`io.KeyCtrl` -- the flag `MaskSession::handle_keys` and `paint_for` both read
-- never becomes true, and `io.KeySuper` does instead. ImGui's own mouse code
then converts a Super-held left click into a right click (`imgui.cpp:1958-1967`,
"macOS: Convert Ctrl(Super)+Left click into Right-click"), which is why a
synthetic `Ctrl+drag` silently behaved exactly like the app's own documented
right-click no-op, and why `guictl.py key "Ctrl+Z"` never reached
`MaskSession::handle_keys` at all. This is a defect in `tools/guictl.py` /
`src/app/gui/Automation.cpp` (shared GUI test tooling), not in the mask
editor's own code, and it predates this plan -- nothing in this repository had
exercised a Ctrl-chord through the automation harness on a macOS build before
this task. Fixed by resolving "the physical key that produces logical Ctrl"
at call time (`logical_ctrl_key()`, `Automation.cpp`) instead of hard-coding
`ImGuiKey_LeftCtrl`, used both by `/ui/key`'s chord parser and by a new
`/ui/drag?shift=1&ctrl=1&space=1&esc_mid=1` set of flags added so a modifier
(or Escape, mid-gesture) can be held for the duration of a drag, which the
existing endpoints had no way to express. Verified fixed: `Ctrl+Z` now
reaches `MaskSession::undo()` through the keyboard exactly as the Undo button
does (both call the identical function, `MaskPanel.cpp:112` and `:250`), and
`Ctrl+drag` now force-keeps instead of doing nothing. This is a real,
committed change to test infrastructure outside this task's stated file list;
flagged for review rather than folded silently into the docs commit.

### Criterion #4 in the app (Step 3), with a corroborating quantity

Brush grown to 106 px via nine `]` presses from the 24 px default
(`step_brush`'s 1.18x per step: 24 -> 28 -> ... -> 106.4, rounds to 106; the
brief's example values of 96/113 px do not fall out of this stepping, so 106
is recorded as the nearest step actually reached). Zoomed to ~8x about the
canvas center. 20 horizontal brush strokes, each ~500 mask px long (~761
screen px at this zoom), all plain drags (ForceDrop).

**Why a second quantity is necessary, not just nice to have.**
`MaskSession::commit_stroke` short-circuits on an empty rect
(`MaskSession.cpp:343-359`, `if (r.empty()) return {};`) and `upload_rect`
does the same (`MaskPanel.cpp:65`), so a coordinate-mapping bug that made
every automated drag much shorter than the claimed 500 px at radius 106
would make the "Last stroke" readings *faster*, not slower or absent --
indistinguishable from genuine success by timing alone. Task 9 corroborated
its CPU-side number with a deterministic `history_bytes()` of exactly 60039
across all three runs; this in-app run corroborates with the "Kept %"
readout (`MaskPanel.cpp:267`), read before the series and after every one of
the 20 strokes, the same readout Step 6 already uses to confirm the Ctrl and
Shift+Ctrl gestures actually change the mask.

| stroke | ms | Kept % | stroke | ms | Kept % |
|---|---|---|---|---|---|
| (start) | -- | 50.1% | | | |
| 1 | 11.6 | 49.7% | 11 | 5.0 | 49.4% |
| 2 | 36.5 | 49.6% | 12 | 18.4 | 49.3% |
| 3 | 12.2 | 49.6% | 13 | 21.6 | 49.3% |
| 4 | 20.1 | 49.6% | 14 | 16.1 | 49.3% |
| 5 | 12.6 | 49.5% | 15 | 27.6 | 49.2% |
| 6 | 39.1 | 49.5% | 16 | 8.8 | 49.2% |
| 7 | 45.1 | 49.5% | 17 | 18.4 | 49.2% |
| 8 | 35.5 | 49.4% | 18 | 5.0 | 49.1% |
| 9 | 5.1 | 49.4% | 19 | 23.1 | 49.1% |
| 10 | 5.4 | 49.4% | 20 | 20.7 | 49.1% |

**Median 18.4 ms, max 45.1 ms, min 5.0 ms. PASS** (bar: median <= 100 ms, max
<= 250 ms; margin ~5x on the median, ~5.5x on the max). Two different runs of
this same recipe, on the same machine, gave two different but both-passing
medians (12.7 ms in an earlier run, 18.4 ms here) -- consistent with genuine
per-stroke GL upload cost varying with machine load between runs, not a
fixed or stubbed number.

**The corroboration**: Kept % fell monotonically, 50.1% -> 49.1%, but read
the table as what it actually shows, not as a smooth ramp. Differenced,
13 of the 20 per-stroke transitions are exactly 0.0pp (three in a row at
49.6%, four in a row at 49.4%, three in a row at each of 49.3%, 49.2% and
49.1%); only 7 show a visible 0.1pp move, one of them the first stroke's
0.4pp drop off the pristine baseline. **Individual per-stroke deltas mostly
sit at or below the readout's 0.1pp quantisation floor and carry no signal
on their own.** What does the corroborating work is the **cumulative**
20-stroke delta: 1.0 percentage point is exactly 10 quantisation steps,
well clear of the floor a single stroke's reading sits at. Over
7680x3840 = 29,491,200 mask pixels, 1.0pp is 294,912 px^2 of newly-forced-
drop area. A single stroke's own claimed footprint (capsule: length x
diameter + pi x r^2 = 500x212 + pi x 106^2 = 141,298.94 px^2) is the same
order of magnitude as that cumulative delta's per-stroke average
(294,912 / 20 ~ 14,746 px^2 marginal, small because the 20 strokes overlap
heavily -- they were placed 20 screen px apart, ~13 mask px at this zoom,
against a 212 px brush diameter, by design a dense serpentine sweep, not 20
independent patches). Modelling the swept region as one continuous band
(500 mask px long, ~380 screen px / ~249 mask px of travel across the 20
positions plus the 212 px brush diameter overhang on the two open ends, so
~461 mask px tall) predicts ~230,500 px^2 -- the same order of magnitude as
the observed 294,912 px^2 (~10 steps either way).

**Discriminating power**: a coordinate bug that halved both stroke size and
spacing would shrink that swept-band prediction by ~4x (area scales with
the square of a linear shrink), landing the cumulative delta around 2-3
quantisation steps instead of 10 -- smaller, but still visible against the
floor, not swallowed by it. This does not prove the strokes were exactly
500 px; it does rule out the specific failure this criterion's timing alone
cannot catch -- strokes silently far smaller than claimed producing a
falsely reassuring fast number.

### Criterion #9 in the app (Step 4)

`ps -o rss= -p $(pgrep -n spirula)`, editor closed vs. one 8K frame open plus
one painted stroke, three independent open/paint/close cycles on the same
process (the brief's own script is a single sample each; repeated here
because the first cycle's reading proved volatile -- see below):

| trial | baseline | editor open | delta |
|---|---|---|---|
| 1 | 309.2 MB | 520.7 MB | 211.5 MB |
| 2 | 462.5 MB | 575.8 MB | 113.3 MB |
| 3 | 443.0 MB | 575.3 MB | 132.3 MB |

**Max delta 211.5 MB (median 132.3 MB). PASS** in every trial (bar <= 600 MB).
The max is the number to trust here, not the median: see the noise floor
described next -- with the effect size and the measurement noise this close
together, the worst observed reading is the defensible one, and it still
clears the bar by 2.8x.

Machine state this was measured under: NOT idle. `top`/`vm_stat` during these
trials showed 23 GB of 24 GB physical memory in use, only ~250-280 MB free,
and 4+ GB already in the compressor, with a third-party `splat-trainer`
process (AirVis Studio, unrelated to this repository) alone holding ~5.2 GB
RSS. `ps -o rss=` on a live process is not immune to the kernel's memory
compressor reclaiming pages between two samples: a same-state re-check a few
seconds after trial 1's "editor open" reading, with nothing repainted, showed
RSS fall from 520.7 MB to as low as ~81 MB, and the *baseline* reading itself
drifted 309 -> 462 -> 443 MB across the three trials with no editor activity
between them. All three deltas still clear the 600 MB bar by a wide margin,
but a single sample on this machine, at this moment, would not have been a
reliable number on its own; that is the reason for three trials rather than
one. This is a property of the shared machine's memory pressure, not of the
mask editor.

### Criterion #3 simulated, spec §12.3 steps 3/4/6 (Step 5)

All four assertions matched the brief's expected output exactly, run against
frame `f0000`:

1. **Paint + Save**: `mask_edits/` held `f0000.base.png f0000.drop.png
   f0000.keep.png index.json`; `frames.keys()` was `dict_keys(['f0000'])`;
   `cmp base vs masks/f0000.png` exited 1 (composite differs from base).
   **PASS.**
2. **Undo + Save**: `cmp base vs masks/f0000.png` exited 0 (byte-identical to
   the base again). Performed with the Undo *button*, not the `Ctrl+Z` key
   chord -- at this point in the task the automation-harness defect above was
   not yet found, and the button was the way to isolate whether undo itself
   worked. `MaskPanel.cpp:112` (`ui::Button(msg::undo)`) and `:250`
   (`handle_keys`'s Ctrl+Z path) call the identical `MaskSession::undo()`, and
   the keyboard path was independently exercised later in Step 6 once the fix
   landed, so this substitution does not weaken the result. **PASS.**
3. **Simulated re-run**: after painting again, saving, and Done, then
   rewriting `masks/f0000.png` to a deterministic left-keep/right-drop split
   and reopening the editor on frame 0: the status strip read exactly "A run
   regenerated this mask; the corrections were re-applied over the new one.",
   the picture showed the new base's right half tinted dropped with the
   earlier stroke re-applied on top of it, and `cmp mask_edits/f0000.base.png
   /tmp/f0000_regenerated.png` exited 0. **PASS.**
4. **Revert all + Done**: `mask_edits/` held only `index.json`; `cmp
   masks/f0000.png /tmp/f0000_regenerated.png` exited 0. **PASS.**

Criterion #3 as a whole is fully covered only by combining this task with Task 14, not
by either alone: `recomposite_frame` (`MaskLayer.cpp:317-345`) is the single primitive
behind both paths -- the hand-crafted PNG rewrite above exercises it through "open a
frame whose mask fingerprint no longer matches," and Task 14 already drove the same
function through `recomposite_all` and the `DatasetPrep::run()` call site, end to end,
against a real running SAM 3 model. This task adds no coverage of that call site or its
logging; it only re-confirms the shared primitive from the editor-open path.

### Interaction checklist (Step 6)

Driven through `guictl.py`, screenshots read visually (this session has no
human at a keyboard); ticked only where actually exercised this session, with
what was inferred rather than run flagged as such.

- [x] **Wheel zooms about the cursor; stops at 1 and 64.** Extreme zoom-in
  (`--dy 200`) and extreme zoom-out (`--dy -100`) both clamped rather than
  diverging or inverting: zoomed-in shows heavy box-filter blur at the
  window-texture cap with the brush circle scaled up hugely; zoomed-out
  returns to exactly the original full-frame framing. "The pixel under the
  pointer stays put" is `zoom_about`'s documented contract, read from source
  (`MaskWindow.cpp`) rather than independently measured pixel-for-pixel here.
- [x] **Middle drag pans; Space+left drag pans; neither paints.** Both
  gestures shifted the visible window (confirmed by screenshot diff) with
  "Last stroke" and "Kept %" unchanged after each.
- [x] **Left/Shift+drag tints drop (red-ish); Ctrl+drag tints keep
  (green-ish); Shift+Ctrl+drag clears back to base tint.** All four gestures
  exercised end to end after the automation fix above: Ctrl+drag raised
  "Kept %" and painted a lighter/keep-tinted stroke inside a drop region;
  Shift+Ctrl+drag over that same stroke returned "Kept %" exactly to its
  pre-stroke value and removed the tint. "The modifier read is the one held
  at release" is read from source (`MaskPanel.cpp:215`, sampled on the
  commit frame) rather than reproduced with the modifier changed mid-drag.
- [x] **Right drag paints nothing and moves nothing.** No stroke, no pan.
- [x] **Box, ellipse, lasso, polygon, brush all commit on release.** Box,
  brush and polygon (four corners placed by individual clicks, closed with
  Enter) directly exercised, each producing a correctly-shaped tinted region
  and updating "Last stroke". Ellipse and lasso were **not** independently
  driven this session; the inference rests on two different directly-tested
  tools, not one. Ellipse shares Box's two-corner-drag mechanics exactly
  (`EditTool.cpp:205`, `_id == ToolId::Box || _id == ToolId::Ellipse`), so it
  is inferred from Box. Lasso does **not** share Box's path: it shares
  Brush's freehand point-accumulation (`EditTool.cpp:160`,
  `_id == ToolId::Lasso || _id == ToolId::Brush`), a structurally different
  path with its own step threshold and geometry builder, so it is inferred
  from Brush, not from Box.
- [x] **`[`/`]` change `Brush: N px`; the drawn circle scales with zoom;
  radius is constant in mask pixels across zoom.** Grown 24 -> 28 -> 106 px;
  the preview circle visibly grew between the 1x and ~8x screenshots at the
  same 106 px setting.
- [x] **Esc during a drag cancels it; nothing is painted.** A drag with
  Escape injected halfway through left "Last stroke" and "Kept %" completely
  unchanged.
- [x] **Ctrl+Z / Ctrl+Shift+Z step the strokes; Ctrl+Z while placing a
  polygon removes the last corner.** All three exercised via the keyboard
  after the fix: undo removed a stroke and restored "Kept: 50.0%"; redo
  brought it back; pressing Ctrl+Z with 3 polygon corners placed and then
  moving the cursor showed a 2-corner rubber-band to the new cursor position,
  not a static 3rd corner, confirming the corner was actually removed rather
  than merely occluded by the live preview.
- [x] **`<`, `>` and the slider change frames; a dirty frame reports "Saved"
  after the switch and its layers appear in `mask_edits/`.** `>` exercised:
  switching from a dirty `f0000` to `f0001` autosaved `f0000`'s layers to
  disk and the new frame's status read "Saved". `<` and the slider were
  **not** independently exercised; `go_to()` is the single function behind
  all three (`MaskPanel.cpp`), so this is inferred from the one call tested.
- [x] **Closing the window with Done or its close box while dirty writes the
  files.** Both exercised independently: Done and the title-bar close box
  (unnamed item, clicked by coordinate) each wrote the dirty frame's layer
  files to `mask_edits/` before `mask_editor_open` went false.

### Step 2, `align_fit_test`: a pre-existing failure, unrelated to this plan

`align_fit_test` fails (`FAIL ... and its axes are the room's`) on this
branch. Verified **not** a regression from this plan: `git diff --stat
66342882 HEAD -- src/app/gui/tests/align_fit_test.cpp
src/app/gui/edit/AlignFit.cpp` (the test's entire source list per
`cmake/SsApps.cmake:374-377`) is empty, and the same failure was reproduced
building and running `align_fit_test` from a clean worktree at `66342882`
(the branch's own cut point) with no other changes. The eight `mask_doc_test`
targets in Step 2 all reported 0; only this ninth, unrelated test misses, and
it needs its own investigation outside this plan.

### Criterion #13: re-doing a real hand correction on the playroom capture

The last check before this ships against everything else in this plan being a
synthetic fixture or an invented bench: does the editor reproduce a correction
the operator already made by hand, on their own real 120 MP capture, with a
separate Python tool, before this editor existed?

Source data lives outside this submodule, in the parent slam repo at
`work/osmo_playroom/`: `masks_eq/f00000/` (the SAM 3 output before hand
correction) and `masks_eq_edited/f00000/` (what the operator painted),
15520x7760, mode L, values exactly `{0, 255}`. **Both of those trees are
255 = DROP -- the opposite of spirula's 255 = KEEP** (`src/app/FrameMask.h:53-54`).
Inverted on the way in with `PIL.ImageOps.invert`, confirmed by re-reading the
inverted file's own pixel values afterward, not assumed from the invert call
succeeding.

**Assembled a one-frame spirula dataset** (`/tmp/playroom_mask_check/`:
`images/f00000.jpg` copied from `masks_eq/f00000/frame.jpg`'s symlink target,
`masks/f00000.png` = the inverted SAM mask). Confirmed after inversion: size
`(15520, 7760)`, 2 distinct values `{0, 255}`, SAM drop frac **0.159191**
(brief expected ~0.159) and the operator's recorded reference drop frac
**0.068559**, matching `mask_editor_edits.json`'s `f00000.drop_frac` exactly.
One brief defect found and fixed here: its Step 1 `uv run` line
(`task-16-brief.md:17`) lists `--with pillow` only but the script imports
`numpy` at `:23` -- ran with `--with pillow --with numpy` instead, which is
the only change from the brief's script. This is the ninth defect this plan
has found in its own briefs (see the standing tally at the top of this
task's brief); independently confirmed by review.

**Diffed the SAM mask against the operator's reference before touching the
GUI**, to know what the correction actually was rather than guessing at
strokes: `recovered = ref_keep & ~sam_keep` (operator force-kept what SAM had
dropped) is **9.07%** of the frame and, visualized at 8x downsample, is almost
entirely a **sharp, full-width horizontal band from row 7056 to the bottom
edge (row 7759 of 7760)** -- the blanket nadir-sweep cone this project's
CLAUDE.md already documents recovering on a different capture from this same
pipeline family. `newly_dropped = sam_keep & ~ref_keep` (operator dropped what
SAM had kept) is **0.0095%**, confined to a thin outline around the
already-SAM-dropped person/vacuum silhouette -- edge refinement, not a second
region. Computed the discriminating-power null required by this task's
instructions before running the GUI at all: scoring the **unmodified SAM
mask** against the reference (i.e. "painted nothing") gives IoU **0.4298**,
far under the 0.97 bar -- so a passing score here cannot be a check that
cannot fail.

**Re-did the correction in the real app**, driven through
`SS_GUI_AUTOMATION=1` + `guictl.py` (`build/spirula`, not the `.app` bundle --
the bundle binary hung idle at `_glfwWaitEventsTimeoutCocoa` and never bound
the automation port on this run; `build/spirula` answered immediately, same
binary content otherwise, not investigated further as out of scope). **Fix
round 1**: the reviewer could not reproduce this. They tried both the app
bundle's inner binary directly
(`build/Spirula Studio.app/Contents/MacOS/spirula`, bound the automation port
within 15 s, no hang) and the bundle launch path itself (fails immediately
with `PermissionError`, not a hang). The observation above may still be a
real one-off -- a first-launch Gatekeeper/quarantine check is plausible -- but
it is now unconfirmed and flagged as such rather than as a reproducible
defect, so nobody chases a phantom.
`native_dialogs` set to 0 in `~/.config/spirula-studio/gui.conf` for the
built-in folder browser, restored to 1 after. Opened the dataset, clicked
**Correct masks**, screenshot confirmed the same SAM mask rendered (`Kept:
84.1%` = `1 - 0.159191`, exact). Selected the **Box** tool and, with the
diff analysis above in hand rather than eyeballing the reference image, did a
single **Ctrl+drag** (force-keep) rectangle spanning the full image width and
the bottom band (screen coordinates derived from the canvas's reported extent
in the tree: content x in [68, 1531], y in [92, 823] for a 15520x7760 source
-- dragged generously past those bounds, [40,750] to [1560,830], to guarantee
full coverage rather than clipping at the edge). One drag, committed on
release: **`Kept: 84.1% -> 93.2%`**, `Last stroke: 257.9 ms` -- the one
in-app timing this task measured, for a single full-width box commit on a
120 MP mask (not comparable to Task 9's 702 ms `MaskDoc::save` figure for 8K,
a different operation). **Save**: `mask_edits/f00000.{base,drop,keep}.png` +
`index.json` appeared (`kept: 0.932195842`, matching the displayed 93.2%
exactly), status strip read `Saved`, `Corrected frames: 1`. **Done**: `state`
confirmed `mask_editor_open: false`.

**Compared per the brief's script, unmodified, against the on-disk
`masks/f00000.png`** (post-Done, i.e. the actually-saved composite, not an
in-memory value):

```
drop-region IoU 0.9857  [bar >= 0.97]     -- PASS
our drop frac 0.067804  recorded 0.068559  delta -0.000755  [bar |delta| <= 0.005] -- PASS
```

**Both criteria pass, with margin**: IoU is 1.57 points above the 0.97 floor,
the delta is 6.6x inside the +-0.005 band. Re-verified the saved file is still
strictly binary after the paint (`{0: 8,166,010 px, 255: 112,269,190 px}`,
no intermediate values from box-edge antialiasing) and unchanged in size,
before trusting the numbers above.

**Fix round 1 -- sensitivity table, to show what 0.97 is actually demanding.**
`0.9857 >= 0.97` alone does not tell a reader whether the bar is a formality
or a real test. Recomputed by shifting the same full-width force-keep
boundary off its true row (7056) and rescoring the resulting drop mask
against the reference, independently (not copied from the reviewer's
figures, which were given only as a target to check against):

| offset from the true row 7056 | boundary row | IoU | verdict |
|---|---|---|---|
| 0 (exact) | 7056 | 0.9934 | PASS |
| -100 rows (1.3% of frame height) | 6956 | 0.9826 | PASS |
| +50 rows (0.6%) | 7106 | 0.9125 | fail |
| -200 rows (2.6%) | 6856 | 0.9647 | fail |

All four values matched the reviewer's exactly; no discrepancy to report. A
boundary error of about 50 rows out of 7760 -- well under 1.5% of the frame
height -- already fails. So the 0.9857 PASS above means the box drag
reproduced the reference band *closely*, not merely approximately, and the
caveat two paragraphs below (that this run's stroke was informed by a diff a
blind operator would not have) cuts both ways: it is also what the bar would
have caught had the bounds been wrong.

**What this run does and does not establish.** One frame, one operator
correction, one geometrically simple region (a sharp horizontal band, not an
irregular silhouette) -- a favorable case for a box tool, and the brief's own
instructions are explicit that a miss here would be reported as a miss rather
than retried with different strokes. It was not a miss. The stroke I used was
informed by a pixel-level diff against the reference that a human operator
redoing this blind would not have available -- a real re-do without that
foreknowledge would plausibly score lower (more iterations, imprecise
bounds), and this run does not measure that. What it does establish: the
paint-to-composite-to-disk pipeline (Box tool, Ctrl force-keep, Save, Done)
reproduces a real hand correction on a real 120 MP capture to well inside the
pre-registered tolerance, and the check is not vacuous -- the unmodified-SAM
null scores 0.4298, decisively separated from both the 0.97 bar and the
0.9857 result.

Cleanup: `/tmp/playroom_mask_check` and `/tmp/playroom_reference_keep.png`
removed; `~/.config/spirula-studio/gui.conf`'s `native_dialogs` restored to 1
from the pre-session backup. `work/osmo_playroom/` in the parent repo was
only read, never modified.

## Path shape and livewire

A `path` is one more `MaskShape::Kind`: 3 or more corners normalised to the
frame, closed implicitly, `-path` removing what is inside exactly as `-rect`
does, in the same ordered list. It is filled by the even-odd scanline fill in
`src/core/PolygonFill.h`, which is the fill the 3D editor's lasso already
used (moved down so the CLI's `spirula sam mask --shape` can reach it).

The pen tool (`src/app/gui/mask/PathTool.h`) drops anchors and joins them
with the livewire (`Livewire.h`): Dijkstra over a cost image from gradient
magnitude, gradient direction and Laplacian zero crossings, after Mortensen
and Barrett 1995, with diagonal links weighted by their length. The cost
image is built once per frame shown over the frame decimated to at most 4096
px on its long edge (1.9 px of precision at 8K, 3.8 px on a 15520-wide
still, both under the masker's rim dilation). The search is lazy: each
cursor move pops the heap only until the cursor's pixel is settled.

Keys mirror the polygon tool rather than the spec's first draft: a click
drops an anchor, a click on the first anchor, Enter or a right click closes,
Ctrl+Z takes an anchor back, Esc cancels. Two polygon tools in one panel
with opposite right-click meanings was judged worse than the departure.

## Not in this phase

Propagate, find-missing, slideshow, view modes and the peek key, session
persistence; vertex handles on a path; a Bezier path.
