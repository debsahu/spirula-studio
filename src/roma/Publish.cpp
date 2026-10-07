#include "roma/Publish.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include "core/ProcessAlive.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
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

bool alive(long pid) { return process_alive(pid); }

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

struct Held {
    std::thread::id owner;
    int count = 0;
};
std::mutex g_mu;
std::map<std::string, Held> g_held;

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
    const std::string file = pathOf(model_dir);
    auto held = g_held.find(key_);
    if (held != g_held.end()) {
        if (held->second.owner != std::this_thread::get_id()) throw WriterBusy(file, selfPid());
        held->second.count++;
        return;
    }
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    for (int attempt = 0; attempt < 3; attempt++) {
        if (std::FILE* f = std::fopen(file.c_str(), "wx")) {
            std::fprintf(f, "%ld\n", selfPid());
            std::fclose(f);
            g_held[key_] = {std::this_thread::get_id(), 1};
            return;
        }
        long pid = readPid(file);
        // A file with no pid yet is a writer between creating and writing it.
        if (pid == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            pid = readPid(file);
        }
        if (pid < 0) continue;
        if (pid == 0 || alive(pid)) throw WriterBusy(file, pid);
        // Two takers must not both win: renaming the file away succeeds for one, and
        // what was moved is read again, since a live writer may have replaced the
        // stale lock between the read above and the rename.
        probe("lock-stale");
        const std::string moved = file + ".stale." + std::to_string(selfPid()) + "." + std::to_string(attempt);
        fs::rename(file, moved, ec);
        if (ec) continue;
        const long was = readPid(moved);
        if (was != pid && alive(was)) {
            fs::create_hard_link(moved, file, ec);
            if (ec && !fs::exists(file)) fs::rename(moved, file, ec);
            fs::remove(moved, ec);
            throw WriterBusy(file, was);
        }
        fs::remove(moved, ec);
    }
    throw WriterBusy(file, readPid(file));
}

WriterLock::~WriterLock() {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_held.find(key_);
    if (it == g_held.end() || --it->second.count > 0) return;
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
    if (!fs::exists(aside, ec) || !isDensifyOutput(aside)) return;
    if (fs::exists(out, ec)) {
        fs::remove_all(aside, ec);
        return;
    }
    fs::rename(aside, out, ec);
    if (ec) throw std::runtime_error("cannot restore " + out + " from " + aside + ": " + ec.message());
}

bool isDensifyOutput(const std::string& dir) {
    std::error_code ec;
    return fs::exists(fs::path(dir) / "densify.json", ec);
}

Claim claimOut(const std::string& out, bool overwrite, std::unique_ptr<WriterLock>& lock) {
    lock = std::make_unique<WriterLock>(out);
    recoverPublish(out);
    std::error_code ec;
    Claim c = Claim::Ok;
    if (fs::exists(out, ec)) c = !overwrite ? Claim::Exists : isDensifyOutput(out) ? Claim::Ok : Claim::NotDensify;
    if (c != Claim::Ok) lock.reset();
    return c;
}

bool removeStaleOutput(const std::string& out) {
    std::error_code ec;
    if (!fs::exists(out, ec) || !isDensifyOutput(out)) return false;
    fs::remove_all(out, ec);
    return !ec;
}

void publishDir(const std::string& tmp, const std::string& out) {
    std::error_code ec;
    recoverPublish(out);
    const bool replacing = fs::exists(out, ec);
    if (replacing && !isDensifyOutput(out))
        throw std::runtime_error(out + " is not a densify output (no densify.json); not replacing it");
    const std::string aside = asideDir(out);
    // Recovery leaves a set-aside folder alone only when it is not ours to touch.
    if (fs::exists(aside, ec)) throw std::runtime_error(aside + " is in the way and is not a densify output");
    probe("complete");
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
