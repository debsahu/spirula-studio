// The edit round trip of a dense cloud, the writer lock and the crash safety of
// publishing a model folder. Each check is named for the mutation it catches.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "core/Sha256.h"
#include "data/Json.h"
#include "roma/DensifyEdit.h"
#include "roma/DensifyRun.h"
#include "roma/Publish.h"
#include "roma/Synthetic.h"
#include "sfm/core/Model.h"

namespace fs = std::filesystem;

namespace {

int fails = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    fails += !ok;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// File name -> SHA-256 of every file in a folder.
std::map<std::string, std::string> tree(const fs::path& dir) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) out[it->path().filename().string()] = spirula::sha256_file(it->path().string());
    return out;
}

template <class T>
T rd(const std::string& s, size_t at) {
    T v;
    std::memcpy(&v, s.data() + at, sizeof v);
    return v;
}

// points3D.bin as (id, the record's bytes), read here and not by the writer's code.
std::vector<std::pair<uint64_t, std::string>> readPoints(const fs::path& p) {
    const std::string s = slurp(p);
    std::vector<std::pair<uint64_t, std::string>> out;
    size_t at = 8;
    const uint64_t n = rd<uint64_t>(s, 0);
    for (uint64_t i = 0; i < n; i++) {
        const size_t row = at;
        const uint64_t id = rd<uint64_t>(s, at);
        at += 8 + 24 + 3 + 8;
        at += 8 + (size_t)rd<uint64_t>(s, at) * 8;
        out.emplace_back(id, s.substr(row, at - row));
    }
    return out;
}

std::vector<std::pair<uint64_t, std::string>> readTracks(const fs::path& p) {
    const std::string s = slurp(p);
    std::vector<std::pair<uint64_t, std::string>> out;
    size_t at = 12;
    const uint64_t n = rd<uint64_t>(s, 4);
    for (uint64_t i = 0; i < n; i++) {
        const size_t row = at;
        const uint64_t id = rd<uint64_t>(s, at);
        at += 8;
        const uint32_t k = rd<uint32_t>(s, at);
        at += 4 + (size_t)k * 12;
        out.emplace_back(id, s.substr(row, at - row));
    }
    return out;
}

std::vector<uint64_t> readNormalIds(const fs::path& p) {
    const std::string s = slurp(p);
    const size_t body = s.find("end_header\n") + 11;
    std::vector<uint64_t> ids;
    for (size_t at = body; at + 28 <= s.size(); at += 28) ids.push_back(rd<uint32_t>(s, at + 24));
    return ids;
}

JsonValue meta(const fs::path& dir) { return json_parse_file((dir / "densify.json").string()); }

std::string jstr(const JsonValue& o, const char* a, const char* b = nullptr) {
    const JsonValue* v = o.find(a);
    if (v && b) v = v->find(b);
    return v ? v->as_string() : std::string("<absent>");
}

constexpr size_t kPoints = 600;

// A dataset with a sibling of kPoints points, each with a track of 1-3 observations
// that no other point shares, and normals for every point.
struct Fixture {
    fs::path dir, src, dense;
    std::vector<roma::DensePoint> cloud;
};

Fixture make(const std::string& name) {
    Fixture f;
    f.dir = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(f.dir, ec);
    roma::writeStairDataset(roma::stairScene(), f.dir.string(), 96, 192, 300);
    f.src = f.dir / "sparse" / "0";
    roma::DensifyJob job;
    job.model_dir = f.src.string();
    const roma::DensifyPlan pl = roma::planDensify(job);
    f.cloud.resize(kPoints);
    for (size_t i = 0; i < f.cloud.size(); i++) {
        roma::DensePoint& p = f.cloud[i];
        p.xyz = {0.01 * (double)i, 0.5 + 0.001 * (double)(i % 7), 0.25};
        for (int c = 0; c < 3; c++) p.rgb[c] = (float)((i * 7 + (size_t)c * 31) % 256) / 255.0f;
        p.normal[0] = 0.0f;
        p.normal[1] = 0.6f;
        p.normal[2] = 0.8f;
        for (size_t k = 0; k <= i % 3; k++) p.track.push_back({(int)(k % pl.views.size()), 10.0 + (double)i, 20.0 + (double)k});
    }
    f.dense = fs::path(roma::siblingDir(f.src.string()));
    roma::writeSibling(f.src.string(), f.dense.string(), pl, f.cloud, "{\n  \"tool\": \"test\"\n}\n");
    return f;
}

std::vector<uint8_t> dropEvery(size_t n, size_t step, size_t phase = 0) {
    std::vector<uint8_t> keep(n, 1);
    for (size_t i = phase; i < n; i += step) keep[i] = 0;
    return keep;
}

#ifndef _WIN32
const char* g_crash_at = nullptr;
void crashProbe(const char* point) {
    if (g_crash_at && !std::strcmp(point, g_crash_at)) _exit(77);
}

// Runs `fn` in a child and reports how it ended: its exit code.
template <class F>
int inChild(F&& fn) {
    const pid_t pid = fork();
    if (pid == 0) {
        fn();
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 255;
}
#endif

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        // ---- the round trip -----------------------------------------------
        Fixture f = make("spirula_densify_edit_test");
        const auto before = tree(f.dense);
        const auto src_points = readPoints(f.dense / "points3D.bin");
        const auto src_tracks = readTracks(f.dense / "points3D_tracks.bin");
        check(src_points.size() == kPoints && src_tracks.size() == kPoints, "fixture: the sibling holds every point and its track");

        const std::vector<uint8_t> keep = dropEvery(kPoints, 5, 2);
        const size_t kept_n = (size_t)std::count(keep.begin(), keep.end(), (uint8_t)1);
        const roma::EditResult er = roma::writeEditedSibling(f.dense.string(), keep);
        const fs::path edit = er.out_dir;
        check(fs::path(edit).filename() == "0-roma-edit" && fs::path(edit).parent_path() == f.dense.parent_path(),
              "the edit is the -roma-edit sibling, beside the original");
        check(er.before == (int64_t)kPoints && er.kept == (int64_t)kept_n, "the result counts what was kept");

        check(slurp(edit / "cameras.bin") == slurp(f.src / "cameras.bin") &&
                  slurp(edit / "cameras.bin") == slurp(f.dense / "cameras.bin"),
              "edit: cameras.bin is byte-identical to the original's");
        check(slurp(edit / "images.bin") == slurp(f.dense / "images.bin"),
              "edit: images.bin (every pose) is byte-identical to the original's");

        const auto out_points = readPoints(edit / "points3D.bin");
        bool rows = out_points.size() == kept_n;
        for (size_t i = 0, j = 0; rows && i < kPoints; i++)
            if (keep[i]) rows = out_points[j++] == src_points[i];
        check(rows, "edit: points3D.bin holds exactly the kept records, unchanged and in order");
        const auto out_tracks = readTracks(edit / "points3D_tracks.bin");
        bool tracks = out_tracks.size() == kept_n;
        for (size_t i = 0, j = 0; tracks && i < kPoints; i++)
            if (keep[i]) tracks = out_tracks[j++] == src_tracks[i];
        check(tracks, "edit: each kept point keeps its own track, and no track outlives its point");
        std::vector<uint64_t> want_ids;
        for (size_t i = 0; i < kPoints; i++)
            if (keep[i]) want_ids.push_back(src_points[i].first);
        check(readNormalIds(edit / "points3D_normals.ply") == want_ids,
              "edit: the normals file follows the same points");

        // A valid COLMAP model, through readers that are not the writer's.
        const sfm::Reconstruction rec = sfm::Reconstruction::readBinary(edit.string());
        check(rec.points3D.size() == kept_n && rec.images.size() == sfm::Reconstruction::readBinary(f.src.string()).images.size(),
              "edit: sfm reads it back, every image and the kept points");

        // ---- the record -----------------------------------------------------
        const JsonValue m = meta(edit);
        check(jstr(m, "edited_from", "model") == "0-roma", "densify.json: edited_from names the original");
        check(jstr(m, "edited_from", "points3D_sha256") == spirula::sha256_file((f.dense / "points3D.bin").string()),
              "densify.json: edited_from carries the original's cloud checksum");
        check(jstr(m, "points3D_sha256") == spirula::sha256_file((edit / "points3D.bin").string()) &&
                  jstr(m, "tracks_sha256") == spirula::sha256_file((edit / "points3D_tracks.bin").string()),
              "densify.json: the edit's own checksums are those of the files beside it");
        check(m.find("points") && m.find("points")->as_int() == (int64_t)kept_n, "densify.json: points is the kept count");
        check(m.find("edit") && m.find("edit")->find("points_removed") &&
                  m.find("edit")->find("points_removed")->as_int() == (int64_t)(kPoints - kept_n),
              "densify.json: the number removed is recorded");

        // ---- the original is never modified ---------------------------------
        check(tree(f.dense) == before, "the original sibling is untouched (every file's SHA-256)");

        // The writer's own checksums on a fresh sibling.
        const JsonValue om = meta(f.dense);
        check(jstr(om, "points3D_sha256") == spirula::sha256_file((f.dense / "points3D.bin").string()) &&
                  jstr(om, "tracks_sha256") == spirula::sha256_file((f.dense / "points3D_tracks.bin").string()),
              "densify.json of a fresh sibling records its cloud checksums");

        // ---- refusals leave nothing behind ----------------------------------
        auto refuses = [&](const std::string& dir, const std::vector<uint8_t>& k, bool replace, const std::string& what) {
            bool threw = false;
            try {
                roma::writeEditedSibling(dir, k, replace);
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, what);
        };
        const auto edit_before = tree(edit);
        refuses(f.dense.string(), std::vector<uint8_t>(kPoints, 1), true, "refused: an edit that removes nothing");
        refuses(f.dense.string(), std::vector<uint8_t>(kPoints, 0), true, "refused: an edit that leaves no points");
        refuses(f.dense.string(), std::vector<uint8_t>(kPoints - 1, 1), true, "refused: a selection that does not cover the file");
        refuses(f.dense.string(), dropEvery(kPoints, 3), false, "refused: replacing an existing edit unless asked");
        refuses(f.src.string(), dropEvery(kPoints, 3), true, "refused: the sparse model is not a dense one");
        check(tree(edit) == edit_before && tree(f.dense) == before && !fs::exists(roma::partialDir(edit.string())),
              "refusals changed neither folder and left no .partial");

        // ---- an edit of the edit ------------------------------------------------
        const std::vector<uint8_t> keep2 = dropEvery(kept_n, 4, 1);
        const size_t kept2 = (size_t)std::count(keep2.begin(), keep2.end(), (uint8_t)1);
        const roma::EditResult er2 = roma::writeEditedSibling(edit.string(), keep2);
        check(fs::path(er2.out_dir) == edit && er2.kept == (int64_t)kept2, "an edit of the edit rewrites the edit");
        const JsonValue m2 = meta(edit);
        check(jstr(m2, "edited_from", "model") == "0-roma" &&
                  jstr(m2, "edited_from", "points3D_sha256") == spirula::sha256_file((f.dense / "points3D.bin").string()),
              "the second edit still points at the first model, not at the first edit");
        check(tree(f.dense) == before, "the original is untouched after the second edit too");
        check(readPoints(edit / "points3D.bin").size() == kept2, "the second edit's cloud has the points it kept");

        // ---- replacing an edit from the original, when asked -----------------
        const roma::EditResult er3 = roma::writeEditedSibling(f.dense.string(), dropEvery(kPoints, 2), true);
        check(er3.kept == (int64_t)(kPoints / 2) && tree(f.dense) == before, "replace: a new edit from the original");

#ifndef _WIN32
        // ---- the writer lock ------------------------------------------------------
        {
            const std::string out = (f.dense.parent_path() / "0-roma-edit").string();
            const auto locked_before = tree(out);
            int hold[2], release[2];
            if (pipe(hold) || pipe(release)) throw std::runtime_error("pipe");
            const pid_t holder = fork();
            if (holder == 0) {
                roma::WriterLock lk(out);
                const char one = 1;
                (void)!write(hold[1], &one, 1);
                char c;
                (void)!read(release[0], &c, 1);
                _exit(0);
            }
            char c;
            (void)!read(hold[0], &c, 1);
            long busy_pid = 0;
            try {
                roma::writeEditedSibling(f.dense.string(), dropEvery(kPoints, 3), true);
            } catch (const roma::WriterBusy& b) {
                busy_pid = b.pid;
            } catch (const std::exception&) {
            }
            check(busy_pid == (long)holder, "lock: a second writer of the edit is refused, naming the holder");
            bool second = false;
            try {
                roma::WriterLock again(out);
            } catch (const roma::WriterBusy&) {
                second = true;
            }
            check(second, "lock: a second process cannot take the lock either");
            check(roma::WriterLock::holder(out) == (long)holder, "lock: holder() reports the live pid");
            check(tree(out) == locked_before, "lock: the refused writer wrote nothing");
            // Let the holder die without releasing: the file stays, the pid is dead.
            (void)!write(release[1], &c, 1);
            int st = 0;
            waitpid(holder, &st, 0);
            check(fs::exists(roma::WriterLock::pathOf(out)), "lock: a dead holder leaves its file behind");
            check(roma::WriterLock::holder(out) == 0, "lock: a dead holder's pid is not live");
            bool took = false;
            try {
                roma::WriterLock lk(out);
                roma::WriterLock nested(out);
                took = true;
            } catch (const std::exception&) {
            }
            check(took, "lock: a dead holder's lock is taken over, and this process may take it twice");
            check(!fs::exists(roma::WriterLock::pathOf(out)), "lock: released when the last holder lets go");
            close(hold[0]); close(hold[1]); close(release[0]); close(release[1]);
        }

        // ---- a crash inside publishing ----------------------------------------------
        {
            Fixture g = make("spirula_densify_publish_crash_test");
            const std::string out = (g.dense.parent_path() / "0-roma-edit").string();
            roma::writeEditedSibling(g.dense.string(), dropEvery(kPoints, 5, 2));
            const auto previous = tree(out);
            const auto original = tree(g.dense);
            for (const char* point : {"complete", "set-aside", "promoted"}) {
                g_crash_at = point;
                const int rc = inChild([&] {
                    roma::setPublishProbe(crashProbe);
                    roma::writeEditedSibling(out, dropEvery(kPoints - kPoints / 5, 3, 1));
                });
                g_crash_at = nullptr;
                check(rc == 77, std::string("crash: the writer died at '") + point + "'");
                roma::recoverPublish(out);
                const auto after = tree(out);
                const bool intact = after == previous;
                const std::vector<uint8_t> second = dropEvery(kPoints - kPoints / 5, 3, 1);
                const bool whole_new = !after.empty() && after != previous && after.count("points3D.bin") &&
                                       readPoints(fs::path(out) / "points3D.bin").size() ==
                                           (size_t)std::count(second.begin(), second.end(), (uint8_t)1);
                if (std::string(point) == "promoted")
                    check(whole_new && !fs::exists(roma::asideDir(out)), "crash at 'promoted': the new model is whole and the aside copy is dropped");
                else
                    check(intact && !fs::exists(roma::asideDir(out)),
                          std::string("crash at '") + point + "': the previous model is back, byte for byte");
                check(tree(g.dense) == original, std::string("crash at '") + point + "': the original is untouched");
                roma::writeEditedSibling(g.dense.string(), dropEvery(kPoints, 5, 2), true);   // back to `previous`
                check(tree(out) == previous, std::string("crash at '") + point + "': the state for the next case is restored");
            }

            // The same through `densify`'s own publish.
            roma::DensifyJob job;
            job.model_dir = g.src.string();
            const roma::DensifyPlan pl = roma::planDensify(job);
            const auto dense_before = tree(g.dense);
            for (const char* point : {"complete", "set-aside"}) {
                g_crash_at = point;
                const int rc = inChild([&] {
                    roma::setPublishProbe(crashProbe);
                    std::vector<roma::DensePoint> other(40);
                    for (size_t i = 0; i < other.size(); i++) other[i].xyz = {1.0 + 0.01 * (double)i, 2.0, 3.0};
                    roma::writeSibling(g.src.string(), g.dense.string(), pl, other, "{}\n");
                });
                g_crash_at = nullptr;
                check(rc == 77, std::string("crash: writeSibling died at '") + point + "'");
                roma::recoverPublish(g.dense.string());
                check(tree(g.dense) == dense_before, std::string("crash in writeSibling at '") + point + "': the previous dense model is intact");
            }
            std::error_code ec;
            fs::remove_all(g.dir, ec);
        }
#else
        std::printf("skipped: lock and crash checks need fork()\n");
#endif

        std::error_code ec;
        fs::remove_all(f.dir, ec);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 2;
    }
    return fails ? 1 : 0;
}
