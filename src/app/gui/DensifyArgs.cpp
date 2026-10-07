// DensifyArgs.cpp -- the part of DensifyRunner a test links without the engine.

#include "app/gui/DensifyRunner.h"

#include <algorithm>
#include <filesystem>

namespace gui {

std::vector<std::string> densify_args(const DensifyJob& job, const std::string& dataset,
                                      const std::string& images, const std::string& masks,
                                      bool masks_flipped, bool preset_supported,
                                      const std::string& progress_dir) {
    std::vector<std::string> a = {"densify", dataset,
                                  "--image-dir", images.empty() ? std::string("images") : images};
    if (masks.empty() || !job.use_masks) {
        a.push_back("--no-masks");
    } else {
        a.push_back("--mask-dir");
        a.push_back(masks);
        if (masks_flipped) a.push_back("--flip-mask");
    }
    auto num = [&](const char* flag, int64_t v) {
        if (v <= 0) return;
        a.push_back(flag);
        a.push_back(std::to_string(v));
    };
    if (!job.model.empty()) {
        a.push_back("--model");
        a.push_back(job.model);
    }
    const int preset = std::clamp(job.preset, 0, kNumDensifyPresets - 1);
    if (preset > 0 && preset_supported) {
        a.push_back("--preset");
        a.push_back(kDensifyPresets[preset]);
    }
    if (const int source = std::clamp(job.source, 0, kNumDensifySources - 1); source > 0) {
        a.push_back("--source");
        a.push_back(kDensifySources[source]);
    }
    num("--refs", job.refs);
    num("--neighbours", job.neighbours);
    if (const int rule = std::clamp(job.rule, 0, 2); rule > 0) {
        a.push_back("--neighbour-rule");
        a.push_back(kDensifyRules[rule]);
    }
    num("--matches-per-ref", job.matches_per_ref);
    num("--max-points", job.max_points);
    num("--min-track", job.min_track);
    if (!progress_dir.empty()) {
        a.push_back("--progress-dir");
        a.push_back(progress_dir);
    }
    if (job.overwrite) a.push_back("--overwrite");
    if (!job.device_uuid.empty()) {
        a.push_back("--device");
        a.push_back(job.device_uuid);
    }
    return a;
}

DensifyJob densify_after_settings(const DensifyJob& current, const DensifyJob& incoming,
                                  ModelCarry carry) {
    DensifyJob out = incoming;
    out.model = carry == ModelCarry::Keep ? current.model : std::string();
    return out;
}

int densify_resolved_source(int source) {
    source = std::clamp(source, 0, kNumDensifySources - 1);
    return source == kSourceAuto ? kSourceRoma : source;
}

bool densify_has_depth_maps(const std::string& dataset) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path d = fs::path(dataset) / "depths";
    return fs::is_directory(d, ec) && !fs::is_empty(d, ec);
}

bool densify_needs_roma(const DensifyJob& job) {
    return std::clamp(job.source, 0, kNumDensifySources - 1) != kSourceMoge;
}

bool densify_blocks_run(const DensifyJob& job, bool lidar_run, bool ready) {
    return job.enable && !lidar_run && !ready;
}

}  // namespace gui
