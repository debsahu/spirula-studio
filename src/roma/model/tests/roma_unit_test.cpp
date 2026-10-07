// RoMa v2's host-side conversions, with no checkpoint and no GPU: the
// resize RoMaV2.match() applies, the RoPE tables on a non-square grid, and the
// load-time qkv bias fold and row permutation. Each case names the mistake it
// catches; the goldens are torch 2.14.1 on the same bytes
// (tools/roma/make_unit_goldens.py regenerates them).

#include "roma/Roma.h"
#include "roma/model/Dump.h"
#include "roma/model/RomaMatcher.h"
#include "roma/model/Rope.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#define SS_SETENV(k, v) _putenv_s(k, v)
#else
#define SS_SETENV(k, v) setenv(k, v, 1)
#endif

using namespace roma;

namespace {

int g_failures = 0, g_checks = 0;

void check(bool ok, const char* name, const char* fmt, ...) {
    ++g_checks;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::printf("  %s %s: %s\n", ok ? "ok  " : "FAIL", name, buf);
    if (!ok) ++g_failures;
}

// The bytes the goldens were made from.
std::vector<uint8_t> lcg(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed;
    for (uint8_t& b : v) {
        x = (x * 1103515245u + 12345u) & 0x7fffffffu;
        b = (uint8_t)((x >> 16) & 255u);
    }
    return v;
}

// F.interpolate(bicubic, antialias=True, align_corners=False) in torch 2.14.1.
const float kDown[] = {0.472005039f, 0.315563411f, 0.483953983f, 0.429525316f, 0.417998701f, 0.422284156f, 0.453993529f, 0.486178368f, 0.538304985f, 0.532999456f, 0.478147835f, 0.433438361f, 0.511934817f, 0.55380249f, 0.543749392f, 0.619546294f, 0.561426759f, 0.672623098f, 0.612130582f, 0.633291125f, 0.465429544f, 0.570728183f, 0.52629137f, 0.345308185f, 0.507728159f, 0.514107406f, 0.500409901f, 0.504689872f, 0.482896596f, 0.583858669f, 0.380548358f, 0.553184867f, 0.49806878f, 0.638287008f, 0.607527375f, 0.521915555f, 0.444382578f, 0.523248732f, 0.559764147f, 0.610876381f, 0.606474936f, 0.295081705f, 0.359400511f, 0.327445328f, 0.525827289f, 0.540376723f, 0.523149848f, 0.452610552f, 0.629442275f, 0.469842136f, 0.571510315f, 0.421039015f, 0.553382576f, 0.578337669f, 0.568305612f, 0.638944268f, 0.478413135f, 0.635044038f, 0.553468883f, 0.56695956f};   // 13x11 -> 5x4, seed 7
const float kUp[] = {0.478870362f, 0.885404825f, 0.432983965f, 0.523101628f, 0.78842324f, 0.509331226f, 0.620007277f, 0.611048937f, 0.628532529f, 0.842523992f, 0.629968643f, 0.370207161f, 1.00468016f, 0.689532399f, 0.0516682416f, 0.54357338f, 0.440352887f, 0.429018468f, 0.055761762f, 0.155655876f, 0.910446882f, 0.13420403f, 0.0135465702f, 1.04015243f, 0.225816205f, 0.0958014429f, 0.971388161f, 0.13091372f, 0.530881047f, 0.666637182f, 0.076533407f, 0.766676545f, 0.500846326f, 0.620773852f, 0.794505298f, 0.309797287f, 0.660781443f, 0.746727169f, 0.372907966f, 0.741306961f, 0.653054357f, 0.481631875f, 0.865110278f, 0.598704636f, 0.384191394f, 0.912725806f, 0.537905931f, 0.259401292f, 0.474611223f, 0.355688035f, 0.54598546f, 0.0534405522f, 0.171408042f, 0.858074605f, 0.193597302f, 0.0583070144f, 0.871564507f, 0.296830744f, 0.114336245f, 0.745834827f, 0.120417692f, 0.446088582f, 0.477572769f, 0.0205146037f, 0.626113832f, 0.335307896f, 0.862085223f, 0.651151419f, 0.0919797122f, 0.891875446f, 0.679624975f, 0.132027388f, 0.937863827f, 0.715358734f, 0.222021028f, 0.879050851f, 0.542298198f, 0.400891095f, 0.719781339f, 0.291748494f, 0.60641402f, 0.342079639f, 0.232491985f, 0.720881641f, 0.0598536953f, 0.222519591f, 0.727737844f, 0.308176816f, 0.152102426f, 0.544918001f, 0.432729363f, 0.153245375f, 0.330342144f, 0.124415122f, 0.296754718f, 0.13723591f, -0.0494122282f, 0.375555903f, 0.0418464728f, 0.787940621f, 0.861908495f, 0.0472932979f, 0.780778229f, 0.774711788f, 0.0962884203f, 0.75392139f, 0.607580125f, 0.194607854f, 0.616655469f, 0.519419312f, 0.294708997f, 0.423508465f, 0.479366213f, 0.37697807f, 0.236617386f, 0.48075366f, 0.377862781f, 0.158621296f, 0.454675376f, 0.329437912f, 0.406394005f, 0.279279441f, 0.239820331f, 0.541244924f, 0.185918674f, 0.143710718f, 0.340402663f, 0.294445604f, 0.0541158468f, 0.224058807f, 0.358729839f, 0.00993237458f, 0.588174284f, 1.04831386f, 0.165121675f, 0.566172719f, 0.835238874f, 0.219781712f, 0.511782885f, 0.45398587f, 0.311964124f, 0.352126628f, 0.538622379f, 0.234026998f, 0.174840719f, 0.815300643f, 0.0821001083f, 0.173774526f, 0.806372583f, 0.0208333321f, 0.26898551f, 0.67260021f, 0.0134966066f, 0.461768389f, 0.414916933f, 0.0810464099f, 0.59463954f, 0.263446778f, 0.13495189f, 0.576207578f, 0.35886091f, 0.13028881f, 0.559163034f, 0.419192016f, 0.125165939f, 0.478906691f, 0.460536003f, 0.649986565f, 0.591387928f, 0.434312046f, 0.619835377f, 0.780482173f, 0.408912838f, 0.565934837f, 0.609038174f, 0.678297698f, 0.582788467f, 0.290659606f, 0.996984184f, 0.620093882f, 0.210475326f, 0.769858241f, 0.574533343f, 0.244576171f, 0.46707046f, 0.507977426f, 0.390988469f, 0.502691627f, 0.466134012f, 0.499751776f, 0.521782458f, 0.415115476f, 0.486595213f, 0.393571913f, 0.338464051f, 0.473980904f, 0.323585004f, 0.299258858f, 0.40543139f, 0.0701722652f, 0.873456955f, 0.584430397f, 0.202699468f, 0.833219469f, 0.899112523f, 0.466557503f, 0.766808331f, 0.783081949f, 0.73525542f, 0.842840314f, 0.463564008f, 0.916795731f, 0.973116815f, 0.306903899f, 0.646436453f, 1.0052619f, 0.238471583f, 0.373976052f, 0.956983387f, 0.288192987f, 0.600339413f, 0.773960292f, 0.361087769f, 0.723605871f, 0.616594434f, 0.41953969f, 0.478626251f, 0.560190201f, 0.448439866f, 0.339361817f, 0.537225366f, 0.370187402f, 0.521813154f, 0.323495418f, 0.387115121f, 0.584229946f, 0.482059538f, 0.423279703f, 0.686845601f, 0.765683115f, 0.490143716f, 0.545499027f, 0.688571632f, 0.5441944f, 0.348898441f, 0.488993287f, 0.468450099f, 0.53238672f, 0.678312898f, 0.347792238f, 0.761074543f, 0.852312088f, 0.193210468f, 0.753625572f, 0.596324861f, 0.218222588f, 0.709096313f, 0.466147363f, 0.583308995f, 0.680553913f, 0.720237136f, 0.782786548f, 0.666133046f, 0.865278661f, 0.355732977f, 0.80050534f, -0.00227681687f, 0.276875019f, 0.814204514f, 0.269511491f, 0.147767186f, 0.808992743f, 0.75225234f, 0.31510976f, 0.434440672f, 0.585644007f, 0.580904305f, 0.0302362461f, 0.194690362f, 0.554537177f, 0.473466694f, 0.468784183f, 0.409443885f, 0.984744966f, 0.767543495f, 0.145277679f, 0.833647072f, 0.47803548f, 0.145350978f, 0.688072979f, 0.368925065f, 0.678790808f, 0.789473116f, 0.797135592f, 0.971832097f, 0.850096226f, 1.03610694f};     // 6x5 -> 11x9, seed 11

// Catches: torch's non-antialiased a = -0.75, an unscaled support on the
// downscale, a pixel-centre slip, the two axes swapped.
void test_resize() {
    std::printf("\nresize_rgb vs torch\n");
    struct Case { int w, h, ow, oh; uint32_t seed; const float* want; size_t n; const char* name; };
    const Case cases[] = {{13, 11, 5, 4, 7, kDown, sizeof kDown / 4, "resize_down"},
                          {6, 5, 11, 9, 11, kUp, sizeof kUp / 4, "resize_up"}};
    for (const Case& c : cases) {
        const std::vector<uint8_t> in = lcg((size_t)c.w * c.h * 3, c.seed);
        const std::vector<float> got = resize_rgb(in.data(), c.w, c.h, c.ow, c.oh);
        double e = got.size() == c.n ? 0 : 1e9;
        for (size_t i = 0; i < std::min(got.size(), c.n); ++i)
            e = std::max(e, (double)std::fabs(got[i] - c.want[i]));
        check(e < 2e-6, c.name, "%dx%d -> %dx%d, max |ours - torch| %.2e (bar 2e-6)", c.w,
              c.h, c.ow, c.oh, e);
    }
}

std::vector<float> periods16() {
    std::vector<float> p(16);
    for (int k = 0; k < 16; ++k) p[(size_t)k] = to_bf16((float)std::pow(100.0, k / 16.0));
    return p;
}

// On H != W: pairs 0..15 rotate by the row coordinate over H, 16..31 by the
// column coordinate over W. Catches the axes or the extents swapped, which a
// square grid cannot see.
void test_rope_nonsquare() {
    std::printf("\nrope tables, 3 x 7 grid\n");
    const int64_t H = 3, W = 7;
    const std::vector<float> p = periods16();
    const std::vector<float> f = backbone_rope(p, H, W), b = matcher_rope_bf16(p, H, W);
    double ef = 0, eb = 0;
    for (int64_t y = 0; y < H; ++y)
        for (int64_t x = 0; x < W; ++x)
            for (int k = 0; k < 32; ++k) {
                const double c = k < 16 ? ((y + 0.5) / H) * 2 - 1 : ((x + 0.5) / W) * 2 - 1;
                const double a = 2 * M_PI * c / p[(size_t)(k % 16)];
                const size_t i = (size_t)(((y * W + x) * 32 + k) * 2);
                ef = std::max({ef, std::fabs(f[i] - std::cos(a)), std::fabs(f[i + 1] - std::sin(a))});
                eb = std::max({eb, std::fabs(b[i] - std::cos(a)), std::fabs(b[i + 1] - std::sin(a))});
            }
    check(ef < 1e-6, "rope_nonsquare_fp32", "max |table - closed form| %.2e", ef);
    // bf16 rounds the coordinate, the angle and the result: a few 1e-2 at most.
    check(eb < 5e-2, "rope_nonsquare_bf16", "max |table - closed form| %.2e", eb);
}

// The fold multiplies by the mask, then permutes q and k only. Catches: the
// mask skipped, applied after the permutation, V permuted too.
void test_qkv_fold() {
    std::printf("\nqkv bias fold and row permutation\n");
    const int heads = 2, hd = 8;
    const int64_t W = heads * hd, cols = 3;
    std::vector<float> bias((size_t)(3 * W)), mask((size_t)(3 * W)), rows((size_t)(3 * W * cols));
    for (int64_t i = 0; i < 3 * W; ++i) {
        bias[(size_t)i] = 1.0f + (float)i;
        mask[(size_t)i] = (i >= W && i < 2 * W) ? 0.0f : (float)(i % 3);   // K masked, Q/V varied
    }
    for (size_t i = 0; i < rows.size(); ++i) rows[i] = (float)i;
    const std::vector<float> fb = fold_qkv_bias(bias, mask, W, heads);
    const std::vector<float> pr = permute_qk_rows(rows, W, heads, cols);
    bool bias_ok = fb.size() == bias.size(), rows_ok = pr.size() == rows.size();
    for (int part = 0; part < 3; ++part)
        for (int h = 0; h < heads; ++h)
            for (int d = 0; d < hd; ++d) {
                // Rotate-half element d of a head lands at 2d (d < hd/2) or 2(d-hd/2)+1.
                const int64_t from = part * W + h * hd + d;
                const int64_t to = part == 2 ? from
                                   : part * W + h * hd + (d < hd / 2 ? 2 * d : 2 * (d - hd / 2) + 1);
                if (bias_ok) bias_ok = fb[(size_t)to] == bias[(size_t)from] * mask[(size_t)from];
                for (int64_t c = 0; c < cols && rows_ok; ++c)
                    rows_ok = pr[(size_t)(to * cols + c)] == rows[(size_t)(from * cols + c)];
            }
    check(bias_ok, "qkv_bias_fold", "bias * mask, then q and k permuted, v in place");
    check(rows_ok, "qkv_row_permutation", "q and k rows permuted per head, v rows untouched");
}

// A dump directory is cleaned of the files its last manifest lists, and of
// nothing else. Catches: sweeping every .npy in a directory the user pointed at.
void test_dump_dir_cleanup() {
    std::printf("\ndump directory cleanup\n");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "roma_dump_unit_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto touch = [&](const char* name, const char* text) { std::ofstream(dir / name) << text; };
    touch("listed.npy", "x");
    touch("listed.1.npy", "x");
    touch("unrelated.npy", "x");
    touch("notes.txt", "x");
    touch("manifest.json",
          "{\"nonce\": \"n\", \"finished\": true, \"exit_status\": 0, \"files\": [\"listed\", \"listed.1\"]}\n");
    SS_SETENV("SS_ROMA_DUMP", dir.string().c_str());
    const bool on = dump_enabled();
    check(on, "dump_enabled", "SS_ROMA_DUMP set");
    check(!fs::exists(dir / "listed.npy") && !fs::exists(dir / "listed.1.npy"), "dump_listed_removed",
          "files the old manifest named are gone");
    check(fs::exists(dir / "unrelated.npy") && fs::exists(dir / "notes.txt"), "dump_unlisted_kept",
          "files it did not name are untouched");
    std::ifstream m(dir / "manifest.json");
    const std::string fresh((std::istreambuf_iterator<char>(m)), std::istreambuf_iterator<char>());
    check(fresh.find("\"finished\": false") != std::string::npos &&
              fresh.find("listed") == std::string::npos,
          "dump_manifest_rewritten", "an unfinished manifest naming nothing yet");
    fs::remove_all(dir);
}

// Catches: precision left empty, read from the logit's channel or one channel
// off, the axes reordered, or squared on the way out.
void test_warp_precision() {
    DenseMatch d;
    d.w = 3;
    d.h = 2;
    d.warp.assign(12, 0.25f);
    for (int i = 0; i < 6; ++i)
        for (int c = 0; c < 4; ++c) d.confidence.push_back((float)(10 * i + c) + 0.5f);
    const Warp w = warpOf(d);
    bool same = w.precision.size() == 18;
    for (size_t i = 0; same && i < 6; ++i)
        for (int c = 0; c < 3; ++c) same = same && w.precision[3 * i + c] == d.confidence[4 * i + 1 + c];
    check(same, "warp_precision", "%zu values, (p00, p01, p11) = confidence[1:4] as stored",
          w.precision.size());
}

}  // namespace

int main() {
    test_dump_dir_cleanup();
    test_resize();
    test_rope_nonsquare();
    test_qkv_fold();
    test_warp_precision();
    std::printf("\n%d checks, %d failures\n%s\n", g_checks, g_failures,
                g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
