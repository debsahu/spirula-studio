// Matches read back from files another RoMa implementation wrote:
// <dir>/<A>__<B>.rwm per pair, where A and B are MatchImage::name.
// reference/python/roma_dump_matches.py writes them from upstream RoMaV2.
//
// .rwm, little endian: "RWM1", int32 width, height, encoding; then the warp
// and the certainty. Encoding 0 is float32 for both; 1 stores the warp as
// int16 of u * 32767 (0.01 px at 640) and the certainty as uint16 of c * 65535.
#pragma once

#include "roma/Matcher.h"

namespace roma {

class DumpMatcher : public Matcher {
public:
    DumpMatcher(std::string dir, int input_size);
    int inputSize() const override { return input_size_; }
    Warp match(const MatchImage& a, const MatchImage& b) override;
    std::string describe() const override;

    static std::string pairFile(const std::string& dir, const std::string& a,
                                const std::string& b);

private:
    std::string dir_;
    int input_size_;
};

Warp readWarp(const std::string& path);
void writeWarp(const std::string& path, const Warp& w, int encoding);

}  // namespace roma
