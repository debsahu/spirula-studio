// RoMa v2's frozen DINOv3 ViT-L/16, composed from the architecture rather
// than from Meta's code: a pre-norm ViT with LayerScale, a class token and
// four storage tokens in front of the patches, axial RoPE on the patch rows
// only, and a K bias that is masked to zero (folded at load). RoMa reads
// blocks 11 and 17 through the final LayerNorm, so blocks 18-23 never run.
//
// LayerScale stays a separate multiply, for the reason in loma/model/Dino.cpp:
// the matrices are f16 and a trained gamma is ~1e-5.

#include "roma/model/Model.h"

#include "roma/model/Dump.h"
#include "roma/model/Rope.h"
#include "nn/Ops.h"
#include "nn/vk/Stream.h"

#include <string>

namespace roma {
namespace {

using nn::Act;
using nn::DType;
using nn::LinearOpts;
using nn::Tensor;

constexpr float kNormEps = 1e-5f;   // DINOv3's "layernormbf16"
constexpr uint64_t kSlack = 64ull << 20;   // the conv column chunk and attention partials

}  // namespace

void RopeTable::release() {
    if (blob_) vk::device_free(blob_);
    blob_ = 0;
    h_ = w_ = 0;
}

const Tensor& RopeTable::get(const std::vector<float>& periods, int64_t h, int64_t w,
                             bool bf16) {
    if (blob_ && h == h_ && w == w_) return t_;
    release();
    const std::vector<float> host =
        bf16 ? matcher_rope_bf16(periods, h, w) : backbone_rope(periods, h, w);
    blob_ = vk::device_alloc((uint64_t)host.size() * 4, "roma-rope");
    t_ = Tensor(blob_, DType::F32, h * w, (int64_t)periods.size() * 2, 2);
    nn::tensor_from_host(t_, host.data(), (int64_t)host.size());
    vk::Stream::get().sync();
    h_ = h;
    w_ = w;
    return t_;
}

uint64_t Backbone::planBytes(const Weights& w, int64_t h, int64_t wd) {
    const BackboneHparams& hp = w.backbone();
    const int64_t N = hp.prefix + h * wd, D = hp.width;
    // x, then one block's t plus the wider of qkv + attn and the MLP hidden.
    const int64_t block = N * D + std::max<int64_t>(4 * N * D, N * hp.mlp);
    return (uint64_t)(N * D + block) * 4 + kSlack;
}

void Backbone::run(const Weights& w, vk::Arena& arena, const Tensor& image,
                   const Tensor& tap0, const Tensor& tap1, int64_t h, int64_t wd) {
    const BackboneHparams& hp = w.backbone();
    const int64_t D = hp.width, n = h * wd, P = hp.prefix, N = P + n;
    const int hd = (int)(D / hp.heads);
    const Tensor& freqs = rope_.get(w.backbonePeriods(), h, wd, false);
    dump_tensor("rope_backbone", freqs, {n, hd / 2, 2});

    vk::ArenaScope tokens(arena);
    Tensor x = nn::arena_tensor(arena, DType::F32, N, D);
    Tensor patch_rows = x.offsetElems(P * D).view(n, D);
    {
        vk::ArenaScope scope(arena);
        Tensor patches = nn::arena_tensor(arena, DType::F32, h, wd, D);
        nn::ConvOpts co;
        co.stride_y = co.stride_x = hp.patch;
        co.bias = w.get("f.patch_embed.proj.bias");
        nn::conv2d(arena, patches, image, w.get("f.patch_embed.proj.weight"), hp.patch,
                   hp.patch, co);
        nn::copy(patch_rows, patches.view(n, D));
        nn::copy(x.view(N, D).slice0(0, P), w.get("f.prefix"));
    }

    for (int b = 0; b <= hp.taps[1]; ++b) {
        vk::ArenaScope scope(arena);
        const std::string q = "f.blocks." + std::to_string(b) + ".";
        Tensor t = nn::arena_tensor(arena, DType::F32, N, D);

        nn::layer_norm(t, x, w.get(q + "norm1.weight"), w.get(q + "norm1.bias"), kNormEps);
        {
            vk::ArenaScope inner(arena);
            Tensor qkv = nn::arena_tensor(arena, DType::F32, N, 3 * D);
            LinearOpts lo;
            lo.bias = w.get(q + "attn.qkv.bias");
            nn::linear(qkv, t, w.get(q + "attn.qkv.weight"), lo);
            // The prefix tokens are not rotated; q and k's rows were permuted
            // at load so this adjacent-pair RoPE is the reference's rotate-half.
            nn::rope(qkv.offsetElems(P * 3 * D), freqs, hp.heads, hd, n, 1, 3 * D);
            nn::rope(qkv.offsetElems(P * 3 * D + D), freqs, hp.heads, hd, n, 1, 3 * D);

            Tensor attn = nn::arena_tensor(arena, DType::F32, N, D);
            nn::AttnOpts ao;
            ao.n_heads = hp.heads;
            ao.head_dim = hd;
            ao.arena = &arena;
            ao.q_stride = ao.k_stride = ao.v_stride = 3 * D;
            nn::attention(attn, qkv, qkv.offsetElems(D), qkv.offsetElems(2 * D), N, N, ao);

            LinearOpts po;
            po.bias = w.get(q + "attn.proj.bias");
            nn::linear(t, attn, w.get(q + "attn.proj.weight"), po);
        }
        nn::mul(t, t, w.get(q + "ls1.gamma"));
        nn::add(x, x, t);

        nn::layer_norm(t, x, w.get(q + "norm2.weight"), w.get(q + "norm2.bias"), kNormEps);
        {
            vk::ArenaScope inner(arena);
            Tensor hid = nn::arena_tensor(arena, DType::F32, N, hp.mlp);
            LinearOpts lo;
            lo.bias = w.get(q + "mlp.fc1.bias");
            lo.act = Act::GeluErf;
            nn::linear(hid, t, w.get(q + "mlp.fc1.weight"), lo);
            LinearOpts o2;
            o2.bias = w.get(q + "mlp.fc2.bias");
            nn::linear(t, hid, w.get(q + "mlp.fc2.weight"), o2);
        }
        nn::mul(t, t, w.get(q + "ls2.gamma"));
        nn::add(x, x, t);

        const Tensor* tap = b == hp.taps[0] ? &tap0 : b == hp.taps[1] ? &tap1 : nullptr;
        if (tap)
            nn::layer_norm(tap->view(n, D), patch_rows, w.get("f.norm.weight"),
                           w.get("f.norm.bias"), kNormEps);
    }
}

void rope_half_bf16(const Tensor& x, const Tensor& cs, int n_heads, int head_dim, int64_t n,
                    int batch, int64_t row_stride) {
    NN_CHECK(x.dtype == DType::F32 && (head_dim & 1) == 0, "rope_half_bf16: bad operands");
    NN_CHECK(cs.numel() >= n * head_dim, "rope_half_bf16: table too small for %lld tokens",
             (long long)n);
    struct {
        uint64_t x, cs;
        uint32_t n, n_heads, head_dim, batch, row_stride, groups_per_row;
    } p{x.ptr, cs.ptr, (uint32_t)n, (uint32_t)n_heads, (uint32_t)head_dim, (uint32_t)batch,
        (uint32_t)row_stride, 0};
    const int64_t total = (int64_t)batch * n * n_heads * (head_dim / 2);
    vk::Stream::get().dispatchFlat("roma.rope_half_bf16", {}, total, 256, &p, sizeof(p),
                                   &p.groups_per_row);
}

}  // namespace roma
