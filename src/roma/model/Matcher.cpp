// RoMa v2's coarse matcher, composed from the architecture: both images' two
// backbone taps concatenated and projected into one ViT-B over 2*h*w tokens,
// whose even blocks attend across both images with no position encoding and
// whose odd blocks attend within each image under axial RoPE. Then a cosine
// similarity at temperature `temp`, a softmax over B, an expected sin/cos
// embedding of B's grid, and the DPT head.

#include "roma/model/Model.h"

#include "roma/model/Dump.h"
#include "nn/Ops.h"
#include "nn/vk/Stream.h"

#include <cmath>
#include <string>

namespace roma {
namespace {

using nn::Act;
using nn::DType;
using nn::LinearOpts;
using nn::Tensor;

constexpr float kNormEps = 1e-6f;
// Attention's split-K partials, <= 512*64/heads queries x width floats.
constexpr uint64_t kAttnSlack = 10ull << 20;

}  // namespace

std::vector<float> torch_linspace(double a, double b, int64_t n) {
    std::vector<float> g((size_t)std::max<int64_t>(n, 0));
    const float fa = (float)a, fb = (float)b;
    const float step = n > 1 ? (fb - fa) / (float)(n - 1) : 0.0f;
    const int64_t half = n / 2;
    for (int64_t i = 0; i < n; ++i)
        g[(size_t)i] = i < half ? fa + step * (float)i : fb - step * (float)(n - 1 - i);
    return g;
}

std::vector<float> centred_grid(int64_t n) {
    return torch_linspace(-1.0 + 1.0 / (double)n, 1.0 - 1.0 / (double)n, n);
}

CoarseMatcher::~CoarseMatcher() {
    if (pos_blob_) vk::device_free(pos_blob_);
}

// [out, h*w]: column j of B's grid embedded as sin(x.o_k) for k < pos_pairs
// then cos(x.o_k), with x = (gx, gy), so the match embedding is one matmul_nt.
void CoarseMatcher::ensurePosEmbed(const Weights& w, int64_t h, int64_t wd) {
    if (pos_blob_ && pos_h_ == h && pos_w_ == wd) return;
    const MatcherHparams& mp = w.matcher();
    const std::vector<float>& om = w.omega();
    const std::vector<float> gx = centred_grid(wd), gy = centred_grid(h);
    const int64_t n = h * wd, K = mp.pos_pairs;
    std::vector<float> t((size_t)(2 * K * n));
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < wd; ++x) {
            const int64_t j = y * wd + x;
            for (int64_t k = 0; k < K; ++k) {
                const float e = gx[(size_t)x] * (mp.scale * om[(size_t)(2 * k)]) +
                                gy[(size_t)y] * (mp.scale * om[(size_t)(2 * k + 1)]);
                t[(size_t)(k * n + j)] = std::sin(e);
                t[(size_t)((K + k) * n + j)] = std::cos(e);
            }
        }
    if (pos_blob_) vk::device_free(pos_blob_);
    pos_blob_ = vk::device_alloc((uint64_t)t.size() * 4, "roma-pos-emb");
    pos_t_ = Tensor(pos_blob_, DType::F32, 2 * K, n);
    nn::tensor_from_host(pos_t_, t.data(), (int64_t)t.size());
    vk::Stream::get().sync();
    pos_h_ = h;
    pos_w_ = wd;
}

CoarseMatcher::Plan CoarseMatcher::plan(const Weights& w, int64_t h, int64_t wd) {
    const MatcherHparams& mp = w.matcher();
    const int64_t n = h * wd, T = 2 * n, E = mp.width;
    // mv is allocated first and lives through the transformer.
    const int64_t proj = T * mp.out + T * E + T * mp.in;
    const int64_t block = T * mp.out + T * E * 2 + std::max<int64_t>(T * 4 * E, T * mp.mlp);
    // Normalized copies, the similarity, the match embedding and the head's input.
    const int64_t sim = 2 * n * mp.out + n * n + 2 * n * mp.out;
    Plan p;
    p.transformer = (uint64_t)std::max(proj, block) * 4 + kAttnSlack;
    p.similarity = (uint64_t)sim * 4 + (1u << 20);
    p.head = dpt_plan_bytes(w, h, wd);
    return p;
}

void CoarseMatcher::run(const Weights& w, vk::Arena& arena, const Tensor taps_a[2],
                        const Tensor taps_b[2], int64_t h, int64_t wd, const Tensor& out,
                        StageLog& log, const Tensor& out_ba) {
    const MatcherHparams& mp = w.matcher();
    const int64_t n = h * wd, T = 2 * n, E = mp.width, C = mp.in / 2, O = mp.out;
    const int hd = (int)(E / mp.heads);
    const std::string p = "matcher.mv_vit.";
    const Tensor& cs = rope_.get(w.matcherPeriods(), h, wd, matcher_rope_rounds());
    dump_tensor("rope_matcher", cs, {n, hd / 2, 2});
    ensurePosEmbed(w, h, wd);

    const Plan pl = plan(w, h, wd);
    vk::ArenaScope scope(arena);
    Tensor mv;
    log.run("matcher transformer", pl.transformer, [&] {
        mv = nn::arena_tensor(arena, DType::F32, T, O);
        vk::ArenaScope tokens(arena);
        Tensor x = nn::arena_tensor(arena, DType::F32, T, E);
        {
            vk::ArenaScope s(arena);
            // Rows: A's tokens then B's; columns: tap 0 then tap 1.
            Tensor fin = nn::arena_tensor(arena, DType::F32, T, 2 * C);
            for (int img = 0; img < 2; ++img)
                for (int t = 0; t < 2; ++t)
                    nn::strided_copy(fin.offsetElems(img * n * 2 * C + t * C),
                                     (img ? taps_b : taps_a)[t], n, C, C, 2 * C);
            LinearOpts lo;
            lo.bias = w.get(p + "projector.bias");
            nn::linear(x, fin, w.get(p + "projector.weight"), lo);
        }

        for (int b = 0; b < mp.blocks; ++b) {
            vk::ArenaScope s(arena);
            const std::string q = p + "blocks." + std::to_string(b) + ".";
            const bool per_image = b % 2 == 1;
            Tensor t = nn::arena_tensor(arena, DType::F32, T, E);
            nn::layer_norm(t, x, w.get(q + "norm1.weight"), w.get(q + "norm1.bias"),
                           kNormEps);
            {
                vk::ArenaScope inner(arena);
                Tensor qkv = nn::arena_tensor(arena, DType::F32, T, 3 * E);
                LinearOpts lo;
                lo.bias = w.get(q + "attn.qkv.bias");
                nn::linear(qkv, t, w.get(q + "attn.qkv.weight"), lo);
                nn::AttnOpts ao;
                ao.n_heads = mp.heads;
                ao.head_dim = hd;
                ao.arena = &arena;
                ao.q_stride = ao.k_stride = ao.v_stride = 3 * E;
                if (per_image) {
                    rope_half_bf16(qkv, cs, mp.heads, hd, n, 2, 3 * E);
                    rope_half_bf16(qkv.offsetElems(E), cs, mp.heads, hd, n, 2, 3 * E);
                    ao.batch = 2;
                }
                Tensor attn = nn::arena_tensor(arena, DType::F32, T, E);
                const int64_t nq = per_image ? n : T;
                nn::attention(attn, qkv, qkv.offsetElems(E), qkv.offsetElems(2 * E), nq, nq,
                              ao);
                LinearOpts po;
                po.bias = w.get(q + "attn.proj.bias");
                po.residual = x;
                nn::linear(x, attn, w.get(q + "attn.proj.weight"), po);
            }
            nn::layer_norm(t, x, w.get(q + "norm2.weight"), w.get(q + "norm2.bias"),
                           kNormEps);
            {
                vk::ArenaScope inner(arena);
                Tensor hid = nn::arena_tensor(arena, DType::F32, T, mp.mlp);
                LinearOpts lo;
                lo.bias = w.get(q + "mlp.fc1.bias");
                lo.act = Act::GeluErf;
                nn::linear(hid, t, w.get(q + "mlp.fc1.weight"), lo);
                LinearOpts o2;
                o2.bias = w.get(q + "mlp.fc2.bias");
                o2.residual = x;
                nn::linear(x, hid, w.get(q + "mlp.fc2.weight"), o2);
            }
        }
        Tensor xn = nn::arena_tensor(arena, DType::F32, T, E);
        nn::layer_norm(xn, x, w.get(p + "norm.weight"), w.get(p + "norm.bias"), kNormEps);
        LinearOpts lo;
        lo.bias = w.get(p + "output_projector.bias");
        nn::linear(mv, xn, w.get(p + "output_projector.weight"), lo);
    });
    const Tensor mv_a = mv.slice0(0, n), mv_b = mv.slice0(n, n);
    dump_tensor("mv_A", mv_a, {h, wd, O});
    dump_tensor("mv_B", mv_b, {h, wd, O});

    // Upstream's order: A's head, then B's from the same tokens with the roles swapped.
    auto direction = [&](const Tensor& mx, const Tensor& my, const Tensor taps_x[2],
                         const Tensor& o, const char* tag) {
        const std::string t(tag);
        vk::ArenaScope dir(arena);
        Tensor head_x;
        log.run("matcher similarity", pl.similarity, [&] {
            head_x = nn::arena_tensor(arena, DType::F32, n, O);
            vk::ArenaScope s(arena);
            Tensor na = nn::arena_tensor(arena, DType::F32, n, O);
            Tensor nb = nn::arena_tensor(arena, DType::F32, n, O);
            nn::l2_normalize_rows(na, mx);
            nn::l2_normalize_rows(nb, my);
            Tensor sim = nn::arena_tensor(arena, DType::F32, n, n);
            // 1/temp as torch evaluates it: the reciprocal first, then the product.
            nn::matmul_nt(sim, na, nb, 1.0f / mp.temp);
            dump_tensor(("sim_" + t).c_str(), sim, {n, n});
            nn::softmax_rows(sim, sim);
            Tensor emb = nn::arena_tensor(arena, DType::F32, n, O);
            nn::matmul_nt(emb, sim, pos_t_);
            dump_tensor(("match_emb_" + t).c_str(), emb, {h, wd, O});
            nn::add(head_x, taps_x[1].view(n, O), mx);
            nn::add(head_x, head_x, emb);
        });
        log.run("dpt head", pl.head, [&] { dpt_head(w, arena, taps_x[0], head_x, h, wd, o); });
        dump_tensor(("dpt_out_" + t).c_str(), o, {4 * h, 4 * wd, 3});
    };
    direction(mv_a, mv_b, taps_a, out, "AB");
    if (out_ba.valid()) direction(mv_b, mv_a, taps_b, out_ba, "BA");
}

}  // namespace roma
