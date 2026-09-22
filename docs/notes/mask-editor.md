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
`mask_edits/` directory survives in the fixture root).

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

### Criterion #4 in the app (Step 3)

Brush grown to 106 px via nine `]` presses from the 24 px default
(`step_brush`'s 1.18x per step: 24 -> 28 -> ... -> 106.4, rounds to 106; the
brief's example values of 96/113 px do not fall out of this stepping, so 106
is recorded as the nearest step actually reached). Zoomed to ~8x about the
canvas center. 20 horizontal brush strokes, each ~500 mask px long (~761
screen px at this zoom), reading "Last stroke: N ms" from the status strip
after each:

11.8, 11.4, 8.0, 43.1, 20.3, 8.8, 4.6, 13.6, 42.4, 48.4, 18.8, 5.0, 5.1, 11.8,
29.4, 20.3, 36.6, 31.7, 5.0, 5.0 (ms)

**Median 12.7 ms, max 48.4 ms. PASS** (bar: median <= 100 ms, max <= 250 ms;
margin ~8x on the median, ~5x on the max -- smaller than the CPU-only margin
above because this number also includes `glTexSubImage2D`, but not GPU
completion; see the note already on file about what this number is CPU time
through). Real variance across the 20 strokes (4.6-48.4 ms) rather than a flat
line -- consistent with genuine per-stroke GL upload cost on a machine under
load (see the memory section below for how loaded), not a stub.

### Criterion #9 in the app (Step 4)

`ps -o rss= -p $(pgrep -n spirula)`, editor closed vs. one 8K frame open plus
one painted stroke, three independent open/paint/close cycles on the same
process (the brief's own script is a single sample each; repeated here
because the first cycle's reading proved volatile -- see below):

| trial | baseline | editor open | delta |
|---|---|---|---|
| 1 | 309.2 MB | 520.7 MB | 211.6 MB |
| 2 | 462.5 MB | 575.8 MB | 113.3 MB |
| 3 | 443.0 MB | 575.3 MB | 132.3 MB |

**Median delta 132.3 MB, max 211.6 MB. PASS** in every trial (bar <= 600 MB),
with margin to spare even at the worst observed delta.

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
  driven this session; both share the identical `EditTool` commit path as
  Box (spec's own "reused as is"), so this is an inference from Box's result
  and from code, not a separate run.
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

## Not in this phase

Path shape, livewire, propagate, find-missing, slideshow, view modes and the
peek key, session persistence.
