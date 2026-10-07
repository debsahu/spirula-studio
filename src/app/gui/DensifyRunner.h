#pragma once

// The dense-points step of a dataset run: `spirula densify` over the model the
// reconstruction just wrote, or over one that was already there. A child
// process for the reasons SfmRunner gives, and a free function so both runners
// share one argument list (GeometryRunner.h does the same).

#include "app/gui/ModelCache.h"
#include "app/gui/PrepProgress.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace gui {

// What `--preset` spells, in the order the combo lists them; index 0 sends nothing.
inline constexpr const char* kDensifyPresets[] = {"auto", "turbo", "fast", "base", "high", "precise"};
inline constexpr int kNumDensifyPresets = 6;
// `--neighbour-rule`; index 0 sends nothing.
inline constexpr const char* kDensifyRules[] = {"auto", "covis", "pose"};
// `--source`, in the order the combo lists them; index 0 sends nothing.
inline constexpr const char* kDensifySources[] = {"auto", "roma", "moge", "hybrid"};
inline constexpr int kNumDensifySources = 4;
inline constexpr int kSourceAuto = 0, kSourceRoma = 1, kSourceMoge = 2, kSourceHybrid = 3;

// Every number is 0 for "let the tool choose", which is also what the CLI does.
struct DensifyJob {
    bool enable = false;
    std::string device_uuid;
    std::string model;          // "" auto, else relative to the dataset ("sparse/0")
    int preset = 0;
    int refs = 0;
    int neighbours = 0;
    int rule = 0;
    int matches_per_ref = 0;
    int max_points = 0;
    int min_track = 0;
    int source = kSourceAuto;   // kDensifySources
    bool use_masks = true;
    bool overwrite = false;     // set by the plan, never by the user
};

// The arguments after `spirula --lang <x>`. `masks` empty means no mask folder.
std::vector<std::string> densify_args(const DensifyJob& job, const std::string& dataset,
                                      const std::string& images, const std::string& masks,
                                      bool masks_flipped, bool preset_supported,
                                      const std::string& progress_dir = {});

// Auto as the CLI resolves it: hybrid when the dataset has depth maps, else roma.
// A fixed source resolves to itself.
int densify_resolved_source(int source, bool have_depth_maps);
// Does `depths/` of this dataset hold anything?
bool densify_has_depth_maps(const std::string& dataset);
// RoMa and its licences are needed by every source but moge; auto is roma or
// hybrid, so it needs them whatever the dataset holds.
bool densify_needs_roma(const DensifyJob& job);

// What a new settings block does to the model the screen is on. A preset leaves
// it (it names one capture's folder); a batch row starts from the tool's own pick.
enum class ModelCarry { Keep, Reset };
DensifyJob densify_after_settings(const DensifyJob& current, const DensifyJob& incoming,
                                  ModelCarry carry);

// Does the checkpoint stand between the user and Run? Not when the step is off
// or a laser scan's run, which plan_job never gives the step.
bool densify_blocks_run(const DensifyJob& job, bool lidar_run, bool ready);

// "" when this build has the step, otherwise why not.
std::string densify_availability();
// Whether `spirula densify --help` lists `flag`; asked once per process.
bool densify_has_flag(const std::string& flag);

// Both licence families the checkpoint's one file carries, in the order shown.
const std::vector<std::string>& densify_license_families();
bool densify_model_cached();
std::vector<PendingDownload> densify_model_downloads();

// `progress_dir`: where the child may write model.bin snapshots of the growing
// cloud, for the screen's model view. After the child exits the cloud it wrote
// is checked against the checksums densify.json recorded.
bool run_densify_step(const DensifyJob& job, const std::string& dataset,
                      const std::string& images, const std::string& masks,
                      bool masks_flipped, RunProgress& prog,
                      const std::atomic<bool>& cancel, std::string& error,
                      const std::string& progress_dir = {});

}  // namespace gui
