// `spirula densify --check`: the synthetic staircase S-1 end to end, through
// the same plan, run and writer a dataset gets. English, like every --check.
#pragma once

#include <string>

namespace roma {

struct CheckOptions {
    std::string dir;          // where the scene dataset is written; "" = a temp dir
    std::string matches;      // .rwm dumps for its views; "" = the geometric oracle
    int match_size = 640;     // the oracle's resolution: RoMa's `base`
    double noise_px = 0.1;    // oracle jitter, match pixels (a guess at RoMa's, not a measurement)
    double outliers = 0.03;   // oracle share of matches sent somewhere random
    std::string source = "roma";  // roma, moge or hybrid; moge and hybrid synthesise depths/
    bool masks = true;        // mask the empty background, as a sky mask would
    bool keep = false;        // leave the dataset behind
    double cycle_px = 0, refine_huber = 0;   // DensifyOptions' own, auto by default
};

// 0 when every gate passes.
int densifyCheck(const CheckOptions& opt);

}  // namespace roma
