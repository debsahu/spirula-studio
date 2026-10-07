// Publishing a model folder: written beside its final name, then swapped in.
// Adapted in design from spirula-studio#154 (D1odeKing, GPL-3.0): a writer lock
// per workspace and a manifest that is only trusted once the folder is whole.
#pragma once

#include <stdexcept>
#include <string>

namespace roma {

struct WriterBusy : std::runtime_error {
    WriterBusy(const std::string& lock_file, long holder_pid)
        : std::runtime_error("another process is writing this model (lock " + lock_file + ", pid " +
                             std::to_string(holder_pid) + ")"),
          lock(lock_file), pid(holder_pid) {}
    std::string lock;
    long pid;
};

// One writer per model folder: a file beside it holding the owner's pid. A
// dead owner's is taken over; this process may take its own again.
class WriterLock {
public:
    explicit WriterLock(const std::string& model_dir);
    ~WriterLock();
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;

    static std::string pathOf(const std::string& model_dir);
    // The pid of a live process holding the lock, 0 for none.
    static long holder(const std::string& model_dir);

private:
    std::string key_;
};

inline std::string partialDir(const std::string& out) { return out + ".partial"; }
inline std::string asideDir(const std::string& out) { return out + ".old"; }

// `tmp` is a finished folder; it becomes `out`. An existing `out` is set aside
// first and removed only once the new one is in place.
void publishDir(const std::string& tmp, const std::string& out);

// After a crash inside publishDir: `out` missing with a set-aside copy gets the
// copy back, and a set-aside copy beside a whole `out` is dropped.
void recoverPublish(const std::string& out);

// Called with the name of each step of publishDir; tests use it to stop the
// process there. Null clears it.
using PublishProbe = void (*)(const char* point);
void setPublishProbe(PublishProbe probe);

}  // namespace roma
