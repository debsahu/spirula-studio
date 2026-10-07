// What the warp cache needs to know about the RoMa v2 matcher this process
// runs, read from the process: header-only because only the tools that link
// ss_roma can resolve it.
#pragma once

#include <string>

#include "core/Env.h"
#include "nn/Device.h"
#include "roma/WarpCache.h"
#include "roma/model/Fetch.h"
#include "roma/model/Model.h"
#include "roma/model/RomaMatcher.h"

namespace roma {

// Call after the matcher has loaded, so the device is the one that ran.
// `checkpoint_sha256` is the digest of the bytes that were loaded.
inline RomaSettings romaSettings(const PresetSpec& spec, const std::string& checkpoint_sha256) {
    RomaSettings rs;
    rs.preset = spec.name;
    rs.lr = spec.lr;
    rs.hr = spec.hr;
    rs.checkpoint_sha256 = checkpoint_sha256;
    rs.f16_weights = f16_weights();
    rs.rope_rounds = matcher_rope_rounds();
    rs.local_corr_fused = local_corr_fused();
    if (const char* g = spirula::env("NN_GEMM_KERNEL")) rs.gemm_kernel = g;
    rs.device = nn::current_device_selector();
    if (rs.device.empty()) rs.device = nn::configured_device_selector();
    rs.model_digest = modelSourceDigest();
    return rs;
}

}  // namespace roma
