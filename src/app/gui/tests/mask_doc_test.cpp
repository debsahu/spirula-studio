// mask_doc_test -- app/gui/mask/: the correction layer on disk, the document
// in memory, the view math, and the session's worker, over synthetic frames
// whose every pixel is known. SS_MASK_BENCH=<dir> also runs the timing
// floors at 7680x3840 and writes that fixture dataset into <dir>.

#include "app/gui/mask/MaskLayer.h"
#include "core/SourcePath.h"
#include "external/stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mk = gui::mask;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

fs::path scratch(const char* name) {
    const fs::path d = fs::temp_directory_path() / "spirula_mask_doc_test" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

// A deterministic 0/255 mask: a filled ellipse of keep on a dropped
// background, plus speckles from a fixed LCG so no two frames are equal.
std::vector<uint8_t> synth_mask(int w, int h, uint32_t seed) {
    std::vector<uint8_t> px((size_t)w * h, 0);
    const float cx = 0.5f * w, cy = 0.5f * h, rx = 0.38f * w, ry = 0.42f * h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) px[(size_t)y * w + x] = 255;
        }
    uint32_t s = seed * 2654435761u + 12345u;
    for (int k = 0; k < w * h / 64; k++) {
        s = s * 1664525u + 1013904223u;
        const size_t i = (size_t)(s % (uint32_t)(w * h));
        px[i] = 255 - px[i];
    }
    return px;
}

std::vector<uint8_t> synth_rgb(int w, int h, uint32_t seed) {
    std::vector<uint8_t> px((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* p = &px[((size_t)y * w + x) * 3];
            p[0] = (uint8_t)((x * 255) / std::max(1, w - 1));
            p[1] = (uint8_t)((y * 255) / std::max(1, h - 1));
            p[2] = (uint8_t)((x + y + (int)seed) & 255);
        }
    return px;
}

bool write_png_gray(const fs::path& p, int w, int h, const std::vector<uint8_t>& px) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return stbi_write_png(p.string().c_str(), w, h, 1, px.data(), w) != 0;
}

bool write_jpg_rgb(const fs::path& p, int w, int h, const std::vector<uint8_t>& px) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return stbi_write_jpg(p.string().c_str(), w, h, 3, px.data(), 90) != 0;
}

std::vector<uint8_t> file_bytes(const fs::path& p) {
    std::vector<uint8_t> out;
    mk::read_file(p.string(), out);
    return out;
}

// A dataset: images/<key>.jpg and masks/<key>.png for every key given, all
// w x h, each frame's mask seeded by its index.
struct Fixture {
    fs::path root, images, masks, layer;
    std::vector<std::string> keys;
    int w = 0, h = 0;
};

Fixture make_dataset(const char* name, int w, int h,
                     const std::vector<std::string>& keys, bool with_masks = true) {
    Fixture f;
    f.root = scratch(name);
    f.images = f.root / "images";
    f.masks = f.root / "masks";
    f.layer = f.root / mk::kLayerDirName;
    f.keys = keys;
    f.w = w;
    f.h = h;
    for (size_t i = 0; i < keys.size(); i++) {
        write_jpg_rgb(f.images / (keys[i] + ".jpg"), w, h, synth_rgb(w, h, (uint32_t)i));
        if (with_masks)
            write_png_gray(f.masks / (keys[i] + ".png"), w, h, synth_mask(w, h, (uint32_t)i));
    }
    return f;
}

// ---------------------------------------------------------------------------
// Task 1: FNV-1a
// ---------------------------------------------------------------------------

void test_fnv() {
    // Published FNV-1a 64 test vectors.
    check(mk::fnv1a64(nullptr, 0) == 0xcbf29ce484222325ull, "fnv1a64 of empty");
    const char* a = "a";
    check(mk::fnv1a64((const uint8_t*)a, 1) == 0xaf63dc4c8601ec8cull, "fnv1a64 of \"a\"");
    const char* foobar = "foobar";
    check(mk::fnv1a64((const uint8_t*)foobar, 6) == 0x85944171f73967e8ull,
          "fnv1a64 of \"foobar\"");
    check(mk::fnv_hex(0xcbf29ce484222325ull) == "cbf29ce484222325", "fnv_hex pads to 16");
    check(mk::fnv_hex(1) == "0000000000000001", "fnv_hex leading zeros");
    uint64_t v = 0;
    check(mk::fnv_parse("cbf29ce484222325", v) && v == 0xcbf29ce484222325ull, "fnv_parse");
    check(!mk::fnv_parse("cbf29ce48422232", v), "fnv_parse rejects 15 chars");
    check(!mk::fnv_parse("cbf29ce48422232g", v), "fnv_parse rejects non-hex");
    check(mk::fnv1a64((const uint8_t*)a, 1) != mk::fnv1a64((const uint8_t*)"b", 1),
          "different bytes differ");
}

// ---------------------------------------------------------------------------
// Task 2: composite truth table, keys, paths
// ---------------------------------------------------------------------------

void test_composite_truth_table() {
    // Every (base, drop, keep) state. Polarity: 255 = keep.
    const uint8_t base[8] = {0, 0, 0, 0, 255, 255, 255, 255};
    const uint8_t drop[8] = {0, 255, 0, 255, 0, 255, 0, 255};
    const uint8_t keep[8] = {0, 0, 255, 255, 0, 0, 255, 255};
    uint8_t out[8];
    mk::composite(base, drop, keep, 8, out);
    const uint8_t want[8] = {0, 0, 255, 255, 255, 0, 255, 255};
    for (int i = 0; i < 8; i++)
        check(out[i] == want[i], "composite state " + std::to_string(i));
    // Absent layers mean "no correction".
    mk::composite(base, nullptr, nullptr, 8, out);
    for (int i = 0; i < 8; i++)
        check(out[i] == base[i], "composite with no layers is the base, " + std::to_string(i));
    mk::composite(base, drop, nullptr, 8, out);
    check(out[5] == 0 && out[4] == 255, "drop only");
    mk::composite(base, nullptr, keep, 8, out);
    check(out[2] == 255 && out[0] == 0, "keep only");
}

void test_keys_and_paths() {
    const std::string root = "/data/set/images";
    check(mk::frame_key(root, "/data/set/images/00023.jpg") == "00023", "key at root");
    check(mk::frame_key(root, "/data/set/images/cam0/00023.jpg") == "cam0/00023", "key in camera");
    check(mk::frame_key(root + "/", "/data/set/images/cam0/left/x.png") == "cam0/left/x",
          "key with trailing slash on root");
    check(mk::frame_key(root, "/elsewhere/y.jpg") == "y", "key outside root is the stem");
    check(mk::normalize_dir("/a/b/") == "/a/b", "normalize_dir strips the slash");
    check(mk::normalize_dir("/a/./b/../c") == "/a/c", "normalize_dir is lexical");
    check(mk::mask_file("/data/set/masks", "cam0/00023") == "/data/set/masks/cam0/00023.png",
          "mask_file");
    check(mk::layer_file("/data/set/mask_edits", "cam0/00023", mk::Layer::Base) ==
              "/data/set/mask_edits/cam0/00023.base.png", "layer_file base");
    check(mk::layer_file("/data/set/mask_edits", "00023", mk::Layer::Drop) ==
              "/data/set/mask_edits/00023.drop.png", "layer_file drop");
    check(mk::layer_file("/data/set/mask_edits", "00023", mk::Layer::Keep) ==
              "/data/set/mask_edits/00023.keep.png", "layer_file keep");
}

}  // namespace

int main() {
    test_fnv();
    test_composite_truth_table();
    test_keys_and_paths();
    std::printf("%s: %d failure(s)\n", SS_FILE, g_failures);
    return g_failures;
}
