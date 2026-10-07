// Gate P-4: the host stage in plugin_exact mode against the Lichtfeld
// plugin's own, fed the same RoMa warps, masks and sample indices.
//
//   roma_plugin_parity_test <fixture>
//
// The fixture is reference/python/roma_plugin_parity.py's output (plus the
// .txt forms of its JSON that the same script's --text step writes). Exit 77
// without one.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "external/npy.hpp"
#include "roma/Densify.h"
#include "roma/DumpMatcher.h"
#include "sfm/tests/TestMain.h"

namespace fs = std::filesystem;
using namespace roma;

namespace {

template <class T>
std::vector<T> npy(const fs::path& p) {
    return npy::read_npy<T>(p.string()).data;
}

struct Key {
    long long x, y;
    bool operator<(const Key& o) const { return x != o.x ? x < o.x : y < o.y; }
};
Key keyOf(double x, double y) { return {std::llround(x * 64), std::llround(y * 64)}; }

int body(int argc, char** argv) {
    if (argc < 2 || !fs::exists(fs::path(argv[1]) / "views.txt")) {
        std::printf("skip: no fixture (roma_plugin_parity_test <fixture>)\n");
        return 77;
    }
    const fs::path fx(argv[1]);
    std::map<std::string, int> index;
    std::vector<View> views;
    {
        std::ifstream f(fx / "views.txt");
        std::string name;
        int w, h;
        double k[4], R[9], t[3];
        while (f >> name >> w >> h >> k[0] >> k[1] >> k[2] >> k[3]) {
            for (double& v : R) f >> v;
            for (double& v : t) f >> v;
            View v;
            v.name = name;
            v.image = (int)views.size();
            v.cam = sfm::Camera::defaultFor(0, w, h, k[0], sfm::CamModel::Pinhole);
            v.cam.fx = k[0]; v.cam.fy = k[1]; v.cam.cx = k[2]; v.cam.cy = k[3];
            for (int i = 0; i < 9; i++) v.R[(size_t)i] = R[i];
            v.t = {t[0], t[1], t[2]};
            v.centre = sfm::mul(sfm::transpose(v.R), v.t) * -1.0;
            // The plugin's C = -R^T t, float32 like the rest of its CameraRecord.
            v.centre = {(float)v.centre.x, (float)v.centre.y, (float)v.centre.z};
            index[name] = (int)views.size();
            views.push_back(v);
        }
    }
    std::map<std::string, double> cfg;
    {
        std::ifstream f(fx / "config.txt");
        std::string k;
        double v;
        while (f >> k >> v) cfg[k] = v;
    }
    DensifyOptions opt;
    opt.plugin_exact = true;
    opt.certainty_floor = (float)cfg.at("certainty_thresh");
    opt.reproj_px = cfg.at("reproj_thresh");
    opt.sampson_px2 = cfg.at("sampson_thresh");
    opt.min_parallax_deg = cfg.at("min_parallax_deg");
    opt.no_filter = cfg.at("no_filter") != 0;
    const int W = (int)cfg.at("w_match"), H = (int)cfg.at("h_match");

    std::vector<double> axis_vals[3];
    double cert_err = 0, xyz_err = 0, rgb_err = 0, err_err = 0;
    int64_t cand_total = 0, cand_disagree = 0, edge_points = 0;
    std::vector<double> cand_xyz;
    double cand_err = 0, refuse_err = 0, mixed_err = 0;
    int64_t ours_total = 0, theirs_total = 0, samples_total = 0, disagree = 0, track_disagree = 0;
    std::vector<double> xyz_all;
    std::ifstream refs(fx / "refs.txt");
    std::string line;
    while (std::getline(refs, line)) {
        std::istringstream in(line);
        std::string ref;
        in >> ref;
        const fs::path d = fx / ref;
        RefMatches m;
        m.ref = index.at(ref);
        m.w = W;
        m.h = H;
        const std::vector<uint8_t> ma = npy<uint8_t>(d / "maskA.npy");
        std::string nb;
        for (int k = 0; in >> nb; k++) {
            const Warp raw = readWarp((d / ("raw_" + std::to_string(k) + ".rwm")).string());
            const std::vector<uint8_t> mb = npy<uint8_t>(d / ("maskB_" + std::to_string(k) + ".npy"));
            std::vector<float> c = collectCertainty(raw, ma, mb, opt);
            const std::vector<float> theirs = npy<float>(d / ("cert_" + std::to_string(k) + ".npy"));
            for (size_t i = 0; i < c.size(); i++) cert_err = std::max(cert_err, (double)std::fabs(c[i] - theirs[i]));
            // Downstream of this point both sides read the plugin's certainty.
            m.cert.push_back(theirs);
            m.warp.push_back(raw.warp);
            m.nbrs.push_back(index.at(nb));
        }
        m.rgb_match = npy<uint8_t>(d / "imA.npy");
        const std::vector<int64_t> sel = npy<int64_t>(d / "sel.npy");
        std::vector<std::vector<RefMatches::Candidate>> ours_rec;
        std::vector<std::vector<int>> ours_cand(sel.size()), theirs_cand(sel.size());
        std::vector<std::map<int, std::pair<sfm::Vec3, double>>> theirs_x(sel.size());
        m.record_candidates = &ours_rec;
        for (size_t k = 0; k < m.nbrs.size(); k++) {
            const fs::path cf = d / ("cand_" + std::to_string(k) + ".npy");
            if (!fs::exists(cf)) continue;
            const std::vector<int64_t> idx = npy<int64_t>(cf);
            std::vector<float> cx, ce;
            if (fs::exists(d / ("candX_" + std::to_string(k) + ".npy"))) {
                cx = npy<float>(d / ("candX_" + std::to_string(k) + ".npy"));
                ce = npy<float>(d / ("candE_" + std::to_string(k) + ".npy"));
            }
            for (size_t j = 0; j < idx.size(); j++) {
                theirs_cand[(size_t)idx[j]].push_back((int)k);
                if (!cx.empty())
                    theirs_x[(size_t)idx[j]][(int)k] = {{cx[3 * j], cx[3 * j + 1], cx[3 * j + 2]}, ce[j]};
            }
        }
        DensifyStats st;
        const std::vector<DensePoint> ours = triangulateRef(m, views, sel, opt, st);
        for (size_t i = 0; i < sel.size(); i++)
            for (const auto& c : ours_rec[i]) {
                ours_cand[i].push_back(c.k);
                auto it = theirs_x[i].find(c.k);
                if (it == theirs_x[i].end()) continue;
                const double dx = (c.X - it->second.first).norm();
                cand_xyz.push_back(dx);
                cand_err = std::max(cand_err, std::fabs(c.err - it->second.second));
            }
        const std::vector<float> txyz = npy<float>(d / "xyz.npy"), terr = npy<float>(d / "err.npy"),
                                 trgb = npy<float>(d / "rgb.npy");
        std::vector<std::vector<std::pair<std::string, std::pair<double, double>>>> ttracks;
        {
            std::ifstream f(d / "tracks.txt");
            std::string tl;
            while (std::getline(f, tl)) {
                std::istringstream ti(tl);
                int n;
                ti >> n;
                ttracks.emplace_back();
                for (int i = 0; i < n; i++) {
                    std::string vn;
                    double x, y;
                    ti >> vn >> x >> y;
                    ttracks.back().push_back({vn, {x, y}});
                }
            }
        }
        const size_t tn = txyz.size() / 3;
        for (size_t i = 0; i < tn; i++)
            for (int a = 0; a < 3; a++) axis_vals[a].push_back(txyz[3 * i + (size_t)a]);
        if (ttracks.size() != tn) throw std::runtime_error(ref + ": tracks and points disagree");
        std::map<Key, size_t> sample_at;
        for (size_t i = 0; i < sel.size(); i++) {
            sample_at[keyOf(referenceX((int)(sel[i] % W), W, true) * views[(size_t)m.ref].cam.width / W,
                            referenceX((int)(sel[i] / W), H, true) * views[(size_t)m.ref].cam.height / H)] = i;
            const std::set<int> a(ours_cand[i].begin(), ours_cand[i].end()),
                b(theirs_cand[i].begin(), theirs_cand[i].end());
            for (int k = 0; k < (int)m.nbrs.size(); k++) cand_disagree += a.count(k) != b.count(k);
            cand_total += (int64_t)m.nbrs.size();
        }
        std::map<Key, size_t> theirs_at;
        for (size_t i = 0; i < tn; i++)
            theirs_at[keyOf(ttracks[i][0].second.first, ttracks[i][0].second.second)] = i;
        std::set<Key> matched;
        for (const DensePoint& p : ours) {
            auto it = theirs_at.find(keyOf(p.track[0].x, p.track[0].y));
            if (it == theirs_at.end()) { disagree++; continue; }
            matched.insert(it->first);
            const size_t j = it->second;
            const double e = std::max({std::fabs(p.xyz.x - txyz[3 * j]), std::fabs(p.xyz.y - txyz[3 * j + 1]),
                                       std::fabs(p.xyz.z - txyz[3 * j + 2])});
            const auto si = sample_at.find(keyOf(p.track[0].x, p.track[0].y));
            if (si == sample_at.end()) throw std::runtime_error(ref + ": a point matches no sample");
            if (ours_cand[si->second] != theirs_cand[si->second]) { edge_points++; continue; }
            xyz_err = std::max(xyz_err, e);
            xyz_all.push_back(e);
            // The plugin's fusion, applied to the plugin's own candidates.
            if (!theirs_x[si->second].empty()) {
                sfm::Vec3 sum{0, 0, 0};
                double ws = 0;
                for (const auto& kv : theirs_x[si->second]) {
                    const double w = 1.0 / std::max(kv.second.second, 1e-4);
                    sum = sum + kv.second.first * w;
                    ws += w;
                }
                const sfm::Vec3 f = sum * (1.0 / ws);
                // Our candidates' positions with the plugin's errors as weights.
                sfm::Vec3 sum2{0, 0, 0};
                double ws2 = 0;
                for (const auto& c : ours_rec[si->second]) {
                    const double w = 1.0 / std::max(theirs_x[si->second].at(c.k).second, 1e-4);
                    sum2 = sum2 + c.X * w;
                    ws2 += w;
                }
                mixed_err = std::max(mixed_err, ((sum2 * (1.0 / ws2)) - sfm::Vec3{txyz[3 * j], txyz[3 * j + 1], txyz[3 * j + 2]}).norm());
                refuse_err = std::max({refuse_err, std::fabs(f.x - txyz[3 * j]), std::fabs(f.y - txyz[3 * j + 1]),
                                       std::fabs(f.z - txyz[3 * j + 2])});
            }
            err_err = std::max(err_err, std::fabs(p.error - terr[j]));
            for (int c = 0; c < 3; c++) rgb_err = std::max(rgb_err, (double)std::fabs(p.rgb[c] - trgb[3 * j + c]));
            std::set<std::string> a, b;
            for (const Observation& o : p.track) a.insert(views[(size_t)o.view].name);
            for (const auto& o : ttracks[j]) b.insert(o.first);
            track_disagree += a != b;
        }
        disagree += (int64_t)(tn - matched.size());
        ours_total += (int64_t)ours.size();
        theirs_total += (int64_t)tn;
        samples_total += (int64_t)sel.size();
        std::printf("%-22s samples %6zu  plugin %6zu  ours %6zu\n", ref.c_str(), sel.size(), tn, ours.size());
    }
    // Scene diameter: the plugin cloud's 1st-99th percentile box diagonal.
    double diam = 0;
    for (auto& v : axis_vals) {
        if (v.empty()) continue;
        std::sort(v.begin(), v.end());
        const double e = v[(size_t)(0.99 * (double)(v.size() - 1))] - v[(size_t)(0.01 * (double)(v.size() - 1))];
        diam += e * e;
    }
    diam = std::sqrt(diam);
    std::sort(xyz_all.begin(), xyz_all.end());
    const double p50 = xyz_all.empty() ? NAN : xyz_all[xyz_all.size() / 2];
    const double p999 = xyz_all.empty() ? NAN : xyz_all[(size_t)(0.999 * (double)(xyz_all.size() - 1))];
    const double agree = 1.0 - (double)disagree / (double)std::max<int64_t>(1, samples_total);
    const double count_rel = (double)(ours_total - theirs_total) / (double)std::max<int64_t>(1, theirs_total);
    std::printf("certainty after collect: max |diff| %.3g\n", cert_err);
    std::sort(cand_xyz.begin(), cand_xyz.end());
    if (!cand_xyz.empty())
        std::printf("per-candidate xyz |diff| over %zu: p50 %.3g, p99.9 %.3g, max %.3g; err |diff| max %.3g px\n",
                    cand_xyz.size(), cand_xyz[cand_xyz.size() / 2],
                    cand_xyz[(size_t)(0.999 * (double)(cand_xyz.size() - 1))], cand_xyz.back(), cand_err);
    std::printf("the plugin's fusion of its own candidates vs its points: max |diff| %.3g\n", refuse_err);
    std::printf("our candidates fused with the plugin's error weights vs its points: max |diff| %.3g\n", mixed_err);
    const double cand_agree = 1.0 - (double)cand_disagree / (double)std::max<int64_t>(1, cand_total);
    std::printf("candidate decisions (sample x neighbour): %lld, agree %.6f; points whose candidate "
                "sets differ (threshold edges, left out of the xyz rows): %lld\n",
                (long long)cand_total, cand_agree, (long long)edge_points);
    std::printf("points: plugin %lld, ours %lld (%+.3f%%); samples %lld; decisions agree %.5f\n",
                (long long)theirs_total, (long long)ours_total, 100 * count_rel, (long long)samples_total, agree);
    std::printf("xyz |diff| over %zu shared points with equal candidate sets: p50 %.3g, p99.9 %.3g, max %.3g; scene "
                "diameter %.4g (max / diameter %.3g)\n",
                xyz_all.size(), p50, p999, xyz_err, diam, xyz_err / diam);
    std::printf("track sets differ on %lld points; error |diff| max %.3g px; colour |diff| max %.3g\n",
                (long long)track_disagree, err_err, rgb_err);
    int fails = 0;
    auto gate = [&](bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); fails += !ok; };
    gate(theirs_total > 1000 && ours_total > 1000, "both sides produced points");
    gate(cert_err <= 1e-6, "collect: certainty matches");
    gate(agree >= 0.999, "decisions agree on >= 99.9% of samples");
    gate(cand_agree >= 0.999, "candidate decisions agree on >= 99.9%");
    gate(std::fabs(count_rel) <= 0.005, "count within 0.5%");
    gate(xyz_err <= 1e-5 * diam, "xyz within 1e-5 x scene diameter (max)");
    gate(p999 <= 1e-5 * diam, "xyz within 1e-5 x scene diameter (p99.9)");
    return fails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
