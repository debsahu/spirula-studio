#pragma once

// The COLMAP models a dataset holds, for the training screen's model chooser.
// Enumerated the way the parser's own auto-pick does (data/parsers/
// ColmapParser.cpp), so "auto" and the first entry agree.

#include <cstdint>
#include <string>
#include <vector>

namespace gui {

struct ReconModel {
    std::string rel;        // relative to the dataset, "sparse/0-roma"
    int64_t images = -1;    // registered images; -1 when it cannot be read
    int64_t points = -1;    // points3D.bin's count; -1 for no .bin
};

// points3D.bin's count of the model in `dir`, or -1.
int64_t recon_point_count(const std::string& dir);

// A model `spirula densify` wrote: densify never reads one as its source.
bool is_dense_model(const std::string& rel);

// Most images first, then by path: the order the parser tries them in.
std::vector<ReconModel> list_recon_models(const std::string& dataset);

}  // namespace gui
