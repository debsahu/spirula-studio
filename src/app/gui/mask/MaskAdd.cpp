// MaskAdd.cpp -- see MaskAdd.h.

#include "app/gui/mask/MaskAdd.h"

#include <algorithm>

namespace gui {
namespace mask {

namespace {

// Nearest, by sam::Masker's own upscale_nearest mapping (floor(d * src / dst)),
// so a region lands on the pixels the model's overlay drew rather than one over.
int nearest(int dst, int dst_n, int src_n) {
    if (dst_n <= 0 || src_n <= 0) return 0;
    return std::min(src_n - 1, (int)((int64_t)dst * src_n / dst_n));
}

}  // namespace

bool build_add_stencil(std::vector<AddRegion>& regions, int W, int H,
                       Stencil& out, Rect& bounds, int64_t& set_px) {
    if (W <= 0 || H <= 0) return false;
    std::vector<uint8_t> plane;
    bool have = false;
    for (AddRegion& g : regions) {
        if (g.w <= 0 || g.h <= 0 || g.mask.size() != (size_t)g.w * (size_t)g.h) continue;
        if (!have && g.w == W && g.h == H && regions.size() == 1) {
            plane = std::move(g.mask);
            have = true;
            continue;
        }
        if (!have) {
            plane.assign((size_t)W * (size_t)H, 0);
            have = true;
        }
        for (int y = 0; y < H; y++) {
            const size_t src = (size_t)nearest(y, H, g.h) * (size_t)g.w;
            const size_t dst = (size_t)y * (size_t)W;
            for (int x = 0; x < W; x++)
                if (g.mask[src + (size_t)nearest(x, W, g.w)]) plane[dst + (size_t)x] = 255;
        }
    }
    if (!have) return false;
    Rect r{W, H, 0, 0};
    int64_t n = 0;
    for (int y = 0; y < H; y++) {
        const size_t row = (size_t)y * (size_t)W;
        for (int x = 0; x < W; x++) {
            if (!plane[row + (size_t)x]) continue;
            n++;
            r.x0 = std::min(r.x0, x);
            r.y0 = std::min(r.y0, y);
            r.x1 = std::max(r.x1, x + 1);
            r.y1 = std::max(r.y1, y + 1);
        }
    }
    if (r.empty()) return false;
    out.W = W;
    out.H = H;
    out.in = std::move(plane);
    bounds = r;
    set_px = n;
    return true;
}

}  // namespace mask
}  // namespace gui
