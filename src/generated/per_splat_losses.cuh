#pragma once

#include "generated/slang.cuh"

struct DiffPair_float_0
{
    float primal_0;
    float differential_0;
};

inline __device__ void _d_exp_0(DiffPair_float_0 * dpx_0, float dOut_0)
{
    float _S1 = (F32_exp(((*dpx_0).primal_0))) * dOut_0;
    dpx_0->primal_0 = (*dpx_0).primal_0;
    dpx_0->differential_0 = _S1;
    return;
}

inline __device__ void _d_max_0(DiffPair_float_0 * dpx_1, DiffPair_float_0 * dpy_0, float dOut_1)
{
    DiffPair_float_0 _S2 = *dpx_1;
    float _S3;
    if(((*dpx_1).primal_0) > ((*dpy_0).primal_0))
    {
        _S3 = dOut_1;
    }
    else
    {
        if(((*dpx_1).primal_0) < ((*dpy_0).primal_0))
        {
            _S3 = 0.0f;
        }
        else
        {
            _S3 = 0.5f * dOut_1;
        }
    }
    dpx_1->primal_0 = _S2.primal_0;
    dpx_1->differential_0 = _S3;
    DiffPair_float_0 _S4 = *dpy_0;
    if(((*dpy_0).primal_0) > (_S2.primal_0))
    {
        _S3 = dOut_1;
    }
    else
    {
        if(((*dpy_0).primal_0) < ((*dpx_1).primal_0))
        {
            _S3 = 0.0f;
        }
        else
        {
            _S3 = 0.5f * dOut_1;
        }
    }
    dpy_0->primal_0 = _S4.primal_0;
    dpy_0->differential_0 = _S3;
    return;
}

inline __device__ void _d_sqrt_0(DiffPair_float_0 * dpx_2, float dOut_2)
{
    float _S5 = 0.5f / (F32_sqrt(((F32_max((1.00000001168609742e-07f), ((*dpx_2).primal_0)))))) * dOut_2;
    dpx_2->primal_0 = (*dpx_2).primal_0;
    dpx_2->differential_0 = _S5;
    return;
}

inline __device__ float dot_0(float4  x_0, float4  y_0)
{
    int i_0 = int(0);
    float result_0 = 0.0f;
    for(;;)
    {
        if(i_0 < int(4))
        {
        }
        else
        {
            break;
        }
        float result_1 = result_0 + _slang_vector_get_element(x_0, i_0) * _slang_vector_get_element(y_0, i_0);
        i_0 = i_0 + int(1);
        result_0 = result_1;
    }
    return result_0;
}

inline __device__ float length_0(float4  x_1)
{
    return (F32_sqrt((dot_0(x_1, x_1))));
}

inline __device__ void _d_log_0(DiffPair_float_0 * dpx_3, float dOut_3)
{
    float _S6 = 1.0f / (*dpx_3).primal_0 * dOut_3;
    dpx_3->primal_0 = (*dpx_3).primal_0;
    dpx_3->differential_0 = _S6;
    return;
}

inline __device__ void _d_min_0(DiffPair_float_0 * dpx_4, DiffPair_float_0 * dpy_1, float dOut_4)
{
    DiffPair_float_0 _S7 = *dpx_4;
    float _S8;
    if(((*dpx_4).primal_0) < ((*dpy_1).primal_0))
    {
        _S8 = dOut_4;
    }
    else
    {
        if(((*dpx_4).primal_0) > ((*dpy_1).primal_0))
        {
            _S8 = 0.0f;
        }
        else
        {
            _S8 = 0.5f * dOut_4;
        }
    }
    dpx_4->primal_0 = _S7.primal_0;
    dpx_4->differential_0 = _S8;
    DiffPair_float_0 _S9 = *dpy_1;
    if(((*dpy_1).primal_0) < (_S7.primal_0))
    {
        _S8 = dOut_4;
    }
    else
    {
        if(((*dpy_1).primal_0) > ((*dpx_4).primal_0))
        {
            _S8 = 0.0f;
        }
        else
        {
            _S8 = 0.5f * dOut_4;
        }
    }
    dpy_1->primal_0 = _S9.primal_0;
    dpy_1->differential_0 = _S8;
    return;
}

struct DiffPair_vectorx3Cfloatx2C3x3E_0
{
    float3  primal_0;
    float3  differential_0;
};

inline __device__ float3  exp_0(float3  x_2)
{
    float3  result_2;
    int i_1 = int(0);
    for(;;)
    {
        if(i_1 < int(3))
        {
        }
        else
        {
            break;
        }
        *_slang_vector_get_element_ptr(&result_2, i_1) = (F32_exp((_slang_vector_get_element(x_2, i_1))));
        i_1 = i_1 + int(1);
    }
    return result_2;
}

inline __device__ void _d_exp_vector_0(DiffPair_vectorx3Cfloatx2C3x3E_0 * dpx_5, float3  dOut_5)
{
    float3  _S10 = exp_0((*dpx_5).primal_0) * dOut_5;
    dpx_5->primal_0 = (*dpx_5).primal_0;
    dpx_5->differential_0 = _S10;
    return;
}

inline __device__ void per_splat_losses(float3  scales_0, float opacity_0, float4  quat_0, float mcmc_opacity_reg_weight_0, float mcmc_scale_reg_weight_0, float max_gauss_ratio_0, float scale_regularization_weight_0, float erank_reg_weight_0, float erank_reg_weight_s3_0, float quat_norm_reg_weight_0, FixedArray<float, 5>  * _S11)
{
    FixedArray<float, 5>  losses_0;
    losses_0[int(0)] = mcmc_opacity_reg_weight_0 * (1.0f / (1.0f + (F32_exp((- opacity_0)))));
    float quat_norm_0 = length_0(quat_0);
    losses_0[int(4)] = quat_norm_reg_weight_0 * (quat_norm_0 - 1.0f - (F32_log((quat_norm_0))));
    float _S12 = scales_0.x;
    float _S13 = scales_0.y;
    float _S14 = scales_0.z;
    losses_0[int(1)] = mcmc_scale_reg_weight_0 * 0.00999999977648258f * (_S12 + _S13 + _S14) / 3.0f;
    float _S15 = (F32_max(((F32_max((_S12), (_S13)))), (_S14)));
    losses_0[int(2)] = scale_regularization_weight_0 * ((F32_max(((F32_exp(((F32_min((_S15 - (F32_min(((F32_min((_S12), (_S13)))), (_S14)))), (80.0f))))))), (max_gauss_ratio_0))) - max_gauss_ratio_0);
    float3  _S16 = exp_0(make_float3 (2.0f) * (scales_0 - make_float3 (_S15)));
    float x_3 = _S16.x;
    float y_1 = _S16.y;
    float z_0 = _S16.z;
    float s_0 = x_3 + y_1 + z_0;
    float s1_0 = (F32_max(((F32_max((x_3), (y_1)))), (z_0))) / s_0;
    float _S17 = (F32_max(((F32_min(((F32_min((x_3), (y_1)))), (z_0))) / s_0), (1.00000000317107685e-30f)));
    float _S18 = (F32_max((1.0f - s1_0 - _S17), (1.00000000317107685e-30f)));
    losses_0[int(3)] = erank_reg_weight_0 * (F32_max((- (F32_log(((F32_exp((- s1_0 * (F32_log((s1_0))) - _S18 * (F32_log((_S18))) - _S17 * (F32_log((_S17)))))) - 0.99998998641967773f)))), (0.0f))) + erank_reg_weight_s3_0 * _S17;
    *_S11 = losses_0;
    return;
}

inline __device__ float s_primal_ctx_exp_0(float _S19)
{
    return (F32_exp((_S19)));
}

inline __device__ float3  s_primal_ctx_exp_1(float3  _S20)
{
    return exp_0(_S20);
}

inline __device__ float s_primal_ctx_log_0(float _S21)
{
    return (F32_log((_S21)));
}

inline __device__ void s_bwd_prop_log_0(DiffPair_float_0 * _S22, float _S23)
{
    _d_log_0(_S22, _S23);
    return;
}

inline __device__ void s_bwd_prop_exp_0(DiffPair_float_0 * _S24, float _S25)
{
    _d_exp_0(_S24, _S25);
    return;
}

inline __device__ void s_bwd_prop_exp_1(DiffPair_vectorx3Cfloatx2C3x3E_0 * _S26, float3  _S27)
{
    _d_exp_vector_0(_S26, _S27);
    return;
}

struct DiffPair_vectorx3Cfloatx2C4x3E_0
{
    float4  primal_0;
    float4  differential_0;
};

inline __device__ void s_bwd_prop_sqrt_0(DiffPair_float_0 * _S28, float _S29)
{
    _d_sqrt_0(_S28, _S29);
    return;
}

inline __device__ void s_bwd_prop_length_impl_0(DiffPair_vectorx3Cfloatx2C4x3E_0 * dpx_6, float _s_dOut_0)
{
    float _S30 = (*dpx_6).primal_0.x;
    float _S31 = (*dpx_6).primal_0.y;
    float _S32 = (*dpx_6).primal_0.z;
    float _S33 = (*dpx_6).primal_0.w;
    DiffPair_float_0 _S34;
    (&_S34)->primal_0 = _S30 * _S30 + _S31 * _S31 + _S32 * _S32 + _S33 * _S33;
    (&_S34)->differential_0 = 0.0f;
    s_bwd_prop_sqrt_0(&_S34, _s_dOut_0);
    float _S35 = (*dpx_6).primal_0.w * _S34.differential_0;
    float _S36 = _S35 + _S35;
    float _S37 = (*dpx_6).primal_0.z * _S34.differential_0;
    float _S38 = _S37 + _S37;
    float _S39 = (*dpx_6).primal_0.y * _S34.differential_0;
    float _S40 = _S39 + _S39;
    float _S41 = (*dpx_6).primal_0.x * _S34.differential_0;
    float _S42 = _S41 + _S41;
    float4  _S43 = make_float4 (0.0f);
    *&((&_S43)->w) = _S36;
    *&((&_S43)->z) = _S38;
    *&((&_S43)->y) = _S40;
    *&((&_S43)->x) = _S42;
    dpx_6->primal_0 = (*dpx_6).primal_0;
    dpx_6->differential_0 = _S43;
    return;
}

inline __device__ void s_bwd_length_impl_0(DiffPair_vectorx3Cfloatx2C4x3E_0 * _S44, float _S45)
{
    s_bwd_prop_length_impl_0(_S44, _S45);
    return;
}

inline __device__ void per_splat_losses_bwd(float3  scales_1, float opacity_1, float4  quat_1, FixedArray<float, 5>  v_loss_0, float3  * v_scales_0, float * v_opacity_0, float4  * v_quat_0, float mcmc_opacity_reg_weight_1, float mcmc_scale_reg_weight_1, float max_gauss_ratio_1, float scale_regularization_weight_1, float erank_reg_weight_1, float erank_reg_weight_s3_1, float quat_norm_reg_weight_1)
{
    float _S46 = - opacity_1;
    float _S47 = 1.0f + s_primal_ctx_exp_0(_S46);
    float _S48 = _S47 * _S47;
    float _S49 = length_0(quat_1);
    float _S50 = mcmc_scale_reg_weight_1 * 0.00999999977648258f;
    float _S51 = scales_1.x;
    float _S52 = scales_1.y;
    float _S53 = scales_1.z;
    float _S54 = (F32_max((_S51), (_S52)));
    float _S55 = (F32_max((_S54), (_S53)));
    float _S56 = (F32_min((_S51), (_S52)));
    float _S57 = _S55 - (F32_min((_S56), (_S53)));
    float _S58 = (F32_min((_S57), (80.0f)));
    float _S59 = s_primal_ctx_exp_0(_S58);
    float3  _S60 = make_float3 (2.0f) * (scales_1 - make_float3 (_S55));
    float3  _S61 = s_primal_ctx_exp_1(_S60);
    float x_4 = _S61.x;
    float y_2 = _S61.y;
    float z_1 = _S61.z;
    float s_1 = x_4 + y_2 + z_1;
    float _S62 = (F32_max((x_4), (y_2)));
    float _S63 = (F32_max((_S62), (z_1)));
    float s1_1 = _S63 / s_1;
    float _S64 = s_1 * s_1;
    float _S65 = (F32_min((x_4), (y_2)));
    float _S66 = (F32_min((_S65), (z_1)));
    float _S67 = _S66 / s_1;
    float _S68 = (F32_max((_S67), (1.00000000317107685e-30f)));
    float _S69 = 1.0f - s1_1 - _S68;
    float _S70 = (F32_max((_S69), (1.00000000317107685e-30f)));
    float _S71 = - s1_1;
    float _S72 = s_primal_ctx_log_0(s1_1);
    float _S73 = s_primal_ctx_log_0(_S70);
    float _S74 = s_primal_ctx_log_0(_S68);
    float _S75 = _S71 * _S72 - _S70 * _S73 - _S68 * _S74;
    float _S76 = s_primal_ctx_exp_0(_S75) - 0.99998998641967773f;
    float _S77 = erank_reg_weight_s3_1 * v_loss_0[int(3)];
    float _S78 = erank_reg_weight_1 * v_loss_0[int(3)];
    DiffPair_float_0 _S79;
    (&_S79)->primal_0 = - s_primal_ctx_log_0(_S76);
    (&_S79)->differential_0 = 0.0f;
    DiffPair_float_0 _S80;
    (&_S80)->primal_0 = 0.0f;
    (&_S80)->differential_0 = 0.0f;
    _d_max_0(&_S79, &_S80, _S78);
    float _S81 = - _S79.differential_0;
    DiffPair_float_0 _S82;
    (&_S82)->primal_0 = _S76;
    (&_S82)->differential_0 = 0.0f;
    s_bwd_prop_log_0(&_S82, _S81);
    DiffPair_float_0 _S83;
    (&_S83)->primal_0 = _S75;
    (&_S83)->differential_0 = 0.0f;
    s_bwd_prop_exp_0(&_S83, _S82.differential_0);
    float _S84 = - _S83.differential_0;
    float _S85 = _S68 * _S84;
    float _S86 = _S74 * _S84;
    DiffPair_float_0 _S87;
    (&_S87)->primal_0 = _S68;
    (&_S87)->differential_0 = 0.0f;
    s_bwd_prop_log_0(&_S87, _S85);
    float _S88 = _S70 * _S84;
    float _S89 = _S73 * _S84;
    DiffPair_float_0 _S90;
    (&_S90)->primal_0 = _S70;
    (&_S90)->differential_0 = 0.0f;
    s_bwd_prop_log_0(&_S90, _S88);
    float _S91 = _S71 * _S83.differential_0;
    float _S92 = _S72 * _S83.differential_0;
    DiffPair_float_0 _S93;
    (&_S93)->primal_0 = s1_1;
    (&_S93)->differential_0 = 0.0f;
    s_bwd_prop_log_0(&_S93, _S91);
    float _S94 = - _S92;
    float _S95 = _S89 + _S90.differential_0;
    DiffPair_float_0 _S96;
    (&_S96)->primal_0 = _S69;
    (&_S96)->differential_0 = 0.0f;
    DiffPair_float_0 _S97;
    (&_S97)->primal_0 = 1.00000000317107685e-30f;
    (&_S97)->differential_0 = 0.0f;
    _d_max_0(&_S96, &_S97, _S95);
    float _S98 = - _S96.differential_0;
    float _S99 = _S77 + _S86 + _S87.differential_0 + _S98;
    DiffPair_float_0 _S100;
    (&_S100)->primal_0 = _S67;
    (&_S100)->differential_0 = 0.0f;
    DiffPair_float_0 _S101;
    (&_S101)->primal_0 = 1.00000000317107685e-30f;
    (&_S101)->differential_0 = 0.0f;
    _d_max_0(&_S100, &_S101, _S99);
    float _S102 = _S100.differential_0 / _S64;
    float _S103 = _S66 * - _S102;
    float _S104 = s_1 * _S102;
    DiffPair_float_0 _S105;
    (&_S105)->primal_0 = _S65;
    (&_S105)->differential_0 = 0.0f;
    DiffPair_float_0 _S106;
    (&_S106)->primal_0 = z_1;
    (&_S106)->differential_0 = 0.0f;
    _d_min_0(&_S105, &_S106, _S104);
    DiffPair_float_0 _S107;
    (&_S107)->primal_0 = x_4;
    (&_S107)->differential_0 = 0.0f;
    DiffPair_float_0 _S108;
    (&_S108)->primal_0 = y_2;
    (&_S108)->differential_0 = 0.0f;
    _d_min_0(&_S107, &_S108, _S105.differential_0);
    float _S109 = (_S93.differential_0 + _S94 + _S98) / _S64;
    float _S110 = _S63 * - _S109;
    float _S111 = s_1 * _S109;
    DiffPair_float_0 _S112;
    (&_S112)->primal_0 = _S62;
    (&_S112)->differential_0 = 0.0f;
    DiffPair_float_0 _S113;
    (&_S113)->primal_0 = z_1;
    (&_S113)->differential_0 = 0.0f;
    _d_max_0(&_S112, &_S113, _S111);
    DiffPair_float_0 _S114;
    (&_S114)->primal_0 = x_4;
    (&_S114)->differential_0 = 0.0f;
    DiffPair_float_0 _S115;
    (&_S115)->primal_0 = y_2;
    (&_S115)->differential_0 = 0.0f;
    _d_max_0(&_S114, &_S115, _S112.differential_0);
    float _S116 = _S103 + _S110;
    float3  _S117 = make_float3 (_S107.differential_0 + _S114.differential_0 + _S116, _S108.differential_0 + _S115.differential_0 + _S116, _S106.differential_0 + _S113.differential_0 + _S116);
    float3  _S118 = make_float3 (0.0f);
    DiffPair_vectorx3Cfloatx2C3x3E_0 _S119;
    (&_S119)->primal_0 = _S60;
    (&_S119)->differential_0 = _S118;
    s_bwd_prop_exp_1(&_S119, _S117);
    float3  _S120 = make_float3 (2.0f) * _S119.differential_0;
    float3  _S121 = - _S120;
    float s_diff_scale_reg_T_0 = scale_regularization_weight_1 * v_loss_0[int(2)];
    DiffPair_float_0 _S122;
    (&_S122)->primal_0 = _S59;
    (&_S122)->differential_0 = 0.0f;
    DiffPair_float_0 _S123;
    (&_S123)->primal_0 = max_gauss_ratio_1;
    (&_S123)->differential_0 = 0.0f;
    _d_max_0(&_S122, &_S123, s_diff_scale_reg_T_0);
    DiffPair_float_0 _S124;
    (&_S124)->primal_0 = _S58;
    (&_S124)->differential_0 = 0.0f;
    s_bwd_prop_exp_0(&_S124, _S122.differential_0);
    DiffPair_float_0 _S125;
    (&_S125)->primal_0 = _S57;
    (&_S125)->differential_0 = 0.0f;
    DiffPair_float_0 _S126;
    (&_S126)->primal_0 = 80.0f;
    (&_S126)->differential_0 = 0.0f;
    _d_min_0(&_S125, &_S126, _S124.differential_0);
    float _S127 = - _S125.differential_0;
    DiffPair_float_0 _S128;
    (&_S128)->primal_0 = _S56;
    (&_S128)->differential_0 = 0.0f;
    DiffPair_float_0 _S129;
    (&_S129)->primal_0 = _S53;
    (&_S129)->differential_0 = 0.0f;
    _d_min_0(&_S128, &_S129, _S127);
    DiffPair_float_0 _S130;
    (&_S130)->primal_0 = _S51;
    (&_S130)->differential_0 = 0.0f;
    DiffPair_float_0 _S131;
    (&_S131)->primal_0 = _S52;
    (&_S131)->differential_0 = 0.0f;
    _d_min_0(&_S130, &_S131, _S128.differential_0);
    float _S132 = _S125.differential_0 + _S121.x + _S121.y + _S121.z;
    DiffPair_float_0 _S133;
    (&_S133)->primal_0 = _S54;
    (&_S133)->differential_0 = 0.0f;
    DiffPair_float_0 _S134;
    (&_S134)->primal_0 = _S53;
    (&_S134)->differential_0 = 0.0f;
    _d_max_0(&_S133, &_S134, _S132);
    DiffPair_float_0 _S135;
    (&_S135)->primal_0 = _S51;
    (&_S135)->differential_0 = 0.0f;
    DiffPair_float_0 _S136;
    (&_S136)->primal_0 = _S52;
    (&_S136)->differential_0 = 0.0f;
    _d_max_0(&_S135, &_S136, _S133.differential_0);
    float _S137 = _S50 * (0.3333333432674408f * v_loss_0[int(1)]);
    float _S138 = _S129.differential_0 + _S134.differential_0 + _S137;
    float _S139 = _S131.differential_0 + _S136.differential_0 + _S137;
    float _S140 = _S130.differential_0 + _S135.differential_0 + _S137;
    float s_diff_quat_norm_reg_T_0 = quat_norm_reg_weight_1 * v_loss_0[int(4)];
    float _S141 = - s_diff_quat_norm_reg_T_0;
    DiffPair_float_0 _S142;
    (&_S142)->primal_0 = _S49;
    (&_S142)->differential_0 = 0.0f;
    s_bwd_prop_log_0(&_S142, _S141);
    float _S143 = _S142.differential_0 + s_diff_quat_norm_reg_T_0;
    float4  _S144 = make_float4 (0.0f);
    DiffPair_vectorx3Cfloatx2C4x3E_0 _S145;
    (&_S145)->primal_0 = quat_1;
    (&_S145)->differential_0 = _S144;
    s_bwd_length_impl_0(&_S145, _S143);
    float _S146 = - (mcmc_opacity_reg_weight_1 * v_loss_0[int(0)] / _S48);
    DiffPair_float_0 _S147;
    (&_S147)->primal_0 = _S46;
    (&_S147)->differential_0 = 0.0f;
    s_bwd_prop_exp_0(&_S147, _S146);
    float _S148 = - _S147.differential_0;
    *v_scales_0 = _S120 + make_float3 (_S140, _S139, _S138);
    *v_opacity_0 = _S148;
    *v_quat_0 = _S145.differential_0;
    return;
}

