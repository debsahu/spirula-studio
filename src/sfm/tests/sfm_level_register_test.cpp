// The mapper's level check on a registration (Mapper::levelCheck): a scripted
// source declares each image's true camera-frame up, one image or one rig lens
// 3 deg off it, and the check must refuse exactly that one. Apart from
// sfm_gps_register_test so neither binary opens more GPU contexts than a
// process on some drivers can re-enumerate by uuid.
//
//   sfm_level_register_test [--device N] [--verbose]
//
// Prints FAIL lines and returns the count. The BA is real (GPU).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "sfm/core/PriorSource.h"
#include "sfm/core/Rig.h"
#include "sfm/map/ImuExtrinsic.h"
#include "sfm/map/Mapper.h"
#include "sfm/tests/SyntheticRegister.h"
#include "sfm/tests/SyntheticRig.h"
#include "sfm/tests/TestMain.h"

using namespace sfm;
using namespace synth_reg;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what.c_str());
        fails++;
    }
}

// ---- the level check on a registration (Mapper::levelCheck) -----------------

// Every image declares its true camera-frame up (world +Y through its pose), the
// images in `lie` one 3 deg off it; the level frame is the posed images' consensus,
// as ExifGpsPriors fits it. `checked` lists the images the check asked about, in order.
class TruthLevel : public PriorSource {
public:
    TruthLevel(const std::vector<Pose>& gt, std::set<uint32_t> lie) : lie_(std::move(lie)) {
        for (const Pose& p : gt) u_.push_back(mul(p.R, Vec3{0, 1, 0}));
    }
    bool has(uint32_t) const override { return false; }
    bool relativeRotation(uint32_t, uint32_t, Mat3&, double&) const override { return false; }
    std::vector<uint32_t> neighbours(uint32_t) const override { return {}; }
    PosePriors factors(const std::vector<PosedImage>& imgs) override {
        std::vector<Vec3> votes;
        for (const PosedImage& im : imgs)
            if (im.image < u_.size() && !lie_.count(im.image))
                votes.push_back(mul(transpose(im.pose.R), u_[im.image]));
        PosePriors p;
        const UpConsensus c = consensusUp(votes);
        p.level = {c.ok, c.up, 1.0};
        return p;
    }
    bool declaredUp(uint32_t img, Vec3& u) const override {
        if (img >= u_.size()) return false;
        checked.push_back(img);
        u = lie_.count(img) ? mul(angleAxisToRotation({0.0, 0.0, 3.0 * M_PI / 180.0}), u_[img])
                            : u_[img];
        return true;
    }
    mutable std::vector<uint32_t> checked;

private:
    std::set<uint32_t> lie_;
    std::vector<Vec3> u_;
};

static void testLevel(const Scene& sc, MapperOptions opt) {
    const uint32_t M = (uint32_t)sc.db.images.size();
    TruthLevel honest(sc.gt, {});
    Mapper m0(sc.db, sc.feats, opt, {}, nullptr, nullptr, &honest);
    const std::vector<Reconstruction> r0 = m0.run();
    const Mapper::PriorStats s0 = m0.priorStats();
    const uint32_t reg0 = r0.empty() ? 0 : r0.front().numRegistered();
    std::printf("level honest: %u/%u registered, checked %u, refused %u\n", reg0, M,
                s0.level_checked, s0.level_refused);
    check(reg0 == M && s0.level_refused == 0, "level: no registration of a level image is refused");
    check(s0.level_checked >= M / 2 && s0.level_checked == honest.checked.size(),
          "level: the check reads every registration once the frame exists");
    const uint32_t liar = honest.checked.empty() ? 0 : honest.checked.back();

    TruthLevel lying(sc.gt, {liar});
    Mapper m1(sc.db, sc.feats, opt, {}, nullptr, nullptr, &lying);
    const std::vector<Reconstruction> r1 = m1.run();
    const Mapper::PriorStats s1 = m1.priorStats();
    const bool in = !r1.empty() && registered(r1.front(), liar);
    const size_t asked = (size_t)std::count(lying.checked.begin(), lying.checked.end(), liar);
    std::printf("level, image %u 3 deg off: in %d, asked %zu time(s), refused %u, %u/%u registered\n",
                liar, (int)in, asked, s1.level_refused, r1.empty() ? 0 : r1.front().numRegistered(), M);
    check(asked >= 1 && !in && s1.level_refused >= 1,
          "level: a registration tilted off its declared up is refused");
    check(!r1.empty() && r1.front().numRegistered() == M - 1 && s1.refused == 0 &&
              s1.gps_refused == 0,
          "level: every other image registers, and the refusal is the level check's");
}

static void testLevelRig(MapperOptions opt) {
    std::vector<char> weak(40, 0);
    for (int f = 8; f < 37; f++) weak[f] = f % 4 != 0;
    const synth_rig::RigScene sc = synth_rig::makeRigScene(40, 37, weak);
    const uint32_t M = (uint32_t)sc.M, n = 2 * M;
    const RigTable rigs = buildRigTable(sc.names, {RigDef{"rig", {{"cam0"}, {"cam1"}}}});
    TruthLevel honest(sc.gt, {});
    const RigRun r0 = runRig(sc, rigs, opt, honest, "F");
    std::printf("level rig honest: %u/%u registered, checked %u, refused %u\n",
                r0.model.numRegistered(), n, r0.st.level_checked, r0.st.level_refused);
    check(r0.model.numRegistered() == n && r0.st.level_refused == 0,
          "level rig: no frame of level images is refused");
    // A weak frame the rig placed late; only its cam1 lies, so the rig must check every lens.
    uint32_t f = M;
    for (auto it = r0.placed.rbegin(); it != r0.placed.rend() && f == M; ++it)
        if (weak[*it % M]) f = *it % M;
    TruthLevel lying(sc.gt, {M + f});
    const RigRun r1 = runRig(sc, rigs, opt, lying, "F");
    std::printf("level rig, frame %u cam1 3 deg off: in %d %d, refused %u, %u/%u registered\n", f,
                (int)registered(r1.model, f), (int)registered(r1.model, M + f), r1.st.level_refused,
                r1.model.numRegistered(), n);
    check(f < M && !registered(r1.model, M + f) && r1.st.level_refused >= 1,
          "level rig: a frame with one lens tilted off its declared up is refused");
}

static int body(int argc, char** argv) {
    MapperOptions opt;
    opt.verbose = false;
    opt.focal = 1200;
    opt.focal_trials = 0;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--device" && i + 1 < argc) opt.device = std::stoi(argv[++i]);
        else if (a == "--verbose") opt.verbose = true;
    }
    testLevel(makeScene(40), opt);
    testLevelRig(opt);
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "OK", fails, fails == 1 ? "" : "s");
    return fails;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
