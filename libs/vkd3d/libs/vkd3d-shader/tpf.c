/*
 * TPF (Direct3D shader models 4 and 5 bytecode) support
 *
 * Copyright 2008-2009 Henri Verbeet for CodeWeavers
 * Copyright 2010 Rico Schüller
 * Copyright 2017 Józef Kucia for CodeWeavers
 * Copyright 2019-2020 Zebediah Figura for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "vkd3d_shader_private.h"

#define SM4_MAX_SRC_COUNT 6
#define SM4_MAX_DST_COUNT 2

STATIC_ASSERT(SM4_MAX_SRC_COUNT <= SPIRV_MAX_SRC_COUNT);

#define VKD3D_SM4_PS  0x0000u
#define VKD3D_SM4_VS  0x0001u
#define VKD3D_SM4_GS  0x0002u
#define VKD3D_SM5_HS  0x0003u
#define VKD3D_SM5_DS  0x0004u
#define VKD3D_SM5_CS  0x0005u
#define VKD3D_SM4_LIB 0xfff0u

#define VKD3D_SM4_INSTRUCTION_MODIFIER        (0x1u << 31)

#define VKD3D_SM4_MODIFIER_MASK               0x3fu

#define VKD3D_SM5_MODIFIER_DATA_TYPE_SHIFT    6
#define VKD3D_SM5_MODIFIER_DATA_TYPE_MASK     (0xffffu << VKD3D_SM5_MODIFIER_DATA_TYPE_SHIFT)

#define VKD3D_SM5_MODIFIER_RESOURCE_TYPE_SHIFT 6
#define VKD3D_SM5_MODIFIER_RESOURCE_TYPE_MASK (0xfu << VKD3D_SM5_MODIFIER_RESOURCE_TYPE_SHIFT)

#define VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_SHIFT 11
#define VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_MASK  (0xfffu << VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_SHIFT)

#define VKD3D_SM4_AOFFIMMI_U_SHIFT            9
#define VKD3D_SM4_AOFFIMMI_U_MASK             (0xfu << VKD3D_SM4_AOFFIMMI_U_SHIFT)
#define VKD3D_SM4_AOFFIMMI_V_SHIFT            13
#define VKD3D_SM4_AOFFIMMI_V_MASK             (0xfu << VKD3D_SM4_AOFFIMMI_V_SHIFT)
#define VKD3D_SM4_AOFFIMMI_W_SHIFT            17
#define VKD3D_SM4_AOFFIMMI_W_MASK             (0xfu << VKD3D_SM4_AOFFIMMI_W_SHIFT)

#define VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT    24
#define VKD3D_SM4_INSTRUCTION_LENGTH_MASK     (0x1fu << VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT)

#define VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT     11
#define VKD3D_SM4_INSTRUCTION_FLAGS_MASK      (0x7u << VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT)

#define VKD3D_SM4_RESOURCE_TYPE_SHIFT         11
#define VKD3D_SM4_RESOURCE_TYPE_MASK          (0xfu << VKD3D_SM4_RESOURCE_TYPE_SHIFT)

#define VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT 16
#define VKD3D_SM4_RESOURCE_SAMPLE_COUNT_MASK  (0xfu << VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT)

#define VKD3D_SM4_PRIMITIVE_TYPE_SHIFT        11
#define VKD3D_SM4_PRIMITIVE_TYPE_MASK         (0x3fu << VKD3D_SM4_PRIMITIVE_TYPE_SHIFT)

#define VKD3D_SM4_INDEX_TYPE_SHIFT            11
#define VKD3D_SM4_INDEX_TYPE_MASK             (0x1u << VKD3D_SM4_INDEX_TYPE_SHIFT)

#define VKD3D_SM4_SAMPLER_MODE_SHIFT          11
#define VKD3D_SM4_SAMPLER_MODE_MASK           (0xfu << VKD3D_SM4_SAMPLER_MODE_SHIFT)

#define VKD3D_SM4_SHADER_DATA_TYPE_SHIFT      11
#define VKD3D_SM4_SHADER_DATA_TYPE_MASK       (0xfu << VKD3D_SM4_SHADER_DATA_TYPE_SHIFT)

#define VKD3D_SM4_INTERPOLATION_MODE_SHIFT    11
#define VKD3D_SM4_INTERPOLATION_MODE_MASK     (0xfu << VKD3D_SM4_INTERPOLATION_MODE_SHIFT)

#define VKD3D_SM4_GLOBAL_FLAGS_SHIFT          11
#define VKD3D_SM4_GLOBAL_FLAGS_MASK           (0xffu << VKD3D_SM4_GLOBAL_FLAGS_SHIFT)

#define VKD3D_SM5_PRECISE_SHIFT               19
#define VKD3D_SM5_PRECISE_MASK                (0xfu << VKD3D_SM5_PRECISE_SHIFT)

#define VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT   11
#define VKD3D_SM5_CONTROL_POINT_COUNT_MASK    (0xffu << VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT)

#define VKD3D_SM5_FP_ARRAY_SIZE_SHIFT         16
#define VKD3D_SM5_FP_TABLE_COUNT_MASK         0xffffu

#define VKD3D_SM5_UAV_FLAGS_SHIFT             15
#define VKD3D_SM5_UAV_FLAGS_MASK              (0x1ffu << VKD3D_SM5_UAV_FLAGS_SHIFT)

#define VKD3D_SM5_SYNC_FLAGS_SHIFT            11
#define VKD3D_SM5_SYNC_FLAGS_MASK             (0xffu << VKD3D_SM5_SYNC_FLAGS_SHIFT)

#define VKD3D_SM5_TESSELLATOR_SHIFT           11
#define VKD3D_SM5_TESSELLATOR_MASK            (0xfu << VKD3D_SM5_TESSELLATOR_SHIFT)

#define VKD3D_SM4_OPCODE_MASK                 0xff

#define VKD3D_SM4_EXTENDED_OPERAND            (0x1u << 31)

#define VKD3D_SM4_EXTENDED_OPERAND_TYPE_MASK  0x3fu

#define VKD3D_SM4_REGISTER_MODIFIER_SHIFT     6
#define VKD3D_SM4_REGISTER_MODIFIER_MASK      (0xffu << VKD3D_SM4_REGISTER_MODIFIER_SHIFT)

#define VKD3D_SM4_REGISTER_PRECISION_SHIFT    14
#define VKD3D_SM4_REGISTER_PRECISION_MASK     (0x7u << VKD3D_SM4_REGISTER_PRECISION_SHIFT)

#define VKD3D_SM4_REGISTER_NON_UNIFORM_SHIFT  17
#define VKD3D_SM4_REGISTER_NON_UNIFORM_MASK   (0x1u << VKD3D_SM4_REGISTER_NON_UNIFORM_SHIFT)

#define VKD3D_SM4_ADDRESSING_SHIFT2           28
#define VKD3D_SM4_ADDRESSING_MASK2            (0x3u << VKD3D_SM4_ADDRESSING_SHIFT2)

#define VKD3D_SM4_ADDRESSING_SHIFT1           25
#define VKD3D_SM4_ADDRESSING_MASK1            (0x3u << VKD3D_SM4_ADDRESSING_SHIFT1)

#define VKD3D_SM4_ADDRESSING_SHIFT0           22
#define VKD3D_SM4_ADDRESSING_MASK0            (0x3u << VKD3D_SM4_ADDRESSING_SHIFT0)

#define VKD3D_SM4_REGISTER_ORDER_SHIFT        20
#define VKD3D_SM4_REGISTER_ORDER_MASK         (0x3u << VKD3D_SM4_REGISTER_ORDER_SHIFT)

#define VKD3D_SM4_REGISTER_TYPE_SHIFT         12
#define VKD3D_SM4_REGISTER_TYPE_MASK          (0xffu << VKD3D_SM4_REGISTER_TYPE_SHIFT)

#define VKD3D_SM4_SWIZZLE_TYPE_SHIFT          2
#define VKD3D_SM4_SWIZZLE_TYPE_MASK           (0x3u << VKD3D_SM4_SWIZZLE_TYPE_SHIFT)

#define VKD3D_SM4_DIMENSION_SHIFT             0
#define VKD3D_SM4_DIMENSION_MASK              (0x3u << VKD3D_SM4_DIMENSION_SHIFT)

#define VKD3D_SM4_WRITEMASK_SHIFT             4
#define VKD3D_SM4_WRITEMASK_MASK              (0xfu << VKD3D_SM4_WRITEMASK_SHIFT)

#define VKD3D_SM4_SWIZZLE_SHIFT               4
#define VKD3D_SM4_SWIZZLE_MASK                (0xffu << VKD3D_SM4_SWIZZLE_SHIFT)

#define VKD3D_SM4_SCALAR_DIM_SHIFT            4
#define VKD3D_SM4_SCALAR_DIM_MASK             (0x3u << VKD3D_SM4_SCALAR_DIM_SHIFT)

#define VKD3D_SM4_VERSION_MAJOR(version)      (((version) >> 4) & 0xf)
#define VKD3D_SM4_VERSION_MINOR(version)      (((version) >> 0) & 0xf)

#define VKD3D_SM4_ADDRESSING_RELATIVE         0x2
#define VKD3D_SM4_ADDRESSING_OFFSET           0x1

#define VKD3D_SM4_INSTRUCTION_FLAG_SATURATE   0x4

#define VKD3D_SM4_CONDITIONAL_NZ              (0x1u << 18)

#define VKD3D_SM4_TYPE_COMPONENT(com, i)      (((com) >> (4 * (i))) & 0xfu)

enum vkd3d_sm4_opcode
{
    VKD3D_SM4_OP_ADD                              = 0x00,
    VKD3D_SM4_OP_AND                              = 0x01,
    VKD3D_SM4_OP_BREAK                            = 0x02,
    VKD3D_SM4_OP_BREAKC                           = 0x03,
    VKD3D_SM4_OP_CASE                             = 0x06,
    VKD3D_SM4_OP_CONTINUE                         = 0x07,
    VKD3D_SM4_OP_CONTINUEC                        = 0x08,
    VKD3D_SM4_OP_CUT                              = 0x09,
    VKD3D_SM4_OP_DEFAULT                          = 0x0a,
    VKD3D_SM4_OP_DERIV_RTX                        = 0x0b,
    VKD3D_SM4_OP_DERIV_RTY                        = 0x0c,
    VKD3D_SM4_OP_DISCARD                          = 0x0d,
    VKD3D_SM4_OP_DIV                              = 0x0e,
    VKD3D_SM4_OP_DP2                              = 0x0f,
    VKD3D_SM4_OP_DP3                              = 0x10,
    VKD3D_SM4_OP_DP4                              = 0x11,
    VKD3D_SM4_OP_ELSE                             = 0x12,
    VKD3D_SM4_OP_EMIT                             = 0x13,
    VKD3D_SM4_OP_ENDIF                            = 0x15,
    VKD3D_SM4_OP_ENDLOOP                          = 0x16,
    VKD3D_SM4_OP_ENDSWITCH                        = 0x17,
    VKD3D_SM4_OP_EQ                               = 0x18,
    VKD3D_SM4_OP_EXP                              = 0x19,
    VKD3D_SM4_OP_FRC                              = 0x1a,
    VKD3D_SM4_OP_FTOI                             = 0x1b,
    VKD3D_SM4_OP_FTOU                             = 0x1c,
    VKD3D_SM4_OP_GE                               = 0x1d,
    VKD3D_SM4_OP_IADD                             = 0x1e,
    VKD3D_SM4_OP_IF                               = 0x1f,
    VKD3D_SM4_OP_IEQ                              = 0x20,
    VKD3D_SM4_OP_IGE                              = 0x21,
    VKD3D_SM4_OP_ILT                              = 0x22,
    VKD3D_SM4_OP_IMAD                             = 0x23,
    VKD3D_SM4_OP_IMAX                             = 0x24,
    VKD3D_SM4_OP_IMIN                             = 0x25,
    VKD3D_SM4_OP_IMUL                             = 0x26,
    VKD3D_SM4_OP_INE                              = 0x27,
    VKD3D_SM4_OP_INEG                             = 0x28,
    VKD3D_SM4_OP_ISHL                             = 0x29,
    VKD3D_SM4_OP_ISHR                             = 0x2a,
    VKD3D_SM4_OP_ITOF                             = 0x2b,
    VKD3D_SM4_OP_LABEL                            = 0x2c,
    VKD3D_SM4_OP_LD                               = 0x2d,
    VKD3D_SM4_OP_LD2DMS                           = 0x2e,
    VKD3D_SM4_OP_LOG                              = 0x2f,
    VKD3D_SM4_OP_LOOP                             = 0x30,
    VKD3D_SM4_OP_LT                               = 0x31,
    VKD3D_SM4_OP_MAD                              = 0x32,
    VKD3D_SM4_OP_MIN                              = 0x33,
    VKD3D_SM4_OP_MAX                              = 0x34,
    VKD3D_SM4_OP_SHADER_DATA                      = 0x35,
    VKD3D_SM4_OP_MOV                              = 0x36,
    VKD3D_SM4_OP_MOVC                             = 0x37,
    VKD3D_SM4_OP_MUL                              = 0x38,
    VKD3D_SM4_OP_NE                               = 0x39,
    VKD3D_SM4_OP_NOP                              = 0x3a,
    VKD3D_SM4_OP_NOT                              = 0x3b,
    VKD3D_SM4_OP_OR                               = 0x3c,
    VKD3D_SM4_OP_RESINFO                          = 0x3d,
    VKD3D_SM4_OP_RET                              = 0x3e,
    VKD3D_SM4_OP_RETC                             = 0x3f,
    VKD3D_SM4_OP_ROUND_NE                         = 0x40,
    VKD3D_SM4_OP_ROUND_NI                         = 0x41,
    VKD3D_SM4_OP_ROUND_PI                         = 0x42,
    VKD3D_SM4_OP_ROUND_Z                          = 0x43,
    VKD3D_SM4_OP_RSQ                              = 0x44,
    VKD3D_SM4_OP_SAMPLE                           = 0x45,
    VKD3D_SM4_OP_SAMPLE_C                         = 0x46,
    VKD3D_SM4_OP_SAMPLE_C_LZ                      = 0x47,
    VKD3D_SM4_OP_SAMPLE_LOD                       = 0x48,
    VKD3D_SM4_OP_SAMPLE_GRAD                      = 0x49,
    VKD3D_SM4_OP_SAMPLE_B                         = 0x4a,
    VKD3D_SM4_OP_SQRT                             = 0x4b,
    VKD3D_SM4_OP_SWITCH                           = 0x4c,
    VKD3D_SM4_OP_SINCOS                           = 0x4d,
    VKD3D_SM4_OP_UDIV                             = 0x4e,
    VKD3D_SM4_OP_ULT                              = 0x4f,
    VKD3D_SM4_OP_UGE                              = 0x50,
    VKD3D_SM4_OP_UMUL                             = 0x51,
    VKD3D_SM4_OP_UMAX                             = 0x53,
    VKD3D_SM4_OP_UMIN                             = 0x54,
    VKD3D_SM4_OP_USHR                             = 0x55,
    VKD3D_SM4_OP_UTOF                             = 0x56,
    VKD3D_SM4_OP_XOR                              = 0x57,
    VKD3D_SM4_OP_DCL_RESOURCE                     = 0x58,
    VKD3D_SM4_OP_DCL_CONSTANT_BUFFER              = 0x59,
    VKD3D_SM4_OP_DCL_SAMPLER                      = 0x5a,
    VKD3D_SM4_OP_DCL_INDEX_RANGE                  = 0x5b,
    VKD3D_SM4_OP_DCL_OUTPUT_TOPOLOGY              = 0x5c,
    VKD3D_SM4_OP_DCL_INPUT_PRIMITIVE              = 0x5d,
    VKD3D_SM4_OP_DCL_VERTICES_OUT                 = 0x5e,
    VKD3D_SM4_OP_DCL_INPUT                        = 0x5f,
    VKD3D_SM4_OP_DCL_INPUT_SGV                    = 0x60,
    VKD3D_SM4_OP_DCL_INPUT_SIV                    = 0x61,
    VKD3D_SM4_OP_DCL_INPUT_PS                     = 0x62,
    VKD3D_SM4_OP_DCL_INPUT_PS_SGV                 = 0x63,
    VKD3D_SM4_OP_DCL_INPUT_PS_SIV                 = 0x64,
    VKD3D_SM4_OP_DCL_OUTPUT                       = 0x65,
    VKD3D_SM4_OP_DCL_OUTPUT_SGV                   = 0x66,
    VKD3D_SM4_OP_DCL_OUTPUT_SIV                   = 0x67,
    VKD3D_SM4_OP_DCL_TEMPS                        = 0x68,
    VKD3D_SM4_OP_DCL_INDEXABLE_TEMP               = 0x69,
    VKD3D_SM4_OP_DCL_GLOBAL_FLAGS                 = 0x6a,
    VKD3D_SM4_OP_LOD                              = 0x6c,
    VKD3D_SM4_OP_GATHER4                          = 0x6d,
    VKD3D_SM4_OP_SAMPLE_POS                       = 0x6e,
    VKD3D_SM4_OP_SAMPLE_INFO                      = 0x6f,
    VKD3D_SM5_OP_HS_DECLS                         = 0x71,
    VKD3D_SM5_OP_HS_CONTROL_POINT_PHASE           = 0x72,
    VKD3D_SM5_OP_HS_FORK_PHASE                    = 0x73,
    VKD3D_SM5_OP_HS_JOIN_PHASE                    = 0x74,
    VKD3D_SM5_OP_EMIT_STREAM                      = 0x75,
    VKD3D_SM5_OP_CUT_STREAM                       = 0x76,
    VKD3D_SM5_OP_FCALL                            = 0x78,
    VKD3D_SM5_OP_BUFINFO                          = 0x79,
    VKD3D_SM5_OP_DERIV_RTX_COARSE                 = 0x7a,
    VKD3D_SM5_OP_DERIV_RTX_FINE                   = 0x7b,
    VKD3D_SM5_OP_DERIV_RTY_COARSE                 = 0x7c,
    VKD3D_SM5_OP_DERIV_RTY_FINE                   = 0x7d,
    VKD3D_SM5_OP_GATHER4_C                        = 0x7e,
    VKD3D_SM5_OP_GATHER4_PO                       = 0x7f,
    VKD3D_SM5_OP_GATHER4_PO_C                     = 0x80,
    VKD3D_SM5_OP_RCP                              = 0x81,
    VKD3D_SM5_OP_F32TOF16                         = 0x82,
    VKD3D_SM5_OP_F16TOF32                         = 0x83,
    VKD3D_SM5_OP_COUNTBITS                        = 0x86,
    VKD3D_SM5_OP_FIRSTBIT_HI                      = 0x87,
    VKD3D_SM5_OP_FIRSTBIT_LO                      = 0x88,
    VKD3D_SM5_OP_FIRSTBIT_SHI                     = 0x89,
    VKD3D_SM5_OP_UBFE                             = 0x8a,
    VKD3D_SM5_OP_IBFE                             = 0x8b,
    VKD3D_SM5_OP_BFI                              = 0x8c,
    VKD3D_SM5_OP_BFREV                            = 0x8d,
    VKD3D_SM5_OP_SWAPC                            = 0x8e,
    VKD3D_SM5_OP_DCL_STREAM                       = 0x8f,
    VKD3D_SM5_OP_DCL_FUNCTION_BODY                = 0x90,
    VKD3D_SM5_OP_DCL_FUNCTION_TABLE               = 0x91,
    VKD3D_SM5_OP_DCL_INTERFACE                    = 0x92,
    VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT    = 0x93,
    VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT   = 0x94,
    VKD3D_SM5_OP_DCL_TESSELLATOR_DOMAIN           = 0x95,
    VKD3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING     = 0x96,
    VKD3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE = 0x97,
    VKD3D_SM5_OP_DCL_HS_MAX_TESSFACTOR            = 0x98,
    VKD3D_SM5_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT = 0x99,
    VKD3D_SM5_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT = 0x9a,
    VKD3D_SM5_OP_DCL_THREAD_GROUP                 = 0x9b,
    VKD3D_SM5_OP_DCL_UAV_TYPED                    = 0x9c,
    VKD3D_SM5_OP_DCL_UAV_RAW                      = 0x9d,
    VKD3D_SM5_OP_DCL_UAV_STRUCTURED               = 0x9e,
    VKD3D_SM5_OP_DCL_TGSM_RAW                     = 0x9f,
    VKD3D_SM5_OP_DCL_TGSM_STRUCTURED              = 0xa0,
    VKD3D_SM5_OP_DCL_RESOURCE_RAW                 = 0xa1,
    VKD3D_SM5_OP_DCL_RESOURCE_STRUCTURED          = 0xa2,
    VKD3D_SM5_OP_LD_UAV_TYPED                     = 0xa3,
    VKD3D_SM5_OP_STORE_UAV_TYPED                  = 0xa4,
    VKD3D_SM5_OP_LD_RAW                           = 0xa5,
    VKD3D_SM5_OP_STORE_RAW                        = 0xa6,
    VKD3D_SM5_OP_LD_STRUCTURED                    = 0xa7,
    VKD3D_SM5_OP_STORE_STRUCTURED                 = 0xa8,
    VKD3D_SM5_OP_ATOMIC_AND                       = 0xa9,
    VKD3D_SM5_OP_ATOMIC_OR                        = 0xaa,
    VKD3D_SM5_OP_ATOMIC_XOR                       = 0xab,
    VKD3D_SM5_OP_ATOMIC_CMP_STORE                 = 0xac,
    VKD3D_SM5_OP_ATOMIC_IADD                      = 0xad,
    VKD3D_SM5_OP_ATOMIC_IMAX                      = 0xae,
    VKD3D_SM5_OP_ATOMIC_IMIN                      = 0xaf,
    VKD3D_SM5_OP_ATOMIC_UMAX                      = 0xb0,
    VKD3D_SM5_OP_ATOMIC_UMIN                      = 0xb1,
    VKD3D_SM5_OP_IMM_ATOMIC_ALLOC                 = 0xb2,
    VKD3D_SM5_OP_IMM_ATOMIC_CONSUME               = 0xb3,
    VKD3D_SM5_OP_IMM_ATOMIC_IADD                  = 0xb4,
    VKD3D_SM5_OP_IMM_ATOMIC_AND                   = 0xb5,
    VKD3D_SM5_OP_IMM_ATOMIC_OR                    = 0xb6,
    VKD3D_SM5_OP_IMM_ATOMIC_XOR                   = 0xb7,
    VKD3D_SM5_OP_IMM_ATOMIC_EXCH                  = 0xb8,
    VKD3D_SM5_OP_IMM_ATOMIC_CMP_EXCH              = 0xb9,
    VKD3D_SM5_OP_IMM_ATOMIC_IMAX                  = 0xba,
    VKD3D_SM5_OP_IMM_ATOMIC_IMIN                  = 0xbb,
    VKD3D_SM5_OP_IMM_ATOMIC_UMAX                  = 0xbc,
    VKD3D_SM5_OP_IMM_ATOMIC_UMIN                  = 0xbd,
    VKD3D_SM5_OP_SYNC                             = 0xbe,
    VKD3D_SM5_OP_DADD                             = 0xbf,
    VKD3D_SM5_OP_DMAX                             = 0xc0,
    VKD3D_SM5_OP_DMIN                             = 0xc1,
    VKD3D_SM5_OP_DMUL                             = 0xc2,
    VKD3D_SM5_OP_DEQ                              = 0xc3,
    VKD3D_SM5_OP_DGE                              = 0xc4,
    VKD3D_SM5_OP_DLT                              = 0xc5,
    VKD3D_SM5_OP_DNE                              = 0xc6,
    VKD3D_SM5_OP_DMOV                             = 0xc7,
    VKD3D_SM5_OP_DMOVC                            = 0xc8,
    VKD3D_SM5_OP_DTOF                             = 0xc9,
    VKD3D_SM5_OP_FTOD                             = 0xca,
    VKD3D_SM5_OP_EVAL_SAMPLE_INDEX                = 0xcc,
    VKD3D_SM5_OP_EVAL_CENTROID                    = 0xcd,
    VKD3D_SM5_OP_DCL_GS_INSTANCES                 = 0xce,
    VKD3D_SM5_OP_DDIV                             = 0xd2,
    VKD3D_SM5_OP_DFMA                             = 0xd3,
    VKD3D_SM5_OP_DRCP                             = 0xd4,
    VKD3D_SM5_OP_MSAD                             = 0xd5,
    VKD3D_SM5_OP_DTOI                             = 0xd6,
    VKD3D_SM5_OP_DTOU                             = 0xd7,
    VKD3D_SM5_OP_ITOD                             = 0xd8,
    VKD3D_SM5_OP_UTOD                             = 0xd9,
    VKD3D_SM5_OP_GATHER4_S                        = 0xdb,
    VKD3D_SM5_OP_GATHER4_C_S                      = 0xdc,
    VKD3D_SM5_OP_GATHER4_PO_S                     = 0xdd,
    VKD3D_SM5_OP_GATHER4_PO_C_S                   = 0xde,
    VKD3D_SM5_OP_LD_S                             = 0xdf,
    VKD3D_SM5_OP_LD2DMS_S                         = 0xe0,
    VKD3D_SM5_OP_LD_UAV_TYPED_S                   = 0xe1,
    VKD3D_SM5_OP_LD_RAW_S                         = 0xe2,
    VKD3D_SM5_OP_LD_STRUCTURED_S                  = 0xe3,
    VKD3D_SM5_OP_SAMPLE_LOD_S                     = 0xe4,
    VKD3D_SM5_OP_SAMPLE_C_LZ_S                    = 0xe5,
    VKD3D_SM5_OP_SAMPLE_CL_S                      = 0xe6,
    VKD3D_SM5_OP_SAMPLE_B_CL_S                    = 0xe7,
    VKD3D_SM5_OP_SAMPLE_GRAD_CL_S                 = 0xe8,
    VKD3D_SM5_OP_SAMPLE_C_CL_S                    = 0xe9,
    VKD3D_SM5_OP_CHECK_ACCESS_FULLY_MAPPED        = 0xea,

    VKD3D_SM4_OP_COUNT,
};

enum vkd3d_sm4_instruction_modifier
{
    VKD3D_SM4_MODIFIER_AOFFIMMI         = 0x1,
    VKD3D_SM5_MODIFIER_RESOURCE_TYPE    = 0x2,
    VKD3D_SM5_MODIFIER_DATA_TYPE        = 0x3,
};

enum vkd3d_sm4_register_type
{
    VKD3D_SM4_RT_TEMP                    = 0x00,
    VKD3D_SM4_RT_INPUT                   = 0x01,
    VKD3D_SM4_RT_OUTPUT                  = 0x02,
    VKD3D_SM4_RT_INDEXABLE_TEMP          = 0x03,
    VKD3D_SM4_RT_IMMCONST                = 0x04,
    VKD3D_SM4_RT_IMMCONST64              = 0x05,
    VKD3D_SM4_RT_SAMPLER                 = 0x06,
    VKD3D_SM4_RT_RESOURCE                = 0x07,
    VKD3D_SM4_RT_CONSTBUFFER             = 0x08,
    VKD3D_SM4_RT_IMMCONSTBUFFER          = 0x09,
    VKD3D_SM4_RT_PRIMID                  = 0x0b,
    VKD3D_SM4_RT_DEPTHOUT                = 0x0c,
    VKD3D_SM4_RT_NULL                    = 0x0d,
    VKD3D_SM4_RT_RASTERIZER              = 0x0e,
    VKD3D_SM4_RT_OMASK                   = 0x0f,
    VKD3D_SM5_RT_STREAM                  = 0x10,
    VKD3D_SM5_RT_FUNCTION_BODY           = 0x11,
    VKD3D_SM5_RT_FUNCTION_POINTER        = 0x13,
    VKD3D_SM5_RT_OUTPUT_CONTROL_POINT_ID = 0x16,
    VKD3D_SM5_RT_FORK_INSTANCE_ID        = 0x17,
    VKD3D_SM5_RT_JOIN_INSTANCE_ID        = 0x18,
    VKD3D_SM5_RT_INPUT_CONTROL_POINT     = 0x19,
    VKD3D_SM5_RT_OUTPUT_CONTROL_POINT    = 0x1a,
    VKD3D_SM5_RT_PATCH_CONSTANT_DATA     = 0x1b,
    VKD3D_SM5_RT_DOMAIN_LOCATION         = 0x1c,
    VKD3D_SM5_RT_UAV                     = 0x1e,
    VKD3D_SM5_RT_SHARED_MEMORY           = 0x1f,
    VKD3D_SM5_RT_THREAD_ID               = 0x20,
    VKD3D_SM5_RT_THREAD_GROUP_ID         = 0x21,
    VKD3D_SM5_RT_LOCAL_THREAD_ID         = 0x22,
    VKD3D_SM5_RT_COVERAGE                = 0x23,
    VKD3D_SM5_RT_LOCAL_THREAD_INDEX      = 0x24,
    VKD3D_SM5_RT_GS_INSTANCE_ID          = 0x25,
    VKD3D_SM5_RT_DEPTHOUT_GREATER_EQUAL  = 0x26,
    VKD3D_SM5_RT_DEPTHOUT_LESS_EQUAL     = 0x27,
    VKD3D_SM5_RT_OUTPUT_STENCIL_REF      = 0x29,

    VKD3D_SM4_REGISTER_TYPE_COUNT,
};

enum vkd3d_sm4_extended_operand_type
{
    VKD3D_SM4_EXTENDED_OPERAND_NONE      = 0x0,
    VKD3D_SM4_EXTENDED_OPERAND_MODIFIER  = 0x1,
};

enum vkd3d_sm4_register_modifier
{
    VKD3D_SM4_REGISTER_MODIFIER_NONE       = 0x00,
    VKD3D_SM4_REGISTER_MODIFIER_NEGATE     = 0x01,
    VKD3D_SM4_REGISTER_MODIFIER_ABS        = 0x02,
    VKD3D_SM4_REGISTER_MODIFIER_ABS_NEGATE = 0x03,
};

enum vkd3d_sm4_register_precision
{
    VKD3D_SM4_REGISTER_PRECISION_DEFAULT      = 0x0,
    VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_16 = 0x1,
    VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_10 = 0x2,
    VKD3D_SM4_REGISTER_PRECISION_MIN_INT_16   = 0x4,
    VKD3D_SM4_REGISTER_PRECISION_MIN_UINT_16  = 0x5,
};

enum vkd3d_sm4_output_primitive_type
{
    VKD3D_SM4_OUTPUT_PT_POINTLIST     = 0x1,
    VKD3D_SM4_OUTPUT_PT_LINESTRIP     = 0x3,
    VKD3D_SM4_OUTPUT_PT_TRIANGLESTRIP = 0x5,
};

enum vkd3d_sm4_input_primitive_type
{
    VKD3D_SM4_INPUT_PT_POINT          = 0x01,
    VKD3D_SM4_INPUT_PT_LINE           = 0x02,
    VKD3D_SM4_INPUT_PT_TRIANGLE       = 0x03,
    VKD3D_SM4_INPUT_PT_LINEADJ        = 0x06,
    VKD3D_SM4_INPUT_PT_TRIANGLEADJ    = 0x07,
    VKD3D_SM5_INPUT_PT_PATCH1         = 0x08,
    VKD3D_SM5_INPUT_PT_PATCH2         = 0x09,
    VKD3D_SM5_INPUT_PT_PATCH3         = 0x0a,
    VKD3D_SM5_INPUT_PT_PATCH4         = 0x0b,
    VKD3D_SM5_INPUT_PT_PATCH5         = 0x0c,
    VKD3D_SM5_INPUT_PT_PATCH6         = 0x0d,
    VKD3D_SM5_INPUT_PT_PATCH7         = 0x0e,
    VKD3D_SM5_INPUT_PT_PATCH8         = 0x0f,
    VKD3D_SM5_INPUT_PT_PATCH9         = 0x10,
    VKD3D_SM5_INPUT_PT_PATCH10        = 0x11,
    VKD3D_SM5_INPUT_PT_PATCH11        = 0x12,
    VKD3D_SM5_INPUT_PT_PATCH12        = 0x13,
    VKD3D_SM5_INPUT_PT_PATCH13        = 0x14,
    VKD3D_SM5_INPUT_PT_PATCH14        = 0x15,
    VKD3D_SM5_INPUT_PT_PATCH15        = 0x16,
    VKD3D_SM5_INPUT_PT_PATCH16        = 0x17,
    VKD3D_SM5_INPUT_PT_PATCH17        = 0x18,
    VKD3D_SM5_INPUT_PT_PATCH18        = 0x19,
    VKD3D_SM5_INPUT_PT_PATCH19        = 0x1a,
    VKD3D_SM5_INPUT_PT_PATCH20        = 0x1b,
    VKD3D_SM5_INPUT_PT_PATCH21        = 0x1c,
    VKD3D_SM5_INPUT_PT_PATCH22        = 0x1d,
    VKD3D_SM5_INPUT_PT_PATCH23        = 0x1e,
    VKD3D_SM5_INPUT_PT_PATCH24        = 0x1f,
    VKD3D_SM5_INPUT_PT_PATCH25        = 0x20,
    VKD3D_SM5_INPUT_PT_PATCH26        = 0x21,
    VKD3D_SM5_INPUT_PT_PATCH27        = 0x22,
    VKD3D_SM5_INPUT_PT_PATCH28        = 0x23,
    VKD3D_SM5_INPUT_PT_PATCH29        = 0x24,
    VKD3D_SM5_INPUT_PT_PATCH30        = 0x25,
    VKD3D_SM5_INPUT_PT_PATCH31        = 0x26,
    VKD3D_SM5_INPUT_PT_PATCH32        = 0x27,
};

enum vkd3d_sm4_swizzle_type
{
    VKD3D_SM4_SWIZZLE_NONE            = 0x0, /* swizzle bitfield contains a mask */
    VKD3D_SM4_SWIZZLE_VEC4            = 0x1,
    VKD3D_SM4_SWIZZLE_SCALAR          = 0x2,

    VKD3D_SM4_SWIZZLE_DEFAULT         = ~0u - 1,
    VKD3D_SM4_SWIZZLE_INVALID         = ~0u,
};

enum vkd3d_sm4_dimension
{
    VKD3D_SM4_DIMENSION_NONE    = 0x0,
    VKD3D_SM4_DIMENSION_SCALAR  = 0x1,
    VKD3D_SM4_DIMENSION_VEC4    = 0x2,
};

static enum vsir_dimension vsir_dimension_from_sm4_dimension(enum vkd3d_sm4_dimension dim)
{
    switch (dim)
    {
        case VKD3D_SM4_DIMENSION_NONE:
            return VSIR_DIMENSION_NONE;
        case VKD3D_SM4_DIMENSION_SCALAR:
            return VSIR_DIMENSION_SCALAR;
        case VKD3D_SM4_DIMENSION_VEC4:
            return VSIR_DIMENSION_VEC4;
        default:
            FIXME("Unknown SM4 dimension %#x.\n", dim);
            return VSIR_DIMENSION_NONE;
    }
}

static enum vkd3d_sm4_dimension sm4_dimension_from_vsir_dimension(enum vsir_dimension dim)
{
    switch (dim)
    {
        case VSIR_DIMENSION_NONE:
            return VKD3D_SM4_DIMENSION_NONE;
        case VSIR_DIMENSION_SCALAR:
            return VKD3D_SM4_DIMENSION_SCALAR;
        case VSIR_DIMENSION_VEC4:
            return VKD3D_SM4_DIMENSION_VEC4;
        case VSIR_DIMENSION_COUNT:
            vkd3d_unreachable();
    }
    vkd3d_unreachable();
}

enum vkd3d_sm4_resource_type
{
    VKD3D_SM4_RESOURCE_BUFFER             = 0x1,
    VKD3D_SM4_RESOURCE_TEXTURE_1D         = 0x2,
    VKD3D_SM4_RESOURCE_TEXTURE_2D         = 0x3,
    VKD3D_SM4_RESOURCE_TEXTURE_2DMS       = 0x4,
    VKD3D_SM4_RESOURCE_TEXTURE_3D         = 0x5,
    VKD3D_SM4_RESOURCE_TEXTURE_CUBE       = 0x6,
    VKD3D_SM4_RESOURCE_TEXTURE_1DARRAY    = 0x7,
    VKD3D_SM4_RESOURCE_TEXTURE_2DARRAY    = 0x8,
    VKD3D_SM4_RESOURCE_TEXTURE_2DMSARRAY  = 0x9,
    VKD3D_SM4_RESOURCE_TEXTURE_CUBEARRAY  = 0xa,
    VKD3D_SM4_RESOURCE_RAW_BUFFER         = 0xb,
    VKD3D_SM4_RESOURCE_STRUCTURED_BUFFER  = 0xc,
};

enum vkd3d_sm4_data_type
{
    VKD3D_SM4_DATA_UNORM     = 0x1,
    VKD3D_SM4_DATA_SNORM     = 0x2,
    VKD3D_SM4_DATA_INT       = 0x3,
    VKD3D_SM4_DATA_UINT      = 0x4,
    VKD3D_SM4_DATA_FLOAT     = 0x5,
    VKD3D_SM4_DATA_MIXED     = 0x6,
    VKD3D_SM4_DATA_DOUBLE    = 0x7,
    VKD3D_SM4_DATA_CONTINUED = 0x8,
    VKD3D_SM4_DATA_UNUSED    = 0x9,
};

enum vkd3d_sm4_sampler_mode
{
    VKD3D_SM4_SAMPLER_DEFAULT    = 0x0,
    VKD3D_SM4_SAMPLER_COMPARISON = 0x1,
};

enum vkd3d_sm4_shader_data_type
{
    VKD3D_SM4_SHADER_DATA_IMMEDIATE_CONSTANT_BUFFER = 0x3,
    VKD3D_SM4_SHADER_DATA_MESSAGE                   = 0x4,
};

enum vkd3d_sm4_stat_field
{
    VKD3D_STAT_UNUSED = 0,
    VKD3D_STAT_INSTR_COUNT,
    VKD3D_STAT_MOV,
    VKD3D_STAT_MOVC,
    VKD3D_STAT_CONV,
    VKD3D_STAT_FLOAT,
    VKD3D_STAT_INT,
    VKD3D_STAT_UINT,
    VKD3D_STAT_EMIT,
    VKD3D_STAT_CUT,
    VKD3D_STAT_SAMPLE,
    VKD3D_STAT_SAMPLE_C,
    VKD3D_STAT_SAMPLE_GRAD,
    VKD3D_STAT_SAMPLE_BIAS,
    VKD3D_STAT_LOAD,
    VKD3D_STAT_STORE,
    VKD3D_STAT_DCL_VERTICES_OUT,
    VKD3D_STAT_DCL_INPUT_PRIMITIVE,
    VKD3D_STAT_DCL_OUTPUT_TOPOLOGY,
    VKD3D_STAT_DCL_GS_INSTANCES,
    VKD3D_STAT_BITWISE,
    VKD3D_STAT_ATOMIC,
    VKD3D_STAT_TESS_DOMAIN,
    VKD3D_STAT_TESS_PARTITIONING,
    VKD3D_STAT_TESS_OUTPUT_PRIMITIVE,
    VKD3D_STAT_TESS_CONTROL_POINT_COUNT,
    VKD3D_STAT_BARRIER,
    VKD3D_STAT_LOD,
    VKD3D_STAT_GATHER,
    VKD3D_STAT_TEMPS,
    VKD3D_STAT_COUNT,
};

struct vkd3d_sm4_stat_field_info
{
    enum vkd3d_sm4_opcode opcode;
    enum vkd3d_sm4_stat_field field;
};

struct sm4_index_range
{
    unsigned int index;
    unsigned int count;
    unsigned int mask;
};

struct sm4_index_range_array
{
    unsigned int count;
    struct sm4_index_range ranges[MAX_REG_OUTPUT * 2];
};

struct vkd3d_sm4_lookup_tables
{
    const struct vkd3d_sm4_opcode_info *opcode_info_from_sm4[VKD3D_SM4_OP_COUNT];
    const struct vkd3d_sm4_opcode_info *opcode_info_from_vsir[VSIR_OP_COUNT];
    const struct vkd3d_sm4_register_type_info *register_type_info_from_sm4[VKD3D_SM4_REGISTER_TYPE_COUNT];
    const struct vkd3d_sm4_register_type_info *register_type_info_from_vkd3d[VKD3DSPR_COUNT];
    const struct vkd3d_sm4_stat_field_info *stat_field_from_sm4[VKD3D_SM4_OP_COUNT];
};

struct vkd3d_shader_sm4_parser
{
    const uint32_t *start, *end, *ptr;

    enum vkd3d_shader_opcode phase;
    bool has_control_point_phase;
    unsigned int input_register_masks[MAX_REG_OUTPUT];
    unsigned int output_register_masks[MAX_REG_OUTPUT];
    unsigned int patch_constant_register_masks[MAX_REG_OUTPUT];

    struct sm4_index_range_array input_index_ranges;
    struct sm4_index_range_array output_index_ranges;
    struct sm4_index_range_array patch_constant_index_ranges;

    struct vkd3d_sm4_lookup_tables lookup;

    struct vkd3d_shader_parser p;
};

struct vkd3d_sm4_opcode_info
{
    enum vkd3d_sm4_opcode opcode;
    enum vkd3d_shader_opcode handler_idx;
    char dst_info[SM4_MAX_DST_COUNT];
    char src_info[SM4_MAX_SRC_COUNT];
    void (*read_opcode_func)(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
            const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv);
    bool is_conditional_op;
};

static const enum vkd3d_primitive_type output_primitive_type_table[] =
{
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_OUTPUT_PT_POINTLIST */       VKD3D_PT_POINTLIST,
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_OUTPUT_PT_LINESTRIP */       VKD3D_PT_LINESTRIP,
    /* UNKNOWN */                             VKD3D_PT_UNDEFINED,
    /* VKD3D_SM4_OUTPUT_PT_TRIANGLESTRIP */   VKD3D_PT_TRIANGLESTRIP,
};

static const struct
{
    unsigned int control_point_count;
    enum vkd3d_primitive_type vkd3d_type;
}
input_primitive_type_table[] =
{
    [VKD3D_SM4_INPUT_PT_POINT]          = {1, VKD3D_PT_POINTLIST},
    [VKD3D_SM4_INPUT_PT_LINE]           = {2, VKD3D_PT_LINELIST},
    [VKD3D_SM4_INPUT_PT_TRIANGLE]       = {3, VKD3D_PT_TRIANGLELIST},
    [VKD3D_SM4_INPUT_PT_LINEADJ]        = {4, VKD3D_PT_LINELIST_ADJ},
    [VKD3D_SM4_INPUT_PT_TRIANGLEADJ]    = {6, VKD3D_PT_TRIANGLELIST_ADJ},
};

static const enum vkd3d_sm4_input_primitive_type sm4_input_primitive_type_table[] =
{
    [VKD3D_PT_POINTLIST]            = VKD3D_SM4_INPUT_PT_POINT,
    [VKD3D_PT_LINELIST]             = VKD3D_SM4_INPUT_PT_LINE,
    [VKD3D_PT_TRIANGLELIST]         = VKD3D_SM4_INPUT_PT_TRIANGLE,
    [VKD3D_PT_LINELIST_ADJ]         = VKD3D_SM4_INPUT_PT_LINEADJ,
    [VKD3D_PT_TRIANGLELIST_ADJ]     = VKD3D_SM4_INPUT_PT_TRIANGLEADJ,
};

static const enum vkd3d_sm4_output_primitive_type sm4_output_primitive_type_table[] =
{
    [VKD3D_PT_POINTLIST]            = VKD3D_SM4_OUTPUT_PT_POINTLIST,
    [VKD3D_PT_LINESTRIP]            = VKD3D_SM4_OUTPUT_PT_LINESTRIP,
    [VKD3D_PT_TRIANGLESTRIP]        = VKD3D_SM4_OUTPUT_PT_TRIANGLESTRIP,
};

static const enum vkd3d_shader_resource_type resource_type_table[] =
{
    /* 0 */                                       VKD3D_SHADER_RESOURCE_NONE,
    /* VKD3D_SM4_RESOURCE_BUFFER */               VKD3D_SHADER_RESOURCE_BUFFER,
    /* VKD3D_SM4_RESOURCE_TEXTURE_1D */           VKD3D_SHADER_RESOURCE_TEXTURE_1D,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2D */           VKD3D_SHADER_RESOURCE_TEXTURE_2D,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2DMS */         VKD3D_SHADER_RESOURCE_TEXTURE_2DMS,
    /* VKD3D_SM4_RESOURCE_TEXTURE_3D */           VKD3D_SHADER_RESOURCE_TEXTURE_3D,
    /* VKD3D_SM4_RESOURCE_TEXTURE_CUBE */         VKD3D_SHADER_RESOURCE_TEXTURE_CUBE,
    /* VKD3D_SM4_RESOURCE_TEXTURE_1DARRAY */      VKD3D_SHADER_RESOURCE_TEXTURE_1DARRAY,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2DARRAY */      VKD3D_SHADER_RESOURCE_TEXTURE_2DARRAY,
    /* VKD3D_SM4_RESOURCE_TEXTURE_2DMSARRAY */    VKD3D_SHADER_RESOURCE_TEXTURE_2DMSARRAY,
    /* VKD3D_SM4_RESOURCE_TEXTURE_CUBEARRAY */    VKD3D_SHADER_RESOURCE_TEXTURE_CUBEARRAY,
    /* VKD3D_SM4_RESOURCE_RAW_BUFFER */           VKD3D_SHADER_RESOURCE_BUFFER,
    /* VKD3D_SM4_RESOURCE_STRUCTURED_BUFFER */    VKD3D_SHADER_RESOURCE_BUFFER,
};

static const enum vkd3d_data_type data_type_table[] =
{
    /* 0 */                         VKD3D_DATA_FLOAT,
    /* VKD3D_SM4_DATA_UNORM */      VKD3D_DATA_UNORM,
    /* VKD3D_SM4_DATA_SNORM */      VKD3D_DATA_SNORM,
    /* VKD3D_SM4_DATA_INT */        VKD3D_DATA_INT,
    /* VKD3D_SM4_DATA_UINT */       VKD3D_DATA_UINT,
    /* VKD3D_SM4_DATA_FLOAT */      VKD3D_DATA_FLOAT,
    /* VKD3D_SM4_DATA_MIXED */      VKD3D_DATA_MIXED,
    /* VKD3D_SM4_DATA_DOUBLE */     VKD3D_DATA_DOUBLE,
    /* VKD3D_SM4_DATA_CONTINUED */  VKD3D_DATA_CONTINUED,
    /* VKD3D_SM4_DATA_UNUSED */     VKD3D_DATA_UNUSED,
};

static bool shader_is_sm_5_1(const struct vkd3d_shader_sm4_parser *sm4)
{
    const struct vkd3d_shader_version *version = &sm4->p.program->shader_version;

    return version->major >= 5 && version->minor >= 1;
}

static bool shader_sm4_read_src_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_src_param *src_param);
static bool shader_sm4_read_dst_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_dst_param *dst_param);

static bool shader_sm4_read_register_space(struct vkd3d_shader_sm4_parser *priv,
        const uint32_t **ptr, const uint32_t *end, unsigned int *register_space)
{
    *register_space = 0;

    if (!shader_is_sm_5_1(priv))
        return true;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }

    *register_space = *(*ptr)++;
    return true;
}

static void shader_sm4_read_conditional_op(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_src_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_UINT, &ins->src[0]);
    ins->flags = (opcode_token & VKD3D_SM4_CONDITIONAL_NZ) ?
            VKD3D_SHADER_CONDITIONAL_OP_NZ : VKD3D_SHADER_CONDITIONAL_OP_Z;
}

static void shader_sm4_read_case_condition(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_src_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_UINT, &ins->src[0]);
    if (ins->src[0].reg.type != VKD3DSPR_IMMCONST)
    {
        FIXME("Switch case value is not a 32-bit constant.\n");
        vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_CASE_VALUE,
                "Switch case value is not a 32-bit immediate constant register.");
    }
}

static void shader_sm4_read_shader_data(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_immediate_constant_buffer *icb;
    enum vkd3d_sm4_shader_data_type type;
    unsigned int icb_size;

    type = (opcode_token & VKD3D_SM4_SHADER_DATA_TYPE_MASK) >> VKD3D_SM4_SHADER_DATA_TYPE_SHIFT;
    if (type != VKD3D_SM4_SHADER_DATA_IMMEDIATE_CONSTANT_BUFFER)
    {
        FIXME("Ignoring shader data type %#x.\n", type);
        ins->opcode = VSIR_OP_NOP;
        return;
    }

    ++tokens;
    icb_size = token_count - 1;
    if (icb_size % 4)
    {
        FIXME("Unexpected immediate constant buffer size %u.\n", icb_size);
        ins->opcode = VSIR_OP_INVALID;
        return;
    }

    if (!(icb = vkd3d_malloc(offsetof(struct vkd3d_shader_immediate_constant_buffer, data[icb_size]))))
    {
        ERR("Failed to allocate immediate constant buffer, size %u.\n", icb_size);
        vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
        ins->opcode = VSIR_OP_INVALID;
        return;
    }
    icb->register_idx = 0;
    icb->data_type = VKD3D_DATA_FLOAT;
    icb->component_count = VKD3D_VEC4_SIZE;
    icb->element_count = icb_size / VKD3D_VEC4_SIZE;
    icb->is_null = false;
    memcpy(icb->data, tokens, sizeof(*tokens) * icb_size);
    shader_instruction_array_add_icb(&priv->p.program->instructions, icb);
    ins->declaration.icb = icb;
}

static void shader_sm4_set_descriptor_register_range(struct vkd3d_shader_sm4_parser *sm4,
        const struct vkd3d_shader_register *reg, struct vkd3d_shader_register_range *range)
{
    range->first = reg->idx[1].offset;
    range->last = reg->idx[shader_is_sm_5_1(sm4) ? 2 : 1].offset;
    if (range->last < range->first)
    {
        FIXME("Invalid register range [%u:%u].\n", range->first, range->last);
        vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_RANGE,
                "Last register %u must not be less than first register %u in range.", range->last, range->first);
    }
}

static void shader_sm4_read_dcl_resource(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_semantic *semantic = &ins->declaration.semantic;
    enum vkd3d_sm4_resource_type resource_type;
    const uint32_t *end = &tokens[token_count];
    enum vkd3d_sm4_data_type data_type;
    enum vkd3d_data_type reg_data_type;
    uint32_t components;
    unsigned int i;

    resource_type = (opcode_token & VKD3D_SM4_RESOURCE_TYPE_MASK) >> VKD3D_SM4_RESOURCE_TYPE_SHIFT;
    if (!resource_type || (resource_type >= ARRAY_SIZE(resource_type_table)))
    {
        FIXME("Unhandled resource type %#x.\n", resource_type);
        semantic->resource_type = VKD3D_SHADER_RESOURCE_NONE;
    }
    else
    {
        semantic->resource_type = resource_type_table[resource_type];
    }

    if (semantic->resource_type == VKD3D_SHADER_RESOURCE_TEXTURE_2DMS
            || semantic->resource_type == VKD3D_SHADER_RESOURCE_TEXTURE_2DMSARRAY)
    {
        semantic->sample_count = (opcode_token & VKD3D_SM4_RESOURCE_SAMPLE_COUNT_MASK)
                >> VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT;
    }

    reg_data_type = VKD3D_DATA_UNUSED;
    shader_sm4_read_dst_param(priv, &tokens, end, reg_data_type, &semantic->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &semantic->resource.reg.reg, &semantic->resource.range);

    components = *tokens++;
    for (i = 0; i < VKD3D_VEC4_SIZE; i++)
    {
        data_type = VKD3D_SM4_TYPE_COMPONENT(components, i);

        if (!data_type || (data_type >= ARRAY_SIZE(data_type_table)))
        {
            FIXME("Unhandled data type %#x.\n", data_type);
            semantic->resource_data_type[i] = VKD3D_DATA_FLOAT;
        }
        else
        {
            semantic->resource_data_type[i] = data_type_table[data_type];
        }
    }

    if (opcode != VKD3D_SM4_OP_DCL_RESOURCE)
        ins->flags = (opcode_token & VKD3D_SM5_UAV_FLAGS_MASK) >> VKD3D_SM5_UAV_FLAGS_SHIFT;

    shader_sm4_read_register_space(priv, &tokens, end, &semantic->resource.range.space);
}

static void shader_sm4_read_dcl_constant_buffer(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_src_param(priv, &tokens, end, VKD3D_DATA_FLOAT, &ins->declaration.cb.src);
    shader_sm4_set_descriptor_register_range(priv, &ins->declaration.cb.src.reg, &ins->declaration.cb.range);
    if (opcode_token & VKD3D_SM4_INDEX_TYPE_MASK)
        ins->flags |= VKD3DSI_INDEXED_DYNAMIC;

    ins->declaration.cb.size = ins->declaration.cb.src.reg.idx[2].offset;
    ins->declaration.cb.range.space = 0;

    if (shader_is_sm_5_1(priv))
    {
        if (tokens >= end)
        {
            FIXME("Invalid ptr %p >= end %p.\n", tokens, end);
            return;
        }

        ins->declaration.cb.size = *tokens++;
        shader_sm4_read_register_space(priv, &tokens, end, &ins->declaration.cb.range.space);
    }

    ins->declaration.cb.size *= VKD3D_VEC4_SIZE * sizeof(float);
}

static void shader_sm4_read_dcl_sampler(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    const uint32_t *end = &tokens[token_count];

    ins->flags = (opcode_token & VKD3D_SM4_SAMPLER_MODE_MASK) >> VKD3D_SM4_SAMPLER_MODE_SHIFT;
    if (ins->flags & ~VKD3D_SM4_SAMPLER_COMPARISON)
        FIXME("Unhandled sampler mode %#x.\n", ins->flags);
    shader_sm4_read_src_param(priv, &tokens, end, VKD3D_DATA_UNUSED, &ins->declaration.sampler.src);
    shader_sm4_set_descriptor_register_range(priv, &ins->declaration.sampler.src.reg, &ins->declaration.sampler.range);
    shader_sm4_read_register_space(priv, &tokens, end, &ins->declaration.sampler.range.space);
}

static bool sm4_parser_is_in_fork_or_join_phase(const struct vkd3d_shader_sm4_parser *sm4)
{
    return sm4->phase == VSIR_OP_HS_FORK_PHASE || sm4->phase == VSIR_OP_HS_JOIN_PHASE;
}

static void shader_sm4_read_dcl_index_range(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_index_range *index_range = &ins->declaration.index_range;
    struct vsir_program *program = priv->p.program;
    unsigned int i, register_idx, register_count;
    const struct shader_signature *signature;
    enum vkd3d_shader_register_type type;
    struct sm4_index_range_array *ranges;
    unsigned int *io_masks;
    uint32_t write_mask;

    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_OPAQUE,
            &index_range->dst);
    index_range->register_count = *tokens;

    register_idx = index_range->dst.reg.idx[index_range->dst.reg.idx_count - 1].offset;
    register_count = index_range->register_count;
    write_mask = index_range->dst.write_mask;

    switch ((type = index_range->dst.reg.type))
    {
        case VKD3DSPR_INPUT:
        case VKD3DSPR_INCONTROLPOINT:
            io_masks = priv->input_register_masks;
            ranges = &priv->input_index_ranges;
            signature = &program->input_signature;
            break;
        case VKD3DSPR_OUTPUT:
            if (sm4_parser_is_in_fork_or_join_phase(priv))
            {
                io_masks = priv->patch_constant_register_masks;
                ranges = &priv->patch_constant_index_ranges;
                signature = &program->patch_constant_signature;
            }
            else
            {
                io_masks = priv->output_register_masks;
                ranges = &priv->output_index_ranges;
                signature = &program->output_signature;
            }
            break;
        case VKD3DSPR_COLOROUT:
        case VKD3DSPR_OUTCONTROLPOINT:
            io_masks = priv->output_register_masks;
            ranges = &priv->output_index_ranges;
            signature = &program->output_signature;
            break;
        case VKD3DSPR_PATCHCONST:
            io_masks = priv->patch_constant_register_masks;
            ranges = &priv->patch_constant_index_ranges;
            signature = &program->patch_constant_signature;
            break;

        default:
            WARN("Unhandled register type %#x.\n", type);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_INDEX_RANGE_DCL,
                    "Invalid register type %#x for index range base %u, count %u, mask %#x.",
                    type, register_idx, register_count, write_mask);
            return;
    }

    for (i = 0; i < ranges->count; ++i)
    {
        struct sm4_index_range r = ranges->ranges[i];

        if (!(r.mask & write_mask))
            continue;
        /* Ranges with the same base but different lengths are not an issue. */
        if (register_idx == r.index)
            continue;

        if ((r.index <= register_idx && register_idx - r.index < r.count)
                || (register_idx < r.index && r.index - register_idx < register_count))
        {
            WARN("Detected index range collision for base %u, count %u, mask %#x.\n",
                    register_idx, register_count, write_mask);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_INDEX_RANGE_DCL,
                    "Register index range base %u, count %u, mask %#x collides with a previous declaration.",
                    register_idx, register_count, write_mask);
            return;
        }
    }
    ranges->ranges[ranges->count].index = register_idx;
    ranges->ranges[ranges->count].count = register_count;
    ranges->ranges[ranges->count++].mask = write_mask;

    for (i = 0; i < register_count; ++i)
    {
        const struct signature_element *e = vsir_signature_find_element_for_reg(signature, register_idx + i, write_mask);
        /* Index ranges should not contain non-arrayed sysvals. FXC tries to forbid this but it is buggy,
         * and can emit a range containing a sysval if the sysval is not actually accessed. */
        if (e && e->sysval_semantic && register_count > 1 && !vsir_sysval_semantic_is_tess_factor(e->sysval_semantic)
                && !vsir_sysval_semantic_is_clip_cull(e->sysval_semantic))
        {
            WARN("Sysval %u included in an index range declaration.\n", e->sysval_semantic);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_INDEX_RANGE_DCL,
                    "Index range base %u, count %u, mask %#x contains sysval %u.",
                    register_idx, register_count, write_mask, e->sysval_semantic);
            return;
        }
        if ((io_masks[register_idx + i] & write_mask) != write_mask
                && (io_masks[register_idx + i] & write_mask) != 0)
        {
            WARN("No matching declaration for index range base %u, count %u, mask %#x.\n",
                    register_idx, register_count, write_mask);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_INDEX_RANGE_DCL,
                    "Input/output registers matching index range base %u, count %u, mask %#x were not declared.",
                    register_idx, register_count, write_mask);
            return;
        }
    }
}

static void shader_sm4_read_dcl_output_topology(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    enum vkd3d_sm4_output_primitive_type primitive_type;

    primitive_type = (opcode_token & VKD3D_SM4_PRIMITIVE_TYPE_MASK) >> VKD3D_SM4_PRIMITIVE_TYPE_SHIFT;
    if (primitive_type >= ARRAY_SIZE(output_primitive_type_table))
        ins->declaration.primitive_type.type = VKD3D_PT_UNDEFINED;
    else
        ins->declaration.primitive_type.type = output_primitive_type_table[primitive_type];

    if (ins->declaration.primitive_type.type == VKD3D_PT_UNDEFINED)
        FIXME("Unhandled output primitive type %#x.\n", primitive_type);

    priv->p.program->output_topology = ins->declaration.primitive_type.type;
}

static void shader_sm4_read_dcl_input_primitive(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *sm4)
{
    enum vkd3d_sm4_input_primitive_type primitive_type;
    struct vsir_program *program = sm4->p.program;

    primitive_type = (opcode_token & VKD3D_SM4_PRIMITIVE_TYPE_MASK) >> VKD3D_SM4_PRIMITIVE_TYPE_SHIFT;
    if (VKD3D_SM5_INPUT_PT_PATCH1 <= primitive_type && primitive_type <= VKD3D_SM5_INPUT_PT_PATCH32)
    {
        ins->declaration.primitive_type.type = VKD3D_PT_PATCH;
        ins->declaration.primitive_type.patch_vertex_count = primitive_type - VKD3D_SM5_INPUT_PT_PATCH1 + 1;
        program->input_control_point_count = ins->declaration.primitive_type.patch_vertex_count;
    }
    else if (primitive_type >= ARRAY_SIZE(input_primitive_type_table))
    {
        ins->declaration.primitive_type.type = VKD3D_PT_UNDEFINED;
    }
    else
    {
        ins->declaration.primitive_type.type = input_primitive_type_table[primitive_type].vkd3d_type;
        program->input_control_point_count = input_primitive_type_table[primitive_type].control_point_count;
    }

    if (ins->declaration.primitive_type.type == VKD3D_PT_UNDEFINED)
        FIXME("Unhandled input primitive type %#x.\n", primitive_type);

    program->input_primitive = ins->declaration.primitive_type.type;
}

static void shader_sm4_read_declaration_count(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *sm4)
{
    struct vsir_program *program = sm4->p.program;

    ins->declaration.count = *tokens;
    if (opcode == VKD3D_SM4_OP_DCL_TEMPS)
        program->temp_count = max(program->temp_count, *tokens);
    else if (opcode == VKD3D_SM4_OP_DCL_VERTICES_OUT)
        program->vertices_out_count = *tokens;
}

static void shader_sm4_read_declaration_dst(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT, &ins->declaration.dst);
}

static void shader_sm4_read_declaration_register_semantic(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT,
            &ins->declaration.register_semantic.reg);
    ins->declaration.register_semantic.sysval_semantic = *tokens;
}

static void shader_sm4_read_dcl_input_ps(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_dst_param *dst = &ins->declaration.dst;

    ins->flags = (opcode_token & VKD3D_SM4_INTERPOLATION_MODE_MASK) >> VKD3D_SM4_INTERPOLATION_MODE_SHIFT;
    if (shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT, dst))
    {
        struct signature_element *e = vsir_signature_find_element_for_reg(
                &priv->p.program->input_signature, dst->reg.idx[dst->reg.idx_count - 1].offset, dst->write_mask);

        if (!e)
        {
            WARN("No matching signature element for input register %u with mask %#x.\n",
                    dst->reg.idx[dst->reg.idx_count - 1].offset, dst->write_mask);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_DCL,
                    "No matching signature element for input register %u with mask %#x.",
                    dst->reg.idx[dst->reg.idx_count - 1].offset, dst->write_mask);
        }
        else
        {
            e->interpolation_mode = ins->flags;
        }
    }
}

static void shader_sm4_read_dcl_input_ps_siv(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_dst_param *dst = &ins->declaration.register_semantic.reg;

    ins->flags = (opcode_token & VKD3D_SM4_INTERPOLATION_MODE_MASK) >> VKD3D_SM4_INTERPOLATION_MODE_SHIFT;
    if (shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT, dst))
    {
        struct signature_element *e = vsir_signature_find_element_for_reg(
                &priv->p.program->input_signature, dst->reg.idx[dst->reg.idx_count - 1].offset, dst->write_mask);

        if (!e)
        {
            WARN("No matching signature element for input register %u with mask %#x.\n",
                    dst->reg.idx[dst->reg.idx_count - 1].offset, dst->write_mask);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_DCL,
                    "No matching signature element for input register %u with mask %#x.",
                    dst->reg.idx[dst->reg.idx_count - 1].offset, dst->write_mask);
        }
        else
        {
            e->interpolation_mode = ins->flags;
        }
    }
    ins->declaration.register_semantic.sysval_semantic = *tokens;
}

static void shader_sm4_read_dcl_indexable_temp(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.indexable_temp.register_idx = *tokens++;
    ins->declaration.indexable_temp.register_size = *tokens++;
    ins->declaration.indexable_temp.alignment = 0;
    ins->declaration.indexable_temp.data_type = VKD3D_DATA_FLOAT;
    ins->declaration.indexable_temp.component_count = *tokens;
    ins->declaration.indexable_temp.has_function_scope = false;
}

static void shader_sm4_read_dcl_global_flags(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *sm4)
{
    ins->declaration.global_flags = (opcode_token & VKD3D_SM4_GLOBAL_FLAGS_MASK) >> VKD3D_SM4_GLOBAL_FLAGS_SHIFT;
    sm4->p.program->global_flags = ins->declaration.global_flags;
}

static void shader_sm5_read_fcall(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->src[0].reg.u.fp_body_idx = *tokens++;
    shader_sm4_read_src_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_OPAQUE, &ins->src[0]);
}

static void shader_sm5_read_dcl_function_body(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.index = *tokens;
}

static void shader_sm5_read_dcl_function_table(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.index = *tokens++;
    FIXME("Ignoring set of function bodies (count %u).\n", *tokens);
}

static void shader_sm5_read_dcl_interface(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.fp.index = *tokens++;
    ins->declaration.fp.body_count = *tokens++;
    ins->declaration.fp.array_size = *tokens >> VKD3D_SM5_FP_ARRAY_SIZE_SHIFT;
    ins->declaration.fp.table_count = *tokens++ & VKD3D_SM5_FP_TABLE_COUNT_MASK;
    FIXME("Ignoring set of function tables (count %u).\n", ins->declaration.fp.table_count);
}

static void shader_sm5_read_control_point_count(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *sm4)
{
    struct vsir_program *program = sm4->p.program;

    ins->declaration.count = (opcode_token & VKD3D_SM5_CONTROL_POINT_COUNT_MASK)
            >> VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT;

    if (opcode == VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT)
        program->input_control_point_count = ins->declaration.count;
    else
        program->output_control_point_count = ins->declaration.count;
}

static void shader_sm5_read_dcl_tessellator_domain(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.tessellator_domain = (opcode_token & VKD3D_SM5_TESSELLATOR_MASK)
            >> VKD3D_SM5_TESSELLATOR_SHIFT;
    priv->p.program->tess_domain = ins->declaration.tessellator_domain;
}

static void shader_sm5_read_dcl_tessellator_partitioning(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.tessellator_partitioning = (opcode_token & VKD3D_SM5_TESSELLATOR_MASK)
            >> VKD3D_SM5_TESSELLATOR_SHIFT;
    priv->p.program->tess_partitioning = ins->declaration.tessellator_partitioning;
}

static void shader_sm5_read_dcl_tessellator_output_primitive(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.tessellator_output_primitive = (opcode_token & VKD3D_SM5_TESSELLATOR_MASK)
            >> VKD3D_SM5_TESSELLATOR_SHIFT;
    priv->p.program->tess_output_primitive = ins->declaration.tessellator_output_primitive;
}

static void shader_sm5_read_dcl_hs_max_tessfactor(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->declaration.max_tessellation_factor = *(float *)tokens;
}

static void shader_sm5_read_dcl_thread_group(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *sm4)
{
    struct vsir_program *program = sm4->p.program;

    ins->declaration.thread_group_size.x = *tokens++;
    ins->declaration.thread_group_size.y = *tokens++;
    ins->declaration.thread_group_size.z = *tokens++;
    program->thread_group_size = ins->declaration.thread_group_size;
}

static void shader_sm5_read_dcl_uav_raw(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_raw_resource *resource = &ins->declaration.raw_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_UNUSED, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    ins->flags = (opcode_token & VKD3D_SM5_UAV_FLAGS_MASK) >> VKD3D_SM5_UAV_FLAGS_SHIFT;
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_dcl_uav_structured(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_structured_resource *resource = &ins->declaration.structured_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_UNUSED, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    ins->flags = (opcode_token & VKD3D_SM5_UAV_FLAGS_MASK) >> VKD3D_SM5_UAV_FLAGS_SHIFT;
    resource->byte_stride = *tokens++;
    if (resource->byte_stride % 4)
        FIXME("Byte stride %u is not multiple of 4.\n", resource->byte_stride);
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_dcl_tgsm_raw(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT, &ins->declaration.tgsm_raw.reg);
    ins->declaration.tgsm_raw.byte_count = *tokens;
    if (ins->declaration.tgsm_raw.byte_count % 4)
        FIXME("Byte count %u is not multiple of 4.\n", ins->declaration.tgsm_raw.byte_count);
    ins->declaration.tgsm_raw.zero_init = false;
}

static void shader_sm5_read_dcl_tgsm_structured(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, &tokens[token_count], VKD3D_DATA_FLOAT,
            &ins->declaration.tgsm_structured.reg);
    ins->declaration.tgsm_structured.byte_stride = *tokens++;
    ins->declaration.tgsm_structured.structure_count = *tokens;
    if (ins->declaration.tgsm_structured.byte_stride % 4)
        FIXME("Byte stride %u is not multiple of 4.\n", ins->declaration.tgsm_structured.byte_stride);
    ins->declaration.tgsm_structured.zero_init = false;
}

static void shader_sm5_read_dcl_resource_structured(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_structured_resource *resource = &ins->declaration.structured_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_UNUSED, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    resource->byte_stride = *tokens++;
    if (resource->byte_stride % 4)
        FIXME("Byte stride %u is not multiple of 4.\n", resource->byte_stride);
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_dcl_resource_raw(struct vkd3d_shader_instruction *ins, uint32_t opcode,
        uint32_t opcode_token, const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    struct vkd3d_shader_raw_resource *resource = &ins->declaration.raw_resource;
    const uint32_t *end = &tokens[token_count];

    shader_sm4_read_dst_param(priv, &tokens, end, VKD3D_DATA_UNUSED, &resource->resource.reg);
    shader_sm4_set_descriptor_register_range(priv, &resource->resource.reg.reg, &resource->resource.range);
    shader_sm4_read_register_space(priv, &tokens, end, &resource->resource.range.space);
}

static void shader_sm5_read_sync(struct vkd3d_shader_instruction *ins, uint32_t opcode, uint32_t opcode_token,
        const uint32_t *tokens, unsigned int token_count, struct vkd3d_shader_sm4_parser *priv)
{
    ins->flags = (opcode_token & VKD3D_SM5_SYNC_FLAGS_MASK) >> VKD3D_SM5_SYNC_FLAGS_SHIFT;
}

struct vkd3d_sm4_register_type_info
{
    enum vkd3d_sm4_register_type sm4_type;
    enum vkd3d_shader_register_type vkd3d_type;

    /* Swizzle type to be used for src registers when their dimension is VKD3D_SM4_DIMENSION_VEC4. */
    enum vkd3d_sm4_swizzle_type default_src_swizzle_type;
};

static const enum vkd3d_shader_register_precision register_precision_table[] =
{
    /* VKD3D_SM4_REGISTER_PRECISION_DEFAULT */      VKD3D_SHADER_REGISTER_PRECISION_DEFAULT,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_16 */ VKD3D_SHADER_REGISTER_PRECISION_MIN_FLOAT_16,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_FLOAT_10 */ VKD3D_SHADER_REGISTER_PRECISION_MIN_FLOAT_10,
    /* UNKNOWN */                                   VKD3D_SHADER_REGISTER_PRECISION_INVALID,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_INT_16 */   VKD3D_SHADER_REGISTER_PRECISION_MIN_INT_16,
    /* VKD3D_SM4_REGISTER_PRECISION_MIN_UINT_16 */  VKD3D_SHADER_REGISTER_PRECISION_MIN_UINT_16,
};

struct sm4_stat
{
    uint32_t fields[VKD3D_STAT_COUNT];
};

struct tpf_compiler
{
    struct vsir_program *program;
    struct vkd3d_sm4_lookup_tables lookup;
    struct sm4_stat *stat;

    int result;

    struct vkd3d_bytecode_buffer *buffer;
    struct dxbc_writer dxbc;
};

static void init_sm4_lookup_tables(struct vkd3d_sm4_lookup_tables *lookup)
{
    unsigned int i;

    /*
     * d -> VKD3D_DATA_DOUBLE
     * f -> VKD3D_DATA_FLOAT
     * i -> VKD3D_DATA_INT
     * u -> VKD3D_DATA_UINT
     * O -> VKD3D_DATA_OPAQUE
     * R -> VKD3D_DATA_RESOURCE
     * S -> VKD3D_DATA_SAMPLER
     * U -> VKD3D_DATA_UAV
     */
    static const struct vkd3d_sm4_opcode_info opcode_table[] =
    {
        {VKD3D_SM4_OP_ADD,                              VSIR_OP_ADD,                              "f",    "ff"},
        {VKD3D_SM4_OP_AND,                              VSIR_OP_AND,                              "u",    "uu"},
        {VKD3D_SM4_OP_BREAK,                            VSIR_OP_BREAK,                            "",     ""},
        {VKD3D_SM4_OP_BREAKC,                           VSIR_OP_BREAKP,                           "",     "u",
                shader_sm4_read_conditional_op, true},
        {VKD3D_SM4_OP_CASE,                             VSIR_OP_CASE,                             "",     "u",
                shader_sm4_read_case_condition},
        {VKD3D_SM4_OP_CONTINUE,                         VSIR_OP_CONTINUE,                         "",     ""},
        {VKD3D_SM4_OP_CONTINUEC,                        VSIR_OP_CONTINUEP,                        "",     "u",
                shader_sm4_read_conditional_op, true},
        {VKD3D_SM4_OP_CUT,                              VSIR_OP_CUT,                              "",     ""},
        {VKD3D_SM4_OP_DEFAULT,                          VSIR_OP_DEFAULT,                          "",     ""},
        {VKD3D_SM4_OP_DERIV_RTX,                        VSIR_OP_DSX,                              "f",    "f"},
        {VKD3D_SM4_OP_DERIV_RTY,                        VSIR_OP_DSY,                              "f",    "f"},
        {VKD3D_SM4_OP_DISCARD,                          VSIR_OP_DISCARD,                          "",     "u",
                shader_sm4_read_conditional_op, true},
        {VKD3D_SM4_OP_DIV,                              VSIR_OP_DIV,                              "f",    "ff"},
        {VKD3D_SM4_OP_DP2,                              VSIR_OP_DP2,                              "f",    "ff"},
        {VKD3D_SM4_OP_DP3,                              VSIR_OP_DP3,                              "f",    "ff"},
        {VKD3D_SM4_OP_DP4,                              VSIR_OP_DP4,                              "f",    "ff"},
        {VKD3D_SM4_OP_ELSE,                             VSIR_OP_ELSE,                             "",     ""},
        {VKD3D_SM4_OP_EMIT,                             VSIR_OP_EMIT,                             "",     ""},
        {VKD3D_SM4_OP_ENDIF,                            VSIR_OP_ENDIF,                            "",     ""},
        {VKD3D_SM4_OP_ENDLOOP,                          VSIR_OP_ENDLOOP,                          "",     ""},
        {VKD3D_SM4_OP_ENDSWITCH,                        VSIR_OP_ENDSWITCH,                        "",     ""},
        {VKD3D_SM4_OP_EQ,                               VSIR_OP_EQO,                              "u",    "ff"},
        {VKD3D_SM4_OP_EXP,                              VSIR_OP_EXP,                              "f",    "f"},
        {VKD3D_SM4_OP_FRC,                              VSIR_OP_FRC,                              "f",    "f"},
        {VKD3D_SM4_OP_FTOI,                             VSIR_OP_FTOI,                             "i",    "f"},
        {VKD3D_SM4_OP_FTOU,                             VSIR_OP_FTOU,                             "u",    "f"},
        {VKD3D_SM4_OP_GE,                               VSIR_OP_GEO,                              "u",    "ff"},
        {VKD3D_SM4_OP_IADD,                             VSIR_OP_IADD,                             "i",    "ii"},
        {VKD3D_SM4_OP_IF,                               VSIR_OP_IF,                               "",     "u",
                shader_sm4_read_conditional_op, true},
        {VKD3D_SM4_OP_IEQ,                              VSIR_OP_IEQ,                              "u",    "ii"},
        {VKD3D_SM4_OP_IGE,                              VSIR_OP_IGE,                              "u",    "ii"},
        {VKD3D_SM4_OP_ILT,                              VSIR_OP_ILT,                              "u",    "ii"},
        {VKD3D_SM4_OP_IMAD,                             VSIR_OP_IMAD,                             "i",    "iii"},
        {VKD3D_SM4_OP_IMAX,                             VSIR_OP_IMAX,                             "i",    "ii"},
        {VKD3D_SM4_OP_IMIN,                             VSIR_OP_IMIN,                             "i",    "ii"},
        {VKD3D_SM4_OP_IMUL,                             VSIR_OP_IMUL,                             "ii",   "ii"},
        {VKD3D_SM4_OP_INE,                              VSIR_OP_INE,                              "u",    "ii"},
        {VKD3D_SM4_OP_INEG,                             VSIR_OP_INEG,                             "i",    "i"},
        {VKD3D_SM4_OP_ISHL,                             VSIR_OP_ISHL,                             "i",    "ii"},
        {VKD3D_SM4_OP_ISHR,                             VSIR_OP_ISHR,                             "i",    "ii"},
        {VKD3D_SM4_OP_ITOF,                             VSIR_OP_ITOF,                             "f",    "i"},
        {VKD3D_SM4_OP_LABEL,                            VSIR_OP_LABEL,                            "",     "O"},
        {VKD3D_SM4_OP_LD,                               VSIR_OP_LD,                               "u",    "i*"},
        {VKD3D_SM4_OP_LD2DMS,                           VSIR_OP_LD2DMS,                           "u",    "i*i"},
        {VKD3D_SM4_OP_LOG,                              VSIR_OP_LOG,                              "f",    "f"},
        {VKD3D_SM4_OP_LOOP,                             VSIR_OP_LOOP,                             "",     ""},
        {VKD3D_SM4_OP_LT,                               VSIR_OP_LTO,                              "u",    "ff"},
        {VKD3D_SM4_OP_MAD,                              VSIR_OP_MAD,                              "f",    "fff"},
        {VKD3D_SM4_OP_MIN,                              VSIR_OP_MIN,                              "f",    "ff"},
        {VKD3D_SM4_OP_MAX,                              VSIR_OP_MAX,                              "f",    "ff"},
        {VKD3D_SM4_OP_SHADER_DATA,                      VSIR_OP_DCL_IMMEDIATE_CONSTANT_BUFFER,    "",     "",
                shader_sm4_read_shader_data},
        {VKD3D_SM4_OP_MOV,                              VSIR_OP_MOV,                              "f",    "f"},
        {VKD3D_SM4_OP_MOVC,                             VSIR_OP_MOVC,                             "f",    "uff"},
        {VKD3D_SM4_OP_MUL,                              VSIR_OP_MUL,                              "f",    "ff"},
        {VKD3D_SM4_OP_NE,                               VSIR_OP_NEU,                              "u",    "ff"},
        {VKD3D_SM4_OP_NOP,                              VSIR_OP_NOP,                              "",     ""},
        {VKD3D_SM4_OP_NOT,                              VSIR_OP_NOT,                              "u",    "u"},
        {VKD3D_SM4_OP_OR,                               VSIR_OP_OR,                               "u",    "uu"},
        {VKD3D_SM4_OP_RESINFO,                          VSIR_OP_RESINFO,                          "f",    "i*"},
        {VKD3D_SM4_OP_RET,                              VSIR_OP_RET,                              "",     ""},
        {VKD3D_SM4_OP_RETC,                             VSIR_OP_RETP,                             "",     "u",
                shader_sm4_read_conditional_op, true},
        {VKD3D_SM4_OP_ROUND_NE,                         VSIR_OP_ROUND_NE,                         "f",    "f"},
        {VKD3D_SM4_OP_ROUND_NI,                         VSIR_OP_ROUND_NI,                         "f",    "f"},
        {VKD3D_SM4_OP_ROUND_PI,                         VSIR_OP_ROUND_PI,                         "f",    "f"},
        {VKD3D_SM4_OP_ROUND_Z,                          VSIR_OP_ROUND_Z,                          "f",    "f"},
        {VKD3D_SM4_OP_RSQ,                              VSIR_OP_RSQ,                              "f",    "f"},
        {VKD3D_SM4_OP_SAMPLE,                           VSIR_OP_SAMPLE,                           "u",    "f**"},
        {VKD3D_SM4_OP_SAMPLE_C,                         VSIR_OP_SAMPLE_C,                         "f",    "f**f"},
        {VKD3D_SM4_OP_SAMPLE_C_LZ,                      VSIR_OP_SAMPLE_C_LZ,                      "f",    "f**f"},
        {VKD3D_SM4_OP_SAMPLE_LOD,                       VSIR_OP_SAMPLE_LOD,                       "u",    "f**f"},
        {VKD3D_SM4_OP_SAMPLE_GRAD,                      VSIR_OP_SAMPLE_GRAD,                      "u",    "f**ff"},
        {VKD3D_SM4_OP_SAMPLE_B,                         VSIR_OP_SAMPLE_B,                         "u",    "f**f"},
        {VKD3D_SM4_OP_SQRT,                             VSIR_OP_SQRT,                             "f",    "f"},
        {VKD3D_SM4_OP_SWITCH,                           VSIR_OP_SWITCH,                           "",     "i"},
        {VKD3D_SM4_OP_SINCOS,                           VSIR_OP_SINCOS,                           "ff",   "f"},
        {VKD3D_SM4_OP_UDIV,                             VSIR_OP_UDIV,                             "uu",   "uu"},
        {VKD3D_SM4_OP_ULT,                              VSIR_OP_ULT,                              "u",    "uu"},
        {VKD3D_SM4_OP_UGE,                              VSIR_OP_UGE,                              "u",    "uu"},
        {VKD3D_SM4_OP_UMUL,                             VSIR_OP_UMUL,                             "uu",   "uu"},
        {VKD3D_SM4_OP_UMAX,                             VSIR_OP_UMAX,                             "u",    "uu"},
        {VKD3D_SM4_OP_UMIN,                             VSIR_OP_UMIN,                             "u",    "uu"},
        {VKD3D_SM4_OP_USHR,                             VSIR_OP_USHR,                             "u",    "uu"},
        {VKD3D_SM4_OP_UTOF,                             VSIR_OP_UTOF,                             "f",    "u"},
        {VKD3D_SM4_OP_XOR,                              VSIR_OP_XOR,                              "u",    "uu"},
        {VKD3D_SM4_OP_DCL_RESOURCE,                     VSIR_OP_DCL,                              "",     "",
                shader_sm4_read_dcl_resource},
        {VKD3D_SM4_OP_DCL_CONSTANT_BUFFER,              VSIR_OP_DCL_CONSTANT_BUFFER,              "",     "",
                shader_sm4_read_dcl_constant_buffer},
        {VKD3D_SM4_OP_DCL_SAMPLER,                      VSIR_OP_DCL_SAMPLER,                      "",     "",
                shader_sm4_read_dcl_sampler},
        {VKD3D_SM4_OP_DCL_INDEX_RANGE,                  VSIR_OP_DCL_INDEX_RANGE,                  "",     "",
                shader_sm4_read_dcl_index_range},
        {VKD3D_SM4_OP_DCL_OUTPUT_TOPOLOGY,              VSIR_OP_DCL_OUTPUT_TOPOLOGY,              "",     "",
                shader_sm4_read_dcl_output_topology},
        {VKD3D_SM4_OP_DCL_INPUT_PRIMITIVE,              VSIR_OP_DCL_INPUT_PRIMITIVE,              "",     "",
                shader_sm4_read_dcl_input_primitive},
        {VKD3D_SM4_OP_DCL_VERTICES_OUT,                 VSIR_OP_DCL_VERTICES_OUT,                 "",     "",
                shader_sm4_read_declaration_count},
        {VKD3D_SM4_OP_DCL_INPUT,                        VSIR_OP_DCL_INPUT,                        "",     "",
                shader_sm4_read_declaration_dst},
        {VKD3D_SM4_OP_DCL_INPUT_SGV,                    VSIR_OP_DCL_INPUT_SGV,                    "",     "",
                shader_sm4_read_declaration_register_semantic},
        {VKD3D_SM4_OP_DCL_INPUT_SIV,                    VSIR_OP_DCL_INPUT_SIV,                    "",     "",
                shader_sm4_read_declaration_register_semantic},
        {VKD3D_SM4_OP_DCL_INPUT_PS,                     VSIR_OP_DCL_INPUT_PS,                     "",     "",
                shader_sm4_read_dcl_input_ps},
        {VKD3D_SM4_OP_DCL_INPUT_PS_SGV,                 VSIR_OP_DCL_INPUT_PS_SGV,                 "",     "",
                shader_sm4_read_declaration_register_semantic},
        {VKD3D_SM4_OP_DCL_INPUT_PS_SIV,                 VSIR_OP_DCL_INPUT_PS_SIV,                 "",     "",
                shader_sm4_read_dcl_input_ps_siv},
        {VKD3D_SM4_OP_DCL_OUTPUT,                       VSIR_OP_DCL_OUTPUT,                       "",     "",
                shader_sm4_read_declaration_dst},
        {VKD3D_SM4_OP_DCL_OUTPUT_SGV,                   VSIR_OP_DCL_OUTPUT_SGV,                   "",     "",
                shader_sm4_read_declaration_register_semantic},
        {VKD3D_SM4_OP_DCL_OUTPUT_SIV,                   VSIR_OP_DCL_OUTPUT_SIV,                   "",     "",
                shader_sm4_read_declaration_register_semantic},
        {VKD3D_SM4_OP_DCL_TEMPS,                        VSIR_OP_DCL_TEMPS,                        "",     "",
                shader_sm4_read_declaration_count},
        {VKD3D_SM4_OP_DCL_INDEXABLE_TEMP,               VSIR_OP_DCL_INDEXABLE_TEMP,               "",     "",
                shader_sm4_read_dcl_indexable_temp},
        {VKD3D_SM4_OP_DCL_GLOBAL_FLAGS,                 VSIR_OP_DCL_GLOBAL_FLAGS,                 "",     "",
                shader_sm4_read_dcl_global_flags},
        {VKD3D_SM4_OP_LOD,                              VSIR_OP_LOD,                              "f",    "f**"},
        {VKD3D_SM4_OP_GATHER4,                          VSIR_OP_GATHER4,                          "u",    "f**"},
        {VKD3D_SM4_OP_SAMPLE_POS,                       VSIR_OP_SAMPLE_POS,                       "f",    "*u"},
        {VKD3D_SM4_OP_SAMPLE_INFO,                      VSIR_OP_SAMPLE_INFO,                      "f",    "*"},
        {VKD3D_SM5_OP_HS_DECLS,                         VSIR_OP_HS_DECLS,                         "",     ""},
        {VKD3D_SM5_OP_HS_CONTROL_POINT_PHASE,           VSIR_OP_HS_CONTROL_POINT_PHASE,           "",     ""},
        {VKD3D_SM5_OP_HS_FORK_PHASE,                    VSIR_OP_HS_FORK_PHASE,                    "",     ""},
        {VKD3D_SM5_OP_HS_JOIN_PHASE,                    VSIR_OP_HS_JOIN_PHASE,                    "",     ""},
        {VKD3D_SM5_OP_EMIT_STREAM,                      VSIR_OP_EMIT_STREAM,                      "",     "f"},
        {VKD3D_SM5_OP_CUT_STREAM,                       VSIR_OP_CUT_STREAM,                       "",     "f"},
        {VKD3D_SM5_OP_FCALL,                            VSIR_OP_FCALL,                            "",     "O",
                shader_sm5_read_fcall},
        {VKD3D_SM5_OP_BUFINFO,                          VSIR_OP_BUFINFO,                          "i",    "*"},
        {VKD3D_SM5_OP_DERIV_RTX_COARSE,                 VSIR_OP_DSX_COARSE,                       "f",    "f"},
        {VKD3D_SM5_OP_DERIV_RTX_FINE,                   VSIR_OP_DSX_FINE,                         "f",    "f"},
        {VKD3D_SM5_OP_DERIV_RTY_COARSE,                 VSIR_OP_DSY_COARSE,                       "f",    "f"},
        {VKD3D_SM5_OP_DERIV_RTY_FINE,                   VSIR_OP_DSY_FINE,                         "f",    "f"},
        {VKD3D_SM5_OP_GATHER4_C,                        VSIR_OP_GATHER4_C,                        "f",    "f**f"},
        {VKD3D_SM5_OP_GATHER4_PO,                       VSIR_OP_GATHER4_PO,                       "f",    "fi**"},
        {VKD3D_SM5_OP_GATHER4_PO_C,                     VSIR_OP_GATHER4_PO_C,                     "f",    "fi**f"},
        {VKD3D_SM5_OP_RCP,                              VSIR_OP_RCP,                              "f",    "f"},
        {VKD3D_SM5_OP_F32TOF16,                         VSIR_OP_F32TOF16,                         "u",    "f"},
        {VKD3D_SM5_OP_F16TOF32,                         VSIR_OP_F16TOF32,                         "f",    "u"},
        {VKD3D_SM5_OP_COUNTBITS,                        VSIR_OP_COUNTBITS,                        "u",    "u"},
        {VKD3D_SM5_OP_FIRSTBIT_HI,                      VSIR_OP_FIRSTBIT_HI,                      "u",    "u"},
        {VKD3D_SM5_OP_FIRSTBIT_LO,                      VSIR_OP_FIRSTBIT_LO,                      "u",    "u"},
        {VKD3D_SM5_OP_FIRSTBIT_SHI,                     VSIR_OP_FIRSTBIT_SHI,                     "u",    "i"},
        {VKD3D_SM5_OP_UBFE,                             VSIR_OP_UBFE,                             "u",    "iiu"},
        {VKD3D_SM5_OP_IBFE,                             VSIR_OP_IBFE,                             "i",    "iii"},
        {VKD3D_SM5_OP_BFI,                              VSIR_OP_BFI,                              "u",    "iiuu"},
        {VKD3D_SM5_OP_BFREV,                            VSIR_OP_BFREV,                            "u",    "u"},
        {VKD3D_SM5_OP_SWAPC,                            VSIR_OP_SWAPC,                            "ff",   "uff"},
        {VKD3D_SM5_OP_DCL_STREAM,                       VSIR_OP_DCL_STREAM,                       "",     "O"},
        {VKD3D_SM5_OP_DCL_FUNCTION_BODY,                VSIR_OP_DCL_FUNCTION_BODY,                "",     "",
                shader_sm5_read_dcl_function_body},
        {VKD3D_SM5_OP_DCL_FUNCTION_TABLE,               VSIR_OP_DCL_FUNCTION_TABLE,               "",     "",
                shader_sm5_read_dcl_function_table},
        {VKD3D_SM5_OP_DCL_INTERFACE,                    VSIR_OP_DCL_INTERFACE,                    "",     "",
                shader_sm5_read_dcl_interface},
        {VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT,    VSIR_OP_DCL_INPUT_CONTROL_POINT_COUNT,    "",     "",
                shader_sm5_read_control_point_count},
        {VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT,   VSIR_OP_DCL_OUTPUT_CONTROL_POINT_COUNT,   "",     "",
                shader_sm5_read_control_point_count},
        {VKD3D_SM5_OP_DCL_TESSELLATOR_DOMAIN,           VSIR_OP_DCL_TESSELLATOR_DOMAIN,           "",     "",
                shader_sm5_read_dcl_tessellator_domain},
        {VKD3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING,     VSIR_OP_DCL_TESSELLATOR_PARTITIONING,     "",     "",
                shader_sm5_read_dcl_tessellator_partitioning},
        {VKD3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE, VSIR_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE, "",     "",
                shader_sm5_read_dcl_tessellator_output_primitive},
        {VKD3D_SM5_OP_DCL_HS_MAX_TESSFACTOR,            VSIR_OP_DCL_HS_MAX_TESSFACTOR,            "",     "",
                shader_sm5_read_dcl_hs_max_tessfactor},
        {VKD3D_SM5_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT, VSIR_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT, "",     "",
                shader_sm4_read_declaration_count},
        {VKD3D_SM5_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT, VSIR_OP_DCL_HS_JOIN_PHASE_INSTANCE_COUNT, "",     "",
                shader_sm4_read_declaration_count},
        {VKD3D_SM5_OP_DCL_THREAD_GROUP,                 VSIR_OP_DCL_THREAD_GROUP,                 "",     "",
                shader_sm5_read_dcl_thread_group},
        {VKD3D_SM5_OP_DCL_UAV_TYPED,                    VSIR_OP_DCL_UAV_TYPED,                    "",     "",
                shader_sm4_read_dcl_resource},
        {VKD3D_SM5_OP_DCL_UAV_RAW,                      VSIR_OP_DCL_UAV_RAW,                      "",     "",
                shader_sm5_read_dcl_uav_raw},
        {VKD3D_SM5_OP_DCL_UAV_STRUCTURED,               VSIR_OP_DCL_UAV_STRUCTURED,               "",     "",
                shader_sm5_read_dcl_uav_structured},
        {VKD3D_SM5_OP_DCL_TGSM_RAW,                     VSIR_OP_DCL_TGSM_RAW,                     "",     "",
                shader_sm5_read_dcl_tgsm_raw},
        {VKD3D_SM5_OP_DCL_TGSM_STRUCTURED,              VSIR_OP_DCL_TGSM_STRUCTURED,              "",     "",
                shader_sm5_read_dcl_tgsm_structured},
        {VKD3D_SM5_OP_DCL_RESOURCE_RAW,                 VSIR_OP_DCL_RESOURCE_RAW,                 "",     "",
                shader_sm5_read_dcl_resource_raw},
        {VKD3D_SM5_OP_DCL_RESOURCE_STRUCTURED,          VSIR_OP_DCL_RESOURCE_STRUCTURED,          "",     "",
                shader_sm5_read_dcl_resource_structured},
        {VKD3D_SM5_OP_LD_UAV_TYPED,                     VSIR_OP_LD_UAV_TYPED,                     "u",    "i*"},
        {VKD3D_SM5_OP_STORE_UAV_TYPED,                  VSIR_OP_STORE_UAV_TYPED,                  "*",    "iu"},
        {VKD3D_SM5_OP_LD_RAW,                           VSIR_OP_LD_RAW,                           "u",    "i*"},
        {VKD3D_SM5_OP_STORE_RAW,                        VSIR_OP_STORE_RAW,                        "*",    "uu"},
        {VKD3D_SM5_OP_LD_STRUCTURED,                    VSIR_OP_LD_STRUCTURED,                    "u",    "ii*"},
        {VKD3D_SM5_OP_STORE_STRUCTURED,                 VSIR_OP_STORE_STRUCTURED,                 "*",    "iiu"},
        {VKD3D_SM5_OP_ATOMIC_AND,                       VSIR_OP_ATOMIC_AND,                       "*",    "iu"},
        {VKD3D_SM5_OP_ATOMIC_OR,                        VSIR_OP_ATOMIC_OR,                        "*",    "iu"},
        {VKD3D_SM5_OP_ATOMIC_XOR,                       VSIR_OP_ATOMIC_XOR,                       "*",    "iu"},
        {VKD3D_SM5_OP_ATOMIC_CMP_STORE,                 VSIR_OP_ATOMIC_CMP_STORE,                 "*",    "iuu"},
        {VKD3D_SM5_OP_ATOMIC_IADD,                      VSIR_OP_ATOMIC_IADD,                      "*",    "ii"},
        {VKD3D_SM5_OP_ATOMIC_IMAX,                      VSIR_OP_ATOMIC_IMAX,                      "*",    "ii"},
        {VKD3D_SM5_OP_ATOMIC_IMIN,                      VSIR_OP_ATOMIC_IMIN,                      "*",    "ii"},
        {VKD3D_SM5_OP_ATOMIC_UMAX,                      VSIR_OP_ATOMIC_UMAX,                      "*",    "iu"},
        {VKD3D_SM5_OP_ATOMIC_UMIN,                      VSIR_OP_ATOMIC_UMIN,                      "*",    "iu"},
        {VKD3D_SM5_OP_IMM_ATOMIC_ALLOC,                 VSIR_OP_IMM_ATOMIC_ALLOC,                 "u",    "*"},
        {VKD3D_SM5_OP_IMM_ATOMIC_CONSUME,               VSIR_OP_IMM_ATOMIC_CONSUME,               "u",    "*"},
        {VKD3D_SM5_OP_IMM_ATOMIC_IADD,                  VSIR_OP_IMM_ATOMIC_IADD,                  "u*",   "ii"},
        {VKD3D_SM5_OP_IMM_ATOMIC_AND,                   VSIR_OP_IMM_ATOMIC_AND,                   "u*",   "iu"},
        {VKD3D_SM5_OP_IMM_ATOMIC_OR,                    VSIR_OP_IMM_ATOMIC_OR,                    "u*",   "iu"},
        {VKD3D_SM5_OP_IMM_ATOMIC_XOR,                   VSIR_OP_IMM_ATOMIC_XOR,                   "u*",   "iu"},
        {VKD3D_SM5_OP_IMM_ATOMIC_EXCH,                  VSIR_OP_IMM_ATOMIC_EXCH,                  "u*",   "iu"},
        {VKD3D_SM5_OP_IMM_ATOMIC_CMP_EXCH,              VSIR_OP_IMM_ATOMIC_CMP_EXCH,              "u*",   "iuu"},
        {VKD3D_SM5_OP_IMM_ATOMIC_IMAX,                  VSIR_OP_IMM_ATOMIC_IMAX,                  "i*",   "ii"},
        {VKD3D_SM5_OP_IMM_ATOMIC_IMIN,                  VSIR_OP_IMM_ATOMIC_IMIN,                  "i*",   "ii"},
        {VKD3D_SM5_OP_IMM_ATOMIC_UMAX,                  VSIR_OP_IMM_ATOMIC_UMAX,                  "u*",   "iu"},
        {VKD3D_SM5_OP_IMM_ATOMIC_UMIN,                  VSIR_OP_IMM_ATOMIC_UMIN,                  "u*",   "iu"},
        {VKD3D_SM5_OP_SYNC,                             VSIR_OP_SYNC,                             "",     "",
                shader_sm5_read_sync},
        {VKD3D_SM5_OP_DADD,                             VSIR_OP_DADD,                             "d",    "dd"},
        {VKD3D_SM5_OP_DMAX,                             VSIR_OP_DMAX,                             "d",    "dd"},
        {VKD3D_SM5_OP_DMIN,                             VSIR_OP_DMIN,                             "d",    "dd"},
        {VKD3D_SM5_OP_DMUL,                             VSIR_OP_DMUL,                             "d",    "dd"},
        {VKD3D_SM5_OP_DEQ,                              VSIR_OP_DEQO,                             "u",    "dd"},
        {VKD3D_SM5_OP_DGE,                              VSIR_OP_DGEO,                             "u",    "dd"},
        {VKD3D_SM5_OP_DLT,                              VSIR_OP_DLT,                              "u",    "dd"},
        {VKD3D_SM5_OP_DNE,                              VSIR_OP_DNE,                              "u",    "dd"},
        {VKD3D_SM5_OP_DMOV,                             VSIR_OP_DMOV,                             "d",    "d"},
        {VKD3D_SM5_OP_DMOVC,                            VSIR_OP_DMOVC,                            "d",    "udd"},
        {VKD3D_SM5_OP_DTOF,                             VSIR_OP_DTOF,                             "f",    "d"},
        {VKD3D_SM5_OP_FTOD,                             VSIR_OP_FTOD,                             "d",    "f"},
        {VKD3D_SM5_OP_EVAL_SAMPLE_INDEX,                VSIR_OP_EVAL_SAMPLE_INDEX,                "f",    "fi"},
        {VKD3D_SM5_OP_EVAL_CENTROID,                    VSIR_OP_EVAL_CENTROID,                    "f",    "f"},
        {VKD3D_SM5_OP_DCL_GS_INSTANCES,                 VSIR_OP_DCL_GS_INSTANCES,                 "",     "",
                shader_sm4_read_declaration_count},
        {VKD3D_SM5_OP_DDIV,                             VSIR_OP_DDIV,                             "d",    "dd"},
        {VKD3D_SM5_OP_DFMA,                             VSIR_OP_DFMA,                             "d",    "ddd"},
        {VKD3D_SM5_OP_DRCP,                             VSIR_OP_DRCP,                             "d",    "d"},
        {VKD3D_SM5_OP_MSAD,                             VSIR_OP_MSAD,                             "u",    "uuu"},
        {VKD3D_SM5_OP_DTOI,                             VSIR_OP_DTOI,                             "i",    "d"},
        {VKD3D_SM5_OP_DTOU,                             VSIR_OP_DTOU,                             "u",    "d"},
        {VKD3D_SM5_OP_ITOD,                             VSIR_OP_ITOD,                             "d",    "i"},
        {VKD3D_SM5_OP_UTOD,                             VSIR_OP_UTOD,                             "d",    "u"},
        {VKD3D_SM5_OP_GATHER4_S,                        VSIR_OP_GATHER4_S,                        "uu",   "f**"},
        {VKD3D_SM5_OP_GATHER4_C_S,                      VSIR_OP_GATHER4_C_S,                      "fu",   "f**f"},
        {VKD3D_SM5_OP_GATHER4_PO_S,                     VSIR_OP_GATHER4_PO_S,                     "fu",   "fi**"},
        {VKD3D_SM5_OP_GATHER4_PO_C_S,                   VSIR_OP_GATHER4_PO_C_S,                   "fu",   "fi**f"},
        {VKD3D_SM5_OP_LD_S,                             VSIR_OP_LD_S,                             "uu",   "i*"},
        {VKD3D_SM5_OP_LD2DMS_S,                         VSIR_OP_LD2DMS_S,                         "uu",   "i*i"},
        {VKD3D_SM5_OP_LD_UAV_TYPED_S,                   VSIR_OP_LD_UAV_TYPED_S,                   "uu",   "iU"},
        {VKD3D_SM5_OP_LD_RAW_S,                         VSIR_OP_LD_RAW_S,                         "uu",   "iU"},
        {VKD3D_SM5_OP_LD_STRUCTURED_S,                  VSIR_OP_LD_STRUCTURED_S,                  "uu",   "ii*"},
        {VKD3D_SM5_OP_SAMPLE_LOD_S,                     VSIR_OP_SAMPLE_LOD_S,                     "uu",   "f**f"},
        {VKD3D_SM5_OP_SAMPLE_C_LZ_S,                    VSIR_OP_SAMPLE_C_LZ_S,                    "fu",   "f**f"},
        {VKD3D_SM5_OP_SAMPLE_CL_S,                      VSIR_OP_SAMPLE_CL_S,                      "uu",   "f**f"},
        {VKD3D_SM5_OP_SAMPLE_B_CL_S,                    VSIR_OP_SAMPLE_B_CL_S,                    "uu",   "f**ff"},
        {VKD3D_SM5_OP_SAMPLE_GRAD_CL_S,                 VSIR_OP_SAMPLE_GRAD_CL_S,                 "uu",   "f**fff"},
        {VKD3D_SM5_OP_SAMPLE_C_CL_S,                    VSIR_OP_SAMPLE_C_CL_S,                    "fu",   "f**ff"},
        {VKD3D_SM5_OP_CHECK_ACCESS_FULLY_MAPPED,        VSIR_OP_CHECK_ACCESS_FULLY_MAPPED,        "u",    "u"},
    };

    static const struct vkd3d_sm4_register_type_info register_type_table[] =
    {
        {VKD3D_SM4_RT_TEMP,                    VKD3DSPR_TEMP,            VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM4_RT_INPUT,                   VKD3DSPR_INPUT,           VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM4_RT_OUTPUT,                  VKD3DSPR_OUTPUT,          VKD3D_SM4_SWIZZLE_INVALID},
        {VKD3D_SM4_RT_INDEXABLE_TEMP,          VKD3DSPR_IDXTEMP,         VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM4_RT_IMMCONST,                VKD3DSPR_IMMCONST,        VKD3D_SM4_SWIZZLE_NONE},
        {VKD3D_SM4_RT_IMMCONST64,              VKD3DSPR_IMMCONST64,      VKD3D_SM4_SWIZZLE_NONE},
        {VKD3D_SM4_RT_SAMPLER,                 VKD3DSPR_SAMPLER,         VKD3D_SM4_SWIZZLE_SCALAR},
        {VKD3D_SM4_RT_RESOURCE,                VKD3DSPR_RESOURCE,        VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM4_RT_CONSTBUFFER,             VKD3DSPR_CONSTBUFFER,     VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM4_RT_IMMCONSTBUFFER,          VKD3DSPR_IMMCONSTBUFFER,  VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM4_RT_PRIMID,                  VKD3DSPR_PRIMID,          VKD3D_SM4_SWIZZLE_NONE},
        {VKD3D_SM4_RT_DEPTHOUT,                VKD3DSPR_DEPTHOUT,        VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM4_RT_NULL,                    VKD3DSPR_NULL,            VKD3D_SM4_SWIZZLE_INVALID},
        {VKD3D_SM4_RT_RASTERIZER,              VKD3DSPR_RASTERIZER,      VKD3D_SM4_SWIZZLE_SCALAR},
        {VKD3D_SM4_RT_OMASK,                   VKD3DSPR_SAMPLEMASK,      VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_STREAM,                  VKD3DSPR_STREAM,          VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_FUNCTION_BODY,           VKD3DSPR_FUNCTIONBODY,    VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_FUNCTION_POINTER,        VKD3DSPR_FUNCTIONPOINTER, VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_OUTPUT_CONTROL_POINT_ID, VKD3DSPR_OUTPOINTID,      VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_FORK_INSTANCE_ID,        VKD3DSPR_FORKINSTID,      VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_JOIN_INSTANCE_ID,        VKD3DSPR_JOININSTID,      VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_INPUT_CONTROL_POINT,     VKD3DSPR_INCONTROLPOINT,  VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_OUTPUT_CONTROL_POINT,    VKD3DSPR_OUTCONTROLPOINT, VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_PATCH_CONSTANT_DATA,     VKD3DSPR_PATCHCONST,      VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_DOMAIN_LOCATION,         VKD3DSPR_TESSCOORD,       VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_UAV,                     VKD3DSPR_UAV,             VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_SHARED_MEMORY,           VKD3DSPR_GROUPSHAREDMEM,  VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_THREAD_ID,               VKD3DSPR_THREADID,        VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_THREAD_GROUP_ID,         VKD3DSPR_THREADGROUPID,   VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_LOCAL_THREAD_ID,         VKD3DSPR_LOCALTHREADID,   VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_COVERAGE,                VKD3DSPR_COVERAGE,        VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_LOCAL_THREAD_INDEX,      VKD3DSPR_LOCALTHREADINDEX,VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_GS_INSTANCE_ID,          VKD3DSPR_GSINSTID,        VKD3D_SM4_SWIZZLE_SCALAR},
        {VKD3D_SM5_RT_DEPTHOUT_GREATER_EQUAL,  VKD3DSPR_DEPTHOUTGE,      VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_DEPTHOUT_LESS_EQUAL,     VKD3DSPR_DEPTHOUTLE,      VKD3D_SM4_SWIZZLE_VEC4},
        {VKD3D_SM5_RT_OUTPUT_STENCIL_REF,      VKD3DSPR_OUTSTENCILREF,   VKD3D_SM4_SWIZZLE_VEC4},
    };

    static const struct vkd3d_sm4_stat_field_info stat_field_table[] =
    {
        {VKD3D_SM4_OP_MOV,      VKD3D_STAT_MOV},
        {VKD3D_SM4_OP_MOVC,     VKD3D_STAT_MOVC},
        {VKD3D_SM5_OP_DMOV,     VKD3D_STAT_MOV},
        {VKD3D_SM5_OP_DMOVC,    VKD3D_STAT_MOVC},

        {VKD3D_SM4_OP_ITOF,     VKD3D_STAT_CONV},
        {VKD3D_SM4_OP_FTOI,     VKD3D_STAT_CONV},
        {VKD3D_SM4_OP_FTOU,     VKD3D_STAT_CONV},
        {VKD3D_SM4_OP_UTOF,     VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_DTOU,     VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_UTOD,     VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_DTOF,     VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_FTOD,     VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_DTOI,     VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_ITOD,     VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_F32TOF16, VKD3D_STAT_CONV},
        {VKD3D_SM5_OP_F16TOF32, VKD3D_STAT_CONV},

        {VKD3D_SM4_OP_ADD,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_DIV,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_DP2,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_DP3,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_DP4,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_EQ,       VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_EXP,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_FRC,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_GE,       VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_LT,       VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_MAD,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_MIN,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_MAX,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_MUL,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_NE,       VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_ROUND_NE, VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_ROUND_NI, VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_ROUND_PI, VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_ROUND_Z,  VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_RSQ,      VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_SQRT,     VKD3D_STAT_FLOAT},
        {VKD3D_SM4_OP_SINCOS,   VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_RCP,      VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DADD,     VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DMAX,     VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DMIN,     VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DMUL,     VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DEQ,      VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DGE,      VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DLT,      VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DNE,      VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DDIV,     VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DFMA,     VKD3D_STAT_FLOAT},
        {VKD3D_SM5_OP_DRCP,     VKD3D_STAT_FLOAT},

        {VKD3D_SM4_OP_IADD, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_IEQ,  VKD3D_STAT_INT},
        {VKD3D_SM4_OP_IGE,  VKD3D_STAT_INT},
        {VKD3D_SM4_OP_ILT,  VKD3D_STAT_INT},
        {VKD3D_SM4_OP_IMAD, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_IMAX, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_IMIN, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_IMUL, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_INE,  VKD3D_STAT_INT},
        {VKD3D_SM4_OP_INEG, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_ISHL, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_ISHR, VKD3D_STAT_INT},
        {VKD3D_SM4_OP_ITOF, VKD3D_STAT_INT},

        {VKD3D_SM4_OP_UDIV, VKD3D_STAT_UINT},
        {VKD3D_SM4_OP_ULT,  VKD3D_STAT_UINT},
        {VKD3D_SM4_OP_UGE,  VKD3D_STAT_UINT},
        {VKD3D_SM4_OP_UMUL, VKD3D_STAT_UINT},
        {VKD3D_SM4_OP_UMAX, VKD3D_STAT_UINT},
        {VKD3D_SM4_OP_UMIN, VKD3D_STAT_UINT},
        {VKD3D_SM4_OP_USHR, VKD3D_STAT_UINT},

        {VKD3D_SM4_OP_EMIT,        VKD3D_STAT_EMIT},
        {VKD3D_SM4_OP_CUT,         VKD3D_STAT_CUT},
        {VKD3D_SM5_OP_EMIT_STREAM, VKD3D_STAT_EMIT},
        {VKD3D_SM5_OP_CUT_STREAM,  VKD3D_STAT_CUT},

        {VKD3D_SM4_OP_SAMPLE,           VKD3D_STAT_SAMPLE},
        {VKD3D_SM4_OP_SAMPLE_LOD,       VKD3D_STAT_SAMPLE},
        {VKD3D_SM5_OP_SAMPLE_LOD_S,     VKD3D_STAT_SAMPLE},
        {VKD3D_SM5_OP_SAMPLE_CL_S,      VKD3D_STAT_SAMPLE},
        {VKD3D_SM4_OP_SAMPLE_C,         VKD3D_STAT_SAMPLE_C},
        {VKD3D_SM4_OP_SAMPLE_C_LZ,      VKD3D_STAT_SAMPLE_C},
        {VKD3D_SM5_OP_SAMPLE_C_LZ_S,    VKD3D_STAT_SAMPLE_C},
        {VKD3D_SM5_OP_SAMPLE_C_CL_S,    VKD3D_STAT_SAMPLE_C},
        {VKD3D_SM4_OP_SAMPLE_GRAD,      VKD3D_STAT_SAMPLE_GRAD},
        {VKD3D_SM5_OP_SAMPLE_GRAD_CL_S, VKD3D_STAT_SAMPLE_GRAD},
        {VKD3D_SM4_OP_SAMPLE_B,         VKD3D_STAT_SAMPLE_BIAS},
        {VKD3D_SM4_OP_GATHER4,          VKD3D_STAT_GATHER},
        {VKD3D_SM5_OP_GATHER4_PO,       VKD3D_STAT_GATHER},
        {VKD3D_SM4_OP_LOD,              VKD3D_STAT_LOD},

        {VKD3D_SM4_OP_LD,              VKD3D_STAT_LOAD},
        {VKD3D_SM4_OP_LD2DMS,          VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD_UAV_TYPED,    VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD_RAW,          VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD_STRUCTURED,   VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD_S,            VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD2DMS_S,        VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD_UAV_TYPED_S,  VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD_RAW_S,        VKD3D_STAT_LOAD},
        {VKD3D_SM5_OP_LD_STRUCTURED_S, VKD3D_STAT_LOAD},

        {VKD3D_SM5_OP_STORE_UAV_TYPED, VKD3D_STAT_STORE},
        {VKD3D_SM5_OP_STORE_RAW,       VKD3D_STAT_STORE},
        {VKD3D_SM5_OP_STORE_STRUCTURED,VKD3D_STAT_STORE},

        {VKD3D_SM4_OP_DCL_VERTICES_OUT,    VKD3D_STAT_DCL_VERTICES_OUT},
        {VKD3D_SM4_OP_DCL_INPUT_PRIMITIVE, VKD3D_STAT_DCL_INPUT_PRIMITIVE},
        {VKD3D_SM4_OP_DCL_OUTPUT_TOPOLOGY, VKD3D_STAT_DCL_OUTPUT_TOPOLOGY},
        {VKD3D_SM5_OP_DCL_GS_INSTANCES,    VKD3D_STAT_DCL_GS_INSTANCES},

        {VKD3D_SM4_OP_AND, VKD3D_STAT_BITWISE},
        {VKD3D_SM4_OP_NOT, VKD3D_STAT_BITWISE},
        {VKD3D_SM4_OP_OR,  VKD3D_STAT_BITWISE},
        {VKD3D_SM4_OP_XOR, VKD3D_STAT_BITWISE},

        {VKD3D_SM5_OP_ATOMIC_AND,          VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_OR,           VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_XOR,          VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_CMP_STORE,    VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_IADD,         VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_IMAX,         VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_IMIN,         VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_UMAX,         VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_ATOMIC_UMIN,         VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_ALLOC,    VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_CONSUME,  VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_IADD,     VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_AND,      VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_OR,       VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_XOR,      VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_EXCH,     VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_CMP_EXCH, VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_IMAX,     VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_IMIN,     VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_UMAX,     VKD3D_STAT_ATOMIC},
        {VKD3D_SM5_OP_IMM_ATOMIC_UMIN,     VKD3D_STAT_ATOMIC},

        {VKD3D_SM5_OP_DCL_TESSELLATOR_DOMAIN, VKD3D_STAT_TESS_DOMAIN},
        {VKD3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING, VKD3D_STAT_TESS_PARTITIONING},
        {VKD3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE, VKD3D_STAT_TESS_OUTPUT_PRIMITIVE},
        {VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT, VKD3D_STAT_TESS_CONTROL_POINT_COUNT},
        {VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT, VKD3D_STAT_TESS_CONTROL_POINT_COUNT},

        {VKD3D_SM5_OP_SYNC, VKD3D_STAT_BARRIER},

        {VKD3D_SM4_OP_DCL_TEMPS, VKD3D_STAT_TEMPS},
    };

    memset(lookup, 0, sizeof(*lookup));

    for (i = 0; i < ARRAY_SIZE(opcode_table); ++i)
    {
        const struct vkd3d_sm4_opcode_info *info = &opcode_table[i];

        lookup->opcode_info_from_sm4[info->opcode] = info;
        lookup->opcode_info_from_vsir[info->handler_idx] = info;
    }

    for (i = 0; i < ARRAY_SIZE(register_type_table); ++i)
    {
        const struct vkd3d_sm4_register_type_info *info = &register_type_table[i];

        lookup->register_type_info_from_sm4[info->sm4_type] = info;
        lookup->register_type_info_from_vkd3d[info->vkd3d_type] = info;
    }

    for (i = 0; i < ARRAY_SIZE(stat_field_table); ++i)
    {
        const struct vkd3d_sm4_stat_field_info *info = &stat_field_table[i];

        lookup->stat_field_from_sm4[info->opcode] = info;
    }
}

static const struct vkd3d_sm4_opcode_info *get_info_from_sm4_opcode(
        const struct vkd3d_sm4_lookup_tables *lookup, enum vkd3d_sm4_opcode sm4_opcode)
{
    if (sm4_opcode >= VKD3D_SM4_OP_COUNT)
        return NULL;
    return lookup->opcode_info_from_sm4[sm4_opcode];
}

static const struct vkd3d_sm4_opcode_info *get_info_from_vsir_opcode(
        const struct vkd3d_sm4_lookup_tables *lookup, enum vkd3d_shader_opcode vsir_opcode)
{
    if (vsir_opcode >= VSIR_OP_COUNT)
        return NULL;
    return lookup->opcode_info_from_vsir[vsir_opcode];
}

static unsigned int opcode_info_get_dst_count(const struct vkd3d_sm4_opcode_info *info)
{
    return strnlen(info->dst_info, SM4_MAX_DST_COUNT);
}

static unsigned int opcode_info_get_src_count(const struct vkd3d_sm4_opcode_info *info)
{
    return strnlen(info->src_info, SM4_MAX_SRC_COUNT);
}

static const struct vkd3d_sm4_register_type_info *get_info_from_sm4_register_type(
        const struct vkd3d_sm4_lookup_tables *lookup, enum vkd3d_sm4_register_type sm4_type)
{
    if (sm4_type >= VKD3D_SM4_REGISTER_TYPE_COUNT)
        return NULL;
    return lookup->register_type_info_from_sm4[sm4_type];
}

static const struct vkd3d_sm4_register_type_info *get_info_from_vkd3d_register_type(
        const struct vkd3d_sm4_lookup_tables *lookup, enum vkd3d_shader_register_type vkd3d_type)
{
    if (vkd3d_type >= VKD3DSPR_COUNT)
        return NULL;
    return lookup->register_type_info_from_vkd3d[vkd3d_type];
}

static enum vkd3d_sm4_stat_field get_stat_field_from_sm4_opcode(
        const struct vkd3d_sm4_lookup_tables *lookup, enum vkd3d_sm4_opcode sm4_opcode)
{
    const struct vkd3d_sm4_stat_field_info *field_info;

    if (sm4_opcode >= VKD3D_SM4_OP_COUNT || !(field_info = lookup->stat_field_from_sm4[sm4_opcode]))
        return VKD3D_STAT_UNUSED;
    return field_info->field;
}

static enum vkd3d_data_type map_data_type(char t)
{
    switch (t)
    {
        case 'd':
            return VKD3D_DATA_DOUBLE;
        case 'f':
            return VKD3D_DATA_FLOAT;
        case 'i':
            return VKD3D_DATA_INT;
        case 'u':
            return VKD3D_DATA_UINT;
        case 'O':
            return VKD3D_DATA_OPAQUE;
        case '*':
            return VKD3D_DATA_UNUSED;
        default:
            ERR("Invalid data type '%c'.\n", t);
            return VKD3D_DATA_FLOAT;
    }
}

static bool shader_sm4_read_reg_idx(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, uint32_t addressing, struct vkd3d_shader_register_index *reg_idx)
{
    if (addressing & VKD3D_SM4_ADDRESSING_RELATIVE)
    {
        struct vkd3d_shader_src_param *rel_addr = vsir_program_get_src_params(priv->p.program, 1);

        if (!(reg_idx->rel_addr = rel_addr))
        {
            ERR("Failed to get src param for relative addressing.\n");
            return false;
        }

        if (addressing & VKD3D_SM4_ADDRESSING_OFFSET)
            reg_idx->offset = *(*ptr)++;
        else
            reg_idx->offset = 0;
        shader_sm4_read_src_param(priv, ptr, end, VKD3D_DATA_INT, rel_addr);
    }
    else
    {
        reg_idx->rel_addr = NULL;
        reg_idx->offset = *(*ptr)++;
    }

    return true;
}

static bool shader_sm4_read_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr, const uint32_t *end,
        enum vkd3d_data_type data_type, struct vkd3d_shader_register *param, enum vkd3d_shader_src_modifier *modifier)
{
    const struct vkd3d_sm4_register_type_info *register_type_info;
    enum vkd3d_shader_register_type vsir_register_type;
    enum vkd3d_sm4_register_precision precision;
    uint32_t token, order, extended, addressing;
    enum vkd3d_sm4_register_type register_type;
    enum vkd3d_sm4_extended_operand_type type;
    enum vkd3d_sm4_dimension sm4_dimension;
    enum vkd3d_sm4_register_modifier m;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }
    token = *(*ptr)++;

    register_type = (token & VKD3D_SM4_REGISTER_TYPE_MASK) >> VKD3D_SM4_REGISTER_TYPE_SHIFT;
    register_type_info = get_info_from_sm4_register_type(&priv->lookup, register_type);
    if (!register_type_info)
    {
        FIXME("Unhandled register type %#x.\n", register_type);
        vsir_register_type = VKD3DSPR_TEMP;
    }
    else
    {
        vsir_register_type = register_type_info->vkd3d_type;
    }

    order = (token & VKD3D_SM4_REGISTER_ORDER_MASK) >> VKD3D_SM4_REGISTER_ORDER_SHIFT;

    vsir_register_init(param, vsir_register_type, data_type, order);
    param->precision = VKD3D_SHADER_REGISTER_PRECISION_DEFAULT;
    param->non_uniform = false;

    *modifier = VKD3DSPSM_NONE;
    if (token & VKD3D_SM4_EXTENDED_OPERAND)
    {
        if (*ptr >= end)
        {
            WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
            return false;
        }
        extended = *(*ptr)++;

        if (extended & VKD3D_SM4_EXTENDED_OPERAND)
        {
            FIXME("Skipping second-order extended operand.\n");
            *ptr += *ptr < end;
        }

        type = extended & VKD3D_SM4_EXTENDED_OPERAND_TYPE_MASK;
        if (type == VKD3D_SM4_EXTENDED_OPERAND_MODIFIER)
        {
            m = (extended & VKD3D_SM4_REGISTER_MODIFIER_MASK) >> VKD3D_SM4_REGISTER_MODIFIER_SHIFT;
            switch (m)
            {
                case VKD3D_SM4_REGISTER_MODIFIER_NEGATE:
                    *modifier = VKD3DSPSM_NEG;
                    break;

                case VKD3D_SM4_REGISTER_MODIFIER_ABS:
                    *modifier = VKD3DSPSM_ABS;
                    break;

                case VKD3D_SM4_REGISTER_MODIFIER_ABS_NEGATE:
                    *modifier = VKD3DSPSM_ABSNEG;
                    break;

                case VKD3D_SM4_REGISTER_MODIFIER_NONE:
                    break;

                default:
                    FIXME("Unhandled register modifier %#x.\n", m);
                    break;
            }

            precision = (extended & VKD3D_SM4_REGISTER_PRECISION_MASK) >> VKD3D_SM4_REGISTER_PRECISION_SHIFT;
            if (precision >= ARRAY_SIZE(register_precision_table)
                    || register_precision_table[precision] == VKD3D_SHADER_REGISTER_PRECISION_INVALID)
            {
                FIXME("Unhandled register precision %#x.\n", precision);
                param->precision = VKD3D_SHADER_REGISTER_PRECISION_INVALID;
            }
            else
            {
                param->precision = register_precision_table[precision];
            }

            if (extended & VKD3D_SM4_REGISTER_NON_UNIFORM_MASK)
                param->non_uniform = true;

            extended &= ~(VKD3D_SM4_EXTENDED_OPERAND_TYPE_MASK | VKD3D_SM4_REGISTER_MODIFIER_MASK
                    | VKD3D_SM4_REGISTER_PRECISION_MASK | VKD3D_SM4_REGISTER_NON_UNIFORM_MASK
                    | VKD3D_SM4_EXTENDED_OPERAND);
            if (extended)
                FIXME("Skipping unhandled extended operand bits 0x%08x.\n", extended);
        }
        else if (type)
        {
            FIXME("Skipping unhandled extended operand token 0x%08x (type %#x).\n", extended, type);
        }
    }

    if (order >= 1)
    {
        addressing = (token & VKD3D_SM4_ADDRESSING_MASK0) >> VKD3D_SM4_ADDRESSING_SHIFT0;
        if (!(shader_sm4_read_reg_idx(priv, ptr, end, addressing, &param->idx[0])))
        {
            ERR("Failed to read register index.\n");
            return false;
        }
    }

    if (order >= 2)
    {
        addressing = (token & VKD3D_SM4_ADDRESSING_MASK1) >> VKD3D_SM4_ADDRESSING_SHIFT1;
        if (!(shader_sm4_read_reg_idx(priv, ptr, end, addressing, &param->idx[1])))
        {
            ERR("Failed to read register index.\n");
            return false;
        }
    }

    if (order >= 3)
    {
        addressing = (token & VKD3D_SM4_ADDRESSING_MASK2) >> VKD3D_SM4_ADDRESSING_SHIFT2;
        if (!(shader_sm4_read_reg_idx(priv, ptr, end, addressing, &param->idx[2])))
        {
            ERR("Failed to read register index.\n");
            return false;
        }
    }

    if (order > 3)
    {
        WARN("Unhandled order %u.\n", order);
        return false;
    }

    sm4_dimension = (token & VKD3D_SM4_DIMENSION_MASK) >> VKD3D_SM4_DIMENSION_SHIFT;
    param->dimension = vsir_dimension_from_sm4_dimension(sm4_dimension);

    if (register_type == VKD3D_SM4_RT_IMMCONST || register_type == VKD3D_SM4_RT_IMMCONST64)
    {
        unsigned int u32_count;

        switch (param->dimension)
        {
            case VSIR_DIMENSION_SCALAR:
                u32_count = 1 + (register_type == VKD3D_SM4_RT_IMMCONST64);
                if (end - *ptr < u32_count)
                {
                    WARN("Invalid ptr %p, end %p.\n", *ptr, end);
                    return false;
                }
                memcpy(param->u.immconst_u32, *ptr, u32_count * sizeof(uint32_t));
                *ptr += u32_count;
                break;

            case VSIR_DIMENSION_VEC4:
                if (end - *ptr < VKD3D_VEC4_SIZE)
                {
                    WARN("Invalid ptr %p, end %p.\n", *ptr, end);
                    return false;
                }
                memcpy(param->u.immconst_u32, *ptr, VKD3D_VEC4_SIZE * sizeof(uint32_t));
                *ptr += 4;
                break;

            default:
                FIXME("Unhandled dimension %#x.\n", param->dimension);
                break;
        }
    }
    else if (register_type == VKD3D_SM4_RT_IMMCONSTBUFFER)
    {
        if (param->idx_count != 1)
        {
            WARN("Unexpected idx count %u.\n", param->idx_count);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_INDEX_COUNT,
                    "Invalid index count %u for immediate const buffer register; expected count 1.", param->idx_count);
        }
    }
    else if (!shader_is_sm_5_1(priv) && vsir_register_is_descriptor(param))
    {
        /* SM5.1 places a symbol identifier in idx[0] and moves
         * other values up one slot. Normalize to SM5.1. */
        param->idx[2] = param->idx[1];
        param->idx[1] = param->idx[0];
        ++param->idx_count;
    }

    return true;
}

bool shader_sm4_is_scalar_register(const struct vkd3d_shader_register *reg)
{
    switch (reg->type)
    {
        case VKD3DSPR_COVERAGE:
        case VKD3DSPR_DEPTHOUT:
        case VKD3DSPR_DEPTHOUTGE:
        case VKD3DSPR_DEPTHOUTLE:
        case VKD3DSPR_GSINSTID:
        case VKD3DSPR_LOCALTHREADINDEX:
        case VKD3DSPR_OUTPOINTID:
        case VKD3DSPR_PRIMID:
        case VKD3DSPR_SAMPLEMASK:
        case VKD3DSPR_OUTSTENCILREF:
            return true;
        default:
            return false;
    }
}

static uint32_t swizzle_from_sm4(uint32_t s)
{
    return vkd3d_shader_create_swizzle(s & 0x3, (s >> 2) & 0x3, (s >> 4) & 0x3, (s >> 6) & 0x3);
}

static uint32_t swizzle_to_sm4(uint32_t s)
{
    uint32_t ret = 0;
    ret |= ((vsir_swizzle_get_component(s, 0)) & 0x3);
    ret |= ((vsir_swizzle_get_component(s, 1)) & 0x3) << 2;
    ret |= ((vsir_swizzle_get_component(s, 2)) & 0x3) << 4;
    ret |= ((vsir_swizzle_get_component(s, 3)) & 0x3) << 6;
    return ret;
}

static bool register_is_input_output(const struct vkd3d_shader_register *reg)
{
    switch (reg->type)
    {
        case VKD3DSPR_INPUT:
        case VKD3DSPR_OUTPUT:
        case VKD3DSPR_COLOROUT:
        case VKD3DSPR_INCONTROLPOINT:
        case VKD3DSPR_OUTCONTROLPOINT:
        case VKD3DSPR_PATCHCONST:
            return true;

        default:
            return false;
    }
}

static bool register_is_control_point_input(const struct vkd3d_shader_register *reg,
        const struct vkd3d_shader_sm4_parser *priv)
{
    return reg->type == VKD3DSPR_INCONTROLPOINT || reg->type == VKD3DSPR_OUTCONTROLPOINT
            || (reg->type == VKD3DSPR_INPUT && (priv->phase == VSIR_OP_HS_CONTROL_POINT_PHASE
            || priv->p.program->shader_version.type == VKD3D_SHADER_TYPE_GEOMETRY));
}

static uint32_t mask_from_swizzle(uint32_t swizzle)
{
    return (1u << vsir_swizzle_get_component(swizzle, 0))
            | (1u << vsir_swizzle_get_component(swizzle, 1))
            | (1u << vsir_swizzle_get_component(swizzle, 2))
            | (1u << vsir_swizzle_get_component(swizzle, 3));
}

static bool shader_sm4_validate_input_output_register(struct vkd3d_shader_sm4_parser *priv,
        const struct vkd3d_shader_register *reg, unsigned int mask)
{
    unsigned int idx_count = 1 + register_is_control_point_input(reg, priv);
    const unsigned int *masks;
    unsigned int register_idx;

    if (reg->idx_count != idx_count)
    {
        vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_INDEX_COUNT,
                "Invalid index count %u for register type %#x; expected count %u.",
                reg->idx_count, reg->type, idx_count);
        return false;
    }

    switch (reg->type)
    {
        case VKD3DSPR_INPUT:
        case VKD3DSPR_INCONTROLPOINT:
            masks = priv->input_register_masks;
            break;
        case VKD3DSPR_OUTPUT:
            masks = sm4_parser_is_in_fork_or_join_phase(priv) ? priv->patch_constant_register_masks
                    : priv->output_register_masks;
            break;
        case VKD3DSPR_COLOROUT:
        case VKD3DSPR_OUTCONTROLPOINT:
            masks = priv->output_register_masks;
            break;
        case VKD3DSPR_PATCHCONST:
            masks = priv->patch_constant_register_masks;
            break;

        default:
            vkd3d_unreachable();
    }

    register_idx = reg->idx[reg->idx_count - 1].offset;
    /* The signature element registers have already been checked against MAX_REG_OUTPUT. */
    if (register_idx >= MAX_REG_OUTPUT || (masks[register_idx] & mask) != mask)
    {
        WARN("Failed to find signature element for register type %#x, index %u and mask %#x.\n",
                reg->type, register_idx, mask);
        vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_IO_REGISTER,
                "Could not find signature element matching register type %#x, index %u and mask %#x.",
                reg->type, register_idx, mask);
        return false;
    }

    return true;
}

static bool shader_sm4_read_src_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_src_param *src_param)
{
    unsigned int dimension, mask;
    uint32_t token;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }
    token = **ptr;

    if (!shader_sm4_read_param(priv, ptr, end, data_type, &src_param->reg, &src_param->modifiers))
    {
        ERR("Failed to read parameter.\n");
        return false;
    }

    switch ((dimension = (token & VKD3D_SM4_DIMENSION_MASK) >> VKD3D_SM4_DIMENSION_SHIFT))
    {
        case VKD3D_SM4_DIMENSION_NONE:
        case VKD3D_SM4_DIMENSION_SCALAR:
            src_param->swizzle = VKD3D_SHADER_SWIZZLE(X, X, X, X);
            break;

        case VKD3D_SM4_DIMENSION_VEC4:
        {
            enum vkd3d_sm4_swizzle_type swizzle_type =
                    (token & VKD3D_SM4_SWIZZLE_TYPE_MASK) >> VKD3D_SM4_SWIZZLE_TYPE_SHIFT;

            switch (swizzle_type)
            {
                case VKD3D_SM4_SWIZZLE_NONE:
                    src_param->swizzle = VKD3D_SHADER_NO_SWIZZLE;

                    mask = (token & VKD3D_SM4_WRITEMASK_MASK) >> VKD3D_SM4_WRITEMASK_SHIFT;
                    /* Mask seems only to be used for vec4 constants and is always zero. */
                    if (!register_is_constant(&src_param->reg))
                    {
                        FIXME("Source mask %#x is not for a constant.\n", mask);
                        vkd3d_shader_parser_warning(&priv->p, VKD3D_SHADER_WARNING_TPF_UNHANDLED_REGISTER_MASK,
                                "Unhandled mask %#x for a non-constant source register.", mask);
                    }
                    else if (mask)
                    {
                        FIXME("Unhandled mask %#x.\n", mask);
                        vkd3d_shader_parser_warning(&priv->p, VKD3D_SHADER_WARNING_TPF_UNHANDLED_REGISTER_MASK,
                                "Unhandled source register mask %#x.", mask);
                    }

                    break;

                case VKD3D_SM4_SWIZZLE_SCALAR:
                    src_param->swizzle = (token & VKD3D_SM4_SWIZZLE_MASK) >> VKD3D_SM4_SWIZZLE_SHIFT;
                    src_param->swizzle = (src_param->swizzle & 0x3) * 0x01010101;
                    break;

                case VKD3D_SM4_SWIZZLE_VEC4:
                    src_param->swizzle = swizzle_from_sm4((token & VKD3D_SM4_SWIZZLE_MASK) >> VKD3D_SM4_SWIZZLE_SHIFT);
                    break;

                default:
                    FIXME("Unhandled swizzle type %#x.\n", swizzle_type);
                    vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_SWIZZLE,
                            "Source register swizzle type %#x is invalid.", swizzle_type);
                    break;
            }
            break;
        }

        default:
            FIXME("Unhandled dimension %#x.\n", dimension);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_DIMENSION,
                    "Source register dimension %#x is invalid.", dimension);
            break;
    }

    if (data_type_is_64_bit(data_type))
        src_param->swizzle = vsir_swizzle_64_from_32(src_param->swizzle);

    if (register_is_input_output(&src_param->reg) && !shader_sm4_validate_input_output_register(priv,
            &src_param->reg, mask_from_swizzle(src_param->swizzle)))
        return false;

    return true;
}

static bool shader_sm4_read_dst_param(struct vkd3d_shader_sm4_parser *priv, const uint32_t **ptr,
        const uint32_t *end, enum vkd3d_data_type data_type, struct vkd3d_shader_dst_param *dst_param)
{
    enum vkd3d_sm4_swizzle_type swizzle_type;
    enum vkd3d_shader_src_modifier modifier;
    unsigned int dimension, swizzle;
    uint32_t token;

    if (*ptr >= end)
    {
        WARN("Invalid ptr %p >= end %p.\n", *ptr, end);
        return false;
    }
    token = **ptr;

    if (!shader_sm4_read_param(priv, ptr, end, data_type, &dst_param->reg, &modifier))
    {
        ERR("Failed to read parameter.\n");
        return false;
    }

    if (modifier != VKD3DSPSM_NONE)
    {
        ERR("Invalid source modifier %#x on destination register.\n", modifier);
        return false;
    }

    switch ((dimension = (token & VKD3D_SM4_DIMENSION_MASK) >> VKD3D_SM4_DIMENSION_SHIFT))
    {
        case VKD3D_SM4_DIMENSION_NONE:
            dst_param->write_mask = 0;
            break;

        case VKD3D_SM4_DIMENSION_SCALAR:
            dst_param->write_mask = VKD3DSP_WRITEMASK_0;
            break;

        case VKD3D_SM4_DIMENSION_VEC4:
            swizzle_type = (token & VKD3D_SM4_SWIZZLE_TYPE_MASK) >> VKD3D_SM4_SWIZZLE_TYPE_SHIFT;
            switch (swizzle_type)
            {
                case VKD3D_SM4_SWIZZLE_NONE:
                    dst_param->write_mask = (token & VKD3D_SM4_WRITEMASK_MASK) >> VKD3D_SM4_WRITEMASK_SHIFT;
                    break;

                case VKD3D_SM4_SWIZZLE_VEC4:
                    swizzle = swizzle_from_sm4((token & VKD3D_SM4_SWIZZLE_MASK) >> VKD3D_SM4_SWIZZLE_SHIFT);
                    if (swizzle != VKD3D_SHADER_NO_SWIZZLE)
                    {
                        FIXME("Unhandled swizzle %#x.\n", swizzle);
                        vkd3d_shader_parser_warning(&priv->p, VKD3D_SHADER_WARNING_TPF_UNHANDLED_REGISTER_SWIZZLE,
                                "Unhandled destination register swizzle %#x.", swizzle);
                    }
                    dst_param->write_mask = VKD3DSP_WRITEMASK_ALL;
                    break;

                default:
                    FIXME("Unhandled swizzle type %#x.\n", swizzle_type);
                    vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_SWIZZLE,
                            "Destination register swizzle type %#x is invalid.", swizzle_type);
                    break;
            }
            break;

        default:
            FIXME("Unhandled dimension %#x.\n", dimension);
            vkd3d_shader_parser_error(&priv->p, VKD3D_SHADER_ERROR_TPF_INVALID_REGISTER_DIMENSION,
                    "Destination register dimension %#x is invalid.", dimension);
            break;
    }

    if (data_type == VKD3D_DATA_DOUBLE)
        dst_param->write_mask = vsir_write_mask_64_from_32(dst_param->write_mask);
    /* Some scalar registers are declared with no write mask in shader bytecode. */
    if (!dst_param->write_mask && shader_sm4_is_scalar_register(&dst_param->reg))
        dst_param->write_mask = VKD3DSP_WRITEMASK_0;
    dst_param->modifiers = 0;
    dst_param->shift = 0;

    if (register_is_input_output(&dst_param->reg) && !shader_sm4_validate_input_output_register(priv,
            &dst_param->reg, dst_param->write_mask))
        return false;

    return true;
}

static void shader_sm4_read_instruction_modifier(uint32_t modifier, struct vkd3d_shader_instruction *ins)
{
    enum vkd3d_sm4_instruction_modifier modifier_type = modifier & VKD3D_SM4_MODIFIER_MASK;

    switch (modifier_type)
    {
        case VKD3D_SM4_MODIFIER_AOFFIMMI:
        {
            static const uint32_t recognized_bits = VKD3D_SM4_INSTRUCTION_MODIFIER
                    | VKD3D_SM4_MODIFIER_MASK
                    | VKD3D_SM4_AOFFIMMI_U_MASK
                    | VKD3D_SM4_AOFFIMMI_V_MASK
                    | VKD3D_SM4_AOFFIMMI_W_MASK;

            /* Bit fields are used for sign extension. */
            struct
            {
                int u : 4;
                int v : 4;
                int w : 4;
            } aoffimmi;

            if (modifier & ~recognized_bits)
                FIXME("Unhandled instruction modifier %#x.\n", modifier);

            aoffimmi.u = (modifier & VKD3D_SM4_AOFFIMMI_U_MASK) >> VKD3D_SM4_AOFFIMMI_U_SHIFT;
            aoffimmi.v = (modifier & VKD3D_SM4_AOFFIMMI_V_MASK) >> VKD3D_SM4_AOFFIMMI_V_SHIFT;
            aoffimmi.w = (modifier & VKD3D_SM4_AOFFIMMI_W_MASK) >> VKD3D_SM4_AOFFIMMI_W_SHIFT;
            ins->texel_offset.u = aoffimmi.u;
            ins->texel_offset.v = aoffimmi.v;
            ins->texel_offset.w = aoffimmi.w;
            break;
        }

        case VKD3D_SM5_MODIFIER_DATA_TYPE:
        {
            uint32_t components = (modifier & VKD3D_SM5_MODIFIER_DATA_TYPE_MASK) >> VKD3D_SM5_MODIFIER_DATA_TYPE_SHIFT;
            unsigned int i;

            for (i = 0; i < VKD3D_VEC4_SIZE; i++)
            {
                enum vkd3d_sm4_data_type data_type = VKD3D_SM4_TYPE_COMPONENT(components, i);

                if (!data_type || (data_type >= ARRAY_SIZE(data_type_table)))
                {
                    FIXME("Unhandled data type %#x.\n", data_type);
                    ins->resource_data_type[i] = VKD3D_DATA_FLOAT;
                }
                else
                {
                    ins->resource_data_type[i] = data_type_table[data_type];
                }
            }
            break;
        }

        case VKD3D_SM5_MODIFIER_RESOURCE_TYPE:
        {
            enum vkd3d_sm4_resource_type resource_type
                    = (modifier & VKD3D_SM5_MODIFIER_RESOURCE_TYPE_MASK) >> VKD3D_SM5_MODIFIER_RESOURCE_TYPE_SHIFT;

            if (resource_type == VKD3D_SM4_RESOURCE_RAW_BUFFER)
                ins->raw = true;
            else if (resource_type == VKD3D_SM4_RESOURCE_STRUCTURED_BUFFER)
                ins->structured = true;

            if (resource_type < ARRAY_SIZE(resource_type_table))
                ins->resource_type = resource_type_table[resource_type];
            else
            {
                FIXME("Unhandled resource type %#x.\n", resource_type);
                ins->resource_type = VKD3D_SHADER_RESOURCE_NONE;
            }

            ins->resource_stride
                    = (modifier & VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_MASK) >> VKD3D_SM5_MODIFIER_RESOURCE_STRIDE_SHIFT;
            break;
        }

        default:
            FIXME("Unhandled instruction modifier %#x.\n", modifier);
    }
}

static void shader_sm4_read_instruction(struct vkd3d_shader_sm4_parser *sm4, struct vkd3d_shader_instruction *ins)
{
    const struct vkd3d_sm4_opcode_info *opcode_info;
    struct vsir_program *program = sm4->p.program;
    uint32_t opcode_token, opcode, previous_token;
    struct vkd3d_shader_dst_param *dst_params;
    struct vkd3d_shader_src_param *src_params;
    const uint32_t **ptr = &sm4->ptr;
    unsigned int i, len;
    const uint32_t *p;
    uint32_t precise;
    size_t remaining;

    if (*ptr >= sm4->end)
    {
        WARN("End of byte-code, failed to read opcode.\n");
        goto fail;
    }
    remaining = sm4->end - *ptr;

    ++sm4->p.location.line;

    opcode_token = *(*ptr)++;
    opcode = opcode_token & VKD3D_SM4_OPCODE_MASK;

    len = ((opcode_token & VKD3D_SM4_INSTRUCTION_LENGTH_MASK) >> VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT);
    if (!len)
    {
        if (remaining < 2)
        {
            WARN("End of byte-code, failed to read length token.\n");
            goto fail;
        }
        len = **ptr;
    }
    if (!len || remaining < len)
    {
        WARN("Read invalid length %u (remaining %zu).\n", len, remaining);
        goto fail;
    }
    --len;

    if (!(opcode_info = get_info_from_sm4_opcode(&sm4->lookup, opcode)))
    {
        FIXME("Unrecognized opcode %#x, opcode_token 0x%08x.\n", opcode, opcode_token);
        ins->opcode = VSIR_OP_INVALID;
        *ptr += len;
        return;
    }

    vsir_instruction_init(ins, &sm4->p.location, opcode_info->handler_idx);
    if (ins->opcode == VSIR_OP_HS_CONTROL_POINT_PHASE || ins->opcode == VSIR_OP_HS_FORK_PHASE
            || ins->opcode == VSIR_OP_HS_JOIN_PHASE)
        sm4->phase = ins->opcode;
    sm4->has_control_point_phase |= ins->opcode == VSIR_OP_HS_CONTROL_POINT_PHASE;
    ins->flags = 0;
    ins->coissue = false;
    ins->raw = false;
    ins->structured = false;
    ins->predicate = NULL;
    ins->dst_count = opcode_info_get_dst_count(opcode_info);
    ins->src_count = opcode_info_get_src_count(opcode_info);
    ins->src = src_params = vsir_program_get_src_params(program, ins->src_count);
    if (!src_params && ins->src_count)
    {
        ERR("Failed to allocate src parameters.\n");
        vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
        ins->opcode = VSIR_OP_INVALID;
        return;
    }
    ins->resource_type = VKD3D_SHADER_RESOURCE_NONE;
    ins->resource_stride = 0;
    ins->resource_data_type[0] = VKD3D_DATA_FLOAT;
    ins->resource_data_type[1] = VKD3D_DATA_FLOAT;
    ins->resource_data_type[2] = VKD3D_DATA_FLOAT;
    ins->resource_data_type[3] = VKD3D_DATA_FLOAT;
    memset(&ins->texel_offset, 0, sizeof(ins->texel_offset));

    p = *ptr;
    *ptr += len;

    if (opcode_info->read_opcode_func)
    {
        ins->dst = NULL;
        ins->dst_count = 0;
        opcode_info->read_opcode_func(ins, opcode, opcode_token, p, len, sm4);
    }
    else
    {
        enum vkd3d_shader_dst_modifier instruction_dst_modifier = VKD3DSPDM_NONE;

        previous_token = opcode_token;
        while (previous_token & VKD3D_SM4_INSTRUCTION_MODIFIER && p != *ptr)
            shader_sm4_read_instruction_modifier(previous_token = *p++, ins);

        ins->flags = (opcode_token & VKD3D_SM4_INSTRUCTION_FLAGS_MASK) >> VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT;
        if (ins->flags & VKD3D_SM4_INSTRUCTION_FLAG_SATURATE)
        {
            ins->flags &= ~VKD3D_SM4_INSTRUCTION_FLAG_SATURATE;
            instruction_dst_modifier = VKD3DSPDM_SATURATE;
        }
        precise = (opcode_token & VKD3D_SM5_PRECISE_MASK) >> VKD3D_SM5_PRECISE_SHIFT;
        ins->flags |= precise << VKD3DSI_PRECISE_SHIFT;

        ins->dst = dst_params = vsir_program_get_dst_params(program, ins->dst_count);
        if (!dst_params && ins->dst_count)
        {
            ERR("Failed to allocate dst parameters.\n");
            vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
            ins->opcode = VSIR_OP_INVALID;
            return;
        }
        for (i = 0; i < ins->dst_count; ++i)
        {
            if (!(shader_sm4_read_dst_param(sm4, &p, *ptr, map_data_type(opcode_info->dst_info[i]),
                    &dst_params[i])))
            {
                ins->opcode = VSIR_OP_INVALID;
                return;
            }
            dst_params[i].modifiers |= instruction_dst_modifier;
        }

        for (i = 0; i < ins->src_count; ++i)
        {
            if (!(shader_sm4_read_src_param(sm4, &p, *ptr, map_data_type(opcode_info->src_info[i]),
                    &src_params[i])))
            {
                ins->opcode = VSIR_OP_INVALID;
                return;
            }
        }
    }

    return;

fail:
    *ptr = sm4->end;
    ins->opcode = VSIR_OP_INVALID;
    return;
}

static bool shader_sm4_init(struct vkd3d_shader_sm4_parser *sm4, struct vsir_program *program,
        const uint32_t *byte_code, size_t byte_code_size, const struct vkd3d_shader_compile_info *compile_info,
        struct vkd3d_shader_message_context *message_context)
{
    struct vkd3d_shader_version version;
    uint32_t version_token, token_count;

    if (byte_code_size / sizeof(*byte_code) < 2)
    {
        WARN("Invalid byte code size %lu.\n", (long)byte_code_size);
        return false;
    }

    version_token = byte_code[0];
    TRACE("Version: 0x%08x.\n", version_token);
    token_count = byte_code[1];
    TRACE("Token count: %u.\n", token_count);

    if (token_count < 2 || byte_code_size / sizeof(*byte_code) < token_count)
    {
        WARN("Invalid token count %u.\n", token_count);
        return false;
    }

    sm4->start = &byte_code[2];
    sm4->end = &byte_code[token_count];

    switch (version_token >> 16)
    {
        case VKD3D_SM4_PS:
            version.type = VKD3D_SHADER_TYPE_PIXEL;
            break;

        case VKD3D_SM4_VS:
            version.type = VKD3D_SHADER_TYPE_VERTEX;
            break;

        case VKD3D_SM4_GS:
            version.type = VKD3D_SHADER_TYPE_GEOMETRY;
            break;

        case VKD3D_SM5_HS:
            version.type = VKD3D_SHADER_TYPE_HULL;
            break;

        case VKD3D_SM5_DS:
            version.type = VKD3D_SHADER_TYPE_DOMAIN;
            break;

        case VKD3D_SM5_CS:
            version.type = VKD3D_SHADER_TYPE_COMPUTE;
            break;

        default:
            FIXME("Unrecognised shader type %#x.\n", version_token >> 16);
    }
    version.major = VKD3D_SM4_VERSION_MAJOR(version_token);
    version.minor = VKD3D_SM4_VERSION_MINOR(version_token);

    /* Estimate instruction count to avoid reallocation in most shaders. */
    if (!vsir_program_init(program, compile_info,
            &version, token_count / 7u + 20, VSIR_CF_STRUCTURED, VSIR_NORMALISED_SM4))
        return false;
    vkd3d_shader_parser_init(&sm4->p, program, message_context, compile_info->source_name);
    sm4->ptr = sm4->start;

    init_sm4_lookup_tables(&sm4->lookup);

    return true;
}

static void uninvert_used_masks(struct shader_signature *signature)
{
    for (unsigned int i = 0; i < signature->element_count; ++i)
    {
        struct signature_element *e = &signature->elements[i];

        e->used_mask = e->mask & ~e->used_mask;
    }
}

static bool shader_sm4_parser_validate_signature(struct vkd3d_shader_sm4_parser *sm4,
        const struct shader_signature *signature, unsigned int *masks, const char *name)
{
    unsigned int i, register_idx, register_count, mask;

    for (i = 0; i < signature->element_count; ++i)
    {
        register_idx = signature->elements[i].register_index;
        register_count = signature->elements[i].register_count;
        if (register_idx != ~0u && (register_idx >= MAX_REG_OUTPUT || MAX_REG_OUTPUT - register_idx < register_count))
        {
            WARN("%s signature element %u unhandled register index %u, count %u.\n",
                    name, i, register_idx, register_count);
            vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_TOO_MANY_REGISTERS,
                    "%s signature element %u register index %u, count %u exceeds maximum index of %u.", name,
                    i, register_idx, register_count, MAX_REG_OUTPUT - 1);
            return false;
        }

        if (!vkd3d_bitmask_is_contiguous(mask = signature->elements[i].mask))
        {
            WARN("%s signature element %u mask %#x is not contiguous.\n", name, i, mask);
            vkd3d_shader_parser_warning(&sm4->p, VKD3D_SHADER_WARNING_TPF_MASK_NOT_CONTIGUOUS,
                    "%s signature element %u mask %#x is not contiguous.", name, i, mask);
        }

        if (register_idx != ~0u)
            masks[register_idx] |= mask;
    }

    return true;
}

static int index_range_compare(const void *a, const void *b)
{
    return memcmp(a, b, sizeof(struct sm4_index_range));
}

static void shader_sm4_validate_default_phase_index_ranges(struct vkd3d_shader_sm4_parser *sm4)
{
    if (!sm4->input_index_ranges.count || !sm4->output_index_ranges.count)
        return;

    if (sm4->input_index_ranges.count == sm4->output_index_ranges.count)
    {
        qsort(sm4->input_index_ranges.ranges, sm4->input_index_ranges.count, sizeof(sm4->input_index_ranges.ranges[0]),
                index_range_compare);
        qsort(sm4->output_index_ranges.ranges, sm4->output_index_ranges.count, sizeof(sm4->output_index_ranges.ranges[0]),
                index_range_compare);
        if (!memcmp(sm4->input_index_ranges.ranges, sm4->output_index_ranges.ranges,
                sm4->input_index_ranges.count * sizeof(sm4->input_index_ranges.ranges[0])))
            return;
    }

    /* This is very unlikely to occur and would complicate the default control point phase implementation. */
    WARN("Default phase index ranges are not identical.\n");
    vkd3d_shader_parser_error(&sm4->p, VKD3D_SHADER_ERROR_TPF_INVALID_INDEX_RANGE_DCL,
            "Default control point phase input and output index range declarations are not identical.");
    return;
}

int tpf_parse(const struct vkd3d_shader_compile_info *compile_info, uint64_t config_flags,
        struct vkd3d_shader_message_context *message_context, struct vsir_program *program)
{
    struct vkd3d_shader_instruction_array *instructions;
    struct vkd3d_shader_sm4_parser sm4 = {0};
    struct dxbc_shader_desc dxbc_desc = {0};
    struct vkd3d_shader_instruction *ins;
    int ret;

    dxbc_desc.is_dxil = false;
    if ((ret = shader_extract_from_dxbc(&compile_info->source,
            message_context, compile_info->source_name, &dxbc_desc)) < 0)
    {
        WARN("Failed to extract shader, vkd3d result %d.\n", ret);
        return ret;
    }

    if (!shader_sm4_init(&sm4, program, dxbc_desc.byte_code, dxbc_desc.byte_code_size,
            compile_info, message_context))
    {
        WARN("Failed to initialise shader parser.\n");
        free_dxbc_shader_desc(&dxbc_desc);
        return VKD3D_ERROR_INVALID_ARGUMENT;
    }

    program->input_signature = dxbc_desc.input_signature;
    program->output_signature = dxbc_desc.output_signature;
    program->patch_constant_signature = dxbc_desc.patch_constant_signature;
    program->features = dxbc_desc.features;
    memset(&dxbc_desc, 0, sizeof(dxbc_desc));

    /* DXBC stores used masks inverted for output signatures, for some reason.
     * We return them un-inverted. */
    uninvert_used_masks(&program->output_signature);
    if (program->shader_version.type == VKD3D_SHADER_TYPE_HULL)
        uninvert_used_masks(&program->patch_constant_signature);

    switch (program->shader_version.type)
    {
        case VKD3D_SHADER_TYPE_HULL:
        case VKD3D_SHADER_TYPE_DOMAIN:
            break;

        default:
            if (program->patch_constant_signature.element_count != 0)
            {
                WARN("The patch constant signature only makes sense for Hull and Domain Shaders, ignoring it.\n");
                shader_signature_cleanup(&program->patch_constant_signature);
            }
            break;
    }

    if (!shader_sm4_parser_validate_signature(&sm4, &program->input_signature,
            sm4.input_register_masks, "Input")
            || !shader_sm4_parser_validate_signature(&sm4, &program->output_signature,
            sm4.output_register_masks, "Output")
            || !shader_sm4_parser_validate_signature(&sm4, &program->patch_constant_signature,
            sm4.patch_constant_register_masks, "Patch constant"))
    {
        vsir_program_cleanup(program);
        return VKD3D_ERROR_INVALID_SHADER;
    }

    instructions = &program->instructions;
    while (sm4.ptr != sm4.end)
    {
        if (!shader_instruction_array_reserve(instructions, instructions->count + 1))
        {
            ERR("Failed to allocate instructions.\n");
            vkd3d_shader_parser_error(&sm4.p, VKD3D_SHADER_ERROR_TPF_OUT_OF_MEMORY, "Out of memory.");
            vsir_program_cleanup(program);
            return VKD3D_ERROR_OUT_OF_MEMORY;
        }
        ins = &instructions->elements[instructions->count];
        shader_sm4_read_instruction(&sm4, ins);

        if (ins->opcode == VSIR_OP_INVALID)
        {
            WARN("Encountered unrecognized or invalid instruction.\n");
            vsir_program_cleanup(program);
            return VKD3D_ERROR_OUT_OF_MEMORY;
        }
        ++instructions->count;
    }
    if (program->shader_version.type == VKD3D_SHADER_TYPE_HULL
            && !sm4.has_control_point_phase && !sm4.p.failed)
        shader_sm4_validate_default_phase_index_ranges(&sm4);

    if (sm4.p.failed)
    {
        vsir_program_cleanup(program);
        return VKD3D_ERROR_INVALID_SHADER;
    }

    return VKD3D_OK;
}

bool sm4_register_from_semantic_name(const struct vkd3d_shader_version *version,
        const char *semantic_name, bool output, enum vkd3d_shader_register_type *type, bool *has_idx)
{
    unsigned int i;

    static const struct
    {
        const char *semantic;
        bool output;
        enum vkd3d_shader_type shader_type;
        enum vkd3d_shader_register_type type;
        bool has_idx;
    }
    register_table[] =
    {
        {"sv_dispatchthreadid", false, VKD3D_SHADER_TYPE_COMPUTE,  VKD3DSPR_THREADID,         false},
        {"sv_groupid",          false, VKD3D_SHADER_TYPE_COMPUTE,  VKD3DSPR_THREADGROUPID,    false},
        {"sv_groupindex",       false, VKD3D_SHADER_TYPE_COMPUTE,  VKD3DSPR_LOCALTHREADINDEX, false},
        {"sv_groupthreadid",    false, VKD3D_SHADER_TYPE_COMPUTE,  VKD3DSPR_LOCALTHREADID,    false},

        {"sv_domainlocation",   false, VKD3D_SHADER_TYPE_DOMAIN,   VKD3DSPR_TESSCOORD,     false},
        {"sv_primitiveid",      false, VKD3D_SHADER_TYPE_DOMAIN,   VKD3DSPR_PRIMID,        false},

        {"sv_primitiveid",      false, VKD3D_SHADER_TYPE_GEOMETRY, VKD3DSPR_PRIMID,        false},
        {"sv_gsinstanceid",     false, VKD3D_SHADER_TYPE_GEOMETRY, VKD3DSPR_GSINSTID,      false},

        {"sv_outputcontrolpointid", false, VKD3D_SHADER_TYPE_HULL, VKD3DSPR_OUTPOINTID,    false},
        {"sv_primitiveid",          false, VKD3D_SHADER_TYPE_HULL, VKD3DSPR_PRIMID,        false},

        /* Put sv_target in this table, instead of letting it fall through to
         * default varying allocation, so that the register index matches the
         * usage index. */
        {"color",               true,  VKD3D_SHADER_TYPE_PIXEL,    VKD3DSPR_OUTPUT,        true},
        {"depth",               true,  VKD3D_SHADER_TYPE_PIXEL,    VKD3DSPR_DEPTHOUT,      false},
        {"sv_depth",            true,  VKD3D_SHADER_TYPE_PIXEL,    VKD3DSPR_DEPTHOUT,      false},
        {"sv_target",           true,  VKD3D_SHADER_TYPE_PIXEL,    VKD3DSPR_OUTPUT,        true},
        {"sv_coverage",         true,  VKD3D_SHADER_TYPE_PIXEL,    VKD3DSPR_SAMPLEMASK,    false},
    };

    for (i = 0; i < ARRAY_SIZE(register_table); ++i)
    {
        if (!ascii_strcasecmp(semantic_name, register_table[i].semantic)
                && output == register_table[i].output
                && version->type == register_table[i].shader_type)
        {
            if (type)
                *type = register_table[i].type;
            *has_idx = register_table[i].has_idx;
            return true;
        }
    }

    return false;
}

static bool get_tessfactor_sysval_semantic(enum vkd3d_shader_sysval_semantic *semantic,
        enum vkd3d_tessellator_domain domain, uint32_t index)
{
    switch (domain)
    {
        case VKD3D_TESSELLATOR_DOMAIN_LINE:
            if (index == 0)
                *semantic = VKD3D_SHADER_SV_TESS_FACTOR_LINEDEN;
            else if (index == 1)
                *semantic = VKD3D_SHADER_SV_TESS_FACTOR_LINEDET;
            else
                return false;
            return true;

        case VKD3D_TESSELLATOR_DOMAIN_TRIANGLE:
            *semantic = VKD3D_SHADER_SV_TESS_FACTOR_TRIEDGE;
            return index < 3;

        case VKD3D_TESSELLATOR_DOMAIN_QUAD:
            *semantic = VKD3D_SHADER_SV_TESS_FACTOR_QUADEDGE;
            return index < 4;

        default:
            vkd3d_unreachable();
    }
}

static bool get_insidetessfactor_sysval_semantic(enum vkd3d_shader_sysval_semantic *semantic,
        enum vkd3d_tessellator_domain domain, uint32_t index)
{
    switch (domain)
    {
        case VKD3D_TESSELLATOR_DOMAIN_LINE:
            return false;

        case VKD3D_TESSELLATOR_DOMAIN_TRIANGLE:
            *semantic = VKD3D_SHADER_SV_TESS_FACTOR_TRIINT;
            return index == 0;

        case VKD3D_TESSELLATOR_DOMAIN_QUAD:
            *semantic = VKD3D_SHADER_SV_TESS_FACTOR_QUADINT;
            return index < 2;

        default:
            vkd3d_unreachable();
    }
}

bool sm4_sysval_semantic_from_semantic_name(enum vkd3d_shader_sysval_semantic *sysval_semantic,
        const struct vkd3d_shader_version *version, bool semantic_compat_mapping, enum vkd3d_tessellator_domain domain,
        const char *semantic_name, unsigned int semantic_idx, bool output,
        bool is_patch_constant_func, bool is_primitive)
{
    unsigned int i;

    static const struct
    {
        const char *name;
        bool output;
        enum vkd3d_shader_type shader_type;
        enum vkd3d_shader_sysval_semantic semantic;
    }
    semantics[] =
    {
        {"sv_dispatchthreadid",         false, VKD3D_SHADER_TYPE_COMPUTE,   ~0u},
        {"sv_groupid",                  false, VKD3D_SHADER_TYPE_COMPUTE,   ~0u},
        {"sv_groupindex",               false, VKD3D_SHADER_TYPE_COMPUTE,   ~0u},
        {"sv_groupthreadid",            false, VKD3D_SHADER_TYPE_COMPUTE,   ~0u},

        {"sv_domainlocation",           false, VKD3D_SHADER_TYPE_DOMAIN,    ~0u},
        {"sv_position",                 false, VKD3D_SHADER_TYPE_DOMAIN,    VKD3D_SHADER_SV_NONE},
        {"sv_primitiveid",              false, VKD3D_SHADER_TYPE_DOMAIN,    ~0u},
        {"sv_rendertargetarrayindex",   false, VKD3D_SHADER_TYPE_DOMAIN,    VKD3D_SHADER_SV_NONE},
        {"sv_viewportarrayindex",       false, VKD3D_SHADER_TYPE_DOMAIN,    VKD3D_SHADER_SV_NONE},

        {"sv_position",                 true,  VKD3D_SHADER_TYPE_DOMAIN,    VKD3D_SHADER_SV_POSITION},
        {"sv_rendertargetarrayindex",   true,  VKD3D_SHADER_TYPE_DOMAIN,    VKD3D_SHADER_SV_RENDER_TARGET_ARRAY_INDEX},
        {"sv_viewportarrayindex",       true,  VKD3D_SHADER_TYPE_DOMAIN,    VKD3D_SHADER_SV_VIEWPORT_ARRAY_INDEX},

        {"sv_primitiveid",              false, VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SHADER_SV_PRIMITIVE_ID},
        {"sv_gsinstanceid",             false, VKD3D_SHADER_TYPE_GEOMETRY,  ~0u},

        {"position",                    true,  VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SHADER_SV_POSITION},
        {"sv_position",                 true,  VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SHADER_SV_POSITION},
        {"sv_primitiveid",              true,  VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SHADER_SV_PRIMITIVE_ID},
        {"sv_isfrontface",              true,  VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SHADER_SV_IS_FRONT_FACE},
        {"sv_rendertargetarrayindex",   true,  VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SHADER_SV_RENDER_TARGET_ARRAY_INDEX},
        {"sv_viewportarrayindex",       true,  VKD3D_SHADER_TYPE_GEOMETRY,  VKD3D_SHADER_SV_VIEWPORT_ARRAY_INDEX},

        {"sv_outputcontrolpointid",     false, VKD3D_SHADER_TYPE_HULL,      ~0u},
        {"sv_position",                 false, VKD3D_SHADER_TYPE_HULL,      ~0u},
        {"sv_primitiveid",              false, VKD3D_SHADER_TYPE_HULL,      ~0u},

        {"sv_position",                 true,  VKD3D_SHADER_TYPE_HULL,      VKD3D_SHADER_SV_POSITION},
        {"sv_rendertargetarrayindex",   true,  VKD3D_SHADER_TYPE_HULL,      VKD3D_SHADER_SV_RENDER_TARGET_ARRAY_INDEX},
        {"sv_viewportarrayindex",       true,  VKD3D_SHADER_TYPE_HULL,      VKD3D_SHADER_SV_VIEWPORT_ARRAY_INDEX},

        {"position",                    false, VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_POSITION},
        {"sv_position",                 false, VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_POSITION},
        {"sv_primitiveid",              false, VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_PRIMITIVE_ID},
        {"sv_isfrontface",              false, VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_IS_FRONT_FACE},
        {"sv_rendertargetarrayindex",   false, VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_RENDER_TARGET_ARRAY_INDEX},
        {"sv_viewportarrayindex",       false, VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_VIEWPORT_ARRAY_INDEX},
        {"sv_sampleindex",              false, VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_SAMPLE_INDEX},

        {"color",                       true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_TARGET},
        {"depth",                       true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_DEPTH},
        {"sv_target",                   true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_TARGET},
        {"sv_depth",                    true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_DEPTH},
        {"sv_coverage",                 true,  VKD3D_SHADER_TYPE_PIXEL,     VKD3D_SHADER_SV_COVERAGE},

        {"sv_position",                 false, VKD3D_SHADER_TYPE_VERTEX,    VKD3D_SHADER_SV_NONE},
        {"sv_vertexid",                 false, VKD3D_SHADER_TYPE_VERTEX,    VKD3D_SHADER_SV_VERTEX_ID},
        {"sv_instanceid",               false, VKD3D_SHADER_TYPE_VERTEX,    VKD3D_SHADER_SV_INSTANCE_ID},

        {"position",                    true,  VKD3D_SHADER_TYPE_VERTEX,    VKD3D_SHADER_SV_POSITION},
        {"sv_position",                 true,  VKD3D_SHADER_TYPE_VERTEX,    VKD3D_SHADER_SV_POSITION},
        {"sv_rendertargetarrayindex",   true,  VKD3D_SHADER_TYPE_VERTEX,    VKD3D_SHADER_SV_RENDER_TARGET_ARRAY_INDEX},
        {"sv_viewportarrayindex",       true,  VKD3D_SHADER_TYPE_VERTEX,    VKD3D_SHADER_SV_VIEWPORT_ARRAY_INDEX},
    };
    bool has_sv_prefix = !ascii_strncasecmp(semantic_name, "sv_", 3);

    if (is_primitive)
    {
        VKD3D_ASSERT(!output);

        if (!ascii_strcasecmp(semantic_name, "sv_position")
                || (semantic_compat_mapping && !ascii_strcasecmp(semantic_name, "position")))
            *sysval_semantic = VKD3D_SHADER_SV_POSITION;
        else if (!ascii_strcasecmp(semantic_name, "sv_rendertargetarrayindex"))
            *sysval_semantic = VKD3D_SHADER_SV_RENDER_TARGET_ARRAY_INDEX;
        else if (!ascii_strcasecmp(semantic_name, "sv_viewportarrayindex"))
            *sysval_semantic = VKD3D_SHADER_SV_VIEWPORT_ARRAY_INDEX;
        else if (has_sv_prefix)
            return false;
        else
            *sysval_semantic = VKD3D_SHADER_SV_NONE;
        return true;
    }

    if (is_patch_constant_func)
    {
        if (output)
        {
            if (!ascii_strcasecmp(semantic_name, "sv_tessfactor"))
                return get_tessfactor_sysval_semantic(sysval_semantic, domain, semantic_idx);
            if (!ascii_strcasecmp(semantic_name, "sv_insidetessfactor"))
                return get_insidetessfactor_sysval_semantic(sysval_semantic, domain, semantic_idx);
        }
        else
        {
            if (!ascii_strcasecmp(semantic_name, "sv_primitiveid")
                    || !ascii_strcasecmp(semantic_name, "sv_position"))
            {
                *sysval_semantic = ~0u;
                return true;
            }
            return false;
        }
    }
    else if (version->type == VKD3D_SHADER_TYPE_DOMAIN)
    {
        if (!output)
        {
            if (!ascii_strcasecmp(semantic_name, "sv_tessfactor"))
                return get_tessfactor_sysval_semantic(sysval_semantic, domain, semantic_idx);
            if (!ascii_strcasecmp(semantic_name, "sv_insidetessfactor"))
                return get_insidetessfactor_sysval_semantic(sysval_semantic, domain, semantic_idx);
        }
    }

    for (i = 0; i < ARRAY_SIZE(semantics); ++i)
    {
        if (!ascii_strcasecmp(semantic_name, semantics[i].name)
                && output == semantics[i].output
                && (semantic_compat_mapping || has_sv_prefix)
                && version->type == semantics[i].shader_type)
        {
            if (is_patch_constant_func && output && semantics[i].semantic != ~0u)
                *sysval_semantic = VKD3D_SHADER_SV_NONE;
            else
                *sysval_semantic = semantics[i].semantic;
            return true;
        }
    }

    if (has_sv_prefix)
        return false;
    if (!output && version->type == VKD3D_SHADER_TYPE_GEOMETRY)
        return false;

    *sysval_semantic = VKD3D_SHADER_SV_NONE;
    return true;
}

static void add_section(struct tpf_compiler *tpf, uint32_t tag, struct vkd3d_bytecode_buffer *buffer)
{
    /* Native D3DDisassemble() expects at least the sizes of the ISGN and OSGN
     * sections to be aligned. Without this, the sections themselves will be
     * aligned, but their reported sizes won't. */
    size_t size = bytecode_align(buffer);

    dxbc_writer_add_section(&tpf->dxbc, tag, buffer->data, size);

    if (buffer->status < 0)
        tpf->result = buffer->status;
}

static int signature_element_pointer_compare(const void *x, const void *y)
{
    const struct signature_element *e = *(const struct signature_element **)x;
    const struct signature_element *f = *(const struct signature_element **)y;
    int ret;

    if ((ret = vkd3d_u32_compare(e->stream_index, f->stream_index)))
        return ret;
    if ((ret = vkd3d_u32_compare(e->register_index, f->register_index)))
        return ret;
    return vkd3d_u32_compare(e->mask, f->mask);
}

static void tpf_write_signature(struct tpf_compiler *tpf, const struct shader_signature *signature, uint32_t tag)
{
    bool has_minimum_precision = tpf->program->global_flags & VKD3DSGF_ENABLE_MINIMUM_PRECISION;
    const struct vkd3d_shader_version *version = &tpf->program->shader_version;
    const struct signature_element **sorted_elements;
    struct vkd3d_bytecode_buffer buffer = {0};
    bool has_stream_index, output;
    unsigned int i;

    output = tag == TAG_OSGN || (tag == TAG_PCSG && version->type == VKD3D_SHADER_TYPE_HULL);
    if (output && version->type == VKD3D_SHADER_TYPE_GEOMETRY && version->major >= 5)
        tag = TAG_OSG5;
    has_stream_index = tag == TAG_OSG5 || has_minimum_precision;

    put_u32(&buffer, signature->element_count);
    put_u32(&buffer, 8); /* unknown */

    if (!(sorted_elements = vkd3d_calloc(signature->element_count, sizeof(*sorted_elements))))
        return;
    for (i = 0; i < signature->element_count; ++i)
        sorted_elements[i] = &signature->elements[i];
    qsort(sorted_elements, signature->element_count, sizeof(*sorted_elements), signature_element_pointer_compare);

    for (i = 0; i < signature->element_count; ++i)
    {
        const struct signature_element *element = sorted_elements[i];
        enum vkd3d_shader_sysval_semantic sysval;
        uint32_t used_mask = element->used_mask;

        if (output)
            used_mask = 0xf ^ used_mask;

        sysval = element->sysval_semantic;
        if (sysval >= VKD3D_SHADER_SV_TARGET)
            sysval = VKD3D_SHADER_SV_NONE;

        if (has_stream_index)
            put_u32(&buffer, element->stream_index);
        put_u32(&buffer, 0); /* name */
        put_u32(&buffer, element->semantic_index);
        put_u32(&buffer, sysval);
        put_u32(&buffer, element->component_type);
        put_u32(&buffer, element->register_index);
        put_u32(&buffer, vkd3d_make_u16(element->mask, used_mask));
        if (has_minimum_precision)
            put_u32(&buffer, element->min_precision);
    }

    for (i = 0; i < signature->element_count; ++i)
    {
        const struct signature_element *element = sorted_elements[i];
        size_t name_index = 2 + i * 6;
        size_t string_offset;

        if (has_stream_index)
            name_index += i + 1;
        if (has_minimum_precision)
            name_index += i;

        string_offset = put_string(&buffer, element->semantic_name);
        set_u32(&buffer, name_index * sizeof(uint32_t), string_offset);
    }

    if (has_minimum_precision)
    {
        if (tag == TAG_ISGN)
            tag = TAG_ISG1;
        else if (tag == TAG_OSGN || tag == TAG_OSG5)
            tag = TAG_OSG1;
        else if (tag == TAG_PCSG)
            tag = TAG_PSG1;
    }
    add_section(tpf, tag, &buffer);
    vkd3d_free(sorted_elements);
}

static enum vkd3d_sm4_resource_type sm4_resource_dimension(enum vkd3d_shader_resource_type resource_type)
{
    switch (resource_type)
    {
        case VKD3D_SHADER_RESOURCE_TEXTURE_1D:
            return VKD3D_SM4_RESOURCE_TEXTURE_1D;
        case VKD3D_SHADER_RESOURCE_TEXTURE_2D:
            return VKD3D_SM4_RESOURCE_TEXTURE_2D;
        case VKD3D_SHADER_RESOURCE_TEXTURE_3D:
            return VKD3D_SM4_RESOURCE_TEXTURE_3D;
        case VKD3D_SHADER_RESOURCE_TEXTURE_CUBE:
            return VKD3D_SM4_RESOURCE_TEXTURE_CUBE;
        case VKD3D_SHADER_RESOURCE_TEXTURE_1DARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_1DARRAY;
        case VKD3D_SHADER_RESOURCE_TEXTURE_2DARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_2DARRAY;
        case VKD3D_SHADER_RESOURCE_TEXTURE_2DMS:
            return VKD3D_SM4_RESOURCE_TEXTURE_2DMS;
        case VKD3D_SHADER_RESOURCE_TEXTURE_2DMSARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_2DMSARRAY;
        case VKD3D_SHADER_RESOURCE_TEXTURE_CUBEARRAY:
            return VKD3D_SM4_RESOURCE_TEXTURE_CUBEARRAY;
        case VKD3D_SHADER_RESOURCE_BUFFER:
            return VKD3D_SM4_RESOURCE_BUFFER;
        default:
            vkd3d_unreachable();
    }
}

struct sm4_instruction_modifier
{
    enum vkd3d_sm4_instruction_modifier type;

    union
    {
        struct
        {
            int u, v, w;
        } aoffimmi;
    } u;
};

static uint32_t sm4_encode_instruction_modifier(const struct sm4_instruction_modifier *imod)
{
    uint32_t word = 0;

    word |= VKD3D_SM4_MODIFIER_MASK & imod->type;

    switch (imod->type)
    {
        case VKD3D_SM4_MODIFIER_AOFFIMMI:
            VKD3D_ASSERT(-8 <= imod->u.aoffimmi.u && imod->u.aoffimmi.u <= 7);
            VKD3D_ASSERT(-8 <= imod->u.aoffimmi.v && imod->u.aoffimmi.v <= 7);
            VKD3D_ASSERT(-8 <= imod->u.aoffimmi.w && imod->u.aoffimmi.w <= 7);
            word |= ((uint32_t)imod->u.aoffimmi.u & 0xf) << VKD3D_SM4_AOFFIMMI_U_SHIFT;
            word |= ((uint32_t)imod->u.aoffimmi.v & 0xf) << VKD3D_SM4_AOFFIMMI_V_SHIFT;
            word |= ((uint32_t)imod->u.aoffimmi.w & 0xf) << VKD3D_SM4_AOFFIMMI_W_SHIFT;
            break;

        default:
            vkd3d_unreachable();
    }

    return word;
}

struct sm4_instruction
{
    enum vkd3d_sm4_opcode opcode;
    uint32_t extra_bits;

    struct sm4_instruction_modifier modifiers[1];
    unsigned int modifier_count;

    struct vkd3d_shader_dst_param dsts[2];
    unsigned int dst_count;

    struct vkd3d_shader_src_param srcs[5];
    unsigned int src_count;

    unsigned int byte_stride;

    uint32_t idx[3];
    unsigned int idx_count;

    struct vkd3d_shader_src_param idx_srcs[7];
    unsigned int idx_src_count;
};

static unsigned int sm4_get_index_addressing_from_reg(const struct vkd3d_shader_register *reg,
        unsigned int i)
{
    if (reg->idx[i].rel_addr)
    {
        if (reg->idx[i].offset == 0)
            return VKD3D_SM4_ADDRESSING_RELATIVE;
        else
            return VKD3D_SM4_ADDRESSING_RELATIVE | VKD3D_SM4_ADDRESSING_OFFSET;
    }

    return 0;
}

static uint32_t sm4_encode_register(const struct tpf_compiler *tpf, const struct vkd3d_shader_register *reg,
        enum vkd3d_sm4_swizzle_type sm4_swizzle_type, uint32_t sm4_swizzle)
{
    const struct vkd3d_sm4_register_type_info *register_type_info;
    uint32_t sm4_reg_type, sm4_reg_dim;
    uint32_t token = 0;

    register_type_info = get_info_from_vkd3d_register_type(&tpf->lookup, reg->type);
    if (!register_type_info)
    {
        FIXME("Unhandled vkd3d-shader register type %#x.\n", reg->type);
        sm4_reg_type = VKD3D_SM4_RT_TEMP;
        if (sm4_swizzle_type == VKD3D_SM4_SWIZZLE_DEFAULT)
            sm4_swizzle_type = VKD3D_SM4_SWIZZLE_VEC4;
    }
    else
    {
        sm4_reg_type = register_type_info->sm4_type;
        if (sm4_swizzle_type == VKD3D_SM4_SWIZZLE_DEFAULT)
            sm4_swizzle_type = register_type_info->default_src_swizzle_type;
    }
    sm4_reg_dim = sm4_dimension_from_vsir_dimension(reg->dimension);

    token |= sm4_reg_type << VKD3D_SM4_REGISTER_TYPE_SHIFT;
    token |= reg->idx_count << VKD3D_SM4_REGISTER_ORDER_SHIFT;
    token |= sm4_reg_dim << VKD3D_SM4_DIMENSION_SHIFT;
    if (reg->idx_count > 0)
        token |= sm4_get_index_addressing_from_reg(reg, 0) << VKD3D_SM4_ADDRESSING_SHIFT0;
    if (reg->idx_count > 1)
        token |= sm4_get_index_addressing_from_reg(reg, 1) << VKD3D_SM4_ADDRESSING_SHIFT1;
    if (reg->idx_count > 2)
        token |= sm4_get_index_addressing_from_reg(reg, 2) << VKD3D_SM4_ADDRESSING_SHIFT2;

    if (sm4_reg_dim == VKD3D_SM4_DIMENSION_VEC4)
    {
        token |= (uint32_t)sm4_swizzle_type << VKD3D_SM4_SWIZZLE_TYPE_SHIFT;

        switch (sm4_swizzle_type)
        {
            case VKD3D_SM4_SWIZZLE_NONE:
                VKD3D_ASSERT(sm4_swizzle || register_is_constant(reg));
                token |= (sm4_swizzle << VKD3D_SM4_WRITEMASK_SHIFT) & VKD3D_SM4_WRITEMASK_MASK;
                break;

            case VKD3D_SM4_SWIZZLE_VEC4:
                token |= (sm4_swizzle << VKD3D_SM4_SWIZZLE_SHIFT) & VKD3D_SM4_SWIZZLE_MASK;
                break;

            case VKD3D_SM4_SWIZZLE_SCALAR:
                token |= (sm4_swizzle << VKD3D_SM4_SCALAR_DIM_SHIFT) & VKD3D_SM4_SCALAR_DIM_MASK;
                break;

            default:
                vkd3d_unreachable();
        }
    }

    return token;
}

static void sm4_write_register_index(const struct tpf_compiler *tpf, const struct vkd3d_shader_register *reg,
        unsigned int j)
{
    unsigned int addressing = sm4_get_index_addressing_from_reg(reg, j);
    const struct vkd3d_shader_register_index *idx = &reg->idx[j];
    struct vkd3d_bytecode_buffer *buffer = tpf->buffer;
    unsigned int k;

    if (!addressing || (addressing & VKD3D_SM4_ADDRESSING_OFFSET))
        put_u32(buffer, idx->offset);

    if (addressing & VKD3D_SM4_ADDRESSING_RELATIVE)
    {
        const struct vkd3d_shader_src_param *idx_src = idx->rel_addr;
        uint32_t idx_src_token;

        VKD3D_ASSERT(idx_src);
        VKD3D_ASSERT(!idx_src->modifiers);
        VKD3D_ASSERT(idx_src->reg.type != VKD3DSPR_IMMCONST);
        idx_src_token = sm4_encode_register(tpf, &idx_src->reg, VKD3D_SM4_SWIZZLE_SCALAR, idx_src->swizzle);

        put_u32(buffer, idx_src_token);
        for (k = 0; k < idx_src->reg.idx_count; ++k)
        {
            put_u32(buffer, idx_src->reg.idx[k].offset);
            VKD3D_ASSERT(!idx_src->reg.idx[k].rel_addr);
        }
    }
}

static void sm4_write_dst_register(const struct tpf_compiler *tpf, const struct vkd3d_shader_dst_param *dst)
{
    struct vkd3d_bytecode_buffer *buffer = tpf->buffer;
    uint32_t token = 0;
    unsigned int j;

    token = sm4_encode_register(tpf, &dst->reg, VKD3D_SM4_SWIZZLE_NONE, dst->write_mask);
    put_u32(buffer, token);

    for (j = 0; j < dst->reg.idx_count; ++j)
        sm4_write_register_index(tpf, &dst->reg, j);
}

static void sm4_write_src_register(const struct tpf_compiler *tpf, const struct vkd3d_shader_src_param *src)
{
    struct vkd3d_bytecode_buffer *buffer = tpf->buffer;
    uint32_t token = 0, mod_token = 0;
    unsigned int j;

    token = sm4_encode_register(tpf, &src->reg, VKD3D_SM4_SWIZZLE_DEFAULT, swizzle_to_sm4(src->swizzle));

    switch (src->modifiers)
    {
        case VKD3DSPSM_NONE:
            mod_token = VKD3D_SM4_REGISTER_MODIFIER_NONE;
            break;

        case VKD3DSPSM_ABS:
            mod_token = (VKD3D_SM4_REGISTER_MODIFIER_ABS << VKD3D_SM4_REGISTER_MODIFIER_SHIFT)
                    | VKD3D_SM4_EXTENDED_OPERAND_MODIFIER;
            break;

        case VKD3DSPSM_NEG:
            mod_token = (VKD3D_SM4_REGISTER_MODIFIER_NEGATE << VKD3D_SM4_REGISTER_MODIFIER_SHIFT)
                    | VKD3D_SM4_EXTENDED_OPERAND_MODIFIER;
            break;

        case VKD3DSPSM_ABSNEG:
            mod_token = (VKD3D_SM4_REGISTER_MODIFIER_ABS_NEGATE << VKD3D_SM4_REGISTER_MODIFIER_SHIFT)
                    | VKD3D_SM4_EXTENDED_OPERAND_MODIFIER;
            break;

        default:
            ERR("Unhandled register modifier %#x.\n", src->modifiers);
            vkd3d_unreachable();
            break;
    }

    if (src->modifiers)
    {
        token |= VKD3D_SM4_EXTENDED_OPERAND;
        put_u32(buffer, token);
        put_u32(buffer, mod_token);
    }
    else
    {
        put_u32(buffer, token);
    }

    for (j = 0; j < src->reg.idx_count; ++j)
        sm4_write_register_index(tpf, &src->reg, j);

    if (src->reg.type == VKD3DSPR_IMMCONST)
    {
        put_u32(buffer, src->reg.u.immconst_u32[0]);
        if (src->reg.dimension == VSIR_DIMENSION_VEC4)
        {
            put_u32(buffer, src->reg.u.immconst_u32[1]);
            put_u32(buffer, src->reg.u.immconst_u32[2]);
            put_u32(buffer, src->reg.u.immconst_u32[3]);
        }
    }
}

static void sm4_update_stat_counters(const struct tpf_compiler *tpf, const struct sm4_instruction *instr)
{
    enum vkd3d_shader_type shader_type = tpf->program->shader_version.type;
    enum vkd3d_sm4_stat_field stat_field;
    uint32_t opcode;

    ++tpf->stat->fields[VKD3D_STAT_INSTR_COUNT];

    opcode = instr->opcode & VKD3D_SM4_OPCODE_MASK;
    stat_field = get_stat_field_from_sm4_opcode(&tpf->lookup, opcode);

    switch (opcode)
    {
        case VKD3D_SM4_OP_DCL_TEMPS:
            tpf->stat->fields[stat_field] = max(tpf->stat->fields[stat_field], instr->idx[0]);
            break;
        case VKD3D_SM4_OP_DCL_OUTPUT_TOPOLOGY:
        case VKD3D_SM4_OP_DCL_INPUT_PRIMITIVE:
            tpf->stat->fields[stat_field] = (instr->opcode & VKD3D_SM4_PRIMITIVE_TYPE_MASK)
                    >> VKD3D_SM4_PRIMITIVE_TYPE_SHIFT;
            break;
        case VKD3D_SM4_OP_DCL_VERTICES_OUT:
        case VKD3D_SM5_OP_DCL_GS_INSTANCES:
            tpf->stat->fields[stat_field] = instr->idx[0];
            break;
        case VKD3D_SM5_OP_DCL_TESSELLATOR_DOMAIN:
        case VKD3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING:
        case VKD3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE:
            tpf->stat->fields[stat_field] = (instr->opcode & VKD3D_SM5_TESSELLATOR_MASK) >> VKD3D_SM5_TESSELLATOR_SHIFT;
            break;
        case VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT:
        case VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT:
            if ((shader_type == VKD3D_SHADER_TYPE_HULL && opcode == VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT)
                    || (shader_type == VKD3D_SHADER_TYPE_DOMAIN
                            && opcode == VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT))
            {
                tpf->stat->fields[stat_field] = (instr->opcode & VKD3D_SM5_CONTROL_POINT_COUNT_MASK)
                        >> VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT;
            }
            break;
        default:
            ++tpf->stat->fields[stat_field];
    }
}

static void write_sm4_instruction(const struct tpf_compiler *tpf, const struct sm4_instruction *instr)
{
    uint32_t token = instr->opcode | instr->extra_bits;
    struct vkd3d_bytecode_buffer *buffer = tpf->buffer;
    unsigned int size, i, j;
    size_t token_position;

    if (instr->modifier_count > 0)
        token |= VKD3D_SM4_INSTRUCTION_MODIFIER;

    token_position = put_u32(buffer, 0);

    for (i = 0; i < instr->modifier_count; ++i)
    {
        uint32_t modifier_token = sm4_encode_instruction_modifier(&instr->modifiers[i]);

        if (instr->modifier_count > i + 1)
            modifier_token |= VKD3D_SM4_INSTRUCTION_MODIFIER;
        put_u32(buffer, modifier_token);
    }

    for (i = 0; i < instr->dst_count; ++i)
        sm4_write_dst_register(tpf, &instr->dsts[i]);

    for (i = 0; i < instr->src_count; ++i)
        sm4_write_src_register(tpf, &instr->srcs[i]);

    if (instr->byte_stride)
        put_u32(buffer, instr->byte_stride);

    for (j = 0; j < instr->idx_count; ++j)
        put_u32(buffer, instr->idx[j]);

    size = (bytecode_get_size(buffer) - token_position) / sizeof(uint32_t);
    token |= (size << VKD3D_SM4_INSTRUCTION_LENGTH_SHIFT);
    set_u32(buffer, token_position, token);

    sm4_update_stat_counters(tpf, instr);
}

static void tpf_dcl_constant_buffer(const struct tpf_compiler *tpf, const struct vkd3d_shader_instruction *ins)
{
    const struct vkd3d_shader_constant_buffer *cb = &ins->declaration.cb;
    size_t size = (cb->size + 3) / 4;

    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_CONSTANT_BUFFER,

        .srcs[0] = cb->src,
        .src_count = 1,
    };

    if (vkd3d_shader_ver_ge(&tpf->program->shader_version, 5, 1))
    {
        instr.srcs[0].reg.idx[0].offset = cb->src.reg.idx[0].offset;
        instr.srcs[0].reg.idx[1].offset = cb->range.first;
        instr.srcs[0].reg.idx[2].offset = cb->range.last;
        instr.srcs[0].reg.idx_count = 3;

        instr.idx[0] = size;
        instr.idx[1] = cb->range.space;
        instr.idx_count = 2;
    }
    else
    {
        instr.srcs[0].reg.idx[0].offset = cb->range.first;
        instr.srcs[0].reg.idx[1].offset = size;
        instr.srcs[0].reg.idx_count = 2;
    }

    if (ins->flags & VKD3DSI_INDEXED_DYNAMIC)
        instr.extra_bits |= VKD3D_SM4_INDEX_TYPE_MASK;

    write_sm4_instruction(tpf, &instr);
}

static void tpf_dcl_temps(const struct tpf_compiler *tpf, unsigned int count)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_TEMPS,

        .idx = {count},
        .idx_count = 1,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_dcl_indexable_temp(const struct tpf_compiler *tpf, const struct vkd3d_shader_indexable_temp *temp)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_INDEXABLE_TEMP,

        .idx = {temp->register_idx, temp->register_size, temp->component_count},
        .idx_count = 3,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_dcl_semantic(const struct tpf_compiler *tpf, enum vkd3d_sm4_opcode opcode,
        const struct vkd3d_shader_dst_param *dst, uint32_t interpolation_flags)
{
    struct sm4_instruction instr =
    {
        .opcode = opcode,

        .dsts[0] = *dst,
        .dst_count = 1,

        .extra_bits = interpolation_flags << VKD3D_SM4_INTERPOLATION_MODE_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_dcl_siv_semantic(const struct tpf_compiler *tpf, enum vkd3d_sm4_opcode opcode,
        const struct vkd3d_shader_register_semantic *semantic, uint32_t interpolation_flags)
{
    struct sm4_instruction instr =
    {
        .opcode = opcode,

        .dsts[0] = semantic->reg,
        .dst_count = 1,

        .idx[0] = semantic->sysval_semantic,
        .idx_count = 1,

        .extra_bits = interpolation_flags << VKD3D_SM4_INTERPOLATION_MODE_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_dcl_thread_group(const struct tpf_compiler *tpf, const struct vsir_thread_group_size *group_size)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_DCL_THREAD_GROUP,

        .idx = {group_size->x, group_size->y, group_size->z},
        .idx_count = 3,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_dcl_sampler(const struct tpf_compiler *tpf, const struct vkd3d_shader_instruction *ins)
{
    const struct vkd3d_shader_sampler *sampler = &ins->declaration.sampler;
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_SAMPLER,
        .extra_bits = ins->flags << VKD3D_SM4_SAMPLER_MODE_SHIFT,

        .dsts[0].reg.type = VKD3DSPR_SAMPLER,
        .dst_count = 1,
    };

    if (vkd3d_shader_ver_ge(&tpf->program->shader_version, 5, 1))
    {
        instr.dsts[0].reg.idx[0].offset = sampler->src.reg.idx[0].offset;
        instr.dsts[0].reg.idx[1].offset = sampler->range.first;
        instr.dsts[0].reg.idx[2].offset = sampler->range.last;
        instr.dsts[0].reg.idx_count = 3;

        instr.idx[0] = ins->declaration.sampler.range.space;
        instr.idx_count = 1;
    }
    else
    {
        instr.dsts[0].reg.idx[0].offset = sampler->range.first;
        instr.dsts[0].reg.idx_count = 1;
    }

    write_sm4_instruction(tpf, &instr);
}

static uint32_t pack_resource_data_type(const enum vkd3d_data_type *resource_data_type)
{
    unsigned int i, k, type = 0;

    for (k = 0; k < 4; ++k)
    {
        for (i = ARRAY_SIZE(data_type_table) - 1; i < ARRAY_SIZE(data_type_table); --i)
        {
            if (resource_data_type[k] == data_type_table[i])
            {
                type |= i << (4 * k);
                break;
            }
        }
    }

    return type;
}

static void tpf_dcl_texture(const struct tpf_compiler *tpf, const struct vkd3d_shader_instruction *ins)
{
    const struct vkd3d_shader_version *version = &tpf->program->shader_version;
    const struct vkd3d_shader_resource *resource;
    const struct vkd3d_sm4_opcode_info *info;
    struct sm4_instruction instr = {0};
    bool uav;

    info = get_info_from_vsir_opcode(&tpf->lookup, ins->opcode);
    VKD3D_ASSERT(info);

    uav = ins->opcode == VSIR_OP_DCL_UAV_TYPED
            || ins->opcode == VSIR_OP_DCL_UAV_RAW
            || ins->opcode == VSIR_OP_DCL_UAV_STRUCTURED;

    instr.opcode = info->opcode;

    if (ins->opcode == VSIR_OP_DCL || ins->opcode == VSIR_OP_DCL_UAV_TYPED)
    {
        instr.idx[0] = pack_resource_data_type(ins->declaration.semantic.resource_data_type);
        instr.idx_count = 1;
        instr.extra_bits |= ins->declaration.semantic.sample_count << VKD3D_SM4_RESOURCE_SAMPLE_COUNT_SHIFT;
        resource = &ins->declaration.semantic.resource;
    }
    else if (ins->opcode == VSIR_OP_DCL_RESOURCE_RAW || ins->opcode == VSIR_OP_DCL_UAV_RAW)
    {
        resource = &ins->declaration.raw_resource.resource;
    }
    else
    {
        instr.byte_stride = ins->declaration.structured_resource.byte_stride;
        resource = &ins->declaration.structured_resource.resource;
    }

    instr.dsts[0] = resource->reg;
    instr.dst_count = 1;

    if (vkd3d_shader_ver_ge(version, 5, 1))
    {
        instr.dsts[0].reg.idx[0].offset = resource->reg.reg.idx[0].offset;
        instr.dsts[0].reg.idx[1].offset = resource->range.first;
        instr.dsts[0].reg.idx[2].offset = resource->range.last;
        instr.dsts[0].reg.idx_count = 3;

        instr.idx[instr.idx_count++] = resource->range.space;
    }
    else
    {
        instr.dsts[0].reg.idx[0].offset = resource->range.first;
        instr.dsts[0].reg.idx_count = 1;
    }

    if (uav)
        instr.extra_bits |= ins->flags << VKD3D_SM5_UAV_FLAGS_SHIFT;

    instr.extra_bits |= (sm4_resource_dimension(ins->resource_type) << VKD3D_SM4_RESOURCE_TYPE_SHIFT);

    write_sm4_instruction(tpf, &instr);
}

static void write_sm4_dcl_global_flags(const struct tpf_compiler *tpf, uint32_t flags)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_GLOBAL_FLAGS,
        .extra_bits = flags << VKD3D_SM4_GLOBAL_FLAGS_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_hs_decls(const struct tpf_compiler *tpf)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_HS_DECLS,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_input_control_point_count(const struct tpf_compiler *tpf, const uint32_t count)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT,
        .extra_bits = count << VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_output_control_point_count(const struct tpf_compiler *tpf, const uint32_t count)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT,
        .extra_bits = count << VKD3D_SM5_CONTROL_POINT_COUNT_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_tessellator_domain(const struct tpf_compiler *tpf, enum vkd3d_tessellator_domain domain)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_DCL_TESSELLATOR_DOMAIN,
        .extra_bits = domain << VKD3D_SM5_TESSELLATOR_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_tessellator_partitioning(const struct tpf_compiler *tpf,
        enum vkd3d_shader_tessellator_partitioning partitioning)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING,
        .extra_bits = partitioning << VKD3D_SM5_TESSELLATOR_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_tessellator_output_primitive(const struct tpf_compiler *tpf,
        enum vkd3d_shader_tessellator_output_primitive output_primitive)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE,
        .extra_bits = output_primitive << VKD3D_SM5_TESSELLATOR_SHIFT,
    };

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_input_primitive(const struct tpf_compiler *tpf, enum vkd3d_primitive_type input_primitive,
        unsigned int patch_vertex_count)
{
    enum vkd3d_sm4_input_primitive_type sm4_input_primitive;
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_INPUT_PRIMITIVE,
    };

    if (input_primitive == VKD3D_PT_PATCH)
    {
        VKD3D_ASSERT(patch_vertex_count >= 1 && patch_vertex_count <= 32);
        sm4_input_primitive = VKD3D_SM5_INPUT_PT_PATCH1 + patch_vertex_count - 1;
    }
    else
    {
        VKD3D_ASSERT(input_primitive < ARRAY_SIZE(sm4_input_primitive_type_table));
        sm4_input_primitive = sm4_input_primitive_type_table[input_primitive];
    }

    instr.extra_bits = sm4_input_primitive << VKD3D_SM4_PRIMITIVE_TYPE_SHIFT;

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_output_topology(const struct tpf_compiler *tpf, enum vkd3d_primitive_type output_topology)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_OUTPUT_TOPOLOGY,
    };

    VKD3D_ASSERT(output_topology < ARRAY_SIZE(sm4_output_primitive_type_table));
    instr.extra_bits = sm4_output_primitive_type_table[output_topology] << VKD3D_SM4_PRIMITIVE_TYPE_SHIFT;

    write_sm4_instruction(tpf, &instr);
}

static void tpf_write_dcl_vertices_out(const struct tpf_compiler *tpf, unsigned int count)
{
    struct sm4_instruction instr =
    {
        .opcode = VKD3D_SM4_OP_DCL_VERTICES_OUT,

        .idx = {count},
        .idx_count = 1,
    };

    write_sm4_instruction(tpf, &instr);
}

/* Descriptor registers are stored in shader model 5.1 format regardless
 * of the program's version. Convert them to the 4.0 format if necessary. */
static void rewrite_descriptor_register(const struct tpf_compiler *tpf, struct vkd3d_shader_register *reg)
{
    if (vkd3d_shader_ver_ge(&tpf->program->shader_version, 5, 1))
        return;

    switch (reg->type)
    {
        case VKD3DSPR_CONSTBUFFER:
            reg->idx[0] = reg->idx[1];
            reg->idx[1] = reg->idx[2];
            reg->idx_count = 2;
            break;

        case VKD3DSPR_RESOURCE:
        case VKD3DSPR_SAMPLER:
        case VKD3DSPR_UAV:
            reg->idx[0] = reg->idx[1];
            reg->idx_count = 1;
            break;

        default:
            break;
    }

    for (unsigned int i = 0; i < reg->idx_count; ++i)
    {
        if (reg->idx[i].rel_addr)
            rewrite_descriptor_register(tpf, &reg->idx[i].rel_addr->reg);
    }
}

static void tpf_simple_instruction(struct tpf_compiler *tpf, const struct vkd3d_shader_instruction *ins)
{
    struct sm4_instruction_modifier *modifier;
    const struct vkd3d_sm4_opcode_info *info;
    struct sm4_instruction instr = {0};
    unsigned int dst_count, src_count;

    info = get_info_from_vsir_opcode(&tpf->lookup, ins->opcode);
    VKD3D_ASSERT(info);

    dst_count = opcode_info_get_dst_count(info);
    src_count = opcode_info_get_src_count(info);

    if (ins->dst_count != dst_count)
    {
        ERR("Invalid destination count %u for vsir instruction %#x (expected %u).\n",
                ins->dst_count, ins->opcode, dst_count);
        tpf->result = VKD3D_ERROR_INVALID_SHADER;
        return;
    }
    if (ins->src_count != src_count)
    {
        ERR("Invalid source count %u for vsir instruction %#x (expected %u).\n",
                ins->src_count, ins->opcode, src_count);
        tpf->result = VKD3D_ERROR_INVALID_SHADER;
        return;
    }

    instr.opcode = info->opcode;
    instr.extra_bits = ins->flags << VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT;
    instr.dst_count = ins->dst_count;
    instr.src_count = ins->src_count;

    for (unsigned int i = 0; i < ins->dst_count; ++i)
    {
        instr.dsts[i] = ins->dst[i];
        rewrite_descriptor_register(tpf, &instr.dsts[i].reg);

        if (instr.dsts[i].modifiers & VKD3DSPDM_SATURATE)
        {
            /* For vsir SATURATE is a dst modifier, while for tpf it is an instruction flag. */
            VKD3D_ASSERT(ins->dst_count == 1);
            instr.dsts[i].modifiers &= ~VKD3DSPDM_SATURATE;
            instr.extra_bits |= VKD3D_SM4_INSTRUCTION_FLAG_SATURATE << VKD3D_SM4_INSTRUCTION_FLAGS_SHIFT;
        }
    }
    for (unsigned int i = 0; i < ins->src_count; ++i)
    {
        instr.srcs[i] = ins->src[i];
        rewrite_descriptor_register(tpf, &instr.srcs[i].reg);
    }

    if (ins->texel_offset.u || ins->texel_offset.v || ins->texel_offset.w)
    {
        VKD3D_ASSERT(instr.modifier_count < ARRAY_SIZE(instr.modifiers));
        modifier = &instr.modifiers[instr.modifier_count++];
        modifier->type = VKD3D_SM4_MODIFIER_AOFFIMMI;
        modifier->u.aoffimmi.u = ins->texel_offset.u;
        modifier->u.aoffimmi.v = ins->texel_offset.v;
        modifier->u.aoffimmi.w = ins->texel_offset.w;
    }

    if (info->is_conditional_op)
    {
        if (ins->flags == VKD3D_SHADER_CONDITIONAL_OP_NZ)
            instr.extra_bits = VKD3D_SM4_CONDITIONAL_NZ;
    }

    write_sm4_instruction(tpf, &instr);
}

static void tpf_handle_instruction(struct tpf_compiler *tpf, const struct vkd3d_shader_instruction *ins)
{
    switch (ins->opcode)
    {
        case VSIR_OP_DCL_CONSTANT_BUFFER:
            tpf_dcl_constant_buffer(tpf, ins);
            break;

        case VSIR_OP_DCL_TEMPS:
            tpf_dcl_temps(tpf, ins->declaration.count);
            break;

        case VSIR_OP_DCL_INDEXABLE_TEMP:
            tpf_dcl_indexable_temp(tpf, &ins->declaration.indexable_temp);
            break;

        case VSIR_OP_DCL_INPUT:
            tpf_dcl_semantic(tpf, VKD3D_SM4_OP_DCL_INPUT, &ins->declaration.dst, 0);
            break;

        case VSIR_OP_DCL_INPUT_PS:
            tpf_dcl_semantic(tpf, VKD3D_SM4_OP_DCL_INPUT_PS, &ins->declaration.dst, ins->flags);
            break;

        case VSIR_OP_DCL_INPUT_PS_SGV:
            tpf_dcl_siv_semantic(tpf, VKD3D_SM4_OP_DCL_INPUT_PS_SGV, &ins->declaration.register_semantic, 0);
            break;

        case VSIR_OP_DCL_INPUT_PS_SIV:
            tpf_dcl_siv_semantic(tpf, VKD3D_SM4_OP_DCL_INPUT_PS_SIV, &ins->declaration.register_semantic, ins->flags);
            break;

        case VSIR_OP_DCL_INPUT_SGV:
            tpf_dcl_siv_semantic(tpf, VKD3D_SM4_OP_DCL_INPUT_SGV, &ins->declaration.register_semantic, 0);
            break;

        case VSIR_OP_DCL_INPUT_SIV:
            tpf_dcl_siv_semantic(tpf, VKD3D_SM4_OP_DCL_INPUT_SIV, &ins->declaration.register_semantic, 0);
            break;

        case VSIR_OP_DCL_OUTPUT:
            tpf_dcl_semantic(tpf, VKD3D_SM4_OP_DCL_OUTPUT, &ins->declaration.dst, 0);
            break;

        case VSIR_OP_DCL_OUTPUT_SGV:
            tpf_dcl_siv_semantic(tpf, VKD3D_SM4_OP_DCL_OUTPUT_SGV, &ins->declaration.register_semantic, 0);
            break;

        case VSIR_OP_DCL_OUTPUT_SIV:
            tpf_dcl_siv_semantic(tpf, VKD3D_SM4_OP_DCL_OUTPUT_SIV, &ins->declaration.register_semantic, 0);
            break;

        case VSIR_OP_DCL_SAMPLER:
            tpf_dcl_sampler(tpf, ins);
            break;

        case VSIR_OP_DCL:
        case VSIR_OP_DCL_RESOURCE_RAW:
        case VSIR_OP_DCL_UAV_RAW:
        case VSIR_OP_DCL_UAV_STRUCTURED:
        case VSIR_OP_DCL_UAV_TYPED:
            tpf_dcl_texture(tpf, ins);
            break;

        case VSIR_OP_ADD:
        case VSIR_OP_ATOMIC_AND:
        case VSIR_OP_ATOMIC_CMP_STORE:
        case VSIR_OP_ATOMIC_IADD:
        case VSIR_OP_ATOMIC_IMAX:
        case VSIR_OP_ATOMIC_IMIN:
        case VSIR_OP_ATOMIC_UMAX:
        case VSIR_OP_ATOMIC_UMIN:
        case VSIR_OP_ATOMIC_OR:
        case VSIR_OP_ATOMIC_XOR:
        case VSIR_OP_AND:
        case VSIR_OP_BREAK:
        case VSIR_OP_CASE:
        case VSIR_OP_CONTINUE:
        case VSIR_OP_CUT:
        case VSIR_OP_CUT_STREAM:
        case VSIR_OP_DCL_STREAM:
        case VSIR_OP_DEFAULT:
        case VSIR_OP_DISCARD:
        case VSIR_OP_DIV:
        case VSIR_OP_DP2:
        case VSIR_OP_DP3:
        case VSIR_OP_DP4:
        case VSIR_OP_DSX:
        case VSIR_OP_DSX_COARSE:
        case VSIR_OP_DSX_FINE:
        case VSIR_OP_DSY:
        case VSIR_OP_DSY_COARSE:
        case VSIR_OP_DSY_FINE:
        case VSIR_OP_ELSE:
        case VSIR_OP_EMIT:
        case VSIR_OP_EMIT_STREAM:
        case VSIR_OP_ENDIF:
        case VSIR_OP_ENDLOOP:
        case VSIR_OP_ENDSWITCH:
        case VSIR_OP_EQO:
        case VSIR_OP_EXP:
        case VSIR_OP_F16TOF32:
        case VSIR_OP_F32TOF16:
        case VSIR_OP_FRC:
        case VSIR_OP_FTOI:
        case VSIR_OP_FTOU:
        case VSIR_OP_GATHER4:
        case VSIR_OP_GATHER4_PO:
        case VSIR_OP_GATHER4_C:
        case VSIR_OP_GATHER4_PO_C:
        case VSIR_OP_GEO:
        case VSIR_OP_HS_CONTROL_POINT_PHASE:
        case VSIR_OP_HS_FORK_PHASE:
        case VSIR_OP_IADD:
        case VSIR_OP_IEQ:
        case VSIR_OP_IF:
        case VSIR_OP_IGE:
        case VSIR_OP_ILT:
        case VSIR_OP_IMAD:
        case VSIR_OP_IMAX:
        case VSIR_OP_IMIN:
        case VSIR_OP_IMM_ATOMIC_AND:
        case VSIR_OP_IMM_ATOMIC_CMP_EXCH:
        case VSIR_OP_IMM_ATOMIC_EXCH:
        case VSIR_OP_IMM_ATOMIC_IADD:
        case VSIR_OP_IMM_ATOMIC_IMAX:
        case VSIR_OP_IMM_ATOMIC_IMIN:
        case VSIR_OP_IMM_ATOMIC_UMAX:
        case VSIR_OP_IMM_ATOMIC_UMIN:
        case VSIR_OP_IMM_ATOMIC_OR:
        case VSIR_OP_IMM_ATOMIC_XOR:
        case VSIR_OP_SYNC:
        case VSIR_OP_IMUL:
        case VSIR_OP_INE:
        case VSIR_OP_INEG:
        case VSIR_OP_ISHL:
        case VSIR_OP_ISHR:
        case VSIR_OP_ITOF:
        case VSIR_OP_LD:
        case VSIR_OP_LD2DMS:
        case VSIR_OP_LD_RAW:
        case VSIR_OP_LD_UAV_TYPED:
        case VSIR_OP_LOG:
        case VSIR_OP_LOOP:
        case VSIR_OP_LTO:
        case VSIR_OP_MAD:
        case VSIR_OP_MAX:
        case VSIR_OP_MIN:
        case VSIR_OP_MOV:
        case VSIR_OP_MOVC:
        case VSIR_OP_MUL:
        case VSIR_OP_NEU:
        case VSIR_OP_NOT:
        case VSIR_OP_OR:
        case VSIR_OP_RCP:
        case VSIR_OP_RESINFO:
        case VSIR_OP_RET:
        case VSIR_OP_ROUND_NE:
        case VSIR_OP_ROUND_NI:
        case VSIR_OP_ROUND_PI:
        case VSIR_OP_ROUND_Z:
        case VSIR_OP_RSQ:
        case VSIR_OP_SAMPLE:
        case VSIR_OP_SAMPLE_B:
        case VSIR_OP_SAMPLE_C:
        case VSIR_OP_SAMPLE_C_LZ:
        case VSIR_OP_SAMPLE_GRAD:
        case VSIR_OP_SAMPLE_INFO:
        case VSIR_OP_SAMPLE_LOD:
        case VSIR_OP_SINCOS:
        case VSIR_OP_SQRT:
        case VSIR_OP_STORE_RAW:
        case VSIR_OP_STORE_UAV_TYPED:
        case VSIR_OP_SWITCH:
        case VSIR_OP_UDIV:
        case VSIR_OP_UGE:
        case VSIR_OP_ULT:
        case VSIR_OP_UMAX:
        case VSIR_OP_UMIN:
        case VSIR_OP_USHR:
        case VSIR_OP_UTOF:
        case VSIR_OP_XOR:
            tpf_simple_instruction(tpf, ins);
            break;

        default:
            vkd3d_unreachable();
            break;
    }
}

static void tpf_write_program(struct tpf_compiler *tpf, const struct vsir_program *program)
{
    unsigned int i;

    if (tpf->program->shader_version.type == VKD3D_SHADER_TYPE_COMPUTE)
        tpf_dcl_thread_group(tpf, &tpf->program->thread_group_size);

    for (i = 0; i < program->instructions.count; ++i)
        tpf_handle_instruction(tpf, &program->instructions.elements[i]);
}

static void tpf_write_shdr(struct tpf_compiler *tpf)
{
    const struct vsir_program *program = tpf->program;
    const struct vkd3d_shader_version *version;
    struct vkd3d_bytecode_buffer buffer = {0};
    size_t token_count_position;

    static const uint16_t shader_types[VKD3D_SHADER_TYPE_COUNT] =
    {
        VKD3D_SM4_PS,
        VKD3D_SM4_VS,
        VKD3D_SM4_GS,
        VKD3D_SM5_HS,
        VKD3D_SM5_DS,
        VKD3D_SM5_CS,
        0, /* EFFECT */
        0, /* TEXTURE */
        VKD3D_SM4_LIB,
    };

    tpf->buffer = &buffer;

    version = &program->shader_version;
    put_u32(&buffer, vkd3d_make_u32((version->major << 4) | version->minor, shader_types[version->type]));
    token_count_position = put_u32(&buffer, 0);

    if (program->global_flags)
        write_sm4_dcl_global_flags(tpf, program->global_flags);

    if (version->type == VKD3D_SHADER_TYPE_HULL)
    {
        tpf_write_hs_decls(tpf);

        tpf_write_dcl_input_control_point_count(tpf, program->input_control_point_count);
        tpf_write_dcl_output_control_point_count(tpf, program->output_control_point_count);
        tpf_write_dcl_tessellator_domain(tpf, program->tess_domain);
        tpf_write_dcl_tessellator_partitioning(tpf, program->tess_partitioning);
        tpf_write_dcl_tessellator_output_primitive(tpf, program->tess_output_primitive);
    }
    else if (version->type == VKD3D_SHADER_TYPE_DOMAIN)
    {
        tpf_write_dcl_input_control_point_count(tpf, program->input_control_point_count);
        tpf_write_dcl_tessellator_domain(tpf, program->tess_domain);
    }
    else if (version->type == VKD3D_SHADER_TYPE_GEOMETRY)
    {
        tpf_write_dcl_input_primitive(tpf, program->input_primitive, program->input_control_point_count);
        if (program->output_topology != VKD3D_PT_UNDEFINED)
            tpf_write_dcl_output_topology(tpf, program->output_topology);
        tpf_write_dcl_vertices_out(tpf, program->vertices_out_count);
    }

    tpf_write_program(tpf, program);

    set_u32(&buffer, token_count_position, bytecode_get_size(&buffer) / sizeof(uint32_t));

    add_section(tpf, TAG_SHDR, &buffer);
    tpf->buffer = NULL;
}

static void tpf_write_sfi0(struct tpf_compiler *tpf)
{
    uint64_t *flags;

    flags = vkd3d_calloc(1, sizeof(*flags));

    if (tpf->program->features.rovs)
        *flags |= DXBC_SFI0_REQUIRES_ROVS;

    if (tpf->program->global_flags & VKD3DSGF_ENABLE_MINIMUM_PRECISION)
        *flags |= DXBC_SFI0_REQUIRES_MINIMUM_PRECISION;

    /* FIXME: We also emit code that should require UAVS_AT_EVERY_STAGE,
     * STENCIL_REF, and TYPED_UAV_LOAD_ADDITIONAL_FORMATS. */

    if (*flags)
        dxbc_writer_add_section(&tpf->dxbc, TAG_SFI0, flags, sizeof(*flags));
    else
        vkd3d_free(flags);
}

static void tpf_write_stat(struct tpf_compiler *tpf)
{
    struct vkd3d_bytecode_buffer buffer = {0};
    const struct sm4_stat *stat = tpf->stat;

    put_u32(&buffer, stat->fields[VKD3D_STAT_INSTR_COUNT]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_TEMPS]);
    put_u32(&buffer, 0); /* Def count */
    put_u32(&buffer, 0); /* DCL count */
    put_u32(&buffer, stat->fields[VKD3D_STAT_FLOAT]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_INT]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_UINT]);
    put_u32(&buffer, 0); /* Static flow control count */
    put_u32(&buffer, 0); /* Dynamic flow control count */
    put_u32(&buffer, 0); /* Macro instruction count */
    put_u32(&buffer, 0); /* Temp array count */
    put_u32(&buffer, 0); /* Array instr count */
    put_u32(&buffer, stat->fields[VKD3D_STAT_CUT]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_EMIT]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_SAMPLE]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_LOAD]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_SAMPLE_C]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_SAMPLE_BIAS]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_SAMPLE_GRAD]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_MOV]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_MOVC]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_CONV]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_BITWISE]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_DCL_INPUT_PRIMITIVE]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_DCL_OUTPUT_TOPOLOGY]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_DCL_VERTICES_OUT]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_GATHER]);
    put_u32(&buffer, stat->fields[VKD3D_STAT_LOD]);
    put_u32(&buffer, 0); /* Sample frequency */

    if (vkd3d_shader_ver_ge(&tpf->program->shader_version, 5, 0))
    {
        put_u32(&buffer, stat->fields[VKD3D_STAT_DCL_GS_INSTANCES]);
        put_u32(&buffer, stat->fields[VKD3D_STAT_TESS_CONTROL_POINT_COUNT]);
        put_u32(&buffer, stat->fields[VKD3D_STAT_TESS_OUTPUT_PRIMITIVE]);
        put_u32(&buffer, stat->fields[VKD3D_STAT_TESS_PARTITIONING]);
        put_u32(&buffer, stat->fields[VKD3D_STAT_TESS_DOMAIN]);
        put_u32(&buffer, stat->fields[VKD3D_STAT_BARRIER]);
        put_u32(&buffer, stat->fields[VKD3D_STAT_ATOMIC]);
        put_u32(&buffer, stat->fields[VKD3D_STAT_STORE]);
    }

    add_section(tpf, TAG_STAT, &buffer);
}

static void tpf_write_section(struct tpf_compiler *tpf, uint32_t tag, const struct vkd3d_shader_code *code)
{
    struct vkd3d_bytecode_buffer buffer = {0};

    bytecode_put_bytes(&buffer, code->code, code->size);
    add_section(tpf, tag, &buffer);
}

int tpf_compile(struct vsir_program *program, uint64_t config_flags, const struct vkd3d_shader_code *rdef,
        struct vkd3d_shader_code *out, struct vkd3d_shader_message_context *message_context)
{
    enum vkd3d_shader_type shader_type = program->shader_version.type;
    struct tpf_compiler tpf = {0};
    struct sm4_stat stat = {0};
    size_t i;
    int ret;

    if ((ret = vsir_allocate_temp_registers(program, message_context)))
        return ret;

    tpf.program = program;
    tpf.buffer = NULL;
    tpf.stat = &stat;
    init_sm4_lookup_tables(&tpf.lookup);
    dxbc_writer_init(&tpf.dxbc);

    tpf_write_signature(&tpf, &program->input_signature, TAG_ISGN);
    tpf_write_signature(&tpf, &program->output_signature, TAG_OSGN);
    if (shader_type == VKD3D_SHADER_TYPE_HULL || shader_type == VKD3D_SHADER_TYPE_DOMAIN)
        tpf_write_signature(&tpf, &program->patch_constant_signature, TAG_PCSG);
    tpf_write_section(&tpf, TAG_RDEF, rdef);
    tpf_write_shdr(&tpf);
    tpf_write_sfi0(&tpf);
    tpf_write_stat(&tpf);

    ret = VKD3D_OK;
    if (tpf.result)
        ret = tpf.result;

    if (!ret)
        ret = dxbc_writer_write(&tpf.dxbc, out);
    for (i = 0; i < tpf.dxbc.section_count; ++i)
        vkd3d_shader_free_shader_code(&tpf.dxbc.sections[i].data);
    return ret;
}
