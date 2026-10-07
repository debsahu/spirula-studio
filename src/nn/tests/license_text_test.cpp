// The licence summaries and titles, in all 13 languages, carry no restriction the
// licences do not: no commercial-use, non-commercial or redistribution wording.
// A catalog edit that adds one fails here by language and message.
//
// The DINOv3 Agreement has no non-commercial clause and Spirula redistributes no
// weights, so neither idea belongs in the text a user reads before accepting.

#include "i18n/Message.h"
#include "i18n/catalog/Cli.h"
#include "i18n/catalog/Dataset.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace dmsg = spirula::i18n::msg::dataset;
namespace cmsg = spirula::i18n::msg::cli;
using spirula::i18n::Lang;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

std::string lower(std::string s) {
    for (char& c : s)
        if ((unsigned char)c < 0x80) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Stems, so inflected forms are caught; each language's words for "commercial",
// "non-commercial" and "redistribute" (and the same ideas in the nearest noun).
const char* kBanned[] = {
    "commercial", "redistribut", "re-distribut", "sublicens", "resell", "resale",
    "kommerz", "weiterverbreit", "weitergabe", "weiterverkauf",
    "commercia", "redistribu", "revend",
    "comercial", "redistribu", "reventa", "revenda",
    "commerci", "herdistribu", "doorverkoop",
    "коммерч", "перераспростран", "перепрод",
    "ticari", "yeniden dağıt", "yeniden sat",
    "商用", "商业", "商業", "非営利", "営利", "再配布", "再頒布", "転売",
    "非商业", "非商業", "再分发", "再分發", "转售", "轉售", "再发布", "再發布",
    "상업", "영리", "재배포", "재판매",
};

}  // namespace

int main() {
    struct Entry {
        const char* name;
        const spirula::i18n::Msg* msg;
    };
    const Entry entries[] = {
        {"license_dinov3_title", &dmsg::license_dinov3_title},
        {"license_dinov3_summary", &dmsg::license_dinov3_summary},
        {"license_romav2_title", &dmsg::license_romav2_title},
        {"license_romav2_summary", &dmsg::license_romav2_summary},
        {"license_full_text", &dmsg::license_full_text},
        {"license_accept", &dmsg::license_accept},
        {"license_not_accepted_download", &dmsg::license_not_accepted_download},
        {"license_accept_tick", &dmsg::license_accept_tick},
        {"license_declined_download", &dmsg::license_declined_download},
        {"batch_licence_declined", &dmsg::batch_licence_declined},
        {"license_ask_header", &cmsg::license_ask_header},
        {"license_ask_confirm", &cmsg::license_ask_confirm},
        {"license_ask_declined", &cmsg::license_ask_declined},
        {"license_no_terminal", &cmsg::license_no_terminal},
    };
    int scanned = 0;
    for (const Entry& e : entries) {
        for (unsigned l = 0; l < spirula::i18n::kLangCount; ++l) {
            const std::string text = lower(e.msg->in((Lang)l));
            ++scanned;
            std::string hit;
            for (const char* w : kBanned)
                if (text.find(lower(w)) != std::string::npos) { hit = w; break; }
            check(hit.empty(), std::string(e.name) + " [" + std::to_string(l) + "]" +
                                   (hit.empty() ? "" : " contains '" + hit + "'"));
        }
    }
    check(scanned == 14 * (int)spirula::i18n::kLangCount,
          "all fourteen messages scanned in every language");
    // A translation that drops a placeholder asks for acceptance of nothing in particular,
    // or leaves the user without the flag to type.
    for (unsigned l = 0; l < spirula::i18n::kLangCount; ++l) {
        const std::string n = " [" + std::to_string(l) + "]";
        check(std::string(dmsg::license_accept_tick.in((Lang)l)).find("{0}") != std::string::npos,
              "license_accept_tick" + n + " carries the licence name");
        check(std::string(cmsg::license_ask_confirm.in((Lang)l)).find("{0}") != std::string::npos &&
                  std::string(cmsg::license_ask_confirm.in((Lang)l)).find("'yes'") != std::string::npos,
              "license_ask_confirm" + n + " names the licence and asks for 'yes'");
        const std::string t = cmsg::license_no_terminal.in((Lang)l);
        check(t.find("{0}") != std::string::npos && t.find("{1}") != std::string::npos &&
                  t.find("--accept-license {2}=yes") != std::string::npos,
              "license_no_terminal" + n + " carries the name, the URL and the exact flag");
    }
    // The scan can see: a sentence with the wording in it is caught.
    check(lower("for non-commercial use only").find("commercial") != std::string::npos,
          "control: the English stem matches a restricted sentence");
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
