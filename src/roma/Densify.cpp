// Ported from Lichtfeld-Densification-Plugin (GPL-3.0-or-later), Copyright (c) 2025 Shady Gmira and contributors; core/pipeline.py, core/geometry.py, densify.py@ab0b04e.
#include "roma/Densify.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <set>
#include <unordered_map>

#include "sfm/geometry/Fundamental.h"
#include "sfm/geometry/Triangulation.h"

namespace roma {

using sfm::Mat3;
using sfm::Vec2;
using sfm::Vec3;

void DensifyStats::add(const DensifyStats& o) {
    samples += o.samples; below_certainty += o.below_certainty; outside += o.outside;
    sampson += o.sampson; nonfinite += o.nonfinite; reproj += o.reproj;
    cheirality += o.cheirality; parallax += o.parallax; candidates += o.candidates;
    ref_reproj += o.ref_reproj; inconsistent += o.inconsistent; uncertain += o.uncertain; fused += o.fused; short_track += o.short_track;
    lone_kept += o.lone_kept; voxel_merged += o.voxel_merged; capped += o.capped;
    for (const auto& kv : o.track_hist) track_hist[kv.first] += kv.second;
}

std::vector<float> collectCertainty(const Warp& raw, const std::vector<uint8_t>& mask_a,
                                    const std::vector<uint8_t>& mask_b,
                                    const DensifyOptions& opt) {
    const int w = raw.width, h = raw.height;
    const size_t n = (size_t)w * h;
    std::vector<float> c(raw.certainty.begin(), raw.certainty.end());
    if (opt.plugin_exact)
        for (float& v : c) v = std::max(v, opt.certainty_floor);
    if (!mask_a.empty())
        for (size_t i = 0; i < n; i++) c[i] *= (float)mask_a[i];
    if (!mask_b.empty()) {
        for (size_t i = 0; i < n; i++) {
            // torch's grid_sampler_unnormalize, in float, then nearbyint.
            const float ix = ((raw.warp[2 * i] + 1.0f) * (float)w - 1.0f) / 2.0f;
            const float iy = ((raw.warp[2 * i + 1] + 1.0f) * (float)h - 1.0f) / 2.0f;
            const float rx = std::nearbyint(ix), ry = std::nearbyint(iy);
            float m = 0.0f;
            if (rx >= 0 && rx <= (float)(w - 1) && ry >= 0 && ry <= (float)(h - 1))
                m = (float)mask_b[(size_t)ry * w + (size_t)rx];
            c[i] *= m;
        }
    }
    return c;
}

double referenceX(int j, int w, bool plugin_exact) {
    if (!plugin_exact) return j + 0.5;
    // RoMa's grid is linspace(-1 + 1/w, 1 - 1/w, w); the plugin maps it with (w - 1).
    const float g = (float)(-1.0 + 1.0 / w + j * (2.0 - 2.0 / w) / (w - 1));
    return (double)((g + 1.0f) * 0.5f * (float)(w - 1));
}

double warpX(float u, int w, bool plugin_exact) {
    if (plugin_exact) return (double)((u + 1.0f) * 0.5f * (float)(w - 1));
    return ((double)u + 1.0) * 0.5 * w;
}

namespace {

constexpr double kRadToDeg = 57.29577951308232;

bool isPinhole(const sfm::Camera& c) {
    return c.model == sfm::CamModel::Pinhole || c.model == sfm::CamModel::SimplePinhole;
}

sfm::Mat34 projection(const View& v, bool with_k) {
    sfm::Mat34 P;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) P[r * 4 + c] = v.R[r * 3 + c];
        P[r * 4 + 3] = r == 0 ? v.t.x : r == 1 ? v.t.y : v.t.z;
    }
    if (!with_k) return P;
    const Mat3 K = v.cam.K();
    sfm::Mat34 Q{};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            for (int k = 0; k < 3; k++) Q[r * 4 + c] += K[r * 3 + k] * P[k * 4 + c];
    return Q;
}

Vec3 toView(const View& v, const Vec3& X) { return sfm::mul(v.R, X) + v.t; }

// The plugin's reprojection_errors: K[R|t], depth clamped at 1e-12, camera px.
double reprojPlugin(const sfm::Mat34& P, const Vec3& X, double u, double v) {
    const double px = P[0] * X.x + P[1] * X.y + P[2] * X.z + P[3];
    const double py = P[4] * X.x + P[5] * X.y + P[6] * X.z + P[7];
    const double pz = std::max(P[8] * X.x + P[9] * X.y + P[10] * X.z + P[11], 1e-12);
    return std::hypot(px / pz - u, py / pz - v);
}

// Default mode: through the view's camera model, in match-resolution pixels;
// +inf behind the camera.
double reprojMatch(const View& v, const Vec3& X, double u, double vv, double scale) {
    const Vec3 Xc = toView(v, X);
    if (isPinhole(v.cam) || !v.cam.wideFov()) {
        if (!(Xc.z > 0)) return INFINITY;
    } else {
        const Vec3 b = v.cam.bearing({u, vv});
        if (!(Xc.dot(b) > 0)) return INFINITY;
    }
    const Vec2 p = v.cam.project(Xc);
    return std::hypot(p.x - u, p.y - vv) / scale;
}

bool cheiral(const View& v, const Vec3& X, const Vec3& bearing) {
    const Vec3 Xc = toView(v, X);
    if (isPinhole(v.cam)) return Xc.z > 0;
    return Xc.dot(bearing) > 0;
}

Mat3 plugFundamental(const View& a, const View& b) {
    const Mat3 R = sfm::mul(b.R, sfm::transpose(a.R));
    const Vec3 t = b.t - sfm::mul(R, a.t);
    const Mat3 E = sfm::mul(sfm::crossMatrix(t), R);
    const Mat3 Kai = sfm::inverse3(a.cam.K()), Kbi = sfm::inverse3(b.cam.K());
    return sfm::mul(sfm::mul(sfm::transpose(Kbi), E), Kai);
}

Mat3 essential(const View& a, const View& b) {
    const Mat3 R = sfm::mul(b.R, sfm::transpose(a.R));
    const Vec3 t = b.t - sfm::mul(R, a.t);
    return sfm::mul(sfm::crossMatrix(t), R);
}

// The plugin's DLT, min |A X| over |X| = 1, by one-sided Jacobi SVD of A
// itself: with pixel-scale rows, sfm::triangulateDLT's eigen-solve of A^T A
// squares a condition number near 1e8 and lands millimetres off (gate P-4).
Vec3 dltSvd(const sfm::Mat34& P1, const sfm::Mat34& P2, const Vec3& b1, const Vec3& b2) {
    double A[4][4];
    sfm::dltRows(A[0], A[1], b1, P1);
    sfm::dltRows(A[2], A[3], b2, P2);
    double V[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for (int sweep = 0; sweep < 30; sweep++) {
        double off = 0;
        for (int p = 0; p < 3; p++)
            for (int q = p + 1; q < 4; q++) {
                double a = 0, b = 0, g = 0;
                for (int r = 0; r < 4; r++) {
                    a += A[r][p] * A[r][p];
                    b += A[r][q] * A[r][q];
                    g += A[r][p] * A[r][q];
                }
                if (std::fabs(g) <= 1e-300 || std::fabs(g) <= 1e-17 * std::sqrt(a * b)) continue;
                off = std::max(off, std::fabs(g) / std::sqrt(a * b));
                const double zeta = (b - a) / (2 * g);
                const double t = (zeta >= 0 ? 1.0 : -1.0) / (std::fabs(zeta) + std::sqrt(1 + zeta * zeta));
                const double c = 1 / std::sqrt(1 + t * t), sn = c * t;
                for (int r = 0; r < 4; r++) {
                    const double x = A[r][p], y = A[r][q];
                    A[r][p] = c * x - sn * y;
                    A[r][q] = sn * x + c * y;
                    const double vx = V[r][p], vy = V[r][q];
                    V[r][p] = c * vx - sn * vy;
                    V[r][q] = sn * vx + c * vy;
                }
            }
        if (off < 1e-15) break;
    }
    int mi = 0;
    double best = INFINITY;
    for (int c = 0; c < 4; c++) {
        double n = 0;
        for (int r = 0; r < 4; r++) n += A[r][c] * A[r][c];
        if (n < best) { best = n; mi = c; }
    }
    // The plugin's |w| clamp at 1e-12, sign kept.
    double w = V[3][mi];
    if (std::fabs(w) < 1e-12) w = 1e-12;
    return {V[0][mi] / w, V[1][mi] / w, V[2][mi] / w};
}

struct Candidate {
    Vec3 X;
    double err, parallax;
    double depth_per_px;    // relative depth change per match pixel, coarser view
    int view;
    double xb, yb;
};

// The plugin's colour: bilinear in the match-resolution reference, its own
// clipping kept (the weights are not renormalised at the last row/column).
std::array<float, 3> pluginColour(const std::vector<uint8_t>& rgb, int w, int h, double x,
                                  double y) {
    const float xf = (float)x, yf = (float)y;
    const int x0 = std::clamp((int)std::floor(xf), 0, w - 1);
    const int y0 = std::clamp((int)std::floor(yf), 0, h - 1);
    const int x1 = std::clamp(x0 + 1, 0, w - 1), y1 = std::clamp(y0 + 1, 0, h - 1);
    const float wa = (x1 - xf) * (y1 - yf), wb = (xf - x0) * (y1 - yf);
    const float wc = (x1 - xf) * (yf - y0), wd = (xf - x0) * (yf - y0);
    std::array<float, 3> out{};
    for (int c = 0; c < 3; c++) {
        const float Ia = rgb[((size_t)y0 * w + x0) * 3 + c], Ib = rgb[((size_t)y0 * w + x1) * 3 + c];
        const float Ic = rgb[((size_t)y1 * w + x0) * 3 + c], Id = rgb[((size_t)y1 * w + x1) * 3 + c];
        out[(size_t)c] = (Ia * wa + Ib * wb + Ic * wc + Id * wd) / 255.0f;
    }
    return out;
}

}  // namespace

std::vector<DensePoint> triangulateRef(const RefMatches& m, const std::vector<View>& views,
                                       const std::vector<int64_t>& samples,
                                       const DensifyOptions& opt, DensifyStats& st) {
    const bool exact = opt.plugin_exact;
    const int W = m.w, H = m.h;
    const View& A = views[(size_t)m.ref];
    const double sxA = (double)A.cam.width / W, syA = (double)A.cam.height / H;
    const size_t S = samples.size();
    st.samples += (int64_t)S;

    std::vector<double> xm(S), ym(S), xa(S), ya(S);
    for (size_t s = 0; s < S; s++) {
        xm[s] = referenceX((int)(samples[s] % W), W, exact);
        ym[s] = referenceX((int)(samples[s] / W), H, exact);
        xa[s] = xm[s] * sxA;
        ya[s] = ym[s] * syA;
        if (exact) { xa[s] = (float)xa[s]; ya[s] = (float)ya[s]; }
    }
    std::vector<std::vector<Candidate>> cand(S);
    if (m.record_candidates) m.record_candidates->assign(S, {});
    const sfm::Mat34 PA = projection(A, exact);

    for (size_t k = 0; k < m.nbrs.size(); k++) {
        const View& B = views[(size_t)m.nbrs[k]];
        const double sxB = (double)B.cam.width / W, syB = (double)B.cam.height / H;
        const sfm::Mat34 PB = projection(B, exact);
        const bool sampson_on = !opt.no_filter && opt.sampson_px2 > 0 &&
                                (!exact || (isPinhole(A.cam) && isPinhole(B.cam)));
        Mat3 F{}, E{};
        double sampson_rad2 = 0;
        if (exact) {
            F = plugFundamental(A, B);
        } else {
            E = essential(A, B);
            const double fm = 0.5 * (A.cam.focal() / sxA + B.cam.focal() / sxB);
            sampson_rad2 = opt.sampson_px2 / (fm * fm);
        }
        const Vec3 CA = A.centre, CB = B.centre;
        const double fm_pair = std::min(0.5 * (A.cam.fx / sxA + A.cam.fy / syA),
                                        0.5 * (B.cam.fx / sxB + B.cam.fy / syB));
        for (size_t s = 0; s < S; s++) {
            const size_t i = (size_t)samples[s];
            const float u = m.warp[k][2 * i], v = m.warp[k][2 * i + 1];
            if (!exact) {
                if (m.cert[k][i] < opt.min_certainty) { st.below_certainty++; continue; }
                if (!(std::fabs(u) <= 1.0f && std::fabs(v) <= 1.0f)) { st.outside++; continue; }
            }
            double xb = warpX(u, W, exact) * sxB, yb = warpX(v, H, exact) * syB;
            if (exact) { xb = (float)xb; yb = (float)yb; }
            Vec3 bA, bB;
            if (exact) {
                bA = {xa[s], ya[s], 1.0};
                bB = {xb, yb, 1.0};
            } else {
                bA = A.cam.bearing({xa[s], ya[s]});
                bB = B.cam.bearing({xb, yb});
            }
            if (sampson_on) {
                const double se = exact ? sfm::sampsonSq(F, {xa[s], ya[s]}, {xb, yb})
                                        : sfm::sampsonSqBearing(E, bA, bB);
                if (!(se < (exact ? opt.sampson_px2 : sampson_rad2))) { st.sampson++; continue; }
            }
            const Vec3 X = exact ? dltSvd(PA, PB, bA, bB) : sfm::triangulateDLT(PA, PB, bA, bB);
            const bool finite = std::isfinite(X.x) && std::isfinite(X.y) && std::isfinite(X.z);
            double err;
            if (exact)
                err = std::max(reprojPlugin(PA, X, xa[s], ya[s]), reprojPlugin(PB, X, xb, yb));
            else
                err = std::max(reprojMatch(A, X, xa[s], ya[s], sxA),
                               reprojMatch(B, X, xb, yb, sxB));
            const double par = finite ? sfm::triangulationAngle(X, CA, CB) * kRadToDeg : 0.0;
            if (!finite) { st.nonfinite++; continue; }
            if (!std::isfinite(err)) {
                // Default mode projects through the lens: inf is behind the camera.
                if (exact || opt.no_filter) st.nonfinite++;
                else st.cheirality++;
                continue;
            }
            if (!opt.no_filter) {
                if (!(err <= opt.reproj_px)) { st.reproj++; continue; }
                if (!cheiral(A, X, A.cam.bearing({xa[s], ya[s]})) ||
                    !cheiral(B, X, B.cam.bearing({xb, yb}))) { st.cheirality++; continue; }
                if (opt.min_parallax_deg > 0 && !(par >= opt.min_parallax_deg)) {
                    st.parallax++;
                    continue;
                }
            }
            st.candidates++;
            const double dpp = 1.0 / (fm_pair * std::max(1e-12, std::sin(par / kRadToDeg)));
            cand[s].push_back({X, err, par, dpp, m.nbrs[k], xb, yb});
            if (m.record_candidates) (*m.record_candidates)[s].push_back({(int)k, X, err});
        }
    }

    auto reproj = [&](const View& v, const Vec3& X, double x, double y) {
        if (exact) return reprojPlugin(projection(v, true), X, x, y);
        return reprojMatch(v, X, x, y, (double)v.cam.width / W);
    };

    std::vector<DensePoint> out;
    for (size_t s = 0; s < S; s++) {
        std::vector<Candidate> cs = cand[s];
        if (cs.empty()) continue;
        if (!exact && cs.size() > 1 && !opt.no_filter) {
            // The plugin averages every neighbour's point, so one bad warp drags a
            // good point off the surface. Keep the largest mutually consistent set.
            size_t best = 0;
            int best_n = -1;
            std::vector<std::vector<char>> ok(cs.size(), std::vector<char>(cs.size(), 0));
            for (size_t i = 0; i < cs.size(); i++) {
                int n = 0;
                for (size_t j = 0; j < cs.size(); j++) {
                    ok[i][j] = i == j || reproj(views[(size_t)cs[j].view], cs[i].X, cs[j].xb,
                                                cs[j].yb) <= opt.reproj_px;
                    n += ok[i][j];
                }
                if (n > best_n || (n == best_n && cs[i].err < cs[best].err)) { best = i; best_n = n; }
            }
            std::vector<Candidate> keep;
            // No two neighbours agree: nothing says which one is right.
            if (best_n > 1)
                for (size_t j = 0; j < cs.size(); j++)
                    if (ok[best][j]) keep.push_back(cs[j]);
            st.inconsistent += (int64_t)(cs.size() - keep.size());
            cs = std::move(keep);
            if (cs.empty()) continue;
        }
        Vec3 sum{0, 0, 0};
        double wsum = 0, par = 0, max_err = 0;
        for (const Candidate& c : cs) {
            // The plugin weighs by 1 / two-view residual, which a DLT drives to
            // rounding noise (gate P-4); the default weighs by depth precision.
            const double w = exact ? 1.0 / std::max(c.err, 1e-4) : 1.0 / (c.depth_per_px * c.depth_per_px);
            sum = sum + c.X * w;
            wsum += w;
            par = std::max(par, c.parallax);
            if (std::isfinite(c.err)) max_err = std::max(max_err, c.err);
        }
        const Vec3 X = sum * (1.0 / std::max(wsum, 1e-8));
        const double ref_err = reproj(A, X, xa[s], ya[s]);
        if (!opt.no_filter && (!std::isfinite(ref_err) || ref_err > opt.reproj_px)) {
            st.ref_reproj++;
            continue;
        }
        DensePoint p;
        p.xyz = X;
        p.parallax_deg = par;
        p.track.push_back({m.ref, xa[s], ya[s]});
        std::vector<double> errs;
        if (std::isfinite(ref_err)) errs.push_back(ref_err);
        std::set<int> used{exact ? m.ref : A.image};
        double track_par = 0, track_dpp = INFINITY;
        for (const Candidate& c : cs) {
            const int key = exact ? c.view : views[(size_t)c.view].image;
            if (used.count(key)) continue;
            const double e = reproj(views[(size_t)c.view], X, c.xb, c.yb);
            if (!opt.no_filter && (!std::isfinite(e) || e > opt.reproj_px)) continue;
            used.insert(key);
            p.track.push_back({c.view, c.xb, c.yb});
            if (std::isfinite(e)) errs.push_back(std::max(e, c.err));
            track_par = std::max(track_par, c.parallax);
            track_dpp = std::min(track_dpp, c.depth_per_px);
        }
        if (!exact) {
            p.parallax_deg = track_par;
            // Depth moves d / (f sin(parallax)) per match pixel along the ray, f
            // the coarser view's; the best pair in the track decides.
            if (!opt.no_filter && opt.max_depth_error > 0 && !(track_dpp <= opt.max_depth_error)) {
                st.uncertain++;
                continue;
            }
        }
        p.error = errs.empty() ? max_err : *std::max_element(errs.begin(), errs.end());
        std::set<int> imgs;
        for (const Observation& o : p.track) imgs.insert(views[(size_t)o.view].image);
        p.distinct_images = (int)imgs.size();
        const std::array<float, 3> rgb =
            exact || !m.colour_at
                ? pluginColour(m.rgb_match, W, H, xm[s], ym[s])
                : m.colour_at(xa[s], ya[s]);
        for (int c = 0; c < 3; c++) p.rgb[c] = rgb[(size_t)c];
        out.push_back(std::move(p));
    }
    st.fused += (int64_t)out.size();
    return out;
}

namespace {

struct VoxelKey {
    int64_t x, y, z;
    bool operator==(const VoxelKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct VoxelHash {
    size_t operator()(const VoxelKey& k) const {
        return (size_t)(k.x * 73856093LL) ^ (size_t)(k.y * 19349663LL) ^ (size_t)(k.z * 83492791LL);
    }
};
VoxelKey keyOf(const Vec3& p, double voxel) {
    return {(int64_t)std::floor(p.x / voxel), (int64_t)std::floor(p.y / voxel),
            (int64_t)std::floor(p.z / voxel)};
}
int trackLen(const DensePoint& p, bool by_images) {
    return by_images ? p.distinct_images : (int)p.track.size();
}

}  // namespace

std::vector<size_t> voxelSelect(const std::vector<DensePoint>& pts, double voxel,
                                bool by_images) {
    std::vector<size_t> sel;
    if (!(voxel > 0)) {
        sel.resize(pts.size());
        std::iota(sel.begin(), sel.end(), 0);
        return sel;
    }
    std::unordered_map<VoxelKey, size_t, VoxelHash> chosen;
    chosen.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        auto [it, fresh] = chosen.emplace(keyOf(pts[i].xyz, voxel), i);
        if (fresh) continue;
        const size_t prev = it->second;
        const int tl = trackLen(pts[i], by_images), pl = trackLen(pts[prev], by_images);
        if (tl > pl || (tl == pl && pts[i].error < pts[prev].error)) it->second = i;
    }
    for (const auto& kv : chosen) sel.push_back(kv.second);
    std::sort(sel.begin(), sel.end());
    return sel;
}

std::vector<DensePoint> finalizePoints(std::vector<DensePoint> pts, int min_track,
                                       double voxel, int64_t max_points,
                                       const DensifyOptions& opt, DensifyStats& st) {
    const bool by_images = !opt.plugin_exact;
    std::vector<DensePoint> kept;
    kept.reserve(pts.size());
    if (opt.plugin_exact || !(voxel > 0)) {
        for (DensePoint& p : pts) {
            if (trackLen(p, by_images) >= min_track) kept.push_back(std::move(p));
            else st.short_track++;
        }
    } else {
        std::unordered_map<VoxelKey, int, VoxelHash> occupancy;
        for (const DensePoint& p : pts) occupancy[keyOf(p.xyz, voxel)]++;
        for (DensePoint& p : pts) {
            if (trackLen(p, by_images) >= min_track) {
                kept.push_back(std::move(p));
            } else if (occupancy[keyOf(p.xyz, voxel)] == 1 &&
                       p.parallax_deg >= opt.lone_parallax_deg) {
                st.lone_kept++;
                kept.push_back(std::move(p));
            } else {
                st.short_track++;
            }
        }
    }
    auto cap = [&](std::vector<DensePoint>& v) {
        if (max_points <= 0 || (int64_t)v.size() <= max_points) return;
        std::vector<size_t> idx(v.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::mt19937_64 rng(opt.seed);
        std::shuffle(idx.begin(), idx.end(), rng);
        idx.resize((size_t)max_points);
        std::vector<DensePoint> c;
        c.reserve(idx.size());
        for (size_t i : idx) c.push_back(std::move(v[i]));
        st.capped += (int64_t)(v.size() - c.size());
        v = std::move(c);
    };
    auto vox = [&](std::vector<DensePoint>& v) {
        if (!(voxel > 0)) return;
        const std::vector<size_t> sel = voxelSelect(v, voxel, by_images);
        std::vector<DensePoint> c;
        c.reserve(sel.size());
        for (size_t i : sel) c.push_back(std::move(v[i]));
        st.voxel_merged += (int64_t)(v.size() - c.size());
        v = std::move(c);
    };
    // The plugin caps before the voxel select; ours dedups first so the cap
    // spends its budget on distinct surface.
    if (opt.plugin_exact) { cap(kept); vox(kept); }
    else { vox(kept); cap(kept); }
    for (const DensePoint& p : kept) st.track_hist[trackLen(p, by_images)]++;
    return kept;
}

}  // namespace roma
