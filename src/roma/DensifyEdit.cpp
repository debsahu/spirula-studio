#include "roma/DensifyEdit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

#include "core/Sha256.h"
#include "data/Json.h"
#include "data/JsonWrite.h"
#include "roma/DensifyRun.h"
#include "roma/Publish.h"
#include "sfm/core/FixedPoses.h"

namespace roma {

namespace fs = std::filesystem;

namespace {

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() > n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void put(const fs::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), (std::streamsize)bytes.size());
    if (!f) throw std::runtime_error("cannot write " + p.string());
}

template <class T>
T take(const std::string& s, size_t& at, const char* what) {
    if (at + sizeof(T) > s.size()) throw std::runtime_error(std::string(what) + " is truncated");
    T v;
    std::memcpy(&v, s.data() + at, sizeof v);
    at += sizeof v;
    return v;
}

template <class T>
void give(std::string& s, T v) {
    s.append(reinterpret_cast<const char*>(&v), sizeof v);
}

JsonValue jstr(const std::string& v) {
    JsonValue j;
    j.type = JsonValue::Type::String;
    j.str = v;
    return j;
}

JsonValue jnum(double v) {
    JsonValue j;
    j.type = JsonValue::Type::Number;
    j.num = v;
    return j;
}

JsonValue jobj() {
    JsonValue j;
    j.type = JsonValue::Type::Object;
    return j;
}

void setKey(JsonValue& o, const std::string& key, JsonValue v) {
    for (auto& kv : o.obj)
        if (kv.first == key) {
            kv.second = std::move(v);
            return;
        }
    o.obj.emplace_back(key, std::move(v));
}

fs::path tidy(const std::string& dir) {
    fs::path p = fs::path(dir).lexically_normal();
    return p.has_filename() ? p : p.parent_path();
}

// points3D.bin without the points `keep` drops; the ids that stay.
std::string filterPoints(const std::string& src, const std::vector<uint8_t>& keep,
                         std::unordered_set<uint64_t>& ids) {
    size_t at = 0;
    const uint64_t n = take<uint64_t>(src, at, "points3D.bin");
    if (n != keep.size())
        throw std::runtime_error("the selection covers " + std::to_string(keep.size()) +
                                 " points but points3D.bin holds " + std::to_string(n));
    std::string out;
    out.reserve(src.size());
    give<uint64_t>(out, 0);
    uint64_t kept = 0;
    for (uint64_t i = 0; i < n; i++) {
        const size_t row = at;
        const uint64_t id = take<uint64_t>(src, at, "points3D.bin");
        at += 3 * 8 + 3 + 8;
        const uint64_t len = take<uint64_t>(src, at, "points3D.bin");
        if (len > (src.size() - std::min(at, src.size())) / 8) throw std::runtime_error("points3D.bin is truncated");
        at += (size_t)len * 8;
        if (at > src.size()) throw std::runtime_error("points3D.bin is truncated");
        if (!keep[(size_t)i]) continue;
        out.append(src, row, at - row);
        ids.insert(id);
        kept++;
    }
    if (at != src.size()) throw std::runtime_error("points3D.bin has trailing bytes");
    std::memcpy(&out[0], &kept, sizeof kept);
    return out;
}

std::string filterTracks(const std::string& src, const std::unordered_set<uint64_t>& ids) {
    if (src.compare(0, 4, "RTK1") != 0) throw std::runtime_error("unreadable points3D_tracks.bin");
    size_t at = 4;
    const uint64_t n = take<uint64_t>(src, at, "points3D_tracks.bin");
    std::string out = "RTK1";
    give<uint64_t>(out, 0);
    uint64_t kept = 0;
    for (uint64_t i = 0; i < n; i++) {
        const size_t row = at;
        const uint64_t id = take<uint64_t>(src, at, "points3D_tracks.bin");
        const uint32_t k = take<uint32_t>(src, at, "points3D_tracks.bin");
        at += (size_t)k * 12;
        if (at > src.size()) throw std::runtime_error("points3D_tracks.bin is truncated");
        if (!ids.count(id)) continue;
        out.append(src, row, at - row);
        kept++;
    }
    if (at != src.size()) throw std::runtime_error("points3D_tracks.bin has trailing bytes");
    if (kept != ids.size()) throw std::runtime_error("points3D_tracks.bin does not cover every point");
    std::memcpy(&out[4], &kept, sizeof kept);
    return out;
}

std::string plyHeader(uint64_t n) {
    return "ply\nformat binary_little_endian 1.0\nelement vertex " + std::to_string(n) +
           "\nproperty float x\nproperty float y\nproperty float z\nproperty float nx\n"
           "property float ny\nproperty float nz\nproperty uint point3D_id\nend_header\n";
}

std::string filterNormals(const std::string& src, const std::unordered_set<uint64_t>& ids) {
    const size_t end = src.find("end_header\n");
    const size_t el = src.find("element vertex ");
    if (end == std::string::npos || el == std::string::npos) throw std::runtime_error("unreadable " + std::string(kNormalsPly));
    const uint64_t n = std::strtoull(src.c_str() + el + 15, nullptr, 10);
    const size_t body = end + 11;
    if (src.compare(0, body, plyHeader(n)) != 0 || src.size() != body + (size_t)n * 28)
        throw std::runtime_error(std::string(kNormalsPly) + " is not the layout densify writes");
    std::string rows;
    uint64_t kept = 0;
    for (uint64_t i = 0; i < n; i++) {
        uint32_t id;
        std::memcpy(&id, src.data() + body + (size_t)i * 28 + 24, sizeof id);
        if (!ids.count(id)) continue;
        rows.append(src, body + (size_t)i * 28, 28);
        kept++;
    }
    return plyHeader(kept) + rows;
}

}  // namespace

bool isEditedModel(const std::string& model_dir) {
    return endsWith(tidy(model_dir).filename().string(), "-roma-edit");
}

std::string editedSiblingDir(const std::string& model_dir) {
    const fs::path p = tidy(model_dir);
    const std::string name = p.filename().string();
    if (endsWith(name, "-roma-edit")) return p.string();
    if (!endsWith(name, "-roma")) throw std::runtime_error(name + " is not a dense model (<m>-roma)");
    return (p.parent_path() / (name + kEditSuffix)).string();
}

std::string editProblem(const std::string& model_dir) {
    std::error_code ec;
    const fs::path p = tidy(model_dir);
    const std::string name = p.filename().string();
    if (!endsWith(name, "-roma") && !endsWith(name, "-roma-edit")) return name + " is not a dense model (<m>-roma)";
    for (const char* f : {"cameras.bin", "images.bin", "points3D.bin", "points3D_tracks.bin", "densify.json"})
        if (!fs::is_regular_file(p / f, ec)) return std::string(f) + " is missing from " + p.string();
    return {};
}

EditResult writeEditedSibling(const std::string& model_dir, const std::vector<uint8_t>& keep, bool replace) {
    if (const std::string why = editProblem(model_dir); !why.empty()) throw std::runtime_error(why);
    const fs::path src = tidy(model_dir);
    const std::string out = editedSiblingDir(src.string());
    const bool in_place = fs::path(out) == src;
    WriterLock lock(out);
    if (!in_place) {
        if (const long pid = WriterLock::holder(src.string())) throw WriterBusy(WriterLock::pathOf(src.string()), pid);
        std::error_code ec;
        if (fs::exists(out, ec) && !replace) throw std::runtime_error(out + " exists already");
    }

    const std::string points_src = slurp(src / "points3D.bin");
    std::unordered_set<uint64_t> ids;
    const std::string points = filterPoints(points_src, keep, ids);
    uint64_t before = 0, kept = ids.size();
    std::memcpy(&before, points_src.data(), sizeof before);
    if (kept == before) throw std::runtime_error("nothing was removed");
    if (kept == 0) throw std::runtime_error("no points are left; nothing written to " + out);
    const std::string tracks_src = slurp(src / "points3D_tracks.bin");
    const std::string tracks = filterTracks(tracks_src, ids);
    const bool has_normals = fs::exists(src / kNormalsPly);
    const std::string normals = has_normals ? filterNormals(slurp(src / kNormalsPly), ids) : std::string();

    JsonValue meta = json_parse_file((src / "densify.json").string());
    if (!meta.is_object()) throw std::runtime_error("densify.json is not an object");
    JsonValue first = jobj();
    if (in_place) {
        const JsonValue* from = meta.find("edited_from");
        if (!from || !from->is_object()) throw std::runtime_error("densify.json of an edit has no edited_from");
        first = *from;
    } else {
        setKey(first, "model", jstr(src.filename().string()));
        setKey(first, "points3D_sha256", jstr(spirula::sha256_file((src / "points3D.bin").string())));
        setKey(first, "tracks_sha256", jstr(spirula::sha256_file((src / "points3D_tracks.bin").string())));
        setKey(first, "points", jnum((double)before));
    }
    JsonValue edit = jobj();
    setKey(edit, "parent", jstr(src.filename().string()));
    setKey(edit, "parent_points3D_sha256", jstr(spirula::sha256_file((src / "points3D.bin").string())));
    setKey(edit, "points_removed", jnum((double)(before - kept)));
    setKey(edit, "points_kept", jnum((double)kept));

    const fs::path tmp = partialDir(out);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);
    try {
        const std::string cameras = slurp(src / "cameras.bin"), images = slurp(src / "images.bin");
        put(tmp / "cameras.bin", cameras);
        put(tmp / "images.bin", images);
        for (const char* extra : {"gauge.txt", "rigs.txt", "rigs.bin", "frames.bin"})
            if (fs::exists(src / extra)) put(tmp / extra, slurp(src / extra));
        put(tmp / "points3D.bin", points);
        put(tmp / "points3D_tracks.bin", tracks);
        if (has_normals) put(tmp / kNormalsPly, normals);

        const ReprojStats rs = reprojectWritten(tmp.string());
        JsonValue repro = jobj();
        setKey(repro, "observations", jnum((double)rs.observations));
        setKey(repro, "invalid", jnum((double)rs.invalid));
        setKey(repro, "mean_px", std::isfinite(rs.mean_px) ? jnum(rs.mean_px) : JsonValue());
        setKey(repro, "p95_px", std::isfinite(rs.p95_px) ? jnum(rs.p95_px) : JsonValue());
        setKey(meta, "points", jnum((double)kept));
        setKey(meta, "reprojection", repro);
        setKey(meta, "points3D_sha256", jstr(spirula::sha256_file((tmp / "points3D.bin").string())));
        setKey(meta, "tracks_sha256", jstr(spirula::sha256_file((tmp / "points3D_tracks.bin").string())));
        setKey(meta, "edited_from", first);
        setKey(meta, "edit", edit);
        JsonWriter w;
        json_write(w, meta);
        put(tmp / "densify.json", w.str());

        const std::string bad = sfm::checkFixedModel(tmp.string(), sfm::readFixedPoses(src.string()));
        if (!bad.empty()) throw std::runtime_error("the edited model differs from its source: " + bad);
        if (slurp(tmp / "cameras.bin") != cameras || slurp(tmp / "images.bin") != images)
            throw std::runtime_error("the edited model's cameras or images changed");
        publishDir(tmp.string(), out);
    } catch (...) {
        fs::remove_all(tmp, ec);
        throw;
    }
    return {(int64_t)before, (int64_t)kept, out};
}

}  // namespace roma
