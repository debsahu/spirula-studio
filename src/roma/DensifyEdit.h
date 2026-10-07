// A hand-cleaned dense cloud written as a sibling of the model it came from:
// <m>-roma becomes <m>-roma-edit. The original is never written; cameras and
// images are copied byte for byte. Round trip of the editor, design after
// spirula-studio#154 (D1odeKing, GPL-3.0). docs/notes/densify.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace roma {

inline constexpr const char* kEditSuffix = "-edit";

// "<m>-roma" -> "<m>-roma-edit"; an edit maps to itself. Throws for any other name.
std::string editedSiblingDir(const std::string& model_dir);
bool isEditedModel(const std::string& model_dir);

struct EditResult {
    int64_t before = 0, kept = 0;
    std::string out_dir;
};

// Keeps the i-th point of points3D.bin where keep[i] is non-zero. Throws,
// writing nothing, unless something is removed and something stays; an edit
// of an edit rewrites it, and `edited_from` keeps naming the first model.
EditResult writeEditedSibling(const std::string& model_dir, const std::vector<uint8_t>& keep,
                              bool replace = false);

// Reason a folder cannot be edited this way, "" when it can.
std::string editProblem(const std::string& model_dir);

}  // namespace roma
