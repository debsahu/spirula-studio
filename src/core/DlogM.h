#pragma once

// DJI D-Log M, Osmo 360: code value -> scene-linear Rec.2020 (grey 0.18).
// Host copy; shaders/dlogm.slang is the device copy and must hold the same
// constants (gt_decode_dlogm compares the two).
// Based on https://github.com/Kemerd/OpenOSV/blob/3a39776272efb5dfdc1d29711ae746e855383084/include/osv/color/DlogM.h
// and https://github.com/Kemerd/OpenOSV/blob/3a39776272efb5dfdc1d29711ae746e855383084/include/osv/color/Matrices.h
// SPDX-FileCopyrightText: Copyright 2026 The OpenOSV Contributors
// SPDX-License-Identifier: Apache-2.0

#include "core/ColorSpace.h"

#include <cmath>

namespace colorspace {

// kDlogMOsmo360: a fit to the neutral axis of DJI's Osmo 360 D-Log M LUT.
inline constexpr float kDlogMXShift    = -2.360862594f;
inline constexpr float kDlogMYShift    = 0.630835854f;
inline constexpr float kDlogMScale     = 6.691455736f;
inline constexpr float kDlogMSlope     = 1.011886004f;
inline constexpr float kDlogMSlope2    = 3.035658045f;
inline constexpr float kDlogMIntercept = 0.822056039f;
inline constexpr float kDlogMMidGray   = 0.00786506109f;
// Where the two linear branches meet, in exp2 space: reached at code 0.1252.
inline constexpr float kDlogMCut = kDlogMIntercept / (kDlogMSlope2 - kDlogMSlope);

// kNativeToRec2020_Osmo360, row-major, linear camera-native -> linear Rec.2020.
// Rows sum to 1, so white stays white.
inline constexpr Mat3 kOsmo360ToRec2020 = {
    0.807268560f, 0.152663648f, 0.040067792f,
    0.042878162f, 0.990737677f, -0.033615828f,
    -0.009603872f, -0.094263740f, 1.103867650f};

// Not clamped: codes above 1 keep extrapolating, as the camera's do.
inline float dlogm_osmo360_to_linear(float code) {
    const float t = std::exp2(kDlogMScale * code + kDlogMYShift) + kDlogMXShift;
    const float pw = t < kDlogMCut ? t * kDlogMSlope + kDlogMIntercept
                                   : t * kDlogMSlope2;
    return pw * kDlogMMidGray;
}

// The exact inverse; the log argument is floored so it never sees <= 0.
inline float dlogm_osmo360_to_code(float lin) {
    const float pw = lin / kDlogMMidGray;
    float t = (pw - kDlogMIntercept) / kDlogMSlope;
    if (!(t < kDlogMCut)) t = pw / kDlogMSlope2;
    return (std::log2(std::max(t - kDlogMXShift, 1e-30f)) - kDlogMYShift)
         / kDlogMScale;
}

inline void dlogm_osmo360_to_rec2020(float v[3]) {
    for (int c = 0; c < 3; c++) v[c] = dlogm_osmo360_to_linear(v[c]);
    apply3x3(kOsmo360ToRec2020, v);
}

}  // namespace colorspace
