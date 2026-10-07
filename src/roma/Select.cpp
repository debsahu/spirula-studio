// Ported from Lichtfeld-Densification-Plugin (GPL-3.0-or-later), Copyright (c) 2025 Shady Gmira and contributors; core/selection.py@ab0b04e.
#include "roma/Select.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <set>

#include "data/CameraMath.h"

namespace roma {

using sfm::Mat3;
using sfm::Vec3;

const char* const kFaceNames[6] = {"front", "right", "down", "left", "up", "back"};

std::vector<SourceImage> sourceImages(const sfm::Reconstruction& rec) {
    std::vector<SourceImage> out;
    for (const auto& kv : rec.images) {
        const sfm::Image& im = kv.second;
        if (!im.registered) continue;
        SourceImage s;
        s.id = im.id;
        s.name = im.name;
        s.cam = rec.cameras.at(im.camera_id);
        s.pose = im.pose;
        s.centre = sfm::cameraCenter(im.pose);
        s.points = sfm::observedPoints(im);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<double> flatPose(const sfm::Pose& p) {
    return {p.R[0], p.R[1], p.R[2], p.t.x, p.R[3], p.R[4], p.R[5], p.t.y,
            p.R[6], p.R[7], p.R[8], p.t.z, 0.0,    0.0,    0.0,    1.0};
}

std::vector<int> refsByVisibility(const std::vector<SourceImage>& imgs,
                                  const std::vector<int>& allowed, int k) {
    k = std::min<int>(k, (int)allowed.size());
    std::set<uint64_t> covered;
    auto score = [&](int i) {
        int64_t n = 0;
        for (uint64_t p : imgs[(size_t)i].points) n += !covered.count(p);
        return n;
    };
    // (bound, -index): the heap's top is the largest bound, then the lower index.
    std::priority_queue<std::pair<int64_t, int>> heap;
    for (int i : allowed) heap.push({(int64_t)imgs[(size_t)i].points.size(), -i});
    std::vector<int> out;
    while ((int)out.size() < k && !heap.empty()) {
        auto top = heap.top();
        heap.pop();
        const int i = -top.second;
        const int64_t s = score(i);
        if (heap.empty() || std::make_pair(s, -i) >= heap.top()) {
            out.push_back(i);
            for (uint64_t p : imgs[(size_t)i].points) covered.insert(p);
        } else {
            heap.push({s, -i});
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> refsKCenters(const std::vector<SourceImage>& imgs,
                              const std::vector<int>& allowed, int k) {
    const size_t n = allowed.size();
    if (n == 0) return {};
    k = std::max(1, std::min<int>(k, (int)n));
    std::vector<std::array<double, 16>> X(n);
    for (size_t i = 0; i < n; i++) {
        const std::vector<double> f = flatPose(imgs[(size_t)allowed[i]].pose);
        std::copy(f.begin(), f.end(), X[i].begin());
    }
    for (int d = 0; d < 16; d++) {
        double mu = 0, var = 0;
        for (size_t i = 0; i < n; i++) mu += X[i][(size_t)d];
        mu /= (double)n;
        for (size_t i = 0; i < n; i++) var += (X[i][(size_t)d] - mu) * (X[i][(size_t)d] - mu);
        const double sd = std::sqrt(var / (double)n) + 1e-8;
        for (size_t i = 0; i < n; i++) X[i][(size_t)d] = (X[i][(size_t)d] - mu) / sd;
    }
    auto dist = [&](size_t a, size_t b) {
        double s = 0;
        for (int d = 0; d < 16; d++) s += (X[a][(size_t)d] - X[b][(size_t)d]) * (X[a][(size_t)d] - X[b][(size_t)d]);
        return std::sqrt(s);
    };
    size_t first = 0;
    double best = -1;
    for (size_t i = 0; i < n; i++) {
        double s = 0;
        for (int d = 0; d < 16; d++) s += X[i][(size_t)d] * X[i][(size_t)d];
        if (s > best) { best = s; first = i; }
    }
    std::vector<size_t> centres{first};
    std::vector<double> dmin(n);
    for (size_t i = 0; i < n; i++) dmin[i] = dist(i, first);
    dmin[first] = -INFINITY;
    while ((int)centres.size() < k) {
        const size_t nxt = (size_t)(std::max_element(dmin.begin(), dmin.end()) - dmin.begin());
        centres.push_back(nxt);
        for (size_t i = 0; i < n; i++) dmin[i] = std::min(dmin[i], dist(i, nxt));
        dmin[nxt] = -INFINITY;
    }
    std::vector<int> out;
    for (size_t c : centres) out.push_back(allowed[c]);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> neighboursByPose(const std::vector<SourceImage>& imgs,
                                  const std::vector<int>& allowed, int ref, int k) {
    const std::vector<double> fr = flatPose(imgs[(size_t)ref].pose);
    std::vector<std::pair<double, int>> d;
    for (int i : allowed) {
        if (i == ref) continue;
        const std::vector<double> f = flatPose(imgs[(size_t)i].pose);
        double s = 0;
        for (int c = 0; c < 16; c++) s += (f[(size_t)c] - fr[(size_t)c]) * (f[(size_t)c] - fr[(size_t)c]);
        d.push_back({std::sqrt(s), i});
    }
    std::stable_sort(d.begin(), d.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<int> out;
    for (size_t i = 0; i < d.size() && (int)out.size() < k; i++) out.push_back(d[i].second);
    return out;
}

std::vector<int> neighboursByCovis(const std::vector<SourceImage>& imgs,
                                   const sfm::Reconstruction& rec,
                                   const std::vector<int>& allowed, int ref, int k,
                                   double min_angle_deg, double max_baseline) {
    const SourceImage& r = imgs[(size_t)ref];
    struct Cand { int64_t shared; int idx; };
    std::vector<Cand> c;
    for (int i : allowed) {
        if (i == ref) continue;
        const SourceImage& q = imgs[(size_t)i];
        if (max_baseline > 0 && (q.centre - r.centre).norm() > max_baseline) continue;
        std::vector<uint64_t> both;
        std::set_intersection(r.points.begin(), r.points.end(), q.points.begin(),
                              q.points.end(), std::back_inserter(both));
        if (both.empty()) continue;
        Vec3 g{0, 0, 0};
        for (uint64_t p : both) g = g + rec.points3D.at(p).xyz;
        g = g * (1.0 / (double)both.size());
        const double ang = sfm::triangulationAngle(g, r.centre, q.centre) * 57.29577951308232;
        if (ang < min_angle_deg) continue;
        c.push_back({(int64_t)both.size(), i});
    }
    std::stable_sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.shared > b.shared; });
    std::vector<int> out;
    for (size_t i = 0; i < c.size() && (int)out.size() < k; i++) out.push_back(c[i].idx);
    return out;
}

std::vector<View> imageViews(const SourceImage& img, int image_index, bool split, int face_size) {
    std::string stem = img.name;
    const size_t dot = stem.rfind('.');
    if (dot != std::string::npos && stem.find('/', dot) == std::string::npos) stem.erase(dot);
    std::vector<View> out;
    if (!split) {
        View v;
        v.name = stem;
        v.image = image_index;
        v.cam = img.cam;
        v.R = img.pose.R;
        v.t = img.pose.t;
        v.centre = img.centre;
        out.push_back(v);
        return out;
    }
    const double* axes = camhost::equirect_face_axes();
    for (int f = 0; f < 6; f++) {
        View v;
        v.name = stem + "_" + kFaceNames[f];
        v.image = image_index;
        v.face = f;
        v.cam = sfm::Camera::defaultFor(0, face_size, face_size, face_size * 0.5,
                                        sfm::CamModel::Pinhole);
        for (int i = 0; i < 9; i++) v.face_R[(size_t)i] = axes[9 * f + i];
        v.R = sfm::mul(v.face_R, img.pose.R);
        v.t = sfm::mul(v.face_R, img.pose.t);
        v.centre = img.centre;
        out.push_back(v);
    }
    return out;
}

std::vector<int> pairFaces(const std::vector<View>& views, int a,
                           const std::vector<int>& nbr_views, double max_deg, bool all) {
    if (all) return nbr_views;
    auto axis = [&](int v) {
        const Mat3& R = views[(size_t)v].R;
        return Vec3{R[6], R[7], R[8]};   // R^T (0,0,1): the optical axis in the world
    };
    const Vec3 za = axis(a);
    const double cmin = std::cos(max_deg / 57.29577951308232);
    std::vector<int> out;
    for (int b : nbr_views)
        if ((views[(size_t)a].face < 0 && views[(size_t)b].face < 0) || za.dot(axis(b)) >= cmin)
            out.push_back(b);
    return out;
}

}  // namespace roma
