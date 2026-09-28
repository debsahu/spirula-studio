// The open-seam weld (Mapper::weldSeams): a straight 24-camera track whose halves
// were built as two fronts, so every point both halves see is held twice and the
// east half sits a rigid 2 m / 1.5 deg off. Camera 12, where they meet, is weakly
// attached. The BA is real (GPU).
//
//   sfm_seam_weld_test [--device N] [--verbose]
//
// Prints FAIL lines and returns the count.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "sfm/core/Model.h"
#include "sfm/map/Mapper.h"
#include "sfm/map/SensorPriors.h"
#include "sfm/tests/TestMain.h"

using namespace sfm;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        fails++;
    }
}

constexpr int kCams = 24, kWest = 12, kPts = 4000, kW = 1280, kH = 960;
constexpr uint32_t kDupA = 3, kDupB = 4;  // the near-duplicate pair: a real link, junk-diluted

struct Scene {
    MatchesDatabase db;
    std::vector<FeatureSet> feats;
    std::vector<Vec3> pts;
    std::vector<Vec3> centres;
    std::vector<std::vector<char>> vis;
    Camera K;
};

static Scene makeScene() {
    Scene s;
    s.K = Camera::defaultFor(1, kW, kH, 1200);
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> ux(-5, 28), uy(-3, 3), uz(7, 13);
    std::normal_distribution<double> noise(0.0, 0.3);
    s.pts.resize(kPts);
    for (auto& p : s.pts) p = {ux(rng), uy(rng), uz(rng)};
    s.feats.resize(kCams);
    s.vis.assign(kCams, std::vector<char>(kPts, 0));
    for (int c = 0; c < kCams; c++) {
        const Vec3 C = {1.0 * c, 0.3 * std::sin(0.4 * c), 1.5 * std::sin(0.2 * c)};
        s.centres.push_back(C);
        s.feats[c].width = kW;
        s.feats[c].height = kH;
        s.feats[c].keypoints.resize(kPts);
        for (int p = 0; p < kPts; p++) {
            const Vec3 pc = s.pts[p] - C;
            const Vec2 px = s.K.project(pc);
            if (pc.z > 0.1 && px.x > 0 && px.x < kW && px.y > 0 && px.y < kH) {
                const float x = (float)(px.x + noise(rng)), y = (float)(px.y + noise(rng));
                s.feats[c].keypoints[p] = {x, y, 2, 0, 0};
                s.vis[c][p] = 1;
            } else {
                s.feats[c].keypoints[p] = {-1000, -1000, 2, 0, 0};
            }
        }
    }
    s.db.images.resize(kCams);
    for (int c = 0; c < kCams; c++) s.db.images[c] = {"cam" + std::to_string(c), (uint32_t)kPts};
    for (int i = 0; i < kCams; i++)
        for (int j = i + 1; j < kCams; j++) {
            TwoViewMatches tv;
            tv.image1 = i;
            tv.image2 = j;
            tv.config = (int)TwoViewConfig::Uncalibrated;
            std::vector<uint32_t> both;
            for (int p = 0; p < kPts; p++)
                if (s.vis[i][p] && s.vis[j][p]) both.push_back(p);
            if ((uint32_t)i == kDupA && (uint32_t)j == kDupB) {
                // 400 true matches and 850 between features of different points:
                // a stationary pair's worth of evidence that no model can explain.
                std::shuffle(both.begin(), both.end(), rng);
                std::vector<uint32_t> spare(both.begin() + 400, both.end());
                both.resize(400);
                std::vector<uint32_t> other = spare;
                std::rotate(other.begin(), other.begin() + 1, other.end());
                for (uint32_t p : both) tv.matches.push_back({p, p, 0});
                for (size_t k = 0; k < spare.size() && k < 850; k++)
                    tv.matches.push_back({spare[k], other[k], 0});
            } else {
                for (uint32_t p : both) tv.matches.push_back({p, p, 0});
            }
            if (tv.matches.size() >= 15) s.db.pairs.push_back(std::move(tv));
        }
    return s;
}

// The east half's error: a rigid move about a point at the join.
struct Piece {
    Mat3 Q = mat3Identity();
    Vec3 o{11.5, 0, 10}, d{0, 0, 0};
    Vec3 apply(const Vec3& x) const { return mul(Q, x - o) + o + d; }
};

// `seam` false: one front, every track whole, every pose true.
static Reconstruction makeModel(const Scene& s, bool seam) {
    Piece east;
    if (seam) {
        east.Q = angleAxisToRotation({0, 1.5 * M_PI / 180.0, 0});
        east.d = {2.0, 0, 0};
    }
    Reconstruction m;
    m.cameras[1] = s.K;
    for (int c = 0; c < kCams; c++) {
        Image im;
        im.id = c;
        im.camera_id = 1;
        im.name = s.db.images[c].name;
        im.registered = true;
        const bool e = seam && c >= kWest;
        const Vec3 C = e ? east.apply(s.centres[c]) : s.centres[c];
        const Mat3 R = e ? transpose(east.Q) : mat3Identity();
        const Vec3 t = mul(R, C);
        im.pose = {R, {-t.x, -t.y, -t.z}};
        for (const Keypoint& k : s.feats[c].keypoints) im.points2D.push_back({k.x, k.y});
        im.point3D_ids.assign(kPts, kInvalidPoint3D);
        m.images[c] = im;
    }
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(0, 1);
    auto add = [&](const Vec3& X, const std::vector<TrackElement>& tr) {
        if (tr.size() < 2) return;
        const uint64_t id = m.addPoint3D(X, tr);
        for (const TrackElement& e : tr) m.images[e.image_id].point3D_ids[e.point2D_idx] = id;
    };
    for (uint32_t p = 0; p < (uint32_t)kPts; p++) {
        std::vector<TrackElement> west, rest;
        for (uint32_t c = 0; c < (uint32_t)kCams; c++) {
            if (!s.vis[c][p]) continue;
            // Camera 12 registered last on little support: most of what it sees is free.
            if (seam && c == kWest && u(rng) < 0.62) continue;
            (seam && c < (uint32_t)kWest ? west : rest).push_back({c, p});
        }
        add(s.pts[p], west);
        add(seam ? east.apply(s.pts[p]) : s.pts[p], rest);
    }
    return m;
}

static bool sameModel(const Reconstruction& a, const Reconstruction& b) {
    if (a.images.size() != b.images.size() || a.points3D.size() != b.points3D.size()) return false;
    for (const auto& kv : a.images) {
        auto it = b.images.find(kv.first);
        if (it == b.images.end()) return false;
        const Pose &p = kv.second.pose, &q = it->second.pose;
        for (int k = 0; k < 9; k++)
            if (p.R[k] != q.R[k]) return false;
        if (p.t.x != q.t.x || p.t.y != q.t.y || p.t.z != q.t.z) return false;
        if (kv.second.point3D_ids != it->second.point3D_ids) return false;
    }
    for (const auto& kv : a.points3D) {
        auto it = b.points3D.find(kv.first);
        if (it == b.points3D.end()) return false;
        const Vec3 &x = kv.second.xyz, &y = it->second.xyz;
        if (x.x != y.x || x.y != y.y || x.z != y.z) return false;
        if (kv.second.track.size() != it->second.track.size()) return false;
    }
    return true;
}

// |C12 - C11| over the median consecutive step, and the relative rotation there (deg).
static void joinGeometry(const Reconstruction& m, double& step_ratio, double& kink_deg) {
    std::vector<Vec3> C;
    std::vector<Mat3> R;
    for (int c = 0; c < kCams; c++) {
        const Pose& p = m.images.at(c).pose;
        C.push_back(cameraCenter(p));
        R.push_back(p.R);
    }
    std::vector<double> steps;
    for (int c = 0; c + 1 < kCams; c++) steps.push_back((C[c + 1] - C[c]).norm());
    const double at = steps[kWest - 1];
    std::nth_element(steps.begin(), steps.begin() + steps.size() / 2, steps.end());
    step_ratio = at / steps[steps.size() / 2];
    const Mat3 D = mul(R[kWest], transpose(R[kWest - 1]));
    const double c = std::max(-1.0, std::min(1.0, (D[0] + D[4] + D[8] - 1) / 2));
    kink_deg = std::acos(c) * 180.0 / M_PI;
}

// Metres east/north of a reference as a fix, by the local radii of curvature.
static Geodetic fixAt(double e, double n, double alt) {
    constexpr double lat0 = 42.2, lon0 = -83.6;
    constexpr double a = 6378137.0, f = 1.0 / 298.257223563, e2 = f * (2.0 - f);
    const double p = lat0 * M_PI / 180.0, w = 1.0 - e2 * std::sin(p) * std::sin(p);
    const double M = a * (1.0 - e2) / std::pow(w, 1.5), N = a / std::sqrt(w);
    return {lat0 + n / M * 180.0 / M_PI, lon0 + e / (N * std::cos(p)) * 180.0 / M_PI, alt};
}

static std::set<std::pair<uint32_t, uint32_t>> pairsOf(const std::vector<Mapper::SeamPair>& v) {
    std::set<std::pair<uint32_t, uint32_t>> s;
    for (const Mapper::SeamPair& p : v) s.insert({std::min(p.a, p.b), std::max(p.a, p.b)});
    return s;
}

static int body(int argc, char** argv) {
    int gps_cap = 3;
    MapperOptions opt;
    opt.verbose = false;
    opt.focal = 1200;
    opt.focal_trials = 0;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--device" && i + 1 < argc) opt.device = std::stoi(argv[++i]);
        else if (a == "--verbose") opt.verbose = true;
        else if (a == "--gps-cap" && i + 1 < argc) gps_cap = std::stoi(argv[++i]);
    }
    const Scene sc = makeScene();
    const Reconstruction healthy = makeModel(sc, false);
    const Reconstruction seam = makeModel(sc, true);

    std::set<std::pair<uint32_t, uint32_t>> across;
    for (const TwoViewMatches& p : sc.db.pairs)
        if ((p.image1 < (uint32_t)kWest) != (p.image2 < (uint32_t)kWest) &&
            p.matches.size() >= (size_t)opt.seam_min_matches)
            across.insert({std::min(p.image1, p.image2), std::max(p.image1, p.image2)});

    {
        Mapper m(sc.db, sc.feats, opt);
        size_t strong = 0;
        const auto open = m.openSeams(seam, &strong);
        const auto found = pairsOf(open);
        std::printf("seam model: %zu open of %zu strong, %zu pairs cross the join\n", open.size(),
                    strong, across.size());
        check(across.size() >= 40, "fixture: the join has strong pairs across it");
        check(!found.empty() && found == across, "detector: exactly the pairs across the join");
        check(!found.count({kDupA, kDupB}), "detector: the diluted pair is not a seam");

        MapperOptions loose = opt;
        loose.seam_weld_frac = 0.5;
        Mapper ml(sc.db, sc.feats, loose);
        const auto wide = pairsOf(ml.openSeams(seam));
        check(wide.count({kDupA, kDupB}) == 1, "fixture: a 0.5 bar takes the diluted pair");
    }
    {
        double r0, k0;
        joinGeometry(seam, r0, k0);
        Mapper m(sc.db, sc.feats, opt);
        Mapper::SeamStats st;
        const Reconstruction out = m.weldSeams(seam, &st);
        double r1, k1;
        joinGeometry(out, r1, k1);
        double worst = 1;
        for (const Mapper::SeamPair& p : st.after) worst = std::min(worst, p.frac());
        std::printf("weld: %zu pair(s), %zu point(s) fused; join step x%.2f -> x%.2f, kink "
                    "%.2f -> %.3f deg; weakest welded pair after %.3f; reproj %.3f -> %.3f px\n",
                    st.open.size(), st.points, r0, r1, k0, k1, worst, st.reproj_before,
                    st.reproj_after);
        check(r0 > 2.0 && k0 > 1.4, "fixture: the join steps and kinks before the weld");
        check(st.points > 0 && st.after.size() == st.open.size(), "weld: the open pairs are fused");
        check(!st.after.empty() && worst >= 0.40,
              "weld: every welded pair is explained at 0.40 or more");
        check(std::fabs(r1 - 1.0) <= 0.1, "weld: the step at the join closes");
        check(k1 < 0.3, "weld: the kink at the join closes");
        check(st.reproj_after > 0 && st.reproj_after < 1.0, "weld: the welded model reprojects");
        check(out.numRegistered() == (uint32_t)kCams, "weld: every image stays registered");
    }
    {
        // --metric-gps full at the true centres, each round capped at 3 LM iterations, as
        // Hickory's final solves are: the fused points must survive a round that ends short.
        std::vector<std::optional<Geodetic>> fixes(kCams);
        for (int c = 0; c < kCams; c++)
            fixes[c] = fixAt(sc.centres[c].x, sc.centres[c].z, 250.0 - sc.centres[c].y);
        SensorPriorOptions po;
        po.trusted_position = true;
        ExifGpsPriors gps(fixes, po);
        MapperOptions capped = opt;
        capped.ba_final_prior_max_iters = gps_cap;
        Mapper m(sc.db, sc.feats, capped, {}, nullptr, nullptr, &gps);
        Mapper::SeamStats st;
        const Reconstruction out = m.weldSeams(seam, &st);
        double r1, k1;
        joinGeometry(out, r1, k1);
        double worst = 1;
        for (const Mapper::SeamPair& p : st.after) worst = std::min(worst, p.frac());
        std::printf("weld under GPS, %d LM its a round: %zu point(s) fused; join step x%.2f, "
                    "kink %.3f deg; weakest welded pair after %.3f\n", gps_cap, st.points, r1,
                    k1, worst);
        check(std::fabs(r1 - 1.0) <= 0.1, "weld, capped under GPS: the step at the join closes");
        check(k1 < 0.3, "weld, capped under GPS: the kink at the join closes");
    }
    {
        MapperOptions off = opt;
        off.seam_weld_frac = 0;
        Mapper m(sc.db, sc.feats, off);
        Mapper::SeamStats st;
        const Reconstruction out = m.weldSeams(seam, &st);
        check(st.open.empty() && sameModel(out, seam),
              "off: a bar of 0 finds nothing and returns the model untouched");
    }
    {
        Mapper m(sc.db, sc.feats, opt);
        Mapper::SeamStats st;
        const Reconstruction out = m.weldSeams(healthy, &st);
        std::printf("healthy model: %zu open of %zu strong\n", st.open.size(), st.strong);
        check(st.strong > 100, "fixture: the healthy model has strong pairs to judge");
        check(st.open.empty() && sameModel(out, healthy),
              "healthy: no open seam, and the model comes back untouched");
    }

    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
