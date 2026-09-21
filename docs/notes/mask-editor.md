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
| (pending) | | |

## Not in this phase

Path shape, livewire, propagate, find-missing, slideshow, view modes and the
peek key, session persistence.
