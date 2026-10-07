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
#include <cstring>

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

// FNV-1a over the floats' bits, folded into `h`: rejects a different image
// cheaply; the cache confirms a match with an exact compare.
uint64_t content_hash(const float* v, size_t n, uint64_t h) {
    h ^= 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        uint32_t u;
        std::memcpy(&u, v + i, 4);
        h = (h ^ u) * 1099511628211ull;
    }
    return h;
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

// One image's matcher inputs on the device: the normalized image at each
// scale, the backbone taps at lr, and the VGG maps (strides 1, 2, 4) at each.
struct ImageFeatures {
    Tensor img[2], taps[2], vgg[2][3];
};

int64_t vgg_floats(const Weights& w, int64_t H, int64_t W) {
    int64_t f = 0;
    for (const VggConv& c : w.vgg())
        if (c.tap_after) {
            f += H * W * c.cout;
            H /= 2;
            W /= 2;
        }
    return f;
}

struct Model::Impl {
    Weights       w;
    Backbone      backbone;
    CoarseMatcher matcher;
    vk::Arena     arena{"roma"};
    vk::Arena     cache{"roma-ref"};   // A's features, kept across pairs
    // A's identity: its sizes and its exact input floats (lr, then hr), the
    // hash only to reject a different image without the compare.
    MatchSpec     cache_spec;
    uint64_t      cache_hash = 0;
    std::vector<float> cache_bytes;
    bool          cache_valid = false;
    uint64_t      hits = 0, misses = 0;
    ImageFeatures cached;
    uint64_t      planned = 0;
    StageLog      log{arena};

    // `img_out[s]` gets the normalized image of scale s; the rest from it.
    void features(ImageFeatures& f, vk::Arena& dst, const float* const rgb[2],
                  const int dims[2][2], int scales);
};

void Model::Impl::features(ImageFeatures& f, vk::Arena& dst, const float* const rgb[2],
                           const int dims[2][2], int scales) {
    const int64_t P = w.backbone().patch, D = w.backbone().width;
    for (int s = 0; s < scales; ++s) {
        const int H = dims[s][0], W = dims[s][1];
        f.img[s] = nn::arena_tensor(dst, DType::F32, H, W, 3);
        const std::vector<float> norm = imagenet(rgb[s], (int64_t)H * W * 3);
        nn::tensor_from_host(f.img[s], norm.data(), (int64_t)norm.size());
        int64_t hh = H, ww = W, k = 0;
        for (const VggConv& c : w.vgg())
            if (c.tap_after) {
                f.vgg[s][k++] = nn::arena_tensor(dst, DType::F32, hh, ww, c.cout);
                hh /= 2;
                ww /= 2;
            }
    }
    const int64_t h = dims[0][0] / P, wd = dims[0][1] / P;
    for (int t = 0; t < 2; ++t) f.taps[t] = nn::arena_tensor(dst, DType::F32, h * wd, D);
    log.run("backbone", Backbone::planBytes(w, h, wd),
            [&] { backbone.run(w, arena, f.img[0], f.taps[0], f.taps[1], h, wd); });
    for (int s = 0; s < scales; ++s)
        log.run("fine features", FineFeatures::planBytes(w, dims[s][0], dims[s][1]), [&] {
            FineFeatures::run(w, arena, f.img[s], f.vgg[s], dims[s][0], dims[s][1]);
        });
}

Model::Model() : impl_(new Impl) {}
Model::~Model() { delete impl_; }
bool Model::loaded() const { return impl_->w.loaded(); }
uint64_t Model::plannedBytes() const { return impl_->planned; }
uint64_t Model::peakBytes() const { return impl_->log.overall(); }
const std::vector<Model::Stage>& Model::stages() const { return impl_->log.stages(); }
uint64_t Model::weightBytes() const { return impl_->w.deviceBytes(); }
uint64_t Model::cacheBytes() const { return impl_->cache.highWater(); }
uint64_t Model::cacheHits() const { return impl_->hits; }
uint64_t Model::cacheMisses() const { return impl_->misses; }

float DenseMatch::overlap(size_t i) const {
    return 1.0f / (1.0f + std::exp(-confidence[i * 4]));
}

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
        fixed + std::max({Backbone::planBytes(w, h, wd), CoarseMatcher::plan(w, h, wd).total(), vgg});
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

    im.log.clear();
    for (int i = 0; i < 2; ++i) {
        im.log.run("backbone", Backbone::planBytes(w, h, wd), [&] {
            im.backbone.run(w, im.arena, img[i], taps[i][0], taps[i][1], h, wd);
        });
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
            im.log.run("fine features", FineFeatures::planBytes(w, H, W),
                     [&] { FineFeatures::run(w, im.arena, img[i], ft, H, W); });
            const int scale[3] = {1, 2, 4};
            for (int t = 0; t < 3; ++t) {
                const std::string name =
                    "vgg_s" + std::to_string(scale[t]) + "_" + (i ? "B" : "A");
                dump_tensor(name.c_str(), ft[t], {ft[t].shape[0], ft[t].shape[1], ft[t].shape[2]});
            }
        }
    }
    im.matcher.run(w, im.arena, taps[0], taps[1], h, wd, res, im.log);
    nn::tensor_to_host(res, out.data.data(), (int64_t)out.data.size());
    return out;
}

MatchResult Model::match(const float* a_lr, const float* b_lr, const float* a_hr,
                         const float* b_hr, const MatchSpec& spec) {
    NN_CHECK(loaded(), "roma::Model::match before load()");
    Impl& im = *impl_;
    const Weights& w = im.w;
    const int64_t P = w.backbone().patch, D = w.backbone().width;
    const int scales = spec.hr_h > 0 ? 2 : 1;
    const int dims[2][2] = {{spec.lr_h, spec.lr_w}, {spec.hr_h, spec.hr_w}};
    for (int s = 0; s < scales; ++s)
        NN_CHECK(dims[s][0] > 0 && dims[s][1] > 0 && dims[s][0] % P == 0 && dims[s][1] % P == 0,
                 "roma: %dx%d is not a multiple of the %lld-pixel patch", dims[s][1],
                 dims[s][0], (long long)P);
    NN_CHECK(scales == 1 || (a_hr && b_hr), "roma: an hr scale needs both hr images");
    const int64_t h = spec.lr_h / P, wd = spec.lr_w / P, n = h * wd;
    const int dirs = spec.bidirectional ? 2 : 1;
    const std::vector<RefinerHparams>& refs = w.refiners();
    NN_CHECK(refs.size() == 3, "roma: %zu refiners, the stage table has 3", refs.size());

    // ---- the plan: A's cache, then B's inputs and the widest phase over them ----
    int64_t img_f = 0, vgg_f[2] = {0, 0};
    for (int s = 0; s < scales; ++s) {
        img_f += (int64_t)dims[s][0] * dims[s][1] * 3;
        vgg_f[s] = vgg_floats(w, dims[s][0], dims[s][1]);
    }
    const uint64_t one_image = (uint64_t)(img_f + 2 * n * D + vgg_f[0] + vgg_f[1]) * 4 +
                               16 * 256;
    const int64_t full = (int64_t)dims[scales - 1][0] * dims[scales - 1][1];
    // Per direction, a warp and confidence to refine from and one to refine into.
    const uint64_t state = (uint64_t)(dirs * 2 * full * 6) * 4 + 8 * 256;
    uint64_t compute = std::max(Backbone::planBytes(w, h, wd),
                                CoarseMatcher::plan(w, h, wd).total() +
                                    (uint64_t)(dirs * 16 * n * 3) * 4 + 2 * 256);
    for (int s = 0; s < scales; ++s) {
        uint64_t worst = FineFeatures::planBytes(w, dims[s][0], dims[s][1]);
        for (const RefinerHparams& r : refs)
            worst = std::max(worst, Refiner::planBytes(r, dims[s][0] / r.stride,
                                                       dims[s][1] / r.stride));
        compute = std::max(compute, (uint64_t)vgg_f[s] * 4 + 3 * 256 + worst);
    }
    const uint64_t b_root = (uint64_t)(img_f + 2 * n * D) * 4 + 8 * 256;
    const uint64_t plan = b_root + state + compute;
    im.planned = std::max(im.planned, plan);
    im.arena.reserve(plan);

    const size_t n_lr = (size_t)spec.lr_h * spec.lr_w * 3;
    const size_t n_hr = scales == 2 ? (size_t)spec.hr_h * spec.hr_w * 3 : 0;
    const uint64_t hash = content_hash(a_lr, n_lr, content_hash(a_hr, n_hr, 0));
    const bool same = im.cache_valid && hash == im.cache_hash &&
                      spec.lr_h == im.cache_spec.lr_h && spec.lr_w == im.cache_spec.lr_w &&
                      spec.hr_h == im.cache_spec.hr_h && spec.hr_w == im.cache_spec.hr_w &&
                      im.cache_bytes.size() == n_lr + n_hr &&
                      std::memcmp(im.cache_bytes.data(), a_lr, n_lr * 4) == 0 &&
                      (n_hr == 0 ||
                       std::memcmp(im.cache_bytes.data() + n_lr, a_hr, n_hr * 4) == 0);
    im.log.clear();
    vk::ArenaScope root(im.arena);
    ++(same ? im.hits : im.misses);
    if (!same) {
        im.cache_valid = false;
        im.cache.reset();
        im.cache.resetHighWater();
        im.cache.reserve(one_image);
        const float* const rgb_a[2] = {a_lr, a_hr};
        im.features(im.cached, im.cache, rgb_a, dims, scales);
        dump_tensor("dino_tap11_A", im.cached.taps[0], {h, wd, D});
        dump_tensor("dino_tap17_A", im.cached.taps[1], {h, wd, D});
        im.cache_bytes.assign(a_lr, a_lr + n_lr);
        if (n_hr) im.cache_bytes.insert(im.cache_bytes.end(), a_hr, a_hr + n_hr);
        im.cache_hash = hash;
        im.cache_spec = spec;
        im.cache_valid = true;
    }
    const ImageFeatures& A = im.cached;
    ImageFeatures B;
    {
        // B's VGG maps are needed one scale at a time, so they are made in the
        // phase that reads them; here only the image and the taps.
        for (int s = 0; s < scales; ++s) {
            B.img[s] = nn::arena_tensor(im.arena, DType::F32, dims[s][0], dims[s][1], 3);
            const std::vector<float> norm =
                imagenet(s ? b_hr : b_lr, (int64_t)dims[s][0] * dims[s][1] * 3);
            nn::tensor_from_host(B.img[s], norm.data(), (int64_t)norm.size());
        }
        for (int t = 0; t < 2; ++t) B.taps[t] = nn::arena_tensor(im.arena, DType::F32, n, D);
        im.log.run("backbone", Backbone::planBytes(w, h, wd),
                   [&] { im.backbone.run(w, im.arena, B.img[0], B.taps[0], B.taps[1], h, wd); });
        dump_tensor("dino_tap11_B", B.taps[0], {h, wd, D});
        dump_tensor("dino_tap17_B", B.taps[1], {h, wd, D});
    }
    dump_host("input_A", a_lr, {spec.lr_h, spec.lr_w, 3});
    dump_host("input_B", b_lr, {spec.lr_h, spec.lr_w, 3});
    if (scales == 2) {
        dump_host("input_A_hr", a_hr, {spec.hr_h, spec.hr_w, 3});
        dump_host("input_B_hr", b_hr, {spec.hr_h, spec.hr_w, 3});
    }

    // ---- state: [dir][0] is refined from, [dir][1] refined into ----
    Tensor warp[2][2], conf[2][2];
    for (int d = 0; d < dirs; ++d)
        for (int k = 0; k < 2; ++k) {
            warp[d][k] = nn::arena_tensor(im.arena, DType::F32, full, 2);
            conf[d][k] = nn::arena_tensor(im.arena, DType::F32, full, 4);
        }
    int64_t ch = 4 * h, cw = 4 * wd;
    int prev_c = 1;
    {
        vk::ArenaScope s(im.arena);
        Tensor out[2];
        for (int d = 0; d < dirs; ++d) out[d] = nn::arena_tensor(im.arena, DType::F32, ch, cw, 3);
        im.matcher.run(w, im.arena, A.taps, B.taps, h, wd, out[0], im.log,
                       dirs == 2 ? out[1] : Tensor{});
        for (int d = 0; d < dirs; ++d) {
            nn::strided_copy(warp[d][1], out[d], ch * cw, 2, 3, 2);
            nn::strided_copy(conf[d][1], out[d].offsetElems(2), ch * cw, 1, 3, 1);
        }
    }

    static const char* kStage[2][3] = {{"refiner lr s4", "refiner lr s2", "refiner lr s1"},
                                       {"refiner hr s4", "refiner hr s2", "refiner hr s1"}};
    for (int s = 0; s < scales; ++s) {
        const int H = dims[s][0], W = dims[s][1];
        vk::ArenaScope phase(im.arena);
        int64_t hh = H, ww = W, k = 0;
        for (const VggConv& c : w.vgg())
            if (c.tap_after) {
                B.vgg[s][k++] = nn::arena_tensor(im.arena, DType::F32, hh, ww, c.cout);
                hh /= 2;
                ww /= 2;
            }
        im.log.run("fine features", FineFeatures::planBytes(w, H, W),
                   [&] { FineFeatures::run(w, im.arena, B.img[s], B.vgg[s], H, W); });
        if (s == 1) {
            // RoMaV2.forward zeroes the precision before the second scale: it is
            // absolute in pixels, and the logit is all that carries over.
            for (int d = 0; d < dirs; ++d) {
                const Tensor c(conf[d][1].ptr, DType::F32, ch * cw, 4);
                const Tensor tmp(conf[d][0].ptr, DType::F32, ch * cw, 4);
                nn::fill(tmp, 0.0f);
                nn::strided_copy(tmp, c, ch * cw, 1, 4, 4);
                nn::copy(c, tmp);
            }
        }
        // AB reads (A, B); BA the same maps swapped.
        const float sx = (float)(W / 512.0), sy = (float)(H / 512.0);
        for (size_t ri = 0; ri < refs.size(); ++ri) {
            const RefinerHparams& r = refs[ri];
            const int64_t rh = H / r.stride, rw = W / r.stride, rn = rh * rw;
            const int tap = r.stride == 1 ? 0 : r.stride == 2 ? 1 : 2;
            for (int d = 0; d < dirs; ++d) {
                // The confidence buffers hold [rows, prev_c] packed at their start.
                const Tensor src_w(warp[d][1].ptr, DType::F32, ch, cw, 2);
                const Tensor src_c(conf[d][1].ptr, DType::F32, ch, cw, prev_c);
                const Tensor mid_w(warp[d][0].ptr, DType::F32, rh, rw, 2);
                const Tensor mid_c(conf[d][0].ptr, DType::F32, rh, rw, prev_c);
                nn::resize_bilinear(mid_w, src_w, false);
                nn::resize_bilinear(mid_c, src_c, false);
                const Tensor& fa = d ? B.vgg[s][tap] : A.vgg[s][tap];
                const Tensor& fb = d ? A.vgg[s][tap] : B.vgg[s][tap];
                im.log.run(kStage[s][ri], Refiner::planBytes(r, rh, rw), [&] {
                    Refiner::run(w, im.arena, r, fa, fb, mid_w, mid_c, prev_c, sx, sy, rh, rw,
                                 Tensor(warp[d][1].ptr, DType::F32, rn, 2),
                                 Tensor(conf[d][1].ptr, DType::F32, rn, 4));
                });
                if (dump_enabled()) {
                    const std::string tag = std::string(s ? "hr" : "lr") + "_s" +
                                            std::to_string(r.stride) + (d ? "_BA" : "_AB");
                    dump_tensor(("refine_warp_" + tag).c_str(),
                                Tensor(warp[d][1].ptr, DType::F32, rn, 2), {rh, rw, 2});
                    dump_tensor(("refine_conf_" + tag).c_str(),
                                Tensor(conf[d][1].ptr, DType::F32, rn, 4), {rh, rw, 4});
                }
            }
            ch = rh;
            cw = rw;
            prev_c = 4;
        }
    }

    MatchResult res;
    for (int d = 0; d < dirs; ++d) {
        DenseMatch& m = d ? res.ba : res.ab;
        m.h = (int)ch;
        m.w = (int)cw;
        m.warp.resize((size_t)(ch * cw * 2));
        m.confidence.resize((size_t)(ch * cw * 4));
        nn::tensor_to_host(Tensor(warp[d][1].ptr, DType::F32, ch * cw, 2), m.warp.data(),
                           (int64_t)m.warp.size());
        nn::tensor_to_host(Tensor(conf[d][1].ptr, DType::F32, ch * cw, 4), m.confidence.data(),
                           (int64_t)m.confidence.size());
    }
    return res;
}

}  // namespace roma
