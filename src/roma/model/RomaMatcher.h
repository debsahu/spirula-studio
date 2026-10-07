#pragma once
// roma::Matcher (roma/Matcher.h) on RoMa v2 itself. Until the refiners land
// this is the COARSE match: the stride-4 DPT warp, certainty = sigmoid of its
// overlap logit, which runs ahead of RoMa's refined certainty.

#include "roma/Matcher.h"
#include "roma/Roma.h"

#include <string>

namespace roma {

class RomaMatcher : public Matcher {
public:
    // `size` is the square the pair is matched at: RoMa's H_lr (640 is `base`).
    explicit RomaMatcher(const std::string& checkpoint, int size = 640);
    int inputSize() const override { return size_; }
    Warp match(const MatchImage& a, const MatchImage& b) override;
    std::string describe() const override;

private:
    Model model_;
    int   size_;
};

}  // namespace roma
