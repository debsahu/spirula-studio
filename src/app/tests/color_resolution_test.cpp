// color_resolution -- what resolve_color() makes of `--image-color-log` and
// `--point-color-log`, and what the seed colours become under it. A wrong
// answer here trains without erroring: grey read as linear seeds 1.15 stops
// bright, and a skipped linear flag decodes the log image through sRGB too.

#include "app/TrainerCore.h"
#include "core/DlogM.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace spirula;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) g_failures++;
}

bool throws(const TrainConfig& c) {
    try { resolve_color(c); } catch (const std::exception&) { return true; }
    return false;
}

TrainConfig dlogm() {
    TrainConfig c;
    c.image_color_log = "dlogm-osmo360";
    return c;
}

// First seed's display-independent colour, back out of the SH DC.
std::array<float, 3> seed_color(const TrainConfig& c, uint8_t r, uint8_t g, uint8_t b) {
    ColmapPoints3D pts;
    for (int i = 0; i < 8; i++) {
        pts.xyz.insert(pts.xyz.end(), {(double)i, (double)(i % 3), (double)(i % 2)});
        pts.rgb.insert(pts.rgb.end(), {r, g, b});
    }
    // One point a different colour, or seed_splats randomizes a uniform cloud.
    pts.rgb[21] = (uint8_t)(pts.rgb[21] ^ 1);
    TrainConfig cc = c;
    cc.cap_max = 8;
    const SeedSplats s = seed_splats(pts, cc, resolve_color(cc));
    std::array<float, 3> out{};
    for (int d = 0; d < 3; d++) out[d] = s.features_dc[d] * 0.28209479177387814f + 0.5f;
    return out;
}

void test_image_side() {
    const ColorResolution r = resolve_color(dlogm());
    check(r.image_curve == colorspace::InputCurve::DlogMOsmo360,
          "image: dlogm-osmo360 selects the D-Log M decode");
    check(r.image_linear, "image: the decode's output is linear, so the image side is");
    check(r.image_gamut == "Rec.2020", "image: an unset gamut resolves to Rec.2020");
    check(r.image_on(), "image: the GT conversion pass runs");
    check(r.splat_linear && r.splat_gamut == "Rec.2020",
          "splat: follows the decoded image side (linear Rec.2020)");

    TrainConfig c = dlogm();
    c.image_color_is_linear = false;
    check(throws(c), "image: dlogm with --image-color-is-linear off is refused");
    c = dlogm();
    c.image_color_gamut = "ACEScg";
    check(throws(c), "image: dlogm with a gamut other than Rec.2020 is refused");
    c = dlogm();
    c.image_color_gamut = "Rec.2020";
    c.image_color_is_linear = true;
    check(!throws(c), "image: dlogm with Rec.2020 / linear spelled out is accepted");

    TrainConfig none;
    none.image_color_log = "none";
    const ColorResolution n = resolve_color(none);
    check(n.image_curve == colorspace::InputCurve::None && !n.image_on() &&
              n.image_gamut.empty() && !n.image_linear,
          "image: `none` means no decode and changes nothing else");
}

void test_point_side() {
    const ColorResolution r = resolve_color(dlogm());
    check(r.point_curve == colorspace::InputCurve::DlogMOsmo360,
          "point: unset follows the images (SfM sampled the log frames)");
    check(!r.point_is_splat(), "point: a log seed is never already in splat space");

    TrainConfig c = dlogm();
    c.point_color_log = "off";
    const ColorResolution o = resolve_color(c);
    check(o.point_curve == colorspace::InputCurve::None && o.point_is_splat(),
          "point: `off` leaves the seeds undecoded");

    c = dlogm();
    c.point_color_is_linear = false;
    check(throws(c), "point: a log seed with --point-color-is-linear off is refused");
}

void test_seeds() {
    // 102/255 is code 0.4 exactly: D-Log M mid grey.
    const auto grey = seed_color(dlogm(), 102, 102, 102);
    check(std::fabs(grey[0] - 0.18f) < 1e-4f && std::fabs(grey[1] - 0.18f) < 1e-4f &&
              std::fabs(grey[2] - 0.18f) < 1e-4f,
          "seed: code 0.4 grey lands at linear 0.18 in the splats");

    float want[3] = {153 / 255.0f, 51 / 255.0f, 102 / 255.0f};
    colorspace::dlogm_osmo360_to_rec2020(want);
    const auto sat = seed_color(dlogm(), 153, 51, 102);
    check(std::fabs(sat[0] - want[0]) < 1e-4f && std::fabs(sat[1] - want[1]) < 1e-4f &&
              std::fabs(sat[2] - want[2]) < 1e-4f,
          "seed: a saturated code goes through the Osmo matrix");

    TrainConfig c = dlogm();
    c.point_color_log = "off";
    const auto raw = seed_color(c, 102, 102, 102);
    check(std::fabs(raw[0] - 0.4f) < 1e-4f, "seed: `off` keeps the stored value");
}

}  // namespace

int main() {
    test_image_side();
    test_point_side();
    test_seeds();
    std::printf("%s\n", g_failures ? "FAILED" : "all ok");
    return g_failures ? 1 : 0;
}
