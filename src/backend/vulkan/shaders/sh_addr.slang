// Packed-SH cell addressing for the Vulkan backend, mirroring
// core/Tensor.h's ShQuantAddr and harmonics.slang's sh_cell_at.
// Layouts: docs/notes/sh-quant-layout.md.

#include "int64_compat.slang"

struct ShAddrW {
    gindex_t bounds_stride;
    gindex_t pair_pitch;   // 0 = per-cell-block AoS
    uint cells_per_splat;
};

// `arg_stride` is the launcher's raw sh_bounds_stride; 0 selects FPBO.
[ForceInline]
ShAddrW sh_quant_addr_w(uint num_sh_buffer, gindex_t arg_stride) {
    uint R = 3u * num_sh_buffer;
    ShAddrW a;
    a.cells_per_splat = R;
    if (arg_stride > 0) {
        a.bounds_stride = arg_stride;
        a.pair_pitch = 0;
    } else {
        a.bounds_stride = (gindex_t)max(((R + 1u) >> 1) * 512u, 1u);
        a.pair_pitch = 512;
    }
    return a;
}

[ForceInline]
gindex_t sh_addr_base(ShAddrW a, uint gid) {
    return a.pair_pitch > 0
        ? (gindex_t)((gid >> 8) * (((a.cells_per_splat + 1u) >> 1) * 512u)
                     + (gid & 255u) * 2u)
        : (gindex_t)gid * (gindex_t)a.cells_per_splat;
}

[ForceInline]
gindex_t sh_cell_at_w(gindex_t base, int c, gindex_t pair_pitch) {
    return pair_pitch > 0
        ? base + (gindex_t)(c >> 1) * pair_pitch + (gindex_t)(c & 1)
        : base + (gindex_t)c;
}
