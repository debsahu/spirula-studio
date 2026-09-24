#include "data/DatasetColor.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace spirula {

const char* clip_color_token(ClipColor c) {
    switch (c) {
        case ClipColor::Normal:  return "normal";
        case ClipColor::DlogM:   return "dlogm";
        case ClipColor::Other:   return "other";
        case ClipColor::Unknown: return "unknown";
        default:                 return "unrecorded";
    }
}

ClipColor clip_color_from_token(const std::string& t) {
    if (t == "normal")     return ClipColor::Normal;
    if (t == "dlogm")      return ClipColor::DlogM;
    if (t == "other")      return ClipColor::Other;
    if (t == "unrecorded") return ClipColor::NotRecorded;
    return ClipColor::Unknown;
}

DatasetColor read_dataset_color(const std::string& dataset_dir) {
    DatasetColor d;
    if (dataset_dir.empty()) return d;
    std::ifstream f(fs::path(dataset_dir) / kDatasetColorFile, std::ios::binary);
    std::string line;
    while (f && std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        std::istringstream in(line);
        std::string token;
        ClipColorEntry e;
        if (!(in >> token >> e.code)) continue;
        e.mode = clip_color_from_token(token);
        std::getline(in >> std::ws, e.source);
        d.clips.push_back(e);
    }
    return d;
}

void write_dataset_color(const std::string& dataset_dir, const DatasetColor& d) {
    if (dataset_dir.empty()) return;
    const fs::path p = fs::path(dataset_dir) / kDatasetColorFile;
    bool any = false;
    for (const ClipColorEntry& e : d.clips) any = any || e.mode != ClipColor::NotRecorded;
    std::error_code ec;
    if (!any) {
        fs::remove(p, ec);
        return;
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    for (const ClipColorEntry& e : d.clips)
        f << clip_color_token(e.mode) << ' ' << e.code << ' ' << e.source << '\n';
}

DatasetColorSummary summarize_dataset_color(const DatasetColor& d) {
    DatasetColorSummary s;
    for (const ClipColorEntry& e : d.clips) {
        switch (e.mode) {
            case ClipColor::DlogM:  s.dlogm++; break;
            case ClipColor::Normal:
            case ClipColor::Other:  s.not_log++; break;
            case ClipColor::Unknown:
                if (s.unknown++ == 0) s.first_unknown = e.source;
                break;
            default: s.unrecorded++; break;
        }
    }
    const int rest = s.not_log + s.unknown + s.unrecorded;
    if (s.dlogm > 0)        s.verdict = rest > 0 ? DatasetColorVerdict::Mixed : DatasetColorVerdict::DlogM;
    else if (s.unknown > 0) s.verdict = DatasetColorVerdict::Unknown;
    else if (s.not_log > 0) s.verdict = DatasetColorVerdict::NotLog;
    return s;
}

}  // namespace spirula
