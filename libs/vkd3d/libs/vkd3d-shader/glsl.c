/*
 * Copyright 2021 Atharva Nimbalkar
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

struct glsl_src
{
    struct vkd3d_string_buffer *str;
};

struct glsl_dst
{
    const struct vkd3d_shader_dst_param *vsir;
    struct vkd3d_string_buffer *register_name;
    struct vkd3d_string_buffer *mask;
};

struct vkd3d_glsl_generator
{
    struct vsir_program *program;
    struct vkd3d_string_buffer_cache string_buffers;
    struct vkd3d_string_buffer *buffer;
    struct vkd3d_shader_location location;
    struct vkd3d_shader_message_context *message_context;
    unsigned int indent;
    const char *prefix;
    bool failed;

    struct shader_limits
    {
        unsigned int input_count;
        unsigned int output_count;
    } limits;
    bool interstage_input;
    bool interstage_output;

    const struct vkd3d_shader_interface_info *interface_info;
    const struct vkd3d_shader_descriptor_offset_info *offset_info;
    const struct vkd3d_shader_scan_descriptor_info1 *descriptor_info;
};

static void VKD3D_PRINTF_FUNC(3, 4) vkd3d_glsl_compiler_error(
        struct vkd3d_glsl_generator *generator,
        enum vkd3d_shader_error error, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vkd3d_shader_verror(generator->message_context, &generator->location, error, fmt, args);
    va_end(args);
    generator->failed = true;
}

static const char *shader_glsl_get_prefix(enum vkd3d_shader_type type)
{
    switch (type)
    {
        case VKD3D_SHADER_TYPE_VERTEX:
            return "vs";
        case VKD3D_SHADER_TYPE_HULL:
            return "hs";
        case VKD3D_SHADER_TYPE_DOMAIN:
            return "ds";
        case VKD3D_SHADER_TYPE_GEOMETRY:
            return "gs";
        case VKD3D_SHADER_TYPE_PIXEL:
            return "ps";
        case VKD3D_SHADER_TYPE_COMPUTE:
            return "cs";
        default:
            return NULL;
    }
}

static void shader_glsl_print_indent(struct vkd3d_string_buffer *buffer, unsigned int indent)
{
    vkd3d_string_buffer_printf(buffer, "%*s", 4 * indent, "");
}

static void shader_glsl_print_register_name(struct vkd3d_string_buffer *buffer,
        struct vkd3d_glsl_generator *gen, const struct vkd3d_shader_register *reg)
{
    switch (reg->type)
    {
        case VKD3DSPR_TEMP:
            vkd3d_string_buffer_printf(buffer, "r[%u]", reg->idx[0].offset);
            break;

        case VKD3DSPR_INPUT:
            if (reg->idx_count != 1)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled input register index count %u.", reg->idx_count);
                vkd3d_string_buffer_printf(buffer, "<unhandled register %#x>", reg->type);
                break;
            }
            if (reg->idx[0].rel_addr)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled input register indirect addressing.");
                vkd3d_string_buffer_printf(buffer, "<unhandled register %#x>", reg->type);
                break;
            }
            vkd3d_string_buffer_printf(buffer, "%s_in[%u]", gen->prefix, reg->idx[0].offset);
            break;

        case VKD3DSPR_OUTPUT:
            if (reg->idx_count != 1)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled output register index count %u.", reg->idx_count);
                vkd3d_string_buffer_printf(buffer, "<unhandled register %#x>", reg->type);
                break;
            }
            if (reg->idx[0].rel_addr)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled output register indirect addressing.");
                vkd3d_string_buffer_printf(buffer, "<unhandled register %#x>", reg->type);
                break;
            }
            vkd3d_string_buffer_printf(buffer, "%s_out[%u]", gen->prefix, reg->idx[0].offset);
            break;

        case VKD3DSPR_IMMCONST:
            switch (reg->dimension)
            {
                case VSIR_DIMENSION_SCALAR:
                    vkd3d_string_buffer_printf(buffer, "%#xu", reg->u.immconst_u32[0]);
                    break;

                case VSIR_DIMENSION_VEC4:
                    vkd3d_string_buffer_printf(buffer, "uvec4(%#xu, %#xu, %#xu, %#xu)",
                            reg->u.immconst_u32[0], reg->u.immconst_u32[1],
                            reg->u.immconst_u32[2], reg->u.immconst_u32[3]);
                    break;

                default:
                    vkd3d_string_buffer_printf(buffer, "<unhandled_dimension %#x>", reg->dimension);
                    vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                            "Internal compiler error: Unhandled dimension %#x.", reg->dimension);
                    break;
            }
            break;

        case VKD3DSPR_CONSTBUFFER:
            if (reg->idx_count != 3)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled constant buffer register index count %u.", reg->idx_count);
                vkd3d_string_buffer_printf(buffer, "<unhandled register %#x>", reg->type);
                break;
            }
            if (reg->idx[0].rel_addr || reg->idx[2].rel_addr)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled constant buffer register indirect addressing.");
                vkd3d_string_buffer_printf(buffer, "<unhandled register %#x>", reg->type);
                break;
            }
            vkd3d_string_buffer_printf(buffer, "%s_cb_%u[%u]",
                    gen->prefix, reg->idx[0].offset, reg->idx[2].offset);
            break;

        default:
            vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                    "Internal compiler error: Unhandled register type %#x.", reg->type);
            vkd3d_string_buffer_printf(buffer, "<unrecognised register %#x>", reg->type);
            break;
    }
}

static void shader_glsl_print_swizzle(struct vkd3d_string_buffer *buffer, uint32_t swizzle, uint32_t mask)
{
    const char swizzle_chars[] = "xyzw";
    unsigned int i;

    vkd3d_string_buffer_printf(buffer, ".");
    for (i = 0; i < VKD3D_VEC4_SIZE; ++i)
    {
        if (mask & (VKD3DSP_WRITEMASK_0 << i))
            vkd3d_string_buffer_printf(buffer, "%c", swizzle_chars[vsir_swizzle_get_component(swizzle, i)]);
    }
}

static void shader_glsl_print_write_mask(struct vkd3d_string_buffer *buffer, uint32_t write_mask)
{
    vkd3d_string_buffer_printf(buffer, ".");
    if (write_mask & VKD3DSP_WRITEMASK_0)
        vkd3d_string_buffer_printf(buffer, "x");
    if (write_mask & VKD3DSP_WRITEMASK_1)
        vkd3d_string_buffer_printf(buffer, "y");
    if (write_mask & VKD3DSP_WRITEMASK_2)
        vkd3d_string_buffer_printf(buffer, "z");
    if (write_mask & VKD3DSP_WRITEMASK_3)
        vkd3d_string_buffer_printf(buffer, "w");
}

static void glsl_src_cleanup(struct glsl_src *src, struct vkd3d_string_buffer_cache *cache)
{
    vkd3d_string_buffer_release(cache, src->str);
}

static void shader_glsl_print_bitcast(struct vkd3d_string_buffer *dst, struct vkd3d_glsl_generator *gen,
        const char *src, enum vkd3d_data_type dst_data_type, enum vkd3d_data_type src_data_type, unsigned int size)
{
    if (dst_data_type == VKD3D_DATA_UNORM || dst_data_type == VKD3D_DATA_SNORM)
        dst_data_type = VKD3D_DATA_FLOAT;
    if (src_data_type == VKD3D_DATA_UNORM || src_data_type == VKD3D_DATA_SNORM)
        src_data_type = VKD3D_DATA_FLOAT;

    if (dst_data_type == src_data_type)
    {
        vkd3d_string_buffer_printf(dst, "%s", src);
        return;
    }

    if (src_data_type == VKD3D_DATA_FLOAT)
    {
        switch (dst_data_type)
        {
            case VKD3D_DATA_INT:
                vkd3d_string_buffer_printf(dst, "floatBitsToInt(%s)", src);
                return;
            case VKD3D_DATA_UINT:
                vkd3d_string_buffer_printf(dst, "floatBitsToUint(%s)", src);
                return;
            default:
                break;
        }
    }

    if (src_data_type == VKD3D_DATA_UINT)
    {
        switch (dst_data_type)
        {
            case VKD3D_DATA_FLOAT:
                vkd3d_string_buffer_printf(dst, "uintBitsToFloat(%s)", src);
                return;
            case VKD3D_DATA_INT:
                if (size == 1)
                    vkd3d_string_buffer_printf(dst, "int(%s)", src);
                else
                    vkd3d_string_buffer_printf(dst, "ivec%u(%s)", size, src);
                return;
            default:
                break;
        }
    }

    vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
            "Internal compiler error: Unhandled bitcast from %#x to %#x.",
            src_data_type, dst_data_type);
    vkd3d_string_buffer_printf(dst, "%s", src);
}

static void glsl_src_init(struct glsl_src *glsl_src, struct vkd3d_glsl_generator *gen,
        const struct vkd3d_shader_src_param *vsir_src, uint32_t mask)
{
    const struct vkd3d_shader_register *reg = &vsir_src->reg;
    struct vkd3d_string_buffer *register_name, *str;
    enum vkd3d_data_type src_data_type;
    unsigned int size;

    glsl_src->str = vkd3d_string_buffer_get(&gen->string_buffers);
    register_name = vkd3d_string_buffer_get(&gen->string_buffers);

    if (reg->non_uniform)
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                "Internal compiler error: Unhandled 'non-uniform' modifier.");

    if (reg->type == VKD3DSPR_IMMCONST)
        src_data_type = VKD3D_DATA_UINT;
    else
        src_data_type = VKD3D_DATA_FLOAT;

    shader_glsl_print_register_name(register_name, gen, reg);

    if (!vsir_src->modifiers)
        str = glsl_src->str;
    else
        str = vkd3d_string_buffer_get(&gen->string_buffers);

    size = reg->dimension == VSIR_DIMENSION_VEC4 ? 4 : 1;
    shader_glsl_print_bitcast(str, gen, register_name->buffer, reg->data_type, src_data_type, size);
    if (reg->dimension == VSIR_DIMENSION_VEC4)
        shader_glsl_print_swizzle(str, vsir_src->swizzle, mask);

    switch (vsir_src->modifiers)
    {
        case VKD3DSPSM_NONE:
            break;
        case VKD3DSPSM_ABS:
            vkd3d_string_buffer_printf(glsl_src->str, "abs(%s)", str->buffer);
            break;
        default:
            vkd3d_string_buffer_printf(glsl_src->str, "<unhandled modifier %#x>(%s)",
                    vsir_src->modifiers, str->buffer);
            vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                    "Internal compiler error: Unhandled source modifier(s) %#x.", vsir_src->modifiers);
            break;
    }

    if (str != glsl_src->str)
        vkd3d_string_buffer_release(&gen->string_buffers, str);
    vkd3d_string_buffer_release(&gen->string_buffers, register_name);
}

static void glsl_dst_cleanup(struct glsl_dst *dst, struct vkd3d_string_buffer_cache *cache)
{
    vkd3d_string_buffer_release(cache, dst->mask);
    vkd3d_string_buffer_release(cache, dst->register_name);
}

static uint32_t glsl_dst_init(struct glsl_dst *glsl_dst, struct vkd3d_glsl_generator *gen,
        const struct vkd3d_shader_instruction *ins, const struct vkd3d_shader_dst_param *vsir_dst)
{
    uint32_t write_mask = vsir_dst->write_mask;

    if (ins->flags & VKD3DSI_PRECISE_XYZW)
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                "Internal compiler error: Unhandled 'precise' modifier.");
    if (vsir_dst->reg.non_uniform)
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                "Internal compiler error: Unhandled 'non-uniform' modifier.");

    glsl_dst->vsir = vsir_dst;
    glsl_dst->register_name = vkd3d_string_buffer_get(&gen->string_buffers);
    glsl_dst->mask = vkd3d_string_buffer_get(&gen->string_buffers);

    shader_glsl_print_register_name(glsl_dst->register_name, gen, &vsir_dst->reg);
    shader_glsl_print_write_mask(glsl_dst->mask, write_mask);

    return write_mask;
}

static void VKD3D_PRINTF_FUNC(3, 4) shader_glsl_print_assignment(
        struct vkd3d_glsl_generator *gen, struct glsl_dst *dst, const char *format, ...)
{
    const struct vkd3d_shader_register *dst_reg = &dst->vsir->reg;
    struct vkd3d_string_buffer *buffer = gen->buffer;
    bool close = true;
    va_list args;

    if (dst->vsir->shift)
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                "Internal compiler error: Unhandled destination shift %#x.", dst->vsir->shift);
    if (dst->vsir->modifiers)
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                "Internal compiler error: Unhandled destination modifier(s) %#x.", dst->vsir->modifiers);

    shader_glsl_print_indent(buffer, gen->indent);
    vkd3d_string_buffer_printf(buffer, "%s%s = ", dst->register_name->buffer, dst->mask->buffer);

    switch (dst_reg->data_type)
    {
        default:
            vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                    "Internal compiler error: Unhandled destination register data type %#x.", dst_reg->data_type);
            /* fall through */
        case VKD3D_DATA_FLOAT:
            close = false;
            break;
        case VKD3D_DATA_UINT:
            vkd3d_string_buffer_printf(buffer, "uintBitsToFloat(");
            break;
    }

    va_start(args, format);
    vkd3d_string_buffer_vprintf(buffer, format, args);
    va_end(args);

    vkd3d_string_buffer_printf(buffer, "%s;\n", close ? ")" : "");
}

static void shader_glsl_unhandled(struct vkd3d_glsl_generator *gen, const struct vkd3d_shader_instruction *ins)
{
    shader_glsl_print_indent(gen->buffer, gen->indent);
    vkd3d_string_buffer_printf(gen->buffer, "/* <unhandled instruction %#x> */\n", ins->opcode);
    vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
            "Internal compiler error: Unhandled instruction %#x.", ins->opcode);
}

static void shader_glsl_binop(struct vkd3d_glsl_generator *gen,
        const struct vkd3d_shader_instruction *ins, const char *op)
{
    struct glsl_src src[2];
    struct glsl_dst dst;
    uint32_t mask;

    mask = glsl_dst_init(&dst, gen, ins, &ins->dst[0]);
    glsl_src_init(&src[0], gen, &ins->src[0], mask);
    glsl_src_init(&src[1], gen, &ins->src[1], mask);

    shader_glsl_print_assignment(gen, &dst, "%s %s %s", src[0].str->buffer, op, src[1].str->buffer);

    glsl_src_cleanup(&src[1], &gen->string_buffers);
    glsl_src_cleanup(&src[0], &gen->string_buffers);
    glsl_dst_cleanup(&dst, &gen->string_buffers);
}

static void shader_glsl_relop(struct vkd3d_glsl_generator *gen,
        const struct vkd3d_shader_instruction *ins, const char *scalar_op, const char *vector_op)
{
    unsigned int mask_size;
    struct glsl_src src[2];
    struct glsl_dst dst;
    uint32_t mask;

    mask = glsl_dst_init(&dst, gen, ins, &ins->dst[0]);
    glsl_src_init(&src[0], gen, &ins->src[0], mask);
    glsl_src_init(&src[1], gen, &ins->src[1], mask);

    if ((mask_size = vsir_write_mask_component_count(mask)) > 1)
        shader_glsl_print_assignment(gen, &dst, "uvec%u(%s(%s, %s)) * 0xffffffffu",
                mask_size, vector_op, src[0].str->buffer, src[1].str->buffer);
    else
        shader_glsl_print_assignment(gen, &dst, "%s %s %s ? 0xffffffffu : 0u",
                src[0].str->buffer, scalar_op, src[1].str->buffer);

    glsl_src_cleanup(&src[1], &gen->string_buffers);
    glsl_src_cleanup(&src[0], &gen->string_buffers);
    glsl_dst_cleanup(&dst, &gen->string_buffers);
}

static void shader_glsl_mov(struct vkd3d_glsl_generator *gen, const struct vkd3d_shader_instruction *ins)
{
    struct glsl_src src;
    struct glsl_dst dst;
    uint32_t mask;

    mask = glsl_dst_init(&dst, gen, ins, &ins->dst[0]);
    glsl_src_init(&src, gen, &ins->src[0], mask);

    shader_glsl_print_assignment(gen, &dst, "%s", src.str->buffer);

    glsl_src_cleanup(&src, &gen->string_buffers);
    glsl_dst_cleanup(&dst, &gen->string_buffers);
}

static void shader_glsl_print_sysval_name(struct vkd3d_string_buffer *buffer, struct vkd3d_glsl_generator *gen,
        enum vkd3d_shader_sysval_semantic sysval, unsigned int idx)
{
    const struct vkd3d_shader_version *version = &gen->program->shader_version;

    switch (sysval)
    {
        case VKD3D_SHADER_SV_POSITION:
            if (version->type == VKD3D_SHADER_TYPE_PIXEL || version->type == VKD3D_SHADER_TYPE_COMPUTE)
            {
                vkd3d_string_buffer_printf(buffer, "<unhandled sysval %#x>", sysval);
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled system value %#x.", sysval);
            }
            else
            {
                vkd3d_string_buffer_printf(buffer, "gl_Position");
                if (idx)
                    vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                            "Internal compiler error: Unhandled SV_POSITION index %u.", idx);
            }
            break;

        case VKD3D_SHADER_SV_TARGET:
            if (version->type != VKD3D_SHADER_TYPE_PIXEL)
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled SV_TARGET in shader type #%x.", version->type);
            vkd3d_string_buffer_printf(buffer, "shader_out_%u", idx);
            break;

        default:
            vkd3d_string_buffer_printf(buffer, "<unhandled sysval %#x>", sysval);
            vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                    "Internal compiler error: Unhandled system value %#x.", sysval);
            break;
    }
}

static void shader_glsl_shader_prologue(struct vkd3d_glsl_generator *gen)
{
    const struct shader_signature *signature = &gen->program->input_signature;
    struct vkd3d_string_buffer *buffer = gen->buffer;
    const struct signature_element *e;
    unsigned int i;

    for (i = 0; i < signature->element_count; ++i)
    {
        e = &signature->elements[i];

        if (e->target_location == SIGNATURE_TARGET_LOCATION_UNUSED)
            continue;

        shader_glsl_print_indent(buffer, gen->indent);
        vkd3d_string_buffer_printf(buffer, "%s_in[%u]", gen->prefix, e->register_index);
        shader_glsl_print_write_mask(buffer, e->mask);
        if (e->sysval_semantic == VKD3D_SHADER_SV_NONE)
        {
            if (gen->interstage_input)
            {
                vkd3d_string_buffer_printf(buffer, " = shader_in.reg_%u", e->target_location);
                if (e->target_location >= gen->limits.input_count)
                    vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                            "Internal compiler error: Input element %u specifies target location %u, "
                            "but only %u inputs are supported.",
                            i, e->target_location, gen->limits.input_count);
            }
            else
            {
                vkd3d_string_buffer_printf(buffer, " = shader_in_%u", i);
            }
        }
        else
        {
            vkd3d_string_buffer_printf(buffer, " = ");
            shader_glsl_print_sysval_name(buffer, gen, e->sysval_semantic, e->semantic_index);
        }
        shader_glsl_print_write_mask(buffer, e->mask);
        vkd3d_string_buffer_printf(buffer, ";\n");
    }
}

static void shader_glsl_shader_epilogue(struct vkd3d_glsl_generator *gen)
{
    const struct shader_signature *signature = &gen->program->output_signature;
    struct vkd3d_string_buffer *buffer = gen->buffer;
    const struct signature_element *e;
    unsigned int i;

    for (i = 0; i < signature->element_count; ++i)
    {
        e = &signature->elements[i];

        if (e->target_location == SIGNATURE_TARGET_LOCATION_UNUSED)
            continue;

        shader_glsl_print_indent(buffer, gen->indent);
        if (e->sysval_semantic == VKD3D_SHADER_SV_NONE)
        {
            if (gen->interstage_output)
            {
                vkd3d_string_buffer_printf(buffer, "shader_out.reg_%u", e->target_location);
                if (e->target_location >= gen->limits.output_count)
                    vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                            "Internal compiler error: Output element %u specifies target location %u, "
                            "but only %u outputs are supported.",
                            i, e->target_location, gen->limits.output_count);
            }
            else
            {
                vkd3d_string_buffer_printf(buffer, "<unhandled output %u>", e->target_location);
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled output.");
            }
        }
        else
        {
            shader_glsl_print_sysval_name(buffer, gen, e->sysval_semantic, e->semantic_index);
        }
        shader_glsl_print_write_mask(buffer, e->mask);
        vkd3d_string_buffer_printf(buffer, " = %s_out[%u]", gen->prefix, e->register_index);
        shader_glsl_print_write_mask(buffer, e->mask);
        vkd3d_string_buffer_printf(buffer, ";\n");
    }
}

static void shader_glsl_ret(struct vkd3d_glsl_generator *gen, const struct vkd3d_shader_instruction *ins)
{
    const struct vkd3d_shader_version *version = &gen->program->shader_version;

    if (version->major >= 4)
    {
        shader_glsl_shader_epilogue(gen);
        shader_glsl_print_indent(gen->buffer, gen->indent);
        vkd3d_string_buffer_printf(gen->buffer, "return;\n");
    }
}

static void vkd3d_glsl_handle_instruction(struct vkd3d_glsl_generator *gen,
        const struct vkd3d_shader_instruction *ins)
{
    gen->location = ins->location;

    switch (ins->opcode)
    {
        case VKD3DSIH_ADD:
            shader_glsl_binop(gen, ins, "+");
            break;
        case VKD3DSIH_AND:
            shader_glsl_binop(gen, ins, "&");
            break;
        case VKD3DSIH_DCL_INPUT:
        case VKD3DSIH_DCL_OUTPUT:
        case VKD3DSIH_DCL_OUTPUT_SIV:
        case VKD3DSIH_NOP:
            break;
        case VKD3DSIH_INE:
        case VKD3DSIH_NEU:
            shader_glsl_relop(gen, ins, "!=", "notEqual");
            break;
        case VKD3DSIH_MOV:
            shader_glsl_mov(gen, ins);
            break;
        case VKD3DSIH_MUL:
            shader_glsl_binop(gen, ins, "*");
            break;
        case VKD3DSIH_OR:
            shader_glsl_binop(gen, ins, "|");
            break;
        case VKD3DSIH_RET:
            shader_glsl_ret(gen, ins);
            break;
        default:
            shader_glsl_unhandled(gen, ins);
            break;
    }
}

static bool shader_glsl_check_shader_visibility(const struct vkd3d_glsl_generator *gen,
        enum vkd3d_shader_visibility visibility)
{
    enum vkd3d_shader_type t = gen->program->shader_version.type;

    switch (visibility)
    {
        case VKD3D_SHADER_VISIBILITY_ALL:
            return true;
        case VKD3D_SHADER_VISIBILITY_VERTEX:
            return t == VKD3D_SHADER_TYPE_VERTEX;
        case VKD3D_SHADER_VISIBILITY_HULL:
            return t == VKD3D_SHADER_TYPE_HULL;
        case VKD3D_SHADER_VISIBILITY_DOMAIN:
            return t == VKD3D_SHADER_TYPE_DOMAIN;
        case VKD3D_SHADER_VISIBILITY_GEOMETRY:
            return t == VKD3D_SHADER_TYPE_GEOMETRY;
        case VKD3D_SHADER_VISIBILITY_PIXEL:
            return t == VKD3D_SHADER_TYPE_PIXEL;
        case VKD3D_SHADER_VISIBILITY_COMPUTE:
            return t == VKD3D_SHADER_TYPE_COMPUTE;
        default:
            WARN("Invalid shader visibility %#x.\n", visibility);
            return false;
    }
}

static bool shader_glsl_get_cbv_binding(const struct vkd3d_glsl_generator *gen,
        unsigned int register_space, unsigned int register_idx, unsigned int *binding_idx)
{
    const struct vkd3d_shader_interface_info *interface_info = gen->interface_info;
    const struct vkd3d_shader_resource_binding *binding;
    unsigned int i;

    if (!interface_info)
        return false;

    for (i = 0; i < interface_info->binding_count; ++i)
    {
        binding = &interface_info->bindings[i];

        if (binding->type != VKD3D_SHADER_DESCRIPTOR_TYPE_CBV)
            continue;
        if (binding->register_space != register_space)
            continue;
        if (binding->register_index != register_idx)
            continue;
        if (!shader_glsl_check_shader_visibility(gen, binding->shader_visibility))
            continue;
        if (!(binding->flags & VKD3D_SHADER_BINDING_FLAG_BUFFER))
            continue;
        *binding_idx = i;
        return true;
    }

    return false;
}

static void shader_glsl_generate_cbv_declaration(struct vkd3d_glsl_generator *gen,
        const struct vkd3d_shader_descriptor_info1 *cbv)
{
    const struct vkd3d_shader_descriptor_binding *binding;
    const struct vkd3d_shader_descriptor_offset *offset;
    struct vkd3d_string_buffer *buffer = gen->buffer;
    const char *prefix = gen->prefix;
    unsigned int binding_idx;
    size_t size;

    if (cbv->count != 1)
    {
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_BINDING_NOT_FOUND,
                "Constant buffer %u has unsupported descriptor array size %u.", cbv->register_id, cbv->count);
        return;
    }

    if (!shader_glsl_get_cbv_binding(gen, cbv->register_space, cbv->register_index, &binding_idx))
    {
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_BINDING_NOT_FOUND,
                "No descriptor binding specified for constant buffer %u.", cbv->register_id);
        return;
    }

    binding = &gen->interface_info->bindings[binding_idx].binding;

    if (binding->set != 0)
    {
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_BINDING_NOT_FOUND,
                "Unsupported binding set %u specified for constant buffer %u.", binding->set, cbv->register_id);
        return;
    }

    if (binding->count != 1)
    {
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_BINDING_NOT_FOUND,
                "Unsupported binding count %u specified for constant buffer %u.", binding->count, cbv->register_id);
        return;
    }

    if (gen->offset_info && gen->offset_info->binding_offsets)
    {
        offset = &gen->offset_info->binding_offsets[binding_idx];
        if (offset->static_offset || offset->dynamic_offset_index != ~0u)
        {
            vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                    "Internal compiler error: Unhandled descriptor offset specified for constant buffer %u.",
                    cbv->register_id);
            return;
        }
    }

    size = align(cbv->buffer_size, VKD3D_VEC4_SIZE * sizeof(uint32_t));
    size /= VKD3D_VEC4_SIZE * sizeof(uint32_t);

    vkd3d_string_buffer_printf(buffer,
            "layout(std140, binding = %u) uniform block_%s_cb_%u { vec4 %s_cb_%u[%zu]; };\n",
            binding->binding, prefix, cbv->register_id, prefix, cbv->register_id, size);
}

static void shader_glsl_generate_descriptor_declarations(struct vkd3d_glsl_generator *gen)
{
    const struct vkd3d_shader_scan_descriptor_info1 *info = gen->descriptor_info;
    const struct vkd3d_shader_descriptor_info1 *descriptor;
    unsigned int i;

    for (i = 0; i < info->descriptor_count; ++i)
    {
        descriptor = &info->descriptors[i];

        switch (descriptor->type)
        {
            case VKD3D_SHADER_DESCRIPTOR_TYPE_CBV:
                shader_glsl_generate_cbv_declaration(gen, descriptor);
                break;

            default:
                vkd3d_string_buffer_printf(gen->buffer, "/* <unhandled descriptor type %#x> */\n", descriptor->type);
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled descriptor type %#x.", descriptor->type);
                break;
        }
    }
    if (info->descriptor_count)
        vkd3d_string_buffer_printf(gen->buffer, "\n");
}

static void shader_glsl_generate_interface_block(struct vkd3d_string_buffer *buffer,
        const char *type, unsigned int count)
{
    unsigned int i;

    vkd3d_string_buffer_printf(buffer, "%s shader_in_out\n{\n", type);
    for (i = 0; i < count; ++i)
    {
        vkd3d_string_buffer_printf(buffer, "    vec4 reg_%u;\n", i);
    }
    vkd3d_string_buffer_printf(buffer, "} shader_%s;\n", type);
}

static void shader_glsl_generate_input_declarations(struct vkd3d_glsl_generator *gen)
{
    const struct shader_signature *signature = &gen->program->input_signature;
    struct vkd3d_string_buffer *buffer = gen->buffer;
    const struct signature_element *e;
    unsigned int i;

    if (!gen->interstage_input)
    {
        for (i = 0; i < signature->element_count; ++i)
        {
            e = &signature->elements[i];

            if (e->target_location == SIGNATURE_TARGET_LOCATION_UNUSED)
                continue;

            if (e->sysval_semantic)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled system value %#x.", e->sysval_semantic);
                continue;
            }

            if (e->component_type != VKD3D_SHADER_COMPONENT_FLOAT)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled component type %#x.", e->component_type);
                continue;
            }

            if (e->min_precision != VKD3D_SHADER_MINIMUM_PRECISION_NONE)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled minimum precision %#x.", e->min_precision);
                continue;
            }

            if (e->interpolation_mode != VKD3DSIM_NONE)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled interpolation mode %#x.", e->interpolation_mode);
                continue;
            }

            vkd3d_string_buffer_printf(buffer,
                    "layout(location = %u) in vec4 shader_in_%u;\n", e->target_location, i);
        }
    }
    else if (gen->limits.input_count)
    {
        shader_glsl_generate_interface_block(buffer, "in", gen->limits.input_count);
    }
    vkd3d_string_buffer_printf(buffer, "\n");
}

static void shader_glsl_generate_output_declarations(struct vkd3d_glsl_generator *gen)
{
    const struct shader_signature *signature = &gen->program->output_signature;
    struct vkd3d_string_buffer *buffer = gen->buffer;
    const struct signature_element *e;
    unsigned int i;

    if (!gen->interstage_output)
    {
        for (i = 0; i < signature->element_count; ++i)
        {
            e = &signature->elements[i];

            if (e->target_location == SIGNATURE_TARGET_LOCATION_UNUSED)
                continue;

            if (e->sysval_semantic != VKD3D_SHADER_SV_TARGET)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled system value %#x.", e->sysval_semantic);
                continue;
            }

            if (e->component_type != VKD3D_SHADER_COMPONENT_FLOAT)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled component type %#x.", e->component_type);
                continue;
            }

            if (e->min_precision != VKD3D_SHADER_MINIMUM_PRECISION_NONE)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled minimum precision %#x.", e->min_precision);
                continue;
            }

            if (e->interpolation_mode != VKD3DSIM_NONE)
            {
                vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                        "Internal compiler error: Unhandled interpolation mode %#x.", e->interpolation_mode);
                continue;
            }

            vkd3d_string_buffer_printf(buffer,
                    "layout(location = %u) out vec4 shader_out_%u;\n", e->target_location, i);
        }
    }
    else if (gen->limits.output_count)
    {
        shader_glsl_generate_interface_block(buffer, "out", gen->limits.output_count);
    }
    vkd3d_string_buffer_printf(buffer, "\n");
}

static void shader_glsl_generate_declarations(struct vkd3d_glsl_generator *gen)
{
    const struct vsir_program *program = gen->program;
    struct vkd3d_string_buffer *buffer = gen->buffer;

    shader_glsl_generate_descriptor_declarations(gen);
    shader_glsl_generate_input_declarations(gen);
    shader_glsl_generate_output_declarations(gen);

    if (gen->limits.input_count)
        vkd3d_string_buffer_printf(buffer, "vec4 %s_in[%u];\n", gen->prefix, gen->limits.input_count);
    if (gen->limits.output_count)
        vkd3d_string_buffer_printf(buffer, "vec4 %s_out[%u];\n", gen->prefix, gen->limits.output_count);
    if (program->temp_count)
        vkd3d_string_buffer_printf(buffer, "vec4 r[%u];\n", program->temp_count);
    vkd3d_string_buffer_printf(buffer, "\n");
}

static int vkd3d_glsl_generator_generate(struct vkd3d_glsl_generator *gen, struct vkd3d_shader_code *out)
{
    const struct vkd3d_shader_instruction_array *instructions = &gen->program->instructions;
    struct vkd3d_string_buffer *buffer = gen->buffer;
    unsigned int i;
    void *code;

    MESSAGE("Generating a GLSL shader. This is unsupported; you get to keep all the pieces if it breaks.\n");

    vkd3d_string_buffer_printf(buffer, "#version 440\n\n");

    vkd3d_string_buffer_printf(buffer, "/* Generated by %s. */\n\n", vkd3d_shader_get_version(NULL, NULL));

    shader_glsl_generate_declarations(gen);

    vkd3d_string_buffer_printf(buffer, "void main()\n{\n");

    ++gen->indent;
    shader_glsl_shader_prologue(gen);
    for (i = 0; i < instructions->count; ++i)
    {
        vkd3d_glsl_handle_instruction(gen, &instructions->elements[i]);
    }

    vkd3d_string_buffer_printf(buffer, "}\n");

    if (TRACE_ON())
        vkd3d_string_buffer_trace(buffer);

    if (gen->failed)
        return VKD3D_ERROR_INVALID_SHADER;

    if ((code = vkd3d_malloc(buffer->buffer_size)))
    {
        memcpy(code, buffer->buffer, buffer->content_size);
        out->size = buffer->content_size;
        out->code = code;
    }
    else return VKD3D_ERROR_OUT_OF_MEMORY;

    return VKD3D_OK;
}

static void vkd3d_glsl_generator_cleanup(struct vkd3d_glsl_generator *gen)
{
    vkd3d_string_buffer_release(&gen->string_buffers, gen->buffer);
    vkd3d_string_buffer_cache_cleanup(&gen->string_buffers);
}

static void shader_glsl_init_limits(struct vkd3d_glsl_generator *gen, const struct vkd3d_shader_version *version)
{
    struct shader_limits *limits = &gen->limits;

    if (version->major < 4 || version->major >= 6)
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                "Internal compiler error: Unhandled shader version %u.%u.", version->major, version->minor);

    switch (version->type)
    {
        case VKD3D_SHADER_TYPE_VERTEX:
            limits->input_count = 32;
            limits->output_count = 32;
            break;
        case VKD3D_SHADER_TYPE_PIXEL:
            limits->input_count = 32;
            limits->output_count = 8;
            break;
        default:
            vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                    "Internal compiler error: Unhandled shader type %#x.", version->type);
            limits->input_count = 0;
            limits->output_count = 0;
            break;
    }
}

static void vkd3d_glsl_generator_init(struct vkd3d_glsl_generator *gen,
        struct vsir_program *program, const struct vkd3d_shader_compile_info *compile_info,
        const struct vkd3d_shader_scan_descriptor_info1 *descriptor_info,
        struct vkd3d_shader_message_context *message_context)
{
    enum vkd3d_shader_type type = program->shader_version.type;

    memset(gen, 0, sizeof(*gen));
    gen->program = program;
    vkd3d_string_buffer_cache_init(&gen->string_buffers);
    gen->buffer = vkd3d_string_buffer_get(&gen->string_buffers);
    gen->location.source_name = compile_info->source_name;
    gen->message_context = message_context;
    if (!(gen->prefix = shader_glsl_get_prefix(type)))
    {
        vkd3d_glsl_compiler_error(gen, VKD3D_SHADER_ERROR_GLSL_INTERNAL,
                "Internal compiler error: Unhandled shader type %#x.", type);
        gen->prefix = "unknown";
    }
    shader_glsl_init_limits(gen, &program->shader_version);
    gen->interstage_input = type != VKD3D_SHADER_TYPE_VERTEX;
    gen->interstage_output = type != VKD3D_SHADER_TYPE_PIXEL;

    gen->interface_info = vkd3d_find_struct(compile_info->next, INTERFACE_INFO);
    gen->offset_info = vkd3d_find_struct(compile_info->next, DESCRIPTOR_OFFSET_INFO);
    gen->descriptor_info = descriptor_info;
}

int glsl_compile(struct vsir_program *program, uint64_t config_flags,
        const struct vkd3d_shader_scan_descriptor_info1 *descriptor_info,
        const struct vkd3d_shader_compile_info *compile_info,
        struct vkd3d_shader_code *out, struct vkd3d_shader_message_context *message_context)
{
    struct vkd3d_glsl_generator generator;
    int ret;

    if ((ret = vsir_program_transform(program, config_flags, compile_info, message_context)) < 0)
        return ret;

    vkd3d_glsl_generator_init(&generator, program, compile_info, descriptor_info, message_context);
    ret = vkd3d_glsl_generator_generate(&generator, out);
    vkd3d_glsl_generator_cleanup(&generator);

    return ret;
}
