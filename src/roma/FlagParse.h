// Number parsing for `spirula densify`'s flags, header-only so a test can run it.
#pragma once

#include <charconv>
#include <string>

namespace roma {

// A whole number in [lo, hi]; 2.5, 2.0, 1e1, " 3", "+3" and trailing text are refused.
inline bool parseWhole(const std::string& v, long long lo, long long hi, long long* out) {
    long long n = 0;
    const char* end = v.data() + v.size();
    const std::from_chars_result r = std::from_chars(v.data(), end, n);
    if (v.empty() || r.ec != std::errc() || r.ptr != end || n < lo || n > hi) return false;
    *out = n;
    return true;
}

}  // namespace roma
