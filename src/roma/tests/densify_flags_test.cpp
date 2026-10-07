// The flag parser of `spirula densify`: what a whole-number flag accepts.
#include <cstdio>
#include <string>

#include "roma/FlagParse.h"
#include "sfm/tests/TestMain.h"

namespace {

int fails = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    fails += !ok;
}

bool accepts(const char* v, long long lo, long long hi, long long want) {
    long long got = -12345;
    return roma::parseWhole(v, lo, hi, &got) && got == want;
}

bool refuses(const char* v, long long lo, long long hi) {
    long long got = 777;
    return !roma::parseWhole(v, lo, hi, &got) && got == 777;
}

int body(int, char**) {
    check(accepts("2", 2, 1000, 2) && accepts("16", 2, 1000, 16), "a whole number in range is taken");
    check(refuses("2.5", 2, 1000), "a fraction is refused, not cut to 2");
    check(refuses("2.0", 2, 1000), "a whole number spelled with a fraction is refused");
    check(refuses("1e1", 2, 1000), "an exponent is refused");
    check(refuses("", 2, 1000) && refuses(" 3", 2, 1000) && refuses("3 ", 2, 1000) && refuses("3x", 2, 1000),
          "empty, padded and trailing text are refused");
    check(refuses("1", 2, 1000) && refuses("1001", 2, 1000), "outside the range is refused at both ends");
    check(accepts("2", 2, 1000, 2) && accepts("1000", 2, 1000, 1000), "both ends of the range are inside it");
    check(refuses("nan", 2, 1000) && refuses("inf", 2, 1000), "nan and inf are refused");
    check(refuses("99999999999999999999", 2, 1000), "a number past 64 bits is refused");
    check(accepts("-3", -10, 10, -3), "a negative number is taken when the range allows it");
    return fails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) { return sfmTestMain(argc, argv, body); }
