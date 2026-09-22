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
| livewire build, 8K synthetic (7680x3840 -> grid 3840x1920 step 2) | 12.6 ms | median of 3; bar <= 300 ms; `builds()` = 1 in every run |
| livewire cursor move, 8K synthetic, 200 moves | p95 2.38 ms / max 4.68 ms | worst of 3, **unfiltered**, see the fix-round-1 correction below for why |
| livewire build, real still 15520x7760 (grid 3880x1940 step 4) | 18.3 ms | median of 3; bar <= 300 ms; `builds()` = 1 in every run |
| livewire cursor move, real still, 200 moves | p95 0.88 ms / max 1.54 ms | worst of 3, unfiltered, same caveat |

M5 Pro, 18 cores, macOS 26.6.2, load average ~0.7-1.2/18 across the two
measurement passes (quiet). Fixture: `bench_8k`'s synthetic 7680x3840 frame,
and the real 15520x7760 Osmo still
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

**The brief's own warm-up filter cannot produce a sample at this speed.**
Its rule is "skip a move while the *summed measured time of the timed moves
so far in this segment* is <= 200 ms"; with per-move cost in the
0.003-4.7 ms range, the cumulative sum across all 200 moves never reached
200 ms in any of the 3 runs at either fixture, so `ms` stayed empty every
time and the brief's own printf would read `p95 0.00 ms max 0.00 ms` --
zero data presenting as an easy pass. `lw.build` was also changed from
`median_ms` (3 repeats) to one direct timed call, because 3 repeats left
`builds()` at 3 before a single cursor move, making criterion 5's "built
once" unmeasurable the same way.

**This is fragile, not dead -- quantify the margin rather than call it
settled.** Total elapsed time across the 8K fixture's 200 moves was
146.3-146.6 ms against the 200 ms threshold, **73.2-73.3% of the way**; a
CPU roughly **1.37x slower** (200 / 146.5) opens the filter, and then p95
is computed over however few moves land after that point, with far more
run-to-run variance than a sample of 200. The still fixture has more
headroom -- 60.5-61.2 ms, 30.3-30.6% of the way, ~3.3x of slack -- but
neither is a dead branch on different hardware, under thermal throttling,
under CI contention, or in a debug build. Treat the filtered form of this
bench as something that could start producing real (if noisier) samples
under different conditions, not as permanently inert.

**Fix round 1 correction: the unfiltered set is a valid bound for max, but
not for a percentile, and it is not "never a friendlier substitute" --
reviewer-caught.** The original text here claimed the unfiltered `ms_all`
"can only be equal to or slower than the intended number, never a
friendlier substitute", stated unconditionally. That holds for **max**
(the max of a superset is at least the max of any subset) but not for
**p95**: per-move cost correlates with move index, because each target
sits farther from the anchor and the lazy Dijkstra expands further to
reach it (`index_corr` in the bench, Pearson r between call order and
time): **+0.65 on the still fixture, +0.13 on the 8K synthetic** (weaker
there because its near-uniform gradient lets the search settle some later
targets almost for free -- `pops/move` on the 8K frame ranges 0 to 41,257
per move, min/median/max; on the still, 0 to 15,664). The brief's filter
admits a *suffix* of the segment once its cumulative-time threshold opens,
which is exactly the late, expensive moves -- so a genuinely-opened filter
would read **higher**, not lower, than the reported all-200 p95. `tail_p95`
in the bench estimates that suffix directly, still fixture, worst of 3:

| sample | p95 |
|---|---|
| reported, all 200 (this run) | 0.88 ms |
| last 150 | 1.027 ms |
| last 100 | 1.187 ms |
| last 50 | 1.226 ms |

Every tail estimate is above the reported all-200 figure, in the direction
the correlation predicts. The verdict is unaffected -- even the worst tail
estimate, 1.226 ms, clears the 16 ms bar more than twelve times over -- but
the note's original claim about the direction of the substitution was
wrong, and this is the correction.

Livewire, criterion 5 (cost image at 8K, bar 300 ms, built once): 12.6 ms
(median of 3) at 3840 x 1920, `builds()` = 1 after 200 cursor moves, in
every run. **PASS.**

Criterion 6 (cursor response, bar p95 <= 16 ms after the first 200 ms of a
segment): the filtered measurement is 0 samples in all 3 runs at both
sizes (see above -- fragile, not dead), so it cannot itself be read as
pass or fail. Over all 200 moves unfiltered, worst of 3 runs: p95 2.38 ms,
max 4.68 ms on the synthetic 8K frame; p95 0.88 ms, max 1.54 ms on the
15520 x 7760 still at step 4. The tail-p95 estimates above (which trend
*above* the all-200 figure, not below -- see the fix-round-1 correction)
top out at 1.226 ms on the still and 2.514 ms on the 8K frame (last150),
both still well clear of the bar. **PASS**, by the unfiltered numbers and
corroborated by the tail estimates -- the filtered protocol as specified
produced no data at either size on this machine. The synthetic frame is a
smooth gradient with almost no edges, which is the search's worst case
(the expanded region is a disc around the anchor rather than a strip along
an edge); the still is the realistic one, and its lower pops/move despite
a near-identical grid size (3880x1940 vs 3840x1920) is consistent with
real edges giving Dijkstra a steeper cost gradient to steer by. Memory:
`Livewire::bytes()` = 21.1 MB at the 8K grid, 21.6 MB at the still's.

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

### Task 10 manual check, in the app (2026-09-22)

Driven through `guictl.py` against `/tmp/spirula_mask_bench` (`SS_GUI_AUTOMATION=1
SS_GUI_OFFSCREEN=1`), restored to pristine afterward (`masks/f0000.png` MD5
`4c8f42a2...` before and after; `mask_edits/` removed). The five observations
the brief asked for:

1. **Status strip built the edge map once.** Clicking **Path** showed the hint
   text (`hint_path`) and "Path anchors: 0"; the "Edge map: ..." line was not
   caught in a screenshot (it is replaced by the hint text once the build
   finishes, and the 7680x3840 bench frame builds fast), so N was not read off
   the strip this run -- inferred fast, not measured, unlike Task 9's bench.
2. **A left-click path snapped to the edge, closed, and painted drop (red).**
   Three anchors placed along the boundary the blue channel's diagonal stripes
   make; the committed polyline visibly followed the boundary between anchors
   rather than cutting a straight line across it (screenshot: the polyline
   traces a staircase along the stripe edge, not the anchors' straight-line
   triangle). Closing on the first anchor painted the interior red-tinted and
   moved Kept 50.1% -> 44.9%.
3. **Save/undo round-trip is byte-exact, once the async save is waited out.**
   `cmp mask_edits/f0000.base.png masks/f0000.png` after a committed path +
   Save differed; after Ctrl+Z + Save it matched again -- confirmed by `cmp`
   returning 0 AND by independently decoding both PNGs' IDAT streams
   (byte-identical, 29,495,040 bytes, same per-row filter sequence). One
   caveat found the hard way: `cmp` run immediately after clicking Save can
   read the file mid-write (the save is async) and report the WRONG answer in
   either direction -- this run caught exactly that (an early `cmp` on the
   dirty state reported "identical" when it should not have, because the
   write had not started; the correct result appeared once enough time had
   passed). A test harness that does not wait for the save queue to drain
   will get a coin-flip here, not a false pass specifically.
4. **A right-click path closes on a right click near the first anchor and
   paints keep (green/lighter).** Three anchors and the close all placed with
   the right button; the region tinted lighter, not red, and Kept rose
   (50.1% -> 50.2%) rather than fell -- confirms `_stroke_right`'s button
   grammar (the first button held decides ForceDrop/ForceKeep for the whole
   stroke, Task 10 Step 7) end to end, not just in `PathTool`'s own tests.
   **Superseded by fix round 1 below** -- `_stroke_right` reverted an explicit
   operator decision from plan 1 (Shift/Ctrl, not left/right) and was removed;
   this observation stands as a record of what the pre-fix build did, not of
   current behaviour.
5. **Esc mid-path cancels cleanly; zoom mid-path keeps the polyline on the
   traced feature.** Two anchors placed, Esc: overlay gone, Kept and "Last
   stroke" unchanged, nothing added to the undo stack. Separately, two
   anchors placed, then a 7x wheel-zoom centered mid-canvas: the committed
   polyline was still exactly on the stripe-boundary feature it had traced
   before the zoom (screenshot), confirming `path_space`'s frame-pixel
   storage survives a view change mid-path (Decision 4).

**One thing this check surfaced that the brief did not ask about, RESOLVED by
fix round 1 below**: `hint_buttons` (the always-shown line above `hint_path`)
read "Ctrl+drag: force keep... Right click closes a polygon" while the code
listened for a right button -- wrong for every tool, not just Path. Restoring
`paint_for(shift, ctrl)` made `hint_buttons` accurate again with no edit to
its own text; `hint_path` got one new sentence for the pen tool's own
first-anchor convention. See below.

### Fix round 1: `_stroke_right` reverted an explicit operator decision (2026-09-22)

Plan 1 gave the operator a choice of paint grammar and they chose Shift/Ctrl
(matching the 3D editor's Add/Subtract/Intersect, and keeping one binary to
one drag grammar) over left-drops/right-keeps, implemented as
`MaskSession::paint_for(bool shift, bool ctrl)`. Task 10, following its own
brief literally, introduced `_stroke_right` and read it at both commit sites
in `MaskPanel.cpp`, ahead of the `if (_path_mode)` branch -- so the button
grammar applied to every tool, not just the pen, and silently reverted the
operator's choice. `_stroke_right` did not exist anywhere before Task 10 (see
the original report); the brief's own "Consumes" line was simply wrong about
it being pre-existing, and implementing its code as given carried the
reversion through without anyone having decided it.

**Fix**: removed `_stroke_right` entirely. Both `MaskPanel.cpp` commit sites
(the canvas branch and the Enter handler) route through `paint_for` again for
every tool including the pen's non-path branch. `ViewportInput` construction
reverted to the pre-Task-10 form -- left button for down/clicked/released/
double-clicked, right button only for `right_clicked` -- so the pen's right
button is close-only again, never a second "which button" mode switch, and it
can close a path however that path was started.

**The pen tool still needs SOME per-path state, and it is now confined to the
path tool's own anchor bookkeeping, per the operator's instruction if removal
broke something structural.** A pen has no drag to read a held modifier off
at release (as `paint_for(in.shift, in.ctrl)` does for the other tools), so
`MaskSession` gained one member, `Paint _path_paint = Paint::ForceDrop;`,
captured once via `paint_for(io.KeyShift, io.KeyCtrl)` on the click that
plants the FIRST anchor (`if (!_path.in_progress()) _path_paint = ...`, read
before `_path.update` runs) and used whichever way the path later closes
(first-anchor click, Enter, or right-click). This is the operator's explicit
overruling of plan 2's Global Constraints ("the button that placed the first
anchor picks the mode"): **Ctrl+click on the first anchor selects keep**, as
naturally as a right click, with no drag to make the modifier awkward.

**`hint_path` gained one new sentence** in all 13 languages: "Ctrl+click the
first anchor to keep instead, Shift+Ctrl to clear." (or that language's
equivalent), inserted after the edge-snapping sentence and before the closing
sentence -- everything else in the message is untouched. Per-language note:
German already renders "Ctrl+Z" as "Strg+Z" in this specific message (an
established exception to the file's general "keys stay Latin" rule -- see
`hint_buttons`, which keeps "Ctrl" untranslated), so the new clause mirrors
that local precedent ("Strg+Klick", "Umschalt+Strg") rather than the file-wide
one; every other language's new clause keeps "Ctrl"/"Shift" untranslated,
matching that language's own existing `hint_path` and `hint_buttons` text.

**Re-verified live in the app**, `/tmp/spirula_mask_bench`, Box tool and Path
tool, restored to pristine afterward (`masks/f0000.png` MD5 unchanged):

- Box tool: plain drag painted drop (red, Kept 50.1% -> 46.5%); Ctrl+drag on
  the same region painted keep (Kept back to 50.3%); Shift+Ctrl+drag on it
  cleared the correction (tint gone, Kept 50.2%, matching the untouched
  baseline within antialiasing); a right-button drag over a clean area
  produced a byte-identical screenshot to the frame before it -- no paint, no
  pan, confirmed both by the unchanged "Last stroke"/"Kept" readouts and by
  the screenshot's file size matching exactly.
- Path tool: a plain-click path closed and painted drop (Kept 50.2% -> 43.1%,
  `hint_path`'s new sentence visible and correctly worded in the screenshot);
  a path whose first anchor was Ctrl+clicked (via a zero-length `guictl.py
  drag --ctrl`, since `click` has no modifier flags) painted keep and raised
  Kept to 52.1%; a path whose first anchor was Shift+Ctrl+clicked cleared the
  same region back to 50.2%; a path with every anchor plain-clicked but
  closed by a right click **away from the first anchor** still closed and
  used the drop mode captured at the first anchor, confirming right-click is
  close-only and no longer gated by which button started the path. One
  harness miss along the way: a right-click aimed below the canvas bounds
  (`(900, 900)` against a canvas that ends around y=822) landed outside
  `ImGui::IsItemHovered()` and did nothing -- not a product defect, corrected
  by aiming on-canvas.

Rebuilt clean; `mask_doc_test` and `frame_mask_test` both unchanged (no test
file touched this round) and both 0 failures; `check_i18n.sh` still 3155/3155
translated, 0 stubs; `check_font_coverage.py` and both comment lints clean.

**Two smaller items from the original report, now on record as recurring /
systemic rather than one-off**:
- `--print-only` does not exist as a CLI flag (`sam mask --help` lists
  `--print`) -- this is the SECOND plan in this feature to cite it (Task 11's
  brief here; an earlier one before it), so it is worth treating as a plan
  defect to watch for rather than a typo in one document.
- Task 3's `build/pathcli/data/` fixture had been cleaned from `build/`
  between when Task 3 ran and when Task 11 needed it, and had to be
  regenerated from Task 3's own script. A fixture that lives only under
  `build/` (gitignored, routinely wiped) is a fixture that vanishes; the next
  person to need it will hit the same thing unless it moves somewhere that
  survives a clean build, or its generating script is treated as the durable
  artifact (as this round did).

### Task 11 manual check, in the app and the CLI (2026-09-22)

Fixture regenerated from Task 3's brief (`build/pathcli/fixture.py write build/pathcli/data`,
three 96x64 gradient PNGs -- `build/` had been cleaned since Task 3 ran). Driven
through `guictl.py` against `New dataset -> Add photos -> Remove moving or
unwanted objects -> Try the mask`, `native_dialogs=0` temporarily as the other
sessions in this file do.

1. **The edge map built once, on the panel's worker.** Status read "Edge map:
   96x64, step 1, built in 1 ms" immediately after **Draw a path** -- matches
   the brief's expected string exactly, including the 96x64/step 1 the
   fixture's size implies.
2. **A left-click path snapped to the vertical gradient boundary, closed on the
   first anchor, and landed in the shape list as `Path 1`.** Outline drawn
   white, red stencil tint filling its inside, "78% of the frame is kept"
   (below 100%, as expected). Dragging its body moved the outline and tint
   together (screenshot diff). Toggling **removes the inside** <->
   **keeps the inside** inverted the tint and the kept fraction to
   `100 - 78 = 22%` and back to 78% exactly.
3. **A second `Draw a path`, two anchors, Esc: no `Path 2` appeared, no
   overlay left behind, and the kept fraction was unchanged** (still 78%,
   `Path 1` still the only shape).
4. **A REAL defect found and fixed by this check, not merely observed**: the
   brief's Step 4 code computed `path_consumed` from `_path.update()` but
   never read it. On the frame a path closes by a plain click, `_path_armed`
   is cleared inside that same block, so the line below,
   `canvas_free = hovered && !on_shape && _drag_handle == -1 && !_path_armed`,
   evaluates with the NEW (false) `_path_armed` and the closing click's
   `IsMouseClicked(Left)` is still true for the rest of that ImGui frame -- so
   the SAME click that closed the path also fell through to the SAM
   object-click handler below it. Reproduced directly: closing a path with a
   plain click produced "Clicked objects: 1" and started loading the SAM
   model, unprompted, in the unpatched build. Fixed by adding
   `&& !path_consumed` to `canvas_free` (`SegmentPanel.cpp`, the line above);
   re-ran the identical steps against the patched build and "Objects to click
   on: Object 1 (no clicks yet)" stayed unchanged through a path close.
   Confirmed by direct source reasoning (`PathTool::update` sets
   `consumed = true` on exactly the branches that could otherwise leak a
   click: anchor placement, the near-first close, and the right-click close)
   as well as by the screenshot pair.
5. **CLI spelling.** The brief's `--print-only` does not exist (`sam mask
   --help` lists `--print`); with the correct flag,
   `./build/spirula sam mask build/pathcli/data/frames --shape "-path
   0.2,0.2,0.8,0.2,0.5,0.8" --print` printed back
   `-path 0.2000,0.2000,0.8000,0.2000,0.5000,0.8000`, confirming plan 1's
   `-path` round-trip is intact and this panel's in-memory stencil (Decision
   10) would hand a run the same spelling.

One harness note, not a product defect: reopening **Try the mask** immediately
after closing it once, in the same offscreen session, produced "No frame could
be read from this video" on one attempt and worked cleanly on the next and on
a fresh process -- a rapid-reopen race in the automation harness or the
panel's own open/close teardown timing, not reproduced with a normal pause
between close and reopen. Not investigated further; noted so a future run
does not mistake it for the pen tool being broken.

### Fix round 2 (2026-09-22)

Review returned spec FAIL / quality PASS. Four items.

**1. `MaskPanel.cpp`'s header comment was false.** It claimed to be "the only
file in `app/gui/mask/` that includes imgui or calls GL", written before
`PathOverlay.cpp` existed and never updated once it did. Verified directly
rather than trusted: `grep`-ing every `.h`/`.cpp` in the directory for
`imgui` and for GL (`GlLoader.h`, the GL 1.1 calls, and `glx::`, the
dynamically-loaded post-1.1 subset this tree namespaces to avoid colliding
with the system header -- `GlLoader.h:1-6`) shows `imgui.h` in exactly
`MaskPanel.cpp` and `PathOverlay.cpp`; GL calls in exactly `MaskPanel.cpp`
(`MaskSession.h` includes `GlLoader.h` only for the `GLuint` member type,
calls nothing); every other file, neither. `glx::` itself has zero hits
anywhere in the directory -- the GL this file calls is all GL 1.1
(`glGenTextures`, `glTexImage2D`, ...), which comes from the system library,
never `glx::`. Rewrote the header to say what is actually true: ImGui
permitted in `MaskPanel.cpp` and `PathOverlay.cpp` (named as the deliberate
exception), GL only in `MaskPanel.cpp`, neither anywhere else.
`PathOverlay.h`'s own comment ("The ImGui half of the pen tool") was already
consistent and needed no change.

**2. The report's test-count arithmetic did not reconcile, and was wrong.**
"656 -> 662 (+6)" was an unfounded guess -- there is no operation in this
plan that produces 6 of anything relevant. Re-derived both real numbers by
execution rather than by re-reading the diff:

- **Source `check(` call sites**: `grep -c "check("` on the current file vs.
  `git show fe47dc8b:....cpp` (the commit immediately before Task 10) gives
  436 vs. 433, **+3** -- the two `check()` lines inside `test_to_displayed_float`'s
  nested loop plus the one hand-derived orientation-6 check, matching the
  review's own re-derivation exactly.
- **Runtime executed checks** (`./build/mask_doc_test | grep -c "^ok"`):
  built and ran the `fe47dc8b` commit itself in a throwaway `git worktree`
  (not inferred) to get the true baseline, **656**, matching the figure this
  plan's task prompt started from; the current binary prints **737**, so the
  delta is **+81** = 8 orientations x 5 points x 2 checks (160... no: 8 x 5 x
  2 = 80) + 1 hand check = 81, which is exactly `737 - 656`.

**656 -> 737 (+81) is the number to report as of Task 10 alone** for "how many
checks ran"; **433 -> 436 (+3) is the number for "how many lines of `check()`
code were added"**. They measure different things and neither is "662" --
that figure is retracted. Item 3 below adds a further `test_path_tool_mode_latch`
(13 checks) in this same round, bringing the running total to 750; see
item 3's own numbers rather than re-deriving from this paragraph.

**3. The paint-mode bug fix round 1 shipped had no regression test, and
review named the seam.** The bug lived entirely in `MaskPanel.cpp` (ImGui,
therefore outside `mask_doc_test`'s reach by the same convention item 1 is
about), so the only net under it was the hand-driven live pass in fix round
1 -- not CI, not repeatable without someone remembering to redo it by hand.
Factored the latch into `PathTool` itself, which `mask_doc_test` already
links with zero ImGui: `note_modifiers(bool shift, bool ctrl)` mirrors while
idle (`!in_progress()`) and freezes on the first anchor; `mode_shift()` /
`mode_ctrl()` read the frozen (or live-idle) state. `MaskSession` lost its
`_path_paint` member entirely -- both `MaskPanel.cpp` commit sites now call
`paint_for(_path.mode_shift(), _path.mode_ctrl())` at read time, so there is
nothing left to go stale. `MaskPanel.cpp` is the "thin adapter" the ruling
asked for: one line, `_path.note_modifiers(io.KeyShift, io.KeyCtrl);`, called
once per frame before `_path.update(...)`.

New test `test_path_tool_mode_latch` (13 checks) pins the exact regression:
idle mirrors live modifiers; the first anchor's modifiers freeze the mode;
changing modifiers mid-path, and again on the very click that closes the
path, does not move it; a fresh idle period after close (or after cancel)
resumes mirroring. **Mutation-tested before trusting it**: temporarily
removed the `if (in_progress()) return;` guard (i.e. always re-capture,
reproducing the exact bug review caught), rebuilt, and 5 of the 13 checks
failed **by name** -- all and only the "frozen" ones (`frozen while in
progress...`, `still frozen with no modifiers held`, `still frozen with both
modifiers held...`, `closed mode is still the first anchor's...`, `frozen
through cancel's setup`); the "idle mirrors" and "closes" checks correctly
kept passing, since the mutant does not touch idle behaviour or closing
mechanics. Reverted the mutation (`diff` against the pre-mutation copy: `ok`)
and confirmed 0 failures again before moving on.

Re-verified live in the app after the refactor (one gesture, not the full
fix-round-1 sweep, since the logic moved without changing and is now
unit-tested): Ctrl+click-first-anchor on `/tmp/spirula_mask_bench` still
painted keep and raised Kept to 52.1% -- the identical figure fix round 1
measured for the identical gesture before the refactor. Restored pristine
afterward.

**4. `_path_paint` staleness -- dissolved by item 3, not patched.** There is
no longer a `_path_paint` member for `_path.cancel()` sites to forget to
reset; the frozen state lives inside `PathTool` and is governed solely by
`in_progress()`, the same invariant `cancel()` already maintains for
everything else it owns. Nothing to do here beyond what item 3 already did.

**`hint_buttons`'s "Right click closes a polygon" was left as a stale
generalisation, on purpose.** It is incomplete (two tools share the
mechanism now) but not wrong, and `hint_path` already documents the pen's
own right-click-close and its first-anchor convention. Flagged as
optional by the ruling; a 13-language edit for a completeness-only gain
was judged not worth the translation risk this round.

Rebuilt clean; `mask_doc_test` 750/0 (737 + 13 for the new latch test),
`frame_mask_test` 54/0, `check_i18n.sh` 3155/3155 with 0 stubs, both comment
lints and font coverage clean.

### Task 12: verified (2026-09-22)

Everything below was run on this checkout on the M5 Pro (18 cores, macOS
26.6.2), `build_develop.bash -DSS_BACKEND=vulkan -DSS_ENABLE_PATENTED=ON` ->
`Build complete: build/spirula`. The in-app half was driven through
`tools/guictl.py` against an `SS_GUI_AUTOMATION=1 SS_GUI_OFFSCREEN=1` process,
with `native_dialogs=0` set temporarily in `~/.config/spirula-studio/gui.conf`
and restored to `1` afterward, as Tasks 10/11 and 13/14/15 did.

**Lints.** `check_i18n.sh`: "all 3155 messages are translated into every
language", 0 stubs. That is **3145 at plan 1's last commit (78809508) + 10**,
measured by running the script in a throwaway `git worktree` at that commit
rather than by counting the diff. `check_font_coverage.py`: 10994 characters
across 5 fonts, none missing. `check_comment_length.py`, `check_private_paths.sh`,
`check_ss_prefix.sh`, `check_comments.sh`, `check_file_macro.sh`: all 0.
**The fonts were NOT regenerated by either plan** -- `assets/` has no change in
`66342882..HEAD`, and the coverage check passes without one, so Task 5's and
Task 10's new messages fit the existing subsets.

**Test counts, to be compared by a future run.** `frame_mask_test` **54** `ok`
lines, `mask_doc_test` **749**. The 749 is 750 minus the one assertion removed
below; nothing else changed. Both binaries exit 0.

**`align_fit_test` is a KNOWN PRE-EXISTING FAILURE. Do not re-diagnose it.**
Nine of the ten test binaries in the battery exit 0; `align_fit_test` exits 1
on `FAIL ... and its axes are the room's`, **deterministically, 10 of 10 runs**.
It is not ours and no plan-2 change can reach it, established three independent
ways: the target links only `align_fit_test.cpp` and `AlignFit.cpp`
(`cmake/SsApps.cmake:382`); `git diff 66342882 HEAD` over both files is empty;
and neither file appears in the 33-file change set of plans 1 and 2. `AlignFit`
was last touched by `f60271a8`, which predates plan 1's base. Plan 1 recorded
the same failure above, independently. **A brief that asks for ten zeros from
this battery is asking for something no commit in either plan can deliver.**

**The readers are untouched (spec #2).** `git diff --stat 66342882 -- src/data
src/sfm src/kernels` is empty.

#### Criterion 5 in the app: the edge map at 8K, bar 300 ms, built once

`/tmp/spirula_mask_bench` (three synthetic 7680x3840 frames), **I**, status
line read off a screenshot:

| reading | frame reached how | grid / step | build | stale-proof? |
|---|---|---|---|---|
| 1 | editor opened on f0000, **I** pressed | 3840x1920, step 2 | 96 ms | no |
| 2 | `>` then `<`, back to f0000 | 3840x1920, step 2 | 96 ms | no |
| 3 | `>` then `<`, back to f0000 | 3840x1920, step 2 | 96 ms | no |
| **4** | `>` to **f0001**, pen tool already on | 3840x1920, step 2 | **108 ms** | **yes** |
| **5** | `>` to **f0002** with the BRUSH tool, then **I** | 3840x1920, step 2 | **88 ms** | **yes** |
| 6-8, after the strip fix below | editor reopened; then `>`/`<` x3 | 3840x1920, step 2 | 19, 83, 83, 87 ms | mixed |
| 9 | 120 MP still, see below | 3880x1940, step 4 | 128-129 ms | yes |

**PASS**, 2.8x inside the bar at the worst reading. The grid and step are what
the design predicts for a 7680-wide frame (4096 px cap on the long edge ->
step 2).

**Readings 4 and 5 are the ones to quote. 1-3 cannot be told from a stale
line and are kept only to show the trap.** `post_status` is sticky, so had the
map *not* been rebuilt, the previous build's text would still be on screen and
would read 96 again -- three identical readings is exactly what a stale status
line looks like. Two independent checks settle it. (a) Source: `pump()` clears
`_status` and calls `_livewire.reset()` on every frame load
(`MaskSession.cpp:200,240`), so the line can only come from a fresh
`ensure_livewire()`. (b) Observed: with the **Brush** tool selected, changing to
f0002 leaves **no `Edge map` line at all** on the strip, and pressing **I**
makes it appear reading 88 ms -- an appearance, not a persistence, so nothing
stale could have produced it. Readings 1-3 are `>` then `<` back to the *same*
frame, so identical input doing identical work is the expected answer and the
repeat carries no information either way.

**The 19-108 ms spread is real and its cause was not isolated.** The low
readings (18, 19 ms) were taken with the app otherwise idle and are close to
`bench_livewire`'s 12.6 ms median, which is the build with nothing else
running. The 83-108 ms readings were all taken within a few seconds of a frame
navigation, when the worker is still decoding and the panel is re-deriving and
uploading an 8K window texture. That is a plausible account, not a measurement:
nobody instrumented the contention. Every reading clears the bar regardless.

**Built once.** With a two-anchor path open on f0002, 47 cursor moves scattered
over the frame across 10 s left the status line unchanged at
`Edge map: 3840x1920, step 2, built in 88 ms`, anchors still 2. Read that as
corroboration, not proof: a rebuild that happened to take 88 ms again would be
invisible here. The deterministic proof is `lw.builds() == 1` in
`mask_doc_test` ("the tool never rebuilt the cost image") and `builds 1` after
200 moves in `bench_livewire`.

#### Criterion 6 in the app: cursor response, bar p95 <= 16 ms

Same fixture, zoomed to 4.0x (`1.2^7.604`), one anchor on a sawtooth boundary
at display (710, 458), the boundary walked in 20 steps of ~9.9 px for 198 px of
screen, then the flat interior. **Every number below is the wall-clock
round-trip of a `/ui/move` call**, which blocks until two frames have been
drawn -- so it is an upper bound on the whole frame, not a measurement of the
livewire, and its floor is the harness. The floor was measured: hovering the
same point six times reads **52.3-56.4 ms**.

| gesture | times |
|---|---|
| along the boundary, 20 moves | 96.0 then 53.2-57.2 ms (median 55.3) |
| across the interior, 6 fresh targets | 135.8, 164.9, **260.0**, 55.5, 121.0, 55.2 |
| the same 6 targets again | 53.0-56.1 ms |
| 4 fresh far corners | **816.7**, 56.5, 55.5, 55.1 ms |
| the same 4 corners again | 53.0-55.1 ms |

**Along the boundary: no lag.** Every move after the first sits inside the
harness floor's own spread, i.e. the livewire contributes nothing measurable at
this resolution. Screenshot: the live segment runs from the anchor to the
cursor and rides the stripe boundary, its free end exactly at the cursor.

**Across the interior: it lags on the first far move and then does not**, which
is the behaviour the task predicted. The differential is what isolates it --
the same six targets, hovered a second time, are all at the floor, and the
overlay draw is identical in both passes, so the excess is the search settling
a region it had not reached. The single worst move measured was **816.7 ms**
(~760 ms above floor) on the first jump to a far corner of an 8K frame; the
next three corners were then free, because that one expansion settled the rest
of the grid.

**Verdict: PASS on criterion 6, which excludes exactly this warm-up** ("p95
after the first 200 ms of a segment"), corroborated by the bench's **p95
2.38 ms / max 4.68 ms** on the same 8K synthetic fixture. The criterion is
honest about what it leaves out. What it leaves out is the next section, and
anyone about to read criterion 6 as a guarantee has to read that first.

#### KNOWN COST: the first far cursor move after an anchor stalls ~0.76 s at 8K

**This is the most useful thing the plan-2 verification found, and no test or
bench in this tree covers it.** Criterion 6 passes and the pen tool is still
shipping a stall a user feels on every anchor they plant.

**The measurement.** 8K synthetic frame, one anchor planted, then the cursor
jumped to a far corner it had never been near: **816.7 ms**, against a measured
harness floor of 52.3-56.4 ms, so roughly **760 ms of search**. The next three
far corners came back at the floor, and a second visit to all four was at the
floor. A gentler version of the same thing shows in the interior sweep above:
first pass 135.8 / 164.9 / **260.0** / 55.5 / 121.0 / 55.2 ms, the identical six
targets on a second pass **all** 53.0-56.1 ms.

**The mechanism.** The search is lazy by design: each cursor move pops the heap
only until the cursor's own pixel is settled, which is what makes the common
case free. The cost of settling a pixel is therefore paid by whichever move
first asks for it. Move the cursor a little and you extend the settled region a
little; jump it across the frame and that one move expands the frontier out to
a disc reaching the new cursor, settling everything nearer on the way. It is
paid once per anchor, because `set_anchor` is what resets the search, and after
the first big jump most of the grid is already settled. The share of the 760 ms
that is Dijkstra expansion versus first-touch paging of the distance and parent
arrays was **not** separated; both are first-use costs and both vanish on the
second visit, so the split does not change the user's experience.

**Why the bench cannot see it, structurally.** `bench_livewire_on` walks the
cursor **2 grid px per move** for 200 moves from an anchor at the grid's own
centre. Every move therefore extends an already-adjacent frontier by a sliver,
and **no single move in the bench ever settles a disc**. Its p95 and max
describe steady-state tracking along a path, which is the right thing for
criterion 6 and the wrong thing for this. The gap between its 4.68 ms max and
the app's 816.7 ms is not a discrepancy between two measurements of one
quantity -- they are measurements of two different gestures, and only the app's
matches "plant an anchor, then click somewhere else".

**A suggestion, UNTESTED, for whoever picks this up.** The anchor click is
already a natural place to spend time: the user has just committed a point and
is not yet moving. Expanding the search eagerly from `set_anchor` -- on the
panel's worker, for a budgeted number of pops, with the cursor's last known
position as the direction hint -- would move this cost off the first move and
into a moment nothing is waiting on. Nothing about that has been implemented or
measured, the budget is unknown, and the interaction with `Livewire`'s
single-threaded state would have to be worked out before any of it is real.
Recorded as a lead, not a plan.

#### The 120 MP still (the operator's capture; not the design point)

One-frame dataset at `/tmp/spirula_osmo_one`: `CAM_20260810150859_0066_D.JPG`
and `work/osmo_playroom/masks_eq/f00000/equirect_mask.png` inverted to the
app's polarity (spec §12.4). Both are **15520 x 7760**, confirmed by reading
them, and `frame.jpg` in that mask directory is a symlink to that exact still
(`ls -la`). The inversion is right end to end, checked two ways that cannot
both be wrong the same way: the source mask is **15.9191%** white (255 = drop
in the slam repo), the written mask is **84.0809%** white, an exact complement;
and the editor then reported **Kept: 84.1%** on open.

| quantity | value |
|---|---|
| edge map | `Edge map: 3880x1940, step 4, built in 129 ms` |
| Last stroke (5-anchor path, closed with Enter) | **68.6 ms** |
| RSS, home screen | 97,168 KB (94.9 MB) |
| RSS, dataset open, editor not yet open | 100,672 KB (98.3 MB) |
| RSS, frame open in the editor | 1,366,272 KB (1.30 GB) |
| RSS, after the edge map was built | 1,467,888 KB (1.40 GB) |
| RSS, after the path closed and painted | 1,235,808 KB (1.18 GB) |
| RSS, after Save | 1,706,528 KB (1.63 GB) |

`ps -o rss= -p "$(pgrep -n spirula)"`. No bar applies (spec §8.1, "it works or
it does not"); **it worked.** The grid and step are exactly what the task
predicted. The +102 MB the edge map cost against `Livewire::bytes()` = 21.6 MB
at this grid is allocator and first-touch, not the structure.

**Saved, and the save was checked rather than assumed.** `mask_edits/` holds
`f0066.base.png`, `f0066.drop.png`, `f0066.keep.png` and `index.json`; the
status strip went "Unsaved changes" -> "Saved", "Corrected frames: 0" -> "1".
Decoding the base and the rewritten `masks/f0066.png` and differencing them:
**86,364 pixels changed, 0.0717% of the frame**, inside the bounding box
x 12368-12673, y 5593-6091 -- the monopod, at full 15520 x 7760 resolution, not
at the working resolution the livewire runs on. Kept 0.840809 -> 0.840092,
which is `index.json`'s `"kept": 0.840092123` to six places.

The fixture is left in place at `/tmp/spirula_osmo_one` rather than deleted; it
is a copy, and nothing in the repo or in `work/` was written.

#### The status strip used to overflow the window, hiding the status AND errors

**Found by this check and fixed.** `draw_canvas` reserved a fixed `px(118.0f)`
for the strip and `draw_status` drew below the canvas, so the strip's top edge
was pinned 118 px above the window bottom **whatever the window size** -- a
taller window did not help, because the reserve is subtracted from the
available height rather than positioned in it. Anything past 118 px fell off
the bottom. `draw_status` emits the status line last, and the **error** line in
the same slot, so the two things that most need to be read were the first to
go. An error nobody can see is worse than no error, because the code believes
it reported.

**What the strip actually needs, measured** in the running app by
instrumenting the height and reading it back, ui scale 1.0, line pitch 22 px:

| window width | tool | status line | measured |
|---|---|---|---|
| 1600 (the size the app opens at) | shape | empty | **110 px** |
| 1600 | shape | present | **132 px** |
| 1600 | pen (livewire ready) | present | **176 px** |
| 900 (the window narrowed by hand) | shape | present | 132 px |
| 900 | pen, `hint_path` now wrapping to 2 lines | present | **192 px** |

So the old 118 was short by 14 px in the commonest case a user ever sees -- a
shape tool with any status text at all -- and by 58 to 74 px with the pen tool.
Five status lines become six when a status or error exists, eight with the pen
tool (`hint_path`, `path_anchors`, and `path_straight` when the livewire is
absent), nine with both, and more again whenever a hint or a long error path
wraps.

**The fix measures the strip instead of predicting it.** `draw()` records
`ImGui::GetCursorPosY()` either side of `draw_status()` into `_status_h`, and
`draw_canvas()` reserves that. **A constant is the wrong shape for this
quantity and was the cause of the bug**: the height depends on the tool, on
whether a status or error is live, on the window width through text wrapping,
on the translation (the same message is longer in German), and on the ui scale
-- none of which a number in the source can know. Computing it ahead of the
draw was rejected as the other way round: it would mean re-deriving ImGui's
wrapping for each message and keeping that copy in step with `draw_status`,
which is the same class of duplication that produced the original defect.
There is no feedback risk in using last frame's value, because the strip's
content does not depend on the canvas height -- the wrap width is the window's.
The cost is one frame of staleness on a tool switch, invisible at 60 Hz.

Frame one has no measurement, so it seeds from
`8.0f * ImGui::GetTextLineHeightWithSpacing()` -- the pen tool's eight
unwrapped lines, which reproduces the measured 176 px exactly at scale 1.0 and
tracks the font and the ui scale instead of pinning a pixel count.

**Verified in the running app, without scrolling anything.** Pen tool on
`/tmp/spirula_mask_bench`: `Edge map: 3840x1920, step 2, built in 19 ms` fully
visible at the window's default size, no scrollbar. Window dragged to 900 px
wide so `hint_path` wraps: all nine lines visible, strip 192 px. Error path
provoked by `chmod 555` on the mask directory and a save:
`Could not write /tmp/spirula_mask_bench/masks/f0000.png.` visible in red, with
the pen tool (9 lines) **and** with a shape tool (7 lines). Both would have
been below the window edge before.

**Re-taken readings after the fix, none of them scrolled**, to check the change
did not move any number this section reports: edge map at 8K **83, 83, 87 ms**
across three `>`/`<` cycles (before: 96, 96, 96, 108, 88); on the 120 MP still
**128 ms** (before: 129); RSS on the still 99,488 KB before the editor /
1,365,056 KB with the frame open / 1,424,208 KB after the edge map (before:
100,672 / 1,366,272 / 1,467,888). Nothing moved beyond the spread already
present between readings of the same quantity.

**For plan 3: `px(118.0f)` no longer exists**, so there is no constant left to
raise. Plan 3's Task 9 proposed 154, which is below what the pen tool needed
even unwrapped; the measured strip makes that adjustment unnecessary rather
than wrong.

#### Three traps this feature has already paid for

**A fixture that lives only in `build/` is a fixture that vanishes.** Task 11's
check had to regenerate Task 3's dataset from its brief because `build/` had
been cleaned in between. A fixture a check depends on belongs somewhere a build
does not delete, or the check must build it.

**A sub-millisecond wall-clock comparison is a flake generator, never an
assertion.** `test_path_tool_livewire` asserted that a 50-pixel livewire hover
took longer than a 1-pixel hover. Both readings are `steady_clock` values in
the microseconds: the far hop runs **0.0045-0.0075 ms** and the near hop
**0.00075-0.00096 ms** over 12 observed runs, nominally 7x apart -- but the
near hop's whole duration is smaller than one scheduler preemption, so a single
stall on that one reading inverts the comparison. A reviewer's 15-run sweep on
an unchanged binary failed once, run 9, with `0.005667 vs 0.006375 ms`: the far
hop was normal and the *near* one had been stretched eightfold. The
deterministic companion assertion on the same lines, `lw.pops() > pops_tiny`,
**passed in that same failing run** -- the far hop really had done more work,
and only the timing proxy lied.

The timing comparison is now printed, not asserted:
`note  far/near segment time 0.005958 vs 0.000750 ms`. `pops()` already proves
the claim and is already asserted, so nothing was lost. The `ms >= 0.0 &&
ms < 1000.0` range guard on the line above is kept: it is a bound with five
orders of magnitude of headroom, not a comparison of two sub-millisecond
readings. **25 consecutive runs of `mask_doc_test` after the change: 25 passes,
749 `ok` lines every time.** No other assertion in the file compares wall-clock
times; `bench_8k` and `bench_livewire` print theirs and fail on nothing.

**`post_status` is sticky, so a stale status line is indistinguishable from a
fresh one.** Nothing clears `_status` on a timer; it holds whatever was last
posted until something overwrites it, and `pump()`'s clear on a frame load is
the only automatic reset. Reading a number off the strip therefore measures
"the last time this was posted", not "now", and a check that reads the same
value three times has learned nothing -- it is satisfied equally by a working
rebuild and by no rebuild at all. Two ways out, both used above: reach the line
through a path that provably reposts (change frame, which clears it first), or
establish its **absence** and then its appearance (select a shape tool, change
frame, confirm no `Edge map` line, press **I**, watch it arrive). An appearance
cannot be stale. This is the same defect shape as a check that reads a
condition something other than the thing being checked could satisfy, and it
is worth assuming of every other sticky field in this panel.

## Not in this phase

Propagate, find-missing, slideshow, view modes and the peek key, session
persistence; vertex handles on a path; a Bezier path.
