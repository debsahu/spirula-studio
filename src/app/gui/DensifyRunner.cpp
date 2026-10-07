// DensifyRunner.cpp -- see DensifyRunner.h.

#include "app/gui/DensifyRunner.h"

#include "app/AppPaths.h"
#include "app/gui/ReconModels.h"
#include "app/gui/Subprocess.h"
#include "i18n/Locale.h"
#include "i18n/catalog/DenseGui.h"
#include "i18n/catalog/Densify.h"
#include "i18n/catalog/Log.h"

#ifdef SS_TOOL_DENSIFY
#include "app/gui/FetchSource.h"
#include "data/DatasetParser.h"
#include "nn/Device.h"
#include "core/LicenseConsent.h"
#include "nn/io/Fetch.h"
#include "roma/model/Fetch.h"
#endif

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
namespace dg = spirula::i18n::msg::densegui;
namespace dn = spirula::i18n::msg::densify;
namespace lmsg = spirula::i18n::msg::log;
using spirula::i18n::format;

namespace gui {

std::string densify_availability() {
#ifndef SS_TOOL_DENSIFY
    return dg::unavailable.get();
#else
    if (app::exe_path().empty()) return lmsg::err_no_exe_path.get();
    return "";
#endif
}

bool densify_has_flag(const std::string& flag, bool wait) {
    struct Help {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        std::string text;
    };
    // Shared with the worker, which may outlive a process that quits mid-probe.
    static const std::shared_ptr<Help> help = [] {
        auto h = std::make_shared<Help>();
        if (densify_availability().empty())
            std::thread([h] {
                std::string text;
                const std::atomic<bool> never{false};
                run_process({app::exe_path(), "densify", "--help"}, "",
                            [&](const std::string& l) { text += l + "\n"; }, never);
                std::lock_guard<std::mutex> lk(h->mu);
                h->text = std::move(text);
                h->done = true;
                h->cv.notify_all();
            }).detach();
        else
            h->done = true;
        return h;
    }();
    std::unique_lock<std::mutex> lk(help->mu);
    if (wait) help->cv.wait(lk, [] { return help->done; });
    return help->done && help->text.find(flag) != std::string::npos;
}

const std::vector<std::string>& densify_license_families() {
    static const std::vector<std::string> kFamilies = [] {
        std::vector<std::string> out;
#ifdef SS_TOOL_DENSIFY
        roma::register_licenses();
        const std::string list = roma::checkpoint_file().license_family;
        out = spirula::license::split_families(list);
        for (const std::string& f : out) {
            if (f == "dinov3") register_license_info(f.c_str(), &dg::license_dinov3_title, &dg::license_dinov3_summary);
            if (f == "romav2") register_license_info(f.c_str(), &dg::license_romav2_title, &dg::license_romav2_summary);
        }
#endif
        return out;
    }();
    return kFamilies;
}

std::vector<PendingDownload> densify_model_downloads() {
    std::vector<PendingDownload> out;
#ifdef SS_TOOL_DENSIFY
    const nn::FetchFile& f = roma::checkpoint_file();
    if (!file_is_cached(nn::cached_path(f), f.bytes)) out.push_back(pending_download(f));
#endif
    return out;
}

bool densify_model_cached() {
#ifdef SS_TOOL_DENSIFY
    return densify_model_downloads().empty();
#else
    return false;
#endif
}

bool run_densify_step(const DensifyJob& job, const std::string& dataset,
                      const std::string& images, const std::string& masks,
                      bool masks_flipped, RunProgress& prog,
                      const std::atomic<bool>& cancel, std::string& error,
                      const std::string& progress_dir) {
    if (std::string why = densify_availability(); !why.empty()) {
        error = why;
        return false;
    }
    prog.enter(Stage::Densify, dg::stage_dense.get());
#ifdef SS_TOOL_DENSIFY
    {
        const std::string model = job.model.empty() ? find_colmap_poses(dataset)
                                                    : (fs::path(dataset) / job.model).string();
        std::error_code ec;
        if (model.empty() || !fs::exists(fs::path(model) / "images.bin", ec)) {
            error = dg::err_no_model.get();
            return false;
        }
    }
#endif

    std::string model_dir;
#ifdef SS_TOOL_DENSIFY
    model_dir = job.model.empty() ? find_colmap_poses(dataset) : (fs::path(dataset) / job.model).string();
#endif
    DensifyJob j = job;
#ifdef SS_TOOL_DENSIFY
    if (j.device_uuid.empty()) j.device_uuid = nn::configured_device_selector();
#endif
    std::vector<std::string> argv = {app::exe_path(), "--lang",
                                     spirula::i18n::code(spirula::i18n::current())};
    for (std::string& a : densify_args(j, dataset, images, masks, masks_flipped,
                                       densify_has_flag("--preset", true), progress_dir))
        argv.push_back(std::move(a));
    std::string cmd;
    for (const std::string& a : argv) cmd += (cmd.empty() ? "$ " : " ") + a;
    prog.note(cmd, true);

    int rc = run_process(argv, "", [&](const std::string& line) {
        std::vector<std::string> got;
        if (spirula::i18n::scan(dn::progress, line, got) && got.size() >= 3) {
            prog.count(Stage::Densify, std::atoll(got[0].c_str()), std::atoll(got[1].c_str()));
            prog.detail(Stage::Densify, line);
            return;
        }
        prog.note(line, false);
    }, cancel);

    if (rc == kCancelled) {
        error = lmsg::err_cancelled.get();
        return false;
    }
    if (rc == kSpawnFailed) {
        error = format(dg::err_spawn, {argv[0]});
        return false;
    }
    if (rc != 0) {
        error = dg::err_failed.get();
        return false;
    }
    // The child's own output, read back: a cloud that is not the one it recorded is not offered.
    if (!model_dir.empty() &&
        cloud_check((fs::path(model_dir).parent_path() / (fs::path(model_dir).filename().string() + "-roma")).string()) ==
            CloudCheck::Mismatch) {
        error = dg::err_cloud_mismatch.get();
        return false;
    }
    prog.mark(Stage::Densify, StageStatus::Done);
    return true;
}

}  // namespace gui
