// The D-Log M ground-truth decode on the device, against the host mirror the
// mean-luma features read (_engine_color_space_gt_pixel) and the host curve
// (core/DlogM.h). A decode never dispatched, run after the display encode, or
// with the matrix multiplied from the wrong side trains without erroring.
// Self-checking, either backend:
//
//   ./gt_decode_dlogm

#include <core/ColorSpace.h>
#include <core/DlogM.h>
#include <engine/Engine.h>
#include <engine/EngineInternal.h>
#include <engine/EngineState.h>

#include <cmath>
#include <cstdio>
#include <vector>

using backend::MemcpyKind;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) g_failures++;
}

static TorchTensorView ttv(const void* p, uint32_t elem, std::vector<int64_t> shape) {
    return std::make_tuple((uint64_t)p, elem, std::move(shape));
}
static TorchTensorView ttv_null() {
    return std::make_tuple((uint64_t)0, 4u, std::vector<int64_t>{0});
}

int main() {
    // Grey at the anchors, both sides of the cut, then saturated triples: grey
    // cannot tell mul(M, v) from mul(v, M), a saturated code can.
    const float codes[][3] = {
        {0.0f, 0.0f, 0.0f},    {0.05f, 0.05f, 0.05f}, {0.10f, 0.10f, 0.10f},
        {0.40f, 0.40f, 0.40f}, {0.714f, 0.714f, 0.714f}, {1.0f, 1.0f, 1.0f},
        {0.6f, 0.3f, 0.2f},    {0.2f, 0.6f, 0.3f},    {0.3f, 0.2f, 0.6f},
        {0.9f, 0.15f, 0.5f},
    };
    const int W = 10, H = 1, C = 1;
    std::vector<uint16_t> gt((size_t)W * 3);
    for (int i = 0; i < W; i++)
        for (int k = 0; k < 3; k++)
            gt[(size_t)i * 3 + k] = (uint16_t)std::lround(codes[i][k] * 65535.0f);

    const colorspace::Mat3 m = colorspace::gamut_to_rec709("Rec.2020");
    engine_init_color_space(false, 0, false, {}, true,
                            (int)colorspace::Transfer::Srgb, true,
                            std::vector<float>(m.begin(), m.end()));
    engine_init_image_decode((int)colorspace::InputCurve::DlogMOsmo360);
    set_training_data(ttv(gt.data(), 2, {C, H, W, 3}), ttv_null(), ttv_null(),
                      ttv_null(), true);
    backend::device_synchronize();

    std::vector<float> got((size_t)W * 3);
    backend::memcpy_sync(got.data(), engine().gt.rgb.data_ptr(), got.size() * sizeof(float),
                         MemcpyKind::DeviceToHost);
    if (const char* err = backend::last_error()) {
        std::fprintf(stderr, "backend error: %s\n", err);
        return 1;
    }

    // What the device must match: the curve, the Osmo matrix, Rec.2020->709,
    // then the open sRGB encode -- written out here, not via the mirror.
    double worst_ref = 0.0, worst_mirror = 0.0;
    for (int i = 0; i < W; i++) {
        float want[3], mirror[3];
        for (int k = 0; k < 3; k++) want[k] = mirror[k] = gt[(size_t)i * 3 + k] / 65535.0f;
        colorspace::dlogm_osmo360_to_rec2020(want);
        colorspace::apply3x3(m, want);
        for (int k = 0; k < 3; k++)
            want[k] = colorspace::tone_encode(want[k], colorspace::Transfer::Srgb);
        _engine_color_space_gt_pixel(mirror);
        for (int k = 0; k < 3; k++) {
            const float g = got[(size_t)i * 3 + k];
            worst_ref = std::max(worst_ref, (double)std::fabs(g - want[k]));
            worst_mirror = std::max(worst_mirror, (double)std::fabs(g - mirror[k]));
        }
    }
    std::printf("max |device - reference| = %.3g, |device - mirror| = %.3g\n",
                worst_ref, worst_mirror);
    check(worst_ref < 1e-5, "device: decode -> Rec.709 -> sRGB matches the host curve");
    check(worst_mirror < 1e-5, "mirror: the mean-luma host copy matches the device");
    // Code 0.4 is mid grey: 0.18 linear, 0.4614 once sRGB-encoded.
    check(std::fabs(got[3 * 3] - 0.461356f) < 1e-4f, "device: code 0.4 grey -> display 0.4614");
    // The open sRGB encode lets decoded highlights through above 1.
    check(std::fabs(got[5 * 3] - 1.777905f) < 1e-4f, "device: code 1.0 -> display 1.7779");

    engine_reset();
    check(engine().color_space.image_curve == 0, "reset: the decode does not outlive the run");

    // The decode hands the conversion linear Rec.2020; a display-encoded image
    // side would push it through the sRGB EOTF a second time.
    engine_init_color_space(false, 0, false, {}, true, 0, false,
                            std::vector<float>(m.begin(), m.end()));
    bool refused = false;
    try {
        engine_init_image_decode((int)colorspace::InputCurve::DlogMOsmo360);
    } catch (const std::exception&) { refused = true; }
    check(refused && engine().color_space.image_curve == 0,
          "guard: the decode is refused on a display-encoded image side");
    engine_reset();
    std::printf("%s\n", g_failures ? "FAILED" : "all ok");
    return g_failures ? 1 : 0;
}
