// Ported from Lichtfeld-Densification-Plugin (GPL-3.0-or-later), Copyright (c) 2025 Shady Gmira and contributors; core/sampling.py@ab0b04e.
#include "roma/Sample.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>

namespace roma {

std::vector<int64_t> sampleWithCoverage(const std::vector<float>& certainty, int w, int h,
                                        const SampleOptions& opt) {
    const int64_t n = (int64_t)w * h;
    std::vector<double> weight((size_t)n);
    for (int64_t i = 0; i < n; i++) weight[(size_t)i] = std::min(certainty[(size_t)i], opt.cap);
    if (opt.no_filter) {
        std::vector<int64_t> order((size_t)n);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(),
                         [&](int64_t a, int64_t b) { return weight[(size_t)a] > weight[(size_t)b]; });
        order.resize((size_t)std::min<int64_t>(opt.count, n));
        std::sort(order.begin(), order.end());
        return order;
    }
    double total = 0;
    int64_t nonzero = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double& v = weight[(size_t)y * w + x];
            const bool inside = x >= opt.border && x <= w - 1 - opt.border && y >= opt.border &&
                                y <= h - 1 - opt.border;
            if (!inside || !(v > 0)) v = 0;
            total += v;
            nonzero += v > 0;
        }
    if (!(total > 0)) return {};

    const int64_t m_main = std::min<int64_t>((int64_t)(opt.count * opt.weighted_share), nonzero);
    std::mt19937_64 rng(opt.seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::vector<std::pair<double, int64_t>> keys;
    keys.reserve((size_t)nonzero);
    for (int64_t i = 0; i < n; i++)
        if (weight[(size_t)i] > 0) {
            const double u = std::max(uni(rng), 1e-300);
            keys.push_back({std::log(u) / weight[(size_t)i], i});
        }
    // nth_element past the end is undefined behaviour, not an error: say so.
    if (m_main > (int64_t)keys.size()) throw std::logic_error("more weighted draws than non-zero pixels");
    std::nth_element(keys.begin(), keys.begin() + m_main, keys.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::set<int64_t> chosen;
    for (int64_t k = 0; k < m_main; k++) chosen.insert(keys[(size_t)k].second);

    const int tile = std::max(1, w / opt.tiles);
    std::vector<int64_t> order;
    order.reserve((size_t)nonzero);
    for (int64_t i = 0; i < n; i++)
        if (weight[(size_t)i] > 0) order.push_back(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](int64_t a, int64_t b) { return weight[(size_t)a] > weight[(size_t)b]; });
    std::set<int64_t> seen_bins;
    int64_t cover = 0;
    const int64_t want = opt.count - m_main;
    for (int64_t i : order) {
        if (cover >= want) break;
        const int64_t bin = (int64_t)((i % w) / tile) * 100000 + (i / w) / tile;
        if (!seen_bins.insert(bin).second) continue;
        chosen.insert(i);
        cover++;
    }
    return std::vector<int64_t>(chosen.begin(), chosen.end());
}

}  // namespace roma
