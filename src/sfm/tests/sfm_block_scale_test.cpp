// The block scale statistic's pure half (map/BlockScale.h), on synthetic tracks: model
// centres in metres beside quantised EXIF-like fixes. No GPU, no mapper.
//
//   sfm_block_scale_test [--verbose]
//
// Prints FAIL lines and returns the count.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sfm/map/BlockScale.h"
#include "sfm/tests/TestMain.h"

using namespace sfm;

static int fails = 0;
static bool verbose = false;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        fails++;
    }
}

// A true path, the model's copy of it (per-step scale), and the fixes: true + offset,
// quantised to 0.01 arcsec at 42 N (0.31 m north, 0.23 m east).
struct Track {
    std::vector<Vec3> truth, off;
    std::vector<double> scale;  // model step / true step, per step into frame i
    std::vector<Vec3> jitter;   // model-only displacement (a hover's centimetre jitter)
    std::vector<int> stale;     // 1: the receiver repeats the previous fix
    void walk(int n, double dx, double dy = 0) {
        for (int i = 0; i < n; i++) {
            const Vec3 p = truth.empty() ? Vec3{0, 0, 0} : truth.back();
            add({p.x + dx, p.y + dy, 0});
        }
    }
    void add(const Vec3& p) {
        truth.push_back(p);
        off.push_back({0, 0, 0});
        scale.push_back(1);
        jitter.push_back({0, 0, 0});
        stale.push_back(0);
    }
};

static double quant(double v, double q) { return std::round(v / q) * q; }

static std::vector<bss::Frame> frames(const Track& t, uint32_t stamp_from = 0) {
    std::vector<bss::Frame> f(t.truth.size());
    Vec3 m{0, 0, 0};
    for (size_t i = 0; i < f.size(); i++) {
        if (i) m = m + (t.truth[i] - t.truth[i - 1]) * t.scale[i];
        f[i].img = (uint32_t)i;
        f[i].pos = (int64_t)i;
        f[i].c = m + t.jitter[i];
        const Vec3 g = t.truth[i] + t.off[i];
        f[i].g = t.stale[i] && i ? f[i - 1].g : Vec3{quant(g.x, 0.23), quant(g.y, 0.31), 0};
        f[i].fresh = !i || f[i].g.x != f[i - 1].g.x || f[i].g.y != f[i - 1].g.y;
        f[i].stamp = i >= stamp_from ? (uint32_t)i + 1 : 0;
    }
    return f;
}

// Registration in capture order, one check per frame, the in-run rate limit of 10.
struct Replay {
    int fires = 0;
    double worst = 0;  // largest |x| over every reading
    std::vector<int> at;
};
static Replay replay(const std::vector<bss::Frame>& all) {
    Replay r;
    int since = 10;
    for (size_t n = 1; n <= all.size(); n++) {
        since++;
        const std::vector<bss::Frame> f(all.begin(), all.begin() + n);
        const bss::Reading rd = bss::read(f, n - 1, bss::globalRatio(f));
        for (int s = 0; s < 2; s++)
            for (int l = 0; l < bss::kLengths; l++)
                if (rd.have[s][l]) r.worst = std::max(r.worst, std::fabs(rd.x[s][l]));
        if (since < 10) continue;
        if (bss::pickOver(rd, bss::kTauRun).ok) {
            r.fires++;
            r.at.push_back((int)n - 1);
            since = 0;
        }
    }
    return r;
}

// The whole track's worst window under a rule the statistic rejects, for fixture power:
// frame steps (m1), nodes on GPS chord (m2), no path cap (m3) or stale fixes as nodes (m5).
enum class Wrong { FrameSteps, GpsNodes, NoCap, Stale };
static double wrongWorst(const std::vector<bss::Frame>& f, Wrong w) {
    std::vector<bss::Pair> p;
    size_t a = 0, prev = 0;
    double path = 0;
    for (size_t i = 1; i < f.size(); i++) {
        path += (f[i].c - f[prev].c).norm();
        prev = i;
        const Vec3& A = w == Wrong::GpsNodes ? f[a].g : f[a].c;
        const Vec3& I = w == Wrong::GpsNodes ? f[i].g : f[i].c;
        const bool node = w == Wrong::FrameSteps ||
                          ((I - A).norm() >= bss::kNode && (f[i].fresh || w == Wrong::Stale));
        if (node) {
            p.push_back({a, i});
            a = i;
            path = 0;
        } else if (path > 2 * bss::kNode && w != Wrong::NoCap) {
            a = i;
            path = 0;
        }
    }
    double M = 0, G = 0;
    for (auto& q : p) {
        M += (f[q.b].c - f[q.a].c).norm();
        G += (f[q.b].g - f[q.a].g).norm();
    }
    const size_t n = w == Wrong::FrameSteps ? 20 : 12;
    double worst = 0;
    for (size_t j = n; j <= p.size(); j++) {
        double m = 0, g = 0;
        for (size_t k = j - n; k < j; k++) {
            m += (f[p[k].b].c - f[p[k].a].c).norm();
            g += (f[p[k].b].g - f[p[k].a].g).norm();
        }
        if (g > 0) worst = std::max(worst, std::fabs(std::log(m / g / (M / G))));
    }
    return worst;
}

static void report(const char* what, const Replay& r) {
    if (verbose) std::printf("  %-10s fires %d, worst |x| %.4f\n", what, r.fires, r.worst);
}

// A still camera under a wandering receiver: the fix leaves 6 m and comes back while the
// model jitters 0.4 m a frame (its path cap drops the segment), then walking resumes.
static void testHover() {
    Track t;
    t.walk(150, 1.3);
    const int h0 = (int)t.truth.size();
    for (int i = 0; i < 100; i++) {
        t.add(t.truth.back());
        t.jitter.back() = {0, i % 2 ? 0.4 : 0.0, 0};
        if (i >= 10 && i <= 70) t.off.back() = {0, 6 * std::sin(M_PI * (i - 10) / 60.0), 0};
    }
    t.walk(150, 1.3);
    const auto f = frames(t);
    const Replay r = replay(f);
    report("hover", r);
    const double m1 = wrongWorst(f, Wrong::FrameSteps), m2 = wrongWorst(f, Wrong::GpsNodes);
    if (verbose) std::printf("    frame steps %.3f, GPS nodes %.3f (hover at %d)\n", m1, m2, h0);
    check(m1 > 0.06 && m2 > 0.06, "hover fixture: frame steps and GPS-chord nodes read it off");
    check(r.fires == 0 && r.worst < 0.02, "hover: a still camera under wandering fixes reads true");
}

// A 30-frame loop within 5 m, across which the fix drifts 4 m sideways.
static void testLoop() {
    Track t;
    t.walk(150, 1.3);
    const Vec3 c = t.truth.back();
    for (int i = 1; i <= 30; i++) {
        const double a = 2 * M_PI * i / 30.0;
        t.add({c.x + 2 * std::sin(a), c.y + 2 * (1 - std::cos(a)), 0});
        t.off.back() = {4.0 * i / 30.0, 0, 0};
    }
    t.walk(150, 1.3);
    for (size_t i = 180; i < t.off.size(); i++) t.off[i] = {4.0, 0, 0};
    const auto f = frames(t);
    const Replay r = replay(f);
    report("loop", r);
    const double m3 = wrongWorst(f, Wrong::NoCap);
    if (verbose) std::printf("    no path cap %.3f\n", m3);
    check(m3 > 0.05, "loop fixture: without the path cap a window bridges the drift");
    check(r.fires == 0 && r.worst < 0.03, "loop: a loop does not bridge the GPS's drift");
}

// The receiver repeats one fix for 12 frames through a right-angle turn.
static void testStale() {
    Track t;
    t.walk(150, 1.3);
    for (int i = 0; i < 12; i++) {
        t.walk(1, i < 6 ? 1.3 : 0, i < 6 ? 0 : 1.3);
        t.stale.back() = 1;
    }
    t.walk(150, 0, 1.3);
    const auto f = frames(t);
    const Replay r = replay(f);
    report("stale", r);
    if (verbose) for (int a : r.at) std::printf("    fire at %d\n", a);
    const double m5 = wrongWorst(f, Wrong::Stale);
    if (verbose) std::printf("    stale as nodes %.3f\n", m5);
    check(m5 > 0.05, "stale fixture: stale fixes as nodes read it off");
    check(r.fires == 0 && r.worst < 0.03, "stale: a stalled receiver's repeats are not nodes");
}

// Two lenses 5 cm apart at every position; the second registers after the first.
static void testRig() {
    Track t;
    t.walk(200, 1.1, 0.2);
    const auto one = frames(t);
    std::vector<bss::Frame> two;
    for (const auto& fr : one) {
        bss::Frame b = fr;
        b.img += 1000;
        b.c = b.c + Vec3{0.03, 0.04, 0};
        b.stamp = fr.stamp + 1;
        two.push_back(b);
        two.push_back(fr);
    }
    const auto col = bss::collapse(two);
    bool same = col.size() == one.size();
    for (size_t i = 0; same && i < col.size(); i++) same = col[i].img == one[i].img;
    check(same, "rig: one frame per position, the first registered");
    const bss::Reading a = bss::read(one, one.size() - 1, bss::globalRatio(one));
    const bss::Reading b = bss::read(col, col.size() - 1, bss::globalRatio(col));
    check(a.have[0][2] && b.have[0][2] && a.x[0][2] == b.x[0][2],
          "rig: the collapsed rig reads what one lens does");
}

// A track whose model is `k` short over its last `n` steps.
static std::vector<bss::Frame> tail(int head, int n, double k, uint32_t stamp_from) {
    Track t;
    t.walk(head, 1.3);
    t.walk(n, 1.3);
    for (int i = head; i < head + n; i++) t.scale[i] = k;
    return frames(t, stamp_from);
}

static std::vector<bss::Reading> readings(const std::vector<bss::Frame>& f, int last) {
    std::vector<bss::Reading> out;
    for (size_t n = f.size() - last + 1; n <= f.size(); n++) {
        const std::vector<bss::Frame> g(f.begin(), f.begin() + n);
        out.push_back(bss::read(g, n - 1, bss::globalRatio(g)));
    }
    return out;
}

// Thresholds depend on the length: a 3 % block over 150 m is acted on after growth, a
// 3.5 % one over 60 m is not, and neither fires in-run.
static void testThresholds() {
    const auto f = tail(400, 140, 0.964, 401);
    size_t w;
    const bss::Pick e = bss::pickEnd(readings(f, 20), w);
    const Replay r = replay(f);
    if (verbose) std::printf("  thresholds: long block end L%d x %.4f ok %d; in-run fires %d\n",
                             e.l, e.x, e.ok, r.fires);
    check(r.fires == 0, "thresholds: a 3.6 % block over 180 m does not fire in-run");
    check(e.ok && e.x < 0 && bss::kLength[e.l] >= 100,
          "thresholds: a 3.6 % block over 180 m is acted on after growth");
    const auto g = tail(400, 46, 0.962, 401);
    const bss::Pick e2 = bss::pickEnd(readings(g, 20), w);
    if (verbose) std::printf("  thresholds: short block end L%d x %.4f ok %d\n", e2.l, e2.x, e2.ok);
    check(e2.x < -0.03 && !e2.ok, "thresholds: a 3.5 % reading over 60 m is not acted on");
}

// Two fronts registering alternately, the west one 7 % short; the newest is west.
static void testTwoFronts() {
    Track t;
    t.walk(300, 1.3);
    for (int i = 1; i < 100; i++) t.scale[i] = 0.93;
    auto f = frames(t);
    std::vector<bss::Frame> reg;
    for (int i = 0; i < 100; i++) {
        bss::Frame e = f[200 + i], w = f[99 - i];
        e.stamp = 2 * i + 1;
        w.stamp = 2 * i + 2;
        reg.push_back(e);
        reg.push_back(w);
    }
    const auto col = bss::collapse(reg);
    size_t k = 0;
    while (k < col.size() && col[k].img != 0) k++;
    const double all = bss::globalRatio(col);
    const bss::Reading r = bss::read(col, k, all);
    if (verbose) std::printf("  two fronts: x60 %.4f against %.4f\n", r.x[1][0], std::log(0.93 / all));
    check(r.have[1][0] && std::fabs(r.x[1][0] - std::log(0.93 / all)) < 0.005,
          "two fronts: the newest front is read alone, in capture order");
}

// A fit whose scale lags the model by 5 %: every chord is 5 % long, and nothing is off.
static void testFitScale() {
    Track t;
    t.walk(300, 1.3, 0.3);
    for (auto& s : t.scale) s = 1.05;
    const Replay r = replay(frames(t));
    report("fit scale", r);
    check(r.fires == 0 && r.worst < 0.01, "fit scale: a fit 5 % off scale reads true");
}

// The block: frames since the last BA, bounded by the first solved one; the CUSUM caps it.
static void testBlock() {
    // A solved stretch whose last 20 m the GPS bows 3 m out, then a 6 % short block.
    Track t;
    t.walk(200, 1.3);
    for (int i = 185; i < 200; i++) t.off[i] = {0, 3 * std::sin(M_PI * (i - 184) / 16.0), 0};
    t.walk(100, 1.3);
    for (int i = 200; i < 300; i++) t.scale[i] = 0.94;
    const auto f = frames(t, 200);
    const double all = bss::globalRatio(f);
    const bss::Reading r = bss::read(f, 299, all);
    const bss::Pick p = bss::pickOver(r, bss::kTauRun);
    const bss::Block b = bss::block(f, 299, p.side, p.x, all, 0);
    if (verbose)
        std::printf("  block: fire %d x %.4f pivot %zu far %zu pairs %zu capped %d s %.4f\n",
                    p.ok, p.x, b.pivot, b.far, b.pairs, b.capped, b.s);
    check(p.ok && p.side == 0, "block: a 6 % short block fires on its own side");
    check(b.ok && f[b.pivot].img == 199 && f[b.far].img == 200 && !b.capped,
          "block: the pivot is the last solved frame");
    check(std::fabs(b.s * 0.94 / all - 1.0) < 0.01, "block: the factor undoes the block's ratio");
    auto every = f;
    for (auto& fr : every) fr.stamp = fr.img ? 1 : 0;
    const bss::Block c = bss::block(every, 299, p.side, p.x, all, 0);
    if (verbose) std::printf("    CUSUM alone: pivot %zu\n", c.pivot);
    check(c.ok && c.capped && f[c.pivot].img < 195, "block fixture: the CUSUM alone overshoots");

    // 100 frames since the last BA, of which only the last 40 are short: the CUSUM caps.
    Track u;
    u.walk(360, 1.3);
    for (int i = 320; i < 360; i++) u.scale[i] = 0.85;
    const auto g = frames(u, 260);
    const double all2 = bss::globalRatio(g);
    const bss::Pick q = bss::pickOver(bss::read(g, 359, all2), bss::kTauRun);
    const bss::Block d = bss::block(g, 359, q.side, q.x, all2, 0);
    if (verbose) std::printf("  cap: pivot %zu capped %d s %.4f\n", d.pivot, d.capped, d.s);
    check(q.ok && d.ok && d.capped && g[d.pivot].img >= 310 && g[d.pivot].img <= 325,
          "block: a change-point closer than the last BA caps the block");
}

// After growth: the strongest stored reading, not the newest frame's window.
static void testEnd() {
    std::vector<bss::Reading> st(20);
    for (int i = 0; i < 20; i++) {
        st[i].have[0][0] = true;
        st[i].x[0][0] = i < 17 ? -0.043 : 0.042;
    }
    size_t w;
    const bss::Pick p = bss::pickEnd(st, w);
    check(p.ok && p.x < 0 && w < 17, "end: the strongest stored reading, not the newest");
}

// Cameras 0-7 one metre apart looking +z; 5-7 registered since the last BA.
static void testRescale() {
    Reconstruction rec;
    std::vector<bss::Frame> f(8);
    for (uint32_t i = 0; i < 8; i++) {
        Image im;
        im.id = i;
        im.registered = true;
        im.pose = {mat3Identity(), {-(double)i, 0, 0}};
        rec.images[i] = im;
        f[i].img = i;
        f[i].pos = i;
        f[i].c = {(double)i, 0, 0};
        f[i].stamp = i >= 5 ? i : 0;
    }
    auto point = [&](uint64_t id, Vec3 x, std::vector<uint32_t> obs) {
        Point3D p;
        p.xyz = x;
        for (uint32_t o : obs) p.track.push_back({o, 0});
        rec.points3D[id] = p;
    };
    point(1, {6.5, 1, 10}, {5, 6, 7});
    point(2, {4.5, -1, 9}, {4, 5});
    point(3, {1, 0, 8}, {1, 2});
    bss::Block b;
    b.ok = true;
    b.newest = 7;
    b.pivot = 4;
    b.far = 5;
    b.s = 1.1;
    auto proj = [&](uint32_t cam, uint64_t pt) {
        const Pose& ps = rec.images.at(cam).pose;
        const Vec3 x = mul(ps.R, rec.points3D.at(pt).xyz) + ps.t;
        return Vec3{x.x / x.z, x.y / x.z, 1};
    };
    const Vec3 before = proj(6, 1), shared = rec.points3D[2].xyz, out = rec.points3D[3].xyz;
    const size_t moved = bss::rescale(rec, f, b, [](uint32_t i) {
        return std::pair<int64_t, int64_t>{0, (int64_t)i};
    });
    const Vec3 c4 = cameraCenter(rec.images[4].pose), c5 = cameraCenter(rec.images[5].pose);
    const Vec3 c7 = cameraCenter(rec.images[7].pose), c3 = cameraCenter(rec.images[3].pose);
    check(moved == 3, "rescale: the block's three cameras move");
    check((c4 - Vec3{4, 0, 0}).norm() < 1e-12 && (c3 - Vec3{3, 0, 0}).norm() < 1e-12,
          "rescale: the pivot and the solved frames stay");
    check(std::fabs((c5 - c4).norm() - 1.1) < 1e-12 && std::fabs((c7 - c4).norm() - 3.3) < 1e-12,
          "rescale: the block scales about the pivot, no step at the junction");
    check((proj(6, 1) - before).norm() < 1e-12, "rescale: a block-only point reprojects as before");
    check((rec.points3D[2].xyz - shared).norm() == 0 && (rec.points3D[3].xyz - out).norm() == 0,
          "rescale: points outside the block, or shared with it, stay");
}

static int body(int argc, char** argv) {
    for (int i = 0; i < argc; i++)
        if (std::string(argv[i]) == "--verbose") verbose = true;
    testHover();
    testLoop();
    testStale();
    testRig();
    testThresholds();
    testTwoFronts();
    testFitScale();
    testBlock();
    testEnd();
    testRescale();
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
