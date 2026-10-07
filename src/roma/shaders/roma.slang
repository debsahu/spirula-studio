// RoMa v2's model-specific kernels: the refiners' elementwise heads, and the
// matcher's bf16 RoPE. The multi-view transformer builds its
// RoPE in bf16 (`pos_embed_rope_dtype="bf16"`) and casts q and k to it before
// rotating, so the reference rounds them on every RoPE block even in fp32.
// Measured on a 640^2 pair: running that rotation in fp32 instead moves the
// coarse warp by 0.26 px at the median, 26x the parity bar.

#include "_common.slang"

// 0 runs the same rotation in fp32: SS_ROMA_ROPE_F32, a parity diagnostic
// against compare_torch.py --ref rope32, never a production path.
[SpecializationConstant] const int kRound = 1;

static const int WG = 256;

struct RopeBf16Params {
    uint*  x;       // [batch, n, n_heads * head_dim] f32, updated in place
    uint*  cs;      // [n, head_dim/2, 2] f32 holding bf16 values: (cos, sin)
    uint   n;
    uint   n_heads;
    uint   head_dim;
    uint   batch;
    uint   row_stride;
    uint   groups_per_row;
};

// torch's float -> bfloat16, round to nearest even (c10::BFloat16). No NaN
// case: a NaN here is already a failed parity run.
float bf16(float f) {
    if (kRound == 0) return f;
    uint u = asuint(f);
    u = (u + 0x7FFFu + ((u >> 16u) & 1u)) & 0xFFFF0000u;
    return asfloat(u);
}

// rope_apply's `x*cos + rotate_half(x)*sin` on bf16 tensors, one rounding per
// eager op. Rotate-half pairs element i with i + head_dim/2.
[shader("compute")]
[numthreads(WG, 1, 1)]
void rope_half_bf16(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID,
                    uniform RopeBf16Params p) {
    uint i = fold_index(gid, tid, p.groups_per_row, uint(WG));
    uint half_d = p.head_dim / 2u;
    uint per_token = p.n_heads * half_d;
    if (i >= p.batch * p.n * per_token) return;

    uint b = i / (p.n * per_token);
    uint rem = i % (p.n * per_token);
    uint tok = rem / per_token;
    uint r2 = rem % per_token;
    uint h = r2 / half_d;
    uint k = r2 % half_d;

    uint lo = (b * p.n + tok) * p.row_stride + h * p.head_dim + k;
    uint hi = lo + half_d;
    uint t = (tok * half_d + k) * 2u;
    float c = asfloat(p.cs[t]);
    float s = asfloat(p.cs[t + 1u]);
    float a = bf16(asfloat(p.x[lo]));
    float d = bf16(asfloat(p.x[hi]));
    p.x[lo] = asuint(bf16(bf16(a * c) + bf16(-d * s)));
    p.x[hi] = asuint(bf16(bf16(d * c) + bf16(a * s)));
}

struct RefineDispParams {
    float* out;    // [n, 2]: scale * (warp - grid)
    uint*  warp;   // [n, 2]
    uint*  grid;   // [n, 2], get_normalized_grid
    float  sx;
    float  sy;
    uint   n;
    uint   groups_per_row;
};

// ConvRefiner's displacement input, before disp_emb: scale_factor * (prev - grid).
[shader("compute")]
[numthreads(WG, 1, 1)]
void refine_disp(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID,
                 uniform RefineDispParams p) {
    uint i = fold_index(gid, tid, p.groups_per_row, uint(WG));
    if (i >= 2u * p.n) return;
    float d = asfloat(p.warp[i]) - asfloat(p.grid[i]);
    p.out[i] = ((i & 1u) == 0u ? p.sx : p.sy) * d;
}

struct RefineUpdateParams {
    float* warp_out;   // [n, 2]
    float* conf_out;   // [n, 4]: logit, then the precision matrix (p00, p10, p11)
    uint*  warp;       // [n, 2], the previous warp
    uint*  conf;       // [n, prev_c], the previous confidence; prev_c 1 pads with 0
    uint*  dwarp;      // [n, 2], warp_head
    uint*  dconf;      // [n, 4], confidence_head
    float  den_x;      // refine_init * W of the refiner's map
    float  den_y;
    uint   n;
    uint   prev_c;
    uint   groups_per_row;
};

float softplus20(float x) { return x > 20.0f ? x : log(1.0f + exp(x)); }

// warp = prev + d / (4 (W, H)); confidence = prev + (c0, l00^2, l00 l10,
// l10^2 + l11^2), l00 and l11 softplus + 1e-6 (refiner.py's Cholesky head).
[shader("compute")]
[numthreads(WG, 1, 1)]
void refine_update(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID,
                   uniform RefineUpdateParams p) {
    uint i = fold_index(gid, tid, p.groups_per_row, uint(WG));
    if (i >= p.n) return;
    p.warp_out[2u * i] = asfloat(p.warp[2u * i]) + asfloat(p.dwarp[2u * i]) / p.den_x;
    p.warp_out[2u * i + 1u] =
        asfloat(p.warp[2u * i + 1u]) + asfloat(p.dwarp[2u * i + 1u]) / p.den_y;
    float c0 = asfloat(p.dconf[4u * i]);
    float l00 = softplus20(asfloat(p.dconf[4u * i + 1u])) + 1e-6f;
    float l10 = asfloat(p.dconf[4u * i + 2u]);
    float l11 = softplus20(asfloat(p.dconf[4u * i + 3u])) + 1e-6f;
    float d[4] = { c0, l00 * l00, l00 * l10, l10 * l10 + l11 * l11 };
    for (uint k = 0; k < 4u; ++k) {
        float prev = k < p.prev_c ? asfloat(p.conf[p.prev_c * i + k]) : 0.0f;
        p.conf_out[4u * i + k] = prev + d[k];
    }
}
