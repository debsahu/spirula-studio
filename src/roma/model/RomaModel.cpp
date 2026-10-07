// The public Model: two images in, the coarse warp out, with the arena planned
// before anything runs.

#include "roma/Roma.h"

#include "roma/Common.h"
#include "roma/model/Dump.h"
#include "roma/model/Model.h"
#include "nn/Ops.h"
#include "nn/vk/EmbeddedSpirv.h"
#include "nn/vk/Stream.h"

#include <algorithm>
#include <cmath>

// ss_roma is a static archive: an object nothing references is not linked.
NN_DECLARE_EMBEDDED_MODULES(roma)

namespace roma {
namespace {

using nn::DType;
using nn::Tensor;

const float kMean[3] = {0.485f, 0.456f, 0.406f};
const float kStd[3] = {0.229f, 0.224f, 0.225f};

// romav2.normalizers.imagenet: (x - mean) / std, two fp32 ops as torch does.
std::vector<float> imagenet(const float* rgb, int64_t count) {
    std::vector<float> v((size_t)count);
    for (int64_t i = 0; i < count; ++i) v[(size_t)i] = (rgb[i] - kMean[i % 3]) / kStd[i % 3];
    return v;
}

// torch's antialiased cubic, a = -0.5 (UpSampleKernel.cpp's aa_filter).
double aa_cubic(double x) {
    const double a = -0.5;
    x = std::fabs(x);
    if (x < 1.0) return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;
    if (x < 2.0) return (((x - 5.0) * x + 8.0) * x - 4.0) * a;
    return 0.0;
}

// One separable pass; `other` walks the axis not being resampled.
void resample_axis(const float* src, float* dst, int64_t n_other, int64_t n_in, int64_t n_out,
                   int64_t src_other, int64_t dst_other, int64_t src_step, int64_t dst_step) {
    const double scale = (double)n_in / (double)n_out;
    const double support = scale >= 1.0 ? 2.0 * scale : 2.0;
    const double inv = scale >= 1.0 ? 1.0 / scale : 1.0;
    nn::parallel_for(n_out, [&](int64_t o0, int64_t o1) {
        std::vector<double> wb;
        for (int64_t o = o0; o < o1; ++o) {
            const double centre = scale * ((double)o + 0.5);
            const int64_t lo = std::max<int64_t>((int64_t)(centre - support + 0.5), 0);
            const int64_t hi = std::min<int64_t>((int64_t)(centre + support + 0.5), n_in);
            wb.assign((size_t)std::max<int64_t>(hi - lo, 1), 0.0);
            double total = 0;
            for (int64_t i = lo; i < hi; ++i) {
                wb[(size_t)(i - lo)] = aa_cubic(((double)i - centre + 0.5) * inv);
                total += wb[(size_t)(i - lo)];
            }
            if (total == 0.0) total = 1.0;
            for (int64_t k = 0; k < n_other; ++k) {
                const float* sp = src + k * src_other;
                float* d = dst + k * dst_other + o * dst_step;
                for (int c = 0; c < 3; ++c) {
                    double acc = 0;
                    for (int64_t i = lo; i < hi; ++i)
                        acc += wb[(size_t)(i - lo)] * sp[i * src_step + c];
                    d[c] = (float)(acc / total);
                }
            }
        }
    });
}

}  // namespace

std::vector<float> resize_rgb(const uint8_t* rgb, int w, int h, int ow, int oh) {
    std::vector<float> a((size_t)w * h * 3);
    for (size_t i = 0; i < a.size(); ++i) a[i] = rgb[i] / 255.0f;
    if (ow != w) {
        std::vector<float> b((size_t)h * ow * 3);
        resample_axis(a.data(), b.data(), h, w, ow, (int64_t)w * 3, (int64_t)ow * 3, 3, 3);
        a.swap(b);
    }
    if (oh != h) {
        std::vector<float> b((size_t)oh * ow * 3);
        resample_axis(a.data(), b.data(), ow, h, oh, 3, 3, (int64_t)ow * 3, (int64_t)ow * 3);
        a.swap(b);
    }
    return a;
}

struct Model::Impl {
    Weights       w;
    Backbone      backbone;
    CoarseMatcher matcher;
    vk::Arena     arena{"roma"};
    uint64_t      planned = 0;
};

Model::Model() : impl_(new Impl) {}
Model::~Model() { delete impl_; }
bool Model::loaded() const { return impl_->w.loaded(); }
uint64_t Model::plannedBytes() const { return impl_->planned; }
uint64_t Model::peakBytes() const { return impl_->arena.highWater(); }
uint64_t Model::weightBytes() const { return impl_->w.deviceBytes(); }

void Model::load(const std::string& checkpoint) {
    NN_ENSURE_EMBEDDED_MODULES(roma);
    impl_->w.load(checkpoint);
}

CoarseMatch Model::coarse(const float* a, const float* b, int H, int W) {
    NN_CHECK(loaded(), "roma::Model::coarse before load()");
    Impl& im = *impl_;
    const Weights& w = im.w;
    const int64_t P = w.backbone().patch, D = w.backbone().width;
    NN_CHECK(H > 0 && W > 0 && H % P == 0 && W % P == 0,
             "roma: %dx%d is not a multiple of the %lld-pixel patch", W, H, (long long)P);
    const int64_t h = H / P, wd = W / P, n = h * wd;

    // Inputs, taps and output, then the widest of the passes that run on them.
    const uint64_t fixed = (uint64_t)(2 * H * W * 3 + 4 * n * D + 16 * n * 3) * 4 + 16 * 256;
    const bool dump = dump_enabled();
    uint64_t vgg = 0;
    if (dump) {
        int64_t taps = 0, hh = H, ww = W;
        for (const VggConv& c : w.vgg())
            if (c.tap_after) {
                taps += hh * ww * c.cout;
                hh /= 2;
                ww /= 2;
            }
        vgg = (uint64_t)taps * 4 + FineFeatures::planBytes(w, H, W);
    }
    const uint64_t plan =
        fixed + std::max({Backbone::planBytes(w, h, wd), CoarseMatcher::planBytes(w, h, wd), vgg});
    im.planned = std::max(im.planned, plan);
    im.arena.reserve(plan);

    CoarseMatch out;
    out.h = (int)(4 * h);
    out.w = (int)(4 * wd);
    out.data.resize((size_t)out.h * out.w * 3);

    vk::ArenaScope root(im.arena);
    Tensor img[2], taps[2][2];
    const float* src[2] = {a, b};
    for (int i = 0; i < 2; ++i) {
        dump_host(i ? "input_B" : "input_A", src[i], {H, W, 3});
        img[i] = nn::arena_tensor(im.arena, DType::F32, H, W, 3);
        const std::vector<float> norm = imagenet(src[i], (int64_t)H * W * 3);
        nn::tensor_from_host(img[i], norm.data(), (int64_t)norm.size());
        for (int t = 0; t < 2; ++t) taps[i][t] = nn::arena_tensor(im.arena, DType::F32, n, D);
    }
    Tensor res = nn::arena_tensor(im.arena, DType::F32, 4 * h, 4 * wd, 3);

    for (int i = 0; i < 2; ++i) {
        im.backbone.run(w, im.arena, img[i], taps[i][0], taps[i][1], h, wd);
        const char* tag = i ? "B" : "A";
        dump_tensor((std::string("dino_tap11_") + tag).c_str(), taps[i][0], {h, wd, D});
        dump_tensor((std::string("dino_tap17_") + tag).c_str(), taps[i][1], {h, wd, D});
    }
    if (dump) {
        // Parity probe only: the refiners (WS-3) are what consume these maps.
        for (int i = 0; i < 2; ++i) {
            vk::ArenaScope s(im.arena);
            Tensor ft[3];
            int64_t hh = H, ww = W, k = 0;
            for (const VggConv& c : w.vgg())
                if (c.tap_after) {
                    ft[k++] = nn::arena_tensor(im.arena, DType::F32, hh, ww, c.cout);
                    hh /= 2;
                    ww /= 2;
                }
            FineFeatures::run(w, im.arena, img[i], ft, H, W);
            const int scale[3] = {1, 2, 4};
            for (int t = 0; t < 3; ++t) {
                const std::string name =
                    "vgg_s" + std::to_string(scale[t]) + "_" + (i ? "B" : "A");
                dump_tensor(name.c_str(), ft[t], {ft[t].shape[0], ft[t].shape[1], ft[t].shape[2]});
            }
        }
    }
    im.matcher.run(w, im.arena, taps[0], taps[1], h, wd, res);
    nn::tensor_to_host(res, out.data.data(), (int64_t)out.data.size());
    return out;
}

}  // namespace roma
