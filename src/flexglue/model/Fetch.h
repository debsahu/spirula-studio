#pragma once
// The FlexGlue checkpoint, from the authors' release. It bundles Meta's DINOv3
// ConvNeXt-B (extractor.backbone.*), so loading it needs the DINOv3 licence as
// well as FlexGlue's MIT one. Nothing is redistributed: no_mirror.

#include "nn/io/Fetch.h"

#include <string>

namespace flexglue {

// Adds "dinov3" and "flexglue" to core/LicenseConsent.h's table. Idempotent.
void register_licenses();

inline const nn::FetchFile& checkpoint_file() {
    static const nn::FetchFile f = [] {
        nn::FetchFile x;
        x.file = "flexglue.pth";
        x.url = "https://github.com/cvg/FlexGlue/releases/download/v0.1/flexglue.pth";
        x.sha256 = "d7371f743b8cdc4adf723c2aa05b181587fe4bc177fced139697975dff790068";
        x.bytes = 567328059ull;
        x.license_family = "dinov3,flexglue";
        x.license_gates_load = true;
        x.no_mirror = true;
        return x;
    }();
    return f;
}

// "flexglue" is fetched (licence-gated); anything else is a path to the .pth,
// which is used as is.
std::string resolve_checkpoint(const std::string& id_or_path);

}  // namespace flexglue
