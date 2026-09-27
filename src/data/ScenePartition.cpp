// ScenePartition.cpp -- see ScenePartition.h and docs/notes/scene-partition.md.

#include "data/ScenePartition.h"

#include "data/CameraMath.h"
#include "data/Json.h"
#include "data/JsonWrite.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace spirula {

namespace {

std::string leaf_of(const std::string& path) { return fs::path(path).filename().string(); }

// Generic paths with the separators the two sides may disagree on unified.
std::string slashed(const std::string& path) {
    std::string s = path;
    for (char& c : s)
        if (c == '\\') c = '/';
    return s;
}

bool ends_with_name(const std::string& path, const std::string& name) {
    if (path.size() < name.size()) return false;
    if (path.compare(path.size() - name.size(), name.size(), name) != 0) return false;
    return path.size() == name.size() || path[path.size() - name.size() - 1] == '/';
}

// Frames by the tail of their path: a name from a model file is `a/b/c.jpg`
// relative to some folder, and the frame whose absolute path ends in it is
// the one -- a rig has the same leaf under several folders.
class FrameIndex {
public:
    explicit FrameIndex(const std::vector<std::string>& paths) : _paths(paths.size()) {
        for (size_t i = 0; i < paths.size(); i++) {
            _paths[i] = slashed(paths[i]);
            _by_leaf[leaf_of(_paths[i])].push_back((int32_t)i);
        }
    }
    int32_t find(const std::string& name) const {
        const std::string n = slashed(name);
        auto it = _by_leaf.find(leaf_of(n));
        if (it == _by_leaf.end()) return -1;
        for (int32_t i : it->second)
            if (ends_with_name(_paths[(size_t)i], n)) return i;
        return -1;
    }
    // The shortest tail of each path that names it alone among all of them.
    std::vector<std::string> unique_tails() const {
        std::vector<std::string> out(_paths.size());
        for (const auto& kv : _by_leaf) {
            for (int32_t i : kv.second) {
                const std::vector<std::string> parts = split(_paths[(size_t)i]);
                std::string tail;
                for (size_t k = parts.size(); k-- > 0;) {
                    tail = tail.empty() ? parts[k] : parts[k] + "/" + tail;
                    int hits = 0;
                    for (int32_t j : kv.second) hits += ends_with_name(_paths[(size_t)j], tail);
                    if (hits == 1) break;
                }
                out[(size_t)i] = tail;
            }
        }
        return out;
    }

private:
    static std::vector<std::string> split(const std::string& p) {
        std::vector<std::string> out;
        size_t at = 0;
        while (at <= p.size()) {
            const size_t next = p.find('/', at);
            const std::string piece = p.substr(at, next == std::string::npos ? std::string::npos : next - at);
            if (!piece.empty()) out.push_back(piece);
            if (next == std::string::npos) break;
            at = next + 1;
        }
        return out;
    }
    std::vector<std::string> _paths;
    std::unordered_map<std::string, std::vector<int32_t>> _by_leaf;
};

void say(const PartitionLog& log, const std::string& s) {
    if (log) log(s);
}

template <typename T>
void keep_rows(std::vector<T>& v, int64_t n, int stride, const uint8_t* keep) {
    if (v.empty()) return;
    int64_t w = 0;
    for (int64_t i = 0; i < n; i++) {
        if (!keep[i]) continue;
        if (w != i)
            for (int k = 0; k < stride; k++) v[(size_t)(w * stride + k)] = std::move(v[(size_t)(i * stride + k)]);
        w++;
    }
    v.resize((size_t)(w * stride));
}

// Camera-to-world of frame i as CV world-to-camera: ray = R_cv (p - t).
struct FrameView {
    double R[9];   // rows: the CV camera axes in world
    double t[3];
    camhost::Camera cam;
    double cos_max = -2.0;   // cone pre-test for pinhole; -2 disables
};

FrameView frame_view(const ParsedDataset& ds, int64_t i) {
    FrameView f;
    const float* M = &ds.c2w[(size_t)i * 12];
    // OpenGL columns X, Y, Z (camera looks down -Z); CV is (x, -y, -z).
    for (int k = 0; k < 3; k++) {
        f.R[0 + k] = M[k * 4 + 0];
        f.R[3 + k] = -M[k * 4 + 1];
        f.R[6 + k] = -M[k * 4 + 2];
        f.t[k] = M[k * 4 + 3];
    }
    f.cam.model = ds.camera_models.empty() ? 0 : ds.camera_models[(size_t)i];
    f.cam.tier = ds.camera_distortions.empty() ? 0 : ds.camera_distortions[(size_t)i];
    f.cam.width = ds.widths[(size_t)i];
    f.cam.height = ds.heights[(size_t)i];
    f.cam.fx = ds.intrins[(size_t)i * 4 + 0];
    f.cam.fy = ds.intrins[(size_t)i * 4 + 1];
    f.cam.cx = ds.intrins[(size_t)i * 4 + 2];
    f.cam.cy = ds.intrins[(size_t)i * 4 + 3];
    if (!ds.dist_coeffs.empty())
        for (int k = 0; k < 8; k++) f.cam.dist[k] = ds.dist_coeffs[(size_t)i * 8 + k];
    if (f.cam.model == 0 && f.cam.fx > 0 && f.cam.fy > 0) {
        // The widest normalized coordinate any corner reaches, with slack for
        // lens distortion pulling the border in or out.
        double r2 = 0;
        for (int c = 0; c < 4; c++) {
            const double u = ((c & 1 ? f.cam.width : 0) - f.cam.cx) / f.cam.fx;
            const double v = ((c & 2 ? f.cam.height : 0) - f.cam.cy) / f.cam.fy;
            r2 = std::max(r2, u * u + v * v);
        }
        const double r = std::sqrt(r2) * 1.15;
        f.cos_max = 1.0 / std::sqrt(1.0 + r * r);
    }
    return f;
}

bool frame_sees(const FrameView& f, const double p[3]) {
    const double d[3] = {p[0] - f.t[0], p[1] - f.t[1], p[2] - f.t[2]};
    double ray[3];
    for (int r = 0; r < 3; r++)
        ray[r] = f.R[r * 3] * d[0] + f.R[r * 3 + 1] * d[1] + f.R[r * 3 + 2] * d[2];
    const double len = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
    if (!(len > 1e-12)) return false;
    if (f.cos_max > -1.5 && ray[2] / len < f.cos_max) return false;
    double px[2];
    return camhost::ray_in_frame(f.cam, ray, px);
}

// Pair weights from who sees what: every pair of frames sharing a point gets
// one. Long tracks are strided so one point seen everywhere does not cost a
// quadratic number of pairs.
graph::WeightedGraph camera_graph_of(const std::vector<int64_t>& beg,
                                     const std::vector<int32_t>& frame, size_t n_frames) {
    graph::EdgeAccumulator acc;
    acc.reserve(frame.size() * 4);
    constexpr int64_t kMaxTrack = 48;
    for (size_t i = 0; i + 1 < beg.size(); i++) {
        const int64_t lo = beg[i], hi = beg[i + 1];
        const int64_t m = hi - lo;
        if (m < 2) continue;
        const int64_t step = m > kMaxTrack ? (m + kMaxTrack - 1) / kMaxTrack : 1;
        for (int64_t a = lo; a < hi; a += step)
            for (int64_t b = a + step; b < hi; b += step)
                acc.add((uint32_t)frame[(size_t)a], (uint32_t)frame[(size_t)b], 1.0);
    }
    return acc.build(n_frames);
}

}  // namespace

// ===========================================================================
// Names
// ===========================================================================

const char* covisibility_source_name(CovisibilitySource s) {
    switch (s) {
        case CovisibilitySource::Auto: return "auto";
        case CovisibilitySource::Tracks: return "tracks";
        case CovisibilitySource::Projection: return "projection";
        case CovisibilitySource::Proximity: return "proximity";
    }
    return "auto";
}

bool covisibility_source_from_name(const std::string& s, CovisibilitySource& out) {
    for (CovisibilitySource c : {CovisibilitySource::Auto, CovisibilitySource::Tracks,
                                 CovisibilitySource::Projection, CovisibilitySource::Proximity})
        if (s == covisibility_source_name(c)) { out = c; return true; }
    return false;
}

// ===========================================================================
// Covisibility
// ===========================================================================

namespace {

bool covisibility_from_tracks(const ParsedDataset& ds, const SparseStats& st, Covisibility& out,
                              const PartitionLog& log) {
    if (st.empty() || (int64_t)st.track_beg.size() - 1 != ds.points.num()) return false;
    const FrameIndex index(ds.image_filenames);
    std::vector<int32_t> image_frame(st.image_names.size(), -1);
    size_t matched = 0;
    for (size_t i = 0; i < st.image_names.size(); i++) {
        image_frame[i] = index.find(st.image_names[i]);
        matched += image_frame[i] >= 0;
    }
    if (matched == 0) return false;
    out.beg.assign(1, 0);
    out.frame.clear();
    out.frame.reserve(st.track_image.size());
    for (size_t i = 0; i + 1 < st.track_beg.size(); i++) {
        for (int64_t k = st.track_beg[i]; k < st.track_beg[i + 1]; k++) {
            const int32_t f = image_frame[(size_t)st.track_image[(size_t)k]];
            if (f >= 0) out.frame.push_back(f);
        }
        out.beg.push_back((int64_t)out.frame.size());
    }
    out.cameras = camera_graph_of(out.beg, out.frame, (size_t)ds.num_cameras);
    out.source = CovisibilitySource::Tracks;
    (void)log;
    return true;
}

bool covisibility_from_projection(const ParsedDataset& ds, const PartitionOptions& opt,
                                  Covisibility& out) {
    const int64_t n_pts = ds.points.num();
    const int64_t n_cam = ds.num_cameras;
    if (n_pts == 0 || n_cam == 0) return false;
    const int64_t stride = std::max<int64_t>(1, n_pts / std::max(1, opt.max_projected_points));
    std::vector<int64_t> sample;
    for (int64_t i = 0; i < n_pts; i += stride) sample.push_back(i);
    std::vector<FrameView> views((size_t)n_cam);
    for (int64_t i = 0; i < n_cam; i++) views[(size_t)i] = frame_view(ds, i);

    // Per frame, which sampled points it sees; then inverted to per point.
    std::vector<std::vector<int32_t>> seen((size_t)n_cam);
#pragma omp parallel for schedule(dynamic, 4)
    for (int64_t c = 0; c < n_cam; c++) {
        std::vector<int32_t>& s = seen[(size_t)c];
        for (size_t k = 0; k < sample.size(); k++)
            if (frame_sees(views[(size_t)c], &ds.points.xyz[(size_t)sample[k] * 3]))
                s.push_back((int32_t)k);
    }
    std::vector<int64_t> count(sample.size() + 1, 0);
    for (int64_t c = 0; c < n_cam; c++)
        for (int32_t k : seen[(size_t)c]) count[(size_t)k + 1]++;
    for (size_t i = 1; i < count.size(); i++) count[i] += count[i - 1];
    std::vector<int32_t> frame((size_t)count.back());
    {
        std::vector<int64_t> fill(count.begin(), count.end() - 1);
        for (int64_t c = 0; c < n_cam; c++)
            for (int32_t k : seen[(size_t)c]) frame[(size_t)fill[(size_t)k]++] = (int32_t)c;
    }
    // Spread back over every point: the unsampled ones get their sample's
    // observers, so the tables stay one row per seed point.
    out.beg.assign(1, 0);
    out.frame.clear();
    out.frame.reserve(frame.size() * (size_t)stride);
    for (int64_t i = 0; i < n_pts; i++) {
        const size_t k = (size_t)(i / stride);
        for (int64_t j = count[k]; j < count[k + 1]; j++) out.frame.push_back(frame[(size_t)j]);
        out.beg.push_back((int64_t)out.frame.size());
    }
    // The graph from the sample alone; the copies would only scale it.
    out.cameras = camera_graph_of(count, frame, (size_t)n_cam);
    out.source = CovisibilitySource::Projection;
    return out.cameras.adj.size() > 0;
}

void covisibility_from_proximity(const ParsedDataset& ds, const PartitionOptions& opt,
                                 Covisibility& out) {
    const int64_t n = ds.num_cameras;
    const int k = std::max(1, opt.proximity_neighbours);
    std::vector<double> c((size_t)n * 3), dir((size_t)n * 3);
    for (int64_t i = 0; i < n; i++) {
        const float* M = &ds.c2w[(size_t)i * 12];
        for (int r = 0; r < 3; r++) {
            c[(size_t)i * 3 + r] = M[r * 4 + 3];
            dir[(size_t)i * 3 + r] = -M[r * 4 + 2];
        }
    }
    std::vector<std::vector<std::pair<double, int32_t>>> nearest((size_t)n);
#pragma omp parallel for schedule(dynamic, 16)
    for (int64_t i = 0; i < n; i++) {
        std::vector<std::pair<double, int32_t>>& best = nearest[(size_t)i];
        for (int64_t j = 0; j < n; j++) {
            if (j == i) continue;
            double d2 = 0;
            for (int r = 0; r < 3; r++) {
                const double d = c[(size_t)i * 3 + r] - c[(size_t)j * 3 + r];
                d2 += d * d;
            }
            if ((int)best.size() < k) {
                best.emplace_back(d2, (int32_t)j);
                std::push_heap(best.begin(), best.end());
            } else if (d2 < best.front().first) {
                std::pop_heap(best.begin(), best.end());
                best.back() = {d2, (int32_t)j};
                std::push_heap(best.begin(), best.end());
            }
        }
    }
    graph::EdgeAccumulator acc;
    for (int64_t i = 0; i < n; i++)
        for (const auto& [d2, j] : nearest[(size_t)i]) {
            double cosang = 0;
            for (int r = 0; r < 3; r++) cosang += dir[(size_t)i * 3 + r] * dir[(size_t)j * 3 + r];
            // Facing the same way counts for more than standing side by side.
            acc.add((uint32_t)i, (uint32_t)j, 1.0 + std::max(0.0, cosang));
        }
    out.cameras = acc.build((size_t)n);
    out.beg.assign((size_t)ds.points.num() + 1, 0);
    out.frame.clear();
    out.source = CovisibilitySource::Proximity;
}

}  // namespace

Covisibility build_covisibility(const ParsedDataset& ds, const SparseStats* tracks,
                                const PartitionOptions& opt, const PartitionLog& log) {
    Covisibility out;
    if (ds.num_cameras == 0) return out;
    const CovisibilitySource want = opt.source;
    if ((want == CovisibilitySource::Auto || want == CovisibilitySource::Tracks) && tracks &&
        covisibility_from_tracks(ds, *tracks, out, log))
        return out;
    if (want == CovisibilitySource::Tracks)
        say(log, "no usable tracks; falling back to projection");
    if ((want != CovisibilitySource::Proximity) && covisibility_from_projection(ds, opt, out))
        return out;
    if (want == CovisibilitySource::Projection)
        say(log, "no seed points to project; falling back to camera proximity");
    covisibility_from_proximity(ds, opt, out);
    return out;
}

// ===========================================================================
// Partition
// ===========================================================================

std::vector<int32_t> ScenePartition::frames_of(int part) const {
    std::vector<int32_t> out;
    if (part < 0 || part >= num_parts) return out;
    for (size_t i = 0; i < frame_label.size(); i++)
        if (frame_label[i] == part) out.push_back((int32_t)i);
    out.insert(out.end(), ring[(size_t)part].begin(), ring[(size_t)part].end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int64_t ScenePartition::core_count(int part) const {
    int64_t n = 0;
    for (int32_t l : frame_label) n += l == part;
    return n;
}

ScenePartition partition_scene(const ParsedDataset& ds, const Covisibility& cov,
                               const PartitionOptions& opt, const PartitionLog& log) {
    const int64_t n_cam = ds.num_cameras;
    if (n_cam == 0) throw std::runtime_error("partition: the dataset has no cameras");
    ScenePartition p;
    p.options = opt;
    p.source = cov.source;
    p.frame_names = FrameIndex(ds.image_filenames).unique_tails();

    // ---- the cut ----
    graph::LabelCutOptions co;
    co.parts = opt.parts > 0 ? (size_t)opt.parts : 0;
    co.leaf_max = (double)std::max(1, opt.max_images);
    co.min_part = 2;
    co.min_final = (size_t)std::max<int64_t>(2, n_cam / 50);
    p.frame_label = graph::cut_labels(cov.cameras, co);

    // A camera that shares nothing with anyone is a part of its own after the
    // cut; it joins the part of the nearest camera that is in a real one.
    {
        int n_parts = 0;
        for (int32_t l : p.frame_label) n_parts = std::max(n_parts, l + 1);
        std::vector<int64_t> size((size_t)n_parts, 0);
        for (int32_t l : p.frame_label) size[(size_t)l]++;
        std::vector<char> tiny((size_t)n_parts, 0);
        int n_real = 0;
        for (int k = 0; k < n_parts; k++) {
            tiny[(size_t)k] = size[(size_t)k] < (int64_t)co.min_final;
            n_real += !tiny[(size_t)k];
        }
        if (n_real > 0 && n_real < n_parts) {
            for (int64_t i = 0; i < n_cam; i++) {
                if (!tiny[(size_t)p.frame_label[(size_t)i]]) continue;
                const float* a = &ds.c2w[(size_t)i * 12];
                double best = 1e300;
                int32_t pick = -1;
                for (int64_t j = 0; j < n_cam; j++) {
                    if (tiny[(size_t)p.frame_label[(size_t)j]]) continue;
                    const float* b = &ds.c2w[(size_t)j * 12];
                    double d2 = 0;
                    for (int r = 0; r < 3; r++) {
                        const double d = (double)a[r * 4 + 3] - b[r * 4 + 3];
                        d2 += d * d;
                    }
                    if (d2 < best) { best = d2; pick = p.frame_label[(size_t)j]; }
                }
                if (pick >= 0) p.frame_label[(size_t)i] = pick;
            }
        }
        // Dense labels, largest part first.
        std::vector<int64_t> count((size_t)n_parts, 0);
        for (int32_t l : p.frame_label) count[(size_t)l]++;
        std::vector<int> order((size_t)n_parts);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return count[(size_t)a] > count[(size_t)b]; });
        std::vector<int32_t> remap((size_t)n_parts, -1);
        int next = 0;
        for (int k : order)
            if (count[(size_t)k] > 0) remap[(size_t)k] = next++;
        for (int32_t& l : p.frame_label) l = remap[(size_t)l];
        p.num_parts = next;
    }
    if (p.num_parts > 254) throw std::runtime_error("partition: more than 254 parts");

    p.core_pieces.assign((size_t)p.num_parts, 0);
    for (int k = 0; k < p.num_parts; k++) {
        std::vector<uint32_t> nodes;
        for (int64_t i = 0; i < n_cam; i++)
            if (p.frame_label[(size_t)i] == k) nodes.push_back((uint32_t)i);
        p.core_pieces[(size_t)k] = (int32_t)graph::connected_components(cov.cameras, nodes).size();
    }

    // ---- how much the cut severed ----
    {
        double total = 0, cut = 0;
        const graph::WeightedGraph& g = cov.cameras;
        for (uint32_t i = 0; i < g.n(); i++)
            for (uint32_t k = g.offs[i]; k < g.offs[i + 1]; k++) {
                total += g.w[k];
                if (p.frame_label[i] != p.frame_label[g.adj[k]]) cut += g.w[k];
            }
        p.cut_fraction = total > 0 ? cut / total : 0.0;
    }

    // ---- point ownership: the part most of a point's observers are in ----
    const int64_t n_pts = ds.points.num();
    p.point_label.assign((size_t)n_pts, -1);
    std::vector<double> part_centroid((size_t)p.num_parts * 3, 0.0);
    {
        std::vector<int64_t> n((size_t)p.num_parts, 0);
        for (int64_t i = 0; i < n_cam; i++) {
            const int32_t l = p.frame_label[(size_t)i];
            for (int r = 0; r < 3; r++) part_centroid[(size_t)l * 3 + r] += ds.c2w[(size_t)i * 12 + r * 4 + 3];
            n[(size_t)l]++;
        }
        for (int k = 0; k < p.num_parts; k++)
            for (int r = 0; r < 3; r++) part_centroid[(size_t)k * 3 + r] /= std::max<int64_t>(1, n[(size_t)k]);
    }
    if (cov.has_tracks() && cov.num_points() == n_pts) {
#pragma omp parallel
        {
            std::vector<int32_t> votes((size_t)p.num_parts, 0);
#pragma omp for schedule(static)
            for (int64_t i = 0; i < n_pts; i++) {
                const int64_t lo = cov.beg[(size_t)i], hi = cov.beg[(size_t)i + 1];
                if (lo == hi) continue;
                for (int64_t k = lo; k < hi; k++) votes[(size_t)p.frame_label[(size_t)cov.frame[(size_t)k]]]++;
                int best = -1;
                int32_t best_n = 0;
                const double* q = &ds.points.xyz[(size_t)i * 3];
                for (int64_t k = lo; k < hi; k++) {
                    const int l = p.frame_label[(size_t)cov.frame[(size_t)k]];
                    if (votes[(size_t)l] == 0) continue;
                    bool take = votes[(size_t)l] > best_n;
                    if (votes[(size_t)l] == best_n) {
                        // A tie goes to the part whose cameras are nearer.
                        double da = 0, db = 0;
                        for (int r = 0; r < 3; r++) {
                            const double x = q[r] - part_centroid[(size_t)l * 3 + r];
                            const double y = q[r] - part_centroid[(size_t)best * 3 + r];
                            da += x * x;
                            db += y * y;
                        }
                        take = da < db;
                    }
                    if (take) { best_n = votes[(size_t)l]; best = l; }
                    votes[(size_t)l] = 0;
                }
                p.point_label[(size_t)i] = best;
            }
        }
    } else {
        // No tracks: a point belongs with the nearest camera's part.
#pragma omp parallel for schedule(static)
        for (int64_t i = 0; i < n_pts; i++) {
            const double* q = &ds.points.xyz[(size_t)i * 3];
            double best = 1e300;
            int32_t pick = -1;
            for (int64_t j = 0; j < n_cam; j++) {
                const float* b = &ds.c2w[(size_t)j * 12];
                double d2 = 0;
                for (int r = 0; r < 3; r++) {
                    const double d = q[r] - b[r * 4 + 3];
                    d2 += d * d;
                }
                if (d2 < best) { best = d2; pick = p.frame_label[(size_t)j]; }
            }
            p.point_label[(size_t)i] = pick;
        }
    }

    // ---- the owned field: points with the side they were seen from, and
    // the cameras, which see all around ----
    p.frame_centers.resize((size_t)n_cam * 3);
    for (int64_t i = 0; i < n_cam; i++)
        for (int r = 0; r < 3; r++) p.frame_centers[(size_t)i * 3 + r] = ds.c2w[(size_t)i * 12 + r * 4 + 3];
    {
        const int64_t stride = std::max<int64_t>(1, (n_pts + std::max(1, opt.max_seeds) - 1) /
                                                        std::max(1, opt.max_seeds));
        std::vector<float> xyz, dirs;
        std::vector<int32_t> lab;
        const bool tracked = cov.has_tracks() && cov.num_points() == n_pts;
        for (int64_t i = 0; i < n_pts; i += stride) {
            if (p.point_label[(size_t)i] < 0) continue;
            const double* q = &ds.points.xyz[(size_t)i * 3];
            float d[3] = {0, 0, 0};
            if (tracked) {
                for (int64_t k = cov.beg[(size_t)i]; k < cov.beg[(size_t)i + 1]; k++) {
                    const float* c = &p.frame_centers[(size_t)cov.frame[(size_t)k] * 3];
                    float v[3] = {(float)(c[0] - q[0]), (float)(c[1] - q[1]), (float)(c[2] - q[2])};
                    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    if (len > 1e-9f)
                        for (int r = 0; r < 3; r++) d[r] += v[r] / len;
                }
            }
            for (int r = 0; r < 3; r++) xyz.push_back((float)q[r]);
            dirs.insert(dirs.end(), d, d + 3);
            lab.push_back(p.point_label[(size_t)i]);
        }
        for (int64_t i = 0; i < n_cam; i++) {
            for (int r = 0; r < 3; r++) xyz.push_back(p.frame_centers[(size_t)i * 3 + r]);
            dirs.insert(dirs.end(), {0.f, 0.f, 0.f});
            lab.push_back(p.frame_label[(size_t)i]);
        }
        p.field = std::make_shared<LabelField>(
            LabelField::build(xyz.data(), lab.data(), dirs.data(), (int64_t)lab.size()));
        for (int64_t i = 0; i < n_pts; i++)
            if (p.point_label[(size_t)i] < 0)
                p.point_label[(size_t)i] = p.field->label(&ds.points.xyz[(size_t)i * 3]);
    }

    // ---- rings: outside cameras that see enough of a part ----
    p.ring.assign((size_t)p.num_parts, {});
    if (cov.has_tracks() && cov.num_points() == n_pts) {
        std::vector<std::vector<int64_t>> seen_per_part((size_t)n_cam,
                                                        std::vector<int64_t>((size_t)p.num_parts, 0));
        std::vector<int64_t> seen_total((size_t)n_cam, 0);
        for (int64_t i = 0; i < n_pts; i++) {
            const int32_t owner = p.point_label[(size_t)i];
            for (int64_t k = cov.beg[(size_t)i]; k < cov.beg[(size_t)i + 1]; k++) {
                const int32_t f = cov.frame[(size_t)k];
                seen_per_part[(size_t)f][(size_t)owner]++;
                seen_total[(size_t)f]++;
            }
        }
        for (int64_t c = 0; c < n_cam; c++) {
            if (seen_total[(size_t)c] == 0) continue;
            for (int k = 0; k < p.num_parts; k++) {
                if (k == p.frame_label[(size_t)c]) continue;
                const int64_t n = seen_per_part[(size_t)c][(size_t)k];
                if (n >= opt.ring_min_points &&
                    (double)n >= opt.ring_fraction * (double)seen_total[(size_t)c])
                    p.ring[(size_t)k].push_back((int32_t)c);
            }
        }
    } else {
        // Proximity only: a camera joins the ring of every part it has an
        // edge into worth a share of its own connectivity.
        const graph::WeightedGraph& g = cov.cameras;
        for (uint32_t c = 0; c < g.n(); c++) {
            std::vector<double> into((size_t)p.num_parts, 0.0);
            double total = 0;
            for (uint32_t k = g.offs[c]; k < g.offs[c + 1]; k++) {
                into[(size_t)p.frame_label[g.adj[k]]] += g.w[k];
                total += g.w[k];
            }
            for (int k = 0; k < p.num_parts; k++)
                if (k != p.frame_label[c] && total > 0 && into[(size_t)k] >= opt.ring_fraction * total)
                    p.ring[(size_t)k].push_back((int32_t)c);
        }
    }

    // ---- what each part seeds from ----
    p.part_points.assign((size_t)p.num_parts, {});
    {
        std::vector<int32_t> part_of_frame((size_t)n_cam, -1);
        std::vector<std::vector<uint8_t>> in_part((size_t)p.num_parts,
                                                  std::vector<uint8_t>((size_t)n_cam, 0));
        for (int k = 0; k < p.num_parts; k++)
            for (int32_t f : p.frames_of(k)) in_part[(size_t)k][(size_t)f] = 1;
        for (int k = 0; k < p.num_parts; k++) {
            std::vector<uint8_t> take((size_t)n_pts, 0);
            for (int64_t i = 0; i < n_pts; i++) {
                if (p.point_label[(size_t)i] == k) { take[(size_t)i] = 1; continue; }
                if (!cov.has_tracks() || cov.num_points() != n_pts) continue;
                for (int64_t t = cov.beg[(size_t)i]; t < cov.beg[(size_t)i + 1]; t++)
                    if (in_part[(size_t)k][(size_t)cov.frame[(size_t)t]]) { take[(size_t)i] = 1; break; }
            }
            for (int64_t i = 0; i < n_pts; i++)
                if (take[(size_t)i]) p.part_points[(size_t)k].push_back(i);
        }
    }

    if (log) {
        char buf[256];
        std::snprintf(buf, sizeof buf, "partition: %d parts from %lld cameras (%s), cut %.1f%%",
                      p.num_parts, (long long)n_cam, covisibility_source_name(p.source),
                      100.0 * p.cut_fraction);
        log(buf);
        for (int k = 0; k < p.num_parts; k++) {
            std::snprintf(buf, sizeof buf, "  part %d: core %lld, ring %lld, points %lld%s", k,
                          (long long)p.core_count(k), (long long)p.ring[(size_t)k].size(),
                          (long long)p.part_points[(size_t)k].size(),
                          p.core_pieces[(size_t)k] > 1 ? " (not one piece)" : "");
            log(buf);
        }
    }
    return p;
}

// ===========================================================================
// Files
// ===========================================================================

namespace {

constexpr char kBinMagic[4] = {'S', 'S', 'P', 'T'};

template <typename T>
void put(std::string& out, const T& v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof v);
}
template <typename T>
bool get(const std::string& s, size_t& at, T& v) {
    if (at + sizeof v > s.size()) return false;
    std::memcpy(&v, s.data() + at, sizeof v);
    at += sizeof v;
    return true;
}

std::string read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot read " + path);
    std::string bytes;
    char buf[1 << 16];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, got);
    std::fclose(f);
    return bytes;
}

void write_file(const std::string& path, const std::string& bytes) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write " + path);
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

}  // namespace

void write_partition(const ScenePartition& p, const std::string& json_path,
                     const std::string& dataset) {
    const fs::path jp(json_path);
    const std::string bin_name = jp.stem().string() + ".bin";
    std::error_code ec;
    if (!jp.parent_path().empty()) fs::create_directories(jp.parent_path(), ec);

    std::string bin;
    bin.append(kBinMagic, 4);
    put(bin, (uint32_t)2);
    if (p.field) p.field->write(bin);
    else LabelField().write(bin);
    put(bin, (uint64_t)p.frame_centers.size() / 3);
    bin.append(reinterpret_cast<const char*>(p.frame_centers.data()),
               p.frame_centers.size() * sizeof(float));
    put(bin, (uint64_t)p.point_label.size());
    for (int32_t l : p.point_label) put(bin, (uint8_t)(l < 0 ? LabelField::kNone : l));
    put(bin, (uint32_t)p.num_parts);
    for (int k = 0; k < p.num_parts; k++) {
        put(bin, (uint64_t)p.part_points[(size_t)k].size());
        for (int64_t i : p.part_points[(size_t)k]) put(bin, (uint32_t)i);
    }
    write_file((jp.parent_path() / bin_name).string(), bin);

    JsonWriter w;
    w.object();
    w.field("format", "spirula-scene-partition");
    w.field("version", 1);
    w.field("dataset", dataset);
    w.field("source", covisibility_source_name(p.source));
    w.key("options").object();
    w.field("parts", p.options.parts);
    w.field("max_images", p.options.max_images);
    w.field("ring_fraction", p.options.ring_fraction);
    w.field("ring_min_points", p.options.ring_min_points);
    w.field("max_seeds", p.options.max_seeds);
    w.field("source", covisibility_source_name(p.options.source));
    w.field("max_projected_points", p.options.max_projected_points);
    w.field("proximity_neighbours", p.options.proximity_neighbours);
    w.end();
    w.field("num_parts", p.num_parts);
    w.field("num_frames", (long long)p.frame_names.size());
    w.field("num_points", (long long)p.point_label.size());
    w.field("cut_fraction", p.cut_fraction);
    w.field("binary", bin_name);
    w.key("frame_names").array();
    for (const std::string& s : p.frame_names) w.value(s);
    w.end();
    w.key("frame_parts").array();
    for (int32_t l : p.frame_label) w.value(l);
    w.end();
    w.key("parts").array();
    for (int k = 0; k < p.num_parts; k++) {
        w.object();
        w.field("core", (long long)p.core_count(k));
        w.field("points", (long long)p.part_points[(size_t)k].size());
        w.key("ring").array();
        for (int32_t f : p.ring[(size_t)k]) w.value(f);
        w.end();
        w.end();
    }
    w.end();
    w.end();
    write_file(json_path, w.str());
}

ScenePartition read_partition(const std::string& json_path, std::string* dataset) {
    const JsonValue root = json_parse_file(json_path);
    if (!root.is_object() || !root.find("format") ||
        root.find("format")->as_string() != "spirula-scene-partition")
        throw std::runtime_error(json_path + " is not a partition file");
    ScenePartition p;
    if (dataset) *dataset = root.find("dataset") ? root.find("dataset")->as_string() : "";
    if (const JsonValue* s = root.find("source"))
        covisibility_source_from_name(s->as_string(), p.source);
    if (const JsonValue* o = root.find("options"); o && o->is_object()) {
        p.options.parts = (int)o->find("parts")->as_int(0);
        p.options.max_images = (int)o->get_double("max_images", p.options.max_images);
        p.options.ring_fraction = (float)o->get_double("ring_fraction", p.options.ring_fraction);
        p.options.ring_min_points = (int)o->get_double("ring_min_points", p.options.ring_min_points);
        p.options.max_seeds = (int)o->get_double("max_seeds", p.options.max_seeds);
        if (const JsonValue* s = o->find("source"))
            covisibility_source_from_name(s->as_string(), p.options.source);
        p.options.max_projected_points =
            (int)o->get_double("max_projected_points", p.options.max_projected_points);
        p.options.proximity_neighbours =
            (int)o->get_double("proximity_neighbours", p.options.proximity_neighbours);
    }
    p.num_parts = (int)root.find("num_parts")->as_int(0);
    p.cut_fraction = root.get_double("cut_fraction", 0.0);
    if (const JsonValue* a = root.find("frame_names"); a && a->is_array())
        for (const JsonValue& v : a->arr) p.frame_names.push_back(v.as_string());
    if (const JsonValue* a = root.find("frame_parts"); a && a->is_array())
        for (const JsonValue& v : a->arr) p.frame_label.push_back((int32_t)v.as_int(-1));
    if (p.frame_label.size() != p.frame_names.size())
        throw std::runtime_error(json_path + ": frame_names and frame_parts differ in length");
    p.ring.assign((size_t)p.num_parts, {});
    if (const JsonValue* a = root.find("parts"); a && a->is_array()) {
        if ((int)a->arr.size() != p.num_parts)
            throw std::runtime_error(json_path + ": parts[] does not match num_parts");
        for (size_t k = 0; k < a->arr.size(); k++)
            if (const JsonValue* r = a->arr[k].find("ring"); r && r->is_array())
                for (const JsonValue& v : r->arr) p.ring[k].push_back((int32_t)v.as_int(-1));
    }

    const std::string bin_name = root.find("binary") ? root.find("binary")->as_string()
                                                     : fs::path(json_path).stem().string() + ".bin";
    const std::string bin = read_file((fs::path(json_path).parent_path() / bin_name).string());
    size_t at = 0;
    if (bin.size() < 8 || std::memcmp(bin.data(), kBinMagic, 4) != 0)
        throw std::runtime_error(bin_name + " is not a partition table");
    at = 4;
    uint32_t version = 0;
    if (!get(bin, at, version) || version != 2)
        throw std::runtime_error(bin_name + ": unsupported version; compute the partition again");
    size_t used = 0;
    p.field = std::make_shared<LabelField>();
    if (!LabelField::read(bin.data() + at, bin.size() - at, used, *p.field))
        throw std::runtime_error(bin_name + ": bad label field");
    at += used;
    uint64_t n_centers = 0;
    if (!get(bin, at, n_centers) || at + n_centers * 12 > bin.size())
        throw std::runtime_error(bin_name + ": truncated camera centres");
    p.frame_centers.resize((size_t)n_centers * 3);
    std::memcpy(p.frame_centers.data(), bin.data() + at, (size_t)n_centers * 12);
    at += n_centers * 12;
    uint64_t n_pts = 0;
    if (!get(bin, at, n_pts) || at + n_pts > bin.size())
        throw std::runtime_error(bin_name + ": truncated point labels");
    p.point_label.resize((size_t)n_pts);
    for (uint64_t i = 0; i < n_pts; i++) {
        const uint8_t l = (uint8_t)bin[at + i];
        p.point_label[(size_t)i] = l == LabelField::kNone ? -1 : l;
    }
    at += n_pts;
    uint32_t n_parts = 0;
    if (!get(bin, at, n_parts) || (int)n_parts != p.num_parts)
        throw std::runtime_error(bin_name + ": part count differs from the json");
    p.part_points.assign((size_t)n_parts, {});
    for (uint32_t k = 0; k < n_parts; k++) {
        uint64_t n = 0;
        if (!get(bin, at, n) || at + n * 4 > bin.size())
            throw std::runtime_error(bin_name + ": truncated part points");
        p.part_points[k].resize((size_t)n);
        for (uint64_t i = 0; i < n; i++) {
            uint32_t v;
            std::memcpy(&v, bin.data() + at + i * 4, 4);
            p.part_points[k][(size_t)i] = v;
        }
        at += n * 4;
    }
    return p;
}

// ===========================================================================
// Applying one part to a parsed dataset
// ===========================================================================

bool apply_partition(ParsedDataset& ds, const ScenePartition& p, int part,
                     PartitionApplied& out) {
    if (part < 0 || part >= p.num_parts) return false;
    out = PartitionApplied{};
    out.frames_before = ds.num_cameras;
    out.points_before = ds.points.num();

    const FrameIndex index(ds.image_filenames);
    const int64_t n = ds.num_cameras;
    std::vector<uint8_t> keep((size_t)n, 0);
    for (int32_t f : p.frames_of(part)) {
        const int32_t i = index.find(p.frame_names[(size_t)f]);
        if (i < 0 || keep[(size_t)i]) {
            out.missing++;
            continue;
        }
        keep[(size_t)i] = 1;
        if (p.frame_label[(size_t)f] == part) out.core++;
        else out.ring++;
    }

    keep_rows(ds.camera_models, n, 1, keep.data());
    keep_rows(ds.camera_distortions, n, 1, keep.data());
    keep_rows(ds.image_filenames, n, 1, keep.data());
    keep_rows(ds.mask_filenames, n, 1, keep.data());
    keep_rows(ds.depth_filenames, n, 1, keep.data());
    keep_rows(ds.normal_filenames, n, 1, keep.data());
    keep_rows(ds.widths, n, 1, keep.data());
    keep_rows(ds.heights, n, 1, keep.data());
    keep_rows(ds.c2w, n, 12, keep.data());
    keep_rows(ds.intrins, n, 4, keep.data());
    keep_rows(ds.dist_coeffs, n, 8, keep.data());
    keep_rows(ds.redistort, n, 1, keep.data());
    keep_rows(ds.exif_quarter_turns, n, 1, keep.data());
    std::vector<int32_t> new_index((size_t)n, -1);
    int32_t w = 0;
    for (int64_t i = 0; i < n; i++)
        if (keep[(size_t)i]) new_index[(size_t)i] = w++;
    auto remap = [&](std::vector<int32_t>& v) {
        std::vector<int32_t> o;
        for (int32_t i : v)
            if (i >= 0 && i < n && new_index[(size_t)i] >= 0) o.push_back(new_index[(size_t)i]);
        v.swap(o);
    };
    remap(ds.train_indices);
    remap(ds.val_indices);
    ds.num_cameras = w;
    out.frames_after = w;

    const int64_t n_pts = ds.points.num();
    if (n_pts > 0 && (int64_t)p.point_label.size() == n_pts) {
        std::vector<uint8_t> pk((size_t)n_pts, 0);
        for (int64_t i : p.part_points[(size_t)part])
            if (i >= 0 && i < n_pts) pk[(size_t)i] = 1;
        keep_rows(ds.points.xyz, n_pts, 3, pk.data());
        keep_rows(ds.points.rgb, n_pts, 3, pk.data());
    }
    out.points_after = ds.points.num();
    return true;
}

// ===========================================================================
// Colours
// ===========================================================================

void part_color(int part, float rgb[3]) {
    // Golden-angle hues at full saturation, alternating value so neighbours in
    // index differ in more than hue.
    const double h = std::fmod(0.11 + part * 0.6180339887498949, 1.0) * 6.0;
    const double v = (part % 3 == 1) ? 0.72 : ((part % 3 == 2) ? 0.88 : 1.0);
    const double s = (part % 2) ? 0.85 : 0.65;
    const int i = (int)std::floor(h);
    const double f = h - i;
    const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double r, g, b;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    rgb[0] = (float)r;
    rgb[1] = (float)g;
    rgb[2] = (float)b;
}

}  // namespace spirula
