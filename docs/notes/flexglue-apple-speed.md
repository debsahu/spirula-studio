# FlexGlue on Apple Silicon: where the time goes and how to get it back

Consultant note, 2026-11-05. Hardware: Apple M5 Pro (20 GPU cores, Metal 4),
MoltenVK 1.4.2 statically linked. No production code was changed.

Provenance tags: **VERIFIED** = measured or read in this session. **SHOWS** =
someone else's number. **CLAIMS** = reasoning or a claim with no measurement
behind it yet.

## Verdict

1. **The fp32 ceiling on this GPU is about 2x, not 4x.** At the current parity
   bars, FlexGlue has to stay fp32. A native-Metal fp32 path (MPSGraph SDPA and
   matmul) should take a pair from about 1.05 s to about 0.45–0.55 s. That is
   where PyTorch MPS fp32 already sits (0.47–0.58 s, SHOWS). The 0.26–0.29 s
   PyTorch fp16 number comes from fp16 operands running on the M5's GPU neural
   accelerators. **PyTorch's own fp16 autocast fails 33 of 89 gated parity rows
   on `berlin` and 41 of 89 on `sacre_x_dsc`** (VERIFIED, below). So the 4x
   is only available if the operator accepts a separate fp16 tier with its own
   bars, as COLMAP does on CUDA.
2. **Ranked recommendation:**
   - (a) A Metal backend for `nn::attention` and `nn::matmul_nt` on macOS,
     through `VK_EXT_metal_objects` interop.
   - (b) Cache the per-image layer-1 self block. It is exact and saves about 5%.
   - (c) Log the covisibility score at every check layer on the next large run,
     then fit a stricter layer-2/4 stop table. Upper bound about 18% of matcher
     time across a run. It needs an operator decision, because it changes the
     reported score, though not the match set.
3. **Dead or null levers:**
   - MoltenVK fast math is already on by default, and forcing it changes ±0.9%.
   - The MoltenVK cooperative-matrix PR is WIP and 8x8-only. It maps to
     `simdgroup_matrix`, which runs at fp32 ALU rate on this GPU, not on the
     accelerators.
   - KosmicKrisp has no cooperative matrix.
   - fp16 inside our Vulkan kernels saves bandwidth, but this kernel is not
     bandwidth-bound.
   - Batching pairs does not raise attention throughput (512 workgroups already
     cover 20 cores).

**Measurement caveat.** Another job held the GPU at 100% "Device Utilization"
(ioreg) from about 17:01. The Vulkan numbers before that are clean. **Every
Metal number below was taken under that contention**, so treat it as a lower
bound on throughput. A clean rerun is item B1 in the queue at the end.

## 1. Why our attention runs at 1.65 TFLOP/s

### Peaks (Metal micro-benchmark, contended, `mtlbench peak`)

| unit | measured | how |
|---|---|---|
| fp32 FMA, scalar | 6.90–7.62 TFLOP/s (4 runs) | 8 independent FMA chains per thread |
| fp16 FMA, `half2` | 7.74–9.82 TFLOP/s | 4 `half2` chains |
| `simdgroup_matrix` 8x8, fp32 | 8.13 TFLOP/s | register-only MMA loop |
| `simdgroup_matrix` 8x8, fp16 / fp16→fp32 | 8.17 / 8.14 TFLOP/s | same |

VERIFIED: **`simdgroup_matrix` runs at the shader-ALU rate, and fp16 is no faster
than fp32.** The only path faster than ~8 TFLOP/s is the one MPSGraph uses for
fp16 (next table). The idle peak was not measured (B1). It is at least 7.6
TFLOP/s fp32.

### Our kernels versus MPSGraph on the same GPU

| op | ours (Vulkan, clean) | MPSGraph fp32 (contended) | MPSGraph fp16 (contended) |
|---|---|---|---|
| attention, d64 | 1.70–1.79 TFLOP/s (`vit global`, 5184² x16 heads, 3 runs) | **3.81 TFLOP/s** (4096² x8 heads; 9.0 ms) | **15.20 TFLOP/s** (2.26 ms) |
| GEMM 4096³ | 3.17–3.65 TFLOP/s (`square 4096`) | **6.12 TFLOP/s** | 11.26 TFLOP/s |

VERIFIED. Two conclusions:
- **MPSGraph's fp32 attention is at least 2.2x ours on the same ALUs.** That is
  about 50% of the fp32 ALU peak, against our ~22%. The gap is the kernel, not
  the hardware.
- **MPSGraph fp16 SDPA at 15.2 TFLOP/s is above every ALU and `simdgroup_matrix`
  rate measured above**, so it must run on the M5 GPU's neural accelerators. On
  the "MPS uses AMX/SME2" claim that was forwarded: AMX and SME are CPU units,
  and MPSGraph runs these ops on the GPU. The 15 TFLOP/s cannot come from the
  CPU or from the shader ALUs. The route to the accelerators is Metal 4 tensor
  ops (MetalPerformancePrimitives `matmul2d`). PyTorch main ships such kernels,
  `aten/src/ATen/native/mps/kernels/Matmul.h`, whose header says "Tuned on M5
  Pro" (VERIFIED, read via `gh api`). fp32 MPSGraph (3.8 / 6.1 TFLOP/s) stays
  under the ALU peak, which is consistent with the ALUs. Whether MPSGraph fp32
  touches the accelerators at all is not known (CLAIMS).

### What limits `flash_attn` (reading `attention.slang`)

| suspect | finding |
|---|---|
| memory | **Q is restaged from global memory on every 32-key tile.** The Q staging loop sits inside the `j0` loop (`attention.slang:173-195`), even though Q is constant across key tiles. Per workgroup-tile that is 16 KB of Q against 16 KB of K+V. Across the 27 attention ops of a pair this is ~58 GB of L2/SLC traffic (CLAIMS, by arithmetic; not profiled). |
| barriers | About 12 `GroupMemoryBarrierWithGroupSync` per 32 keys (4 chunks x 2, publish, 2–3 in the softmax, P·V) for 1024 FMAs per thread between them (VERIFIED by reading). An earlier comment in the file records the kernel as "bound by latency and occupancy", measured on NVIDIA. |
| 16 KiB rule | The kernel uses 15.1 KiB. **Apple allows 32 KiB per threadgroup.** VERIFIED from the driver's own refusal: "Threadgroup memory size (37376) exceeds the maximum threadgroup memory allowed (32768)". The rule forbids holding Q resident at BR=64, d=64, fp32 (16 KiB on its own). |
| subgroup size | Apple's SIMD width is 32 (CLAIMS, Apple docs; not queried). The kernel's lane maths assumes neither 16 nor 32. Not a suspect. |
| fp16 operands | `load_elem(..., f16)` unpacks to fp32 and all maths stays fp32 (`_common.slang`). It halves bytes, but the kernel is not bandwidth-bound, and it adds unpack ALU. That is why it measured slower (CLAIMS, consistent with the port agent's measurement and with fp16 MMA = fp32 MMA above). |
| SPIR-V→MSL | Not established. `MVK_CONFIG_SHADER_DUMP_DIR` works (VERIFIED), but it names files by SPIR-V module hash. `flash_attn` and `flash_attn_combine` share one module, so the bench's last compile (`combine`) overwrote it. The combine MSL is a plain translation: `fast::max`, `precise::tanh`, nothing pathological. Dumping `flash_attn` alone is B2. |

**My own `simdgroup_matrix` flash attention** (`mtlbench fa`, about 150 lines of
MSL, 4096² x8 x d64, correct to 4.4e-8 abs fp32 / 8.5e-6 fp16 against fp64):

| version | fp32 | fp16 operands, fp32 acc |
|---|---|---|
| 32 queries per simdgroup | 0.33 TFLOP/s | 0.62 |
| 16 queries per simdgroup (register pressure) | 0.72 | 2.89 |
| + K/V staged in threadgroup memory, 4 simdgroups | **0.80** | **3.45** |
| + `MTLMathMode.fast` | 0.80 | 3.45 |

VERIFIED, contended. fp32 `simdgroup_matrix` is **not** a shortcut to MPS's
3.8: an hour's kernel loses to our scalar Vulkan one. The fp16 variant reaches
2x our fp32 kernel without the accelerators, but fp16 fails parity (see below).

## 2. Inside Vulkan/MoltenVK

| question | answer |
|---|---|
| Fast math, `MVK_CONFIG_FAST_MATH_ENABLED` | **Default `2` (on demand)**, which means fast math unless a shader requests float controls (`MVKEnvironment.h:183-185` at `v1.4.2`, and the config doc, VERIFIED). Spirula sets nothing (grep of `src/nn/vk`, VERIFIED). Per pair, `flexglue_test --pair sacre_coeur --bench 5`, n=3, clean: **FM=0 1063.7 ms (1063.2–1064.4), FM=1 1054.6 ms (1051.3–1058.0)**. A 0.9% difference, against a 1 ms within-arm spread. FM=2 r1 was 1055.8 before contention arrived. Its later runs (1550–1587, 1261–1273) and the unset arm (1600–1626) were contended and are not comparable. Parity with FM=1: 18 checks, 0 failures, on `sacre_coeur`. **Null lever: it is already on.** |
| Cooperative matrix in MoltenVK | Open PR **KhronosGroup/MoltenVK#2753** ("WIP: Add support for VK_KHR_cooperative_matrix", created 2026-06-09, last updated 2026-07-19, not merged, not in 1.4.2). It relies on SPIRV-Cross#2596. Its comments state "MSL cooperative matrices only support 8x8 dimensions" and no spec-constant dimensions (VERIFIED, `gh pr view`). Our coop kernels are 16x16 with spec constants, so they would need an 8x8 variant. **It would map to `simdgroup_matrix`, which runs at ALU rate (8.1 TFLOP/s measured)**, and our coop kernels use fp16 operands, which fail parity. Expected gain for FlexGlue fp32: none. |
| Subgroup ops → simdgroup matrix | MoltenVK maps subgroup arithmetic and shuffles to `simd_*` intrinsics, and no subgroup op maps to `simdgroup_matrix` (CLAIMS; nothing in the 1.4.2 extension list suggests otherwise). |
| KosmicKrisp (Mesa, Vulkan-on-Metal) | Mesa main `docs/features.txt` lists `VK_KHR_cooperative_matrix` for anv, lvp, nvk, panvk, radv and vn, **not kk** (VERIFIED, fetched today). Conformant to Vulkan 1.4 as of October 2026 (SHOWS, Phoronix). It also translates to Metal, so it would hit the same simdgroup ceiling. |
| Apple-tuned scalar `flash_attn` | Hoist Q out of the key loop, use BC=64 and fewer barriers, and allow 32 KiB where the device reports it. Plausible range is 2.3–3.0 TFLOP/s, i.e. attention 0.74 → 0.45 s (CLAIMS: MPS's fp32 3.8 on the same ALUs is the evidence that it is reachable; six earlier variants failed). |

## 3. Outside Vulkan: a native Metal path

- **Interop is supported by the pinned driver.** `EXT_metal_objects` is in
  MoltenVK 1.4.2's `MVKExtensions.def:161` (VERIFIED). Whether this device
  exposes it at runtime has not been checked (B5). It exports:
  - the `MTLDevice` and the `MTLCommandQueue`;
  - the `MTLBuffer` behind a `VkDeviceMemory`;
  - the `MTLSharedEvent` behind a `VkSemaphore`.
- **Our allocator makes buffer mapping easy.** It is one `VkBuffer` plus
  dedicated memory per allocation, with an address-range map
  (`src/nn/vk/README.md`), so a `Tensor.ptr` resolves to (MTLBuffer, offset).
- **Ordering works without a CPU stall.** `Stream` already has `waitOn(semaphore,
  value)`: Vulkan signals timeline value V, the Metal command buffer encodes a
  wait on V and a signal of V+1, and the next Vulkan submit waits on V+1.
- **MPSGraph wants an offset.** `MPSGraphTensorData(MTLBuffer, shape, dtype)`
  takes no offset. The likely route is `MPSNDArray` built on a buffer with an
  offset (CLAIMS, macOS 15+; verify before building). The fallback is our own
  MSL kernels, which take `setBuffer(_:offset:)`.

**Projected per-pair time with MPSGraph fp32 for attention and GEMM**
(CLAIMS, arithmetic on VERIFIED rates):

| part | today | with MPSGraph fp32 |
|---|---|---|
| attention, 27 ops x 45.9 GFLOP | 740 ms | 27 x 9.0 ms = **243 ms** (contended rate) |
| GEMM, 734 GFLOP per pair (9 layers x 2 images x 4096 tokens x 4.98 M MAC) | ~216 ms at 3.4 TFLOP/s | **120 ms** at 6.1 |
| everything else (LN, RoPE, readout, NN, host) | ~95 ms | ~95 ms + ~5–10 ms of transitions |
| **total** | **1.05 s** | **~0.46–0.47 s** |

The fp16 version of the same table (accelerators) gives about 0.25 s. That is
exactly PyTorch's autocast number, which is a sanity check of the model.

**Parity of fp16, measured.** `compare.py check` on the reference's own
`mutant_fp16` dump (PyTorch fp16 autocast, transformer layers only):
- `berlin`: **33 of 89** gated rows failed.
- `sacre_x_dsc`: **41 of 89** failed.
- Examples: `fmatch0` rel L2 9.9e-2 against a 1e-4 bar, `mass0` 1.6e-2 against
  1e-3, `cov_layer4` 2.2e-4 against 1e-4, `argmax0` 0.78% differing against 0.2%.
- `colmap.matches0`: 0 differing on both pairs.

VERIFIED. fp16 cannot ship under the current bars. An fp16 tier is an operator
decision (COLMAP itself uses the fp16 graph on CUDA cc >= 7.0, per
`ARCHITECTURE.md`).

## 4. Algorithmic levers, at parity

| lever | exact? | saving per pair | notes |
|---|---|---|---|
| Cache layer-1 **self block** per image (QKV, attention, out_proj, FFN depend only on that image) | bit-exact | 2 x (34.4 GFLOP attention + 21.5 GFLOP GEMM) = 112 of 1974 GFLOP → **~55 ms (5%)** | 8 MB per cached image, next to the existing 4 MB feature cache. CLAIMS (arithmetic). |
| Reuse K/V across an image's pairs | n/a | **0 beyond layer 1** | Layer-1 cross mixes the two images, so every later K/V is pair-specific (`Model.cpp` crossBlock, VERIFIED by reading). |
| Batch pairs or images per dispatch | exact | ~1–2% | Attention launches 64 x 8 = 512 workgroups over 20 cores already. The win is only on small elementwise dispatches and the global barrier `Stream` puts between every recorded command. CLAIMS. |
| Fuse q, k, v into one N=1536 GEMM; fuse LN+RoPE | exact | ≤3% | Elementwise passes move ~16 MB each, about 4 ms per pair in total. CLAIMS. |
| Early-stop placement | **changes output** | see §5 | `stop_layer` is an exact parity row. |

## 5. Pair count: a stricter covisibility prepass

The final covisibility scores of a 27,157-pair production run (SHOWS, produced
by another agent; it is the score at the stop layer) show **32.0% below 0.5**.
Cumulative: <0.0039 0.41%, <0.0156 4.72%, <0.0625 13.77%, <0.25 24.59%.
Today's COLMAP thresholds then imply an average of **8.38 of 9 layers**
(CLAIMS, using the final score as a proxy for the stop-layer score).

A stop at layer 2 for every pair that ends below 0.5 would save at most:
- 7.4% x (7/9 plus the readout) for the [0.25, 0.5) pairs that run all 9 layers today;
- 10.8% x 6/9 for the pairs that stop at layer 8;
- 9.1% x 4/9 for the pairs that stop at layer 6;
- 4.3% x 2/9 for the pairs that stop at layer 4.

That totals **≤ 18% of matcher time**, which on 49k pairs at 1.05 s (14.3 h)
is **≤ 2.6 h**. The match set is unchanged as long as no pair with final ≥ 0.5
is stopped. The reported score and `stop_layer` do change. The fixtures show
how weak layer 2 is as a predictor: `sacre_coeur` scores 0.50 / 0.56 at layers
1–2 and ends at 0.98 (VERIFIED, manifests). So the realistic gain is below the
bound, and only per-layer scores logged on a real run can set the thresholds.
Reducing the pair list before FlexGlue (retrieval) is a larger lever, but it is
out of scope here.

## Ranked options

| # | option | per pair (est.) | agent-hours | risk | helps LoMa / RoMa / SAM |
|---|---|---|---|---|---|
| 1 | Metal backend for `attention` + `matmul_nt` (MPSGraph fp32, `VK_EXT_metal_objects`) | **0.46–0.55 s** | 12–20 | medium (offset tensors, event sync, MPSGraph numerics) | **yes**: every ViT attention and large GEMM on macOS |
| 2 | Layer-1 self-block cache | −55 ms (to ~1.0 s, or ~0.42 s on top of 1) | 2–4 | low | no |
| 3 | Logged per-layer scores → stricter stop table | ≤ −18% of run time | 3–6 + one run | medium, **operator decision** | no |
| 4 | Apple-tuned scalar Vulkan `flash_attn` (Q resident, 32 KiB, BC=64) | ~0.75–0.85 s | 8–16 | medium-high (6 variants already failed) | yes, portable |
| 5 | fp16 tier on the accelerators (MPSGraph fp16 / MPP `matmul2d`) | ~0.25–0.30 s | +6–10 on top of 1 | **fails current parity** (33–41 of 89 rows) | yes, if each model gets an fp16 tier |
| 6 | Fusions and batching | −1–3% | 2–4 | low | partly |
| 7 | MoltenVK fast math | 0 (±0.9%, measured) | 0 | none | — |
| 8 | MoltenVK coop-matrix PR / KosmicKrisp | 0 for fp32 | wait | — | fp16 paths only |

## Plan for options 1 and 2

### Pre-registered criteria (fixed before any implementation exists)

- **Speed, option 1.** `flexglue_test --pair sacre_coeur --bench 5`, n=3 runs per
  arm. **Pass ≤ 600 ms per pair; target ≤ 500 ms; miss > 600 ms.** Report a miss
  as a miss.
  - Floor: today's clean arms span 1051–1064 ms over 6 runs (~1%), so a
    difference above 3% counts.
  - Each run is preceded by `ioreg -r -d 1 -c IOAccelerator | grep "Device Utilization"`
    reading < 10%. Otherwise the run is void, not slow.
  - Single-variable arm: one binary, `SS_NN_METAL=0|1`, nothing else changed.
- **Speed, option 2.** At least 40 ms saved per pair on a pair whose images were
  both already described. Floor as above.
- **Parity.**
  - `flexglue_test` (all six pairs): 99 checks, 0 failures.
  - `compare.py check` on all six committed pairs: **0 failed gated rows, and
    exactly 91 gated rows per pair**. Fewer rows means a dump stage went
    missing, and that is a harness failure, not a pass.
  - Option 2 must be bit-identical on `x{0,1}_layer1` against the uncached path
    (`cmp` on the dumped `.npy`).
- **Battery per checkpoint**, not the headline alone:
  - per-pair ms, plus attention / GEMM / other ms (`SS_FLEXGLUE_PROFILE=1`);
  - the count of Metal dispatches per pair (must be 27 attention + the GEMMs; 0 means the fallback ran);
  - `stop_layer` and match count per pair;
  - peak arena use;
  - SAM 3 frame time and its own parity test, since `src/nn` is shared.
- **Harness asserts:**
  - the exported `MTLDevice` is MoltenVK's own (refuse otherwise);
  - each Metal command buffer's status is `completed` (abort on error; never retry);
  - every tensor (MTLBuffer, offset + bytes) lies inside its buffer;
  - the recorded env settings match the requested ones.
- **Falsifiers.**
  - Option 1 is dead if, idle and standalone, MPSGraph fp32 SDPA at 4096² x8 x
    d64 runs below 2.5 TFLOP/s.
  - It is also dead if its output against an fp64 reference on real layer-1
    q/k/v has rel L2 > 1e-5, or if any `compare.py` row fails that passes today.
  - Option 2 is wrong if `x0_layer1` differs by one bit.

### Steps for option 1 (runs on the Mac, GPU idle only)

1. Run go/no-go benchmarks B1, B4 and B5, below. Stop if a falsifier fires.
2. In `src/nn/vk`:
   - an ObjC++ `MetalInterop.mm`, macOS only, behind a CMake option;
   - export the device, queue and timeline-semaphore event once;
   - export each allocation's `MTLBuffer` lazily;
   - `Stream::metalSection(fn)`: flush, encode a wait, run `fn(MTLCommandBuffer)`, signal, then `waitOn`.
3. `nn::attention` (no-bias, d=64 first) and `nn::matmul_nt` (fp32 weights)
   dispatch to cached `MPSGraphExecutable`s keyed by shape when `SS_NN_METAL=1`.
   The token-major `[N, H*64]` layout goes in as a reshape+transpose inside the
   graph.
4. Measure the transition overhead. If it is above 10% of pair time, fold a
   whole transformer layer into one graph (one transition per layer, which the
   early-stop readback needs anyway).
5. Run the parity and speed battery above, then SAM 3's test and frame time.

### Steps for option 2

- `Model` keeps `x_i` after the layer-1 self block in the image cache that
  already holds the 4 MB features.
- `match()` starts at the layer-1 cross block when both images hit.
- Dump both paths and `cmp` them.

## Benchmarks still to run, with the GPU idle and after the coordinator clears it

The bench source is `docs/notes/flexglue-apple-speed/mtlbench.swift`
(`swiftc -O mtlbench.swift -o mtlbench`).

| id | command | purpose |
|---|---|---|
| B1 | `./mtlbench peak; ./mtlbench mps; NSG=4 STAGE=1 ./mtlbench fa` | uncontended peaks, MPSGraph rates and own kernel |
| B2 | `MVK_CONFIG_SHADER_DUMP_DIR=$D ./build/flexglue_test --pair sacre_coeur` | `flash_attn` MSL (this test never compiles `combine`) |
| B3 | `for fm in 0 1 2; do MVK_CONFIG_FAST_MATH_ENABLED=$fm ./build/flexglue_test --pair sacre_coeur --bench 5; done` x3, then `SS_FLEXGLUE_DUMP=$D MVK_CONFIG_FAST_MATH_ENABLED=1 ./build/flexglue_test --pair berlin` + `uv run tools/flexglue_ref/compare.py check --dump $D --pair berlin` (and `dsc`) | close the fast-math row |
| B4 | extend `mtlbench mps` to check MPSGraph fp32 SDPA against fp64 on dumped `x0_layer1` q/k/v | option 1 numerics falsifier |
| B5 | `SS_FLEXGLUE_PROFILE=1 ./build/flexglue_test --pair sacre_coeur --bench 5` | clean attention / readout split |

**Incident record.** The first `mtlbench peak` run with the `mma*` kernels
faulted the GPU ("BIF0 page fault", four `gpuEvent-mtlbench-*.ips` reports at
17:08:44). The resulting GPU restart cost another agent's SfM run a
`VK_ERROR_DEVICE_LOST`.
- **Cause:** those kernels store an 8x8 tile at `(id/32)*64` elements, up to ~8n
  bytes, into a buffer sized n x 4 bytes.
- **Fixed** in the committed copy: the buffer is 16n bytes, and `precondition`s
  check every buffer against its dispatch extents before encoding. A failed
  command buffer now exits at once instead of re-dispatching twice more.
- **Open:** the fault reports give read addresses near 0, which the OOB write
  does not fully explain. Run B1 alone, on an idle machine, first.

## Not covered

- No profiler trace (Xcode Instruments) of `flash_attn`; occupancy and register
  count are inferred.
- The idle GPU clock and peak.
- MPSGraph's fp32 numerics on real tensors.
- Whether `VK_EXT_metal_objects` is exposed at runtime on this device.
- LoMa and RoMa shapes.
- Descriptor (0.22 s per image) speed.
