#pragma once

// The correction layer on disk: per edited frame a byte copy of the mask the
// run wrote (.base.png), a forced-drop and a forced-keep stencil, composed as
// keep ? 255 : drop ? 0 : base into masks/. An index of FNV-1a fingerprints
// tells a regenerated mask from the composite written last time. Polarity is
// the app's, 255 = keep. No ImGui, no GL. Design: docs/notes/mask-editor.md.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gui {
namespace mask {

inline constexpr const char* kLayerDirName = "mask_edits";
inline constexpr const char* kIndexFileName = "index.json";

uint64_t fnv1a64(const uint8_t* p, size_t n);
std::string fnv_hex(uint64_t h);
bool fnv_parse(const std::string& s, uint64_t& out);

bool read_file(const std::string& path, std::vector<uint8_t>& out);

// `dir` lexically normalized, without a trailing separator: what the frame
// keys and the skip test are computed against.
std::string normalize_dir(const std::string& dir);

// "<rel_dir>/<stem>" of `file` under `image_root` ("cam0/00023", or "00023"
// at the root itself). Lexical, as group_frames_by_camera keys it.
std::string frame_key(const std::string& image_root, const std::string& file);

std::string mask_file(const std::string& mask_root, const std::string& key);

enum class Layer { Base, Drop, Keep };
std::string layer_file(const std::string& layer_root, const std::string& key,
                       Layer l);

// final = keep ? 255 : drop ? 0 : base. Either layer may be null.
void composite(const uint8_t* base, const uint8_t* drop, const uint8_t* keep,
               size_t n, uint8_t* out);

bool encode_gray_png(const uint8_t* px, int w, int h, std::vector<uint8_t>& png);
// A sibling of `dst` no other call, in this process or another, is using:
// <dst>.<pid>.<counter>.tmp.
std::string temp_write_path(const std::string& dst);
// Through a sibling temp file and a rename, so an existing file's inode is
// never written into: masks gathered beside photos can be hard links.
bool write_file_atomic(const std::string& path, const uint8_t* data, size_t n);
bool fingerprint_file(const std::string& path, uint64_t& out);
std::string utc_now_iso();

struct IndexEntry {
    uint64_t base_fp = 0;        // of .base.png; 0 while no base exists
    uint64_t composite_fp = 0;   // of masks/<key>.png as last written; 0 if never
    float kept = 0.0f;
    std::string saved_at;        // UTC, "2026-09-21T10:00:00Z"
};

struct LayerIndex {
    std::string mask_root;
    std::map<std::string, IndexEntry> frames;
    // A missing file is an empty index and succeeds; a corrupt one fails.
    bool load(const std::string& layer_root, std::string& error);
    bool save(const std::string& layer_root, std::string& error) const;
};

}  // namespace mask
}  // namespace gui
