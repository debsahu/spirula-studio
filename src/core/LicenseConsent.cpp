#include "core/LicenseConsent.h"

#include "core/LicenseTexts.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace spirula::license {
namespace {

const char* kKey = "accepted_license=";

// What a gui.conf line is, minus the line ending.
std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path, std::ios::binary);
    for (std::string s; std::getline(in, s);) {
        while (!s.empty() && s.back() == '\r') s.pop_back();
        lines.push_back(s);
    }
    return lines;
}

std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v && *v ? v : "";
}

}  // namespace

const Terms* terms_for(const std::string& family) {
    static const Terms kDinov3{
        "dinov3", "DINOv3 License Agreement (Meta)",
        "https://github.com/facebookresearch/dinov3/blob/main/LICENSE.md", kDinov3Agreement};
    static const Terms kRoma{
        "romav2", "RoMa v2 License (MIT)",
        "https://github.com/Parskatt/RoMaV2/blob/main/LICENSE", kRomaV2Mit};
    static const Terms kSam3{
        "sam3", "SAM License (Meta)",
        "https://github.com/facebookresearch/sam3/blob/main/LICENSE", kSam3License};
    static const Terms kSam2{
        "sam2", "Apache License 2.0 (SAM 2.1, Meta)",
        "https://github.com/facebookresearch/sam2/blob/main/LICENSE", kSam2Apache};
    static const Terms kGdino{
        "gdino", "Apache License 2.0 (Grounding DINO, IDEA Research)",
        "https://github.com/IDEA-Research/GroundingDINO/blob/main/LICENSE", kGdinoApache};
    static const Terms kBirefnet{
        "birefnet", "MIT License (BiRefNet)",
        "https://github.com/ZhengPeng7/BiRefNet/blob/main/LICENSE", kBirefnetMit};
    if (family == "sam3") return &kSam3;
    if (family == "sam2") return &kSam2;
    if (family == "gdino") return &kGdino;
    if (family == "birefnet") return &kBirefnet;
    if (family == "dinov3") return &kDinov3;
    if (family == "romav2") return &kRoma;
    return nullptr;
}

std::string known_families() { return "sam3, sam2, gdino, birefnet, dinov3, romav2"; }

std::string settings_path() {
#ifdef _WIN32
    fs::path dir = env_or_empty("APPDATA");
#else
    fs::path dir = env_or_empty("XDG_CONFIG_HOME");
    if (dir.empty()) {
        const std::string home = env_or_empty("HOME");
        if (!home.empty()) dir = fs::path(home) / ".config";
    }
#endif
    if (dir.empty()) return std::string();
    // An existing spirulae-splat/ from before the rename is adopted where it is.
    std::error_code ec;
    fs::path app = dir / "spirula-studio";
    if (!fs::exists(app, ec) && fs::is_directory(dir / "spirulae-splat", ec))
        app = dir / "spirulae-splat";
    return (app / "gui.conf").string();
}

std::vector<std::string> accepted_all() {
    std::vector<std::string> out;
    const std::string path = settings_path();
    if (path.empty()) return out;
    for (const std::string& l : read_lines(path))
        if (l.rfind(kKey, 0) == 0 && l.size() > std::char_traits<char>::length(kKey)) {
            const std::string f = l.substr(std::char_traits<char>::length(kKey));
            if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
        }
    return out;
}

bool accepted(const std::string& family) {
    const auto all = accepted_all();
    return std::find(all.begin(), all.end(), family) != all.end();
}

std::vector<std::string> missing(const std::string& families) {
    std::vector<std::string> out;
    std::stringstream ss(families);
    for (std::string t; std::getline(ss, t, ',');) {
        const size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        t = t.substr(a, b - a + 1);
        if (!accepted(t)) out.push_back(t);
    }
    return out;
}

std::vector<std::string> unique_families(const std::vector<std::string>& lists) {
    std::vector<std::string> out;
    for (const std::string& list : lists) {
        std::stringstream ss(list);
        for (std::string t; std::getline(ss, t, ',');) {
            const size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
            if (a == std::string::npos) continue;
            t = t.substr(a, b - a + 1);
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        }
    }
    return out;
}

bool accept_enabled(const std::string& family, bool ticked) {
    return ticked && terms_for(family) != nullptr;
}

bool record(const std::string& family) {
    if (family.empty() || settings_path().empty()) return false;
    if (accepted(family)) return true;
    const fs::path path = settings_path();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::vector<std::string> lines = read_lines(path.string());
    lines.push_back(kKey + family);
    // Written beside and renamed over, so a crash cannot leave half a settings file.
    fs::path tmp = path;
    tmp += ".tmp";
    bool written = false;
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        for (const std::string& l : lines) o << l << '\n';
        o.flush();
        written = o.good();
    }
    if (written) fs::rename(tmp, path, ec);
    if (!written || ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return accepted(family);
}

}  // namespace spirula::license
