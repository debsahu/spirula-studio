// The RoMa v2 checkpoint's two licences (DINOv3 and RoMa v2's MIT): registered
// into the consent table, refused until accepted -- even for a cached,
// hash-correct file -- shown in full, and worded without a restriction the
// licences do not carry. The generic mechanism is tested in fetch_consent_test.
//
// Runs against a scratch config and cache directory, never the user's.

#include "app/cli/LicenseCli.h"
#include "app/gui/FetchSource.h"
#include "core/LicenseConsent.h"
#include "i18n/Message.h"
#include "i18n/catalog/DenseGui.h"
#include "nn/core/Error.h"
#include "nn/io/Fetch.h"
#include "nn/tests/license_wording.h"
#include "roma/model/Fetch.h"
#include "roma/model/LicenseTexts.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define SS_SETENV(k, v) _putenv_s(k, v)
#else
#define SS_SETENV(k, v) setenv(k, v, 1)
#endif

namespace fs = std::filesystem;
namespace lic = spirula::license;
namespace dg = spirula::i18n::msg::densegui;

namespace {

int g_failures = 0;
fs::path g_root;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

std::string message_of(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const nn::Error& e) {
        return e.what();
    }
    return "<did not throw>";
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void reset_dirs() {
    std::error_code ec;
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root / "config", ec);
    fs::create_directories(g_root / "cache", ec);
#ifdef _WIN32
    SS_SETENV("APPDATA", (g_root / "config").string().c_str());
    SS_SETENV("LOCALAPPDATA", (g_root / "cache").string().c_str());
#else
    SS_SETENV("XDG_CONFIG_HOME", (g_root / "config").string().c_str());
    SS_SETENV("XDG_CACHE_HOME", (g_root / "cache").string().c_str());
#endif
}

struct Capture {
    std::FILE* f = std::tmpfile();
    std::string text() {
        std::fflush(f);
        std::rewind(f);
        std::string s;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        return s;
    }
    ~Capture() { if (f) std::fclose(f); }
};

void test_registration() {
    check(lic::terms_for("dinov3") == nullptr && lic::terms_for("romav2") == nullptr,
          "before register_licenses(), neither family is known");
    roma::register_licenses();
    roma::register_licenses();   // idempotent
    const lic::Terms* d = lic::terms_for("dinov3");
    const lic::Terms* r = lic::terms_for("romav2");
    check(d && r, "after it, both are");
    check(lic::known_families() == "sam3, sam2, gdino, birefnet, dinov3, romav2",
          "known_families lists them after the built-ins: " + lic::known_families());
    for (const char* fam : {"dinov3", "romav2"}) {
        check(!lic::accept_enabled(fam, false), std::string(fam) + ": Accept is OFF until ticked");
        check(lic::accept_enabled(fam, true), std::string(fam) + ": Accept is ON once ticked");
    }
    check(lic::unique_families({"sam3", "dinov3,romav2", "romav2"}) ==
              std::vector<std::string>{"sam3", "dinov3", "romav2"},
          "unique_families: the checkpoint's list, each family once, in its order");
    check(d && r && std::string(d->text) == roma::kDinov3Agreement &&
              std::string(r->text) == roma::kRomaV2Mit,
          "terms_for carries the generated texts");
    const fs::path lic_dir = fs::path(SS_REPO_ROOT) / "LICENSES";
    check(fs::exists(lic_dir / "MIT-RoMaV2.txt") &&
              slurp(lic_dir / "DINOv3-License-Agreement.md") == roma::kDinov3Agreement &&
              slurp(lic_dir / "MIT-RoMaV2.txt") == roma::kRomaV2Mit,
          "the embedded texts are LICENSES/DINOv3-License-Agreement.md and MIT-RoMaV2.txt");
    check(std::string(roma::kDinov3Agreement).size() > 500 && std::string(roma::kRomaV2Mit).size() > 500,
          "both texts are non-trivial");
}

void test_terms_shown() {
    reset_dirs();
    Capture cap;
    app::ConsentIO io;
    io.out = cap.f;
    app::accept_licenses("dinov3=yes,romav2=yes", io);
    const std::string shown = cap.text();
    check(has(shown, roma::kDinov3Agreement), "the DINOv3 Agreement is printed in full, verbatim");
    check(has(shown, roma::kRomaV2Mit), "the RoMa v2 MIT text is printed in full, verbatim");
    std::string ours = shown;
    for (const std::string& t : {std::string(roma::kDinov3Agreement), std::string(roma::kRomaV2Mit)})
        ours.erase(ours.find(t), t.size());
    check(!has(licwording::lower(ours), "commercial") && !has(licwording::lower(ours), "redistribut"),
          "our framing adds no commercial-use or redistribution wording");
    check(lic::accepted("dinov3") && lic::accepted("romav2"), "both were recorded");
}

void test_checkpoint() {
    reset_dirs();
    const nn::FetchFile& f = roma::checkpoint_file();
    check(std::string(f.url) ==
              "https://github.com/Parskatt/RoMaV2/releases/download/v2.0.1/romav2.0.1.pt",
          "fetched from the authors' own release URL");
    check(f.no_mirror && nn::mirror_url(f).empty(), "never from the project mirror");
    check(std::string(f.license_family) == "dinov3,romav2" && f.bytes == 1095883548ull &&
              f.license_gates_load,
          "needs both licences before loading; 1,095,883,548 bytes");
    const gui::PendingDownload p = gui::pending_download(f);
    check(p.mirror.empty() && p.url == f.url && p.license_family == "dinov3,romav2" &&
              p.dest == nn::cached_path(f),
          "GUI download queue entry: no mirror, both licences carried, the cache path");

    const std::string m = message_of([] { roma::ensure_checkpoint(); });
    check(has(m, "has not been accepted") && has(m, "--accept-license dinov3") &&
              !fs::exists(nn::cached_path(f) + ".part"),
          "ensure_checkpoint refuses until accepted, with nothing fetched");

    const char* real = std::getenv("SS_TEST_ROMAV2_PT");
    if (!real || !*real) {
        std::printf("SKIP real-file hash (set SS_TEST_ROMAV2_PT)\n");
        return;
    }
    check(nn::sha256_file(real) == f.sha256, "the pinned SHA-256 is the real file's");
    fs::create_directories(fs::path(nn::cached_path(f)).parent_path());
    std::error_code ec;
    fs::create_symlink(real, nn::cached_path(f), ec);
    check(!ec, "cache seeded with the real file");
    check(has(message_of([] { roma::ensure_checkpoint(); }), "has not been accepted"),
          "a cached, hash-correct checkpoint is still refused until accepted");
    app::ConsentIO io;
    Capture cap;
    io.out = cap.f;
    app::accept_licenses("dinov3=yes", io);
    check(has(message_of([] { roma::ensure_checkpoint(); }), "--accept-license romav2"),
          "dinov3 alone is not enough: romav2 is named");
    app::accept_licenses("romav2=yes", io);
    check(roma::ensure_checkpoint() == nn::cached_path(f), "accepted, it loads from the cache");
}

void test_argv() {
    reset_dirs();
    std::vector<std::string> store = {"spirula", "densify", "--accept-license", "dinov3=yes",
                                      "ds", "--accept-license=romav2=yes", "--preset", "base"};
    std::vector<char*> argv;
    for (auto& s : store) argv.push_back(s.data());
    argv.push_back(nullptr);
    int argc = (int)store.size();
    const int n = app::consume_accept_license_args(argc, argv.data());
    check(n == 2 && argc == 5 && std::string(argv[2]) == "ds" && std::string(argv[4]) == "base",
          "both spellings are found and removed, every other argument keeps its order");
    check(lic::accepted("dinov3") && lic::accepted("romav2"), "and both were recorded");
}

void test_wording() {
    struct Entry {
        const char* name;
        const spirula::i18n::Msg* msg;
    };
    const Entry entries[] = {
        {"license_dinov3_title", &dg::license_dinov3_title},
        {"license_dinov3_summary", &dg::license_dinov3_summary},
        {"license_romav2_title", &dg::license_romav2_title},
        {"license_romav2_summary", &dg::license_romav2_summary},
    };
    int scanned = 0;
    for (const Entry& e : entries)
        for (unsigned l = 0; l < spirula::i18n::kLangCount; ++l) {
            ++scanned;
            const std::string text = e.msg->in((spirula::i18n::Lang)l);
            const std::string hit = licwording::banned_in(text);
            check(hit.empty() && !text.empty(), std::string(e.name) + " [" + std::to_string(l) + "]" +
                                                    (hit.empty() ? "" : " contains '" + hit + "'"));
            if (std::string(e.name) == "license_romav2_summary")
                check(!has(licwording::lower(text), "nothing unusual") &&
                          !has(licwording::lower(text), "nichts ungewöhnliches"),
                      "the RoMa v2 summary makes no 'nothing unusual to agree to' claim [" +
                          std::to_string(l) + "]");
        }
    check(scanned == 4 * (int)spirula::i18n::kLangCount, "every message scanned in every language");
}

}  // namespace

int main() {
    g_root = fs::temp_directory_path() / "spirula_roma_license_test";
    app::install_license_gate();
    try {
        test_registration();
        test_terms_shown();
        test_checkpoint();
        test_argv();
        test_wording();
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        return 1;
    }
    std::error_code ec;
    fs::remove_all(g_root, ec);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
