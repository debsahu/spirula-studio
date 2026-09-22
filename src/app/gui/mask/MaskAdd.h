#pragma once

// One SAM prompt's detections -> one document-resolution stencil and the
// rectangle it covers, as MaskDoc::paint takes them. No ImGui, no GL, no sam::,
// no nn:: -- this half is what mask_doc_test compiles.
// Design: docs/notes/mask-editor.md.

#include "app/gui/mask/MaskDoc.h"

#include <cstdint>
#include <vector>

namespace gui {
namespace mask {

// A detection as the seam takes it: a 0/255 plane at the size the model was
// handed. No box: the bounds come from the plane, so they cannot disagree.
struct AddRegion {
    int w = 0, h = 0;
    std::vector<uint8_t> mask;
    float score = 0.0f;
};

// Unions `regions` (consumed) into one W x H document-pixel stencil, nearest-
// neighbour where sizes differ; `bounds` is its extent, `set_px` its count.
// False, with the three outputs untouched, when nothing landed.
bool build_add_stencil(std::vector<AddRegion>& regions, int W, int H,
                       Stencil& out, Rect& bounds, int64_t& set_px);

}  // namespace mask
}  // namespace gui
