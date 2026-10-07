#pragma once
// The RoMa v2 checkpoint: one file, from the authors' own release. It bundles
// Meta's DINOv3 weights, so using it needs both licences accepted
// (core/LicenseConsent.h). Spirula redistributes nothing: no_mirror keeps the
// project mirror out of it, and the file is only ever downloaded to the user's
// own cache.

#include "nn/io/Fetch.h"

#include <string>

namespace roma {

inline const nn::FetchFile& checkpoint_file() {
    static const nn::FetchFile f = [] {
        nn::FetchFile x;
        x.file = "romav2.0.1.pt";
        x.url = "https://github.com/Parskatt/RoMaV2/releases/download/v2.0.1/romav2.0.1.pt";
        x.sha256 = "1557dec0d21b62366465f7ff4d5fdf228cc695d0582e196ad2b80e05230828b7";
        x.bytes = 1095883548ull;
        x.license_family = "dinov3,romav2";
        x.no_mirror = true;
        return x;
    }();
    return f;
}

// A verified local copy, downloading it first if need be. Throws nn::Error
// unless both licences are accepted, before anything is fetched.
inline std::string ensure_checkpoint() { return nn::ensure_file(checkpoint_file(), "roma"); }

}  // namespace roma
