// The mapper's GPS check on a registration (Mapper::gpsCheck): a scripted
// source says how far each new registration lands from its fix, so the trigger
// for an early global BA and the refusal of a wrong-place PnP are exercised
// apart from any real fit. The scene is synthetic, the BA real (GPU).
//
//   sfm_gps_register_test [--device N] [--verbose]
//
// Prints FAIL lines and returns the count.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "sfm/core/Model.h"
#include "sfm/core/PriorSource.h"
#include "sfm/map/Assemble.h"
#include "sfm/map/Mapper.h"
#include "sfm/tests/TestMain.h"

using namespace sfm;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        fails++;
    }
}

static Pose lookAt(const Vec3& C, const Vec3& target) {
    Vec3 f = (target - C).normalized();
    Vec3 up0 = {0, 1, 0};
    Vec3 r = up0.cross(f).normalized();
    Vec3 u = f.cross(r);
    Mat3 R = {r.x, r.y, r.z, u.x, u.y, u.z, f.x, f.y, f.z};
    Vec3 t = mul(R, C);
    return {R, {-t.x, -t.y, -t.z}};
}

struct Scene {
    MatchesDatabase db;
    std::vector<FeatureSet> feats;
};

// 40 cameras on a 150-degree arc round a cloud of points, so growth crosses
// many BA boundaries and every camera sees most of the cloud.
static Scene makeScene(int M) {
    const int W = 1280, H = 960, N = 400;
    Camera K = Camera::defaultFor(1, W, H, 1200);
    std::mt19937 rng(5);
    std::uniform_real_distribution<double> ub(-2.5, 2.5);
    std::normal_distribution<double> noise(0.0, 0.4);
    std::vector<Vec3> pts(N);
    for (auto& p : pts) p = {ub(rng), ub(rng), ub(rng)};
    Scene s;
    std::vector<std::vector<char>> vis(M, std::vector<char>(N, 0));
    s.feats.resize(M);
    for (int c = 0; c < M; c++) {
        const double ang = -1.3 + 2.6 * c / (M - 1);
        const Pose P = lookAt({9 * std::sin(ang), 1.5 * std::sin(0.7 * c), 9 * std::cos(ang)}, {0, 0, 0});
        s.feats[c].width = W;
        s.feats[c].height = H;
        s.feats[c].keypoints.resize(N);
        for (int p = 0; p < N; p++) {
            const Vec3 pc = mul(P.R, pts[p]) + P.t;
            const Vec2 px = K.project(pc);
            if (pc.z > 0.1 && px.x > 0 && px.x < W && px.y > 0 && px.y < H) {
                s.feats[c].keypoints[p] = {(float)(px.x + noise(rng)), (float)(px.y + noise(rng)), 2, 0, 0};
                vis[c][p] = 1;
            } else {
                s.feats[c].keypoints[p] = {-1000, -1000, 2, 0, 0};
            }
        }
    }
    s.db.images.resize(M);
    for (int c = 0; c < M; c++) s.db.images[c] = {"cam" + std::to_string(c), (uint32_t)N};
    for (int i = 0; i < M; i++)
        for (int j = i + 1; j < M; j++) {
            TwoViewMatches tv;
            tv.image1 = i;
            tv.image2 = j;
            tv.config = (int)TwoViewConfig::Uncalibrated;
            for (int p = 0; p < N; p++)
                if (vis[i][p] && vis[j][p]) tv.matches.push_back({(uint32_t)p, (uint32_t)p, 0});
            if (tv.matches.size() >= 15) s.db.pairs.push_back(std::move(tv));
        }
    return s;
}

// Metres off the fix for each image's FIRST check, in check order; a retry and
// anything past the script read 1 m. No factors, so solves are unconstrained.
// `events` logs 'P' per check and 'F' per factors() call (a BA round, or an adoption).
class ScriptedGps : public PriorSource {
public:
    explicit ScriptedGps(std::vector<double> script) : script_(std::move(script)) {}
    bool has(uint32_t) const override { return true; }
    bool relativeRotation(uint32_t, uint32_t, Mat3&, double&) const override { return false; }
    std::vector<uint32_t> neighbours(uint32_t) const override { return {}; }
    PosePriors factors(const std::vector<PosedImage>& imgs) override {
        events += 'F';
        PosePriors p;
        p.gps.ok = imgs.size() >= 2;
        p.gps.gate = 5.0;
        p.gps.t = {(double)imgs.size(), 0.0, 0.0};  // which fit a check was made through
        return p;
    }
    bool positionError(uint32_t img, const Pose&, const GpsFrame& f, double& m) const override {
        if (!f.ok) return false;
        fits.push_back(f.t.x);
        const int c = calls[img]++;
        if (c == 0 && next < script_.size()) {
            at[next] = events.size();
            image_at[next] = img;
            m = script_[next++];
        } else {
            m = 1.0;
        }
        events += 'P';
        return true;
    }
    // Whether the event right after script entry k's check was a solve.
    bool solvedAfter(size_t k) const {
        auto it = at.find(k);
        return it != at.end() && it->second + 1 < events.size() && events[it->second + 1] == 'F';
    }

    mutable std::string events;
    mutable std::vector<double> fits;              // per check: images in the fit it used
    mutable std::map<uint32_t, int> calls;
    mutable std::map<size_t, size_t> at;           // script entry -> its 'P' in events
    mutable std::map<size_t, uint32_t> image_at;   // script entry -> image
private:
    std::vector<double> script_;
    mutable size_t next = 0;
};

struct Run {
    Mapper::PriorStats st;
    uint32_t registered = 0;
    size_t models = 0;
};

static Run runWith(const Scene& s, MapperOptions opt, ScriptedGps& gps,
                   Reconstruction* model = nullptr) {
    Mapper m(s.db, s.feats, opt, {}, nullptr, nullptr, &gps);
    std::vector<Reconstruction> models = m.run();
    if (model && !models.empty()) *model = models.front();
    Run r;
    r.st = m.priorStats();
    r.registered = models.empty() ? 0 : models.front().numRegistered();
    r.models = models.size();
    return r;
}

// Entries k with value v, the rest 1 m.
static std::vector<double> script(size_t n, std::map<size_t, double> at) {
    std::vector<double> s(n, 1.0);
    for (const auto& kv : at) s[kv.first] = kv.second;
    return s;
}

// `full` cut down to images lo..hi, tracks with them: a model the assembler holds.
static Reconstruction subModel(const Reconstruction& full, uint32_t lo, uint32_t hi) {
    Reconstruction r = full;
    for (auto it = r.images.begin(); it != r.images.end();)
        it = it->first < lo || it->first > hi ? r.images.erase(it) : std::next(it);
    for (auto it = r.points3D.begin(); it != r.points3D.end();) {
        auto& tr = it->second.track;
        tr.erase(std::remove_if(tr.begin(), tr.end(),
                                [&](const TrackElement& e) { return e.image_id < lo || e.image_id > hi; }),
                 tr.end());
        it = tr.size() < 2 ? r.points3D.erase(it) : std::next(it);
    }
    return r;
}

// Images `after` holds that `before` did not, and whether every one was checked.
static bool allChecked(const Reconstruction& before, const Reconstruction& after,
                       const ScriptedGps& g, uint32_t& added) {
    bool ok = true;
    added = 0;
    for (const auto& kv : after.images) {
        if (!kv.second.registered) continue;
        auto b = before.images.find(kv.first);
        if (b != before.images.end() && b->second.registered) continue;
        added++;
        ok = ok && g.calls.count(kv.first) && g.calls.at(kv.first) >= 1;
    }
    return ok && added > 0;
}

// The assembly path (Assemble.h): growth by PnP between joint solves, and the
// continuation of a finished model, each on a model the mapper adopted rather
// than built, so no global solve of its own has run before it registers.
static void testAssembly(const Scene& sc, const Reconstruction& full, MapperOptions opt) {
    const int M = (int)sc.db.images.size();
    {
        ScriptedGps g(script(M, {{0, 25.0}}));
        Mapper m(sc.db, sc.feats, opt, {}, nullptr, nullptr, &g);
        std::vector<Reconstruction> models{subModel(full, 0, 19)};
        const Reconstruction before = models[0];
        std::vector<char> dirty(1, 0);
        size_t rejected = 0;
        const size_t reg = detail::growModels(m, models, dirty, {}, 0.25, 10, rejected);
        uint32_t added = 0;
        const bool every = allChecked(before, models[0], g, added);
        std::printf("growth: %zu registered, %zu check(s), refused %u | %s\n", reg, g.at.size(),
                    m.priorStats().gps_refused, g.events.c_str());
        check(every, "assembly: every image growth registers is checked against the GPS");
        check(m.priorStats().gps_refused == 1, "assembly: growth refuses a wrong-place PnP");
    }
    {
        ScriptedGps g(script(M, {}));
        Mapper m(sc.db, sc.feats, opt, {}, nullptr, nullptr, &g);
        std::vector<Reconstruction> models{subModel(full, 0, 16), subModel(full, 26, 39)};
        const uint32_t na = models[0].numRegistered(), nb = models[1].numRegistered();
        std::vector<char> dirty(2, 0);
        size_t rejected = 0;
        detail::growModels(m, models, dirty, {}, 0.25, 4, rejected);
        size_t k = 0, a = 0, b = 0;
        while (k < g.fits.size() && g.fits[k] == na) k++, a++;
        while (k < g.fits.size() && g.fits[k] == nb) k++, b++;
        std::printf("two models (%u, %u images): %zu check(s) through a %u-image fit, then %zu "
                    "through a %u-image one, %zu other\n", na, nb, a, na, b, nb,
                    g.fits.size() - k);
        check(na != nb && a > 0 && b > 0 && k == g.fits.size(),
              "assembly: each model is checked through its own fit");
    }
    {
        // No global solve falls due inside this pass, so only a trigger stops it.
        MapperOptions o = opt;
        o.ba_growth_ratio = 3.0;
        for (const bool drift : {false, true}) {
            ScriptedGps g(drift ? script(M, {{10, 6.0}, {11, 6.0}, {12, 6.0}}) : script(M, {}));
            Mapper m(sc.db, sc.feats, o, {}, nullptr, nullptr, &g);
            std::vector<Reconstruction> models{subModel(full, 0, 14)};
            std::vector<char> dirty(1, 0);
            size_t rejected = 0;
            const size_t reg = detail::growModels(m, models, dirty, {}, 0.25, 40, rejected);
            std::printf("growth %s: %zu registered, ba %u, out %u\n",
                        drift ? "drifting at 11-13" : "in gate", reg, m.priorStats().gps_ba,
                        m.priorStats().gps_out);
            if (!drift)
                check(reg == (size_t)M - 15 && m.priorStats().gps_ba == 0,
                      "fixture: growth in the gate takes every image");
            else
                check(reg == 13 && m.priorStats().gps_ba == 1,
                      "assembly: a drifting run stops growth for the joint solve");
        }
    }
    {
        ScriptedGps g(script(M, {{0, 25.0}}));
        Mapper m(sc.db, sc.feats, opt, {}, nullptr, nullptr, &g);
        const Reconstruction part = subModel(full, 0, 29);
        Mapper::GrowStats gs;
        const Reconstruction back = m.continueFrom(part, &gs);
        uint32_t added = 0;
        const bool every = allChecked(part, back, g, added);
        std::printf("continuation: %u added, refused %u | %s\n", added,
                    m.priorStats().gps_refused, g.events.c_str());
        check(every, "continuation: every image it registers is checked against the GPS");
        check(m.priorStats().gps_refused == 1, "continuation: refuses a wrong-place PnP");
    }
    {
        // A misplaced image makes the audit repair, and a repair re-grows the model.
        ScriptedGps g(script(M, {}));
        Mapper m(sc.db, sc.feats, opt, {}, nullptr, nullptr, &g);
        Reconstruction bad = subModel(full, 6, 29);
        Image moved = full.images.at(5);
        moved.pose.R = mul(angleAxisToRotation({0.0, 0.6, 0.0}), moved.pose.R);
        moved.point3D_ids.assign(moved.points2D.size(), kInvalidPoint3D);
        bad.images[5] = moved;
        Mapper::AuditStats as;
        const Reconstruction back = m.audit(bad, &as);
        uint32_t added = 0;
        const bool every = allChecked(bad, back, g, added);
        std::printf("audit: %u contradicted, %u added | %s\n", as.unsupported, added,
                    g.events.c_str());
        check(as.unsupported >= 1 && every, "audit: every image its repair registers is checked");
    }
}

// Each camera between a Cauchy(7.815) centre 10 m along x and a quadratic one where
// it stands, 20 times over: every step clears rtol, and it settles after 500 or more
// iterations. `n` 3 states the same factor as an inertial triple.
class PullGps : public PriorSource {
public:
    explicit PullGps(int n) : n_(n) {}
    bool has(uint32_t) const override { return true; }
    bool relativeRotation(uint32_t, uint32_t, Mat3&, double&) const override { return false; }
    std::vector<uint32_t> neighbours(uint32_t) const override { return {}; }
    PosePriors factors(const std::vector<PosedImage>& imgs) override {
        PosePriors p;
        p.huber = 1e9;
        for (const PosedImage& im : imgs) {
            PriorCentre c;
            c.n = n_;
            for (int k = 0; k < 3; k++) c.img[k] = im.image;
            for (int k = 1; k < 3; k++) c.A[k] = Mat3{};
            for (int r = 0; r < 20; r++) {
                c.b = cameraCenter(im.pose) + Vec3{10.0, 0.0, 0.0};
                c.cauchy = 7.815;
                p.centres.push_back(c);
                c.b = cameraCenter(im.pose);
                c.cauchy = 0;
                p.centres.push_back(c);
            }
        }
        return p;
    }

private:
    int n_;
};

// LM iterations per solve of a finishing refinement, and how many solves it ran.
static double finalIters(const Scene& sc, const Reconstruction& model, MapperOptions opt, int n,
                         long& solves) {
    PullGps g(n);
    Mapper m(sc.db, sc.feats, opt, {}, nullptr, nullptr, &g);
    const long b0 = g_map_prof.n_ba, i0 = g_map_prof.n_ba_iters;
    m.refine(model);
    solves = g_map_prof.n_ba - b0;
    return solves ? (double)(g_map_prof.n_ba_iters - i0) / solves : 0;
}

// COLMAP's global BA allows 50 LM iterations; the mapper's own cap is 25.
static void testFinalCap(const Scene& sc, const Reconstruction& full, MapperOptions opt) {
    // Off the seed's identity rotation, whose angle-axis Jacobian the solver drops.
    Sim3 turn;
    turn.R = angleAxisToRotation({0.2, -0.3, 0.1});
    Reconstruction model = full;
    for (auto& kv : model.images) kv.second.pose = transformPose(turn, kv.second.pose);
    for (auto& kv : model.points3D) kv.second.xyz = transformPoint(turn, kv.second.xyz);
    long s1 = 0, s3 = 0, sa = 0;
    const double abs = finalIters(sc, model, opt, 1, s1);
    const double tri = finalIters(sc, model, opt, 3, s3);
    MapperOptions atom = opt;
    atom.ba_final_tight = false;
    atom.ba_growth_rtol = 0;
    const double loose = finalIters(sc, model, atom, 1, sa);
    std::printf("final cap: absolute centres %.1f its x %ld, inertial triples %.1f x %ld, "
                "not a final pass %.1f x %ld\n", abs, s1, tri, s3, loose, sa);
    check(s1 > 0 && abs == 50, "final cap: a finishing solve on GPS centres runs 50 iterations");
    check(s3 > 0 && tri == 25, "final cap: a finishing solve on inertial triples keeps 25");
    check(sa > 0 && loose == 25, "final cap: a solve that is not a final pass keeps 25");
}

static int body(int argc, char** argv) {
    MapperOptions opt;
    opt.verbose = false;
    opt.focal = 1200;
    opt.focal_trials = 0;   // no bootstrap growth: one pass consumes the script
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--device" && i + 1 < argc) opt.device = std::stoi(argv[++i]);
        else if (a == "--verbose") opt.verbose = true;
    }
    const int M = 40;
    const Scene sc = makeScene(M);
    const size_t n = M;

    ScriptedGps base(script(n, {}));
    Reconstruction full;
    const Run r0 = runWith(sc, opt, base, &full);
    std::printf("in gate: %u/%d registered, %zu model(s), %zu checks | ba %u refused %u out %u\n  %s\n",
                r0.registered, M, r0.models, base.at.size(), r0.st.gps_ba, r0.st.gps_refused,
                r0.st.gps_out, base.events.c_str());
    check(r0.registered == (uint32_t)M && r0.models == 1, "in gate: every image registers");
    check(base.at.size() >= 30, "in gate: the registrations are checked");
    check(r0.st.gps_ba == 0 && r0.st.gps_refused == 0 && r0.st.gps_out == 0,
          "in gate: no trigger, no refusal");

    // The run must end on a registration the plain schedule does not solve
    // after, or an ordinary BA would stand in for the triggered one.
    size_t j = 14;
    while (j + 5 < base.at.size() && base.solvedAfter(j)) j++;
    std::printf("run ends at script entry %zu (plain schedule solves after it: %d)\n", j,
                (int)base.solvedAfter(j));
    check(j >= 12 && j + 5 < base.at.size() && !base.solvedAfter(j),
          "fixture: a registration the ordinary schedule does not solve after");

    ScriptedGps three(script(n, {{j - 2, 6.0}, {j - 1, 6.0}, {j, 6.0}}));
    const Run r1 = runWith(sc, opt, three);
    std::printf("three at 6 m: ba %u refused %u out %u, solve right after: %d\n  %s\n", r1.st.gps_ba,
                r1.st.gps_refused, r1.st.gps_out, (int)three.solvedAfter(j), three.events.c_str());
    check(r1.st.gps_ba == 1 && r1.st.gps_out == 3, "trigger: three in a row beyond the radius");
    check(three.solvedAfter(j), "trigger: the BA runs on the third registration");
    check(r1.registered == (uint32_t)M, "trigger: every image still registers");

    ScriptedGps alt(script(n, {{j - 4, 6.0}, {j - 2, 6.0}, {j, 6.0}}));
    const Run r2 = runWith(sc, opt, alt);
    std::printf("6,1,6,1,6: ba %u out %u\n", r2.st.gps_ba, r2.st.gps_out);
    check(r2.st.gps_ba == 0 && r2.st.gps_out == 3, "trigger: an in-radius registration breaks the run");

    // Every one of the first ten registrations here is followed by an ordinary
    // BA, so the rate limit is tested after the triggered one: a second run
    // straight after it waits for ten registrations, a third one later fires.
    size_t k = j + 3, m = j + 13;
    while (m + 1 < three.at.size() && three.solvedAfter(m)) m++;
    std::printf("rate limit: second run ends at %zu (plain solve after: %d), third at %zu (%d)\n", k,
                (int)three.solvedAfter(k), m, (int)three.solvedAfter(m));
    check(!three.solvedAfter(k) && !three.solvedAfter(m) && m < three.at.size(),
          "fixture: no ordinary BA right after either later run");
    ScriptedGps limited(script(n, {{j - 2, 6.0}, {j - 1, 6.0}, {j, 6.0}, {k - 2, 6.0}, {k - 1, 6.0},
                                   {k, 6.0}, {m - 2, 6.0}, {m - 1, 6.0}, {m, 6.0}}));
    const Run r3 = runWith(sc, opt, limited);
    std::printf("three runs: ba %u, solve after each %d %d %d\n", r3.st.gps_ba,
                (int)limited.solvedAfter(j), (int)limited.solvedAfter(k), (int)limited.solvedAfter(m));
    check(r3.st.gps_ba == 2 && limited.solvedAfter(j) && !limited.solvedAfter(k) &&
              limited.solvedAfter(m),
          "trigger: ten registrations between triggered BAs");

    ScriptedGps far(script(n, {{j, 25.0}}));
    const Run r4 = runWith(sc, opt, far);
    const uint32_t far_img = far.image_at.count(j) ? far.image_at.at(j) : ~0u;
    const int far_calls = far.calls.count(far_img) ? far.calls.at(far_img) : 0;
    std::printf("one at 25 m: refused %u, ba %u, image %u checked %d time(s), %u/%d registered\n",
                r4.st.gps_refused, r4.st.gps_ba, far_img, far_calls, r4.registered, M);
    check(r4.st.gps_refused == 1 && r4.st.gps_ba == 0, "refuse: four radii off after an in-radius one");
    check(far_calls >= 2 && r4.registered == (uint32_t)M, "refuse: the image registers on a later trial");

    // A drifting run reaches 25 m from beyond the radius, never from inside it.
    ScriptedGps drift(script(n, {{j - 2, 6.0}, {j - 1, 25.0}, {j, 25.0}}));
    const Run r5 = runWith(sc, opt, drift);
    std::printf("6 then two at 25 m: refused %u, ba %u, %u/%d registered\n", r5.st.gps_refused,
                r5.st.gps_ba, r5.registered, M);
    check(r5.st.gps_refused == 0 && r5.st.gps_ba == 1, "refuse: never a drifting run");
    check(r5.registered == (uint32_t)M, "refuse: the drifting run registers");

    testAssembly(sc, full, opt);
    testFinalCap(sc, full, opt);

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
