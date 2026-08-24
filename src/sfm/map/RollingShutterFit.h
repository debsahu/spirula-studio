// Rolling-shutter fitting: infer per-image timestamps, derive each image's
// readout twist from its temporal neighbours, and estimate the readout time.
//
// The readout time is one scalar per camera group, found by a 1-D search on the
// robust reprojection cost -- no solver column and no new kernel (docs/notes/
// rolling-shutter.md). Reversed readout is a negative readout time, so the
// direction search is two hypotheses (vertical / horizontal), not four.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "sfm/core/Model.h"
#include "sfm/core/RollingShutter.h"
#include "sfm/map/Bundle.h"

namespace sfm {

struct RollingShutterOptions {
    // off | auto | vertical | horizontal
    std::string mode = "off";
    // Search bound on |readout| / median frame interval. Above 1 the readout
    // outlasts the interval, which only a subsampled capture can do.
    double max_ratio = 1.5;
    // Relative cost drop below which the fit is called noise and rejected.
    double min_gain = 0.01;
    // Observations sampled for one cost evaluation; the search runs ~60 of them.
    size_t sample_obs = 300000;
    // 0 picks for itself: a trajectory twist converges in two, a per-image one
    // needs about six (measured, sfm_rolling_shutter_test).
    int refine_rounds = 0;
    // Per-image twists in the finishing rounds, damped toward the trajectory's
    // by `twist_prior`. Off by default: free per-image velocities are what
    // makes rolling-shutter SfM degenerate, and the prior is all that holds it (D75).
    bool per_image_twist = false;
    // 0.5 measured best: below it the free twist absorbs pose error instead of
    // the shutter, and at 0 one round leaves the twist worse than no twist.
    double twist_prior = 0.5;
    bool verbose = true;
};

// One camera group's images in time order, with the interval each image's
// central difference spans.
struct ShutterSequence {
    uint32_t camera_id = 0;
    std::vector<uint32_t> image_ids;  // sorted by timestamp
    std::vector<double> timestamps;
    double median_dt = 1.0;
    bool synthetic_time = false;  // timestamps are positions, not frame numbers
};

// Registered images grouped by camera and ordered in time. Frame numbers come
// from the file names; a group whose names carry none falls back to name order.
inline std::vector<ShutterSequence> buildShutterSequences(const Reconstruction& rec,
                                                          TimeUnit& unit) {
    std::map<uint32_t, std::vector<const Image*>> byCam;
    for (const auto& kv : rec.images)
        if (kv.second.registered) byCam[kv.second.camera_id].push_back(&kv.second);

    std::vector<ShutterSequence> out;
    unit = TimeUnit::Frames;
    for (auto& kv : byCam) {
        std::vector<const Image*> ims = kv.second;
        std::sort(ims.begin(), ims.end(),
                  [](const Image* a, const Image* b) { return a->name < b->name; });
        std::vector<std::string> names;
        names.reserve(ims.size());
        for (const Image* im : ims) names.push_back(im->name);

        ShutterSequence sq;
        sq.camera_id = kv.first;
        if (!frameNumbers(names, sq.timestamps)) {
            sq.synthetic_time = true;
            unit = TimeUnit::Index;
            sq.timestamps.resize(ims.size());
            for (size_t i = 0; i < ims.size(); i++) sq.timestamps[i] = (double)i;
        }
        std::vector<size_t> order(ims.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return sq.timestamps[a] < sq.timestamps[b]; });

        std::vector<double> ts;
        for (size_t i : order) {
            sq.image_ids.push_back(ims[i]->id);
            ts.push_back(sq.timestamps[i]);
        }
        sq.timestamps = ts;

        std::vector<double> gaps;
        for (size_t i = 1; i < ts.size(); i++) gaps.push_back(ts[i] - ts[i - 1]);
        if (!gaps.empty()) {
            std::sort(gaps.begin(), gaps.end());
            sq.median_dt = gaps[gaps.size() / 2];
        }
        if (!(sq.median_dt > 0)) sq.median_dt = 1.0;
        out.push_back(std::move(sq));
    }
    return out;
}

// Central-difference twist per image, scaled to the readout interval. The
// interval divides out the sampling rate, which is why a non-uniformly
// extracted capture needs the timestamps rather than the frame count.
inline void twistsForSequence(const Reconstruction& rec, const ShutterSequence& sq, double readout,
                              std::map<uint32_t, Twist>& out) {
    const size_t n = sq.image_ids.size();
    for (size_t i = 0; i < n; i++) {
        const size_t a = (i > 0) ? i - 1 : i;
        const size_t b = (i + 1 < n) ? i + 1 : i;
        if (a == b) {
            out[sq.image_ids[i]] = Twist{};
            continue;
        }
        const double dt = sq.timestamps[b] - sq.timestamps[a];
        if (!(dt > 0)) {
            out[sq.image_ids[i]] = Twist{};
            continue;
        }
        const Image& ia = rec.images.at(sq.image_ids[a]);
        const Image& ib = rec.images.at(sq.image_ids[b]);
        out[sq.image_ids[i]] = twistBetween(ia.pose, ib.pose).scaled(readout / dt);
    }
}

// The observations one cost evaluation walks: (image, point, measured pixel),
// subsampled to a fixed budget so the search cost is independent of the model.
struct ShutterCostSet {
    struct Obs {
        uint32_t image_id;
        Vec3 X;
        Vec2 px;
    };
    std::vector<Obs> obs;
    double loss_param = 2.0;
};

inline ShutterCostSet buildShutterCostSet(const Reconstruction& rec,
                                          const std::vector<uint32_t>& image_ids,
                                          size_t budget) {
    ShutterCostSet cs;
    size_t total = 0;
    for (uint32_t id : image_ids) {
        const Image& im = rec.images.at(id);
        for (uint64_t pid : im.point3D_ids)
            if (pid != kInvalidPoint3D) total++;
    }
    const size_t stride = (budget && total > budget) ? (total + budget - 1) / budget : 1;
    size_t seen = 0;
    for (uint32_t id : image_ids) {
        const Image& im = rec.images.at(id);
        for (size_t k = 0; k < im.point3D_ids.size(); k++) {
            const uint64_t pid = im.point3D_ids[k];
            if (pid == kInvalidPoint3D) continue;
            if (seen++ % stride) continue;
            auto it = rec.points3D.find(pid);
            if (it == rec.points3D.end()) continue;
            cs.obs.push_back({id, it->second.xyz, im.points2D[k]});
        }
    }
    return cs;
}

// Robust reprojection cost under a given twist per image. Huber, matching the
// mapper's BA loss so the search and the solve agree about which observations
// carry weight.
inline double shutterCost(const Reconstruction& rec, const ShutterCostSet& cs,
                          const std::map<uint32_t, Twist>& twists, const ShutterAxis& axis) {
    const double d2 = cs.loss_param * cs.loss_param;
    double total = 0;
    uint32_t cur = UINT32_MAX;
    const Camera* cam = nullptr;
    const Image* im = nullptr;
    const Twist* xi = nullptr;
    for (const ShutterCostSet::Obs& o : cs.obs) {
        if (o.image_id != cur) {
            cur = o.image_id;
            im = &rec.images.at(cur);
            cam = &rec.cameras.at(im->camera_id);
            auto it = twists.find(cur);
            xi = (it == twists.end()) ? nullptr : &it->second;
        }
        const double s = axis.time(o.px);
        Pose p = im->pose;
        if (xi && !xi->isZero()) p = poseAtShutterTime(p, *xi, s);
        const Vec3 pc = mul(p.R, o.X) + p.t;
        if (!cam->isSpherical() && pc.z <= 1e-9) continue;
        const Vec2 q = cam->project(pc);
        const double e2 = (q.x - o.px.x) * (q.x - o.px.x) + (q.y - o.px.y) * (q.y - o.px.y);
        total += (e2 <= d2) ? e2 : (2.0 * cs.loss_param * std::sqrt(e2) - d2);
    }
    return total;
}

struct ShutterFit {
    ShutterDir dir = ShutterDir::Global;  // Global unless the guard accepted it
    double readout = 0;
    double gain = 0;  // relative cost drop against global shutter
    // What the search settled on before the guard. A camera can have a real
    // readout too fast to be worth correcting, and saying so beats "no signal".
    ShutterDir best_dir = ShutterDir::Global;
    double best_readout = 0, best_gain = 0;
};

// Coarse grid then refinement, for one direction. The grid is not optional: the
// cost stops being convex in the readout once the twist is large enough to
// reorder which observations the robust loss down-weights.
inline void searchReadout(const Reconstruction& rec, const ShutterSequence& sq,
                          const ShutterCostSet& cs, const ShutterAxis& axis, double max_ratio,
                          double& best_r, double& best_cost) {
    std::map<uint32_t, Twist> tw;
    auto eval = [&](double r) {
        twistsForSequence(rec, sq, r, tw);
        return shutterCost(rec, cs, tw, axis);
    };
    const double span = max_ratio * sq.median_dt;
    const int kGrid = 20;
    const char* dump = getenv("SS_SFM_RS_CURVE");
    best_r = 0;
    best_cost = eval(0.0);
    const double c0 = best_cost;
    for (int i = -kGrid; i <= kGrid; i++) {
        if (i == 0) continue;
        const double r = span * i / kGrid;
        const double c = eval(r);
        if (dump && *dump && *dump != '0')
            fprintf(stderr, "[rs-curve] %+.4f  %.6f\n", r / sq.median_dt, c / c0);
        if (c < best_cost) {
            best_cost = c;
            best_r = r;
        }
    }
    double h = span / kGrid;
    for (int round = 0; round < 12 && h > span * 1e-4; round++) {
        const double lo = best_r - h, hi = best_r + h;
        const double cl = eval(lo), ch = eval(hi);
        if (cl < best_cost && cl <= ch) {
            best_cost = cl;
            best_r = lo;
        } else if (ch < best_cost) {
            best_cost = ch;
            best_r = hi;
        } else {
            h *= 0.5;
        }
    }
}

// Fit one camera group: try both readout axes, keep the better, and reject the
// fit outright unless it beats global shutter by `min_gain`. A tripod capture
// has no signal at all and must come back Global rather than fitted to noise.
inline ShutterFit fitShutter(const Reconstruction& rec, const ShutterSequence& sq,
                             const RollingShutterOptions& opt) {
    ShutterFit fit;
    if (sq.image_ids.size() < 3) return fit;
    const Camera& cam = rec.cameras.at(sq.camera_id);
    ShutterCostSet cs = buildShutterCostSet(rec, sq.image_ids, opt.sample_obs);
    if (cs.obs.size() < 100) return fit;

    std::map<uint32_t, Twist> zero;
    const double base = shutterCost(rec, cs, zero, ShutterAxis{});
    if (!(base > 0)) return fit;

    std::vector<ShutterDir> dirs;
    if (opt.mode == "vertical") dirs = {ShutterDir::Vertical};
    else if (opt.mode == "horizontal") dirs = {ShutterDir::Horizontal};
    else dirs = {ShutterDir::Vertical, ShutterDir::Horizontal};

    for (ShutterDir d : dirs) {
        ShutterAxis axis = ShutterAxis::of(d, cam.width, cam.height);
        if (axis.ax == 0 && axis.ay == 0) continue;
        double r = 0, c = base;
        searchReadout(rec, sq, cs, axis, opt.max_ratio, r, c);
        const double gain = (base - c) / base;
        if (opt.verbose)
            fprintf(stderr, "[rs] camera %u %s: readout %.4f frame, cost %.2f%% of global\n",
                    sq.camera_id, shutterDirName(d), r / sq.median_dt, 100.0 * (1.0 - gain));
        if (gain > fit.best_gain) {
            fit.best_gain = gain;
            fit.best_dir = d;
            fit.best_readout = r;
        }
    }
    if (fit.best_gain >= opt.min_gain && fit.best_readout != 0) {
        fit.dir = fit.best_dir;
        fit.readout = fit.best_readout;
        fit.gain = fit.best_gain;
    }
    return fit;
}

// Symmetric 6x6 by Gaussian elimination with partial pivoting. False if the
// image's observations do not determine the twist at all.
inline bool solveSym6(double A[36], double b[6], double x[6]) {
    for (int c = 0; c < 6; c++) {
        int p = c;
        for (int r = c + 1; r < 6; r++)
            if (std::fabs(A[r * 6 + c]) > std::fabs(A[p * 6 + c])) p = r;
        if (std::fabs(A[p * 6 + c]) < 1e-20) return false;
        if (p != c) {
            for (int k = 0; k < 6; k++) std::swap(A[c * 6 + k], A[p * 6 + k]);
            std::swap(b[c], b[p]);
        }
        for (int r = c + 1; r < 6; r++) {
            const double f = A[r * 6 + c] / A[c * 6 + c];
            for (int k = c; k < 6; k++) A[r * 6 + k] -= f * A[c * 6 + k];
            b[r] -= f * b[c];
        }
    }
    for (int r = 5; r >= 0; r--) {
        double v = b[r];
        for (int k = r + 1; k < 6; k++) v -= A[r * 6 + k] * x[k];
        x[r] = v / A[r * 6 + r];
    }
    for (int i = 0; i < 6; i++)
        if (!std::isfinite(x[i])) return false;
    return true;
}

// Only image `im`'s observations see its twist, so this is a 6x6 solve and never
// a column of the reduced camera system (D75). `prior` damps toward the incoming
// trajectory estimate relative to the data curvature, so it is dimensionless.
inline bool refineImageTwist(const Reconstruction& rec, const Image& im, const Camera& cam,
                             const ShutterAxis& ax, double prior, double loss_param,
                             const Twist& anchor, Twist& xi) {
    struct Ob { Vec3 X; Vec2 px; double s; };
    std::vector<Ob> obs;
    for (size_t f = 0; f < im.point3D_ids.size(); f++) {
        if (im.point3D_ids[f] == kInvalidPoint3D) continue;
        auto it = rec.points3D.find(im.point3D_ids[f]);
        if (it == rec.points3D.end()) continue;
        obs.push_back({it->second.xyz, im.points2D[f], ax.time(im.points2D[f])});
    }
    if (obs.size() < 40) return false;

    const double d2 = loss_param * loss_param;
    auto cost = [&](const Twist& t) {
        double c = 0;
        for (const Ob& o : obs) {
            const Pose p = poseAtShutterTime(im.pose, t, o.s);
            const Vec3 xc = mul(p.R, o.X) + p.t;
            if (!cam.isSpherical() && xc.z <= 1e-9) continue;
            const Vec2 q = cam.project(xc);
            const double e2 = (q.x - o.px.x) * (q.x - o.px.x) + (q.y - o.px.y) * (q.y - o.px.y);
            c += (e2 <= d2) ? e2 : (2.0 * loss_param * std::sqrt(e2) - d2);
        }
        return c;
    };

    const Twist start = xi;
    const double c0 = cost(start);
    Twist cur = start;
    for (int round = 0; round < 4; round++) {
        double A[36] = {}, b[6] = {};
        for (const Ob& o : obs) {
            const Pose p = poseAtShutterTime(im.pose, cur, o.s);
            const Vec3 xc = mul(p.R, o.X) + p.t;
            if (!cam.isSpherical() && xc.z <= 1e-9) continue;
            const Vec2 q = cam.project(xc);
            const double r[2] = {q.x - o.px.x, q.y - o.px.y};
            const double e2 = r[0] * r[0] + r[1] * r[1];
            const double w = (e2 <= d2) ? 1.0 : loss_param / std::sqrt(e2);  // Huber IRLS

            double P[6];  // d(pixel)/d(camera point)
            const double h = 1e-5 * std::max(1.0, std::fabs(xc.z));
            for (int c = 0; c < 3; c++) {
                Vec3 lo = xc, hi = xc;
                (&hi.x)[c] += h;
                (&lo.x)[c] -= h;
                const Vec2 qa = cam.project(hi), qb = cam.project(lo);
                P[c] = (qa.x - qb.x) / (2 * h);
                P[3 + c] = (qa.y - qb.y) / (2 * h);
            }
            // d(shuttered point)/d(twist) = s * [ -[x]_x | I ]
            const double M[3][6] = {{0, xc.z, -xc.y, 1, 0, 0},
                                    {-xc.z, 0, xc.x, 0, 1, 0},
                                    {xc.y, -xc.x, 0, 0, 0, 1}};
            double J[2][6];
            for (int r2 = 0; r2 < 2; r2++)
                for (int c = 0; c < 6; c++) {
                    double v = 0;
                    for (int m = 0; m < 3; m++) v += P[r2 * 3 + m] * M[m][c];
                    J[r2][c] = o.s * v;
                }
            for (int r2 = 0; r2 < 2; r2++)
                for (int i = 0; i < 6; i++) {
                    b[i] -= w * J[r2][i] * r[r2];
                    for (int j = 0; j < 6; j++) A[i * 6 + j] += w * J[r2][i] * J[r2][j];
                }
        }
        const double dv[6] = {cur.omega.x - anchor.omega.x, cur.omega.y - anchor.omega.y,
                              cur.omega.z - anchor.omega.z, cur.v.x - anchor.v.x,
                              cur.v.y - anchor.v.y,         cur.v.z - anchor.v.z};
        for (int i = 0; i < 6; i++) {
            const double d = prior * A[i * 6 + i] + 1e-12;
            b[i] -= d * dv[i];
            A[i * 6 + i] += d;
        }
        double x[6] = {};
        if (!solveSym6(A, b, x)) return false;
        cur.omega = cur.omega + Vec3{x[0], x[1], x[2]};
        cur.v = cur.v + Vec3{x[3], x[4], x[5]};
        double step = 0;
        for (double q : x) step += q * q;
        if (std::sqrt(step) < 1e-10) break;
    }
    if (!(cost(cur) < c0)) return false;
    xi = cur;
    return true;
}

// Fill `out` from the current poses: every registered image gets the twist its
// camera group's readout implies, and every camera its fitted direction.
inline void applyShutterFits(const Reconstruction& rec,
                             const std::vector<ShutterSequence>& seqs,
                             const std::vector<ShutterFit>& fits, TimeUnit unit,
                             RollingShutterData& out) {
    out = RollingShutterData{};
    out.unit = unit;
    for (size_t i = 0; i < seqs.size(); i++) {
        const ShutterSequence& sq = seqs[i];
        out.cameras[sq.camera_id] = {fits[i].dir, fits[i].readout};
        std::map<uint32_t, Twist> tw;
        if (fits[i].dir != ShutterDir::Global)
            twistsForSequence(rec, sq, fits[i].readout, tw);
        for (size_t k = 0; k < sq.image_ids.size(); k++) {
            const uint32_t id = sq.image_ids[k];
            auto it = tw.find(id);
            out.images[id] = {it == tw.end() ? Twist{} : it->second, sq.timestamps[k]};
        }
    }
}

// The finishing pass: fit the shutter, then alternate a rolling-shutter bundle
// adjustment with a re-fit of the readout against the poses it produced.
// Returns false (and leaves `rec` untouched) when no camera shows a shutter.
inline bool runRollingShutterPass(Reconstruction& rec, const RollingShutterOptions& ropt,
                                  BundleOptions bopt, RollingShutterData& out) {
    if (ropt.mode == "off") return false;
    TimeUnit unit = TimeUnit::Frames;
    std::vector<ShutterSequence> seqs = buildShutterSequences(rec, unit);
    if (seqs.empty()) return false;

    std::vector<ShutterFit> fits;
    bool any = false;
    for (const ShutterSequence& sq : seqs) {
        fits.push_back(fitShutter(rec, sq, ropt));
        any |= fits.back().dir != ShutterDir::Global;
    }
    if (!any) {
        if (ropt.verbose)
            for (size_t i = 0; i < seqs.size(); i++)
                fprintf(stderr,
                        "[rs] camera %u: best fit %s readout %.4f %s (%.4f of a frame interval) "
                        "removes %.2f%% of the cost, under the %.1f%% floor -- left "
                        "global-shutter\n",
                        seqs[i].camera_id, shutterDirName(fits[i].best_dir), fits[i].best_readout,
                        timeUnitName(unit), fits[i].best_readout / seqs[i].median_dt,
                        100 * fits[i].best_gain, 100 * ropt.min_gain);
        return false;
    }
    for (size_t i = 0; i < seqs.size(); i++)
        if (fits[i].dir != ShutterDir::Global && seqs[i].synthetic_time && ropt.verbose)
            fprintf(stderr,
                    "[rs] camera %u has no frame numbers in its file names; the readout assumes "
                    "a uniform interval and a subsampled capture will be under-corrected\n",
                    seqs[i].camera_id);

    applyShutterFits(rec, seqs, fits, unit, out);
    const int rounds =
        ropt.refine_rounds > 0 ? ropt.refine_rounds : (ropt.per_image_twist ? 6 : 2);
    for (int round = 0; round < rounds; round++) {
        bopt.rs = &out;
        runGlobalBA(rec, bopt);
        for (size_t i = 0; i < seqs.size(); i++) {
            if (fits[i].dir == ShutterDir::Global) continue;
            const Camera& cam = rec.cameras.at(seqs[i].camera_id);
            ShutterAxis ax = ShutterAxis::of(fits[i].dir, cam.width, cam.height);
            ShutterCostSet cs = buildShutterCostSet(rec, seqs[i].image_ids, ropt.sample_obs);
            if (cs.obs.size() < 100) continue;
            double r = fits[i].readout, c = 0;
            searchReadout(rec, seqs[i], cs, ax, ropt.max_ratio, r, c);
            fits[i].readout = r;
        }
        // The anchor the per-image prior pulls toward. It replaces the working
        // twists only when there is no per-image refinement -- that is what lets
        // successive rounds compound instead of each starting over.
        RollingShutterData anchor;
        applyShutterFits(rec, seqs, fits, unit, anchor);
        if (!ropt.per_image_twist) {
            out = anchor;
            continue;
        }
        out.unit = anchor.unit;
        out.cameras = anchor.cameras;
        for (size_t i = 0; i < seqs.size(); i++) {
            if (fits[i].dir == ShutterDir::Global) continue;
            const Camera& cam = rec.cameras.at(seqs[i].camera_id);
            const ShutterAxis ax = ShutterAxis::of(fits[i].dir, cam.width, cam.height);
            size_t moved = 0;
            for (uint32_t id : seqs[i].image_ids) {
                auto it = out.images.find(id);
                auto an = anchor.images.find(id);
                if (it == out.images.end() || an == anchor.images.end()) continue;
                it->second.timestamp = an->second.timestamp;
                if (refineImageTwist(rec, rec.images.at(id), cam, ax, ropt.twist_prior,
                                     bopt.loss_param, an->second.twist, it->second.twist))
                    moved++;
            }
            if (ropt.verbose)
                fprintf(stderr, "[rs] camera %u: per-image twist refined on %zu of %zu images\n",
                        seqs[i].camera_id, moved, seqs[i].image_ids.size());
        }
    }
    if (ropt.verbose)
        for (size_t i = 0; i < seqs.size(); i++)
            fprintf(stderr, "[rs] camera %u: %s readout %.4f %s (%.4f of a frame interval)\n",
                    seqs[i].camera_id, shutterDirName(fits[i].dir), fits[i].readout,
                    timeUnitName(unit), fits[i].readout / seqs[i].median_dt);
    return true;
}

}  // namespace sfm
