#pragma once

// The picture profile each source clip of a dataset was shot in, recorded when
// the dataset is prepared, because extracted frames carry no such tag. The
// trainer reads it to pick `--image-color-log` when the user left it on `auto`.

#include <string>
#include <vector>

namespace spirula {

// In the dataset root, beside .spirula-frames: "<mode> <code> <source>" per line.
inline constexpr const char* kDatasetColorFile = ".spirula-color";

// NotRecorded: the input carries no profile metadata at all (photos, a
// non-DJI video). Unknown: DJI metadata in a layout not verified here.
enum class ClipColor { NotRecorded, Normal, DlogM, Other, Unknown };

struct ClipColorEntry {
    ClipColor mode = ClipColor::NotRecorded;
    int code = -1;
    std::string source;   // file name only
};

struct DatasetColor {
    std::vector<ClipColorEntry> clips;
};

const char* clip_color_token(ClipColor c);
ClipColor clip_color_from_token(const std::string& token);

// Empty when the dataset has no record.
DatasetColor read_dataset_color(const std::string& dataset_dir);
// Removes a stale record when no input carries any profile metadata.
void write_dataset_color(const std::string& dataset_dir, const DatasetColor& d);

enum class DatasetColorVerdict { None, DlogM, NotLog, Unknown, Mixed };

struct DatasetColorSummary {
    DatasetColorVerdict verdict = DatasetColorVerdict::None;
    int dlogm = 0, not_log = 0, unknown = 0, unrecorded = 0;
    std::string first_unknown;   // for the line that names one
};

// Mixed is D-Log M beside anything that is not: a guess either way decodes
// some clips wrongly.
DatasetColorSummary summarize_dataset_color(const DatasetColor& d);

}  // namespace spirula
