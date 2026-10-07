// nn::TorchCheckpoint against torch.save files written by real torch
// (tools/make_torch_fixtures.py): the 2-tensor fixture, the awkward shapes of
// a real state dict, the refusals, and -- when the file is present -- all 907
// tensors of romav2.0.1.pt against a table dumped from torch.
//
//   torch_pickle_test [--require-real]     $SS_TEST_ROMAV2_PT = the .pt path

#include "nn/core/Error.h"
#include "nn/io/TorchPickle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

std::string data_dir() { return std::string(SS_REPO_ROOT) + "/src/nn/tests/data/"; }

// True when `fn` throws an nn::Error whose message contains `needle`.
bool throws_with(const std::function<void()>& fn, const char* needle) {
    try {
        fn();
    } catch (const nn::Error& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

bool same(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], 4) != 0) return false;   // bit-exact, -0.0 included
    return true;
}

void test_two_tensors() {
    nn::TorchCheckpoint c(data_dir() + "two_tensors.pt");
    check(c.names() == std::vector<std::string>{"a.weight", "b.bias"},
          "two_tensors: names, in the file's order");
    check(c.entry("a.weight").dtype == "F32" &&
              c.entry("a.weight").shape == std::vector<int64_t>{2, 3},
          "two_tensors: a.weight is F32 [2,3]");
    check(c.entry("b.bias").dtype == "BF16" &&
              c.entry("b.bias").shape == std::vector<int64_t>{5},
          "two_tensors: b.bias is BF16 [5]");
    const nn::OnnxTensor a = c.read("a.weight");
    check(same(a.data, {0.25f, 0.75f, 1.25f, 1.75f, 2.25f, 2.75f}) && !a.was_f16,
          "two_tensors: a.weight values");
    const nn::OnnxTensor b = c.read("b.bias");
    check(same(b.data, {1.5f, -2.25f, 0.10009765625f, 65536.0f, -0.0f}) && !b.was_f16,
          "two_tensors: b.bias is the bf16 rounding of 0.1, widened exactly");
    check(c.read_raw("b.bias").size() == 10, "two_tensors: read_raw is 5 x 2 bytes");
    check(throws_with([&] { c.read("nope"); }, "nope"), "two_tensors: a missing name throws");
}

void test_misc() {
    nn::TorchCheckpoint c(data_dir() + "wrapped_misc.pt");
    check(c.names().size() == 7, "misc: the {\"state_dict\": ...} wrapper is looked through");
    check(same(c.read("view.tail").data, {10, 11, 12, 13, 14, 15}),
          "misc: a view at storage offset 10 reads elements 10..15, not 0..5");
    check(same(c.read("view.head").data, {0, 1, 2, 3}),
          "misc: a second view of the same storage reads its own range");
    check(c.entry("mask").dtype == "BOOL" &&
              c.read_raw("mask") == std::vector<uint8_t>{1, 0, 1},
          "misc: a bool buffer reads as raw 1/0 bytes");
    check(c.entry("idx").dtype == "I64" && c.read_raw("idx").size() == 24,
          "misc: int64 is 8 bytes an element");
    int64_t big = 0;
    std::memcpy(&big, c.read_raw("idx").data() + 16, 8);
    check(big == 3000000000ll, "misc: int64 payload is intact beyond 32 bits");
    check(throws_with([&] { c.read("idx"); }, "idx"), "misc: read() refuses a non-float");
    const nn::OnnxTensor h = c.read("half");
    check(h.was_f16 && same(h.data, {0.5f, -1.0f, 3.0f}), "misc: f16 sets was_f16");
    const nn::OnnxTensor s = c.read("scalar");
    check(s.shape.empty() && s.numel() == 1 && same(s.data, {7.25f}), "misc: 0-d tensor");
    check(c.read("empty").numel() == 0 && c.entry("empty").shape == std::vector<int64_t>{0, 4},
          "misc: an empty tensor");
}

void test_refusals() {
    check(throws_with([] { nn::TorchCheckpoint c(data_dir() + "transposed.pt"); },
                      "contiguous"),
          "a non-contiguous tensor is refused, naming why");
    check(throws_with([] { nn::TorchCheckpoint c(data_dir() + "no_such.pt"); }, "no_such.pt"),
          "a missing file is refused, naming the path");

    std::ifstream in(data_dir() + "two_tensors.pt", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string whole = ss.str();
    const char* tmp = "torch_pickle_test.tmp";
    auto write = [&](const std::string& bytes) {
        std::ofstream o(tmp, std::ios::binary);
        o << bytes;
    };
    write(whole.substr(0, whole.size() / 2));
    check(throws_with([&] { nn::TorchCheckpoint c(tmp); }, tmp), "a truncated zip is refused");
    write(std::string(4096, 'x'));
    check(throws_with([&] { nn::TorchCheckpoint c(tmp); }, tmp), "not a zip at all is refused");

    // A pickle that would call os.system is refused by name, never run.
    check(throws_with([] { nn::TorchCheckpoint c(data_dir() + "evil_reduce.pt"); },
                      "unsupported global"),
          "a pickle importing os.system is refused as an unsupported global");
    std::remove(tmp);
}

struct Row {
    std::string name, dtype, shape;
    double sum = 0, abs_sum = 0;
};

void test_real(const std::string& path) {
    std::ifstream tab(data_dir() + "romav2_0_1_tensors.txt");
    std::vector<Row> rows;
    for (std::string line; std::getline(tab, line);) {
        std::istringstream l(line);
        Row r;
        l >> r.name >> r.dtype >> r.shape >> r.sum >> r.abs_sum;
        rows.push_back(r);
    }
    check(rows.size() == 907, "real: the table holds 907 tensors");

    nn::TorchCheckpoint c(path);
    check(c.names().size() == 907, "real: the file lists 907 tensors");
    int bad_meta = 0, bad_sum = 0, shown = 0;
    std::string first_bad;
    for (size_t i = 0; i < rows.size() && i < c.names().size(); ++i) {
        const Row& r = rows[i];
        if (c.names()[i] != r.name) { bad_meta++; first_bad = first_bad.empty() ? r.name : first_bad; continue; }
        const auto& e = c.entry(r.name);
        std::string shape;
        for (size_t d = 0; d < e.shape.size(); ++d)
            shape += (d ? "x" : "") + std::to_string(e.shape[d]);
        if (shape.empty()) shape = "-";
        if (e.dtype != r.dtype || shape != r.shape) {
            bad_meta++;
            if (first_bad.empty()) first_bad = r.name + " " + e.dtype + " " + shape;
            continue;
        }
        double s = 0, a = 0;
        if (e.dtype == "I64") {
            const auto raw = c.read_raw(r.name);
            for (size_t k = 0; k + 8 <= raw.size(); k += 8) {
                int64_t v;
                std::memcpy(&v, raw.data() + k, 8);
                s += (double)v;
                a += std::fabs((double)v);
            }
        } else {
            for (float v : c.read(r.name).data) {
                s += v;
                a += std::fabs((double)v);
            }
        }
        // Summation order differs from torch's; the bound is a few ulp of the
        // absolute sum, orders of magnitude below any wrong offset or dtype.
        const double tol = 1e-9 * r.abs_sum + 1e-12;
        if (std::fabs(s - r.sum) > tol || std::fabs(a - r.abs_sum) > tol) {
            bad_sum++;
            if (shown++ < 5)
                std::printf("     %s: sum %.12g want %.12g, abs %.12g want %.12g\n",
                            r.name.c_str(), s, r.sum, a, r.abs_sum);
        }
    }
    check(bad_meta == 0, "real: all 907 names, dtypes and shapes match torch" +
                             (first_bad.empty() ? "" : " (first: " + first_bad + ")"));
    check(bad_sum == 0, "real: every tensor's payload sums match torch (" +
                            std::to_string(bad_sum) + " differ)");
}

}  // namespace

int main(int argc, char** argv) {
    bool require_real = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--require-real") require_real = true;
    try {
        test_two_tensors();
        test_misc();
        test_refusals();
        const char* real = std::getenv("SS_TEST_ROMAV2_PT");
        if (real && *real) {
            test_real(real);
        } else {
            std::printf("SKIP real romav2.0.1.pt (set SS_TEST_ROMAV2_PT)\n");
            if (require_real) check(false, "--require-real but SS_TEST_ROMAV2_PT is unset");
        }
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        return 1;
    }
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
