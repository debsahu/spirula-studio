// ReconModels.cpp -- see ReconModels.h.

#include "app/gui/ReconModels.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

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
