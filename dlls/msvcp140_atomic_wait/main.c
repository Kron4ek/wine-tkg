/*
 * Copyright 2022 Daniel Lehman
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

#include <stdarg.h>
#include "stdint.h"
#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(msvcp);

extern unsigned int __cdecl _Thrd_hardware_concurrency(void);

unsigned int __stdcall __std_parallel_algorithms_hw_threads(void)
{
    TRACE("()\n");
    return _Thrd_hardware_concurrency();
}

void __stdcall __std_bulk_submit_threadpool_work(PTP_WORK work, size_t count)
{
    TRACE("(%p %Iu)\n", work, count);
    while (count--)
        SubmitThreadpoolWork(work);
}

void __stdcall __std_close_threadpool_work(PTP_WORK work)
{
    TRACE("(%p)\n", work);
    return CloseThreadpoolWork(work);
}

PTP_WORK __stdcall __std_create_threadpool_work(PTP_WORK_CALLBACK callback, void *context,
                                                PTP_CALLBACK_ENVIRON environ)
{
    TRACE("(%p %p %p)\n", callback, context, environ);
    return CreateThreadpoolWork(callback, context, environ);
}

void __stdcall __std_submit_threadpool_work(PTP_WORK work)
{
    TRACE("(%p)\n", work);
    return SubmitThreadpoolWork(work);
}

void __stdcall __std_wait_for_threadpool_work_callbacks(PTP_WORK work, BOOL cancel)
{
    TRACE("(%p %d)\n", work, cancel);
    return WaitForThreadpoolWorkCallbacks(work, cancel);
}

void __stdcall __std_atomic_notify_one_direct(void *addr)
{
    TRACE("(%p)\n", addr);
    WakeByAddressSingle(addr);
}

void __stdcall __std_atomic_notify_all_direct(void *addr)
{
    TRACE("(%p)\n", addr);
    WakeByAddressAll(addr);
}

BOOL __stdcall __std_atomic_wait_direct(volatile void *addr, void *cmp,
                                        SIZE_T size, DWORD timeout)
{
    TRACE("(%p %p %Id %ld)\n", addr, cmp, size, timeout);
    return WaitOnAddress(addr, cmp, size, timeout);
}

void __stdcall __std_execution_wait_on_uchar(void *addr, unsigned char val)
{
    TRACE("(%p %d)\n", addr, val);
    WaitOnAddress(addr, &val, sizeof(val), INFINITE);
}

void __stdcall __std_execution_wake_by_address_all(void *addr)
{
    TRACE("(%p)\n", addr);
    WakeByAddressAll(addr);
}

typedef struct
{
    SRWLOCK srwlock;
} shared_mutex;

struct shared_mutex_elem
{
    struct list entry;
    int ref;
    void *ptr;
    shared_mutex mutex;
};

static CRITICAL_SECTION shared_mutex_cs;
static CRITICAL_SECTION_DEBUG shared_mutex_cs_debug =
{
    0, 0, &shared_mutex_cs,
    { &shared_mutex_cs_debug.ProcessLocksList, &shared_mutex_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": shared_mutex_cs") }
};
static CRITICAL_SECTION shared_mutex_cs = { &shared_mutex_cs_debug, -1, 0, 0, 0, 0 };
static struct list shared_mutex_list = LIST_INIT(shared_mutex_list);

/* shared_mutex_cs must be held by caller */
static struct shared_mutex_elem* find_shared_mutex(void *ptr)
{
    struct shared_mutex_elem *sme;

    LIST_FOR_EACH_ENTRY(sme, &shared_mutex_list, struct shared_mutex_elem, entry)
    {
        if (sme->ptr == ptr)
            return sme;
    }
    return NULL;
}

shared_mutex* __stdcall __std_acquire_shared_mutex_for_instance(void *ptr)
{
    struct shared_mutex_elem *sme;

    TRACE("(%p)\n", ptr);

    EnterCriticalSection(&shared_mutex_cs);
    sme = find_shared_mutex(ptr);
    if (sme)
    {
        sme->ref++;
        LeaveCriticalSection(&shared_mutex_cs);
        return &sme->mutex;
    }

    sme = malloc(sizeof(*sme));
    sme->ref = 1;
    sme->ptr = ptr;
    InitializeSRWLock(&sme->mutex.srwlock);
    list_add_head(&shared_mutex_list, &sme->entry);
    LeaveCriticalSection(&shared_mutex_cs);
    return &sme->mutex;
}

void __stdcall __std_release_shared_mutex_for_instance(void *ptr)
{
    struct shared_mutex_elem *sme;

    TRACE("(%p)\n", ptr);

    EnterCriticalSection(&shared_mutex_cs);
    sme = find_shared_mutex(ptr);
    if (!sme)
    {
        LeaveCriticalSection(&shared_mutex_cs);
        return;
    }

    sme->ref--;
    if (!sme->ref)
    {
        list_remove(&sme->entry);
        free(sme);
    }
    LeaveCriticalSection(&shared_mutex_cs);
}

void* __stdcall __std_calloc_crt(size_t count, size_t size)
{
    return calloc(count, size);
}

void __stdcall __std_free_crt(void *ptr)
{
    free(ptr);
}

enum tzdb_error
{
    TZDB_ERROR_SUCCESS,
    TZDB_ERROR_WIN,
    TZDB_ERROR_ICU,
};

struct tzdb_time_zones
{
    enum tzdb_error error;
    char *ver;
    unsigned int count;
    char **names;
    char **links;
};

struct tzdb_current_zone
{
    enum tzdb_error error;
    char *name;
};

struct tzdb_leap_second
{
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t negative;
    uint16_t reserved;
};

struct tzdb_time_zones * __stdcall __std_tzdb_get_time_zones(void)
{
    DYNAMIC_TIME_ZONE_INFORMATION tzd;
    static char ver[] = "2022g";
    struct tzdb_time_zones *z;
    unsigned int i, j;

    FIXME("returning Windows time zone names.\n");

    z = calloc(1, sizeof(*z));
    while (!EnumDynamicTimeZoneInformation(z->count, &tzd))
        ++z->count;

    z->ver = ver;
    z->names = calloc(z->count, sizeof(*z->names));
    z->links = calloc(z->count, sizeof(*z->links));

    for (i = 0; i < z->count; ++i)
    {
        if (EnumDynamicTimeZoneInformation(i, &tzd))
            break;
        z->names[i] = malloc(wcslen(tzd.StandardName) + 1);
        j = 0;
        while ((z->names[i][j] = tzd.StandardName[j]))
            ++j;
    }
    z->count = i;
    return z;
}

void __stdcall __std_tzdb_delete_time_zones(struct tzdb_time_zones *z)
{
    unsigned int i;

    TRACE("(%p)\n", z);

    for (i = 0; i < z->count; ++i)
    {
        free(z->names[i]);
        free(z->links[i]);
    }
    free(z->names);
    free(z->links);
    free(z);
}

struct tzdb_current_zone * __stdcall __std_tzdb_get_current_zone(void)
{
    DYNAMIC_TIME_ZONE_INFORMATION tzd;
    struct tzdb_current_zone *c;
    unsigned int i;

    FIXME("returning Windows time zone name.\n");

    c = calloc(1, sizeof(*c));

    if (GetDynamicTimeZoneInformation(&tzd) == TIME_ZONE_ID_INVALID)
    {
        c->error = TZDB_ERROR_WIN;
        return c;
    }

    c->name = malloc(wcslen(tzd.StandardName) + 1);
    i = 0;
    while ((c->name[i] = tzd.StandardName[i]))
        ++i;
    return c;
}

void __stdcall __std_tzdb_delete_current_zone(struct tzdb_current_zone *c)
{
    TRACE("(%p)\n", c);

    free(c->name);
    free(c);
}

struct tzdb_leap_second * __stdcall __std_tzdb_get_leap_seconds(size_t prev_size, size_t *new_size)
{
    FIXME("(%#Ix %p) stub\n", prev_size, new_size);

    *new_size = 0;
    return NULL;
}

void __stdcall __std_tzdb_delete_leap_seconds(struct tzdb_leap_second *l)
{
    TRACE("(%p)\n", l);

    free(l);
}
