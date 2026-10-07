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

// What the process reports about the matcher it ran.
struct ProcessProbes {
    bool f16_weights = false, rope_rounds = true, local_corr_fused = true;
    std::string gemm_kernel, device, model_digest;
};

// Call after the matcher has loaded, so the device is the one that ran.
inline ProcessProbes processProbes() {
    ProcessProbes p;
    p.f16_weights = f16_weights();
    p.rope_rounds = matcher_rope_rounds();
    p.local_corr_fused = local_corr_fused();
    if (const char* g = spirula::env("NN_GEMM_KERNEL")) p.gemm_kernel = g;
    p.device = nn::current_device_selector();
    if (p.device.empty()) p.device = nn::configured_device_selector();
    p.model_digest = modelSourceDigest();
    return p;
}

// `checkpoint_sha256` is the digest of the bytes that were loaded.
inline RomaSettings romaSettings(const PresetSpec& spec, const std::string& checkpoint_sha256,
                                 const ProcessProbes& p) {
    RomaSettings rs;
    rs.preset = spec.name;
    rs.lr = spec.lr;
    rs.hr = spec.hr;
    rs.checkpoint_sha256 = checkpoint_sha256;
    rs.f16_weights = p.f16_weights;
    rs.rope_rounds = p.rope_rounds;
    rs.local_corr_fused = p.local_corr_fused;
    rs.gemm_kernel = p.gemm_kernel;
    rs.device = p.device;
    rs.model_digest = p.model_digest;
    return rs;
}

inline RomaSettings romaSettings(const PresetSpec& spec, const std::string& checkpoint_sha256) {
    return romaSettings(spec, checkpoint_sha256, processProbes());
}

}  // namespace roma
