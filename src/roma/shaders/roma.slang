// RoMa v2's model-specific kernels: the refiners' elementwise heads, and the
// matcher's bf16 RoPE. Written from the reference's behaviour (q and k are cast
// to bf16 and rounded after each eager op of x*cos + rotate_half(x)*sin, even in
// an fp32 run), not translated from its code. In fp32 instead, the coarse warp
// moves 0.26 px at the median on a 640^2 pair, 26x the parity bar.

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

// F.softplus(beta 1, threshold 20) = max(x, 0) + log1p(exp(-|x|)); Slang has no
// log1p and log(1 + t) alone loses t below 6e-8, so t < 1/8 uses the series
// (7 terms, 6e-8 relative at the edge) and above it 1 + t rounds under 5e-7.
float softplus20(float x) {
    if (x > 20.0f) return x;
    const float t = exp(-abs(x));
    const float series = t * (1.0f - t * (0.5f - t * (0.33333334f - t * (0.25f
                         - t * (0.2f - t * (0.16666667f - t * 0.14285715f))))));
    return max(x, 0.0f) + ((t < 0.125f) ? series : log(1.0f + t));
}

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
