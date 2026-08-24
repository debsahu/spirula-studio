// Rolling shutter: recover a synthetic capture's readout direction and time,
// and confirm a global-shutter capture of the same scene produces no fit.
//
// The generator projects by the IMPLICIT rule a renderer needs -- iterate until
// the row a point lands on agrees with the shutter time used to place it --
// while the estimator uses the explicit measured-row rule. A flipped axis
// convention or a sign error shows up as a failure to recover the readout.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "sfm/ba/CpuCamera.h"
#include "sfm/core/RollingShutter.h"
#include "sfm/map/Merge.h"
#include "sfm/map/RollingShutterFit.h"
#include "sfm/tests/TestMain.h"

using namespace sfm;

static int g_fail = 0;

static void report(const char* name, double err, double tol) {
    const bool ok = err < tol && std::isfinite(err);
    printf("%-42s err %.3e (tol %.0e)  %s\n", name, err, tol, ok ? "PASS" : "FAIL");
    if (!ok) g_fail++;
}

static void check(const char* name, bool ok) {
    printf("%-42s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) g_fail++;
}

// Where X lands under a rolling shutter, by iterating the row against the
// shutter time until they agree.
static bool projectRolling(const Camera& cam, const Pose& ref, const Twist& xi,
                           const ShutterAxis& ax, const Vec3& X, Vec2& out) {
    Vec3 pc = mul(ref.R, X) + ref.t;
    if (pc.z <= 1e-6) return false;
    Vec2 px = cam.project(pc);
    for (int it = 0; it < 50; it++) {
        const Pose p = poseAtShutterTime(ref, xi, ax.time(px));
        pc = mul(p.R, X) + p.t;
        if (pc.z <= 1e-6) return false;
        const Vec2 q = cam.project(pc);
        const double d = std::fabs(q.x - px.x) + std::fabs(q.y - px.y);
        px = q;
        if (d < 1e-11) break;
    }
    out = px;
    return px.x >= 0 && px.y >= 0 && px.x < cam.width && px.y < cam.height;
}

// A capture walking past a point cloud while turning: both parts of the twist
// are non-zero, and the frame numbers are deliberately uneven so the readout
// has to divide by the real interval rather than by a frame count.
struct Synth {
    Reconstruction rec;
    std::vector<uint32_t> ids;
    std::vector<double> ts;
};

static Synth makeCapture(int nImg, int nPt, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    std::uniform_int_distribution<int> gap(11, 19);

    Synth S;
    Camera K = Camera::defaultFor(1, 1920, 1080, 1400);
    K.model = CamModel::Pinhole;
    S.rec.cameras[1] = K;

    double frame = 7;
    for (int i = 0; i < nImg; i++) {
        Image im;
        im.id = (uint32_t)(i + 1);
        im.camera_id = 1;
        char buf[64];
        snprintf(buf, sizeof buf, "images/%05d.jpg", (int)frame);
        im.name = buf;
        S.ts.push_back(frame);
        frame += gap(rng);

        const double t = 0.06 * i;
        const Vec3 c{2.5 * std::sin(t), 0.35 * std::sin(1.7 * t), -6.0 + 1.3 * t};
        const double yaw = 0.22 * std::sin(0.9 * t), pitch = 0.13 * std::sin(1.3 * t + 0.4);
        const Mat3 R = mul(angleAxisToRotation({pitch, 0, 0}), angleAxisToRotation({0, yaw, 0}));
        im.pose.R = R;
        im.pose.t = mul(R, c) * -1.0;
        im.registered = true;
        S.rec.images[im.id] = im;
        S.ids.push_back(im.id);
    }
    for (int p = 0; p < nPt; p++) {
        Point3D pt;
        pt.xyz = {3.0 * u(rng), 2.0 * u(rng), 2.5 * u(rng)};
        S.rec.points3D[(uint64_t)p + 1] = pt;
    }
    return S;
}

// Observations for a capture whose images move by `twists` during readout.
static void renderObservations(Synth& S, const std::map<uint32_t, Twist>& twists,
                               const ShutterAxis& ax) {
    const Camera& K = S.rec.cameras.at(1);
    for (auto& kv : S.rec.points3D) kv.second.track.clear();
    for (uint32_t id : S.ids) {
        Image& im = S.rec.images.at(id);
        im.points2D.clear();
        im.point3D_ids.clear();
        auto it = twists.find(id);
        const Twist xi = (it == twists.end()) ? Twist{} : it->second;
        for (auto& kv : S.rec.points3D) {
            Vec2 px;
            if (!projectRolling(K, im.pose, xi, ax, kv.second.xyz, px)) continue;
            kv.second.track.push_back({id, (uint32_t)im.points2D.size()});
            im.points2D.push_back(px);
            im.point3D_ids.push_back(kv.first);
        }
        im.registered = true;
    }
}

// The twist the estimator would derive at `readout`, for the capture as built.
static std::map<uint32_t, Twist> trueTwists(const Synth& S, double readout, TimeUnit& unit) {
    std::vector<ShutterSequence> seqs = buildShutterSequences(S.rec, unit);
    std::map<uint32_t, Twist> tw;
    if (!seqs.empty()) twistsForSequence(S.rec, seqs[0], readout, tw);
    return tw;
}

static void testRecovery(ShutterDir dir, double ratio, const char* label) {
    Synth S = makeCapture(48, 260, 5);
    TimeUnit unit = TimeUnit::Frames;
    std::vector<ShutterSequence> seqs = buildShutterSequences(S.rec, unit);
    const double readout = ratio * seqs[0].median_dt;
    const Camera& K = S.rec.cameras.at(1);
    const ShutterAxis ax = ShutterAxis::of(dir, K.width, K.height);

    std::map<uint32_t, Twist> tw = trueTwists(S, readout, unit);
    renderObservations(S, tw, ax);

    RollingShutterOptions opt;
    opt.mode = "auto";
    opt.verbose = false;
    ShutterFit fit = fitShutter(S.rec, seqs[0], opt);

    char name[96];
    snprintf(name, sizeof name, "%s direction", label);
    check(name, fit.dir == dir);
    snprintf(name, sizeof name, "%s readout", label);
    report(name, std::fabs(fit.readout - readout) / std::fabs(readout), 0.05);
}

// A global-shutter capture must come back Global: the guard is what keeps a
// tripod run from being fitted to its own noise.
static void testNegativeControl() {
    Synth S = makeCapture(48, 260, 9);
    std::map<uint32_t, Twist> none;
    renderObservations(S, none, ShutterAxis{});
    for (auto& kv : S.rec.images) {
        std::mt19937 rng(kv.first);
        std::normal_distribution<double> n(0.0, 0.3);
        for (Vec2& p : kv.second.points2D) {
            p.x += n(rng);
            p.y += n(rng);
        }
    }
    TimeUnit unit = TimeUnit::Frames;
    std::vector<ShutterSequence> seqs = buildShutterSequences(S.rec, unit);
    RollingShutterOptions opt;
    opt.mode = "auto";
    opt.verbose = false;
    ShutterFit fit = fitShutter(S.rec, seqs[0], opt);
    check("global-shutter capture -> no fit", fit.dir == ShutterDir::Global);
}

// The residual the BA kernels evaluate (bacpu::applyShutter, a Jet-templated
// Rodrigues) against poseAtShutterTime (a Mat3 one).
static void testShutterAgreesWithPose() {
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    double worst = 0;
    for (int trial = 0; trial < 200; trial++) {
        Twist xi{{0.04 * u(rng), 0.04 * u(rng), 0.04 * u(rng)},
                 {0.3 * u(rng), 0.3 * u(rng), 0.3 * u(rng)}};
        const Vec3 X{2.0 * u(rng), 2.0 * u(rng), 4.0 + u(rng)};
        const Vec2 obs{900 + 400 * u(rng), 500 + 300 * u(rng)};
        const double rs[8] = {xi.omega.x, xi.omega.y, xi.omega.z, xi.v.x,
                              xi.v.y,     xi.v.z,     0.0,        1.0 / 1080.0};
        const double p[3] = {X.x, X.y, X.z};
        double out[3];
        bacpu::applyShutter<double>(rs, &obs.x, p, out);

        const ShutterAxis ax{rs[6], rs[7]};
        Pose id;
        id.R = mat3Identity();
        id.t = {0, 0, 0};
        const Pose ps = poseAtShutterTime(id, xi, ax.time(obs));
        const Vec3 ref = mul(ps.R, X) + ps.t;
        worst = std::max(worst, std::fabs(out[0] - ref.x) + std::fabs(out[1] - ref.y) +
                                    std::fabs(out[2] - ref.z));
    }
    report("bacpu::applyShutter vs poseAtShutterTime", worst, 1e-12);
}

static void testSidecarRoundTrip() {
    RollingShutterData a;
    a.unit = TimeUnit::Frames;
    a.cameras[3] = {ShutterDir::Horizontal, -0.0731};
    a.images[11] = {Twist{{0.1, -0.2, 0.3}, {1.5, -2.5, 3.5}}, 42.0};
    a.images[12] = {Twist{}, 57.0};
    const std::string dir = ".";
    writeRollingShutter(dir, a);
    RollingShutterData b;
    check("sidecar reads back", readRollingShutter(dir, b));
    const bool same = b.unit == a.unit && b.cameras.size() == 1 && b.images.size() == 2 &&
                      b.cameras.at(3).dir == ShutterDir::Horizontal &&
                      b.cameras.at(3).readout == -0.0731 && b.images.at(11).timestamp == 42.0 &&
                      b.images.at(11).twist.v.y == -2.5 && b.images.at(12).twist.isZero();
    check("sidecar round-trips", same);
    std::remove("rolling_shutter.bin");
    std::remove("rolling_shutter.txt");
}

static void testFrameNumbers() {
    std::vector<double> out;
    check("frame numbers from names",
          frameNumbers({"images/00013.jpg", "images/cam0/00029.png", "x/img_7.jpg"}, out) &&
              out[0] == 13 && out[1] == 29 && out[2] == 7);
    check("duplicate frame numbers rejected",
          !frameNumbers({"a/00013.jpg", "b/00013.jpg"}, out));
    check("nameless frames rejected", !frameNumbers({"alpha.jpg", "beta.jpg"}, out));
}

// Mean camera-centre and rotation error against `ref`, after the Sim3 that
// aligns the two gauges -- without it this measures the gauge, not the error.
static void poseError(const Reconstruction& ref, const Reconstruction& m, double& dc, double& dr) {
    std::vector<Vec3> src, dst;
    std::vector<uint32_t> ids;
    for (const auto& kv : ref.images) {
        auto it = m.images.find(kv.first);
        if (it == m.images.end() || !it->second.registered) continue;
        src.push_back(cameraCenter(it->second.pose));
        dst.push_back(cameraCenter(kv.second.pose));
        ids.push_back(kv.first);
    }
    Sim3 T;
    dc = dr = 1e9;
    if (ids.size() < 3 || !estimateSim3(src, dst, T)) return;
    Vec3 mid{0, 0, 0};
    for (const Vec3& d : dst) mid = mid + d;
    mid = mid * (1.0 / dst.size());
    double sc = 0, sr = 0, rad = 0;
    for (size_t k = 0; k < ids.size(); k++) {
        const Pose p = transformPose(T, m.images.at(ids[k]).pose);
        const Pose& r = ref.images.at(ids[k]).pose;
        const Vec3 a = cameraCenter(r);
        sc += (a - cameraCenter(p)).norm();
        rad += (a - mid).norm();
        sr += rotationToAngleAxis(mul(p.R, transpose(r.R))).norm();
    }
    dc = (sc / ids.size()) / std::max(rad / ids.size(), 1e-12);
    dr = sr / ids.size() * 180.0 / M_PI;
}

// The claim the whole pass exists for: a global-shutter bundle adjustment bends
// a rolling-shutter capture, and the pass puts it back. Runs on the host
// solver, so it asserts the same thing on a machine with no usable GPU.
static void testPoseRecovery() {
    Synth S = makeCapture(40, 220, 17);
    TimeUnit unit = TimeUnit::Frames;
    std::vector<ShutterSequence> seqs = buildShutterSequences(S.rec, unit);
    const double readout = 0.8 * seqs[0].median_dt;
    const Camera& K = S.rec.cameras.at(1);
    const ShutterAxis ax = ShutterAxis::of(ShutterDir::Vertical, K.width, K.height);
    std::map<uint32_t, Twist> tw = trueTwists(S, readout, unit);

    Reconstruction ref = S.rec;
    renderObservations(S, tw, ax);

    BundleOptions bo;
    bo.real = RealCfg::CPU;
    bo.verbose = false;
    RollingShutterOptions opt;
    opt.mode = "auto";
    opt.verbose = false;

    Reconstruction gs = S.rec;
    runGlobalBA(gs, bo);
    double gs_c, gs_r;
    poseError(ref, gs, gs_c, gs_r);

    RollingShutterData out;
    const bool ran = runRollingShutterPass(S.rec, opt, bo, out);
    double rs_c, rs_r;
    poseError(ref, S.rec, rs_c, rs_r);

    printf("   global shutter: centre %.3f%% rotation %.4f deg | "
           "rolling shutter: centre %.3f%% rotation %.4f deg\n",
           100 * gs_c, gs_r, 100 * rs_c, rs_r);
    check("rolling-shutter pass ran", ran);
    check("pass wrote a shutter for the camera",
          out.cameras.count(1) && out.cameras.at(1).dir == ShutterDir::Vertical);
    check("centre error at least 2x better", rs_c < 0.5 * gs_c);
    check("rotation error at least 2x better", rs_r < 0.5 * gs_r);
}

// The case a per-image twist exists for: the camera's instantaneous velocity
// during each readout is NOT what its neighbours imply, so the trajectory
// estimate is systematically wrong and only the data can correct it.
static void testPerImageTwist() {
    Synth S = makeCapture(40, 240, 23);
    TimeUnit unit = TimeUnit::Frames;
    std::vector<ShutterSequence> seqs = buildShutterSequences(S.rec, unit);
    const double readout = 0.8 * seqs[0].median_dt;
    const Camera& K = S.rec.cameras.at(1);
    const ShutterAxis ax = ShutterAxis::of(ShutterDir::Vertical, K.width, K.height);

    std::map<uint32_t, Twist> tw = trueTwists(S, readout, unit);
    std::mt19937 rng(101);
    std::normal_distribution<double> j(0.0, 0.5);
    for (auto& kv : tw) {  // per-frame jitter no neighbour difference can see
        const Twist& t = kv.second;
        const double m = t.omega.norm(), n = t.v.norm();
        kv.second.omega = t.omega + Vec3{m * j(rng), m * j(rng), m * j(rng)};
        kv.second.v = t.v + Vec3{n * j(rng), n * j(rng), n * j(rng)};
    }
    Reconstruction ref = S.rec;
    renderObservations(S, tw, ax);

    BundleOptions bo;
    bo.real = RealCfg::CPU;
    bo.verbose = false;
    RollingShutterOptions opt;
    opt.mode = "auto";
    opt.verbose = false;

    auto twistErr = [&](const RollingShutterData& d) {
        double e = 0, m = 0;
        for (const auto& kv : tw) {
            auto it = d.images.find(kv.first);
            const Twist got = (it == d.images.end()) ? Twist{} : it->second.twist;
            e += (got.omega - kv.second.omega).norm();
            m += kv.second.omega.norm();
        }
        return e / std::max(m, 1e-12);
    };

    Reconstruction a = S.rec;
    RollingShutterData da;
    runRollingShutterPass(a, opt, bo, da);
    double a_c, a_r;
    poseError(ref, a, a_c, a_r);

    Reconstruction b = S.rec;
    RollingShutterData db;
    opt.per_image_twist = true;
    runRollingShutterPass(b, opt, bo, db);
    double b_c, b_r;
    poseError(ref, b, b_c, b_r);

    // The solver on its own: S.rec still carries the true poses and points --
    // rendering only filled in observations -- so this isolates what the 6x6
    // can see from how much of the twist a bundle adjustment absorbed.
    RollingShutterData solo, traj;
    std::map<uint32_t, Twist> t0 = trueTwists(S, readout, unit);
    for (const auto& kv : tw) {
        Twist x = t0[kv.first];
        traj.images[kv.first] = {x, 0};
        refineImageTwist(S.rec, S.rec.images.at(kv.first), K, ax, 0.0, 2.0, t0[kv.first], x);
        solo.images[kv.first] = {x, 0};
    }
    printf("   omega error: no twist %.1f%%, trajectory %.1f%%, solver alone %.1f%%, "
           "through the pass %.1f%%\n",
           100 * twistErr(RollingShutterData{}), 100 * twistErr(traj), 100 * twistErr(solo),
           100 * twistErr(db));
    printf("   poses: trajectory centre %.3f%% rot %.4f deg | per-image centre %.3f%% rot %.4f deg\n",
           100 * a_c, a_r, 100 * b_c, b_r);
    check("the 6x6 solve recovers the true twist", twistErr(solo) < 0.05);
    // The pose is what a reconstruction is judged on. The recovered twist stays
    // worse than the 6x6 alone, and that is the point of the prior: a free twist
    // can absorb pose error instead of the shutter, and does when it is loosened.
    check("per-image twist beats the trajectory on centre", b_c < 0.3 * a_c);
    check("per-image twist beats the trajectory on rotation", b_r < 0.3 * a_r);
}

static int run(int argc, char** argv) {
    (void)argc;
    (void)argv;
    testFrameNumbers();
    testSidecarRoundTrip();
    testShutterAgreesWithPose();
    testRecovery(ShutterDir::Vertical, 0.09, "vertical");
    testRecovery(ShutterDir::Vertical, -0.09, "vertical reversed");
    testRecovery(ShutterDir::Horizontal, 0.09, "horizontal");
    testNegativeControl();
    testPoseRecovery();
    testPerImageTwist();
    printf(g_fail ? "FAIL (%d)\n" : "PASS\n", g_fail);
    return g_fail ? 1 : 0;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, run); }
