// The real romav2.0.1.pt through nn::TorchCheckpoint: all 907 tensors' names,
// dtypes, shapes and payload sums against a table dumped from torch.
//
//   roma_checkpoint_test [--require-real]     $SS_TEST_ROMAV2_PT = the .pt path

#include "nn/core/Error.h"
#include "nn/io/TorchPickle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) g_failures++;
}

std::string data_dir() { return std::string(SS_REPO_ROOT) + "/src/roma/model/tests/data/"; }

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
