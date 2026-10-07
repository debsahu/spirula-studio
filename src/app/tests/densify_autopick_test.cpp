// Gate H-3: a densified sibling model never becomes the one the trainer picks
// by itself. The pick is the dataset parser's (most images, then path), so
// this runs the parser's own search over a dataset holding both models.
// Mutants it catches: a sibling name that sorts first, a writer that adds an
// image record, a writer that drops one.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "data/DatasetParser.h"
#include "roma/DensifyEdit.h"
#include "roma/DensifyRun.h"
#include "roma/Synthetic.h"

namespace fs = std::filesystem;

int main() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
        fails += !ok;
    };
    try {
        const fs::path d = fs::temp_directory_path() / "spirula_densify_autopick_test";
        std::error_code ec;
        fs::remove_all(d, ec);
        roma::writeStairDataset(roma::stairScene(), d.string(), 96, 192, 300);
        const std::string src = (d / "sparse" / "0").string();
        const std::string before = find_colmap_poses(d.string());
        check(fs::equivalent(before, src), "the source is the pick before densifying: " + before);

        roma::DensifyJob job;
        job.model_dir = src;
        const roma::DensifyPlan pl = roma::planDensify(job);
        std::vector<roma::DensePoint> cloud(5000);
        for (size_t i = 0; i < cloud.size(); i++) cloud[i].xyz = {0.001 * (double)i, 0.5, 0.5};
        const std::string out = roma::siblingDir(src);
        check(fs::path(out).parent_path() == fs::path(src).parent_path(), "the sibling sits beside its source");
        roma::writeSibling(src, out, pl, cloud, "{}\n");
        check(fs::exists(fs::path(out) / "points3D.bin"), "the sibling was written");

        const std::string picked = find_colmap_poses(d.string());
        check(fs::equivalent(picked, src), "with the sibling present the pick is still the source: " + picked);
        const std::string with_points = find_colmap_model(d.string(), "");
        check(fs::equivalent(with_points, src), "the seeding pick is still the source: " + with_points);
        // And it is reachable when asked for by name.
        const std::string named = find_colmap_model(d.string(), fs::path(out).lexically_relative(d).string());
        check(fs::equivalent(named, out), "--colmap-recon-dir reaches the sibling: " + named);
        // The edit of the sibling is no more the pick than the sibling is.
        std::vector<uint8_t> keep(cloud.size(), 1);
        for (size_t i = 0; i < keep.size(); i += 4) keep[i] = 0;
        const roma::EditResult er = roma::writeEditedSibling(out, keep);
        check(fs::path(er.out_dir).filename() == "0-roma-edit", "the edit is <model>-roma-edit");
        check(read_points3D_binary(er.out_dir).num() == er.kept && er.kept < (int64_t)cloud.size(),
              "the dataset parser reads the kept points of the edit");
        const std::string with_edit = find_colmap_poses(d.string());
        check(fs::equivalent(with_edit, src), "with the edit present the pick is still the source: " + with_edit);
        check(fs::equivalent(find_colmap_model(d.string(), ""), src), "the seeding pick is still the source with the edit present");
        check(fs::equivalent(find_colmap_model(d.string(), fs::path(er.out_dir).lexically_relative(d).string()), er.out_dir),
              "--colmap-recon-dir reaches the edit by name");
        fs::remove_all(d, ec);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 2;
    }
    return fails ? 1 : 0;
}
