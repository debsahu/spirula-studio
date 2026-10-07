// What romaSettings() copies into the warp cache's key: every field the process reports must
// change the identity, so a cache never serves a warp made by other weights or sources.
#include <cstdio>
#include <string>

#include "roma/RomaIdentity.h"
#include "sfm/tests/TestMain.h"

namespace {

int fails = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    fails += !ok;
}

roma::ProcessProbes probes() {
    roma::ProcessProbes p;
    p.f16_weights = true;
    p.rope_rounds = false;
    p.local_corr_fused = false;
    p.gemm_kernel = "tile16";
    p.device = "gpu0";
    p.model_digest = "digest-a";
    return p;
}

int body(int, char**) {
    const roma::PresetSpec spec{"base", 640, 0, false};
    const std::string sha = "checkpoint-a";
    const std::string base = roma::romaIdentity(roma::romaSettings(spec, sha, probes()));

    auto varied = [&](const char* what, auto change) {
        roma::ProcessProbes p = probes();
        change(p);
        check(roma::romaIdentity(roma::romaSettings(spec, sha, p)) != base,
              std::string("the cache key changes with ") + what);
    };
    varied("f16_weights", [](roma::ProcessProbes& p) { p.f16_weights = false; });
    varied("rope_rounds", [](roma::ProcessProbes& p) { p.rope_rounds = true; });
    varied("local_corr_fused", [](roma::ProcessProbes& p) { p.local_corr_fused = true; });
    varied("the gemm kernel", [](roma::ProcessProbes& p) { p.gemm_kernel = "tile32"; });
    varied("the device", [](roma::ProcessProbes& p) { p.device = "gpu1"; });
    varied("model_digest", [](roma::ProcessProbes& p) { p.model_digest = "digest-b"; });

    const roma::RomaSettings rs = roma::romaSettings(spec, sha, probes());
    check(rs.f16_weights && rs.model_digest == "digest-a", "romaSettings carries f16_weights and model_digest through");
    check(rs.preset == "base" && rs.lr == 640 && rs.hr == 0 && rs.checkpoint_sha256 == sha,
          "romaSettings carries the preset and the checkpoint digest");
    check(roma::romaIdentity(roma::romaSettings(spec, "checkpoint-b", probes())) != base,
          "the cache key changes with the checkpoint");
    check(std::string(roma::modelSourceDigest()).size() == 64,
          "the build's model digest is a SHA-256 hex string, not empty");
    return fails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
