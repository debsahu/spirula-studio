// mask_doc_test -- app/gui/mask/: the correction layer on disk, the document
// in memory, the view math, and the session's worker, over synthetic frames
// whose every pixel is known. SS_MASK_BENCH=<dir> also runs the timing
// floors at 7680x3840 and writes that fixture dataset into <dir>.

#include "app/FrameMask.h"
#include "app/gui/edit/Selection.h"
#include "app/gui/mask/MaskDoc.h"
#include "app/gui/mask/MaskLayer.h"
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
    test_doc_load_and_paint();
    test_doc_without_mask();
    test_read_write_rect_roundtrip();
    test_noop_paint_does_not_dirty();
    test_undo_redo();
    test_byte_cap_eviction();
    std::printf("%s: %d failure(s)\n", SS_FILE, g_failures);
    return g_failures;
}
