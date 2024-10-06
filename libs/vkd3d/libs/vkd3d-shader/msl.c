/*
 * Copyright 2024 Feifan He for CodeWeavers
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

struct msl_src
{
    struct vkd3d_string_buffer *str;
};

struct msl_dst
{
    const struct vkd3d_shader_dst_param *vsir;
    struct vkd3d_string_buffer *register_name;
    struct vkd3d_string_buffer *mask;
};

struct msl_generator
{
    struct vsir_program *program;
    struct vkd3d_string_buffer_cache string_buffers;
    struct vkd3d_string_buffer *buffer;
    struct vkd3d_shader_location location;
    struct vkd3d_shader_message_context *message_context;
    unsigned int indent;
};

static void VKD3D_PRINTF_FUNC(3, 4) msl_compiler_error(struct msl_generator *gen,
        enum vkd3d_shader_error error, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vkd3d_shader_verror(gen->message_context, &gen->location, error, fmt, args);
    va_end(args);
}

static void msl_print_indent(struct vkd3d_string_buffer *buffer, unsigned int indent)
{
    vkd3d_string_buffer_printf(buffer, "%*s", 4 * indent, "");
}

static void msl_print_register_datatype(struct vkd3d_string_buffer *buffer,
        struct msl_generator *gen, const struct vkd3d_shader_register *reg)
{
    vkd3d_string_buffer_printf(buffer, ".");
    switch (reg->data_type)
    {
        case VKD3D_DATA_FLOAT:
            vkd3d_string_buffer_printf(buffer, "f");
            break;
        case VKD3D_DATA_INT:
            vkd3d_string_buffer_printf(buffer, "i");
            break;
        case VKD3D_DATA_UINT:
            vkd3d_string_buffer_printf(buffer, "u");
            break;
        default:
            msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                    "Internal compiler error: Unhandled register datatype %#x.", reg->data_type);
            vkd3d_string_buffer_printf(buffer, "<unrecognised register datatype %#x>", reg->data_type);
            break;
    }
}

static void msl_print_register_name(struct vkd3d_string_buffer *buffer,
        struct msl_generator *gen, const struct vkd3d_shader_register *reg)
{
    switch (reg->type)
    {
        case VKD3DSPR_TEMP:
            vkd3d_string_buffer_printf(buffer, "r[%u]", reg->idx[0].offset);
            msl_print_register_datatype(buffer, gen, reg);
            break;
        default:
            msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                    "Internal compiler error: Unhandled register type %#x.", reg->type);
            vkd3d_string_buffer_printf(buffer, "<unrecognised register %#x>", reg->type);
            break;
    }
}

static void msl_print_swizzle(struct vkd3d_string_buffer *buffer, uint32_t swizzle, uint32_t mask)
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

static void msl_print_write_mask(struct vkd3d_string_buffer *buffer, uint32_t write_mask)
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

static void msl_src_cleanup(struct msl_src *src, struct vkd3d_string_buffer_cache *cache)
{
    vkd3d_string_buffer_release(cache, src->str);
}

static void msl_src_init(struct msl_src *msl_src, struct msl_generator *gen,
        const struct vkd3d_shader_src_param *vsir_src, uint32_t mask)
{
    const struct vkd3d_shader_register *reg = &vsir_src->reg;

    msl_src->str = vkd3d_string_buffer_get(&gen->string_buffers);

    if (reg->non_uniform)
        msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                "Internal compiler error: Unhandled 'non-uniform' modifier.");
    if (vsir_src->modifiers)
        msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                "Internal compiler error: Unhandled source modifier(s) %#x.", vsir_src->modifiers);

    msl_print_register_name(msl_src->str, gen, reg);
    if (reg->dimension == VSIR_DIMENSION_VEC4)
        msl_print_swizzle(msl_src->str, vsir_src->swizzle, mask);
}

static void msl_dst_cleanup(struct msl_dst *dst, struct vkd3d_string_buffer_cache *cache)
{
    vkd3d_string_buffer_release(cache, dst->mask);
    vkd3d_string_buffer_release(cache, dst->register_name);
}

static uint32_t msl_dst_init(struct msl_dst *msl_dst, struct msl_generator *gen,
        const struct vkd3d_shader_instruction *ins, const struct vkd3d_shader_dst_param *vsir_dst)
{
    uint32_t write_mask = vsir_dst->write_mask;

    if (ins->flags & VKD3DSI_PRECISE_XYZW)
        msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                "Internal compiler error: Unhandled 'precise' modifier.");
    if (vsir_dst->reg.non_uniform)
        msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                "Internal compiler error: Unhandled 'non-uniform' modifier.");

    msl_dst->vsir = vsir_dst;
    msl_dst->register_name = vkd3d_string_buffer_get(&gen->string_buffers);
    msl_dst->mask = vkd3d_string_buffer_get(&gen->string_buffers);

    msl_print_register_name(msl_dst->register_name, gen, &vsir_dst->reg);
    msl_print_write_mask(msl_dst->mask, write_mask);

    return write_mask;
}

static void VKD3D_PRINTF_FUNC(3, 4) msl_print_assignment(
        struct msl_generator *gen, struct msl_dst *dst, const char *format, ...)
{
    va_list args;

    if (dst->vsir->shift)
        msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                "Internal compiler error: Unhandled destination shift %#x.", dst->vsir->shift);
    if (dst->vsir->modifiers)
        msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
                "Internal compiler error: Unhandled destination modifier(s) %#x.", dst->vsir->modifiers);

    msl_print_indent(gen->buffer, gen->indent);
    vkd3d_string_buffer_printf(gen->buffer, "%s%s = ", dst->register_name->buffer, dst->mask->buffer);

    va_start(args, format);
    vkd3d_string_buffer_vprintf(gen->buffer, format, args);
    va_end(args);

    vkd3d_string_buffer_printf(gen->buffer, ";\n");
}

static void msl_unhandled(struct msl_generator *gen, const struct vkd3d_shader_instruction *ins)
{
    msl_print_indent(gen->buffer, gen->indent);
    vkd3d_string_buffer_printf(gen->buffer, "/* <unhandled instruction %#x> */\n", ins->opcode);
    msl_compiler_error(gen, VKD3D_SHADER_ERROR_MSL_INTERNAL,
            "Internal compiler error: Unhandled instruction %#x.", ins->opcode);
}

static void msl_mov(struct msl_generator *gen, const struct vkd3d_shader_instruction *ins)
{
    struct msl_src src;
    struct msl_dst dst;
    uint32_t mask;

    mask = msl_dst_init(&dst, gen, ins, &ins->dst[0]);
    msl_src_init(&src, gen, &ins->src[0], mask);

    msl_print_assignment(gen, &dst, "%s", src.str->buffer);

    msl_src_cleanup(&src, &gen->string_buffers);
    msl_dst_cleanup(&dst, &gen->string_buffers);
}

static void msl_ret(struct msl_generator *gen, const struct vkd3d_shader_instruction *ins)
{
    msl_print_indent(gen->buffer, gen->indent);
    vkd3d_string_buffer_printf(gen->buffer, "return;\n");
}

static void msl_handle_instruction(struct msl_generator *gen, const struct vkd3d_shader_instruction *ins)
{
    gen->location = ins->location;

    switch (ins->opcode)
    {
        case VKD3DSIH_NOP:
            break;
        case VKD3DSIH_MOV:
            msl_mov(gen, ins);
            break;
        case VKD3DSIH_RET:
            msl_ret(gen, ins);
            break;
        default:
            msl_unhandled(gen, ins);
            break;
    }
}

static void msl_generator_generate(struct msl_generator *gen)
{
    const struct vkd3d_shader_instruction_array *instructions = &gen->program->instructions;
    unsigned int i;

    MESSAGE("Generating a MSL shader. This is unsupported; you get to keep all the pieces if it breaks.\n");

    vkd3d_string_buffer_printf(gen->buffer, "/* Generated by %s. */\n\n", vkd3d_shader_get_version(NULL, NULL));

    vkd3d_string_buffer_printf(gen->buffer, "union vkd3d_vec4\n{\n");
    vkd3d_string_buffer_printf(gen->buffer, "    uint4 u;\n");
    vkd3d_string_buffer_printf(gen->buffer, "    int4 i;\n");
    vkd3d_string_buffer_printf(gen->buffer, "    float4 f;\n};\n\n");

    vkd3d_string_buffer_printf(gen->buffer, "void shader_main()\n{\n");

    ++gen->indent;

    if (gen->program->temp_count)
    {
        msl_print_indent(gen->buffer, gen->indent);
        vkd3d_string_buffer_printf(gen->buffer, "vkd3d_vec4 r[%u];\n\n", gen->program->temp_count);
    }

    for (i = 0; i < instructions->count; ++i)
    {
        msl_handle_instruction(gen, &instructions->elements[i]);
    }

    vkd3d_string_buffer_printf(gen->buffer, "}\n");

    if (TRACE_ON())
        vkd3d_string_buffer_trace(gen->buffer);
}

static void msl_generator_cleanup(struct msl_generator *gen)
{
    vkd3d_string_buffer_release(&gen->string_buffers, gen->buffer);
    vkd3d_string_buffer_cache_cleanup(&gen->string_buffers);
}

static int msl_generator_init(struct msl_generator *gen, struct vsir_program *program,
        struct vkd3d_shader_message_context *message_context)
{
    memset(gen, 0, sizeof(*gen));
    gen->program = program;
    vkd3d_string_buffer_cache_init(&gen->string_buffers);
    if (!(gen->buffer = vkd3d_string_buffer_get(&gen->string_buffers)))
    {
        vkd3d_string_buffer_cache_cleanup(&gen->string_buffers);
        return VKD3D_ERROR_OUT_OF_MEMORY;
    }
    gen->message_context = message_context;

    return VKD3D_OK;
}

int msl_compile(struct vsir_program *program, uint64_t config_flags,
        const struct vkd3d_shader_compile_info *compile_info, struct vkd3d_shader_message_context *message_context)
{
    struct msl_generator generator;
    int ret;

    if ((ret = vsir_program_transform(program, config_flags, compile_info, message_context)) < 0)
        return ret;

    if ((ret = msl_generator_init(&generator, program, message_context)) < 0)
        return ret;
    msl_generator_generate(&generator);
    msl_generator_cleanup(&generator);

    return VKD3D_ERROR_INVALID_SHADER;
}
