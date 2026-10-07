#pragma once
// Which licences the user has accepted, and the full terms of the ones we show
// verbatim. The record is the `accepted_license=<family>` lines of the GUI's
// gui.conf, so the GUI, the CLI and a child process the GUI launches all read
// one answer. Nothing here downloads anything.

#include <string>
#include <vector>

namespace spirula::license {

// A licence displayed in full before a download. `text` is the agreement as
// published, byte for byte (LICENSES/), for offline display.
struct Terms {
    const char* family;   // "dinov3", "romav2"
    const char* title;    // English, for the terminal; the GUI uses its Msg
    const char* url;      // the current text, which may be newer than `text`
    const char* text;
};

// Null for a family whose terms are not shown in full (the SAM families).
const Terms* terms_for(const std::string& family);

// <config>/gui.conf, where app::config_dir() puts it. Empty when neither
// XDG_CONFIG_HOME nor HOME is set (APPDATA on Windows): app::config_dir() falls
// back to the working directory there, and consent read from there is not consent.
std::string settings_path();

bool accepted(const std::string& family);
// Idempotent; every other line of the file is kept as it is. False when the
// file could not be written, in which case the family is NOT accepted.
bool record(const std::string& family);
std::vector<std::string> accepted_all();
// The families of a comma list that are not accepted, in order. Empty means go.
std::vector<std::string> missing(const std::string& families);

}  // namespace spirula::license
