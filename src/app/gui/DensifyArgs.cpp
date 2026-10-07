// DensifyArgs.cpp -- the part of DensifyRunner a test links without the engine.

#include "app/gui/DensifyRunner.h"

#include <algorithm>

namespace gui {

std::vector<std::string> densify_args(const DensifyJob& job, const std::string& dataset,
                                      const std::string& images, const std::string& masks,
                                      bool masks_flipped, bool preset_supported) {
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
    num("--refs", job.refs);
    num("--neighbours", job.neighbours);
    if (const int rule = std::clamp(job.rule, 0, 2); rule > 0) {
        a.push_back("--neighbour-rule");
        a.push_back(kDensifyRules[rule]);
    }
    num("--matches-per-ref", job.matches_per_ref);
    num("--max-points", job.max_points);
    num("--min-track", job.min_track);
    if (job.overwrite) a.push_back("--overwrite");
    if (!job.device_uuid.empty()) {
        a.push_back("--device");
        a.push_back(job.device_uuid);
    }
    return a;
}

}  // namespace gui
