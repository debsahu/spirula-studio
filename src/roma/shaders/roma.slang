// RoMa v2's one model-specific kernel. The multi-view transformer builds its
// RoPE in bf16 (`pos_embed_rope_dtype="bf16"`) and casts q and k to it before
// rotating, so the reference rounds them on every RoPE block even in fp32.
// Measured on a 640^2 pair: running that rotation in fp32 instead moves the
// coarse warp by 0.26 px at the median, 26x the parity bar.

#include "_common.slang"

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
