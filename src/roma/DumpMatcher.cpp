#include "roma/DumpMatcher.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace roma {

namespace {

template <class T>
void get(std::ifstream& f, T* v, size_t n, const std::string& path) {
    f.read(reinterpret_cast<char*>(v), (std::streamsize)(n * sizeof(T)));
    if (!f) throw std::runtime_error(path + ": truncated");
}

}  // namespace

DumpMatcher::DumpMatcher(std::string dir, int input_size)
    : dir_(std::move(dir)), input_size_(input_size) {
    if (!std::filesystem::is_directory(dir_))
        throw std::runtime_error("matches directory not found: " + dir_);
}

std::string DumpMatcher::pairFile(const std::string& dir, const std::string& a,
                                  const std::string& b) {
    return (std::filesystem::path(dir) / (a + "__" + b + ".rwm")).string();
}

Warp DumpMatcher::match(const MatchImage& a, const MatchImage& b) {
    return readWarp(pairFile(dir_, a.name, b.name));
}

std::string DumpMatcher::describe() const { return "matches from " + dir_; }

Warp readWarp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("no matches for this pair: " + path);
    char magic[4];
    get(f, magic, 4, path);
    if (std::memcmp(magic, "RWM1", 4) != 0) throw std::runtime_error(path + ": not an .rwm file");
    int32_t hdr[3];
    get(f, hdr, 3, path);
    Warp w;
    w.width = hdr[0];
    w.height = hdr[1];
    if (w.width <= 0 || w.height <= 0 || w.width > 16384 || w.height > 16384)
        throw std::runtime_error(path + ": bad size");
    const size_t n = (size_t)w.width * (size_t)w.height;
    w.warp.resize(2 * n);
    w.certainty.resize(n);
    if (hdr[2] == 0 || hdr[2] == 2) {
        get(f, w.warp.data(), 2 * n, path);
        get(f, w.certainty.data(), n, path);
        if (hdr[2] == 2) {
            w.precision.resize(3 * n);
            get(f, w.precision.data(), 3 * n, path);
        }
    } else if (hdr[2] == 1) {
        std::vector<int16_t> q(2 * n);
        std::vector<uint16_t> c(n);
        get(f, q.data(), 2 * n, path);
        get(f, c.data(), n, path);
        for (size_t i = 0; i < 2 * n; i++) w.warp[i] = (float)q[i] / 32767.0f;
        for (size_t i = 0; i < n; i++) w.certainty[i] = (float)c[i] / 65535.0f;
    } else {
        throw std::runtime_error(path + ": unknown encoding " + std::to_string(hdr[2]));
    }
    return w;
}

void writeWarp(const std::string& path, const Warp& w, int encoding) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    const int32_t hdr[3] = {w.width, w.height, encoding};
    f.write("RWM1", 4);
    f.write(reinterpret_cast<const char*>(hdr), sizeof hdr);
    if (encoding == 2 && w.precision.size() != 3 * w.certainty.size())
        throw std::runtime_error(path + ": encoding 2 needs a precision per pixel");
    if (encoding == 0 || encoding == 2) {
        f.write(reinterpret_cast<const char*>(w.warp.data()),
                (std::streamsize)(w.warp.size() * sizeof(float)));
        f.write(reinterpret_cast<const char*>(w.certainty.data()),
                (std::streamsize)(w.certainty.size() * sizeof(float)));
        if (encoding == 2)
            f.write(reinterpret_cast<const char*>(w.precision.data()),
                    (std::streamsize)(w.precision.size() * sizeof(float)));
    } else {
        std::vector<int16_t> q(w.warp.size());
        std::vector<uint16_t> c(w.certainty.size());
        for (size_t i = 0; i < q.size(); i++)
            q[i] = (int16_t)std::lround(std::clamp(w.warp[i], -1.0f, 1.0f) * 32767.0f);
        for (size_t i = 0; i < c.size(); i++)
            c[i] = (uint16_t)std::lround(std::clamp(w.certainty[i], 0.0f, 1.0f) * 65535.0f);
        f.write(reinterpret_cast<const char*>(q.data()), (std::streamsize)(q.size() * 2));
        f.write(reinterpret_cast<const char*>(c.data()), (std::streamsize)(c.size() * 2));
    }
    if (!f) throw std::runtime_error("cannot write " + path);
}

}  // namespace roma
