#include "roma/model/Weights.h"

#include "roma/model/Rope.h"

#include "core/Env.h"
#include "nn/io/TorchPickle.h"
#include "nn/vk/Memory.h"
#include "nn/vk/Stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>

namespace roma {
namespace {

constexpr uint64_t kAlign = 256;
// One allocation per this many bytes of weights: a single 1.7 GB blob is
// past maxMemoryAllocationSize on some drivers.
constexpr uint64_t kChunk = 256ull << 20;
constexpr float kBnEps = 1e-5f;   // torchvision BatchNorm2d's default; not in the file

uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

struct Staged {
    std::string name;
    std::vector<int64_t> shape;
    bool f16 = false;
    std::function<std::vector<float>()> produce;
    int64_t numel() const {
        int64_t n = 1;
        for (int64_t d : shape) n *= d;
        return n;
    }
    uint64_t bytes() const { return (uint64_t)numel() * (f16 ? 2 : 4); }
};

const nn::TorchCheckpoint::Entry& need(const nn::TorchCheckpoint& f, const std::string& n) {
    if (!f.has(n))
        nn::fail("'%s' has no tensor '%s'; is this romav2.0.1.pt?", f.path().c_str(),
                 n.c_str());
    return f.entry(n);
}

std::vector<int64_t> shape_of(const nn::TorchCheckpoint& f, const std::string& n) {
    return need(f, n).shape;
}

void expect_shape(const nn::TorchCheckpoint& f, const std::string& n,
                  const std::vector<int64_t>& want) {
    const std::vector<int64_t> got = shape_of(f, n);
    if (got == want) return;
    auto str = [](const std::vector<int64_t>& s) {
        std::string r = "[";
        for (size_t i = 0; i < s.size(); ++i) r += (i ? ", " : "") + std::to_string(s[i]);
        return r + "]";
    };
    nn::fail("'%s': '%s' is %s, expected %s", f.path().c_str(), n.c_str(), str(got).c_str(),
             str(want).c_str());
}

float read_scalar(const nn::TorchCheckpoint& f, const std::string& n) {
    const auto& e = need(f, n);
    NN_CHECK(e.numel() == 1, "'%s': '%s' is not a scalar", f.path().c_str(), n.c_str());
    if (e.dtype == "I64") {
        const std::vector<uint8_t> raw = f.read_raw(n);
        int64_t v = 0;
        std::memcpy(&v, raw.data(), 8);
        return (float)v;
    }
    return f.read(n).data.at(0);
}

int count_blocks(const nn::TorchCheckpoint& f, const std::string& prefix) {
    int n = 0;
    while (f.has(prefix + std::to_string(n) + ".norm1.weight")) ++n;
    NN_CHECK(n > 0, "'%s' has no '%s0.norm1.weight'", f.path().c_str(), prefix.c_str());
    return n;
}

// ConvTranspose2d's [Cin, Cout, k, k] -> [Cout*k*k, Cin], row c*k*k + ky*k + kx.
std::vector<float> repack_conv_transpose(const std::vector<float>& s, int64_t cin,
                                         int64_t cout, int64_t k) {
    std::vector<float> d(s.size());
    for (int64_t ci = 0; ci < cin; ++ci)
        for (int64_t co = 0; co < cout; ++co)
            for (int64_t t = 0; t < k * k; ++t)
                d[(size_t)((co * k * k + t) * cin + ci)] = s[(size_t)((ci * cout + co) * k * k + t)];
    return d;
}

}  // namespace

bool f16_weights() {
    static const bool f32 = [] {
        const char* v = spirula::env("ROMA_F32_WEIGHTS");
        return v && v[0] && v[0] != '0';
    }();
    return !f32;
}

Weights::~Weights() {
    for (nn::DevicePtr p : blobs_) vk::device_free(p);
}

void Weights::load(const std::string& path) {
    NN_CHECK(!loaded_, "roma::Weights::load called twice");
    const nn::TorchCheckpoint file(path);
    const nn::TorchCheckpoint& f = file;
    const bool half = f16_weights();
    std::vector<Staged> staged;

    auto plain = [&](const std::string& n, bool as_f16 = false) {
        Staged s;
        s.name = n;
        s.shape = shape_of(f, n);
        s.f16 = as_f16 && half;
        s.produce = [&f, n] { return f.read(n).data; };
        staged.push_back(std::move(s));
    };

    // ---- DINOv3 ViT-L/16 ----
    {
        const auto pe = shape_of(f, "f.patch_embed.proj.weight");
        NN_CHECK(pe.size() == 4 && pe[1] == 3 && pe[2] == pe[3],
                 "'%s': f.patch_embed.proj.weight is not a square RGB patch kernel",
                 path.c_str());
        bb_.width = pe[0];
        bb_.patch = (int)pe[2];
        bb_.blocks = count_blocks(f, "f.blocks.");
        bb_periods_ = f.read("f.rope_embed.periods").data;
        // The head width is in no shape; RoPE's period table is head_dim / 4 long.
        const int64_t hd = 4 * (int64_t)bb_periods_.size();
        bb_.heads = (int)(bb_.width / hd);
        NN_CHECK(bb_.heads * hd == bb_.width, "'%s': backbone width %lld, head dim %lld",
                 path.c_str(), (long long)bb_.width, (long long)hd);
        bb_.mlp = shape_of(f, "f.blocks.0.mlp.fc1.weight")[0];
        const auto st = shape_of(f, "f.storage_tokens");
        bb_.prefix = 1 + (int)st[1];
        NN_CHECK(bb_.taps[1] < bb_.blocks, "'%s': %d blocks, tap at %d", path.c_str(),
                 bb_.blocks, bb_.taps[1]);

        plain("f.patch_embed.proj.weight");
        plain("f.patch_embed.proj.bias");
        Staged pre;
        pre.name = "f.prefix";
        pre.shape = {bb_.prefix, bb_.width};
        pre.produce = [&f] {
            std::vector<float> v = f.read("f.cls_token").data;
            const std::vector<float> s = f.read("f.storage_tokens").data;
            v.insert(v.end(), s.begin(), s.end());
            return v;
        };
        staged.push_back(std::move(pre));

        const int64_t W = bb_.width;
        const int heads = bb_.heads;
        for (int b = 0; b <= bb_.taps[1]; ++b) {
            const std::string q = "f.blocks." + std::to_string(b) + ".";
            expect_shape(f, q + "attn.qkv.weight", {3 * W, W});
            expect_shape(f, q + "attn.qkv.bias_mask", {3 * W});
            for (const char* n : {"norm1.weight", "norm1.bias", "norm2.weight",
                                  "norm2.bias", "ls1.gamma", "ls2.gamma", "attn.proj.bias",
                                  "mlp.fc1.bias", "mlp.fc2.bias"})
                plain(q + n);
            for (const char* n : {"attn.proj.weight", "mlp.fc1.weight", "mlp.fc2.weight"})
                plain(q + n, true);

            Staged w;
            w.name = q + "attn.qkv.weight";
            w.shape = {3 * W, W};
            w.f16 = half;
            w.produce = [&f, q, W, heads] {
                return permute_qk_rows(f.read(q + "attn.qkv.weight").data, W, heads, W);
            };
            staged.push_back(std::move(w));
            Staged bias;
            bias.name = q + "attn.qkv.bias";
            bias.shape = {3 * W};
            bias.produce = [&f, q, W, heads] {
                std::vector<float> v = f.read(q + "attn.qkv.bias").data;
                const std::vector<float> m = f.read(q + "attn.qkv.bias_mask").data;
                for (size_t i = 0; i < v.size(); ++i) v[i] *= m[i];
                return permute_qk_rows(v, W, heads, 1);
            };
            staged.push_back(std::move(bias));
        }
        plain("f.norm.weight");
        plain("f.norm.bias");
    }

    // ---- multi-view ViT-B ----
    {
        const std::string p = "matcher.mv_vit.";
        const auto pj = shape_of(f, p + "projector.weight");
        mt_.width = pj[0];
        mt_.in = pj[1];
        NN_CHECK(mt_.in == 2 * bb_.width, "'%s': the matcher takes %lld, two taps are %lld",
                 path.c_str(), (long long)mt_.in, (long long)(2 * bb_.width));
        mt_.out = shape_of(f, p + "output_projector.weight")[0];
        mt_.blocks = count_blocks(f, p + "blocks.");
        mt_.mlp = shape_of(f, p + "blocks.0.mlp.fc1.weight")[0];
        mt_periods_ = f.read(p + "rope_embed.periods").data;
        const int64_t hd = 4 * (int64_t)mt_periods_.size();
        mt_.heads = (int)(mt_.width / hd);
        NN_CHECK(mt_.heads * hd == mt_.width, "'%s': matcher width %lld, head dim %lld",
                 path.c_str(), (long long)mt_.width, (long long)hd);
        mt_.temp = read_scalar(f, "matcher.temp");
        mt_.scale = read_scalar(f, "matcher.scale");
        omega_ = f.read("matcher.omega").data;
        const auto om = shape_of(f, "matcher.omega");
        NN_CHECK(om.size() == 2 && om[1] == 2 && 2 * om[0] == mt_.out,
                 "'%s': omega is not [%lld, 2]", path.c_str(), (long long)(mt_.out / 2));
        mt_.pos_pairs = om[0];

        plain(p + "projector.weight");
        plain(p + "projector.bias");
        plain(p + "output_projector.weight");
        plain(p + "output_projector.bias");
        plain(p + "norm.weight");
        plain(p + "norm.bias");
        for (int b = 0; b < mt_.blocks; ++b) {
            const std::string q = p + "blocks." + std::to_string(b) + ".";
            NN_CHECK(!f.has(q + "ls1.gamma") && !f.has(q + "attn.qkv.bias_mask"),
                     "'%s': matcher block %d has LayerScale or a K-bias mask; this port "
                     "assumes neither", path.c_str(), b);
            for (const char* n : {"norm1.weight", "norm1.bias", "norm2.weight", "norm2.bias",
                                  "attn.qkv.bias", "attn.proj.bias", "mlp.fc1.bias",
                                  "mlp.fc2.bias"})
                plain(q + n);
            for (const char* n : {"attn.qkv.weight", "attn.proj.weight", "mlp.fc1.weight",
                                  "mlp.fc2.weight"})
                plain(q + n);
        }
    }

    // ---- DPT head ----
    {
        const std::string h = "matcher.head.";
        plain(h + "norm.weight");
        plain(h + "norm.bias");
        for (int i = 0; i < 4; ++i) {
            const std::string n = h + "projects." + std::to_string(i) + ".";
            const auto s = shape_of(f, n + "weight");
            NN_CHECK(s.size() == 4 && s[1] == mt_.out && s[2] == 1 && s[3] == 1,
                     "'%s': %sweight is not a 1x1 conv from %lld", path.c_str(), n.c_str(),
                     (long long)mt_.out);
            mt_.dpt_channels.push_back(s[0]);
            Staged w;
            w.name = n + "weight";
            w.shape = {s[0], s[1]};
            w.produce = [&f, n] { return f.read(n + "weight").data; };
            staged.push_back(std::move(w));
            plain(n + "bias");
        }
        const std::vector<int64_t>& c = mt_.dpt_channels;
        for (int i : {0, 1}) {
            const int64_t k = i == 0 ? 4 : 2;
            const std::string n = h + "resize_layers." + std::to_string(i) + ".";
            expect_shape(f, n + "weight", {c[i], c[i], k, k});
            Staged w;
            w.name = n + "weight";
            w.shape = {c[i] * k * k, c[i]};
            const int64_t ch = c[i];
            w.produce = [&f, n, ch, k] {
                return repack_conv_transpose(f.read(n + "weight").data, ch, ch, k);
            };
            staged.push_back(std::move(w));
            plain(n + "bias");
        }
        expect_shape(f, h + "resize_layers.3.weight", {c[3], c[3], 3, 3});
        plain(h + "resize_layers.3.weight");
        plain(h + "resize_layers.3.bias");

        mt_.dpt_features = shape_of(f, h + "scratch.layer1_rn.weight")[0];
        const int64_t F = mt_.dpt_features;
        for (int i = 0; i < 4; ++i) {
            const std::string n = h + "scratch.layer" + std::to_string(i + 1) + "_rn.weight";
            expect_shape(f, n, {F, c[i], 3, 3});
            plain(n);
        }
        for (int r = 1; r <= 4; ++r) {
            const std::string n = h + "scratch.refinenet" + std::to_string(r) + ".";
            expect_shape(f, n + "out_conv.weight", {F, F, 1, 1});
            plain(n + "out_conv.weight");
            plain(n + "out_conv.bias");
            for (int u = (r == 4 ? 2 : 1); u <= 2; ++u)
                for (const char* cv : {"conv1", "conv2"}) {
                    const std::string m =
                        n + "resConfUnit" + std::to_string(u) + "." + cv + ".";
                    expect_shape(f, m + "weight", {F, F, 3, 3});
                    plain(m + "weight");
                    plain(m + "bias");
                }
        }
        NN_CHECK(!f.has(h + "scratch.refinenet4.resConfUnit1.conv1.weight"),
                 "'%s': refinenet4 has a residual unit; this port assumes it has none",
                 path.c_str());
        for (const char* n : {"scratch.output_conv1.weight", "scratch.output_conv1.bias",
                              "scratch.output_conv2.0.weight", "scratch.output_conv2.0.bias",
                              "scratch.output_conv2.2.weight", "scratch.output_conv2.2.bias"})
            plain(h + n);
    }

    // ---- VGG19-BN, BatchNorm folded ----
    {
        const std::string p = "refiner_features.layers.";
        std::map<int, std::vector<int64_t>> convs;
        for (const std::string& n : f.names()) {
            if (n.compare(0, p.size(), p) != 0) continue;
            const size_t dot = n.find('.', p.size());
            if (dot == std::string::npos || n.compare(dot, std::string::npos, ".weight") != 0)
                continue;
            const auto s = f.entry(n).shape;
            if (s.size() == 4) convs[std::atoi(n.c_str() + p.size())] = s;
        }
        NN_CHECK(!convs.empty(), "'%s' has no '%s*.weight'", path.c_str(), p.c_str());
        std::vector<int> idx;
        for (const auto& kv : convs) idx.push_back(kv.first);
        for (size_t i = 0; i < idx.size(); ++i) {
            const int at = idx[i];
            const std::vector<int64_t> s = convs[at];
            NN_CHECK(s[2] == 3 && s[3] == 3, "'%s': VGG conv %d is not 3x3", path.c_str(), at);
            VggConv c;
            c.name = p + std::to_string(at);
            c.cin = s[1];
            c.cout = s[0];
            // [conv, bn, relu] per layer, so a MaxPool is a gap of 4; the
            // truncation ends on one, which makes the last conv a tap too.
            c.tap_after = i + 1 == idx.size() || idx[i + 1] - at == 4;
            vgg_.push_back(c);

            const std::string conv = c.name, bn = p + std::to_string(at + 1);
            for (const char* t : {".weight", ".bias", ".running_mean", ".running_var"})
                expect_shape(f, bn + t, {c.cout});
            const int64_t per = c.cin * 9, co = c.cout;
            Staged w;
            w.name = conv + ".weight";
            w.shape = s;
            w.produce = [&f, conv, bn, per, co] {
                std::vector<float> v = f.read(conv + ".weight").data;
                const std::vector<float> g = f.read(bn + ".weight").data;
                const std::vector<float> var = f.read(bn + ".running_var").data;
                for (int64_t o = 0; o < co; ++o) {
                    const float sc = g[(size_t)o] / std::sqrt(var[(size_t)o] + kBnEps);
                    for (int64_t i = 0; i < per; ++i) v[(size_t)(o * per + i)] *= sc;
                }
                return v;
            };
            staged.push_back(std::move(w));
            Staged b;
            b.name = conv + ".bias";
            b.shape = {c.cout};
            b.produce = [&f, conv, bn, co] {
                std::vector<float> v = f.read(conv + ".bias").data;
                const std::vector<float> g = f.read(bn + ".weight").data;
                const std::vector<float> beta = f.read(bn + ".bias").data;
                const std::vector<float> mean = f.read(bn + ".running_mean").data;
                const std::vector<float> var = f.read(bn + ".running_var").data;
                for (int64_t o = 0; o < co; ++o) {
                    const float sc = g[(size_t)o] / std::sqrt(var[(size_t)o] + kBnEps);
                    v[(size_t)o] = (v[(size_t)o] - mean[(size_t)o]) * sc + beta[(size_t)o];
                }
                return v;
            };
            staged.push_back(std::move(b));
        }
        int taps = 0;
        for (const VggConv& c : vgg_) taps += c.tap_after;
        NN_CHECK(taps == 3, "'%s': %d VGG taps, the refiners read 3", path.c_str(), taps);
    }

    // ---- upload, one tensor at a time so the host never holds the file ----
    std::vector<uint16_t> h16;
    uint64_t chunk_used = kChunk, chunk_size = 0;
    nn::DevicePtr chunk = 0;
    size_t next = 0;
    for (size_t i = 0; i < staged.size(); ++i) {
        const Staged& s = staged[i];
        if (align_up(chunk_used, kAlign) + s.bytes() > chunk_size) {
            uint64_t want = 0;
            for (next = i; next < staged.size(); ++next) {
                const uint64_t nb = align_up(want, kAlign) + staged[next].bytes();
                if (want > 0 && nb > kChunk) break;
                want = nb;
            }
            chunk = vk::device_alloc(want, "roma-weights");
            blobs_.push_back(chunk);
            chunk_size = want;
            chunk_used = 0;
            device_bytes_ += want;
        }
        chunk_used = align_up(chunk_used, kAlign);
        const nn::DevicePtr ptr = chunk + chunk_used;
        const std::vector<float> data = s.produce();
        NN_CHECK((int64_t)data.size() == s.numel(), "'%s': '%s' produced %zu of %lld values",
                 path.c_str(), s.name.c_str(), data.size(), (long long)s.numel());
        if (s.f16) {
            h16.resize(data.size());
            for (size_t k = 0; k < data.size(); ++k) {
                h16[k] = nn::float_to_half(data[k]);
                f16_inexact_ += nn::half_to_float(h16[k]) != data[k];
            }
            vk::Stream::get().upload(ptr, h16.data(), s.bytes());
        } else {
            vk::Stream::get().upload(ptr, data.data(), s.bytes());
        }
        chunk_used += s.bytes();

        NN_CHECK(s.shape.size() <= 4, "'%s': '%s' has rank %zu", path.c_str(), s.name.c_str(),
                 s.shape.size());
        nn::Tensor t;
        t.ptr = ptr;
        t.dtype = s.f16 ? nn::DType::F16 : nn::DType::F32;
        t.ndim = std::max<int32_t>(1, (int32_t)s.shape.size());
        for (size_t k = 0; k < s.shape.size(); ++k) t.shape[k] = s.shape[k];
        tensors_[s.name] = t;
    }
    vk::Stream::get().sync();
    path_ = path;
    loaded_ = true;
    NN_LOG_INFO("[roma] %s: DINOv3 %lldw x %d blocks (%d run), matcher %lldw x %d, "
                "%zu tensors, %.2f GB on device (%s)\n",
                path.c_str(), (long long)bb_.width, bb_.blocks, bb_.taps[1] + 1,
                (long long)mt_.width, mt_.blocks, tensors_.size(), device_bytes_ / 1e9,
                half ? "f16 transformer matrices" : "all f32");
}

nn::Tensor Weights::get(const std::string& name) const {
    auto it = tensors_.find(name);
    if (it == tensors_.end()) nn::fail("roma: no weight named '%s'", name.c_str());
    return it->second;
}

}  // namespace roma
