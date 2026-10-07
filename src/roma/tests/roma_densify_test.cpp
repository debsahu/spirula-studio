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

#include "app/DepthManifest.h"
#include "app/DepthPng.h"
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
#include "sfm/Pipeline.h"
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
    // Depth points are no evidence either (mutant: a map too deep vetoes the surface).
    std::vector<DensePoint> dep = trusted;
    for (DensePoint& p : dep) p.from_depth = true;
    const FreeSpace by_depth(pl, dep, 3, 0.05);
    int depth_vetoes = 0;
    for (size_t i = 0; i < 200 && i < trusted.size(); i++) {
        DensePoint fl;
        fl.xyz = pl.images[(size_t)seen_by[i][0]].centre * 0.4 + trusted[i].xyz * 0.6;
        fl.track = {trusted[i].track[1], trusted[i].track[2]};
        fl.distinct_images = 2;
        depth_vetoes += by_depth.seesThrough(fl);
    }
    check(depth_vetoes == 0, "vetoed on depth points' evidence: " + std::to_string(depth_vetoes));
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
    // Mutant: the evaluation hold-out ignored (its anchors would score their own fit).
    DepthFitOptions ho = fo;
    ho.holdout_mod = 3;
    const DepthField fh = fitDepth(img, rec, raw, ho);
    check(fh.ok && fh.anchors == 200, "anchors with every third held out: " + std::to_string(fh.anchors));
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
        if (!agreement) {   // the union of per-image clouds, as the DA360 seed is
            job.opt.min_track = 1;
            job.opt.depth_normal_check = false;
        }
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

// Mutant: normals faced by n.z instead of each pixel's own ray. A wall seen
// off axis on a 90-degree face: its camera-facing normal has n.z > 0
// everywhere, so the n.z rule inverts all of it.
void normals_face_their_own_ray() {
    SourceImage img;
    img.cam = sfm::Camera::defaultFor(1, 256, 256, 128, sfm::CamModel::Pinhole);
    img.pose = {sfm::mat3Identity(), {0, 0, 0}};
    const Vec3 n0 = Vec3{-1, 0, 0.3}.normalized();
    const double c = -0.5;   // n0 . X = c, the camera on the side n0 points to
    check(n0.z > 0, "the fixture's normal must have n.z > 0");
    for (bool ray : {true, false}) {
        std::vector<float> dist(256 * 256, 0.0f);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                const Vec3 b = img.cam.bearing({x + 0.5, y + 0.5}).normalized();
                const double nb = n0.dot(b);
                if (nb > -1e-3) continue;
                const double t = c / nb;
                dist[(size_t)y * 256 + x] = (float)(ray ? t : t * b.z);
            }
        std::vector<float> nrm;
        normalsFromDepth(img, 256, 256, ray, dist, nrm);
        int64_t valid = 0, wrong = 0, away = 0, off_axis = 0;
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++) {
                const float* v = &nrm[((size_t)y * 256 + x) * 3];
                if (!v[0] && !v[1] && !v[2]) continue;
                valid++;
                const Vec3 n{v[0], v[1], v[2]}, r = mapRay(img, 256, 256, x, y, true);
                wrong += normalAngleDeg(n, n0) > 1.0;
                away += n.dot(r) > 0;
                off_axis += std::acos(r.z) > 30 * M_PI / 180;
            }
        const std::string mode = ray ? "ray depth: " : "z depth: ";
        check(valid > 256 * 256 / 4, mode + "too few normals: " + std::to_string(valid));
        check(off_axis * 2 > valid, mode + "the fixture is not off axis");
        check(wrong == 0 && away == 0, mode + std::to_string(wrong) + " normals off the plane's, " +
                                           std::to_string(away) + " facing away, of " + std::to_string(valid));
        // faceCamera on the inverted field gives the plane's back.
        std::vector<float> inv = nrm;
        for (float& v : inv) v = -v;
        const int64_t flipped = faceCamera(img, 256, 256, inv);
        check(flipped == valid && inv == nrm, mode + "faceCamera flipped " + std::to_string(flipped) + " of " +
                                                 std::to_string(valid));
    }
}

// A fitted stair field (the fit off: the maps are distances) and that field's
// own normals from depth, for the normal tests.
struct StairField {
    DensifyPlan pl;
    sfm::Reconstruction rec;
};
StairField stairField(const fs::path& d) {
    StairField sf;
    DensifyJob job;
    job.model_dir = (d / "sparse" / "0").string();
    sf.pl = planDensify(job);
    sf.rec = sfm::Reconstruction::readBinary(job.model_dir);
    return sf;
}
std::unique_ptr<DepthFiles> stairDepthsWithNormals(const fs::path& d) {
    return std::make_unique<DepthFiles>(
        [d](const std::string& n) { return (d / "depths" / n).string(); },
        [](const SourceImage& im) { return im.cam.isSpherical(); }, (d / "depths").string(),
        [d](const std::string& n) {
            const fs::path p = (d / "normals" / n).replace_extension(".png");
            return fs::exists(p) ? p.string() : std::string();
        });
}

// Mutants: normals/ ignored when present (always from depth); the convention
// checked after facing (a map pointing away, 180 degrees, is then "repaired").
void file_normals_used_when_present() {
    for (double tilt : {15.0, 120.0, 180.0}) {
        const fs::path d = tempDir("filenormals");
        writeStairDataset(stairScene(), d.string(), 384, 768, 3000);
        SyntheticDepth sd;
        sd.noise = 0;
        sd.copies = 0;
        sd.normals = true;
        sd.tilted = 100;
        sd.tilt_deg = tilt;
        writeStairDepths(stairScene(), d.string(), sd);
        auto files = stairDepthsWithNormals(d);
        const StairField sf = stairField(d);
        int used = 0, fitted = 0;
        int64_t off = 0, total = 0;
        for (const SourceImage& im : sf.pl.images) {
            RawDepth raw;
            if (!files->load(im, raw)) continue;
            check(!raw.normal.empty(), "no normal map loaded for " + im.name);
            const DepthField f = fitDepth(im, sf.rec, raw, DepthFitOptions{});
            if (!f.ok) continue;
            fitted++;
            used += f.normal_from == NormalFrom::File;
            std::vector<float> own;
            normalsFromDepth(im, f.width, f.height, f.ray, f.dist, own);
            for (int y = 0; y < f.height; y++)
                for (int x = 0; x < f.width; x++) {
                    const float* v = &own[((size_t)y * f.width + x) * 3];
                    Vec3 n;
                    if ((!v[0] && !v[1] && !v[2]) || !f.normalAtPixel(x, y, &n)) continue;
                    total++;
                    off += normalAngleDeg(n, Vec3{v[0], v[1], v[2]}) > 10;
                }
        }
        check(fitted >= 8, "fitted maps: " + std::to_string(fitted));
        const std::string t = std::to_string((int)tilt) + " deg: ";
        if (tilt < 90) {
            check(used == fitted, t + "normal maps used for " + std::to_string(used) + " of " + std::to_string(fitted));
            check(off * 2 > total, t + "the field follows the depth, not the file: " + std::to_string(off) +
                                       " of " + std::to_string(total) + " pixels turned");
        } else {
            check(used == 0, t + "a normal map in another convention was used for " + std::to_string(used));
            check(off * 50 < total, t + "rejected maps still in the field: " + std::to_string(off));
        }
        fs::remove_all(d);
    }
}

// Mutants: a depth map recomputed when present (compute run with nothing
// missing, or allowed to rewrite a present map).
void depth_reuse_computes_only_missing() {
    const fs::path d = tempDir("reuse");
    const std::vector<std::string> names = {"a.jpg", "b.jpg", "c.jpg", "d.jpg", "e.jpg"};
    auto find = [&](const std::string& n) {
        const fs::path p = (d / n).replace_extension(".png");
        return fs::exists(p) ? p.string() : std::string();
    };
    auto put = [&](const std::string& n, const std::string& bytes) {
        std::ofstream((d / n).replace_extension(".png"), std::ios::binary) << bytes;
    };
    for (const char* n : {"a.jpg", "b.jpg", "c.jpg"}) put(n, "kept");
    int calls = 0;
    const DepthInventory inv = ensureDepths(names, find, [&] {
        calls++;
        for (const std::string& n : names)
            if (find(n).empty()) put(n, "new");
    });
    check(calls == 1 && inv.reused == 3 && inv.computed == 2 && inv.missing == 0,
          "inventory " + std::to_string(inv.reused) + "/" + std::to_string(inv.computed) + "/" +
              std::to_string(inv.missing) + ", calls " + std::to_string(calls));
    check(slurp(d / "a.png") == "kept", "a present map changed");
    const DepthInventory all = ensureDepths(names, find, [&] { calls++; });
    check(calls == 1 && all.reused == 5 && all.computed == 0, "compute ran with nothing missing");
    fs::remove(d / "e.png");
    // In place: the present maps are read-only while it runs, so nothing changes.
    bool threw = false;
    try {
        ensureDepths(names, find, [&] { for (const std::string& n : names) put(n, "rewritten"); });
    } catch (const std::exception&) {
        threw = true;
    }
    check(!threw && slurp(d / "a.png") == "kept", "a present map was overwritten in place: " + slurp(d / "a.png"));
    // Replaced (removed, written anew), which a permission does not stop: refused.
    fs::remove(d / "e.png");
    threw = false;
    try {
        ensureDepths(names, find, [&] {
            fs::remove(d / "a.png");
            put("a.jpg", "replaced");
            put("e.jpg", "new");
        });
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a compute that replaced a present map was accepted");
    fs::remove_all(d);
}

// Points of a depth run whose reference is one of the first `n` images.
int64_t fromFirstImages(const DensifyResult& r, const DensifyPlan& pl, int n) {
    int64_t k = 0;
    for (const DensePoint& p : r.cloud)
        if (p.from_depth && !p.track.empty() &&
            (int)pl.images[(size_t)pl.views[(size_t)p.track[0].view].image].id <= n)
            k++;
    return k;
}

// Mutant: the normal-agreement check off. Four images' normal maps are wrong
// (turned 70 degrees) while their depth is right.
void normal_check_drops_disagreeing() {
    const fs::path d = tempDir("normalcheck");
    writeStairDataset(stairScene(), d.string(), 384, 768, 3000);
    SyntheticDepth sd;
    sd.copies = 0;
    sd.normals = true;
    sd.tilted = 4;
    writeStairDepths(stairScene(), d.string(), sd);
    auto files = stairDepthsWithNormals(d);
    auto run = [&](bool normal_check) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
        job.depth = files.get();
        job.opt.refs = 1.0;
        job.opt.matches_per_ref = 3000;
        job.opt.depth_normal_check = normal_check;
        job.opt.depth_normal_min_cos = -1;   // past the per-image gate: the cross-view check alone
        const DensifyPlan pl = planDensify(job);
        return std::make_pair(runDensify(job, pl, nullptr), pl);
    };
    const auto on = run(true), off = run(false);
    const int64_t bad_on = fromFirstImages(on.first, on.second, 4), bad_off = fromFirstImages(off.first, off.second, 4);
    const int64_t good_on = (int64_t)on.first.cloud.size() - bad_on, good_off = (int64_t)off.first.cloud.size() - bad_off;
    check(bad_off > 300, "the fixture's wrong-normal images give too few points: " + std::to_string(bad_off));
    check(bad_on * 10 < bad_off, "points from wrong-normal images: " + std::to_string(bad_on) + " with the check, " +
                                     std::to_string(bad_off) + " without");
    check(good_on * 10 > good_off * 7, "the check also removed good points: " + std::to_string(good_on) + " of " +
                                           std::to_string(good_off));
    fs::remove_all(d);
}

// Mutant: hybrid's fill-against-its-neighbours normal test off. The
// cross-view normal check is off here, so only that test can remove them.
void hybrid_fill_normal_matches_neighbours() {
    const fs::path d = tempDir("hybridnormal");
    writeStairDataset(stairScene(), d.string(), 384, 768, 3000);
    SyntheticDepth sd;
    sd.copies = 0;
    sd.normals = true;
    sd.tilted = 4;
    writeStairDepths(stairScene(), d.string(), sd);
    auto files = stairDepthsWithNormals(d);
    const Scene scene = stairScene();
    struct Size : Matcher {
        int inputSize() const override { return 128; }
        Warp match(const MatchImage&, const MatchImage&) override { return {}; }
        std::string describe() const override { return ""; }
    } sizer;
    auto run = [&](bool local_normal) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
        job.depth = files.get();
        job.matcher = &sizer;
        job.opt.refs = 1.0;
        job.opt.matches_per_ref = 3000;
        job.opt.source = DensifySource::Hybrid;
        job.opt.depth_normal_check = false;
        job.opt.hybrid_normal_check = local_normal;
        job.opt.depth_normal_min_cos = -1;
        const DensifyPlan pl = planDensify(job);
        // Sparse matches, so the fill has neighbours to compare with.
        OracleMatcher om(&scene, OracleMatcher::independentViews(pl.images), 128, 0.0, 0.0, 1);
        job.matcher = &om;
        job.opt.matches_per_ref = 300;
        return std::make_pair(runDensify(job, pl, nullptr), pl);
    };
    const auto on = run(true), off = run(false);
    const int64_t bad_on = fromFirstImages(on.first, on.second, 4), bad_off = fromFirstImages(off.first, off.second, 4);
    check(on.first.stats.depth_local_normal > 0, "no fill was tested against its neighbours' plane");
    // 96 since fill next to another reference's matches is dropped (8 with the test).
    check(bad_off > 50, "the fixture's wrong-normal images give too few fills: " + std::to_string(bad_off));
    check(bad_on * 4 < bad_off, "fills from wrong-normal images: " + std::to_string(bad_on) + " with the test, " +
                                    std::to_string(bad_off) + " without");
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
    check(h.stats.depth_left_to_matches > 0, "hybrid saw no matched pixels to leave alone");
    check(h.stats.depth_kept * 4 < m.stats.depth_kept,
          "hybrid kept " + std::to_string(h.stats.depth_kept) + " depth points against " +
              std::to_string(m.stats.depth_kept) + " depth-only: it is not filling only the gaps");
    fs::remove_all(d);
}


// Mutant: auto resolving to hybrid whenever depth maps exist (the default until C-4,
// docs/notes/densify.md); or to roma when no matcher can run and maps are all there is.
void auto_source_is_roma_unless_no_matcher() {
    const fs::path d = tempDir("autosource");
    writeStairDataset(stairScene(), d.string(), 384, 768, 3000);
    writeStairDepths(stairScene(), d.string(), SyntheticDepth{});
    auto files = stairDepths(d);
    const Scene scene = stairScene();
    struct Size : Matcher {
        int inputSize() const override { return 128; }
        Warp match(const MatchImage&, const MatchImage&) override { return {}; }
        std::string describe() const override { return ""; }
    } sizer;
    auto job_of = [&](DensifySource src, bool matcher, bool depth) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
        if (depth) job.depth = files.get();
        if (matcher) job.matcher = &sizer;
        job.opt.refs = 4;
        job.opt.matches_per_ref = 3000;
        job.opt.source = src;
        return job;
    };
    auto source = [&](DensifySource src, bool m, bool dp) { return planDensify(job_of(src, m, dp)).source; };
    check(source(DensifySource::Auto, true, true) == DensifySource::Roma, "auto with a matcher and maps is not roma");
    check(source(DensifySource::Auto, true, false) == DensifySource::Roma, "auto with a matcher only is not roma");
    check(source(DensifySource::Auto, false, true) == DensifySource::Depth, "auto with maps and no matcher is not moge");
    check(source(DensifySource::Hybrid, true, true) == DensifySource::Hybrid, "an explicit hybrid was changed");
    check(source(DensifySource::Roma, true, true) == DensifySource::Roma, "an explicit roma was changed");
    auto run = [&](DensifySource src) {
        DensifyJob job = job_of(src, true, true);
        const DensifyPlan pl = planDensify(job);
        OracleMatcher om(&scene, OracleMatcher::independentViews(pl.images), 128, 0.0, 0.0, 1);
        job.matcher = &om;
        return runDensify(job, pl, nullptr);
    };
    const DensifyResult a = run(DensifySource::Auto), r = run(DensifySource::Roma), h = run(DensifySource::Hybrid);
    int from_depth = 0;
    for (const DensePoint& p : a.cloud) from_depth += p.from_depth;
    // The hybrid is what the old default produced; it must differ, or this checks nothing.
    check(h.stats.depth_kept > 0, "the hybrid arm made no depth points, so auto = roma proves nothing");
    check(a.stats.depth_samples == 0 && from_depth == 0, "auto drew depth points: " + std::to_string(a.stats.depth_samples));
    check(a.cloud.size() == r.cloud.size() && a.points == r.points, "auto and roma clouds differ in size");
    fs::remove_all(d);
}

// A far-isolated fixture the wrong rule cannot pass: the groups and the verdict each is built to force.
struct FarGroups {
    std::vector<DensePoint> pts;
    std::vector<int> group;           // per point
    std::vector<char> dropped;        // the verdict the rule must give
    int64_t beyond = 0;               // points more than the margin outside
};
FarGroups farFixture() {
    FarGroups g;
    auto add = [&](int id, Vec3 p, bool drop, bool beyond) {
        DensePoint d;
        d.xyz = p;
        d.error = id;
        d.distinct_images = 2;
        d.track.resize(2);
        g.pts.push_back(d);
        g.group.push_back(id);
        g.dropped.push_back(drop);
        g.beyond += beyond;
    };
    // Box [0,10]^3, margin 2, radius 0.9, at most 2 neighbours.
    for (int i = 0; i < 20; i++) add(0, {0.5 * i, 5, 5}, false, false);                 // inside, dense
    for (int i = 0; i < 3; i++) add(1, {2.0 + 3 * i, 8, 8}, false, false);              // inside, isolated
    for (int i = 0; i < 5; i++) add(2, {11.0, 3.0 + 1.5 * i, 2}, false, false);         // 1 outside: within the margin
    add(3, {11.5, 11.5, 5}, false, false);                                              // 1.5 on two axes: max-norm 1.5, 2-norm 2.1
    for (int k = 0; k < 50; k++) add(4, {16, 3.0 * (k % 10), 3.0 * (k / 10)}, true, true);   // 6 outside, isolated
    for (int a = 0; a < 5; a++)
        for (int b = 0; b < 10; b++) add(5, {-6, 100 + 0.5 * a, 100 + 0.5 * b}, false, true); // 6 outside, a coherent grid
    for (int i = 0; i < 3; i++) add(6, {16, 200 + 0.4 * i, 0}, true, true);              // 2 neighbours each: isolated
    for (int i = 0; i < 4; i++) add(7, {16, 300 + 0.25 * i, 0}, false, true);            // 3 neighbours each: not
    add(8, {12.5, 5, 5}, false, true);                                                  // far, with 3 neighbours that are not
    for (double dy : {-0.3, 0.3, 0.6}) add(9, {11.9, 5 + dy, 5}, false, false);         // ... within the margin
    // Squares of 0.2 straddling a cell corner (cell = radius = 0.9), 3 neighbours each, all across a cell boundary.
    for (double dx : {-0.1, 0.1}) for (double dy : {-0.1, 0.1}) add(10, {-9.0 + dx, 500.4 + dy, 100}, false, true);
    for (double dy : {-0.1, 0.1}) for (double dz : {-0.1, 0.1}) add(11, {-20, 540 + dy, 540 + dz}, false, true);
    return g;
}

// Mutants: isolation ignored (everything far goes); the margin ignored or the 2-norm used; neighbours counted
// among the far points only; at-most-2 turned into fewer-than-2 or at-most-3; the order of the rest changed.
void far_isolated_drops_only_isolated_far_points() {
    const FarGroups g = farFixture();
    FarFilter f;
    f.lo = {0, 0, 0};
    f.hi = {10, 10, 10};
    f.margin = 2;
    f.radius = 0.9;
    f.max_neighbours = 2;
    std::vector<DensePoint> pts = g.pts;
    int64_t beyond = 0;
    const int64_t n = dropFarIsolated(pts, f, &beyond);
    int expect_dropped = 0;
    std::vector<DensePoint> expect;
    for (size_t i = 0; i < g.pts.size(); i++) {
        if (g.dropped[i]) expect_dropped++;
        else expect.push_back(g.pts[i]);
    }
    check(expect_dropped == 53 && g.beyond == 116, "fixture drifted: " + std::to_string(expect_dropped) + " " + std::to_string(g.beyond));
    check(n == expect_dropped, "removed " + std::to_string(n) + ", expected " + std::to_string(expect_dropped));
    check(beyond == g.beyond, "far points " + std::to_string(beyond) + ", expected " + std::to_string(g.beyond));
    bool same = pts.size() == expect.size();
    for (size_t i = 0; same && i < pts.size(); i++) same = pts[i].xyz.x == expect[i].xyz.x && pts[i].xyz.y == expect[i].xyz.y && pts[i].xyz.z == expect[i].xyz.z;
    check(same, "the survivors are not the input minus the isolated far groups, in order");
    int per_group[12] = {};
    for (const DensePoint& p : pts) per_group[(int)p.error]++;
    const int want[12] = {20, 3, 5, 1, 0, 50, 0, 4, 1, 3, 4, 4};
    for (int k = 0; k < 12; k++) check(per_group[k] == want[k], "group " + std::to_string(k) + " kept " + std::to_string(per_group[k]));
    std::vector<DensePoint> none = g.pts;
    FarFilter off = f;
    off.margin = 0;
    check(dropFarIsolated(none, off) == 0 && none.size() == g.pts.size(), "a margin of 0 is off");
}

// The same fixture through finalizePoints: after the voxel select, before the cap, and only in the default mode.
void far_isolated_runs_before_the_cap() {
    const FarGroups g = farFixture();
    FarFilter f;
    f.lo = {0, 0, 0};
    f.hi = {10, 10, 10};
    f.margin = 2;
    f.radius = 0.9;
    DensifyOptions opt;
    DensifyStats st;
    const int64_t kept = (int64_t)g.pts.size() - 53;
    // A cap of exactly the survivors: filtered first, the cap removes nothing; capped first, it would draw
    // 53 points at random and the filter would then take the isolated ones among what is left.
    const std::vector<DensePoint> out = finalizePoints(g.pts, 1, 0, kept, opt, st, {}, -1, &f);
    check((int64_t)out.size() == kept && st.capped == 0, "kept " + std::to_string(out.size()) + ", capped " + std::to_string(st.capped));
    check(st.far_isolated == 53 && st.far_beyond == 116, "stats " + std::to_string(st.far_isolated) + "/" + std::to_string(st.far_beyond));
    DensifyOptions no = opt;
    no.far_isolated = false;
    DensifyStats s2;
    check(finalizePoints(g.pts, 1, 0, 0, no, s2, {}, -1, &f).size() == g.pts.size() && s2.far_isolated == 0, "--far-isolated off still filtered");
    DensifyOptions px = opt;
    px.plugin_exact = true;
    DensifyStats s3;
    check(finalizePoints(g.pts, 1, 0, 0, px, s3, {}, -1, &f).size() == g.pts.size() && s3.far_isolated == 0, "plugin_exact filtered");
    DensifyStats s4;
    check(finalizePoints(g.pts, 1, 0, 0, opt, s4).size() == g.pts.size(), "no filter given, yet points were dropped");
}

// Mutants: the box from the extremes, not p0.5-p99.5; the margin the same on a metric and a scale-free model; a
// box diagonal other than 0.2 x; the radius not 8 x the spacing; a filter on a model of under 100 points.
void far_filter_is_scale_free() {
    std::vector<Vec3> sp;
    for (int i = 0; i <= 1000; i++) sp.push_back({(double)i, (double)((i * 3) % 1001), (double)((i * 5) % 1001)});
    const FarFilter m = resolveFarFilter(sp, true, 0.5), u = resolveFarFilter(sp, false, 0.5);
    check(m.lo.x == 5 && m.lo.y == 5 && m.lo.z == 5 && m.hi.x == 995 && m.hi.y == 995 && m.hi.z == 995,
          "the box is not p0.5-p99.5: " + std::to_string(m.lo.x) + " " + std::to_string(m.hi.x));
    check(m.margin == 2.0, "a metric model's margin " + std::to_string(m.margin));
    check(std::fabs(u.margin - 0.2 * 990 * std::sqrt(3.0)) < 1e-9, "a scale-free model's margin " + std::to_string(u.margin));
    check(m.radius == 4.0 && u.radius == 4.0 && m.max_neighbours == 2, "radius " + std::to_string(m.radius));
    std::vector<Vec3> outl = sp;
    outl.push_back({5000, 5000, 5000});
    outl.push_back({-5000, -5000, -5000});
    check(resolveFarFilter(outl, true, 0.5).hi.x < 1100 && resolveFarFilter(outl, true, 0.5).lo.x > -100, "two outliers moved the box");
    sp.resize(99);
    check(resolveFarFilter(sp, true, 0.5).margin == 0, "a filter on a 99-point model");
    check(resolveFarFilter(outl, true, 0).margin == 0, "a filter with no spacing");
}

// The plan's verdict: off by flag, off in plugin-exact, off on a tiny model, and the margin from gauge.txt.
void far_isolated_plan_states() {
    const fs::path d = tempDir("farplan");
    writeStairDataset(stairScene(), d.string(), 96, 192, 300);
    auto plan = [&](bool far_on, bool exact) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.opt.far_isolated = far_on;
        job.opt.plugin_exact = exact;
        return planDensify(job);
    };
    using FS = DensifyPlan::FarState;
    check(plan(false, false).far_state == FS::Off, "--far-isolated off is not Off");
    check(plan(true, true).far_state == FS::PluginExact, "plugin-exact runs the filter");
    const DensifyPlan on = plan(true, false);
    check(on.far_state == FS::On || on.sparse_points < 100, "default mode: state " + std::to_string((int)on.far_state));
    if (on.far_state == FS::On) {
        check(on.far_filter.radius == 8 * on.sparse_spacing, "radius " + std::to_string(on.far_filter.radius));
        const double diag = (on.far_filter.hi - on.far_filter.lo).norm();
        check(std::fabs(on.far_filter.margin - 0.2 * diag) < 1e-9, "scale-free margin " + std::to_string(on.far_filter.margin));
        { std::ofstream((d / "sparse" / "0" / "gauge.txt")) << "metric 1\n"; }
        check(plan(true, false).far_filter.margin == 2.0, "a metric gauge did not give a 2 m margin");
    }
    fs::remove_all(d);
}

// The gauge.txt spirula sfm writes, comment line and all, is what the plan must read.
void plan_reads_the_gauge_sfm_writes() {
    const fs::path d = tempDir("gaugeplan");
    writeStairDataset(stairScene(), d.string(), 96, 192, 300);
    const fs::path model = d / "sparse" / "0";
    auto metricOf = [&](bool metric) {
        sfm::ModelGauge g;
        g.metric = metric;
        g.oriented = true;
        g.scale = "cameras";
        g.scale_sigma = 0.01;
        sfm::writeGauge(model, g);
        DensifyJob job;
        job.model_dir = model.string();
        return planDensify(job).metric;
    };
    check(metricOf(true), "a metric gauge written by sfm::writeGauge was not read as metric");
    check(!metricOf(false), "a non-metric gauge written by sfm::writeGauge was read as metric");
    fs::remove_all(d);
}

// Mutant: a hybrid's fill sharing the cap (matches alone fill it, so no fill
// survives), or the fill's own budget ignored.
void hybrid_fill_has_its_own_budget() {
    std::vector<DensePoint> pts;
    for (int i = 0; i < 20; i++) {
        DensePoint p;
        p.xyz = {0.1 * i, 0, 0};
        p.distinct_images = 3;
        p.track.resize(3);
        p.from_depth = i >= 10;
        pts.push_back(p);
    }
    DensifyOptions opt;
    DensifyStats st;
    const std::vector<DensePoint> out = finalizePoints(pts, 3, 0, 5, opt, st, {}, 2);
    int fill = 0;
    for (const DensePoint& p : out) fill += p.from_depth;
    check(out.size() == 7 && fill == 2, "kept " + std::to_string(out.size()) + " with " + std::to_string(fill) + " fill points");
    DensifyStats s2;
    const std::vector<DensePoint> shared = finalizePoints(pts, 3, 0, 5, opt, s2);
    check(shared.size() == 5 && std::none_of(shared.begin(), shared.end(), [](const DensePoint& p) { return p.from_depth; }),
          "the shared cap did not keep matches first");
}

// The vote on its own: pinhole images of the plane x = 3, each with a map that
// is the true distance times `scale` (no fit), and one sample grid of the first.
struct VoteRig {
    std::vector<View> views;
    std::vector<SourceImage> images;
    std::vector<DepthField> fields;
    std::vector<int> others;
};
VoteRig voteRig(const std::vector<Vec3>& eyes, const std::vector<double>& scale) {
    VoteRig r;
    const Scene sc = planeScene();
    for (size_t i = 0; i < eyes.size(); i++) {
        View v = pinView("v" + std::to_string(i), (int)i, eyes[i], {3, 0, 0}, 64, 64, 48);
        SourceImage im;
        im.id = (uint32_t)i + 1;
        im.name = v.name;
        im.cam = v.cam;
        im.pose = {v.R, v.t};
        im.centre = v.centre;
        DepthField f;
        f.width = f.height = 64;
        f.ray = true;
        f.ok = true;
        f.dist.assign(64 * 64, 0.0f);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++) {
                const Vec3 d = sfm::mul(sfm::transpose(v.R), v.cam.bearing({x + 0.5, y + 0.5})).normalized();
                const double tt = sc.hit(v.centre, d);
                if (std::isfinite(tt)) f.dist[(size_t)y * 64 + x] = (float)(tt * scale[i]);
            }
        r.views.push_back(v);
        r.images.push_back(im);
        r.fields.push_back(f);
        r.others.push_back((int)i);
    }
    return r;
}
std::vector<DensePoint> runVote(const VoteRig& r, const DepthAgreeOptions& o, DensifyStats& st) {
    std::vector<int64_t> samples;
    for (int y = 16; y < 48; y += 4)
        for (int x = 16; x < 48; x += 4) samples.push_back((int64_t)y * 64 + x);
    return depthPointsForView(r.views, r.images, r.fields, r.others, 0, 64, 64, samples, o, {}, {}, st);
}

// Mutant: the "seen through" vote off (two agree, three see past the point).
void depth_vote_seen_through_wins() {
    const std::vector<Vec3> eyes = {{0, 0, 0}, {0, 0.6, 0}, {0, -0.6, 0}, {0, 0, 0.6}, {0, 0, -0.6}, {0, 0.4, 0.4}};
    DepthAgreeOptions o;
    DensifyStats s1, s2;
    const VoteRig fair = voteRig(eyes, {1, 1, 1, 1, 1, 1});
    check(runVote(fair, o, s1).size() == 64, "the fixture's points are not kept when every map is right");
    const VoteRig deep = voteRig(eyes, {1, 1, 1, 1.3, 1.3, 1.3});
    const std::vector<DensePoint> out = runVote(deep, o, s2);
    check(out.empty() && s2.depth_through == 64, "kept " + std::to_string(out.size()) +
                                                   " points three images see through (two agree)");
}

// Mutant: voters no parallax apart counted (adjacent frames share the error).
void depth_vote_needs_parallax() {
    // A wrong map (10 % deep) and two images a centimetre from it with the same error.
    const std::vector<Vec3> eyes = {{0, 0, 0}, {0, 0.01, 0}, {0, -0.01, 0}};
    DepthAgreeOptions o;
    DensifyStats st, s0;
    const VoteRig r = voteRig(eyes, {1.1, 1.1, 1.1});
    const std::vector<DensePoint> out = runVote(r, o, st);
    check(out.empty() && st.depth_vote_close > 0, "kept " + std::to_string(out.size()) + " points that only "
                                                  "images 0.2 degrees away agree with");
    DepthAgreeOptions none = o;
    none.min_parallax_deg = 0;
    check(runVote(r, none, s0).size() == 64, "the fixture's close images do not agree, so it tests nothing");
}

// Mutant: the rank-correlation gate off. Most anchors fit the map in a narrow
// depth band; the rest, spread wide, run the other way.
void depth_fit_refuses_rank_disorder() {
    sfm::Reconstruction rec;
    SourceImage img;
    img.cam = sfm::Camera::defaultFor(1, 400, 300, 300, sfm::CamModel::Pinhole);
    img.pose = {sfm::mat3Identity(), {0, 0, 0}};
    RawDepth raw;
    raw.width = 400;
    raw.height = 300;
    raw.ray = false;
    raw.value.assign(400 * 300, 0.0f);
    for (int y = 0; y < 300; y++)
        for (int x = 0; x < 400; x++) raw.value[(size_t)y * 400 + x] = (float)(2.0 + 4.0 * x / 400.0);
    std::mt19937 rng(4);
    std::uniform_real_distribution<double> u(0, 1);
    for (uint64_t i = 1; i <= 100; i++) {
        const double px = 10 + 380 * u(rng), py = 10 + 280 * u(rng);
        const double raw_d = 2.0 + 4.0 * std::floor(px) / 400.0;
        // 55 inliers at x in [190, 210) (depth ~4), 45 outliers anti-correlated across the frame.
        const double ax = i <= 55 ? 190 + 20 * u(rng) : px;
        const double d = i <= 55 ? 2.0 + 4.0 * std::floor(ax) / 400.0 : 8.0 - raw_d;
        const Vec3 b = img.cam.bearing({ax, py});
        sfm::Point3D p;
        p.xyz = b * (d / b.z);
        rec.points3D[i] = p;
        img.points.push_back(i);
    }
    const DepthField f = fitDepth(img, rec, raw, DepthFitOptions{});
    check(!f.ok && f.refused.rfind("rank", 0) == 0, "refused: '" + f.refused + "', rank " + std::to_string(f.spearman) +
                                                       ", inliers " + std::to_string(f.inliers));
}

// Mutant: the inlier-share gate off. 40 of 100 anchors on the map; the rest
// 25 % off either way, still in rank order.
void depth_fit_refuses_few_inliers() {
    sfm::Reconstruction rec;
    SourceImage img;
    img.cam = sfm::Camera::defaultFor(1, 400, 300, 300, sfm::CamModel::Pinhole);
    img.pose = {sfm::mat3Identity(), {0, 0, 0}};
    RawDepth raw;
    raw.width = 400;
    raw.height = 300;
    raw.ray = false;
    raw.value.assign(400 * 300, 0.0f);
    for (int y = 0; y < 300; y++)
        for (int x = 0; x < 400; x++) raw.value[(size_t)y * 400 + x] = (float)(2.0 + 4.0 * x / 400.0);
    std::mt19937 rng(6);
    std::uniform_real_distribution<double> u(0, 1);
    for (uint64_t i = 1; i <= 100; i++) {
        const double px = 10 + 380 * u(rng), py = 10 + 280 * u(rng);
        // A scale alone is an affine disparity: two groups, 25 % either way, are not one.
        const double d = (2.0 + 4.0 * std::floor(px) / 400.0) * (i <= 40 ? 1.0 : i % 2 ? 1.25 : 0.8);
        const Vec3 b = img.cam.bearing({px, py});
        sfm::Point3D p;
        p.xyz = b * (d / b.z);
        rec.points3D[i] = p;
        img.points.push_back(i);
    }
    const DepthField f = fitDepth(img, rec, raw, DepthFitOptions{});
    check(!f.ok && f.refused.rfind("inlier share", 0) == 0, "refused: '" + f.refused + "', inliers " +
                                                                std::to_string(f.inliers) + ", rank " + std::to_string(f.spearman));
}

// Mutant: the flatness test off (a plane fitted through a corner or a line).
void plane_normal_needs_a_plane() {
    std::vector<Vec3> flat, corner, line;
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 10; j++) {
            flat.push_back({0.01 * i, 0.01 * j, 0.0001 * ((i + j) % 2)});
            corner.push_back(i < 5 ? Vec3{0.01 * i, 0.01 * j, 0} : Vec3{0.05, 0.01 * j, 0.01 * (i - 5)});
        }
    for (int i = 0; i < 20; i++) line.push_back({0.01 * i, 0.0001 * (i % 2), 0});
    Vec3 n;
    check(planeNormal(flat, &n) && std::fabs(std::fabs(n.z) - 1) < 1e-3, "no normal for a flat patch");
    check(!planeNormal(corner, &n), "a normal for two planes at right angles");
    check(!planeNormal(line, &n), "a normal for points on a line");
}

// Mutant: hybrid's residual test off (a fill whose map disagrees with the
// matched points around it by more than the tolerance).
void hybrid_local_residual_check() {
    const int G = 32;
    std::vector<float> local((size_t)G * G, NAN);
    for (int k = 0; k < 6; k++) local[(size_t)(10 + k) * G + 12] = 0.04f;
    check(!localResidualOk(local, G, 12 * G + 12, 0.02), "a fill 4 % off its matched neighbours passed at 2 %");
    check(localResidualOk(local, G, 12 * G + 12, 0.05), "a fill within tolerance refused");
    check(localResidualOk(local, G, 30 * G + 30, 0.02), "a fill with no neighbours refused");
}

// Mutant: the hybrid's fill budget changed (a quarter of the automatic cap).
void hybrid_fill_budget_is_a_quarter() {
    const fs::path d = tempDir("fillbudget");
    writeStairDataset(stairScene(), d.string(), 96, 192, 300);
    writeStairDepths(stairScene(), d.string(), SyntheticDepth{});
    auto files = stairDepths(d);
    struct Size : Matcher {
        int inputSize() const override { return 64; }
        Warp match(const MatchImage&, const MatchImage&) override { return {}; }
        std::string describe() const override { return ""; }
    } sizer;
    DensifyJob job;
    job.model_dir = (d / "sparse" / "0").string();
    job.depth = files.get();
    job.matcher = &sizer;
    job.opt.source = DensifySource::Hybrid;
    const DensifyPlan a = planDensify(job);
    check(a.source == DensifySource::Hybrid && a.max_fill == a.max_points / 4,
          "auto cap " + std::to_string(a.max_points) + ", fill " + std::to_string(a.max_fill));
    job.opt.max_points = 5000;
    const DensifyPlan e = planDensify(job);
    check(e.max_fill == -1, "an explicit cap must be one budget, fill " + std::to_string(e.max_fill));
    fs::remove_all(d);
}

// Mutant: hybrid fill kept where another reference's matches already measured
// the surface (a fill 1 cm off it is a second layer).
void fill_near_matches_dropped() {
    std::vector<DensePoint> pts;
    for (int i = 0; i < 10; i++) {
        DensePoint m;
        m.xyz = {0.02 * i, 0, 0};
        pts.push_back(m);
        DensePoint f = m;
        f.from_depth = true;
        f.xyz.z = 0.01;
        pts.push_back(f);
        DensePoint distant = f;
        distant.xyz.z = 0.5;
        pts.push_back(distant);
    }
    const int64_t n = dropFillNearMatches(pts, 0.02);
    int fill = 0;
    for (const DensePoint& p : pts) fill += p.from_depth;
    check(n == 10 && fill == 10 && pts.size() == 20, "dropped " + std::to_string(n) + ", fill left " + std::to_string(fill));
}

// The synthetic depths given geometry's record (app/DepthManifest.h).
void recordStairDepths(const fs::path& d) {
    std::map<std::string, app::DepthMapRecord> m;
    for (const auto& e : fs::directory_iterator(d / "depths")) {
        if (e.path().extension() != ".png") continue;
        const std::string name = e.path().filename().string();
        m[name] = {name, app::file_print((d / "images" / name).string()), app::file_print(e.path().string()), false};
    }
    check(app::write_depth_manifest((d / "depths").string(), m), "cannot write the record");
}
std::unique_ptr<DepthFiles> recordedStairDepths(const fs::path& d) {
    return std::make_unique<DepthFiles>(
        [d](const std::string& n) { return (d / "depths" / n).string(); },
        [](const SourceImage& im) { return im.cam.isSpherical(); }, (d / "depths").string(),
        std::function<std::string(const std::string&)>{},
        [d](const std::string& n) { return (d / "images" / n).string(); });
}

// Mutants: the share gate off; a record's image name, the map's own print, or
// the image's print not checked; an 8-bit map or one of another shape read.
void stale_depth_maps_refused() {
    const fs::path d = tempDir("staledepth");
    writeStairDataset(stairScene(), d.string(), 192, 384, 3000);
    writeStairDepths(stairScene(), d.string(), SyntheticDepth{});
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(d / "depths")) names.push_back(e.path().filename().string());
    std::sort(names.begin(), names.end());
    const Scene scene = stairScene();
    auto run = [&](DepthSource* src, DensifySource s, bool matcher, std::string* error) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
        job.depth = src;
        job.opt.refs = 1.0;
        job.opt.matches_per_ref = 2000;
        job.opt.source = s;
        struct Size : Matcher {
            int inputSize() const override { return 96; }
            Warp match(const MatchImage&, const MatchImage&) override { return {}; }
            std::string describe() const override { return ""; }
        } sizer;
        if (matcher) job.matcher = &sizer;
        const DensifyPlan pl = planDensify(job);
        OracleMatcher om(&scene, OracleMatcher::independentViews(pl.images), 96, 0.0, 0.0, 1);
        if (matcher) job.matcher = &om;
        try {
            return runDensify(job, pl, nullptr);
        } catch (const std::exception& e) {
            *error = e.what();
            return DensifyResult{};
        }
    };
    std::string err;
    // The fixture's power: right maps are used.
    auto good = stairDepths(d);
    const DensifyResult ok = run(good.get(), DensifySource::Depth, false, &err);
    check(err.empty() && ok.depth_share > 0.9, "right maps: share " + std::to_string(ok.depth_share) + " " + err);
    // Shifted three frames, with no record: the share gate.
    const fs::path shifted = d / "depths_shifted";
    fs::create_directories(shifted);
    for (size_t i = 0; i < names.size(); i++) fs::copy_file(d / "depths" / names[(i + 3) % names.size()], shifted / names[i]);
    DepthFiles sh([&](const std::string& n) { return (shifted / n).string(); },
                  [](const SourceImage& im) { return im.cam.isSpherical(); }, shifted.string());
    err.clear();
    run(&sh, DensifySource::Depth, false, &err);
    check(err.find("usable depth map") != std::string::npos, "shifted maps used by --source moge: '" + err + "'");
    err.clear();
    // Auto with a matcher never reads the maps; with none it is the moge source, share gate included.
    const DensifyResult fb = run(&sh, DensifySource::Auto, true, &err);
    check(err.empty() && fb.stats.depth_samples == 0 && fb.depth_share < 0,
          "auto with a matcher and shifted maps read them: '" + err + "', share " + std::to_string(fb.depth_share));
    err.clear();
    run(&sh, DensifySource::Auto, false, &err);
    check(err.find("usable depth map") != std::string::npos, "auto with no matcher and shifted maps: '" + err + "'");
    // With geometry's record, every shifted map is named for another image.
    recordStairDepths(d);
    for (size_t i = 0; i < names.size(); i++) {
        fs::copy_file(d / "depths" / names[(i + 3) % names.size()], shifted / names[i], fs::copy_options::overwrite_existing);
    }
    fs::copy_file(d / "depths" / app::kDepthManifest, shifted / app::kDepthManifest);
    DepthFiles rec_sh([&](const std::string& n) { return (shifted / n).string(); },
                      [](const SourceImage& im) { return im.cam.isSpherical(); }, shifted.string());
    const DensifyPlan pl = [&] { DensifyJob j; j.model_dir = (d / "sparse" / "0").string(); return planDensify(j); }();
    int refused = 0;
    for (const SourceImage& im : pl.images) {
        RawDepth r;
        if (rec_sh.load(im, r) && r.recorded && !r.refused.empty()) refused++;
    }
    check(refused == (int)pl.images.size(), "maps under another image's name passed: " + std::to_string(refused) + " of " +
                                               std::to_string(pl.images.size()) + " refused");
    // A record that names another image for an unchanged map (a renamed image set).
    {
        auto m = app::read_depth_manifest((d / "depths").string());
        const std::string key = pl.images[2].name;
        m[key].image = pl.images[3].name;
        app::write_depth_manifest((d / "depths").string(), m);
        auto renamed = recordedStairDepths(d);
        RawDepth rr;
        renamed->load(pl.images[2], rr);
        check(rr.refused.rfind("made for", 0) == 0, "a map recorded for another image used: '" + rr.refused + "'");
        recordStairDepths(d);
    }
    // Right names, changed contents: a map rewritten, an image changed since.
    auto rec = recordedStairDepths(d);
    const SourceImage& a = pl.images[0];
    const SourceImage& b = pl.images[1];
    RawDepth r0;
    check(rec->load(a, r0) && r0.recorded && r0.refused.empty(), "a recorded, unchanged map refused: " + r0.refused);
    std::ofstream(d / "depths" / names[0], std::ios::app) << "x";
    std::ofstream(d / "images" / b.name, std::ios::app) << "x";
    RawDepth r1, r2;
    rec->load(pl.images[0], r1);
    rec->load(b, r2);
    check(r1.refused.find("changed since geometry") != std::string::npos, "a rewritten map used: '" + r1.refused + "'");
    check(r2.refused.find("image changed") != std::string::npos, "a map of an image that changed used: '" + r2.refused + "'");
    // No record: an 8-bit map and a map of another shape.
    const fs::path odd = d / "depths_odd";
    fs::create_directories(odd);
    const int w8 = a.cam.width / 4, h8 = a.cam.height / 4;   // the camera's shape: only the depth of bits differs
    const std::vector<uint8_t> px8((size_t)w8 * h8, 100);
    stbi_write_png((odd / a.name).replace_extension(".png").string().c_str(), w8, h8, 1, px8.data(), w8);
    std::vector<uint16_t> sq(50 * 50, 1000);
    app::save_depth_png16((odd / b.name).replace_extension(".png").string(), sq.data(), 50, 50);
    DepthFiles of([&](const std::string& n) { return (odd / fs::path(n).replace_extension(".png")).string(); },
                  [](const SourceImage& im) { return im.cam.isSpherical(); }, odd.string());
    RawDepth r3, r4;
    of.load(a, r3);
    of.load(b, r4);
    check(r3.refused.find("16-bit") != std::string::npos, "an 8-bit map read: '" + r3.refused + "'");
    check(b.cam.width != b.cam.height && r4.refused.find("shape") != std::string::npos,
          "a square map used for a " + std::to_string(b.cam.width) + "x" + std::to_string(b.cam.height) + " camera: '" +
              r4.refused + "'");
    fs::remove_all(d);
}

// Mutant: a depth point's long track winning a voxel from a matched point
// (the hybrid then loses its matches wherever another reference filled).
void voxel_prefers_matched_points() {
    DensePoint m, d;
    m.xyz = {0.05, 0.05, 0.05};
    m.distinct_images = 2;
    m.track.resize(2);
    m.error = 0.5;
    d = m;
    d.from_depth = true;
    d.distinct_images = 12;
    d.track.resize(12);
    d.error = 0.001;
    for (const std::vector<DensePoint>& v : {std::vector<DensePoint>{m, d}, std::vector<DensePoint>{d, m}}) {
        const std::vector<size_t> sel = voxelSelect(v, 1.0, true);
        check(sel.size() == 1 && !v[sel[0]].from_depth, "the depth point won the voxel");
    }
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

// Mutant: --flip-mask ignored for a mask file, or applied to an image's alpha (which always
// means transparent is not the subject).
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
    check(alpha_flip.keep == std::vector<uint8_t>({1, 0, 1, 1}), "--flip-mask inverted an image's alpha");
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
                check(std::fabs(xy[0] - want.x) < 1e-3 && std::fabs(xy[1] - want.y) < 1e-3,
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

// Mutant: an empty cloud written as a sibling (it sorts after its source, so
// a trainer could pick it and train from nothing).
void empty_cloud_writes_nothing() {
    const fs::path d = tempDir("emptycloud");
    writeStairDataset(stairScene(), d.string(), 96, 192, 300);
    const std::string src = (d / "sparse" / "0").string(), out = (d / "sparse" / "0-roma").string();
    DensifyJob job;
    job.model_dir = src;
    const DensifyPlan pl = planDensify(job);
    bool threw = false;
    try {
        writeSibling(src, out, pl, {}, "{}\n");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "an empty cloud was written");
    check(!fs::exists(out) && !fs::exists(out + ".partial"), "an empty sibling is on disk");
    fs::remove_all(d);
}

// The written points through each image's own camera at the written pixels,
// read back from disk. Mutant: the face-to-panorama mapping turned the wrong
// way (face_R for its transpose), which the writer test cannot see.
void written_points_reproject() {
    const fs::path d = tempDir("reproject");
    writeStairDataset(stairScene(), d.string(), 192, 384, 1000);
    const std::string src = (d / "sparse" / "0").string(), out = (d / "sparse" / "0-roma").string();
    const Scene scene = stairScene();
    struct Size : Matcher {
        int inputSize() const override { return 96; }
        Warp match(const MatchImage&, const MatchImage&) override { return {}; }
        std::string describe() const override { return ""; }
    } sizer;
    DensifyJob job;
    job.model_dir = src;
    job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
    job.matcher = &sizer;
    job.opt.refs = 1.0;
    job.opt.max_depth_error = -1;
    const DensifyPlan pl = planDensify(job);
    OracleMatcher om(&scene, OracleMatcher::independentViews(pl.images), 96, 0.0, 0.0, 1);
    job.matcher = &om;
    const DensifyResult r = runDensify(job, pl, nullptr);
    int64_t sphere = 0;
    for (const DensePoint& p : r.cloud)
        for (const Observation& o : p.track) sphere += pl.views[(size_t)o.view].face >= 0;
    check(sphere > 1000, "too few panorama-face observations to test the mapping: " + std::to_string(sphere));
    const ReprojStats rs = writeSibling(src, out, pl, r.cloud, "{}\n");
    check(rs.observations > 1000 && rs.invalid == 0,
          std::to_string(rs.invalid) + " of " + std::to_string(rs.observations) + " observations invalid");
    check(rs.p95_px < 0.5, "written points reproject at p95 " + std::to_string(rs.p95_px) + " px");
    const std::string js = slurp(fs::path(out) / "densify.json");
    check(js.find("\"reprojection\"") != std::string::npos && js.find("\"p95_px\"") != std::string::npos,
          "densify.json has no reprojection: " + js);
    // A depth point is an average over the agreeing images' fitted maps: its
    // track must be where the average lands, not where the reference ray was.
    SyntheticDepth noisy;
    noisy.noise = 0.01;   // so the average moves off the reference ray by pixels
    writeStairDepths(stairScene(), d.string(), noisy);
    auto files = stairDepths(d);
    DensifyJob dj;
    dj.model_dir = src;
    dj.image_path = job.image_path;
    dj.depth = files.get();
    dj.opt.refs = 1.0;
    dj.opt.matches_per_ref = 2000;
    const DensifyPlan dpl = planDensify(dj);
    const DensifyResult dr = runDensify(dj, dpl, nullptr);
    const ReprojStats ds = writeSibling(src, out, dpl, dr.cloud, "{}\n");
    check(dr.stats.depth_kept > 1000 && ds.invalid == 0, "depth run: " + std::to_string(dr.stats.depth_kept) +
                                                             " points, " + std::to_string(ds.invalid) + " invalid");
    check(ds.p95_px < 0.1, "depth points reproject at p95 " + std::to_string(ds.p95_px) + " px");
    fs::remove_all(d);
}

// Mutant: the `nonzero` clamp on the weighted draw dropped (it asks for more
// pixels than exist).
void sparse_mask_draws_each_pixel_once() {
    const int w = 64, h = 48;
    std::vector<float> c((size_t)w * h, 0.0f);
    const int64_t on[] = {10 * 64 + 10, 20 * 64 + 30, 30 * 64 + 40, 40 * 64 + 50, 12 * 64 + 33};
    for (int64_t i : on) c[(size_t)i] = 0.7f;
    SampleOptions o;
    o.count = 2000;
    std::vector<int64_t> s;
    try {
        s = sampleWithCoverage(c, w, h, o);
    } catch (const std::exception& e) {
        check(false, std::string("threw: ") + e.what());
    }
    check(s.size() == 5, "drew " + std::to_string(s.size()) + " samples from 5 non-zero pixels");
    for (int64_t i : on) check(std::count(s.begin(), s.end(), i) == 1, "pixel " + std::to_string(i) + " not drawn once");
}

// Mutants: the certainty test made strict (a value AT the threshold dropped),
// the mask applied before the plugin's floor (the floor un-masks it), or the
// matcher's warp modified in place.
void certainty_threshold_and_masks() {
    const Scene sc = planeScene();
    const std::vector<View> views = {
        pinView("a", 0, {0, -0.3, 0}, {3, 0, 0}, 640, 640, 320),
        pinView("b", 1, {0, 0.3, 0.1}, {3, 0, 0}, 640, 640, 320)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.min_parallax_deg = 1.0;
    RefMatches m = oracleMatches(sc, views, 0, {1}, 64, 0.0, opt);
    const std::vector<int64_t> px = allPixels(m);
    for (float& v : m.cert[0]) v = v > 0 ? opt.min_certainty : 0.0f;
    DensifyStats st;
    check(!triangulateRef(m, views, px, opt, st).empty() && st.below_certainty == 0,
          "certainty exactly at the threshold dropped: " + std::to_string(st.below_certainty));
    for (float& v : m.cert[0]) v = v > 0 ? std::nextafter(opt.min_certainty, 0.0f) : 0.0f;
    DensifyStats st2;
    check(triangulateRef(m, views, px, opt, st2).empty(), "certainty below the threshold kept");

    Warp w;
    w.width = w.height = 4;
    w.warp.assign(32, 0.0f);
    w.certainty.assign(16, 0.05f);
    const Warp before = w;
    std::vector<uint8_t> mask(16, 1);
    mask[5] = 0;
    DensifyOptions ex;
    ex.plugin_exact = true;
    const std::vector<float> c = collectCertainty(w, mask, {}, ex);
    check(c[5] == 0.0f, "a masked pixel kept certainty " + std::to_string(c[5]) + " under the plugin's floor");
    check(c[4] == ex.certainty_floor, "the plugin's floor not applied: " + std::to_string(c[4]));
    check(w.warp == before.warp && w.certainty == before.certainty, "the matcher's warp was modified");
}

// Mutant: a neighbour's certainty ignored. One whose certainty is 0 and whose
// warp is wrong must leave the output exactly as without it.
void rejected_neighbour_changes_nothing() {
    const Scene sc = planeScene();
    const std::vector<View> views = {
        pinView("a", 0, {0, -0.3, 0}, {3, 0, 0}, 960, 960, 480),
        pinView("b", 1, {0, 0.3, 0.1}, {3, 0, 0}, 960, 960, 480),
        pinView("c", 2, {0.2, 0.0, -0.3}, {3, 0, 0}, 960, 960, 480),
        pinView("d", 3, {-0.2, 0.1, 0.3}, {3, 0, 0}, 960, 960, 480)};
    DensifyOptions opt;
    opt.max_depth_error = 0;
    opt.min_parallax_deg = 1.0;
    const RefMatches m = oracleMatches(sc, views, 0, {1, 2}, 96, 0.0, opt);
    RefMatches m2 = oracleMatches(sc, views, 0, {1, 2, 3}, 96, 0.0, opt);
    for (size_t i = 0; i < m2.cert[2].size(); i++) {
        m2.cert[2][i] = 0.0f;
        m2.warp[2][2 * i] = 0.1f;   // everything to one wrong pixel
        m2.warp[2][2 * i + 1] = -0.2f;
    }
    DensifyStats s1, s2;
    const std::vector<DensePoint> a = triangulateRef(m, views, allPixels(m), opt, s1);
    const std::vector<DensePoint> b = triangulateRef(m2, views, allPixels(m), opt, s2);
    bool same = a.size() == b.size() && !a.empty();
    for (size_t i = 0; same && i < a.size(); i++)
        same = (a[i].xyz - b[i].xyz).norm() == 0 && a[i].error == b[i].error && a[i].track.size() == b[i].track.size() &&
               a[i].distinct_images == b[i].distinct_images;
    check(same, "a rejected neighbour changed the output: " + std::to_string(a.size()) + " vs " + std::to_string(b.size()));
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
        {"auto_source_is_roma_unless_no_matcher", auto_source_is_roma_unless_no_matcher},
        {"far_isolated_drops_only_isolated_far_points", far_isolated_drops_only_isolated_far_points},
        {"far_isolated_runs_before_the_cap", far_isolated_runs_before_the_cap},
        {"far_filter_is_scale_free", far_filter_is_scale_free},
        {"far_isolated_plan_states", far_isolated_plan_states},
        {"plan_reads_the_gauge_sfm_writes", plan_reads_the_gauge_sfm_writes},
        {"normals_face_their_own_ray", normals_face_their_own_ray},
        {"file_normals_used_when_present", file_normals_used_when_present},
        {"depth_reuse_computes_only_missing", depth_reuse_computes_only_missing},
        {"normal_check_drops_disagreeing", normal_check_drops_disagreeing},
        {"hybrid_fill_normal_matches_neighbours", hybrid_fill_normal_matches_neighbours},
        {"holdout_is_every_nth_and_never_matched", holdout_is_every_nth_and_never_matched},
        {"cut_view_is_pixel_exact", cut_view_is_pixel_exact},
        {"flip_mask_inverts_keep", flip_mask_inverts_keep},
        {"refs_by_visibility_is_greedy_coverage", refs_by_visibility_is_greedy_coverage},
        {"covis_neighbours_need_parallax", covis_neighbours_need_parallax},
        {"faces_map_back_to_the_panorama", faces_map_back_to_the_panorama},
        {"face_pairing_by_axis", face_pairing_by_axis},
        {"sibling_is_consistent_and_readable", sibling_is_consistent_and_readable},
        {"warp_files_round_trip", warp_files_round_trip},
        {"voxel_prefers_matched_points", voxel_prefers_matched_points},
        {"depth_vote_seen_through_wins", depth_vote_seen_through_wins},
        {"depth_vote_needs_parallax", depth_vote_needs_parallax},
        {"depth_fit_refuses_rank_disorder", depth_fit_refuses_rank_disorder},
        {"depth_fit_refuses_few_inliers", depth_fit_refuses_few_inliers},
        {"plane_normal_needs_a_plane", plane_normal_needs_a_plane},
        {"hybrid_local_residual_check", hybrid_local_residual_check},
        {"hybrid_fill_budget_is_a_quarter", hybrid_fill_budget_is_a_quarter},
        {"fill_near_matches_dropped", fill_near_matches_dropped},
        {"stale_depth_maps_refused", stale_depth_maps_refused},
        {"hybrid_fill_has_its_own_budget", hybrid_fill_has_its_own_budget},
        {"empty_cloud_writes_nothing", empty_cloud_writes_nothing},
        {"written_points_reproject", written_points_reproject},
        {"sparse_mask_draws_each_pixel_once", sparse_mask_draws_each_pixel_once},
        {"certainty_threshold_and_masks", certainty_threshold_and_masks},
        {"rejected_neighbour_changes_nothing", rejected_neighbour_changes_nothing},
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
