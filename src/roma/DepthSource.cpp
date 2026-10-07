#include "roma/DepthSource.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <random>
#include <stdexcept>

#include "external/stb_image.h"

namespace roma {

using sfm::Vec2;
using sfm::Vec3;

DepthFiles::DepthFiles(std::function<std::string(const std::string&)> path,
                       std::function<bool(const SourceImage&)> ray, std::string dir,
                       std::function<std::string(const std::string&)> normal_path,
                       std::function<std::string(const std::string&)> image_path)
    : path_(std::move(path)), normal_path_(std::move(normal_path)), image_path_(std::move(image_path)),
      ray_(std::move(ray)), dir_(std::move(dir)), records_(app::read_depth_manifest(dir_)) {}

bool DepthFiles::load(const SourceImage& img, RawDepth& out) {
    const std::string p = path_(img.name);
    if (p.empty()) return false;
    out = RawDepth{};
    out.ray = ray_(img);
    std::error_code ec;
    const std::string rel = std::filesystem::relative(p, dir_, ec).generic_string();
    auto rec = records_.find(rel);
    if (rec != records_.end()) {
        // A map shifted, renamed or left from an older extraction is plausible
        // depth for the wrong picture, and some pass every fit gate.
        out.recorded = true;
        const std::string want = std::filesystem::path(img.name).generic_string();
        if (rec->second.image != want) out.refused = "made for " + rec->second.image;
        else if (app::file_print(p) != rec->second.map_print) out.refused = "changed since geometry wrote it";
        else if (image_path_ && app::file_print(image_path_(img.name)) != rec->second.image_print)
            out.refused = "the image changed since its map was made";
        if (!out.refused.empty()) return true;
        out.ray = rec->second.ray;
    }
    if (!stbi_is_16_bit(p.c_str())) {
        out.refused = "not a 16-bit map";
        return true;
    }
    int w = 0, h = 0, c = 0;
    uint16_t* d = stbi_load_16(p.c_str(), &w, &h, &c, 1);
    if (!d) throw std::runtime_error("cannot read depth " + p);
    out.width = w;
    out.height = h;
    out.value.assign(d, d + (size_t)w * h);
    stbi_image_free(d);
    const double aspect = (double)w / h, cam = (double)img.cam.width / img.cam.height;
    if (std::fabs(aspect / cam - 1.0) > 0.01) {
        out.refused = std::to_string(w) + "x" + std::to_string(h) + " is not the camera's shape (" +
                      std::to_string(img.cam.width) + "x" + std::to_string(img.cam.height) + ")";
        return true;
    }
    out.normal.clear();
    const std::string np = normal_path_ ? normal_path_(img.name) : std::string();
    if (!np.empty()) {
        int nw = 0, nh = 0;
        uint8_t* n = stbi_load(np.c_str(), &nw, &nh, &c, 3);
        if (!n) throw std::runtime_error("cannot read normal map " + np);
        out.normal_width = nw;
        out.normal_height = nh;
        out.normal.assign((size_t)nw * nh * 3, 0.0f);
        // geometry's encoding; black (length under 0.5) is "no normal here".
        for (size_t i = 0; i < (size_t)nw * nh; i++) {
            float v[3], l2 = 0;
            for (int k = 0; k < 3; k++) {
                v[k] = n[i * 3 + k] / 127.5f - 1.0f;
                l2 += v[k] * v[k];
            }
            if (l2 < 0.25f) continue;
            const float inv = 1.0f / std::sqrt(l2);
            for (int k = 0; k < 3; k++) out.normal[i * 3 + k] = v[k] * inv;
        }
        stbi_image_free(n);
    }
    return true;
}

Vec3 mapRay(const SourceImage& img, int w, int h, int x, int y, bool ray) {
    const Vec3 b = img.cam.bearing({(x + 0.5) * img.cam.width / w, (y + 0.5) * img.cam.height / h});
    if (ray) return b.normalized();
    return b.z > 1e-6 ? b * (1.0 / b.z) : Vec3{0, 0, 0};
}

void normalsFromDepth(const SourceImage& img, int w, int h, bool ray,
                      const std::vector<float>& dist, std::vector<float>& normal) {
    normal.assign((size_t)w * h * 3, 0.0f);
    const bool wraps = img.cam.isSpherical();
    const int r = std::max(1, (int)std::lround(std::max(w, h) / 800.0));
    std::vector<Vec3> rays((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) rays[(size_t)y * w + x] = mapRay(img, w, h, x, y, ray);
    auto at = [&](int x, int y, Vec3* p) {
        if (y < 0 || y >= h) return false;
        if (wraps) x = (x % w + w) % w;
        else if (x < 0 || x >= w) return false;
        const float d = dist[(size_t)y * w + x];
        if (!(d > 0)) return false;
        *p = rays[(size_t)y * w + x] * (double)d;
        return true;
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const float d0 = dist[(size_t)y * w + x];
            if (!(d0 > 0)) continue;
            Vec3 p[4], c;
            if (!at(x - r, y, &p[0]) || !at(x + r, y, &p[1]) || !at(x, y - r, &p[2]) ||
                !at(x, y + r, &p[3]) || !at(x, y, &c))
                continue;
            // Only where the stencil stays on one surface (ScanDepth's 10 %).
            bool one = true;
            for (const Vec3& q : p) one = one && std::fabs(q.norm() - c.norm()) <= 0.1 * c.norm();
            if (!one) continue;
            Vec3 n = (p[1] - p[0]).cross(p[2] - p[3]);
            const double len = n.norm();
            if (!(len > 0)) continue;
            n = n * ((n.dot(c) > 0 ? -1.0 : 1.0) / len);
            float* out = &normal[((size_t)y * w + x) * 3];
            out[0] = (float)n.x;
            out[1] = (float)n.y;
            out[2] = (float)n.z;
        }
}

int64_t faceCamera(const SourceImage& img, int w, int h, std::vector<float>& normal) {
    int64_t flipped = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float* n = &normal[((size_t)y * w + x) * 3];
            if (n[0] == 0 && n[1] == 0 && n[2] == 0) continue;
            const Vec3 r = mapRay(img, w, h, x, y, true);
            if (n[0] * r.x + n[1] * r.y + n[2] * r.z <= 0) continue;
            for (int k = 0; k < 3; k++) n[k] = -n[k];
            flipped++;
        }
    return flipped;
}

bool planeNormal(const std::vector<Vec3>& pts, Vec3* n) {
    if (pts.size() < 6) return false;
    Vec3 m{0, 0, 0};
    for (const Vec3& p : pts) m = m + p;
    m = m * (1.0 / (double)pts.size());
    double C[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}, w[3], V[9];
    for (const Vec3& p : pts) {
        const double d[3] = {p.x - m.x, p.y - m.y, p.z - m.z};
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) C[r * 3 + c] += d[r] * d[c];
    }
    sfm::jacobiEigenSymmetric(C, 3, w, V);
    int o[3] = {0, 1, 2};
    std::sort(o, o + 3, [&](int a, int b) { return w[a] < w[b]; });
    if (!(w[o[0]] < 0.1 * w[o[1]]) || !(w[o[1]] > 0.05 * w[o[2]])) return false;
    *n = Vec3{V[0 * 3 + o[0]], V[1 * 3 + o[0]], V[2 * 3 + o[0]]}.normalized();
    return true;
}

double normalAngleDeg(const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(a.dot(b), -1.0, 1.0)) * 180.0 / M_PI;
}

bool DepthField::normalAtPixel(int x, int y, Vec3* n) const {
    if (normal.empty() || x < 0 || y < 0 || x >= width || y >= height) return false;
    const int8_t* v = &normal[((size_t)y * width + x) * 3];
    if (v[0] == 0 && v[1] == 0 && v[2] == 0) return false;
    *n = Vec3{(double)v[0], (double)v[1], (double)v[2]}.normalized();
    return true;
}

bool DepthField::normalAt(const SourceImage& img, const Vec3& X, Vec3* n) const {
    if (!ok || normal.empty()) return false;
    const Vec3 Xc = sfm::mul(img.pose.R, X) + img.pose.t;
    if (!img.cam.isSpherical() && !(Xc.z > 0)) return false;
    const Vec2 px = img.cam.project(Xc);
    Vec3 nc;
    if (!normalAtPixel((int)std::floor(px.x * width / img.cam.width),
                       (int)std::floor(px.y * height / img.cam.height), &nc))
        return false;
    *n = sfm::mul(sfm::transpose(img.pose.R), nc);
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
        if (o.holdout_mod > 0 && pid % (uint64_t)o.holdout_mod == 0) continue;
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
    std::vector<float> derived;
    normalsFromDepth(img, f.width, f.height, f.ray, f.dist, derived);
    const std::vector<float>* use = &derived;
    f.normal_from = NormalFrom::Depth;
    std::vector<float> file;
    if (o.normal_files && !raw.normal.empty()) {
        // To the depth's grid (nearest), checked against the depth's own normals
        // for the convention, then faced to each pixel's ray.
        file.assign(derived.size(), 0.0f);
        for (int y = 0; y < f.height; y++)
            for (int x = 0; x < f.width; x++) {
                const size_t s = ((size_t)(y * raw.normal_height / f.height) * raw.normal_width +
                                  (size_t)(x * raw.normal_width / f.width)) * 3;
                for (int k = 0; k < 3; k++) file[((size_t)y * f.width + x) * 3 + k] = raw.normal[s + k];
            }
        // Checked BEFORE facing: faced, any field lands in the camera's hemisphere
        // and an unrelated map scores ~0.5 against the depth's.
        std::vector<double> cs;
        for (size_t i = 0; i < derived.size(); i += 3) {
            const double c = (double)file[i] * derived[i] + (double)file[i + 1] * derived[i + 1] +
                             (double)file[i + 2] * derived[i + 2];
            const bool both = (file[i] != 0 || file[i + 1] != 0 || file[i + 2] != 0) &&
                              (derived[i] != 0 || derived[i + 1] != 0 || derived[i + 2] != 0);
            if (both) cs.push_back(c);
        }
        if (!cs.empty()) {
            std::nth_element(cs.begin(), cs.begin() + (long)(cs.size() / 2), cs.end());
            f.normal_file_cos = cs[cs.size() / 2];
        }
        f.normal_flipped = faceCamera(img, f.width, f.height, file);
        if (f.normal_file_cos >= o.normal_min_cos) {
            use = &file;
            f.normal_from = NormalFrom::File;
        }
    }
    f.normal.assign(use->size(), 0);
    for (size_t i = 0; i < use->size(); i++)
        f.normal[i] = (int8_t)std::lround(std::clamp((*use)[i], -1.0f, 1.0f) * 127.0f);
    return f;
}

std::vector<DensePoint> depthPointsForView(
    const std::vector<View>& views, const std::vector<SourceImage>& images,
    const std::vector<DepthField>& fields, const std::vector<int>& others, int ref, int W,
    int H, const std::vector<int64_t>& samples, const DepthAgreeOptions& o,
    const std::function<std::array<float, 3>(double, double)>& colour_at,
    const std::function<bool(int64_t, const Vec3&, const Vec3*)>& local_ok, DensifyStats& st) {
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
        Vec3 nA;
        const bool has_n = fA.normalAtPixel(mx, my, &nA);
        if (has_n) nA = sfm::mul(Rt, nA);
        if (local_ok && !local_ok(s, X, has_n ? &nA : nullptr)) { st.depth_local++; continue; }
        DensePoint p;
        p.from_depth = true;
        std::vector<int> agreeing;
        Vec3 sum = X;
        int n = 1, agree = 0, through = 0, front = 0;
        double worst = 0;
        std::vector<double> angles;
        uint64_t h = (uint64_t)s * 0x9E3779B97F4A7C15ull + (uint64_t)ref;
        for (int v : others) {
            if (v == A.image) continue;
            const Vec3 ra = (imA.centre - X).normalized(), rb = (images[(size_t)v].centre - X).normalized();
            if (normalAngleDeg(ra, rb) < o.min_parallax_deg) { st.depth_vote_close++; continue; }
            double field, own;
            if (!fields[(size_t)v].at(images[(size_t)v], X, &field, &own)) continue;
            const double rel = (own - field) / field;
            if (rel < -o.through) { through++; continue; }
            if (rel > o.tol && rel <= o.front) { front++; continue; }
            if (std::fabs(rel) > o.tol) continue;
            agree++;
            worst = std::max(worst, std::fabs(rel));
            const DepthField& fv = fields[(size_t)v];
            Vec3 nv;
            if (has_n && fv.normalAt(images[(size_t)v], X, &nv)) {
                const double ang = normalAngleDeg(nA, nv);
                angles.push_back(ang);
                st.normal_agree_hist[std::min<size_t>(35, (size_t)(ang / 5))]++;
                // The null: the same image's normal at an unrelated pixel.
                for (int tries = 0; tries < 8; tries++) {
                    h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 29;
                    Vec3 nr;
                    if (!fv.normalAtPixel((int)(h % (uint64_t)fv.width), (int)((h >> 32) % (uint64_t)fv.height), &nr)) continue;
                    nr = sfm::mul(sfm::transpose(images[(size_t)v].pose.R), nr);
                    st.normal_null_hist[std::min<size_t>(35, (size_t)(normalAngleDeg(nA, nr) / 5))]++;
                    break;
                }
            }
            const Vec3 C = images[(size_t)v].centre;
            sum = sum + (C + (X - C) * (field / own));
            n++;
            agreeing.push_back(v);
        }
        // A vote, not a veto: one image whose map is too deep there (a doubled
        // copy) sees through every true point in front of it.
        if (o.vote && (agree < o.min_agree || agree <= front)) { st.depth_disagree++; continue; }
        if (o.vote && agree <= through) { st.depth_through++; continue; }
        if (o.normal_check && !angles.empty()) {
            std::nth_element(angles.begin(), angles.begin() + (long)(angles.size() / 2), angles.end());
            if (angles[angles.size() / 2] > o.normal_deg) { st.depth_normal++; continue; }
        }
        if (has_n)
            for (int k = 0; k < 3; k++) p.normal[k] = (float)(k == 0 ? nA.x : k == 1 ? nA.y : nA.z);
        p.xyz = sum * (1.0 / n);
        // The track is where the AVERAGED point lands, the reference first: the
        // average leaves every ray it came from.
        auto observe = [&](int image) {
            for (int u : views_of[(size_t)image]) {
                const View& U = views[(size_t)u];
                const Vec3 Xc = sfm::mul(U.R, p.xyz) + U.t;
                if (!U.cam.isSpherical() && !(Xc.z > 0)) continue;
                const Vec2 q = U.cam.project(Xc);
                if (q.x < 0 || q.y < 0 || q.x >= U.cam.width || q.y >= U.cam.height) continue;
                p.track.push_back({u, q.x, q.y});
                return;
            }
        };
        observe(A.image);
        if (p.track.empty()) p.track.push_back({ref, x, y});
        for (int v : agreeing) observe(v);
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
