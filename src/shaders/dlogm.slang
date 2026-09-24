// DJI D-Log M, Osmo 360: code value -> scene-linear Rec.2020 (grey 0.18).
// Device copy of core/DlogM.h; the constants must match it, and the
// gt_decode_dlogm engine test compares the two.
// Based on https://github.com/Kemerd/OpenOSV/blob/3a39776272efb5dfdc1d29711ae746e855383084/include/osv/color/DlogM.h
// and https://github.com/Kemerd/OpenOSV/blob/3a39776272efb5dfdc1d29711ae746e855383084/include/osv/color/Matrices.h
// SPDX-FileCopyrightText: Copyright 2026 The OpenOSV Contributors
// SPDX-License-Identifier: Apache-2.0

static const float kDlogMXShift    = -2.360862594f;
static const float kDlogMYShift    = 0.630835854f;
static const float kDlogMScale     = 6.691455736f;
static const float kDlogMSlope     = 1.011886004f;
static const float kDlogMSlope2    = 3.035658045f;
static const float kDlogMIntercept = 0.822056039f;
static const float kDlogMMidGray   = 0.00786506109f;

float dlogm_osmo360_to_linear(float code) {
    float cut = kDlogMIntercept / (kDlogMSlope2 - kDlogMSlope);
    float t = exp2(kDlogMScale * code + kDlogMYShift) + kDlogMXShift;
    float pw = t < cut ? t * kDlogMSlope + kDlogMIntercept : t * kDlogMSlope2;
    return pw * kDlogMMidGray;
}

// Row-major, as mul(M, v) reads it: linear camera-native -> linear Rec.2020.
[CudaDeviceExport]
float3 dlogm_osmo360_to_rec2020(float3 code) {
    float3x3 m = float3x3(
        0.807268560f, 0.152663648f, 0.040067792f,
        0.042878162f, 0.990737677f, -0.033615828f,
        -0.009603872f, -0.094263740f, 1.103867650f);
    float3 lin = float3(dlogm_osmo360_to_linear(code.x),
                        dlogm_osmo360_to_linear(code.y),
                        dlogm_osmo360_to_linear(code.z));
    return mul(m, lin);
}
