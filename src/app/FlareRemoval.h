#pragma once

// Sun ghost removal for one fisheye lens frame: find the sun, fit its internal
// reflections as soft rounded rectangles on a small working image, subtract
// them in the lens's native linear light. Everything else stays bit-identical.
// docs/notes/flare.md. Based on
// https://github.com/Kemerd/OpenOSV/blob/e169fe1dd7b1e2da7cd0a52734393dc155670722/src/osv/render/Flare.cpp
// and the [WP-FLARE] kernel functions of its osv_kernel.h.
// SPDX-FileCopyrightText: Copyright 2026 The OpenOSV Contributors
// SPDX-License-Identifier: Apache-2.0

#include "sfm/core/Telemetry.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace app::flare {

inline constexpr int kMaxGhosts = 4;

// How a frame's code values map to native linear light (OpenOSV's
// osvCodeToLinear): D-Log M per channel, or the BT.709 camera curve.
enum class Encoding { DlogM, Rec709 };

// A native-linear RGB image of one lens. Pixel (x, y) is the mean of a 2 x 2
// sample grid inside the lens's `factor`-sized block at (x, y) * factor.
struct Image {
    uint32_t w = 0, h = 0, factor = 1;
    std::vector<float> rgb;  // interleaved, w * h * 3
    bool coded = false;      // decoded from code values by `encoding`; enables the clipped-sun path
    Encoding encoding = Encoding::Rec709;
    bool valid() const {
        return w > 0 && h > 0 && factor > 0 && w <= 16384 && h <= 16384 &&
               rgb.size() == (size_t)w * h * 3;
    }
};

// Optical centre and image-circle radius, in lens pixels.
struct Lens {
    double cx = 0, cy = 0, r_max = 0;
    bool valid() const;
};

// One fitted ghost in lens pixels. Its light per channel is
// amp * S + rim * E + (grad_x * u + grad_y * v) * S, clamped at 0 (ghost_light).
struct Ghost {
    double cx = 0, cy = 0;
    double hx = 0, hy = 0;   // half extents along the rotated local axes
    double radius = 0;       // corner radius, 0 .. min(hx, hy)
    double angle = 0;        // rotation of the local x axis, radians
    double soft = 1;         // half width of the edge ramp
    double amp[3] = {}, rim[3] = {}, grad_x[3] = {}, grad_y[3] = {};
    double contrast = 0;     // plateau luma / background luma
    double fit_r2 = 0;       // share of the local variance the ghost explains
    // Past this distance from the centre every term is exactly 0.
    double reach() const;
};

struct LensFlare {
    bool sun_found = false;
    double sun_x = 0, sun_y = 0, sun_radius = 0;  // lens px
    std::vector<Ghost> ghosts;                    // most visible first
    uint32_t candidates = 0, rejected = 0;
};

// Defaults are OpenOSV's, measured on its sample clip (docs/notes/flare.md).
struct Params {
    uint32_t factor = 4;
    double sun_level_fraction = 0.92;
    double sun_min_ratio_to_median = 6.0;
    double sun_max_aspect = 1.8;
    double sun_min_fill = 0.5;
    double sun_min_area = 3.0;             // working px
    // The clipped path, tried on a coded image when the ratio above finds no
    // sun: the largest blob clipped in every channel. Limits measured on three
    // Avata D-Log M suns at the rim (docs/notes/flare.md).
    double clip_code = 0.95;               // every channel at or above this code
    double clip_max_radius = 0.12;         // equivalent radius / circle radius; suns 0.056-0.067
    double clip_max_aspect = 2.0;          // second-moment aspect; suns 1.22-1.56
    double clip_min_fill = 0.6;            // area / its moment ellipse; suns 0.77-0.94
    double corridor_deg = 20.0;            // azimuth tolerance about the sun line
    double background_sigma = 12.0;        // working px
    double seed_contrast = 0.03;
    double max_texture = 0.02;
    double min_peak_over_texture = 4.0;
    double sun_exclusion_radii = 3.0;
    double rim_fraction = 0.95;
    double max_candidate_area = 2000.0;    // working px^2
    double max_candidate_extent = 160.0;   // lens px
    double min_contrast = 0.04;
    double min_fit_r2 = 0.5;
    double max_aspect = 3.0;
    double max_soft = 6.0;                 // working px
    int max_iterations = 30;
    int max_ghosts = kMaxGhosts;
    bool valid() const;
};

// A frame without a sun is not an error: sun_found stays false.
bool analyse(const Image& image, const Lens& lens, const Params& params,
             LensFlare& out, std::string& error);

// ---- the per-pixel model (float, as a renderer evaluates it) ----

// f(x) = x - g x^3 / (x^3 + g^3): >= 0.47 x, slope >= 0.16, never negative.
float soft_subtract(float x, float g);

// A ghost ready to evaluate, or false when a value is unusable.
struct KernelGhost {
    float cx, cy, hx, hy, radius, cos_a, sin_a, soft;
    float amp[3], rim[3], grad_x[3], grad_y[3];
    float reach2;
};
bool to_kernel(const Ghost& g, KernelGhost& k);
float ghost_shape(const KernelGhost& g, float px, float py);
// False, `rgb` untouched, past the ghost's reach.
bool ghost_light(const KernelGhost& g, float px, float py, float rgb[3]);
// Every ghost of `ghosts` subtracted from the linear triple `rgb`, in place.
void remove_at(const std::vector<KernelGhost>& ghosts, float px, float py, float rgb[3]);

// ---- frames in code values ----

// False for a log profile with no decode here. Normal, unknown and unrecorded
// clips are read as BT.709.
bool encoding_for(sfm::VideoColorMode mode, Encoding& e);
float code_to_linear(float code, Encoding e);
float linear_to_code(float lin, Encoding e);

// Code values, 8 or 16 bits, 3 or 4 channels (alpha untouched).
struct Frame {
    int w = 0, h = 0, channels = 3, bits = 8;
    uint8_t* data8 = nullptr;
    uint16_t* data16 = nullptr;
};
bool downsample(const Frame& f, Encoding e, uint32_t factor, Image& out,
                std::string& error);

// Changes only pixels inside a ghost's reach. Returns how many changed.
int64_t subtract(Frame& f, Encoding e, const LensFlare& flare);

// The default lens of a square fisheye frame: centred, circle touching the edges.
Lens centred_lens(int w, int h);

struct FileReport {
    bool sun_found = false;
    double sun_x = 0, sun_y = 0, sun_radius = 0;
    int ghosts = 0;
    uint32_t candidates = 0;
    int64_t changed = 0;
    bool written = false;   // the file was rewritten
    double ms = 0;
};

// Reads an 8-bit JPEG/PNG or a 16-bit PNG, analyses it with centred_lens, and
// rewrites it in place (same format, written to a temporary and renamed) only
// when a ghost was removed.
bool process_file(const std::string& path, Encoding e, const Params& params,
                  FileReport& report, std::string& error);

struct TreeReport {
    int64_t files = 0;     // processed by this call
    int64_t changed = 0;   // of those, rewritten
};

// process_file over every image under `dir` that its folder's `.spirula-flare`
// record does not list, `threads` at a time; each file is recorded as it is
// done, so a resumed pass never subtracts twice. Stops at the first failure.
bool process_tree(const std::string& dir, Encoding e, const Params& params, int threads,
                  const std::function<void(const std::string&, const FileReport&)>& on_file,
                  const std::atomic<bool>* cancel, TreeReport& report, std::string& error);

}  // namespace app::flare
