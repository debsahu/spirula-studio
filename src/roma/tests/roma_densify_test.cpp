// The densify host stage on inputs whose answer is known. Each test names
// the wrong implementation it exists to catch; docs/notes/densify.md lists
// the mutation runs that showed each one fails.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "data/CameraMath.h"
#include "external/stb_image_write.h"
#include "roma/Densify.h"
#include "roma/DensifyRun.h"
#include "roma/DepthSource.h"
#include "roma/DumpMatcher.h"
#include "roma/Sample.h"
#include "roma/Select.h"
#include "roma/Synthetic.h"
#include "sfm/core/FixedPoses.h"
#include "sfm/core/Model.h"
#include "sfm/tests/TestMain.h"

namespace fs = std::filesystem;
using namespace roma;
using sfm::Mat3;
using sfm::Vec2;
using sfm::Vec3;

namespace {

int g_fails = 0;
std::string g_test;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s: %s\n", g_test.c_str(), what.c_str());
        g_fails++;
    }
}

fs::path tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("spirula_roma_test_") + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// A pinhole view looking from `eye` at `target`.
View pinView(const std::string& name, int image, const Vec3& eye, const Vec3& target, int w,
             int h, double f) {
    View v;
    v.name = name;
    v.image = image;
    v.cam = sfm::Camera::defaultFor(0, w, h, f, sfm::CamModel::Pinhole);
    const Vec3 z = (target - eye).normalized();
    const Vec3 x = z.cross({0, 0, 1}).normalized();
    const Vec3 y = z.cross(x);
    v.R = {x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z};
    v.t = sfm::mul(v.R, eye) * -1.0;
    v.centre = eye;
    return v;
}

// A single textured plane x = 3, for warps with a closed-form answer.
Scene planeScene() {
    Scene s;
    Quad q;
    q.o = {3.0, -4.0, -4.0};
    q.u = {0, 8.0, 0};
    q.v = {0, 0, 8.0};
    s.quads.push_back(q);
    return s;
}

RefMatches oracleMatches(const Scene& sc, const std::vector<View>& views, int ref,
                         const std::vector<int>& nbrs, int S, double noise,
                         const DensifyOptions& opt) {
    OracleMatcher om(&sc, views, S, noise, 0.0, 5);
    RefMatches m;
    m.ref = ref;
    m.w = m.h = S;
    for (int b : nbrs) {
        const Warp w = om.match({views[(size_t)ref].name, S, S, nullptr},
                                {views[(size_t)b].name, S, S, nullptr});
        m.cert.push_back(collectCertainty(w, {}, {}, opt));
        m.warp.push_back(w.warp);
        m.nbrs.push_back(b);
    }
    m.rgb_match.assign((size_t)S * S * 3, 128);
    m.colour_at = [](double, double) { return std::array<float, 3>{0.5f, 0.5f, 0.5f}; };
    return m;
}

std::vector<int64_t> allPixels(const RefMatches& m) {
    std::vector<int64_t> s;
    for (int64_t i = 0; i < (int64_t)m.w * m.h; i++)
        if (m.cert[0][(size_t)i] > 0) s.push_back(i);
    return s;
}

double medianTruthError(const Scene& sc, const View& A, const std::vector<DensePoint>& pts) {
    std::vector<double> e;
    for (const DensePoint& p : pts) {
        const Vec3 d = (p.xyz - A.centre).normalized();
        const double t = sc.hit(A.centre, d);
        if (std::isfinite(t)) e.push_back((A.centre + d * t - p.xyz).norm());
    }
    if (e.empty()) return INFINITY;
    std::nth_element(e.begin(), e.begin() + (long)(e.size() / 2), e.end());
    return e[e.size() / 2];
}

// ===========================================================================
// Sampling
// ===========================================================================

// Mutant: the border not excluded, or zero-certainty pixels drawn.
void sample_respects_border_and_zero() {
    const int w = 64, h = 48;
    std::vector<float> c((size_t)w * h, 0.5f);
    for (int x = 0; x < w; x++) c[(size_t)10 * w + x] = 0.0f;
    SampleOptions o;
    o.count = 2000;
    const std::vector<int64_t> s = sampleWithCoverage(c, w, h, o);
    check(!s.empty(), "drew nothing");
    for (int64_t i : s) {
        const int x = (int)(i % w), y = (int)(i / w);
        check(x >= 2 && x <= w - 3 && y >= 2 && y <= h - 3, "a border pixel was drawn");
        check(c[(size_t)i] > 0, "a zero-certainty pixel was drawn");
    }
    check(std::is_sorted(s.begin(), s.end()) && std::adjacent_find(s.begin(), s.end()) == s.end(),
          "indices not sorted and unique");
}

// Mutant: uniform weighted draws (ratio ~1), or the 0.9 cap ignored.
void sample_weights_by_capped_certainty() {
    const int w = 200, h = 100;
    std::vector<float> c((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) c[(size_t)y * w + x] = x < w / 2 ? 0.8f : 0.2f;
    double lo = 0, hi = 0;
    for (uint64_t seed = 0; seed < 20; seed++) {
        SampleOptions o;
        o.count = 400;
        o.tiles = 1;
        o.seed = seed;
        for (int64_t i : sampleWithCoverage(c, w, h, o)) (i % w < w / 2 ? hi : lo) += 1;
    }
    const double ratio = hi / std::max(1.0, lo);
    check(ratio > 3.3 && ratio < 4.8, "0.8 vs 0.2 drawn at " + std::to_string(ratio) + ", want ~4");

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) c[(size_t)y * w + x] = x < w / 2 ? 0.9f : 0.5f;
    hi = lo = 0;
    for (uint64_t seed = 0; seed < 20; seed++) {
        SampleOptions o;
        o.count = 400;
        o.tiles = 1;
        o.cap = 0.5f;
        o.seed = seed;
        for (int64_t i : sampleWithCoverage(c, w, h, o)) (i % w < w / 2 ? hi : lo) += 1;
    }
    const double capped = hi / std::max(1.0, lo);
    check(capped > 0.85 && capped < 1.18, "0.9 vs 0.5 drawn at " + std::to_string(capped) +
                                              ", want ~1 (both capped at 0.5)");
}

// Mutant: no coverage picks (a dark tile stays empty), or a non-deterministic seed.
void sample_covers_tiles_and_repeats() {
    const int w = 96, h = 96;
    std::vector<float> c((size_t)w * h, 0.05f);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w / 2; x++) c[(size_t)y * w + x] = 0.9f;
    SampleOptions o;
    o.count = 200;
    o.tiles = 4;
    const std::vector<int64_t> a = sampleWithCoverage(c, w, h, o);
    std::set<int> tiles;
    for (int64_t i : a) tiles.insert((int)((i % w) / 24) * 10 + (int)((i / w) / 24));
    int dark = 0;
    for (int t : tiles) dark += t / 10 >= 2;
    check(dark == 8, "dark tiles with a sample: " + std::to_string(dark) + " of 8");
    check(a == sampleWithCoverage(c, w, h, o), "same seed, different samples");
    o.seed = 1;
    check(a != sampleWithCoverage(c, w, h, o), "different seed, same samples");
}

// ===========================================================================
// Pixel mapping and triangulation
// ===========================================================================

// Mutant: the plugin's (w - 1) mapping in the default mode.
void mapping_default_is_pixel_centre() {
    for (int w : {512, 640, 1280}) {
        for (int j : {0, 1, w / 2, w - 1}) {
            const double g = -1.0 + 1.0 / w + j * (2.0 - 2.0 / w) / (w - 1);
            check(std::fabs(referenceX(j, w, false) - (j + 0.5)) < 1e-12, "reference x");
            check(std::fabs(warpX((float)g, w, false) - (j + 0.5)) < 1e-4, "warp x of the grid");
        }
        check(std::fabs(referenceX(w - 1, w, true) - (w - 1.5)) < 1e-3, "plugin x at the last pixel");
        check(std::fabs(referenceX(0, w, true) - 0.5 * (w - 1) / w) < 1e-3, "plugin x at the first pixel");
    }
}

// Mutant: (w - 1) mapping, transposed rotation, cam-to-world as world-to-cam.
void triangulate_recovers_plane() {
    const Scene sc = planeScene();
    const std::vector<View> views = {
        pinView("a", 0, {0, -0.3, 0}, {3, 0, 0}, 3840, 3840, 1920),
        pinView("b", 1, {0, 0.3, 0.1}, {3, 0, 0}, 3840, 3840, 1920),
        pinView("c", 2, {0.2, 0.0, -0.3}, {3, 0, 0}, 3840, 3840, 1920)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.min_parallax_deg = 1.0;
    const RefMatches m = oracleMatches(sc, views, 0, {1, 2}, 160, 0.0, opt);
    DensifyStats st;
    const std::vector<DensePoint> pts = triangulateRef(m, views, allPixels(m), opt, st);
    check(pts.size() > 15000, "points from an exact warp: " + std::to_string(pts.size()));
    const double e = medianTruthError(sc, views[0], pts);
    check(e < 1e-6, "median error on an exact warp " + std::to_string(e) + " m");
    int full = 0;
    for (const DensePoint& p : pts) full += p.distinct_images == 3;
    check(full > (int)pts.size() * 9 / 10, "points seen by all three images: " + std::to_string(full));

    // The plugin's mapping biases every match by up to half a match pixel.
    DensifyOptions px = opt;
    px.plugin_exact = true;
    px.reproj_px = 1e9;
    px.sampson_px2 = 1e18;
    px.min_parallax_deg = 0;
    const RefMatches mp = oracleMatches(sc, views, 0, {1, 2}, 160, 0.0, px);
    DensifyStats st2;
    const std::vector<DensePoint> biased = triangulateRef(mp, views, allPixels(mp), px, st2);
    const double eb = medianTruthError(sc, views[0], biased);
    check(eb > 1e-3, "the (w - 1) mapping should be visibly off: " + std::to_string(eb) + " m");
}

// Mutant: thresholds applied in native instead of match pixels.
void reprojection_threshold_is_in_match_pixels() {
    const Scene sc = planeScene();
    for (int native : {640, 3840}) {
        const std::vector<View> views = {
            pinView("a", 0, {0, -0.3, 0}, {3, 0, 0}, native, native, native / 2.0),
            pinView("b", 1, {0, 0.3, 0}, {3, 0, 0}, native, native, native / 2.0)};
        DensifyOptions opt;
    opt.max_depth_error = 0;
        opt.sampson_px2 = 0;
        opt.min_parallax_deg = 0;
        opt.min_certainty = 0.5f;
        RefMatches m = oracleMatches(sc, views, 0, {1}, 128, 0.0, opt);
        for (size_t i = 0; i < m.warp[0].size() / 2; i++) m.warp[0][2 * i + 1] += 2.0f * 0.7f / 128;
        DensifyStats st;
        opt.reproj_px = 1.0;
        const size_t kept = triangulateRef(m, views, allPixels(m), opt, st).size();
        opt.reproj_px = 0.2;
        DensifyStats st2;
        const size_t tight = triangulateRef(m, views, allPixels(m), opt, st2).size();
        const size_t n = allPixels(m).size();
        check(kept > n * 9 / 10, "native " + std::to_string(native) + ": 0.7 px off kept at 1 px: " +
                                     std::to_string(kept) + " of " + std::to_string(n));
        check(tight < n / 10, "native " + std::to_string(native) + ": 0.7 px off kept at 0.2 px: " +
                                  std::to_string(tight));
    }
}

// Mutant: Sampson threshold not rescaled to match pixels, or skipped.
void sampson_rejects_off_epipolar_matches() {
    const Scene sc = planeScene();
    const std::vector<View> views = {
        pinView("a", 0, {0, -0.4, 0}, {3, 0, 0}, 3840, 3840, 1920),
        pinView("b", 1, {0, 0.4, 0}, {3, 0, 0}, 3840, 3840, 1920)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.reproj_px = 1e9;
    opt.min_parallax_deg = 0;
    RefMatches m = oracleMatches(sc, views, 0, {1}, 128, 0.0, opt);
    // Baseline along world y, epipolar lines run along image x: a shift in y
    // is off the line by its full length.
    for (size_t i = 0; i < m.warp[0].size() / 2; i++) m.warp[0][2 * i + 1] += 2.0f * 3.0f / 128;
    opt.sampson_px2 = 5.0;
    DensifyStats st;
    const size_t loose = triangulateRef(m, views, allPixels(m), opt, st).size();
    opt.sampson_px2 = 1.0;
    DensifyStats st2;
    const size_t tight = triangulateRef(m, views, allPixels(m), opt, st2).size();
    const size_t n = allPixels(m).size();
    // Sampson of a 3 px perpendicular offset is ~ 9 / 2 = 4.5 px^2.
    check(loose > n * 9 / 10, "3 px off the epipolar line kept at 5 px^2: " + std::to_string(loose));
    check(tight < n / 10, "3 px off the epipolar line kept at 1 px^2: " + std::to_string(tight));
}

// Mutant: cheirality test dropped. Every match here is the image of a point
// 3 m BEHIND both cameras, which a pinhole projects into the frame all the same.
void cheirality_rejects_points_behind() {
    const std::vector<View> views = {
        pinView("a", 0, {0, -0.3, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b", 1, {0, 0.3, 0}, {3, 0, 0}, 640, 640, 320)};
    const int S = 64;
    RefMatches m;
    m.ref = 0;
    m.w = m.h = S;
    m.nbrs = {1};
    m.warp.assign(1, std::vector<float>((size_t)S * S * 2, 0.0f));
    m.cert.assign(1, std::vector<float>((size_t)S * S, 0.0f));
    m.colour_at = [](double, double) { return std::array<float, 3>{0, 0, 0}; };
    m.rgb_match.assign((size_t)S * S * 3, 0);
    const View& A = views[0];
    const View& B = views[1];
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            const Vec3 d = sfm::mul(sfm::transpose(A.R), A.cam.bearing({(x + 0.5) * 10, (y + 0.5) * 10}));
            const Vec3 X = A.centre - d * 3.0;
            const Vec2 px = B.cam.project(sfm::mul(B.R, X) + B.t);
            if (px.x < 0 || px.x >= 640 || px.y < 0 || px.y >= 640) continue;
            const size_t i = (size_t)y * S + x;
            m.warp[0][2 * i] = (float)(px.x / 320 - 1);
            m.warp[0][2 * i + 1] = (float)(px.y / 320 - 1);
            m.cert[0][i] = 1.0f;
        }
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.sampson_px2 = 0;
    opt.min_parallax_deg = 0;
    const std::vector<int64_t> all = allPixels(m);
    check(all.size() > 500, "fixture has " + std::to_string(all.size()) + " matches");
    DensifyStats st;
    const size_t n = triangulateRef(m, views, all, opt, st).size();
    check(n == 0, "points survived behind both cameras: " + std::to_string(n));
    check(st.cheirality > (int64_t)all.size() / 2, "behind-camera rejections: " + std::to_string(st.cheirality));
    DensifyOptions ex = opt;
    ex.plugin_exact = true;
    ex.reproj_px = 1e300;   // the plugin clamps depth at 1e-12: only cheirality can reject
    DensifyStats st2;
    check(triangulateRef(m, views, all, ex, st2).empty(), "plugin mode kept points behind the cameras");
}

// Mutant: parallax filter dropped or applied in radians.
void parallax_rejects_narrow_baselines() {
    const Scene sc = planeScene();
    // 2 cm apart at 3 m: 0.38 degrees.
    const std::vector<View> views = {
        pinView("a", 0, {0, -0.01, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b", 1, {0, 0.01, 0}, {3, 0, 0}, 640, 640, 320)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.sampson_px2 = 0;
    const RefMatches m = oracleMatches(sc, views, 0, {1}, 64, 0.0, opt);
    opt.min_parallax_deg = 0.5;
    DensifyStats st;
    check(triangulateRef(m, views, allPixels(m), opt, st).empty(), "0.38 deg kept at a 0.5 deg bar");
    opt.min_parallax_deg = 0.2;
    DensifyStats st2;
    check(!triangulateRef(m, views, allPixels(m), opt, st2).empty(), "0.38 deg dropped at a 0.2 deg bar");
}

// Mutant: max_depth_error ignored, or measured with the finer view's focal.
void depth_error_rejects_weak_geometry() {
    const Scene sc = planeScene();
    auto run = [&](double half_baseline, double fb, double bar) {
        const std::vector<View> views = {
            pinView("a", 0, {0, -half_baseline, 0}, {3, 0, 0}, 640, 640, 320),
            pinView("b", 1, {0, half_baseline, 0}, {3, 0, 0}, 640, 640, fb)};
        DensifyOptions opt;
        opt.sampson_px2 = 0;
        opt.min_parallax_deg = 0;
        opt.max_depth_error = bar;
        const RefMatches m = oracleMatches(sc, views, 0, {1}, 640, 0.0, opt);
        DensifyStats st;
        return std::make_pair(triangulateRef(m, views, allPixels(m), opt, st).size(), allPixels(m).size());
    };
    // 10 cm apart at 3 m: ~2 deg, 9.5% of the depth per pixel at f = 320.
    auto narrow = run(0.05, 320, 0.02);
    check(narrow.first < narrow.second / 20, "2-degree pair kept: " + std::to_string(narrow.first));
    check(run(0.05, 320, 0).first > narrow.second * 9 / 10, "with the bar off the pair must stay");
    // 1.6 m apart: ~28 deg, 0.7% per pixel -- unless B is a wide lens (f = 100),
    // whose pixel is three of A's: 2.1%.
    auto wide = run(0.8, 320, 0.02);
    check(wide.first > wide.second * 9 / 10, "28-degree pair dropped: " + std::to_string(wide.first));
    auto coarse = run(0.8, 100, 0.02);
    check(coarse.first < coarse.second / 20, "the coarse neighbour's focal must decide: " + std::to_string(coarse.first));
}

// Mutant: every candidate averaged (the plugin's fusion) in the default mode,
// or a sample kept when no two neighbours agree.
void fusion_drops_the_disagreeing_neighbour() {
    const Scene sc = planeScene();
    const std::vector<View> views = {
        pinView("a", 0, {0, 0, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b", 1, {0, 0.5, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("c", 2, {0, -0.5, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("d", 3, {0, 0.6, 0.0}, {3, 0, 0}, 640, 640, 320)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.min_parallax_deg = 0;
    RefMatches m = oracleMatches(sc, views, 0, {1, 2, 3}, 160, 0.0, opt);
    // Along d's epipolar lines (image x), so Sampson and reprojection both pass.
    for (size_t i = 0; i < m.warp[2].size() / 2; i++) m.warp[2][2 * i] += 2.0f * 6.0f / 160;
    DensifyStats st;
    const std::vector<DensePoint> pts = triangulateRef(m, views, allPixels(m), opt, st);
    double worst = 0;
    for (const DensePoint& p : pts) {
        const Vec3 d = (p.xyz - views[0].centre).normalized();
        worst = std::max(worst, (views[0].centre + d * sc.hit(views[0].centre, d) - p.xyz).norm());
    }
    check(pts.size() > 10000, "points: " + std::to_string(pts.size()));
    check(worst < 1e-4, "a point was pulled off the plane by " + std::to_string(worst) + " m");
    check(st.inconsistent > 10000, "disagreeing candidates removed: " + std::to_string(st.inconsistent));
    // Two neighbours that disagree: the sample is dropped, not averaged.
    RefMatches two = m;
    two.nbrs = {1, 3};
    two.warp = {m.warp[0], m.warp[2]};
    two.cert = {m.cert[0], m.cert[2]};
    DensifyStats st2;
    double worst2 = 0;
    const std::vector<DensePoint> kept2 = triangulateRef(two, views, allPixels(two), opt, st2);
    check(kept2.size() < allPixels(two).size() / 10,
          "points where the only two neighbours disagree: " + std::to_string(kept2.size()));
    for (const DensePoint& p : kept2) {
        const Vec3 d = (p.xyz - views[0].centre).normalized();
        worst2 = std::max(worst2, (views[0].centre + d * sc.hit(views[0].centre, d) - p.xyz).norm());
    }
    check(worst2 < 1e-4, "two disagreeing neighbours averaged: " + std::to_string(worst2) + " m off");
}

// Mutant: the floor applied in the default mode, masks ignored, maskB read
// at the reference pixel rather than where the warp lands.
void certainty_floor_and_masks() {
    Warp w;
    w.width = w.height = 4;
    w.certainty = std::vector<float>(16, 0.05f);
    w.warp.resize(32);
    for (int i = 0; i < 16; i++) {   // every pixel lands on B's pixel (3, 0)
        w.warp[(size_t)2 * i] = (2.0f * 3.5f / 4) - 1.0f;
        w.warp[(size_t)2 * i + 1] = (2.0f * 0.5f / 4) - 1.0f;
    }
    std::vector<uint8_t> ma(16, 1), mb(16, 1);
    ma[5] = 0;
    DensifyOptions ex;
    ex.plugin_exact = true;
    std::vector<float> c = collectCertainty(w, ma, mb, ex);
    check(std::fabs(c[0] - 0.2f) < 1e-7, "plugin floor not applied");
    check(c[5] == 0.0f, "reference mask not applied");
    mb[3] = 0;
    c = collectCertainty(w, ma, mb, ex);
    check(c[0] == 0.0f && c[10] == 0.0f, "neighbour mask not read where the warp lands");
    DensifyOptions def;
    mb[3] = 1;
    c = collectCertainty(w, ma, mb, def);
    check(std::fabs(c[0] - 0.05f) < 1e-7, "default mode must not floor");
}

// Mutant: two faces of one neighbour image counted as two views of support.
void support_counts_distinct_images() {
    const Scene sc = planeScene();
    std::vector<View> views = {
        pinView("a", 0, {0, -0.3, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b1", 1, {0, 0.3, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b2", 1, {0, 0.3, 0}, {3, 0.3, 0}, 640, 640, 320)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.sampson_px2 = 0;
    const RefMatches m = oracleMatches(sc, views, 0, {1, 2}, 64, 0.0, opt);
    DensifyStats st;
    const std::vector<DensePoint> pts = triangulateRef(m, views, allPixels(m), opt, st);
    check(!pts.empty(), "no points");
    for (const DensePoint& p : pts) {
        check(p.distinct_images <= 2, "one neighbour image counted twice");
        check(p.track.size() <= 2, "a second face of the same image joined the track");
        if (p.distinct_images > 2 || p.track.size() > 2) break;
    }
}

// Mutant: agreement counted in candidates rather than source images, so two
// faces of one neighbour, wrong together, outvote a third image.
void consistency_counts_images_not_faces() {
    const Scene truth = planeScene();
    Scene wrong = planeScene();
    wrong.quads[0].o.x = 3.3;
    const std::vector<View> views = {
        pinView("a", 0, {0, 0, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b1", 1, {0, 0.6, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b2", 1, {0, 0.6, 0}, {3, 0.2, 0.1}, 640, 640, 320),
        pinView("c", 2, {0, -0.6, 0.2}, {3, 0, 0}, 640, 640, 320)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.min_parallax_deg = 0;
    const RefMatches bad = oracleMatches(wrong, views, 0, {1, 2}, 320, 0.0, opt);
    const RefMatches good = oracleMatches(truth, views, 0, {3}, 320, 0.0, opt);
    RefMatches m = bad;
    m.nbrs.push_back(3);
    m.warp.push_back(good.warp[0]);
    m.cert.push_back(good.cert[0]);
    std::vector<int64_t> centre;   // where all three neighbours see the plane
    for (int y = 120; y < 200; y += 4)
        for (int x = 120; x < 200; x += 4) centre.push_back((int64_t)y * 320 + x);
    DensifyStats st;
    const std::vector<DensePoint> pts = triangulateRef(m, views, centre, opt, st);
    double worst = 0;
    for (const DensePoint& p : pts) {
        const Vec3 d = (p.xyz - views[0].centre).normalized();
        worst = std::max(worst, (views[0].centre + d * truth.hit(views[0].centre, d) - p.xyz).norm());
    }
    check(worst < 1e-3, "two faces of one image outvoted the third: a point " + std::to_string(worst) + " m off");
    check(st.inconsistent > 0, "nothing was found inconsistent");
}

// Mutant: thresholds left in input pixels when the warp is coarser (RoMa's
// stride-4 coarse match): 0.8 warp px of noise is 3.2 input px.
void coarse_warp_thresholds_scale() {
    const fs::path d = tempDir("coarse");
    writeStairDataset(stairScene(), d.string(), 96, 192, 400);
    const Scene scene = stairScene();
    struct Coarse : Matcher {
        OracleMatcher* inner = nullptr;
        int inputSize() const override { return 256; }
        Warp match(const MatchImage& a, const MatchImage& b) override { return inner->match(a, b); }
        std::string describe() const override { return "coarse"; }
    } coarse;
    DensifyJob job;
    job.model_dir = (d / "sparse" / "0").string();
    job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
    job.opt.refs = 4;
    job.opt.max_depth_error = -1;
    job.opt.sampson_px2 = 0;
    job.matcher = &coarse;
    int seen_warp = 0, seen_input = 0;
    job.on_warp_scale = [&](int w, int i) { seen_warp = w; seen_input = i; };
    const DensifyPlan pl = planDensify(job);
    OracleMatcher om(&scene, OracleMatcher::independentViews(pl.images), 64, 0.8, 0.0, 2);
    coarse.inner = &om;
    const DensifyResult r = runDensify(job, pl, nullptr);
    check(seen_warp == 64 && seen_input == 256, "no warning of the coarse warp");
    const double rejected = (double)r.stats.reproj / (double)std::max<int64_t>(1, r.stats.reproj + r.stats.candidates);
    check(rejected > 0.5, "0.8 warp px of noise passed a 1 px (0.25 warp px) bar: rejected " + std::to_string(rejected));
    fs::remove_all(d);
}

// Mutants: the visibility test off, its margin ignored, or the point's own
// images allowed to veto it. A floater on the line of sight from an image
// to a surface that image saw must go; a two-image point on a surface stays.
void visibility_drops_seen_through_pairs() {
    const fs::path d = tempDir("vis");
    // Dense enough that an occluder is sampled wherever one is in the way.
    writeStairDataset(stairScene(), d.string(), 96, 192, 30000);
    DensifyJob job;
    job.model_dir = (d / "sparse" / "0").string();
    const DensifyPlan pl = planDensify(job);
    const sfm::Reconstruction rec = sfm::Reconstruction::readBinary(job.model_dir);
    std::map<uint32_t, int> index;
    for (size_t i = 0; i < pl.images.size(); i++) index[pl.images[i].id] = (int)i;
    std::vector<DensePoint> trusted;
    std::vector<std::vector<int>> seen_by;
    for (const auto& kv : rec.points3D) {
        if (kv.second.track.size() < 3) continue;
        DensePoint p;
        p.xyz = kv.second.xyz;
        std::vector<int> imgs;
        for (const sfm::TrackElement& e : kv.second.track) {
            const int im = index.at(e.image_id);
            imgs.push_back(im);
            p.track.push_back({pl.views_of[(size_t)im][0], 0, 0});
        }
        p.distinct_images = (int)imgs.size();
        trusted.push_back(p);
        seen_by.push_back(imgs);
    }
    check(trusted.size() > 300, "trusted points: " + std::to_string(trusted.size()));
    const FreeSpace fs_(pl, trusted, 3, 0.05);
    int floaters = 0, vetoed = 0, surface = 0, wrongly = 0;
    for (size_t i = 0; i < trusted.size() && floaters < 200; i++) {
        const std::vector<int>& im = seen_by[i];
        // On the surface, observed by two of its images.
        DensePoint on;
        on.xyz = trusted[i].xyz;
        on.track = {trusted[i].track[0], trusted[i].track[1]};
        on.distinct_images = 2;
        surface++;
        wrongly += fs_.seesThrough(on);
        // 40% of the way from image im[0] to it, observed by two other images.
        const Vec3 C = pl.images[(size_t)im[0]].centre;
        DensePoint fl;
        fl.xyz = C + (trusted[i].xyz - C) * 0.6;
        fl.track = {trusted[i].track[1], trusted[i].track[2]};
        fl.distinct_images = 2;
        floaters++;
        vetoed += fs_.seesThrough(fl);
    }
    check(vetoed > floaters * 9 / 10, "floaters seen through: " + std::to_string(vetoed) + " of " + std::to_string(floaters));
    check(wrongly < surface / 20, "surface points vetoed: " + std::to_string(wrongly) + " of " + std::to_string(surface));
    // Where no image saw anything there is no evidence, and nothing is vetoed.
    const FreeSpace blind(pl, {}, 3, 0.05);
    int blind_vetoes = 0;
    for (size_t i = 0; i < 200 && i < trusted.size(); i++) {
        DensePoint fl;
        fl.xyz = pl.images[(size_t)seen_by[i][0]].centre * 0.4 + trusted[i].xyz * 0.6;
        fl.track = {trusted[i].track[1], trusted[i].track[2]};
        fl.distinct_images = 2;
        blind_vetoes += blind.seesThrough(fl);
    }
    check(blind_vetoes == 0, "vetoed with no evidence: " + std::to_string(blind_vetoes));
    // Through the filter itself: auto min-track drops the floater, keeps the pair.
    std::vector<DensePoint> pts = trusted;
    for (DensePoint& p : pts) p.error = 0.1;
    DensePoint fl;
    fl.xyz = pl.images[(size_t)seen_by[0][0]].centre + (trusted[0].xyz - pl.images[(size_t)seen_by[0][0]].centre) * 0.6;
    fl.track = {trusted[0].track[1], trusted[0].track[2]};
    fl.distinct_images = 2;
    fl.error = 0.05;
    pts.push_back(fl);
    DensifyOptions opt;
    DensifyStats st;
    const std::vector<DensePoint> out = finalizePoints(pts, 0, 0, 0, opt, st, [&](const DensePoint& p) { return fs_.seesThrough(p); });
    check(out.size() == trusted.size() && st.seen_through == 1, "the floater survived the filter");
    fs::remove_all(d);
}

// Mutants: the alignment off (raw values taken as distances), the rank gate
// off (an unrelated map fitted anyway).
void depth_fit_recovers_affine() {
    sfm::Reconstruction rec;
    SourceImage img;
    img.name = "a.png";
    img.cam = sfm::Camera::defaultFor(1, 400, 300, 300, sfm::CamModel::Pinhole);
    img.pose = {sfm::mat3Identity(), {0, 0, 0}};
    img.centre = {0, 0, 0};
    std::mt19937 rng(2);
    std::uniform_real_distribution<double> u(-1, 1), z(2, 6);
    for (uint64_t i = 1; i <= 300; i++) {
        sfm::Point3D p;
        const double d = z(rng);
        p.xyz = {u(rng) * d * 0.6, u(rng) * d * 0.45, d};
        rec.points3D[i] = p;
        img.points.push_back(i);
    }
    // A slanted plane z = 3 + 0.4 x; raw = 1 / ((1/z - b) / a), a = 0.7, b = 0.05.
    RawDepth raw;
    raw.width = 400;
    raw.height = 300;
    raw.ray = false;
    raw.value.assign(400 * 300, 0.0f);
    auto truth = [&](int x, int y) {
        const Vec3 b = img.cam.bearing({x + 0.5, y + 0.5});
        const double t = 3.0 / (b.z - 0.4 * b.x);   // ray b*t on z = 3 + 0.4 x
        return b.z * t;
    };
    for (int y = 0; y < 300; y++)
        for (int x = 0; x < 400; x++) raw.value[(size_t)y * 400 + x] = (float)(1.0 / ((1.0 / truth(x, y) - 0.05) / 0.7));
    // The anchors on that plane, so the fit has the true depths to agree with.
    for (auto& kv : rec.points3D) {
        const Vec3 b = img.cam.bearing(img.cam.project(kv.second.xyz));
        kv.second.xyz = b * (3.0 / (b.z - 0.4 * b.x));
    }
    DepthFitOptions fo;
    const DepthField f = fitDepth(img, rec, raw, fo);
    check(f.ok, "fit refused: " + f.refused);
    double worst = 0;
    for (int y = 10; y < 300; y += 37)
        for (int x = 10; x < 400; x += 41)
            worst = std::max(worst, std::fabs(f.dist[(size_t)y * 400 + x] / truth(x, y) - 1));
    check(worst < 1e-3, "fitted depth off by " + std::to_string(worst) + " (relative)");
    // An unrelated map: no monotone relation to the anchors.
    RawDepth noise = raw;
    for (float& v : noise.value) v = (float)(1.0 + 10.0 * std::fabs(u(rng)));
    check(!fitDepth(img, rec, noise, fo).ok, "an unrelated map was fitted");
}

std::unique_ptr<DepthFiles> stairDepths(const fs::path& d) {
    return std::make_unique<DepthFiles>([d](const std::string& n) { return (d / "depths" / n).string(); },
                                        [](const SourceImage& im) { return im.cam.isSpherical(); },
                                        (d / "depths").string());
}

// Mutants: the cross-view agreement off (the doubled stairs survive), the
// no-data sentinel taken as a depth (points at the camera centres).
void depth_agreement_drops_copies() {
    const fs::path d = tempDir("depthcopy");
    writeStairDataset(stairScene(), d.string(), 384, 768, 3000);
    SyntheticDepth sd;
    sd.copies = 3;
    writeStairDepths(stairScene(), d.string(), sd);
    auto files = stairDepths(d);
    const Scene scene = stairScene();
    auto run = [&](bool agreement) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
        job.depth = files.get();
        job.opt.refs = 1.0;
        job.opt.matches_per_ref = 3000;
        job.opt.depth_agreement = agreement;
        if (!agreement) job.opt.min_track = 1;   // the union of per-image clouds, as the DA360 seed is
        const DensifyPlan pl = planDensify(job);
        return runDensify(job, pl, nullptr);
    };
    const DensifyResult r = run(true);
    const CloudScore s = scoreCloud(scene, r.cloud, 0.01, 0.05, 0.02, 0.02);
    check(s.points > 2000, "depth points: " + std::to_string(s.points));
    check(s.beyond * 1000 <= s.points, "points beyond 5 cm with the agreement check: " + std::to_string(s.beyond));
    double nearest_centre = INFINITY;
    DensifyJob pj;
    pj.model_dir = (d / "sparse" / "0").string();
    const DensifyPlan pl = planDensify(pj);
    for (const DensePoint& p : r.cloud)
        for (const SourceImage& im : pl.images) nearest_centre = std::min(nearest_centre, (p.xyz - im.centre).norm());
    check(nearest_centre > 0.2, "a point at a camera centre: the no-data sentinel became a depth");
    // The fixture's power: without the agreement check the copies are there.
    const DensifyResult lr = run(false);
    // With no vote to catch it, only the sentinel keeps no-data pixels out.
    double loose_centre = INFINITY;
    for (const DensePoint& p : lr.cloud)
        for (const SourceImage& im : pl.images) loose_centre = std::min(loose_centre, (p.xyz - im.centre).norm());
    check(loose_centre > 0.2, "a point at a camera centre with no vote: the sentinel became a depth");
    const CloudScore loose = scoreCloud(scene, lr.cloud, 0.01, 0.05, 0.02, 0.02);
    check(loose.beyond > 20 * std::max<int64_t>(1, s.beyond) && loose.beyond * 50 > loose.points,
          "the fixture has no doubled copy to remove: " + std::to_string(loose.beyond) + " of " +
              std::to_string(loose.points));
    fs::remove_all(d);
}

// Mutant: hybrid's fill-only rule off (depth points everywhere, not only
// where the matches have nothing).
void hybrid_fills_only_uncovered() {
    const fs::path d = tempDir("hybrid");
    writeStairDataset(stairScene(), d.string(), 384, 768, 3000);
    writeStairDepths(stairScene(), d.string(), SyntheticDepth{});
    auto files = stairDepths(d);
    const Scene scene = stairScene();
    struct Size : Matcher {
        int inputSize() const override { return 128; }
        Warp match(const MatchImage&, const MatchImage&) override { return {}; }
        std::string describe() const override { return ""; }
    } sizer;
    auto run = [&](DensifySource src) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
        job.depth = files.get();
        job.matcher = &sizer;
        job.opt.refs = 4;
        job.opt.matches_per_ref = 3000;
        job.opt.source = src;
        const DensifyPlan pl = planDensify(job);
        OracleMatcher om(&scene, OracleMatcher::independentViews(pl.images), 128, 0.0, 0.0, 1);
        job.matcher = &om;
        return runDensify(job, pl, nullptr);
    };
    const DensifyResult h = run(DensifySource::Hybrid), m = run(DensifySource::Depth);
    check(m.stats.depth_kept > 2000, "depth-only points: " + std::to_string(m.stats.depth_kept));
    check(h.stats.depth_covered > 0, "hybrid saw no matched pixels to leave alone");
    check(h.stats.depth_kept * 4 < m.stats.depth_kept,
          "hybrid kept " + std::to_string(h.stats.depth_kept) + " depth points against " +
              std::to_string(m.stats.depth_kept) + " depth-only: it is not filling only the gaps");
    fs::remove_all(d);
}

// Mutant: shortest track kept, or the error tie-break reversed.
void voxel_select_keeps_longest_then_best() {
    auto pt = [](double x, int images, double err) {
        DensePoint p;
        p.xyz = {x, 0.05, 0.05};
        p.distinct_images = images;
        p.track.resize((size_t)images);
        p.error = err;
        return p;
    };
    const std::vector<DensePoint> v = {pt(0.01, 2, 0.1), pt(0.02, 3, 0.5), pt(0.03, 3, 0.2), pt(1.05, 1, 0.1)};
    check(voxelSelect(v, 0.1, true) == std::vector<size_t>({2, 3}), "voxel select picked the wrong points");
}

// Mutants: two-image points always or never admitted, the bar taken as the
// mean (0.5 here) rather than the median (0.3), one-image points admitted.
void auto_min_track_admits_consistent_pairs() {
    auto pt = [](double x, int images, double err) {
        DensePoint p;
        p.xyz = {x, 0, 0};
        p.distinct_images = images;
        p.track.resize((size_t)images);
        p.error = err;
        return p;
    };
    std::vector<DensePoint> in;
    for (double e : {0.1, 0.2, 0.3, 0.4, 1.5}) in.push_back(pt(in.size() * 1.0, 3, e));
    in.push_back(pt(10, 2, 0.25));   // kept: under the median
    in.push_back(pt(11, 2, 0.45));   // dropped: over the median, under the mean
    in.push_back(pt(12, 1, 0.01));   // dropped: one image
    DensifyOptions opt;
    DensifyStats st;
    const std::vector<DensePoint> out = finalizePoints(in, 0, 0, 0, opt, st);
    std::vector<double> xs;
    for (const DensePoint& p : out) xs.push_back(p.xyz.x);
    check(xs == std::vector<double>({0, 1, 2, 3, 4, 10}), "auto min-track kept the wrong points");
    check(std::fabs(st.two_image_bar - 0.3) < 1e-12, "bar " + std::to_string(st.two_image_bar));
    check(st.two_image_kept == 1, "two-image points kept: " + std::to_string(st.two_image_kept));
    DensifyStats st3;
    check(finalizePoints(in, 3, 0, 0, opt, st3).size() == 5, "--min-track 3 kept a short track");
}

// Mutants: uniform, 1 / error, or depth_per_px weights. B (wide) is right but
// 0.3 px off its epipolar lines, so its error is the larger; C (narrow) is
// 0.05 px along them, error near zero, depth 2 cm off.
void fusion_weights_by_depth_precision() {
    const Scene sc = planeScene();
    const std::vector<View> views = {
        pinView("a", 0, {0, 0, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b", 1, {0, 0.8, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("c", 2, {0, 0.12, 0}, {3, 0, 0}, 640, 640, 320)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.min_parallax_deg = 0;
    RefMatches m = oracleMatches(sc, views, 0, {1, 2}, 320, 0.0, opt);
    for (size_t i = 0; i < m.warp[0].size() / 2; i++) {
        m.warp[0][2 * i + 1] += 2.0f * 0.3f / 320;   // b: across its epipolar lines
        m.warp[1][2 * i] += 2.0f * 0.05f / 320;      // c: along them
    }
    std::vector<int64_t> centre;   // the middle of the frame, where all three agree
    for (int y = 120; y < 200; y += 4)
        for (int x = 120; x < 200; x += 4) centre.push_back((int64_t)y * 320 + x);
    DensifyStats st;
    const std::vector<DensePoint> pts = triangulateRef(m, views, centre, opt, st);
    int both = 0;
    for (const DensePoint& p : pts) both += p.distinct_images == 3;
    check(both > (int)centre.size() * 9 / 10, "points fused from both neighbours: " + std::to_string(both));
    const double e = medianTruthError(sc, views[0], pts);
    check(e < 0.0015, "median error " + std::to_string(e) + " m: the imprecise pair outweighed the precise one");
}

// Mutant: channels swapped when sampling, fusing or writing.
void colour_channels_in_order() {
    ImageData img;
    img.width = img.height = 2;
    img.rgb = {200, 100, 50, 200, 100, 50, 200, 100, 50, 200, 100, 50};
    const std::array<float, 3> c = sampleRgb(img, 1.0, 1.0);
    check(std::fabs(c[0] - 200 / 255.f) < 1e-6 && std::fabs(c[2] - 50 / 255.f) < 1e-6, "sampleRgb channel order");
    const Scene sc = planeScene();
    const std::vector<View> views = {pinView("a", 0, {0, -0.3, 0}, {3, 0, 0}, 640, 640, 320),
                                     pinView("b", 1, {0, 0.3, 0}, {3, 0, 0}, 640, 640, 320)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    RefMatches m = oracleMatches(sc, views, 0, {1}, 64, 0.0, opt);
    m.colour_at = [](double, double) { return std::array<float, 3>{0.9f, 0.5f, 0.1f}; };
    DensifyStats st;
    const std::vector<DensePoint> pts = triangulateRef(m, views, allPixels(m), opt, st);
    check(!pts.empty() && pts[0].rgb[0] == 0.9f && pts[0].rgb[2] == 0.1f, "triangulateRef colour order");
}

// Mutant: an off-by-one in the held-out set, or held-out images left
// selectable as references or neighbours.
void holdout_is_every_nth_and_never_matched() {
    const fs::path d = tempDir("holdout");
    writeStairDataset(stairScene(), d.string(), 96, 192, 600);
    DensifyJob job;
    job.model_dir = (d / "sparse" / "0").string();
    job.opt.holdout_every = 3;
    job.opt.refs = 1.0;
    const DensifyPlan pl = planDensify(job);
    check(pl.held_out == std::vector<int>({0, 3, 6, 9}), "held out the wrong images");
    std::set<int> held(pl.held_out.begin(), pl.held_out.end());
    for (int r : pl.refs) check(!held.count(r), "a held-out image is a reference");
    for (const auto& n : pl.nbrs)
        for (int q : n) check(!held.count(q), "a held-out image is a neighbour");
    for (const auto& rv : pl.ref_views) {
        check(!held.count(pl.views[(size_t)rv.view].image), "a held-out view is matched");
        for (int v : rv.nbr_views) check(!held.count(pl.views[(size_t)v].image), "a held-out view is a neighbour view");
    }
    check(pl.refs.size() == 8, "references: " + std::to_string(pl.refs.size()));
    fs::remove_all(d);
}

// Mutant: a half-pixel shift in cutView's sampling grid or its mask lookup.
void cut_view_is_pixel_exact() {
    ImageData img;
    img.width = img.height = 32;
    std::mt19937 rng(4);
    img.rgb.resize(32 * 32 * 3);
    img.keep.resize(32 * 32);
    for (auto& v : img.rgb) v = (uint8_t)(rng() & 255);
    for (auto& v : img.keep) v = (uint8_t)(rng() & 1);
    SourceImage src;
    src.cam = sfm::Camera::defaultFor(1, 32, 32, 30, sfm::CamModel::Pinhole);
    const std::vector<View> v = imageViews(src, 0, false, 0);
    std::vector<uint8_t> rgb, keep;
    cutView(img, src, v[0], 32, rgb, keep);
    check(keep == img.keep, "keep mask moved");
    size_t bad = 0;
    for (size_t i = 0; i < rgb.size(); i++)
        bad += img.keep[i / 3] ? rgb[i] != img.rgb[i] : rgb[i] != 0;
    check(bad == 0, "pixels changed by a same-size cut: " + std::to_string(bad));
    // At half size the mask is read at each output pixel's centre: source (2x+1, 2y+1).
    cutView(img, src, v[0], 16, rgb, keep);
    size_t off = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) off += keep[(size_t)y * 16 + x] != img.keep[(size_t)(2 * y + 1) * 32 + 2 * x + 1];
    check(off == 0, "half-size mask read off-centre at " + std::to_string(off) + " pixels");
}

// Mutant: --flip-mask ignored, for a mask file or an image's alpha.
void flip_mask_inverts_keep() {
    const fs::path d = tempDir("flip");
    const uint8_t rgba[16] = {10, 20, 30, 255, 10, 20, 30, 0, 10, 20, 30, 255, 10, 20, 30, 255};
    const uint8_t m[4] = {255, 0, 0, 255};
    stbi_write_png((d / "a.png").string().c_str(), 2, 2, 4, rgba, 8);
    stbi_write_png((d / "m.png").string().c_str(), 2, 2, 1, m, 2);
    const ImageData plain = loadImage((d / "a.png").string(), (d / "m.png").string(), false);
    const ImageData flip = loadImage((d / "a.png").string(), (d / "m.png").string(), true);
    check(plain.keep == std::vector<uint8_t>({1, 0, 0, 1}), "mask file read wrong");
    check(flip.keep == std::vector<uint8_t>({0, 1, 1, 0}), "--flip-mask ignored for a mask file");
    const ImageData alpha = loadImage((d / "a.png").string(), "", false);
    const ImageData alpha_flip = loadImage((d / "a.png").string(), "", true);
    check(alpha.keep == std::vector<uint8_t>({1, 0, 1, 1}), "alpha read wrong");
    check(alpha_flip.keep == std::vector<uint8_t>({0, 1, 0, 0}), "--flip-mask ignored for alpha");
    fs::remove_all(d);
}

// ===========================================================================
// Selection and faces
// ===========================================================================

std::vector<SourceImage> imagesWithPoints(const std::vector<std::vector<uint64_t>>& pts) {
    std::vector<SourceImage> v;
    for (size_t i = 0; i < pts.size(); i++) {
        SourceImage s;
        s.id = (uint32_t)i + 1;
        s.points = pts[i];
        v.push_back(s);
    }
    return v;
}

std::vector<int> naiveVisibility(const std::vector<SourceImage>& imgs, int k) {
    std::set<uint64_t> covered;
    std::vector<int> out, left(imgs.size());
    std::iota(left.begin(), left.end(), 0);
    while ((int)out.size() < k && !left.empty()) {
        int best = -1;
        int64_t bs = -1;
        for (int i : left) {
            int64_t s = 0;
            for (uint64_t p : imgs[(size_t)i].points) s += !covered.count(p);
            if (s > bs) { bs = s; best = i; }
        }
        out.push_back(best);
        for (uint64_t p : imgs[(size_t)best].points) covered.insert(p);
        left.erase(std::find(left.begin(), left.end(), best));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Mutant: scores not refreshed after a pick (plain sort by point count).
void refs_by_visibility_is_greedy_coverage() {
    // 0 and 1 overlap completely; 2 is smaller but new.
    const auto imgs = imagesWithPoints({{1, 2, 3, 4, 5}, {1, 2, 3, 4, 5, 6}, {10, 11, 12}});
    check(refsByVisibility(imgs, {0, 1, 2}, 2) == std::vector<int>({1, 2}), "greedy coverage");
    std::mt19937 rng(3);
    for (int trial = 0; trial < 30; trial++) {
        std::vector<std::vector<uint64_t>> pts(20);
        for (auto& p : pts) {
            std::set<uint64_t> s;
            const int n = (int)(rng() % 40);
            for (int i = 0; i < n; i++) s.insert(rng() % 120);
            p.assign(s.begin(), s.end());
        }
        const auto im = imagesWithPoints(pts);
        std::vector<int> all(20);
        std::iota(all.begin(), all.end(), 0);
        const int k = 1 + (int)(rng() % 12);
        if (refsByVisibility(im, all, k) != naiveVisibility(im, k)) {
            check(false, "lazy greedy differs from the plain greedy, trial " + std::to_string(trial));
            break;
        }
    }
}

// Mutant: neighbours by covisibility ignoring the angle prior.
void covis_neighbours_need_parallax() {
    sfm::Reconstruction rec;
    std::vector<SourceImage> imgs(3);
    for (uint64_t p = 1; p <= 30; p++) {
        sfm::Point3D pt;
        pt.xyz = {5.0, 0.0, 0.0};
        rec.points3D[p] = pt;
    }
    imgs[0].centre = {0, 0, 0};
    imgs[1].centre = {0, 0.01, 0};   // 0.1 degrees from imgs[0] at the points
    imgs[2].centre = {0, 1.0, 0};
    for (uint64_t p = 1; p <= 30; p++) { imgs[0].points.push_back(p); imgs[1].points.push_back(p); }
    for (uint64_t p = 1; p <= 10; p++) imgs[2].points.push_back(p);
    const std::vector<int> n = neighboursByCovis(imgs, rec, {0, 1, 2}, 0, 1, 1.0, 0);
    check(n == std::vector<int>({2}), "picked the near-coincident camera");
    check(neighboursByCovis(imgs, rec, {0, 1, 2}, 0, 1, 0.0, 0) == std::vector<int>({1}),
          "with no angle prior the most covisible should win");
}

// Mutant: face rotation transposed (wrong source pixel for a face pixel).
void faces_map_back_to_the_panorama() {
    SourceImage s;
    s.name = "p.jpg";
    s.cam = sfm::Camera::defaultFor(1, 4000, 2000, 0, sfm::CamModel::Equirect);
    const Mat3 R = sfm::angleAxisToRotation({0.3, -0.7, 0.2});
    s.pose = {R, {0.5, -0.2, 1.0}};
    s.centre = sfm::cameraCenter(s.pose);
    const std::vector<View> v = imageViews(s, 0, true, 1000);
    check(v.size() == 6, "six faces");
    std::mt19937 rng(9);
    std::uniform_real_distribution<double> u(-5, 5);
    int seen = 0;
    for (int k = 0; k < 200; k++) {
        const Vec3 X{u(rng), u(rng), u(rng)};
        const Vec2 truth = s.cam.project(sfm::mul(s.pose.R, X) + s.pose.t);
        for (const View& f : v) {
            const Vec3 Xc = sfm::mul(f.R, X) + f.t;
            if (Xc.z <= 0) continue;
            const Vec2 px = f.cam.project(Xc);
            if (px.x < 0 || px.x >= 1000 || px.y < 0 || px.y >= 1000) continue;
            const Vec2 back = viewToSource(s, f, px.x, px.y);
            seen++;
            double dx = std::fabs(back.x - truth.x);
            dx = std::min(dx, 4000 - dx);
            check(dx < 1e-6 && std::fabs(back.y - truth.y) < 1e-6, "face pixel maps to the wrong panorama pixel");
            if (dx >= 1e-6) return;
        }
    }
    check(seen >= 200, "points seen by a face: " + std::to_string(seen));
}

// Mutant: every face of the neighbour paired, or none past the exact axis.
void face_pairing_by_axis() {
    SourceImage a, b;
    a.cam = b.cam = sfm::Camera::defaultFor(1, 4000, 2000, 0, sfm::CamModel::Equirect);
    a.pose = {sfm::mat3Identity(), {0, 0, 0}};
    b.pose = {sfm::angleAxisToRotation({0, 45.0 * 0.017453292519943295, 0}), {0, 0, 0}};
    std::vector<View> views = imageViews(a, 0, true, 1000);
    for (View& v : imageViews(b, 1, true, 1000)) views.push_back(v);
    const std::vector<int> nb = {6, 7, 8, 9, 10, 11};
    int equator = 0;
    for (int f : {0, 1, 3, 5}) equator += (int)pairFaces(views, f, nb, 60, false).size();
    check(equator == 8, "a 45-degree yaw should pair each equator face with two: " +
                            std::to_string(equator));
    check(pairFaces(views, 2, nb, 60, false).size() == 1, "down pairs with down only");
    check(pairFaces(views, 2, nb, 60, true).size() == 6, "--face-pairs all");
}

// ===========================================================================
// Output
// ===========================================================================

// Mutants: the source's point ids left in images.bin (they alias the dense
// points), a pose rounded, the tracks written y-first or in the wrong
// channel order, the output allowed over or above its source.
void sibling_is_consistent_and_readable() {
    const fs::path d = tempDir("sibling");
    writeStairDataset(stairScene(), d.string(), 96, 192, 300);
    const std::string src = (d / "sparse" / "0").string(), out = (d / "sparse" / "0-roma").string();
    std::ofstream(fs::path(src) / "gauge.txt") << "metric 1\n";
    DensifyJob job;
    job.model_dir = src;
    const DensifyPlan pl = planDensify(job);
    std::vector<DensePoint> cloud(3);
    for (int i = 0; i < 3; i++) {
        cloud[(size_t)i].xyz = {0.1 * i, 0.2, 0.3};
        cloud[(size_t)i].track = {{0, 10.0 + i, 200.0}, {7, 30.0, 400.0 - i}};
        cloud[(size_t)i].rgb[0] = 0.9f;
        cloud[(size_t)i].rgb[1] = 0.5f;
        cloud[(size_t)i].rgb[2] = 0.1f;
    }
    writeSibling(src, out, pl, cloud, "{}\n");
    for (const char* f : {"cameras.bin", "gauge.txt"})
        check(slurp(fs::path(src) / f) == slurp(fs::path(out) / f), std::string(f) + " not byte-identical");
    check(sfm::checkFixedModel(out, sfm::readFixedPoses(src)).empty(), "poses not byte-identical");
    const sfm::Reconstruction r = sfm::Reconstruction::readBinary(out);
    const sfm::Reconstruction s0 = sfm::Reconstruction::readBinary(src);
    size_t obs = 0, linked = 0;
    for (const auto& kv : r.images) {
        obs += kv.second.points2D.size();
        for (uint64_t id : kv.second.point3D_ids) linked += id != sfm::kInvalidPoint3D;
        check(kv.second.points2D.size() == s0.images.at(kv.first).points2D.size(), "2-D points changed");
    }
    check(obs > 0, "the source has no 2-D points to test against");
    check(linked == 0, "image rows still name point ids: " + std::to_string(linked));
    for (const auto& kv : r.points3D) check(kv.second.track.empty(), "a point has a track");
    check(r.points3D.size() == 3, "points3D.bin holds " + std::to_string(r.points3D.size()));
    const uint8_t* c = r.points3D.begin()->second.rgb;
    check(c[0] == 230 && c[1] == 128 && c[2] == 26, "colour written out of order");
    {
        std::ifstream t(fs::path(out) / "points3D_tracks.bin", std::ios::binary);
        char magic[4];
        uint64_t n = 0;
        t.read(magic, 4);
        t.read((char*)&n, 8);
        check(n == 3, "tracks count");
        for (uint64_t i = 0; i < n; i++) {
            uint64_t id;
            uint32_t k;
            t.read((char*)&id, 8);
            t.read((char*)&k, 4);
            check(k == 2, "track length");
            for (uint32_t j = 0; j < k; j++) {
                uint32_t img;
                float xy[2];
                t.read((char*)&img, 4);
                t.read((char*)xy, 8);
                const Observation& o = cloud[i].track[j];
                const View& v = pl.views[(size_t)o.view];
                const Vec2 want = viewToSource(pl.images[(size_t)v.image], v, o.x, o.y);
                check(img == pl.images[(size_t)v.image].id, "track image id");
                check(std::fabs(xy[0] - want.x) < 1e-2 && std::fabs(xy[1] - want.y) < 1e-2,
                      "track observation written as (" + std::to_string(xy[0]) + ", " + std::to_string(xy[1]) +
                          "), want (" + std::to_string(want.x) + ", " + std::to_string(want.y) + ")");
            }
        }
    }
    for (const std::string& bad : {src, (d / "sparse").string(), d.string(), (fs::path(src) / "x").string()}) {
        bool refused = false;
        try {
            writeSibling(src, bad, pl, cloud, "{}\n");
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused, "writing to " + bad + " was allowed");
    }
    check(!outDirProblem(d.string(), src, (d / "sparse" / "-0").string()).empty(),
          "a sibling sorting before its source was allowed");
    check(outDirProblem(d.string(), src, out).empty(), "the default sibling was refused");
    check(fs::exists(fs::path(src) / "images.bin") && fs::exists(fs::path(src) / "cameras.bin"), "source damaged");

    // A sibling is a model like any other: densify runs on it.
    DensifyJob again;
    again.model_dir = out;
    again.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
    again.opt.refs = 2;
    again.opt.max_depth_error = -1;
    const Scene scene = stairScene();
    struct Size : Matcher {
        int inputSize() const override { return 64; }
        Warp match(const MatchImage&, const MatchImage&) override { return {}; }
        std::string describe() const override { return ""; }
    } sizer;
    again.matcher = &sizer;
    const DensifyPlan p2 = planDensify(again);
    OracleMatcher om(&scene, OracleMatcher::independentViews(p2.images), 64, 0.0, 0.0, 1);
    again.matcher = &om;
    const DensifyResult res = runDensify(again, p2, nullptr);
    check(!res.cloud.empty(), "densifying a sibling produced nothing");
    fs::remove_all(d);
}

// The .rwm round trip, both encodings. Mutant: int16 scale wrong.
void warp_files_round_trip() {
    const fs::path d = tempDir("rwm");
    Warp w;
    w.width = 3;
    w.height = 2;
    w.warp = {-1, 1, 0.5f, -0.25f, 0.001f, 0.999f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
    w.certainty = {0, 0.2f, 0.5f, 0.9f, 1.0f, 0.33f};
    for (int enc : {0, 1}) {
        const std::string p = (d / ("w" + std::to_string(enc) + ".rwm")).string();
        writeWarp(p, w, enc);
        const Warp r = readWarp(p);
        double e = 0, c = 0;
        for (size_t i = 0; i < w.warp.size(); i++) e = std::max(e, (double)std::fabs(r.warp[i] - w.warp[i]));
        for (size_t i = 0; i < w.certainty.size(); i++) c = std::max(c, (double)std::fabs(r.certainty[i] - w.certainty[i]));
        check(r.width == 3 && r.height == 2, "size");
        check(e <= (enc ? 2e-5 : 0.0) && c <= (enc ? 1e-5 : 0.0), "encoding " + std::to_string(enc));
    }
    fs::remove_all(d);
}

}  // namespace

static int body(int argc, char** argv) {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"sample_respects_border_and_zero", sample_respects_border_and_zero},
        {"sample_weights_by_capped_certainty", sample_weights_by_capped_certainty},
        {"sample_covers_tiles_and_repeats", sample_covers_tiles_and_repeats},
        {"mapping_default_is_pixel_centre", mapping_default_is_pixel_centre},
        {"triangulate_recovers_plane", triangulate_recovers_plane},
        {"reprojection_threshold_is_in_match_pixels", reprojection_threshold_is_in_match_pixels},
        {"sampson_rejects_off_epipolar_matches", sampson_rejects_off_epipolar_matches},
        {"cheirality_rejects_points_behind", cheirality_rejects_points_behind},
        {"parallax_rejects_narrow_baselines", parallax_rejects_narrow_baselines},
        {"depth_error_rejects_weak_geometry", depth_error_rejects_weak_geometry},
        {"fusion_drops_the_disagreeing_neighbour", fusion_drops_the_disagreeing_neighbour},
        {"certainty_floor_and_masks", certainty_floor_and_masks},
        {"support_counts_distinct_images", support_counts_distinct_images},
        {"voxel_select_keeps_longest_then_best", voxel_select_keeps_longest_then_best},
        {"auto_min_track_admits_consistent_pairs", auto_min_track_admits_consistent_pairs},
        {"fusion_weights_by_depth_precision", fusion_weights_by_depth_precision},
        {"colour_channels_in_order", colour_channels_in_order},
        {"consistency_counts_images_not_faces", consistency_counts_images_not_faces},
        {"coarse_warp_thresholds_scale", coarse_warp_thresholds_scale},
        {"visibility_drops_seen_through_pairs", visibility_drops_seen_through_pairs},
        {"depth_fit_recovers_affine", depth_fit_recovers_affine},
        {"depth_agreement_drops_copies", depth_agreement_drops_copies},
        {"hybrid_fills_only_uncovered", hybrid_fills_only_uncovered},
        {"holdout_is_every_nth_and_never_matched", holdout_is_every_nth_and_never_matched},
        {"cut_view_is_pixel_exact", cut_view_is_pixel_exact},
        {"flip_mask_inverts_keep", flip_mask_inverts_keep},
        {"refs_by_visibility_is_greedy_coverage", refs_by_visibility_is_greedy_coverage},
        {"covis_neighbours_need_parallax", covis_neighbours_need_parallax},
        {"faces_map_back_to_the_panorama", faces_map_back_to_the_panorama},
        {"face_pairing_by_axis", face_pairing_by_axis},
        {"sibling_is_consistent_and_readable", sibling_is_consistent_and_readable},
        {"warp_files_round_trip", warp_files_round_trip},
    };
    int ran = 0;
    for (const auto& t : tests) {
        if (argc > 1 && std::strcmp(argv[1], t.first) != 0) continue;
        g_test = t.first;
        const int before = g_fails;
        t.second();
        std::printf("%s %s\n", g_fails == before ? "ok  " : "FAIL", t.first);
        ran++;
    }
    if (!ran) { std::printf("FAIL: no test named %s\n", argv[1]); return 1; }
    return g_fails ? 1 : 0;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
