// mask_doc_test -- app/gui/mask/: the correction layer on disk, the document
// in memory, the view math, and the session's worker, over synthetic frames
// whose every pixel is known. SS_MASK_BENCH=<dir> also runs the timing
// floors at 7680x3840 and writes that fixture dataset into <dir>.

#include "app/FrameLook.h"
#include "app/FrameMask.h"
#include "app/gui/edit/Selection.h"
#include "app/gui/mask/Livewire.h"
#include "app/gui/mask/MaskDoc.h"
#include "app/gui/mask/PathTool.h"
#include "app/gui/mask/MaskLayer.h"
#include "app/gui/mask/MaskSession.h"
#include "app/gui/mask/MaskWindow.h"
#include "core/ImageOrient.h"
#include "core/SourcePath.h"
#include "external/stb_image_write.h"
#include "i18n/catalog/MaskEdit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
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
    // lexically_relative does not normalize its own argument, so a dot
    // segment in image_root (not the file) needs normalize_dir's call.
    check(mk::frame_key("/data/set/other/../images", "/data/set/images/cam0/00023.jpg") ==
              "cam0/00023",
          "key with dot segment in root");
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

// ---------------------------------------------------------------------------
// Task 3: PNG bytes, atomic write, index round trip
// ---------------------------------------------------------------------------

void test_png_and_atomic_write() {
    const fs::path d = scratch("png");
    const std::vector<uint8_t> px = synth_mask(64, 48, 7);
    std::vector<uint8_t> png;
    check(mk::encode_gray_png(px.data(), 64, 48, png), "encode_gray_png");
    check(png.size() > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G', "PNG signature");
    const fs::path p = d / "sub" / "a.png";
    check(mk::write_file_atomic(p.string(), png.data(), png.size()), "write_file_atomic creates dirs");
    bool tmp_leftover = false;
    for (const auto& entry : fs::directory_iterator(d / "sub"))
        if (entry.path().extension() == ".tmp") tmp_leftover = true;
    check(!tmp_leftover, "no temp file left behind");
    check(file_bytes(p) == png, "file holds exactly the encoded bytes");
    int w = 0, h = 0;
    std::vector<uint8_t> back;
    check(app::load_stencil(p.string(), w, h, back) && w == 64 && h == 48, "load_stencil reads it");
    check(back == px, "decoded pixels are the input, every one");
    uint64_t fp = 0;
    check(mk::fingerprint_file(p.string(), fp) && fp == mk::fnv1a64(png.data(), png.size()),
          "fingerprint_file is the fingerprint of the bytes");
    // Writing again replaces the directory entry rather than the inode: a
    // hard link to the first file must keep the first bytes.
    const fs::path link = d / "sub" / "link.png";
    std::error_code ec;
    fs::create_hard_link(p, link, ec);
    if (!ec) {
        const std::vector<uint8_t> other = {1, 2, 3, 4};
        check(mk::write_file_atomic(p.string(), other.data(), other.size()), "rewrite");
        check(file_bytes(link) == png, "hard link still holds the old bytes");
        check(file_bytes(p) == other, "path holds the new bytes");
    }
    check(!mk::fingerprint_file((d / "missing.png").string(), fp), "fingerprint of a missing file fails");
}

// A fixed temp name (<dst>.tmp) lets two writers of the same destination
// fopen() the same inode and interleave writes before either renames. The
// property that rules that out: every call gets its own sibling name.
void test_temp_write_path_unique() {
    const fs::path d = scratch("temp_names");
    const fs::path dst = d / "shared.bin";
    const std::string a = mk::temp_write_path(dst.string());
    const std::string b = mk::temp_write_path(dst.string());
    check(a != b, "temp_write_path differs across calls for the same destination");
    check(fs::path(a).parent_path() == dst.parent_path() &&
              fs::path(b).parent_path() == dst.parent_path(),
          "temp paths are siblings of the destination");
    // Concurrent-in-flight writers do not collide on disk, and the write
    // that lands last still wins cleanly -- the hard-link guarantee holds.
    const std::vector<uint8_t> first = {9, 9, 9};
    check(mk::write_file_atomic(dst.string(), first.data(), first.size()), "first write lands");
    const fs::path link = d / "shared.link";
    std::error_code ec;
    fs::create_hard_link(dst, link, ec);
    if (!ec) {
        const std::vector<uint8_t> second = {1, 2};
        check(mk::write_file_atomic(dst.string(), second.data(), second.size()), "second write lands");
        check(file_bytes(link) == first, "hard link unaffected by the second write");
        check(file_bytes(dst) == second, "destination holds the second write");
    }
}

void test_index_roundtrip() {
    const fs::path d = scratch("index");
    mk::LayerIndex idx;
    std::string err;
    check(idx.load(d.string(), err) && idx.frames.empty(), "absent index loads empty");
    idx.mask_root = "/data/set/masks";
    mk::IndexEntry e;
    e.base_fp = 0xcbf29ce484222325ull;
    e.composite_fp = 0x85944171f73967e8ull;
    e.kept = 0.4375f;
    e.saved_at = "2026-09-21T10:00:00Z";
    idx.frames["cam0/00023"] = e;
    idx.frames["00001"] = mk::IndexEntry{};
    check(idx.save(d.string(), err), "index saves: " + err);
    check(fs::exists(d / mk::kIndexFileName), "index.json exists");
    mk::LayerIndex back;
    check(back.load(d.string(), err), "index loads: " + err);
    check(back.mask_root == "/data/set/masks", "mask_root round trips");
    check(back.frames.size() == 2, "two entries");
    const mk::IndexEntry& r = back.frames["cam0/00023"];
    check(r.base_fp == e.base_fp, "base fingerprint round trips exactly (64 bits)");
    check(r.composite_fp == e.composite_fp, "composite fingerprint round trips exactly");
    check(r.kept == e.kept, "kept round trips");
    check(r.saved_at == e.saved_at, "saved_at round trips");
    check(back.frames["00001"].base_fp == 0 && back.frames["00001"].composite_fp == 0,
          "zero fingerprints round trip");
    const std::vector<uint8_t> text = file_bytes(d / mk::kIndexFileName);
    const std::string s(text.begin(), text.end());
    check(s.find("\"85944171f73967e8\"") != std::string::npos, "fingerprints are hex strings");
    check(s.find("spirula_mask_edits") != std::string::npos, "marker key present");
    // Garbage is an error, not an empty index.
    const std::vector<uint8_t> junk = {'{', 'x'};
    mk::write_file_atomic((d / mk::kIndexFileName).string(), junk.data(), junk.size());
    check(!back.load(d.string(), err) && !err.empty(), "corrupt index fails with a message");
    check(mk::utc_now_iso().size() == 20 && mk::utc_now_iso()[10] == 'T', "utc_now_iso shape");
}

// ---------------------------------------------------------------------------
// Task 4: save, fingerprint decides, revert byte-exact
// ---------------------------------------------------------------------------

std::vector<uint8_t> box_layer(int w, int h, int x0, int y0, int x1, int y1) {
    std::vector<uint8_t> v((size_t)w * h, 0);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) v[(size_t)y * w + x] = 255;
    return v;
}

void test_save_and_layers_roundtrip() {
    Fixture f = make_dataset("save", 64, 48, {"a", "cam0/b"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    const std::vector<uint8_t> original = file_bytes(f.masks / "a.png");
    int w = 0, h = 0;
    std::vector<uint8_t> base;
    app::load_stencil((f.masks / "a.png").string(), w, h, base);
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> keep = box_layer(64, 48, 40, 20, 50, 30);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    check(mk::base_state(mask_root, "a", idx) == mk::BaseState::Unedited, "unedited before save");
    check(mk::save_frame(layer_root, mask_root, "a", 64, 48, base.data(), drop.data(),
                         keep.data(), true, idx, err), "save_frame: " + err);
    // The base is a byte copy of the mask as the run wrote it.
    check(file_bytes(f.layer / "a.base.png") == original, "base is byte-identical to the original");
    // The layers read back as the same sets, through the app's own reader.
    mk::FrameLayers back;
    std::string warn;
    check(mk::read_layers(layer_root, "a", 64, 48, back, warn) && warn.empty(), "read_layers");
    check(back.drop == drop, "drop layer round trips");
    check(back.keep == keep, "keep layer round trips");
    int lw, lh;
    std::vector<uint8_t> raw;
    app::load_stencil((f.layer / "a.drop.png").string(), lw, lh, raw);
    check(raw == drop, "load_stencil reads .drop.png as the same set");
    // The layer files are 0/255 and nothing else.
    std::vector<uint8_t> png_px;
    check(app::load_stencil((f.layer / "a.keep.png").string(), lw, lh, png_px), "keep decodes");
    bool binary = true;
    for (uint8_t v : back.keep) binary = binary && (v == 0 || v == 255);
    check(binary, "keep layer is binary");
    // masks/a.png is now the composite.
    std::vector<uint8_t> comp;
    app::load_stencil((f.masks / "a.png").string(), lw, lh, comp);
    std::vector<uint8_t> want(base.size());
    mk::composite(base.data(), drop.data(), keep.data(), base.size(), want.data());
    check(comp == want, "masks/a.png holds the composite");
    // Index entry.
    check(idx.frames.count("a") == 1, "index has the entry");
    uint64_t fp = 0;
    mk::fingerprint_file((f.masks / "a.png").string(), fp);
    check(idx.frames["a"].composite_fp == fp, "composite fingerprint is the file's");
    mk::fingerprint_file((f.layer / "a.base.png").string(), fp);
    check(idx.frames["a"].base_fp == fp, "base fingerprint is the file's");
    size_t kept = 0;
    for (uint8_t v : want) kept += v ? 1 : 0;
    check(std::abs(idx.frames["a"].kept - (float)kept / (64.0f * 48.0f)) < 1e-6f, "kept fraction recorded");
    check(mk::base_state(mask_root, "a", idx) == mk::BaseState::Unchanged, "unchanged after save");
    mk::LayerIndex reloaded;
    check(reloaded.load(layer_root, err) && reloaded.frames.count("a") == 1, "index was written to disk");
    // A second save keeps the base as it was (never re-copied over).
    const std::vector<uint8_t> drop2 = box_layer(64, 48, 0, 0, 2, 2);
    check(mk::save_frame(layer_root, mask_root, "a", 64, 48, base.data(), drop2.data(),
                         keep.data(), true, idx, err), "second save");
    check(file_bytes(f.layer / "a.base.png") == original, "base untouched by a second save");
    // Nested key lands nested.
    check(mk::save_frame(layer_root, mask_root, "cam0/b", 64, 48, base.data(), drop.data(),
                         keep.data(), true, idx, err), "save nested key");
    check(fs::exists(f.layer / "cam0" / "b.drop.png"), "nested layer path");
    // A layer of another size is skipped and named.
    write_png_gray(f.layer / "a.keep.png", 32, 24, std::vector<uint8_t>(32 * 24, 255));
    mk::FrameLayers mism;
    check(!mk::read_layers(layer_root, "a", 64, 48, mism, warn) && !warn.empty(),
          "size mismatch is reported");
    check(mism.keep.size() == 64 * 48 && mism.keep[0] == 0, "mismatched layer reads as zero");
    check(mism.drop == drop2, "the other layer still reads");
    // Both wrong: both are named, not only the second.
    write_png_gray(f.layer / "a.drop.png", 32, 24, std::vector<uint8_t>(32 * 24, 255));
    warn.clear();
    check(!mk::read_layers(layer_root, "a", 64, 48, mism, warn) &&
              warn.find("a.drop.png") != std::string::npos &&
              warn.find("a.keep.png") != std::string::npos,
          "both mismatched layers are named in the warning");
}

void test_fingerprint_decides() {
    Fixture f = make_dataset("rebase", 64, 48, {"a", "b"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    int w, h;
    std::vector<uint8_t> base_a;
    app::load_stencil((f.masks / "a.png").string(), w, h, base_a);
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> keep = box_layer(64, 48, 40, 20, 50, 30);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    check(mk::save_frame(layer_root, mask_root, "a", 64, 48, base_a.data(), drop.data(),
                         keep.data(), true, idx, err), "save a");
    const std::vector<uint8_t> composite_bytes = file_bytes(f.masks / "a.png");

    // Same bytes: nothing regenerated, nothing touched.
    mk::BaseState st;
    check(mk::recomposite_frame(layer_root, mask_root, "a", idx, st, err) &&
              st == mk::BaseState::Unchanged, "same bytes are Unchanged");
    check(mk::recomposite_all(layer_root, err) == 0, "recomposite_all over an unchanged set is 0");

    // Different bytes: the run regenerated it. The NEW mask must become the
    // base, byte for byte, and the composite must be the layers over it.
    const std::vector<uint8_t> new_mask = synth_mask(64, 48, 99);
    write_png_gray(f.masks / "a.png", 64, 48, new_mask);
    const std::vector<uint8_t> new_bytes = file_bytes(f.masks / "a.png");
    check(new_bytes != composite_bytes, "fixture: the rewrite really differs");
    check(mk::base_state(mask_root, "a", idx) == mk::BaseState::Regenerated, "Regenerated detected");
    check(mk::recomposite_all(layer_root, err) == 1, "recomposite_all re-based one: " + err);
    check(file_bytes(f.layer / "a.base.png") == new_bytes, ".base.png is the regenerated file, byte for byte");
    std::vector<uint8_t> comp;
    app::load_stencil((f.masks / "a.png").string(), w, h, comp);
    std::vector<uint8_t> want(comp.size());
    mk::composite(new_mask.data(), drop.data(), keep.data(), want.size(), want.data());
    check(comp == want, "composite is the layers over the NEW base");
    mk::LayerIndex re;
    re.load(layer_root, err);
    uint64_t fp;
    mk::fingerprint_file((f.masks / "a.png").string(), fp);
    check(re.frames["a"].composite_fp == fp, "index updated with the new composite fingerprint");
    check(re.frames["a"].base_fp == mk::fnv1a64(new_bytes.data(), new_bytes.size()),
          "index updated with the new base fingerprint");
    // Criterion #3's failure signature: a base equal to a previous composite.
    check(file_bytes(f.layer / "a.base.png") != composite_bytes, "no base equals an old composite");

    // Missing: a cancelled re-run. Layers stay, nothing is written.
    fs::remove(f.masks / "a.png");
    check(mk::base_state(mask_root, "a", idx) == mk::BaseState::Missing, "Missing detected");
    check(mk::recomposite_all(layer_root, err) == 0, "missing is not re-based");
    check(fs::exists(f.layer / "a.drop.png") && fs::exists(f.layer / "a.keep.png"), "layers kept");
    check(!fs::exists(f.masks / "a.png"), "no mask conjured");

    // A layer at another size refuses to re-base and reports, and does not
    // touch the base. "a" is the batch's only entry and it failed, so the
    // honest count is 0 rebased, not -1, reported through `err`.
    write_png_gray(f.masks / "a.png", 64, 48, synth_mask(64, 48, 5));
    write_png_gray(f.layer / "a.drop.png", 32, 24, std::vector<uint8_t>(32 * 24, 255));
    const std::vector<uint8_t> base_before = file_bytes(f.layer / "a.base.png");
    check(mk::recomposite_all(layer_root, err) == 0 && err.find("a") != std::string::npos,
          "mismatch refuses without a batch abort: " + err);
    check(file_bytes(f.layer / "a.base.png") == base_before, "base untouched on refusal");
}

void test_revert_is_byte_exact() {
    Fixture f = make_dataset("revert", 64, 48, {"a", "b"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    const std::vector<uint8_t> original_a = file_bytes(f.masks / "a.png");
    const std::vector<uint8_t> original_b = file_bytes(f.masks / "b.png");
    int w, h;
    std::vector<uint8_t> base_a, base_b;
    app::load_stencil((f.masks / "a.png").string(), w, h, base_a);
    app::load_stencil((f.masks / "b.png").string(), w, h, base_b);
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> keep = box_layer(64, 48, 40, 20, 50, 30);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    mk::save_frame(layer_root, mask_root, "a", 64, 48, base_a.data(), drop.data(), keep.data(), true, idx, err);
    mk::save_frame(layer_root, mask_root, "b", 64, 48, base_b.data(), keep.data(), drop.data(), true, idx, err);
    check(file_bytes(f.masks / "a.png") != original_a, "fixture: a was changed by the save");
    check(mk::revert_frame(layer_root, mask_root, "a", idx, err), "revert_frame: " + err);
    check(file_bytes(f.masks / "a.png") == original_a, "masks/a.png is byte-identical to the original");
    for (const char* tag : {"a.base.png", "a.drop.png", "a.keep.png"})
        check(!fs::exists(f.layer / tag), std::string("removed ") + tag);
    check(idx.frames.count("a") == 0, "entry removed");
    check(idx.frames.count("b") == 1 && fs::exists(f.layer / "b.base.png"), "b untouched");
    mk::LayerIndex disk;
    disk.load(layer_root, err);
    check(disk.frames.count("a") == 0 && disk.frames.count("b") == 1, "index on disk agrees");
    check(mk::revert_all(layer_root, err) == 1, "revert_all reverts the remaining one");
    check(file_bytes(f.masks / "b.png") == original_b, "masks/b.png byte-identical after revert_all");
    disk.load(layer_root, err);
    check(disk.frames.empty(), "index empty after revert_all");
}

void test_save_without_mask() {
    // A frame with no mask on disk: layers and an entry, no base, no composite.
    Fixture f = make_dataset("nomask", 64, 48, {"a"}, /*with_masks=*/false);
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    const std::vector<uint8_t> all_keep(64 * 48, 255);
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> none(64 * 48, 0);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    check(mk::base_state(mask_root, "a", idx) == mk::BaseState::Missing, "no mask is Missing");
    check(mk::save_frame(layer_root, mask_root, "a", 64, 48, all_keep.data(), drop.data(),
                         none.data(), /*write_composite=*/false, idx, err), "save layers only: " + err);
    check(fs::exists(f.layer / "a.drop.png") && !fs::exists(f.layer / "a.base.png"), "layers, no base");
    check(!fs::exists(f.masks / "a.png"), "no composite written");
    check(idx.frames["a"].base_fp == 0 && idx.frames["a"].composite_fp == 0, "zero fingerprints");
    // The run then writes the mask: it is a new base, and the layers apply.
    const std::vector<uint8_t> m = synth_mask(64, 48, 3);
    write_png_gray(f.masks / "a.png", 64, 48, m);
    check(mk::recomposite_all(layer_root, err) == 1, "the arriving mask is re-based: " + err);
    std::vector<uint8_t> comp;
    int w, h;
    app::load_stencil((f.masks / "a.png").string(), w, h, comp);
    std::vector<uint8_t> want(comp.size());
    mk::composite(m.data(), drop.data(), nullptr, want.size(), want.data());
    check(comp == want, "layers applied over the arriving mask");
    // Revert of an entry that never had a base deletes the layers only.
    fs::remove(f.layer / "a.base.png");
    idx.load(layer_root, err);
    check(mk::revert_frame(layer_root, mask_root, "a", idx, err), "revert without base");
    check(fs::exists(f.masks / "a.png") && !fs::exists(f.layer / "a.drop.png"), "mask left, layers gone");
}

// ---------------------------------------------------------------------------
// Fix round 1: a removal failure must not report success, and one bad frame
// must not stop the rest of a batch.
// ---------------------------------------------------------------------------

// fs::remove refuses a non-empty directory everywhere -- a portable stand-in
// for revert's removal step failing, without a platform-specific immutable
// flag (the reviewer used chflags uchg, macOS-only).
void test_revert_reports_removal_failure() {
    Fixture f = make_dataset("revert_fail", 64, 48, {"a"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    int w, h;
    std::vector<uint8_t> base_a;
    app::load_stencil((f.masks / "a.png").string(), w, h, base_a);
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> keep = box_layer(64, 48, 40, 20, 50, 30);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    check(mk::save_frame(layer_root, mask_root, "a", 64, 48, base_a.data(), drop.data(),
                         keep.data(), true, idx, err), "save before forcing a removal failure");

    const fs::path drop_path = f.layer / "a.drop.png";
    std::error_code ec;
    fs::remove(drop_path, ec);
    fs::create_directories(drop_path / "nested", ec);
    check(fs::is_directory(drop_path) && !fs::is_empty(drop_path),
          "fixture: a.drop.png is a non-empty directory fs::remove refuses");

    check(!mk::revert_frame(layer_root, mask_root, "a", idx, err) && !err.empty(),
          "revert_frame reports the removal failure: " + err);
    check(err.find("a.drop.png") != std::string::npos, "the failing path is named");
    // The invariant that matters: no layer files on disk with no entry.
    check(idx.frames.count("a") == 1, "index entry kept -- an orphan file must keep its owner");
    mk::LayerIndex disk;
    disk.load(layer_root, err);
    check(disk.frames.count("a") == 1, "index on disk still names the frame");
    check(fs::exists(drop_path), "the undeletable layer path is still there, and still tracked");
}

void test_recomposite_all_continues_past_failure() {
    Fixture f = make_dataset("recomp_continue", 64, 48, {"a", "b", "c"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> keep = box_layer(64, 48, 40, 20, 50, 30);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    for (const char* k : {"a", "b", "c"}) {
        int w, h;
        std::vector<uint8_t> base;
        app::load_stencil((f.masks / (std::string(k) + ".png")).string(), w, h, base);
        check(mk::save_frame(layer_root, mask_root, k, 64, 48, base.data(), drop.data(),
                             keep.data(), true, idx, err), std::string("save ") + k);
    }
    const std::vector<uint8_t> base_b_before = file_bytes(f.layer / "b.base.png");

    // All three are regenerated by "the run"; "b", the middle key in sorted
    // order, is the one whose layer is sized wrong and must refuse.
    for (const char* k : {"a", "b", "c"})
        write_png_gray(f.masks / (std::string(k) + ".png"), 64, 48, synth_mask(64, 48, 42));
    write_png_gray(f.layer / "b.drop.png", 32, 24, std::vector<uint8_t>(32 * 24, 255));

    const int rebased = mk::recomposite_all(layer_root, err);
    check(rebased == 2, "a and c rebase despite b's refusal, got " + std::to_string(rebased));
    check(err.find('b') != std::string::npos, "the failing key is named in the batch error: " + err);
    // recomposite_all loads its own index off disk; read that back rather
    // than the caller's now-stale copy.
    mk::LayerIndex disk;
    check(disk.load(layer_root, err), "reload the persisted index: " + err);
    check(mk::base_state(mask_root, "a", disk) == mk::BaseState::Unchanged &&
              mk::base_state(mask_root, "c", disk) == mk::BaseState::Unchanged,
          "a and c are durably rebased, not just counted");
    check(mk::base_state(mask_root, "b", disk) == mk::BaseState::Regenerated,
          "b is still pending -- it was skipped, not silently marked done");
    check(file_bytes(f.layer / "b.base.png") == base_b_before,
          "b's base untouched: refusing one frame must not corrupt it");
}

// Fix round 2: revert_all had the same abort-on-first-failure shape
// recomposite_all was corrected out of last round, and round 1 made it more
// reachable by turning a swallowed error into a first-class false.
void test_revert_all_continues_past_failure() {
    Fixture f = make_dataset("revert_continue", 64, 48, {"a", "b", "c"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    const std::vector<uint8_t> original_a = file_bytes(f.masks / "a.png");
    const std::vector<uint8_t> original_c = file_bytes(f.masks / "c.png");
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> keep = box_layer(64, 48, 40, 20, 50, 30);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    for (const char* k : {"a", "b", "c"}) {
        int w, h;
        std::vector<uint8_t> base;
        app::load_stencil((f.masks / (std::string(k) + ".png")).string(), w, h, base);
        check(mk::save_frame(layer_root, mask_root, k, 64, 48, base.data(), drop.data(),
                             keep.data(), true, idx, err), std::string("save ") + k);
    }
    check(file_bytes(f.masks / "a.png") != original_a && file_bytes(f.masks / "c.png") != original_c,
          "fixture: a and c were changed by the save");

    // "b", the middle key in sorted order, cannot have its .drop.png removed.
    const fs::path drop_b = f.layer / "b.drop.png";
    std::error_code ec;
    fs::remove(drop_b, ec);
    fs::create_directories(drop_b / "nested", ec);
    check(fs::is_directory(drop_b) && !fs::is_empty(drop_b),
          "fixture: b.drop.png is a non-empty directory fs::remove refuses");

    const int reverted = mk::revert_all(layer_root, err);
    check(reverted == 2, "a and c revert despite b's refusal, got " + std::to_string(reverted));
    check(err.find('b') != std::string::npos, "the failing key is named in the batch error: " + err);
    check(file_bytes(f.masks / "a.png") == original_a, "a genuinely reverted, not just counted");
    check(file_bytes(f.masks / "c.png") == original_c, "c genuinely reverted -- attempted past b");
    for (const char* tag : {"a.base.png", "a.drop.png", "a.keep.png",
                            "c.base.png", "c.drop.png", "c.keep.png"})
        check(!fs::exists(f.layer / tag), std::string("removed ") + tag);

    mk::LayerIndex disk;
    check(disk.load(layer_root, err), "reload the persisted index: " + err);
    check(disk.frames.count("a") == 0 && disk.frames.count("c") == 0,
          "a and c fully reverted on disk");
    check(disk.frames.count("b") == 1, "b kept its entry -- skipped, not silently marked done");
    check(fs::exists(drop_b), "b's undeletable layer path is still there");
}

// revert_all lacked recomposite_all's empty-mask_root guard: a hand-edited or
// downgraded index.json without mask_root resolves mask_file("", key) to a
// bare "<key>.png", writing outside the dataset relative to the process cwd.
void test_revert_all_guards_empty_mask_root() {
    Fixture f = make_dataset("revert_empty_root", 64, 48, {"a"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    const std::vector<uint8_t> drop = box_layer(64, 48, 4, 4, 14, 14);
    const std::vector<uint8_t> keep = box_layer(64, 48, 40, 20, 50, 30);
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err;
    int w, h;
    std::vector<uint8_t> base;
    app::load_stencil((f.masks / "a.png").string(), w, h, base);
    check(mk::save_frame(layer_root, mask_root, "a", 64, 48, base.data(), drop.data(),
                         keep.data(), true, idx, err), "save a: " + err);
    check(fs::exists(f.layer / "a.base.png"), "fixture: a.base.png exists to revert from");
    // save_frame already composited over masks/a.png; that composite, not the
    // pre-save original, is what a no-op revert_all must leave standing.
    const std::vector<uint8_t> composited_a = file_bytes(f.masks / "a.png");

    // What a hand-edited or downgraded index.json leaves: LayerIndex::load
    // clears mask_root before parsing, so a document missing that field (or
    // set to "") loads with the frame entries intact but mask_root empty.
    idx.mask_root.clear();
    check(idx.save(layer_root, err), "persist an index with empty mask_root: " + err);

    const fs::path stray = fs::current_path() / "a.png";
    std::error_code ec;
    fs::remove(stray, ec);

    const int reverted = mk::revert_all(layer_root, err);
    check(reverted == 0, "revert_all on an empty mask_root writes nothing, got " +
                             std::to_string(reverted));
    check(!fs::exists(stray),
          "revert_all did not write a bare '<key>.png' into the working directory");
    check(file_bytes(f.masks / "a.png") == composited_a, "masks/a.png left untouched");
    check(fs::exists(f.layer / "a.base.png"), "layer files untouched -- the guard returns early");
    fs::remove(stray, ec);
}

// ---------------------------------------------------------------------------
// Task 5: the document
// ---------------------------------------------------------------------------

gui::Stencil box_stencil(int W, int H, int x0, int y0, int x1, int y1) {
    gui::ShapeStroke s;
    s.kind = gui::ShapeKind::Box;
    s.pts = {(float)x0, (float)y0, (float)x1, (float)y1};
    gui::Stencil st;
    gui::rasterize_shape(s, W, H, st);
    return st;
}

bool exclusive(const mk::MaskDoc& d) {
    for (size_t i = 0; i < d.drop().size(); i++)
        if (d.drop()[i] && d.keep()[i]) return false;
    return true;
}

bool composite_consistent(const mk::MaskDoc& d) {
    std::vector<uint8_t> want(d.base().size());
    mk::composite(d.base().data(), d.drop().data(), d.keep().data(), want.size(), want.data());
    if (want != d.composite()) return false;
    int64_t kept = 0;
    for (uint8_t v : want) kept += v ? 1 : 0;
    return kept == d.kept();
}

void test_doc_load_and_paint() {
    Fixture f = make_dataset("doc", 64, 48, {"a"});
    const std::string mask_root = f.masks.string(), layer_root = f.layer.string();
    mk::LayerIndex idx;
    idx.mask_root = mask_root;
    std::string err, warn;
    mk::MaskDoc d;
    check(d.load(layer_root, mask_root, "a", 64, 48, idx, err, warn), "load: " + err);
    check(d.width() == 64 && d.height() == 48, "size from the mask");
    check(d.base_state() == mk::BaseState::Unedited, "unedited");
    check(d.base() == synth_mask(64, 48, 0), "base is the mask on disk");
    check(d.composite() == d.base() && !d.dirty(), "composite is the base, clean");
    check(composite_consistent(d), "kept count at load");
    const uint64_t rev0 = d.revision();

    // ForceDrop over a keep region. `bounds` deliberately larger than the box.
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 20, 16, 30, 26), mk::Rect{10, 10, 40, 40});
    check(d.dirty() && d.revision() != rev0, "dirty after a paint");
    check(d.drop()[(size_t)20 * 64 + 25] == 255 && d.composite()[(size_t)20 * 64 + 25] == 0,
          "forced drop: layer set and composite 0");
    check(d.drop()[(size_t)20 * 64 + 35] == 0, "outside the box untouched");
    check(exclusive(d) && composite_consistent(d), "invariant after ForceDrop");
    check(d.last_change().x0 <= 20 && d.last_change().x1 >= 30, "last_change covers the stroke");

    // ForceKeep over a dropped region, overlapping the drop box: keep wins,
    // and the overlap leaves the drop layer.
    d.paint(mk::Paint::ForceKeep, box_stencil(64, 48, 25, 20, 60, 44), mk::Rect{0, 0, 64, 48});
    check(d.keep()[(size_t)30 * 64 + 50] == 255 && d.composite()[(size_t)30 * 64 + 50] == 255,
          "forced keep: layer set and composite 255");
    check(d.drop()[(size_t)22 * 64 + 27] == 0 && d.keep()[(size_t)22 * 64 + 27] == 255,
          "overlap: drop cleared where keep painted");
    check(exclusive(d) && composite_consistent(d), "invariant after ForceKeep");

    // ForceDrop back over PART of the still-standing keep region, with no
    // Clear in between: the overlap must clear keep there, not just set drop.
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 40, 25, 55, 35), mk::Rect{0, 0, 64, 48});
    check(d.drop()[(size_t)30 * 64 + 45] == 255 && d.keep()[(size_t)30 * 64 + 45] == 0,
          "ForceDrop over a keep region clears keep, not just sets drop");
    check(d.keep()[(size_t)40 * 64 + 58] == 255, "keep survives outside the new drop box");
    check(exclusive(d) && composite_consistent(d), "invariant after ForceDrop-over-keep overlap");

    // Clear puts both layers back over its stencil.
    d.paint(mk::Paint::Clear, box_stencil(64, 48, 0, 0, 64, 48), mk::Rect{0, 0, 64, 48});
    check(d.drop() == std::vector<uint8_t>(64 * 48, 0) && d.keep() == std::vector<uint8_t>(64 * 48, 0),
          "clear everything");
    check(d.composite() == d.base() && composite_consistent(d), "composite back to the base");

    // A stencil pixel outside `bounds` is ignored, so bounds really bound.
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 0, 0, 64, 48), mk::Rect{0, 0, 8, 8});
    check(d.drop()[0] == 255 && d.drop()[(size_t)10 * 64 + 10] == 0, "bounds clip the stencil");
    check(composite_consistent(d), "kept count tracks a clipped paint");

    // A bounds rectangle that runs off the image edge must be clipped to the
    // canvas before it is used, not just to the stencil.
    d.paint(mk::Paint::ForceKeep, box_stencil(64, 48, 0, 0, 64, 48), mk::Rect{60, 44, 200, 200});
    check(d.keep()[(size_t)47 * 64 + 63] == 255, "the clipped corner is painted");
    check(d.keep()[(size_t)43 * 64 + 62] == 0 && d.drop()[(size_t)43 * 64 + 62] == 0,
          "just above the off-edge bounds is untouched");
    check(exclusive(d) && composite_consistent(d), "invariant after an off-edge bounds paint");

    // Save through the document, reload, same planes.
    check(d.save(layer_root, mask_root, idx, err), "doc save: " + err);
    check(!d.dirty(), "clean after save");
    mk::MaskDoc e;
    check(e.load(layer_root, mask_root, "a", 64, 48, idx, err, warn), "reload");
    check(e.base_state() == mk::BaseState::Unchanged, "unchanged after our own save");
    check(e.drop() == d.drop() && e.keep() == d.keep() && e.composite() == d.composite(),
          "planes survive a save and load");
    check(mk::clip(mk::Rect{-5, -5, 100, 100}, 64, 48).x1 == 64, "clip");
    check(mk::join(mk::Rect{1, 1, 2, 2}, mk::Rect{5, 5, 9, 9}).x1 == 9, "join");
    check(mk::join(mk::Rect{}, mk::Rect{5, 5, 9, 9}).x0 == 5, "join with empty");
}

void test_doc_without_mask() {
    Fixture f = make_dataset("docnomask", 64, 48, {"a"}, false);
    mk::LayerIndex idx;
    idx.mask_root = f.masks.string();
    std::string err, warn;
    mk::MaskDoc d;
    check(d.load(f.layer.string(), f.masks.string(), "a", 64, 48, idx, err, warn), "load without mask");
    check(d.base_state() == mk::BaseState::Missing, "state Missing");
    check(d.base() == std::vector<uint8_t>(64 * 48, 255), "base is all keep");
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 0, 0, 8, 8), mk::Rect{0, 0, 64, 48});
    check(d.save(f.layer.string(), f.masks.string(), idx, err), "save without mask: " + err);
    check(!fs::exists(f.masks / "a.png") && fs::exists(f.layer / "a.drop.png"), "layers only");
}

// ---------------------------------------------------------------------------
// Carried from the Task 5 review: read_rect/write_rect had no coverage, and
// a paint that flips no bytes must not dirty the document.
// ---------------------------------------------------------------------------

void test_read_write_rect_roundtrip() {
    Fixture f = make_dataset("rw_rect", 64, 48, {"a"});
    mk::LayerIndex idx;
    idx.mask_root = f.masks.string();
    std::string err, warn;
    mk::MaskDoc d;
    check(d.load(f.layer.string(), f.masks.string(), "a", 64, 48, idx, err, warn), "load");
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 5, 5, 20, 20), mk::Rect{0, 0, 64, 48});
    d.paint(mk::Paint::ForceKeep, box_stencil(64, 48, 15, 15, 40, 40), mk::Rect{0, 0, 64, 48});

    const mk::Rect r{3, 3, 45, 45};
    std::vector<uint8_t> drop_r, keep_r;
    d.read_rect(r, drop_r, keep_r);
    check(drop_r != keep_r, "fixture: the two layers differ inside the rect");

    d.write_rect(r, drop_r.data(), keep_r.data());
    std::vector<uint8_t> drop_r2, keep_r2;
    d.read_rect(r, drop_r2, keep_r2);
    check(drop_r2 == drop_r && keep_r2 == keep_r, "read_rect/write_rect round trip is byte-exact");
}

void test_noop_paint_does_not_dirty() {
    Fixture f = make_dataset("noop_paint", 64, 48, {"a"});
    mk::LayerIndex idx;
    idx.mask_root = f.masks.string();
    std::string err, warn;
    mk::MaskDoc d;
    check(d.load(f.layer.string(), f.masks.string(), "a", 64, 48, idx, err, warn), "load");
    const uint64_t rev0 = d.revision();
    const mk::Rect last0 = d.last_change();

    // Fully off-canvas: box_stencil clips to an empty run, so every pixel is 0.
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 100, 100, 120, 120), mk::Rect{0, 0, 64, 48});
    check(d.revision() == rev0, "a paint that flips no bytes leaves the revision unchanged");
    check(!d.dirty(), "and leaves the doc clean");
    check(!d.can_undo() && d.history_size() == 0, "and pushes no undo entry");
    check(d.last_change().x0 == last0.x0 && d.last_change().x1 == last0.x1 &&
          d.last_change().y0 == last0.y0 && d.last_change().y1 == last0.y1,
          "and reports no change rectangle");
}

// ---------------------------------------------------------------------------
// Task 6: undo restores exactly; caps
// ---------------------------------------------------------------------------

void test_undo_redo() {
    Fixture f = make_dataset("undo", 64, 48, {"a"});
    mk::LayerIndex idx;
    idx.mask_root = f.masks.string();
    std::string err, warn;
    mk::MaskDoc d;
    check(d.load(f.layer.string(), f.masks.string(), "a", 64, 48, idx, err, warn), "load");
    const std::vector<uint8_t> drop0 = d.drop(), keep0 = d.keep(), comp0 = d.composite();
    check(!d.can_undo() && !d.can_redo() && d.history_size() == 0, "empty history");

    // 96 strokes, alternating modes, each different.
    for (int k = 0; k < 96; k++) {
        const int x = (k * 7) % 56, y = (k * 5) % 40;
        const mk::Paint mode = k % 3 == 0 ? mk::Paint::ForceDrop
                             : k % 3 == 1 ? mk::Paint::ForceKeep : mk::Paint::Clear;
        d.paint(mode, box_stencil(64, 48, x, y, x + 8, y + 8), mk::Rect{0, 0, 64, 48});
        check(exclusive(d), "invariant after stroke " + std::to_string(k));
    }
    check(d.history_size() == 96 && d.can_undo() && !d.can_redo(), "96 ops held");
    check(d.history_bytes() <= mk::kMaxHistoryBytes, "history bytes under the cap");
    check(d.last_label() == &spirula::i18n::msg::maskedit::op_clear, "last label is the 96th op's");
    // The 95th op (k=94, 94%3==1) is ForceKeep -- separates a ForceDrop/
    // ForceKeep label swap from the all-Clear tail this loop ends on.
    d.undo();
    check(d.last_label() == &spirula::i18n::msg::maskedit::op_keep, "label after undo is the 95th op's");
    d.redo();
    const std::vector<uint8_t> dropN = d.drop(), keepN = d.keep(), compN = d.composite();
    check(dropN != drop0 || keepN != keep0, "fixture: the strokes changed something");

    for (int k = 0; k < 96; k++) d.undo();
    check(!d.can_undo() && d.can_redo(), "at the start of history");
    check(d.drop() == drop0 && d.keep() == keep0, "96 undos restore both layers byte-exact");
    check(d.composite() == comp0 && composite_consistent(d), "composite and count restored");

    for (int k = 0; k < 96; k++) d.redo();
    check(d.drop() == dropN && d.keep() == keepN && d.composite() == compN, "96 redos replay exactly");
    check(composite_consistent(d), "count after redo");

    // Undo, then a new stroke truncates the redo branch.
    d.undo();
    d.undo();
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 1, 1, 3, 3), mk::Rect{0, 0, 64, 48});
    check(!d.can_redo() && d.history_size() == 95, "redo branch truncated");

    // The 97th op evicts the oldest: still 96, and undo bottoms out early.
    // Alternating mode keeps both strokes real changes -- a repeated identical
    // stroke is a no-op under the Task 6 fix below and would push only one.
    d.redo();
    for (int k = 0; k < 2; k++)
        d.paint(k == 0 ? mk::Paint::ForceKeep : mk::Paint::ForceDrop,
                box_stencil(64, 48, 2, 2, 4, 4), mk::Rect{0, 0, 64, 48});
    check(d.history_size() == mk::kMaxHistoryOps, "op cap holds at 96");
    // last_change reports the undone rectangle.
    d.undo();
    check(d.last_change().x0 <= 2 && d.last_change().x1 >= 4, "last_change after undo");
}

// ---------------------------------------------------------------------------
// Fix round 1: the byte cap must evict independently of the op-count cap.
// ---------------------------------------------------------------------------

void test_byte_cap_eviction() {
    Fixture f = make_dataset("bytecap", 64, 48, {"a"});
    mk::LayerIndex idx;
    idx.mask_root = f.masks.string();
    std::string err, warn;
    mk::MaskDoc d;
    check(d.load(f.layer.string(), f.masks.string(), "a", 64, 48, idx, err, warn), "load");
    const std::vector<uint8_t> drop0 = d.drop(), keep0 = d.keep();
    const mk::Rect full{0, 0, 64, 48};

    // A's own before/after RLE size, computed the same way StrokeOp does, so
    // the check below pins an exact value rather than an inequality a wrong
    // byte count could still satisfy.
    std::vector<uint8_t> bd, bk;
    d.read_rect(full, bd, bk);
    d.paint(mk::Paint::ForceDrop, box_stencil(64, 48, 4, 4, 12, 12), full);
    check(exclusive(d) && composite_consistent(d), "invariant after A");
    std::vector<uint8_t> ad, ak;
    d.read_rect(full, ad, ak);
    const size_t want_bytes_a = gui::rle_encode(bd).size() + gui::rle_encode(bk).size() +
                                 gui::rle_encode(ad).size() + gui::rle_encode(ak).size();
    check(d.history_bytes() == want_bytes_a,
          "history_bytes after one push equals its own RLE size, not an under-count");
    check(d.history_size() == 1 && d.can_undo(), "one op recorded so far");
    const std::vector<uint8_t> dropAfterA = d.drop(), keepAfterA = d.keep();

    // A cap just over A's own size forces B's push to evict A on bytes alone;
    // 2 ops sits far under kMaxHistoryOps, so the op-count cap never fires.
    d.set_history_byte_cap_for_test(want_bytes_a + 1);
    d.paint(mk::Paint::ForceKeep, box_stencil(64, 48, 30, 20, 45, 35), full);
    check(exclusive(d) && composite_consistent(d), "invariant after B");
    std::vector<uint8_t> bd2, bk2;
    d.read_rect(full, bd2, bk2);
    const size_t want_bytes_b = gui::rle_encode(dropAfterA).size() + gui::rle_encode(keepAfterA).size() +
                                 gui::rle_encode(bd2).size() + gui::rle_encode(bk2).size();
    check(d.history_bytes() == want_bytes_b,
          "history_bytes after the eviction equals B's own RLE size, not a stale total");
    check(d.history_size() == 1, "the byte cap evicted one op, well under the 96-op cap");
    check(d.can_undo(), "the newest op (B) is still undoable");

    d.undo();
    check(d.drop() == dropAfterA && d.keep() == keepAfterA,
          "undo lands on the state after A, before B -- B's entry survived the eviction");
    check(d.drop() != drop0 || d.keep() != keep0,
          "and NOT on the pristine original -- A's own undo entry is gone, not B's");
    check(!d.can_undo(), "no further undo: the evicted op cannot be recovered");
    check(exclusive(d) && composite_consistent(d), "invariant holds across the eviction boundary");
}

// ---------------------------------------------------------------------------
// Task 7: orientation mapping vs orient_pixels
// ---------------------------------------------------------------------------

void test_orientation_mapping() {
    const int W = 64, H = 48;   // stored
    for (int o = 1; o <= 8; o++) {
        const sfm::ExifTransform t = sfm::exifTransform(o);
        int dw = W, dh = H;
        spirula::oriented_size(t.turns_cw, dw, dh);
        // Three probe pixels; orient_pixels says where each lands.
        const int probes[3][2] = {{0, 0}, {W - 1, H - 1}, {10, 42}};
        for (const auto& p : probes) {
            std::vector<uint8_t> stored((size_t)W * H, 0), shown((size_t)dw * dh, 0);
            stored[(size_t)p[1] * W + p[0]] = 255;
            spirula::orient_pixels(stored.data(), W, H, 1, t.turns_cw, t.mirror, shown.data());
            int found_x = -1, found_y = -1;
            for (int y = 0; y < dh; y++)
                for (int x = 0; x < dw; x++)
                    if (shown[(size_t)y * dw + x]) { found_x = x; found_y = y; }
            int sx, sy;
            mk::to_stored(t, W, H, found_x, found_y, sx, sy);
            check(sx == p[0] && sy == p[1],
                  "to_stored inverts orient_pixels, orientation " + std::to_string(o) +
                      " probe " + std::to_string(p[0]) + "," + std::to_string(p[1]));
            int dx, dy;
            mk::to_displayed(t, W, H, sx, sy, dx, dy);
            check(dx == found_x && dy == found_y, "to_displayed agrees, orientation " + std::to_string(o));
            // The continuous form at the pixel centre lands in the same pixel.
            float fx, fy;
            mk::to_stored(t, W, H, found_x + 0.5f, found_y + 0.5f, fx, fy);
            check((int)std::floor(fx) == p[0] && (int)std::floor(fy) == p[1],
                  "continuous form at the centre, orientation " + std::to_string(o));
        }
        // A one-point brush paints that stored pixel plus its 4 edge
        // neighbours only: rasterize_shape clamps radius to 1.0 and
        // stamps `<= r*r` (SelectShape.cpp:20-33,:133-135) -- a plus disc.
        {
            const int dx = 5, dy = 10;
            gui::ShapeStroke s;
            s.kind = gui::ShapeKind::Brush;
            s.brush_radius = 0.4f;
            s.pts = {dx + 0.5f, dy + 0.5f};
            const gui::ShapeStroke st = mk::stroke_to_stored(s, t, W, H);
            gui::Stencil sten;
            gui::rasterize_shape(st, W, H, sten);
            int sx, sy;
            mk::to_stored(t, W, H, dx, dy, sx, sy);
            size_t count = 0, near = 0;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    if (sten.at(x, y)) {
                        count++;
                        near += std::abs(x - sx) + std::abs(y - sy) <= 1;
                    }
            check(count == 5 && near == 5 && sten.at(sx, sy),
                  "brush point lands on the mapped stored pixel (plus its 4 neighbours), orientation " +
                      std::to_string(o));
            const mk::Rect b = mk::stroke_bounds(st, W, H);
            check(b.x0 <= sx && sx < b.x1 && b.y0 <= sy && sy < b.y1, "bounds contain it");
            const mk::Rect back = mk::rect_to_displayed(mk::Rect{sx, sy, sx + 1, sy + 1}, t, W, H);
            check(back.x0 == dx && back.y0 == dy && back.x1 == dx + 1 && back.y1 == dy + 1,
                  "rect_to_displayed maps a one-pixel rect back, orientation " + std::to_string(o));
        }
        // A genuinely multi-pixel, non-square rect: area and aspect are
        // invariant under any 90-degree turn/mirror, so a corner-crossing
        // bug shows up here as a wrong or negative w()/h(), not just on 1x1.
        {
            const mk::Rect sr{4, 6, 13, 9};   // 9 x 3, stored
            const mk::Rect dr = mk::rect_to_displayed(sr, t, W, H);
            const bool swapped = (t.turns_cw & 1) != 0;
            check(dr.x1 > dr.x0 && dr.y1 > dr.y0,
                  "multi-pixel rect stays ordered, orientation " + std::to_string(o));
            check(dr.w() == (swapped ? sr.h() : sr.w()) && dr.h() == (swapped ? sr.w() : sr.h()),
                  "multi-pixel rect area/aspect preserved, orientation " + std::to_string(o));
        }
    }
    // Orientation 6 (one turn clockwise), the phone-portrait case, by hand:
    // displayed (5, 10) is stored (10, H-1-5) = (10, 42).
    int sx, sy;
    mk::to_stored(sfm::exifTransform(6), W, H, 5, 10, sx, sy);
    check(sx == 10 && sy == 42, "orientation 6 by hand");
    // A multi-pixel rect maps to a rect of the same area.
    const mk::Rect r = mk::rect_to_displayed(mk::Rect{2, 3, 12, 8}, sfm::exifTransform(6), W, H);
    check(r.w() * r.h() == 50 && r.w() == 5 && r.h() == 10, "rect area and turn preserved");
}

// ---------------------------------------------------------------------------
// Task 8: view math and the window's pixels
// ---------------------------------------------------------------------------

void test_view_math() {
    const int dw = 800, dh = 600;
    const float pw = 400.0f, ph = 400.0f;
    check(std::fabs(mk::fit_scale(dw, dh, pw, ph) - 0.5f) < 1e-6f, "fit_scale is the smaller ratio");
    mk::View v;
    v.zoom = 1.0f;
    v.cx = 400.0f;
    v.cy = 300.0f;
    mk::Mapping m = mk::mapping(v, dw, dh, pw, ph);
    check(std::fabs(m.scale - 0.5f) < 1e-6f, "scale at zoom 1");
    check(std::fabs(m.to_mask_x(200.0f) - 400.0f) < 1e-3f && std::fabs(m.to_mask_y(200.0f) - 300.0f) < 1e-3f,
          "pane centre is the view centre");
    check(std::fabs(m.to_screen_x(400.0f) - 200.0f) < 1e-3f, "to_screen inverts to_mask");
    // Zoom about a point keeps that point under the cursor.
    const float sx = 260.0f, sy = 150.0f;
    const float before_x = m.to_mask_x(sx), before_y = m.to_mask_y(sy);
    mk::zoom_about(v, 2.0f, sx, sy, dw, dh, pw, ph);
    m = mk::mapping(v, dw, dh, pw, ph);
    check(std::fabs(v.zoom - 2.0f) < 1e-6f, "zoom doubled");
    check(std::fabs(m.to_mask_x(sx) - before_x) < 1e-2f && std::fabs(m.to_mask_y(sy) - before_y) < 1e-2f,
          "the point under the cursor stayed put");
    mk::zoom_about(v, 0.01f, sx, sy, dw, dh, pw, ph);
    check(v.zoom == 1.0f, "zoom clamps at 1");
    for (int i = 0; i < 40; i++) mk::zoom_about(v, 2.0f, sx, sy, dw, dh, pw, ph);
    check(v.zoom == 64.0f, "zoom clamps at 64");
    // Cursor at a pane corner, zooming in from an already-cornered view: the
    // recentred point lands off-image and must come back clamped, not just
    // the zoom factor -- the final clamp_view() has to run after recentring.
    mk::View corner{1.0f, 0.0f, 0.0f};
    mk::zoom_about(corner, 8.0f, 0.0f, 0.0f, dw, dh, pw, ph);
    check(corner.zoom == 8.0f && corner.cx == 0.0f && corner.cy == 0.0f,
          "zoom_about clamps the recentred centre, not only the zoom factor");
    v.zoom = 4.0f;
    v.cx = 400.0f;
    v.cy = 300.0f;
    m = mk::mapping(v, dw, dh, pw, ph);
    mk::pan(v, 20.0f, -10.0f, m, dw, dh);
    check(std::fabs(v.cx - (400.0f - 20.0f / m.scale)) < 1e-3f, "pan moves the centre against the drag");
    mk::pan(v, -1e6f, 0.0f, m, dw, dh);
    check(v.cx == (float)dw, "centre clamps to the mask");

    // Window: at zoom 1 the whole 800x600 fits and needs no decimation.
    v = mk::View{1.0f, 400.0f, 300.0f};
    m = mk::mapping(v, dw, dh, pw, ph);
    mk::Window w = mk::window_for(m, dw, dh, pw, ph);
    check(w.r.x0 == 0 && w.r.y0 == 0 && w.r.x1 == dw && w.r.y1 == dh, "window is the whole mask");
    check(w.step == 1 && w.tw == dw && w.th == dh, "no decimation under 4096");
    // A 9000-wide mask fully visible decimates by 3.
    mk::View big{1.0f, 4500.0f, 2000.0f};
    const mk::Mapping bm = mk::mapping(big, 9000, 4000, pw, ph);
    const mk::Window bw = mk::window_for(bm, 9000, 4000, pw, ph);
    check(bw.step == 3 && bw.tw == 3000 && bw.th == 1334, "9000 wide decimates by 3 into 3000x1334");
    // Zoomed to 64 the window is a small rect.
    mk::View z{64.0f, 400.0f, 300.0f};
    const mk::Mapping zm = mk::mapping(z, dw, dh, pw, ph);
    const mk::Window zw = mk::window_for(zm, dw, dh, pw, ph);
    check(zw.step == 1 && zw.r.w() <= 16 && zw.r.h() <= 16 && zw.r.w() >= 12, "64x window is ~12.5 px wide");
    check(mk::same_window(zw, zw) && !mk::same_window(zw, w), "same_window");
    // Same rect, different step/tw/th: a hand-built Window (as derive_window's
    // own fixtures do) must not read as the same window just because the
    // visible rect agrees.
    mk::Window step_diff = zw; step_diff.step += 1;
    check(!mk::same_window(zw, step_diff), "same_window: same rect, different step");
    mk::Window tw_diff = zw; tw_diff.tw += 1;
    check(!mk::same_window(zw, tw_diff), "same_window: same rect, different tw");
    mk::Window th_diff = zw; th_diff.th += 1;
    check(!mk::same_window(zw, th_diff), "same_window: same rect, different th");
}

void test_derive_window() {
    // A 4x2 stored mask, identity turn, frame the same size.
    const int W = 4, H = 2;
    const uint8_t rgb[4 * 2 * 3] = {
        90, 60, 30,  100, 100, 100,  200, 40, 80,  10, 20, 30,
        90, 60, 30,  100, 100, 100,  200, 40, 80,  10, 20, 30};
    const uint8_t comp[8] = {0, 255, 255, 255, 0, 255, 255, 255};
    const uint8_t drop[8] = {0, 255, 0, 0, 0, 255, 0, 0};
    const uint8_t keep[8] = {0, 0, 255, 0, 0, 0, 255, 0};
    mk::WindowSource src;
    src.rgb = rgb; src.fw = W; src.fh = H;
    src.composite = comp; src.drop = drop; src.keep = keep;
    src.W = W; src.H = H;
    mk::Window win;
    win.r = {0, 0, W, H};
    win.step = 1;
    win.tw = W;
    win.th = H;
    std::vector<uint8_t> rgba;
    const mk::Rect t = mk::derive_window(win, win.r, src, rgba);
    check(t.x0 == 0 && t.y0 == 0 && t.x1 == W && t.y1 == H, "texel rect is the whole window");
    check(rgba.size() == (size_t)W * H * 4, "rgba sized");
    // Texel 0: dropped, no layer -> Picture.cpp tint (r/3+150, g/3, b/3).
    check(rgba[0] == 90 / 3 + 150 && rgba[1] == 60 / 3 && rgba[2] == 30 / 3 && rgba[3] == 255,
          "dropped pixel tinted like Picture.cpp");
    // Texel 1: drop layer over a kept composite -> 25% toward (235,45,45).
    check(rgba[4] == (100 * 3 + 235) / 4 && rgba[5] == (100 * 3 + 45) / 4 && rgba[6] == (100 * 3 + 45) / 4,
          "drop layer tint");
    // Texel 2: keep layer -> 25% toward (60,220,90).
    check(rgba[8] == (200 * 3 + 60) / 4 && rgba[9] == (40 * 3 + 220) / 4 && rgba[10] == (80 * 3 + 90) / 4,
          "keep layer tint");
    // Texel 3: kept, no layer -> the photo.
    check(rgba[12] == 10 && rgba[13] == 20 && rgba[14] == 30, "kept pixel is the photo");
    // A sub-rectangle writes only its texels.
    std::fill(rgba.begin(), rgba.end(), 7);
    const mk::Rect part = mk::derive_window(win, mk::Rect{2, 0, 3, 2}, src, rgba);
    check(part.x0 == 2 && part.x1 == 3 && part.y0 == 0 && part.y1 == 2, "part texel rect");
    check(rgba[0] == 7 && rgba[8] == (200 * 3 + 60) / 4 && rgba[12] == 7, "only the part was rewritten");
    // Step 2 over uniform 2x2 blocks averages exactly; the mask decides by majority.
    const uint8_t rgb2[4 * 2 * 3] = {
        8, 8, 8,  8, 8, 8,  40, 40, 40,  40, 40, 40,
        8, 8, 8,  8, 8, 8,  40, 40, 40,  40, 40, 40};
    const uint8_t comp2[8] = {0, 0, 0, 255, 0, 0, 255, 255};
    const uint8_t zero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    src.rgb = rgb2; src.composite = comp2; src.drop = zero; src.keep = zero;
    mk::Window w2;
    w2.r = {0, 0, W, H};
    w2.step = 2;
    w2.tw = 2;
    w2.th = 1;
    mk::derive_window(w2, w2.r, src, rgba);
    check(rgba[0] == 8 / 3 + 150 && rgba[1] == 8 / 3, "decimated block, all dropped, tinted");
    check(rgba[4] == 40 && rgba[5] == 40 && rgba[6] == 40, "decimated block, 3 of 4 kept, photo");
    // An exact 2-of-4 tie in a step-2 block counts as dropped: hiding a
    // correction is worse than over-showing one. This is reachable on real
    // 8K captures, whose SS_MASK_BENCH size decimates to exactly step 2.
    const uint8_t rgb3[4 * 2 * 3] = {
        100, 100, 100,  100, 100, 100,  40, 40, 40,  40, 40, 40,
        100, 100, 100,  100, 100, 100,  40, 40, 40,  40, 40, 40};
    const uint8_t comp3[8] = {0, 255, 255, 255, 0, 255, 255, 255};
    src.rgb = rgb3; src.composite = comp3; src.drop = zero; src.keep = zero;
    mk::derive_window(w2, w2.r, src, rgba);
    check(rgba[0] == 100 / 3 + 150 && rgba[1] == 100 / 3, "2-of-4 tie counts as dropped");
    check(rgba[4] == 40 && rgba[5] == 40 && rgba[6] == 40, "non-tied block stays kept");
    // A turned source: orientation 6, stored 4x2 shows as 2x4; displayed
    // (1,3) is stored (3,0), whose photo is 40 and comp 255.
    src.turn = sfm::exifTransform(6);
    mk::Window w3;
    w3.r = {0, 0, 2, 4};
    w3.step = 1;
    w3.tw = 2;
    w3.th = 4;
    mk::derive_window(w3, w3.r, src, rgba);
    check(rgba[((size_t)3 * 2 + 1) * 4] == 40, "turned: displayed (1,3) reads stored (3,0)");
    // A frame at another size than the mask is sampled to the mask grid.
    const uint8_t rgb8[8 * 4 * 3] = {0};
    src.turn = sfm::ExifTransform{};
    src.rgb = rgb8; src.fw = 8; src.fh = 4;
    src.composite = comp; src.drop = zero; src.keep = zero;
    mk::derive_window(win, win.r, src, rgba);
    check(rgba[12] == 0 && rgba[15] == 255, "frame sampled to mask grid, no crash");
}

// ---------------------------------------------------------------------------
// Task 11: the session end to end, without drawing
// ---------------------------------------------------------------------------

void settle(mk::MaskSession& s) {
    for (int i = 0; i < 2000 && !s.idle(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    s.pump();
}

void test_session() {
    Fixture f = make_dataset("session", 64, 48, {"cam0/a", "cam0/b", "cam1/c"});
    const std::vector<uint8_t> original_a = file_bytes(f.masks / "cam0" / "a.png");
    mk::MaskSession s;
    std::string err;
    // Refusals.
    check(!s.open(f.images.string(), f.images.string(), f.masks.string(), err) &&
              err == spirula::i18n::msg::maskedit::err_workspace_inside_images.get(),
          "workspace inside images is refused with the message");
    check(!s.open((f.images / "sub").string(), f.images.string(), f.masks.string(), err) &&
              err == spirula::i18n::msg::maskedit::err_workspace_inside_images.get(),
          "a workspace under the image root is refused");
    const fs::path empty = scratch("session_empty");
    check(!s.open(f.root.string(), empty.string(), f.masks.string(), err) &&
              err.find(mk::normalize_dir(empty.string())) != std::string::npos,
          "no frames is refused naming the folder");
    check(!s.is_open(), "not open after refusals");

    check(s.open(f.root.string(), f.images.string(), f.masks.string(), err), "open: " + err);
    check(s.is_open() && s.frame_count() == 3, "three frames");
    check(s.frames()[0].key == "cam0/a" && s.frames()[0].camera == "cam0" &&
              s.frames()[2].key == "cam1/c" && s.frames()[2].camera == "cam1",
          "frames keyed and grouped as the run keys them");
    check(s.layer_root() == (f.root / mk::kLayerDirName).string(), "layer root under the workspace");
    check(mk::MaskSession::paint_for(false, false) == mk::Paint::ForceDrop &&
              mk::MaskSession::paint_for(true, false) == mk::Paint::ForceDrop &&
              mk::MaskSession::paint_for(false, true) == mk::Paint::ForceKeep &&
              mk::MaskSession::paint_for(true, true) == mk::Paint::Clear,
          "paint_for is combine_now's table: plain/Shift drop, Ctrl keep, both clear");
    check(mk::MaskSession::step_brush(100.0f, true) == 100.0f * 1.18f &&
              mk::MaskSession::step_brush(100.0f, false) == 100.0f * 0.85f,
          "step_brush: grow x1.18, shrink x0.85");
    check(mk::MaskSession::step_brush(4096.0f, true) == 4096.0f &&
              mk::MaskSession::step_brush(4000.0f, true) == 4096.0f,
          "step_brush clamps growth at 4096, exactly, not past it");
    check(mk::MaskSession::step_brush(1.0f, false) == 1.0f &&
              mk::MaskSession::step_brush(1.1f, false) == 1.0f,
          "step_brush clamps shrink at 1, exactly, not below it");
    {
        // Monotonic in both directions, and the clamp is REACHED, not
        // approached asymptotically: 4096/0.85^n < 1 well inside 200 steps.
        float r = 24.0f;
        bool grew = true, shrank = true;
        for (int i = 0; i < 200; i++) {
            const float next = mk::MaskSession::step_brush(r, true);
            grew = grew && next >= r;
            r = next;
        }
        check(grew && r == 4096.0f, "repeated growth is monotonic and lands exactly on 4096");
        for (int i = 0; i < 200; i++) {
            const float next = mk::MaskSession::step_brush(r, false);
            shrank = shrank && next <= r;
            r = next;
        }
        check(shrank && r == 1.0f, "repeated shrink is monotonic and lands exactly on 1");
    }
    settle(s);
    check(s.doc() != nullptr && s.frame_index() == 0, "frame 0 loaded");
    check(s.shown_width() == 64 && s.shown_height() == 48, "shown size (no EXIF turn)");
    check(s.corrected_count() == 0 && s.error().empty(), "nothing corrected yet");

    // A stroke in pane pixels under an identity mapping.
    mk::Mapping m;
    m.scale = 1.0f;
    gui::ShapeStroke box;
    box.kind = gui::ShapeKind::Box;
    box.pts = {4.0f, 4.0f, 14.0f, 14.0f};
    const mk::Rect changed = s.commit_stroke(box, mk::Paint::ForceDrop, m);
    check(!changed.empty() && changed.x0 <= 4 && changed.x1 >= 14, "commit returns the changed rect");
    check(s.doc()->dirty() && s.doc()->drop()[(size_t)8 * 64 + 8] == 255, "painted and dirty");
    // A 2x mapping: pane (80,8)-(100,28) is mask (40,4)-(50,14).
    mk::Mapping m2;
    m2.scale = 2.0f;
    gui::ShapeStroke box2;
    box2.kind = gui::ShapeKind::Box;
    box2.pts = {80.0f, 8.0f, 100.0f, 28.0f};
    s.commit_stroke(box2, mk::Paint::ForceKeep, m2);
    check(s.doc()->keep()[(size_t)8 * 64 + 45] == 255 && s.doc()->keep()[(size_t)8 * 64 + 39] == 0,
          "pane pixels mapped through the scale");
    const mk::Rect un = s.undo();
    check(!un.empty() && s.doc()->keep()[(size_t)8 * 64 + 45] == 0, "undo through the session");
    check(!s.redo().empty() && s.doc()->keep()[(size_t)8 * 64 + 45] == 255, "redo through the session");

    // Explicit save: the composite lands, the doc stays open and clean.
    s.save();
    settle(s);
    check(!s.doc()->dirty() && s.error().empty(), "clean after save: " + s.error());
    check(s.corrected_count() == 1, "one corrected frame");
    check(file_bytes(f.masks / "cam0" / "a.png") != original_a, "masks/cam0/a.png rewritten");
    check(file_bytes(f.layer / "cam0" / "a.base.png") == original_a, "base is the original");

    // Paint, then switch frames: autosave, then the next frame loads.
    s.commit_stroke(box, mk::Paint::Clear, m);
    check(s.doc()->dirty(), "dirty again");
    s.go_to(2);
    settle(s);
    check(s.frame_index() == 2 && s.doc() && s.doc()->key() == "cam1/c", "frame 2 open");
    check(s.doc()->base_state() == mk::BaseState::Unedited, "frame 2 untouched");
    mk::LayerIndex disk;
    disk.load(s.layer_root(), err);
    check(disk.frames.count("cam0/a") == 1, "frame 0 was saved on the way out");
    // Back to 0: our own composite is Unchanged and the layers are there.
    s.go_to(0);
    settle(s);
    check(s.doc()->base_state() == mk::BaseState::Unchanged, "reopened frame is Unchanged");
    check(s.doc()->drop()[(size_t)8 * 64 + 8] == 0 && s.doc()->keep()[(size_t)8 * 64 + 45] == 255,
          "layers as saved (clear applied, keep kept)");

    // Someone regenerated the mask meanwhile: reopening re-bases it.
    s.go_to(1);
    settle(s);
    write_png_gray(f.masks / "cam0" / "a.png", 64, 48, synth_mask(64, 48, 77));
    const std::vector<uint8_t> regenerated = file_bytes(f.masks / "cam0" / "a.png");
    s.go_to(0);
    settle(s);
    check(s.doc()->base_state() == mk::BaseState::Regenerated, "regenerated mask detected on open");
    check(file_bytes(f.layer / "cam0" / "a.base.png") == regenerated, "re-based to the regenerated file");
    check(s.doc()->base() == synth_mask(64, 48, 77), "doc base is the new mask");

    // Revert the open frame: the mask is the base again, entry gone.
    s.revert_open_frame();
    settle(s);
    check(file_bytes(f.masks / "cam0" / "a.png") == regenerated, "revert copies the base back");
    check(s.corrected_count() == 0 && s.doc() && s.doc()->base_state() == mk::BaseState::Unedited,
          "reverted and reloaded");

    // Close with a dirty frame saves it.
    s.commit_stroke(box, mk::Paint::ForceDrop, m);
    s.close();
    check(!s.is_open(), "closed");
    disk.load(s.layer_root().empty() ? (f.root / mk::kLayerDirName).string() : s.layer_root(), err);
    check(disk.frames.count("cam0/a") == 1, "close saved the dirty frame");
    check(fs::exists(f.layer / "cam0" / "a.drop.png"), "layer file written by close");

    // revert_every_frame.
    check(s.open(f.root.string(), f.images.string(), f.masks.string(), err), "reopen");
    settle(s);
    s.revert_every_frame();
    settle(s);
    check(s.corrected_count() == 0 && !fs::exists(f.layer / "cam0" / "a.drop.png"), "everything reverted");
    s.close();
}

// Two mismatched layer files, each its own size: the joined warning from
// read_layers must not collapse into one file's dimensions for both names.
void test_session_size_mismatch() {
    Fixture f = make_dataset("session_mismatch", 64, 48, {"a"});
    write_png_gray(f.layer / "a.drop.png", 32, 24, std::vector<uint8_t>(32 * 24, 255));
    write_png_gray(f.layer / "a.keep.png", 16, 12, std::vector<uint8_t>(16 * 12, 255));
    mk::MaskSession s;
    std::string err;
    check(s.open(f.root.string(), f.images.string(), f.masks.string(), err), "open: " + err);
    settle(s);
    const std::string e = s.error();
    check(e.find((f.layer / "a.drop.png").string()) != std::string::npos &&
              e.find((f.layer / "a.keep.png").string()) != std::string::npos,
          "both mismatched files are named");
    check(e.find("32x24") != std::string::npos && e.find("16x12") != std::string::npos,
          "each file reports its own size, not the first file's for both");
    s.close();
}

void test_session_close_resets_paths() {
    Fixture f = make_dataset("session_close_reset", 64, 48, {"a"});
    mk::MaskSession s;
    std::string err;
    check(s.open(f.root.string(), f.images.string(), f.masks.string(), err), "open: " + err);
    settle(s);
    s.close();
    check(s.layer_root().empty() && s.mask_root().empty(), "closed session reports no paths");
    check(s.frame_count() == 0 && s.frame_index() == -1, "closed session reports no frames");
    check(!s.is_open(), "closed session reports not open");
}

// ---------------------------------------------------------------------------
// Plan 2, Task 4: the GUI lasso and the CLI path are one fill
// ---------------------------------------------------------------------------

void test_path_fill_parity() {
    // Non-square, and a polygon that is not symmetric under a transpose, so a
    // fill that swaps its axes cannot pass.
    const int W = 96, H = 40;
    const std::vector<float> norm = {0.08f, 0.12f, 0.91f, 0.07f, 0.66f, 0.55f,
                                     0.97f, 0.93f, 0.21f, 0.88f, 0.44f, 0.41f};
    gui::ShapeStroke lasso;
    lasso.kind = gui::ShapeKind::Lasso;
    for (size_t i = 0; i + 1 < norm.size(); i += 2) {
        lasso.pts.push_back(norm[i] * (float)W);
        lasso.pts.push_back(norm[i + 1] * (float)H);
    }
    gui::Stencil st;
    gui::rasterize_shape(lasso, W, H, st);

    app::FrameMask m;
    app::MaskShape p;
    p.kind = app::MaskShape::Kind::Path;
    p.remove = true;
    p.pts = norm;
    m.shapes.push_back(p);
    std::vector<uint8_t> out;
    std::string err;
    check(app::rasterize_frame_mask(m, W, H, out, err), "parity: rasterizes");
    size_t differ = 0, inside = 0;
    for (size_t i = 0; i < (size_t)W * H; i++) {
        inside += st.in[i] != 0;
        differ += (st.in[i] != 0) != (out[i] == 0);
    }
    check(inside > 300, "parity: the lasso covers something: " + std::to_string(inside));
    check(differ == 0, "parity: every pixel agrees between lasso and -path: " +
                           std::to_string(differ) + " differ");
}

// ---------------------------------------------------------------------------
// Plan 2, Task 6: the livewire's cost image
// ---------------------------------------------------------------------------

// Grey RGB frames whose every gradient is known by hand.
std::vector<uint8_t> gray_rgb(int w, int h, const std::function<uint8_t(int, int)>& f) {
    std::vector<uint8_t> px((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t v = f(x, y);
            uint8_t* p = &px[((size_t)y * w + x) * 3];
            p[0] = p[1] = p[2] = v;
        }
    return px;
}

// 40 left of column 100, 200 from it on: a vertical step edge between
// columns 99 and 100. Sobel puts Gx = 640 on both, 0 elsewhere.
std::vector<uint8_t> step_edge_rgb(int w, int h) {
    return gray_rgb(w, h, [](int x, int) { return (uint8_t)(x < 100 ? 40 : 200); });
}

// 40 up to column 98, 120 at 99, 200 from 100: a two-step ramp whose
// Laplacian is +80, 0, -80 on columns 98, 99, 100.
std::vector<uint8_t> ramp_rgb(int w, int h) {
    return gray_rgb(w, h, [](int x, int) {
        return (uint8_t)(x <= 98 ? 40 : x == 99 ? 120 : 200);
    });
}

std::vector<uint8_t> flat_rgb(int w, int h, uint8_t v) {
    return gray_rgb(w, h, [v](int, int) { return v; });
}

// Dark for x < 100, or x >= 100 and y < 60; light otherwise: a right-angle
// edge (vertical at 99/100 for y >= 60, horizontal at 59/60 for x >= 100).
// The straight line between a point on each arm misses the edge.
std::vector<uint8_t> l_corner_rgb(int w, int h) {
    return gray_rgb(w, h, [](int x, int y) {
        return (uint8_t)((x < 100 || y < 60) ? 40 : 200);
    });
}

// Vertical edge flipping polarity at row 60: (100,59)/(100,60) are then
// 8-adjacent with differing codes and opposite sign vs the vertical link
// -- a corner's diagonal transition always bisects, so hand-built here.
std::vector<uint8_t> bowtie_rgb(int w, int h) {
    return gray_rgb(w, h, [](int x, int y) {
        const bool right = x >= 100;
        return (uint8_t)((y < 60) == right ? 40 : 200);
    });
}

size_t count_zero_crossings(const mk::Livewire& lw) {
    size_t n = 0;
    for (int y = 0; y < lw.height(); y++)
        for (int x = 0; x < lw.width(); x++) n += lw.zero_crossing(x, y);
    return n;
}

void test_livewire_features() {
    const int W = 200, H = 120;
    mk::Livewire lw;
    lw.build(step_edge_rgb(W, H).data(), W, H);
    check(lw.ready() && lw.step() == 1 && lw.width() == W && lw.height() == H,
          "step edge: grid is the frame at step 1");
    check(lw.builds() == 1, "one build counted");
    // Exact lower bound: three per-pixel planes plus the three 256x8 tables
    // -- capacity() >= size() always, so this can't pass on an under-count.
    const size_t min_bytes = (size_t)W * H * 3 + sizeof(float) * 256 * 8 * 2 + 256 * 8;
    check(lw.bytes() >= min_bytes,
          "bytes accounts for the feature planes and tables: " + std::to_string(lw.bytes()));

    // fG: 0 on the two ridge columns (G = Gmax), 255 off them.
    check(lw.magnitude_cost(99, 60) == 0 && lw.magnitude_cost(100, 60) == 0,
          "ridge columns have zero magnitude cost");
    check(lw.magnitude_cost(50, 60) == 255 && lw.magnitude_cost(101, 60) == 255 &&
              lw.magnitude_cost(98, 60) == 255,
          "flat columns have full magnitude cost");

    // fZ: exactly the two ridge columns over the interior rows.
    check(count_zero_crossings(lw) == 2 * (size_t)(H - 2),
          "zero crossings are the two ridge columns, interior rows: " +
              std::to_string(count_zero_crossings(lw)));
    check(lw.zero_crossing(99, 60) && lw.zero_crossing(100, 60) &&
              !lw.zero_crossing(98, 60) && !lw.zero_crossing(101, 60),
          "zero crossing on 99 and 100 only");

    // D' is perpendicular to the gradient: vertical on a vertical edge.
    float dx, dy;
    lw.direction(99, 60, dx, dy);
    check(std::fabs(dx) < 0.02f && std::fabs(dy) > 0.99f, "D' on the ridge is vertical");
    lw.direction(50, 60, dx, dy);
    check(dx == 0.0f && dy == 0.0f, "D' is zero where there is no gradient");

    // fD: along the edge ~0, across it ~2/3 (0.0026 and 0.6641 after the
    // 255-code angle quantisation; hand-checked). The parallel-direction
    // mutant swaps the two.
    const float along = lw.direction_cost(99, 60, 99, 61);
    const float across = lw.direction_cost(99, 60, 100, 60);
    check(along >= 0.0f && along < 0.02f, "direction cost along the edge ~0: " + std::to_string(along));
    check(std::fabs(across - 2.0f / 3.0f) < 0.02f,
          "direction cost across the edge ~2/3: " + std::to_string(across));
    check(along < across, "along < across");
    check(lw.direction_cost(99, 60, 99, 62) < 0.0f, "non-adjacent pixels have no direction cost");
    check(lw.direction_cost(-1, 60, 0, 60) < 0.0f, "outside the grid has no direction cost");

    // The full link: ~0 along the ridge; 0.43 + 0.43*2/3 + 0.14 = 0.85667 on
    // a flat pixel, times sqrt(2) on a diagonal.
    check(lw.link_cost(99, 60, 99, 61) < 0.02f, "link along the ridge is almost free");
    check(std::fabs(lw.link_cost(50, 60, 50, 61) - 0.85667f) < 0.01f,
          "flat axial link costs 0.85667: " + std::to_string(lw.link_cost(50, 60, 50, 61)));
    check(std::fabs(lw.link_cost(50, 60, 51, 61) - 0.85667f * 1.41421356f) < 0.02f,
          "flat diagonal link is sqrt(2) times that");
    check(lw.link_cost(50, 60, 52, 60) < 0.0f, "non-adjacent link cost is -1");

    // The ramp: its centre column has Laplacian exactly 0 between +80 and
    // -80 and must be the one and only zero crossing.
    mk::Livewire ramp;
    ramp.build(ramp_rgb(W, H).data(), W, H);
    check(count_zero_crossings(ramp) == (size_t)(H - 2),
          "ramp: one zero-crossing column: " + std::to_string(count_zero_crossings(ramp)));
    check(ramp.zero_crossing(99, 60) && !ramp.zero_crossing(98, 60) && !ramp.zero_crossing(100, 60),
          "ramp: the centre column is the crossing");

    // Flat: no gradient anywhere, full magnitude cost, no crossings.
    mk::Livewire flat;
    flat.build(flat_rgb(64, 32, 128).data(), 64, 32);
    check(flat.magnitude_cost(10, 10) == 255 && count_zero_crossings(flat) == 0,
          "flat image: fG = 1 everywhere, no zero crossings");

    // clear() empties the grid but keeps the build count.
    flat.clear();
    check(!flat.ready() && flat.builds() == 1, "clear keeps the build counter");

    // A cancel flag already set: the build stops between passes, leaves the
    // grid empty and does not count. Catches a checkpoint that is ignored.
    std::atomic<bool> cancel{true};
    mk::Livewire stopped;
    stopped.build(flat_rgb(64, 32, 128).data(), 64, 32, mk::kLivewireMaxEdge,
                  mk::LivewireWeights{}, &cancel);
    check(!stopped.ready() && stopped.builds() == 0, "a set cancel flag stops the build before it counts");
    cancel = false;
    stopped.build(flat_rgb(64, 32, 128).data(), 64, 32, mk::kLivewireMaxEdge,
                  mk::LivewireWeights{}, &cancel);
    check(stopped.ready() && stopped.builds() == 1, "a clear cancel flag builds as before");
}

void test_livewire_mapping() {
    // 4097 x 5 at the default cap: step 2, 2049 x 3. The fifth row is a
    // partial block; the y factor must still be the step, never fh / gh.
    mk::Livewire lw;
    lw.build(flat_rgb(4097, 5, 90).data(), 4097, 5);
    check(lw.step() == 2 && lw.width() == 2049 && lw.height() == 3,
          "4097x5 decimates to 2049x3 at step 2");
    float fx, fy;
    lw.to_frame(2048, 0, fx, fy);
    check((int)std::floor(fx) == 4096, "last grid column lands on the last frame column");
    lw.to_frame(0, 1, fx, fy);
    check((int)std::floor(fy) == 3, "grid row 1 is frame row 3 (step), not row 2 (fh/gh)");
    lw.to_frame(0, 2, fx, fy);
    check((int)std::floor(fy) == 4, "last grid row clamps to the last frame row");
    lw.to_frame(0, 0, fx, fy);
    check((int)std::floor(fx) == 1 && (int)std::floor(fy) == 1, "grid (0,0) is the block centre (1,1)");
    int gx, gy;
    lw.to_grid(4096.9f, 4.9f, gx, gy);
    check(gx == 2048 && gy == 2, "frame corner maps to the last grid cell");
    lw.to_grid(0.0f, 0.0f, gx, gy);
    check(gx == 0 && gy == 0, "frame origin maps to grid origin");
    lw.to_grid(-3.0f, -3.0f, gx, gy);
    check(gx == 0 && gy == 0, "outside clamps");
    lw.to_grid(3.0f, 2.0f, gx, gy);
    check(gx == 1 && gy == 1, "frame (3,2) is grid (1,1)");

    // An explicit cap.
    mk::Livewire small;
    small.build(step_edge_rgb(200, 120).data(), 200, 120, 50);
    check(small.step() == 4 && small.width() == 50 && small.height() == 30,
          "cap 50 on 200x120 gives step 4, 50x30");
    small.to_frame(25, 15, fx, fy);
    check(std::fabs(fx - 102.0f) < 1e-4f && std::fabs(fy - 62.0f) < 1e-4f,
          "block centre at step 4: (25.5*4, 15.5*4)");
}

// ---------------------------------------------------------------------------
// Plan 2, Task 7: the search
// ---------------------------------------------------------------------------

void test_livewire_edge_path() {
    const int W = 200, H = 120;
    mk::Livewire lw;
    lw.build(step_edge_rgb(W, H).data(), W, H);
    lw.set_anchor(100, 10);
    check(lw.has_anchor(), "anchor set");
    std::vector<int> p;
    check(lw.path_to(100, 110, p), "path found along the edge");
    check(p.size() >= 2 * 101, "path has at least 101 points: " + std::to_string(p.size() / 2));
    check(p.size() >= 4 && p[0] == 100 && p[1] == 10, "path starts at the anchor");
    check(p.size() >= 4 && p[p.size() - 2] == 100 && p[p.size() - 1] == 110,
          "path ends at the target");
    // Criterion #7: every point within one working pixel of the edge at
    // x = 99.5, and consecutive points 8-adjacent.
    float worst = 0.0f;
    bool adjacent = true;
    for (size_t i = 0; i + 1 < p.size(); i += 2) {
        worst = std::max(worst, std::fabs((float)p[i] - 99.5f));
        if (i >= 2)
            adjacent &= std::abs(p[i] - p[i - 2]) <= 1 && std::abs(p[i + 1] - p[i - 1]) <= 1 &&
                        (p[i] != p[i - 2] || p[i + 1] != p[i - 1]);
    }
    check(worst <= 1.0f, "every path pixel within 1 px of the edge (worst " +
                             std::to_string(worst) + ")");
    check(adjacent, "consecutive path points are 8-adjacent and distinct");
    const double cost = lw.path_cost(100, 110);
    check(cost >= 0.0 && cost < 1.0, "the edge path is nearly free: " + std::to_string(cost));
    check(lw.pops() < 2000, "lazy: settled far fewer nodes than the grid holds: " +
                                std::to_string(lw.pops()));
    // A target off the edge is reachable too, and costlier per pixel.
    check(lw.path_to(150, 60, p) && p[p.size() - 2] == 150 && p[p.size() - 1] == 60,
          "off-edge target reached");
    check(lw.path_cost(150, 60) > 20.0, "leaving the edge costs about 0.86 per pixel");
    check(!lw.path_to(-1, 5, p), "outside the grid: no path");
    mk::Livewire empty;
    check(!empty.path_to(0, 0, p) && empty.path_cost(0, 0) < 0.0, "no anchor: no path");
}

void test_livewire_reanchor() {
    mk::Livewire lw;
    lw.build(step_edge_rgb(200, 120).data(), 200, 120);
    std::vector<int> p1, p2;
    lw.set_anchor(20, 20);
    check(lw.path_to(150, 60, p1) && p1[0] == 20 && p1[1] == 20, "first anchor's path");
    lw.set_anchor(180, 100);
    check(lw.pops() == 0, "set_anchor resets the pop count");
    check(lw.path_to(150, 60, p2) && p2[0] == 180 && p2[1] == 100,
          "second anchor's path starts at the second anchor");
    check(lw.path_cost(180, 100) == 0.0, "the anchor itself costs nothing");
}

void test_livewire_diagonal() {
    // Uniform image: every link costs 0.85667 per unit length, so the cost
    // to (20, 10) is (10 sqrt2 + 10) * 0.85667 = 20.6818 (hand-checked), and
    // to (20, 0) is 17.1333. A Chebyshev metric gives 17.13 for both.
    mk::Livewire lw;
    lw.build(flat_rgb(64, 32, 128).data(), 64, 32);
    lw.set_anchor(0, 0);
    const double d = lw.path_cost(20, 10);
    check(std::fabs(d - 20.6818) < 0.02, "diagonal links weighted by sqrt2: " + std::to_string(d));
    const double a = lw.path_cost(20, 0);
    check(std::fabs(a - 17.1333) < 0.02, "axial run: " + std::to_string(a));
}

void test_livewire_once() {
    mk::Livewire lw;
    lw.build(step_edge_rgb(200, 120).data(), 200, 120);
    lw.set_anchor(100, 10);
    std::vector<int> p;
    for (int i = 0; i < 100; i++) lw.path_to(100 + (i % 7) - 3, 20 + i, p);
    check(lw.builds() == 1, "100 cursor moves, one build");
}

// ---------------------------------------------------------------------------
// Plan 2, Task 8: the pen tool
// ---------------------------------------------------------------------------

gui::ViewportInput at(float x, float y) {
    gui::ViewportInput in;
    in.hovered = true;
    in.x = x;
    in.y = y;
    in.W = 200;
    in.H = 120;
    return in;
}

gui::ViewportInput click_at(float x, float y) {
    gui::ViewportInput in = at(x, y);
    in.clicked = in.down = true;
    return in;
}

gui::ViewportInput right_click_at(float x, float y) {
    gui::ViewportInput in = at(x, y);
    in.right_clicked = true;
    return in;
}

bool same_points(const std::vector<float>& a, const std::vector<float>& b, float tol) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::fabs(a[i] - b[i]) > tol) return false;
    return true;
}

void test_path_tool_basic() {
    mk::PathTool t;
    std::vector<float> out;
    bool consumed = false;
    check(!t.in_progress() && !t.snapping(), "idle, no livewire");
    check(!t.update(at(10, 10), out, consumed) && !consumed, "a move does nothing while idle");
    gui::ViewportInput off = click_at(10, 10);
    off.hovered = false;
    check(!t.update(off, out, consumed) && !consumed && !t.in_progress(),
          "a click off the canvas does nothing");

    check(!t.update(click_at(10, 10), out, consumed) && consumed && t.in_progress(),
          "first click drops an anchor and is consumed");
    check(t.anchor_count() == 1, "one anchor");
    std::vector<float> an, co, li;
    t.update(at(30, 30), out, consumed);
    t.overlay(an, co, li);
    check(same_points(an, {10, 10}, 1e-6f) && same_points(co, {10, 10}, 1e-6f) &&
              same_points(li, {10, 10, 30, 30}, 1e-6f),
          "live segment is a straight line to the cursor without a livewire");
    check(!consumed, "a plain move is not consumed");
    gui::ViewportInput drag = at(30, 30);
    drag.down = true;
    t.update(drag, out, consumed);
    check(consumed, "the held button stays the tool's");

    t.update(click_at(50, 10), out, consumed);
    check(t.anchor_count() == 2 && !t.commit_pending(out), "two anchors cannot close");
    check(t.in_progress(), "a refused commit keeps the path");
    t.update(click_at(50, 40), out, consumed);
    t.overlay(an, co, li);
    check(same_points(co, {10, 10, 50, 10, 50, 40}, 1e-6f), "committed polyline is the anchors");

    // Close on the first anchor: within 10 px of it, with 3 anchors.
    t.update(at(40, 27), out, consumed);
    check(!t.near_first(), "34.5 px away is not near the first anchor");
    t.update(at(12, 11), out, consumed);
    check(t.near_first(), "2.2 px away is near");
    // Straddle kPathCloseRadius = 10.0f exactly, so the constant is pinned.
    t.update(at(19.9f, 10.0f), out, consumed);
    check(t.near_first(), "9.9 px away is inside the close radius");
    t.update(at(20.1f, 10.0f), out, consumed);
    check(!t.near_first(), "10.1 px away is outside the close radius");
    check(t.update(click_at(12, 11), out, consumed) && consumed, "clicking the first anchor closes");
    check(same_points(out, {10, 10, 50, 10, 50, 40}, 1e-6f), "closed polygon is the three anchors");
    check(!t.in_progress() && t.anchor_count() == 0, "closing resets the tool");

    // Enter closes.
    t.update(click_at(10, 10), out, consumed);
    t.update(click_at(50, 10), out, consumed);
    t.update(click_at(50, 40), out, consumed);
    check(t.commit_pending(out) && out.size() == 6, "Enter closes three anchors");

    // A right click closes; below 3 anchors it is refused and keeps the path.
    t.update(click_at(10, 10), out, consumed);
    t.update(click_at(50, 10), out, consumed);
    check(!t.update(right_click_at(50, 40), out, consumed) && consumed && t.in_progress(),
          "right click with two anchors: refused, consumed, path kept");
    t.update(click_at(50, 40), out, consumed);
    check(t.update(right_click_at(70, 70), out, consumed) && out.size() == 6,
          "right click with three anchors closes");

    // Pop.
    t.update(click_at(10, 10), out, consumed);
    t.update(click_at(50, 10), out, consumed);
    t.update(click_at(50, 40), out, consumed);
    check(t.pop_anchor() && t.anchor_count() == 2, "pop takes one anchor");
    t.overlay(an, co, li);
    check(same_points(co, {10, 10, 50, 10}, 1e-6f), "pop trims the committed polyline");
    check(t.pop_anchor() && t.pop_anchor() && !t.in_progress(), "pop to empty");
    check(!t.pop_anchor(), "pop on empty is false");

    // Cancel.
    t.update(click_at(10, 10), out, consumed);
    t.cancel();
    check(!t.in_progress(), "cancel empties the path");
}

void test_path_tool_livewire() {
    const int W = 200, H = 120;
    mk::Livewire lw;
    lw.build(step_edge_rgb(W, H).data(), W, H);
    mk::PathTool t;
    t.set_livewire(&lw);
    check(t.snapping(), "snapping with a ready livewire");
    std::vector<float> out, an, co, li;
    bool consumed;
    t.update(click_at(100, 10), out, consumed);
    check(lw.has_anchor(), "the first anchor seeds the search");
    // A near hop and a far hop must both grow, so no constant satisfies both.
    t.update(at(100, 11), out, consumed);
    const double ms_tiny = t.last_segment_ms();
    const size_t pops_tiny = lw.pops();
    t.update(at(100, 60), out, consumed);
    t.overlay(an, co, li);
    check(li.size() >= 2 * 51, "live segment follows the edge: " + std::to_string(li.size() / 2));
    bool on_edge = true;
    for (size_t i = 2; i + 3 < li.size(); i += 2) on_edge &= std::floor(li[i]) == 99.0f || std::floor(li[i]) == 100.0f;
    check(on_edge, "live segment's interior points sit on the ridge");
    check(li[0] == 100.0f && li[1] == 10.0f && li[li.size() - 2] == 100.0f && li[li.size() - 1] == 60.0f,
          "live segment's ends are the exact anchor and cursor");
    const double ms = t.last_segment_ms();
    check(ms >= 0.0 && ms < 1000.0, "segment time stays in a sane range: " + std::to_string(ms) + " ms");
    check(lw.pops() > pops_tiny, "the far hover visits more nodes: " + std::to_string(lw.pops()) +
                                      " vs " + std::to_string(pops_tiny));
    check(ms > ms_tiny, "segment time grows with the search, not a constant: " + std::to_string(ms) +
                             " vs " + std::to_string(ms_tiny) + " ms");

    t.update(click_at(100, 110), out, consumed);
    t.overlay(an, co, li);
    const size_t after_two = co.size();
    check(after_two >= 2 * 101, "committed polyline runs down the edge");
    check(co[0] == 100.0f && co[1] == 10.0f && co[after_two - 2] == 100.0f && co[after_two - 1] == 110.0f,
          "committed ends are exact anchors");
    check(li.empty() || li.size() == 4, "live segment cleared after a click");

    t.update(click_at(150, 60), out, consumed);
    t.overlay(an, co, li);
    check(co.size() > after_two, "third anchor appends its segment");
    check(t.pop_anchor(), "pop the third");
    t.overlay(an, co, li);
    check(co.size() == after_two, "pop restores the two-anchor polyline exactly");
    t.update(click_at(150, 60), out, consumed);
    check(t.commit_pending(out) && out.size() >= 6 && out[0] == 100.0f && out[1] == 10.0f,
          "Enter closes with the edge path back to the first anchor");
    check(lw.builds() == 1, "the tool never rebuilt the cost image");

    // Detaching the livewire falls back to straight segments.
    t.set_livewire(nullptr);
    check(!t.snapping(), "no livewire, no snapping");
    t.update(click_at(10, 10), out, consumed);
    t.update(at(30, 30), out, consumed);
    t.overlay(an, co, li);
    check(same_points(li, {10, 10, 30, 30}, 1e-6f), "straight again");
    t.cancel();
}

void test_path_tool_space() {
    // Fed pixels are frame pixels scaled 2x on X, 4x on Y, each offset --
    // different axes catch a transposed scale a uniform factor would miss.
    // Anchors and the closed polygon come back in fed pixels; the livewire sees frame pixels.
    const int W = 200, H = 120;
    mk::Livewire lw;
    lw.build(step_edge_rgb(W, H).data(), W, H);
    mk::PathTool t;
    t.set_livewire(&lw);
    mk::PathSpace sp;
    sp.to_frame = [](float x, float y, float& fx, float& fy) { fx = (x - 5.0f) * 0.5f; fy = (y - 7.0f) * 0.25f; };
    sp.from_frame = [](float fx, float fy, float& x, float& y) { x = fx * 2.0f + 5.0f; y = fy * 4.0f + 7.0f; };
    t.set_space(sp);
    std::vector<float> out, an, co, li;
    bool consumed;
    t.update(click_at(205, 47), out, consumed);       // frame (100, 10)
    t.update(click_at(205, 447), out, consumed);      // frame (100, 110)
    t.update(click_at(305, 247), out, consumed);      // frame (150, 60)
    t.overlay(an, co, li);
    check(same_points(an, {205, 47, 205, 447, 305, 247}, 1e-3f), "anchors reported in fed pixels");
    bool on_edge = true;
    for (size_t i = 2; i + 3 < co.size() && i < 2 * 100; i += 2) {
        const float fx = (co[i] - 5.0f) * 0.5f;
        on_edge &= std::floor(fx) == 99.0f || std::floor(fx) == 100.0f;
    }
    check(on_edge, "the committed edge run maps back to the ridge in frame pixels");
    t.update(at(30, 30), out, consumed);
    check(!t.near_first(), "fed (30,30) is 175.8 px from the first anchor");
    t.update(at(210, 45), out, consumed);
    check(t.near_first(), "fed (210,45) is 5.4 px from it");
    check(t.update(click_at(210, 45), out, consumed) && out.size() >= 6, "closes in fed space");
    check(std::fabs(out[0] - 205.0f) < 1e-3f && std::fabs(out[1] - 47.0f) < 1e-3f,
          "closed polygon starts at the fed first anchor");
}

// Anchor on the vertical arm, target on the horizontal: the straight line
// between them is off both edges, so this is the case criterion #7 needs
// -- the true minimum curves around the corner.
void test_livewire_corner_path() {
    const int W = 200, H = 120;
    mk::Livewire lw;
    lw.build(l_corner_rgb(W, H).data(), W, H);
    lw.set_anchor(100, 100);
    std::vector<int> p;
    check(lw.path_to(150, 60, p), "corner path found");
    check(p.size() >= 4 && p[0] == 100 && p[1] == 100, "corner path starts at the anchor");
    check(p.size() >= 4 && p[p.size() - 2] == 150 && p[p.size() - 1] == 60,
          "corner path ends at the target");
    // Distance to the nearer arm; also flags a link crossing two
    // non-degenerate, different direction codes -- the pair link_cost_k
    // takes through the search that no prior fixture reached.
    float worst = 0.0f;
    bool asymmetric_pair = false;
    for (size_t i = 0; i + 1 < p.size(); i += 2) {
        const float px = (float)p[i], py = (float)p[i + 1];
        worst = std::max(worst, std::min(std::fabs(px - 99.5f), std::fabs(py - 59.5f)));
        if (i + 3 < p.size()) {
            float dpx, dpy, dqx, dqy;
            lw.direction(p[i], p[i + 1], dpx, dpy);
            lw.direction(p[i + 2], p[i + 3], dqx, dqy);
            const bool both_gradient = (dpx != 0.0f || dpy != 0.0f) && (dqx != 0.0f || dqy != 0.0f);
            if (both_gradient && dpx * dqx + dpy * dqy < 0.5f) asymmetric_pair = true;
        }
    }
    check(worst <= 1.0f, "corner path within 1 px of the nearer arm (worst " +
                             std::to_string(worst) + ")");
    check(asymmetric_pair,
          "the search crosses a genuinely different, non-degenerate direction code pair");
    const double cost = lw.path_cost(150, 60);
    check(cost >= 0.0 && cost < 5.0,
          "the corner path is cheap, not the straight diagonal: " + std::to_string(cost));
}

// Pins link_cost_k's sign source directly: 0.327843 hand-derived (fd 2/3,
// fz 0 on the zero crossing, fg 75/255, all axial) against the bowtie
// pair. The sign[cq] mutant gives 0.186196 -- fails at this tolerance.
void test_livewire_sign_alignment() {
    mk::Livewire lw;
    lw.build(bowtie_rgb(200, 120).data(), 200, 120);
    float dpx, dpy, dqx, dqy;
    lw.direction(100, 59, dpx, dpy);
    lw.direction(100, 60, dqx, dqy);
    check((dpx != 0.0f || dpy != 0.0f) && (dqx != 0.0f || dqy != 0.0f),
          "both sides of the bowtie have a gradient");
    check(dpx * dqx + dpy * dqy < 0.5f, "the bowtie pair's codes genuinely differ");
    const float lc = lw.link_cost(100, 59, 100, 60);
    check(std::fabs(lc - 0.327843f) < 0.001f,
          "link_cost_k sign-aligns to D'(p), not D'(q): " + std::to_string(lc));
}

// ---------------------------------------------------------------------------
// Task 9: floors at 8K. SS_MASK_BENCH=<dir> writes the fixture there and
// prints medians of three repeats; nothing here fails on a number.
// ---------------------------------------------------------------------------

template <class F>
double median_ms(F&& f, int repeats = 3) {
    std::vector<double> t;
    for (int i = 0; i < repeats; i++) {
        const auto a = std::chrono::steady_clock::now();
        f();
        const auto b = std::chrono::steady_clock::now();
        t.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

void bench_8k(const char* dir) {
    const int W = 7680, H = 3840;
    const fs::path root(dir), images = root / "images", masks = root / "masks";
    std::error_code ec;
    fs::create_directories(images, ec);
    fs::create_directories(masks, ec);
    for (int i = 0; i < 3; i++) {
        const std::string key = "f000" + std::to_string(i);
        if (!fs::exists(images / (key + ".jpg")))
            write_jpg_rgb(images / (key + ".jpg"), W, H, synth_rgb(W, H, (uint32_t)i));
        if (!fs::exists(masks / (key + ".png")))
            write_png_gray(masks / (key + ".png"), W, H, synth_mask(W, H, (uint32_t)i));
    }
    std::printf("bench: fixture at %s\n", dir);
    int fw, fh;
    std::vector<uint8_t> rgb;
    std::printf("bench load_rgb 8K JPEG           %8.1f ms\n",
                median_ms([&] { app::load_rgb((images / "f0000.jpg").string(), fw, fh, rgb); }));
    int mw, mh;
    std::vector<uint8_t> base;
    std::printf("bench load_stencil 8K PNG        %8.1f ms\n",
                median_ms([&] { app::load_stencil((masks / "f0000.png").string(), mw, mh, base); }));
    std::vector<uint8_t> png;
    std::printf("bench encode_gray_png 8K mask    %8.1f ms\n",
                median_ms([&] { mk::encode_gray_png(base.data(), W, H, png); }));
    std::printf("bench rle_encode 8K plane        %8.1f ms\n",
                median_ms([&] { gui::rle_encode(base); }));

    const fs::path layer = root / mk::kLayerDirName;
    fs::remove_all(layer, ec);
    mk::LayerIndex idx;
    idx.mask_root = masks.string();
    std::string err, warn;
    mk::MaskDoc d;
    d.load(layer.string(), masks.string(), "f0000", W, H, idx, err, warn);

    // Criterion #4, CPU half: 20 brush strokes, radius 100 px, 500 px long:
    // rasterize + paint (with the op's RLE) + derive the stroke's part of a
    // 4096-square window at step 1.
    mk::Window win;
    win.r = {1000, 0, 5096, 3840};
    win.step = 1;
    win.tw = 4096;
    win.th = 3840;
    std::vector<uint8_t> rgba;
    mk::WindowSource src;
    src.rgb = rgb.data(); src.fw = fw; src.fh = fh;
    src.W = W; src.H = H;
    std::vector<double> strokes;
    for (int k = 0; k < 20; k++) {
        gui::ShapeStroke s;
        s.kind = gui::ShapeKind::Brush;
        s.brush_radius = 100.0f;
        const float x = 1200.0f + 150.0f * k, y = 400.0f + 120.0f * k;
        s.pts = {x, y, x + 250.0f, y + 50.0f, x + 500.0f, y};
        const auto a = std::chrono::steady_clock::now();
        const mk::Rect r = mk::stroke_bounds(s, W, H);
        gui::Stencil st;
        gui::rasterize_shape(s, W, H, st);
        d.paint(k % 2 ? mk::Paint::ForceKeep : mk::Paint::ForceDrop, std::move(st), r);
        src.composite = d.composite().data(); src.drop = d.drop().data(); src.keep = d.keep().data();
        mk::derive_window(win, d.last_change(), src, rgba);
        const auto b = std::chrono::steady_clock::now();
        strokes.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    std::sort(strokes.begin(), strokes.end());
    std::printf("bench stroke commit (CPU) 8K     median %8.1f ms   max %8.1f ms   [bar: median <= 100, max <= 250]\n",
                strokes[10], strokes.back());
    std::printf("bench history bytes after 20     %zu\n", d.history_bytes());
    std::printf("bench derive full 4096x3840 win  %8.1f ms\n",
                median_ms([&] { mk::derive_window(win, win.r, src, rgba); }));
    mk::Window whole;
    whole.r = {0, 0, W, H};
    whole.step = 2;
    whole.tw = 3840;
    whole.th = 1920;
    std::printf("bench derive whole mask step 2   %8.1f ms\n",
                median_ms([&] { mk::derive_window(whole, whole.r, src, rgba); }));
    std::printf("bench MaskDoc::save 8K           %8.1f ms\n",
                median_ms([&] { d.save(layer.string(), masks.string(), idx, err); }));
    std::printf("bench undo x20                   %8.1f ms\n",
                median_ms([&] { for (int k = 0; k < 20; k++) d.undo(); for (int k = 0; k < 20; k++) d.redo(); }, 1));
    fs::remove_all(layer, ec);
    for (int i = 0; i < 3; i++) {
        const std::string key = "f000" + std::to_string(i);
        write_png_gray(masks / (key + ".png"), W, H, synth_mask(W, H, (uint32_t)i));
    }
}

// ---------------------------------------------------------------------------
// Plan 2, Task 9: livewire floors. Criterion 5: build <= 300 ms at 8K, built
// once. Criterion 6: <= 16 ms per cursor move, p95, after the first 200 ms.
// ---------------------------------------------------------------------------

// p95/max of a sorted-in-place sample; (0, 0) on an empty one.
void p95_max(std::vector<double>& v, double& p95, double& mx) {
    std::sort(v.begin(), v.end());
    p95 = v.empty() ? 0.0 : v[(size_t)(0.95 * (double)(v.size() - 1))];
    mx = v.empty() ? 0.0 : v.back();
}

// Pearson r between move index and its time, in call order: whether cost
// trends with index (it does -- see docs/notes/mask-editor.md).
double index_corr(const std::vector<double>& y) {
    const size_t n = y.size();
    double mi = 0.0, my = 0.0;
    for (size_t i = 0; i < n; i++) { mi += (double)i; my += y[i]; }
    mi /= (double)n; my /= (double)n;
    double cov = 0.0, vi = 0.0, vy = 0.0;
    for (size_t i = 0; i < n; i++) {
        const double di = (double)i - mi, dy = y[i] - my;
        cov += di * dy; vi += di * di; vy += dy * dy;
    }
    return (vi > 0.0 && vy > 0.0) ? cov / std::sqrt(vi * vy) : 0.0;
}

// p95 of the last n moves in call order: what the warm-up filter would keep
// if it admitted a fixed suffix instead of a cumulative-time threshold.
double tail_p95(const std::vector<double>& all, size_t n) {
    std::vector<double> tail(all.end() - (long)std::min(n, all.size()), all.end());
    double p95, mx;
    p95_max(tail, p95, mx);
    return p95;
}

void bench_livewire_on(const char* label, const std::vector<uint8_t>& rgb, int fw, int fh) {
    mk::Livewire lw;
    // One direct call, not median_ms (repeats=3 would leave builds() at 3
    // before a single cursor move, making criterion 5's builds==1 unmeasurable).
    const auto t0 = std::chrono::steady_clock::now();
    lw.build(rgb.data(), fw, fh);
    const double build_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    std::printf("bench livewire %-8s %dx%d -> grid %dx%d step %d  build %8.1f ms  [bar: <= 300 at 8K]  bytes %.1f MB\n",
                label, fw, fh, lw.width(), lw.height(), lw.step(), build_ms,
                (double)lw.bytes() / 1048576.0);
    // A segment: anchor a third in, cursor walks right 2 grid px/move, 200
    // moves each timed alone. `ms` is the brief's cumulative-time warm-up
    // filter; `ms_all` is unfiltered (see report: the filter can starve).
    const int ax = lw.width() / 3, ay = lw.height() / 2;
    lw.set_anchor(ax, ay);
    std::vector<int> path;
    std::vector<double> ms, ms_all;
    std::vector<size_t> dpops;
    double elapsed = 0.0;
    size_t skipped = 0, found = 0, prev_pops = lw.pops();
    for (int i = 1; i <= 200; i++) {
        const auto a = std::chrono::steady_clock::now();
        const bool ok = lw.path_to(std::min(lw.width() - 1, ax + 2 * i), ay + (i % 5) - 2, path);
        const auto b = std::chrono::steady_clock::now();
        if (ok && !path.empty()) found++;
        const double t = std::chrono::duration<double, std::milli>(b - a).count();
        ms_all.push_back(t);
        dpops.push_back(lw.pops() - prev_pops);
        prev_pops = lw.pops();
        elapsed += t;
        if (elapsed <= 200.0) { skipped++; continue; }
        ms.push_back(t);
    }
    double p95, mx, p95_all, mx_all;
    p95_max(ms, p95, mx);
    std::vector<double> ms_all_sorted = ms_all;
    p95_max(ms_all_sorted, p95_all, mx_all);
    std::printf("bench livewire %-8s cursor moves: %zu timed (%zu in the first 200 ms), p95 %8.2f ms  max %8.2f ms  | all 200: p95 %8.2f ms  max %8.2f ms  pops %zu  builds %d  found %zu/200  [bar: p95 <= 16, builds == 1]\n",
                label, ms.size(), skipped, p95, mx, p95_all, mx_all, lw.pops(), lw.builds(), found);
    // Does cost trend with move index, and what would a suffix-based filter
    // (rather than the cumulative-time one above) have reported.
    std::vector<size_t> dp = dpops;
    std::sort(dp.begin(), dp.end());
    std::printf("bench livewire %-8s tail p95: last150 %7.3f  last100 %7.3f  last50 %7.3f ms  corr(idx,ms) %+.3f  elapsed_total %7.1f/200 ms  pops/move min/median/max %zu/%zu/%zu\n",
                label, tail_p95(ms_all, 150), tail_p95(ms_all, 100), tail_p95(ms_all, 50),
                index_corr(ms_all), elapsed, dp.front(), dp[dp.size() / 2], dp.back());
}

void bench_livewire(const char* dir) {
    const fs::path images = fs::path(dir) / "images";
    const fs::path f0 = images / "f0000.jpg";
    if (!fs::exists(f0))
        write_jpg_rgb(f0, 7680, 3840, synth_rgb(7680, 3840, 0));
    int fw, fh;
    std::vector<uint8_t> rgb;
    if (app::load_rgb(f0.string(), fw, fh, rgb)) bench_livewire_on("8K", rgb, fw, fh);
    else std::printf("bench livewire 8K: could not read %s\n", f0.string().c_str());
    if (const char* real = std::getenv("SS_LIVEWIRE_IMAGE")) {
        if (app::load_rgb(real, fw, fh, rgb)) bench_livewire_on("still", rgb, fw, fh);
        else std::printf("bench livewire still: could not read %s\n", real);
    }
}

}  // namespace

int main() {
    test_fnv();
    test_composite_truth_table();
    test_keys_and_paths();
    test_png_and_atomic_write();
    test_temp_write_path_unique();
    test_index_roundtrip();
    test_save_and_layers_roundtrip();
    test_fingerprint_decides();
    test_revert_is_byte_exact();
    test_save_without_mask();
    test_revert_reports_removal_failure();
    test_recomposite_all_continues_past_failure();
    test_revert_all_continues_past_failure();
    test_revert_all_guards_empty_mask_root();
    test_doc_load_and_paint();
    test_doc_without_mask();
    test_read_write_rect_roundtrip();
    test_noop_paint_does_not_dirty();
    test_undo_redo();
    test_byte_cap_eviction();
    test_orientation_mapping();
    test_view_math();
    test_derive_window();
    test_session();
    test_session_size_mismatch();
    test_session_close_resets_paths();
    test_path_fill_parity();
    test_livewire_features();
    test_livewire_mapping();
    test_livewire_edge_path();
    test_livewire_reanchor();
    test_livewire_diagonal();
    test_livewire_once();
    test_path_tool_basic();
    test_path_tool_livewire();
    test_path_tool_space();
    test_livewire_corner_path();
    test_livewire_sign_alignment();
    if (const char* b = std::getenv("SS_MASK_BENCH")) bench_8k(b);
    if (const char* b = std::getenv("SS_MASK_BENCH")) bench_livewire(b);
    std::printf("%s: %d failure(s)\n", SS_FILE, g_failures);
    return g_failures;
}
