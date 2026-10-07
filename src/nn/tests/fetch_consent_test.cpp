// Licence consent: an unaccepted family REFUSES in the CLI path (ensure_file),
// before any network or cache access; acceptance is recorded in the key the GUI
// reads; the terms shown are the published text and nothing more.
//
// Runs against a scratch config and cache directory, never the user's.

#include "app/gui/FetchSource.h"
#include "core/LicenseConsent.h"
#include "core/ModelMirror.h"
#include "nn/core/Error.h"
#include "nn/io/Fetch.h"
#include "roma/model/Fetch.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define SS_SETENV(k, v) _putenv_s(k, v)
#define SS_UNSETENV(k) _putenv_s(k, "")
#else
#include <unistd.h>
#define SS_SETENV(k, v) setenv(k, v, 1)
#define SS_UNSETENV(k) unsetenv(k)
#endif

namespace fs = std::filesystem;
namespace lic = spirula::license;

namespace {

int g_failures = 0;

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

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

fs::path g_root;

// A fresh, empty config + cache for each case.
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

fs::path gui_conf() { return lic::settings_path(); }

// Each call captures what accept_licenses prints into its own file.
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

// A file that would download from nowhere: a refusal that is NOT about consent
// shows up as a curl failure, a different message.
const char* kNever = "http://127.0.0.1:9/never-downloaded.bin";

nn::FetchFile never_file(const char* family) {
    nn::FetchFile f;
    f.file = "consent-test.bin";
    f.url = kNever;
    f.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    f.bytes = 1;
    f.license_family = family;
    f.no_mirror = true;
    return f;
}

void test_refusal() {
    reset_dirs();
    const nn::FetchFile f = never_file("dinov3,romav2");
    const std::string m = message_of([&] { nn::ensure_file(f, "test"); });
    check(has(m, "has not been accepted") && has(m, "--accept-license dinov3"),
          "unaccepted family: ensure_file REFUSES, naming the family and the flag");
    check(!fs::exists(g_root / "cache" / "spirula-studio" / "models" / "consent-test.bin.part") &&
              !fs::exists(g_root / "cache" / "spirula-studio" / "models" / "consent-test.bin"),
          "unaccepted family: nothing was fetched or written");
    check(!fs::exists(gui_conf()), "unaccepted family: the refusal records nothing");

    // Already on disk with the right hash is still a refusal: the licence
    // governs loading as well as fetching.
    fs::create_directories(g_root / "cache" / "spirula-studio" / "models");
    const fs::path dst = nn::cached_path(f);
    { std::ofstream o(dst, std::ios::binary); o << "abc"; }
    nn::FetchFile cached = f;
    cached.license_gates_load = true;   // the RoMa v2 file's setting
    cached.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::string m2 = message_of([&] { nn::ensure_file(cached, "test"); });
    check(has(m2, "has not been accepted"), "a cached, hash-correct file is refused until accepted");

    // Positive control: with consent, the same call returns the path. Without
    // it the refusals above could be any failure at all.
    nn::ConsentIO io;
    Capture cap;
    io.out = cap.f;
    nn::accept_licenses("dinov3=yes", io);
    const std::string m3 = message_of([&] { nn::ensure_file(cached, "test"); });
    check(has(m3, "has not been accepted") && has(m3, "--accept-license romav2") &&
              !has(m3, "--accept-license dinov3"),
          "accepting dinov3 alone still refuses a file that also needs romav2, naming romav2");
    nn::accept_licenses("romav2=yes", io);
    check(nn::ensure_file(cached, "test") == dst.string(),
          "both families accepted: the cached file is returned");
    const std::string m4 = message_of([&] { nn::ensure_file(f, "test"); });
    check(!has(m4, "has not been accepted") && m4 != "<did not throw>",
          "accepted but absent: it proceeds to the download, which fails on its own terms");
    check(!has(m4, "modelscope") && !has(m4, "trying"), "a no_mirror download never names a mirror");

    // A family that gates only the DOWNLOAD leaves a verified cached copy usable.
    reset_dirs();
    fs::create_directories(g_root / "cache" / "spirula-studio" / "models");
    { std::ofstream o(nn::cached_path(cached), std::ios::binary); o << "abc"; }
    nn::FetchFile dl_only = cached;
    dl_only.license_gates_load = false;
    check(nn::ensure_file(dl_only, "test") == nn::cached_path(cached) && !lic::accepted("dinov3"),
          "download-only gate: a cached, hash-correct copy loads with no consent recorded");
    fs::remove(nn::cached_path(cached));
    const std::string m5 = message_of([&] { nn::ensure_file(dl_only, "test"); });
    check(has(m5, "has not been accepted"), "download-only gate: the DOWNLOAD is still refused");
}

// What the dialogs and a batch lean on, in core: the tick policy and the family list.
void test_dialog_policy() {
    for (const char* fam : {"sam3", "sam2", "gdino", "birefnet", "dinov3", "romav2"}) {
        check(!lic::accept_enabled(fam, false), std::string(fam) + ": Accept is OFF until ticked");
        check(lic::accept_enabled(fam, true), std::string(fam) + ": Accept is ON once ticked");
    }
    check(!lic::accept_enabled("nonsense", true), "a family with no terms can never be accepted");
    const std::vector<std::string> want = {"sam3", "gdino", "dinov3", "romav2"};
    check(lic::unique_families({"sam3", "", "gdino, sam3", "dinov3,romav2", "romav2"}) == want,
          "unique_families: comma lists flattened, each family once, first-seen order");
    check(lic::unique_families({}).empty() && lic::unique_families({"", " , "}).empty(),
          "unique_families: nothing named, nothing asked");
}

// A terminal is asked; a script is not. Both paths go through require_license, which
// is what ensure_file calls, so this is the prompt a gated download shows.
void test_require_prompt() {
    reset_dirs();
    Capture cap;
    nn::ConsentIO io;
    io.out = cap.f;
    io.tty = true;
    int asked = 0;
    io.read_line = [&] { ++asked; return std::string("no"); };
    std::string m = message_of([&] { nn::require_license("sam3", io); });
    const std::string shown = cap.text();
    check(has(m, "was not accepted") && !lic::accepted("sam3") && asked == 1,
          "TTY, 'no': refused after one question, nothing recorded");
    check(has(shown, lic::terms_for("sam3")->text),
          "TTY: the whole SAM license is printed before the question");
    check(has(shown, "read and accept the terms of SAM License (Meta)"),
          "TTY: the question says the user has read and accepts, naming the licence");
    io.read_line = [&] { ++asked; return std::string("y"); };
    m = message_of([&] { nn::require_license("sam3", io); });
    check(has(m, "was not accepted") && !lic::accepted("sam3"), "TTY, 'y': only 'yes' accepts");
    io.read_line = [&] { ++asked; return std::string("yes"); };
    check(message_of([&] { nn::require_license("sam3", io); }) == "<did not throw>" &&
              lic::accepted("sam3"),
          "TTY, 'yes': proceeds, and the acceptance is recorded");
    const int before = asked;
    nn::require_license("sam3", io);
    check(asked == before, "an accepted family is not asked again");

    // Two families: asked once each, in order, and a refusal of the second keeps the first.
    reset_dirs();
    std::vector<std::string> answers = {"yes", "no"};
    io.read_line = [&] { const std::string a = answers.front(); answers.erase(answers.begin()); return a; };
    m = message_of([&] { nn::require_license("dinov3,romav2", io); });
    check(has(m, "MIT") && lic::accepted("dinov3") && !lic::accepted("romav2"),
          "two families: the first accepted and kept, the second refused by name");

    // No terminal.
    reset_dirs();
    nn::ConsentIO script;
    Capture cap2;
    script.out = cap2.f;
    script.read_line = [] { return std::string("yes"); };   // present, but tty is false
    m = message_of([&] { nn::require_license("gdino", script); });
    check(has(m, "--accept-license gdino=yes") && has(m, "no terminal") && !lic::accepted("gdino"),
          "no terminal: refused, naming --accept-license gdino=yes, even with an input present");
    check(cap2.text().empty(), "no terminal: nothing is printed to a script's stdout");
    check(message_of([&] { nn::require_license(nullptr, script); }) == "<did not throw>" &&
              message_of([&] { nn::require_license("", script); }) == "<did not throw>",
          "a null or empty family needs nothing");
    check(has(message_of([&] { nn::require_license("nonsense", script); }), "unknown licence"),
          "an unknown family is refused, not waved through");
}

void test_no_family_unaffected() {
    reset_dirs();
    nn::FetchFile f = never_file(nullptr);
    f.no_mirror = false;
    SS_SETENV("SS_NO_AUTO_FETCH", "1");   // the mirror is a real host; do not call it
    const std::string m = message_of([&] { nn::ensure_file(f, "test"); });
    SS_UNSETENV("SS_NO_AUTO_FETCH");
    check(has(m, "may not download") && !has(m, "has not been accepted"),
          "a file with no license_family is never gated");
    check(has(m, "modelscope.cn"), "control: an ordinary file's refusal names the project mirror");
    check(!nn::mirror_url(f).empty(), "default FetchFile keeps its project mirror");
    f.no_mirror = true;
    check(nn::mirror_url(f).empty(), "no_mirror: there is no mirror URL to fall back to");
}

void test_record_and_gui_key() {
    reset_dirs();
    fs::create_directories(gui_conf().parent_path());
    {   // What GuiApp::save_settings writes, plus an accepted family.
        std::ofstream o(gui_conf(), std::ios::binary);
        o << "colmap_exe=/opt/colmap\nlang=de\naccepted_license=sam3\nui_scale=1.250\n";
    }
    check(lic::accepted("sam3") && !lic::accepted("dinov3"),
          "reads the GUI's accepted_license= lines");
    check(lic::record("dinov3") && lic::accepted("dinov3"), "record() accepts a family");
    const std::string conf = slurp(gui_conf());
    check(has(conf, "accepted_license=dinov3\n"), "the key the GUI writes: accepted_license=dinov3");
    check(has(conf, "colmap_exe=/opt/colmap\nlang=de\naccepted_license=sam3\nui_scale=1.250\n"),
          "every other line survives, in order");
    lic::record("dinov3");
    size_t n = 0, at = 0;
    while ((at = conf.find("accepted_license=dinov3", at)) != std::string::npos) { ++n; ++at; }
    check(n == 1 && slurp(gui_conf()) == conf, "record() is idempotent");
    check(!lic::record(""), "an empty family is not recorded");
    check(lic::accepted_all() == std::vector<std::string>{"sam3", "dinov3"},
          "accepted_all() lists them in file order");
    // A prefix of an accepted family is not that family.
    check(!lic::accepted("dino") && !lic::accepted("dinov3x"),
          "a family is matched whole, not as a prefix");
}

void test_prompts() {
    reset_dirs();
    Capture cap;
    nn::ConsentIO io;
    io.out = cap.f;
    io.tty = true;
    io.read_line = [] { return std::string("no"); };
    std::string m = message_of([&] { nn::accept_licenses("romav2", io); });
    check(has(m, "was not accepted") && !lic::accepted("romav2"),
          "TTY, answer 'no': refused and nothing recorded");
    io.read_line = [] { return std::string("YES"); };
    m = message_of([&] { nn::accept_licenses("romav2", io); });
    check(has(m, "was not accepted") && !lic::accepted("romav2"),
          "TTY, answer 'YES': only the exact word 'yes' accepts");
    io.read_line = [] { return std::string("yes"); };
    nn::accept_licenses("romav2", io);
    check(lic::accepted("romav2"), "TTY, answer 'yes': accepted and recorded");

    nn::ConsentIO script;
    Capture cap2;
    script.out = cap2.f;
    m = message_of([&] { nn::accept_licenses("dinov3", script); });
    check(has(m, "dinov3=yes") && !lic::accepted("dinov3"),
          "no terminal and no '=yes': refused, and the message names the =yes form");
    m = message_of([&] { nn::accept_licenses("nonsense=yes", script); });
    check(has(m, "unknown licence") && !lic::accepted("nonsense"), "an unknown family is refused");
    m = message_of([&] { nn::accept_licenses("dinov3=maybe", script); });
    check(has(m, "=yes") && !lic::accepted("dinov3"), "only '=yes' follows a family");
    nn::accept_licenses("dinov3=yes", script);
    check(lic::accepted("dinov3"), "no terminal, '=yes': accepted");
}

void test_terms_shown() {
    reset_dirs();
    Capture cap;
    nn::ConsentIO io;
    io.out = cap.f;
    nn::accept_licenses("dinov3=yes,romav2=yes", io);
    const std::string shown = cap.text();
    const lic::Terms* d = lic::terms_for("dinov3");
    const lic::Terms* r = lic::terms_for("romav2");
    check(d && r, "both families have terms");
    check(has(shown, d->text), "the DINOv3 Agreement is printed in full, verbatim");
    check(has(shown, r->text), "the RoMa v2 MIT text is printed in full, verbatim");
    // Everything printed besides the two texts is ours; it must add no term.
    std::string ours = shown;
    for (const std::string& t : {std::string(d->text), std::string(r->text)})
        ours.erase(ours.find(t), t.size());
    check(!has(lower(ours), "commercial") && !has(lower(ours), "redistribut"),
          "our framing adds no commercial-use or redistribution wording");

    // The embedded text is the file under LICENSES/, not a stale copy.
    const fs::path lic_dir = fs::path(SS_REPO_ROOT) / "LICENSES";
    if (fs::exists(lic_dir / "DINOv3-License-Agreement.md")) {
        check(slurp(lic_dir / "DINOv3-License-Agreement.md") == d->text,
              "embedded DINOv3 text == LICENSES/DINOv3-License-Agreement.md");
        check(slurp(lic_dir / "MIT-RoMaV2.txt") == r->text,
              "embedded MIT text == LICENSES/MIT-RoMaV2.txt");
    } else {
        check(false, "LICENSES/ not found next to the sources");
    }
    // Every gated family has its terms, and they are the files under LICENSES/.
    const struct { const char* fam; const char* file; } kAll[] = {
        {"sam3", "SAM3-License.txt"}, {"sam2", "Apache-2.0-SAM2.txt"},
        {"gdino", "Apache-2.0-GroundingDINO.txt"}, {"birefnet", "MIT-BiRefNet.txt"},
        {"dinov3", "DINOv3-License-Agreement.md"}, {"romav2", "MIT-RoMaV2.txt"}};
    for (const auto& e : kAll) {
        const lic::Terms* t = lic::terms_for(e.fam);
        check(t && std::string(t->text).size() > 500 && slurp(lic_dir / e.file) == t->text,
              std::string("terms_for(") + e.fam + ") is LICENSES/" + e.file + ", non-trivially");
    }
    check(lic::terms_for("nonsense") == nullptr, "an unknown family has no terms");
}

void test_argv() {
    reset_dirs();
    std::vector<std::string> store = {"spirula", "densify", "--accept-license", "dinov3=yes",
                                      "ds", "--accept-license=romav2=yes", "--preset", "base"};
    std::vector<char*> argv;
    for (auto& s : store) argv.push_back(s.data());
    argv.push_back(nullptr);
    int argc = (int)store.size();
    const int n = nn::consume_accept_license_args(argc, argv.data());
    check(n == 2 && argc == 5, "both spellings are found and removed from argv");
    check(std::string(argv[1]) == "densify" && std::string(argv[2]) == "ds" &&
              std::string(argv[3]) == "--preset" && std::string(argv[4]) == "base",
          "every other argument keeps its order");
    check(lic::accepted("dinov3") && lic::accepted("romav2"), "and both were recorded");
    int c2 = 2;
    char a0[] = "x", a1[] = "--accept-license";
    char* v2[] = {a0, a1, nullptr};
    check(message_of([&] { nn::consume_accept_license_args(c2, v2); }).find("needs a value") !=
              std::string::npos,
          "--accept-license with no value is an error");
}

void test_roma_checkpoint() {
    reset_dirs();
    const nn::FetchFile& f = roma::checkpoint_file();
    check(std::string(f.url) ==
              "https://github.com/Parskatt/RoMaV2/releases/download/v2.0.1/romav2.0.1.pt",
          "roma: fetched from the authors' own release URL");
    check(f.no_mirror && nn::mirror_url(f).empty(), "roma: never from the project mirror");
    check(std::string(f.license_family) == "dinov3,romav2" && f.bytes == 1095883548ull,
          "roma: needs both licences; 1,095,883,548 bytes");
    const std::string m = message_of([] { roma::ensure_checkpoint(); });
    check(has(m, "has not been accepted") && has(m, "--accept-license dinov3") &&
              !fs::exists(nn::cached_path(f) + ".part"),
          "roma: ensure_checkpoint refuses until accepted, with nothing fetched");

    const char* real = std::getenv("SS_TEST_ROMAV2_PT");
    if (!real || !*real) {
        std::printf("SKIP roma: real-file hash (set SS_TEST_ROMAV2_PT)\n");
        return;
    }
    check(nn::sha256_file(real) == f.sha256, "roma: the pinned SHA-256 is the real file's");
    fs::create_directories(fs::path(nn::cached_path(f)).parent_path());
    std::error_code ec;
    fs::create_symlink(real, nn::cached_path(f), ec);
    check(!ec, "roma: cache seeded with the real file");
    check(has(message_of([] { roma::ensure_checkpoint(); }), "has not been accepted"),
          "roma: a cached, hash-correct checkpoint is still refused until accepted");
    nn::ConsentIO io;
    Capture cap;
    io.out = cap.f;
    nn::accept_licenses("dinov3=yes,romav2=yes", io);
    check(roma::ensure_checkpoint() == nn::cached_path(f), "roma: accepted, it loads from the cache");
}

// Every route a download takes resolves its fallback through spirula::mirror_for.
void test_mirror_paths() {
    const nn::FetchFile& roma_file = roma::checkpoint_file();
    check(spirula::mirror_for(roma_file.file, roma_file.mirror, roma_file.no_mirror).empty(),
          "mirror_for: empty for the RoMa file");
    check(nn::mirror_url(roma_file).empty(), "nn::mirror_url: empty for the RoMa file");
    const gui::PendingDownload p = gui::pending_download(roma_file);
    check(p.mirror.empty() && p.url == roma_file.url && p.license_family == "dinov3,romav2" &&
              p.dest == nn::cached_path(roma_file),
          "GUI download queue entry: no mirror, both licences carried, the cache path");
    // An own mirror still wins for an ordinary file, and the project's is the default.
    nn::FetchFile own;
    own.file = "x.bin";
    own.url = "https://example.invalid/primary/x.bin";
    own.mirror = "https://example.invalid/x.bin";
    check(gui::pending_download(own).mirror == "https://example.invalid/x.bin",
          "control: a file's own mirror is carried");
    own.mirror = nullptr;
    check(has(gui::pending_download(own).mirror, "modelscope.cn") &&
              gui::pending_download(own).license_family.empty(),
          "control: an ordinary file gets the project mirror and no licence");
    own.no_mirror = true;
    check(gui::pending_download(own).mirror.empty(), "no_mirror alone empties the entry");
}

}  // namespace

int main() {
    g_root = fs::temp_directory_path() / "spirula_fetch_consent_test";
    try {
        test_refusal();
        test_no_family_unaffected();
        test_record_and_gui_key();
        test_prompts();
        test_terms_shown();
        test_require_prompt();
        test_dialog_policy();
        test_argv();
        test_roma_checkpoint();
        test_mirror_paths();
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
