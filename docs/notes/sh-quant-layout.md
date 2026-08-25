# Packed SH storage layouts

When `sh_value_bits != 32` the canonical SH parameter store is a packed
`uint8`/`uint16` cell buffer plus a `float2` (min, max) bounds table, decoded
per cell as `mm.x + (mm.y - mm.x) * (q / kQMax)` (`QuantizedTensor<BITS, 256>`,
`core/Tensor.h`). The SH *Adam state* (`QuantizedAdamState<8, 256>`) uses the
same cell addressing over its own buffer. Gradients are never quantized --
`v_coeffs` writes stay fp32 in `engine().grad.features_sh`.

One *cell* is one `(splat, SH basis j, channel ch)` triple. A splat owns
`R = 3 * num_sh_buffer` of them, where `num_sh_buffer` is the buffer's max SH
count, not the runtime warmup degree.

Two layouts exist, and every reader picks between them with the
`(bounds_stride, pair_pitch)` pair that `_sh_load_q8` / `_sh_load_q16`
(`shaders/harmonics.slang`) and their Vulkan mirror `_sh_load_qw`
(`backend/vulkan/shaders/sh_quant.slang`) take.

## Per-cell-block AoS -- `bounds_stride = 256`, `pair_pitch = 0`

Cell index is `R * gid + 3 * j + ch`, one bound per 256 consecutive cells.
This is what the non-FPBO optimizer path
(`FusedAppearanceOptim.cu`'s per-cell Adam kernel) writes: one thread per
cell, so its 256-thread block IS the bound block.

## Per-splat-block, word-paired and transposed -- FPBO

`pair_pitch = 512`; `bounds_stride = 512 * Rw` where `Rw = (R + 1) / 2` is
the u32 words per splat. One bound per 256 splats, covering all their cells.
For splat `gid` in block `b = gid >> 8` at lane `l = gid & 255`, cell `c` is at

    idx = (b * Rw + (c >> 1)) * 512 + l * 2 + (c & 1)

i.e. a splat's cells sit two per u32 word, and the words are transposed across
the 256 splats of the block. `sh_cell_at` / `sh_fpbo_base` in
`shaders/harmonics.slang` are the only places that spell this out.

The transpose is what makes it worth having: FPBO runs one thread per splat,
so under the AoS layout a wave's load or store for one coefficient touches 32
different cache lines. Transposed, it touches two. Measured on a 5M-splat
bicycle step (RTX 4080 Super), the fused kernel went 13.8 -> 8.5 ms on CUDA
and 20.3 -> 13.1 ms on Vulkan, and the writeback no longer needs the
groupshared staging round that used to hide the strided stores.

The bounds index still falls out of `idx / bounds_stride`, because
`(c >> 1) * 512 + l * 2 + (c & 1) < Rw * 512` for every cell of the block --
so the decode path needs no separate block index.

## Allocation

The transposed layout addresses whole blocks, so both buffers are sized
`ceil(N / 256) * 256 * Rw * 2` cells rather than `N * R`: the tail block is
rounded up, and an odd `R` leaves one unused cell per splat.
