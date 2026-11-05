#include "roma/model/Fetch.h"

#include "core/LicenseConsent.h"
#include "roma/model/LicenseTexts.h"

namespace roma {

void register_licenses() {
    static const spirula::license::Terms kRoma{
        "romav2", "RoMa v2 License (MIT)",
        "https://github.com/Parskatt/RoMaV2/blob/main/LICENSE", kRomaV2Mit};
    spirula::license::register_dinov3();
    spirula::license::register_terms(&kRoma);
}

}  // namespace roma
