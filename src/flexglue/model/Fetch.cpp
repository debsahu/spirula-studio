#include "flexglue/model/Fetch.h"

#include "core/LicenseConsent.h"
#include "flexglue/model/LicenseTexts.h"

namespace flexglue {

void register_licenses() {
    static const spirula::license::Terms kFlexGlue{
        "flexglue", "FlexGlue License (MIT)",
        "https://github.com/cvg/FlexGlue/blob/main/LICENSE", kFlexGlueMit};
    spirula::license::register_dinov3();
    spirula::license::register_terms(&kFlexGlue);
}

std::string resolve_checkpoint(const std::string& id_or_path) {
    register_licenses();
    if (id_or_path.empty() || id_or_path == "flexglue")
        return nn::ensure_file(checkpoint_file(), "flexglue");
    return id_or_path;
}

}  // namespace flexglue
