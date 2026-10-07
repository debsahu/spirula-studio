// `spirula densify` -- a dense point cloud for a solved COLMAP model, from
// dense matches between its images, written as a sibling model so nothing the
// dataset already has is touched. src/roma/DensifyRun.h does the work;
// docs/notes/densify.md is the design and what each setting is chosen from.

#include "app/Tools.h"

#include "core/VulkanDeviceSelection.h"
#include "data/DatasetParser.h"
#include "i18n/Locale.h"
#include "i18n/TimeFormat.h"
#include "i18n/catalog/Densify.h"
#include "nn/Device.h"
#include "nn/io/Fetch.h"
#include "roma/DensifyCheck.h"
#include "roma/DensifyRun.h"
#include "roma/DumpMatcher.h"
#include "roma/model/Fetch.h"
#include "roma/model/RomaMatcher.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace D = spirula::i18n::msg::densify;
using spirula::i18n::format;

namespace {

void help_row(const char* flags, const spirula::i18n::Msg& m, int col = 30) {
    std::string left = std::string("    ") + flags;
    if (spirula::i18n::display_width(left) >= col + 4) {
        std::fprintf(stderr, "%s\n", left.c_str());
        left.clear();
    }
    left = spirula::i18n::pad_to(left, col + 4);
    for (const std::string& line : spirula::i18n::wrap(m.get(), 86 - col - 4)) {
        std::fprintf(stderr, "%s%s\n", left.c_str(), line.c_str());
        left.assign((size_t)col + 4, ' ');
    }
}

void usage() {
    const std::string prog = app::program_name();
    std::fprintf(stderr, "%s -- %s\n\n", prog.c_str(), D::tagline.get());
    std::fprintf(stderr, "    %s <dataset> [options]\n\n", prog.c_str());
    for (const std::string& l : spirula::i18n::wrap(D::usage_target.get(), 80))
        std::fprintf(stderr, "    %s\n", l.c_str());
    std::fprintf(stderr, "\n");
    for (const std::string& l : spirula::i18n::wrap(D::head_auto.get(), 80))
        std::fprintf(stderr, "    %s\n", l.c_str());
    std::fprintf(stderr, "\n%s\n", D::head_options.get());
    help_row("--model <dir>", D::opt_model);
    help_row("--out <dir>", D::opt_out);
    help_row("--image-dir <dir>", D::opt_image_dir);
    help_row("--mask-dir <dir> | --no-masks", D::opt_mask_dir);
    help_row("--flip-mask", D::opt_flip_mask);
    help_row("--matches <dir>", D::opt_matches);
    help_row("--export-pairs <dir>", D::opt_export_pairs);
    help_row("--plugin-exact", D::opt_plugin_exact);
    help_row("--refs <fraction|n>", D::opt_refs);
    help_row("--neighbours <k>", D::opt_neighbours);
    help_row("--neighbour-rule covis|pose", D::opt_neighbour_rule);
    help_row("--holdout-every <n>", D::opt_holdout);
    help_row("--split auto|yes|no", D::opt_split);
    help_row("--face-pairs auto|all", D::opt_face_pairs);
    help_row("--matches-per-ref <n>", D::opt_matches_per_ref);
    help_row("--min-certainty <c>", D::opt_min_certainty);
    help_row("--reproj <px>", D::opt_reproj);
    help_row("--sampson <px2>", D::opt_sampson);
    help_row("--parallax <deg>", D::opt_parallax);
    help_row("--min-track auto|<n>", D::opt_min_track);
    help_row("--covis-min-angle <deg>", D::opt_covis_min_angle);
    help_row("--max-depth-error auto|<f>|off", D::opt_max_depth_error);
    help_row("--voxel auto|<size>|off", D::opt_voxel);
    help_row("--max-points auto|<n>|off", D::opt_max_points);
    help_row("--max-baseline auto|<d>|off", D::opt_max_baseline);
    help_row("--seed <n>", D::opt_seed);
    help_row("--overwrite", D::opt_overwrite);
    help_row("--force", D::opt_force);
    help_row("--accept-license dinov3,romav2", D::opt_accept_license);
    // English, like every --check: a table of errors for whoever changed the stage.
    std::fprintf(stderr, "    --check [--check-dir <dir>] [--matches <dir>] [--check-noise <px>]\n"
                         "            [--check-outliers <share>] [--check-size <px>] [--check-no-masks]\n"
                         "                                  run the synthetic staircase S-1 and exit\n");
    std::fprintf(stderr, "\n%s --device <index|name|uuid>  --lang <code>\n", D::label_common.get());
}

std::string num(double v, int prec = 3) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*g", prec, v);
    return b;
}

// JSON has no inf or nan.
std::string jnum(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream o;
    o << v;
    return o.str();
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if ((unsigned char)c < 0x20) { o += ' '; continue; }
        o += c;
    }
    return o;
}

}  // namespace

int spirula_densify_main(int argc, char** argv) {
    app::set_program_name(argc > 0 ? argv[0] : nullptr, "spirula densify");
    roma::DensifyJob job;
    roma::DensifyOptions& o = job.opt;
    std::string dataset, model, image_dir = "images", mask_dir = "masks", matches, check_dir;
    bool no_masks = false, check = false;
    roma::CheckOptions check_opt;
    double check_noise = check_opt.noise_px, check_outliers = check_opt.outliers;
    int check_size = check_opt.match_size;
    bool rule_set = false;
    std::string device;
    bool device_set = false;   // an explicit `--device ""` is Auto and beats SS_VK_DEVICE

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s\n", format(D::bad_value, {a, ""}).c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        auto bad = [&](const std::string& v) {
            std::fprintf(stderr, "%s\n", format(D::bad_value, {a, v}).c_str());
            std::exit(2);
        };
        auto real = [&](double lo) {
            const std::string v = next();
            char* end = nullptr;
            const double d = std::strtod(v.c_str(), &end);
            if (v.empty() || *end || !(d >= lo) || !std::isfinite(d)) bad(v);
            return d;
        };
        // auto -> 0, off -> -1, else a positive number.
        auto autoOff = [&]() {
            const std::string v = next();
            if (v == "auto") return 0.0;
            if (v == "off") return -1.0;
            char* end = nullptr;
            const double d = std::strtod(v.c_str(), &end);
            if (v.empty() || *end || !(d > 0)) bad(v);
            return d;
        };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--check") check = true;
        else if (a == "--check-dir") check_dir = next();
        else if (a == "--check-noise") check_noise = real(0);
        else if (a == "--check-outliers") check_outliers = real(0);
        else if (a == "--check-size") check_size = (int)real(16);
        else if (a == "--check-no-masks") check_opt.masks = false;
        else if (a == "--device") { device = next(); device_set = true; }
        else if (a == "--model") model = next();
        else if (a == "--out") job.out_dir = next();
        else if (a == "--image-dir") image_dir = next();
        else if (a == "--mask-dir") mask_dir = next();
        else if (a == "--no-masks") no_masks = true;
        else if (a == "--flip-mask") job.flip_mask = true;
        else if (a == "--matches") matches = next();
        else if (a == "--export-pairs") job.export_dir = next();
        else if (a == "--plugin-exact") o.plugin_exact = true;
        else if (a == "--refs") { o.refs = real(1e-9); }
        else if (a == "--neighbours") o.neighbours = (int)real(1);
        else if (a == "--neighbour-rule") {
            const std::string v = next();
            if (v == "covis") o.neighbour_rule = roma::NeighbourRule::Covis;
            else if (v == "pose") o.neighbour_rule = roma::NeighbourRule::Pose;
            else bad(v);
            rule_set = true;
        }
        else if (a == "--holdout-every") o.holdout_every = (int)real(2);
        else if (a == "--split") {
            const std::string v = next();
            o.split = v == "auto" ? -1 : v == "yes" ? 1 : v == "no" ? 0 : (bad(v), 0);
        }
        else if (a == "--face-pairs") {
            const std::string v = next();
            if (v == "all") o.all_face_pairs = true;
            else if (v != "auto") bad(v);
        }
        else if (a == "--matches-per-ref") o.matches_per_ref = (int)real(1);
        else if (a == "--min-certainty") o.min_certainty = (float)real(0);
        else if (a == "--reproj") o.reproj_px = real(1e-9);
        else if (a == "--sampson") o.sampson_px2 = real(0);
        else if (a == "--parallax") o.min_parallax_deg = real(0);
        else if (a == "--min-track") {
            const std::string v = next();
            if (v == "auto") o.min_track = 0;
            else {
                char* end = nullptr;
                const long n = std::strtol(v.c_str(), &end, 10);
                if (v.empty() || *end || n < 1) bad(v);
                o.min_track = (int)n;
            }
        }
        else if (a == "--covis-min-angle") o.covis_min_angle_deg = real(0);
        else if (a == "--max-depth-error") o.max_depth_error = autoOff();
        else if (a == "--voxel") o.voxel = autoOff();
        else if (a == "--max-points") o.max_points = (int64_t)autoOff();
        else if (a == "--max-baseline") o.max_baseline = autoOff();
        else if (a == "--seed") o.seed = (uint64_t)real(0);
        else if (a == "--overwrite") job.overwrite = true;
        else if (a == "--force") job.force = true;
        else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "%s\n", format(D::unknown_option, {a}).c_str());
            return 2;
        } else if (dataset.empty()) dataset = a;
        else { usage(); return 2; }
    }

    try {
        if (check) {
            roma::CheckOptions co = check_opt;
            co.dir = check_dir;
            co.matches = matches;
            co.keep = !check_dir.empty();
            co.noise_px = check_noise;
            co.outliers = check_outliers;
            co.match_size = check_size;
            return roma::densifyCheck(co);
        }
        if (dataset.empty()) { usage(); return 2; }
        if (o.plugin_exact) {
            // The plugin's own defaults, in its pixels (core/config.py).
            if (!rule_set) o.neighbour_rule = roma::NeighbourRule::Pose;
            auto unset = [&](const char* flag) {
                for (int i = 1; i < argc; i++) if (!std::strcmp(argv[i], flag)) return false;
                return true;
            };
            if (unset("--reproj")) o.reproj_px = 0.8;
            if (unset("--parallax")) o.min_parallax_deg = 0.5;
        }

        job.model_dir = model.empty() ? find_colmap_poses(dataset)
                                      : (fs::path(dataset) / model).string();
        if (job.model_dir.empty() || !fs::exists(fs::path(job.model_dir) / "images.bin"))
            throw std::runtime_error("no binary COLMAP model under " + dataset);
        if (job.out_dir.empty()) {
            job.out_dir = roma::siblingDir(job.model_dir);
        } else if (fs::path(job.out_dir).is_relative()) {
            job.out_dir = (fs::path(dataset) / job.out_dir).string();
        }
        const fs::path img_root = fs::path(image_dir).is_absolute() ? fs::path(image_dir)
                                                                    : fs::path(dataset) / image_dir;
        const fs::path mask_root = fs::path(mask_dir).is_absolute() ? fs::path(mask_dir)
                                                                    : fs::path(dataset) / mask_dir;
        job.image_path = [img_root](const std::string& n) { return (img_root / n).string(); };
        if (!no_masks && fs::is_directory(mask_root))
            job.mask_path = [mask_root](const std::string& n) {
                return dsparse::find_aux_file(mask_root.string(), n, "mask");
            };

        if (job.export_dir.empty() && matches.empty())
            nn::configure_device(spirula::vkselect::requestFrom(device, device_set).text);
        std::unique_ptr<roma::Matcher> matcher;
        if (!matches.empty()) matcher = std::make_unique<roma::DumpMatcher>(matches, 640);
        else if (job.export_dir.empty())
            matcher = std::make_unique<roma::RomaMatcher>(roma::ensure_checkpoint(), roma::Preset::Base);
        job.matcher = matcher.get();
        if (const std::string why = roma::outDirProblem(dataset, job.model_dir, job.out_dir); !why.empty())
            throw std::runtime_error(job.out_dir + ": " + why);
        if (job.export_dir.empty() && fs::exists(job.out_dir) && !job.overwrite) {
            std::fprintf(stderr, "%s\n", format(D::out_exists, {job.out_dir}).c_str());
            return 2;
        }

        const roma::DensifyPlan pl = roma::planDensify(job);
        const roma::DensifyOptions& r = pl.opt;
        std::printf("%s\n", format(D::model, {job.model_dir, (long long)pl.images.size(),
                                              (long long)pl.sparse_points, num(pl.sparse_spacing)})
                                .c_str());
        if (!pl.held_out.empty()) {
            std::string names;
            for (int i : pl.held_out) names += (names.empty() ? "" : " ") + pl.images[(size_t)i].name;
            std::printf("%s\n", format(D::held_out, {(long long)r.holdout_every, names}).c_str());
        }
        if (pl.split) std::printf("%s\n", format(D::faces, {(long long)pl.face_size}).c_str());
        std::printf("%s\n",
                    format(D::selection,
                           {(long long)pl.refs.size(), (long long)(pl.images.size() - pl.held_out.size()),
                            (long long)pl.ref_views.size(), (long long)pl.pairs,
                            (long long)(pl.nbrs.empty() ? 0 : pl.nbrs[0].size()),
                            r.neighbour_rule == roma::NeighbourRule::Covis ? "covis" : "pose"})
                        .c_str());
        if (pl.max_baseline > 0)
            std::printf("%s\n", format(D::baseline, {num(pl.max_baseline)}).c_str());
        std::printf("%s\n", format(D::filters, {(long long)pl.match_size, num(r.reproj_px),
                                                num(r.sampson_px2), num(r.min_parallax_deg),
                                                num(r.plugin_exact ? r.certainty_floor : r.min_certainty),
                                                pl.min_track > 0 ? std::to_string(pl.min_track) : std::string("auto")})
                                .c_str());
        std::printf("%s\n", format(D::budget, {(long long)r.matches_per_ref,
                                               pl.voxel > 0 ? num(pl.voxel) : std::string("off"),
                                               pl.max_points > 0 ? std::to_string(pl.max_points)
                                                                 : std::string("off")})
                                .c_str());
        if (pl.masks_sampled > 0) {
            std::printf("%s\n", format(D::masks, {num(pl.mask_keep), (long long)pl.masks_sampled}).c_str());
            // Under 30% kept is far more often an inverted mask than a capture.
            if ((pl.mask_keep < 0.30 || pl.mask_keep > 0.995) && !job.force) {
                std::fprintf(stderr, "%s\n", format(D::masks_inverted, {num(pl.mask_keep)}).c_str());
                return 2;
            }
        }
        if (job.matcher) std::printf("%s\n", format(D::matcher, {job.matcher->describe()}).c_str());
        job.on_warp_scale = [](int warp, int input) {
            std::printf("%s\n", format(D::warp_scale, {(long long)warp, (long long)input}).c_str());
            std::fflush(stdout);
        };
        std::fflush(stdout);

        int last_pct = -1;
        const roma::DensifyResult res = roma::runDensify(job, pl, [&](int done, int total, int64_t pts) {
            const int pct = (int)(100.0 * done / std::max(1, total));
            if (pct / 5 != last_pct / 5 || done == total) {
                last_pct = pct;
                std::printf("%s\n", format(D::progress, {(long long)done, (long long)total, (long long)pts}).c_str());
                std::fflush(stdout);
            }
        });
        if (!job.export_dir.empty()) {
            std::printf("%s\n", format(D::exported, {job.export_dir, (long long)pl.pairs}).c_str());
            return 0;
        }
        const roma::DensifyStats& st = res.stats;
        std::printf("%s\n", format(D::rejected, {(long long)st.samples, (long long)st.below_certainty,
                                                 (long long)st.outside, (long long)st.sampson,
                                                 (long long)st.reproj, (long long)st.cheirality,
                                                 (long long)st.parallax, (long long)st.short_track,
                                                 (long long)st.voxel_merged})
                                .c_str());

        std::ostringstream js;
        js << "{\n  \"tool\": \"spirula densify\",\n"
           << "  \"source_model\": \"" << jsonEscape(job.model_dir) << "\",\n"
           << "  \"source_images_bin_sha256\": \"" << nn::sha256_file(job.model_dir + "/images.bin") << "\",\n"
           << "  \"source_cameras_bin_sha256\": \"" << nn::sha256_file(job.model_dir + "/cameras.bin") << "\",\n"
           << "  \"matcher\": \"" << jsonEscape(job.matcher->describe()) << "\",\n"
           << "  \"plugin_exact\": " << (r.plugin_exact ? "true" : "false") << ",\n"
           << "  \"images\": " << pl.images.size() << ", \"sparse_points\": " << pl.sparse_points
           << ", \"sparse_spacing\": " << jnum(pl.sparse_spacing) << ",\n"
           << "  \"split\": " << (pl.split ? "true" : "false") << ", \"face_size\": " << pl.face_size
           << ", \"match_size\": " << pl.match_size << ",\n"
           << "  \"refs\": " << pl.refs.size() << ", \"ref_views\": " << pl.ref_views.size()
           << ", \"pairs\": " << pl.pairs << ",\n"
           << "  \"neighbour_rule\": \"" << (r.neighbour_rule == roma::NeighbourRule::Covis ? "covis" : "pose")
           << "\", \"neighbours\": " << r.neighbours << ", \"max_baseline\": " << jnum(pl.max_baseline) << ",\n"
           << "  \"held_out\": [";
        for (size_t i = 0; i < pl.held_out.size(); i++)
            js << (i ? ", " : "") << '"' << jsonEscape(pl.images[(size_t)pl.held_out[i]].name) << '"';
        js << "],\n  \"refs_list\": [";
        for (size_t i = 0; i < pl.refs.size(); i++)
            js << (i ? ", " : "") << '"' << jsonEscape(pl.images[(size_t)pl.refs[i]].name) << '"';
        js << "],\n  \"matches_per_ref\": " << r.matches_per_ref << ", \"min_certainty\": " << jnum(r.min_certainty)
           << ", \"certainty_floor\": " << jnum(r.certainty_floor) << ", \"sample_cap\": " << jnum(r.sample_cap) << ",\n"
           << "  \"reproj_px\": " << jnum(r.reproj_px) << ", \"sampson_px2\": " << r.sampson_px2
           << ", \"min_parallax_deg\": " << jnum(r.min_parallax_deg) << ", \"min_track\": " << (pl.min_track > 0 ? std::to_string(pl.min_track) : std::string("\"auto\""))
           << ", \"covis_min_angle_deg\": " << jnum(r.covis_min_angle_deg)
           << ", \"max_depth_error\": " << jnum(r.max_depth_error)
           << ", \"median_pair_angle_deg\": " << jnum(pl.median_pair_angle_deg)
           << ", \"match_focal\": " << jnum(pl.match_focal) << ",\n"
           << "  \"voxel\": " << jnum(pl.voxel) << ", \"max_points\": " << pl.max_points << ", \"seed\": " << r.seed << ",\n"
           << "  \"mask_keep\": " << jnum(pl.mask_keep) << ", \"flip_mask\": " << (job.flip_mask ? "true" : "false") << ",\n"
           << "  \"stats\": {\"samples\": " << st.samples << ", \"below_certainty\": " << st.below_certainty
           << ", \"outside\": " << st.outside << ", \"sampson\": " << st.sampson << ", \"nonfinite\": " << st.nonfinite
           << ", \"reproj\": " << st.reproj << ", \"cheirality\": " << st.cheirality << ", \"parallax\": " << st.parallax
           << ", \"candidates\": " << st.candidates << ", \"ref_reproj\": " << st.ref_reproj << ", \"fused\": " << st.fused
           << ", \"short_track\": " << st.short_track << ", \"inconsistent\": " << st.inconsistent << ", \"uncertain\": " << st.uncertain
           << ", \"two_image_kept\": " << st.two_image_kept << ", \"two_image_bar\": " << jnum(st.two_image_bar)
           << ", \"voxel_merged\": " << st.voxel_merged << ", \"capped\": " << st.capped << "},\n"
           << "  \"track_hist\": {";
        bool first = true;
        for (const auto& kv : st.track_hist) { js << (first ? "" : ", ") << '"' << kv.first << "\": " << kv.second; first = false; }
        js << "},\n  \"points\": " << res.points << ", \"seconds_total\": " << jnum(res.seconds_total)
           << ", \"seconds_match\": " << jnum(res.seconds_match) << "\n}\n";

        roma::writeSibling(job.model_dir, job.out_dir, pl, res.cloud, js.str());
        std::printf("%s\n", format(D::done, {job.out_dir, (long long)res.points,
                                             spirula::i18n::format_duration(res.seconds_total),
                                             spirula::i18n::format_duration(res.seconds_match)})
                                .c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", format(D::error, {e.what()}).c_str());
        return 1;
    }
}
