/*
 * atexit implementation
 *
 * Copyright 2026 Jacek Caban for CodeWeavers
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

#if 0
#pragma makedep implib
#endif

#ifdef __WINE_PE_BUILD

#include <process.h>
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <wine/asm.h>

static _onexit_table_t atexit_table = { 0 };

int __cdecl atexit(_PVFV func)
{
    return _register_onexit_function(&atexit_table, (_onexit_t)func);
}

void __wine_exec_atexit(void)
{
    _execute_onexit_table(&atexit_table);
}

__ASM_SECTION_POINTER( ".section .CRT$XTB", __wine_exec_atexit )

#endif
