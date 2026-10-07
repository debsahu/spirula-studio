// ReconModels.cpp -- see ReconModels.h.

#include "app/gui/ReconModels.h"

#include "core/Sha256.h"
#include "data/Json.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>

namespace fs = std::filesystem;

namespace gui {

namespace {

int64_t leading_count(const fs::path& file) {
    std::FILE* f = std::fopen(file.string().c_str(), "rb");
    if (!f) return -1;
    uint64_t n = 0;
    const bool ok = std::fread(&n, sizeof n, 1, f) == 1;
    std::fclose(f);
    return ok && n < (1ull << 40) ? (int64_t)n : -1;
}

}  // namespace

namespace {

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

bool is_dense_model(const std::string& rel) {
    return ends_with(rel, "-roma") || ends_with(rel, "-roma-edit");
}

CloudCheck cloud_check(const std::string& dir) {
    struct Seen {
        uintmax_t size = 0;
        fs::file_time_type time;
        CloudCheck result = CloudCheck::None;
    };
    static std::mutex mu;
    static std::map<std::string, Seen> seen;
    std::error_code ec;
    const fs::path points = fs::path(dir) / "points3D.bin", tracks = fs::path(dir) / "points3D_tracks.bin",
                   record = fs::path(dir) / "densify.json";
    const uintmax_t size = fs::file_size(points, ec) + fs::file_size(tracks, ec) + fs::file_size(record, ec);
    const fs::file_time_type time = std::max({fs::last_write_time(points, ec), fs::last_write_time(tracks, ec),
                                              fs::last_write_time(record, ec)});
    {
        std::lock_guard<std::mutex> lk(mu);
        auto it = seen.find(dir);
        if (it != seen.end() && it->second.size == size && it->second.time == time) return it->second.result;
    }
    CloudCheck result = CloudCheck::None;
    try {
        const JsonValue meta = json_parse_file(record.string());
        const JsonValue* a = meta.find("points3D_sha256");
        const JsonValue* b = meta.find("tracks_sha256");
        if (a && b)
            result = a->as_string() == spirula::sha256_file(points.string()) &&
                             b->as_string() == spirula::sha256_file(tracks.string())
                         ? CloudCheck::Ok
                         : CloudCheck::Mismatch;
    } catch (const std::exception&) {
    }
    std::lock_guard<std::mutex> lk(mu);
    seen[dir] = {size, time, result};
    return result;
}

int64_t recon_point_count(const std::string& dir) {
    return leading_count(fs::path(dir) / "points3D.bin");
}

std::vector<ReconModel> list_recon_models(const std::string& dataset) {
    std::vector<ReconModel> out;
    std::error_code ec;
    for (const char* parent : {"sparse", "colmap/sparse"}) {
        const fs::path p = fs::path(dataset) / parent;
        if (!fs::is_directory(p, ec)) continue;
        for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const fs::path d = it->path();
            // A writer's folder in progress, or one a crash left behind.
            if (ends_with(d.filename().string(), ".partial") || ends_with(d.filename().string(), ".old")) continue;
            if (!fs::exists(d / "cameras.bin", ec) || !fs::exists(d / "images.bin", ec))
                continue;
            ReconModel m;
            m.rel = (fs::path(parent) / d.filename()).generic_string();
            m.images = leading_count(d / "images.bin");
            m.points = leading_count(d / "points3D.bin");
            out.push_back(std::move(m));
        }
    }
    std::sort(out.begin(), out.end(), [](const ReconModel& a, const ReconModel& b) {
        return a.images != b.images ? a.images > b.images : a.rel < b.rel;
    });
    return out;
}

}  // namespace gui
