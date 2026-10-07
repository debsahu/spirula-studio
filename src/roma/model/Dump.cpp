#include "roma/model/Dump.h"

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
    std::vector<std::string> written;   // file stems, in order
    std::mutex mu;
};

// The manifest compare_torch.py requires: which files THIS run wrote, and how
// it ended. A file it does not list is stale and must not be read.
void write_manifest(const DumpState& s, int status, bool finished) {
    const std::string path = std::string(s.dir) + "/manifest.json";
    std::FILE* f = std::fopen((path + ".tmp").c_str(), "w");
    if (!f) return;
    std::fprintf(f, "{\"nonce\": \"%s\", \"finished\": %s, \"exit_status\": %d, \"files\": [",
                 s.nonce.c_str(), finished ? "true" : "false", status);
    for (size_t i = 0; i < s.written.size(); ++i)
        std::fprintf(f, "%s\"%s\"", i ? ", " : "", s.written[i].c_str());
    std::fprintf(f, "]}\n");
    std::fclose(f);
    std::error_code ec;
    fs::rename(path + ".tmp", path, ec);
}

// Resolved once. A dump directory left by an earlier run is emptied of its
// .npy files and manifest first, so nothing stale can be read as this run's.
void init(DumpState& d) {
    d.dir = spirula::env("ROMA_DUMP");
    if (!d.dir) return;
    std::error_code ec;
    fs::create_directories(d.dir, ec);
    std::vector<fs::path> stale;
    for (const auto& e : fs::directory_iterator(d.dir, ec))
        if (e.path().extension() == ".npy" || e.path().filename() == "manifest.json")
            stale.push_back(e.path());
    for (const fs::path& p : stale) fs::remove(p, ec);
    std::random_device rd;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%08x%08x-%lld", rd(), rd(),
                  (long long)std::chrono::system_clock::now().time_since_epoch().count());
    d.nonce = buf;
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

void dump_finish(int exit_status) {
    DumpState& s = state();
    if (!s.dir) return;
    std::lock_guard<std::mutex> lock(s.mu);
    write_manifest(s, exit_status, true);
}

}  // namespace roma
