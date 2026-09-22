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

// The smallest destination index whose nearest() source index reaches `src`.
int first_dst(int src, int src_n, int dst_n) {
    return (int)std::min<int64_t>(dst_n, ((int64_t)src * dst_n + src_n - 1) / src_n);
}

// The extent of the set pixels of a `w`-wide plane inside `within`, and their count.
Rect extent(const std::vector<uint8_t>& p, int w, const Rect& within, int64_t& n) {
    Rect r{within.x1, within.y1, within.x0, within.y0};
    n = 0;
    for (int y = within.y0; y < within.y1; y++) {
        const uint8_t* row = p.data() + (size_t)y * (size_t)w;
        for (int x = within.x0; x < within.x1; x++) {
            if (!row[x]) continue;
            n++;
            r.x0 = std::min(r.x0, x);
            r.x1 = std::max(r.x1, x + 1);
            r.y0 = std::min(r.y0, y);
            r.y1 = std::max(r.y1, y + 1);
        }
    }
    return r.empty() ? Rect{} : r;
}

}  // namespace

// Filling and the final scan cover only the destination box each region can
// reach, not the whole W x H plane; this runs on the SAM job thread.
bool build_add_stencil(std::vector<AddRegion>& regions, int W, int H,
                       Stencil& out, Rect& bounds, int64_t& set_px) {
    if (W <= 0 || H <= 0) return false;
    std::vector<uint8_t> plane;
    bool have = false;
    Rect box{W, H, 0, 0};
    std::vector<int> xs;
    for (AddRegion& g : regions) {
        if (g.w <= 0 || g.h <= 0 || g.mask.size() != (size_t)g.w * (size_t)g.h) continue;
        int64_t n = 0;
        const Rect s = extent(g.mask, g.w, Rect{0, 0, g.w, g.h}, n);
        if (s.empty()) continue;
        const Rect d{first_dst(s.x0, g.w, W), first_dst(s.y0, g.h, H),
                     first_dst(s.x1, g.w, W), first_dst(s.y1, g.h, H)};
        box = Rect{std::min(box.x0, d.x0), std::min(box.y0, d.y0),
                   std::max(box.x1, d.x1), std::max(box.y1, d.y1)};
        if (!have && g.w == W && g.h == H && regions.size() == 1) {
            plane = std::move(g.mask);
            have = true;
            continue;
        }
        if (!have) {
            plane.assign((size_t)W * (size_t)H, 0);
            have = true;
        }
        xs.resize((size_t)std::max(0, d.w()));
        for (int x = d.x0; x < d.x1; x++) xs[(size_t)(x - d.x0)] = nearest(x, W, g.w);
        for (int y = d.y0; y < d.y1; y++) {
            const uint8_t* src = g.mask.data() + (size_t)nearest(y, H, g.h) * (size_t)g.w;
            uint8_t* dst = plane.data() + (size_t)y * (size_t)W;
            for (int x = d.x0; x < d.x1; x++)
                if (src[xs[(size_t)(x - d.x0)]]) dst[x] = 255;
        }
    }
    if (!have || box.empty()) return false;
    int64_t n = 0;
    const Rect r = extent(plane, W, box, n);
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
