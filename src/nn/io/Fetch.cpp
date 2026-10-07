#include "nn/io/Fetch.h"

#include "core/Env.h"
#include "core/LicenseConsent.h"
#include "core/ModelMirror.h"
#include "core/Sha256.h"
#include "nn/core/Error.h"
#include "nn/core/Log.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define SS_ISATTY(fd) _isatty(fd)
#else
#include <sys/wait.h>
#include <unistd.h>
#define SS_ISATTY(fd) isatty(fd)
#endif

namespace fs = std::filesystem;

namespace nn {
namespace {

std::string env_str(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

fs::path cache_root() {
#ifdef _WIN32
    fs::path dir = env_str("LOCALAPPDATA");
#else
    fs::path dir = env_str("XDG_CACHE_HOME");
    if (dir.empty()) {
        const std::string home = env_str("HOME");
        if (!home.empty()) dir = fs::path(home) / ".cache";
    }
#endif
    if (dir.empty()) dir = ".";
    // An existing spirulae-splat/ from before the rename is adopted where it
    // is -- the checkpoints in it are a big download to repeat.
    std::error_code ec;
    if (!fs::exists(dir / "spirula-studio", ec) &&
        fs::is_directory(dir / "spirulae-splat", ec))
        return dir / "spirulae-splat";
    return dir / "spirula-studio";
}

// Abort a connection that never opens, or a transfer under 1 KB/s for a minute.
const char* kCurlTimeouts = "--connect-timeout 30 --speed-limit 1024 --speed-time 60";

bool have_curl() {
#ifdef _WIN32
    return std::system("curl --version >NUL 2>&1") == 0;
#else
    return std::system("curl --version >/dev/null 2>&1") == 0;
#endif
}

}  // namespace

std::string model_cache_dir() {
    return (cache_root() / "models").string();
}

std::string cached_path(const FetchFile& f) {
    return (cache_root() / "models" / f.file).string();
}

std::string mirror_url(const FetchFile& f) {
    if (f.no_mirror) return std::string();
    return f.mirror ? std::string(f.mirror) : spirula::model_mirror_url(f.file);
}

std::string sha256_file(const std::string& path) {
    return spirula::sha256_file(path);
}

namespace {

std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string t; std::getline(ss, t, ',');) {
        const size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(t.substr(a, b - a + 1));
    }
    return out;
}

}  // namespace

void require_license(const char* family) {
    if (!family || !*family) return;
    for (const std::string& fam : split_commas(family)) {
        if (spirula::license::accepted(fam)) continue;
        const spirula::license::Terms* t = spirula::license::terms_for(fam);
        nn::fail("the licence '%s' has not been accepted, so this model cannot be "
                 "downloaded or loaded.\n  Read it and accept it with\n    "
                 "--accept-license %s\n  (add =yes to accept without a prompt, in a "
                 "script), or accept it in the application's download dialog.\n  Terms: %s",
                 t ? t->title : fam.c_str(), fam.c_str(), t ? t->url : "(unknown family)");
    }
}

void accept_licenses(const std::string& spec, const ConsentIO& io) {
    for (const std::string& token : split_commas(spec)) {
        const size_t eq = token.find('=');
        const std::string fam = token.substr(0, eq);
        const std::string how = eq == std::string::npos ? "" : token.substr(eq + 1);
        const spirula::license::Terms* t = spirula::license::terms_for(fam);
        NN_CHECK(t, "--accept-license: unknown licence '%s' (known: dinov3, romav2)", fam.c_str());
        NN_CHECK(how.empty() || how == "yes", "--accept-license %s: only '=yes' follows a family",
                 fam.c_str());
        if (spirula::license::accepted(fam)) {
            std::fprintf(io.out, "[license] %s is already accepted.\n", t->title);
            continue;
        }
        std::fprintf(io.out, "\n==== %s ====\n%s\n\n%s\n==== end of %s ====\n\n", t->title,
                     t->url, t->text, t->title);
        if (how.empty()) {
            NN_CHECK(io.tty && io.read_line,
                     "--accept-license %s: there is no terminal to ask on. After reading the "
                     "terms above, pass --accept-license %s=yes.", fam.c_str(), fam.c_str());
            std::fprintf(io.out, "Type 'yes' to accept the %s: ", t->title);
            std::fflush(io.out);
            const std::string answer = io.read_line();
            NN_CHECK(answer == "yes", "the %s was not accepted (answered '%s').", t->title,
                     answer.c_str());
        }
        NN_CHECK(spirula::license::record(fam), "cannot record the acceptance of %s in %s",
                 t->title, spirula::license::settings_path().c_str());
        std::fprintf(io.out, "[license] accepted %s; recorded in %s\n", t->title,
                     spirula::license::settings_path().c_str());
    }
}

int consume_accept_license_args(int& argc, char** argv) {
    int found = 0, w = 1;
    ConsentIO io;
    io.tty = SS_ISATTY(0) && SS_ISATTY(1);
    io.read_line = [] {
        std::string a;
        std::getline(std::cin, a);
        return a;
    };
    for (int r = 1; r < argc; ++r) {
        const std::string a = argv[r];
        std::string spec;
        if (a == "--accept-license") {
            NN_CHECK(r + 1 < argc, "--accept-license needs a value, e.g. dinov3,romav2");
            spec = argv[++r];
        } else if (a.rfind("--accept-license=", 0) == 0) {
            spec = a.substr(std::string("--accept-license=").size());
        } else {
            argv[w++] = argv[r];
            continue;
        }
        accept_licenses(spec, io);
        ++found;
    }
    argc = w;
    return found;
}

std::string ensure_file(const FetchFile& f, const char* tag) {
    require_license(f.license_family);
    const fs::path dst = cached_path(f);
    std::error_code ec;

    if (fs::exists(dst, ec)) {
        const std::string got = sha256_file(dst.string());
        if (got == f.sha256) return dst.string();
        // A cached file that does not hash is a failed or interrupted download
        // from a previous run, not a reason to stop: say so and refetch.
        NN_LOG_WARN("[%s] cached %s does not match its checksum; re-downloading\n", tag,
                    f.file);
        fs::remove(dst, ec);
    }

    NN_CHECK(!spirula::env_on("NO_AUTO_FETCH"),
             "%s is not in the model cache, and this process may not download "
             "it.\n  Get it from the application's own download button, or "
             "fetch\n    %s%s%s\n  to\n    %s\n  by hand.",
             f.file, f.url, f.no_mirror ? "" : "\n  or\n    ", mirror_url(f).c_str(),
             dst.string().c_str());

    fs::create_directories(dst.parent_path(), ec);
    NN_CHECK(!ec, "cannot create %s: %s", dst.parent_path().string().c_str(),
             ec.message().c_str());

    NN_CHECK(have_curl(),
             "curl was not found, and it is how checkpoints are fetched.\n"
             "  Install curl, or download\n    %s\n  to\n    %s\n  by hand.",
             f.url, dst.string().c_str());

    fs::path part = dst;
    part += ".part";

    std::vector<std::string> urls = {f.url};
    if (!f.no_mirror) urls.push_back(mirror_url(f));
    std::string why;
    for (const std::string& url : urls) {
        if (!why.empty())
            NN_LOG_WARN("[%s] %s; trying %s\n", tag, why.c_str(), url.c_str());
        NN_LOG_INFO("[%s] fetching %s (%.1f MB) from %s\n", tag, f.file,
                    (double)f.bytes / 1e6, url.c_str());
        // -C - resumes a partial .part file; -f makes an HTTP error an exit code
        // rather than a saved error page. The timeouts turn a blocked host into
        // a failure the mirror can answer instead of a hang.
        const std::string cmd = "curl -L -f --progress-bar -C - " + std::string(kCurlTimeouts) +
                                " -o \"" + part.string() + "\" \"" + url + "\"";
        int rc = std::system(cmd.c_str());
#ifndef _WIN32
        if (WIFSIGNALED(rc) && WTERMSIG(rc) == SIGINT) {
            fs::remove(part, ec);
            nn::fail("downloading %s was interrupted", f.file);
        }
        if (WIFEXITED(rc)) rc = WEXITSTATUS(rc);
#endif
        if (rc != 0) {
            fs::remove(part, ec);
            why = "downloading " + std::string(f.file) + " from " + url +
                  " failed (curl exit " + std::to_string(rc) + ")";
            continue;
        }
        const std::string got = sha256_file(part.string());
        if (got != f.sha256) {
            fs::remove(part, ec);
            why = std::string(f.file) + " from " + url + " has SHA-256 " + got +
                  ", expected " + f.sha256;
            continue;
        }
        why.clear();
        break;
    }
    if (!why.empty())
        nn::fail("%s.\n  Fetch it by hand from\n    %s%s%s\n  and save it as\n    %s",
                 why.c_str(), f.url, f.no_mirror ? "" : "\n  or\n    ",
                 f.no_mirror ? "" : urls[1].c_str(), dst.string().c_str());

    fs::rename(part, dst, ec);
    NN_CHECK(!ec, "cannot move the download into place: %s", ec.message().c_str());
    NN_LOG_INFO("[%s] saved %s\n", tag, dst.string().c_str());
    return dst.string();
}

}  // namespace nn
