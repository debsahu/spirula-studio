#pragma once
// roma::Matcher (roma/Matcher.h) on RoMa v2: the full match, coarse plus the
// three refiners at one or two scales, A into B. matchBoth() adds B into A.

#include "roma/Matcher.h"
#include "roma/Roma.h"

#include <string>
#include <utility>

namespace roma {

// RoMaV2.apply_setting's turbo/fast/base/precise, and the Lichtfeld plugin's
// `high` (640 lr, 960 hr, bidirectional).
enum class Preset { Turbo, Fast, Base, High, Precise };

struct PresetSpec {
    const char* name;
    int lr, hr;           // hr 0: one scale
    bool bidirectional;   // what upstream computes; match() needs only A into B
};

const PresetSpec& preset_spec(Preset p);
// The seam's Warp of one direction: certainty is sigmoid(logit), precision the
// confidence's last three channels as they are.
Warp warpOf(const DenseMatch& d);
// "turbo" ... "precise"; false on anything else.
bool parse_preset(const std::string& s, Preset& out);

class RomaMatcher : public Matcher {
public:
    explicit RomaMatcher(const std::string& checkpoint, Preset preset = Preset::Base);
    // The size the warp comes out at: the hr side, or lr for one scale.
    int inputSize() const override;
    // Any input sizes, A's and B's independent: each is resampled to lr (and hr)
    // with RoMa's own antialiased bicubic, the identity at the same size.
    Warp match(const MatchImage& a, const MatchImage& b) override;
    std::pair<Warp, Warp> matchBoth(const MatchImage& a, const MatchImage& b);
    std::string describe() const override;

    Model& model() { return model_; }
    const PresetSpec& spec() const { return preset_spec(preset_); }

private:
    MatchResult run(const MatchImage& a, const MatchImage& b, bool both);

    Model  model_;
    Preset preset_;
};

}  // namespace roma
