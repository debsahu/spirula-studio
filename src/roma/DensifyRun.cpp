#include "roma/DensifyRun.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#include "core/ImageFile.h"
#include "external/stb_image.h"
#include "external/stb_image_write.h"
#include "roma/Sample.h"
#include "sfm/core/FixedPoses.h"
#include "sfm/core/Model.h"

namespace roma {

namespace fs = std::filesystem;
using sfm::Mat3;
using sfm::Vec2;
using sfm::Vec3;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

template <class F>
void parallelFor(int n, F&& fn) {
    const int T = std::max(1, std::min(n, (int)std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 0; t < T; t++)
        pool.emplace_back([&, t] {
            for (int i = t; i < n; i += T) fn(i);
        });
    for (std::thread& th : pool) th.join();
}

// Median distance from a sparse point to its nearest neighbour, over up to
// 20000 of them, by a uniform grid sized for ~2 points a cell.
double medianSpacing(const sfm::Reconstruction& rec) {
    std::vector<Vec3> p;
    p.reserve(rec.points3D.size());
    for (const auto& kv : rec.points3D) p.push_back(kv.second.xyz);
    if (p.size() < 2) return 0;
    std::vector<double> xs, ys, zs;
    for (const Vec3& v : p) { xs.push_back(v.x); ys.push_back(v.y); zs.push_back(v.z); }
    auto pct = [](std::vector<double> v, double q) {
        std::nth_element(v.begin(), v.begin() + (long)(q * (double)(v.size() - 1)), v.end());
        return v[(size_t)(q * (double)(v.size() - 1))];
    };
    const double ex = pct(xs, 0.98) - pct(xs, 0.02), ey = pct(ys, 0.98) - pct(ys, 0.02),
                 ez = pct(zs, 0.98) - pct(zs, 0.02);
    const double vol = std::max(1e-12, std::max(ex, 1e-6) * std::max(ey, 1e-6) * std::max(ez, 1e-6));
    const double cell = std::cbrt(vol * 2.0 / (double)p.size());
    struct H { size_t operator()(const std::array<int64_t, 3>& k) const {
        return (size_t)(k[0] * 73856093LL) ^ (size_t)(k[1] * 19349663LL) ^ (size_t)(k[2] * 83492791LL); } };
    std::unordered_map<std::array<int64_t, 3>, std::vector<int>, H> grid;
    auto key = [&](const Vec3& v) {
        return std::array<int64_t, 3>{(int64_t)std::floor(v.x / cell), (int64_t)std::floor(v.y / cell),
                                      (int64_t)std::floor(v.z / cell)};
    };
    for (size_t i = 0; i < p.size(); i++) grid[key(p[i])].push_back((int)i);
    std::mt19937_64 rng(1);
    std::vector<size_t> pick(p.size());
    std::iota(pick.begin(), pick.end(), 0);
    std::shuffle(pick.begin(), pick.end(), rng);
    pick.resize(std::min<size_t>(pick.size(), 20000));
    std::vector<double> d;
    for (size_t i : pick) {
        const auto k = key(p[i]);
        double best = INFINITY;
        for (int r = 1; r <= 3 && !std::isfinite(best); r++)
            for (int a = -r; a <= r; a++)
                for (int b = -r; b <= r; b++)
                    for (int c = -r; c <= r; c++) {
                        auto it = grid.find({k[0] + a, k[1] + b, k[2] + c});
                        if (it == grid.end()) continue;
                        for (int j : it->second)
                            if ((size_t)j != i) best = std::min(best, (p[(size_t)j] - p[i]).norm());
                    }
        if (std::isfinite(best)) d.push_back(best);
    }
    if (d.empty()) return 0;
    std::nth_element(d.begin(), d.begin() + (long)(d.size() / 2), d.end());
    return d[d.size() / 2];
}

std::string stemOf(const std::string& name) {
    std::string s = fs::path(name).filename().string();
    const size_t dot = s.rfind('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

}  // namespace

// ===========================================================================
// Images
// ===========================================================================

ImageData loadImage(const std::string& path, const std::string& mask_path, bool flip_mask) {
    ImageData out;
    std::vector<uint8_t> alpha;
    if (imagefile::handles(path)) {
        imagefile::Info info;
        imagefile::Options o;
        o.channels = 3;
        const std::string err = imagefile::decode_srgb8(path, o, info, out.rgb);
        if (!err.empty()) throw std::runtime_error("cannot decode " + path + ": " + err);
        out.width = info.width;
        out.height = info.height;
    } else {
        int w = 0, h = 0, c = 0;
        if (!stbi_info(path.c_str(), &w, &h, &c))
            throw std::runtime_error("cannot read " + path + ": " + stbi_failure_reason());
        const int want = c == 4 || c == 2 ? 4 : 3;
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &c, want);
        if (!px) throw std::runtime_error("cannot decode " + path + ": " + stbi_failure_reason());
        const size_t n = (size_t)w * h;
        out.width = w;
        out.height = h;
        out.rgb.resize(n * 3);
        if (want == 4) {
            alpha.resize(n);
            for (size_t i = 0; i < n; i++) {
                std::memcpy(&out.rgb[i * 3], px + i * 4, 3);
                alpha[i] = px[i * 4 + 3];
            }
        } else {
            std::memcpy(out.rgb.data(), px, n * 3);
        }
        stbi_image_free(px);
    }
    const size_t n = (size_t)out.width * out.height;
    if (!mask_path.empty()) {
        int w = 0, h = 0, c = 0;
        unsigned char* m = stbi_load(mask_path.c_str(), &w, &h, &c, 1);
        if (!m) throw std::runtime_error("cannot decode mask " + mask_path);
        out.keep.resize(n);
        // Nearest onto the image grid; the plugin's threshold is value / 255 > 0.5.
        for (int y = 0; y < out.height; y++) {
            const int sy = std::min(h - 1, (int)(((double)y + 0.5) * h / out.height));
            for (int x = 0; x < out.width; x++) {
                const int sx = std::min(w - 1, (int)(((double)x + 0.5) * w / out.width));
                const bool k = m[(size_t)sy * w + sx] > 127;
                out.keep[(size_t)y * out.width + x] = (uint8_t)(k != flip_mask);
            }
        }
        stbi_image_free(m);
    } else if (!alpha.empty()) {
        out.keep.resize(n);
        for (size_t i = 0; i < n; i++) out.keep[i] = (uint8_t)((alpha[i] > 0) != flip_mask);
    }
    return out;
}

std::array<float, 3> sampleRgb(const ImageData& img, double x, double y) {
    // Continuous coordinates -> array index space.
    const double fx = x - 0.5, fy = y - 0.5;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const double ax = fx - x0, ay = fy - y0;
    auto at = [&](int xi, int yi, int c) -> double {
        xi = std::clamp(xi, 0, img.width - 1);
        yi = std::clamp(yi, 0, img.height - 1);
        return img.rgb[((size_t)yi * img.width + xi) * 3 + c];
    };
    std::array<float, 3> out{};
    for (int c = 0; c < 3; c++) {
        const double v = (1 - ay) * ((1 - ax) * at(x0, y0, c) + ax * at(x0 + 1, y0, c)) +
                         ay * ((1 - ax) * at(x0, y0 + 1, c) + ax * at(x0 + 1, y0 + 1, c));
        out[(size_t)c] = (float)(v / 255.0);
    }
    return out;
}

Vec2 viewToSource(const SourceImage& src, const View& v, double x, double y) {
    if (v.face < 0) return {x, y};
    const Vec3 bf = v.cam.bearing({x, y});
    return src.cam.project(sfm::mul(sfm::transpose(v.face_R), bf));
}

void cutView(const ImageData& img, const SourceImage& src, const View& v, int size,
             std::vector<uint8_t>& rgb, std::vector<uint8_t>& keep) {
    const size_t n = (size_t)size * size;
    rgb.assign(n * 3, 0);
    keep.clear();
    if (!img.keep.empty()) keep.assign(n, 0);
    const double sx = (double)v.cam.width / size, sy = (double)v.cam.height / size;
    // Source pixels per output pixel, which sets the supersampling.
    const double density = v.face < 0 ? std::max((double)img.width / size, (double)img.height / size)
                                      : (double)img.width / 4.0 / size;
    const int ss = std::clamp((int)std::ceil(density), 1, 8);
    parallelFor(size, [&](int y) {
        for (int x = 0; x < size; x++) {
            double acc[3] = {0, 0, 0};
            for (int a = 0; a < ss; a++)
                for (int b = 0; b < ss; b++) {
                    const double px = (x + (b + 0.5) / ss) * sx, py = (y + (a + 0.5) / ss) * sy;
                    const Vec2 s = viewToSource(src, v, px, py);
                    const std::array<float, 3> c = sampleRgb(img, s.x, s.y);
                    for (int k = 0; k < 3; k++) acc[k] += c[(size_t)k];
                }
            const size_t o = (size_t)y * size + x;
            for (int k = 0; k < 3; k++)
                rgb[o * 3 + k] = (uint8_t)std::lround(std::clamp(acc[k] / (ss * ss), 0.0, 1.0) * 255.0);
            if (!keep.empty()) {
                const Vec2 s = viewToSource(src, v, (x + 0.5) * sx, (y + 0.5) * sy);
                const int ix = std::clamp((int)std::floor(s.x), 0, img.width - 1);
                const int iy = std::clamp((int)std::floor(s.y), 0, img.height - 1);
                keep[o] = img.keep[(size_t)iy * img.width + ix];
                if (!keep[o]) rgb[o * 3] = rgb[o * 3 + 1] = rgb[o * 3 + 2] = 0;
            }
        }
    });
}

// ===========================================================================
// Plan
// ===========================================================================

DensifyPlan planDensify(const DensifyJob& job) {
    DensifyPlan pl;
    pl.opt = job.opt;
    DensifyOptions& o = pl.opt;
    const sfm::Reconstruction rec = sfm::Reconstruction::readBinary(job.model_dir);
    pl.images = sourceImages(rec);
    std::sort(pl.images.begin(), pl.images.end(),
              [](const SourceImage& a, const SourceImage& b) { return a.name < b.name; });
    const int N = (int)pl.images.size();
    if (N < 2) throw std::runtime_error("the model has fewer than two registered images");
    pl.sparse_points = (int64_t)rec.points3D.size();
    pl.sparse_spacing = medianSpacing(rec);
    {
        std::ifstream g(fs::path(job.model_dir) / "gauge.txt");
        std::string k, v;
        while (g >> k >> v)
            if (k == "metric") pl.metric = v == "1";
    }

    // Plugin defaults where the default mode resolves something itself.
    if (o.plugin_exact) {
        if (o.min_track <= 0) o.min_track = 1;
        if (o.voxel == 0) o.voxel = -1;
        if (o.max_points == 0) o.max_points = -1;
        if (o.matches_per_ref == 0) o.matches_per_ref = 10000;
    }

    bool any_equi = false;
    for (const SourceImage& s : pl.images) any_equi = any_equi || s.cam.isSpherical();
    pl.split = o.split < 0 ? any_equi : o.split == 1;
    if (pl.split && !any_equi) throw std::runtime_error("--split yes needs an equirectangular model");
    pl.match_size = job.matcher ? job.matcher->inputSize() : 640;
    for (int i = 0; i < N; i++) {
        const SourceImage& s = pl.images[(size_t)i];
        const bool split = pl.split && s.cam.isSpherical();
        const int face = (int)std::lround(s.cam.width / 4.0);
        if (split) pl.face_size = std::max(pl.face_size, face);
        std::vector<int> ids;
        for (View& v : imageViews(s, i, split, face)) {
            ids.push_back((int)pl.views.size());
            pl.views.push_back(std::move(v));
        }
        pl.views_of.push_back(ids);
    }

    std::vector<int> allowed;
    for (int i = 0; i < N; i++) {
        if (o.holdout_every > 0 && i % o.holdout_every == 0) pl.held_out.push_back(i);
        else allowed.push_back(i);
    }
    const int k_refs = std::max(1, (int)(o.refs <= 1.0 ? std::lround(o.refs * (double)allowed.size())
                                                       : std::lround(o.refs)));
    pl.refs = rec.points3D.empty() ? refsKCenters(pl.images, allowed, k_refs)
                                   : refsByVisibility(pl.images, allowed, k_refs);
    const int k = std::max(1, std::min(o.neighbours, (int)allowed.size() - 1));

    auto choose = [&](int r, double max_baseline) {
        std::vector<int> n;
        if (o.neighbour_rule == NeighbourRule::Covis && !rec.points3D.empty())
            n = neighboursByCovis(pl.images, rec, allowed, r, k, o.min_parallax_deg, max_baseline);
        if ((int)n.size() < k)
            for (int q : neighboursByPose(pl.images, allowed, r, (int)allowed.size()))
                if ((int)n.size() < k && std::find(n.begin(), n.end(), q) == n.end() &&
                    (max_baseline <= 0 ||
                     (pl.images[(size_t)q].centre - pl.images[(size_t)r].centre).norm() <= max_baseline))
                    n.push_back(q);
        return n;
    };
    for (int r : pl.refs) pl.nbrs.push_back(choose(r, 0));
    pl.max_baseline = o.max_baseline > 0 ? o.max_baseline : 0;
    if (o.max_baseline == 0 && pl.metric) {
        std::vector<double> d;
        for (size_t i = 0; i < pl.refs.size(); i++)
            for (int q : pl.nbrs[i])
                d.push_back((pl.images[(size_t)q].centre - pl.images[(size_t)pl.refs[i]].centre).norm());
        if (!d.empty()) {
            std::nth_element(d.begin(), d.begin() + (long)(d.size() / 2), d.end());
            pl.max_baseline = 3.0 * d[d.size() / 2];
        }
    }
    if (pl.max_baseline > 0)
        for (size_t i = 0; i < pl.refs.size(); i++) pl.nbrs[i] = choose(pl.refs[i], pl.max_baseline);

    for (size_t i = 0; i < pl.refs.size(); i++) {
        std::vector<int> nv;
        for (int q : pl.nbrs[i])
            for (int v : pl.views_of[(size_t)q]) nv.push_back(v);
        for (int a : pl.views_of[(size_t)pl.refs[i]]) {
            DensifyPlan::RefView rv;
            rv.view = a;
            rv.nbr_views = pairFaces(pl.views, a, nv, o.face_pair_deg, o.all_face_pairs);
            if (rv.nbr_views.empty()) continue;
            pl.pairs += (int64_t)rv.nbr_views.size();
            pl.ref_views.push_back(std::move(rv));
        }
    }

    pl.min_track = o.min_track > 0 ? o.min_track : (k >= 3 ? 3 : 2);
    pl.voxel = o.voxel > 0 ? o.voxel : o.voxel < 0 ? 0 : 0.5 * pl.sparse_spacing;
    pl.max_points = o.max_points > 0 ? o.max_points : o.max_points < 0 ? 0
                    : std::clamp<int64_t>(4 * pl.sparse_points, 1000000, 8000000);
    if (o.matches_per_ref <= 0) {
        const int64_t per = pl.max_points / std::max<int64_t>(1, (int64_t)pl.ref_views.size());
        o.matches_per_ref = (int)std::clamp<int64_t>(per, 2000, 50000);
    }

    // Mask polarity, from up to eight masks spread over the capture.
    if (job.mask_path) {
        double keep = 0;
        for (int s = 0; s < std::min(N, 8); s++) {
            const SourceImage& img = pl.images[(size_t)(s * N / std::min(N, 8))];
            const std::string mp = job.mask_path(img.name);
            if (mp.empty()) continue;
            int w = 0, h = 0, c = 0;
            unsigned char* m = stbi_load(mp.c_str(), &w, &h, &c, 1);
            if (!m) continue;
            int64_t kept = 0;
            for (int64_t i = 0; i < (int64_t)w * h; i++) kept += (m[i] > 127) != job.flip_mask;
            stbi_image_free(m);
            keep += (double)kept / ((double)w * h);
            pl.masks_sampled++;
        }
        if (pl.masks_sampled) pl.mask_keep = keep / pl.masks_sampled;
    }
    return pl;
}

// ===========================================================================
// Run
// ===========================================================================

namespace {

struct CutImage {
    std::vector<std::vector<uint8_t>> rgb, keep;   // per view of the image
};

class ViewCache {
public:
    ViewCache(const DensifyJob& job, const DensifyPlan& pl, size_t max_images)
        : job_(job), pl_(pl), max_(max_images) {}

    ImageData load(int image) const {
        const SourceImage& s = pl_.images[(size_t)image];
        return loadImage(job_.image_path(s.name),
                         job_.mask_path ? job_.mask_path(s.name) : std::string(), job_.flip_mask);
    }

    // The current reference at full resolution, for colour; one at a time.
    const ImageData& full(int image) {
        if (full_id_ != image) {
            full_ = load(image);
            full_id_ = image;
        }
        return full_;
    }

    const CutImage& cut(int image) {
        auto it = map_.find(image);
        if (it != map_.end()) {
            order_.splice(order_.begin(), order_, it->second.second);
            return it->second.first;
        }
        ImageData other;
        if (image != full_id_) other = load(image);
        const ImageData& img = image == full_id_ ? full_ : other;
        CutImage c;
        for (int v : pl_.views_of[(size_t)image]) {
            std::vector<uint8_t> rgb, keep;
            cutView(img, pl_.images[(size_t)image], pl_.views[(size_t)v], pl_.match_size, rgb, keep);
            c.rgb.push_back(std::move(rgb));
            c.keep.push_back(std::move(keep));
        }
        order_.push_front(image);
        auto res = map_.emplace(image, std::make_pair(std::move(c), order_.begin()));
        while (map_.size() > max_) {
            map_.erase(order_.back());
            order_.pop_back();
        }
        return res.first->second.first;
    }

    int slot(int view) const {
        const auto& ids = pl_.views_of[(size_t)pl_.views[(size_t)view].image];
        return (int)(std::find(ids.begin(), ids.end(), view) - ids.begin());
    }

private:
    const DensifyJob& job_;
    const DensifyPlan& pl_;
    size_t max_;
    std::map<int, std::pair<CutImage, std::list<int>::iterator>> map_;
    std::list<int> order_;
    ImageData full_;
    int full_id_ = -1;
};

std::vector<uint8_t> resizeKeep(const std::vector<uint8_t>& keep, int from, int to) {
    if (keep.empty() || from == to) return keep;
    std::vector<uint8_t> out((size_t)to * to);
    for (int y = 0; y < to; y++)
        for (int x = 0; x < to; x++)
            out[(size_t)y * to + x] =
                keep[(size_t)std::min(from - 1, (int)((y + 0.5) * from / to)) * from +
                     (size_t)std::min(from - 1, (int)((x + 0.5) * from / to))];
    return out;
}

}  // namespace

DensifyResult runDensify(const DensifyJob& job, const DensifyPlan& pl,
                         const std::function<void(int, int, int64_t)>& progress) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    DensifyResult res;
    const DensifyOptions& o = pl.opt;
    ViewCache cache(job, pl, 48);

    if (!job.export_dir.empty()) {
        fs::create_directories(fs::path(job.export_dir) / "views");
        std::ofstream pairs(fs::path(job.export_dir) / "pairs.txt", std::ios::trunc);
        std::vector<char> written(pl.views.size(), 0);
        auto put = [&](int v) {
            if (written[(size_t)v]) return;
            const CutImage& c = cache.cut(pl.views[(size_t)v].image);
            const std::string p =
                (fs::path(job.export_dir) / "views" / (pl.views[(size_t)v].name + ".png")).string();
            if (!stbi_write_png(p.c_str(), pl.match_size, pl.match_size, 3,
                                c.rgb[(size_t)cache.slot(v)].data(), pl.match_size * 3))
                throw std::runtime_error("cannot write " + p);
            written[(size_t)v] = 1;
        };
        int done = 0;
        for (const auto& rv : pl.ref_views) {
            put(rv.view);
            for (int b : rv.nbr_views) {
                put(b);
                pairs << pl.views[(size_t)rv.view].name << ' ' << pl.views[(size_t)b].name << '\n';
            }
            if (progress) progress(++done, (int)pl.ref_views.size(), 0);
        }
        res.seconds_total = std::chrono::duration<double>(clock::now() - t0).count();
        return res;
    }
    if (!job.matcher) throw std::runtime_error("no matcher");

    std::vector<DensePoint> all;
    double match_s = 0;
    int done = 0;
    for (const auto& rv : pl.ref_views) {
        const View& A = pl.views[(size_t)rv.view];
        if (!o.plugin_exact) cache.full(A.image);
        const CutImage& ca = cache.cut(A.image);
        const int sa = cache.slot(rv.view);
        const std::vector<uint8_t> rgbA = ca.rgb[(size_t)sa], keepA = ca.keep[(size_t)sa];
        const SourceImage& srcA = pl.images[(size_t)A.image];
        RefMatches m;
        m.ref = rv.view;
        MatchImage ma{A.name, pl.match_size, pl.match_size, rgbA.data()};
        for (int b : rv.nbr_views) {
            const View& B = pl.views[(size_t)b];
            const CutImage& cb = cache.cut(B.image);
            const int sb = cache.slot(b);
            const std::vector<uint8_t> rgbB = cb.rgb[(size_t)sb], keepB = cb.keep[(size_t)sb];
            const MatchImage mb{B.name, pl.match_size, pl.match_size, rgbB.data()};
            const auto tm = clock::now();
            Warp w = job.matcher->match(ma, mb);
            match_s += std::chrono::duration<double>(clock::now() - tm).count();
            if (m.w == 0) { m.w = w.width; m.h = w.height; }
            if (w.width != m.w || w.height != m.h)
                throw std::runtime_error("matcher returned differently sized warps for " + A.name);
            const std::vector<uint8_t> ka = resizeKeep(keepA, pl.match_size, m.w);
            const std::vector<uint8_t> kb = resizeKeep(keepB, pl.match_size, m.w);
            m.cert.push_back(collectCertainty(w, ka, kb, o));
            m.warp.push_back(std::move(w.warp));
            m.nbrs.push_back(b);
        }
        if (m.nbrs.empty()) continue;
        if (o.plugin_exact) {
            // The plugin's colour reads the match-resolution, masked reference.
            m.rgb_match = rgbA;
            if (m.w != pl.match_size) m.rgb_match.assign((size_t)m.w * m.h * 3, 0);
        } else {
            const ImageData& full = cache.full(A.image);
            m.colour_at = [&full, &srcA, &A](double x, double y) {
                const Vec2 s = viewToSource(srcA, A, x, y);
                return sampleRgb(full, s.x, s.y);
            };
        }
        std::vector<float> best((size_t)m.w * m.h, 0.0f);
        for (const auto& c : m.cert)
            for (size_t i = 0; i < best.size(); i++) best[i] = std::max(best[i], c[i]);
        if (!o.plugin_exact)
            for (float& v : best)
                if (v < o.min_certainty) v = 0;
        SampleOptions so;
        so.count = o.matches_per_ref;
        so.cap = o.sample_cap;
        so.no_filter = o.no_filter;
        so.seed = o.seed * 1000003ull + (uint64_t)rv.view;
        const std::vector<int64_t> samples = sampleWithCoverage(best, m.w, m.h, so);
        std::vector<DensePoint> pts = triangulateRef(m, pl.views, samples, o, res.stats);
        for (DensePoint& p : pts) all.push_back(std::move(p));
        if (progress) progress(++done, (int)pl.ref_views.size(), (int64_t)all.size());
    }
    res.cloud = finalizePoints(std::move(all), pl.min_track, pl.voxel, pl.max_points, o, res.stats);
    res.points = (int64_t)res.cloud.size();
    res.seconds_match = match_s;
    res.seconds_total = std::chrono::duration<double>(clock::now() - t0).count();
    return res;
}

// ===========================================================================
// Output
// ===========================================================================

std::string siblingDir(const std::string& model_dir) {
    const fs::path m = fs::path(model_dir).lexically_normal();
    const fs::path base = m.filename().empty() ? m.parent_path() : m;
    return (base.parent_path() / (base.filename().string() + "-roma")).string();
}

void writeSibling(const std::string& model_dir, const std::string& out_dir,
                  const DensifyPlan& pl, const std::vector<DensePoint>& cloud,
                  const std::string& settings_json) {
    const fs::path src(model_dir), out(out_dir);
    if (fs::weakly_canonical(src) == fs::weakly_canonical(out))
        throw std::runtime_error("the output model would replace its source");
    const sfm::FixedPoses fp = sfm::readFixedPoses(model_dir);
    const std::string images_bin = slurp(src / "images.bin");
    const fs::path tmp = out.string() + ".partial";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);
    try {
        auto put = [&](const char* name, const std::string& bytes) {
            std::ofstream f(tmp / name, std::ios::binary | std::ios::trunc);
            f.write(bytes.data(), (std::streamsize)bytes.size());
            if (!f) throw std::runtime_error(std::string("cannot write ") + name);
        };
        put("cameras.bin", fp.cameras_bin);
        put("images.bin", images_bin);
        for (const char* extra : {"gauge.txt", "rigs.txt", "rigs.bin", "frames.bin"})
            if (fs::exists(src / extra)) put(extra, slurp(src / extra));
        {
            using sfm::detail::wr;
            std::ofstream f(tmp / "points3D.bin", std::ios::binary | std::ios::trunc);
            std::ofstream t(tmp / "points3D_tracks.bin", std::ios::binary | std::ios::trunc);
            wr<uint64_t>(f, cloud.size());
            t.write("RTK1", 4);
            wr<uint64_t>(t, cloud.size());
            for (size_t i = 0; i < cloud.size(); i++) {
                const DensePoint& p = cloud[i];
                wr<uint64_t>(f, i + 1);
                for (double v : {p.xyz.x, p.xyz.y, p.xyz.z}) wr<double>(f, v);
                for (int c = 0; c < 3; c++)
                    wr<uint8_t>(f, (uint8_t)std::lround(std::clamp(p.rgb[c], 0.0f, 1.0f) * 255.0f));
                wr<double>(f, p.error);
                wr<uint64_t>(f, 0);
                wr<uint64_t>(t, i + 1);
                wr<uint32_t>(t, (uint32_t)p.track.size());
                for (const Observation& ob : p.track) {
                    const View& v = pl.views[(size_t)ob.view];
                    const SourceImage& s = pl.images[(size_t)v.image];
                    const Vec2 q = viewToSource(s, v, ob.x, ob.y);
                    wr<uint32_t>(t, s.id);
                    wr<float>(t, (float)q.x);
                    wr<float>(t, (float)q.y);
                }
            }
            if (!f || !t) throw std::runtime_error("cannot write points3D.bin");
        }
        put("densify.json", settings_json);
        const std::string bad = sfm::checkFixedModel(tmp.string(), fp);
        if (!bad.empty()) throw std::runtime_error("the copied model differs from its source: " + bad);
        if (slurp(tmp / "images.bin") != images_bin || slurp(tmp / "cameras.bin") != slurp(src / "cameras.bin"))
            throw std::runtime_error("the copied model differs from its source: not byte-identical");
        fs::remove_all(out, ec);
        fs::rename(tmp, out);
    } catch (...) {
        fs::remove_all(tmp, ec);
        throw;
    }
}

}  // namespace roma
