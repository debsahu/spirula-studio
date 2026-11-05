// Throwaway micro-benchmark: Apple GPU peak, simdgroup_matrix attention, MPSGraph SDPA/matmul.
import Metal
import MetalPerformanceShadersGraph
import Foundation

let env = ProcessInfo.processInfo.environment
let NSGv = Int(env["NSG"] ?? "8")!
let PRE = "#define NSG \(NSGv)\n#define STAGE \(env["STAGE"] ?? "0")\n"
let dev = MTLCreateSystemDefaultDevice()!
let queue = dev.makeCommandQueue()!
print("device:", dev.name)

let src = PRE + """
#include <metal_stdlib>
using namespace metal;
#define LD 33
#define QR 2
#define KLD 64

kernel void fma32(device float* out [[buffer(0)]], uint id [[thread_position_in_grid]]) {
    float a0 = id*1e-7f, a1=a0+1, a2=a0+2, a3=a0+3, a4=a0+4, a5=a0+5, a6=a0+6, a7=a0+7;
    const float b = 0.999f, c = 1e-6f;
    for (int i = 0; i < 1024; ++i) {
        a0=fma(a0,b,c); a1=fma(a1,b,c); a2=fma(a2,b,c); a3=fma(a3,b,c);
        a4=fma(a4,b,c); a5=fma(a5,b,c); a6=fma(a6,b,c); a7=fma(a7,b,c);
    }
    out[id] = a0+a1+a2+a3+a4+a5+a6+a7;
}
kernel void fma16(device half* out [[buffer(0)]], uint id [[thread_position_in_grid]]) {
    half2 a0 = half2(id*1e-3h), a1=a0+1.0h, a2=a0+2.0h, a3=a0+3.0h;
    const half2 b = half2(0.999h), c = half2(1e-3h);
    for (int i = 0; i < 1024; ++i) {
        a0=fma(a0,b,c); a1=fma(a1,b,c); a2=fma(a2,b,c); a3=fma(a3,b,c);
    }
    half2 s = a0+a1+a2+a3; out[id] = s.x + s.y;
}

template <typename T, typename A>
inline void mma_body(device float* out, uint id) {
    simdgroup_matrix<T,8,8> a(T(0.5)), b(T(0.25));
    simdgroup_matrix<A,8,8> c[8];
    for (int i = 0; i < 8; ++i) c[i] = simdgroup_matrix<A,8,8>(A(i));
    for (int it = 0; it < 512; ++it)
        for (int i = 0; i < 8; ++i) simdgroup_multiply_accumulate(c[i], a, b, c[i]);
    for (int i = 1; i < 8; ++i) c[0] = simdgroup_matrix<A,8,8>(c[0]) ;
    device A* t = (device A*)out + (id / 32) * 64;
    for (int i = 0; i < 8; ++i) simdgroup_store(c[i], t, 8);
}
kernel void mma32(device float* out [[buffer(0)]], uint id [[thread_position_in_grid]]) { mma_body<float,float>(out, id); }
kernel void mma16(device float* out [[buffer(0)]], uint id [[thread_position_in_grid]]) { mma_body<half,half>(out, id); }
kernel void mma16f(device float* out [[buffer(0)]], uint id [[thread_position_in_grid]]) { mma_body<half,float>(out, id); }

// Flash attention, one simdgroup = QR*8 queries, NSG simdgroups per threadgroup, keys in tiles of 32.
// q/k/v token-major [N, H*64] (row stride = H*64), out the same, fp32 accumulate.
template <typename T>
inline void fa_body(device const T* Q, device const T* K, device const T* V, device float* O,
                    uint N, uint stride, float scale, uint qtile, uint head, ushort sg, ushort lane,
                    threadgroup float* S, threadgroup T* P, threadgroup float* D, threadgroup T* tK, threadgroup T* tV, uint tid) {
    const uint q0 = qtile + sg * (QR*8);
    const uint hoff = head * 64;
    simdgroup_matrix<T, 8, 8> q[QR][8];
    for (int i = 0; i < QR; ++i)
        for (int d = 0; d < 8; ++d)
            simdgroup_load(q[i][d], Q + (q0 + 8*i) * stride + hoff + 8*d, stride);
    simdgroup_matrix<float, 8, 8> o[QR][8];
    for (int i = 0; i < QR; ++i) for (int d = 0; d < 8; ++d) o[i][d] = simdgroup_matrix<float,8,8>(0.0f);
    for (int e = lane; e < QR*64; e += 32) D[e] = 0.0f;
    const int row = lane >> 1, c0 = (lane & 1) * 16;   // QR==2: 16 rows, 2 lanes per row
    float m = -1e30f, l = 0.0f;
    simdgroup_barrier(mem_flags::mem_threadgroup);
    for (uint k0 = 0; k0 < N; k0 += 32) {
#if STAGE
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (uint e = tid; e < 32*64; e += NSG*32) {
            const uint r = e >> 6, c = e & 63;
            tK[r*KLD + c] = K[(k0 + r) * stride + hoff + c];
            tV[r*KLD + c] = V[(k0 + r) * stride + hoff + c];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
#define KSRC(j,d) (tK + (8*(j))*KLD + 8*(d)), KLD
#define VSRC(j,d) (tV + (8*(j))*KLD + 8*(d)), KLD
#else
#define KSRC(j,d) (K + (k0 + 8*(j)) * stride + hoff + 8*(d)), stride
#define VSRC(j,d) (V + (k0 + 8*(j)) * stride + hoff + 8*(d)), stride
#endif
        simdgroup_matrix<float, 8, 8> s[QR][4];
        for (int i = 0; i < QR; ++i) for (int j = 0; j < 4; ++j) s[i][j] = simdgroup_matrix<float,8,8>(0.0f);
        for (int d = 0; d < 8; ++d) {
            for (int j = 0; j < 4; ++j) {
                simdgroup_matrix<T, 8, 8> kt;
                simdgroup_load(kt, KSRC(j,d), ulong2(0, 0), true);
                for (int i = 0; i < QR; ++i) simdgroup_multiply_accumulate(s[i][j], q[i][d], kt, s[i][j]);
            }
        }
        for (int i = 0; i < QR; ++i) for (int j = 0; j < 4; ++j) simdgroup_store(s[i][j], S + 8*i*LD + 8*j, LD);
        simdgroup_barrier(mem_flags::mem_threadgroup);
        float mx = -1e30f;
        for (int c = 0; c < 16; ++c) mx = max(mx, S[row*LD + c0 + c]);
        mx = max(mx, simd_shuffle_xor(mx, 1)) * scale;
        mx = max(mx, m);
        const float corr = exp(m - mx);
        float sum = 0.0f;
        for (int c = 0; c < 16; ++c) { float p = exp(S[row*LD + c0 + c] * scale - mx); sum += p; P[row*LD + c0 + c] = T(p); }
        sum += simd_shuffle_xor(sum, 1);
        l = l * corr + sum; m = mx;
        if ((lane & 1) == 0) D[(row / 8) * 64 + (row % 8) * 9] = corr;
        simdgroup_barrier(mem_flags::mem_threadgroup);
        for (int i = 0; i < QR; ++i) {
            simdgroup_matrix<float, 8, 8> dg;
            simdgroup_load(dg, D + 64*i, 8);
            for (int d = 0; d < 8; ++d) simdgroup_multiply(o[i][d], dg, o[i][d]);
        }
        for (int j = 0; j < 4; ++j) {
            simdgroup_matrix<T, 8, 8> p[QR];
            for (int i = 0; i < QR; ++i) simdgroup_load(p[i], P + 8*i*LD + 8*j, LD);
            for (int d = 0; d < 8; ++d) {
                simdgroup_matrix<T, 8, 8> vv;
                simdgroup_load(vv, VSRC(j,d));
                for (int i = 0; i < QR; ++i) simdgroup_multiply_accumulate(o[i][d], p[i], vv, o[i][d]);
            }
        }
        simdgroup_barrier(mem_flags::mem_threadgroup);
    }
    if ((lane & 1) == 0) D[(row / 8) * 64 + (row % 8) * 9] = 1.0f / l;
    simdgroup_barrier(mem_flags::mem_threadgroup);
    for (int i = 0; i < QR; ++i) {
        simdgroup_matrix<float, 8, 8> dg;
        simdgroup_load(dg, D + 64*i, 8);
        for (int d = 0; d < 8; ++d) {
            simdgroup_multiply(o[i][d], dg, o[i][d]);
            simdgroup_store(o[i][d], O + (q0 + 8*i) * stride + hoff + 8*d, stride);
        }
    }
}

kernel void fa32(device const float* Q [[buffer(0)]], device const float* K [[buffer(1)]],
                 device const float* V [[buffer(2)]], device float* O [[buffer(3)]],
                 constant uint& N [[buffer(4)]], constant uint& stride [[buffer(5)]], constant float& scale [[buffer(6)]],
                 uint3 tg [[threadgroup_position_in_grid]], ushort sg [[simdgroup_index_in_threadgroup]],
                 ushort lane [[thread_index_in_simdgroup]], uint tid [[thread_index_in_threadgroup]]) {
    threadgroup float S[NSG][QR*8*LD];
    threadgroup float D[NSG][QR*64];
    threadgroup float tK[32*KLD], tV[32*KLD];
    fa_body<float>(Q, K, V, O, N, stride, scale, tg.x * QR*8 * NSG, tg.y, sg, lane, S[sg], S[sg], D[sg], tK, tV, tid);
}
kernel void fa16(device const half* Q [[buffer(0)]], device const half* K [[buffer(1)]],
                 device const half* V [[buffer(2)]], device float* O [[buffer(3)]],
                 constant uint& N [[buffer(4)]], constant uint& stride [[buffer(5)]], constant float& scale [[buffer(6)]],
                 uint3 tg [[threadgroup_position_in_grid]], ushort sg [[simdgroup_index_in_threadgroup]],
                 ushort lane [[thread_index_in_simdgroup]], uint tid [[thread_index_in_threadgroup]]) {
    threadgroup float S[NSG][QR*8*LD];
    threadgroup half P[NSG][QR*8*LD];
    threadgroup float D[NSG][QR*64];
    threadgroup half tK[32*KLD], tV[32*KLD];
    fa_body<half>(Q, K, V, O, N, stride, scale, tg.x * QR*8 * NSG, tg.y, sg, lane, S[sg], P[sg], D[sg], tK, tV, tid);
}
"""

let opts = MTLCompileOptions()
opts.mathMode = (env["FAST"] ?? "0") == "1" ? .fast : .safe
let lib: MTLLibrary
do { lib = try dev.makeLibrary(source: src, options: opts) } catch { print("compile error:", error); exit(1) }

func pso(_ n: String) -> MTLComputePipelineState { try! dev.makeComputePipelineState(function: lib.makeFunction(name: n)!) }

func timeIt(_ iters: Int, _ enc: (MTLComputeCommandEncoder) -> Void) -> Double {
    // warm up
    do { let cb = queue.makeCommandBuffer()!; let e = cb.makeComputeCommandEncoder()!; enc(e); e.endEncoding(); cb.commit(); cb.waitUntilCompleted() }
    var best = 1e9
    for _ in 0..<3 {
        let cb = queue.makeCommandBuffer()!
        let e = cb.makeComputeCommandEncoder()!
        for _ in 0..<iters { enc(e) }
        e.endEncoding(); cb.commit(); cb.waitUntilCompleted()
        if cb.status != .completed { print("CB FAILED, stopping:", cb.error as Any); exit(2) }; best = min(best, (cb.gpuEndTime - cb.gpuStartTime) / Double(iters))
    }
    return best
}

let which = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "all"

let mmaF: Double = 131072.0
let peakList: [(String, Double)] = [("fma32", 16384.0), ("fma16", 16384.0), ("mma32", mmaF), ("mma16", mmaF), ("mma16f", mmaF)]
if which == "all" || which == "peak" {
    let n = 20 * 1024 * 64
    let ob = dev.makeBuffer(length: n * 16, options: .storageModePrivate)!
    // fma*: out[id], id < n. mma*: simdgroup_store of an 8x8 tile at (id/32)*64 elements, <= 4 B each.
    precondition(ob.length >= n * 4 && ob.length >= ((n - 1) / 32 * 64 + 64) * 4, "peak output buffer too small")
    for (name, flopPerThread) in peakList {
        let p = pso(name)
        let t = timeIt(5) { e in e.setComputePipelineState(p); e.setBuffer(ob, offset: 0, index: 0)
            e.dispatchThreads(MTLSize(width: n, height: 1, depth: 1), threadsPerThreadgroup: MTLSize(width: 256, height: 1, depth: 1)) }
        print(String(format: "%@  %.2f TFLOP/s", name, Double(n) * flopPerThread / t / 1e12))
    }
}

let N = 4096, H = 8, HD = 64, stride = H * HD
let attFlop = 4.0 * Double(H) * Double(N) * Double(N) * Double(HD)

func randArr(_ count: Int, seed: UInt64) -> [Float] {
    var s = seed; var a = [Float](repeating: 0, count: count)
    for i in 0..<count { s = s &* 6364136223846793005 &+ 1442695040888963407; a[i] = Float(Int64(bitPattern: s >> 11) % 2000) / 1000.0 - 1.0 }
    return a
}
let qh = randArr(N * stride, seed: 1), kh = randArr(N * stride, seed: 2), vh = randArr(N * stride, seed: 3)

func refRow(_ qi: Int, _ h: Int) -> [Float] {
    var sc = [Double](repeating: 0, count: N); var mx = -1e30
    for j in 0..<N { var d = 0.0; for c in 0..<HD { d += Double(qh[qi*stride + h*HD + c]) * Double(kh[j*stride + h*HD + c]) }
        sc[j] = d / 8.0; mx = max(mx, sc[j]) }
    var out = [Double](repeating: 0, count: HD); var l = 0.0
    for j in 0..<N { let p = exp(sc[j] - mx); l += p; for c in 0..<HD { out[c] += p * Double(vh[j*stride + h*HD + c]) } }
    return out.map { Float($0 / l) }
}

if which == "all" || which == "fa" {
    var Nu = UInt32(N), Su = UInt32(stride), sc: Float = 0.125
    let ob = dev.makeBuffer(length: N * stride * 4, options: .storageModeShared)!
    for (name, half) in [("fa32", false), ("fa16", true)] {
        let qb, kb, vb: MTLBuffer
        if half {
            qb = dev.makeBuffer(bytes: qh.map { Float16($0) }, length: N*stride*2)!
            kb = dev.makeBuffer(bytes: kh.map { Float16($0) }, length: N*stride*2)!
            vb = dev.makeBuffer(bytes: vh.map { Float16($0) }, length: N*stride*2)!
        } else {
            qb = dev.makeBuffer(bytes: qh, length: N*stride*4)!
            kb = dev.makeBuffer(bytes: kh, length: N*stride*4)!
            vb = dev.makeBuffer(bytes: vh, length: N*stride*4)!
        }
        // Every simdgroup reads/writes rows [q0, q0+16) and keys [k0, k0+32): the grid must tile N exactly.
        precondition(N % (16 * NSGv) == 0 && N % 32 == 0, "N must tile the grid")
        precondition(qb.length >= N * stride * (half ? 2 : 4) && ob.length >= N * stride * 4, "fa buffers too small")
        let p = pso(name)
        let t = timeIt(5) { e in e.setComputePipelineState(p)
            e.setBuffer(qb, offset: 0, index: 0); e.setBuffer(kb, offset: 0, index: 1); e.setBuffer(vb, offset: 0, index: 2); e.setBuffer(ob, offset: 0, index: 3)
            e.setBytes(&Nu, length: 4, index: 4); e.setBytes(&Su, length: 4, index: 5); e.setBytes(&sc, length: 4, index: 6)
            e.dispatchThreadgroups(MTLSize(width: N / (16*NSGv), height: H, depth: 1), threadsPerThreadgroup: MTLSize(width: 32*NSGv, height: 1, depth: 1)) }
        let o = ob.contents().bindMemory(to: Float.self, capacity: N * stride)
        var maxErr: Float = 0, maxRef: Float = 0
        for (qi, h) in [(0, 0), (777, 3), (4095, 7), (2048, 5)] {
            let r = refRow(qi, h)
            for c in 0..<HD { maxErr = max(maxErr, abs(o[qi*stride + h*HD + c] - r[c])); maxRef = max(maxRef, abs(r[c])) }
        }
        print(String(format: "%@  4096x4096 h8 d64: %.3f ms  %.2f TFLOP/s  maxabs err %.2e (ref max %.2e)", name, t * 1e3, attFlop / t / 1e12, maxErr, maxRef))
    }
}

if which == "all" || which == "mps" {
    for dt in [MPSDataType.float32, MPSDataType.float16] {
        let g = MPSGraph()
        let shape: [NSNumber] = [1, NSNumber(value: H), NSNumber(value: N), NSNumber(value: HD)]
        let q = g.placeholder(shape: shape, dataType: dt, name: "q")
        let k = g.placeholder(shape: shape, dataType: dt, name: "k")
        let v = g.placeholder(shape: shape, dataType: dt, name: "v")
        let o = g.scaledDotProductAttention(query: q, key: k, value: v, scale: 0.125, name: "sdpa")
        let mA = g.placeholder(shape: [4096, 4096], dataType: dt, name: "a")
        let mB = g.placeholder(shape: [4096, 4096], dataType: dt, name: "b")
        let mm = g.matrixMultiplication(primary: mA, secondary: mB, name: "mm")
        let es = dt == .float32 ? 4 : 2
        func td(_ s: [NSNumber]) -> MPSGraphTensorData {
            let cnt = s.reduce(1) { $0 * $1.intValue }
            let b = dev.makeBuffer(length: cnt * es, options: .storageModePrivate)!
            return MPSGraphTensorData(b, shape: s, dataType: dt)
        }
        let feedsA = [q: td(shape), k: td(shape), v: td(shape)]
        let feedsM = [mA: td([4096, 4096]), mB: td([4096, 4096])]
        let gq = MPSGraphExecutable.self; _ = gq
        for (label, feeds, target, flop) in [("sdpa", feedsA, o, attFlop), ("matmul 4096^3", feedsM, mm, 2.0 * 4096 * 4096 * 4096)] {
            _ = g.run(with: queue, feeds: feeds, targetTensors: [target], targetOperations: nil)
            let iters = 8
            let t0 = Date()
            for _ in 0..<iters { _ = g.run(with: queue, feeds: feeds, targetTensors: [target], targetOperations: nil) }
            let t = Date().timeIntervalSince(t0) / Double(iters)
            print(String(format: "MPSGraph %@ %@: %.3f ms  %.2f TFLOP/s (wall, incl. encode)", dt == .float32 ? "f32" : "f16", label, t * 1e3, flop / t / 1e12))
        }
    }
}
