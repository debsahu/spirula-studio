// flare_removal -- the sun ghost removal of app/FlareRemoval.h: the per-pixel
// model, detection and fitting on synthetic lens images with known ghosts, the
// refusals, and a frame and a file changed only inside a fitted ghost.
// Cases follow OpenOSV's test_flare.cpp:
// https://github.com/Kemerd/OpenOSV/blob/e169fe1dd7b1e2da7cd0a52734393dc155670722/tests/unit/test_flare.cpp
// SPDX-FileCopyrightText: Copyright 2026 The OpenOSV Contributors
// SPDX-License-Identifier: Apache-2.0

#include "app/DepthPng.h"
#include "app/FlareRemoval.h"
#include "external/stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace app::flare;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%-78s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) g_failures++;
}

bool close_to(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

constexpr double kPi = 3.14159265358979323846;

// A 600 x 600 lens at factor 1, optical centre in the middle.
Lens synthetic_lens() { return Lens{300.0, 300.0, 250.0}; }

Ghost planted_ghost() {
    Ghost g;
    g.cx = 150.0;
    g.cy = 312.0;
    g.hx = 14.0;
    g.hy = 8.0;
    g.radius = 5.0;
    g.angle = 0.2;
    g.soft = 2.0;
    g.amp[0] = 0.060;
    g.amp[1] = 0.050;
    g.amp[2] = 0.040;
    return g;
}

KernelGhost kernel(const Ghost& g) {
    KernelGhost k{};
    to_kernel(g, k);
    return k;
}

struct Scene {
    bool sun = true, ghost = true;
    double texture = 0.0;   // relative amplitude of blocky scene texture
    double noise = 0.0008;  // absolute Gaussian noise
    Ghost g = planted_ghost();
};

// Sky with a gentle gradient, the sun at (240, 300), the ghost exactly as the
// per-pixel model draws it.
Image make_scene(const Scene& o) {
    Image img;
    img.w = img.h = 600;
    img.factor = 1;
    img.rgb.assign(600u * 600u * 3u, 0.0f);
    std::mt19937 rng(1234);
    std::normal_distribution<double> gauss(0.0, 1.0);
    const KernelGhost kg = kernel(o.g);
    for (uint32_t y = 0; y < img.h; ++y)
        for (uint32_t x = 0; x < img.w; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double base = 0.10 + 0.04 * px / 600.0 + 0.02 * py / 600.0;
            double rgb[3] = {0.6 * base, 0.9 * base, 1.4 * base};
            if (o.texture > 0.0) {
                std::mt19937 cell((unsigned)((x / 3) * 7919u + (y / 3) * 104729u));
                std::uniform_real_distribution<double> u(-1.0, 1.0);
                const double t = 1.0 + o.texture * u(cell);
                for (double& c : rgb) c *= t;
            }
            if (o.ghost) {
                float light[3] = {0, 0, 0};
                if (ghost_light(kg, (float)px, (float)py, light))
                    for (int c = 0; c < 3; ++c) rgb[c] += light[c];
            }
            if (o.sun && std::hypot(px - 240.0, py - 300.0) < 8.0) rgb[0] = rgb[1] = rgb[2] = 3.76;
            float* dst = &img.rgb[((size_t)y * img.w + x) * 3];
            for (int c = 0; c < 3; ++c) dst[c] = (float)(rgb[c] + o.noise * gauss(rng));
        }
    return img;
}

LensFlare analyse_ok(const Image& img, const Params& p = {}) {
    LensFlare lf;
    std::string err;
    if (!analyse(img, synthetic_lens(), p, lf, err)) std::printf("  analyse: %s\n", err.c_str());
    return lf;
}

// ---- the per-pixel model ------------------------------------------------------

// Kills: a plateau ramp that is not centred on the edge; a reach without the
// rim's 3 soft; a rotation applied with the wrong sign.
void test_shape() {
    Ghost g = planted_ghost();
    g.angle = 0.0;
    const KernelGhost k = kernel(g);
    check(close_to(ghost_shape(k, 150.0f, 312.0f), 1.0, 1e-6), "shape: plateau 1 at the centre");
    check(close_to(ghost_shape(k, 164.0f, 312.0f), 0.5, 1e-5), "shape: 0.5 on the long edge");
    check(close_to(ghost_shape(k, 150.0f, 320.0f), 0.5, 1e-5), "shape: 0.5 on the short edge");
    check(ghost_shape(k, 164.0f + 2.01f, 312.0f) == 0.0f, "shape: 0 past the ramp");
    bool zero_out = true, light_out = true;
    for (int a = 0; a < 16; ++a) {
        const double ang = a * 2.0 * kPi / 16.0;
        const float x = (float)(150.0 + (g.reach() + 1.5) * std::cos(ang));
        const float y = (float)(312.0 + (g.reach() + 1.5) * std::sin(ang));
        zero_out &= ghost_shape(k, x, y) == 0.0f;
        float rgb[3] = {7, 7, 7};
        light_out &= !ghost_light(k, x, y, rgb) && rgb[0] == 7.0f;
    }
    check(zero_out && light_out, "shape: exactly 0, light untouched, past the reach at 16 angles");
    // A rim bump reaches 3 soft past the edge; the reach must hold it.
    Ghost r = g;
    r.rim[0] = r.rim[1] = r.rim[2] = 0.05;
    float rim_light[3] = {0, 0, 0};
    ghost_light(kernel(r), 150.0f + 14.0f + 5.8f, 312.0f, rim_light);
    check(rim_light[0] > 0.0f, "shape: the rim bump still lights 2.9 soft past the edge");
    Ghost g2 = g;
    g2.angle = 0.5 * kPi;
    std::swap(g2.hx, g2.hy);
    const KernelGhost k2 = kernel(g2);
    bool same = true;
    for (int i = 0; i < 50; ++i) {
        const float x = 130.0f + (float)i, y = 305.0f + 0.3f * (float)i;
        same &= close_to(ghost_shape(k2, x, y), ghost_shape(k, x, y), 1e-5);
    }
    check(same, "shape: turned 90 deg with swapped extents is the same shape");
    // A turned ghost is the unturned one seen through the inverse rotation.
    Ghost t = g;
    t.angle = 0.3;
    const KernelGhost kt = kernel(t);
    bool rotates = true;
    for (int i = 0; i < 60; ++i) {
        const double px = 132.0 + 0.6 * i, py = 300.0 + 0.4 * i;
        const double dx = px - 150.0, dy = py - 312.0;
        const double ux = 150.0 + dx * std::cos(0.3) + dy * std::sin(0.3);
        const double uy = 312.0 - dx * std::sin(0.3) + dy * std::cos(0.3);
        rotates &= close_to(ghost_shape(kt, (float)px, (float)py), ghost_shape(k, (float)ux, (float)uy), 1e-4);
    }
    check(rotates, "shape: +angle turns the local x axis toward +y");
}

// Kills: a plain x - g (goes negative), and a knee that is not monotonic.
void test_soft_subtract() {
    check(soft_subtract(0.3f, 0.0f) == 0.3f, "soft subtract: identity for a zero estimate");
    check(soft_subtract(-0.1f, 0.2f) == -0.1f && soft_subtract(0.0f, 0.2f) == 0.0f,
          "soft subtract: non-positive input passes through");
    check(close_to(soft_subtract(10.0f, 0.1f), 9.9, 1e-4), "soft subtract: full subtraction well above");
    bool bounds = true, rising = true;
    for (float g : {0.001f, 0.01f, 0.05f, 0.2f, 1.0f, 4.0f}) {
        float prev = -1.0f;
        for (int i = 1; i <= 4000; ++i) {
            const float x = 1e-4f * (float)i * (float)i;
            const float f = soft_subtract(x, g);
            bounds &= f >= 0.47f * x && f <= x;
            rising &= f > prev;
            prev = f;
        }
    }
    check(bounds, "soft subtract: 0.47 x <= f <= x over a dense grid");
    check(rising, "soft subtract: strictly increasing");
}

// Kills: a removal that ignores the reach (a global offset) or skips the plateau.
void test_remove_outside_untouched() {
    const std::vector<KernelGhost> ks{kernel(planted_ghost())};
    const double reach = planted_ghost().reach();
    int inside = 0;
    bool outside_same = true, plateau_ok = true;
    for (int y = 250; y < 380; y += 3)
        for (int x = 80; x < 230; x += 3) {
            float rgb[3] = {0.12f, 0.2f, 0.31f};
            const float before[3] = {rgb[0], rgb[1], rgb[2]};
            remove_at(ks, (float)x, (float)y, rgb);
            if (std::hypot(x - 150.0, y - 312.0) > reach + 1.0) {
                outside_same &= std::memcmp(rgb, before, sizeof rgb) == 0;
            } else if (ghost_shape(ks[0], (float)x, (float)y) > 0.99f) {
                ++inside;
                for (int c = 0; c < 3; ++c) plateau_ok &= rgb[c] > 0.0f && rgb[c] < before[c];
                plateau_ok &= close_to(rgb[0], 0.12 - 0.06, 0.02);
            }
        }
    check(outside_same, "remove: every pixel past the reach is bit-identical");
    check(inside > 5 && plateau_ok, "remove: the plateau loses the ghost's amplitude, never goes <= 0");
}

// Kills: to_kernel passing a NaN through, or a negative plateau channel.
void test_to_kernel() {
    KernelGhost k{};
    Ghost g = planted_ghost();
    g.hx = std::nan("");
    check(!to_kernel(g, k), "to_kernel: a non-finite ghost is refused");
    g = planted_ghost();
    g.soft = 0.0;
    check(!to_kernel(g, k), "to_kernel: zero softness is refused");
    g = planted_ghost();
    g.amp[1] = -0.5;
    check(to_kernel(g, k) && k.amp[1] == 0.0f, "to_kernel: a negative plateau channel is clamped to 0");
}

// ---- detection and fitting ----------------------------------------------------

// Kills: a sign-flipped LM step, a seed sized from one sigma instead of two,
// and a centroid not weighted into the sub-pixel.
void test_recovers_planted_ghost() {
    const LensFlare lf = analyse_ok(make_scene(Scene{}));
    check(lf.sun_found && close_to(lf.sun_x, 240.0, 0.5) && close_to(lf.sun_y, 300.0, 0.5) &&
              close_to(lf.sun_radius, 8.0, 1.0),
          "analyse: the sun at (240, 300), radius 8");
    check(lf.ghosts.size() == 1, "analyse: exactly one ghost");
    if (lf.ghosts.size() != 1) return;
    const Ghost& g = lf.ghosts[0];
    const Ghost t = planted_ghost();
    std::printf("  fit c(%.3f, %.3f) h(%.3f, %.3f) a %.4f soft %.3f amp %.4f %.4f %.4f R2 %.3f\n",
                g.cx, g.cy, g.hx, g.hy, g.angle, g.soft, g.amp[0], g.amp[1], g.amp[2], g.fit_r2);
    check(close_to(g.cx, t.cx, 0.3) && close_to(g.cy, t.cy, 0.3), "analyse: centre within 0.3 px");
    check(close_to(g.hx, t.hx, 0.5) && close_to(g.hy, t.hy, 0.5), "analyse: extents within 0.5 px");
    check(close_to(g.angle, t.angle, 0.03), "analyse: angle within 0.03 rad");
    check(close_to(g.soft, t.soft, 0.5), "analyse: softness within 0.5 px");
    bool amp = true, none = true;
    for (int c = 0; c < 3; ++c) {
        amp &= std::fabs(g.amp[c] - t.amp[c]) <= 0.08 * t.amp[c];
        none &= close_to(g.rim[c], 0.0, 0.004) && close_to(g.grad_x[c], 0.0, 0.003) &&
                close_to(g.grad_y[c], 0.0, 0.003);
    }
    check(amp, "analyse: amplitude within 8%");
    check(none, "analyse: no rim or tilt fitted where none was planted");
    check(g.fit_r2 > 0.9, "analyse: the ghost explains > 90% of its footprint");
}

// Kills: a second stage that refines with the plateau alone.
void test_recovers_rim_and_tilt() {
    Scene o;
    o.g.hx = 18.0;
    o.g.hy = 11.0;
    const double rim[3] = {0.030, 0.025, 0.020}, gx[3] = {0.015, 0.012, 0.010},
                 gy[3] = {-0.010, -0.008, -0.006};
    for (int c = 0; c < 3; ++c) {
        o.g.rim[c] = rim[c];
        o.g.grad_x[c] = gx[c];
        o.g.grad_y[c] = gy[c];
    }
    const LensFlare lf = analyse_ok(make_scene(o));
    check(lf.ghosts.size() == 1, "rim and tilt: one ghost");
    if (lf.ghosts.size() != 1) return;
    const Ghost& g = lf.ghosts[0];
    std::printf("  rim %.4f gradX %.4f gradY %.4f amp %.4f\n", g.rim[0], g.grad_x[0], g.grad_y[0], g.amp[0]);
    bool ok = close_to(g.cx, o.g.cx, 0.3) && close_to(g.hx, o.g.hx, 0.5);
    for (int c = 0; c < 3; ++c)
        ok &= close_to(g.amp[c], o.g.amp[c], 0.004) && close_to(g.rim[c], rim[c], 0.004) &&
              close_to(g.grad_x[c], gx[c], 0.003) && close_to(g.grad_y[c], gy[c], 0.003);
    check(ok, "rim and tilt: recovered within 0.004 / 0.003");
}

// Kills: the texture gate removed; the corridor removed; the corridor not
// folded onto the mirrored side of the axis.
void test_refusals() {
    Scene o;
    o.sun = false;
    LensFlare lf = analyse_ok(make_scene(o));
    check(!lf.sun_found && lf.ghosts.empty(), "refuse: no sun, nothing removed");
    o = Scene{};
    o.texture = 0.25;
    lf = analyse_ok(make_scene(o));
    check(lf.sun_found && lf.ghosts.empty(), "refuse: a bump on 25% scene texture is not a ghost");
    o = Scene{};
    o.g.cx = 300.0;  // straight above the axis: 90 deg off the sun line
    o.g.cy = 180.0;
    lf = analyse_ok(make_scene(o));
    check(lf.ghosts.empty(), "refuse: a bump 90 deg off the sun line is scene");
    // The control for the two cases above: the same bump on the sun line is kept.
    check(analyse_ok(make_scene(Scene{})).ghosts.size() == 1, "refuse: control, on the line it is kept");
    o = Scene{};
    o.g.cx = 420.0;  // opposite the sun, the point-symmetric side
    o.g.cy = 296.0;
    lf = analyse_ok(make_scene(o));
    check(lf.ghosts.size() == 1 && close_to(lf.ghosts[0].cx, 420.0, 0.3),
          "search: the mirrored side of the axis is searched too");
    std::string err;
    LensFlare out;
    check(!analyse(Image{}, synthetic_lens(), Params{}, out, err), "refuse: an invalid image");
    check(!analyse(make_scene(Scene{}), Lens{}, Params{}, out, err), "refuse: an invalid lens");
    Params bad;
    bad.sun_level_fraction = std::nan("");
    check(!analyse(make_scene(Scene{}), synthetic_lens(), bad, out, err), "refuse: invalid parameters");
}

// Kills: subtracting at the wrong pixel centre, or anywhere past the reach.
void test_flattens_ghost() {
    const Image img = make_scene(Scene{});
    Scene clean;
    clean.ghost = false;
    const Image ref = make_scene(clean);
    const LensFlare lf = analyse_ok(img);
    check(lf.ghosts.size() == 1, "flatten: one ghost fitted");
    if (lf.ghosts.size() != 1) return;
    const std::vector<KernelGhost> ks{kernel(lf.ghosts[0])};
    double before_err = 0.0, after_err = 0.0;
    size_t touched = 0;
    bool within = true, positive = true;
    for (uint32_t y = 0; y < img.h; ++y)
        for (uint32_t x = 0; x < img.w; ++x) {
            const size_t i = ((size_t)y * img.w + x) * 3;
            float rgb[3] = {img.rgb[i], img.rgb[i + 1], img.rgb[i + 2]};
            const float before[3] = {rgb[0], rgb[1], rgb[2]};
            remove_at(ks, x + 0.5f, y + 0.5f, rgb);
            if (std::memcmp(rgb, before, sizeof rgb) == 0) continue;
            ++touched;
            within &= std::hypot(x + 0.5 - lf.ghosts[0].cx, y + 0.5 - lf.ghosts[0].cy) <=
                      lf.ghosts[0].reach() + 1.0;
            for (int c = 0; c < 3; ++c) {
                positive &= rgb[c] >= 0.0f;
                before_err += std::fabs(before[c] - ref.rgb[i + c]);
                after_err += std::fabs(rgb[c] - ref.rgb[i + c]);
            }
        }
    std::printf("  touched %zu, mean abs error vs clean: before %.5f after %.5f\n", touched,
                before_err / std::max<size_t>(touched, 1), after_err / std::max<size_t>(touched, 1));
    check(touched > 100 && within && positive, "flatten: only pixels within the reach change, none < 0");
    check(after_err < 0.15 * before_err, "flatten: at least 85% of the ghost's light is gone");
}

// ---- frames in code values ----------------------------------------------------

// Kills: averaging code values instead of linear light.
void test_downsample() {
    const int w = 128, h = 64;
    std::vector<uint16_t> px((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                px[((size_t)y * w + x) * 3 + c] = x < w / 2 ? 20000 : (x < 96 ? 40000 : ((x & 1) ? 60000 : 10000));
    Frame f;
    f.w = w;
    f.h = h;
    f.bits = 16;
    f.data16 = px.data();
    Image img;
    std::string err;
    check(downsample(f, Encoding::DlogM, 4, img, err) && img.w == 32 && img.h == 16,
          "downsample: 128 x 64 at factor 4 is 32 x 16");
    const float lo = code_to_linear(20000.0f / 65535.0f, Encoding::DlogM);
    const float hi = code_to_linear(40000.0f / 65535.0f, Encoding::DlogM);
    check(close_to(img.rgb[0], lo, 1e-6 * lo) && close_to(img.rgb[(5 * 32 + 20) * 3 + 1], hi, 1e-6 * hi),
          "downsample: a pure block is its code's linear light");
    // Taps at x offsets 1 and 2: one odd (60000), one even (10000) code.
    const float mixed = 0.5f * (code_to_linear(60000.0f / 65535.0f, Encoding::DlogM) +
                                code_to_linear(10000.0f / 65535.0f, Encoding::DlogM));
    const float code_mean = code_to_linear(35000.0f / 65535.0f, Encoding::DlogM);
    const float got = img.rgb[(5 * 32 + 28) * 3];
    std::printf("  mixed block %.5f, linear mean %.5f, code mean %.5f\n", got, mixed, code_mean);
    check(close_to(got, mixed, 1e-5 * mixed) && std::fabs(mixed - code_mean) > 0.1 * mixed,
          "downsample: a mixed block is the mean of LINEAR light");
    std::vector<uint16_t> odd(13 * 9 * 3, 30000);
    Frame fo;
    fo.w = 13;
    fo.h = 9;
    fo.bits = 16;
    fo.data16 = odd.data();
    check(downsample(fo, Encoding::DlogM, 4, img, err) && img.w == 4 && img.h == 3 &&
              close_to(img.rgb.back(), img.rgb[2], 1e-7),
          "downsample: a partial edge block is a real mean");
    check(!downsample(f, Encoding::DlogM, 0, img, err) && !downsample(f, Encoding::DlogM, 17, img, err) &&
              !downsample(Frame{}, Encoding::DlogM, 4, img, err),
          "downsample: factor 0, 17 and an empty frame are refused");
}

// Kills: an encode that is not the decode's inverse. OpenOSV's BT.709 pair
// breaks at 0.018 / 0.081, which do not quite meet (4.5 * 0.018 = 0.081 but the
// power branch gives 0.0812 there), so 16-bit codes in that gap do not round-trip.
void test_curves() {
    check(close_to(code_to_linear(0.40f, Encoding::DlogM), 0.18, 1e-4), "curves: D-Log M code 0.40 is 0.18");
    for (int e = 0; e < 2; ++e) {
        const Encoding enc = e ? Encoding::Rec709 : Encoding::DlogM;
        int worst16 = 0, worst8 = 0, gap = 0;
        for (int i = 1000; i <= 64000; ++i) {
            const float lin = code_to_linear(i / 65535.0f, enc);
            const int d = std::abs((int)std::lround(linear_to_code(lin, enc) * 65535.0f) - i);
            if (enc == Encoding::Rec709 && i >= 5300 && i <= 5325) gap = std::max(gap, d);
            else worst16 = std::max(worst16, d);
        }
        for (int i = 5; i <= 250; ++i) {
            const float lin = code_to_linear(i / 255.0f, enc);
            worst8 = std::max(worst8, std::abs((int)std::lround(linear_to_code(lin, enc) * 255.0f) - i));
        }
        std::printf("  %s: worst round trip %d (16-bit), %d (8-bit), %d in the BT.709 gap\n",
                    e ? "BT.709" : "D-Log M", worst16, worst8, gap);
        check(worst16 <= 1 && worst8 == 0,
              std::string("curves: ") + (e ? "BT.709" : "D-Log M") + " round trip within a 16-bit code, exact at 8");
    }
}

// A 600 x 600 D-Log M frame of `scene`, 16-bit codes.
std::vector<uint16_t> encode16(const Image& scene) {
    std::vector<uint16_t> px(scene.rgb.size());
    for (size_t i = 0; i < px.size(); ++i)
        px[i] = (uint16_t)std::lround(
            std::clamp(linear_to_code(scene.rgb[i], Encoding::DlogM), 0.0f, 1.0f) * 65535.0f);
    return px;
}

// Kills: re-encoding every pixel (the round trip is not exact), and a subtraction
// that can brighten a pixel.
void test_subtract_frame() {
    std::vector<uint16_t> px = encode16(make_scene(Scene{}));
    const std::vector<uint16_t> orig = px;
    Frame f;
    f.w = f.h = 600;
    f.bits = 16;
    f.data16 = px.data();
    Image img;
    std::string err;
    downsample(f, Encoding::DlogM, 1, img, err);
    LensFlare lf;
    analyse(img, synthetic_lens(), Params{}, lf, err);
    check(lf.ghosts.size() == 1, "frame: the ghost is found through the D-Log M decode");
    if (lf.ghosts.size() != 1) return;
    const int64_t changed = subtract(f, Encoding::DlogM, lf);
    int64_t differ = 0, outside = 0, brighter = 0;
    const double reach = lf.ghosts[0].reach() + 1.0;
    for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 600; ++x) {
            const size_t i = ((size_t)y * 600 + x) * 3;
            bool d = false;
            for (int c = 0; c < 3; ++c) {
                d |= px[i + c] != orig[i + c];
                brighter += px[i + c] > orig[i + c];
            }
            differ += d;
            if (d && std::hypot(x + 0.5 - lf.ghosts[0].cx, y + 0.5 - lf.ghosts[0].cy) > reach) ++outside;
        }
    std::printf("  changed %lld pixels (counted %lld), outside the reach %lld, brighter %lld\n",
                (long long)changed, (long long)differ, (long long)outside, (long long)brighter);
    check(changed > 100 && changed == differ, "frame: the returned count is the pixels that differ");
    check(outside == 0 && brighter == 0, "frame: nothing past the reach changes and nothing brightens");
    // Every pixel is remove_at at its own centre (+0.5), decoded and re-encoded.
    const std::vector<KernelGhost> ks{kernel(lf.ghosts[0])};
    int64_t mismatched = 0;
    for (int y = 0; y < 600; ++y)
        for (int x = 0; x < 600; ++x) {
            const size_t i = ((size_t)y * 600 + x) * 3;
            float rgb[3];
            for (int c = 0; c < 3; ++c) rgb[c] = code_to_linear(orig[i + c] / 65535.0f, Encoding::DlogM);
            remove_at(ks, x + 0.5f, y + 0.5f, rgb);
            for (int c = 0; c < 3; ++c) {
                const int want = (int)std::nearbyint(
                    std::clamp(linear_to_code(rgb[c], Encoding::DlogM), 0.0f, 1.0f) * 65535.0f);
                mismatched += px[i + c] != want;
            }
        }
    check(mismatched == 0, "frame: every sample is remove_at at the pixel centre, re-encoded");
}

// Kills: re-encoding every pixel near a ghost instead of only the ones whose
// light changed -- visible on BT.709 codes in the curve's gap, which move on a
// decode / encode round trip alone.
void test_subtract_leaves_gap_codes() {
    const uint16_t gap_code = 5310;
    const float lin = code_to_linear(gap_code / 65535.0f, Encoding::Rec709);
    const int moved = (int)std::nearbyint(linear_to_code(lin, Encoding::Rec709) * 65535.0f) - gap_code;
    check(moved != 0, "frame: a BT.709 gap code moves on a bare round trip (the fixture can tell)");
    std::vector<uint16_t> px(200 * 200 * 3, gap_code);
    Frame f;
    f.w = f.h = 200;
    f.bits = 16;
    f.data16 = px.data();
    LensFlare lf;
    Ghost g = planted_ghost();
    g.cx = g.cy = 100.0;
    lf.ghosts.push_back(g);
    const int64_t changed = subtract(f, Encoding::Rec709, lf);
    int64_t outside_moved = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x) {
            const size_t i = ((size_t)y * 200 + x) * 3;
            float light[3];
            const bool lit = ghost_light(kernel(g), x + 0.5f, y + 0.5f, light) &&
                             (light[0] > 0.0f || light[1] > 0.0f || light[2] > 0.0f);
            if (!lit) outside_moved += px[i] != gap_code || px[i + 1] != gap_code || px[i + 2] != gap_code;
        }
    std::printf("  changed %lld, moved without any ghost light %lld\n", (long long)changed,
                (long long)outside_moved);
    check(changed > 100 && outside_moved == 0, "frame: pixels the ghost adds no light to keep their codes");
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- the clipped-sun path ----------------------------------------------------

// A coded lens over a sky bright enough that the frame maximum is under 6x the
// median, so only the clipped path can find a sun. `shape` pixels carry `code`
// in every channel, or in red alone.
Image clipped_scene(Encoding e, const std::function<bool(double, double)>& shape, float code = 1.0f,
                    bool red_only = false, float sky_code = 0.65f) {
    Image img;
    img.w = img.h = 600;
    img.factor = 1;
    img.coded = true;
    img.encoding = e;
    const float sky = code_to_linear(sky_code, e), top = code_to_linear(code, e);
    img.rgb.assign(600u * 600u * 3u, sky);
    for (uint32_t y = 0; y < img.h; ++y)
        for (uint32_t x = 0; x < img.w; ++x) {
            if (!shape(x + 0.5, y + 0.5)) continue;
            float* dst = &img.rgb[((size_t)y * img.w + x) * 3];
            dst[0] = top;
            if (!red_only) dst[1] = dst[2] = top;
        }
    return img;
}

std::function<bool(double, double)> disc(double cx, double cy, double r) {
    return [=](double x, double y) { return std::hypot(x - cx, y - cy) < r; };
}

// Each check names what it kills: the path absent; a threshold fixed to the
// D-Log M curve; no size, aspect or fill limit; clipping judged against the
// frame maximum, or on any one channel; the clipped path tried before the ratio.
void test_clipped_sun() {
    const auto sun = disc(300.0, 130.0, 12.0);
    Image img = clipped_scene(Encoding::DlogM, sun);
    LensFlare lf = analyse_ok(img);
    check(lf.sun_found && close_to(lf.sun_x, 300.0, 0.5) && close_to(lf.sun_y, 130.0, 0.5) &&
              close_to(lf.sun_radius, 12.0, 1.0),
          "clipped: a D-Log M sun over a bright sky is found by its clipped pixels");
    img.coded = false;
    check(!analyse_ok(img).sun_found, "clipped: control, the 6x ratio alone misses that sun");
    check(analyse_ok(clipped_scene(Encoding::Rec709, sun, 1.0f, false, 0.60f)).sun_found,
          "clipped: a BT.709 sun is clipped against the BT.709 curve's top");
    check(!analyse_ok(clipped_scene(Encoding::DlogM, [](double x, double y) {
               return std::fabs(x - 300.0) < 75.0 && std::fabs(y - 260.0) < 75.0;
           })).sun_found,
          "clipped: a window-sized clipped region is not the sun");
    check(!analyse_ok(clipped_scene(Encoding::DlogM, [](double x, double y) {
               return std::fabs(x - 300.0) < 30.0 && std::fabs(y - 130.0) < 6.0;
           })).sun_found,
          "clipped: a clipped bar of aspect 5 is not the sun");
    check(!analyse_ok(clipped_scene(Encoding::DlogM, [](double x, double y) {
               const double d = std::hypot(x - 300.0, y - 130.0);
               return d < 14.0 && d >= 10.0;
           })).sun_found,
          "clipped: a clipped ring is not the sun");
    check(!analyse_ok(clipped_scene(Encoding::DlogM, sun, 0.90f)).sun_found,
          "clipped: a disc at code 0.90 is bright, not clipped");
    check(!analyse_ok(clipped_scene(Encoding::DlogM, sun, 1.0f, true)).sun_found,
          "clipped: a disc clipped in red alone is not the sun");
    // A 7 px core at the top code in a 12 px halo at code 0.96, which is
    // clipped but under 92% of the frame maximum, over a dark sky.
    Image cored = clipped_scene(Encoding::DlogM, sun, 0.96f, false, 0.30f);
    for (uint32_t y = 0; y < cored.h; ++y)
        for (uint32_t x = 0; x < cored.w; ++x)
            if (std::hypot(x + 0.5 - 300.0, y + 0.5 - 130.0) < 7.0)
                for (int c = 0; c < 3; ++c)
                    cored.rgb[((size_t)y * cored.w + x) * 3 + c] = code_to_linear(1.0f, Encoding::DlogM);
    lf = analyse_ok(cored);
    check(lf.sun_found && close_to(lf.sun_radius, 7.0, 1.0),
          "clipped: where the 6x ratio finds a sun, its core is the sun, not the clipped halo");
}

// Real frames, when given: SS_FLARE_SUN_FRAME must show a sun, and
// SS_FLARE_WINDOW_FRAME (a blown window, no sun) must not; both 16-bit D-Log M.
void test_real_frames() {
    const auto sun_of = [](const char* path, bool& found) {
        int w = 0, h = 0, n = 0;
        Frame fr;
        fr.data16 = stbi_load_16(path, &w, &h, &n, 0);
        fr.w = w;
        fr.h = h;
        fr.channels = n;
        fr.bits = 16;
        Image img;
        std::string err;
        LensFlare lf;
        const bool ok = fr.data16 && downsample(fr, Encoding::DlogM, Params{}.factor, img, err) &&
                        analyse(img, centred_lens(w, h), Params{}, lf, err);
        stbi_image_free(fr.data16);
        found = lf.sun_found;
        std::printf("  %s: sun %s at (%.0f, %.0f) r %.0f, %u candidates, %zu ghosts\n", path,
                    found ? "found" : "not found", lf.sun_x, lf.sun_y, lf.sun_radius, lf.candidates,
                    lf.ghosts.size());
        return ok;
    };
    bool found = false;
    if (const char* p = std::getenv("SS_FLARE_SUN_FRAME"))
        check(sun_of(p, found) && found, "real: the sun at the rim of an Avata D-Log M frame is found");
    if (const char* p = std::getenv("SS_FLARE_WINDOW_FRAME"))
        check(sun_of(p, found) && !found, "real: a blown-out window is not taken for the sun");
}

// Kills: rewriting a file with nothing removed; a 16-bit writer that loses bits.
void test_process_file(const fs::path& dir) {
    Params p;
    p.factor = 1;
    Scene none;
    none.sun = false;
    const std::vector<uint16_t> a = encode16(make_scene(none));
    const std::vector<uint16_t> b = encode16(make_scene(Scene{}));
    const fs::path fa = dir / "sunless.png", fb = dir / "ghost.png";
    check(app::save_png16(fa.string(), a.data(), 600, 600, 3) &&
              app::save_png16(fb.string(), b.data(), 600, 600, 3),
          "file: two 16-bit RGB PNGs written");
    int w = 0, h = 0, n = 0;
    stbi_us* back = stbi_load_16(fb.string().c_str(), &w, &h, &n, 0);
    check(back && w == 600 && h == 600 && n == 3 && std::memcmp(back, b.data(), b.size() * 2) == 0,
          "file: save_png16 round-trips RGB exactly");
    stbi_image_free(back);
    const std::string bytes_a = slurp(fa);
    const auto old_time = fs::file_time_type::clock::now() - std::chrono::hours(24 * 365);
    fs::last_write_time(fa, old_time);
    FileReport ra, rb;
    std::string err;
    check(process_file(fa.string(), Encoding::DlogM, p, ra, err) && !ra.sun_found && !ra.written &&
              slurp(fa) == bytes_a && fs::last_write_time(fa) == old_time,
          "file: no sun, the file is left alone (bytes and time)");
    check(process_file(fb.string(), Encoding::DlogM, p, rb, err) && rb.sun_found && rb.ghosts == 1 &&
              rb.written && rb.changed > 100,
          "file: a ghost, the file is rewritten");
    back = stbi_load_16(fb.string().c_str(), &w, &h, &n, 0);
    int64_t differ = 0;
    for (size_t i = 0; back && i + 2 < b.size(); i += 3)
        differ += back[i] != b[i] || back[i + 1] != b[i + 1] || back[i + 2] != b[i + 2];
    check(back && stbi_is_16_bit(fb.string().c_str()) && differ == rb.changed,
          "file: still 16-bit, and differs from the original in exactly the changed pixels");
    stbi_image_free(back);
    std::error_code ec;
    int leftovers = 0;
    for (const auto& e : fs::directory_iterator(dir, ec)) leftovers += e.path().string().find(".tmp") != std::string::npos;
    check(leftovers == 0, "file: no temporary left behind");
}

}  // namespace

int main() {
    test_shape();
    test_soft_subtract();
    test_remove_outside_untouched();
    test_to_kernel();
    test_recovers_planted_ghost();
    test_recovers_rim_and_tilt();
    test_refusals();
    test_clipped_sun();
    test_real_frames();
    test_flattens_ghost();
    test_downsample();
    test_curves();
    test_subtract_frame();
    test_subtract_leaves_gap_codes();
    const fs::path tmp = fs::temp_directory_path() / ("flare_removal_test_" + std::to_string(::getpid()));
    fs::create_directories(tmp);
    test_process_file(tmp);
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
