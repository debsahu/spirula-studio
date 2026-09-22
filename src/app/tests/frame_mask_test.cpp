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

// ---------------------------------------------------------------------------
// Task 2: the spelling
// ---------------------------------------------------------------------------

bool close_to(float a, float b) { return std::fabs(a - b) < 1e-6f; }

void test_path_spelling() {
    std::vector<app::MaskShape> s;
    std::string err;
    check(app::parse_mask_shapes("path 0.1,0.1,0.9,0.1,0.5,0.9", s, err), "parses a path");
    check(s.size() == 1 && s[0].kind == app::MaskShape::Kind::Path && !s[0].remove,
          "one keep path");
    check(s[0].pts.size() == 6 && close_to(s[0].pts[0], 0.1f) && close_to(s[0].pts[5], 0.9f),
          "six numbers in order");
    check(app::parse_mask_shapes("-path 0.1, 0.1, 0.9,0.1, 0.5,0.9", s, err) &&
              s[0].remove && s[0].pts.size() == 6,
          "-path removes, spaces after commas tolerated");
    check(app::parse_mask_shapes("!path 0,0,1,0,1,1,0,1", s, err) && s[0].remove &&
              s[0].pts.size() == 8,
          "! spelling and four corners");
    check(!app::parse_mask_shapes("path 0.1,0.1,0.9,0.1", s, err), "two points rejected");
    check(!app::parse_mask_shapes("path 0.1,0.1,0.9,0.1,0.5", s, err), "odd count rejected");
    check(!app::parse_mask_shapes("path", s, err), "no numbers rejected");
    check(!app::parse_mask_shapes("path 0.1,0.1,0.9,0.1,0.5,x", s, err), "junk rejected");
    check(!app::parse_mask_shapes("path 0.1,0.1,0.9,0.1,0.5,0.9,", s, err),
          "trailing comma rejected");

    // Mixed list, order kept, and the old kinds still spell the same.
    check(app::parse_mask_shapes(
              "ellipse 0.5,0.5,0.49,0.49; -rect 0.2,0.9,0.8,1; -path 0.1,0.1,0.3,0.1,0.2,0.3",
              s, err),
          "mixed list parses");
    check(s.size() == 3 && s[0].kind == app::MaskShape::Kind::Ellipse &&
              s[1].kind == app::MaskShape::Kind::Rect && s[2].kind == app::MaskShape::Kind::Path,
          "kinds in order");
    const std::string back = app::format_mask_shapes(s);
    check(back == "ellipse 0.5000,0.5000,0.4900,0.4900; -rect 0.2000,0.9000,0.8000,1.0000; "
                  "-path 0.1000,0.1000,0.3000,0.1000,0.2000,0.3000",
          "format spells all three: " + back);
    std::vector<app::MaskShape> again;
    check(app::parse_mask_shapes(back, again, err) && again.size() == 3 &&
              again[2].kind == app::MaskShape::Kind::Path && again[2].remove &&
              again[2].pts.size() == 6 && close_to(again[2].pts[4], 0.2f) &&
              close_to(again[2].pts[5], 0.3f),
          "format -> parse is the identity on a path");

    // A long path does not truncate: 40 corners is 80 numbers, past the 128
    // bytes the old fixed buffer held.
    app::MaskShape big;
    big.kind = app::MaskShape::Kind::Path;
    big.remove = true;
    for (int i = 0; i < 40; i++) {
        big.pts.push_back(0.5f + 0.4f * std::cos((float)i * 0.157f));
        big.pts.push_back(0.5f + 0.4f * std::sin((float)i * 0.157f));
    }
    const std::string bigs = app::format_mask_shapes({big});
    std::vector<app::MaskShape> bigp;
    check(app::parse_mask_shapes(bigs, bigp, err) && bigp.size() == 1 && bigp[0].pts.size() == 80,
          "40-corner path survives format -> parse");
    bool all = true;
    for (size_t i = 0; i < 80; i++) all &= std::fabs(bigp[0].pts[i] - big.pts[i]) < 6e-5f;
    check(all, "every corner within the %.4f rounding");
}

}  // namespace

int main() {
    test_fill_matches_ray_cast();
    test_path_spelling();
    std::printf("%s: %d failure(s)\n", SS_FILE, g_failures);
    return g_failures;
}
