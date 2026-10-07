#include "roma/model/RomaMatcher.h"

#include <stdexcept>

namespace roma {
namespace {

const PresetSpec kPresets[] = {
    {"turbo", 320, 0, false},
    {"fast", 512, 0, false},
    {"base", 640, 0, false},
    {"high", 640, 960, true},
    {"precise", 800, 1280, true},
};

std::vector<float> to_input(const MatchImage& m, int size) {
    if (!m.rgb || m.width <= 0 || m.height <= 0)
        throw std::runtime_error("roma: empty image '" + m.name + "'");
    return resize_rgb(m.rgb, m.width, m.height, size, size);
}

Warp to_warp(const DenseMatch& d) {
    Warp w;
    w.width = d.w;
    w.height = d.h;
    w.warp = d.warp;
    w.certainty.resize((size_t)d.w * d.h);
    for (size_t i = 0; i < w.certainty.size(); ++i) w.certainty[i] = d.overlap(i);
    return w;
}

}  // namespace

const PresetSpec& preset_spec(Preset p) { return kPresets[(int)p]; }

bool parse_preset(const std::string& s, Preset& out) {
    for (int i = 0; i < 5; ++i)
        if (s == kPresets[i].name) {
            out = (Preset)i;
            return true;
        }
    return false;
}

RomaMatcher::RomaMatcher(const std::string& checkpoint, Preset preset) : preset_(preset) {
    model_.load(checkpoint);
}

int RomaMatcher::inputSize() const { return spec().hr ? spec().hr : spec().lr; }

MatchResult RomaMatcher::run(const MatchImage& a, const MatchImage& b, bool both) {
    const PresetSpec& ps = spec();
    MatchSpec ms;
    ms.lr_h = ms.lr_w = ps.lr;
    ms.hr_h = ms.hr_w = ps.hr;
    ms.bidirectional = both;
    const std::vector<float> al = to_input(a, ps.lr), bl = to_input(b, ps.lr);
    std::vector<float> ah, bh;
    if (ps.hr) {
        ah = to_input(a, ps.hr);
        bh = to_input(b, ps.hr);
    }
    return model_.match(al.data(), bl.data(), ps.hr ? ah.data() : nullptr,
                        ps.hr ? bh.data() : nullptr, ms);
}

Warp RomaMatcher::match(const MatchImage& a, const MatchImage& b) {
    return to_warp(run(a, b, false).ab);
}

std::pair<Warp, Warp> RomaMatcher::matchBoth(const MatchImage& a, const MatchImage& b) {
    const MatchResult r = run(a, b, true);
    return {to_warp(r.ab), to_warp(r.ba)};
}

std::string RomaMatcher::describe() const {
    const PresetSpec& ps = spec();
    std::string s = std::string("RoMa v2 ") + ps.name + " (" + std::to_string(ps.lr) + "px";
    if (ps.hr) s += " + " + std::to_string(ps.hr) + "px";
    return s + ", refined)";
}

}  // namespace roma
