// Ported from Lichtfeld-Densification-Plugin (GPL-3.0-or-later), Copyright (c) 2025 Shady Gmira and contributors; core/sampling.py@ab0b04e.
//
// Which pixels of a reference view are triangulated: most drawn by certainty,
// the rest one per tile so low-certainty regions are not left empty.
#pragma once

#include <cstdint>
#include <vector>

namespace roma {

struct SampleOptions {
    int count = 10000;
    float cap = 0.9f;        // certainties above this weigh the same
    int border = 2;          // pixels from the edge never drawn
    int tiles = 24;          // coverage grid: tile side = width / tiles
    float weighted_share = 0.85f;
    bool no_filter = false;  // the plugin's "no filter": the `count` most certain
    uint64_t seed = 0;
};

// Sorted, unique flat indices (y * w + x). Weighted draws use exponential keys
// (Efraimidis-Spirakis): numpy's choice(replace=False, p) in distribution,
// not draw for draw.
std::vector<int64_t> sampleWithCoverage(const std::vector<float>& certainty, int w, int h,
                                        const SampleOptions& opt);

}  // namespace roma
