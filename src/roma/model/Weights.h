#pragma once
// romav2.0.1.pt in, named device tensors and the network's dimensions out.
// Every dimension is read off a tensor shape and the loader refuses a file
// whose shapes do not close.
//
// Four things happen at load rather than per pass: the DINOv3 K-bias mask is
// multiplied into the qkv bias, the backbone's q/k rows are permuted so its
// rotate-half RoPE becomes nn::rope's adjacent-pair form, every VGG BatchNorm
// is folded into the conv before it, and both transposed convs are repacked
// to the [Cout*k*k, Cin] matrix their nn:: op multiplies by.

#include "roma/Common.h"
#include "nn/Tensor.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace roma {

struct BackboneHparams {
    int64_t width = 0, mlp = 0;
    int     blocks = 0;       // stored; the forward pass stops after the last tap
    int     heads = 0;
    int     patch = 0;
    int     prefix = 0;       // the class token plus the storage tokens
    int     taps[2] = {11, 17};
};

struct MatcherHparams {
    int64_t in = 0, width = 0, mlp = 0, out = 0;
    int     blocks = 0, heads = 0;
    float   temp = 0.0f, scale = 0.0f;
    int64_t pos_pairs = 0;    // rows of omega; the embedding is 2x this wide
    std::vector<int64_t> dpt_channels;   // the four projections, fine first
    int64_t dpt_features = 0;
};

struct VggConv {
    std::string name;   // "refiner_features.layers.<i>"
    int64_t     cin = 0, cout = 0;
    bool        tap_after = false;   // a MaxPool follows: this output is a tap
};

class Weights {
public:
    Weights() = default;
    ~Weights();
    Weights(const Weights&) = delete;
    Weights& operator=(const Weights&) = delete;

    void load(const std::string& path);
    bool loaded() const { return loaded_; }
    const std::string& path() const { return path_; }
    uint64_t deviceBytes() const { return device_bytes_; }
    uint64_t inexactF16() const { return f16_inexact_; }
    uint64_t totalF16() const { return f16_total_; }   // values sent as f16

    const BackboneHparams& backbone() const { return bb_; }
    const MatcherHparams&  matcher() const { return mt_; }
    const std::vector<VggConv>& vgg() const { return vgg_; }

    nn::Tensor get(const std::string& name) const;
    bool has(const std::string& name) const { return tensors_.count(name) != 0; }

    // RoPE periods, 16 each, as the checkpoint stores them (bf16 values).
    const std::vector<float>& backbonePeriods() const { return bb_periods_; }
    const std::vector<float>& matcherPeriods() const { return mt_periods_; }
    const std::vector<float>& omega() const { return omega_; }   // [pos_pairs, 2]

private:
    std::unordered_map<std::string, nn::Tensor> tensors_;
    BackboneHparams bb_;
    MatcherHparams  mt_;
    std::vector<VggConv> vgg_;
    std::vector<float> bb_periods_, mt_periods_, omega_;
    std::vector<nn::DevicePtr> blobs_;
    std::string path_;
    uint64_t device_bytes_ = 0;
    uint64_t f16_inexact_ = 0, f16_total_ = 0;
    bool loaded_ = false;
};

// The backbone's bf16 matrices go to the device as f16 only where tensor cores
// will use them (exact but for the 4.6e-4 below f16's normal range); the
// matcher's stay fp32. SS_ROMA_F32_WEIGHTS=1: all f32.
bool f16_weights();

}  // namespace roma
