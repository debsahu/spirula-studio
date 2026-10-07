// nn::TorchCheckpoint against files built to hurt it (tools/make_torch_fixtures.py,
// hostile/): each must be refused with the reason named, none may crash, read out
// of bounds or spend memory in proportion to anything but the file's own size.
// Meant to run under -fsanitize=address as well as plain.

#include "nn/core/Error.h"
#include "nn/io/TorchPickle.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/resource.h>
#endif

// ASAN's redzones inflate every allocation, so the RSS bound is a plain-build check.
#if defined(__SANITIZE_ADDRESS__)
#define SS_UNDER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SS_UNDER_ASAN 1
#endif
#endif

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

struct Case {
    const char* file;
    const char* reason;   // a substring of the refusal
    const char* defect;   // what a reader without the check would do
};

const Case kCases[] = {
    {"stride_short.pt", "shape and stride have", "reads stride[] past its end (null read)"},
    {"off_overflow.pt", "runs past the end", "offset + numel overflows int64 and passes"},
    {"off_wrap_read.pt", "runs past the end", "offset * elem wraps and reads before the buffer"},
    {"wrap_unsigned.pt", "claims 4611686018427387904 elements", "begin = offset * 8 wraps to a small number"},
    {"numel_wrap.pt", "more than 2^48 elements", "2^32 x 2^32 wraps numel to 0"},
    {"storage_lies.pt", "pickle says storage", "trusts the pickle's storage size over the zip's"},
    {"storage_bounds.pt", "runs past the end", "a tensor reaching one element past an honest storage"},
    {"dup_name.pt", "duplicate tensor", "the second w silently replaces the first"},
    {"build_kind.pt", "unsupported BUILD", "BUILD with a non-dict state is accepted"},
    {"unknown_opcode.pt", "unsupported pickle opcode 0xff", "skips what it cannot read"},
    {"deflated_storage.pt", "is compressed", "inflates a storage of unchecked size"},
    {"deep.pt", "nests deeper", "2M nested tuples overflow the stack when freed"},
    {"amp.pt", "limit", "a 48 KB file allocates 5.6 GB"},
    {"many_objects.pt", "more than 250000 objects", "3M opcodes become 3M heap objects"},
    // Memo/stack bounds: kept last, the aggregate RSS reading below stops before them.
    {"memo_keys.pt", "memoizes more than 250000", "6M MEMOIZE opcodes become 6M std::map nodes"},
    {"binget_stack.pt", "stack grows past 250000", "15M BINGET opcodes become 15M stack slots"},
    {"long_binget_stack.pt", "stack grows past 250000", "6M LONG_BINGET opcodes become 6M stack slots"},
    {"container_items.pt", "containers hold more than 250000 items",
     "16M items in 80 tuples of 200k, each under every per-object cap, are 128 MB of vectors"},
};

std::string refusal(const std::string& path) {
    try {
        nn::TorchCheckpoint c(path);
    } catch (const nn::Error& e) {
        return e.what();
    }
    return "<accepted>";
}

long peak_rss_mib() {
#ifdef _WIN32
    return 0;
#else
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
    return u.ru_maxrss / (1024 * 1024);
#else
    return u.ru_maxrss / 1024;
#endif
#endif
}

// The constructor validates the file it opened; read_raw opens it again. A file
// swapped in between, shorter than the pickle claims, must be refused by read_raw
// itself rather than read past its end.
void test_swapped_after_open(const std::string& dir) {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "torch_pickle_swap.pt";
    fs::copy_file(dir + "../two_tensors.pt", tmp, fs::copy_options::overwrite_existing);
    nn::TorchCheckpoint c(tmp.string());
    fs::copy_file(dir + "shrunk_storage.pt", tmp, fs::copy_options::overwrite_existing);
    std::string m = "<accepted>";
    try {
        c.read_raw("a.weight");
    } catch (const nn::Error& e) {
        m = e.what();
    }
    check(m.find("does not fit") != std::string::npos,
          "read_raw on a storage shrunk after open: refused ('does not fit')");
    std::error_code ec;
    fs::remove(tmp, ec);
}

}  // namespace

int main(int argc, char** argv) {   // argv[1]: run just that file
    const std::string dir = std::string(SS_REPO_ROOT) + "/src/nn/tests/data/hostile/";
    const long before = peak_rss_mib();
    long grew_before_memo = 0;
    for (const Case& c : kCases) {
        if (argc > 1 && std::string(argv[1]) != c.file) continue;
#if !defined(_WIN32) && !defined(SS_UNDER_ASAN)
        const long rss0 = peak_rss_mib();
        if (std::string(c.file) == "memo_keys.pt") grew_before_memo = rss0 - before;
#endif
        const std::string m = refusal(dir + c.file);
#if !defined(_WIN32) && !defined(SS_UNDER_ASAN)
        const long grew_here = peak_rss_mib() - rss0;   // high-water mark: sees only a new peak
#endif
        const bool ok = m.find(c.reason) != std::string::npos;
        check(ok, std::string(c.file) + ": refused as '" + c.reason + "'");
#if !defined(_WIN32) && !defined(SS_UNDER_ASAN)
        check(grew_here < 100, std::string(c.file) + ": peak RSS rose " +
                                   std::to_string(grew_here) + " MiB while refusing (< 100)");
#endif
        if (!ok) std::printf("     got: %s\n     (without the check: %s)\n", m.c_str(), c.defect);
    }
    if (argc == 1) test_swapped_after_open(dir);
    // The memory bound is the point of the three size cases; one peak reading
    // covers all of them, and the unbounded versions each run to GiB.
#if !defined(_WIN32) && !defined(SS_UNDER_ASAN)
    const long grew = grew_before_memo;   // the 14 original files; each new one is checked alone above
    if (argc == 1)
        check(grew < 100, "peak RSS grew " + std::to_string(grew) + " MiB over all " + std::string("14 original") + " files (< 100)");
#endif
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
