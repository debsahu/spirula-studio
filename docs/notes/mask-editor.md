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

Every `file:line` below is checked on every build by
`tools/check_note_cites.sh`, which re-reads the cited lines of the working
tree and greps them for a token the claim needs. Each citation is also quoted
alongside its symbol or statement, so re-find it by that when the check
fails: a citation that lands somewhere unrelated means the file moved, not
that the claim did. Fix the number here and the row in the script together.

## What this is to take on

For whoever has to decide whether to carry this. Each claim below is a command
you can run, so none of it has to be taken on trust.

**Licence.** Original work in this repository, under the project's own GPLv3
(`LICENSE`). No file in the change set carries another project's copyright
header and nothing was vendored in.

**No new third-party dependency.** `cmake/SsApps.cmake` is the only build file
the branch touches at all, and its whole diff is 25 insertions and 1 deletion
-- the deletion being the source-glob line it rewrites to add
`${SS_SRC}/app/gui/mask/*.cpp`, so the new directory compiles into the GUI --
plus two new test targets, `frame_mask_test` and `mask_doc_test`. No
`find_package`, no `FetchContent`, no new library, no new link line.
`git diff --name-only 66342882 HEAD` is 33 files, 20 added and 13 modified,
and no manifest, lockfile or vendored tree is among them.

**The readers are untouched.** `git diff --stat 66342882 HEAD -- src/data
src/sfm src/kernels` is empty: nothing that parses a dataset, a reconstruction
or a kernel changed.

**The livewire is written from the paper, not ported.** Mortensen and Barrett,
"Intelligent Scissors for Image Composition", SIGGRAPH 1995, is the source of
the three local cost terms (gradient magnitude, gradient direction, Laplacian
zero crossing) and of the Dijkstra formulation, and is cited in the header
(`Livewire.h:3`). Two halves to that claim, and they are not equally strong.
Checkable: `src/app/gui/mask/Livewire.{h,cpp}` carries no third-party
copyright, SPDX tag or permission notice -- nor does any other file in the
33 -- and nothing was vendored. Not checkable from the tree, and recorded here
as the author's statement rather than as a finding: no existing implementation
was consulted or copied. The lazy expansion, the 8-connected link tables, the
decimation policy and the `_parent` bit-packing (`Livewire.h:84`) are this
tree's own design decisions either way.

**One existing file moved; no code was rewritten with it.** The even-odd
scanline fill left `app/gui/edit/SelectShape.cpp` for `src/core/PolygonFill.h`
so that `app/FrameMask.cpp`, which the CLI compiles, can reach it. Same
routine, one caller more.

**Blast radius outside the new directory.** The feature is `src/app/gui/mask/`
plus its catalog `src/i18n/catalog/MaskEdit.h`. Everything else it modifies is
named here and nowhere else: `FrameMask`, `DatasetPrep`, `GuiApp`,
`SegmentPanel`, `edit/SelectShape.cpp` and `i18n/catalog/Dataset.h` for the
integration, and `tools/guictl.py` with `app/gui/Automation.cpp` for the
shared GUI test harness -- the last two are a fix to pre-existing tooling,
explained under "A pre-existing automation-tooling defect" below and flagged
there because they sit outside the feature.

**Upstreaming.** Nothing here has been offered upstream and nothing has been
pushed anywhere. This is a branch in this repository.

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
| `<key>.base.png` | a BYTE COPY of `masks/<key>.png` as the run wrote it, made on the first save and replaced only when a re-mask writes a DIFFERENT PICTURE (below) |
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
alone. The fingerprint is over file bytes, so a re-encode that preserves
every pixel -- oxipng, another libpng, a metadata strip -- also differs; the
pass therefore compares the decoded picture against the composite of the
recorded base before it replaces `.base.png`, which is the only copy of what
the run wrote. A missing mask (a cancelled re-run) leaves the layers in place. A
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
then converts a Super-held left click into a right click (`imgui.cpp:1957-1967`,
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
does (both call the identical function, `MaskPanel.cpp:177` and `:408`), and
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
(`MaskSession.cpp:392-403`, `if (r.empty()) return {};`) and `upload_rect`
does the same (`MaskPanel.cpp:76`), so a coordinate-mapping bug that made
every automated drag much shorter than the claimed 500 px at radius 106
would make the "Last stroke" readings *faster*, not slower or absent --
indistinguishable from genuine success by timing alone. Task 9 corroborated
its CPU-side number with a deterministic `history_bytes()` of exactly 60039
across all three runs; this in-app run corroborates with the "Kept %"
readout (`MaskPanel.cpp:455`), read before the series and after every one of
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
   worked. `MaskPanel.cpp:177` (`ui::Button(msg::undo)`) and `:408`
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
by either alone: `recomposite_frame` (`MaskLayer.cpp:341-375`) is the single primitive
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
  at release" is read from source (`MaskPanel.cpp:319-320` samples
  `io.KeyShift` / `io.KeyCtrl` into `ViewportInput` on the frame being drawn,
  and `:350` hands those to `paint_now` on the frame the stroke commits)
  rather than reproduced with the modifier changed mid-drag.
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
  all three (`MaskPanel.cpp:207`, `:211`, `:213`), so this is inferred from
  the one call tested.
- [x] **Closing the window with Done or its close box while dirty writes the
  files.** Both exercised independently: Done and the title-bar close box
  (unnamed item, clicked by coordinate) each wrote the dirty frame's layer
  files to `mask_edits/` before `mask_editor_open` went false.

### Step 2, `align_fit_test`: a pre-existing failure, unrelated to this plan

`align_fit_test` fails (`FAIL ... and its axes are the room's`) on this
branch. Verified **not** a regression from this plan: `git diff --stat
66342882 HEAD -- src/app/gui/tests/align_fit_test.cpp
src/app/gui/edit/AlignFit.cpp` (the test's entire source list per
`cmake/SsApps.cmake:382-384`) is empty, and the same failure was reproduced
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
255 = DROP -- the opposite of spirula's 255 = KEEP** (`src/app/FrameMask.h:52-53`).
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
px on its long edge. **The decimation step is an INTEGER** -- `Livewire.cpp:76`
is `ceil(long edge / 4096)` -- so the precision is exactly **2 frame px at 8K
and exactly 4 on a 15520-wide still**, which is why every grid this note
reports is 3840x1920 or 3880x1940 and never anything 4096 wide. The search is
lazy: each cursor move pops the heap only until the cursor's pixel is settled.

**Correction: the precision was stated here as 1.9 and 3.8 px, and those
numbers are not the code's.** They are the unrounded ratios 7680/4096 = 1.875
and 15520/4096 = 3.789; the integer step never reaches either, and the grid
sizes the rest of this note reports were the standing evidence against them.
The conclusion the original drew survives and is worth restating with the real
figures. At the masker's default `dilate_ratio` 0.05, `sam::dilate_radius_px`
(`src/core/MaskMargin.h:19-28`, which `sam::dilate_radius_px` calls) moves a detection's rim by 2 px once the
box's mean side reaches 80 px and by 4 px at 160 px, so a 2 or 4 px step sits
inside the slack the mask boundary already carries for anything the size of a
person, a vehicle or a monopod. It does **not** sit inside it for a detection
a few tens of pixels across -- the original's unqualified "both under the
masker's rim dilation" was claiming more than the numbers support, whichever
pair of them you use.

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
with the system header -- `GlLoader.h:3-6`) shows `imgui.h` in exactly
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

The "told apart" column asks only what an observer of the strip can conclude:
a reading is told apart from a stale line if it **appeared where there was no
`Edge map` line** or if its **value differs from the reading before it**. An
identical repeat is not evidence of anything on its own, whatever the code
does.

| reading | frame reached how | grid / step | build | told apart |
|---|---|---|---|---|
| 1 | editor opened on f0000, **I** pressed | 3840x1920, step 2 | 96 ms | appeared |
| 2 | `>` then `<`, back to f0000 | 3840x1920, step 2 | 96 ms | **no** |
| 3 | `>` then `<`, back to f0000 | 3840x1920, step 2 | 96 ms | **no** |
| **4** | `>` to **f0001**, pen tool already on | 3840x1920, step 2 | **108 ms** | differs |
| **5** | `>` to **f0002** with the BRUSH tool, then **I** | 3840x1920, step 2 | **88 ms** | **appeared** |
| 6, after the strip fix below | editor reopened on f0000, **I** pressed | 3840x1920, step 2 | 19 ms | appeared |
| 7 | `>` then `<` | 3840x1920, step 2 | 83 ms | differs |
| 8 | `>` then `<` | 3840x1920, step 2 | 83 ms | **no** |
| 9 | `>` then `<` | 3840x1920, step 2 | 87 ms | differs |
| 10 | 120 MP still, see below | 3880x1940, step 4 | 129 ms; 128 after the fix | appeared |

**PASS**, 2.8x inside the bar at the worst reading. The grid and step are what
the design predicts for a 7680-wide frame (4096 px cap on the long edge ->
step 2).

**Reading 5 is the one to quote, and reading 2 is why.** `post_status` is
sticky, so had the map *not* been rebuilt, the previous build's text would
still be on screen and would read 96 again. Three identical readings is exactly
what a stale status line looks like, and nothing about reading 2 or 3 can tell
the two apart. Two checks settle it for the set. (a) Source: every frame load
clears `_status` and resets the livewire, so the line can only come from a
fresh `ensure_livewire()`. The two halves are in different functions, which an
earlier draft of this note put both in `pump()`: `load_frame`'s worker clears
`_status` as it publishes the loaded frame (`MaskSession.cpp:253`, inside the
`enqueue` lambda that starts at `:233`), and `pump()` calls
`_livewire.reset()` when it installs that frame on the UI thread (`:295`).
The argument is unchanged by the correction -- the clear still happens before
the frame is published and the reset still happens as it is installed -- but
an inheritor who went looking for both in `pump()` would have found one.
(b) Observed, and stronger because it needs no source
reading: with the **Brush** tool selected, changing to f0002 leaves **no
`Edge map` line at all** on the strip, and pressing **I** makes it appear
reading 88 ms. An appearance cannot be a persistence. Readings 2, 3 and 8 are
`>` then `<` back to the *same* frame, so identical input doing identical work
is the expected answer and the repeat carries no information either way; they
are kept in the table to show the trap, not as evidence.

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
predicted.

**Do not compare that RSS delta with `Livewire::bytes()`; they measure
different things by construction.** The edge map's row above is +101,616 KB
(99.2 MiB) against a `bytes()` of 21.6 MB, and an earlier draft of this note
explained the 4.7x away as "allocator and first-touch" with nothing behind it.
Most of the gap is not a mystery at all and falls out of
`Livewire.h:80-90` and `Livewire.cpp:83-131,225-226`, counted for this grid
(3880 x 1940, n = 7,527,200):

| | bytes | what `bytes()` does with it |
|---|---|---|
| `_fg` + `_dir` + `_zc`, 1 byte each | 3n = 22,581,600 | counted |
| the three 256x8 link tables | 18,432 | counted |
| **`bytes()` after `build()`** | **22,600,032 = 21.55 MiB** | = the 21.6 MB printed |
| `luma` (n), `mag` + `lap` (4n each) | 67,744,800 = 64.61 MiB | **not counted** -- local to `build()`, freed on return |
| `_dist` (4n) + `_parent` (n) | 37,636,000 = 35.89 MiB | counted, but **`set_anchor` allocates them**, not `build()` |

So `bytes()` is the surviving cost image and nothing else, and it is read in
`bench_livewire_on` immediately after `build()` and before any `set_anchor`,
which is why it prints 21.1 MB at the 8K grid (3n + tables = 21.11 MiB
computed) and 21.6 MB here. Peak *inside* `build()` is 21.55 + 64.61 =
**86.2 MiB**, four times what `bytes()` reports, and the first anchor click
adds another 35.9 MiB that a `bytes()` call before it cannot see.

**What is left over after that arithmetic is not measured, and is hedged
accordingly.** How much of the 64.6 MiB of transient the allocator hands back
to the OS was not traced -- no allocator instrumentation, no page-fault count.
The evidence that an RSS delta around a live GUI is a poor instrument here is
that **the same operation measured twice gave 99.2 MiB and 57.8 MiB** (the
second, on the fixed build, is in the re-take list below): a 41 MiB spread for
an identical build on an identical frame. Treat the RSS rows as evidence the
tool fits in memory at 120 MP, which is what spec §8.1 asks, and not as a
measurement of anything smaller.

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

**Three of those rows are line count x pitch and the fourth is not, which is
the point.** Read back from the running app: `GetFontSize()` = **16.0**,
`ItemSpacing.y` = **6.0**, `GetTextLineHeightWithSpacing()` = **22.0**. So
110 = 5 x 22, 132 = 6 x 22 and 176 = 8 x 22 exactly. The 192 row does **not**
reduce that way, and a naive 9 x 22 = 198 is 6 px over. A wrapped
`TextDisabledWrapped` is **one item two lines tall**, not two items: it costs
`2 x FontSize + ItemSpacing.y` = 2 x 16 + 6 = **38**, where two separate lines
would cost 44. The strip at 900 px is therefore seven single-line items plus
that one wrapped item: 7 x 22 + 38 = **192**, to the pixel. The six-pixel
discrepancy is exactly the one `ItemSpacing` the two wrapped lines share, and
it is why "count the lines and multiply" is not a safe way to predict this
height -- which is the same reason the reserve is measured rather than
computed.

**That 192 px is the worst case the battery reached, not the worst case
`draw_status` can emit.** Enumerating it (`MaskPanel.cpp:441-480`), the eight
items behind the 192 are frame/camera, kept/saved/corrected, brush/commit,
`hint_buttons`, `hint_path` (the wrapped one), `path_anchors`, `hint_view`,
and the status-or-error line. Two more items exist: `status_base_regenerated`
and `status_base_missing` (`MaskPanel.cpp:461-464`), one `TextDisabledWrapped`
each and mutually exclusive, since `base_state()` returns one value. Neither
was on screen for any row of the table above, and the exact arithmetic is how
that is known rather than assumed -- 110, 132 and 176 are 5, 6 and 8 lines to
the pixel, and a base-state line would have made them 6, 7 and 9. Open a
frame whose mask a run has regenerated -- which is the case this whole feature
exists for -- and every row above gains an item: **+22 px where it fits on one
line and +38 where it wraps**, putting the real worst case at 900 px around
214 rather than 192. **The rule below survives unchanged**, because it is
parameterised on the measured `status_h` and not on any row of this table; it
is the table's worst row that is not a bound. That is the third time in this
section that measuring the strip has turned out to beat enumerating it. SAM mode
adds the checkpoint picker's rows on top of these; its heights are under "SAM assist,
Task 5" below.

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

#### KNOWN LIMITATION: below a window height the strip clips again, and here it is

**The fix above removes the defect at every window size a person would work
at, and does not remove it at every window size.** `draw_canvas` floors the
canvas at `px(64.0f)`, and `draw_status` (`MaskPanel.cpp:441-480`) draws its
full content unconditionally with no cap and no truncation. Once the window is
short enough that the canvas has pinned at its floor, every further pixel of
shrink becomes a pixel of strip pushed past the window bottom. It is the same
failure the fix cured, re-triggered by **height** instead of by the old 118 px
constant -- **not a regression**, since the old code failed the same way at a
different threshold, but a boundary that was previously undisclosed because
the manual battery varied width (1600 -> 900) and never height.

Measured, by reading `GetContentRegionAvail().y`, the reserve, the canvas
height and **`ImGui::GetScrollMaxY()`** out of the running app while dragging
the window's bottom edge. `GetScrollMaxY() > 0` is the exact criterion: it is
content height minus window height, so it says by how many pixels the strip is
off the bottom rather than whether it looks off.

| tool / state | `status_h` | window height at which it clips | how |
|---|---|---|---|
| shape, empty status | 110 | **274 px** | formula |
| shape, status or error present | 132 | **296 px** | **measured** |
| pen, full width | 176 | **340 px** | **measured** |
| pen, 900 px wide, `hint_path` wrapped | 192 | **356 px** | **measured** |

**The rule is `window height < status_h + 164`**, and the 164 is not fitted:
it is the 64 px canvas floor plus exactly 100 px of chrome above the canvas
(title bar, tool strip, frame slider, window padding), and `avail_y = winh -
100` held at every one of the fifteen readings taken, from 950 px down to
202 px. The 100 decomposes: 24 px of title bar (`FontSize` 16 from
`Fonts.cpp:33` plus twice `FramePadding.y` 4 from `Layout.cpp:33`), 8 px of
`WindowPadding.y`, two 30 px button rows for the tool strip and the frame
slider (16 + 2x4 + `ItemSpacing.y` 6, `Layout.cpp:34`), and 8 px of padding
below. The mechanism shows directly in the data -- at the pen tool, full
width:

```
winh 342  avail_y 242  status_h 176  canvas 66 (floor 64)  scrollmax   0   fits
winh 302  avail_y 202  status_h 176  canvas 64 (floor 64)  scrollmax  38   CLIPS
winh 252  avail_y 152  status_h 176  canvas 64 (floor 64)  scrollmax  88   CLIPS
winh 202  avail_y 102  status_h 176  canvas 64 (floor 64)  scrollmax 138   CLIPS
```

`winh + scrollmax` is **340.0 in every clipping row** and the canvas sits on
its floor in every one of them: below the threshold the content height stops
changing, so overflow tracks shrink one for one. The shape-tool rows give
296.0 the same way, and the 900 px pen rows give 356.0. Two thresholds in that
table are read straight off a clipping row, one more likewise, and only the
empty-status shape row is the formula alone. The rows are the four states the
battery reached and not the four highest possible: a frame whose mask a run
regenerated adds a line to any of them (above), so the boundary goes up with
it. That is a property of the rule working, not of the rule breaking --
`status_h` is the input, and it moves.

**Both halves of the 164 are quoted at `ui_scale() == 1.0`, and the rule
scales with it.** `px()` is `unscaled * ui_scale()` (`Layout.h:26`), so the
canvas floor is 64 px only at scale 1; and every term of the 100 above is a
font size or a style padding, all of which `apply_style` scales together
(`Layout.cpp:49-50`: `style.ScaleAllSizes(scale)` and
`style.FontScaleMain = scale`). `status_h` needs no such correction because it
is read out of the running layout and already carries the scale. So the
general form is **`window height < status_h + 164 * ui_scale()`**. That is
derived from those three lines, **not measured**: every one of the fifteen
readings above was taken at scale 1.0, and nothing in this plan was run on a
scaled display.

For scale: 340 px is about a third of the height the window opens at, the
canvas is a 64 px sliver by then, and the content is still reachable by
scrolling. **Left unfixed on purpose this round.** The cheap and obviously safe
cap, if someone wants it, is to make the canvas floor yield to the strip rather
than the other way round --

```cpp
const float floor = std::min(px(64.0f), std::max(0.0f, avail.y - status_h));
const ImVec2 size(std::max(avail.x, px(64.0f)), std::max(avail.y - status_h, floor));
```

-- which is identical above the threshold (there `avail.y - status_h >= 64`, so
`floor` is 64 and the outer `max` picks the same value it picks today) and
below it lets the canvas shrink to nothing before any text is pushed off. That
would move the boundary from `status_h + 164` down to `status_h + 100`, i.e. to
the point where the window cannot hold the strip at all and no arrangement
helps. **Not implemented here**, and it is untested.

#### UNCOVERED: the status strip has no automated test, and cannot have one here

**The strip fix rests entirely on the live verification above. Nothing in the
test tree can fail if it regresses.** `_status_h` and `status_h` appear nowhere
under `src/app/gui/tests/`, and the only test change in this task is the
deletion of the flaky timing assertion.

That is a consequence of the architecture rather than an oversight.
`MaskPanel.cpp:1-5` carves ImGui out of this directory on purpose so that
`mask_doc_test` links `MaskDoc`, `MaskSession`, `Livewire` and `PathTool` with
no ImGui at all, which is what makes those 749 checks cheap and headless. The
reserve is `ImGui::GetCursorPosY()` either side of a function that calls
`ImGui::Text` -- there is no seam to test it through without either pulling
ImGui into the test binary or introducing an ImGui test-engine harness, and
neither is worth it for one layout quantity. **The position is defensible; it
is being written down so that it is a known gap and not an assumed
guarantee.**

What has nothing to catch it:

- a refactor that reintroduces a constant reserve, or moves `draw_status`
  above `draw_canvas`, or drops the measurement;
- a **translation** whose text grows the strip past what fits -- the strip is
  measured, so it would adapt, but the height boundary above moves up with it
  and nothing announces that;
- a new status or hint line, which is the likeliest of the three, since the pen
  tool added three;
- any change to the `px(64.0f)` canvas floor, which sets the boundary.

**After touching `draw_status`, `draw_canvas` or the catalog lines they draw,
re-verify by hand**: open the mask editor, press **I**, and confirm the
`Edge map:` line is readable with nothing scrolled; then provoke an error
(`chmod 555` on the mask directory and save) and confirm it is readable under
both a shape tool and the pen tool. That is the whole check and it takes a
minute. The height boundary in the section above is the other half of it.

#### UNCOVERED, the rest of it: what was read rather than run

The section above is one gap, named in detail because it is the newest. These
are the others, collected in one place so that an inheritor does not have to
infer them from what is absent. None of this is a defect; all of it is the
shape of the evidence behind every number in this note.

**Nothing automated ever drives the GUI.** `mask_doc_test` links `MaskLayer`,
`MaskDoc`, `MaskSession`, `MaskWindow`, `EditDoc`, `SelectShape`, `Selection`,
`FrameMask`, `FrameLook`, `Livewire` and `PathTool`
(`cmake/SsApps.cmake:412-427`) -- **`MaskPanel.cpp` and `PathOverlay.cpp` are
in no test target at all**, which is the deliberate ImGui carve-out and is
also the reason nothing in CI can catch a panel regression. Every in-app
result in this note came from a hand-run `guictl.py` battery. It is
reproducible and it is written down step by step, but it runs only when
somebody remembers to run it, and the two files that hold the paint grammar,
the canvas mapping and the whole status strip are the ones it is the only net
under.

**Concurrency was reviewed by reading, never stressed.** There is no
ThreadSanitizer build and no harness that opens, navigates, saves and closes a
session under contention. The worker-to-`pump()` handoff, `_error` and
`_status` stickiness, and the save queue are argued from the source in this
note; none has been run against a scheduler trying to break it. The one place
the race is visible in the record is the `cmp`-mid-write caveat in the Task 10
check, which was found by accident rather than by looking.

**The livewire bench arms were run once.** The `SS_MASK_BENCH` figures under
"Plan 2, Task 9" are three repeats of the test binary; the in-app criterion-6
figures are a single pass. Where the two agree, that is one sample against
three, not two measurements that could have disagreed independently.

**One backend, one platform, one machine.** Everything here is an M5 Pro on
macOS 26.6.2, built `-DSS_BACKEND=vulkan`. No CUDA build of this code has been
run, no non-macOS build, and no display at a ui scale other than 1.0.

**Trusted, not re-verified**: `app::load_stencil`, `app::load_rgb`,
`app::image_size` and `app::group_frames_by_camera` (`src/app/FrameMask.h`),
and `app::photo_turn` (`src/app/FrameLook.h`). All pre-date this work and all
are assumed correct by every fixture in it. The EXIF turn is exercised for
real in one session test only, `test_session_exif_turn`, which writes its own
Orientation 6 JPEG; `stbi_write_jpg` writes no EXIF, so `photo_turn` is the
identity in every other fixture, and no other orientation is tested.

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

## Task 16: the brush-size slider, and the eraser (2026-09-22)

Radius was keyboard-only and invisible. On the operator's 15520 x 7760 masks
the 24 px default is a dot, nothing on screen said `[` and `]` existed, and
finding a usable size meant holding `]`. While that was in flight they asked
for **an eraser with the same affordances**.

### The eraser is a paint mode, not a `ToolId`

`ToolId` (`EditTool.h:27-32`) is the 3D editor's **selection-shape** table:
`shape_of()` maps it to a `ShapeKind`, `kNumSelectTools` lays out the Select
tab, and `EditPanel` iterates it. An eraser is not a shape, and a row there
would put an "Eraser" button in a panel where it means nothing. So it is a
mask-editor mode, one value of `MaskSession`'s `CanvasMode` (shapes, eraser,
pen, SAM), which makes two tools on at once unrepresentable; the eraser also
forces the brush shape. The switches go through `pick_tool` / `pick_eraser` /
`pick_path` / `pick_sam` (`MaskPanel.cpp`), which is also what keeps the
toolbar and the key handler from drifting over what else a switch cancels.

### It paints `ForceKeep`, and the alternative reading is wrong for a reason

`composite()` is `keep ? 255 : drop ? 0 : base` (`MaskLayer.h:47-48`). So
`Paint::Clear` can only undo the operator's own strokes: over a pixel the
**run's** mask dropped it returns the pixel to that drop and nothing visible
happens. Recovering what the run got wrong is most of what this editor is for,
so "erase" has to mean `ForceKeep`. That also undoes a brush stroke, because
`paint_rect` zeroes `_drop` when it sets `_keep`
(`MaskDoc.cpp:176-180`). `Shift+Ctrl` stays `Clear`, and `Ctrl` still swaps
the two, so the three operations stay distinct:
`paint_for(shift, ctrl, erasing)` is now three-argument, and `paint_now` is
the member that supplies the session's own flag at the two commit sites.

**The unit test for this needs a base-DROPPED region or it proves nothing.**
Over base-kept pixels `ForceKeep` and `Clear` produce the identical kept
count, so a fixture painted on ordinary ground is satisfied by either. The
test builds a 64x48 frame whose base drops one 16x12 block: the eraser over it
takes `kept()` from 2880 to 3072 and `Clear` leaves it at 2880. Mutating the
eraser to `Clear` fails that check and fails "the eraser puts back exactly
what the brush took" **not at all** -- which is the evidence that the block is
doing the work.

### ONE radius, shared -- and this reverses the first ruling

Separate radii shipped first, on the general argument that every paint program
keeps them apart. **The operator used it on a real 120 MP correction and asked
for the opposite**: *"carry over eraser and brush size from each other, rather
than keeping it independent."* Their experience of the task beats the
generalisation, so `_eraser` is gone and `radius()` / `set_radius()`
(`MaskSession.h:99-100`) address the one float. The slider, `[`/`]` and
Alt+wheel all move it whichever tool is up.

**The test was inverted, not deleted.** It guarded independence, which is now
the defect; the risk in this design is a **second copy surviving** -- a switch
that forgets to carry the value, or a writer that moves only one of them. It
now asserts the carry in both directions and that the clamp carries with it.
Four mutations, each killed by a named check: a second copy with no carry (the
design being reversed) fails all four; a second copy carried one way fails the
"BOTH ways" and clamp checks; a switch that resets to the default fails the two
carry checks and the clamp check; `set_radius` without the clamp fails the
clamp check.

Worth recording precisely, because it is the kind of thing that makes a test
look stronger than it is: **the "set_erasing never moves the radius" loop is a
backstop, not the load-bearing check.** It kills the no-carry mutant and does
**not** kill the reset-on-switch one -- by the time the loop runs that mutant
has already pinned the value at the default, and every further switch resets it
to the same number. The three directional checks are what bite.

The strip keeps naming the **active tool** (`Eraser: 24 px`, not a generic
`Radius:`). The brush and the eraser are indistinguishable on the canvas, so
the label is the confirmation of which one is armed, and the sharing is
self-evident the moment you switch and the number does not change.

### The slider

`MaskPanel.cpp:219-233`, on the **frame-navigation row**, shown only under the
brush or the eraser, `ImGuiSliderFlags_Logarithmic | AlwaysClamp` over
`[kMinBrush, kMaxBrush]` = [1, 4096]. The range is twelve octaves and `[`/`]`
are multiplicative, so a linear slider would put every usable size in the
first 2% of travel. Measured in the app: at 65 px the grab sits at **0.50** of
the 260 px track, where linear would put it at 0.016.

Its format string is `i18n::format(brush_radius | eraser_radius, {"%.0f"})` --
the sanctioned way to get translated words into a printf pattern ImGui fills
(`Ui.h`, `SliderIntRaw`'s comment), and it reuses the message the strip
already had rather than adding a label.

**No third toolbar row, and the `164` is unchanged -- measured, not argued.**
`##maskcanvas` starts at `y0 = 92` in every tool state after the change,
exactly as before it, so `avail_y = winh - 100` still holds and the rule
`window height < status_h + 164 * ui_scale()` is untouched. `status_h` at
1600x950, read as `942 - canvas.y1`: shape **110**, brush **110**, eraser
**110**, pen **176** -- the same four numbers the table above records. The
eraser costs no strip height because `hint_eraser` REPLACES `hint_buttons`
rather than adding to it, and is shorter (it has no "Right click closes a
polygon" clause).

**What the toolbar row DID cost is width.** Its right edge (the `Done`
button's) goes **964 -> 1068 px** at `ui_scale() == 1.0`, English labels: the
row now needs a window at least ~1076 px wide, where it needed ~972 before.
The window has no `HorizontalScrollbar` flag, so below that `Done` is clipped
rather than scrollable -- the title-bar close box does the same job, so this
is degraded, not fatal. **At the 900 px width the earlier battery used, the
row already overflowed before this change**; the eraser widens an existing
overflow rather than creating one. Not fixed here, and recorded so nobody
rediscovers it as new. The SAM button (Task 5) moves the edge to 1172 px.

### The keys are on the slider, in the corner the tool buttons use

The operator asked for *"[ and ] as shortcuts"*. **They already worked** -- the
gap was discovery. The only place that said so was `hint_view`, the fourth line
of the status strip at the bottom of the window, sharing a sentence with zoom,
pan and Esc. So this is an affordance, not a binding, and nothing about the keys
changed.

`ui::corner_key` (`Ui.h:219-229`) turns out to be the right tool unmodified: it
takes `ImGui::GetItemRectMin()` / `GetItemRectMax()`, so it draws over the **last
item**, not specifically over a button. The slider therefore carries
`[ ] Alt+wheel` in the same corner `Q B E L P C F T K`, `I` and `X` sit in
(`MaskPanel.cpp:234-240`), plus a `help_on_hover` sentence of the kind `save` /
`revert_frame` / `revert_all` already have.

**The hint is a `Msg`, not a raw key string.** The brackets are identifiers and
stay Latin, as the tool keys do -- but "wheel" is a word, and every language
already translates it in `hint_view`, so leaving it English would have been a
real regression that no lint would catch (`corner_key` is not one of
`check_i18n.sh`'s banned entry points). Alt+wheel earned the extra characters by
being the least discoverable of the three ways in.

**Cost, measured in the running app rather than argued:**

| | before | after |
|---|---|---|
| row 1 right edge | 1068 px | **1068 px** -- unchanged, the hint adds no item |
| `status_h` (shape / brush / eraser) | 110 px | **110 px** -- unchanged, nothing new clips |
| row 2 right edge | 588 px | 668 px |

The slider widened 260 -> 340 px so the hint clears the centred value at its
widest (`Eraser: 4096 px`). Row 2 is not the constrained row: it had ~1000 px
spare and still has ~920.

**One blemish, recorded rather than hidden.** At the very top of the range the
grab reaches the right edge and the hint draws **over** it -- an overlap, not a
clip, and exactly what an active tool button already does with its corner key.
It does not reach the state a first-time user meets: the 24 px default puts the
grab at 38% of a logarithmic track, so the right corner is clear. Checked with
the grab mid-track in five languages and the hint renders complete and clear of
both the grab and the value in all five -- `[ ] Alt+wheel`, `[ ] Alt+ホイール`,
`[ ] Alt+tekerlek`, `[ ] Alt+колесо`, `[ ] Alt+molette`.

**No new test, and the reason is not laziness.** No binding changed; the
arithmetic behind `[` and `]` is already pinned by `step_brush`'s six checks and
the shared-radius carry checks, and a hint drawn with `AddText` has no seam
`mask_doc_test` can reach -- `MaskPanel.cpp` is outside that binary by design
(the same gap this note already records for the status strip). What was done
instead is the app check: `[` pressed twice from 4096 px gave **2959 px**, which
is 4096 x 0.85^2, so the keys still drive the slider after the width change.

### Key **X**, not E

Taken in the shared table (`EditTool.cpp`): `Q B E L P C F T K`, plus `I` for
the pen. **E is the Ellipse.** Rebinding a shortcut the editor already
documents in order to add a tool is not a trade worth making, so the eraser
took `X`, which is free in the shared table as well as in this panel -- if an
eraser ever does move into `ToolId`, the key comes with it.

### Alt+wheel over the canvas

`MaskPanel.cpp:273-281`. The bare wheel is already the zoom, and Shift/Ctrl
are the paint modes, so Alt is what was left; it is read by no mask tool and
by no view gesture. The factor is `1.18^wheel` -- the **reciprocal** of the
grow step, not the bracket's 0.85 -- so a notch back exactly undoes a notch,
where `[` after `]` does not (1.18 x 0.85 = 1.003). A test asserts both, so a
wheel wired straight to `step_brush` cannot pass. It sits inside the same
`!_tool.in_progress()` guard as the view, because a `ShapeStroke` carries one
radius and changing it mid-stroke would resize the whole stroke.

Measured in the app, reading the slider's own label: 24.00 -> 28.32 -> 33.42
-> 39.43 -> 46.53 -> 54.91 up, then 54.91 -> 46.53 -> 39.43 -> 33.42 back
down. Exactly reversible, to the digit the strip prints.

**A pre-existing trap this uncovered, and it is NOT new: a second wheel event
at a pixel-identical pointer position is ignored.** ImGui locks wheeling to
one window and releases the lock when the mouse moves; with the pointer held
still, `IsItemHovered()` on the canvas goes false and stays false while
wheeling continues. Proved with a temporary probe: first event `hovered=1`,
every one after it `hovered=0` until a 1 px `move` was injected between
notches, after which all of them read `hovered=1`. **The zoom has always sat
behind the identical gate**, so it has always behaved this way -- a trackpad
scroll, where the pointer genuinely does not move, is the case that reaches it
in real use. Left alone here; it is ImGui's documented lock, not ours.

### The `Brush: N px` strip line stays; the reason to drop it does not hold

It was proposed for deletion on the grounds that it costs a line of strip
height. **It costs none**: it shares its line with `status_commit` via
`SameLine()`, so removing it leaves the line -- and the height -- exactly
where it was. It now names the active tool (`Eraser: N px` under the eraser)
and remains the only radius readout when the slider is hidden.

### `step_brush` had no production caller, and now has two

Its arithmetic was re-inlined into `MaskPanel.cpp` at `6126a650`, leaving six
tests pinning dead code -- the shape of defect this note keeps finding. `[`
and `]` route through it again, and `clamp_brush` / `scale_brush` /
`wheel_brush` join it in `MaskSession.cpp:410-435`, the file
`mask_doc_test` links, so every radius arithmetic both tools use is tested in
one place. `clamp_brush` is a rejection test rather than `std::clamp` because
`std::clamp` **propagates a NaN**, and a NaN radius rasterizes nothing while
the slider and the strip still read a number.

### Verified in the app (`SS_GUI_AUTOMATION=1`, offscreen, 1600x950)

A synthetic three-frame workspace whose base mask drops one 200x200 block, so
`Kept %` has a known starting value of 94.9%.

- The slider is present under **Brush** and **Eraser** and absent under Box,
  Polygon and Path -- by button and by key (`B` hides, `X` shows, `C` shows,
  `I` hides).
- Six `]` presses took the label 24 -> **65 px**, the value `step_brush`
  predicts (24 x 1.18^6 = 64.9).
- Dragging the slider moved the strip to **2097 px**; a linear track at the
  same travel would have read ~3650.
- Erasing inside the base-dropped block took `Kept` **94.9% -> 95.4%** and
  carved a clean untinted capsule out of the red region. Predicted area for an
  18 px radius over a ~77 mask-px path is 0.48% of the frame.
- Brush over base-kept ground, then the eraser over the identical path.
  **With separate radii** (brush 24 px, eraser 18 px): `Kept` 95.4% -> 94.7%
  -> 95.2%, an under-restore of 0.2 pp, which was the clearest demonstration in
  that run that the two radii really were independent. **With the shared
  radius, re-run on the same fixture, same path, same drag**: 94.9% -> 94.2%
  -> **94.9%**, and the strip read `Eraser: 24 px` on the switch. The
  under-restore is gone, which is the cheapest available check on the reversal
  and one that could have failed.
- Plain wheel still zooms: the photo's width at a fixed row goes 895 -> 1492
  screen px over three notches in (clipped by the canvas at 1584).
- The corner hint reads `[ ] Alt+wheel` at the default and stays clear of the
  value at 4096 px; the tooltip raised on hover; `[` twice from 4096 gave
  **2959 px** = 4096 x 0.85^2.

### `/ui/scroll` grew `shift=`, `ctrl=` and `alt=`

`Automation.cpp:509-532`, the way `/ui/drag` already had them, plus the
matching flags in `guictl.py`. Without it a modified wheel cannot be driven
from a script at all, and this binding would have shipped unexercised. This is
a change to test infrastructure outside the feature's file list, flagged here
rather than folded in silently -- the same call the pen tool's fix round made.

## SAM assist, Task 1: the seam, and what one add costs the history (2026-09-22)

`app/gui/mask/MaskAdd.{h,cpp}` turns one SAM prompt's detections into the
`Stencil` + `Rect` pair `MaskDoc::paint` takes, with no ImGui, GL, `sam::` or
`nn::` in it, so `mask_doc_test` compiles it. The bounds are the extent of the
resampled plane itself, never the detections' boxes: a box one pixel tight
would leave set pixels outside the rect, which `paint_rect` never writes.
`set_px` is the plane's own set-pixel count, the quantity P3's area null is
built from.

**Resampling follows `sam::Masker`'s `upscale_nearest` exactly**
(`floor(d * src / dst)`, `sam/Masking.cpp:85-87`), not a pixel-centre mapping.
The two agree at integer ratios and part at any other: on a 2x2 -> 3x3 upscale
source pixel (1,1) lands on (2,2) alone under Masker's rule and on a 2x2 block
under the centre rule. A region the seam resamples therefore covers the pixels
the model's own overlay drew. `add stencil: a 3:2 resample follows
sam::Masker's floor mapping` pins it.

### P5: bytes one add costs the undo history

Measured by `test_add_history_bytes` (1552x776) and `bench_add_history`
(`SS_MASK_BENCH`, 15520x7760). One `ForceDrop` add of a disc of radius H/5
centred at (0.6W, 0.55H). "Earlier edit" = a speckled edit already painted
into the top-left tenth of the frame; "whole frame" = the same stencil handed
to `paint` with the full-frame rect instead of its own bounds (P5's mutant).

| arm | bytes | paint ms |
|---|---|---|
| 1552x776, earlier edit, own bounds | 1,472 | -- |
| 1552x776, earlier edit, whole frame | 25,882 (17.6x) | -- |
| 1552x776, fresh, own / whole | 1,472 / 1,850 | -- |
| 15520x7760, fresh, own / whole | 17,377 / 18,644 | 24.0 / 267.6 |
| 15520x7760, earlier edit, own / whole | 17,377 / 2,427,914 (139.7x) | 23.6 / 258.6 |
| **`kP5Bar` = 2 x the measured 1552x776 own** | **2,944** | -- |

The 1552x776 figures matched the plan's emulation of `rle_encode` to the byte.
M5 Pro, one run; paint times are single samples, bytes are deterministic.

**Why the fixture carries an earlier edit.** On a fresh document the layers
are uniform outside the disc, so the whole-frame rect RLE-encodes to nearly
the same size as the region's own box (1,850 vs 1,472; 18,644 vs 17,377 at
full size) and `history_bytes` cannot tell the mutant from the fix. The
whole-frame rect only costs once it re-encodes an edit that lies elsewhere.
`add bytes: on a fresh document the whole-frame rect is invisible to
history_bytes` asserts that, so the fixture cannot be simplified back to a
fresh document without the byte checks going blind. The paint time separates
the two arms on either document (~11x at full size), but it is not asserted:
it is a timing.

## SAM assist, Task 3: does the device number fall on unload? (2026-09-22)

**VERIFIED, M5 Pro 24 GB, MoltenVK 1.4.2, `sam3-q4_0.ggml`.** Yes, all of it,
every time. A throwaway probe in `cmd_segment` (never committed; reverted)
read five instruments at each stage, then ran three more load / encode /
segment / unload cycles on the same `Session` in the same process:

```
./build/spirula sam segment --model ~/.cache/spirula-studio/models/sam3-q4_0.ggml \
  --image ingest/osmo360/test/play_room/photo-monopod/CAM_20260810150859_0066_D.JPG \
  --point 12261,6053 --vram --out <tmp>
```

Five processes x four cycles = 20 unloads. Every one gave the same numbers to
0.1 MiB (MiB throughout; `footprint` prints MB but means MiB):

| stage | TOTAL | pool | allocator | Metal | phys_footprint |
|---|---|---|---|---|---|
| before first load | 0.0 | 0.0 | 0.0 | 0.1 | 4.4 |
| after load | 2192.0 | 1680.0 | 2257.0 | 2257.5 | -- |
| after encode + segment (15520x7760) | 2407.1 | 1895.1 | 2472.1 | 2472.9 | 4329.6 |
| after `unload()` | **0.0** | **0.0** | 65.0 | 65.7 | 1922.4 (settled) |

`[model] weights: 1652.2 MiB` on every load. Pool delta across unload
**1895.1**, TOTAL delta **2407.1**, both on all 20 unloads; neither creeps
across cycles.

**What each instrument is.** `TOTAL` = every `VramPool` slot's *capacity*
(not its `used`) plus the session arena's capacity (`Session.cpp:565-585`);
`totalCapacity()` is the pool part alone. Capacity is the byte count handed
to `vkAllocateMemory` for that slot, and `VramPool::release*` calls
`Allocator::free`, which calls `vkFreeMemory` at once (`Memory.cpp:243-266`, `:362-395`):
the pool keeps no free list, so "grow-only" means a slot never shrinks while
held, not that freed memory is cached. The 65.0 MiB left after unload is the
Stream's staging and parameter rings, allocated straight from the
`Allocator`, outside both the pool and `TOTAL`.

**Two cross-checks that do not read our bookkeeping.** (1) Metal's own
`currentAllocatedSize` on the system device, via the ObjC runtime: equals the
allocator's live total to within 0.8 MiB at every stage, including after
unload. (2) The kernel ledger: `task_info` `phys_footprint`, and `footprint
-p`'s category *Owned physical footprint (unmapped) (graphics)*, which went
2467 MB loaded -> 60 MB after unload. The footprint fell by 2407.2 MiB per
cycle, i.e. by exactly `TOTAL`.

**The OS gives the memory back late.** `vkFreeMemory` returns before the
kernel ledger moves: polling every 25 ms, `phys_footprint` reached 90% of the
drop 0-200 ms after `unload()` on 10 of 11 polled unloads and 2125 ms on one
(a first-cycle unload). An OS-side reading taken right after close can still
show the checkpoint; the pool reading cannot, because it is our own count.

**`phys_footprint` is not `ps -o rss=`.** RSS misses device memory on Apple
silicon (641 MB against 1652.2 MiB, spec 6.3); `phys_footprint` includes it.
It is still only a delta instrument: it also carries the decoded 120 MP frame
(~750 MiB here) and every other heap.

**Ruling.** P6 is writable against `sam_vram_mib` (`TOTAL`): 2192.0 MiB
loaded, 2407.1 MiB once a 15520x7760 frame is encoded. P12 is writable against
`sam_pool_mib`: it fell 1895.1 MiB, above the 1652.2 MiB weight size, and
returned to its pre-load value exactly. Measured on the visual-prompt path
only: a text prompt fills `TextFeat` / `PromptFeat` / `FusionFeat`, which
`unload()` also releases, but no text prompt ran here.

## SAM assist, Task 5: the first usable slice (2026-09-22)

**How to reach it.** Open the editor with **Correct masks** (Train screen, beside the
dataset path; or the dataset screen, under Update Dataset, when the workspace's
`masks/` is not itself an input). Press **G** or the **SAM** button at the right of
the tool row. The status strip then shows the checkpoint picker (the dataset screen's
own, over the same model id and download), the hint, and the last result. A click on
the canvas drops the object under it; **Ctrl+click** keeps it; **Shift+Ctrl+click**
clears the corrections on it back to the base -- the brush's grammar (on macOS the
logical Ctrl is the Command key, as for every other Ctrl chord in the editor); **Esc**
cancels a prompt in flight once its current step ends. With no cached checkpoint the button still arms, and the strip
offers **Get the model** and the licence prompt; the consent modal is drawn from
`frame()`, so it appears over the editor on any screen.

**What changed underneath.** `CanvasMode {Shape, Eraser, Path, Sam}` replaces the
eraser and pen flags, so two tools on at once is unrepresentable. The dataset screen's
checkpoint picker moved to `draw_mask_model_picker` (`MaskPrompt.cpp`); its row choice
is the ImGui-free `mask_picker_row`, pinned in `mask_doc_test`. `GuiApp::frame()` hands
the editor the app's checkpoint every frame, so a pick on either screen or a finished
download lands at once.

### Measured in the app (M5 Pro, offscreen 1600x950, `sam3-q4_0`, the 15520x7760 fixture)

Every GUI run used a throwaway `XDG_CONFIG_HOME` and `XDG_CACHE_HOME` and its own
automation port; the fixture was a copy of a 46-frame 360 photo dataset (images linked,
`masks/` and `sparse/` copied).

| check | observed |
|---|---|
| P14 keys `b g x g i g x b` | `shape sam eraser sam path sam eraser shape` |
| P14 buttons `sam eraser sam path sam` | `sam eraser sam path sam` |
| P16b picker transcript, before vs after the extraction | `diff` identical; transitions clause PASS |
| P16b against a build that never draws the modal | `asked: modal=0`; both halves fail |
| P9, no cached checkpoint, from inside the editor | mode `sam`, editor model `''`, Get-model drawn in the editor, a click: results 0 -> 0, busy false, clicks 0 -> 0, no modal, nothing written to the cache |
| P9, a build configured `-DSS_BUILD_SAM=OFF` | `key g` and a click on the SAM button both leave the mode `shape`; hovering the disabled button shows `backends().masking_reason` ("built without the segmentation module") |
| P15a, pick in the editor | app `model_id` `sam2.1-tiny`, path `''`; dataset screen then shows Get-model with Tiny in its combo |
| P15b, pick on the dataset screen with the editor open | editor model == app path == the SAM 3 file |
| P18, Train screen, consent + download from the editor | modal on the Train screen (`license_prompt` `sam2`); `running` with Stop in the editor; editor closed mid-download, still `running`; `done`, 79,320,544 bytes; reopened editor holds the Tiny path unasked |
| P17 warm | `sam_vram_mib` 2407.1, text ok true, second click job 50.8 ms |
| P17 switched to Tiny, before any prompt | `sam_vram_mib` -1, text ok false |
| P17 back to SAM 3 | job 5716 ms (paid the load) |
| P17 (c), switch while an encode runs | busy when the pick landed; dropped 0 -> 0, results 3 -> 3, history 0 -> 0 |
| status vs error, during the first load | status = `sam_first_load`, error empty |
| status vs error, a 50 MiB zero-filled Tiny file | status empty, error = the loader's bad-magic sentence, drawn red in the strip; no crash |
| P1b, first prompt of a session (load + encode), job ms | 10455, 6001, 5716, 11118 across four loads; the upload alone logged 7172 ms once |
| P1, first click on a new frame, warm (prompt -> painted / job) | 4096/3614, 4044/3577, 4063/3588, 4109/3595, 4194/3587 ms |
| P2, further clicks on the same frame (prompt -> painted / job) | 648/105, 546/83, 564/97, 623/114 ms |
| P3, three points, document at base before each (re-taken after fix round 1, points read off a fresh screenshot) | drop-layer pixels == `sam_last_area` exactly (2,054,286 / 328,023 / 537,570); pairwise IoU 0.0000; areas differ 84.0 / 73.8 / 39.0 %; PASS |
| Ctrl+click on a dropped person | kept +901,028 px == `sam_last_area` (force-keep) |
| Esc during a first click on a new frame | idle 3372 ms after Esc; results, dropped and history unchanged |

**The cross-screen inference checks, driven rather than read.** (a)+(b) Editor warm
(2407.1 MiB), then the dataset screen's Try the mask: the editor yielded at once
(`sam_vram_mib` -1); the preview ran SAM 3 on a "person" prompt; an editor click while
it was open was refused with `sam_blocked_preview` in `sam_error`; closing the preview
cleared that message; the next editor click showed `sam_first_load` and took 11,461 ms
(a cold reload), painted, no crash. (c) Update Dataset, "Add the masks only": the
editor yielded (2407.1 -> -1); a click during the run was refused with
`sam_blocked_run`; the message cleared when the run ended; the next click reloaded
(11,335 ms). (d) The strip draws `sam_error()` in red (the bad-magic run above).
(e) Revert all's confirmation: before fix round 1 **Esc did not close it** and it
deleted nothing (the `mask_edits/` listing and an MD5 over every file in `masks/` and
`mask_edits/` identical before and after). Esc now acts as Cancel: the modal closes and
`mask_edits/` is untouched.

### Fix round 1: the picture no longer moves under the cursor

**The defect (found in review).** `draw_canvas` sized the canvas as the window minus
last frame's strip height, and a 2:1 frame is height-limited, so every line the strip
gained or lost rescaled the whole picture about its centre. Measured before the fix,
the same screen point (330, 470) over three click cycles: canvas 666 idle, 622 busy,
644 done, and the click reached frame pixel (2283.7, 4404.3) the first time and
(2096.6, 4554.8) after -- 240 px apart.

**The fix, two halves.** The canvas reserves `StripReserve::h` (`MaskWindow.h`): the
tallest strip measured since the mode or the window width last changed, so a line that
leaves keeps its space. In SAM mode the busy / error / result lines share a fixed
two-line slot and the session's status-or-error line always holds its one line, so they
never grow the strip at all. And a click is mapped through the layout the previous frame
drew (`note_shown` / `shown_to_frame`), cleared wherever a new document arrives, so a
resize can never misplace one.

**Measured after.** The same cycle on a frame never encoded, idle -> busy -> done:
canvas **622 / 622 / 622**, and both clicks reached (6512.4, 4466.4). Three warm cycles
at (330, 470): 622 throughout, (1896.3, 4715.9) every time. Against a build with the
slot padding removed, the same fresh-frame cycle read 666 / 622 / 622 and the two clicks
reached (6594.8, 4171.3) and (6512.4, 4466.4).

**Also in this round.**
- The stencil is built on the SAM job thread, limited to the box each region can reach.
  UI-thread time from result to uploaded texture (`sam_ui_ms`): **110, 135, 121 ms**
  before, **46 cold, 29, 28 ms** after on the same clicks (and 6-18 ms on others). The
  job absorbs it: warm job 45-48 ms before, 190-200 ms after. Prompt to painted on the
  P3 clicks: 234 and 278 ms (was 546-648).
- Shift+Ctrl+click clears (measured: a kept person back to exactly the base count, a
  dropped door back to the base count); `paint_now` is pinned in all four modes.
- `sam_cancel_slow` no longer promises a time: the flag is read between stages, and on
  this machine a load is ~2.5 s, an encode 1.9-3.6 s, a first load 5.7-11.1 s.
- The window cannot be made narrower than its tool row as measured last frame: dragged
  to 700 px it stops at 1180 with `Done` at 1172. Without the constraint `Done` falls
  out of the item table entirely.
- The session's status-or-error line is the strip's FIRST line, so a short window clips
  it last: at 386 px the red "Could not write ..." line is fully visible.
- SAM mode no longer shows "Brush: N px / Last stroke".
- `/ui/state` carries `app_ready`, false until the first frame has published the app's
  fields; `guictl.py launch` waits for it.

### Fix round 2: the drop margin, and clicks off the picture

**The margin.** The dataset screen grows every detection by `dilate_ratio` (5% by
default) so the rim of colour a tight outline leaves is covered; the editor called
`segmentVisual` directly and skipped it. The geometry now lives once, in
`core/MaskMargin.h` (header-only, model-free); `sam::dilate_radius_px` and
`sam::accumulate_dilated` call it, and so does the editor's seam (`build_add_stencil`).
A **drop** grows by the editor's slider; a **keep** or a **clear** uses SAM's exact
outline (`drop_margin()`), and the ratio is never signed, so no trim can reach the
editor. The editor's radius is taken from the region's own pixel extent where Masker
uses the model's box. (Superseded by Task 7, below: the editor now takes the model's box.) The slider is the dataset screen's own
(`draw_margin_slider` in `MaskPrompt.cpp`, now called by all three screens), bound to
the editor's MaskSettings: moving it left the dataset screen's `dilate_ratio` at 0.05.

**Masker is byte-identical across the move.** `spirula sam track --text "person;
chair"` on two 15520x7760 frames at `--dilate-ratio` 0.05, -0.05 and 0: the same MD5
over the written masks before and after (7b2c71b4..., 259aa3b8..., 13785ba6...), and a
repeat run before the move matched too. A build with the radius's odd-kernel step
removed changed the +-5% hashes and left ratio 0's alone.

**Measured in the app** on one chair click: drop at 0% 412,176 px, at 5% 496,156, at
12% 598,593; Ctrl-keep and Shift+Ctrl-clear at 12% both 412,176 (the exact outline).

**A changed slider applies to the next click, not the last one.** (Superseded by
Task 6, below: a release of the slider now re-applies it to the drop just made.)

**A click off the picture is ignored.** `sam_prompt_point` refuses a point outside
`[0, fw) x [0, fh)` before any job starts, quietly: clicking the dark margin at
(100, 400) left results, history and the error line unchanged.

**Keys no longer act behind a popup.** `handle_keys` is skipped while any popup is open,
or was open when the frame began -- the Esc that closes the Revert-all confirmation
used to clear the pen's anchors too (2 -> 0). Measured: B under the modal leaves the
pen armed, Esc closes it with the anchors still 2, and a second Esc clears them.

**The canvas reserve only grows within a mode.** Anything tall that appears -- the
picker's download rows, a base-state line, the pen's straight-line line -- keeps the
canvas smaller until the mode or the window width changes. That is cosmetic: a click
maps through the layout that was drawn, whatever it is.

### The strip and the tool row (after fix round 2)

The SAM button widens tool row 1 by one button: `Done`'s right edge is **1172 px** (was
1068), and the window refuses to be narrower than that row. `status_h`, read as
`942 - canvas.y1`: brush **132**, pen **198** (each +22 for the status slot, which is
always one line), SAM **258** with the margin slider, SAM with no cached checkpoint
**288**. By the rule above SAM mode clips below **422 px** of window height, **452 px**
with no checkpoint; the first line to go is the key hint at the bottom, the last the
error line at the top. (Task 6's object box raises these to about 536 and 566 px; Task 7's
text row to 570 and 600.)

### Task 6: the object list, refinement that replaces, and the margin re-applied

**How to reach it.** In SAM mode the strip now carries the dataset screen's own
object list ("Objects to click on"), moved verbatim into `draw_mask_objects`
(`MaskPrompt.cpp`) and bound to the editor's MaskSettings. A click joins the
current object; a **right click** is "not this" on it (red dot with a cross); the
prompt is every click that object has on this frame. **A second click on the same
object replaces its add** while that add is still the newest edit on the frame
(stamped by `sam_frame_stamp()` and `MaskDoc::revision()`; Task 7 replaces the revision
with `MaskDoc::top_step()`); another object, a text
prompt, or any edit in between adds instead. The cost, stated in the hint: one
Ctrl+Z after a refinement removes the whole object. A right click refines in the
mode of the add it replaces (a kept object stays kept); with none to replace it
takes the modifiers. (Task 7 makes both buttons follow one rule; see below.) **Releasing the margin slider re-applies it** to the drop just
made, in place, under the same rule. The list sits in a fixed-height box (two
objects, then it scrolls, to the end on a new one), so adding objects never moves
the picture: the canvas read 474 px with 1 to 5 objects.

**What is held for the re-apply.** The click's detections, cropped to their set
pixels (`HeldRegion`), not the 120 MB plane: 3.1-3.3 MB for the monopod. A keep, a
clear or a text prompt holds nothing, and the crop is let go on the first frame it
can no longer re-apply.

| check (M5 Pro, `sam3-q4_0`, 15520x7760, margin 0) | observed |
|---|---|
| CLI null, `P` 12261,6053 alone / with `N` 12400,6800 | 1,333,113 / 1,147,773 px, separation 13.9 % |
| app, `P` then right-click `N` (landed 12262.1,6057.4 / 12393.1,6794.1) | 1,333,327 / 1,109,641 px; `\|APP_A - A\| / A` 0.016 % |
| history over both prompts; clicks | 0 -> 1; 0 -> 2 |
| saved drop layer after Ctrl+S | 1,109,641 == the refinement, not the union |
| the same run on a build that drops negative points | 1,333,327 / 1,333,327: fails |
| a lone right click on a new object | results 1 -> 1, clicks 1 -> 1 |
| three clicks on one object, then a second object (frame 2) | history 1 after all three, 2 after the fourth |
| slider re-apply, UI thread, first build (inline) | 270-330 ms on one session, 650-1020 ms on another; the stencil rebuild is ~280 ms of it |
| slider re-apply after fix round 1 (a job) | UI thread 0.07-0.11 ms to start it, 15-24 ms to land it; the job 234-250 ms |

**The dataset screen is untouched (P13).** Its fields were set to a prompt, three
objects and the second current, then an editor session made 9 clicks on 3 objects:
`mask_clicks 0, object_count 3, current_object 1, prompt "person"` read identically
before the editor, with it open, and after Done. The object list drawn by the
dataset preview was pixel-identical (0 of 54,000 pixels) before and after the move.

**The strip.** The object box adds 112 px: SAM mode now fits down to a window of
about **536 px** (measured: canvas 73 px at 545, at its 64 px floor by 500), about
566 px with no cached checkpoint (derived, +30, not measured).

**Fix round 1.**
- **Clear and Clear all forget the last add.** The list's numbers then name other
  objects, so a click on the same number used to replace the cleared object's add,
  and one undo lost both. `sam_objects_edited()` now runs whenever the list reports
  an edit. In the app, Clear then a click on another object went history 1 -> 2, and
  undo brought the cleared object back exactly. On a build without the call it went
  1 -> 1 and undo lost both.
- **The re-apply is a model-free job on the SAM worker** (`MaskSam::start_margin`),
  handed back through the same slot as a prompt. `sam_pump()` starts it only once no
  job runs, reading `busy()` before it takes a result, so a prompt in flight lands
  first and the margin re-applies to it. A margin that lands after any other edit is
  dropped, not stacked. In the app, a chair clicked on a fresh frame and the slider
  released at 11 % while the encode ran landed at 839,307 px (5 %), then 1,064,432 px,
  the same as a later re-apply to 11 %.

### Task 7: text prompts, the exception chips, and the refinement rules

**How the operator uses it.** In SAM mode (G), type what to drop into **Text prompt**, in
English, several phrases separated by `;` ("door; chair"), and press **Enter**. Every match
on the open frame is dropped as **one** undo step. **Common subjects** beside the field
opens the dataset screen's palette in a popup: its first group writes the phrase, its
second ("...but keep": person in a painting, statue, mannequin) writes exceptions, and an
exception's pixels are taken back out of every match. The phrase stays in the field across
frames and never runs on its own; Enter runs it on the frame that is open. With a SAM 2
checkpoint the field is disabled and the row says why; with no checkpoint it says to
fetch one first. A phrase that finds nothing says "The prompt matched nothing on this
frame." in the result slot.

**The margin now uses the model's box** (`AddRegion::box`, carried through `hold_region`).
A text detection's box is the detector's regressed box, not its mask's extent; with no box
the extent stands in, now measured inclusively as `sam::mask_bounding_box` does. The old
exclusive extent was a pixel wider and taller than the click path's `Detection::box`, so
"identical on the click path" was true to within that pixel, not exactly: on a 20 px square
at 30% it gives radius 3 where the box gives 2.

**The veto comes after the margin**, as `sam::compose_hit` orders it: the drop is grown
first, then every exception pixel is cleared. The brief put the veto before
`build_add_stencil`, which would have let the grown rim cover the exception again.

| check (M5 Pro, `sam3-q4_0`, 15520x7760, text path capped at 1600) | observed |
|---|---|
| CLI count, full size, frame 0066 | wall 4, door 3, window 2, person 1, person in a painting 1 |
| app, wall on frame 0067 (5 detections), 3 warm samples | job 689.2 / 596.6 / 592.9 ms, median **596.6**; prompt to painted 1342 / 1212 / 1209 ms |
| app, door on 0066 (3) / wall on 0066 (3) / person (1) | medians 306.4 / 305.2 / 263.1 ms |
| `segmentConcept` alone, from the log | 245-418 ms whatever the count: the decoder is ~170 ms of it, the segmentation head 0.3 ms |
| region size logged in the job (then removed) | text 1600x800 every time; a click 15520x7760 |
| person on 0066 without / with "person in a painting" / without again | 4,831,880 / 825,777 / 4,831,880 px |
| wall on 0067, 5 detections | history 0 -> 1; one Ctrl+Z back to the base count, 112,243,211 |
| Ctrl+click the person, then a plain click on it | kept 112,176,393 -> 113,395,324 -> 115,217,607, history 1, one undo back to the base |
| SAM 2.1 Tiny selected, Enter in the field | results 20 -> 20, no job, the row reads the reason |

**P2b's bar is 2386 ms**, 4x the worst median (596.6 ms, five detections). The detection
count does not dominate at the capped size: a detection is 1.28 MB there, so
`max_detections` is not needed. What grows with the count is the stencil built at the
document's size (~270 ms of the five-detection job) and the paint on the UI thread (about
620 ms for 25 M px over a frame-wide rectangle), which is the existing prompt-to-paint
cost, not a text-path one.

**Refinement follows one rule for both buttons** (`sam_click_mode`): Shift or Ctrl held
says the mode outright; a bare click on the object whose add is on top keeps that add's
mode; otherwise a bare click drops. A plain left click on a kept object used to replace the
keep with a drop.

**"On top" is now the step, not the revision.** `MaskDoc::top_step()` is a serial per
recorded step, so an undo and a redo put the same add back on top and a refinement replaces
it; an undo and a new edit in its place give a new serial, so it adds. A refinement that
changes no pixel now also clears the redo stack (`MaskDoc::drop_redo`), so the add it
replaced cannot come back by Redo.

**The strip.** The text row adds 34 px: `status_h` 406 with SAM 3, 436 with no cached
checkpoint, so SAM mode fits down to **570 px** of window height, **600 px** with no
checkpoint. Measured by dragging the window: canvas 84 px at 590, 69 at 575, 64 at 570, and
at 560 the key hint clips. The palette is a popup, not a section, so opening it does not
move the picture (canvas 444 px before, with it open, and after).

**`guictl text` could not replace a field's contents on macOS.** Its select-all sent the
physical Ctrl, which ImGui reads as Super there, so a second `text` appended. It now sends
the logical Ctrl, as `key` already did.

### Misses and open items

- **The P1b row is from model switches**, taken before closing the editor released the
  session (Task 8 below). Its samples split into 5.7-6.0 s and 10.5-11.1 s with no
  explanation, so no P1b threshold should be judged on them.
- **The automation layer read the app mid-frame.** `/ui/state` called
  `GuiApp::state_json()` on the HTTP thread; one read returned a new job time beside an
  old prompt time and a zeroed area. `Automation.cpp` now samples it at the end of each
  frame on the GUI thread and serves that.
- ImGui's `BeginCombo` reports no label to the automation hook, so `tree` cannot name
  the checkpoint combo; the harness finds it as the one unnamed on-screen item px(260)
  wide.

## SAM assist, Task 8: lifetime and teardown (2026-09-22)

**Closing the editor hands the checkpoint back.** `close()` yields SAM first (cancel,
join the job, `sam::Session::unload()`), then joins the load/save worker, so the weights
go back before a pending save lands. It then forgets all SAM state: the `MaskSam` object
with its clicks (their frame indices name a session that is gone), the model path, the
result counters, the replace stamp and the held detections. The model picker callback is
kept; `GuiApp::frame()` pushes the model again on the first frame of the next open. The
editor never builds a `sam::Tracker`, whose memory bank `unload()` does not release.

**P12** (`sam_pool_mib`, the process-wide pool; one fresh process, SAM 3 q4_0, a
15520x7760 frame, M5 Pro):

| stage | pool MiB | `sam_vram_mib` | OS: "Owned physical footprint (unmapped) (graphics)" |
|---|---|---|---|
| fresh, before open | 0.0 | -1 | 100 MB |
| one click, loaded and encoded | 1895.1 | 2407.1 | 2507 MB |
| 10 frames after **Done** | **0.0** | -1 | -- |
| 3 s after **Done** | 0.0 | -1 | **100 MB** |

The pool fell 1895.1 MiB (at least the 1652.2 MiB of weights) and ended equal to its
pre-open value, 0.0 against 0.0, inside the 1 MiB bar. On reopen the next click showed
`sam_first_load` in the strip and its job took 12,532 ms, above P1b's 5716 ms floor: the
upload was paid again. `session -1` after close is expected and proves nothing. Nothing
else allocated from the pool across the close: opening the editor closes the native
previews, and a run bars it.

**P12 catches both wrong releases, in the app.** Each was built, run in a fresh process,
and reverted:

| mutant | pool after close | drop >= weights? | equals pre-open? | reload |
|---|---|---|---|---|
| frees only `Weights`, leaks the session | 236.7 | yes (1658.5) | **no** | -- |
| `close()` neither yields nor forgets (the pre-Task 8 code) | 1895.1 | **no** | **no** | `Segmenting...`, 4014 ms: **no** upload |

The first shows why the equality bar matters: a weights-only release passes the "drop >=
weights" half.

**P6.** `sam_vram_mib` 2407.1 MiB loaded and encoded, under the 2500 MiB bar for q4_0:
PASS. Beside it, `ps -o rss=` read 1,605,280 KiB (1567.7 MiB). **`ps` does not measure
device memory**, so that figure is recorded but judged against nothing.

**P7**, Esc during a first click's encode on frame f1: idle 3303 ms after Esc (this
includes about 0.1 s of polling per round). Kept, history and results were all unchanged,
and nothing dropped.

**P8b**, leaving a frame mid-encode: `sam_dropped` 0 -> 1, `sam_results` 1 -> 1, and on the
next frame history 0 and kept 112,176,393, which equals its untouched count. `state` still
answered.

**Closing during a job freezes the UI for the whole stage.** `sam_close_ms` is the time
`close()` spends joining and unloading on the UI thread:

| Done clicked while | `sam_close_ms` | Done -> next frame, wall |
|---|---|---|
| idle, loaded | 152 | -- |
| an encode runs | **2887** | 3263 |
| the first load runs | **7805** | 8186 |

A load cannot be cancelled, and the stages check the flag only between them. **Proposed,
not built:** `close()` sets the cancel flag and moves the `MaskSam` into a GuiApp-owned
retiring slot, whose own thread joins and unloads. Until that slot is empty, every other
inference user would wait on it, as it does on `sam_yield()` today: a reopened editor,
`stop_inference_users()`'s callers, and shutdown before `nn::shutdown()`. P12 would then
read the pool once the slot empties rather than on the next frame.

**Carried fixes.**
- The cancel check after building the stencil now lives in one helper,
  `publish_unless_cancelled()`, which both job tails use. It builds, *then* reads the
  flag, then publishes. A unit test pins that an Esc during the build wins.
- A held detection now survives an undo while a redo can still bring its add back
  (`MaskDoc::redo_reaches`: the add's step is on top, or waiting on the redo stack), so
  the margin slider re-applies after an undo and a redo. An edit in the undone add's
  place drops the redo stack, and with it the detection.
- A text prompt whose matches the exception chips cleared entirely now reads
  `sam_vetoed_all` ("The prompt matched, but the exceptions removed everything.") instead
  of "matched nothing". The choice lives in `MaskSession::sam_empty_note()` so it can be
  unit-tested. The 12 translations borrow each language's existing word for the chips'
  exceptions, and no fluent speaker has read them.
- `RegionBox`'s comment now says what a text box is: the detector's continuous box, not
  an inclusive pixel extent.

## Not in this phase

Propagate, find-missing, slideshow, view modes and the peek key, session
persistence; vertex handles on a path; a Bezier path.
