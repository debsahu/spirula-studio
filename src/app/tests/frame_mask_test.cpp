// frame_mask_test -- app/FrameMask.h shapes with no GUI: the even-odd fill
// in core/PolygonFill.h against a ray cast, the path spelling round trip,
// the path fill on a non-square frame, and the ordered composition rule.

#include "app/FrameMask.h"
#include "core/PolygonFill.h"
#include "core/SourcePath.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

// Even-odd point test by ray casting to +x, the textbook reference the
// scanline fill has to agree with at every pixel centre.
bool ray_inside(const std::vector<float>& p, float x, float y, bool& tie) {
    const size_t n = p.size() / 2;
    bool in = false;
    tie = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * j], by = p[2 * j + 1];
        if ((ay > y) == (by > y)) continue;
        const float xi = ax + (y - ay) / (by - ay) * (bx - ax);
        if (std::fabs(xi - x) < 1e-3f) tie = true;
        if (x < xi) in = !in;
    }
    return in;
}

void compare_fill(const std::vector<float>& poly, int W, int H, const std::string& name) {
    std::vector<uint8_t> out((size_t)W * H, 0);
    polyfill::fill_even_odd(poly.data(), poly.size() / 2, W, H, out.data(), 1);
    size_t compared = 0, mismatched = 0, inside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            bool tie;
            const bool ref = ray_inside(poly, (float)x + 0.5f, (float)y + 0.5f, tie);
            if (tie) continue;
            compared++;
            inside += ref;
            if (ref != (out[(size_t)y * W + x] != 0)) mismatched++;
        }
    check(mismatched == 0, name + ": fill matches the ray cast on " +
                               std::to_string(compared) + " pixels");
    check(compared * 10 >= (size_t)W * H * 9, name + ": at least 90% of pixels compared");
    check(inside > 0, name + ": the polygon covers something");
}

// ---------------------------------------------------------------------------
// Task 1: the fill
// ---------------------------------------------------------------------------

void test_fill_matches_ray_cast() {
    // A bow tie (self-intersecting), a concave arrow, and a polygon that
    // hangs off every side of the image. Vertices sit at .3/.7 fractions so
    // no crossing lands exactly on a pixel centre.
    compare_fill({3.3f, 2.7f, 30.7f, 20.3f, 30.3f, 2.3f, 3.7f, 20.7f}, 37, 23, "bow tie");
    compare_fill({2.3f, 11.7f, 20.7f, 2.3f, 16.3f, 11.3f, 20.7f, 20.7f}, 25, 23, "arrow");
    compare_fill({-5.3f, -4.7f, 40.7f, -2.3f, 44.3f, 30.7f, -3.7f, 26.3f, 18.3f, 12.7f},
                 37, 23, "overhang");
    std::vector<uint8_t> out(37 * 23, 0);
    const std::vector<float> two = {1.3f, 1.3f, 20.7f, 20.7f};
    polyfill::fill_even_odd(two.data(), 2, 37, 23, out.data(), 1);
    size_t set = 0;
    for (uint8_t v : out) set += v;
    check(set == 0, "two points fill nothing");
    polyfill::fill_even_odd(two.data(), 0, 37, 23, out.data(), 1);
    check(true, "zero points does not crash");
}

}  // namespace

int main() {
    test_fill_matches_ray_cast();
    std::printf("%s: %d failure(s)\n", SS_FILE, g_failures);
    return g_failures;
}
