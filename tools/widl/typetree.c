/*
 * IDL Type Tree
 *
 * Copyright 2008 Robert Shearman
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "widl.h"
#include "utils.h"
#include "parser.h"
#include "typetree.h"
#include "header.h"
#include "hash.h"

type_t *duptype(type_t *t, int dupname)
{
  type_t *d = alloc_type();

  *d = *t;
  if (dupname && t->name)
    d->name = xstrdup(t->name);

  return d;
}

type_t *make_type(enum type_type type)
{
    type_t *t = alloc_type();
    t->name = NULL;
    t->namespace = NULL;
    t->type_type = type;
    t->attrs = NULL;
    t->c_name = NULL;
    t->signature = NULL;
    t->short_name = NULL;
    memset(&t->details, 0, sizeof(t->details));
    t->typestring_offset = 0;
    t->ptrdesc = 0;
    t->ignore = (parse_only != 0);
    t->defined = FALSE;
    t->written = FALSE;
    t->user_types_registered = FALSE;
    t->tfswrite = FALSE;
    t->checked = FALSE;
    t->typelib_idx = -1;
    init_loc_info(&t->loc_info);
    return t;
}

static const var_t *find_arg(const var_list_t *args, const char *name)
{
    const var_t *arg;

    if (args) LIST_FOR_EACH_ENTRY(arg, args, const var_t, entry)
    {
        if (arg->name && !strcmp(name, arg->name))
            return arg;
    }

    return NULL;
}

const char *type_get_name(const type_t *type, enum name_type name_type)
{
    switch(name_type) {
    case NAME_DEFAULT:
        return type->name;
    case NAME_C:
        return type->c_name ? type->c_name : type->name;
    }

    assert(0);
    return NULL;
}

#define append_buf(f, ...) \
   do { int r = f(buf + ret, max(ret, len) - ret, __VA_ARGS__); assert(r >= 0); ret += r; } while(0)

static int append_namespace(char *buf, size_t len, struct namespace *namespace, const char *separator, const char *abi_prefix)
{
    const char *name = namespace && !is_global_namespace(namespace) ? namespace->name : abi_prefix;
    int ret = 0;
    if (!name) return 0;
    if (namespace && !is_global_namespace(namespace)) append_buf(append_namespace, namespace->parent, separator, abi_prefix);
    append_buf(snprintf, "%s%s", name, separator);
    return ret;
}

static int format_namespace_buffer(char *buf, size_t len, struct namespace *namespace, const char *prefix,
                                   const char *separator, const char *suffix, const char *abi_prefix)
{
    int ret = 0;
    append_buf(snprintf, "%s", prefix);
    append_buf(append_namespace, namespace, separator, abi_prefix);
    append_buf(snprintf, "%s", suffix);
    return ret;
}

static int format_parameterized_type_name_buffer(char *buf, size_t len, type_t *type, type_list_t *params)
{
    type_list_t *entry;
    int ret = 0;
    append_buf(snprintf, "%s<", type->name);
    for (entry = params; entry; entry = entry->next)
    {
        for (type = entry->type; type->type_type == TYPE_POINTER; type = type_pointer_get_ref_type(type)) {}
        append_buf(format_namespace_buffer, type->namespace, "", "::", type->name, use_abi_namespace ? "ABI" : NULL);
        for (type = entry->type; type->type_type == TYPE_POINTER; type = type_pointer_get_ref_type(type)) append_buf(snprintf, "*");
        if (entry->next) append_buf(snprintf, ",");
    }
    append_buf(snprintf, ">");
    return ret;
}

static int format_parameterized_type_c_name_buffer(char *buf, size_t len, type_t *type, type_list_t *params, const char *prefix)
{
    type_list_t *entry;
    int ret = 0, count = 0;
    append_buf(format_namespace_buffer, type->namespace, "__x_", "_C", "", use_abi_namespace ? "ABI" : NULL);
    for (entry = params; entry; entry = entry->next) count++;
    append_buf(snprintf, "%s%s_%d", prefix, type->name, count);
    for (entry = params; entry; entry = entry->next)
    {
        for (type = entry->type; type->type_type == TYPE_POINTER; type = type_pointer_get_ref_type(type)) {}
        append_buf(format_namespace_buffer, type->namespace, "_", "__C", type->name, NULL);
    }
    return ret;
}

static int format_type_signature_buffer(char *buf, size_t len, type_t *type);

static int format_var_list_signature_buffer(char *buf, size_t len, var_list_t *var_list)
{
    var_t *var;
    int ret = 0;
    if (!var_list) append_buf(snprintf, ";");
    else LIST_FOR_EACH_ENTRY(var, var_list, var_t, entry)
    {
        append_buf(snprintf, ";");
        append_buf(format_type_signature_buffer, var->declspec.type);
    }
    return ret;
}

static int format_type_signature_buffer(char *buf, size_t len, type_t *type)
{
    const GUID *uuid;
    int ret = 0;
    if (!type) return 0;
    switch (type->type_type)
    {
    case TYPE_INTERFACE:
        if (type->signature) append_buf(snprintf, "%s", type->signature);
        else
        {
             uuid = type_get_uuid(type);
             append_buf(snprintf, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                        uuid->Data1, uuid->Data2, uuid->Data3,
                        uuid->Data4[0], uuid->Data4[1], uuid->Data4[2], uuid->Data4[3],
                        uuid->Data4[4], uuid->Data4[5], uuid->Data4[6], uuid->Data4[7]);
        }
        return ret;
    case TYPE_DELEGATE:
        append_buf(snprintf, "delegate(");
        append_buf(format_type_signature_buffer, type_delegate_get_iface(type));
        append_buf(snprintf, ")");
        return ret;
    case TYPE_RUNTIMECLASS:
        append_buf(snprintf, "rc(");
        append_buf(format_namespace_buffer, type->namespace, "", ".", type->name, NULL);
        append_buf(snprintf, ";");
        append_buf(format_type_signature_buffer, type_runtimeclass_get_default_iface(type));
        append_buf(snprintf, ")");
        return ret;
    case TYPE_POINTER:
        return format_type_signature_buffer(buf, len, type->details.pointer.ref.type);
    case TYPE_ALIAS:
        if (!strcmp(type->name, "boolean")) append_buf(snprintf, "b1");
        else ret = format_type_signature_buffer(buf, len, type->details.alias.aliasee.type);
        return ret;
    case TYPE_STRUCT:
        append_buf(snprintf, "struct(");
        append_buf(format_namespace_buffer, type->namespace, "", ".", type->name, NULL);
        append_buf(format_var_list_signature_buffer, type->details.structure->fields);
        append_buf(snprintf, ")");
        return ret;
    case TYPE_BASIC:
        switch (type_basic_get_type(type))
        {
        case TYPE_BASIC_INT:
        case TYPE_BASIC_INT32:
            append_buf(snprintf, type_basic_get_sign(type) < 0 ? "i4" : "u4");
            return ret;
        case TYPE_BASIC_INT64:
            append_buf(snprintf, type_basic_get_sign(type) < 0 ? "i8" : "u8");
            return ret;
        case TYPE_BASIC_INT8:
            assert(type_basic_get_sign(type) >= 0); /* signature string for signed char isn't specified */
            append_buf(snprintf, "u1");
            return ret;
        case TYPE_BASIC_FLOAT:
            append_buf(snprintf, "f4");
            return ret;
        case TYPE_BASIC_DOUBLE:
            append_buf(snprintf, "f8");
            return ret;
        case TYPE_BASIC_INT16:
        case TYPE_BASIC_INT3264:
        case TYPE_BASIC_LONG:
        case TYPE_BASIC_CHAR:
        case TYPE_BASIC_HYPER:
        case TYPE_BASIC_BYTE:
        case TYPE_BASIC_WCHAR:
        case TYPE_BASIC_ERROR_STATUS_T:
        case TYPE_BASIC_HANDLE:
            error("basic type '%d' signature not implemented\n", type_basic_get_type(type));
            assert(0); /* FIXME: implement when needed */
            break;
        }
    case TYPE_ENUM:
        append_buf(snprintf, "enum(");
        append_buf(format_namespace_buffer, type->namespace, "", ".", type->name, NULL);
        if (is_attr(type->attrs, ATTR_FLAGS)) append_buf(snprintf, ";u4");
        else append_buf(snprintf, ";i4");
        append_buf(snprintf, ")");
        return ret;
    case TYPE_ARRAY:
    case TYPE_ENCAPSULATED_UNION:
    case TYPE_UNION:
    case TYPE_COCLASS:
        error("type '%d' signature for '%s' not implemented\n", type->type_type, type->name);
        assert(0); /* FIXME: implement when needed */
        break;
    case TYPE_VOID:
    case TYPE_FUNCTION:
    case TYPE_BITFIELD:
    case TYPE_MODULE:
    case TYPE_PARAMETERIZED_TYPE:
    case TYPE_PARAMETER:
    case TYPE_APICONTRACT:
        assert(0); /* should not be there */
        break;
    }

    return ret;
}

static int format_parameterized_type_signature_buffer(char *buf, size_t len, type_t *type, type_list_t *params)
{
    type_list_t *entry;
    const GUID *uuid = type_get_uuid(type);
    int ret = 0;
    append_buf(snprintf, "pinterface({%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
               uuid->Data1, uuid->Data2, uuid->Data3,
               uuid->Data4[0], uuid->Data4[1], uuid->Data4[2], uuid->Data4[3],
               uuid->Data4[4], uuid->Data4[5], uuid->Data4[6], uuid->Data4[7]);
    for (entry = params; entry; entry = entry->next)
    {
        append_buf(snprintf, ";");
        append_buf(format_type_signature_buffer, entry->type);
    }
    append_buf(snprintf, ")");
    return ret;
}

static int format_parameterized_type_short_name_buffer(char *buf, size_t len, type_t *type, type_list_t *params, const char *prefix)
{
    type_list_t *entry;
    int ret = 0;
    append_buf(snprintf, "%s%s", prefix, type->name);
    for (entry = params; entry; entry = entry->next)
    {
        for (type = entry->type; type->type_type == TYPE_POINTER; type = type_pointer_get_ref_type(type)) {}
        append_buf(snprintf, "_%s", type->name);
    }
    return ret;
}

#undef append_buf

char *format_namespace(struct namespace *namespace, const char *prefix, const char *separator, const char *suffix, const char *abi_prefix)
{
    int len = format_namespace_buffer(NULL, 0, namespace, prefix, separator, suffix, abi_prefix);
    char *buf = xmalloc(len + 1);
    format_namespace_buffer(buf, len + 1, namespace, prefix, separator, suffix, abi_prefix);
    return buf;
}

char *format_parameterized_type_name(type_t *type, type_list_t *params)
{
    int len = format_parameterized_type_name_buffer(NULL, 0, type, params);
    char *buf = xmalloc(len + 1);
    format_parameterized_type_name_buffer(buf, len + 1, type, params);
    return buf;
}

char *format_type_signature(type_t *type)
{
    int len = format_type_signature_buffer(NULL, 0, type);
    char *buf = xmalloc(len + 1);
    format_type_signature_buffer(buf, len + 1, type);
    return buf;
}

static char const *parameterized_type_shorthands[][2] = {
    {"Windows_CFoundation_CCollections_C", "__F"},
    {"Windows_CFoundation_C", "__F"},
};

static char *format_parameterized_type_c_name(type_t *type, type_list_t *params, const char *prefix)
{
    int i, len = format_parameterized_type_c_name_buffer(NULL, 0, type, params, prefix);
    char *buf = xmalloc(len + 1), *tmp;
    format_parameterized_type_c_name_buffer(buf, len + 1, type, params, prefix);

    for (i = 0; i < ARRAY_SIZE(parameterized_type_shorthands); ++i)
    {
        if ((tmp = strstr(buf, parameterized_type_shorthands[i][0])) &&
            (tmp - buf) == strlen(use_abi_namespace ? "__x_ABI_C" : "__x_C"))
        {
           tmp += strlen(parameterized_type_shorthands[i][0]);
           strcpy(buf, parameterized_type_shorthands[i][1]);
           memmove(buf + 3, tmp, len - (tmp - buf) + 1);
        }
    }

    return buf;
}

static char *format_parameterized_type_signature(type_t *type, type_list_t *params)
{
    int len = format_parameterized_type_signature_buffer(NULL, 0, type, params);
    char *buf = xmalloc(len + 1);
    format_parameterized_type_signature_buffer(buf, len + 1, type, params);
    return buf;
}

static char *format_parameterized_type_short_name(type_t *type, type_list_t *params, const char *prefix)
{
    int len = format_parameterized_type_short_name_buffer(NULL, 0, type, params, prefix);
    char *buf = xmalloc(len + 1);
    format_parameterized_type_short_name_buffer(buf, len + 1, type, params, prefix);
    return buf;
}

type_t *type_new_function(var_list_t *args)
{
    var_t *arg;
    type_t *t;
    unsigned int i = 0;

    if (args)
    {
        arg = LIST_ENTRY(list_head(args), var_t, entry);
        if (list_count(args) == 1 && !arg->name && arg->declspec.type && type_get_type(arg->declspec.type) == TYPE_VOID)
        {
            list_remove(&arg->entry);
            free(arg);
            free(args);
            args = NULL;
        }
    }
    if (args) LIST_FOR_EACH_ENTRY(arg, args, var_t, entry)
    {
        if (arg->declspec.type && type_get_type(arg->declspec.type) == TYPE_VOID)
            error_loc("argument '%s' has void type\n", arg->name);
        if (!arg->name)
        {
            if (i > 26 * 26)
                error_loc("too many unnamed arguments\n");
            else
            {
                int unique;
                do
                {
                    char name[3];
                    name[0] = i > 26 ? 'a' + i / 26 : 'a' + i;
                    name[1] = i > 26 ? 'a' + i % 26 : 0;
                    name[2] = 0;
                    unique = !find_arg(args, name);
                    if (unique)
                        arg->name = xstrdup(name);
                    i++;
                } while (!unique);
            }
        }
    }

    t = make_type(TYPE_FUNCTION);
    t->details.function = xmalloc(sizeof(*t->details.function));
    t->details.function->args = args;
    t->details.function->retval = make_var(xstrdup("_RetVal"));
    return t;
}

type_t *type_new_pointer(type_t *ref)
{
    type_t *t = make_type(TYPE_POINTER);
    t->details.pointer.ref.type = ref;
    return t;
}

type_t *type_new_alias(const decl_spec_t *t, const char *name)
{
    type_t *a = make_type(TYPE_ALIAS);

    a->name = xstrdup(name);
    a->attrs = NULL;
    a->details.alias.aliasee = *t;
    init_loc_info(&a->loc_info);

    return a;
}

type_t *type_new_module(char *name)
{
    type_t *type = get_type(TYPE_MODULE, name, NULL, 0);
    if (type->type_type != TYPE_MODULE || type->defined)
        error_loc("%s: redefinition error; original definition was at %s:%d\n",
                  type->name, type->loc_info.input_name, type->loc_info.line_number);
    type->name = name;
    return type;
}

type_t *type_new_coclass(char *name)
{
    type_t *type = get_type(TYPE_COCLASS, name, NULL, 0);
    if (type->type_type != TYPE_COCLASS || type->defined)
        error_loc("%s: redefinition error; original definition was at %s:%d\n",
                  type->name, type->loc_info.input_name, type->loc_info.line_number);
    type->name = name;
    return type;
}

type_t *type_new_runtimeclass(char *name, struct namespace *namespace)
{
    type_t *type = get_type(TYPE_RUNTIMECLASS, name, NULL, 0);
    if (type->type_type != TYPE_RUNTIMECLASS || type->defined)
        error_loc("%s: redefinition error; original definition was at %s:%d\n",
                  type->name, type->loc_info.input_name, type->loc_info.line_number);
    type->name = name;
    type->namespace = namespace;
    return type;
}

type_t *type_new_array(const char *name, const decl_spec_t *element, int declptr,
                       unsigned int dim, expr_t *size_is, expr_t *length_is)
{
    type_t *t = make_type(TYPE_ARRAY);
    if (name) t->name = xstrdup(name);
    t->details.array.declptr = declptr;
    t->details.array.length_is = length_is;
    if (size_is)
        t->details.array.size_is = size_is;
    else
        t->details.array.dim = dim;
    if (element)
        t->details.array.elem = *element;
    return t;
}

type_t *type_new_basic(enum type_basic_type basic_type)
{
    type_t *t = make_type(TYPE_BASIC);
    t->details.basic.type = basic_type;
    t->details.basic.sign = 0;
    return t;
}

type_t *type_new_int(enum type_basic_type basic_type, int sign)
{
    static type_t *int_types[TYPE_BASIC_INT_MAX+1][3];

    assert(basic_type <= TYPE_BASIC_INT_MAX);

    /* map sign { -1, 0, 1 } -> { 0, 1, 2 } */
    if (!int_types[basic_type][sign + 1])
    {
        int_types[basic_type][sign + 1] = type_new_basic(basic_type);
        int_types[basic_type][sign + 1]->details.basic.sign = sign;
    }
    return int_types[basic_type][sign + 1];
}

type_t *type_new_void(void)
{
    static type_t *void_type = NULL;
    if (!void_type)
        void_type = make_type(TYPE_VOID);
    return void_type;
}

type_t *type_new_enum(const char *name, struct namespace *namespace, int defined, var_list_t *enums)
{
    type_t *t = NULL;

    if (name)
        t = find_type(name, namespace,tsENUM);

    if (!t)
    {
        t = make_type(TYPE_ENUM);
        t->name = name;
        t->namespace = namespace;
        if (name)
            reg_type(t, name, namespace, tsENUM);
    }

    if (!t->defined && defined)
    {
        t->details.enumeration = xmalloc(sizeof(*t->details.enumeration));
        t->details.enumeration->enums = enums;
        t->defined = TRUE;
    }
    else if (defined)
        error_loc("redefinition of enum %s\n", name);

    return t;
}

type_t *type_new_struct(char *name, struct namespace *namespace, int defined, var_list_t *fields)
{
    type_t *t = NULL;

    if (name)
        t = find_type(name, namespace, tsSTRUCT);

    if (!t)
    {
        t = make_type(TYPE_STRUCT);
        t->name = name;
        t->namespace = namespace;
        if (name)
            reg_type(t, name, namespace, tsSTRUCT);
    }

    if (!t->defined && defined)
    {
        t->details.structure = xmalloc(sizeof(*t->details.structure));
        t->details.structure->fields = fields;
        t->defined = TRUE;
    }
    else if (defined)
        error_loc("redefinition of struct %s\n", name);

    return t;
}

type_t *type_new_nonencapsulated_union(const char *name, int defined, var_list_t *fields)
{
    type_t *t = NULL;

    if (name)
        t = find_type(name, NULL, tsUNION);

    if (!t)
    {
        t = make_type(TYPE_UNION);
        t->name = name;
        if (name)
            reg_type(t, name, NULL, tsUNION);
    }

    if (!t->defined && defined)
    {
        t->details.structure = xmalloc(sizeof(*t->details.structure));
        t->details.structure->fields = fields;
        t->defined = TRUE;
    }
    else if (defined)
        error_loc("redefinition of union %s\n", name);

    return t;
}

type_t *type_new_encapsulated_union(char *name, var_t *switch_field, var_t *union_field, var_list_t *cases)
{
    type_t *t = NULL;

    if (name)
        t = find_type(name, NULL, tsUNION);

    if (!t)
    {
        t = make_type(TYPE_ENCAPSULATED_UNION);
        t->name = name;
        if (name)
            reg_type(t, name, NULL, tsUNION);
    }
    t->type_type = TYPE_ENCAPSULATED_UNION;

    if (!t->defined)
    {
        if (!union_field)
            union_field = make_var(xstrdup("tagged_union"));
        union_field->declspec.type = type_new_nonencapsulated_union(gen_name(), TRUE, cases);

        t->details.structure = xmalloc(sizeof(*t->details.structure));
        t->details.structure->fields = append_var(NULL, switch_field);
        t->details.structure->fields = append_var(t->details.structure->fields, union_field);
        t->defined = TRUE;
    }
    else
        error_loc("redefinition of union %s\n", name);

    return t;
}

static int is_valid_bitfield_type(const type_t *type)
{
    switch (type_get_type(type))
    {
    case TYPE_ENUM:
        return TRUE;
    case TYPE_BASIC:
        switch (type_basic_get_type(type))
        {
        case TYPE_BASIC_INT8:
        case TYPE_BASIC_INT16:
        case TYPE_BASIC_INT32:
        case TYPE_BASIC_INT64:
        case TYPE_BASIC_INT:
        case TYPE_BASIC_INT3264:
        case TYPE_BASIC_LONG:
        case TYPE_BASIC_CHAR:
        case TYPE_BASIC_HYPER:
        case TYPE_BASIC_BYTE:
        case TYPE_BASIC_WCHAR:
        case TYPE_BASIC_ERROR_STATUS_T:
            return TRUE;
        case TYPE_BASIC_FLOAT:
        case TYPE_BASIC_DOUBLE:
        case TYPE_BASIC_HANDLE:
            return FALSE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

type_t *type_new_bitfield(type_t *field, const expr_t *bits)
{
    type_t *t;

    if (!is_valid_bitfield_type(field))
        error_loc("bit-field has invalid type\n");

    if (bits->cval < 0)
        error_loc("negative width for bit-field\n");

    /* FIXME: validate bits->cval <= memsize(field) * 8 */

    t = make_type(TYPE_BITFIELD);
    t->details.bitfield.field = field;
    t->details.bitfield.bits = bits;
    return t;
}

static unsigned int compute_method_indexes(type_t *iface)
{
    unsigned int idx;
    statement_t *stmt;

    if (!iface->details.iface)
        return 0;

    if (type_iface_get_inherit(iface))
        idx = compute_method_indexes(type_iface_get_inherit(iface));
    else
        idx = 0;

    STATEMENTS_FOR_EACH_FUNC( stmt, type_iface_get_stmts(iface) )
    {
        var_t *func = stmt->u.var;
        if (!is_callas(func->attrs))
            func->func_idx = idx++;
    }

    return idx;
}

static void compute_delegate_iface_name(type_t *delegate)
{
    char *name = xmalloc(strlen(delegate->name) + 2);
    sprintf(name, "I%s", delegate->name);
    delegate->details.delegate.iface->name = name;
}

static void compute_interface_signature_uuid(type_t *iface)
{
    static unsigned char const wrt_pinterface_namespace[] = {0x11,0xf4,0x7a,0xd5,0x7b,0x73,0x42,0xc0,0xab,0xae,0x87,0x8b,0x1e,0x16,0xad,0xee};
    static const int version = 5;
    unsigned char hash[20];
    SHA_CTX sha_ctx;
    attr_t *attr;
    GUID *uuid;

    if (!iface->attrs)
    {
        iface->attrs = xmalloc( sizeof(*iface->attrs) );
        list_init( iface->attrs );
    }

    LIST_FOR_EACH_ENTRY(attr, iface->attrs, attr_t, entry)
        if (attr->type == ATTR_UUID) break;

    if (&attr->entry == iface->attrs)
    {
        attr = xmalloc( sizeof(*attr) );
        attr->type = ATTR_UUID;
        attr->u.pval = xmalloc( sizeof(GUID) );
        list_add_tail( iface->attrs, &attr->entry );
    }

    A_SHAInit(&sha_ctx);
    A_SHAUpdate(&sha_ctx, wrt_pinterface_namespace, sizeof(wrt_pinterface_namespace));
    A_SHAUpdate(&sha_ctx, (const UCHAR *)iface->signature, strlen(iface->signature));
    A_SHAFinal(&sha_ctx, (ULONG *)hash);

    /* https://tools.ietf.org/html/rfc4122:

       * Set the four most significant bits (bits 12 through 15) of the
         time_hi_and_version field to the appropriate 4-bit version number
         from Section 4.1.3.

       * Set the two most significant bits (bits 6 and 7) of the
         clock_seq_hi_and_reserved to zero and one, respectively.
    */

    hash[6] = ((hash[6] & 0x0f) | (version << 4));
    hash[8] = ((hash[8] & 0x3f) | 0x80);

    uuid = attr->u.pval;
    uuid->Data1 = ((DWORD)hash[0] << 24)|((DWORD)hash[1] << 16)|((DWORD)hash[2] << 8)|(DWORD)hash[3];
    uuid->Data2 = ((WORD)hash[4] << 8)|(WORD)hash[5];
    uuid->Data3 = ((WORD)hash[6] << 8)|(WORD)hash[7];
    memcpy(&uuid->Data4, hash + 8, sizeof(*uuid) - offsetof(GUID, Data4));
}

static type_t *replace_type_parameters_in_type(type_t *type, type_list_t *orig, type_list_t *repl);

static type_list_t *replace_type_parameters_in_type_list(type_list_t *type_list, type_list_t *orig, type_list_t *repl)
{
    type_list_t *entry, *new_entry, **next, *first = NULL;

    if (!type_list) return type_list;

    next = &first;
    for (entry = type_list; entry; entry = entry->next)
    {
        new_entry = xmalloc(sizeof(*new_entry));
        new_entry->type = replace_type_parameters_in_type(entry->type, orig, repl);
        new_entry->next = NULL;
        *next = new_entry;
        next = &new_entry->next;
    }

    return first;
}

static var_t *replace_type_parameters_in_var(var_t *var, type_list_t *orig, type_list_t *repl)
{
    var_t *new_var = xmalloc(sizeof(*new_var));
    *new_var = *var;
    list_init(&new_var->entry);
    new_var->declspec.type = replace_type_parameters_in_type(var->declspec.type, orig, repl);
    return new_var;
}

static var_list_t *replace_type_parameters_in_var_list(var_list_t *var_list, type_list_t *orig, type_list_t *repl)
{
    var_list_t *new_var_list;
    var_t *var, *new_var;

    if (!var_list) return var_list;

    new_var_list = xmalloc(sizeof(*new_var_list));
    list_init(new_var_list);

    LIST_FOR_EACH_ENTRY(var, var_list, var_t, entry)
    {
        new_var = replace_type_parameters_in_var(var, orig, repl);
        list_add_tail(new_var_list, &new_var->entry);
    }

    return new_var_list;
}

static statement_t *replace_type_parameters_in_statement(statement_t *stmt, type_list_t *orig, type_list_t *repl)
{
    statement_t *new_stmt = xmalloc(sizeof(*new_stmt));
    *new_stmt = *stmt;
    list_init(&new_stmt->entry);

    switch (stmt->type)
    {
    case STMT_DECLARATION:
        new_stmt->u.var = replace_type_parameters_in_var(stmt->u.var, orig, repl);
        break;
    case STMT_LIBRARY:
    case STMT_TYPE:
    case STMT_TYPEREF:
    case STMT_MODULE:
    case STMT_TYPEDEF:
        new_stmt->u.type_list = replace_type_parameters_in_type_list(stmt->u.type_list, orig, repl);
        break;
    case STMT_IMPORT:
    case STMT_IMPORTLIB:
    case STMT_PRAGMA:
    case STMT_CPPQUOTE:
        fprintf(stderr, "%d\n", stmt->type);
        assert(0);
        break;
    }

    return new_stmt;
}

static statement_list_t *replace_type_parameters_in_statement_list(statement_list_t *stmt_list, type_list_t *orig, type_list_t *repl)
{
    statement_list_t *new_stmt_list;
    statement_t *stmt, *new_stmt;

    if (!stmt_list) return stmt_list;

    new_stmt_list = xmalloc(sizeof(*new_stmt_list));
    list_init(new_stmt_list);

    LIST_FOR_EACH_ENTRY(stmt, stmt_list, statement_t, entry)
    {
        new_stmt = replace_type_parameters_in_statement(stmt, orig, repl);
        list_add_tail(new_stmt_list, &new_stmt->entry);
    }

    return new_stmt_list;
}

static type_t *replace_type_parameters_in_type(type_t *type, type_list_t *orig, type_list_t *repl)
{
    type_list_t *o, *r;
    type_t *t;

    if (!type) return type;
    switch (type->type_type)
    {
    case TYPE_VOID:
    case TYPE_BASIC:
    case TYPE_ENUM:
    case TYPE_BITFIELD:
    case TYPE_INTERFACE:
    case TYPE_RUNTIMECLASS:
    case TYPE_DELEGATE:
        return type;
    case TYPE_PARAMETER:
        for (o = orig, r = repl; o && r; o = o->next, r = r->next)
            if (type == o->type) return r->type;
        return type;
    case TYPE_POINTER:
        t = replace_type_parameters_in_type(type->details.pointer.ref.type, orig, repl);
        if (t == type->details.pointer.ref.type) return type;
        type = duptype(type, 0);
        type->details.pointer.ref.type = t;
        return type;
    case TYPE_ALIAS:
        t = replace_type_parameters_in_type(type->details.alias.aliasee.type, orig, repl);
        if (t == type->details.alias.aliasee.type) return type;
        type = duptype(type, 0);
        type->details.alias.aliasee.type = t;
        return type;
    case TYPE_ARRAY:
        t = replace_type_parameters_in_type(type->details.array.elem.type, orig, repl);
        if (t == t->details.array.elem.type) return type;
        type = duptype(type, 0);
        t->details.array.elem.type = t;
        return type;
    case TYPE_FUNCTION:
        t = duptype(type, 0);
        t->details.function = xmalloc(sizeof(*t->details.function));
        t->details.function->args = replace_type_parameters_in_var_list(type->details.function->args, orig, repl);
        t->details.function->retval = replace_type_parameters_in_var(type->details.function->retval, orig, repl);
        return t;
    case TYPE_PARAMETERIZED_TYPE:
        t = type->details.parameterized.type;
        if (t->type_type != TYPE_PARAMETERIZED_TYPE) return find_parameterized_type(type, repl, 0);
        repl = replace_type_parameters_in_type_list(type->details.parameterized.params, orig, repl);
        return replace_type_parameters_in_type(t, t->details.parameterized.params, repl);
    case TYPE_STRUCT:
    case TYPE_ENCAPSULATED_UNION:
    case TYPE_UNION:
    case TYPE_MODULE:
    case TYPE_COCLASS:
    case TYPE_APICONTRACT:
        assert(0); /* FIXME: implement when needed */
        break;
    }

    return type;
}

static void type_parameterized_interface_specialize(type_t *tmpl, type_t *iface, type_list_t *orig, type_list_t *repl)
{
    iface->details.iface = xmalloc(sizeof(*iface->details.iface));
    iface->details.iface->disp_methods = NULL;
    iface->details.iface->disp_props = NULL;
    iface->details.iface->stmts = replace_type_parameters_in_statement_list(tmpl->details.iface->stmts, orig, repl);
    iface->details.iface->inherit = replace_type_parameters_in_type(tmpl->details.iface->inherit, orig, repl);
    iface->details.iface->disp_inherit = NULL;
    iface->details.iface->async_iface = NULL;
    iface->details.iface->requires = NULL;
}

static void type_parameterized_delegate_specialize(type_t *tmpl, type_t *delegate, type_list_t *orig, type_list_t *repl)
{
    type_parameterized_interface_specialize(tmpl->details.delegate.iface, delegate->details.delegate.iface, orig, repl);
}

type_t *type_parameterized_type_specialize_partial(type_t *type, type_list_t *params)
{
    type_t *new_type = duptype(type, 0);
    new_type->details.parameterized.type = type;
    new_type->details.parameterized.params = params;
    return new_type;
}

type_t *type_parameterized_type_specialize_declare(type_t *type, type_list_t *params)
{
    type_t *tmpl = type->details.parameterized.type;
    type_t *new_type = duptype(tmpl, 0);

    new_type->namespace = type->namespace;
    new_type->name = format_parameterized_type_name(type, params);
    reg_type(new_type, new_type->name, new_type->namespace, 0);
    new_type->c_name = format_parameterized_type_c_name(type, params, "");
    new_type->short_name = format_parameterized_type_short_name(type, params, "");

    if (new_type->type_type == TYPE_DELEGATE)
    {
        new_type->details.delegate.iface = duptype(tmpl->details.delegate.iface, 0);
        compute_delegate_iface_name(new_type);
        new_type->details.delegate.iface->namespace = new_type->namespace;
        new_type->details.delegate.iface->c_name = format_parameterized_type_c_name(type, params, "I");
        new_type->details.delegate.iface->short_name = format_parameterized_type_short_name(type, params, "I");
    }

    return new_type;
}

type_t *type_parameterized_type_specialize_define(type_t *type, type_list_t *params)
{
    type_list_t *orig = type->details.parameterized.params;
    type_t *tmpl = type->details.parameterized.type;
    type_t *iface = find_parameterized_type(type, params, 0);

    if (tmpl->type_type == TYPE_INTERFACE)
        type_parameterized_interface_specialize(tmpl, iface, orig, params);
    else if (tmpl->type_type == TYPE_DELEGATE)
        type_parameterized_delegate_specialize(tmpl, iface, orig, params);
    else
    {
        error_loc("Unsupported parameterized type template %d\n", tmpl->type_type);
        return NULL;
    }

    iface->signature = format_parameterized_type_signature(type, params);
    iface->defined = TRUE;
    if (iface->type_type == TYPE_DELEGATE)
    {
        iface = iface->details.delegate.iface;
        iface->signature = format_parameterized_type_signature(type, params);
        iface->defined = TRUE;
    }
    compute_interface_signature_uuid(iface);
    compute_method_indexes(iface);
    return iface;
}

void type_parameterized_interface_declare(type_t *type, type_list_t *params)
{
    type_t *iface = make_type(TYPE_INTERFACE);
    type->type_type = TYPE_PARAMETERIZED_TYPE;
    type->details.parameterized.type = iface;
    type->details.parameterized.params = params;
}

void type_parameterized_interface_define(type_t *type, type_list_t *params, type_t *inherit, statement_list_t *stmts)
{
    type_t *iface;

    if (type->type_type != TYPE_PARAMETERIZED_TYPE) type_parameterized_interface_declare(type, params);
    iface = type->details.parameterized.type;

    /* The parameterized type UUID is actually a PIID that is then used as a seed to generate
     * a new type GUID with the rules described in:
     *   https://docs.microsoft.com/en-us/uwp/winrt-cref/winrt-type-system#parameterized-types
     * FIXME: store type signatures for generated interfaces, and generate their GUIDs
     */
    iface->details.iface = xmalloc(sizeof(*iface->details.iface));
    iface->details.iface->disp_props = NULL;
    iface->details.iface->disp_methods = NULL;
    iface->details.iface->stmts = stmts;
    iface->details.iface->inherit = inherit;
    iface->details.iface->disp_inherit = NULL;
    iface->details.iface->async_iface = NULL;
    iface->details.iface->requires = NULL;
}

void type_interface_define(type_t *iface, type_t *inherit, statement_list_t *stmts, type_list_t *requires)
{
    iface->details.iface = xmalloc(sizeof(*iface->details.iface));
    iface->details.iface->disp_props = NULL;
    iface->details.iface->disp_methods = NULL;
    iface->details.iface->stmts = stmts;
    iface->details.iface->inherit = inherit;
    iface->details.iface->disp_inherit = NULL;
    iface->details.iface->async_iface = NULL;
    iface->details.iface->requires = requires;
    iface->defined = TRUE;
    compute_method_indexes(iface);
}

void type_delegate_define(type_t *delegate, statement_list_t *stmts)
{
    type_t *iface = make_type(TYPE_INTERFACE);

    iface->namespace = delegate->namespace;
    iface->details.iface = xmalloc(sizeof(*iface->details.iface));
    iface->details.iface->disp_props = NULL;
    iface->details.iface->disp_methods = NULL;
    iface->details.iface->stmts = stmts;
    iface->details.iface->inherit = find_type("IUnknown", NULL, 0);
    if (!iface->details.iface->inherit) error_loc("IUnknown is undefined\n");
    iface->details.iface->disp_inherit = NULL;
    iface->details.iface->async_iface = NULL;
    iface->details.iface->requires = NULL;
    iface->defined = TRUE;
    compute_method_indexes(iface);

    delegate->details.delegate.iface = iface;
    compute_delegate_iface_name(delegate);
}

void type_parameterized_delegate_define(type_t *type, type_list_t *params, statement_list_t *stmts)
{
    type_t *delegate = make_type(TYPE_DELEGATE);
    type_t *iface = make_type(TYPE_INTERFACE);

    type->type_type = TYPE_PARAMETERIZED_TYPE;
    type->details.parameterized.type = delegate;
    type->details.parameterized.params = params;

    delegate->details.delegate.iface = iface;

    iface->details.iface = xmalloc(sizeof(*iface->details.iface));
    iface->details.iface->disp_props = NULL;
    iface->details.iface->disp_methods = NULL;
    iface->details.iface->stmts = stmts;
    iface->details.iface->inherit = find_type("IUnknown", NULL, 0);
    if (!iface->details.iface->inherit) error_loc("IUnknown is undefined\n");
    iface->details.iface->disp_inherit = NULL;
    iface->details.iface->async_iface = NULL;
    iface->details.iface->requires = NULL;
}

void type_dispinterface_define(type_t *iface, var_list_t *props, var_list_t *methods)
{
    iface->details.iface = xmalloc(sizeof(*iface->details.iface));
    iface->details.iface->disp_props = props;
    iface->details.iface->disp_methods = methods;
    iface->details.iface->stmts = NULL;
    iface->details.iface->inherit = find_type("IDispatch", NULL, 0);
    if (!iface->details.iface->inherit) error_loc("IDispatch is undefined\n");
    iface->details.iface->disp_inherit = NULL;
    iface->details.iface->async_iface = NULL;
    iface->details.iface->requires = NULL;
    iface->defined = TRUE;
    compute_method_indexes(iface);
}

void type_dispinterface_define_from_iface(type_t *dispiface, type_t *iface)
{
    dispiface->details.iface = xmalloc(sizeof(*dispiface->details.iface));
    dispiface->details.iface->disp_props = NULL;
    dispiface->details.iface->disp_methods = NULL;
    dispiface->details.iface->stmts = NULL;
    dispiface->details.iface->inherit = find_type("IDispatch", NULL, 0);
    if (!dispiface->details.iface->inherit) error_loc("IDispatch is undefined\n");
    dispiface->details.iface->disp_inherit = iface;
    dispiface->details.iface->async_iface = NULL;
    dispiface->details.iface->requires = NULL;
    dispiface->defined = TRUE;
    compute_method_indexes(dispiface);
}

void type_module_define(type_t *module, statement_list_t *stmts)
{
    if (module->details.module) error_loc("multiple definition error\n");
    module->details.module = xmalloc(sizeof(*module->details.module));
    module->details.module->stmts = stmts;
    module->defined = TRUE;
}

type_t *type_coclass_define(type_t *coclass, ifref_list_t *ifaces)
{
    coclass->details.coclass.ifaces = ifaces;
    coclass->defined = TRUE;
    return coclass;
}

type_t *type_runtimeclass_define(type_t *runtimeclass, ifref_list_t *ifaces)
{
    runtimeclass->details.runtimeclass.ifaces = ifaces;
    runtimeclass->defined = TRUE;
    return runtimeclass;
}

int type_is_equal(const type_t *type1, const type_t *type2)
{
    if (type_get_type_detect_alias(type1) != type_get_type_detect_alias(type2))
        return FALSE;

    if (type1->name && type2->name)
        return !strcmp(type1->name, type2->name);
    else if ((!type1->name && type2->name) || (type1->name && !type2->name))
        return FALSE;

    /* FIXME: do deep inspection of types to determine if they are equal */

    return FALSE;
}