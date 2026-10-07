#include "roma/model/Dump.h"

#include "roma/model/Model.h"
#include "roma/model/Weights.h"

#include "core/Env.h"
#include "external/npy.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace roma {
namespace {

struct DumpState {
    const char* dir = nullptr;
    std::string nonce;
    std::map<std::string, int> calls;   // times each name was written
    std::map<std::string, std::string> notes;
    std::vector<std::string> written;   // file stems, in order
    std::mutex mu;
};

// The manifest compare_torch.py requires: the files THIS run wrote (any other
// is stale), how it ended, and the settings it ran with.
void write_manifest(const DumpState& s, int status, bool finished) {
    const std::string path = std::string(s.dir) + "/manifest.json";
    std::FILE* f = std::fopen((path + ".tmp").c_str(), "w");
    if (!f) return;
    // Read from the device, so only a finished manifest asks: a run that dies
    // early, or a test with no GPU, must not start one just to say what it ran with.
    auto flag = [&](bool (*get)()) { return finished ? (get() ? "true" : "false") : "null"; };
    std::fprintf(f, "{\"nonce\": \"%s\", \"finished\": %s, \"exit_status\": %d, "
                 "\"f16_weights\": %s, \"rope_rounds\": %s, \"local_corr_fused\": %s, \"notes\": {",
                 s.nonce.c_str(), finished ? "true" : "false", status, flag(f16_weights),
                 flag(matcher_rope_rounds), flag(local_corr_fused));
    bool first = true;
    for (const auto& [k, v] : s.notes) {
        std::fprintf(f, "%s\"%s\": \"%s\"", first ? "" : ", ", k.c_str(), v.c_str());
        first = false;
    }
    std::fprintf(f, "}, \"files\": [");
    for (size_t i = 0; i < s.written.size(); ++i)
        std::fprintf(f, "%s\"%s\"", i ? ", " : "", s.written[i].c_str());
    std::fprintf(f, "]}\n");
    std::fclose(f);
    std::error_code ec;
    fs::rename(path + ".tmp", path, ec);
}

// The stems a previous run's manifest lists: the only files this directory is
// known to have been given by a dump, so the only ones removed.
std::vector<std::string> listed_stems(const fs::path& manifest) {
    std::vector<std::string> stems;
    std::FILE* f = std::fopen(manifest.string().c_str(), "r");
    if (!f) return stems;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    size_t at = text.find("\"files\": [");
    if (at == std::string::npos) return stems;
    const size_t end = text.find(']', at);
    for (at = text.find('"', at + 9); at != std::string::npos && at < end;) {
        const size_t close = text.find('"', at + 1);
        if (close == std::string::npos || close > end) break;
        stems.push_back(text.substr(at + 1, close - at - 1));
        at = text.find('"', close + 1);
    }
    return stems;
}

// Resolved once. The files a previous manifest lists are removed first, so
// nothing stale can be read as this run's; any other file in the directory stays.
void init(DumpState& d) {
    d.dir = spirula::env("ROMA_DUMP");
    if (!d.dir) return;
    std::error_code ec;
    fs::create_directories(d.dir, ec);
    const fs::path manifest = fs::path(d.dir) / "manifest.json";
    for (const std::string& stem : listed_stems(manifest))
        if (stem.find_first_of("/\\") == std::string::npos) fs::remove(fs::path(d.dir) / (stem + ".npy"), ec);
    fs::remove(manifest, ec);
    std::random_device rd;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%08x%08x-%lld", rd(), rd(),
                  (long long)std::chrono::system_clock::now().time_since_epoch().count());
    d.nonce = buf;
    write_manifest(d, -1, false);
}

DumpState& state() {
    static DumpState s;
    static std::once_flag once;
    std::call_once(once, [] { init(s); });
    return s;
}

}  // namespace

bool dump_enabled() { return state().dir != nullptr; }

void dump_host(const char* name, const float* data, const std::vector<int64_t>& shape) {
    DumpState& s = state();
    if (!s.dir) return;
    std::lock_guard<std::mutex> lock(s.mu);
    // The first call keeps the bare name; a repeat (a second coarse() in one
    // process) is written as name.1, name.2, ... rather than over it.
    const int k = s.calls[name]++;
    const std::string stem = k ? std::string(name) + "." + std::to_string(k) : std::string(name);
    npy::shape_t sh;
    for (int64_t d : shape) sh.push_back((unsigned long)d);
    npy::npy_data_ptr<float> d{data, sh, false};
    npy::write_npy(std::string(s.dir) + "/" + stem + ".npy", d);
    s.written.push_back(stem);
    write_manifest(s, -1, false);
}

void dump_tensor(const char* name, const nn::Tensor& t, const std::vector<int64_t>& shape) {
    if (!state().dir) return;
    std::vector<float> host((size_t)t.numel());
    nn::tensor_to_host(t, host.data(), t.numel());
    dump_host(name, host.data(), shape);
}

void dump_note(const char* key, const std::string& value) {
    DumpState& s = state();
    if (!s.dir) return;
    std::lock_guard<std::mutex> lock(s.mu);
    s.notes[key] = value;
    write_manifest(s, -1, false);
}

void dump_finish(int exit_status) {
    DumpState& s = state();
    if (!s.dir) return;
    std::lock_guard<std::mutex> lock(s.mu);
    write_manifest(s, exit_status, true);
}

}  // namespace roma
