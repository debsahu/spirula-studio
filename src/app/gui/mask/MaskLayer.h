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

}  // namespace mask
}  // namespace gui
