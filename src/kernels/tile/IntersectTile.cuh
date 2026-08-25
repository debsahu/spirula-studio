#pragma once

#include <cstdint>

#include <core/Tensor.h>

#include "primitives/Primitive3DGS.cuh"
#include "primitives/Primitive3DGUT.cuh"
#include "shaders/screen_layout.h"


// Ellipse-mode inputs, read out of a packed screen row. data == nullptr
// selects the conservative AABB test instead.
struct ProjEllipseView {
    const float* data = nullptr;
    int32_t stride = 0;
    int32_t xy = -1;     // -1: ellipse center is the AABB center (3DGUT)
    int32_t conic = 0;
    int32_t opac = 0;
};

// The two screen layouts as ellipse views. 3DGUT keeps its projected conic in
// the row's "scale" slot and stores no center.
inline ProjEllipseView proj_ellipse_view(const float* rows, bool eval3d) {
    if (eval3d)
        return {rows, SCRG_STRIDE, -1, SCRG_SCALE, SCRG_OPAC};
    return {rows, SCR2_STRIDE, SCR2_XY, SCR2_CONIC, SCR2_OPAC};
}


/* == AUTO HEADER GENERATOR - DO NOT EDIT THIS LINE OR ANYTHING BELOW THIS LINE == */



int64_t intersect_tile_count(int width, int height);


void compute_tile_active(
    TorchTensorView mask,   // [I, H_mask, W_mask] bool
    int I, int width, int height,
    int32_t* tile_active    // [I, tile_h, tile_w]
);


std::tuple<
    DeviceVector<int64_t>,    // isect_ids [n_isects]
    DeviceVector<int32_t>,    // flatten_ids [n_isects]
    DeviceTensor3D<int32_t>   // offsets [I, tile_h, tile_w]
> do_intersect_tile_generic(
    DeviceTensorFloatND aabb,     // [*N, 4] float32
    DeviceTensorFloatND depths,   // [*N] float32
    ProjEllipseView ellipse,      // .data null for AABB mode
    const uint32_t I,
    TorchTensorView intrins,      // [I, 4]
    const uint32_t image_width,
    const uint32_t image_height,
    DeviceVector<int32_t>* image_ids, // null for non-packed
    const int32_t* tile_active        // [I, tile_h, tile_w]; null = all live
);
