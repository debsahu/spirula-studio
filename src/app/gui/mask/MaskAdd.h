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

// Unions `regions` (consumed), each grown first by `margin` of its own extent
// (core/MaskMargin.h), into one W x H nearest-resampled stencil; `bounds` is its
// extent, `set_px` its count. False, outputs untouched, when nothing landed.
bool build_add_stencil(std::vector<AddRegion>& regions, int W, int H,
                       Stencil& out, Rect& bounds, int64_t& set_px, float margin = 0.0f);

// A detection cut down to its set pixels' extent `box`, so the last add can be
// rebuilt at another margin without keeping the whole plane.
struct HeldRegion {
    int w = 0, h = 0;
    Rect box;
    std::vector<uint8_t> mask;   // box.w() x box.h()
};
HeldRegion hold_region(const AddRegion& g);
AddRegion expand_region(const HeldRegion& held);

// The margin grows what is thrown away, as on the dataset screen: a drop takes
// the editor's ratio, a keep or a clear SAM's exact outline. Never signed.
inline float drop_margin(Paint mode, float ratio) {
    return mode == Paint::ForceDrop && ratio > 0.0f ? ratio : 0.0f;
}

}  // namespace mask
}  // namespace gui
