// The GPS path-length check during growth (Mapper::gpsScaleCheck): a 40-camera
// corridor whose fixes are the true centres, except that the last eight cameras' GPS
// track is stretched by `k` about camera 32, which the images cannot follow, and
// every fix carries 0.2 m of horizontal jitter. The BA is real (GPU).
//
//   sfm_gps_scale_test [--device N] [--verbose]
//
// Prints FAIL lines and returns the count.
#include <cmath>
#include <cstdio>
#include <optional>
#include <random>
#include <string>
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

constexpr int kCams = 40, kPts = 5000, kW = 1280, kH = 960;

struct Scene {
    MatchesDatabase db;
    std::vector<FeatureSet> feats;
    std::vector<Vec3> centres;
};

static Scene makeScene() {
    Scene s;
    const Camera K = Camera::defaultFor(1, kW, kH, 1200);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> ux(-5, 45), uy(-3, 3), uz(7, 13);
    std::normal_distribution<double> noise(0.0, 0.3);
    std::vector<Vec3> pts(kPts);
    for (auto& p : pts) p = {ux(rng), uy(rng), uz(rng)};
    std::vector<std::vector<char>> vis(kCams, std::vector<char>(kPts, 0));
    s.feats.resize(kCams);
    for (int c = 0; c < kCams; c++) {
        const Vec3 C = {1.0 * c, 0.3 * std::sin(0.4 * c), 1.5 * std::sin(0.2 * c)};
        s.centres.push_back(C);
        s.feats[c].width = kW;
        s.feats[c].height = kH;
        s.feats[c].keypoints.resize(kPts);
        for (int p = 0; p < kPts; p++) {
            const Vec3 pc = pts[p] - C;
            const Vec2 px = K.project(pc);
            if (pc.z > 0.1 && px.x > 0 && px.x < kW && px.y > 0 && px.y < kH) {
                const float x = (float)(px.x + noise(rng)), y = (float)(px.y + noise(rng));
                s.feats[c].keypoints[p] = {x, y, 2, 0, 0};
                vis[c][p] = 1;
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
            for (int p = 0; p < kPts; p++)
                if (vis[i][p] && vis[j][p]) tv.matches.push_back({(uint32_t)p, (uint32_t)p, 0});
            if (tv.matches.size() >= 15) s.db.pairs.push_back(std::move(tv));
        }
    return s;
}

// Metres east/north of a reference as a fix, by the local radii of curvature.
static Geodetic fixAt(double e, double n, double alt) {
    constexpr double lat0 = 42.2, lon0 = -83.6;
    constexpr double a = 6378137.0, f = 1.0 / 298.257223563, e2 = f * (2.0 - f);
    const double p = lat0 * M_PI / 180.0, w = 1.0 - e2 * std::sin(p) * std::sin(p);
    const double M = a * (1.0 - e2) / std::pow(w, 1.5), N = a / std::sqrt(w);
    return {lat0 + n / M * 180.0 / M_PI, lon0 + e / (N * std::cos(p)) * 180.0 / M_PI, alt};
}

struct Out {
    uint32_t scale_ba = 0, gps_ba = 0, registered = 0;
    Reconstruction model;
};

// A receiver's fix-to-fix jitter lengthens its path: 0.2 m on 1 m steps reads ~4 % long.
static double g_jitter = 0.2;

static Out run(const Scene& sc, MapperOptions opt, double k, double band) {
    const int from = kCams - 8;
    std::mt19937 rng(23);
    std::normal_distribution<double> jit(0.0, g_jitter);
    std::vector<std::optional<Geodetic>> fixes(kCams);
    for (int c = 0; c < kCams; c++) {
        const Vec3 o = sc.centres[from];
        const Vec3 C = c < from ? sc.centres[c] : o + (sc.centres[c] - o) * k;
        const double je = jit(rng), jn = jit(rng);
        fixes[c] = fixAt(C.x + je, C.z + jn, 250.0 - C.y);
    }
    ExifGpsPriors gps(fixes, SensorPriorOptions{});
    opt.gps_scale_band = band;
    Mapper m(sc.db, sc.feats, opt, {}, nullptr, nullptr, &gps);
    std::vector<Reconstruction> models = m.run();
    Out r;
    r.scale_ba = m.priorStats().gps_scale_ba;
    r.gps_ba = m.priorStats().gps_ba;
    if (!models.empty()) {
        r.model = models.front();
        r.registered = r.model.numRegistered();
    }
    return r;
}

static bool samePoses(const Reconstruction& a, const Reconstruction& b) {
    if (a.images.size() != b.images.size() || a.points3D.size() != b.points3D.size()) return false;
    for (const auto& kv : a.images) {
        auto it = b.images.find(kv.first);
        if (it == b.images.end()) return false;
        for (int j = 0; j < 9; j++)
            if (kv.second.pose.R[j] != it->second.pose.R[j]) return false;
        const Vec3 &p = kv.second.pose.t, &q = it->second.pose.t;
        if (p.x != q.x || p.y != q.y || p.z != q.z) return false;
    }
    return true;
}

static int body(int argc, char** argv) {
    MapperOptions opt;
    opt.verbose = false;
    opt.focal = 1200;
    opt.focal_trials = 0;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--device" && i + 1 < argc) opt.device = std::stoi(argv[++i]);
        else if (a == "--verbose") opt.verbose = true;
        else if (a == "--jitter" && i + 1 < argc) g_jitter = std::stod(argv[++i]);
    }
    const Scene sc = makeScene();
    const Out same = run(sc, opt, 1.0, 0.03), small = run(sc, opt, 1.01, 0.03);
    const Out big = run(sc, opt, 1.3, 0.03), off = run(sc, opt, 1.3, 0);
    const Out off_same = run(sc, opt, 1.0, 0);
    std::printf("requests: true scale %u, 1%% %u, 30%% %u (gps_ba %u, %u registered), band 0 %u\n",
                same.scale_ba, small.scale_ba, big.scale_ba, big.gps_ba, big.registered,
                off.scale_ba);
    check(same.registered == (uint32_t)kCams && big.registered == (uint32_t)kCams,
          "fixture: every image registers");
    check(same.scale_ba == 0, "scale: a true-scale GPS asks for nothing");
    check(small.scale_ba == 0, "scale: a 1 % stretch is inside the band");
    check(big.scale_ba >= 1, "scale: a stretched GPS track asks for a BA");
    // None before the window fills, then at most one per ten registrations.
    check(big.scale_ba <= (big.registered - (uint32_t)opt.gps_scale_window) / 10,
          "scale: at most one request per ten registrations once the window is full");
    check(off.scale_ba == 0, "scale: a band of 0 asks for nothing");
    check(samePoses(same.model, off_same.model),
          "scale: a check that never fires leaves the model bit-identical");
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
