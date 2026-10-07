// Where each of `spirula geometry`'s depth maps came from: depths/geometry_maps.tsv,
// one row per map (the map's path under depths/, the image's under images/,
// fingerprints of both, and whether it is ray depth). `spirula densify` refuses
// a map whose row names another image or whose image or map changed since.
// Fingerprint: size and SHA-256 of the first and last MiB, so a copy keeps it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/Sha256.h"

namespace app {

inline constexpr const char* kDepthManifest = "geometry_maps.tsv";

struct DepthMapRecord {
    std::string image;          // relative to the image folder, generic separators
    std::string image_print, map_print;
    bool ray = true;
};

inline std::string file_print(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    const int64_t size = (int64_t)f.tellg();
    constexpr int64_t kSpan = 1 << 20;
    spirula::Sha256 h;
    std::vector<char> buf((size_t)std::min(size, kSpan));
    for (int64_t at : {(int64_t)0, std::max<int64_t>(0, size - kSpan)}) {
        f.seekg(at);
        f.read(buf.data(), (std::streamsize)buf.size());
        h.update((const uint8_t*)buf.data(), (size_t)f.gcount());
        if (size <= kSpan) break;
    }
    return std::to_string(size) + ":" + h.hex();
}

// Keyed by the map's path under the depth folder, generic separators.
inline std::map<std::string, DepthMapRecord> read_depth_manifest(const std::string& dir) {
    std::map<std::string, DepthMapRecord> m;
    std::ifstream f(std::filesystem::path(dir) / kDepthManifest);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> c;
        std::stringstream ss(line);
        for (std::string cell; std::getline(ss, cell, '\t');) c.push_back(cell);
        if (c.size() != 5) continue;
        m[c[0]] = {c[1], c[2], c[3], c[4] == "1"};
    }
    return m;
}

inline bool write_depth_manifest(const std::string& dir, const std::map<std::string, DepthMapRecord>& m) {
    const std::filesystem::path p = std::filesystem::path(dir) / kDepthManifest, tmp = p.string() + ".partial";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << "# spirula geometry depth maps v1: map, image, image fingerprint, map fingerprint, ray depth\n";
        for (const auto& kv : m)
            f << kv.first << '\t' << kv.second.image << '\t' << kv.second.image_print << '\t'
              << kv.second.map_print << '\t' << (kv.second.ray ? 1 : 0) << '\n';
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}

}  // namespace app
