// Evaluation tool, not a test: run the native RomaMatcher over the pairs
// `spirula densify --export-pairs` wrote and store each warp as float32 .rwm
// (encoding 0), for comparison with PyTorch's dump of the same bytes.
//
//   roma_match_pairs <export_dir> <out_dir> [--preset base] [--checkpoint PATH]
//       [--every N] [--limit N] [--shuffle-seed S] [--csv FILE] [--resume]
//
// <export_dir>/pairs.txt: one "A B" per line; views/<name>.png the 640 px masked
// views. Output <out_dir>/<A>__<B>.rwm plus run.json (preset, checkpoint SHA-256,
// model source digest): a rerun into a directory whose run.json differs is refused
// unless --resume says to keep its warps.

#include "roma/Roma.h"
#include "roma/RomaIdentity.h"
#include "roma/model/Fetch.h"
#include "roma/model/RomaMatcher.h"

#include "core/Env.h"
#include "nn/core/Log.h"
#include "nn/io/Image.h"
#include "nn/vk/EmbeddedSpirv.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

NN_DECLARE_EMBEDDED_MODULES(roma)

namespace fs = std::filesystem;

static double rss_mb() {
#if defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &n) == KERN_SUCCESS)
        return (double)info.phys_footprint / 1048576.0;
#endif
    return 0;
}

static void write_rwm(const fs::path& path, const roma::Warp& w) {
    const fs::path tmp = path.string() + ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        const int32_t hdr[3] = {w.width, w.height, 0};
        f.write("RWM1", 4);
        f.write(reinterpret_cast<const char*>(hdr), sizeof hdr);
        f.write(reinterpret_cast<const char*>(w.warp.data()),
                (std::streamsize)(w.warp.size() * sizeof(float)));
        f.write(reinterpret_cast<const char*>(w.certainty.data()),
                (std::streamsize)(w.certainty.size() * sizeof(float)));
        if (!f) throw std::runtime_error("write failed: " + tmp.string());
    }
    fs::rename(tmp, path);
}

int main(int argc, char** argv) {
    if (!spirula::env("NN_LOG")) nn::set_log_level(2);
    std::string exp, out, ckpt, preset = "base", csv;
    long every = 1, limit = 0;
    long long shuffle_seed = -1;
    bool resume = false;
    int pos = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (s == "--preset") preset = next();
        else if (s == "--checkpoint") ckpt = next();
        else if (s == "--every") every = std::atol(next().c_str());
        else if (s == "--limit") limit = std::atol(next().c_str());
        else if (s == "--shuffle-seed") shuffle_seed = std::atoll(next().c_str());
        else if (s == "--csv") csv = next();
        else if (s == "--resume") resume = true;
        else if (!s.empty() && s[0] == '-') { std::fprintf(stderr, "unknown %s\n", s.c_str()); return 2; }
        else if (pos == 0) { exp = s; pos++; }
        else if (pos == 1) { out = s; pos++; }
    }
    if (exp.empty() || out.empty() || every < 1) {
        std::fprintf(stderr, "usage: roma_match_pairs <export_dir> <out_dir> [--preset p] [--checkpoint f] [--resume]\n");
        return 2;
    }
    try {
        roma::Preset pr;
        if (!roma::parse_preset(preset, pr)) throw std::runtime_error("bad preset " + preset);
        if (ckpt.empty()) ckpt = nn::cached_path(roma::checkpoint_file());
        NN_ENSURE_EMBEDDED_MODULES(roma);

        std::vector<std::pair<std::string, std::string>> pairs;
        {
            std::ifstream f(fs::path(exp) / "pairs.txt");
            std::string line;
            while (std::getline(f, line)) {
                std::istringstream is(line);
                std::string a, b;
                if (is >> a >> b) pairs.emplace_back(a, b);
            }
        }
        if (pairs.empty()) throw std::runtime_error("no pairs in " + exp + "/pairs.txt");
        if (every > 1) {
            std::vector<std::pair<std::string, std::string>> p2;
            for (size_t i = 0; i < pairs.size(); i += (size_t)every) p2.push_back(pairs[i]);
            pairs.swap(p2);
        }
        if (shuffle_seed >= 0) {
            std::mt19937_64 g((uint64_t)shuffle_seed);
            std::shuffle(pairs.begin(), pairs.end(), g);
            if (limit > 0 && (size_t)limit < pairs.size()) pairs.resize((size_t)limit);
            std::stable_sort(pairs.begin(), pairs.end(),
                             [](const auto& x, const auto& y) { return x.first < y.first; });
        } else if (limit > 0 && (size_t)limit < pairs.size()) {
            pairs.resize((size_t)limit);
        }
        fs::create_directories(out);

        // The warps already in <out_dir> came from some run; keep them only if it was this one.
        const std::string identity = "{\"preset\": \"" + preset + "\", \"checkpoint_sha256\": \"" +
                                     nn::sha256_file(ckpt) + "\", \"model_digest\": \"" +
                                     roma::modelSourceDigest() + "\"}\n";
        const fs::path run_json = fs::path(out) / "run.json";
        {
            std::ifstream f(run_json);
            const std::string old((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            bool has_warps = false;
            for (const auto& e : fs::directory_iterator(out))
                has_warps = has_warps || e.path().extension() == ".rwm";
            if (!old.empty() && old != identity && !resume)
                throw std::runtime_error("refusing to resume into " + out + ": run.json is from a different run\n  was " +
                                         old + "  now " + identity + "pass --resume to keep those warps anyway");
            if (old.empty() && has_warps && !resume)
                throw std::runtime_error("refusing to resume into " + out +
                                         ": it holds warps but no run.json; pass --resume to keep them");
            if (old.empty()) {
                std::ofstream w(run_json, std::ios::trunc);
                w << identity;
                if (!w) throw std::runtime_error("cannot write " + run_json.string());
            }
        }

        roma::RomaMatcher rm(ckpt, pr);
        const int S = rm.inputSize();
        std::printf("preset %s input %d, %zu pairs, checkpoint %s\n", preset.c_str(), S,
                    pairs.size(), ckpt.c_str());

        std::map<std::string, nn::Image> cache;   // views are small; 708 x 1.2 MB at most
        auto view = [&](const std::string& n) -> const nn::Image& {
            auto it = cache.find(n);
            if (it != cache.end()) return it->second;
            nn::Image im = nn::load_image((fs::path(exp) / "views" / (n + ".png")).string());
            if (im.empty() || im.channels != 3) throw std::runtime_error("cannot read view " + n);
            if (im.width != S || im.height != S)
                throw std::runtime_error("view " + n + " is not " + std::to_string(S) + " px square");
            return cache.emplace(n, std::move(im)).first->second;
        };

        std::ofstream csvf;
        if (!csv.empty()) {
            csvf.open(csv, std::ios::trunc);
            csvf << "pair,seconds,footprint_mb\n";
        }
        const auto t0 = std::chrono::steady_clock::now();
        double peak = 0, sum_s = 0;
        size_t done = 0, skipped = 0;
        for (const auto& [a, b] : pairs) {
            const fs::path dst = fs::path(out) / (a + "__" + b + ".rwm");
            if (fs::exists(dst)) { ++skipped; continue; }
            const nn::Image& ia = view(a);
            const nn::Image& ib = view(b);
            const roma::MatchImage ma{a, ia.width, ia.height, ia.data.data()};
            const roma::MatchImage mb{b, ib.width, ib.height, ib.data.data()};
            const auto p0 = std::chrono::steady_clock::now();
            roma::Warp w = rm.match(ma, mb);
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - p0).count();
            if (w.width <= 0 || w.height <= 0 || w.certainty.size() != (size_t)w.width * w.height ||
                w.warp.size() != 2 * w.certainty.size())
                throw std::runtime_error("impossible warp for " + a + " " + b);
            for (float v : w.warp) if (!std::isfinite(v)) throw std::runtime_error("non-finite warp " + a + "__" + b);
            for (float v : w.certainty) if (!std::isfinite(v)) throw std::runtime_error("non-finite certainty " + a + "__" + b);
            write_rwm(dst, w);
            const double fp = rss_mb();
            peak = std::max(peak, fp);
            sum_s += s;
            ++done;
            if (csvf) csvf << a << "__" << b << "," << s << "," << fp << "\n";
            if (done % 50 == 0 || done == 1)
                std::printf("%zu/%zu pairs, %.2f s/pair, footprint %.0f MB\n", done, pairs.size(),
                            sum_s / (double)done, fp);
        }
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("done: %zu matched, %zu already present, wall %.0f s, matcher %.3f s/pair, peak footprint %.0f MB\n",
                    done, skipped, wall, done ? sum_s / (double)done : 0.0, peak);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
    return 0;
}
