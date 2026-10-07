// Publishing a model folder: written beside its final name, then swapped in.
// Adapted in design from spirula-studio#154 (D1odeKing, GPL-3.0): a writer lock
// per workspace and a manifest that is only trusted once the folder is whole.
#pragma once

#include <memory>
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
// dead owner's is taken over. The holding thread may take it again; another
// thread of the same process is refused, as a process would be.
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

// Only a folder with a densify.json is ever replaced or removed by this stage.
bool isDensifyOutput(const std::string& dir);

// `tmp` is a finished folder; it becomes `out`. An existing `out` is set aside
// first and removed only once the new one is in place; one that is not a
// densify output is refused.
void publishDir(const std::string& tmp, const std::string& out);

// After a crash inside publishDir: `out` missing with a set-aside copy gets the
// copy back, and a set-aside copy beside a whole `out` is dropped. A folder
// with no densify.json is left alone.
void recoverPublish(const std::string& out);

enum class Claim { Ok, Exists, NotDensify };

// Takes the writer lock on `out`, recovers a crashed publish, and only then
// judges `out`: Exists without `overwrite`, NotDensify for an existing folder
// that is not a densify output. The lock is held in `lock` only on Ok.
Claim claimOut(const std::string& out, bool overwrite, std::unique_ptr<WriterLock>& lock);

// Removes `out` when it is a densify output; false for anything else or nothing.
bool removeStaleOutput(const std::string& out);

// Called with the name of each step of publishDir; tests use it to stop the
// process there. Null clears it.
using PublishProbe = void (*)(const char* point);
void setPublishProbe(PublishProbe probe);

}  // namespace roma
