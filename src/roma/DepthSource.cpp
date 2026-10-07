#include "roma/DepthSource.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

#include "external/stb_image.h"

namespace roma {

using sfm::Vec2;
using sfm::Vec3;

DepthFiles::DepthFiles(std::function<std::string(const std::string&)> path,
                       std::function<bool(const SourceImage&)> ray, std::string dir)
    : path_(std::move(path)), ray_(std::move(ray)), dir_(std::move(dir)) {}

bool DepthFiles::load(const SourceImage& img, RawDepth& out) {
    const std::string p = path_(img.name);
    if (p.empty()) return false;
    int w = 0, h = 0, c = 0;
    uint16_t* d = stbi_load_16(p.c_str(), &w, &h, &c, 1);
    if (!d) throw std::runtime_error("cannot read depth " + p);
    out.width = w;
    out.height = h;
    out.value.assign(d, d + (size_t)w * h);
    stbi_image_free(d);
    out.ray = ray_(img);
    return true;
}

bool DepthField::at(const SourceImage& img, const Vec3& X, double* field, double* own) const {
    if (!ok) return false;
    const Vec3 Xc = sfm::mul(img.pose.R, X) + img.pose.t;
    if (!img.cam.isSpherical() && !(Xc.z > 0)) return false;
    const Vec2 px = img.cam.project(Xc);
    const int x = (int)std::floor(px.x * width / img.cam.width);
    const int y = (int)std::floor(px.y * height / img.cam.height);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    const float d = dist[(size_t)y * width + x];
    if (!(d > 0)) return false;
    *field = d;
    *own = ray ? Xc.norm() : Xc.z;
    return true;
}

namespace {

double spearman(const std::vector<double>& a, const std::vector<double>& b) {
    auto ranks = [](const std::vector<double>& v) {
        std::vector<size_t> o(v.size());
        std::iota(o.begin(), o.end(), 0);
        std::sort(o.begin(), o.end(), [&](size_t i, size_t j) { return v[i] < v[j]; });
        std::vector<double> r(v.size());
        for (size_t k = 0; k < o.size(); k++) r[o[k]] = (double)k;
        return r;
    };
    const std::vector<double> ra = ranks(a), rb = ranks(b);
    const double n = (double)a.size(), m = (n - 1) / 2;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < a.size(); i++) {
        sab += (ra[i] - m) * (rb[i] - m);
        saa += (ra[i] - m) * (ra[i] - m);
        sbb += (rb[i] - m) * (rb[i] - m);
    }
    return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 0;
}

}  // namespace

DepthField fitDepth(const SourceImage& img, const sfm::Reconstruction& rec, const RawDepth& raw,
                    const DepthFitOptions& o) {
    DepthField f;
    f.width = raw.width;
    f.height = raw.height;
    f.ray = raw.ray;
    // Anchors: the image's own sparse points (its measured visibility), where
    // the map has data.
    std::vector<double> xs, ys;   // disparities: raw, true
    for (uint64_t pid : img.points) {
        auto it = rec.points3D.find(pid);
        if (it == rec.points3D.end()) continue;
        const Vec3 Xc = sfm::mul(img.pose.R, it->second.xyz) + img.pose.t;
        if (!img.cam.isSpherical() && !(Xc.z > 0)) continue;
        const Vec2 px = img.cam.project(Xc);
        const int x = (int)std::floor(px.x * raw.width / img.cam.width);
        const int y = (int)std::floor(px.y * raw.height / img.cam.height);
        if (x < 0 || y < 0 || x >= raw.width || y >= raw.height) continue;
        const float v = raw.value[(size_t)y * raw.width + x];
        const double d = raw.ray ? Xc.norm() : Xc.z;
        if (!(v > 0) || !(d > 0)) continue;
        xs.push_back(1.0 / v);
        ys.push_back(1.0 / d);
    }
    f.anchors = (int)xs.size();
    if (!o.align) {
        f.a = 1;
        f.b = 0;
    } else {
        if (f.anchors < o.min_anchors) {
            f.refused = "anchors " + std::to_string(f.anchors);
            return f;
        }
        f.spearman = spearman(xs, ys);
        auto rel = [&](double a, double b, size_t i) {
            const double p = a * xs[i] + b;
            return p > 0 ? std::fabs(1.0 / p - 1.0 / ys[i]) * ys[i] : INFINITY;
        };
        std::mt19937_64 rng(o.seed);
        std::uniform_int_distribution<size_t> pick(0, xs.size() - 1);
        int best = -1;
        double ba = 0, bb = 0;
        for (int it = 0; it < 300; it++) {
            const size_t i = pick(rng), j = pick(rng);
            if (i == j || std::fabs(xs[i] - xs[j]) < 1e-12) continue;
            const double a = (ys[i] - ys[j]) / (xs[i] - xs[j]);
            if (!(a > 0)) continue;
            const double b = ys[i] - a * xs[i];
            int n = 0;
            for (size_t k = 0; k < xs.size(); k++) n += rel(a, b, k) <= o.inlier_rel;
            if (n > best) { best = n; ba = a; bb = b; }
        }
        if (best < 0) {
            f.refused = "no positive-slope fit";
            return f;
        }
        // Huber IRLS on disparity, over the RANSAC inliers.
        for (int it = 0; it < 8; it++) {
            double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
            for (size_t k = 0; k < xs.size(); k++) {
                const double r = rel(ba, bb, k);
                if (!(r <= o.inlier_rel)) continue;
                const double w = r <= 0.03 ? 1.0 : 0.03 / r;
                sw += w; sx += w * xs[k]; sy += w * ys[k];
                sxx += w * xs[k] * xs[k]; sxy += w * xs[k] * ys[k];
            }
            const double den = sw * sxx - sx * sx;
            if (!(std::fabs(den) > 1e-300)) break;
            const double a = (sw * sxy - sx * sy) / den;
            if (!(a > 0)) break;
            bb = (sy - a * sx) / sw;
            ba = a;
        }
        f.a = ba;
        f.b = bb;
        std::vector<double> res;
        for (size_t k = 0; k < xs.size(); k++)
            if (rel(ba, bb, k) <= o.inlier_rel) res.push_back(rel(ba, bb, k));
        f.inliers = (int)res.size();
        if (!res.empty()) {
            std::nth_element(res.begin(), res.begin() + (long)(res.size() / 2), res.end());
            f.rel_residual = res[res.size() / 2];
        }
        if (f.inliers < o.min_inliers) { f.refused = "inliers " + std::to_string(f.inliers); return f; }
        if ((double)f.inliers < o.min_inlier_frac * f.anchors) {
            f.refused = "inlier share " + std::to_string((double)f.inliers / f.anchors);
            return f;
        }
        if (f.spearman < o.min_spearman) { f.refused = "rank correlation " + std::to_string(f.spearman); return f; }
    }
    f.dist.assign(raw.value.size(), 0.0f);
    int64_t valid = 0, lost = 0;
    for (size_t i = 0; i < raw.value.size(); i++) {
        const float v = raw.value[i];
        if (!(v > 0)) continue;   // the no-data sentinel
        valid++;
        const double p = f.a / v + f.b;
        if (!(p > 0)) { lost++; continue; }
        f.dist[i] = (float)(1.0 / p);
    }
    if (o.align && valid > 0 && (double)lost > o.max_lost * (double)valid) {
        f.refused = "pixels lost to the fit " + std::to_string((double)lost / valid);
        f.dist.clear();
        return f;
    }
    f.ok = true;
    return f;
}

std::vector<DensePoint> depthPointsForView(
    const std::vector<View>& views, const std::vector<SourceImage>& images,
    const std::vector<DepthField>& fields, const std::vector<int>& others, int ref, int W,
    int H, const std::vector<int64_t>& samples, const DepthAgreeOptions& o,
    const std::function<std::array<float, 3>(double, double)>& colour_at,
    const std::function<bool(int64_t, double)>& local_ok, DensifyStats& st) {
    const View& A = views[(size_t)ref];
    const SourceImage& imA = images[(size_t)A.image];
    const DepthField& fA = fields[(size_t)A.image];
    std::vector<DensePoint> out;
    if (!fA.ok) return out;
    const sfm::Mat3 Rt = sfm::transpose(imA.pose.R);
    // The views of each image, to name an observation by a view it falls in.
    std::vector<std::vector<int>> views_of(images.size());
    for (size_t v = 0; v < views.size(); v++) views_of[(size_t)views[v].image].push_back((int)v);
    for (int64_t s : samples) {
        st.depth_samples++;
        const double x = ((double)(s % W) + 0.5) * A.cam.width / W;
        const double y = ((double)(s / W) + 0.5) * A.cam.height / H;
        const Vec3 bcam = sfm::mul(sfm::transpose(A.face_R), A.cam.bearing({x, y}));
        const Vec2 src = imA.cam.project(bcam);
        const int mx = (int)std::floor(src.x * fA.width / imA.cam.width);
        const int my = (int)std::floor(src.y * fA.height / imA.cam.height);
        if (mx < 0 || my < 0 || mx >= fA.width || my >= fA.height) { st.depth_nodata++; continue; }
        const float d = fA.dist[(size_t)my * fA.width + mx];
        if (!(d > 0)) { st.depth_nodata++; continue; }
        // Off a depth edge or a grazing surface, where one map cell spans more
        // depth than the agreement tolerance: no data within 2 cells, or 4 tol of spread.
        float lo = d, hi = d;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                int xx = mx + dx;
                const int yy = my + dy;
                if (yy < 0 || yy >= fA.height) continue;
                if (imA.cam.isSpherical()) xx = (xx + fA.width) % fA.width;
                else if (xx < 0 || xx >= fA.width) continue;
                const float v = fA.dist[(size_t)yy * fA.width + xx];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        if (!(lo > 0) || hi > lo * (1.0 + 4 * o.tol)) { st.depth_edge++; continue; }
        if (!fA.ray && !(bcam.z > 1e-6)) { st.depth_nodata++; continue; }
        const double along = fA.ray ? d : d / bcam.z;
        const Vec3 X = imA.centre + sfm::mul(Rt, bcam.normalized()) * along;
        if (local_ok && !local_ok(s, along)) { st.depth_local++; continue; }
        DensePoint p;
        p.from_depth = true;
        p.track.push_back({ref, x, y});
        Vec3 sum = X;
        int n = 1, agree = 0, through = 0, front = 0;
        double worst = 0;
        for (int v : others) {
            if (v == A.image) continue;
            double field, own;
            if (!fields[(size_t)v].at(images[(size_t)v], X, &field, &own)) continue;
            const double rel = (own - field) / field;
            if (rel < -o.through) { through++; continue; }
            if (rel > o.tol && rel <= o.front) { front++; continue; }
            if (std::fabs(rel) > o.tol) continue;
            agree++;
            worst = std::max(worst, std::fabs(rel));
            const Vec3 C = images[(size_t)v].centre;
            sum = sum + (C + (X - C) * (field / own));
            n++;
            for (int u : views_of[(size_t)v]) {
                const View& U = views[(size_t)u];
                const Vec3 Xc = sfm::mul(U.R, X) + U.t;
                if (!U.cam.isSpherical() && !(Xc.z > 0)) continue;
                const Vec2 q = U.cam.project(Xc);
                if (q.x < 0 || q.y < 0 || q.x >= U.cam.width || q.y >= U.cam.height) continue;
                p.track.push_back({u, q.x, q.y});
                break;
            }
        }
        // A vote, not a veto: one image whose map is too deep there (a doubled
        // copy) sees through every true point in front of it.
        if (o.vote && (agree < o.min_agree || agree <= front)) { st.depth_disagree++; continue; }
        if (o.vote && agree <= through) { st.depth_through++; continue; }
        p.xyz = sum * (1.0 / n);
        p.distinct_images = 1 + agree;
        p.error = worst;
        if (colour_at) {
            const std::array<float, 3> c = colour_at(x, y);
            for (int k = 0; k < 3; k++) p.rgb[k] = c[(size_t)k];
        }
        out.push_back(std::move(p));
    }
    st.depth_kept += (int64_t)out.size();
    return out;
}

}  // namespace roma
