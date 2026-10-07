// RoMa v2's local correlation: corr[p, k] = sum_c fa[p, c] / sqrt(C) * fb
// sampled at warp[p] + off[k] (bilinear, zeros, align_corners=False). Offset k
// is (ix, iy) = (k % (2r+1), k / (2r+1)), x first. The fused kernel stores no
// sample; the unfused pair (positions, nn::grid_sample_points, dot) is the
// reference's own shape, kept for parity: at stride 4 of a 1280^2 image it
// would hold 320^2 * 49 * 192 * 4 B = 3.9 GB.

#include "_common.slang"

static const int WG = 256;

struct LocalCorrParams {
    float* out;    // [n, K]
    uint*  fa;     // [n, C] f32, A's projected features
    uint*  fb;     // [h, w, C] f32, B's projected features (NOT warped: RoMaV2 #47)
    uint*  warp;   // [n, 2] f32, normalized (x, y)
    uint*  off;    // [K, 2] f32, normalized (x, y) window offsets
    uint   h;
    uint   w;
    uint   C;
    uint   K;
    float  sqrt_c;
    uint   groups_per_row;
};

// One thread per (p, k); the bilinear weights are found once per thread.
[shader("compute")]
[numthreads(WG, 1, 1)]
void local_corr_fused(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID,
                      uniform LocalCorrParams p) {
    uint i = fold_index(gid, tid, p.groups_per_row, uint(WG));
    uint n = p.h * p.w;
    if (i >= n * p.K) return;
    uint px = i / p.K;
    uint k = i % p.K;
    float gx = asfloat(p.warp[2u * px]) + asfloat(p.off[2u * k]);
    float gy = asfloat(p.warp[2u * px + 1u]) + asfloat(p.off[2u * k + 1u]);
    float sx = ((gx + 1.0f) * float(p.w) - 1.0f) * 0.5f;
    float sy = ((gy + 1.0f) * float(p.h) - 1.0f) * 0.5f;
    float acc = 0.0f;
    if (!(sy <= -1.0f || float(p.h) <= sy || sx <= -1.0f || float(p.w) <= sx)) {
        int x0 = int(floor(sx));
        int y0 = int(floor(sy));
        float lx = sx - float(x0);
        float ly = sy - float(y0);
        float wt[4] = { (1.0f - ly) * (1.0f - lx), (1.0f - ly) * lx,
                        ly * (1.0f - lx), ly * lx };
        int W = int(p.w), H = int(p.h);
        bool ok[4] = { y0 >= 0 && x0 >= 0, y0 >= 0 && x0 + 1 <= W - 1,
                       y0 + 1 <= H - 1 && x0 >= 0, y0 + 1 <= H - 1 && x0 + 1 <= W - 1 };
        uint base[4];
        base[0] = (uint(max(y0, 0)) * p.w + uint(max(x0, 0))) * p.C;
        base[1] = (uint(max(y0, 0)) * p.w + uint(max(x0 + 1, 0))) * p.C;
        base[2] = (uint(max(y0 + 1, 0)) * p.w + uint(max(x0, 0))) * p.C;
        base[3] = (uint(max(y0 + 1, 0)) * p.w + uint(max(x0 + 1, 0))) * p.C;
        uint arow = px * p.C;
        for (uint c = 0; c < p.C; ++c) {
            float v00 = ok[0] ? asfloat(p.fb[base[0] + c]) : 0.0f;
            float v01 = ok[1] ? asfloat(p.fb[base[1] + c]) : 0.0f;
            float v10 = ok[2] ? asfloat(p.fb[base[2] + c]) : 0.0f;
            float v11 = ok[3] ? asfloat(p.fb[base[3] + c]) : 0.0f;
            float s = wt[0] * v00 + wt[1] * v01 + wt[2] * v10 + wt[3] * v11;
            acc += (asfloat(p.fa[arow + c]) / p.sqrt_c) * s;
        }
    }
    p.out[i] = acc;
}

struct WindowPosParams {
    float* out;    // [n * K, 2]
    uint*  warp;   // [n, 2]
    uint*  off;    // [K, 2]
    uint   n;
    uint   K;
    uint   groups_per_row;
};

// Row p * K + k = warp[p] + off[k].
[shader("compute")]
[numthreads(WG, 1, 1)]
void local_corr_positions(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID,
                          uniform WindowPosParams p) {
    uint i = fold_index(gid, tid, p.groups_per_row, uint(WG));
    if (i >= p.n * p.K) return;
    uint px = i / p.K;
    uint k = i % p.K;
    p.out[2u * i] = asfloat(p.warp[2u * px]) + asfloat(p.off[2u * k]);
    p.out[2u * i + 1u] = asfloat(p.warp[2u * px + 1u]) + asfloat(p.off[2u * k + 1u]);
}

struct WindowDotParams {
    float* out;    // [n, K]
    uint*  fa;     // [n, C]
    uint*  s;      // [n * K, C], the sampled window
    uint   n;
    uint   K;
    uint   C;
    float  sqrt_c;
    uint   groups_per_row;
};

// out[p, k] = sum_c fa[p, c] / sqrt(C) * s[p*K + k, c].
[shader("compute")]
[numthreads(WG, 1, 1)]
void local_corr_dot(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID,
                    uniform WindowDotParams p) {
    uint i = fold_index(gid, tid, p.groups_per_row, uint(WG));
    if (i >= p.n * p.K) return;
    uint px = i / p.K;
    float acc = 0.0f;
    for (uint c = 0; c < p.C; ++c)
        acc += (asfloat(p.fa[px * p.C + c]) / p.sqrt_c) * asfloat(p.s[i * p.C + c]);
    p.out[i] = acc;
}
