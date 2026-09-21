// MaskLayer.cpp -- see MaskLayer.h.

#include "app/gui/mask/MaskLayer.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace gui {
namespace mask {

uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::string fnv_hex(uint64_t h) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

bool fnv_parse(const std::string& s, uint64_t& out) {
    if (s.size() != 16) return false;
    uint64_t h = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        h = (h << 4) | (uint64_t)d;
    }
    out = h;
    return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const size_t got = n > 0 ? std::fread(out.data(), 1, (size_t)n, f) : 0;
    std::fclose(f);
    out.resize(got);
    return n >= 0;
}

std::string normalize_dir(const std::string& dir) {
    fs::path p = fs::path(dir).lexically_normal();
    if (p.filename().empty() && p.has_parent_path()) p = p.parent_path();
    return p.string();
}

std::string frame_key(const std::string& image_root, const std::string& file) {
    const fs::path root(normalize_dir(image_root));
    fs::path rel = fs::path(file).lexically_normal().lexically_relative(root);
    if (rel.empty() || *rel.begin() == "..") rel = fs::path(file).filename();
    rel.replace_extension();
    return rel.generic_string();
}

std::string mask_file(const std::string& mask_root, const std::string& key) {
    return (fs::path(mask_root) / (key + ".png")).string();
}

std::string layer_file(const std::string& layer_root, const std::string& key,
                       Layer l) {
    const char* tag = l == Layer::Base ? ".base.png"
                    : l == Layer::Drop ? ".drop.png" : ".keep.png";
    return (fs::path(layer_root) / (key + tag)).string();
}

void composite(const uint8_t* base, const uint8_t* drop, const uint8_t* keep,
               size_t n, uint8_t* out) {
    for (size_t i = 0; i < n; i++)
        out[i] = (keep && keep[i]) ? 255 : (drop && drop[i]) ? 0 : base[i];
}

}  // namespace mask
}  // namespace gui
