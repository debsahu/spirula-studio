// RoMa v2's ConvRefiner (refiner.py, MIT): project both VGG maps, sample B's
// at the previous warp, embed the displacement from the identity grid, add a
// local correlation around the warp, run a depthwise-conv trunk, and add the
// heads' deltas. The local correlation reads B's map itself, not the warped
// one: upstream fix #47, which the Lichtfeld plugin's vendored copy predates.

#include "roma/model/Model.h"

#include "core/Env.h"
#include "nn/Ops.h"
#include "nn/vk/Stream.h"

#include <cmath>
#include <cstring>
#include <string>

namespace roma {
namespace {

using nn::Act;
using nn::DType;
using nn::LinearOpts;
using nn::Tensor;

constexpr uint64_t kSlack = 1ull << 20;   // 256-byte rounding of a dozen allocations

void linear_bias(const Weights& w, const Tensor& out, const Tensor& x, const std::string& n) {
    LinearOpts lo;
    lo.bias = w.get(n + ".bias");
    nn::linear(out, x, w.get(n + ".weight"), lo);
}

Tensor upload(vk::Arena& arena, const std::vector<float>& v, int64_t rows, int64_t cols) {
    Tensor t = nn::arena_tensor(arena, DType::F32, rows, cols);
    nn::tensor_from_host(t, v.data(), (int64_t)v.size());
    return t;
}

// [K, 2] (x, y): x = linspace(-2r/w, 2r/w) varies fastest, as local_correlation.py
// stacks meshgrid(ij)'s outputs.
std::vector<float> window_offsets(int r, int64_t h, int64_t wd) {
    const std::vector<float> ys = torch_linspace(-2.0 * r / (double)h, 2.0 * r / (double)h, 2 * r + 1);
    const std::vector<float> xs = torch_linspace(-2.0 * r / (double)wd, 2.0 * r / (double)wd, 2 * r + 1);
    std::vector<float> off;
    for (float y : ys)
        for (float x : xs) {
            off.push_back(x);
            off.push_back(y);
        }
    return off;
}

std::vector<float> grid_table(int64_t h, int64_t wd) {
    const std::vector<float> gx = centred_grid(wd), gy = centred_grid(h);
    std::vector<float> g((size_t)(2 * h * wd));
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < wd; ++x) {
            g[(size_t)(2 * (y * wd + x))] = gx[(size_t)x];
            g[(size_t)(2 * (y * wd + x) + 1)] = gy[(size_t)y];
        }
    return g;
}

}  // namespace

bool local_corr_fused() {
    static const bool fused = [] {
        const char* v = spirula::env("ROMA_LOCAL_CORR");
        return !(v && std::strcmp(v, "unfused") == 0);
    }();
    return fused;
}

uint64_t local_correlation_plan(int64_t n, int64_t C, int radius, bool fused) {
    const int64_t K = (int64_t)(2 * radius + 1) * (2 * radius + 1);
    const int64_t floats = 2 * K + (fused ? 0 : n * K * (2 + C));
    return (uint64_t)floats * 4 + 4 * 256;
}

void local_correlation(vk::Arena& arena, const Tensor& out, const Tensor& fa, const Tensor& fb,
                       const Tensor& warp, int radius, int64_t h, int64_t wd, bool fused) {
    const int64_t n = h * wd, C = fa.cols(), K = (int64_t)(2 * radius + 1) * (2 * radius + 1);
    NN_CHECK(radius >= 0 && out.numel() == n * K && fa.rows() == n && fb.numel() == n * C &&
                 warp.numel() == 2 * n,
             "local_correlation: operands do not match a %lldx%lld map, C %lld, r %d",
             (long long)h, (long long)wd, (long long)C, radius);
    vk::ArenaScope scope(arena);
    const Tensor off = upload(arena, window_offsets(radius, h, wd), K, 2);
    const float sqrt_c = (float)std::sqrt((double)C);
    if (fused) {
        struct {
            uint64_t out, fa, fb, warp, off;
            uint32_t h, w, C, K;
            float sqrt_c;
            uint32_t groups_per_row;
        } p{out.ptr, fa.ptr, fb.ptr, warp.ptr, off.ptr, (uint32_t)h, (uint32_t)wd,
            (uint32_t)C, (uint32_t)K, sqrt_c, 0};
        vk::Stream::get().dispatchFlat("roma_local_corr.local_corr_fused", {}, n * K, 256, &p,
                                       sizeof(p), &p.groups_per_row,
                                       (double)n * K * C * 12);
        return;
    }
    // The samples are one [n*K, C] f32 buffer: past 4 GiB its offsets overflow.
    NN_CHECK((uint64_t)n * K * C * 4 < (4ull << 30),
             "local_correlation: the unfused window of %lld x %lld x %lld exceeds 4 GiB; "
             "use the fused path", (long long)n, (long long)K, (long long)C);
    Tensor pos = nn::arena_tensor(arena, DType::F32, n * K, 2);
    {
        struct {
            uint64_t out, warp, off;
            uint32_t n, K, groups_per_row;
        } p{pos.ptr, warp.ptr, off.ptr, (uint32_t)n, (uint32_t)K, 0};
        vk::Stream::get().dispatchFlat("roma_local_corr.local_corr_positions", {}, n * K, 256,
                                       &p, sizeof(p), &p.groups_per_row);
    }
    Tensor samples = nn::arena_tensor(arena, DType::F32, n * K, C);
    nn::grid_sample_points(samples, fb.view(h, wd, C), pos, false);
    struct {
        uint64_t out, fa, s;
        uint32_t n, K, C;
        float sqrt_c;
        uint32_t groups_per_row;
    } p{out.ptr, fa.ptr, samples.ptr, (uint32_t)n, (uint32_t)K, (uint32_t)C, sqrt_c, 0};
    vk::Stream::get().dispatchFlat("roma_local_corr.local_corr_dot", {}, n * K, 256, &p,
                                   sizeof(p), &p.groups_per_row, (double)n * K * C * 2);
}

uint64_t Refiner::planBytes(const RefinerHparams& r, int64_t h, int64_t wd) {
    const int64_t n = h * wd, P = r.proj, Hd = r.hidden, K = r.window();
    const int64_t live = n * (2 * P + Hd);
    // fba, grid, disp, emb, corr; or the trunk's scratch and both heads.
    const int64_t gather = n * (P + 2 + 2 + r.demb + K);
    const int64_t trunk = n * (Hd + 2 + 4);
    uint64_t extra = (uint64_t)std::max(gather, trunk) * 4;
    if (K) extra = std::max(extra, (uint64_t)gather * 4 +
                                       local_correlation_plan(n, P, r.radius, local_corr_fused()));
    return (uint64_t)live * 4 + extra + kSlack;
}

void Refiner::run(const Weights& w, vk::Arena& arena, const RefinerHparams& r,
                  const Tensor& feat_a, const Tensor& feat_b, const Tensor& warp,
                  const Tensor& conf, int prev_c, float sx, float sy, int64_t h, int64_t wd,
                  const Tensor& warp_out, const Tensor& conf_out) {
    const int64_t n = h * wd, P = r.proj, Hd = r.hidden, D = r.demb, K = r.window();
    NN_CHECK(feat_a.numel() == n * r.feat && feat_b.numel() == n * r.feat &&
                 warp.numel() >= 2 * n && conf.numel() >= prev_c * n &&
                 (prev_c == 1 || prev_c == 4),
             "roma refiner %d: operands do not match a %lldx%lld map", r.stride,
             (long long)wd, (long long)h);
    const std::string pre = "refiners." + std::to_string(r.stride) + ".";
    const Tensor wv = warp.view(n, 2);

    vk::ArenaScope scope(arena);
    Tensor fa = nn::arena_tensor(arena, DType::F32, n, P);
    Tensor fb = nn::arena_tensor(arena, DType::F32, n, P);
    linear_bias(w, fa, feat_a.view(n, r.feat), pre + "proj");
    linear_bias(w, fb, feat_b.view(n, r.feat), pre + "proj");
    Tensor d = nn::arena_tensor(arena, DType::F32, n, Hd);
    {
        vk::ArenaScope s(arena);
        Tensor fba = nn::arena_tensor(arena, DType::F32, n, P);
        nn::grid_sample_points(fba, fb.view(h, wd, P), wv, false);
        const Tensor grid = upload(arena, grid_table(h, wd), n, 2);
        Tensor disp = nn::arena_tensor(arena, DType::F32, n, 2);
        struct {
            uint64_t out, warp, grid;
            float sx, sy;
            uint32_t n, groups_per_row;
        } p{disp.ptr, wv.ptr, grid.ptr, sx, sy, (uint32_t)n, 0};
        vk::Stream::get().dispatchFlat("roma.refine_disp", {1u}, 2 * n, 256, &p, sizeof(p),
                                       &p.groups_per_row);
        Tensor emb = nn::arena_tensor(arena, DType::F32, n, D);
        linear_bias(w, emb, disp, pre + "disp_emb");
        // d = [f_A, f_BA, emb, corr] along the channels.
        nn::strided_copy(d, fa, n, P, P, Hd);
        nn::strided_copy(d.offsetElems(P), fba, n, P, P, Hd);
        nn::strided_copy(d.offsetElems(2 * P), emb, n, D, D, Hd);
        if (K) {
            NN_CHECK(2 * P + D + K == Hd, "roma refiner %d: %lld + %lld + %lld != %lld",
                     r.stride, (long long)(2 * P), (long long)D, (long long)K, (long long)Hd);
            Tensor corr = nn::arena_tensor(arena, DType::F32, n, K);
            local_correlation(arena, corr, fa, fb, wv, r.radius, h, wd, local_corr_fused());
            nn::strided_copy(d.offsetElems(2 * P + D), corr, n, K, K, Hd);
        }
    }
    {
        vk::ArenaScope s(arena);
        Tensor t = nn::arena_tensor(arena, DType::F32, h, wd, Hd);
        nn::ConvOpts co;
        co.pad_y = co.pad_x = r.kernel / 2;
        co.act = Act::Relu;
        for (int b = -1; b < r.blocks; ++b) {
            const std::string q =
                pre + (b < 0 ? std::string("block1.") : "hidden_blocks." + std::to_string(b) + ".");
            co.bias = w.get(q + "conv_depthwise.bias");
            nn::conv2d_depthwise(t, d.view(h, wd, Hd), w.get(q + "conv_depthwise.weight"),
                                 r.kernel, r.kernel, co);
            linear_bias(w, d, t.view(n, Hd), q + "conv_pointwise");
        }
        Tensor dw = nn::arena_tensor(arena, DType::F32, n, 2);
        Tensor dc = nn::arena_tensor(arena, DType::F32, n, 4);
        linear_bias(w, dw, d, pre + "warp_head");
        linear_bias(w, dc, d, pre + "confidence_head");
        struct {
            uint64_t warp_out, conf_out, warp, conf, dwarp, dconf;
            float den_x, den_y;
            uint32_t n, prev_c, groups_per_row;
        } p{warp_out.ptr, conf_out.ptr, wv.ptr, conf.ptr, dw.ptr, dc.ptr,
            4.0f * (float)wd, 4.0f * (float)h, (uint32_t)n, (uint32_t)prev_c, 0};
        vk::Stream::get().dispatchFlat("roma.refine_update", {1u}, n, 256, &p, sizeof(p),
                                       &p.groups_per_row);
    }
}

}  // namespace roma
