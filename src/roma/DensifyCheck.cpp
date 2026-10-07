#include "roma/DensifyCheck.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "roma/DensifyRun.h"
#include "roma/DumpMatcher.h"
#include "roma/Synthetic.h"

namespace roma {

namespace fs = std::filesystem;

int densifyCheck(const CheckOptions& co) {
    const fs::path dir = co.dir.empty()
                             ? fs::temp_directory_path() / "spirula_densify_check"
                             : fs::path(co.dir);
    const bool fresh = !fs::exists(dir / "sparse" / "0" / "images.bin");
    if (fresh) {
        std::printf("check: writing the staircase scene to %s\n", dir.string().c_str());
        writeStairDataset(stairScene(), dir.string(), 960, 2048, 4000);
    }
    const Scene scene = stairScene();

    DensifyJob job;
    job.model_dir = (dir / "sparse" / "0").string();
    job.out_dir = (dir / "sparse" / "0-roma").string();
    job.image_path = [&](const std::string& n) { return (dir / "images" / n).string(); };
    if (co.masks)
        job.mask_path = [&](const std::string& n) {
            const fs::path m = dir / "masks" / n;
            return fs::exists(m) ? m.string() : std::string();
        };
    job.opt.refs = 1.0;
    job.opt.seed = 7;
    std::unique_ptr<Matcher> matcher;
    DensifyPlan plan;
    if (co.matches.empty()) {
        struct Size : Matcher {
            int s;
            explicit Size(int s_) : s(s_) {}
            int inputSize() const override { return s; }
            Warp match(const MatchImage&, const MatchImage&) override { return {}; }
            std::string describe() const override { return ""; }
        } sizer(co.match_size);
        job.matcher = &sizer;
        plan = planDensify(job);
        matcher = std::make_unique<OracleMatcher>(&scene, OracleMatcher::independentViews(plan.images),
                                                  co.match_size, co.noise_px, co.outliers, 3);
    } else {
        matcher = std::make_unique<DumpMatcher>(co.matches, 640);
        job.matcher = matcher.get();
        plan = planDensify(job);
    }
    job.matcher = matcher.get();
    std::printf("check: %s; %zu images, %zu views, %zu reference views, %lld pairs, voxel %.4f m, "
                "min track %d\n",
                matcher->describe().c_str(), plan.images.size(), plan.views.size(),
                plan.ref_views.size(), (long long)plan.pairs, plan.voxel, plan.min_track);
    const DensifyResult r = runDensify(job, plan, nullptr);
    writeSibling(job.model_dir, job.out_dir, plan, r.cloud, "{\"check\": true}\n");

    const CloudScore s = scoreCloud(scene, r.cloud, 0.01, 0.05, 0.02, 0.02);
    const DensifyStats& st = r.stats;
    std::printf("check: samples %lld, rejected: certainty %lld, outside %lld, sampson %lld, "
                "reproj %lld, cheirality %lld, parallax %lld, ref %lld, short track %lld\n",
                (long long)st.samples, (long long)st.below_certainty, (long long)st.outside,
                (long long)st.sampson, (long long)st.reproj, (long long)st.cheirality,
                (long long)st.parallax, (long long)st.ref_reproj, (long long)st.short_track);
    {
        const double edges[] = {0, 3, 6, 12, 24, 1e9};
        for (int b = 0; b < 5; b++) {
            int64_t n = 0, in = 0, beyond = 0, tl3 = 0;
            for (const DensePoint& p : r.cloud) {
                if (p.parallax_deg < edges[b] || p.parallax_deg >= edges[b + 1]) continue;
                const double d = scene.distance(p.xyz);
                n++; in += d <= 0.01; beyond += d > 0.05; tl3 += p.distinct_images >= 3;
            }
            std::printf("check: parallax %4.0f-%-4.0f deg: points %7lld, within 1 cm %.3f, past 5 cm %lld, "
                        "track>=3 %.3f\n", edges[b], std::min(edges[b + 1], 999.0), (long long)n,
                        n ? (double)in / n : 0.0, (long long)beyond, n ? (double)tl3 / n : 0.0);
        }
    }
    if (std::getenv("SS_DENSIFY_CHECK_DEBUG")) {
        int shown = 0;
        for (const DensePoint& p : r.cloud) {
            if (scene.distance(p.xyz) <= 0.05 || shown++ >= 8) continue;
            std::printf("far %.3f m: images %d parallax %.1f err %.3f track", scene.distance(p.xyz),
                        p.distinct_images, p.parallax_deg, p.error);
            for (const Observation& o : p.track)
                std::printf(" %s(%.1f,%.1f)", plan.views[(size_t)o.view].name.c_str(), o.x, o.y);
            std::printf("\n");
        }
    }
    int fails = 0;
    auto gate = [&](bool ok, const char* what, double got, const char* bar) {
        std::printf("%s: %-40s %.4f (%s)\n", ok ? "ok" : "FAIL", what, got, bar);
        fails += !ok;
    };
    gate(s.points >= 5000, "points", (double)s.points, ">= 5000");
    gate(s.within >= 0.95, "share within 1 cm of a surface", s.within, ">= 0.95");
    gate(s.riser_cover >= 0.90, "riser area covered at 2 cm", s.riser_cover, ">= 0.90");
    gate(s.beyond == 0, "points farther than 5 cm", (double)s.beyond, "== 0");
    if (!co.keep && co.dir.empty()) {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::printf("%s\n", fails ? "check FAILED" : "check passed");
    return fails ? 1 : 0;
}

}  // namespace roma
