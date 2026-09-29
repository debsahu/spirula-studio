#pragma once
// The block scale statistic: whether the stretch of chain growth is extending has left the
// GPS's scale, and the similarity that returns it. Pure functions over one frame per capture
// position in capture order; Mapper::gpsScaleCheck gathers the frames and applies the result.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "sfm/core/Model.h"
#include "sfm/core/Pose.h"

namespace sfm {
namespace bss {

// A node is the first fresh fix at kNode metres of MODEL chord from the last one, within
// 2 kNode of model path: nodes on the GPS walk with a stationary receiver's quantisation
// (0.23-0.38 m steps on EXIF), and a frame-step path is 10-20 % long at 0.4 m/frame.
constexpr double kNode = 5.0;
constexpr int64_t kMaxGap = 3;  // capture positions; a wider gap ends a run
constexpr int kLengths = 3;
constexpr double kLength[kLengths] = {60, 100, 150};
// Open-sky sd of the log ratio on finished Hickory, 0726power and OSV models, per length.
constexpr double kSigma[kLengths] = {0.008, 0.006, 0.0045};
// In-run: no fire at sky >= 0.5 in 3 x 1795 Hickory checks or 939 0726power ones. After
// growth, lower: Hickory's milder modes finish their west chain at 0.956-0.958 over 60 m.
constexpr double kTauRun[kLengths] = {0.05, 0.04, 0.03};
constexpr double kTauEnd[kLengths] = {0.04, 0.03, 0.025};
constexpr size_t kBlockMinPairs = 6;  // fewer, and the firing window's ratio sets the factor
constexpr size_t kEndChecks = 20;

struct Frame {
    uint32_t img = 0;
    int64_t seq = 0, pos = 0;
    Vec3 c{0, 0, 0};      // model centre through the GPS fit, metres; z = 0 on a flat fit
    Vec3 g{0, 0, 0};      // the fix, the same way
    bool fresh = true;    // the fix differs from the capture predecessor's
    uint32_t stamp = 0;   // the check that first saw it registered; 0 = before growth
};

struct Pair {
    size_t a = 0, b = 0;
};

// One frame per capture position, the first registered there, in capture order: a rig's
// lenses add their baseline to every walk otherwise (5.1 cm on the OSV rig, 1045 pairs).
inline std::vector<Frame> collapse(std::vector<Frame> all) {
    std::sort(all.begin(), all.end(), [](const Frame& a, const Frame& b) {
        if (a.seq != b.seq) return a.seq < b.seq;
        if (a.pos != b.pos) return a.pos < b.pos;
        return a.stamp != b.stamp ? a.stamp < b.stamp : a.img < b.img;
    });
    std::vector<Frame> out;
    for (const Frame& f : all)
        if (out.empty() || out.back().seq != f.seq || out.back().pos != f.pos) out.push_back(f);
    return out;
}

// The capture-contiguous run holding frames[k]: [lo, hi] with no position gap over kMaxGap.
inline void runOf(const std::vector<Frame>& f, size_t k, size_t& lo, size_t& hi) {
    lo = hi = k;
    while (lo > 0 && f[lo - 1].seq == f[k].seq && f[lo].pos - f[lo - 1].pos <= kMaxGap) lo--;
    while (hi + 1 < f.size() && f[hi + 1].seq == f[k].seq && f[hi + 1].pos - f[hi].pos <= kMaxGap)
        hi++;
}

// Node pairs walking from frames[from] by `dir` (+1 or -1) inside [lo, hi]. Every anchor is
// a fresh fix: a stalled receiver's repeat is v * stall off (15.6 m in 60 m for 12 frames at
// 1.3 m). A hover or loop runs up 2 kNode of path first and drops its segment.
inline std::vector<Pair> nodePairs(const std::vector<Frame>& f, size_t from, int dir, size_t lo,
                                   size_t hi) {
    std::vector<Pair> out;
    size_t a = from, prev = from;
    bool anchored = f[from].fresh;
    double path = 0;
    for (long i = (long)from + dir; i >= (long)lo && i <= (long)hi; i += dir) {
        path += (f[i].c - f[prev].c).norm();
        prev = (size_t)i;
        const bool node = anchored && (f[i].c - f[a].c).norm() >= kNode && f[i].fresh;
        if (node) out.push_back({a, (size_t)i});
        if (node || !anchored || path > 2 * kNode) {
            anchored = f[i].fresh;
            a = (size_t)i;
            path = 0;
        }
    }
    return out;
}

inline void chordSums(const std::vector<Frame>& f, const std::vector<Pair>& p, size_t n,
                      double& m, double& g) {
    m = g = 0;
    for (size_t j = 0; j < n && j < p.size(); j++) {
        m += (f[p[j].b].c - f[p[j].a].c).norm();
        g += (f[p[j].b].g - f[p[j].a].g).norm();
    }
}

// Model over GPS chord sums through every run, walked forward: the reference each window is
// read against, so the fit's own scale cancels. 0 when there is no pair.
inline double globalRatio(const std::vector<Frame>& f) {
    double m = 0, g = 0;
    for (size_t lo = 0; lo < f.size();) {
        size_t a, hi;
        runOf(f, lo, a, hi);
        const std::vector<Pair> p = nodePairs(f, lo, +1, lo, hi);
        double pm, pg;
        chordSums(f, p, p.size(), pm, pg);
        m += pm;
        g += pg;
        lo = hi + 1;
    }
    return g > 0 && m > 0 ? m / g : 0;
}

// x[side][L] = ln(window ratio / whole-model ratio) over the first N = L / kNode node pairs
// walking away from frames[k]; side 0 toward earlier positions, 1 toward later ones.
struct Reading {
    bool have[2][kLengths] = {};
    double x[2][kLengths] = {};
};

inline Reading read(const std::vector<Frame>& f, size_t k, double all) {
    Reading r;
    if (all <= 0) return r;
    size_t lo, hi;
    runOf(f, k, lo, hi);
    for (int side = 0; side < 2; side++) {
        const std::vector<Pair> p = nodePairs(f, k, side ? +1 : -1, lo, hi);
        for (int l = 0; l < kLengths; l++) {
            const size_t n = (size_t)std::lround(kLength[l] / kNode);
            if (p.size() < n) continue;
            double m, g;
            chordSums(f, p, n, m, g);
            if (m <= 0 || g <= 0) continue;
            r.have[side][l] = true;
            r.x[side][l] = std::log(m / g) - std::log(all);
        }
    }
    return r;
}

struct Pick {
    bool ok = false;
    int side = 0, l = 0;
    double x = 0;
};

// The reading past its threshold that is most sigmas out, if any.
inline Pick pickOver(const Reading& r, const double (&tau)[kLengths]) {
    Pick best;
    double z = 0;
    for (int side = 0; side < 2; side++)
        for (int l = 0; l < kLengths; l++) {
            if (!r.have[side][l] || std::fabs(r.x[side][l]) <= tau[l]) continue;
            const double zz = std::fabs(r.x[side][l]) / kSigma[l];
            if (best.ok && zz <= z) continue;
            best = {true, side, l, r.x[side][l]};
            z = zz;
        }
    return best;
}

// The reading most sigmas out, whatever its size; `ok` only if it clears `tau`.
inline Pick pickStrongest(const Reading& r, const double (&tau)[kLengths], double& z) {
    Pick best;
    z = -1;
    for (int side = 0; side < 2; side++)
        for (int l = 0; l < kLengths; l++) {
            if (!r.have[side][l]) continue;
            const double zz = std::fabs(r.x[side][l]) / kSigma[l];
            if (zz <= z) continue;
            best = {false, side, l, r.x[side][l]};
            z = zz;
        }
    best.ok = z >= 0 && std::fabs(best.x) > tau[best.l];
    return best;
}

// After growth: the strongest of the stored readings, NOT the newest frame's. The last
// registrations are the ones that bridge two fronts, and a window ending on them reads the
// step between the fronts (Hickory S2: 1.043 at the last check, 0.958 three checks earlier).
inline Pick pickEnd(const std::vector<Reading>& stored, size_t& which) {
    Pick best;
    double z = -1;
    which = 0;
    for (size_t i = 0; i < stored.size(); i++) {
        double zz;
        const Pick p = pickStrongest(stored[i], kTauEnd, zz);
        if (zz <= z) continue;
        best = p;
        z = zz;
        which = i;
    }
    return best;
}

// The frames walking from frames[k] on `side` registered since the last BA; the first that
// was not is the pivot. A CUSUM change-point only caps it: alone it overshot Hickory's
// junction by 25 frames, as the woods' per-pair ratios swing 0.85-1.02 with the GPS.
struct Block {
    bool ok = false;
    size_t newest = 0, pivot = 0, far = 0;  // far: the block frame next to the pivot
    int side = 0;
    size_t frames = 0, pairs = 0;
    bool capped = false;
    double ratio = 0;  // block chord ratio over the whole model's (0 = too few pairs)
    double s = 1;      // factor about the pivot
};

inline Block block(const std::vector<Frame>& f, size_t k, int side, double x, double all,
                   uint32_t last_ba) {
    Block b;
    b.newest = k;
    b.side = side;
    const int dir = side ? +1 : -1;
    size_t lo, hi;
    runOf(f, k, lo, hi);
    long pivot = -1;
    for (long i = (long)k; i >= (long)lo && i <= (long)hi; i += dir)
        if (f[i].stamp <= last_ba) {
            pivot = i;
            break;
        }
    if (pivot < 0 || pivot == (long)k || all <= 0) return b;
    const std::vector<Pair> p = nodePairs(f, k, dir, lo, hi);
    auto dist = [&](size_t i) { return std::llabs(f[i].pos - f[k].pos); };
    double S = 0, ext = 0;
    long cap = -1;
    for (const Pair& q : p) {
        const double m = (f[q.b].c - f[q.a].c).norm(), g = (f[q.b].g - f[q.a].g).norm();
        if (m <= 0 || g <= 0) continue;
        S += std::log(m / g) - std::log(all);
        if ((x < 0 ? S < ext : S > ext)) {
            ext = S;
            cap = (long)q.b;
        }
    }
    if (cap >= 0 && dist((size_t)cap) < dist((size_t)pivot)) {
        pivot = cap;
        b.capped = true;
    }
    b.pivot = (size_t)pivot;
    b.far = (size_t)(pivot - dir);
    b.frames = (size_t)std::labs(pivot - (long)k);
    if (b.frames == 0) return b;
    std::vector<Pair> own;
    for (const Pair& q : p)
        if (dist(q.b) <= dist(b.pivot)) own.push_back(q);
    b.pairs = own.size();
    double m, g;
    chordSums(f, own, own.size(), m, g);
    if (b.pairs >= kBlockMinPairs && m > 0 && g > 0) {
        b.ratio = m / g / all;
        b.s = 1.0 / b.ratio;
    } else {
        b.s = std::exp(-x);
    }
    b.ok = true;
    return b;
}

// The block's cameras (every registered image `key` places between the newest frame and the
// one next to the pivot) and the points only they observe, by b.s about the pivot's centre.
// A block-only point reprojects as before; the junction's shared points carry the change.
template <class KeyFn>
inline size_t rescale(Reconstruction& rec, const std::vector<Frame>& f, const Block& b,
                      KeyFn key) {
    const int64_t seq = f[b.newest].seq;
    const int64_t lo = std::min(f[b.newest].pos, f[b.far].pos);
    const int64_t hi = std::max(f[b.newest].pos, f[b.far].pos);
    const Vec3 c = cameraCenter(rec.images.at(f[b.pivot].img).pose);
    std::unordered_set<uint32_t> moved;
    for (auto& kv : rec.images) {
        if (!kv.second.registered) continue;
        const auto k = key(kv.first);
        if (k.first != seq || k.second < lo || k.second > hi) continue;
        moved.insert(kv.first);
        Pose& ps = kv.second.pose;
        const Vec3 t = mul(ps.R, c + (cameraCenter(ps) - c) * b.s);
        ps.t = {-t.x, -t.y, -t.z};
    }
    for (auto& kv : rec.points3D) {
        bool own = !kv.second.track.empty();
        for (const TrackElement& e : kv.second.track)
            if (!moved.count(e.image_id)) {
                own = false;
                break;
            }
        if (own) kv.second.xyz = c + (kv.second.xyz - c) * b.s;
    }
    return moved.size();
}

}  // namespace bss
}  // namespace sfm
