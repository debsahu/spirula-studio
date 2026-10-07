#include "roma/Synthetic.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <unordered_map>

#include "data/CameraMath.h"
#include "external/stb_image_write.h"
#include "roma/Select.h"

namespace roma {

namespace fs = std::filesystem;
using sfm::Mat3;
using sfm::Vec2;
using sfm::Vec3;

namespace {

constexpr double kDeg = 0.017453292519943295;

Quad rect(const Vec3& c, const Vec3& n_in, const Vec3& along, double half_u, double half_v) {
    const Vec3 n = n_in.normalized();
    const Vec3 e1 = (along - n * along.dot(n)).normalized();
    const Vec3 e2 = n.cross(e1);
    Quad q;
    q.o = c - e1 * half_u - e2 * half_v;
    q.u = e1 * (2 * half_u);
    q.v = e2 * (2 * half_v);
    return q;
}

double hash3(int64_t x, int64_t y, int64_t z, int64_t s) {
    uint64_t h = (uint64_t)(x * 73856093LL) ^ (uint64_t)(y * 19349663LL) ^
                 (uint64_t)(z * 83492791LL) ^ (uint64_t)(s * 2654435761LL);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL; h ^= h >> 33;
    return (double)(h & 0xffffff) / (double)0xffffff;
}

double valueNoise(const Vec3& p, int64_t seed) {
    const double fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    auto sm = [](double t) { return t * t * (3 - 2 * t); };
    const double tx = sm(p.x - fx), ty = sm(p.y - fy), tz = sm(p.z - fz);
    const int64_t ix = (int64_t)fx, iy = (int64_t)fy, iz = (int64_t)fz;
    double v = 0;
    for (int c = 0; c < 8; c++) {
        const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        const double w = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty) * (dz ? tz : 1 - tz);
        v += w * hash3(ix + dx, iy + dy, iz + dz, seed);
    }
    return v;
}

Mat3 lookAt(const Vec3& eye, const Vec3& target) {
    const Vec3 z = (target - eye).normalized();
    const Vec3 x = z.cross({0, 0, 1}).normalized();
    const Vec3 y = z.cross(x);
    return {x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z};
}

// World -> camera for an upright panorama turned by `yaw` and tipped by `tilt`.
Mat3 panoRotation(double yaw, double tilt) {
    const Mat3 base = {0, -1, 0, 0, 0, -1, 1, 0, 0};   // cam z = world +x, cam y = world -z
    const double c = std::cos(yaw), s = std::sin(yaw);
    const Mat3 Rz = {c, -s, 0, s, c, 0, 0, 0, 1};
    const double ct = std::cos(tilt), st = std::sin(tilt);
    const Mat3 Rx = {1, 0, 0, 0, ct, -st, 0, st, ct};
    return sfm::mul(Rx, sfm::mul(base, sfm::transpose(Rz)));
}

double pointRectDistance(const Quad& q, const Vec3& p) {
    const Vec3 d = p - q.o;
    const double uu = q.u.dot(q.u), vv = q.v.dot(q.v);
    const double a = std::clamp(d.dot(q.u) / uu, 0.0, 1.0);
    const double b = std::clamp(d.dot(q.v) / vv, 0.0, 1.0);
    return (p - (q.o + q.u * a + q.v * b)).norm();
}

bool sees(const Scene& sc, const sfm::Camera& cam, const sfm::Pose& pose, const Vec3& X,
          Vec2* px_out) {
    const Vec3 Xc = sfm::mul(pose.R, X) + pose.t;
    if (!cam.isSpherical() && !(Xc.z > 1e-6)) return false;
    const Vec2 px = cam.project(Xc);
    if (!(px.x >= 0 && px.x < cam.width && px.y >= 0 && px.y < cam.height)) return false;
    const Vec3 C = sfm::cameraCenter(pose);
    const double d = (X - C).norm();
    const double h = sc.hit(C, (X - C) * (1.0 / d));
    if (h < d * (1 - 1e-7) - 1e-7) return false;
    if (px_out) *px_out = px;
    return true;
}

}  // namespace

double Scene::hit(const Vec3& origin, const Vec3& dir, int* quad) const {
    double best = INFINITY;
    for (size_t i = 0; i < quads.size(); i++) {
        const Quad& q = quads[i];
        const Vec3 n = q.u.cross(q.v);
        const double den = n.dot(dir);
        if (std::fabs(den) < 1e-12) continue;
        const double t = n.dot(q.o - origin) / den;
        if (!(t > 1e-9) || t >= best) continue;
        const Vec3 d = origin + dir * t - q.o;
        const double a = d.dot(q.u) / q.u.dot(q.u), b = d.dot(q.v) / q.v.dot(q.v);
        if (a < 0 || a > 1 || b < 0 || b > 1) continue;
        best = t;
        if (quad) *quad = (int)i;
    }
    return best;
}

double Scene::distance(const Vec3& p) const {
    double best = INFINITY;
    for (const Quad& q : quads) best = std::min(best, pointRectDistance(q, p));
    return best;
}

void Scene::albedo(const Vec3& p, int quad, uint8_t rgb[3]) const {
    double n = 0, amp = 1, tot = 0;
    for (double f : {3.0, 7.0, 17.0, 41.0, 97.0}) {
        n += amp * valueNoise(p * f, (int64_t)f);
        tot += amp;
        amp *= 0.6;
    }
    n /= tot;
    const double tint[3] = {0.6 + 0.4 * hash3(quad, 1, 0, 9), 0.6 + 0.4 * hash3(quad, 2, 0, 9),
                            0.6 + 0.4 * hash3(quad, 3, 0, 9)};
    const double v = std::clamp((n - 0.5) * 2.6 + 0.5, 0.0, 1.0);
    for (int c = 0; c < 3; c++) rgb[c] = (uint8_t)std::lround(255.0 * std::clamp(v * tint[c], 0.0, 1.0));
}

Scene stairScene() {
    Scene s;
    for (int i = 0; i < 6; i++) {
        Quad r;
        r.o = {0.3 * i, 0.0, 0.2 * i};
        r.u = {0, 1.0, 0};
        r.v = {0, 0, 0.2};
        r.riser = true;
        s.quads.push_back(r);
        Quad t;
        t.o = {0.3 * i, 0.0, 0.2 * (i + 1)};
        t.u = {0.3, 0, 0};
        t.v = {0, 1.0, 0};
        s.quads.push_back(t);
    }
    s.quads.push_back(rect({0.5, 0.5, -0.02}, {std::sin(2 * kDeg), 0, std::cos(2 * kDeg)},
                           {1, 0, 0}, 3.5, 3.5));
    s.quads.push_back(rect({2.8, 0.5, 1.0}, {-std::cos(15 * kDeg), std::sin(15 * kDeg), 0},
                           {0, 0, 1}, 1.6, 3.0));
    s.quads.push_back(rect({0.5, -0.6, 1.0}, {-std::sin(10 * kDeg), std::cos(10 * kDeg), 0.08},
                           {1, 0, 0}, 3.0, 1.6));
    return s;
}

void writeStairDataset(const Scene& sc, const std::string& dir, int pin_w, int eq_w,
                       int sparse_points) {
    const fs::path root(dir);
    fs::create_directories(root / "images");
    fs::create_directories(root / "masks");
    fs::create_directories(root / "sparse" / "0");
    sfm::Reconstruction rec;
    const int pin_h = pin_w * 3 / 4;
    rec.cameras[1] = sfm::Camera::defaultFor(1, pin_w, pin_h, 0.73 * pin_w, sfm::CamModel::Pinhole);
    rec.cameras[2] = sfm::Camera::defaultFor(2, eq_w, eq_w / 2, 0, sfm::CamModel::Equirect);

    struct Cam { std::string name; uint32_t cam; sfm::Pose pose; };
    std::vector<Cam> cams;
    const Vec3 target{0.9, 0.5, 0.6};
    for (int i = 0; i < 8; i++) {
        const double az = (95.0 + 95.0 * i / 7.0) * kDeg;
        const Vec3 eye{target.x + 2.8 * std::cos(az), target.y + 2.8 * std::sin(az),
                       i % 2 ? 1.8 : 1.2};
        sfm::Pose p;
        p.R = lookAt(eye, target);
        p.t = sfm::mul(p.R, eye) * -1.0;
        char nm[32];
        std::snprintf(nm, sizeof nm, "pin_%02d.png", i);
        cams.push_back({nm, 1, p});
    }
    const Vec3 pano_at[4] = {{-1.2, 1.2, 1.4}, {-0.8, 2.2, 1.0}, {0.2, 2.0, 1.7}, {-1.6, 0.2, 1.2}};
    const double yaw[4] = {0, 37, 113, -64}, tilt[4] = {0, 3, -4, 6};
    for (int i = 0; i < 4; i++) {
        sfm::Pose p;
        p.R = panoRotation(yaw[i] * kDeg, tilt[i] * kDeg);
        p.t = sfm::mul(p.R, pano_at[i]) * -1.0;
        char nm[32];
        std::snprintf(nm, sizeof nm, "pano_%02d.png", i);
        cams.push_back({nm, 2, p});
    }

    for (size_t k = 0; k < cams.size(); k++) {
        const sfm::Camera& cam = rec.cameras.at(cams[k].cam);
        const sfm::Pose& pose = cams[k].pose;
        const Vec3 C = sfm::cameraCenter(pose);
        const Mat3 Rt = sfm::transpose(pose.R);
        std::vector<uint8_t> img((size_t)cam.width * cam.height * 3, 0);
        std::vector<uint8_t> keep((size_t)cam.width * cam.height, 0);
        for (int y = 0; y < cam.height; y++)
            for (int x = 0; x < cam.width; x++) {
                double acc[3] = {0, 0, 0};
                int hits = 0;
                for (int s = 0; s < 4; s++) {
                    const Vec3 b = cam.bearing({x + 0.25 + 0.5 * (s & 1), y + 0.25 + 0.5 * (s >> 1)});
                    const Vec3 d = sfm::mul(Rt, b).normalized();
                    int q = -1;
                    const double t = sc.hit(C, d, &q);
                    if (!std::isfinite(t)) continue;
                    hits++;
                    uint8_t c[3];
                    sc.albedo(C + d * t, q, c);
                    for (int i = 0; i < 3; i++) acc[i] += c[i];
                }
                for (int i = 0; i < 3; i++)
                    img[((size_t)y * cam.width + x) * 3 + i] = (uint8_t)std::lround(acc[i] / 4);
                keep[(size_t)y * cam.width + x] = hits == 4 ? 255 : 0;
            }
        const std::string path = (root / "images" / cams[k].name).string();
        if (!stbi_write_png(path.c_str(), cam.width, cam.height, 3, img.data(), cam.width * 3))
            throw std::runtime_error("cannot write " + path);
        const std::string mpath = (root / "masks" / cams[k].name).string();
        if (!stbi_write_png(mpath.c_str(), cam.width, cam.height, 1, keep.data(), cam.width))
            throw std::runtime_error("cannot write " + mpath);
        sfm::Image im;
        im.id = (uint32_t)k + 1;
        im.camera_id = cams[k].cam;
        im.name = cams[k].name;
        im.pose = pose;
        im.registered = true;
        rec.images[im.id] = im;
    }

    std::mt19937_64 rng(11);
    std::vector<double> area;
    double total = 0;
    for (const Quad& q : sc.quads) { area.push_back(q.u.cross(q.v).norm()); total += area.back(); }
    std::discrete_distribution<int> pick(area.begin(), area.end());
    std::uniform_real_distribution<double> uni(0, 1);
    int made = 0, tries = 0;
    while (made < sparse_points && tries++ < sparse_points * 50) {
        const Quad& q = sc.quads[(size_t)pick(rng)];
        const Vec3 X = q.o + q.u * uni(rng) + q.v * uni(rng);
        std::vector<sfm::TrackElement> track;
        for (auto& kv : rec.images) {
            Vec2 px;
            if (!sees(sc, rec.cameras.at(kv.second.camera_id), kv.second.pose, X, &px)) continue;
            kv.second.points2D.push_back(px);
            kv.second.point3D_ids.push_back(sfm::kInvalidPoint3D);
            track.push_back({kv.first, (uint32_t)kv.second.points2D.size() - 1});
        }
        if (track.size() < 2) {
            for (const sfm::TrackElement& e : track) {
                rec.images[e.image_id].points2D.pop_back();
                rec.images[e.image_id].point3D_ids.pop_back();
            }
            continue;
        }
        rec.addPoint3D(X, track);
        made++;
    }
    rec.writeBinary((root / "sparse" / "0").string());
}

OracleMatcher::OracleMatcher(const Scene* scene, std::vector<View> views, int size,
                             double noise_px, double outliers, uint64_t seed)
    : scene_(scene), size_(size), noise_(noise_px), outliers_(outliers), seed_(seed) {
    for (View& v : views) views_[v.name] = std::move(v);
}

std::vector<View> OracleMatcher::independentViews(const std::vector<SourceImage>& images) {
    std::vector<View> out;
    const double* axes = camhost::equirect_face_axes();
    for (size_t i = 0; i < images.size(); i++) {
        const SourceImage& s = images[i];
        const std::string stem = fs::path(s.name).stem().string();
        const Vec3 C = sfm::mul(sfm::transpose(s.pose.R), s.pose.t) * -1.0;
        if (!s.cam.isSpherical()) {
            View v;
            v.name = stem;
            v.image = (int)i;
            v.cam = s.cam;
            v.R = s.pose.R;
            v.t = s.pose.t;
            v.centre = C;
            out.push_back(v);
            continue;
        }
        const int n = (int)std::lround(s.cam.width / 4.0);
        for (int f = 0; f < 6; f++) {
            View v;
            v.name = stem + "_" + kFaceNames[f];
            v.image = (int)i;
            v.cam.model = sfm::CamModel::Pinhole;
            v.cam.width = v.cam.height = n;
            v.cam.fx = v.cam.fy = v.cam.cx = v.cam.cy = n / 2.0;
            // Row r of the face table is the face's camera axis r in the panorama frame.
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) {
                    double acc = 0;
                    for (int k = 0; k < 3; k++) acc += axes[9 * f + 3 * r + k] * s.pose.R[(size_t)(3 * k + c)];
                    v.R[(size_t)(3 * r + c)] = acc;
                }
            v.t = {0, 0, 0};
            for (int r = 0; r < 3; r++) {
                double acc = 0;
                for (int k = 0; k < 3; k++)
                    acc += axes[9 * f + 3 * r + k] * (k == 0 ? s.pose.t.x : k == 1 ? s.pose.t.y : s.pose.t.z);
                (r == 0 ? v.t.x : r == 1 ? v.t.y : v.t.z) = acc;
            }
            v.centre = C;
            out.push_back(v);
        }
    }
    return out;
}

Warp OracleMatcher::match(const MatchImage& a, const MatchImage& b) {
    const auto ia = views_.find(a.name), ib = views_.find(b.name);
    if (ia == views_.end() || ib == views_.end())
        throw std::runtime_error("oracle: unknown view " + a.name + " / " + b.name);
    const View& A = ia->second;
    const View& B = ib->second;
    const int S = size_;
    Warp w;
    w.width = w.height = S;
    w.warp.assign((size_t)S * S * 2, 0.0f);
    w.certainty.assign((size_t)S * S, 0.0f);
    std::mt19937_64 rng(seed_ ^ std::hash<std::string>()(a.name + "|" + b.name));
    std::normal_distribution<double> gauss(0.0, noise_);
    std::uniform_real_distribution<double> uni(-1.0, 1.0), coin(0.0, 1.0);
    const Mat3 RtA = sfm::transpose(A.R);
    const Vec3 CB = B.centre;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            const size_t i = (size_t)y * S + x;
            const Vec3 bA = A.cam.bearing({(x + 0.5) * A.cam.width / S, (y + 0.5) * A.cam.height / S});
            const Vec3 d = sfm::mul(RtA, bA).normalized();
            const double t = scene_->hit(A.centre, d);
            if (!std::isfinite(t)) continue;
            const Vec3 X = A.centre + d * t;
            const Vec3 Xc = sfm::mul(B.R, X) + B.t;
            if (!B.cam.isSpherical() && !(Xc.z > 1e-6)) continue;
            const Vec2 px = B.cam.project(Xc);
            if (!(px.x >= 0 && px.x < B.cam.width && px.y >= 0 && px.y < B.cam.height)) continue;
            const double dist = (X - CB).norm();
            if (scene_->hit(CB, (X - CB) * (1.0 / dist)) < dist * (1 - 1e-7) - 1e-7) continue;
            double u = 2.0 * px.x / B.cam.width - 1.0, v = 2.0 * px.y / B.cam.height - 1.0;
            if (noise_ > 0) { u += gauss(rng) * 2.0 / S; v += gauss(rng) * 2.0 / S; }
            if (outliers_ > 0 && coin(rng) < outliers_) { u = uni(rng); v = uni(rng); }
            w.warp[2 * i] = (float)u;
            w.warp[2 * i + 1] = (float)v;
            w.certainty[i] = 1.0f;
        }
    return w;
}

CloudScore scoreCloud(const Scene& sc, const std::vector<DensePoint>& cloud, double tol,
                      double beyond_tol, double step, double cover) {
    CloudScore s;
    s.points = (int64_t)cloud.size();
    int64_t within = 0;
    struct H { size_t operator()(const std::array<int64_t, 3>& k) const {
        return (size_t)(k[0] * 73856093LL) ^ (size_t)(k[1] * 19349663LL) ^ (size_t)(k[2] * 83492791LL); } };
    std::unordered_map<std::array<int64_t, 3>, std::vector<Vec3>, H> grid;
    auto key = [&](const Vec3& p) {
        return std::array<int64_t, 3>{(int64_t)std::floor(p.x / cover), (int64_t)std::floor(p.y / cover),
                                      (int64_t)std::floor(p.z / cover)};
    };
    for (const DensePoint& p : cloud) {
        const double d = sc.distance(p.xyz);
        within += d <= tol;
        s.beyond += d > beyond_tol;
        grid[key(p.xyz)].push_back(p.xyz);
    }
    s.within = cloud.empty() ? 0 : (double)within / (double)cloud.size();
    int64_t samples = 0, covered = 0;
    for (const Quad& q : sc.quads) {
        if (!q.riser) continue;
        const int na = std::max(1, (int)std::round(q.u.norm() / step));
        const int nb = std::max(1, (int)std::round(q.v.norm() / step));
        for (int a = 0; a < na; a++)
            for (int b = 0; b < nb; b++) {
                const Vec3 p = q.o + q.u * ((a + 0.5) / na) + q.v * ((b + 0.5) / nb);
                samples++;
                const auto k = key(p);
                bool hit = false;
                for (int dx = -1; dx <= 1 && !hit; dx++)
                    for (int dy = -1; dy <= 1 && !hit; dy++)
                        for (int dz = -1; dz <= 1 && !hit; dz++) {
                            auto it = grid.find({k[0] + dx, k[1] + dy, k[2] + dz});
                            if (it == grid.end()) continue;
                            for (const Vec3& c : it->second)
                                if ((c - p).norm() <= cover) { hit = true; break; }
                        }
                covered += hit;
            }
    }
    s.riser_cover = samples ? (double)covered / (double)samples : 0;
    return s;
}

}  // namespace roma
