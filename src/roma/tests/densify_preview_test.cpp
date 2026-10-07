// The live preview of a growing dense cloud: snapshots follow the cloud, are
// throttled and decimated, are never seen half written, and cannot stop a run.
// Each check is named for the mutation it catches.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "roma/DensifyPreview.h"
#include "roma/Synthetic.h"
#include "sfm/core/Progress.h"

namespace fs = std::filesystem;

namespace {

int fails = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    fails += !ok;
}

struct Snapshot {
    bool ok = false;
    uint32_t registered = 0;
    uint32_t points = 0;
    float first[3] = {0, 0, 0}, second[3] = {0, 0, 0};
};

// Reads model.bin strictly: any short read or trailing byte is a failure.
Snapshot parse(const fs::path& p) {
    Snapshot s;
    std::ifstream f(p, std::ios::binary);
    const std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t at = 0;
    auto need = [&](size_t n) { return at + n <= b.size(); };
    auto u32 = [&](uint32_t& v) {
        if (!need(4)) return false;
        std::memcpy(&v, b.data() + at, 4);
        at += 4;
        return true;
    };
    if (b.size() < 28 || b.compare(0, 4, "VKPM") != 0) return s;
    at = 4;
    uint32_t version, flags, images;
    uint64_t total;
    if (!u32(version) || version != 4 || !u32(flags) || !u32(images) || !u32(s.registered) || !need(8)) return s;
    std::memcpy(&total, b.data() + at, 8);
    at += 8;
    for (uint32_t i = 0; i < s.registered; i++) {
        uint32_t id, w, h, model, np;
        if (!u32(id) || !need(48)) return s;
        at += 48;
        if (!u32(w) || !u32(h) || !u32(model) || !u32(np) || np > 16 || !need((size_t)np * 8)) return s;
        at += (size_t)np * 8;
    }
    if (!u32(s.points) || !need((size_t)s.points * 15)) return s;
    for (uint32_t i = 0; i < s.points && i < 2; i++)
        std::memcpy(i ? s.second : s.first, b.data() + at + (size_t)i * 15, 12);
    at += (size_t)s.points * 15;
    s.ok = at == b.size();
    return s;
}

std::vector<roma::DensePoint> cloud(size_t n, double x0 = 0) {
    std::vector<roma::DensePoint> c(n);
    for (size_t i = 0; i < n; i++) {
        c[i].xyz = {x0 + 0.001 * (double)i, 0.5 * (double)(i % 11), 0.25};
        c[i].rgb[0] = (float)(i % 256) / 255.0f;
    }
    return c;
}

}  // namespace

int main() {
    try {
        const fs::path d = fs::temp_directory_path() / "spirula_densify_preview_test";
        std::error_code ec;
        fs::remove_all(d, ec);
        roma::writeStairDataset(roma::stairScene(), d.string(), 96, 192, 300);
        const std::string model = (d / "sparse" / "0").string();
        const fs::path progress = d / "progress";
        sfm::progress::set_dir(progress.string());
        const fs::path file = progress / "model.bin";

        {
            roma::CloudPreview off(model);
            check(off.active(), "the preview is on with a progress dir and a readable model");
        }
        sfm::progress::set_dir("");
        {
            roma::CloudPreview off(model);
            off.update(cloud(10), true);
            check(!off.active() && off.writes() == 0, "no progress dir: nothing is written and nothing is cost");
        }
        sfm::progress::set_dir(progress.string());

        // ---- it follows the cloud -----------------------------------------------
        {
            roma::CloudPreview prev(model, 0.0, 50000);
            size_t last = 0;
            bool grows = true, exact = true;
            for (size_t n : {100u, 1000u, 20000u}) {
                prev.update(cloud(n), true);
                const Snapshot s = parse(file);
                grows = grows && s.ok && s.points > last;
                exact = exact && s.points == n;
                last = s.points;
            }
            check(grows, "preview: each snapshot holds more points than the last as the cloud grows");
            check(exact, "preview: a modest cloud is written whole");
            const Snapshot s = parse(file);
            check(s.registered > 0, "preview: the model's cameras are in the snapshot");
            check(prev.writes() == 3, "preview: one write per update when each is due");
        }

        // ---- decimated ----------------------------------------------------------
        {
            roma::CloudPreview prev(model, 0.0, 50000);
            const std::vector<roma::DensePoint> big = cloud(300000);
            prev.update(big, true);
            const Snapshot s = parse(file);
            const size_t stride = (300000 + 49999) / 50000;
            check(s.ok && s.points == (300000 + stride - 1) / stride && s.points <= 50000,
                  "decimated: 300000 points become a strided 50000, not all of them");
            check(std::abs(s.first[0] - (float)big[0].xyz.x) < 1e-6f &&
                      std::abs(s.second[0] - (float)big[stride].xyz.x) < 1e-4f,
                  "decimated: the points are the cloud's own, at the stride");
        }

        // ---- throttled ------------------------------------------------------------
        {
            roma::CloudPreview prev(model, 3600.0, 50000);
            const std::vector<roma::DensePoint> c = cloud(2000);
            for (int i = 0; i < 1000; i++) prev.update(c);
            check(prev.writes() == 1, "throttled: a thousand updates inside the interval write once");
            prev.update(cloud(2500), true);
            check(prev.writes() == 2 && parse(file).points == 2500, "throttled: the final cloud is written whatever the clock says");
        }
        {
            roma::CloudPreview prev(model, 0.0, 200000);
            const std::vector<roma::DensePoint> c = cloud(150000);
            prev.update(c, true);
            const int w = prev.writes();
            prev.update(c);
            check(prev.writes() == w, "throttled: the wait after a write is at least eight times the write");
        }

        // ---- it cannot stop a run ------------------------------------------------
        {
            roma::CloudPreview prev(model, 0.0, 50000);
            // A reader holding the old snapshot open does not hold the writer.
            std::ifstream held(file, std::ios::binary);
            char first = 0;
            held.read(&first, 1);
            prev.update(cloud(500), true);
            prev.update(cloud(700), true);
            check(prev.writes() == 2 && parse(file).points == 700, "never blocks: updates go through while a reader holds the file");
            check(first == 'V', "never blocks: the reader's open handle was not disturbed");
        }
        {
            const fs::path blocker = d / "blocker";
            std::ofstream(blocker) << "a file where the progress dir should be";
            sfm::progress::set_dir((blocker / "progress").string());
            roma::CloudPreview prev(model, 0.0, 50000);
            bool threw = false;
            try {
                prev.update(cloud(100), true);
                prev.update(cloud(200), true);
            } catch (...) {
                threw = true;
            }
            check(!threw, "never blocks: a progress dir that cannot be written to costs nothing and throws nothing");
            sfm::progress::set_dir(progress.string());
        }
        {
            bool threw = false, active = true;
            int writes = -1;
            try {
                roma::CloudPreview prev((d / "no-such-model").string());
                prev.update(cloud(10), true);
                active = prev.active();
                writes = prev.writes();
            } catch (...) {
                threw = true;
            }
            check(!threw && !active && writes == 0, "never blocks: an unreadable model turns the preview off, quietly");
        }

        // ---- never seen half written -----------------------------------------------
        {
            roma::CloudPreview prev(model, 0.0, 50000);
            const std::vector<roma::DensePoint> few = cloud(300), many = cloud(40000);
            prev.update(few, true);
            std::atomic<bool> stop{false};
            std::thread writer([&] {
                bool flip = false;
                while (!stop.load()) {
                    prev.update(flip ? few : many, true);
                    flip = !flip;
                }
            });
            int bad = 0, reads = 0;
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            while (reads < 400 || std::chrono::steady_clock::now() < end) {
                const Snapshot s = parse(file);
                bad += !s.ok;
                reads++;
            }
            stop = true;
            writer.join();
            check(reads >= 400 && bad == 0, "atomic: " + std::to_string(reads) + " reads during writes, " + std::to_string(bad) + " torn");
        }

        fs::remove_all(d, ec);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 2;
    }
    return fails ? 1 : 0;
}
