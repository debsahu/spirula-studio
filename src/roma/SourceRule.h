#pragma once
// What `--source auto` becomes: the one rule, header-only so the GUI states the
// same thing the tool does. The matches are the default; the depth maps stand in
// only when no matcher can run, and with neither it is the matches (which then
// fail loudly rather than quietly produce nothing).

namespace roma {

enum class AutoSource { Roma, Depth };

constexpr AutoSource autoSource(bool matcher_available, bool depth_maps) {
    return matcher_available ? AutoSource::Roma : depth_maps ? AutoSource::Depth : AutoSource::Roma;
}

}  // namespace roma
