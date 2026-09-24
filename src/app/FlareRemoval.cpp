// Detection and fitting run on the factor-4 working image of a lens; only the
// subtraction touches full-resolution pixels, and only inside a ghost's reach.
// Based on
// https://github.com/Kemerd/OpenOSV/blob/e169fe1dd7b1e2da7cd0a52734393dc155670722/src/osv/render/Flare.cpp
// SPDX-FileCopyrightText: Copyright 2026 The OpenOSV Contributors
// SPDX-License-Identifier: Apache-2.0

#include "app/FlareRemoval.h"

#include "app/DepthPng.h"
#include "core/DlogM.h"
#include "external/stb_image.h"
#include "external/stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace app::flare {
namespace {

constexpr double kPi = 3.14159265358979323846;

// BT.2020 luma weights on linear RGB; the ghost and its background use the same.
constexpr double kLumaR = 0.2627, kLumaG = 0.6780, kLumaB = 0.0593;

enum FitParam : int { kPx = 0, kPy, kPhx, kPhy, kPrho, kPang, kPsoft, kNumFitParams };

// Per channel: 6 quadratic background terms, then plateau, rim, tilt x, tilt y.
constexpr int kNumBackground = 6;
constexpr int kNumGhostTerms = 4;
constexpr int kNumLinear = kNumBackground + kNumGhostTerms;

double luma_of(const double* rgb) { return kLumaR * rgb[0] + kLumaG * rgb[1] + kLumaB * rgb[2]; }

template <class... T>
bool all_finite(T... v) {
    return (std::isfinite((double)v) && ...);
}

// ============================================================================
// Small single-channel images
// ============================================================================

struct Gray {
    uint32_t w = 0, h = 0;
    std::vector<float> v;
    Gray() = default;
    Gray(uint32_t width, uint32_t height, float fill = 0.0f)
        : w(width), h(height), v((size_t)width * height, fill) {}
    float at(uint32_t x, uint32_t y) const { return v[(size_t)y * w + x]; }
    float& at(uint32_t x, uint32_t y) { return v[(size_t)y * w + x]; }
};

// Box filter of half width r along one axis, clamp to edge, double running sum.
void box_pass(const Gray& src, Gray& dst, int r, bool horizontal) {
    dst.w = src.w;
    dst.h = src.h;
    dst.v.resize(src.v.size());
    if (r <= 0) {
        dst.v = src.v;
        return;
    }
    const int W = (int)src.w, H = (int)src.h;
    const int lines = horizontal ? H : W, len = horizontal ? W : H;
    const double norm = 1.0 / (2 * r + 1);
    auto idx = [&](int l, int i) -> size_t {
        return horizontal ? (size_t)l * W + i : (size_t)i * W + l;
    };
    for (int l = 0; l < lines; ++l) {
        double sum = 0.0;
        for (int k = -r; k <= r; ++k) sum += src.v[idx(l, std::clamp(k, 0, len - 1))];
        for (int i = 0; i < len; ++i) {
            dst.v[idx(l, i)] = (float)(sum * norm);
            const int out = std::clamp(i - r, 0, len - 1);
            const int in = std::clamp(i + r + 1, 0, len - 1);
            sum += (double)src.v[idx(l, in)] - (double)src.v[idx(l, out)];
        }
    }
}

// Gaussian as three box passes per axis (Wells, IEEE PAMI 1986): O(1) per pixel.
Gray gauss_boxes(const Gray& src, double sigma) {
    if (!(sigma > 0.3) || src.v.empty()) return src;
    constexpr int n = 3;
    const double w_ideal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
    int wl = (int)std::floor(w_ideal);
    if (wl % 2 == 0) --wl;
    wl = std::max(wl, 1);
    const int wu = wl + 2;
    const double m_ideal =
        (12.0 * sigma * sigma - n * wl * wl - 4.0 * n * wl - 3.0 * n) / (-4.0 * wl - 4.0);
    const int m = std::clamp((int)std::lround(m_ideal), 0, n);
    Gray a = src, b;
    for (int pass = 0; pass < n; ++pass) {
        const int r = ((pass < m ? wl : wu) - 1) / 2;
        box_pass(a, b, r, true);
        box_pass(b, a, r, false);
    }
    return a;
}

// Exact separable Gaussian, radius ceil(3 sigma): the small sigmas boxes miss.
Gray gauss_exact(const Gray& src, double sigma) {
    if (!(sigma > 0.05) || src.v.empty()) return src;
    const int r = std::max(1, (int)std::ceil(3.0 * sigma));
    std::vector<double> k((size_t)(2 * r + 1));
    double sum = 0.0;
    for (int i = -r; i <= r; ++i) sum += k[(size_t)(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma));
    for (double& e : k) e /= sum;
    const int W = (int)src.w, H = (int)src.h;
    Gray tmp(src.w, src.h), out(src.w, src.h);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double acc = 0.0;
            for (int i = -r; i <= r; ++i)
                acc += k[(size_t)(i + r)] * src.at((uint32_t)std::clamp(x + i, 0, W - 1), (uint32_t)y);
            tmp.at((uint32_t)x, (uint32_t)y) = (float)acc;
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double acc = 0.0;
            for (int i = -r; i <= r; ++i)
                acc += k[(size_t)(i + r)] * tmp.at((uint32_t)x, (uint32_t)std::clamp(y + i, 0, H - 1));
            out.at((uint32_t)x, (uint32_t)y) = (float)acc;
        }
    return out;
}

template <class Pred>
double median_of(const Gray& img, Pred keep) {
    std::vector<float> vals;
    vals.reserve(img.v.size() / 2);
    for (uint32_t y = 0; y < img.h; ++y)
        for (uint32_t x = 0; x < img.w; ++x)
            if (keep(x, y)) vals.push_back(img.at(x, y));
    if (vals.empty()) return 0.0;
    auto mid = vals.begin() + (ptrdiff_t)(vals.size() / 2);
    std::nth_element(vals.begin(), mid, vals.end());
    return *mid;
}

// ============================================================================
// Connected components (8-connected)
// ============================================================================

struct Blob {
    uint32_t area = 0;
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // inclusive
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;  // raw moments of pixel centres
    double peak = -std::numeric_limits<double>::infinity();
    double cx() const { return area ? sx / area : 0.0; }
    double cy() const { return area ? sy / area : 0.0; }
    uint32_t bw() const { return x1 - x0 + 1; }
    uint32_t bh() const { return y1 - y0 + 1; }
    double fill() const { return (double)area / ((double)bw() * bh()); }
    double aspect() const { return (double)std::max(bw(), bh()) / std::min(bw(), bh()); }
};

std::vector<Blob> find_blobs(const std::vector<uint8_t>& mask, uint32_t w, uint32_t h,
                             const Gray* score) {
    std::vector<Blob> blobs;
    if (mask.size() != (size_t)w * h) return blobs;
    std::vector<uint8_t> seen(mask.size(), 0);
    std::vector<uint32_t> stack;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const size_t i0 = (size_t)y * w + x;
            if (!mask[i0] || seen[i0]) continue;
            Blob b;
            b.x0 = b.x1 = x;
            b.y0 = b.y1 = y;
            seen[i0] = 1;
            stack.assign(1, (uint32_t)i0);
            while (!stack.empty()) {
                const uint32_t i = stack.back();
                stack.pop_back();
                const uint32_t px = i % w, py = i / w;
                const double cxp = px + 0.5, cyp = py + 0.5;
                ++b.area;
                b.sx += cxp;
                b.sy += cyp;
                b.sxx += cxp * cxp;
                b.syy += cyp * cyp;
                b.sxy += cxp * cyp;
                b.x0 = std::min(b.x0, px);
                b.x1 = std::max(b.x1, px);
                b.y0 = std::min(b.y0, py);
                b.y1 = std::max(b.y1, py);
                if (score) b.peak = std::max(b.peak, (double)score->v[i]);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = (int)px + dx, ny = (int)py + dy;
                        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= (int)w ||
                            ny >= (int)h)
                            continue;
                        const size_t j = (size_t)ny * w + nx;
                        if (mask[j] && !seen[j]) {
                            seen[j] = 1;
                            stack.push_back((uint32_t)j);
                        }
                    }
            }
            blobs.push_back(b);
        }
    return blobs;
}

// ============================================================================
// The ghost shape, double precision (twin of the float kernel below)
// ============================================================================

struct ShapeParams {
    double p[kNumFitParams] = {};
};

struct ShapeGeom {
    double cx, cy, c, s, hx, hy, r, soft;
    explicit ShapeGeom(const ShapeParams& sp)
        : cx(sp.p[kPx]), cy(sp.p[kPy]), c(std::cos(sp.p[kPang])), s(std::sin(sp.p[kPang])),
          hx(sp.p[kPhx]), hy(sp.p[kPhy]),
          r(std::clamp(sp.p[kPrho], 0.0, 1.0) * std::min(sp.p[kPhx], sp.p[kPhy])),
          soft(sp.p[kPsoft]) {}
};

// Signed distance to the rounded rectangle (Inigo Quilez's box SDF), negative
// inside; u, v are the local coordinates scaled to +/-1 at the half extents.
double shape_distance(const ShapeGeom& g, double x, double y, double* u = nullptr,
                      double* v = nullptr) {
    const double dx = x - g.cx, dy = y - g.cy;
    const double sx = dx * g.c + dy * g.s;
    const double sy = -dx * g.s + dy * g.c;
    if (u) *u = sx / std::max(g.hx, 1e-3);
    if (v) *v = sy / std::max(g.hy, 1e-3);
    const double qx = std::fabs(sx) - (g.hx - g.r);
    const double qy = std::fabs(sy) - (g.hy - g.r);
    const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - g.r;
}

double shape_distance(const ShapeParams& sp, double x, double y) {
    return shape_distance(ShapeGeom(sp), x, y);
}

double plateau_weight(double d, double soft) {
    const double s = std::max(soft, 1e-3);
    const double t = std::clamp((s - d) / (2.0 * s), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// (1 - (d / 3 soft)^2)^2 inside |d| < 3 soft, exactly 0 beyond.
double rim_weight(double d, double soft) {
    const double t = d / (3.0 * std::max(soft, 1e-3));
    if (!(t * t < 1.0)) return 0.0;
    const double b = 1.0 - t * t;
    return b * b;
}

void ghost_terms(const ShapeGeom& g, double x, double y, double* out4) {
    double u = 0.0, v = 0.0;
    const double d = shape_distance(g, x, y, &u, &v);
    const double s = plateau_weight(d, g.soft);
    out4[0] = s;
    out4[1] = rim_weight(d, g.soft);
    out4[2] = s * u;
    out4[3] = s * v;
}

// ============================================================================
// Ghost fit: Levenberg-Marquardt over the shape, linear solve for the rest
// ============================================================================

struct FitWindow {
    std::vector<double> x, y, rgb, basis;  // rgb 3 and basis 6 per pixel
    size_t size() const { return x.size(); }
};

// Cholesky with a relative ridge; false when singular even so.
bool solve_spd(double* A, double* b, int n) {
    double trace = 0.0;
    for (int i = 0; i < n; ++i) trace += A[i * n + i];
    const double ridge = 1e-12 * std::max(trace, 1e-30);
    for (int i = 0; i < n; ++i) A[i * n + i] += ridge;
    for (int j = 0; j < n; ++j) {
        double d = A[j * n + j];
        for (int k = 0; k < j; ++k) d -= A[j * n + k] * A[j * n + k];
        if (!(d > 0.0)) return false;
        d = std::sqrt(d);
        A[j * n + j] = d;
        for (int i = j + 1; i < n; ++i) {
            double v = A[i * n + j];
            for (int k = 0; k < j; ++k) v -= A[i * n + k] * A[j * n + k];
            A[i * n + j] = v / d;
        }
    }
    for (int i = 0; i < n; ++i) {
        double v = b[i];
        for (int k = 0; k < i; ++k) v -= A[i * n + k] * b[k];
        b[i] = v / A[i * n + i];
    }
    for (int i = n - 1; i >= 0; --i) {
        double v = b[i];
        for (int k = i + 1; k < n; ++k) v -= A[k * n + i] * b[k];
        b[i] = v / A[i * n + i];
    }
    return true;
}

struct Eval {
    double coef[3][kNumLinear] = {};
    std::vector<double> resid;  // 3 per pixel
    double ssr = std::numeric_limits<double>::infinity();
    std::vector<double> terms;  // kNumGhostTerms per pixel
};

enum class Model { Background, Plateau, Full };

// For a fixed shape, the background and ghost terms of every channel by linear
// least squares (variable projection), and the residual.
bool evaluate(const FitWindow& win, const ShapeParams& sp, Model model, Eval& out) {
    const size_t n = win.size();
    const bool with_ghost = model != Model::Background;
    const int m = model == Model::Full ? kNumLinear : (with_ghost ? kNumBackground + 1 : kNumBackground);
    out.terms.assign(n * kNumGhostTerms, 0.0);
    if (with_ghost) {
        const ShapeGeom g(sp);
        for (size_t i = 0; i < n; ++i) ghost_terms(g, win.x[i], win.y[i], &out.terms[i * kNumGhostTerms]);
    }
    double AtA[kNumLinear * kNumLinear] = {};
    double Atb[3][kNumLinear] = {};
    double row[kNumLinear];
    for (size_t i = 0; i < n; ++i) {
        for (int k = 0; k < kNumBackground; ++k) row[k] = win.basis[i * kNumBackground + k];
        for (int k = 0; k < kNumGhostTerms; ++k) row[kNumBackground + k] = out.terms[i * kNumGhostTerms + k];
        for (int a = 0; a < m; ++a) {
            for (int b = a; b < m; ++b) AtA[a * m + b] += row[a] * row[b];
            for (int c = 0; c < 3; ++c) Atb[c][a] += row[a] * win.rgb[i * 3 + c];
        }
    }
    for (int a = 0; a < m; ++a)
        for (int b = 0; b < a; ++b) AtA[a * m + b] = AtA[b * m + a];
    // A ghost term with no footprint in the window solves to 0, not singular.
    for (int a = kNumBackground; a < m; ++a)
        if (AtA[a * m + a] <= 1e-18) AtA[a * m + a] = 1.0;
    for (int c = 0; c < 3; ++c) {
        double A[kNumLinear * kNumLinear], b[kNumLinear];
        std::memcpy(A, AtA, sizeof(double) * (size_t)(m * m));
        std::memcpy(b, Atb[c], sizeof(double) * (size_t)m);
        if (!solve_spd(A, b, m)) return false;
        for (int k = 0; k < kNumLinear; ++k) out.coef[c][k] = k < m ? b[k] : 0.0;
    }
    out.resid.resize(n * 3);
    double ssr = 0.0;
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c) {
            double pred = 0.0;
            for (int k = 0; k < kNumBackground; ++k) pred += out.coef[c][k] * win.basis[i * kNumBackground + k];
            for (int k = 0; k < kNumGhostTerms; ++k)
                pred += out.coef[c][kNumBackground + k] * out.terms[i * kNumGhostTerms + k];
            const double r = win.rgb[i * 3 + c] - pred;
            out.resid[i * 3 + c] = r;
            ssr += r * r;
        }
    out.ssr = ssr;
    return std::isfinite(ssr);
}

struct Bounds {
    double lo[kNumFitParams], hi[kNumFitParams];
};

// The angle wraps rather than clamps: a rectangle turned by pi is the same one.
void clamp_params(ShapeParams& sp, const Bounds& bd) {
    while (sp.p[kPang] > 0.5 * kPi) sp.p[kPang] -= kPi;
    while (sp.p[kPang] < -0.5 * kPi) sp.p[kPang] += kPi;
    for (int k = 0; k < kNumFitParams; ++k)
        if (k != kPang) sp.p[k] = std::clamp(sp.p[k], bd.lo[k], bd.hi[k]);
}

// Never returns a shape worse than the start; `best` gets its evaluation.
ShapeParams fit_shape(const FitWindow& win, ShapeParams start, const Bounds& bd, int max_iterations,
                      Model model, Eval& best) {
    clamp_params(start, bd);
    if (!evaluate(win, start, model, best)) {
        best.ssr = std::numeric_limits<double>::infinity();
        return start;
    }
    // Forward-difference steps (working px / radians).
    static constexpr double kStep[kNumFitParams] = {0.05, 0.05, 0.05, 0.05, 0.01, 0.005, 0.02};
    const size_t nr = best.resid.size();
    std::vector<double> J(nr * kNumFitParams);
    ShapeParams cur = start;
    double lambda = 1e-3;
    Eval trial;
    for (int it = 0; it < max_iterations; ++it) {
        for (int k = 0; k < kNumFitParams; ++k) {
            ShapeParams sp = cur;
            sp.p[k] += kStep[k];
            if (!evaluate(win, sp, model, trial)) return cur;
            for (size_t i = 0; i < nr; ++i) J[i * kNumFitParams + k] = (trial.resid[i] - best.resid[i]) / kStep[k];
        }
        double JtJ[kNumFitParams * kNumFitParams] = {};
        double Jtr[kNumFitParams] = {};
        for (size_t i = 0; i < nr; ++i) {
            const double* ji = &J[i * kNumFitParams];
            for (int a = 0; a < kNumFitParams; ++a) {
                Jtr[a] += ji[a] * best.resid[i];
                for (int b = a; b < kNumFitParams; ++b) JtJ[a * kNumFitParams + b] += ji[a] * ji[b];
            }
        }
        for (int a = 0; a < kNumFitParams; ++a)
            for (int b = 0; b < a; ++b) JtJ[a * kNumFitParams + b] = JtJ[b * kNumFitParams + a];
        bool improved = false;
        for (int attempt = 0; attempt < 8 && !improved; ++attempt) {
            double A[kNumFitParams * kNumFitParams], delta[kNumFitParams];
            for (int a = 0; a < kNumFitParams; ++a) {
                for (int b = 0; b < kNumFitParams; ++b) A[a * kNumFitParams + b] = JtJ[a * kNumFitParams + b];
                A[a * kNumFitParams + a] += lambda * (JtJ[a * kNumFitParams + a] + 1e-12);
                delta[a] = -Jtr[a];
            }
            if (!solve_spd(A, delta, kNumFitParams)) {
                lambda *= 10.0;
                continue;
            }
            ShapeParams next = cur;
            for (int k = 0; k < kNumFitParams; ++k) next.p[k] += delta[k];
            clamp_params(next, bd);
            if (evaluate(win, next, model, trial) && trial.ssr < best.ssr) {
                const double gain = (best.ssr - trial.ssr) / std::max(best.ssr, 1e-30);
                cur = next;
                std::swap(best, trial);
                lambda = std::max(lambda / 3.0, 1e-9);
                improved = true;
                if (gain < 1e-4) return cur;
            } else {
                lambda *= 4.0;
            }
        }
        if (!improved) break;
    }
    return cur;
}

// ============================================================================
// Sun detection
// ============================================================================

struct SunBlob {
    bool found = false;
    double x = 0, y = 0, radius = 0;
};

SunBlob detect_sun(const Gray& luma, const std::vector<uint8_t>& inside, const Params& fp) {
    SunBlob sun;
    double max_y = 0.0;
    for (size_t i = 0; i < luma.v.size(); ++i)
        if (inside[i] && std::isfinite(luma.v[i])) max_y = std::max(max_y, (double)luma.v[i]);
    // Every other pixel both ways: a quarter of the nth_element work.
    const double med = median_of(luma, [&](uint32_t x, uint32_t y) {
        return ((x | y) & 1u) == 0u && inside[(size_t)y * luma.w + x] != 0;
    });
    if (!(max_y > 0.0) || !(med > 0.0) || max_y < fp.sun_min_ratio_to_median * med) return sun;
    const double level = fp.sun_level_fraction * max_y;
    std::vector<uint8_t> mask(luma.v.size(), 0);
    for (size_t i = 0; i < luma.v.size(); ++i) mask[i] = (inside[i] && luma.v[i] >= level) ? 1 : 0;
    // The LARGEST clipped region must itself be compact. Taking the largest
    // compact one instead crowned a glint beside a ragged sunlit fuselage.
    const std::vector<Blob> blobs = find_blobs(mask, luma.w, luma.h, nullptr);
    const Blob* best = nullptr;
    for (const Blob& b : blobs)
        if (!best || b.area > best->area) best = &b;
    if (!best || best->area < fp.sun_min_area || best->aspect() > fp.sun_max_aspect ||
        best->fill() < fp.sun_min_fill)
        return sun;
    sun.found = true;
    sun.radius = std::sqrt((double)best->area / kPi);
    // Sub-pixel centroid weighted by (Y - level) / (max - level): a yes/no mask
    // moved it ~1.5 px frame to frame. A glint beyond 1.5 radii + 2 cannot pull it.
    const double cx0 = best->cx(), cy0 = best->cy();
    const double reach = 1.5 * sun.radius + 2.0;
    const double span = std::max(max_y - level, 1e-12);
    const int x0 = std::max(0, (int)std::floor(cx0 - reach));
    const int y0 = std::max(0, (int)std::floor(cy0 - reach));
    const int x1 = std::min((int)luma.w - 1, (int)std::ceil(cx0 + reach));
    const int y1 = std::min((int)luma.h - 1, (int)std::ceil(cy0 + reach));
    double sw = 0.0, swx = 0.0, swy = 0.0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const size_t i = (size_t)y * luma.w + x;
            const double px = x + 0.5, py = y + 0.5;
            if (!inside[i] || (px - cx0) * (px - cx0) + (py - cy0) * (py - cy0) > reach * reach) continue;
            const double w = std::clamp(((double)luma.v[i] - level) / span, 0.0, 1.0);
            sw += w;
            swx += w * px;
            swy += w * py;
        }
    sun.x = sw > 0.0 ? swx / sw : cx0;
    sun.y = sw > 0.0 ? swy / sw : cy0;
    return sun;
}

// The sun as the largest blob clipped in every channel (a saturated colour
// clips one), when that blob is sun-sized and round. `r_max` in working px.
SunBlob detect_clipped_sun(const Image& img, const std::vector<uint8_t>& inside, double r_max,
                           const Params& fp) {
    SunBlob sun;
    const float level = code_to_linear((float)fp.clip_code, img.encoding);
    std::vector<uint8_t> mask(inside.size(), 0);
    for (size_t i = 0; i < mask.size(); ++i) {
        const float* p = &img.rgb[i * 3];
        mask[i] = inside[i] && std::min({p[0], p[1], p[2]}) >= level ? 1 : 0;
    }
    const std::vector<Blob> blobs = find_blobs(mask, img.w, img.h, nullptr);
    const Blob* best = nullptr;
    for (const Blob& b : blobs)
        if (!best || b.area > best->area) best = &b;
    if (!best || best->area < fp.sun_min_area) return sun;
    const double n = best->area, cx = best->cx(), cy = best->cy();
    const double vxx = std::max(best->sxx / n - cx * cx, 0.0), vyy = std::max(best->syy / n - cy * cy, 0.0);
    const double vxy = best->sxy / n - cx * cy;
    const double disc = std::sqrt(0.25 * (vxx - vyy) * (vxx - vyy) + vxy * vxy);
    const double l1 = 0.5 * (vxx + vyy) + disc, l2 = std::max(0.5 * (vxx + vyy) - disc, 1e-12);
    // A solid ellipse has variances a^2/4 and b^2/4, so its area is 4 pi sqrt(l1 l2).
    const double radius = std::sqrt(n / kPi);
    if (radius > fp.clip_max_radius * r_max || std::sqrt(l1 / l2) > fp.clip_max_aspect ||
        n / (4.0 * kPi * std::sqrt(l1 * l2)) < fp.clip_min_fill)
        return sun;
    sun.found = true;
    sun.x = cx;
    sun.y = cy;
    sun.radius = radius;
    return sun;
}

// ============================================================================
// Seeding and fitting
// ============================================================================

struct Seed {
    double x = 0, y = 0, hx = 1, hy = 1, angle = 0, extent = 1;
};

// A uniform ellipse's variance along an axis is a^2 / 4: half extent 2 sqrt(eig).
Seed seed_from_blob(const Blob& b) {
    Seed s;
    s.x = b.cx();
    s.y = b.cy();
    const double n = (double)std::max<uint32_t>(b.area, 1);
    const double vxx = std::max(b.sxx / n - s.x * s.x, 0.0);
    const double vyy = std::max(b.syy / n - s.y * s.y, 0.0);
    const double vxy = b.sxy / n - s.x * s.y;
    const double tr = vxx + vyy;
    const double disc = std::sqrt(std::max(0.25 * (vxx - vyy) * (vxx - vyy) + vxy * vxy, 0.0));
    s.hx = std::max(2.0 * std::sqrt(0.5 * tr + disc), 1.5);
    s.hy = std::max(2.0 * std::sqrt(std::max(0.5 * tr - disc, 0.0)), 1.5);
    s.angle = 0.5 * std::atan2(2.0 * vxy, vxx - vyy);
    s.extent = (double)std::max(b.bw(), b.bh());
    return s;
}

struct Fitted {
    bool ok = false;
    ShapeParams sp;
    double amp[3] = {}, rim[3] = {}, grad_x[3] = {}, grad_y[3] = {};
    double contrast = 0, r2 = 0, visibility = 0;
};

Fitted fit_seed(const Image& img, const Seed& seed, const Params& fp) {
    Fitted f;
    // The blob plus as much background again, capped at 48: one long thin seed
    // with an uncapped window cost more than every other fit of the frame.
    const double half = std::clamp(1.3 * seed.extent, 12.0, 48.0);
    const int x0 = std::max(0, (int)std::floor(seed.x - half));
    const int y0 = std::max(0, (int)std::floor(seed.y - half));
    const int x1 = std::min((int)img.w, (int)std::ceil(seed.x + half));
    const int y1 = std::min((int)img.h, (int)std::ceil(seed.y + half));
    if (x1 - x0 < 8 || y1 - y0 < 8) return f;
    FitWindow win;
    const size_t n = (size_t)(x1 - x0) * (y1 - y0);
    win.x.reserve(n);
    win.y.reserve(n);
    win.rgb.reserve(n * 3);
    win.basis.reserve(n * 6);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float* px = &img.rgb[((size_t)y * img.w + x) * 3];
            if (!all_finite(px[0], px[1], px[2])) continue;
            const double cx = x + 0.5, cy = y + 0.5;
            win.x.push_back(cx);
            win.y.push_back(cy);
            win.rgb.insert(win.rgb.end(), {px[0], px[1], px[2]});
            const double u = (cx - seed.x) / half, v = (cy - seed.y) / half;
            win.basis.insert(win.basis.end(), {1.0, u, v, u * u, u * v, v * v});
        }
    if (win.size() < 64) return f;
    ShapeParams sp;
    sp.p[kPx] = seed.x;
    sp.p[kPy] = seed.y;
    sp.p[kPhx] = seed.hx;
    sp.p[kPhy] = seed.hy;
    sp.p[kPrho] = 0.7;
    sp.p[kPang] = seed.angle;
    sp.p[kPsoft] = 1.0;
    Bounds bd{};
    bd.lo[kPx] = seed.x - 0.5 * half;
    bd.hi[kPx] = seed.x + 0.5 * half;
    bd.lo[kPy] = seed.y - 0.5 * half;
    bd.hi[kPy] = seed.y + 0.5 * half;
    bd.lo[kPhx] = bd.lo[kPhy] = 1.0;
    bd.hi[kPhx] = bd.hi[kPhy] = half;
    bd.lo[kPrho] = 0.0;
    bd.hi[kPrho] = 1.0;
    bd.lo[kPang] = -0.5 * kPi;
    bd.hi[kPang] = 0.5 * kPi;
    bd.lo[kPsoft] = 0.3;
    bd.hi[kPsoft] = std::max(0.5, fp.max_soft);
    // Geometry with the flat plateau alone first: fitted jointly from the seed,
    // the rim let the rectangle stretch past the aspect gate on 2 frames of 3.
    Eval geom_fit;
    const ShapeParams flat = fit_shape(win, sp, bd, std::max(1, fp.max_iterations), Model::Plateau, geom_fit);
    // A flat fit on its bounds is not a ghost and refining cannot make it one.
    if (flat.p[kPsoft] >= 0.95 * bd.hi[kPsoft] || std::max(flat.p[kPhx], flat.p[kPhy]) >= 0.95 * half)
        return f;
    // Then every term, anchored within a quarter of the flat fit's size.
    Bounds anchored = bd;
    const double span = 0.25 * std::max(flat.p[kPhx], flat.p[kPhy]);
    anchored.lo[kPx] = std::max(bd.lo[kPx], flat.p[kPx] - span);
    anchored.hi[kPx] = std::min(bd.hi[kPx], flat.p[kPx] + span);
    anchored.lo[kPy] = std::max(bd.lo[kPy], flat.p[kPy] - span);
    anchored.hi[kPy] = std::min(bd.hi[kPy], flat.p[kPy] + span);
    anchored.lo[kPhx] = std::max(bd.lo[kPhx], 0.75 * flat.p[kPhx]);
    anchored.hi[kPhx] = std::min(bd.hi[kPhx], 1.25 * flat.p[kPhx]);
    anchored.lo[kPhy] = std::max(bd.lo[kPhy], 0.75 * flat.p[kPhy]);
    anchored.hi[kPhy] = std::min(bd.hi[kPhy], 1.25 * flat.p[kPhy]);
    Eval full_fit;
    const ShapeParams fit =
        std::isfinite(geom_fit.ssr)
            ? fit_shape(win, flat, anchored, std::max(1, fp.max_iterations / 2), Model::Full, full_fit)
            : flat;
    if (!std::isfinite(geom_fit.ssr)) return f;
    // ---- gate 1: a real, bright, well-bounded shape ----
    const double hx = fit.p[kPhx], hy = fit.p[kPhy];
    if (!(std::min(hx, hy) >= 1.5)) return f;
    if (std::max(hx, hy) / std::min(hx, hy) > fp.max_aspect) return f;
    if (fit.p[kPsoft] >= 0.95 * bd.hi[kPsoft]) return f;
    if (std::max(hx, hy) >= 0.95 * half) return f;
    Eval best;
    if (!evaluate(win, fit, Model::Full, best)) return f;
    const double u = (fit.p[kPx] - seed.x) / half, v = (fit.p[kPy] - seed.y) / half;
    const double basis[6] = {1.0, u, v, u * u, u * v, v * v};
    double bg[3] = {};
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 6; ++k) bg[c] += best.coef[c][k] * basis[k];
    const double bg_y = luma_of(bg);
    // Mean light the ghost adds over its plateau, clamped as the kernel does.
    double light_sum = 0.0;
    size_t light_n = 0;
    for (size_t i = 0; i < win.size(); ++i) {
        const double* t = &best.terms[i * kNumGhostTerms];
        if (t[0] < 0.5) continue;
        double light[3];
        for (int c = 0; c < 3; ++c) {
            double l = 0.0;
            for (int k = 0; k < kNumGhostTerms; ++k) l += best.coef[c][kNumBackground + k] * t[k];
            light[c] = std::max(l, 0.0);
        }
        light_sum += luma_of(light);
        ++light_n;
    }
    const double amp_y = light_n > 0 ? light_sum / (double)light_n : 0.0;
    if (!(amp_y > 0.0) || !(bg_y > 0.0)) return f;
    const double contrast = amp_y / bg_y;
    if (contrast < fp.min_contrast) return f;
    // ---- gate 2: the ghost terms explain their own footprint ----
    Eval bg_only;
    if (!evaluate(win, fit, Model::Background, bg_only)) return f;
    const double margin = 3.0 * fit.p[kPsoft] + 2.0;  // the rim reaches 3 soft past the edge
    double ssr_full = 0.0, ssr_bg = 0.0;
    const ShapeGeom fit_geom(fit);
    for (size_t i = 0; i < win.size(); ++i) {
        if (shape_distance(fit_geom, win.x[i], win.y[i]) > margin) continue;
        for (int c = 0; c < 3; ++c) {
            const size_t k = i * 3 + c;
            ssr_full += best.resid[k] * best.resid[k];
            ssr_bg += bg_only.resid[k] * bg_only.resid[k];
        }
    }
    const double r2 = ssr_bg > 0.0 ? 1.0 - ssr_full / ssr_bg : 0.0;
    if (r2 < fp.min_fit_r2) return f;
    // A slightly negative plateau channel adds nothing; rim and tilt keep their
    // sign, and the kernel clamps the per-pixel total at 0.
    f.ok = true;
    f.sp = fit;
    for (int c = 0; c < 3; ++c) {
        f.amp[c] = std::max(best.coef[c][kNumBackground], 0.0);
        f.rim[c] = best.coef[c][kNumBackground + 1];
        f.grad_x[c] = best.coef[c][kNumBackground + 2];
        f.grad_y[c] = best.coef[c][kNumBackground + 3];
    }
    f.contrast = contrast;
    f.r2 = r2;
    f.visibility = contrast * std::sqrt(hx * hy);
    return f;
}

// Two fits of one reflection: either centre lies inside the other's shape.
bool overlapping(const Fitted& a, const Fitted& b) {
    return shape_distance(b.sp, a.sp.p[kPx], a.sp.p[kPy]) < 0.0 ||
           shape_distance(a.sp, b.sp.p[kPx], b.sp.p[kPy]) < 0.0;
}

float smoothstep01(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

float ghost_distance(const KernelGhost& g, float px, float py, float* u, float* v) {
    const float dx = px - g.cx, dy = py - g.cy;
    const float sx = dx * g.cos_a + dy * g.sin_a;
    const float sy = -dx * g.sin_a + dy * g.cos_a;
    *u = sx / std::fmax(g.hx, 1e-3f);
    *v = sy / std::fmax(g.hy, 1e-3f);
    const float qx = std::fabs(sx) - (g.hx - g.radius);
    const float qy = std::fabs(sy) - (g.hy - g.radius);
    const float ox = std::fmax(qx, 0.0f), oy = std::fmax(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::fmin(std::fmax(qx, qy), 0.0f) - g.radius;
}

float kernel_plateau(float d, float soft) {
    const float s = std::fmax(soft, 1e-3f);
    return smoothstep01((s - d) / (2.0f * s));
}

float kernel_rim(float d, float soft) {
    const float t = d / (3.0f * std::fmax(soft, 1e-3f));
    if (!(t * t < 1.0f)) return 0.0f;
    const float b = 1.0f - t * t;
    return b * b;
}

// BT.709 camera curve, as OpenOSV's osvRec709Oetf / osvRec709InverseOetf.
float rec709_oetf(float e) {
    e = std::clamp(e, 0.0f, 1.0f);
    return e < 0.018f ? 4.5f * e : 1.099f * std::pow(e, 0.45f) - 0.099f;
}
float rec709_inverse_oetf(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v < 0.081f ? v / 4.5f : std::pow((v + 0.099f) / 1.099f, 1.0f / 0.45f);
}

// Linear light of every code value: 256 or 65536 entries.
std::vector<float> decode_table(int bits, Encoding e) {
    const int n = 1 << bits;
    std::vector<float> t((size_t)n);
    const float scale = 1.0f / (float)(n - 1);
    for (int i = 0; i < n; ++i) t[(size_t)i] = code_to_linear((float)i * scale, e);
    return t;
}

}  // namespace

// ============================================================================
// Public
// ============================================================================

bool Lens::valid() const {
    return all_finite(cx, cy, r_max) && r_max > 0.0;
}

bool Params::valid() const {
    return all_finite(sun_level_fraction, sun_min_ratio_to_median, corridor_deg, background_sigma,
                      seed_contrast, max_texture, min_contrast, min_fit_r2) &&
           sun_level_fraction > 0.0 && sun_level_fraction <= 1.0 && background_sigma > 0.5 &&
           max_ghosts >= 0 && corridor_deg >= 0.0 && factor >= 1 && factor <= 16 &&
           all_finite(clip_code, clip_max_radius, clip_max_aspect, clip_min_fill) && clip_code > 0.0 &&
           clip_code <= 1.0;
}

double Ghost::reach() const {
    // The rectangle's farthest point plus the rim bump's outer 3 soft.
    return std::sqrt(hx * hx + hy * hy) + 3.0 * std::max(soft, 0.0);
}

bool analyse(const Image& image, const Lens& lens, const Params& fp, LensFlare& out,
             std::string& error) {
    out = LensFlare{};
    if (!image.valid()) {
        error = "flare: invalid working image";
        return false;
    }
    if (!lens.valid()) {
        error = "flare: invalid lens";
        return false;
    }
    if (!fp.valid()) {
        error = "flare: invalid parameters";
        return false;
    }
    const double f = image.factor;
    const uint32_t W = image.w, H = image.h;

    Gray luma(W, H);
    for (size_t i = 0; i < luma.v.size(); ++i) {
        const float* p = &image.rgb[i * 3];
        const double y = kLumaR * p[0] + kLumaG * p[1] + kLumaB * p[2];
        luma.v[i] = std::isfinite(y) ? (float)y : 0.0f;
    }
    const double ocx = lens.cx / f, ocy = lens.cy / f, r_max = lens.r_max / f;
    std::vector<uint8_t> inside(luma.v.size(), 0);
    const double r2_max = (0.97 * r_max) * (0.97 * r_max);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const double dx = x + 0.5 - ocx, dy = y + 0.5 - ocy;
            inside[(size_t)y * W + x] = dx * dx + dy * dy < r2_max ? 1 : 0;
        }
    SunBlob sun = detect_sun(luma, inside, fp);
    if (!sun.found && image.coded) sun = detect_clipped_sun(image, inside, r_max, fp);
    if (!sun.found) return true;
    out.sun_found = true;
    out.sun_x = sun.x * f;
    out.sun_y = sun.y * f;
    out.sun_radius = sun.radius * f;
    if (fp.max_ghosts == 0) return true;

    // ---- relative band-pass and local texture ----
    const Gray bg = gauss_boxes(luma, fp.background_sigma);
    const Gray fine = gauss_exact(luma, 1.0);
    Gray rel(W, H);
    for (size_t i = 0; i < rel.v.size(); ++i)
        rel.v[i] = (float)((fine.v[i] - bg.v[i]) / std::max((double)bg.v[i], 1e-6));
    // Mean absolute detail below sigma 2 relative to the background, averaged
    // over sigma 4: sky ~0.005, a city ~0.1.
    Gray detail(W, H);
    {
        const Gray s2 = gauss_boxes(luma, 2.0);
        for (size_t i = 0; i < detail.v.size(); ++i)
            detail.v[i] = (float)(std::fabs(luma.v[i] - s2.v[i]) / std::max((double)bg.v[i], 1e-6));
    }
    const Gray texture = gauss_boxes(detail, 4.0);

    // ---- candidates: bright bumps near the sun line, away from the sun ----
    const double sun_az = std::atan2(sun.y - ocy, sun.x - ocx);
    const double sun_r = std::hypot(sun.x - ocx, sun.y - ocy);
    const bool any_azimuth = sun_r < 2.0 * sun.radius;  // too near the axis for an azimuth
    const double corridor = fp.corridor_deg * kPi / 180.0;
    const double exclusion = fp.sun_exclusion_radii * sun.radius;
    std::vector<uint8_t> mask(luma.v.size(), 0);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            const size_t i = (size_t)y * W + x;
            if (rel.v[i] <= fp.seed_contrast) continue;
            const double px = x + 0.5, py = y + 0.5;
            if (std::hypot(px - ocx, py - ocy) >= fp.rim_fraction * r_max) continue;
            if (std::hypot(px - sun.x, py - sun.y) <= exclusion) continue;
            if (!any_azimuth) {
                // Folded, so both the sun's side and the mirrored side count.
                const double d = std::remainder(std::atan2(py - ocy, px - ocx) - sun_az, 2.0 * kPi);
                if (std::min(std::fabs(d), kPi - std::fabs(d)) > corridor) continue;
            }
            mask[i] = 1;
        }
    const std::vector<Blob> blobs = find_blobs(mask, W, H, &rel);

    std::vector<Seed> seeds;
    for (const Blob& b : blobs) {
        if (b.area < 6 || (double)b.area > fp.max_candidate_area) continue;
        if (b.aspect() > fp.max_aspect || b.fill() < 0.35) continue;
        if ((double)std::max(b.bw(), b.bh()) * f > fp.max_candidate_extent) continue;
        const uint32_t gx0 = b.x0 >= b.bw() ? b.x0 - b.bw() : 0u;
        const uint32_t gy0 = b.y0 >= b.bh() ? b.y0 - b.bh() : 0u;
        const uint32_t gx1 = std::min(W - 1, b.x1 + b.bw());
        const uint32_t gy1 = std::min(H - 1, b.y1 + b.bh());
        std::vector<float> tv;
        tv.reserve((size_t)(gx1 - gx0 + 1) * (gy1 - gy0 + 1));
        for (uint32_t y = gy0; y <= gy1; ++y)
            for (uint32_t x = gx0; x <= gx1; ++x) tv.push_back(texture.at(x, y));
        auto mid = tv.begin() + (ptrdiff_t)(tv.size() / 2);
        std::nth_element(tv.begin(), mid, tv.end());
        const double tex = *mid;
        // A bump on texture is far more likely scene, and scene is never subtracted.
        if (tex > fp.max_texture || b.peak < fp.min_peak_over_texture * tex) continue;
        seeds.push_back(seed_from_blob(b));
    }
    out.candidates = (uint32_t)seeds.size();

    std::vector<Fitted> accepted;
    for (const Seed& s : seeds) {
        Fitted ft = fit_seed(image, s, fp);
        if (ft.ok) accepted.push_back(ft);
    }
    std::sort(accepted.begin(), accepted.end(),
              [](const Fitted& a, const Fitted& b) { return a.visibility > b.visibility; });
    std::vector<Fitted> kept;
    for (const Fitted& ft : accepted) {
        if (std::any_of(kept.begin(), kept.end(), [&](const Fitted& k) { return overlapping(ft, k); }))
            continue;
        if ((int)kept.size() >= std::min(fp.max_ghosts, kMaxGhosts)) break;
        kept.push_back(ft);
    }
    out.rejected = out.candidates - (uint32_t)kept.size();
    for (const Fitted& ft : kept) {
        Ghost g;
        g.cx = ft.sp.p[kPx] * f;
        g.cy = ft.sp.p[kPy] * f;
        g.hx = ft.sp.p[kPhx] * f;
        g.hy = ft.sp.p[kPhy] * f;
        g.radius = std::clamp(ft.sp.p[kPrho], 0.0, 1.0) * std::min(g.hx, g.hy);
        g.angle = ft.sp.p[kPang];
        g.soft = ft.sp.p[kPsoft] * f;
        for (int c = 0; c < 3; ++c) {
            g.amp[c] = ft.amp[c];
            g.rim[c] = ft.rim[c];
            g.grad_x[c] = ft.grad_x[c];
            g.grad_y[c] = ft.grad_y[c];
        }
        g.contrast = ft.contrast;
        g.fit_r2 = ft.r2;
        out.ghosts.push_back(g);
    }
    return true;
}

float soft_subtract(float x, float g) {
    if (!(g > 1e-7f) || !(x > 0.0f)) return x;
    const float x3 = x * x * x, g3 = g * g * g;
    return x - g * (x3 / (x3 + g3));
}

bool to_kernel(const Ghost& g, KernelGhost& k) {
    if (!all_finite(g.cx, g.cy, g.hx, g.hy, g.radius, g.angle, g.soft, g.amp[0], g.amp[1], g.amp[2],
                    g.rim[0], g.rim[1], g.rim[2], g.grad_x[0], g.grad_x[1], g.grad_x[2], g.grad_y[0],
                    g.grad_y[1], g.grad_y[2]))
        return false;
    if (!(g.hx > 0.0) || !(g.hy > 0.0) || !(g.soft > 0.0)) return false;
    k.cx = (float)g.cx;
    k.cy = (float)g.cy;
    k.hx = (float)g.hx;
    k.hy = (float)g.hy;
    k.radius = (float)std::clamp(g.radius, 0.0, std::min(g.hx, g.hy));
    k.cos_a = (float)std::cos(g.angle);
    k.sin_a = (float)std::sin(g.angle);
    k.soft = (float)g.soft;
    for (int c = 0; c < 3; ++c) {
        k.amp[c] = (float)std::max(g.amp[c], 0.0);
        k.rim[c] = (float)g.rim[c];
        k.grad_x[c] = (float)g.grad_x[c];
        k.grad_y[c] = (float)g.grad_y[c];
    }
    // A pixel of slack, so float rounding never cuts the ramp's last sliver.
    const double reach = g.reach() + 1.0;
    k.reach2 = (float)(reach * reach);
    return true;
}

float ghost_shape(const KernelGhost& g, float px, float py) {
    const float dx = px - g.cx, dy = py - g.cy;
    if (dx * dx + dy * dy > g.reach2) return 0.0f;
    float u, v;
    return kernel_plateau(ghost_distance(g, px, py, &u, &v), g.soft);
}

bool ghost_light(const KernelGhost& g, float px, float py, float rgb[3]) {
    const float dx = px - g.cx, dy = py - g.cy;
    if (dx * dx + dy * dy > g.reach2) return false;
    float u, v;
    const float d = ghost_distance(g, px, py, &u, &v);
    const float s = kernel_plateau(d, g.soft);
    const float e = kernel_rim(d, g.soft);
    for (int c = 0; c < 3; ++c)
        rgb[c] = std::fmax(g.amp[c] * s + g.rim[c] * e + (g.grad_x[c] * u + g.grad_y[c] * v) * s, 0.0f);
    return true;
}

void remove_at(const std::vector<KernelGhost>& ghosts, float px, float py, float rgb[3]) {
    float add[3] = {0.0f, 0.0f, 0.0f};
    const size_t n = std::min(ghosts.size(), (size_t)kMaxGhosts);
    for (size_t k = 0; k < n; ++k) {
        float light[3];
        if (ghost_light(ghosts[k], px, py, light))
            for (int c = 0; c < 3; ++c) add[c] += light[c];
    }
    for (int c = 0; c < 3; ++c) rgb[c] = soft_subtract(rgb[c], add[c]);
}

bool encoding_for(sfm::VideoColorMode mode, Encoding& e) {
    e = mode == sfm::VideoColorMode::DlogM ? Encoding::DlogM : Encoding::Rec709;
    return mode != sfm::VideoColorMode::OtherLog;
}

float code_to_linear(float code, Encoding e) {
    return e == Encoding::DlogM ? colorspace::dlogm_osmo360_to_linear(code) : rec709_inverse_oetf(code);
}

float linear_to_code(float lin, Encoding e) {
    if (e == Encoding::Rec709) return rec709_oetf(lin);
    // Light below the curve's code-0 level stays at code 0 (osvShadeCodeAdd).
    const float floor_lin = colorspace::dlogm_osmo360_to_linear(0.0f);
    return colorspace::dlogm_osmo360_to_code(std::max(lin, floor_lin));
}

bool downsample(const Frame& fr, Encoding e, uint32_t factor, Image& out, std::string& error) {
    const bool ok16 = fr.bits == 16 && fr.data16;
    const bool ok8 = fr.bits == 8 && fr.data8;
    if (factor < 1 || factor > 16 || fr.w <= 0 || fr.h <= 0 || (fr.channels != 3 && fr.channels != 4) ||
        !(ok16 || ok8)) {
        error = "flare: malformed frame";
        return false;
    }
    const std::vector<float> lut = decode_table(fr.bits, e);
    out.factor = factor;
    out.coded = true;
    out.encoding = e;
    out.w = (fr.w + factor - 1) / factor;
    out.h = (fr.h + factor - 1) / factor;
    out.rgb.assign((size_t)out.w * out.h * 3, 0.0f);
    // Two taps per axis at a = factor / 4 and b = factor - 1 - a, centred on the
    // block; one at factor 1. A partial edge block clamps into the frame.
    const int fct = (int)factor;
    const int off_a = fct / 4, off_b = fct - 1 - off_a;
    const int taps = off_b > off_a ? 2 : 1;
    for (uint32_t oy = 0; oy < out.h; ++oy)
        for (uint32_t ox = 0; ox < out.w; ++ox) {
            float acc[3] = {0, 0, 0};
            int count = 0;
            for (int j = 0; j < taps; ++j) {
                const int y = std::min((int)oy * fct + (j == 0 ? off_a : off_b), fr.h - 1);
                for (int i = 0; i < taps; ++i) {
                    const int x = std::min((int)ox * fct + (i == 0 ? off_a : off_b), fr.w - 1);
                    const size_t at = ((size_t)y * fr.w + x) * fr.channels;
                    for (int c = 0; c < 3; ++c)
                        acc[c] += lut[ok16 ? fr.data16[at + c] : fr.data8[at + c]];
                    ++count;
                }
            }
            float* dst = &out.rgb[((size_t)oy * out.w + ox) * 3];
            for (int c = 0; c < 3; ++c) dst[c] = acc[c] / (float)count;
        }
    return true;
}

int64_t subtract(Frame& fr, Encoding e, const LensFlare& flare) {
    std::vector<KernelGhost> ks;
    for (const Ghost& g : flare.ghosts) {
        if ((int)ks.size() >= kMaxGhosts) break;
        KernelGhost k;
        if (to_kernel(g, k)) ks.push_back(k);
    }
    if (ks.empty() || fr.w <= 0 || fr.h <= 0) return 0;
    const bool is16 = fr.bits == 16 && fr.data16;
    if (!is16 && !(fr.bits == 8 && fr.data8)) return 0;
    const std::vector<float> lut = decode_table(is16 ? 16 : 8, e);
    const float max_code = is16 ? 65535.0f : 255.0f;
    // Bounding box of every reach; outside it no ghost can add light.
    int x0 = fr.w, y0 = fr.h, x1 = -1, y1 = -1;
    for (const KernelGhost& k : ks) {
        const float r = std::sqrt(k.reach2);
        x0 = std::min(x0, std::max(0, (int)std::floor(k.cx - r)));
        y0 = std::min(y0, std::max(0, (int)std::floor(k.cy - r)));
        x1 = std::max(x1, std::min(fr.w - 1, (int)std::ceil(k.cx + r)));
        y1 = std::max(y1, std::min(fr.h - 1, (int)std::ceil(k.cy + r)));
    }
    int64_t changed = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const size_t at = ((size_t)y * fr.w + x) * fr.channels;
            float rgb[3], before[3];
            for (int c = 0; c < 3; ++c)
                rgb[c] = before[c] = lut[is16 ? fr.data16[at + c] : fr.data8[at + c]];
            // Lens pixel (x, y) is centred at +0.5, the working image's convention.
            remove_at(ks, x + 0.5f, y + 0.5f, rgb);
            if (std::memcmp(rgb, before, sizeof rgb) == 0) continue;
            bool any = false;
            for (int c = 0; c < 3; ++c) {
                if (rgb[c] == before[c]) continue;
                const float code = std::nearbyint(
                    std::clamp(linear_to_code(rgb[c], e), 0.0f, 1.0f) * max_code);
                if (is16) {
                    any |= fr.data16[at + c] != (uint16_t)code;
                    fr.data16[at + c] = (uint16_t)code;
                } else {
                    any |= fr.data8[at + c] != (uint8_t)code;
                    fr.data8[at + c] = (uint8_t)code;
                }
            }
            changed += any;
        }
    return changed;
}

Lens centred_lens(int w, int h) {
    Lens l;
    l.cx = 0.5 * w;
    l.cy = 0.5 * h;
    l.r_max = 0.5 * std::min(w, h);
    return l;
}

bool process_file(const std::string& path, Encoding e, const Params& params, FileReport& report,
                  std::string& error) {
    namespace fs = std::filesystem;
    const auto t0 = std::chrono::steady_clock::now();
    report = FileReport{};
    const std::string ext = [&] {
        std::string s = fs::path(path).extension().string();
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }();
    const bool png = ext == ".png";
    if (!png && ext != ".jpg" && ext != ".jpeg") {
        error = "flare: not a PNG or JPEG: " + path;
        return false;
    }
    Frame fr;
    int n = 0;
    const bool is16 = png && stbi_is_16_bit(path.c_str());
    stbi_us* p16 = nullptr;
    stbi_uc* p8 = nullptr;
    if (is16)
        p16 = stbi_load_16(path.c_str(), &fr.w, &fr.h, &n, 0);
    else
        p8 = stbi_load(path.c_str(), &fr.w, &fr.h, &n, 0);
    struct Free {
        void* p;
        ~Free() { stbi_image_free(p); }
    } hold{is16 ? (void*)p16 : (void*)p8};
    if (!(p16 || p8) || (n != 3 && n != 4)) {
        error = "flare: cannot read an RGB image from " + path;
        return false;
    }
    fr.channels = n;
    fr.bits = is16 ? 16 : 8;
    fr.data16 = p16;
    fr.data8 = p8;
    Image img;
    if (!downsample(fr, e, params.factor, img, error)) return false;
    LensFlare lf;
    if (!analyse(img, centred_lens(fr.w, fr.h), params, lf, error)) return false;
    report.sun_found = lf.sun_found;
    report.sun_x = lf.sun_x;
    report.sun_y = lf.sun_y;
    report.sun_radius = lf.sun_radius;
    report.ghosts = (int)lf.ghosts.size();
    report.candidates = lf.candidates;
    if (!lf.ghosts.empty()) report.changed = subtract(fr, e, lf);
    if (report.changed > 0) {
        const std::string tmp = path + ".flare-tmp";
        bool ok;
        if (is16)
            ok = save_png16(tmp, p16, fr.w, fr.h, n);
        else if (png)
            ok = stbi_write_png(tmp.c_str(), fr.w, fr.h, n, p8, fr.w * n) != 0;
        else
            ok = stbi_write_jpg(tmp.c_str(), fr.w, fr.h, n, p8, 95) != 0;
        std::error_code ec;
        if (ok) fs::rename(tmp, path, ec);
        if (!ok || ec) {
            fs::remove(tmp, ec);
            error = "flare: cannot rewrite " + path;
            return false;
        }
        report.written = true;
    }
    report.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

namespace {

// "<name> <size> <mtime>" of a file as the pass left it.
std::string done_key(const std::filesystem::path& f) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(f, ec);
    const auto t = std::filesystem::last_write_time(f, ec).time_since_epoch().count();
    return f.filename().string() + " " + std::to_string(size) + " " + std::to_string((long long)t);
}

}  // namespace

bool process_tree(const std::string& dir, Encoding e, const Params& params, int threads,
                  const std::function<void(const std::string&, const FileReport&)>& on_file,
                  const std::atomic<bool>* cancel, TreeReport& report, std::string& error) {
    namespace fs = std::filesystem;
    report = TreeReport{};
    error.clear();
    std::map<fs::path, std::set<std::string>> done;
    std::vector<fs::path> todo;
    std::error_code ec;
    const auto opts = fs::directory_options::skip_permission_denied |
                      fs::directory_options::follow_directory_symlink;
    for (fs::recursive_directory_iterator it(dir, opts, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") continue;
        const fs::path folder = it->path().parent_path();
        if (!done.count(folder)) {
            std::ifstream rec(folder / ".spirula-flare");
            std::set<std::string>& d = done[folder];
            for (std::string line; std::getline(rec, line);) d.insert(line);
        }
        if (!done[folder].count(done_key(it->path()))) todo.push_back(it->path());
    }
    std::sort(todo.begin(), todo.end());
    std::mutex mu;
    std::atomic<size_t> next{0};
    auto work = [&] {
        for (size_t i; !(cancel && cancel->load()) && (i = next++) < todo.size();) {
            FileReport r;
            std::string err;
            const bool ok = process_file(todo[i].string(), e, params, r, err);
            std::lock_guard<std::mutex> lock(mu);
            if (!ok) {
                if (error.empty()) error = err;
                next = todo.size();
                continue;
            }
            ++report.files;
            report.changed += r.written;
            if (on_file) on_file(todo[i].string(), r);
            std::ofstream(todo[i].parent_path() / ".spirula-flare", std::ios::app)
                << done_key(todo[i]) << "\n";
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < std::max(threads, 1); t++) pool.emplace_back(work);
    for (std::thread& t : pool) t.join();
    if (cancel && cancel->load() && error.empty()) error = "cancelled";
    return error.empty();
}

}  // namespace app::flare
