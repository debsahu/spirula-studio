#include "roma/Publish.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <cerrno>
#include <unistd.h>
#endif

namespace roma {

namespace fs = std::filesystem;

namespace {

std::atomic<PublishProbe> g_probe{nullptr};

void probe(const char* point) {
    if (PublishProbe p = g_probe.load()) p(point);
}

long selfPid() {
#ifdef _WIN32
    return (long)GetCurrentProcessId();
#else
    return (long)getpid();
#endif
}

bool alive(long pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return running;
#else
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
#endif
}

long readPid(const std::string& file) {
    std::FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return -1;
    long pid = 0;
    const int got = std::fscanf(f, "%ld", &pid);
    std::fclose(f);
    return got == 1 ? pid : 0;
}

std::string lockKey(const std::string& model_dir) {
    fs::path p = fs::absolute(fs::path(model_dir)).lexically_normal();
    if (!p.has_filename()) p = p.parent_path();
    return p.string();
}

std::mutex g_mu;
std::map<std::string, int> g_held;

}  // namespace

std::string WriterLock::pathOf(const std::string& model_dir) {
    const fs::path p(lockKey(model_dir));
    return (p.parent_path() / ("." + p.filename().string() + ".lock")).string();
}

long WriterLock::holder(const std::string& model_dir) {
    const long pid = readPid(pathOf(model_dir));
    return alive(pid) ? pid : 0;
}

WriterLock::WriterLock(const std::string& model_dir) : key_(lockKey(model_dir)) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_held[key_]++ > 0) return;
    const std::string file = pathOf(model_dir);
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    for (int attempt = 0; attempt < 3; attempt++) {
        if (std::FILE* f = std::fopen(file.c_str(), "wx")) {
            std::fprintf(f, "%ld\n", selfPid());
            std::fclose(f);
            return;
        }
        long pid = readPid(file);
        // A file with no pid yet is a writer between creating and writing it.
        if (pid == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            pid = readPid(file);
        }
        if (pid == 0 || alive(pid)) {
            g_held.erase(key_);
            throw WriterBusy(file, pid);
        }
        fs::remove(file, ec);
    }
    g_held.erase(key_);
    throw WriterBusy(file, readPid(file));
}

WriterLock::~WriterLock() {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_held.find(key_);
    if (it == g_held.end() || --it->second > 0) return;
    g_held.erase(it);
    const fs::path p(key_);
    const std::string file = (p.parent_path() / ("." + p.filename().string() + ".lock")).string();
    // Only a lock that still names this process: a taken-over one is not ours to drop.
    if (readPid(file) == selfPid()) {
        std::error_code ec;
        fs::remove(file, ec);
    }
}

void setPublishProbe(PublishProbe p) { g_probe.store(p); }

void recoverPublish(const std::string& out) {
    std::error_code ec;
    const std::string aside = asideDir(out);
    if (!fs::exists(aside, ec)) return;
    if (fs::exists(out, ec)) {
        fs::remove_all(aside, ec);
        return;
    }
    fs::rename(aside, out, ec);
    if (ec) throw std::runtime_error("cannot restore " + out + " from " + aside + ": " + ec.message());
}

void publishDir(const std::string& tmp, const std::string& out) {
    std::error_code ec;
    recoverPublish(out);
    probe("complete");
    const std::string aside = asideDir(out);
    const bool replacing = fs::exists(out, ec);
    if (replacing) {
        fs::remove_all(aside, ec);
        fs::rename(out, aside, ec);
        if (ec) throw std::runtime_error("cannot set aside " + out + ": " + ec.message());
        probe("set-aside");
    }
    fs::rename(tmp, out, ec);
    if (ec) {
        const std::string why = ec.message();
        if (replacing) fs::rename(aside, out, ec);
        throw std::runtime_error("cannot publish " + out + ": " + why);
    }
    probe("promoted");
    if (replacing) fs::remove_all(aside, ec);
}

}  // namespace roma
