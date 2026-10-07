// `--accept-license` through the real `spirula` binary: Main.cpp consumes it for
// every command, wherever it stands in argv, before the command sees argv.
// Scratch config directory, stdin from /dev/null (so there is no terminal to ask).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
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

struct Run {
    int code = -1;
    std::string out;   // stdout and stderr together
};

fs::path g_root;

fs::path gui_conf() { return g_root / "config" / "spirula-studio" / "gui.conf"; }

void fresh() {
    std::error_code ec;
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root / "config", ec);
    fs::create_directories(g_root / "cache", ec);
}

Run run(const std::string& args, const std::string& env_prefix = "") {
    const fs::path log = g_root / "out.txt";
    const std::string env = env_prefix.empty()
        ? "XDG_CONFIG_HOME='" + (g_root / "config").string() + "' XDG_CACHE_HOME='" +
              (g_root / "cache").string() + "'"
        : env_prefix;
    // From an empty directory of its own, so "nothing was written to the working
    // directory" is a fact about this run and not about wherever ctest started.
    fs::create_directories(g_root / "cwd");
    const std::string cmd = "cd '" + (g_root / "cwd").string() + "' && " + env + " '" +
                            SS_SPIRULA_EXE + "' " + args + " < /dev/null > '" +
                            log.string() + "' 2>&1";
    Run r;
    const int rc = std::system(cmd.c_str());
#ifndef _WIN32
    r.code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#else
    r.code = rc;
#endif
    r.out = slurp(log);
    return r;
}

}  // namespace

int main() {
#ifdef _WIN32
    std::printf("SKIP (POSIX shell syntax)\n");
    return 0;
#else
    g_root = fs::temp_directory_path() / "spirula_accept_license_cli_test";

    fresh();
    Run r = run("--version");
    check(r.code == 0 && !fs::exists(gui_conf()), "baseline: --version alone records nothing");
    const std::string version = r.out;

    fresh();
    r = run("--accept-license dinov3=yes,romav2=yes --version");
    const std::string conf = slurp(gui_conf());
    check(r.code == 0 && has(r.out, version), "leading flag: the command still runs (--version)");
    check(has(conf, "accepted_license=dinov3\n") && has(conf, "accepted_license=romav2\n"),
          "leading flag: both families recorded in gui.conf");
    check(has(r.out, "# DINOv3 License") && has(r.out, "MIT License"),
          "leading flag: the full terms were printed first");

    fresh();
    r = run("--version --accept-license=romav2=yes");
    check(r.code == 0 && has(slurp(gui_conf()), "accepted_license=romav2\n"),
          "trailing --accept-license=family=yes spelling is consumed too");

    fresh();
    r = run("sam --accept-license dinov3=yes --help");
    check(r.code == 0 && has(slurp(gui_conf()), "accepted_license=dinov3\n") &&
              !has(r.out, "accept-license"),
          "after a subcommand: consumed, and the subcommand never sees it");

    fresh();
    r = run("--accept-license dinov3 --version");
    check(r.code == 2 && has(r.out, "dinov3=yes") && !has(r.out, version) &&
              !fs::exists(gui_conf()),
          "no terminal and no =yes: exit 2, names the =yes form, runs nothing, records nothing");

    fresh();
    r = run("--accept-license bogus=yes --version");
    check(r.code == 2 && has(r.out, "unknown licence") && !fs::exists(gui_conf()),
          "an unknown family: exit 2");

    fresh();
    r = run("--accept-license dinov3=yes --version",
            "env -u XDG_CONFIG_HOME -u HOME XDG_CACHE_HOME='" + (g_root / "cache").string() + "'");
    check(r.code == 2 && has(r.out, "XDG_CONFIG_HOME") && fs::is_empty(g_root / "cwd"),
          "no XDG_CONFIG_HOME and no HOME: refused, nothing written to the working directory");

    std::error_code ec;
    fs::remove_all(g_root, ec);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
#endif
}
