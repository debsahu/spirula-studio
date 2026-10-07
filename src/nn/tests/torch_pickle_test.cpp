// nn::TorchCheckpoint against torch.save files written by real torch
// (tools/make_torch_fixtures.py): the 2-tensor fixture, the awkward shapes of
// a real state dict, and the refusals. A model's real checkpoint is read by a
// test beside the model.

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

}  // namespace

int main() {
    try {
        test_two_tensors();
        test_misc();
        test_refusals();
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        return 1;
    }
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
