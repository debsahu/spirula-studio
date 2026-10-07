// The warp cache (roma/WarpCache.h): a hit equals a miss, and nothing stale is
// ever served. Each test names the wrong implementation it exists to catch;
// docs/notes/densify.md lists the mutation runs that showed each one fails.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "roma/DensifyRun.h"
#include "roma/Synthetic.h"
#include "roma/WarpCache.h"
#include "sfm/tests/TestMain.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace roma;

namespace {

int g_fails = 0;
std::string g_test;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s: %s\n", g_test.c_str(), what.c_str());
        g_fails++;
    }
}

fs::path tempDir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("spirula_warpcache_test_") + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

std::vector<uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void spit(const fs::path& p, const std::vector<uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}

struct Img {
    std::string name;
    int w = 16, h = 16;
    std::vector<uint8_t> px;
    MatchImage view() const { return {name, w, h, px.data()}; }
};

Img makeImg(const std::string& name, uint32_t seed, int w = 16, int h = 16) {
    Img im;
    im.name = name;
    im.w = w;
    im.h = h;
    im.px.resize((size_t)w * h * 3);
    uint32_t s = seed * 2654435761u + 12345u;
    for (uint8_t& v : im.px) {
        s = s * 1664525u + 1013904223u;
        v = (uint8_t)(s >> 24);
    }
    return im;
}

// A warp that depends on every input byte, their order and their sizes, with
// values neither a half float nor a uint16 certainty can hold.
class Fake : public Matcher {
public:
    int size = 16;
    int calls = 0;
    int sleep_ms = 0;
    bool bad_warp = false, bad_cert = false;
    int inputSize() const override { return size; }
    std::string describe() const override { return "fake"; }
    Warp match(const MatchImage& a, const MatchImage& b) override {
        calls++;
        if (sleep_ms) std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        mix((uint64_t)a.width);
        mix((uint64_t)a.height);
        if (a.rgb)
            for (size_t i = 0; i < (size_t)a.width * a.height * 3; i++) mix(a.rgb[i]);
        mix(0xabcdef);
        mix((uint64_t)b.width);
        mix((uint64_t)b.height);
        if (b.rgb)
            for (size_t i = 0; i < (size_t)b.width * b.height * 3; i++) mix(b.rgb[i]);
        Warp w;
        w.width = w.height = size;
        const size_t n = (size_t)size * size;
        w.warp.resize(2 * n);
        w.certainty.resize(n);
        const double ph = (double)(h % 100000) * 1e-3;
        for (size_t i = 0; i < n; i++) {
            w.warp[2 * i] = (float)(0.99 * std::sin(0.37 * (double)i + ph));
            w.warp[2 * i + 1] = (float)(0.99 * std::cos(0.53 * (double)i + ph));
            w.certainty[i] = (float)(0.5 + 0.5 * std::sin(0.11 * (double)i + 1.3 * ph));
        }
        if (bad_warp) w.warp[3] = std::nanf("");
        if (bad_cert) w.certainty[1] = 1.5f;
        return w;
    }
};

float f16Round(float x) {
    if (x == 0 || !std::isfinite(x)) return x;
    int e;
    const float m = std::frexp(x, &e);
    return std::ldexp(std::rint(m * 2048.0f) / 2048.0f, e);
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || !std::memcmp(a.data(), b.data(), a.size() * 4));
}

WarpCacheOptions opts(const fs::path& d, uint64_t budget = kDefaultCacheBudget) {
    WarpCacheOptions o;
    o.dir = d.string();
    o.budget_bytes = budget;
    return o;
}

bool sameWarp(const Warp& a, const Warp& b) {
    return a.width == b.width && a.height == b.height && sameBits(a.warp, b.warp) &&
           sameBits(a.certainty, b.certainty);
}

uint64_t dirBytes(const fs::path& d) {
    uint64_t n = 0;
    for (const auto& e : fs::recursive_directory_iterator(d))
        if (e.is_regular_file() && e.path().extension() == ".rwc") n += e.file_size();
    return n;
}

// ===========================================================================

// Mutants: warp stored as half floats (a lossy store reads ~1e-3 here), the
// certainty as uint16 (off by up to 7.6e-6, which moved 3 of 78,695 points on a
// real run); a hit that returns anything but what the matcher returned.
void hit_equals_miss() {
    const fs::path d = tempDir("hit");
    WarpCache cache(opts(d));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    Fake ref;
    const Warp raw = ref.match(a.view(), b.view());
    double f16err = 0, u16err = 0;
    for (float v : raw.warp) f16err = std::max(f16err, (double)std::fabs(f16Round(v) - v));
    for (float c : raw.certainty) u16err = std::max(u16err, (double)std::fabs(std::rint(c * 65535.0f) / 65535.0f - c));
    check(f16err > 1e-4, "fixture cannot tell a half-float warp from a float one: " + std::to_string(f16err));
    check(u16err > 1e-6, "fixture cannot tell a uint16 certainty from a float one: " + std::to_string(u16err));

    const Warp first = cm.match(a.view(), b.view());
    const Warp second = cm.match(a.view(), b.view());
    check(fake.calls == 1, "second call matched again");
    check(sameWarp(first, raw), "miss changed the matcher's output");
    check(sameWarp(second, raw), "hit differs from the matcher's output");
    WarpCache reopened(opts(d));
    Fake other;
    CachedMatcher cm2(other, reopened, "id");
    check(sameWarp(cm2.match(a.view(), b.view()), raw) && other.calls == 0, "a hit from a reopened cache differs");
    fs::remove_all(d);
}

// Mutants: a key over the first bytes only, over A only, over the names; a
// key that ignores the last byte. The same pixels under another name must hit.
void changed_pixels_miss() {
    const fs::path d = tempDir("pix");
    WarpCache cache(opts(d));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    cm.match(a.view(), b.view());
    check(fake.calls == 1, "first call");
    Img r = a;
    r.name = "renamed";
    Img r2 = b;
    r2.name = "";
    cm.match(r.view(), r2.view());
    check(fake.calls == 1, "same pixels under other names missed");

    struct Edit { const char* what; bool inA; size_t at; };
    const Edit edits[] = {{"A first byte", true, 0}, {"A middle byte", true, 400}, {"A last byte", true, a.px.size() - 1},
                          {"B first byte", false, 0}, {"B middle byte", false, 401}, {"B last byte", false, b.px.size() - 1}};
    for (const Edit& e : edits) {
        Img ea = a, eb = b;
        (e.inA ? ea : eb).px[e.at] ^= 1;
        const int before = fake.calls;
        const Warp w = cm.match(ea.view(), eb.view());
        check(fake.calls == before + 1, std::string("stale hit after editing ") + e.what);
        Fake ref;
        check(sameWarp(w, ref.match(ea.view(), eb.view())), std::string("wrong warp after editing ") + e.what);
        cm.match(ea.view(), eb.view());
        check(fake.calls == before + 1, std::string("edited pair did not hit the second time: ") + e.what);
    }
    fs::remove_all(d);
}

// Mutants: a symmetric key; bytes alone without the sizes.
void order_and_shape_are_keyed() {
    const fs::path d = tempDir("order");
    WarpCache cache(opts(d));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    cm.match(a.view(), b.view());
    const Warp ba = cm.match(b.view(), a.view());
    check(fake.calls == 2, "B->A was served A->B's entry");
    Fake ref;
    check(sameWarp(ba, ref.match(b.view(), a.view())), "B->A wrong");
    Img tall = a;
    tall.w = 8;
    tall.h = 32;
    cm.match(tall.view(), b.view());
    check(fake.calls == 3, "the same bytes as a different shape hit");
    fs::remove_all(d);
}

// Mutants: identity left out of the key; the input size left out; a textual
// concatenation of identity and size ("a"+"12" == "a1"+"2").
void identity_and_size_are_keyed() {
    const fs::path d = tempDir("ident");
    WarpCache cache(opts(d));
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    Fake f1;
    f1.size = 12;
    CachedMatcher c1(f1, cache, "a");
    c1.match(a.view(), b.view());
    Fake f2;
    f2.size = 2;
    CachedMatcher c2(f2, cache, "a1");
    c2.match(a.view(), b.view());
    check(f2.calls == 1, "identity/size boundary collided");
    Fake f3;
    f3.size = 14;
    CachedMatcher c3(f3, cache, "a");
    c3.match(a.view(), b.view());
    check(f3.calls == 1, "another input size hit");
    Fake f4;
    f4.size = 12;
    CachedMatcher c4(f4, cache, "b");
    c4.match(a.view(), b.view());
    check(f4.calls == 1, "another identity hit");
    Fake f5;
    f5.size = 12;
    CachedMatcher c5(f5, cache, "a");
    c5.match(a.view(), b.view());
    check(f5.calls == 0, "the same identity and size missed");
    bool threw = false;
    try { CachedMatcher bad(f5, cache, ""); } catch (const std::exception&) { threw = true; }
    check(threw, "an empty identity was accepted");
    fs::remove_all(d);
}

// Mutants: any one RomaSettings field left out of the identity.
void roma_identity_covers_every_setting() {
    RomaSettings base;
    base.preset = "base";
    base.lr = 640;
    base.hr = 0;
    base.checkpoint_sha256 = "1557dec0d21b62366465f7ff4d5fdf228cc695d0582e196ad2b80e05230828b7";
    base.f16_weights = true;
    base.rope_rounds = true;
    base.local_corr_fused = true;
    base.gemm_kernel = "";
    base.device = "uuid:0123";
    base.model_digest = std::string(64, 'a');
    const std::string id0 = romaIdentity(base);
    check(!id0.empty() && id0 == romaIdentity(base), "identity is empty or unstable");
    std::vector<std::pair<const char*, RomaSettings>> v;
    auto vary = [&](const char* what, auto f) {
        RomaSettings s = base;
        f(s);
        v.push_back({what, s});
    };
    vary("preset", [](RomaSettings& s) { s.preset = "high"; });
    vary("lr", [](RomaSettings& s) { s.lr = 800; });
    vary("hr", [](RomaSettings& s) { s.hr = 960; });
    vary("checkpoint", [](RomaSettings& s) { s.checkpoint_sha256[10] = '0'; });
    vary("f16", [](RomaSettings& s) { s.f16_weights = false; });
    vary("rope", [](RomaSettings& s) { s.rope_rounds = false; });
    vary("corr", [](RomaSettings& s) { s.local_corr_fused = false; });
    vary("gemm", [](RomaSettings& s) { s.gemm_kernel = "narrow"; });
    vary("device", [](RomaSettings& s) { s.device = "uuid:0124"; });
    vary("digest", [](RomaSettings& s) { s.model_digest[63] = 'b'; });
    for (size_t i = 0; i < v.size(); i++) {
        check(romaIdentity(v[i].second) != id0, std::string("identity ignores ") + v[i].first);
        for (size_t j = i + 1; j < v.size(); j++)
            check(romaIdentity(v[i].second) != romaIdentity(v[j].second),
                  std::string("identity confuses ") + v[i].first + " and " + v[j].first);
    }
    const char* digest = modelSourceDigest();
    bool hex = digest && std::strlen(digest) == 64;
    for (size_t i = 0; hex && i < 64; i++) hex = std::isxdigit((unsigned char)digest[i]) != 0;
    check(hex, "the model source digest is not 64 hex digits");
}

// Mutants: skipping the checksum, the key check or the length check.
void damaged_entries_are_matched_again() {
    const fs::path d = tempDir("damage");
    WarpCache cache(opts(d));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    const Img a = makeImg("a", 1), b = makeImg("b", 2), c = makeImg("c", 3);
    Fake ref;
    const Warp want = ref.match(a.view(), b.view());
    cm.match(a.view(), b.view());
    cm.match(a.view(), c.view());
    const std::string key = cm.keyFor(a.view(), b.view());
    const std::string other = cm.keyFor(a.view(), c.view());
    const fs::path path = cache.entryPath(key);
    const std::vector<uint8_t> good = slurp(path), foreign = slurp(cache.entryPath(other));
    check(good.size() > 100 && good.size() == foreign.size(), "entries not as expected");

    struct Damage { const char* what; std::vector<uint8_t> bytes; };
    std::vector<Damage> ds;
    auto cut = [&](const char* what, size_t n) { ds.push_back({what, std::vector<uint8_t>(good.begin(), good.begin() + (long)n)}); };
    auto flip = [&](const char* what, size_t at) { std::vector<uint8_t> x = good; x[at] ^= 0x40; ds.push_back({what, x}); };
    cut("truncated to half", good.size() / 2);
    cut("one byte short", good.size() - 1);
    cut("header only", 80);
    cut("empty", 0);
    { std::vector<uint8_t> x = good; x.push_back(0); ds.push_back({"one byte long", x}); }
    flip("payload byte", good.size() / 2);
    flip("checksum byte", good.size() - 1);
    flip("key byte", 20);
    flip("version", 8);
    flip("width", 48);
    ds.push_back({"another key's entry", foreign});
    ds.push_back({"garbage", std::vector<uint8_t>(good.size(), 0x5a)});

    for (const Damage& dm : ds) {
        spit(path, dm.bytes);
        const int before = fake.calls;
        Warp w;
        bool threw = false;
        try { w = cm.match(a.view(), b.view()); } catch (const std::exception&) { threw = true; }
        check(!threw, std::string("threw on ") + dm.what);
        check(fake.calls == before + 1, std::string("served a damaged entry: ") + dm.what);
        check(sameWarp(w, want), std::string("wrong warp after: ") + dm.what);
        cm.match(a.view(), b.view());
        check(fake.calls == before + 1, std::string("entry not healed after: ") + dm.what);
    }
    check(cm.stats().corrupt == (int64_t)ds.size(), "corrupt count " + std::to_string(cm.stats().corrupt));
    fs::remove_all(d);
}

// Entries built to pass a checksum: only the decoder's own checks stand
// between these and the host stage. Mutants: each check removed in turn.
void decoder_refuses_valid_looking_garbage() {
    Fake fake;
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    const Warp w = fake.match(a.view(), b.view());
    const std::string key(64, 'a');
    const std::vector<uint8_t> good = encodeEntry(key, w, 0.5);
    Warp out;
    double secs = 0;
    std::string why;
    check(decodeEntry(good, key, &out, &secs, &why) && secs == 0.5 && sameWarp(out, w), "a good entry did not decode: " + why);
    check(!decodeEntry(good, std::string(64, 'b'), &out, &secs, &why), "decoded under another key");

    auto resign = [](std::vector<uint8_t> x) {
        const uint64_t sum = entryChecksum(x.data(), x.size() - 8);
        std::memcpy(x.data() + x.size() - 8, &sum, 8);
        return x;
    };
    auto put32 = [](std::vector<uint8_t> x, size_t at, uint32_t v) { std::memcpy(x.data() + at, &v, 4); return x; };
    auto put64 = [](std::vector<uint8_t> x, size_t at, uint64_t v) { std::memcpy(x.data() + at, &v, 8); return x; };
    struct Case { const char* what; std::vector<uint8_t> bytes; };
    std::vector<Case> cases;
    { Warp x = w; x.warp[5] = std::nanf(""); cases.push_back({"NaN warp", encodeEntry(key, x, 0.5)}); }
    { Warp x = w; x.warp[7] = INFINITY; cases.push_back({"infinite warp", encodeEntry(key, x, 0.5)}); }
    { Warp x = w; x.width = 0; x.height = 0; x.warp.clear(); x.certainty.clear(); cases.push_back({"zero size", encodeEntry(key, x, 0.5)}); }
    { Warp x = w; x.certainty[3] = 1.5f; cases.push_back({"certainty above one", encodeEntry(key, x, 0.5)}); }
    { Warp x = w; x.certainty[3] = -0.25f; cases.push_back({"negative certainty", encodeEntry(key, x, 0.5)}); }
    { Warp x = w; x.certainty[3] = std::nanf(""); cases.push_back({"NaN certainty", encodeEntry(key, x, 0.5)}); }
    cases.push_back({"negative seconds", encodeEntry(key, w, -1.0)});
    cases.push_back({"NaN seconds", encodeEntry(key, w, std::nan(""))});
    cases.push_back({"width past the file", resign(put32(good, 48, 0x7fffffffu))});
    cases.push_back({"negative width", resign(put32(good, 48, 0xffffffffu))});
    cases.push_back({"flags set", resign(put32(good, 12, 1))});
    cases.push_back({"other byte order", resign(put32(good, 4, 0x04030201u))});
    cases.push_back({"future version", resign(put32(good, 8, kEntryVersion + 1))});
    cases.push_back({"warp count off by one", resign(put64(good, 64, 2ull * w.width * w.height + 1))});
    cases.push_back({"certainty count off", resign(put64(good, 72, (uint64_t)w.width * w.height - 1))});
    {
        std::vector<uint8_t> longer = good, shorter = good;
        longer.insert(longer.end() - 8, 4, 0);
        shorter.erase(shorter.end() - 12, shorter.end() - 8);
        cases.push_back({"payload longer than its counts, resigned", resign(longer)});
        cases.push_back({"payload shorter than its counts, resigned", resign(shorter)});
    }
    for (const Case& c : cases) {
        check(!decodeEntry(c.bytes, key, &out, &secs, &why), std::string("decoded: ") + c.what);
        check(!why.empty(), std::string("no reason given: ") + c.what);
    }
}

// Mutants: a key that includes a counter (never hits); a cache that hits
// without recording; hits counted as misses.
void cold_then_warm_counts() {
    const fs::path d = tempDir("counts");
    const int N = 6;
    std::vector<Img> imgs;
    for (int i = 0; i < N + 1; i++) imgs.push_back(makeImg("i" + std::to_string(i), 10 + i));
    std::vector<Warp> cold;
    {
        WarpCache cache(opts(d));
        Fake fake;
        CachedMatcher cm(fake, cache, "id");
        for (int i = 0; i < N; i++) cold.push_back(cm.match(imgs[i].view(), imgs[i + 1].view()));
        check(fake.calls == N && cm.stats().misses == N && cm.stats().hits == 0, "cold counts");
        check(cache.info().entries == (uint64_t)N, "entries after cold run");
    }
    WarpCache cache(opts(d));
    check(cache.info().entries == (uint64_t)N, "a reopened cache lost its entries");
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    for (int i = 0; i < N; i++)
        check(sameWarp(cm.match(imgs[i].view(), imgs[i + 1].view()), cold[(size_t)i]), "warm differs from cold");
    check(fake.calls == 0 && cm.stats().hits == N && cm.stats().misses == 0, "warm counts");
    fs::remove_all(d);
}

// Mutants: FIFO instead of least-recently-used; no eviction; evicting the
// entry just written.
void budget_evicts_least_recently_used() {
    const fs::path d = tempDir("lru");
    Fake probe;
    const Img x = makeImg("x", 99), y = makeImg("y", 98);
    const uint64_t one = encodeEntry(std::string(64, '0'), probe.match(x.view(), y.view()), 0).size();
    std::vector<Img> imgs;
    for (int i = 0; i < 12; i++) imgs.push_back(makeImg("i" + std::to_string(i), 40 + i));
    WarpCache cache(opts(d, 3 * one + one / 2));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    auto pair = [&](int i) { return std::make_pair(imgs[(size_t)i].view(), imgs[(size_t)i + 1].view()); };
    auto run = [&](int i) { const auto p = pair(i); return cm.match(p.first, p.second); };
    auto has = [&](int i) { const auto p = pair(i); return fs::exists(cache.entryPath(cm.keyFor(p.first, p.second))); };
    run(0); run(1); run(2);
    check(has(0) && has(1) && has(2), "three entries fit");
    run(0);
    check(fake.calls == 3, "touching entry 0 matched again");
    run(3);
    check(has(0) && !has(1) && has(2) && has(3), "entry 1 was the least recently used");
    run(4);
    check(has(0) && !has(2) && has(3) && has(4), "entry 2 was the least recently used");
    check(cache.info().entries == 3, "entries " + std::to_string(cache.info().entries));
    check(dirBytes(d) <= 3 * one + one / 2 + one, "bytes on disk past budget + one entry");
    check(cache.info().evicted == 2, "evicted " + std::to_string(cache.info().evicted));
    for (int i = 5; i < 11; i++) {
        Fake ref;
        const auto p = pair(i);
        check(sameWarp(run(i), ref.match(p.first, p.second)), "wrong warp under eviction");
        check(dirBytes(d) <= 3 * one + one / 2 + one, "bytes on disk past budget + one entry");
    }
    fs::remove_all(d);

    const fs::path d3 = tempDir("lru_exact");
    WarpCache exact(opts(d3, 3 * one));
    Fake f3;
    CachedMatcher c3(f3, exact, "id");
    for (int i = 0; i < 3; i++) c3.match(imgs[(size_t)i].view(), imgs[(size_t)i + 1].view());
    check(exact.info().entries == 3 && exact.info().evicted == 0, "a budget of exactly three entries evicted one");
    fs::remove_all(d3);

    const fs::path d2 = tempDir("lru_tiny");
    WarpCache tiny(opts(d2, 10));
    Fake f2;
    CachedMatcher c2(f2, tiny, "id");
    c2.match(imgs[0].view(), imgs[1].view());
    c2.match(imgs[1].view(), imgs[2].view());
    check(tiny.info().entries == 1 && dirBytes(d2) <= 10 + one, "a budget under one entry keeps exactly the newest");
    c2.match(imgs[1].view(), imgs[2].view());
    check(f2.calls == 2, "the newest entry was evicted");
    fs::remove_all(d2);
}

// Mutants: a hit that leaves the file's time alone; a scan that orders entries
// by name; so a reopened cache would evict the wrong entry.
void recency_is_kept_on_disk() {
    const fs::path d = tempDir("recency");
    Fake probe;
    const Img x = makeImg("x", 99), y = makeImg("y", 98);
    const uint64_t one = encodeEntry(std::string(64, '0'), probe.match(x.view(), y.view()), 0).size();
    std::vector<Img> imgs;
    for (int i = 0; i < 6; i++) imgs.push_back(makeImg("i" + std::to_string(i), 70 + i));
    std::vector<std::string> keys;
    {
        WarpCache cache(opts(d));
        Fake fake;
        CachedMatcher cm(fake, cache, "id");
        for (int i = 0; i < 3; i++) {
            cm.match(imgs[(size_t)i].view(), imgs[(size_t)i + 1].view());
            keys.push_back(cm.keyFor(imgs[(size_t)i].view(), imgs[(size_t)i + 1].view()));
        }
    }
    std::vector<size_t> by_name = {0, 1, 2};
    std::sort(by_name.begin(), by_name.end(), [&](size_t a, size_t b) { return keys[a] < keys[b]; });
    // The entry that sorts first by name is the most recent, the one that sorts last the oldest.
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(WarpCache(opts(d)).entryPath(keys[by_name[0]]), now);
    fs::last_write_time(WarpCache(opts(d)).entryPath(keys[by_name[1]]), now - std::chrono::hours(2));
    fs::last_write_time(WarpCache(opts(d)).entryPath(keys[by_name[2]]), now - std::chrono::hours(5));
    WarpCache cache(opts(d, 3 * one + one / 2));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    cm.match(imgs[3].view(), imgs[4].view());
    check(!fs::exists(cache.entryPath(keys[by_name[2]])), "the entry with the oldest time survived a reopen");
    check(fs::exists(cache.entryPath(keys[by_name[0]])) && fs::exists(cache.entryPath(keys[by_name[1]])),
          "a newer entry was evicted instead");
    const fs::path hit_path = cache.entryPath(keys[by_name[1]]);
    cm.match(imgs[(size_t)by_name[1]].view(), imgs[(size_t)by_name[1] + 1].view());
    check(fake.calls == 1, "a stored pair missed");
    check(fs::file_time_type::clock::now() - fs::last_write_time(hit_path) < std::chrono::minutes(5),
          "a hit did not refresh the entry's time");
    fs::remove_all(d);
}

// Mutant: looking entries up in the index built at open instead of on disk.
void another_process_sees_new_entries() {
    const fs::path d = tempDir("two");
    WarpCache c1(opts(d)), c2(opts(d));
    Fake f1, f2;
    CachedMatcher m1(f1, c1, "id"), m2(f2, c2, "id");
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    m1.match(a.view(), b.view());
    m2.match(a.view(), b.view());
    check(f1.calls == 1 && f2.calls == 0, "the second cache did not see the first's entry");
    fs::remove_all(d);
}

// Mutant: clearing with remove_all on the directory.
void clear_removes_only_entries() {
    const fs::path d = tempDir("clear");
    WarpCache cache(opts(d));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    for (int i = 0; i < 5; i++) cm.match(makeImg("a", 1 + i).view(), makeImg("b", 20 + i).view());
    spit(d / "notes.txt", {'h', 'i'});
    const std::string key = cm.keyFor(makeImg("a", 1).view(), makeImg("b", 20).view());
    const fs::path shard = fs::path(cache.entryPath(key)).parent_path();
    spit(shard / "readme.txt", {'x'});
    spit(shard / "short.rwc", {'y'});
    const ClearResult r = cache.clear();
    check(r.entries == 5 && r.bytes > 0, "cleared " + std::to_string(r.entries) + " entries");
    check(fs::exists(d / "notes.txt") && fs::exists(shard / "readme.txt") && fs::exists(shard / "short.rwc"),
          "clear removed a file that is not an entry");
    check(!fs::exists(cache.entryPath(key)), "the entry survived");
    check(cache.info().entries == 0 && cache.info().bytes == 0, "info after clear");
    cm.match(makeImg("a", 1).view(), makeImg("b", 20).view());
    check(fake.calls == 6, "a cleared entry still hit");
    fs::remove_all(d);
}

// Mutants: writing straight to the final name (a reader sees a partial
// entry); leaving temporaries behind; reading an abandoned temporary.
void entries_appear_whole() {
    const fs::path d = tempDir("atomic");
    WarpCache cache(opts(d));
    const std::string key(64, 'c');
    Warp w;
    w.width = w.height = 1024;
    w.warp.assign(2 * 1024 * 1024, 0.25f);
    w.certainty.assign(1024 * 1024, 0.5f);
    const uint64_t whole = encodeEntry(key, w, 0).size();
    std::atomic<bool> done{false};
    std::atomic<int> partial{0}, seen{0};
    std::thread reader([&] {
        const fs::path p = cache.entryPath(key);
        while (!done) {
            std::error_code ec;
            const auto n = fs::file_size(p, ec);
            if (ec) continue;
            seen++;
            if (n != whole) partial++;
        }
    });
    const bool ok = cache.put(key, w, 0);
    done = true;
    reader.join();
    check(ok, "put failed");
    check(partial == 0, "a partial entry was visible: " + std::to_string(partial.load()) + " of " + std::to_string(seen.load()));
    int tmp = 0;
    for (const auto& e : fs::recursive_directory_iterator(d))
        if (e.path().filename().string().find(".tmp-") != std::string::npos) tmp++;
    check(tmp == 0, "temporaries left behind");

    const fs::path shard = fs::path(cache.entryPath(std::string(64, 'd'))).parent_path();
    const std::vector<uint8_t> good = encodeEntry(std::string(64, 'd'), w, 0);
    spit(shard / (std::string(64, 'd') + ".rwc.tmp-77"), std::vector<uint8_t>(good.begin(), good.begin() + 4096));
    Warp out;
    double s = 0;
    check(cache.get(std::string(64, 'd'), &out, &s) == Lookup::Miss, "an abandoned temporary was read as an entry");
    fs::remove_all(d);
}

// Mutants: abandoned temporaries never removed; a live writer's removed.
void old_temporaries_are_swept() {
    const fs::path d = tempDir("sweep");
    const fs::path shard = d / "ab";
    spit(shard / (std::string(64, 'a') + ".rwc.tmp-1"), {'o'});
    spit(shard / (std::string(64, 'b') + ".rwc.tmp-2"), {'n'});
    fs::last_write_time(shard / (std::string(64, 'a') + ".rwc.tmp-1"), fs::file_time_type::clock::now() - std::chrono::hours(3));
    WarpCache cache(opts(d));
    check(!fs::exists(shard / (std::string(64, 'a') + ".rwc.tmp-1")), "an old temporary stayed");
    check(fs::exists(shard / (std::string(64, 'b') + ".rwc.tmp-2")), "a fresh temporary (a live writer's) was removed");
    check(cache.info().entries == 0, "temporaries counted as entries");
    fs::remove_all(d);
}

// Mutant: put throwing, or the warp lost, when the disk refuses the write.
void unwritable_cache_still_matches() {
#ifndef _WIN32
    if (geteuid() == 0) return;
    const fs::path d = tempDir("ro");
    WarpCache cache(opts(d));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    chmod(d.c_str(), 0500);
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    Fake ref;
    Warp w;
    bool threw = false;
    try { w = cm.match(a.view(), b.view()); } catch (const std::exception&) { threw = true; }
    chmod(d.c_str(), 0700);
    check(!threw, "match threw when the cache could not be written");
    check(sameWarp(w, ref.match(a.view(), b.view())), "wrong warp when the write failed");
    check(cm.stats().write_failed == 1, "write failure count " + std::to_string(cm.stats().write_failed));
    fs::remove_all(d);
#endif
}

// Mutants: caching a non-finite warp or an out-of-range certainty; an empty
// image reaching the key.
void unusable_output_is_never_stored() {
    const fs::path d = tempDir("bad");
    WarpCache cache(opts(d));
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    for (int mode = 0; mode < 3; mode++) {
        Fake fake;
        fake.bad_warp = mode == 0;
        fake.bad_cert = mode == 1;
        CachedMatcher cm(fake, cache, "id" + std::to_string(mode));
        MatchImage empty{"e", 0, 0, nullptr};
        const Warp w = mode == 2 ? cm.match(empty, b.view()) : cm.match(a.view(), b.view());
        if (mode == 2) cm.match(empty, b.view()); else cm.match(a.view(), b.view());
        check(fake.calls == 2, "mode " + std::to_string(mode) + ": stored what it should not have");
        check(cm.stats().uncacheable == 2 && cm.stats().hits == 0, "mode " + std::to_string(mode) + ": uncacheable count");
        if (mode == 0) check(std::isnan(w.warp[3]), "the NaN was hidden from the host stage");
        if (mode == 1) check(w.certainty[1] == 1.5f, "the bad certainty was clamped instead of passed on");
    }
    Fake fake;
    fake.bad_warp = true;
    const Warp nan_warp = fake.match(a.view(), b.view());
    const std::string key(64, 'e');
    check(!cache.put(key, nan_warp, 0.0) && !fs::exists(cache.entryPath(key)), "put stored a non-finite warp");
    Warp wrong_shape;
    wrong_shape.width = wrong_shape.height = 4;
    check(!cache.put(key, wrong_shape, 0.0) && !fs::exists(cache.entryPath(key)), "put stored a warp whose arrays do not fit its size");
    check(cache.info().entries == 0, "entries written for unusable output");
    fs::remove_all(d);
}

// Mutants: seconds_saved left at 0; the hit's own time used instead of the
// entry's recorded matcher time.
void time_saved_is_the_recorded_match_time() {
    const fs::path d = tempDir("time");
    const int N = 4;
    double matched = 0;
    {
        WarpCache cache(opts(d));
        Fake fake;
        fake.sleep_ms = 15;
        CachedMatcher cm(fake, cache, "id");
        for (int i = 0; i < N; i++) cm.match(makeImg("a", i).view(), makeImg("b", i + 50).view());
        matched = cm.stats().seconds_matched;
        check(cm.stats().seconds_overhead > 0, "writing and keying cost nothing");
        check(matched >= N * 0.015, "matched seconds " + std::to_string(matched));
    }
    WarpCache cache(opts(d));
    Fake fake;
    CachedMatcher cm(fake, cache, "id");
    for (int i = 0; i < N; i++) cm.match(makeImg("a", i).view(), makeImg("b", i + 50).view());
    const CachedStats& s = cm.stats();
    check(s.hits == N && s.seconds_loaded >= 0, "warm run");
    check(std::fabs(s.seconds_saved + s.seconds_loaded - matched) < 1e-9,
          "saved " + std::to_string(s.seconds_saved) + " + loaded " + std::to_string(s.seconds_loaded) + " != matched " + std::to_string(matched));
    check(s.seconds_saved > N * 0.010, "saved seconds " + std::to_string(s.seconds_saved));
    fs::remove_all(d);
}

// Mutant: a hit's time leaving out the hashing of the images, which on a 1500 px
// pair is the larger part of it and would overstate what the cache saved.
void hit_time_includes_keying() {
    const fs::path d = tempDir("keytime");
    const Img a = makeImg("a", 1, 1500, 1500), b = makeImg("b", 2, 1500, 1500);
    WarpCache cache(opts(d));
    Fake fake;
    fake.size = 8;
    CachedMatcher cm(fake, cache, "id");
    cm.match(a.view(), b.view());
    check(cm.stats().seconds_overhead > 0.01, "keying 13.5 MB took " + std::to_string(cm.stats().seconds_overhead) + " s");
    cm.match(a.view(), b.view());
    check(cm.stats().hits == 1 && cm.stats().seconds_loaded > 0.01, "a hit cost " + std::to_string(cm.stats().seconds_loaded) + " s");
    fs::remove_all(d);
}

// Mutants: half-float warp or uint16 certainty (the size changes); a header
// that does not account for the trailer.
void entry_size_is_the_format() {
    const fs::path d = tempDir("size");
    WarpCache cache(opts(d));
    Fake fake;
    fake.size = 64;
    CachedMatcher cm(fake, cache, "id");
    const Img a = makeImg("a", 1), b = makeImg("b", 2);
    cm.match(a.view(), b.view());
    const uint64_t n = 64 * 64;
    check(fs::file_size(cache.entryPath(cm.keyFor(a.view(), b.view()))) == 80 + n * 8 + n * 4 + 8,
          "entry is not 80 + 8 B warp + 4 B certainty per pixel + 8");
    check(cache.info().bytes == 80 + n * 8 + n * 4 + 8, "info bytes");
    fs::remove_all(d);
}

// Mutants: units misread; zero accepted; trailing junk accepted.
void byte_sizes_parse() {
    struct C { const char* text; bool ok; uint64_t v; };
    const C cs[] = {{"123", true, 123}, {"1k", true, 1024}, {"1K", true, 1024}, {"2KiB", true, 2048},
                    {"3MB", true, 3ull << 20}, {"16G", true, 16ull << 30}, {"16GiB", true, 16ull << 30},
                    {"1.5G", true, 3ull << 29}, {"1T", true, 1ull << 40}, {"0", false, 0}, {"0G", false, 0},
                    {"", false, 0}, {"G", false, 0}, {"-1G", false, 0}, {"12x", false, 0}, {"1 G", false, 0},
                    {"1e3", false, 0}, {"99999999999T", false, 0}, {"nan", false, 0}};
    for (const C& c : cs) {
        uint64_t v = 7;
        const bool ok = parseByteSize(c.text, &v);
        check(ok == c.ok, std::string("parse '") + c.text + "' " + (ok ? "accepted" : "refused"));
        if (ok && c.ok) check(v == c.v, std::string("parse '") + c.text + "' = " + std::to_string(v));
    }
    check(formatByteSize(16ull << 30) == "16.0 GiB" && formatByteSize(500) == "500 B", "format " + formatByteSize(16ull << 30));
}

// ---- the whole stage --------------------------------------------------------

struct Counting : Matcher {
    Matcher* inner = nullptr;
    int calls = 0;
    int inputSize() const override { return inner->inputSize(); }
    Warp match(const MatchImage& a, const MatchImage& b) override { calls++; return inner->match(a, b); }
    std::string describe() const override { return inner->describe(); }
};

struct StageRun {
    std::vector<DensePoint> cloud;
    std::string points_bin, tracks_bin;
    int calls = 0;
    CachedStats cs;
};

std::string slurpStr(const fs::path& p) {
    const std::vector<uint8_t> b = slurp(p);
    return std::string(b.begin(), b.end());
}

// Mutants: a cache that changes what the stage computes (a lossy store, a
// miss that returns raw data); one that does not hit across a threshold change.
void stage_output_is_unchanged_and_filters_reuse_matches() {
    const fs::path d = tempDir("stage");
    writeStairDataset(stairScene(), d.string(), 96, 192, 400);
    const Scene scene = stairScene();
    auto run = [&](WarpCache* cache, double reproj, const char* out) {
        DensifyJob job;
        job.model_dir = (d / "sparse" / "0").string();
        job.out_dir = (d / "sparse" / out).string();
        job.image_path = [&](const std::string& n) { return (d / "images" / n).string(); };
        job.opt.refs = 4;
        job.opt.seed = 7;
        job.opt.reproj_px = reproj;
        Counting counting;
        struct Sized : Matcher {
            int inputSize() const override { return 64; }
            Warp match(const MatchImage&, const MatchImage&) override { return {}; }
            std::string describe() const override { return ""; }
        } sized;
        job.matcher = &sized;
        const DensifyPlan pl = planDensify(job);
        OracleMatcher om(&scene, OracleMatcher::independentViews(pl.images), 64, 0.1, 0.03, 3);
        counting.inner = &om;
        std::unique_ptr<CachedMatcher> cm;
        if (cache) {
            cm = std::make_unique<CachedMatcher>(counting, *cache, "oracle:64:0.1:0.03:3");
            job.matcher = cm.get();
        } else {
            job.matcher = &counting;
        }
        StageRun r;
        const DensifyResult res = runDensify(job, pl, nullptr);
        r.cloud = res.cloud;
        r.calls = counting.calls;
        if (cm) r.cs = cm->stats();
        writeSibling(job.model_dir, job.out_dir, pl, res.cloud, "{}\n");
        r.points_bin = slurpStr(fs::path(job.out_dir) / "points3D.bin");
        r.tracks_bin = slurpStr(fs::path(job.out_dir) / "points3D_tracks.bin");
        return r;
    };
    auto same = [](const StageRun& a, const StageRun& b) {
        return !a.cloud.empty() && a.cloud.size() == b.cloud.size() && a.points_bin == b.points_bin &&
               a.tracks_bin == b.tracks_bin && !a.points_bin.empty() && !a.tracks_bin.empty();
    };
    const StageRun plain = run(nullptr, 1.0, "plain");
    WarpCache cache(opts(d / "cache"));
    const StageRun cold = run(&cache, 1.0, "cold");
    const StageRun warm = run(&cache, 1.0, "warm");
    // Faces that are entirely masked are the same bytes in several pairs, so a cold run can hit.
    check(plain.calls > 0 && cold.calls == cold.cs.misses && cold.cs.hits + cold.cs.misses == plain.calls,
          "cold run matched " + std::to_string(cold.calls) + " of " + std::to_string(plain.calls));
    check(cold.cs.misses > plain.calls / 2, "cold run hit most of its pairs");
    check(warm.calls == 0 && warm.cs.hits == plain.calls && warm.cs.misses == 0, "warm run matched again");
    check(same(plain, cold), "caching changed the output of a cold run");
    check(same(cold, warm), "a warm run differs from a cold one");

    const StageRun tight_plain = run(nullptr, 0.4, "tight_plain");
    const StageRun tight_warm = run(&cache, 0.4, "tight_warm");
    check(tight_warm.calls == 0 && tight_warm.cs.hits == plain.calls, "a threshold change matched again");
    check(same(tight_plain, tight_warm), "a threshold change on a warm cache differs from a plain run");
    check(!same(plain, tight_plain), "the threshold change changed nothing: the test cannot tell reuse from a stale cloud");
    fs::remove_all(d);
}

}  // namespace

static int body(int argc, char** argv) {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"hit_equals_miss", hit_equals_miss},
        {"changed_pixels_miss", changed_pixels_miss},
        {"order_and_shape_are_keyed", order_and_shape_are_keyed},
        {"identity_and_size_are_keyed", identity_and_size_are_keyed},
        {"roma_identity_covers_every_setting", roma_identity_covers_every_setting},
        {"damaged_entries_are_matched_again", damaged_entries_are_matched_again},
        {"decoder_refuses_valid_looking_garbage", decoder_refuses_valid_looking_garbage},
        {"cold_then_warm_counts", cold_then_warm_counts},
        {"budget_evicts_least_recently_used", budget_evicts_least_recently_used},
        {"recency_is_kept_on_disk", recency_is_kept_on_disk},
        {"another_process_sees_new_entries", another_process_sees_new_entries},
        {"clear_removes_only_entries", clear_removes_only_entries},
        {"entries_appear_whole", entries_appear_whole},
        {"old_temporaries_are_swept", old_temporaries_are_swept},
        {"unwritable_cache_still_matches", unwritable_cache_still_matches},
        {"unusable_output_is_never_stored", unusable_output_is_never_stored},
        {"time_saved_is_the_recorded_match_time", time_saved_is_the_recorded_match_time},
        {"hit_time_includes_keying", hit_time_includes_keying},
        {"entry_size_is_the_format", entry_size_is_the_format},
        {"byte_sizes_parse", byte_sizes_parse},
        {"stage_output_is_unchanged_and_filters_reuse_matches", stage_output_is_unchanged_and_filters_reuse_matches},
    };
    int ran = 0;
    for (const auto& t : tests) {
        if (argc > 1 && std::strcmp(argv[1], t.first) != 0) continue;
        g_test = t.first;
        const int before = g_fails;
        t.second();
        std::printf("%s %s\n", g_fails == before ? "ok  " : "FAIL", t.first);
        ran++;
    }
    if (!ran) { std::printf("FAIL: no test named %s\n", argv[1]); return 1; }
    return g_fails ? 1 : 0;
}

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
