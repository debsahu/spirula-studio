// densify_gui_test -- the argument list the GUI hands `spirula densify`, and
// the model chooser's listing of a dataset's COLMAP models. Scratch folder, no
// matcher, no window.

#include "app/gui/DensifyRunner.h"
#include "app/gui/ReconModels.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace gui;

namespace {

int g_failures = 0;

void expect(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "BAD ", what.c_str());
    if (!ok) g_failures++;
}

bool has(const std::vector<std::string>& a, const std::string& x) {
    return std::find(a.begin(), a.end(), x) != a.end();
}

// The value after `flag`, or "<absent>".
std::string value_of(const std::vector<std::string>& a, const std::string& flag) {
    for (size_t i = 0; i + 1 < a.size(); i++)
        if (a[i] == flag) return a[i + 1];
    return "<absent>";
}

void put_count(const fs::path& file, uint64_t n) {
    fs::create_directories(file.parent_path());
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(&n), sizeof n);
    f << "padding";
}

void model(const fs::path& dir, uint64_t images, int64_t points) {
    put_count(dir / "cameras.bin", 1);
    put_count(dir / "images.bin", images);
    if (points >= 0) put_count(dir / "points3D.bin", (uint64_t)points);
}

}  // namespace

int main() {
    // ---- the arguments -----------------------------------------------------------
    {
        const DensifyJob stock;
        const auto a = densify_args(stock, "/ds", "/ds/images", "", false, true);
        expect(a[0] == "densify" && a[1] == "/ds", "the dataset follows the subcommand");
        expect(has(a, "--no-masks") && !has(a, "--mask-dir"), "no mask folder: no masks");
        expect(!has(a, "--model") && !has(a, "--preset") && !has(a, "--refs") &&
                   !has(a, "--neighbours") && !has(a, "--neighbour-rule") &&
                   !has(a, "--matches-per-ref") && !has(a, "--max-points") &&
                   !has(a, "--min-track") && !has(a, "--overwrite") && !has(a, "--device"),
               "a job of defaults sends nothing but the folders: every setting is the tool's");
        expect(value_of(a, "--image-dir") == "/ds/images", "the image folder is passed");
    }
    {
        DensifyJob j;
        j.model = "sparse/1";
        j.preset = 4;
        j.refs = 12;
        j.neighbours = 5;
        j.rule = 2;
        j.matches_per_ref = 9000;
        j.max_points = 3000000;
        j.min_track = 4;
        j.overwrite = true;
        j.device_uuid = "GPU-abc";
        const auto a = densify_args(j, "/ds", "/ds/images", "/ds/masks", true, true);
        expect(value_of(a, "--model") == "sparse/1" && value_of(a, "--preset") == "high" &&
                   value_of(a, "--refs") == "12" && value_of(a, "--neighbours") == "5" &&
                   value_of(a, "--neighbour-rule") == "pose" &&
                   value_of(a, "--matches-per-ref") == "9000" &&
                   value_of(a, "--max-points") == "3000000" && value_of(a, "--min-track") == "4" &&
                   value_of(a, "--device") == "GPU-abc" && has(a, "--overwrite"),
               "every override reaches its own flag with its own value");
        expect(value_of(a, "--mask-dir") == "/ds/masks" && has(a, "--flip-mask") &&
                   !has(a, "--no-masks"),
               "masks, and the flip that goes with them");
        j.use_masks = false;
        const auto off = densify_args(j, "/ds", "/ds/images", "/ds/masks", true, true);
        expect(has(off, "--no-masks") && !has(off, "--mask-dir") && !has(off, "--flip-mask"),
               "the box that leaves masks out sends no mask folder at all");
        j.preset = 4;
        const auto old = densify_args(j, "/ds", "/ds/images", "", false, false);
        expect(!has(old, "--preset"),
               "a tool that has no presets is never sent the flag it would refuse");
        j.preset = 99;
        j.rule = -3;
        const auto wild = densify_args(j, "/ds", "/ds/images", "", false, true);
        expect(value_of(wild, "--preset") == "precise" && !has(wild, "--neighbour-rule"),
               "an index out of range is clamped, not read past the table");
        expect(std::string(kDensifyPresets[0]) == "auto" && kNumDensifyPresets == 6,
               "the preset table is the one the combo and the record both read");
    }

    // ---- the model chooser ---------------------------------------------------------
    {
        const fs::path ds = fs::temp_directory_path() / "spirula_densify_gui_test";
        fs::remove_all(ds);
        expect(list_recon_models(ds.string()).empty(), "no dataset, no models");
        model(ds / "sparse" / "0", 120, 4000);
        model(ds / "sparse" / "0-roma", 120, 2500000);
        model(ds / "sparse" / "1", 300, 900);
        model(ds / "colmap" / "sparse" / "0", 50, -1);
        fs::create_directories(ds / "sparse" / "empty");
        const auto m = list_recon_models(ds.string());
        expect(m.size() == 4, "a folder without cameras.bin and images.bin is no model");
        expect(m.size() == 4 && m[0].rel == "sparse/1" && m[0].images == 300 && m[0].points == 900,
               "the model with the most images comes first, with both counts");
        expect(m.size() == 4 && m[1].rel == "sparse/0" && m[2].rel == "sparse/0-roma",
               "equal images: by path, so sparse/0 sorts before its dense sibling");
        expect(m.size() == 4 && m[2].points == 2500000, "the dense model's own point count");
        expect(m.size() == 4 && m[3].rel == "colmap/sparse/0" && m[3].points == -1,
               "a model with no points3D.bin reports -1");
        expect(recon_point_count((ds / "sparse" / "0-roma").string()) == 2500000 &&
                   recon_point_count((ds / "nowhere").string()) == -1,
               "the count of a model, and of one that is not there");
        fs::remove_all(ds);
    }

    std::printf(g_failures ? "\nFAILED: %d\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}
