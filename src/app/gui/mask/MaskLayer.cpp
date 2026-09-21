// MaskLayer.cpp -- see MaskLayer.h.

#include "app/gui/mask/MaskLayer.h"

#include "app/FrameMask.h"
#include "data/Json.h"
#include "data/JsonWrite.h"
#include "external/stb_image_write.h"

// v1.16 defines this at :1132 but omits it from the public prototype block;
// STBIWDEF expands to extern "C" here (no STB_IMAGE_WRITE_STATIC build).
extern "C" unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes,
                                                int x, int y, int n, int* out_len);

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

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

bool encode_gray_png(const uint8_t* px, int w, int h, std::vector<uint8_t>& png) {
    int len = 0;
    unsigned char* p = stbi_write_png_to_mem(px, w, w, h, 1, &len);
    if (!p || len <= 0) return false;
    png.assign(p, p + len);
    std::free(p);
    return true;
}

std::string temp_write_path(const std::string& dst) {
    static std::atomic<uint64_t> counter{0};
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const long pid = (long)getpid();
#endif
    return dst + "." + std::to_string(pid) + "." +
           std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
}

bool write_file_atomic(const std::string& path, const uint8_t* data, size_t n) {
    std::error_code ec;
    const fs::path dst(path);
    fs::create_directories(dst.parent_path(), ec);
    const fs::path tmp(temp_write_path(dst.string()));
    FILE* f = std::fopen(tmp.string().c_str(), "wb");
    if (!f) return false;
    const bool wrote = n == 0 || std::fwrite(data, 1, n, f) == n;
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, dst, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool fingerprint_file(const std::string& path, uint64_t& out) {
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes)) return false;
    out = fnv1a64(bytes.data(), bytes.size());
    return true;
}

std::string utc_now_iso() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool LayerIndex::load(const std::string& layer_root, std::string& error) {
    frames.clear();
    mask_root.clear();
    const std::string path = (fs::path(layer_root) / kIndexFileName).string();
    std::error_code ec;
    if (!fs::exists(path, ec)) return true;
    JsonValue doc;
    try {
        doc = json_parse_file(path);
    } catch (const std::exception& e) {
        error = path + ": " + e.what();
        return false;
    }
    if (const JsonValue* r = doc.find("mask_root")) mask_root = r->as_string();
    const JsonValue* fr = doc.find("frames");
    if (!fr || !fr->is_object()) return true;
    for (const auto& [key, v] : fr->obj) {
        IndexEntry e;
        if (const JsonValue* b = v.find("base")) fnv_parse(b->as_string(), e.base_fp);
        if (const JsonValue* c = v.find("composite")) fnv_parse(c->as_string(), e.composite_fp);
        e.kept = (float)v.get_double("kept", 0.0);
        if (const JsonValue* s = v.find("saved_at")) e.saved_at = s->as_string();
        frames[key] = e;
    }
    return true;
}

bool LayerIndex::save(const std::string& layer_root, std::string& error) const {
    JsonWriter w;
    w.object();
    w.field("spirula_mask_edits", 1);
    w.field("mask_root", mask_root);
    w.key("frames").object();
    for (const auto& [key, e] : frames) {
        w.key(key.c_str()).object();
        w.field("base", fnv_hex(e.base_fp));
        w.field("composite", fnv_hex(e.composite_fp));
        w.field("kept", e.kept);
        w.field("saved_at", e.saved_at);
        w.end();
    }
    w.end();
    w.end();
    const std::string text = w.str();
    const std::string path = (fs::path(layer_root) / kIndexFileName).string();
    if (!write_file_atomic(path, (const uint8_t*)text.data(), text.size())) {
        error = path;
        return false;
    }
    return true;
}

BaseState base_state(const std::string& mask_root, const std::string& key,
                     const LayerIndex& idx) {
    const auto it = idx.frames.find(key);
    std::error_code ec;
    const std::string mask = mask_file(mask_root, key);
    const bool have_mask = fs::exists(mask, ec);
    if (it == idx.frames.end()) return have_mask ? BaseState::Unedited : BaseState::Missing;
    if (!have_mask) return BaseState::Missing;
    uint64_t fp = 0;
    if (!fingerprint_file(mask, fp)) return BaseState::Missing;
    return fp == it->second.composite_fp ? BaseState::Unchanged : BaseState::Regenerated;
}

namespace {

bool read_layer_or_zero(const std::string& path, int w, int h,
                        std::vector<uint8_t>& out, std::string& warning) {
    std::error_code ec;
    out.assign((size_t)w * h, 0);
    if (!fs::exists(path, ec)) return true;
    int lw = 0, lh = 0;
    std::vector<uint8_t> px;
    if (!app::load_stencil(path, lw, lh, px) || lw != w || lh != h) {
        if (!warning.empty()) warning += ", ";
        warning += path;
        return false;
    }
    out.swap(px);
    return true;
}

}  // namespace

bool read_layers(const std::string& layer_root, const std::string& key, int w,
                 int h, FrameLayers& out, std::string& warning) {
    out.w = w;
    out.h = h;
    const bool d = read_layer_or_zero(layer_file(layer_root, key, Layer::Drop), w, h,
                                      out.drop, warning);
    const bool k = read_layer_or_zero(layer_file(layer_root, key, Layer::Keep), w, h,
                                      out.keep, warning);
    return d && k;
}

bool save_frame(const std::string& layer_root, const std::string& mask_root,
                const std::string& key, int w, int h, const uint8_t* base,
                const uint8_t* drop, const uint8_t* keep, bool write_composite,
                LayerIndex& idx, std::string& error) {
    std::error_code ec;
    const size_t n = (size_t)w * h;
    const std::string base_path = layer_file(layer_root, key, Layer::Base);
    const std::string mask_path = mask_file(mask_root, key);
    IndexEntry e = idx.frames.count(key) ? idx.frames.at(key) : IndexEntry{};

    if (!fs::exists(base_path, ec) && fs::exists(mask_path, ec)) {
        std::vector<uint8_t> bytes;
        if (!read_file(mask_path, bytes)) { error = mask_path; return false; }
        if (!write_file_atomic(base_path, bytes.data(), bytes.size())) {
            error = base_path;
            return false;
        }
        e.base_fp = fnv1a64(bytes.data(), bytes.size());
    }

    std::vector<uint8_t> png;
    const std::string drop_path = layer_file(layer_root, key, Layer::Drop);
    if (!encode_gray_png(drop, w, h, png) ||
        !write_file_atomic(drop_path, png.data(), png.size())) {
        error = drop_path;
        return false;
    }
    const std::string keep_path = layer_file(layer_root, key, Layer::Keep);
    if (!encode_gray_png(keep, w, h, png) ||
        !write_file_atomic(keep_path, png.data(), png.size())) {
        error = keep_path;
        return false;
    }

    if (write_composite) {
        std::vector<uint8_t> out(n);
        composite(base, drop, keep, n, out.data());
        size_t kept = 0;
        for (uint8_t v : out) kept += v ? 1 : 0;
        if (!encode_gray_png(out.data(), w, h, png) ||
            !write_file_atomic(mask_path, png.data(), png.size())) {
            error = mask_path;
            return false;
        }
        e.composite_fp = fnv1a64(png.data(), png.size());
        e.kept = (float)kept / (float)std::max<size_t>(n, 1);
    }
    e.saved_at = utc_now_iso();
    idx.frames[key] = e;
    return idx.save(layer_root, error);
}

bool recomposite_frame(const std::string& layer_root, const std::string& mask_root,
                       const std::string& key, LayerIndex& idx, BaseState& found,
                       std::string& error) {
    found = base_state(mask_root, key, idx);
    if (found != BaseState::Regenerated) return true;
    const std::string mask_path = mask_file(mask_root, key);
    std::vector<uint8_t> bytes;
    if (!read_file(mask_path, bytes)) { error = mask_path; return false; }
    int w = 0, h = 0;
    std::vector<uint8_t> base;
    if (!app::load_stencil(mask_path, w, h, base)) { error = mask_path; return false; }
    FrameLayers layers;
    std::string warning;
    if (!read_layers(layer_root, key, w, h, layers, warning)) {
        error = warning;
        return false;
    }
    const std::string base_path = layer_file(layer_root, key, Layer::Base);
    if (!write_file_atomic(base_path, bytes.data(), bytes.size())) {
        error = base_path;
        return false;
    }
    idx.frames[key].base_fp = fnv1a64(bytes.data(), bytes.size());
    return save_frame(layer_root, mask_root, key, w, h, base.data(), layers.drop.data(),
                      layers.keep.data(), true, idx, error);
}

// -1 only if the index would not load. One bad frame does not withhold the
// rest of the batch; each failure is named in `error`, "; "-joined.
int recomposite_all(const std::string& layer_root, std::string& error) {
    LayerIndex idx;
    if (!idx.load(layer_root, error)) return -1;
    if (idx.mask_root.empty()) return 0;
    std::vector<std::string> keys;
    for (const auto& [k, e] : idx.frames) keys.push_back(k);
    int rebased = 0;
    std::string failures;
    for (const std::string& k : keys) {
        BaseState st;
        std::string frame_error;
        if (!recomposite_frame(layer_root, idx.mask_root, k, idx, st, frame_error)) {
            if (!failures.empty()) failures += "; ";
            failures += k + ": " + frame_error;
            continue;
        }
        if (st == BaseState::Regenerated) rebased++;
    }
    error = failures;
    return rebased;
}

bool revert_frame(const std::string& layer_root, const std::string& mask_root,
                  const std::string& key, LayerIndex& idx, std::string& error) {
    std::error_code ec;
    const std::string base_path = layer_file(layer_root, key, Layer::Base);
    if (fs::exists(base_path, ec)) {
        std::vector<uint8_t> bytes;
        if (!read_file(base_path, bytes)) { error = base_path; return false; }
        const std::string mask_path = mask_file(mask_root, key);
        if (!write_file_atomic(mask_path, bytes.data(), bytes.size())) {
            error = mask_path;
            return false;
        }
    }
    // An orphaned layer file with no index entry gets silently re-read as a
    // correction next time this frame opens, so a failed removal must leave
    // the entry standing, not just report false.
    std::string failed;
    for (Layer l : {Layer::Base, Layer::Drop, Layer::Keep}) {
        const std::string path = layer_file(layer_root, key, l);
        fs::remove(path, ec);
        if (ec) {
            if (!failed.empty()) failed += ", ";
            failed += path;
        }
    }
    if (!failed.empty()) {
        error = "could not remove: " + failed;
        return false;
    }
    idx.frames.erase(key);
    return idx.save(layer_root, error);
}

int revert_all(const std::string& layer_root, std::string& error) {
    LayerIndex idx;
    if (!idx.load(layer_root, error)) return -1;
    std::vector<std::string> keys;
    for (const auto& [k, e] : idx.frames) keys.push_back(k);
    for (const std::string& k : keys)
        if (!revert_frame(layer_root, idx.mask_root, k, idx, error)) return -1;
    return (int)keys.size();
}

}  // namespace mask
}  // namespace gui
