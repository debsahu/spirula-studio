// An image folder's EXIF GPS as a prior source (ExifGpsPriors,
// sfm/map/SensorPriors.h): its positions, the pairs they propose, the centre
// factors it states about a posed model, and the pipeline's builder reading
// the fixes off files (makeExifGpsPriors, sfm/Pipeline.cpp).
//
//   sfm_exif_gps_prior_test
//
// Prints FAIL lines and returns the count. Needs no GPU.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "sfm/Pipeline.h"
#include "sfm/core/PriorSource.h"
#include "sfm/feature/GpsPairs.h"
#include "sfm/map/SensorPriors.h"
#include "sfm/tests/TestMain.h"

using namespace sfm;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        fails++;
    }
}

static const double kLat0 = 42.2, kLon0 = -83.6, kAlt0 = 250.0;

// Metres east/north of (kLat0, kLon0) as a fix, by the local radii of
// curvature: good to ~1e-4 relative over a few hundred metres.
static Geodetic fixAt(double e, double n, double alt) {
    constexpr double a = 6378137.0, f = 1.0 / 298.257223563, e2 = f * (2.0 - f);
    const double p = kLat0 * M_PI / 180.0, w = 1.0 - e2 * std::sin(p) * std::sin(p);
    const double M = a * (1.0 - e2) / std::pow(w, 1.5), N = a / std::sqrt(w);
    return {kLat0 + n / M * 180.0 / M_PI, kLon0 + e / (N * std::cos(p)) * 180.0 / M_PI, alt};
}

// ---- positions and the pairs they propose ----------------------------------

static void testPositionsAndPairs() {
    // Eight images 7 m apart walking north and 1 m up each step; #3 has no fix.
    std::vector<std::optional<Geodetic>> fixes(8);
    for (int k = 0; k < 8; k++)
        if (k != 3) fixes[(size_t)k] = fixAt(0.0, 7.0 * k, kAlt0 + k);
    ExifGpsPriors src(fixes, SensorPriorOptions{});
    check(src.positioned() == 7, "positions: seven of eight images positioned");
    check(!src.has(3) && src.has(0) && src.has(7) && !src.has(8), "positions: has() per image");
    Vec3 p0, p3, p7;
    check(!src.position(3, p3), "positions: the image with no fix has no position");
    const bool ok = src.position(0, p0) && src.position(7, p7);
    const Vec3 d = p7 - p0;
    std::printf("positions: #7 - #0 = (%.4f, %.4f, %.4f) m\n", d.x, d.y, d.z);
    check(ok && std::fabs(d.x) < 0.01 && std::fabs(d.y - 49.0) < 0.01 && std::fabs(d.z - 7.0) < 0.01,
          "positions: 49 m north and 7 m up, in east-north-up metres");
    Mat3 R;
    double sig;
    check(!src.relativeRotation(0, 1, R, sig) && src.neighbours(0).empty(),
          "positions: no rotation and no temporal neighbours from GPS alone");

    // 10 m admits each 7 m step and not the 14 m gap the missing fix leaves.
    size_t positioned = 0;
    const std::vector<std::pair<uint32_t, uint32_t>> nearby =
        gpsProximityPairs(src, 8, 10.0, 20, &positioned);
    const std::vector<std::pair<uint32_t, uint32_t>> want = {{0, 1}, {1, 2}, {4, 5}, {5, 6}, {6, 7}};
    std::printf("pairs: %zu within 10 m over %zu positioned images\n", nearby.size(), positioned);
    check(positioned == 7 && nearby == want, "pairs: exactly the adjacent positioned images");

    SensorPriorOptions off;
    off.gps = false;
    ExifGpsPriors quiet(fixes, off);
    check(gpsProximityPairs(quiet, 8, 1000.0, 20).empty(), "pairs: none with gps switched off");

    // Through the renumbering an atom's mapper sees (map/Atoms.h).
    RemappedPriorSource sub(src, {5, 6, 3});
    Vec3 a, b;
    check(sub.position(0, a) && src.position(5, b) && (a - b).norm() == 0.0 && !sub.has(2),
          "pairs: positions survive a renumbering");
}

// ---- centre factors over a posed model ---------------------------------------

// A 40-image loop with a hill, in a model gauge 20x smaller, turned and
// shifted; the GPS carries 0.5 m of noise per axis and #7 has no fix. `fold`
// moves images 20-25 that many metres along the loop in the model alone.
struct Loop {
    std::vector<std::optional<Geodetic>> fixes;
    std::vector<PosedImage> imgs;
    Mat3 Rm;
    double sm = 0.05;
};

// `level` turns every camera upright about the loop's up (the model's Rm z),
// tilted by up to ~2 deg, so the cameras' mean up is the true one.
static Loop makeLoop(double fold, bool level = false) {
    const int n = 40;
    std::mt19937 rng(11);
    std::normal_distribution<double> N(0, 0.5);
    Loop L;
    L.Rm = angleAxisToRotation({0.4, -0.7, 1.9});
    const Vec3 tm{1, 2, 3};
    L.fixes.resize((size_t)n);
    for (int k = 0; k < n; k++) {
        const double t = 2.0 * M_PI * 0.8 * k / n;
        const Vec3 enu{30.0 * std::cos(t), 20.0 * std::sin(t), 6.0 * std::sin(3.0 * t)};
        if (k != 7) L.fixes[(size_t)k] = fixAt(enu.x + N(rng), enu.y + N(rng), kAlt0 + enu.z + N(rng));
        const Vec3 tangent = Vec3{-30.0 * std::sin(t), 20.0 * std::cos(t), 0.0}.normalized();
        const Vec3 placed = k >= 20 && k <= 25 ? enu + tangent * fold : enu;
        PosedImage p;
        p.image = (uint32_t)k;
        p.pose.R = angleAxisToRotation({0.1 * k, 0.3, -0.2});
        if (level) {
            // Camera x right, y down, z forward; forward turns with k.
            const double h = 0.37 * k;
            const Vec3 fwd{std::cos(h), std::sin(h), 0}, down{0, 0, -1};
            const Vec3 right = down.cross(fwd);
            const Mat3 wc{right.x, down.x, fwd.x, right.y, down.y, fwd.y, right.z, down.z, fwd.z};
            const Mat3 tilt = angleAxisToRotation({0.03 * std::sin(1.7 * k), 0.03 * std::cos(2.3 * k), 0});
            p.pose.R = transpose(mul(L.Rm, mul(tilt, wc)));
        }
        const Vec3 c = mul(L.Rm, placed) * L.sm + tm;
        p.pose.t = mul(p.pose.R, c) * -1.0;
        L.imgs.push_back(p);
    }
    return L;
}

static bool folded(uint32_t img) { return img >= 20 && img <= 25; }

static void testFactors() {
    const int n = 40;
    const Loop L = makeLoop(0.0);
    const std::vector<PosedImage>& imgs = L.imgs;
    ExifGpsPriors src(L.fixes, SensorPriorOptions{});
    const PosePriors pf = src.factors(imgs);
    const SensorFactorStats st = src.lastFactors();
    std::printf("factors: %zu centres, %zu rotations, %zu ups | gps ok=%d n=%d out=%d rms %.3f m\n",
                pf.centres.size(), pf.rotations.size(), pf.ups.size(), st.gps_ok, st.gps, st.gps_out,
                st.gps_rms);
    check(st.gps_ok && pf.centres.size() == (size_t)(n - 1),
          "factors: one centre per positioned image (39 of 40)");
    check(st.gps == (int)pf.centres.size(), "factors: the stats count what was stated");
    check(st.gps_out == 0, "factors: nothing beyond the gate on an unfolded loop");
    check(pf.rotations.empty() && pf.ups.empty(), "factors: GPS states no rotation or up");
    check(st.gps_rms > 0.3 && st.gps_rms < 1.2, "factors: fit RMS at the injected noise");

    // Full, not horizontal: no IMU up, so the vertical is kept at 3x the level
    // sigma, the fit's RMS with a floor of 0.2 of the 5 m inlier radius (D75).
    int wrong_img = 0, bad_sigma = 0;
    double r2 = 0, rz2 = 0;
    for (const PriorCentre& f : pf.centres) {
        if (f.n != 1 || f.img[0] == 7 || f.img[0] >= (uint32_t)n) {
            wrong_img++;
            continue;
        }
        if (!(std::fabs(f.sigma.x - 1.0) < 1e-12 && f.sigma.y == f.sigma.x && f.sigma.z == 3.0 * f.sigma.x))
            bad_sigma++;
        const Vec3 d = mul(f.A[0], cameraCenter(imgs[f.img[0]].pose)) - f.b;
        r2 += d.dot(d);
        rz2 += d.z * d.z;
    }
    const double m = std::max<size_t>(pf.centres.size(), 1);
    std::printf("  residual at the truth: %.3f m RMS, vertical %.3f m\n", std::sqrt(r2 / m),
                std::sqrt(rz2 / m));
    check(wrong_img == 0, "factors: single-image factors on positioned ids only");
    check(bad_sigma == 0, "factors: full sigma, vertical 3x the level pair");
    check(std::sqrt(r2 / m) < 1.5 && std::sqrt(rz2 / m) < 1.0,
          "factors: A c - b is the GPS noise at the true poses, vertical included");

    // Four posed images are too few for a similarity; the source says nothing.
    const std::vector<PosedImage> few(imgs.begin(), imgs.begin() + 4);
    const PosePriors none = src.factors(few);
    check(none.centres.empty() && !src.lastFactors().gps_ok, "factors: none under five images");
}

// Six images 15 m off their fixes, three gates: the fit is the other 33's,
// and the six still get a factor carrying their whole residual.
static void testFoldedFactors() {
    const Loop L = makeLoop(15.0);
    ExifGpsPriors src(L.fixes, SensorPriorOptions{});
    const PosePriors pf = src.factors(L.imgs);
    const SensorFactorStats st = src.lastFactors();
    int fold_ok = 0, rest_ok = 0, fold_n = 0, rest_n = 0;
    double fold_min = 1e9, fold_max = 0, rest_max = 0, rest_r2 = 0;
    for (const PriorCentre& f : pf.centres) {
        const double d = (mul(f.A[0], cameraCenter(L.imgs[f.img[0]].pose)) - f.b).norm();
        if (folded(f.img[0])) {
            fold_n++;
            fold_ok += d > 13.5 && d < 16.5;
            fold_min = std::min(fold_min, d);
            fold_max = std::max(fold_max, d);
        } else {
            rest_n++;
            rest_ok += d < 2.5;
            rest_max = std::max(rest_max, d);
            rest_r2 += d * d;
        }
    }
    std::printf("folded: %zu centres, out=%d, rms %.3f m | the six %.2f-%.2f m, the rest <= %.2f m\n",
                pf.centres.size(), st.gps_out, st.gps_rms, fold_min, fold_max, rest_max);
    check(st.gps_ok && pf.centres.size() == 39 && st.gps == 39, "factors: the fold is stated too");
    check(st.gps_out == 6, "factors: six stated beyond the gate");
    check(st.gps_rms > 0.3 && st.gps_rms < 1.2, "factors: RMS over the fit's inliers, not the six");
    // 0.5 m per axis puts single frames near 1.9 m; the rest are held as an RMS.
    check(fold_n == 6 && fold_ok == 6 && rest_n == 33 && rest_ok == 33 &&
              std::sqrt(rest_r2 / 33.0) < 1.5,
          "factors: the folded six carry their residual");
}

// ---- the position check a registration takes ------------------------------

static void testPositionError() {
    const Loop F2 = makeLoop(15.0);
    ExifGpsPriors src(F2.fixes, SensorPriorOptions{});
    double d = -1;
    check(!src.positionError(0, F2.imgs[0].pose, GpsFrame{}, d) && d == -1,
          "check: no error before a fit");
    const std::vector<PosedImage> few(F2.imgs.begin(), F2.imgs.begin() + 4);
    const PosePriors none = src.factors(few);
    check(!none.gps.ok && !src.positionError(0, F2.imgs[0].pose, none.gps, d),
          "check: no error from a solve too small to fit");

    const PosePriors pf = src.factors(F2.imgs);
    int fold_ok = 0, rest_ok = 0, n = 0;
    double fold_lo = 1e9, fold_hi = 0, rest_hi = 0;
    for (const PosedImage& p : F2.imgs) {
        if (!src.positionError(p.image, p.pose, pf.gps, d)) continue;
        n++;
        if (folded(p.image)) {
            fold_ok += d > 13.5 && d < 16.5;
            fold_lo = std::min(fold_lo, d);
            fold_hi = std::max(fold_hi, d);
        } else {
            rest_ok += d < 2.0;
            rest_hi = std::max(rest_hi, d);
        }
    }
    std::printf("check: %d positioned, the six %.2f-%.2f m, the rest <= %.2f m, gate %.2f m\n", n,
                fold_lo, fold_hi, rest_hi, pf.gps.gate);
    check(pf.gps.ok && !pf.gps.flat && n == 39 && fold_ok == 6 && rest_ok == 33,
          "check: metres, through the fit");
    check(!src.positionError(7, F2.imgs[7].pose, pf.gps, d), "check: nothing for an image without a fix");
    check(std::fabs(pf.gps.gate - 5.0) < 1e-9 && std::fabs(pf.gps.sigma_h - 1.0) < 1e-9,
          "check: the frame carries the fit's radius and the factors' sigma");

    // Through the renumbering an atom's mapper sees, with the order reversed.
    std::vector<uint32_t> to_global;
    for (uint32_t k = 40; k-- > 0;) to_global.push_back(k);
    RemappedPriorSource sub(src, to_global);
    std::vector<PosedImage> local;
    for (uint32_t k = 0; k < to_global.size(); k++) {
        PosedImage p = F2.imgs[to_global[k]];
        p.image = k;
        local.push_back(p);
    }
    const PosePriors lf = sub.factors(local);
    int same = 0;
    for (uint32_t k = 0; k < to_global.size(); k++) {
        double a, b;
        const bool ga = src.positionError(to_global[k], F2.imgs[to_global[k]].pose, pf.gps, a);
        const bool la = sub.positionError(k, local[k].pose, lf.gps, b);
        same += ga == la && (!ga || std::fabs(a - b) < 1e-9);
    }
    check(lf.gps.ok && same == 40, "check: survives a renumbering");
}

// --metric-gps horizontal: the fit is level about the cameras' mean up, and
// the check reads the level pair only, so altitude cannot move it.
static void testLevelCheck() {
    const Loop F1h = makeLoop(0.0, true);
    SensorPriorOptions o;
    o.gps_flat = true;
    ExifGpsPriors flat(F1h.fixes, o);
    ExifGpsPriors full(F1h.fixes, SensorPriorOptions{});
    const PosePriors pf = flat.factors(F1h.imgs);
    const PosePriors pu = full.factors(F1h.imgs);
    int vertical = 0;
    for (const PriorCentre& f : pf.centres) vertical += f.sigma.z != 0.0;
    std::printf("level: %zu centres, %d with a vertical, rms %.3f m\n", pf.centres.size(), vertical,
                flat.lastFactors().gps_rms);
    check(pf.gps.ok && pf.gps.flat && pf.centres.size() == 39 && vertical == 0,
          "check: a horizontal fit states no vertical");
    // Lift image 12 ten metres along the model's up.
    const Vec3 up_m = mul(F1h.Rm, Vec3{0, 0, 1});
    const PosedImage& p = F1h.imgs[12];
    Pose lifted = p.pose;
    lifted.t = lifted.t - mul(lifted.R, up_m * (10.0 * F1h.sm));
    double d0 = 0, d1 = 0, u0 = 0, u1 = 0;
    const bool ok = flat.positionError(12, p.pose, pf.gps, d0) &&
                    flat.positionError(12, lifted, pf.gps, d1) &&
                    full.positionError(12, p.pose, pu.gps, u0) &&
                    full.positionError(12, lifted, pu.gps, u1);
    std::printf("  lifted 10 m: level %.3f -> %.3f m, full %.3f -> %.3f m\n", d0, d1, u0, u1);
    check(ok && std::fabs(d1 - d0) < 0.5, "check: level only under a horizontal fit");
    check(ok && u1 - u0 > 8.0, "check: the lift is seen under a full fit");
}

// ---- the pipeline's builder, off files --------------------------------------

namespace {
struct Tiff {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { u8((uint8_t)v); u8((uint8_t)(v >> 8)); }
    void u32(uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); }
    void ent(uint16_t tag, uint16_t type, uint32_t count, uint32_t val) {
        u16(tag); u16(type); u32(count); u32(val);
    }
    void entIn(uint16_t tag, uint16_t type, uint32_t count, const std::vector<uint8_t>& raw) {
        u16(tag); u16(type); u32(count);
        for (int i = 0; i < 4; i++) u8(i < (int)raw.size() ? raw[i] : 0);
    }
};
}  // namespace

// Little-endian TIFF with a GPS IFD at 42 deg 12' (sec100/100)" N,
// 83 deg 40' 17.86" W, altitude alt100/100 m when `with_alt`.
static std::vector<uint8_t> gpsTiff(uint32_t sec100, bool with_alt, uint32_t alt100 = 25366) {
    Tiff t;
    t.u8('I'); t.u8('I'); t.u16(42); t.u32(8);
    t.u16(1);
    const uint32_t gps = 8 + 2 + 12 + 4;
    t.ent(0x8825, 4, 1, gps);
    t.u32(0);
    const uint32_t nv = with_alt ? 6 : 5;
    t.u16((uint16_t)nv);
    const uint32_t vals = gps + 2 + nv * 12 + 4;
    t.entIn(1, 2, 2, {'N', 0});
    t.ent(2, 5, 3, vals);
    t.entIn(3, 2, 2, {'W', 0});
    t.ent(4, 5, 3, vals + 24);
    t.entIn(5, 1, 1, {0});
    if (with_alt) t.ent(6, 5, 1, vals + 48);
    t.u32(0);
    for (uint32_t v : {42u, 1u, 12u, 1u, sec100, 100u, 83u, 1u, 40u, 1u, 1786u, 100u, alt100, 100u})
        t.u32(v);
    return t.b;
}

static void writeJpeg(const fs::path& path, const std::vector<uint8_t>& tiff) {
    std::vector<uint8_t> j = {0xFF, 0xD8};
    if (!tiff.empty()) {
        const uint16_t seg = (uint16_t)(tiff.size() + 8);
        j.insert(j.end(), {0xFF, 0xE1, (uint8_t)(seg >> 8), (uint8_t)seg, 'E', 'x', 'i', 'f', 0, 0});
        j.insert(j.end(), tiff.begin(), tiff.end());
    }
    j.push_back(0xFF);
    j.push_back(0xD9);
    std::ofstream f(path.string(), std::ios::binary);
    f.write((const char*)j.data(), (std::streamsize)j.size());
}

static void testBuilder() {
    const fs::path dir = fs::temp_directory_path() / "sfm_exif_gps_prior_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    writeJpeg(dir / "a.jpg", gpsTiff(355, true));
    writeJpeg(dir / "sub" / "b.jpg", gpsTiff(1055, true));   // 7.00" = 215.99 m north of a
    writeJpeg(dir / "c.jpg", {});
    writeJpeg(dir / "d.jpg", gpsTiff(705, false));
    writeJpeg(dir / "e.jpg", gpsTiff(455, true, 26366));   // 10 m above a

    // Feature stems, in an order unlike the files' so an index slip shows.
    MatchesDatabase db;
    for (const char* s : {"sub/b", "c", "a", "missing", "d", "e"}) db.images.push_back({s, 0});
    SfmConfig cfg;
    std::unique_ptr<ExifGpsPriors> src = makeExifGpsPriors(cfg, dir.string(), db, false);
    check(src != nullptr, "builder: a source over a geotagged folder");
    if (src) {
        std::printf("builder: %zu of %zu images positioned\n", src->positioned(), db.images.size());
        check(src->positioned() == 4, "builder: four fixes (no EXIF, no file are not)");
        check(src->has(0) && !src->has(1) && src->has(2) && !src->has(3) && src->has(4) &&
                  src->has(5),
              "builder: fixes land on the database's own image ids");
        Vec3 pa, pb;
        const bool ok = src->position(2, pa) && src->position(0, pb);
        const Vec3 d = pb - pa;
        std::printf("  b - a = (%.4f, %.4f, %.4f) m\n", d.x, d.y, d.z);
        check(ok && std::fabs(d.y - 215.99) < 0.05 && std::fabs(d.x) < 0.01 && std::fabs(d.z) < 0.1,
              "builder: b is 216 m due north of a");
        Vec3 pe;
        const bool ok_e = src->position(5, pe);
        std::printf("  e - a up = %.4f m\n", pe.z - pa.z);
        check(ok_e && std::fabs(pe.z - pa.z - 10.0) < 0.05, "builder: EXIF altitude is kept");
    }

    check(src && !src->options().gps_flat, "builder: a full fit by default");
    SfmConfig level = cfg;
    level.metric_gps = "horizontal";
    std::unique_ptr<ExifGpsPriors> lsrc = makeExifGpsPriors(level, dir.string(), db, false);
    check(lsrc && lsrc->options().gps_flat, "builder: --metric-gps horizontal fits level");
    SfmConfig whole = cfg;
    whole.metric_gps = "full";
    std::unique_ptr<ExifGpsPriors> wsrc = makeExifGpsPriors(whole, dir.string(), db, false);
    check(wsrc && !wsrc->options().gps_flat, "builder: --metric-gps full keeps the vertical");

    SfmConfig off = cfg;
    off.sensor_map = false;
    off.sensor_pairs = false;
    check(!makeExifGpsPriors(off, dir.string(), db, false),
          "builder: none with --no-sensor-map and --no-sensor-pairs");
    SfmConfig pairs_only = off;
    pairs_only.sensor_pairs = true;
    check(makeExifGpsPriors(pairs_only, dir.string(), db, false) != nullptr,
          "builder: pairs alone still want the positions");
    check(!makeExifGpsPriors(cfg, "", db, false), "builder: none without an image folder");
    MatchesDatabase bare;
    bare.images.push_back({"c", 0});
    check(!makeExifGpsPriors(cfg, dir.string(), bare, false), "builder: none when nothing has a fix");

    // Telemetry wins where it exists; the EXIF source stands in only without it.
    VerifyCalibration calib;
    check(calib.positionPriors() == nullptr, "calibration: no source by default");
    calib.exif_priors = makeExifGpsPriors(cfg, dir.string(), db, false);
    check(calib.positionPriors() == calib.exif_priors.get() && calib.exif_priors,
          "calibration: the EXIF source without telemetry");
    calib.priors = std::make_unique<TelemetryPriors>(std::vector<SensorCapture>{},
                                                     std::vector<std::string>{"a"},
                                                     std::vector<uint32_t>{1}, SensorPriorOptions{});
    check(calib.positionPriors() == calib.priors.get(), "calibration: telemetry over EXIF");
    fs::remove_all(dir);
}

static int run(int, char**) {
    testPositionsAndPairs();
    testFactors();
    testFoldedFactors();
    testPositionError();
    testLevelCheck();
    testBuilder();
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, run); }
