#include "roma/WarpCache.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>

#include "core/Sha256.h"

namespace fs = std::filesystem;

namespace roma {

namespace {

// Entry, little-endian: "SPWC", byte order, version, flags, key[32], w, h, seconds (f64) and
// the two counts (u64) end the 80-byte header; then warp f32[], certainty f32[], checksum u64.
constexpr size_t kHead = 88, kTail = 8;
constexpr uint32_t kOrder = 0x01020304u;
constexpr int kMaxSide = 65536;
constexpr auto kStaleTemp = std::chrono::hours(1);

bool hexKey(const std::string& key, uint8_t out[32]) {
    if (key.size() != 64) return false;
    auto nib = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 32; i++) {
        const int hi = nib(key[2 * i]), lo = nib(key[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return true;
}

bool isEntryName(const std::string& n) {
    if (n.size() != 64 + 4 || n.compare(64, 4, ".rwc") != 0) return false;
    return std::all_of(n.begin(), n.begin() + 64, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool isTempName(const std::string& n) { return n.find(".rwc.tmp-") != std::string::npos; }

bool isShardName(const std::string& n) {
    return n.size() == 2 && std::all_of(n.begin(), n.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

template <class T>
void putAt(std::vector<uint8_t>& b, size_t at, T v) { std::memcpy(b.data() + at, &v, sizeof v); }
template <class T>
T getAt(const std::vector<uint8_t>& b, size_t at) { T v; std::memcpy(&v, b.data() + at, sizeof v); return v; }

}  // namespace

uint64_t entryChecksum(const uint8_t* p, size_t n) {
    constexpr uint64_t M = 0x9E3779B97F4A7C15ull;
    uint64_t lane[4] = {1, 2, 3, 4};
    auto step = [&](const uint8_t* q) {
        for (int l = 0; l < 4; l++) {
            uint64_t w;
            std::memcpy(&w, q + 8 * l, 8);
            lane[l] = (lane[l] ^ w) * M;
            lane[l] ^= lane[l] >> 32;
        }
    };
    size_t i = 0;
    for (; i + 32 <= n; i += 32) step(p + i);
    if (i < n) {
        uint8_t rest[32] = {};
        std::memcpy(rest, p + i, n - i);
        step(rest);
    }
    uint64_t h = n;
    for (uint64_t l : lane) {
        h = (h ^ l) * M;
        h ^= h >> 29;
    }
    return h;
}

std::vector<uint8_t> encodeEntry(const std::string& key, const Warp& w, double seconds) {
    const size_t nw = w.warp.size(), nc = w.certainty.size(), np = w.precision.size();
    std::vector<uint8_t> b(kHead + (nw + nc + np) * 4 + kTail, 0);
    std::memcpy(b.data(), "SPWC", 4);
    putAt<uint32_t>(b, 4, kOrder);
    putAt<uint32_t>(b, 8, kEntryVersion);
    putAt<uint32_t>(b, 12, 0);
    uint8_t k[32] = {};
    hexKey(key, k);
    std::memcpy(b.data() + 16, k, 32);
    putAt<int32_t>(b, 48, w.width);
    putAt<int32_t>(b, 52, w.height);
    putAt<double>(b, 56, seconds);
    putAt<uint64_t>(b, 64, nw);
    putAt<uint64_t>(b, 72, nc);
    putAt<uint64_t>(b, 80, np);
    if (nw) std::memcpy(b.data() + kHead, w.warp.data(), nw * 4);
    if (nc) std::memcpy(b.data() + kHead + nw * 4, w.certainty.data(), nc * 4);
    if (np) std::memcpy(b.data() + kHead + (nw + nc) * 4, w.precision.data(), np * 4);
    putAt<uint64_t>(b, b.size() - kTail, entryChecksum(b.data(), b.size() - kTail));
    return b;
}

bool decodeEntry(const std::vector<uint8_t>& b, const std::string& key, Warp* out, double* seconds,
                 std::string* why) {
    auto no = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (b.size() < kHead + kTail) return no("shorter than its header");
    if (std::memcmp(b.data(), "SPWC", 4) != 0) return no("not an entry");
    if (getAt<uint32_t>(b, 4) != kOrder) return no("written with another byte order");
    if (getAt<uint32_t>(b, 8) != kEntryVersion) return no("another entry version");
    if (getAt<uint32_t>(b, 12) != 0) return no("flags this build does not know");
    uint8_t want[32];
    if (!hexKey(key, want)) return no("the key is not a digest");
    if (std::memcmp(b.data() + 16, want, 32) != 0) return no("made for another key");
    const int32_t w = getAt<int32_t>(b, 48), h = getAt<int32_t>(b, 52);
    if (w < 1 || h < 1 || w > kMaxSide || h > kMaxSide) return no("size out of range");
    const double secs = getAt<double>(b, 56);
    if (!std::isfinite(secs) || secs < 0) return no("bad recorded time");
    const uint64_t nw = getAt<uint64_t>(b, 64), nc = getAt<uint64_t>(b, 72), np = getAt<uint64_t>(b, 80);
    const uint64_t px = (uint64_t)w * (uint64_t)h;
    if (nw != 2 * px || nc != px || (np != 0 && np != 3 * px)) return no("counts do not match the size");
    if (b.size() != kHead + (nw + nc + np) * 4 + kTail) return no("length does not match the counts");
    if (getAt<uint64_t>(b, b.size() - kTail) != entryChecksum(b.data(), b.size() - kTail))
        return no("checksum");
    if (out) {
        out->width = w;
        out->height = h;
        out->warp.resize(nw);
        std::memcpy(out->warp.data(), b.data() + kHead, nw * 4);
        for (float v : out->warp)
            if (!std::isfinite(v)) return no("non-finite warp");
        out->certainty.resize(nc);
        std::memcpy(out->certainty.data(), b.data() + kHead + nw * 4, nc * 4);
        for (float c : out->certainty)
            if (!(c >= 0.0f && c <= 1.0f)) return no("certainty outside [0, 1]");
        out->precision.resize(np);
        if (np) std::memcpy(out->precision.data(), b.data() + kHead + (nw + nc) * 4, np * 4);
        for (float v : out->precision)
            if (!std::isfinite(v)) return no("non-finite precision");
    }
    if (seconds) *seconds = secs;
    return true;
}

// ---- sizes, identity ------------------------------------------------------

bool parseByteSize(const std::string& t, uint64_t* out) {
    size_t i = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
    if (i == 0) return false;
    if (i < t.size() && t[i] == '.') {
        size_t j = i + 1;
        while (j < t.size() && t[j] >= '0' && t[j] <= '9') j++;
        if (j == i + 1) return false;
        i = j;
    }
    const double value = std::strtod(t.substr(0, i).c_str(), nullptr);
    std::string unit = t.substr(i);
    double mult = 1;
    if (!unit.empty()) {
        const char u = (char)std::toupper((unsigned char)unit[0]);
        const size_t at = std::string("KMGT").find(u);
        if (at != std::string::npos) {
            mult = std::ldexp(1.0, 10 * (int)(at + 1));
            unit.erase(0, 1);
        }
        for (char& c : unit) c = (char)std::toupper((unsigned char)c);
        if (!(unit.empty() || unit == "B" || unit == "IB")) return false;
    }
    const double bytes = value * mult;
    if (!(bytes >= 1.0) || bytes >= 9.2e18) return false;
    if (out) *out = (uint64_t)bytes;
    return true;
}

std::string formatByteSize(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    static const char* units[] = {"KiB", "MiB", "GiB", "TiB"};
    double v = (double)bytes / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; u++; }
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
    return buf;
}

std::string romaIdentity(const RomaSettings& s) {
    std::string out = "matcher=roma-v2\n";
    auto field = [&](const char* k, const std::string& v) {
        out += std::string(k) + "=" + std::to_string(v.size()) + ":" + v + "\n";
    };
    field("preset", s.preset);
    field("lr", std::to_string(s.lr));
    field("hr", std::to_string(s.hr));
    field("checkpoint", s.checkpoint_sha256);
    field("f16_weights", s.f16_weights ? "1" : "0");
    field("rope_rounds", s.rope_rounds ? "1" : "0");
    field("local_corr_fused", s.local_corr_fused ? "1" : "0");
    field("gemm_kernel", s.gemm_kernel);
    field("device", s.device);
    field("model_digest", s.model_digest);
    return out;
}

// ---- the store --------------------------------------------------------------

WarpCache::WarpCache(const WarpCacheOptions& o) : dir_(o.dir), budget_(o.budget_bytes) {
    std::error_code ec;
    const bool fresh = !fs::exists(dir_, ec) || fs::is_empty(dir_, ec);
    fs::create_directories(dir_, ec);
    if (ec || !fs::is_directory(dir_, ec))
        throw std::runtime_error("cannot use " + dir_ + " as a cache directory");
    // A tag in a directory that held other files could hide them from backups.
    if (fresh) {
        std::ofstream tag(fs::path(dir_) / "CACHEDIR.TAG");
        tag << "Signature: 8a477f597d28d172789f06886806bc55\n# Dense matcher output; safe to delete.\n";
    }
    scan();
    evictLocked("");
}

std::string WarpCache::entryPath(const std::string& key) const {
    return (fs::path(dir_) / key.substr(0, 2) / (key + ".rwc")).string();
}

void WarpCache::scan() {
    std::error_code ec;
    struct Found { std::string key; uint64_t bytes; fs::file_time_type when; };
    std::vector<Found> found;
    const auto now = fs::file_time_type::clock::now();
    for (const auto& shard : fs::directory_iterator(dir_, ec)) {
        if (!shard.is_directory(ec) || !isShardName(shard.path().filename().string())) continue;
        for (const auto& f : fs::directory_iterator(shard.path(), ec)) {
            const std::string name = f.path().filename().string();
            std::error_code e2;
            if (isEntryName(name)) {
                found.push_back({name.substr(0, 64), (uint64_t)f.file_size(e2), f.last_write_time(e2)});
            } else if (isTempName(name) && now - f.last_write_time(e2) > kStaleTemp) {
                fs::remove(f.path(), e2);
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        return a.when != b.when ? a.when < b.when : a.key < b.key;
    });
    for (const Found& f : found) touch(f.key, f.bytes);
}

void WarpCache::touch(const std::string& key, uint64_t bytes) {
    auto it = slots_.find(key);
    if (it != slots_.end()) {
        by_use_.erase(it->second.use);
        bytes_ -= it->second.bytes;
    }
    Slot& s = slots_[key];
    s.bytes = bytes;
    s.use = ++clock_;
    by_use_[s.use] = key;
    bytes_ += bytes;
}

Lookup WarpCache::get(const std::string& key, Warp* out, double* seconds) {
    std::lock_guard<std::mutex> lock(mu_);
    const fs::path path = entryPath(key);
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        auto it = slots_.find(key);
        if (it != slots_.end()) {
            by_use_.erase(it->second.use);
            bytes_ -= it->second.bytes;
            slots_.erase(it);
        }
        return Lookup::Miss;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    if (!decodeEntry(bytes, key, out, seconds, nullptr)) {
        std::error_code ec;
        fs::remove(path, ec);
        auto it = slots_.find(key);
        if (it != slots_.end()) {
            by_use_.erase(it->second.use);
            bytes_ -= it->second.bytes;
            slots_.erase(it);
        }
        return Lookup::Corrupt;
    }
    std::error_code ec;
    fs::last_write_time(path, fs::file_time_type::clock::now(), ec);
    touch(key, bytes.size());
    return Lookup::Hit;
}

bool WarpCache::put(const std::string& key, const Warp& w, double seconds) {
    uint8_t digest[32];
    if (!hexKey(key, digest)) return false;
    if (w.width < 1 || w.height < 1 || w.warp.size() != 2 * (size_t)w.width * w.height ||
        w.certainty.size() != (size_t)w.width * w.height)
        return false;
    for (float v : w.warp)
        if (!std::isfinite(v)) return false;
    for (float c : w.certainty)
        if (!(c >= 0.0f && c <= 1.0f)) return false;
    const std::vector<uint8_t> bytes = encodeEntry(key, w, seconds);

    static std::atomic<uint64_t> counter{0};
    static const uint64_t salt = std::random_device{}();
    const fs::path path = entryPath(key);
    const fs::path tmp = path.string() + ".tmp-" + std::to_string(salt) + "-" + std::to_string(counter++);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        f.flush();
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    touch(key, bytes.size());
    info_.written++;
    info_.written_bytes += bytes.size();
    evictLocked(key);
    return true;
}

void WarpCache::evictLocked(const std::string& keep) {
    while (bytes_ > budget_ && !by_use_.empty()) {
        const auto oldest = by_use_.begin();
        if (oldest->second == keep) break;
        const std::string key = oldest->second;
        const uint64_t size = slots_[key].bytes;
        std::error_code ec;
        fs::remove(entryPath(key), ec);
        bytes_ -= size;
        slots_.erase(key);
        by_use_.erase(oldest);
        info_.evicted++;
        info_.evicted_bytes += size;
    }
}

ClearResult WarpCache::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    ClearResult r;
    std::error_code ec;
    for (const auto& shard : fs::directory_iterator(dir_, ec)) {
        if (!shard.is_directory(ec) || !isShardName(shard.path().filename().string())) continue;
        std::vector<fs::path> doomed;
        for (const auto& f : fs::directory_iterator(shard.path(), ec)) {
            const std::string name = f.path().filename().string();
            std::error_code e2;
            if (isEntryName(name)) {
                r.entries++;
                r.bytes += (uint64_t)f.file_size(e2);
                doomed.push_back(f.path());
            } else if (isTempName(name)) {
                doomed.push_back(f.path());
            }
        }
        for (const fs::path& p : doomed) fs::remove(p, ec);
        if (fs::is_empty(shard.path(), ec)) fs::remove(shard.path(), ec);
    }
    slots_.clear();
    by_use_.clear();
    bytes_ = 0;
    return r;
}

WarpCacheInfo WarpCache::info() const {
    std::lock_guard<std::mutex> lock(mu_);
    WarpCacheInfo i = info_;
    i.entries = slots_.size();
    i.bytes = bytes_;
    return i;
}

// ---- the decorator ----------------------------------------------------------

CachedMatcher::CachedMatcher(Matcher& inner, WarpCache& cache, std::string identity)
    : inner_(inner), cache_(cache), identity_(std::move(identity)) {
    if (identity_.empty()) throw std::invalid_argument("a cached matcher needs an identity");
}

namespace {

bool usable(const MatchImage& m) { return m.rgb && m.width > 0 && m.height > 0; }

bool usable(const Warp& w) {
    const size_t px = (size_t)std::max(w.width, 0) * (size_t)std::max(w.height, 0);
    if (w.width < 1 || w.height < 1 || w.warp.size() != 2 * px || w.certainty.size() != px) return false;
    if (!w.precision.empty() && w.precision.size() != 3 * px) return false;
    for (float v : w.precision)
        if (!std::isfinite(v)) return false;
    for (float v : w.warp)
        if (!std::isfinite(v)) return false;
    for (float c : w.certainty)
        if (!(c >= 0.0f && c <= 1.0f)) return false;
    return true;
}

double since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

}  // namespace

std::string CachedMatcher::keyFor(const MatchImage& a, const MatchImage& b) const {
    spirula::Sha256 sha;
    auto feed = [&](const void* p, size_t n) { sha.update((const uint8_t*)p, n); };
    auto u64 = [&](uint64_t v) { feed(&v, 8); };
    auto i32 = [&](int32_t v) { feed(&v, 4); };
    static const char tag[] = "spirula-warp-cache-key-v1";
    feed(tag, sizeof tag);
    u64(identity_.size());
    feed(identity_.data(), identity_.size());
    i32(inner_.inputSize());
    for (const MatchImage* m : {&a, &b}) {
        i32(m->width);
        i32(m->height);
        const uint64_t n = (uint64_t)m->width * (uint64_t)m->height * 3;
        u64(n);
        feed(m->rgb, (size_t)n);
    }
    return sha.hex();
}

Warp CachedMatcher::match(const MatchImage& a, const MatchImage& b) {
    if (!usable(a) || !usable(b)) {
        stats_.uncacheable++;
        return inner_.match(a, b);
    }
    const auto start = std::chrono::steady_clock::now();
    const std::string key = keyFor(a, b);
    const double keyed = since(start);
    Warp w;
    double recorded = 0;
    const Lookup found = cache_.get(key, &w, &recorded);
    if (found == Lookup::Hit) {
        const double load = since(start);
        stats_.hits++;
        stats_.seconds_loaded += load;
        stats_.seconds_saved += recorded - load;
        return w;
    }
    if (found == Lookup::Corrupt) stats_.corrupt++;
    const auto t = std::chrono::steady_clock::now();
    w = inner_.match(a, b);
    const double took = since(t);
    stats_.seconds_matched += took;
    if (!usable(w)) {
        stats_.uncacheable++;
        return w;
    }
    stats_.misses++;
    const auto t_put = std::chrono::steady_clock::now();
    if (!cache_.put(key, w, took)) stats_.write_failed++;
    stats_.seconds_overhead += keyed + since(t_put);
    return w;
}

std::pair<Warp, Warp> CachedMatcher::matchBoth(
    const MatchImage& a, const MatchImage& b,
    const std::function<std::pair<Warp, Warp>(const MatchImage&, const MatchImage&)>& both) {
    if (!usable(a) || !usable(b)) {
        stats_.uncacheable += 2;
        return both(a, b);
    }
    const std::string kab = keyFor(a, b), kba = keyFor(b, a);
    std::pair<Warp, Warp> r;
    double rab = 0, rba = 0;
    const Lookup fab = cache_.get(kab, &r.first, &rab), fba = cache_.get(kba, &r.second, &rba);
    stats_.corrupt += (fab == Lookup::Corrupt) + (fba == Lookup::Corrupt);
    const bool hab = fab == Lookup::Hit, hba = fba == Lookup::Hit;
    stats_.hits += hab + hba;
    if (hab) stats_.seconds_saved += rab;
    if (hba) stats_.seconds_saved += rba;
    if (hab && hba) return r;
    if (hab) return {r.first, match(b, a)};
    if (hba) return {match(a, b), r.second};
    const auto t = std::chrono::steady_clock::now();
    r = both(a, b);
    const double took = since(t);
    stats_.seconds_matched += took;
    for (const auto& [key, w] : {std::pair<const std::string&, const Warp&>{kab, r.first}, {kba, r.second}}) {
        if (!usable(w)) { stats_.uncacheable++; continue; }
        stats_.misses++;
        if (!cache_.put(key, w, took / 2)) stats_.write_failed++;
    }
    return r;
}

}  // namespace roma
