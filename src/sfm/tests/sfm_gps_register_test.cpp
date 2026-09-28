// The mapper's GPS check on a registration (Mapper::gpsCheck): a scripted
// source says how far each new registration lands from its fix, so the trigger
// for an early global BA and the refusal of a wrong-place PnP are exercised
// apart from any real fit. The scene is synthetic, the BA real (GPU).
//
//   sfm_gps_register_test [--device N] [--verbose]
//
// Prints FAIL lines and returns the count.
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "sfm/core/Model.h"
#include "sfm/core/PriorSource.h"
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
// `events` logs 'P' per check and 'F' per factors() call (one per BA round).
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
        return p;
    }
    bool positionError(uint32_t img, const Pose&, const GpsFrame& f, double& m) const override {
        if (!f.ok) return false;
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

static Run runWith(const Scene& s, MapperOptions opt, ScriptedGps& gps) {
    Mapper m(s.db, s.feats, opt, {}, nullptr, nullptr, &gps);
    std::vector<Reconstruction> models = m.run();
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
    const Run r0 = runWith(sc, opt, base);
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

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
