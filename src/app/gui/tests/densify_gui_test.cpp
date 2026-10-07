// densify_gui_test -- the argument list the GUI hands `spirula densify`, and
// the model chooser's listing of a dataset's COLMAP models. Scratch folder, no
// matcher, no window.

#include "app/gui/DensifyRunner.h"
#include "app/gui/ReconModels.h"

#include "core/Sha256.h"

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
        expect(!has(a, "--accept-license") && !has(a, "--out"),
               "no default job accepts a licence for the user or names an output folder");
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
        expect(!has(a, "--accept-license") && !has(a, "--out"),
               "no override accepts a licence for the user or names an output folder");
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

    // ---- the point source -----------------------------------------------------------
    {
        const DensifyJob stock;
        expect(!has(densify_args(stock, "/ds", "/ds/images", "", false, true), "--source"),
               "auto sends no --source: the tool resolves it from the dataset");
        const char* want[] = {nullptr, "roma", "moge", "hybrid"};
        for (int s = 1; s < kNumDensifySources; s++) {
            DensifyJob j;
            j.source = s;
            expect(value_of(densify_args(j, "/ds", "/ds/images", "", false, true), "--source") ==
                       want[s],
                   std::string("source ") + want[s] + " reaches --source as that word");
        }
        DensifyJob wild;
        wild.source = 99;
        expect(value_of(densify_args(wild, "/ds", "/ds/images", "", false, true), "--source") ==
                   "hybrid",
               "an index out of range is clamped, not read past the table");
        // The CLI's own spellings, which `densify_main.cpp` parses.
        expect(std::string(kDensifySources[kSourceMoge]) == "moge" &&
                   std::string(kDensifySources[kSourceRoma]) == "roma" &&
                   std::string(kDensifySources[kSourceHybrid]) == "hybrid" &&
                   std::string(kDensifySources[kSourceAuto]) == "auto",
               "the source table spells what the CLI parses");

        DensifyJob moge, roma, hybrid, autoj;
        moge.source = kSourceMoge;
        roma.source = kSourceRoma;
        hybrid.source = kSourceHybrid;
        expect(!densify_needs_roma(moge), "MoGe depth needs no RoMa checkpoint or licence");
        expect(densify_needs_roma(roma) && densify_needs_roma(hybrid) && densify_needs_roma(autoj),
               "RoMa, hybrid and auto (roma) all do");
        expect(densify_resolved_source(kSourceAuto) == kSourceRoma &&
                   densify_resolved_source(kSourceMoge) == kSourceMoge &&
                   densify_resolved_source(kSourceHybrid) == kSourceHybrid &&
                   densify_resolved_source(kSourceRoma) == kSourceRoma,
               "auto is roma, with or without depth maps; hybrid is never auto; a chosen source stays");
        const fs::path ds = fs::temp_directory_path() / "spirula_densify_gui_source";
        fs::remove_all(ds);
        fs::create_directories(ds / "depths");
        expect(!densify_has_depth_maps(ds.string()), "an empty depths/ is no depth maps");
        { std::ofstream(ds / "depths" / "a.png") << "x"; }
        expect(densify_has_depth_maps(ds.string()), "a file in depths/ is");
        fs::remove_all(ds);
        expect(!densify_has_depth_maps(ds.string()), "no folder is none");

        DensifyJob cur, incoming;
        incoming.source = kSourceMoge;
        expect(densify_after_settings(cur, incoming, ModelCarry::Reset).source == kSourceMoge &&
                   densify_after_settings(cur, incoming, ModelCarry::Keep).source == kSourceMoge,
               "a preset's source is taken, the model is what it leaves alone");
    }

    // ---- state that must not leak between captures ---------------------------------
    {
        DensifyJob cur, incoming;
        cur.model = "sparse/1";
        cur.enable = true;
        incoming.preset = 3;
        const DensifyJob kept = densify_after_settings(cur, incoming, ModelCarry::Keep);
        expect(kept.model == "sparse/1" && kept.preset == 3 && !kept.enable,
               "a preset takes the settings but leaves the capture's model alone");
        const DensifyJob row = densify_after_settings(cur, incoming, ModelCarry::Reset);
        expect(row.model.empty() && row.preset == 3,
               "a batch row never inherits the previous row's model");
        DensifyJob on;
        on.enable = true;
        expect(densify_blocks_run(on, false, false), "an unready checkpoint blocks Run");
        expect(!densify_blocks_run(on, false, true), "... a ready one does not");
        expect(!densify_blocks_run(on, true, false),
               "a laser scan's run never uses the step, so it never waits for the checkpoint");
        expect(!densify_blocks_run(DensifyJob{}, false, false), "a step that is off blocks nothing");
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
        expect(is_dense_model("sparse/0-roma") && !is_dense_model("sparse/0") &&
                   !is_dense_model("-roma") && !is_dense_model("sparse/roma"),
               "only a name ending -roma is a dense model");
        expect(recon_point_count((ds / "sparse" / "0-roma").string()) == 2500000 &&
                   recon_point_count((ds / "nowhere").string()) == -1,
               "the count of a model, and of one that is not there");
        fs::remove_all(ds);
    }

    // ---- the edit sibling, folders mid-write, the checksums ---------------------
    {
        const fs::path ds = fs::temp_directory_path() / "spirula_densify_gui_test_edit";
        fs::remove_all(ds);
        model(ds / "sparse" / "0", 120, 4000);
        model(ds / "sparse" / "0-roma", 120, 2500);
        model(ds / "sparse" / "0-roma-edit", 120, 2000);
        model(ds / "sparse" / "0-roma.partial", 120, 10);
        model(ds / "sparse" / "0-roma-edit.old", 120, 2500);
        const auto m = list_recon_models(ds.string());
        expect(m.size() == 3 && m[0].rel == "sparse/0" && m[1].rel == "sparse/0-roma" && m[2].rel == "sparse/0-roma-edit",
               "listing: a folder mid-write (.partial) or left by a crash (.old) is no model");
        expect(is_dense_model("sparse/0-roma-edit") && !is_dense_model("sparse/0-edit") && !is_dense_model("-roma-edit"),
               "an edit of a dense model is a dense model, and nothing else is");

        const fs::path dense = ds / "sparse" / "0-roma";
        expect(cloud_check(dense.string()) == CloudCheck::None, "no densify.json, no record");
        int pad = 0;
        auto record = [&](const std::string& a, const std::string& b) {
            std::ofstream(dense / "densify.json") << "{\"points3D_sha256\": \"" << a << "\", \"tracks_sha256\": \"" << b
                                                  << "\"" << std::string((size_t)++pad, ' ') << "}\n";
        };
        std::ofstream(dense / "points3D_tracks.bin", std::ios::binary) << "tracks";
        const std::string pa = spirula::sha256_file((dense / "points3D.bin").string());
        const std::string tb = spirula::sha256_file((dense / "points3D_tracks.bin").string());
        record(pa, tb);
        expect(cloud_check(dense.string()) == CloudCheck::Ok, "checksums that match the files: Ok");
        record(std::string(64, '0'), tb);
        expect(cloud_check(dense.string()) == CloudCheck::Mismatch,
               "a wrong cloud checksum: Mismatch, not the Ok cached for the record before it");
        record(pa, std::string(64, '0'));
        expect(cloud_check(dense.string()) == CloudCheck::Mismatch, "a wrong tracks checksum: Mismatch");
        record(pa, tb);
        std::ofstream(dense / "points3D.bin", std::ios::binary | std::ios::app) << "x";
        expect(cloud_check(dense.string()) == CloudCheck::Mismatch,
               "a cloud changed after it was recorded: Mismatch, not served from the cache of the good one");
        fs::remove_all(ds);
    }

    // ---- the progress dir ----------------------------------------------------------
    {
        const DensifyJob stock;
        const auto without = densify_args(stock, "/ds", "/ds/images", "", false, true);
        expect(!has(without, "--progress-dir"), "no progress dir unless one is given");
        const auto with = densify_args(stock, "/ds", "/ds/images", "", false, true, "/ws/.progress");
        expect(value_of(with, "--progress-dir") == "/ws/.progress", "the progress dir reaches the child");
    }

    std::printf(g_failures ? "\nFAILED: %d\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}
