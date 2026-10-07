// VGG19-BN features[:27], BatchNorm folded at load: 3x3 conv + ReLU, a
// MaxPool after every tap. The MaxPool that ends the truncation feeds nothing
// and is not run.

#include "roma/model/Model.h"

#include "roma/model/Dump.h"
#include "nn/Ops.h"

namespace roma {
namespace {

constexpr uint64_t kSlack = 33ull << 20;   // the conv column chunk (nn/OpConv.cpp)

// Two ping-pong maps, each as large as the widest non-tap map; the taps are
// the caller's.
int64_t widest(const Weights& w, int64_t H, int64_t W) {
    int64_t best = 0, h = H, wd = W;
    for (const VggConv& c : w.vgg()) {
        best = std::max(best, h * wd * c.cout);
        if (c.tap_after) {
            h /= 2;
            wd /= 2;
        }
    }
    return best;
}

}  // namespace

uint64_t FineFeatures::planBytes(const Weights& w, int64_t H, int64_t W) {
    return (uint64_t)widest(w, H, W) * 2 * 4 + kSlack;
}

void FineFeatures::run(const Weights& w, vk::Arena& arena, const nn::Tensor& image,
                       const nn::Tensor taps[3], int64_t H, int64_t W) {
    vk::ArenaScope scope(arena);
    const int64_t cap = widest(w, H, W);
    const nn::DevicePtr buf[2] = {nn::arena_tensor(arena, nn::DType::F32, cap).ptr,
                                  nn::arena_tensor(arena, nn::DType::F32, cap).ptr};
    int next = 0;
    nn::Tensor x = image;
    int64_t h = H, wd = W;
    int tap = 0;
    const std::vector<VggConv>& convs = w.vgg();
    for (size_t i = 0; i < convs.size(); ++i) {
        const VggConv& c = convs[i];
        const nn::Tensor out = c.tap_after
                                   ? taps[tap].view(h, wd, c.cout)
                                   : nn::Tensor(buf[next ^= 1], nn::DType::F32, h, wd, c.cout);
        nn::ConvOpts co;
        co.pad_y = co.pad_x = 1;
        co.act = nn::Act::Relu;
        co.bias = w.get(c.name + ".bias");
        nn::conv2d(arena, out, x, w.get(c.name + ".weight"), 3, 3, co);
        x = out;
        if (!c.tap_after) continue;
        ++tap;
        if (i + 1 == convs.size()) break;
        nn::Tensor pooled(buf[next ^= 1], nn::DType::F32, h / 2, wd / 2, c.cout);
        nn::maxpool2x2(pooled, x);
        x = pooled;
        h /= 2;
        wd /= 2;
    }
}

}  // namespace roma
