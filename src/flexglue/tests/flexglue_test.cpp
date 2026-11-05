// FlexGlue: the checkpoint, the descriptor and the matcher, against a
// reference pair written by tools/flexglue_ref/ (PyTorch, mp=None, COLMAP's
// settings).
//
//   flexglue_test --ref DIR [--checkpoint PATH] [--bench N]
//
// DIR holds image{0,1}_rgb512.npy ([512, 512, 3] in [0, 1]), kpts{0,1}.npy
// ([n, 2], 512-frame pixels) and the reference outputs features{0,1}.npy,
// covisibility_score.npy, stop_layer.npy, matches0.npy, mscores0.npy. With
// SS_FLEXGLUE_DUMP set every stage is written for tools/flexglue_ref's compare.
// Without a checkpoint on disk it SKIPS.

#include "flexglue/FlexGlue.h"
#include "flexglue/model/Fetch.h"
#include "nn/core/Log.h"
#include "nn/io/StageDump.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* fmt, ...) {
    ++g_checks;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", buf);
    if (!ok) ++g_failures;
}

bool load(const fs::path& dir, const char* name, std::vector<float>& v,
          std::vector<int64_t>* shape = nullptr) {
    std::vector<int64_t> s;
    if (!nn::read_npy_f32((dir / (std::string(name) + ".npy")).string(), v, s)) return false;
    if (shape) *shape = s;
    return true;
}

// Relative L2 error of `got` against `want`.
double rel_l2(const std::vector<float>& got, const std::vector<float>& want) {
    double e = 0, n = 0;
    for (size_t i = 0; i < want.size(); ++i) {
        const double d = (double)got[i] - want[i];
        e += d * d;
        n += (double)want[i] * want[i];
    }
    return std::sqrt(e / std::max(n, 1e-30));
}

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    std::string ref, ckpt;
    int bench = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--ref" && i + 1 < argc) ref = argv[++i];
        else if (a == "--checkpoint" && i + 1 < argc) ckpt = argv[++i];
        else if (a == "--bench" && i + 1 < argc) bench = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr, "usage: %s --ref DIR [--checkpoint PATH] [--bench N]\n", argv[0]);
            return 2;
        }
    }
    if (ckpt.empty()) ckpt = nn::cached_path(flexglue::checkpoint_file());
    if (!fs::exists(ckpt)) {
        std::printf("SKIP: no checkpoint at %s\n", ckpt.c_str());
        return 0;
    }
    check(nn::sha256_file(ckpt) == flexglue::checkpoint_file().sha256,
          "checkpoint SHA-256 matches the pinned release");
    if (ref.empty()) {
        std::printf("SKIP: no --ref pair\n");
        return g_failures ? 1 : 0;
    }

    flexglue::Model model;
    model.load(ckpt);
    const fs::path dir(ref);
    std::vector<float> img[2], kp[2];
    for (int s = 0; s < 2; ++s) {
        const std::string i = std::to_string(s);
        std::vector<int64_t> shape;
        if (!load(dir, ("image" + i + "_rgb512").c_str(), img[s], &shape) || shape.size() != 3 ||
            shape[0] != 512 || shape[1] != 512 || shape[2] != 3) {
            std::printf("FAIL: %s has no [512, 512, 3] image%d_rgb512.npy\n", ref.c_str(), s);
            return 1;
        }
        if (!load(dir, ("kpts" + i).c_str(), kp[s]) || kp[s].empty()) {
            std::printf("FAIL: %s has no kpts%d.npy\n", ref.c_str(), s);
            return 1;
        }
    }

    std::shared_ptr<flexglue::DenseFeatures> f[2];
    for (int s = 0; s < 2; ++s) {
        model.setDumpPrefix("image" + std::to_string(s) + "_");
        const double t0 = now_s();
        f[s] = model.describeResized(img[s].data());
        std::printf("describe image%d: %.1f ms\n", s, (now_s() - t0) * 1e3);
        std::vector<float> want;
        if (load(dir, ("features" + std::to_string(s)).c_str(), want)) {
            const std::vector<float> got = model.download(*f[s]);
            const double e = got.size() == want.size() ? rel_l2(got, want) : 1e9;
            check(e < 1e-4, "features%d relative L2 %.3g (bar 1e-4)", s, e);
        }
    }

    flexglue::Keypoints k[2];
    for (int s = 0; s < 2; ++s) {
        k[s].xy = kp[s].data();
        k[s].n = (uint32_t)(kp[s].size() / 2);
        k[s].width = k[s].height = 512;
    }
    model.setDumpPrefix("");
    flexglue::MatchOptions opts;
    opts.keep_correspondences = true;
    double t0 = now_s();
    const flexglue::MatchResult r = model.match(*f[0], k[0], *f[1], k[1], opts);
    std::printf("match %u x %u: %.1f ms, covisibility %.6f, stop layer %d\n", k[0].n, k[1].n,
                (now_s() - t0) * 1e3, r.covisibility, r.stop_layer);

    std::vector<float> want;
    for (int s = 0; s < 2 && r.stop_layer == 9; ++s) {
        // Predicted points in 512-frame pixels: the readout's own unit.
        std::vector<float> wp, wc;
        if (load(dir, ("p2d" + std::to_string(s)).c_str(), wp) && wp.size() == r.p2d[s].size()) {
            std::vector<double> err;
            for (size_t i = 0; i < wp.size(); i += 2)
                err.push_back(256.0 * std::hypot(r.p2d[s][i] - wp[i], r.p2d[s][i + 1] - wp[i + 1]));
            std::sort(err.begin(), err.end());
            const double p99 = err[(size_t)(0.99 * (double)(err.size() - 1))], mx = err.back();
            check(p99 < 0.01, "p2d%d error p99 %.3g px, max %.3g px (bar p99 0.01 px)", s, p99, mx);
        }
        if (load(dir, ("conf" + std::to_string(s)).c_str(), wc) && wc.size() == r.conf[s].size()) {
            double mx = 0;
            for (size_t i = 0; i < wc.size(); ++i)
                mx = std::max(mx, (double)std::fabs(r.conf[s][i] - wc[i]));
            check(mx < 1e-3, "conf%d max |diff| %.3g (bar 1e-3)", s, mx);
        }
    }
    if (load(dir, "covisibility_score", want) && !want.empty())
        check(std::fabs(r.covisibility - want[0]) < 1e-4, "covisibility %.6f vs %.6f",
              r.covisibility, want[0]);
    if (load(dir, "stop_layer", want) && !want.empty())
        check(r.stop_layer == (int)want[0], "stop layer %d vs %d", r.stop_layer, (int)want[0]);
    std::vector<float> wm, ws;
    if (load(dir, "matches0", wm) && load(dir, "mscores0", ws) && wm.size() == r.matches0.size()) {
        size_t ref_n = 0, ours_n = 0, same = 0;
        double max_ds = 0;
        for (size_t i = 0; i < wm.size(); ++i) {
            const int w = (int)wm[i], o = r.matches0[i];
            ref_n += w >= 0;
            ours_n += o >= 0;
            if (w >= 0 && w == o) {
                ++same;
                max_ds = std::max(max_ds, (double)std::fabs(r.scores0[i] - ws[i]));
            }
        }
        const double agree = ref_n ? (double)same / ref_n : (ours_n == 0 ? 1.0 : 0.0);
        std::printf("matches: ours %zu, reference %zu, identical %zu\n", ours_n, ref_n, same);
        check(agree >= 0.99 && ours_n <= ref_n + ref_n / 100 + 1,
              "matches identical to the reference: %.2f%% (bar 99%%)", 100 * agree);
        check(max_ds < 1e-3, "score max |diff| over shared matches %.3g (bar 1e-3)", max_ds);
    }

    if (bench > 0) {
        double td = 0, tm = 0;
        for (int it = 0; it < bench; ++it) {
            t0 = now_s();
            f[0] = model.describeResized(img[0].data());
            td += now_s() - t0;
            t0 = now_s();
            model.match(*f[0], k[0], *f[1], k[1], opts);
            tm += now_s() - t0;
        }
        std::printf("bench: describe %.1f ms/image, match %.1f ms/pair (%d runs)\n",
                    td / bench * 1e3, tm / bench * 1e3, bench);
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
