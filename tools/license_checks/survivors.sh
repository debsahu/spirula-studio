#!/bin/bash
# Structural tripwire over the GUI's licence consent. The dialog has no unit seam
# (it needs ImGui), so what fetch_consent_test proves about the shared record is
# pinned here at the GUI end: the Accept button records through the same store the
# CLI reads, and save_settings cannot drop an acceptance the CLI wrote. Text only,
# never behaviour; the exit status is the number of lines that missed.

cd "$(dirname "$0")/../.." || exit 1
FAILS=0
has() {   # min-count file text
    local n
    n=$(command grep -cF -- "$3" "$2")
    if [ "$n" -ge "$1" ]; then echo "ok   $n >= $1: $3"; else echo "FAIL $n >= $1 in $2: $3"; FAILS=$((FAILS + 1)); fi
}
none() {  # file text
    local n
    n=$(command grep -cF -- "$2" "$1")
    if [ "$n" -eq 0 ]; then echo "ok   0 == 0: $2"; else echo "FAIL $n == 0 in $1: $2"; FAILS=$((FAILS + 1)); fi
}
G=src/app/gui/GuiApp.cpp
has 1 $G 'if (!accept_license(_license_prompt))'
has 1 src/app/gui/ModelCache.cpp 'return spirula::license::record(family);'
has 1 $G 'return spirula::license::accepted(family);'
none $G '_accepted_licenses'
none src/app/gui/GuiApp.h '_accepted_licenses'
has 1 $G 'const std::vector<std::string> accepted = spirula::license::accepted_all();'
has 1 $G 'for (const auto& l : accepted)'
# accepted_all() is read BEFORE the "w" open truncates gui.conf.
read_at=$(command grep -nF 'spirula::license::accepted_all();' $G | head -1 | cut -d: -f1)
open_at=$(command grep -nF 'std::fopen(settings_path().c_str(), "w")' $G | head -1 | cut -d: -f1)
if [ -n "$read_at" ] && [ -n "$open_at" ] && [ "$read_at" -lt "$open_at" ]; then
    echo "ok   accepted_all() ($read_at) precedes the truncating open ($open_at)"
else
    echo "FAIL accepted_all() must be read before save_settings truncates gui.conf"; FAILS=$((FAILS + 1))
fi
has 1 $G 'ImGui::BeginChild("##license_text"'
has 1 $G 'ui::TextWrappedRaw(std::string(li.full_text));'
has 1 src/app/gui/ModelCache.cpp 'spirula::license::terms_for("dinov3")->text'
exit $FAILS
