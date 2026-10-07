// RoMa v2: the RoPE conversions on synthetic data (always), then, given the
// checkpoint, its shapes and a coarse match with the arena held to its plan.
// The gate that matters is parity against upstream PyTorch on the same bytes
// -- tools/roma/compare_torch.py on an SS_ROMA_DUMP of this binary's run.
//
// With no checkpoint the model half SKIPS, so a machine without the 1.1 GB
// file still runs the rest.

#include "roma/Roma.h"
#include "roma/model/Fetch.h"
#include "roma/model/Model.h"
#include "roma/model/RomaMatcher.h"
#include "roma/model/Rope.h"
#include "roma/model/Weights.h"

#include "core/Env.h"
#include "nn/Ops.h"
#include "nn/core/Log.h"
#include "nn/io/Image.h"
#include "nn/vk/Context.h"
#include "nn/vk/Memory.h"
#include "nn/vk/Pipelines.h"
#include "nn/vk/Stream.h"
#include "nn/vk/EmbeddedSpirv.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

NN_DECLARE_EMBEDDED_MODULES(roma)

using namespace roma;
using nn::DType;
using nn::Tensor;

namespace {

int g_failures = 0, g_checks = 0;

void check(bool ok, const char* name, const char* fmt, ...) {
    ++g_checks;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::printf("  %s %s: %s\n", ok ? "ok  " : "FAIL", name, buf);
    if (!ok) ++g_failures;
}

std::vector<float> randn(size_t n, uint32_t seed, float sd = 1.0f) {
    std::mt19937 g(seed);
    std::normal_distribution<float> d(0.0f, sd);
    std::vector<float> v(n);
    for (float& x : v) x = d(g);
    return v;
}

Tensor upload(vk::Arena& a, const std::vector<float>& v, int64_t r, int64_t c) {
    Tensor t = nn::arena_tensor(a, DType::F32, r, c);
    nn::tensor_from_host(t, v.data(), (int64_t)v.size());
    return t;
}

std::vector<float> download(const Tensor& t) {
    std::vector<float> v((size_t)t.numel());
    nn::tensor_to_host(t, v.data(), t.numel());
    return v;
}

// DINOv3's periods for head_dim 64: 100 ** (2 * arange(16) / 32), stored bf16.
std::vector<float> periods16() {
    std::vector<float> p(16);
    for (int k = 0; k < 16; ++k) p[(size_t)k] = to_bf16((float)std::pow(100.0, k / 16.0));
    return p;
}

// ---- The load-time q/k permutation makes nn::rope's adjacent pairing the
// reference's rotate-half. Catches: no permutation, the wrong half, a
// permutation applied to q only.
void test_rope_permutation(vk::Arena& arena) {
    std::printf("\nrope_permutation\n");
    const int heads = 2, hd = 64;
    const int64_t D = heads * hd, K = 48, h = 3, w = 5, n = h * w;
    const std::vector<float> W = randn((size_t)(3 * D * K), 1, 0.2f);
    const std::vector<float> x = randn((size_t)(n * K), 2);
    const std::vector<float> table = backbone_rope(periods16(), h, w);

    // Reference: qkv = x W^T, rotate-half on q and k, per-head scores.
    std::vector<double> ref((size_t)(n * 3 * D));
    for (int64_t t = 0; t < n; ++t)
        for (int64_t o = 0; o < 3 * D; ++o) {
            double s = 0;
            for (int64_t k = 0; k < K; ++k) s += (double)x[(size_t)(t * K + k)] * W[(size_t)(o * K + k)];
            ref[(size_t)(t * 3 * D + o)] = s;
        }
    for (int64_t t = 0; t < n; ++t)
        for (int part = 0; part < 2; ++part)
            for (int hh = 0; hh < heads; ++hh)
                for (int k = 0; k < hd / 2; ++k) {
                    double* r = &ref[(size_t)(t * 3 * D + part * D + hh * hd)];
                    const double c = table[(size_t)((t * hd / 2 + k) * 2)];
                    const double s = table[(size_t)((t * hd / 2 + k) * 2 + 1)];
                    const double a = r[k], b = r[k + hd / 2];
                    r[k] = a * c - b * s;
                    r[k + hd / 2] = b * c + a * s;
                }
    auto scores = [&](auto get) {
        std::vector<double> sc;
        for (int hh = 0; hh < heads; ++hh)
            for (int64_t i = 0; i < n; ++i)
                for (int64_t j = 0; j < n; ++j) {
                    double s = 0;
                    for (int d = 0; d < hd; ++d) s += get(i, hh * hd + d) * get(j, D + hh * hd + d);
                    sc.push_back(s);
                }
        return sc;
    };
    const std::vector<double> want =
        scores([&](int64_t t, int64_t c) { return ref[(size_t)(t * 3 * D + c)]; });

    auto device_scores = [&](const std::vector<float>& weight) {
        vk::ArenaScope s(arena);
        Tensor tx = upload(arena, x, n, K), tw = upload(arena, weight, 3 * D, K);
        Tensor qkv = nn::arena_tensor(arena, DType::F32, n, 3 * D);
        nn::linear(qkv, tx, tw);
        Tensor tf = nn::arena_tensor(arena, DType::F32, n, hd / 2, 2);
        nn::tensor_from_host(tf, table.data(), (int64_t)table.size());
        nn::rope(qkv, tf, heads, hd, n, 1, 3 * D);
        nn::rope(qkv.offsetElems(D), tf, heads, hd, n, 1, 3 * D);
        const std::vector<float> got = download(qkv);
        return scores([&](int64_t t, int64_t c) { return (double)got[(size_t)(t * 3 * D + c)]; });
    };
    auto max_rel = [&](const std::vector<double>& g) {
        double num = 0, den = 0;
        for (size_t i = 0; i < g.size(); ++i) {
            num = std::max(num, std::fabs(g[i] - want[i]));
            den = std::max(den, std::fabs(want[i]));
        }
        return num / den;
    };
    const double e = max_rel(device_scores(permute_qk_rows(W, D, heads, K)));
    const double e0 = max_rel(device_scores(W));
    check(e < 1e-5, "rope_permutation", "permuted q.k vs rotate-half %.2e (bar 1e-5)", e);
    // The fixture must separate the two pairings, or the line above proves nothing.
    check(e0 > 1e-1, "rope_permutation_discriminates",
          "unpermuted q.k vs rotate-half %.2e (must exceed 0.1)", e0);
}

// ---- roma.rope_half_bf16 is the reference's bf16 eager arithmetic exactly.
// Catches: a missing rounding, truncation instead of round-to-even, the wrong
// pair, a batch stride that ignores row_stride.
void test_rope_bf16(vk::Arena& arena) {
    std::printf("\nrope_bf16\n");
    if (!matcher_rope_rounds()) {
        std::printf("  SKIP: SS_ROMA_ROPE_F32 runs this kernel in fp32 by design\n");
        return;
    }
    const int heads = 3, hd = 64, batch = 2;
    const int64_t h = 4, w = 6, n = h * w, stride = 3 * heads * hd;
    const std::vector<float> table = matcher_rope_bf16(periods16(), h, w);
    std::vector<float> x = randn((size_t)(batch * n * stride), 3);
    for (size_t i = 0; i < x.size(); i += 7) x[i] *= 1000.0f;   // span exponents

    std::vector<float> want(x), plain(x);
    for (int b = 0; b < batch; ++b)
        for (int64_t t = 0; t < n; ++t)
            for (int hh = 0; hh < heads; ++hh)
                for (int k = 0; k < hd / 2; ++k) {
                    const size_t lo = (size_t)((b * n + t) * stride + hh * hd + k), hi = lo + hd / 2;
                    const float c = table[(size_t)((t * hd / 2 + k) * 2)];
                    const float s = table[(size_t)((t * hd / 2 + k) * 2 + 1)];
                    const float a = to_bf16(x[lo]), d = to_bf16(x[hi]);
                    want[lo] = to_bf16(to_bf16(a * c) + to_bf16(-d * s));
                    want[hi] = to_bf16(to_bf16(d * c) + to_bf16(a * s));
                    plain[lo] = x[lo] * c - x[hi] * s;
                    plain[hi] = x[hi] * c + x[lo] * s;
                }
    vk::ArenaScope s(arena);
    Tensor tx = upload(arena, x, batch * n, stride);
    Tensor tt = nn::arena_tensor(arena, DType::F32, n, hd / 2, 2);
    nn::tensor_from_host(tt, table.data(), (int64_t)table.size());
    rope_half_bf16(tx, tt, heads, hd, n, batch, stride);
    const std::vector<float> got = download(tx);
    size_t bad = 0, differs = 0, outside = 0;
    for (size_t i = 0; i < got.size(); ++i) {
        bad += got[i] != want[i];
        differs += want[i] != plain[i];
        if ((int64_t)(i % (size_t)stride) >= heads * hd) outside += got[i] != x[i];
    }
    check(bad == 0, "rope_bf16", "%zu of %zu elements differ from the emulation", bad,
          got.size());
    check(differs > got.size() / 4, "rope_bf16_discriminates",
          "%zu elements separate bf16 from fp32 rotation (must exceed a quarter)", differs);
    // Only the first heads*head_dim columns of each row are this call's.
    check(outside == 0, "rope_bf16_extent", "%zu elements past the rotated columns changed",
          outside);
}

// ---- The bf16 table rounds every eager op; the fp32 one does not.
void test_rope_tables() {
    std::printf("\nrope_tables\n");
    const std::vector<float> p = periods16();
    const std::vector<float> f = backbone_rope(p, 40, 40), b = matcher_rope_bf16(p, 40, 40);
    size_t not_bf16 = 0, differ = 0;
    double worst = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        not_bf16 += b[i] != to_bf16(b[i]);
        differ += b[i] != f[i];
    }
    // Pair 0 of token (0, 0): coord -1 + 1/40 on the row axis, period 1.
    const double a = 2 * M_PI * (-1.0 + 1.0 / 40.0);
    worst = std::max(std::fabs(f[0] - std::cos(a)), std::fabs(f[1] - std::sin(a)));
    check(not_bf16 == 0, "rope_table_bf16", "%zu entries are not bf16 values", not_bf16);
    check(differ > b.size() / 2, "rope_table_bf16_rounds", "%zu of %zu differ from fp32",
          differ, b.size());
    check(worst < 1e-6, "rope_table_fp32", "pair 0 vs the closed form: %.2e", worst);
    // Column-axis pair 16 of token (0, 1) sees x = -1 + 3/40.
    const double ax = 2 * M_PI * (-1.0 + 3.0 / 40.0) / p[0];
    const double e = std::fabs(f[(size_t)((1 * 32 + 16) * 2)] - std::cos(ax));
    check(e < 1e-6, "rope_table_axes", "column pair of token (0, 1): %.2e", e);
}


// ---- Host references for the refiner kernels, in double, torch's semantics.

// grid_sample(bilinear, zeros, align_corners) of fb [h, w, C] at normalized (x, y).
void host_sample(const std::vector<float>& fb, int h, int w, int C, double x, double y,
                 bool align_corners, std::vector<double>& out) {
    const double sx = align_corners ? (x + 1) / 2 * (w - 1) : ((x + 1) * w - 1) / 2;
    const double sy = align_corners ? (y + 1) / 2 * (h - 1) : ((y + 1) * h - 1) / 2;
    out.assign((size_t)C, 0.0);
    const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
    const double lx = sx - x0, ly = sy - y0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int xx = x0 + dx, yy = y0 + dy;
            if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
            const double wt = (dx ? lx : 1 - lx) * (dy ? ly : 1 - ly);
            for (int c = 0; c < C; ++c) out[(size_t)c] += wt * fb[(size_t)((yy * w + xx) * C + c)];
        }
}

struct CorrVariant {
    bool swap_xy = false, align_corners = false, no_scale = false;
    double step = 2.0;   // offsets are k * step / size
};

std::vector<double> host_local_corr(const std::vector<float>& fa, const std::vector<float>& fb,
                                    const std::vector<float>& warp, int h, int w, int C, int r,
                                    const CorrVariant& v = {}) {
    const int K = (2 * r + 1) * (2 * r + 1);
    std::vector<double> out((size_t)h * w * K), s;
    for (int p = 0; p < h * w; ++p)
        for (int k = 0; k < K; ++k) {
            int ix = k % (2 * r + 1) - r, iy = k / (2 * r + 1) - r;
            if (v.swap_xy) std::swap(ix, iy);
            const double x = warp[(size_t)(2 * p)] + ix * v.step / w;
            const double y = warp[(size_t)(2 * p + 1)] + iy * v.step / h;
            host_sample(fb, h, w, C, x, y, v.align_corners, s);
            double acc = 0;
            for (int c = 0; c < C; ++c) acc += fa[(size_t)(p * C + c)] * s[(size_t)c];
            out[(size_t)(p * K + k)] = v.no_scale ? acc : acc / std::sqrt((double)C);
        }
    return out;
}

double max_abs_rel(const std::vector<float>& got, const std::vector<double>& want) {
    double num = 0, den = 0;
    for (size_t i = 0; i < want.size(); ++i) {
        num = std::max(num, std::fabs((double)got[i] - want[i]));
        den = std::max(den, std::fabs(want[i]));
    }
    return num / std::max(den, 1e-30);
}

// ---- Local correlation, fused and unfused, vs torch's semantics; warps cross
// the border. The bar must separate it from swapped x/y offsets,
// align_corners=True, no 1/sqrt(C), and half-pixel offsets.
void test_local_corr(vk::Arena& arena) {
    std::printf("\nlocal_corr\n");
    for (int r : {3, 1}) {
        const int h = 9, w = 14, C = 20, n = h * w, K = (2 * r + 1) * (2 * r + 1);
        const std::vector<float> fa = randn((size_t)(n * C), 10 + r);
        const std::vector<float> fb = randn((size_t)(n * C), 20 + r);
        std::vector<float> warp = randn((size_t)(2 * n), 30 + r, 0.7f);
        warp[0] = -1.05f;
        warp[3] = 0.98f;
        const std::vector<double> want = host_local_corr(fa, fb, warp, h, w, C, r);
        for (bool fused : {true, false}) {
            vk::ArenaScope s(arena);
            Tensor ta = upload(arena, fa, n, C), tb = upload(arena, fb, n, C);
            Tensor tw = upload(arena, warp, n, 2);
            Tensor out = nn::arena_tensor(arena, DType::F32, n, K);
            local_correlation(arena, out, ta, tb, tw, r, h, w, fused);
            const double e = max_abs_rel(download(out), want);
            check(e < 1e-5, fused ? "local_corr_fused" : "local_corr_unfused",
                  "r=%d: max abs / max |ref| %.2e (bar 1e-5)", r, e);
        }
        std::vector<float> wf(want.size());
        for (size_t i = 0; i < want.size(); ++i) wf[i] = (float)want[i];
        CorrVariant sw, ac, ns, half;
        sw.swap_xy = true;
        ac.align_corners = true;
        ns.no_scale = true;
        half.step = 1.0;
        double worst = 1e9;
        for (const CorrVariant* v : {&sw, &ac, &ns, &half})
            worst = std::min(worst, max_abs_rel(wf, host_local_corr(fa, fb, warp, h, w, C, r, *v)));
        check(worst > 1e-2, "local_corr_discriminates",
              "r=%d: nearest wrong variant %.2e from the reference (must exceed 1e-2)", r, worst);
    }
}

// ---- refine_disp / refine_update vs refiner.py, non-square. Catches (W, H)
// swapped, the grid sign, relu for softplus, the 1-channel prev unpadded, p10.
void test_refine_kernels(vk::Arena& arena) {
    std::printf("\nrefine_kernels\n");
    const int64_t h = 6, w = 11, n = h * w;
    const std::vector<float> warp = randn((size_t)(2 * n), 41, 0.5f);
    const std::vector<float> gx = centred_grid(w), gy = centred_grid(h);
    std::vector<float> grid((size_t)(2 * n));
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < w; ++x) {
            grid[(size_t)(2 * (y * w + x))] = gx[(size_t)x];
            grid[(size_t)(2 * (y * w + x) + 1)] = gy[(size_t)y];
        }
    {
        vk::ArenaScope s(arena);
        Tensor tw = upload(arena, warp, n, 2), tg = upload(arena, grid, n, 2);
        Tensor out = nn::arena_tensor(arena, DType::F32, n, 2);
        struct {
            uint64_t out, warp, grid;
            float sx, sy;
            uint32_t n, groups_per_row;
        } p{out.ptr, tw.ptr, tg.ptr, 1.25f, 0.5f, (uint32_t)n, 0};
        vk::Stream::get().dispatchFlat("roma.refine_disp", {1u}, 2 * n, 256, &p, sizeof(p),
                                       &p.groups_per_row);
        const std::vector<float> got = download(out);
        double e = 0;
        for (int64_t i = 0; i < 2 * n; ++i)
            e = std::max(e, std::fabs(got[(size_t)i] - (i % 2 ? 0.5 : 1.25) *
                                                         ((double)warp[(size_t)i] - grid[(size_t)i])));
        check(e < 1e-6, "refine_disp", "max abs %.2e", e);
    }
    for (int prev_c : {1, 4}) {
        vk::ArenaScope s(arena);
        const std::vector<float> conf = randn((size_t)(prev_c * n), 42 + prev_c);
        std::vector<float> dw = randn((size_t)(2 * n), 43), dc = randn((size_t)(4 * n), 44, 3.0f);
        dc[1] = 25.0f;   // softplus' linear branch
        Tensor tw = upload(arena, warp, n, 2), tc = upload(arena, conf, n, prev_c);
        Tensor tdw = upload(arena, dw, n, 2), tdc = upload(arena, dc, n, 4);
        Tensor ow = nn::arena_tensor(arena, DType::F32, n, 2);
        Tensor oc = nn::arena_tensor(arena, DType::F32, n, 4);
        struct {
            uint64_t warp_out, conf_out, warp, conf, dwarp, dconf;
            float den_x, den_y;
            uint32_t n, prev_c, groups_per_row;
        } p{ow.ptr, oc.ptr, tw.ptr, tc.ptr, tdw.ptr, tdc.ptr, 4.0f * w, 4.0f * h,
            (uint32_t)n, (uint32_t)prev_c, 0};
        vk::Stream::get().dispatchFlat("roma.refine_update", {1u}, n, 256, &p, sizeof(p),
                                       &p.groups_per_row);
        const std::vector<float> gw = download(ow), gc = download(oc);
        auto sp = [](double x) { return x > 20 ? x : std::log1p(std::exp(x)); };
        double ew = 0, ecv = 0;
        for (int64_t i = 0; i < n; ++i) {
            ew = std::max(ew, std::fabs(gw[(size_t)(2 * i)] - (warp[(size_t)(2 * i)] + dw[(size_t)(2 * i)] / (4.0 * w))));
            ew = std::max(ew, std::fabs(gw[(size_t)(2 * i + 1)] -
                                        (warp[(size_t)(2 * i + 1)] + dw[(size_t)(2 * i + 1)] / (4.0 * h))));
            const double l00 = sp(dc[(size_t)(4 * i + 1)]) + 1e-6, l10 = dc[(size_t)(4 * i + 2)];
            const double l11 = sp(dc[(size_t)(4 * i + 3)]) + 1e-6;
            const double d[4] = {dc[(size_t)(4 * i)], l00 * l00, l00 * l10, l10 * l10 + l11 * l11};
            for (int k = 0; k < 4; ++k) {
                const double prev = k < prev_c ? conf[(size_t)(prev_c * i + k)] : 0.0;
                const double want = prev + d[k];
                ecv = std::max(ecv, std::fabs(gc[(size_t)(4 * i + k)] - want) /
                                        std::max(1.0, std::fabs(want)));
            }
        }
        check(ew < 1e-6, "refine_update_warp", "prev_c %d: max abs %.2e", prev_c, ew);
        check(ecv < 1e-5, "refine_update_conf", "prev_c %d: max rel %.2e", prev_c, ecv);
    }
}

// ---- torch's antialiased bicubic preserves a constant and averages on a
// downscale. Its exact match to torch is reported by compare_torch.py.
void test_resize() {
    std::printf("\nresize\n");
    std::vector<uint8_t> flat(64 * 48 * 3, 200), checker(64 * 48 * 3);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x)
            for (int c = 0; c < 3; ++c) checker[(size_t)((y * 64 + x) * 3 + c)] = ((x + y) & 1) * 255;
    const std::vector<float> a = resize_rgb(flat.data(), 64, 48, 16, 16);
    const std::vector<float> b = resize_rgb(checker.data(), 64, 48, 16, 12);
    double ea = 0, eb = 0;
    for (float v : a) ea = std::max(ea, (double)std::fabs(v - 200.0f / 255.0f));
    for (float v : b) eb = std::max(eb, (double)std::fabs(v - 0.5f));
    check(ea < 1e-6, "resize_constant", "max deviation %.2e", ea);
    check(eb < 2e-2, "resize_antialias", "4x checkerboard reduction off grey by %.2e", eb);
}

std::vector<float> load_rgb(const std::string& path, int size) {
    const nn::Image img = nn::load_image(path);
    NN_CHECK(!img.empty() && img.channels == 3, "cannot read %s as RGB", path.c_str());
    return resize_rgb(img.data.data(), img.width, img.height, size, size);
}

std::vector<float> synthetic(int size, int shift) {
    std::vector<float> v((size_t)size * size * 3);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int u = x + shift;
            const float base = ((u / 23 + y / 29) % 2) ? 0.8f : 0.2f;
            for (int c = 0; c < 3; ++c)
                v[(size_t)((y * size + x) * 3 + c)] =
                    base * (0.6f + 0.4f * (float)c / 2.0f) + 0.1f * std::sin(0.05f * u * (c + 1));
        }
    return v;
}

void test_model(const std::string& ckpt, const std::string& a, const std::string& b, int size,
                int repeat) {
    std::printf("\nmodel %s\n", ckpt.c_str());
    {
        // Every dimension the forward passes read, at the values RoMa v2 ships.
        Weights w;
        w.load(ckpt);
        const BackboneHparams& bb = w.backbone();
        const MatcherHparams& mt = w.matcher();
        check(bb.width == 1024 && bb.blocks == 24 && bb.heads == 16 && bb.patch == 16 &&
                  bb.prefix == 5 && bb.mlp == 4096,
              "hparams_backbone", "%lld wide, %d blocks, %d heads, patch %d, prefix %d",
              (long long)bb.width, bb.blocks, bb.heads, bb.patch, bb.prefix);
        check(mt.width == 768 && mt.blocks == 12 && mt.heads == 12 && mt.in == 2048 &&
                  mt.out == 1024 && std::fabs(mt.temp - 0.1f) < 1e-7f && mt.scale == 1.0f,
              "hparams_matcher", "%lld wide, %d blocks, %d heads, temp %g, scale %g",
              (long long)mt.width, mt.blocks, mt.heads, mt.temp, mt.scale);
        check(mt.dpt_channels == std::vector<int64_t>({256, 512, 1024, 1024}) &&
                  mt.dpt_features == 256,
              "hparams_dpt", "features %lld", (long long)mt.dpt_features);
        int taps = 0;
        for (const VggConv& c : w.vgg()) taps += c.tap_after;
        check(w.vgg().size() == 8 && taps == 3, "hparams_vgg", "%zu convs, %d taps",
              w.vgg().size(), taps);
        // The stored periods are 100 ** (2k / 32), rounded to bf16.
        const std::vector<float> want = periods16();
        if (f16_weights()) {
            // bf16 -> f16 is exact inside f16's normal range; 4.6e-4 of the
            // backbone's values are below it (measured on romav2.0.1.pt).
            const double frac = (double)w.inexactF16() / 303.2e6;
            check(frac < 1e-3, "f16_inexact_bounded", "%llu backbone weights not exact in "
                  "f16 (%.2e of them)", (unsigned long long)w.inexactF16(), frac);
        }
        check(w.backbonePeriods() == want && w.matcherPeriods() == want, "hparams_periods",
              "both RoPE period tables are DINOv3's");
    }

    Model m;
    m.load(ckpt);
    std::printf("  pair: %s | %s at %d\n", a.empty() ? "(synthetic)" : a.c_str(),
                b.empty() ? "(synthetic)" : b.c_str(), size);
    const std::vector<float> A = a.empty() ? synthetic(size, 0) : load_rgb(a, size);
    const std::vector<float> B = b.empty() ? synthetic(size, 13) : load_rgb(b, size);
    CoarseMatch cm;
    for (int r = 0; r < repeat; ++r) {
        vk::Stream::get().sync();
        const double t0 = nn::now_ms();
        cm = m.coarse(A.data(), B.data(), size, size);
        std::printf("  coarse %dx%d: %.0f ms\n", size, size, nn::now_ms() - t0);
    }
    check(cm.h == size / 4 && cm.w == size / 4, "coarse_shape", "%dx%d", cm.w, cm.h);
    size_t finite = 0, inside = 0;
    for (size_t i = 0; i < cm.data.size(); ++i) {
        finite += std::isfinite(cm.data[i]);
        if (i % 3 != 2) inside += std::fabs(cm.data[i]) <= 1.5f;
    }
    check(finite == cm.data.size(), "coarse_finite", "%zu of %zu", finite, cm.data.size());
    check(inside * 3 >= cm.data.size() * 2 * 9 / 10, "coarse_range",
          "%zu of %zu warp values within 1.5", inside, cm.data.size() * 2 / 3);
    const uint64_t plan = m.plannedBytes(), peak = m.peakBytes();
    check(peak <= plan, "arena_within_plan", "%.1f MB used of %.1f MB planned (%.0f%%)",
          peak / 1e6, plan / 1e6, 100.0 * (double)peak / (double)plan);
    // Per stage too: a term that never binds the overall maximum still has
    // to be right, or the size where it does bind fails in the field.
    for (const Model::Stage& st : m.stages())
        check(st.peak <= st.plan, "stage_within_plan", "%s: %.1f of %.1f MB (%.0f%%)",
              st.name, st.peak / 1e6, st.plan / 1e6, 100.0 * (double)st.peak / (double)st.plan);
    std::printf("  weights on device: %.2f GB\n", m.weightBytes() / 1e9);

    // ---- AB is independent of BA, a cache hit equals a miss, BA(A,B) = AB(B,A),
    // the adapter is match() + sigmoid. Catches BA aliasing AB's state, a stale
    // cache, the directions' VGG maps swapped, a missing sigmoid.
    std::vector<uint8_t> qa(A.size()), qb(B.size());
    std::vector<float> fa(A.size()), fb(B.size());
    for (size_t i = 0; i < A.size(); ++i) {
        qa[i] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, A[i])) * 255.0f);
        qb[i] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, B[i])) * 255.0f);
        fa[i] = qa[i] / 255.0f;
        fb[i] = qb[i] / 255.0f;
    }
    MatchSpec ms;
    ms.lr_h = ms.lr_w = size;
    const MatchResult one = m.match(fa.data(), fb.data(), nullptr, nullptr, ms);
    ms.bidirectional = true;
    const MatchResult both = m.match(fa.data(), fb.data(), nullptr, nullptr, ms, "a");
    const MatchResult hit = m.match(fa.data(), fb.data(), nullptr, nullptr, ms, "a");
    const MatchResult swapped = m.match(fb.data(), fa.data(), nullptr, nullptr, ms, "b");
    auto max_diff = [](const std::vector<float>& x, const std::vector<float>& y) {
        if (x.size() != y.size()) return 1e30;
        double e = 0;
        for (size_t i = 0; i < x.size(); ++i) e = std::max(e, (double)std::fabs(x[i] - y[i]));
        return e;
    };
    check(one.ab.h == size && one.ab.w == size && one.ab.warp.size() == (size_t)size * size * 2 &&
              one.ab.confidence.size() == (size_t)size * size * 4 && both.ba.h == size,
          "match_shape", "%dx%d, BA %dx%d", one.ab.w, one.ab.h, both.ba.w, both.ba.h);
    size_t fin = 0;
    for (float v : one.ab.warp) fin += std::isfinite(v);
    for (float v : one.ab.confidence) fin += std::isfinite(v);
    check(fin == one.ab.warp.size() + one.ab.confidence.size(), "match_finite", "%zu values",
          fin);
    check(max_diff(one.ab.warp, both.ab.warp) == 0 &&
              max_diff(one.ab.confidence, both.ab.confidence) == 0,
          "match_ab_independent_of_ba", "warp %.2e, confidence %.2e",
          max_diff(one.ab.warp, both.ab.warp), max_diff(one.ab.confidence, both.ab.confidence));
    check(max_diff(hit.ab.warp, both.ab.warp) == 0 && max_diff(hit.ba.warp, both.ba.warp) == 0 &&
              max_diff(hit.ab.confidence, both.ab.confidence) == 0,
          "match_cache_hit_exact", "warp %.2e", max_diff(hit.ab.warp, both.ab.warp));
    // BA of (A, B) is AB of (B, A) up to the transformer's token order, which
    // reorders sums that the bf16 RoPE then rounds: read the median, and p99.
    std::vector<double> epe;
    for (size_t i = 0; i + 1 < both.ba.warp.size() && swapped.ab.warp.size() == both.ba.warp.size(); i += 2)
        epe.push_back(std::hypot(swapped.ab.warp[i] - both.ba.warp[i],
                                 swapped.ab.warp[i + 1] - both.ba.warp[i + 1]) * size / 2);
    std::sort(epe.begin(), epe.end());
    const double p50 = epe.empty() ? 1e30 : epe[epe.size() / 2];
    const double p99 = epe.empty() ? 1e30 : epe[epe.size() * 99 / 100];
    check(p50 < 1e-3 && p99 < 0.05, "match_ba_is_swapped_ab",
          "EPE p50 %.2e px, p99 %.2e px, max %.2e px (bars 1e-3, 0.05)", p50, p99,
          epe.empty() ? 0.0 : epe.back());
    const double sep = max_diff(both.ab.warp, both.ba.warp);
    check(sep > 1e-2, "match_ba_discriminates", "AB vs BA differ by %.2e (must exceed 1e-2)",
          sep);
    std::printf("  certainty > 0.5: %.1f%% of pixels\n", [&] {
        size_t c = 0;
        for (size_t i = 0; i < (size_t)size * size; ++i) c += one.ab.overlap(i) > 0.5f;
        return 100.0 * (double)c / ((double)size * size);
    }());
    for (const Model::Stage& st : m.stages())
        check(st.peak <= st.plan, "match_stage_within_plan", "%s: %.1f of %.1f MB (%.0f%%)",
              st.name, st.peak / 1e6, st.plan / 1e6, 100.0 * (double)st.peak / (double)st.plan);
    check(m.peakBytes() <= m.plannedBytes(), "match_arena_within_plan", "%.1f of %.1f MB",
          m.peakBytes() / 1e6, m.plannedBytes() / 1e6);

    Preset pr = Preset::Base;
    bool found = false;
    for (Preset q : {Preset::Turbo, Preset::Fast, Preset::Base})
        if (!found && preset_spec(q).lr == size) {
            pr = q;
            found = true;
        }
    if (found) {
        RomaMatcher rm(ckpt, pr);
        const Warp wp = rm.match({"a", size, size, qa.data()}, {"b", size, size, qb.data()});
        const size_t n = (size_t)size * size;
        double ec = 0;
        size_t certain = 0;
        for (size_t i = 0; i < n && wp.certainty.size() == n; ++i) {
            ec = std::max(ec, (double)std::fabs(wp.certainty[i] - one.ab.overlap(i)));
            certain += wp.certainty[i] > 0.5f;
        }
        check(wp.width == size && max_diff(wp.warp, one.ab.warp) == 0 && ec < 1e-7,
              "matcher_adapter", "%s: warp %.2e, certainty %.2e vs match()",
              rm.describe().c_str(), max_diff(wp.warp, one.ab.warp), ec);
        std::printf("  adapter: %.1f%% of pixels certain > 0.5\n",
                    100.0 * (double)certain / (double)std::max<size_t>(n, 1));
    }
}


// ---- Per preset: s/pair with A new each pair (cold) and A repeated (warm, the
// densify loop's case), and device memory beside the weights. The plan's §5
// gate: the precise preset's arena plus A's cache within 4 GB.
void bench(const std::string& ckpt, const std::string& a, const std::string& b,
           const std::string& list, int repeat) {
    std::printf("\nbench %s\n", list.c_str());
    NN_CHECK(!a.empty() && !b.empty(), "--bench needs --pair A B");
    const nn::Image ia = nn::load_image(a), ib = nn::load_image(b);
    NN_CHECK(ia.channels == 3 && ib.channels == 3, "--bench: the pair must be RGB");
    size_t at = 0;
    while (at <= list.size()) {
        const size_t comma = std::min(list.find(',', at), list.size());
        const std::string name = list.substr(at, comma - at);
        at = comma + 1;
        Preset pr;
        if (!parse_preset(name, pr)) {
            check(false, "bench_preset", "unknown preset '%s'", name.c_str());
            continue;
        }
        RomaMatcher rm(ckpt, pr);
        const bool both = spirula::env("ROMA_BENCH_BOTH") != nullptr;
        const MatchImage mb{"b", ib.width, ib.height, ib.data.data()};
        double cold = 0, warm = 0;
        Warp out;
        for (int r = 0; r < repeat + 1; ++r) {
            // Pair 0 also builds pipelines and the arena: not timed.
            const std::string key = "a" + std::to_string(r);
            const MatchImage ma{key, ia.width, ia.height, ia.data.data()};
            vk::Stream::get().sync();
            double t0 = nn::now_ms();
            out = both ? rm.matchBoth(ma, mb).first : rm.match(ma, mb);
            const double t1 = nn::now_ms();
            out = both ? rm.matchBoth(ma, mb).first : rm.match(ma, mb);
            const double t2 = nn::now_ms();
            if (r) {
                cold += (t1 - t0) / repeat;
                warm += (t2 - t1) / repeat;
            }
        }
        Model& m = rm.model();
        const uint64_t arena = m.plannedBytes(), cache = m.cacheBytes();
        size_t certain = 0;
        for (float c : out.certainty) certain += c > 0.5f;
        std::printf("  %-8s %s: cold %.3f s/pair, warm %.3f s/pair | arena plan %.0f MB "
                    "(peak %.0f), A cache %.0f MB, weights %.0f MB | %dx%d, %.1f%% > 0.5\n",
                    name.c_str(), both ? "AB+BA" : "AB", cold / 1e3, warm / 1e3, arena / 1e6,
                    m.peakBytes() / 1e6, cache / 1e6, m.weightBytes() / 1e6, out.width,
                    out.height, 100.0 * (double)certain / (double)std::max<size_t>(out.certainty.size(), 1));
        check(m.peakBytes() <= arena, "bench_arena_within_plan", "%s", name.c_str());
        for (const Model::Stage& st : m.stages())
            check(st.peak <= st.plan, "bench_stage_within_plan", "%s %s: %.1f of %.1f MB",
                  name.c_str(), st.name, st.peak / 1e6, st.plan / 1e6);
        if (pr == Preset::Precise)
            check(arena + cache <= 4000000000ull, "precise_arena_4gb",
                  "arena %.2f GB + A cache %.2f GB = %.2f GB (bar 4 GB)", arena / 1e9,
                  cache / 1e9, (arena + cache) / 1e9);
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (!spirula::env("NN_LOG")) nn::set_log_level(2);
    std::string ckpt, a, b, bench_list;
    int size = 640, repeat = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (s == "--checkpoint") ckpt = next();
        else if (s == "--pair") { a = next(); b = next(); }
        else if (s == "--size") size = std::atoi(next().c_str());
        else if (s == "--repeat") repeat = std::atoi(next().c_str());
        else if (s == "--bench") bench_list = next();
        else { std::printf("unknown argument '%s'\n", s.c_str()); return 2; }
    }
    try {
        vk::Context::get();
        NN_ENSURE_EMBEDDED_MODULES(roma);
        {
            vk::Arena arena("roma-test");
            arena.reserve(64ull << 20);
            test_rope_permutation(arena);
            test_rope_bf16(arena);
            test_local_corr(arena);
            test_refine_kernels(arena);
        }
        test_rope_tables();
        test_resize();
        std::error_code ec;
        if (ckpt.empty()) ckpt = nn::cached_path(checkpoint_file());
        if (!std::filesystem::exists(ckpt, ec))
            std::printf("\nSKIP model: %s is not cached (--checkpoint PATH)\n", ckpt.c_str());
        else if (!bench_list.empty())
            bench(ckpt, a, b, bench_list, repeat);
        else
            test_model(ckpt, a, b, size, repeat);
    } catch (const std::exception& e) {
        std::printf("EXCEPTION: %s\n", e.what());
        return 1;
    }
    std::printf("\n%d checks, %d failures\n%s\n", g_checks, g_failures,
                g_failures ? "FAIL" : "PASS");
    vk::Stream::shutdown();
    vk::Pipelines::get().shutdown();
    vk::VramPool::get().releaseAll();
    return g_failures ? 1 : 0;
}
