// `spirula densify` -- a dense point cloud for a solved COLMAP model, from
// dense matches between its images, written as a sibling model so nothing the
// dataset already has is touched. src/roma/DensifyRun.h does the work;
// docs/notes/densify.md is the design and what each setting is chosen from.

#include "app/Tools.h"

#include "app/AppPaths.h"
#include "data/CameraMath.h"

#include "core/VulkanDeviceSelection.h"
#include "data/DatasetParser.h"
#include "i18n/Locale.h"
#include "i18n/TimeFormat.h"
#include "i18n/catalog/Densify.h"
#include "nn/Device.h"
#include "nn/io/Fetch.h"
#include "roma/DensifyCheck.h"
#include "roma/DensifyRun.h"
#include "roma/DepthSource.h"
#include "roma/DumpMatcher.h"
#include "roma/RomaIdentity.h"
#include "roma/WarpCache.h"
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
    help_row("--source auto|roma|moge|hybrid", D::opt_source);
    help_row("--depth-dir <dir>", D::opt_depth_dir);
    help_row("--normal-dir <dir>", D::opt_normal_dir);
    help_row("--normal-check <deg>|off", D::opt_normal_check);
    help_row("--depth-tol auto|<f>", D::opt_depth_tol);
    help_row("--depth-min-agree <n>", D::opt_depth_min_agree);
    help_row("--no-depth-vote", D::opt_no_depth_vote);
    help_row("--min-depth-share <f>", D::opt_min_depth_share);
    help_row("--depth-fit-holdout <n>", D::opt_depth_fit_holdout);
    help_row("--preset turbo|fast|base|high|precise", D::opt_preset);
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
    help_row("--cache auto|off|<dir>", D::opt_cache);
    help_row("--cache-budget auto|<size>", D::opt_cache_budget);
    help_row("--clear-cache", D::opt_clear_cache);
    help_row("--accept-license dinov3,romav2", D::opt_accept_license);
    // English, like every --check: a table of errors for whoever changed the stage.
    std::fprintf(stderr, "    --check [--check-dir <dir>] [--matches <dir>] [--check-noise <px>]\n"
                         "            [--check-outliers <share>] [--check-size <px>] [--check-no-masks]\n"
                         "            [--check-source roma|moge|hybrid]\n"
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

[[noreturn]] void bad_flag(const char* flag, const std::string& v) {
    std::fprintf(stderr, "%s\n", format(D::bad_value, {flag, v}).c_str());
    std::exit(2);
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
    roma::Preset preset = roma::Preset::Base;
    std::string depth_dir = "depths", normal_dir = "normals";
    bool device_set = false;   // an explicit `--device ""` is Auto and beats SS_VK_DEVICE
    std::string cache_arg = "auto", cache_budget_arg = "auto";
    bool clear_cache = false;

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
        else if (a == "--check-source") check_opt.source = next();
        else if (a == "--device") { device = next(); device_set = true; }
        else if (a == "--source") {
            const std::string v = next();
            if (v == "auto") o.source = roma::DensifySource::Auto;
            else if (v == "roma") o.source = roma::DensifySource::Roma;
            else if (v == "moge") o.source = roma::DensifySource::Depth;
            else if (v == "hybrid") o.source = roma::DensifySource::Hybrid;
            else bad(v);
        }
        else if (a == "--depth-dir") depth_dir = next();
        else if (a == "--normal-dir") normal_dir = next();
        else if (a == "--depth-tol") {
            const double v = autoOff();
            if (v < 0) bad("off");
            o.depth_tol = v;
        }
        else if (a == "--depth-min-agree") o.depth_min_agree = (int)real(1);
        else if (a == "--no-depth-vote") o.depth_agreement = false;
        else if (a == "--min-depth-share") o.min_depth_share = real(0);
        else if (a == "--depth-fit-holdout") o.depth_fit_holdout = (int)real(2);
        else if (a == "--normal-check") {
            const double v = autoOff();
            if (v == 0) bad("auto");
            o.depth_normal_check = o.hybrid_normal_check = v > 0;
            if (v > 0) o.depth_normal_deg = v;
        }
        else if (a == "--preset") {
            const std::string v = next();
            if (!roma::parse_preset(v, preset)) bad(v);
        }
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
        else if (a == "--cache") cache_arg = next();
        else if (a == "--cache-budget") cache_budget_arg = next();
        else if (a == "--clear-cache") clear_cache = true;
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
        // The depth source: the dataset's `spirula geometry` maps; the missing
        // ones are made here when its own folder is in use (D::computing_depths).
        const fs::path depth_root = fs::path(depth_dir).is_absolute() ? fs::path(depth_dir)
                                                                      : fs::path(dataset) / depth_dir;
        const fs::path normal_root = fs::path(normal_dir).is_absolute() ? fs::path(normal_dir)
                                                                        : fs::path(dataset) / normal_dir;
        auto have_depth = [&] {
            std::error_code ec;
            return fs::is_directory(depth_root, ec) && !fs::is_empty(depth_root, ec);
        };
        const bool want_depth = o.source == roma::DensifySource::Depth || o.source == roma::DensifySource::Hybrid;
        std::unique_ptr<roma::DepthFiles> depth;
        if ((have_depth() || want_depth) && o.source != roma::DensifySource::Roma && job.export_dir.empty()) {
            const std::string root = depth_root.string(), nroot = normal_root.string();
            depth = std::make_unique<roma::DepthFiles>(
                [root](const std::string& n) { return dsparse::find_aux_file(root, n, "depth"); },
                // `spirula geometry` writes ray depth exactly where it splits the
                // lens into faces; the trainer asks the same function.
                [](const roma::SourceImage& im) {
                    const sfm::Camera& c = im.cam;
                    const int model = c.isSpherical() ? 3 : c.isFisheye() ? 1 : 0;
                    return camhost::splits_to_pinhole_faces(model, c.width, c.height, c.fx, c.fy);
                },
                root, [nroot](const std::string& n) { return dsparse::find_aux_file(nroot, n, "normal"); },
                job.image_path);
            job.depth = depth.get();
        }

        std::unique_ptr<roma::Matcher> matcher;
        const roma::PresetSpec& spec = roma::preset_spec(preset);
        bool is_roma = false;
        if (!matches.empty()) matcher = std::make_unique<roma::DumpMatcher>(matches, spec.hr ? spec.hr : spec.lr);
        else if (job.export_dir.empty() && o.source != roma::DensifySource::Depth) {
            // The licence gate is inside ensure_checkpoint: nothing below runs without it.
            matcher = std::make_unique<roma::RomaMatcher>(roma::ensure_checkpoint(), preset);
            is_roma = true;
        }
        job.matcher = matcher.get();
        uint64_t cache_budget = roma::kDefaultCacheBudget;
        if (cache_budget_arg != "auto" && !roma::parseByteSize(cache_budget_arg, &cache_budget)) bad_flag("--cache-budget", cache_budget_arg);
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
        const char* src_name = pl.source == roma::DensifySource::Roma ? "roma"
                             : pl.source == roma::DensifySource::Depth ? "moge" : "hybrid";
        std::printf("%s\n", format(D::source, {src_name}).c_str());
        if (job.matcher && pl.source != roma::DensifySource::Depth)
            std::printf("%s\n", format(D::matcher, {job.matcher->describe()}).c_str());
        if (pl.max_fill >= 0)
            std::printf("%s\n", format(D::fill_budget, {(long long)pl.max_fill, (long long)pl.max_points}).c_str());
        struct NormalTally {
            int file = 0, depth = 0, other_convention = 0;
            std::vector<std::pair<std::string, double>> checked;   // per normal map: median cosine
        } normals;
        roma::DepthInventory inventory;
        if (job.depth && pl.source != roma::DensifySource::Roma) {
            std::printf("%s\n", format(D::depth_maps, {job.depth->describe()}).c_str());
            std::vector<std::string> names;
            for (const roma::SourceImage& im : pl.images) names.push_back(im.name);
            const std::string root = depth_root.string();
            // Only into geometry's own folder (a sky-blanked copy cannot be
            // extended), and only when a depth source was asked for by name.
            const bool own = fs::path(depth_dir).lexically_normal() == fs::path("depths");
            const bool asked = o.source != roma::DensifySource::Auto;
            std::function<void()> compute;
            if (own && asked)
                compute = [&] {
                    std::printf("%s\n", format(D::computing_depths, {root}).c_str());
                    std::fflush(stdout);
                    std::string cmd = "\"" + app::exe_path() + "\" geometry \"" + dataset + "\" --depth --no-normal";
                    if (image_dir != "images") cmd += " --image-dir \"" + image_dir + "\"";
                    if (device_set) cmd += " --device \"" + device + "\"";
                    if (std::system(cmd.c_str()) != 0) throw std::runtime_error("spirula geometry failed");
                };
            const roma::DepthInventory inv = inventory = roma::ensureDepths(
                names, [root](const std::string& n) { return dsparse::find_aux_file(root, n, "depth"); }, compute);
            std::printf("%s\n", format(D::depth_inventory, {root, (long long)inv.reused, (long long)inv.computed,
                                                            (long long)inv.missing}).c_str());
            if (inv.computed > 0) depth->rereadRecord();
            if (inv.missing > 0 && !asked)
                std::printf("%s\n", format(D::depth_not_computed, {(long long)inv.missing}).c_str());
            if (depth->recorded() > 0)
                std::printf("%s\n", format(D::depth_records, {(long long)depth->recorded()}).c_str());
            job.on_depth_fit = [&normals](const roma::SourceImage& im, const roma::DepthField& f) {
                if (!f.ok) {
                    std::printf("%s\n", format(D::depth_refused, {im.name, f.refused}).c_str());
                    return;
                }
                if (f.normal_from == roma::NormalFrom::File) normals.file++;
                else normals.depth++;
                if (std::isfinite(f.normal_file_cos)) normals.checked.push_back({im.name, f.normal_file_cos});
                if (std::isfinite(f.normal_file_cos) && f.normal_from != roma::NormalFrom::File) {
                    normals.other_convention++;
                    std::printf("%s\n", format(D::normal_refused, {im.name, num(f.normal_file_cos)}).c_str());
                }
            };
        }
        job.on_warp_scale = [](int warp, int input) {
            std::printf("%s\n", format(D::warp_scale, {(long long)warp, (long long)input}).c_str());
            std::fflush(stdout);
        };
        std::fflush(stdout);

        std::unique_ptr<roma::WarpCache> warp_cache;
        std::unique_ptr<roma::CachedMatcher> cached;
        if (is_roma && cache_arg != "off") {
            roma::WarpCacheOptions co;
            co.dir = cache_arg == "auto" ? (fs::path(dataset) / "densify_cache").string()
                     : fs::path(cache_arg).is_relative() ? (fs::path(dataset) / cache_arg).string() : cache_arg;
            co.budget_bytes = cache_budget;
            try {
                warp_cache = std::make_unique<roma::WarpCache>(co);
                // ensure_checkpoint has just hashed the file against this digest.
                const roma::RomaSettings rs = roma::romaSettings(spec, roma::checkpoint_file().sha256);
                cached = std::make_unique<roma::CachedMatcher>(*matcher, *warp_cache, roma::romaIdentity(rs));
                job.matcher = cached.get();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s\n", format(D::cache_unusable, {co.dir, e.what()}).c_str());
                warp_cache.reset();
            }
        }
        if (warp_cache) {
            if (clear_cache) {
                const roma::ClearResult cr = warp_cache->clear();
                std::printf("%s\n", format(D::cache_cleared, {(long long)cr.entries, roma::formatByteSize(cr.bytes)}).c_str());
            }
            const roma::WarpCacheInfo ci = warp_cache->info();
            std::printf("%s\n", format(D::cache_on, {warp_cache->dir(), roma::formatByteSize(warp_cache->budget()),
                                                      roma::formatByteSize(ci.bytes), (long long)ci.entries}).c_str());
        } else if (is_roma && cache_arg == "off") {
            std::printf("%s\n", D::cache_off.get());
        } else if (!is_roma) {
            std::printf("%s\n", D::cache_unused.get());
        }
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
        if (!res.depth_dropped.empty())
            std::fprintf(stderr, "%s\n", format(D::depth_dropped, {res.depth_dropped}).c_str());
        if (pl.source != roma::DensifySource::Roma)
            std::printf("%s\n", format(D::depth_stats, {(long long)st.depth_samples, (long long)st.depth_nodata,
                                                         (long long)st.depth_disagree, (long long)st.depth_through,
                                                         (long long)st.depth_local, (long long)st.depth_kept,
                                                         num(res.depth_tol)}).c_str());
        if (pl.source != roma::DensifySource::Roma) {
            std::printf("%s\n", format(D::normals_from, {(long long)normals.file, normal_root.string(),
                                                          (long long)normals.depth,
                                                          (long long)normals.other_convention}).c_str());
            std::printf("%s\n", format(D::normal_stats, {o.depth_normal_check ? num(o.depth_normal_deg) : std::string("off"),
                                                          (long long)st.depth_normal,
                                                          (long long)st.depth_local_normal}).c_str());
        }
        std::printf("%s\n", format(D::rejected, {(long long)st.samples, (long long)st.below_certainty,
                                                 (long long)st.outside, (long long)st.sampson,
                                                 (long long)st.reproj, (long long)st.cheirality,
                                                 (long long)st.parallax, (long long)st.short_track,
                                                 (long long)st.voxel_merged})
                                .c_str());

        std::string cache_json = "{\"enabled\": false}";
        if (cached) {
            const roma::CachedStats& cs = cached->stats();
            const roma::WarpCacheInfo ci = warp_cache->info();
            const int64_t lookups = cs.hits + cs.misses;
            const double rate = lookups ? 100.0 * (double)cs.hits / (double)lookups : 0.0;
            const double saved = std::max(0.0, cs.seconds_saved);
            std::printf("%s\n", format(D::cache_summary, {(long long)cs.hits, (long long)cs.misses, num(rate, 3) + " %",
                                                         spirula::i18n::format_duration(saved),
                                                         roma::formatByteSize(ci.written_bytes),
                                                         (long long)ci.evicted}).c_str());
            if (cs.corrupt || cs.write_failed)
                std::printf("%s\n", format(D::cache_trouble, {(long long)cs.corrupt, (long long)cs.write_failed}).c_str());
            std::ostringstream cj;
            cj << "{\"enabled\": true, \"dir\": \"" << jsonEscape(warp_cache->dir()) << "\", \"budget_bytes\": "
               << warp_cache->budget() << ", \"hits\": " << cs.hits << ", \"misses\": " << cs.misses
               << ", \"hit_rate\": " << jnum(rate / 100.0) << ", \"corrupt\": " << cs.corrupt
               << ", \"uncacheable\": " << cs.uncacheable << ", \"write_failed\": " << cs.write_failed
               << ", \"seconds_matched\": " << jnum(cs.seconds_matched) << ", \"seconds_saved\": " << jnum(saved)
               << ", \"written_bytes\": " << ci.written_bytes << ", \"evicted\": " << ci.evicted
               << ", \"entries\": " << ci.entries << ", \"bytes\": " << ci.bytes << "}";
            cache_json = cj.str();
        }

        std::ostringstream js;
        js << "{\n  \"tool\": \"spirula densify\",\n"
           << "  \"source_model\": \"" << jsonEscape(job.model_dir) << "\",\n"
           << "  \"source_images_bin_sha256\": \"" << nn::sha256_file(job.model_dir + "/images.bin") << "\",\n"
           << "  \"source_cameras_bin_sha256\": \"" << nn::sha256_file(job.model_dir + "/cameras.bin") << "\",\n"
           << "  \"matcher\": \"" << jsonEscape(job.matcher ? job.matcher->describe() : std::string()) << "\",\n"
           << "  \"source\": \"" << src_name << "\", \"depth\": \"" << jsonEscape(job.depth ? job.depth->describe() : std::string()) << "\",\n"
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
           << "  \"voxel\": " << jnum(pl.voxel) << ", \"max_points\": " << pl.max_points << ", \"max_fill\": " << pl.max_fill << ", \"seed\": " << r.seed << ",\n"
           << "  \"mask_keep\": " << jnum(pl.mask_keep) << ", \"flip_mask\": " << (job.flip_mask ? "true" : "false") << ",\n"
           << "  \"stats\": {\"samples\": " << st.samples << ", \"below_certainty\": " << st.below_certainty
           << ", \"outside\": " << st.outside << ", \"sampson\": " << st.sampson << ", \"nonfinite\": " << st.nonfinite
           << ", \"reproj\": " << st.reproj << ", \"cheirality\": " << st.cheirality << ", \"parallax\": " << st.parallax
           << ", \"candidates\": " << st.candidates << ", \"ref_reproj\": " << st.ref_reproj << ", \"fused\": " << st.fused
           << ", \"short_track\": " << st.short_track << ", \"inconsistent\": " << st.inconsistent << ", \"uncertain\": " << st.uncertain
           << ", \"depth_samples\": " << st.depth_samples << ", \"depth_nodata\": " << st.depth_nodata
           << ", \"depth_disagree\": " << st.depth_disagree << ", \"depth_through\": " << st.depth_through
           << ", \"depth_local\": " << st.depth_local << ", \"depth_left_to_matches\": " << st.depth_left_to_matches
           << ", \"depth_kept\": " << st.depth_kept << ", \"depth_edge\": " << st.depth_edge << ", \"depth_tol\": " << jnum(res.depth_tol)
           << ", \"depth_normal\": " << st.depth_normal << ", \"depth_local_normal\": " << st.depth_local_normal
           << ", \"depth_vote_close\": " << st.depth_vote_close << ", \"fill_near_matches\": " << st.fill_near_matches
           << ", \"depth_share\": " << jnum(res.depth_share) << ", \"depth_fit_holdout\": " << o.depth_fit_holdout << ", \"depth_dropped\": \"" << jsonEscape(res.depth_dropped) << "\""
           << ", \"two_image_kept\": " << st.two_image_kept << ", \"seen_through\": " << st.seen_through << ", \"two_image_bar\": " << jnum(st.two_image_bar)
           << ", \"voxel_merged\": " << st.voxel_merged << ", \"capped\": " << st.capped << "},\n"
           << "  \"track_hist\": {";
        bool first = true;
        for (const auto& kv : st.track_hist) { js << (first ? "" : ", ") << '"' << kv.first << "\": " << kv.second; first = false; }
        js << "},\n  \"normals\": {\"normal_check_deg\": " << (o.depth_normal_check ? jnum(o.depth_normal_deg) : std::string("null"))
           << ", \"from_file\": " << normals.file << ", \"from_depth\": " << normals.depth
           << ", \"other_convention\": " << normals.other_convention << ", \"agree_5deg\": [";
        for (size_t i = 0; i < st.normal_agree_hist.size(); i++) js << (i ? ", " : "") << st.normal_agree_hist[i];
        js << "], \"null_5deg\": [";
        for (size_t i = 0; i < st.normal_null_hist.size(); i++) js << (i ? ", " : "") << st.normal_null_hist[i];
        js << "], \"hybrid_fill_vs_plane_5deg\": [";
        for (size_t i = 0; i < st.local_normal_hist.size(); i++) js << (i ? ", " : "") << st.local_normal_hist[i];
        js << "], \"map_cosine\": {";
        for (size_t i = 0; i < normals.checked.size(); i++)
            js << (i ? ", " : "") << '"' << jsonEscape(normals.checked[i].first) << "\": " << jnum(normals.checked[i].second);
        js << "}},\n  \"depth_maps\": {\"reused\": " << inventory.reused << ", \"computed\": " << inventory.computed
           << ", \"missing\": " << inventory.missing;
        js << "},\n  \"warp_cache\": " << cache_json << ",\n  \"points\": " << res.points << ", \"seconds_total\": " << jnum(res.seconds_total)
           << ", \"seconds_match\": " << jnum(res.seconds_match) << "\n}\n";

        if (res.cloud.empty()) {
            std::fprintf(stderr, "%s\n", format(D::empty_result, {job.out_dir}).c_str());
            // An older sibling under the same name would be trained from as if it were this run's.
            std::error_code ec;
            if (job.overwrite && fs::exists(job.out_dir, ec)) {
                fs::remove_all(job.out_dir, ec);
                std::fprintf(stderr, "%s\n", format(D::stale_removed, {job.out_dir}).c_str());
            }
            return 1;
        }
        const roma::ReprojStats rp = roma::writeSibling(job.model_dir, job.out_dir, pl, res.cloud, js.str());
        std::printf("%s\n", format(D::reprojection, {(long long)rp.observations, num(rp.mean_px), num(rp.p95_px),
                                                     (long long)rp.invalid}).c_str());
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
