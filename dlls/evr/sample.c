/*
 * Copyright 2020 Nikolay Sivov
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

#define COBJMACROS

#include "evr.h"
#include "mfapi.h"
#include "mferror.h"
#include "d3d9.h"
#include "dxva2api.h"

#include "wine/debug.h"
#include "wine/heap.h"

WINE_DEFAULT_DEBUG_CHANNEL(evr);

static const char *debugstr_time(LONGLONG time)
{
    ULONGLONG abstime = time >= 0 ? time : -time;
    unsigned int i = 0, j = 0;
    char buffer[23], rev[23];

    while (abstime || i <= 8)
    {
        buffer[i++] = '0' + (abstime % 10);
        abstime /= 10;
        if (i == 7) buffer[i++] = '.';
    }
    if (time < 0) buffer[i++] = '-';

    while (i--) rev[j++] = buffer[i];
    while (rev[j-1] == '0' && rev[j-2] != '.') --j;
    rev[j] = 0;

    return wine_dbg_sprintf("%s", rev);
}

struct surface_buffer
{
    IMFMediaBuffer IMFMediaBuffer_iface;
    IMFGetService IMFGetService_iface;
    LONG refcount;

    IUnknown *surface;
    ULONG length;
};

struct video_sample
{
    IMFSample IMFSample_iface;
    IMFTrackedSample IMFTrackedSample_iface;
    IMFDesiredSample IMFDesiredSample_iface;
    LONG refcount;

    IMFSample *sample;

    IMFAsyncResult *tracked_result;
    LONG tracked_refcount;

    LONGLONG desired_time;
    LONGLONG desired_duration;
    BOOL desired_set;
};

static struct video_sample *impl_from_IMFSample(IMFSample *iface)
{
    return CONTAINING_RECORD(iface, struct video_sample, IMFSample_iface);
}

static struct video_sample *impl_from_IMFTrackedSample(IMFTrackedSample *iface)
{
    return CONTAINING_RECORD(iface, struct video_sample, IMFTrackedSample_iface);
}

static struct video_sample *impl_from_IMFDesiredSample(IMFDesiredSample *iface)
{
    return CONTAINING_RECORD(iface, struct video_sample, IMFDesiredSample_iface);
}

static struct surface_buffer *impl_from_IMFMediaBuffer(IMFMediaBuffer *iface)
{
    return CONTAINING_RECORD(iface, struct surface_buffer, IMFMediaBuffer_iface);
}

static struct surface_buffer *impl_from_IMFGetService(IMFGetService *iface)
{
    return CONTAINING_RECORD(iface, struct surface_buffer, IMFGetService_iface);
}

struct sample_allocator
{
    IMFVideoSampleAllocator IMFVideoSampleAllocator_iface;
    IMFVideoSampleAllocatorCallback IMFVideoSampleAllocatorCallback_iface;
    LONG refcount;

    IMFVideoSampleAllocatorNotify *callback;
    unsigned int free_samples;
    IDirect3DDeviceManager9 *device_manager;
    CRITICAL_SECTION cs;
};

static struct sample_allocator *impl_from_IMFVideoSampleAllocator(IMFVideoSampleAllocator *iface)
{
    return CONTAINING_RECORD(iface, struct sample_allocator, IMFVideoSampleAllocator_iface);
}

static struct sample_allocator *impl_from_IMFVideoSampleAllocatorCallback(IMFVideoSampleAllocatorCallback *iface)
{
    return CONTAINING_RECORD(iface, struct sample_allocator, IMFVideoSampleAllocatorCallback_iface);
}

static HRESULT WINAPI sample_allocator_QueryInterface(IMFVideoSampleAllocator *iface, REFIID riid, void **obj)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocator(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IMFVideoSampleAllocator) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = &allocator->IMFVideoSampleAllocator_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFVideoSampleAllocatorCallback))
    {
        *obj = &allocator->IMFVideoSampleAllocatorCallback_iface;
    }
    else
    {
        WARN("Unsupported interface %s.\n", debugstr_guid(riid));
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI sample_allocator_AddRef(IMFVideoSampleAllocator *iface)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocator(iface);
    ULONG refcount = InterlockedIncrement(&allocator->refcount);

    TRACE("%p, refcount %u.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI sample_allocator_Release(IMFVideoSampleAllocator *iface)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocator(iface);
    ULONG refcount = InterlockedDecrement(&allocator->refcount);

    TRACE("%p, refcount %u.\n", iface, refcount);

    if (!refcount)
    {
        if (allocator->callback)
            IMFVideoSampleAllocatorNotify_Release(allocator->callback);
        if (allocator->device_manager)
            IDirect3DDeviceManager9_Release(allocator->device_manager);
        DeleteCriticalSection(&allocator->cs);
        heap_free(allocator);
    }

    return refcount;
}

static HRESULT WINAPI sample_allocator_SetDirectXManager(IMFVideoSampleAllocator *iface,
        IUnknown *manager)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocator(iface);
    IDirect3DDeviceManager9 *device_manager = NULL;
    HRESULT hr;

    TRACE("%p, %p.\n", iface, manager);

    if (manager && FAILED(hr = IUnknown_QueryInterface(manager, &IID_IDirect3DDeviceManager9,
            (void **)&device_manager)))
    {
        return hr;
    }

    EnterCriticalSection(&allocator->cs);

    if (allocator->device_manager)
        IDirect3DDeviceManager9_Release(allocator->device_manager);
    allocator->device_manager = device_manager;

    LeaveCriticalSection(&allocator->cs);

    return S_OK;
}

static HRESULT WINAPI sample_allocator_UninitializeSampleAllocator(IMFVideoSampleAllocator *iface)
{
    FIXME("%p.\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI sample_allocator_InitializeSampleAllocator(IMFVideoSampleAllocator *iface,
        DWORD sample_count, IMFMediaType *media_type)
{
    FIXME("%p, %u, %p.\n", iface, sample_count, media_type);

    return E_NOTIMPL;
}

static HRESULT WINAPI sample_allocator_AllocateSample(IMFVideoSampleAllocator *iface, IMFSample **sample)
{
    FIXME("%p, %p.\n", iface, sample);

    return E_NOTIMPL;
}

static const IMFVideoSampleAllocatorVtbl sample_allocator_vtbl =
{
    sample_allocator_QueryInterface,
    sample_allocator_AddRef,
    sample_allocator_Release,
    sample_allocator_SetDirectXManager,
    sample_allocator_UninitializeSampleAllocator,
    sample_allocator_InitializeSampleAllocator,
    sample_allocator_AllocateSample,
};

static HRESULT WINAPI sample_allocator_callback_QueryInterface(IMFVideoSampleAllocatorCallback *iface,
        REFIID riid, void **obj)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocatorCallback(iface);
    return IMFVideoSampleAllocator_QueryInterface(&allocator->IMFVideoSampleAllocator_iface, riid, obj);
}

static ULONG WINAPI sample_allocator_callback_AddRef(IMFVideoSampleAllocatorCallback *iface)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocatorCallback(iface);
    return IMFVideoSampleAllocator_AddRef(&allocator->IMFVideoSampleAllocator_iface);
}

static ULONG WINAPI sample_allocator_callback_Release(IMFVideoSampleAllocatorCallback *iface)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocatorCallback(iface);
    return IMFVideoSampleAllocator_Release(&allocator->IMFVideoSampleAllocator_iface);
}

static HRESULT WINAPI sample_allocator_callback_SetCallback(IMFVideoSampleAllocatorCallback *iface,
        IMFVideoSampleAllocatorNotify *callback)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocatorCallback(iface);

    TRACE("%p, %p.\n", iface, callback);

    EnterCriticalSection(&allocator->cs);
    if (allocator->callback)
        IMFVideoSampleAllocatorNotify_Release(allocator->callback);
    allocator->callback = callback;
    if (allocator->callback)
        IMFVideoSampleAllocatorNotify_AddRef(allocator->callback);
    LeaveCriticalSection(&allocator->cs);

    return S_OK;
}

static HRESULT WINAPI sample_allocator_callback_GetFreeSampleCount(IMFVideoSampleAllocatorCallback *iface,
        LONG *count)
{
    struct sample_allocator *allocator = impl_from_IMFVideoSampleAllocatorCallback(iface);

    TRACE("%p, %p.\n", iface, count);

    EnterCriticalSection(&allocator->cs);
    if (count)
        *count = allocator->free_samples;
    LeaveCriticalSection(&allocator->cs);

    return S_OK;
}

static const IMFVideoSampleAllocatorCallbackVtbl sample_allocator_callback_vtbl =
{
    sample_allocator_callback_QueryInterface,
    sample_allocator_callback_AddRef,
    sample_allocator_callback_Release,
    sample_allocator_callback_SetCallback,
    sample_allocator_callback_GetFreeSampleCount,
};

HRESULT WINAPI MFCreateVideoSampleAllocator(REFIID riid, void **obj)
{
    struct sample_allocator *object;
    HRESULT hr;

    TRACE("%s, %p.\n", debugstr_guid(riid), obj);

    if (!(object = heap_alloc_zero(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IMFVideoSampleAllocator_iface.lpVtbl = &sample_allocator_vtbl;
    object->IMFVideoSampleAllocatorCallback_iface.lpVtbl = &sample_allocator_callback_vtbl;
    object->refcount = 1;
    InitializeCriticalSection(&object->cs);

    hr = IMFVideoSampleAllocator_QueryInterface(&object->IMFVideoSampleAllocator_iface, riid, obj);
    IMFVideoSampleAllocator_Release(&object->IMFVideoSampleAllocator_iface);

    return hr;
}

static HRESULT WINAPI video_sample_QueryInterface(IMFSample *iface, REFIID riid, void **out)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualIID(riid, &IID_IMFSample) ||
            IsEqualIID(riid, &IID_IMFAttributes) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *out = &sample->IMFSample_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFTrackedSample))
    {
        *out = &sample->IMFTrackedSample_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFDesiredSample))
    {
        *out = &sample->IMFDesiredSample_iface;
    }
    else
    {
        WARN("Unsupported %s.\n", debugstr_guid(riid));
        *out = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI video_sample_AddRef(IMFSample *iface)
{
    struct video_sample *sample = impl_from_IMFSample(iface);
    ULONG refcount = InterlockedIncrement(&sample->refcount);

    TRACE("%p, refcount %u.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI video_sample_Release(IMFSample *iface)
{
    struct video_sample *sample = impl_from_IMFSample(iface);
    ULONG refcount;
    HRESULT hr;

    IMFSample_LockStore(sample->sample);
    refcount = InterlockedDecrement(&sample->refcount);
    if (sample->tracked_result && sample->tracked_refcount == refcount)
    {
        /* Call could fail if queue system is not initialized, it's not critical. */
        if (FAILED(hr = MFInvokeCallback(sample->tracked_result)))
            WARN("Failed to invoke tracking callback, hr %#x.\n", hr);
        IMFAsyncResult_Release(sample->tracked_result);
        sample->tracked_result = NULL;
        sample->tracked_refcount = 0;
    }
    IMFSample_UnlockStore(sample->sample);

    TRACE("%p, refcount %u.\n", iface, refcount);

    if (!refcount)
    {
        if (sample->sample)
            IMFSample_Release(sample->sample);
        heap_free(sample);
    }

    return refcount;
}

static HRESULT WINAPI video_sample_GetItem(IMFSample *iface, REFGUID key, PROPVARIANT *value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), value);

    return IMFSample_GetItem(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_GetItemType(IMFSample *iface, REFGUID key, MF_ATTRIBUTE_TYPE *type)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), type);

    return IMFSample_GetItemType(sample->sample, key, type);
}

static HRESULT WINAPI video_sample_CompareItem(IMFSample *iface, REFGUID key, REFPROPVARIANT value, BOOL *result)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p, %p.\n", iface, debugstr_guid(key), value, result);

    return IMFSample_CompareItem(sample->sample, key, value, result);
}

static HRESULT WINAPI video_sample_Compare(IMFSample *iface, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE type,
        BOOL *result)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p, %d, %p.\n", iface, theirs, type, result);

    return IMFSample_Compare(sample->sample, theirs, type, result);
}

static HRESULT WINAPI video_sample_GetUINT32(IMFSample *iface, REFGUID key, UINT32 *value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), value);

    return IMFSample_GetUINT32(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_GetUINT64(IMFSample *iface, REFGUID key, UINT64 *value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), value);

    return IMFSample_GetUINT64(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_GetDouble(IMFSample *iface, REFGUID key, double *value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), value);

    return IMFSample_GetDouble(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_GetGUID(IMFSample *iface, REFGUID key, GUID *value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), value);

    return IMFSample_GetGUID(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_GetStringLength(IMFSample *iface, REFGUID key, UINT32 *length)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), length);

    return IMFSample_GetStringLength(sample->sample, key, length);
}

static HRESULT WINAPI video_sample_GetString(IMFSample *iface, REFGUID key, WCHAR *value, UINT32 size, UINT32 *length)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p, %u, %p.\n", iface, debugstr_guid(key), value, size, length);

    return IMFSample_GetString(sample->sample, key, value, size, length);
}

static HRESULT WINAPI video_sample_GetAllocatedString(IMFSample *iface, REFGUID key, WCHAR **value, UINT32 *length)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p, %p.\n", iface, debugstr_guid(key), value, length);

    return IMFSample_GetAllocatedString(sample->sample, key, value, length);
}

static HRESULT WINAPI video_sample_GetBlobSize(IMFSample *iface, REFGUID key, UINT32 *size)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), size);

    return IMFSample_GetBlobSize(sample->sample, key, size);
}

static HRESULT WINAPI video_sample_GetBlob(IMFSample *iface, REFGUID key, UINT8 *buf, UINT32 bufsize, UINT32 *blobsize)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p, %u, %p.\n", iface, debugstr_guid(key), buf, bufsize, blobsize);

    return IMFSample_GetBlob(sample->sample, key, buf, bufsize, blobsize);
}

static HRESULT WINAPI video_sample_GetAllocatedBlob(IMFSample *iface, REFGUID key, UINT8 **buf, UINT32 *size)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p, %p.\n", iface, debugstr_guid(key), buf, size);

    return IMFSample_GetAllocatedBlob(sample->sample, key, buf, size);
}

static HRESULT WINAPI video_sample_GetUnknown(IMFSample *iface, REFGUID key, REFIID riid, void **out)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %s, %p.\n", iface, debugstr_guid(key), debugstr_guid(riid), out);

    return IMFSample_GetUnknown(sample->sample, key, riid, out);
}

static HRESULT WINAPI video_sample_SetItem(IMFSample *iface, REFGUID key, REFPROPVARIANT value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), value);

    return IMFSample_SetItem(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_DeleteItem(IMFSample *iface, REFGUID key)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s.\n", iface, debugstr_guid(key));

    return IMFSample_DeleteItem(sample->sample, key);
}

static HRESULT WINAPI video_sample_DeleteAllItems(IMFSample *iface)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p.\n", iface);

    return IMFSample_DeleteAllItems(sample->sample);
}

static HRESULT WINAPI video_sample_SetUINT32(IMFSample *iface, REFGUID key, UINT32 value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %u.\n", iface, debugstr_guid(key), value);

    return IMFSample_SetUINT32(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_SetUINT64(IMFSample *iface, REFGUID key, UINT64 value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %s.\n", iface, debugstr_guid(key), wine_dbgstr_longlong(value));

    return IMFSample_SetUINT64(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_SetDouble(IMFSample *iface, REFGUID key, double value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %f.\n", iface, debugstr_guid(key), value);

    return IMFSample_SetDouble(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_SetGUID(IMFSample *iface, REFGUID key, REFGUID value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %s.\n", iface, debugstr_guid(key), debugstr_guid(value));

    return IMFSample_SetGUID(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_SetString(IMFSample *iface, REFGUID key, const WCHAR *value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %s.\n", iface, debugstr_guid(key), debugstr_w(value));

    return IMFSample_SetString(sample->sample, key, value);
}

static HRESULT WINAPI video_sample_SetBlob(IMFSample *iface, REFGUID key, const UINT8 *buf, UINT32 size)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p, %u.\n", iface, debugstr_guid(key), buf, size);

    return IMFSample_SetBlob(sample->sample, key, buf, size);
}

static HRESULT WINAPI video_sample_SetUnknown(IMFSample *iface, REFGUID key, IUnknown *unknown)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(key), unknown);

    return IMFSample_SetUnknown(sample->sample, key, unknown);
}

static HRESULT WINAPI video_sample_LockStore(IMFSample *iface)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p.\n", iface);

    return IMFSample_LockStore(sample->sample);
}

static HRESULT WINAPI video_sample_UnlockStore(IMFSample *iface)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p.\n", iface);

    return IMFSample_UnlockStore(sample->sample);
}

static HRESULT WINAPI video_sample_GetCount(IMFSample *iface, UINT32 *count)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p.\n", iface, count);

    return IMFSample_GetCount(sample->sample, count);
}

static HRESULT WINAPI video_sample_GetItemByIndex(IMFSample *iface, UINT32 index, GUID *key, PROPVARIANT *value)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %u, %p, %p.\n", iface, index, key, value);

    return IMFSample_GetItemByIndex(sample->sample, index, key, value);
}

static HRESULT WINAPI video_sample_CopyAllItems(IMFSample *iface, IMFAttributes *dest)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p.\n", iface, dest);

    return IMFSample_CopyAllItems(sample->sample, dest);
}

static HRESULT WINAPI video_sample_GetSampleFlags(IMFSample *iface, DWORD *flags)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p.\n", iface, flags);

    return IMFSample_GetSampleFlags(sample->sample, flags);
}

static HRESULT WINAPI video_sample_SetSampleFlags(IMFSample *iface, DWORD flags)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %#x.\n", iface, flags);

    return IMFSample_SetSampleFlags(sample->sample, flags);
}

static HRESULT WINAPI video_sample_GetSampleTime(IMFSample *iface, LONGLONG *timestamp)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p.\n", iface, timestamp);

    return IMFSample_GetSampleTime(sample->sample, timestamp);
}

static HRESULT WINAPI video_sample_SetSampleTime(IMFSample *iface, LONGLONG timestamp)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s.\n", iface, debugstr_time(timestamp));

    return IMFSample_SetSampleTime(sample->sample, timestamp);
}

static HRESULT WINAPI video_sample_GetSampleDuration(IMFSample *iface, LONGLONG *duration)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p.\n", iface, duration);

    return IMFSample_GetSampleDuration(sample->sample, duration);
}

static HRESULT WINAPI video_sample_SetSampleDuration(IMFSample *iface, LONGLONG duration)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %s.\n", iface, debugstr_time(duration));

    return IMFSample_SetSampleDuration(sample->sample, duration);
}

static HRESULT WINAPI video_sample_GetBufferCount(IMFSample *iface, DWORD *count)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p.\n", iface, count);

    return IMFSample_GetBufferCount(sample->sample, count);
}

static HRESULT WINAPI video_sample_GetBufferByIndex(IMFSample *iface, DWORD index, IMFMediaBuffer **buffer)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %u, %p.\n", iface, index, buffer);

    return IMFSample_GetBufferByIndex(sample->sample, index, buffer);
}

static HRESULT WINAPI video_sample_ConvertToContiguousBuffer(IMFSample *iface, IMFMediaBuffer **buffer)
{
    TRACE("%p, %p.\n", iface, buffer);

    return E_NOTIMPL;
}

static HRESULT WINAPI video_sample_AddBuffer(IMFSample *iface, IMFMediaBuffer *buffer)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %p.\n", iface, buffer);

    return IMFSample_AddBuffer(sample->sample, buffer);
}

static HRESULT WINAPI video_sample_RemoveBufferByIndex(IMFSample *iface, DWORD index)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p, %u.\n", iface, index);

    return IMFSample_RemoveBufferByIndex(sample->sample, index);
}

static HRESULT WINAPI video_sample_RemoveAllBuffers(IMFSample *iface)
{
    struct video_sample *sample = impl_from_IMFSample(iface);

    TRACE("%p.\n", iface);

    return IMFSample_RemoveAllBuffers(sample->sample);
}

static HRESULT WINAPI video_sample_GetTotalLength(IMFSample *iface, DWORD *total_length)
{
    TRACE("%p, %p.\n", iface, total_length);

    *total_length = 0;

    return S_OK;
}

static HRESULT WINAPI video_sample_CopyToBuffer(IMFSample *iface, IMFMediaBuffer *buffer)
{
    TRACE("%p, %p.\n", iface, buffer);

    return E_NOTIMPL;
}

static const IMFSampleVtbl video_sample_vtbl =
{
    video_sample_QueryInterface,
    video_sample_AddRef,
    video_sample_Release,
    video_sample_GetItem,
    video_sample_GetItemType,
    video_sample_CompareItem,
    video_sample_Compare,
    video_sample_GetUINT32,
    video_sample_GetUINT64,
    video_sample_GetDouble,
    video_sample_GetGUID,
    video_sample_GetStringLength,
    video_sample_GetString,
    video_sample_GetAllocatedString,
    video_sample_GetBlobSize,
    video_sample_GetBlob,
    video_sample_GetAllocatedBlob,
    video_sample_GetUnknown,
    video_sample_SetItem,
    video_sample_DeleteItem,
    video_sample_DeleteAllItems,
    video_sample_SetUINT32,
    video_sample_SetUINT64,
    video_sample_SetDouble,
    video_sample_SetGUID,
    video_sample_SetString,
    video_sample_SetBlob,
    video_sample_SetUnknown,
    video_sample_LockStore,
    video_sample_UnlockStore,
    video_sample_GetCount,
    video_sample_GetItemByIndex,
    video_sample_CopyAllItems,
    video_sample_GetSampleFlags,
    video_sample_SetSampleFlags,
    video_sample_GetSampleTime,
    video_sample_SetSampleTime,
    video_sample_GetSampleDuration,
    video_sample_SetSampleDuration,
    video_sample_GetBufferCount,
    video_sample_GetBufferByIndex,
    video_sample_ConvertToContiguousBuffer,
    video_sample_AddBuffer,
    video_sample_RemoveBufferByIndex,
    video_sample_RemoveAllBuffers,
    video_sample_GetTotalLength,
    video_sample_CopyToBuffer,
};

static HRESULT WINAPI tracked_video_sample_QueryInterface(IMFTrackedSample *iface, REFIID riid, void **obj)
{
    struct video_sample *sample = impl_from_IMFTrackedSample(iface);
    return IMFSample_QueryInterface(&sample->IMFSample_iface, riid, obj);
}

static ULONG WINAPI tracked_video_sample_AddRef(IMFTrackedSample *iface)
{
    struct video_sample *sample = impl_from_IMFTrackedSample(iface);
    return IMFSample_AddRef(&sample->IMFSample_iface);
}

static ULONG WINAPI tracked_video_sample_Release(IMFTrackedSample *iface)
{
    struct video_sample *sample = impl_from_IMFTrackedSample(iface);
    return IMFSample_Release(&sample->IMFSample_iface);
}

static HRESULT WINAPI tracked_video_sample_SetAllocator(IMFTrackedSample *iface,
        IMFAsyncCallback *sample_allocator, IUnknown *state)
{
    struct video_sample *sample = impl_from_IMFTrackedSample(iface);
    HRESULT hr = S_OK;

    TRACE("%p, %p, %p.\n", iface, sample_allocator, state);

    IMFSample_LockStore(sample->sample);

    if (sample->tracked_result)
        hr = MF_E_NOTACCEPTING;
    else
    {
        if (SUCCEEDED(hr = MFCreateAsyncResult((IUnknown *)iface, sample_allocator, state, &sample->tracked_result)))
        {
            /* Account for additional refcount brought by 'state' object. This threshold is used
               on Release() to invoke tracker callback.  */
            sample->tracked_refcount = 1;
            if (state == (IUnknown *)&sample->IMFTrackedSample_iface ||
                    state == (IUnknown *)&sample->IMFSample_iface)
            {
                ++sample->tracked_refcount;
            }
        }
    }

    IMFSample_UnlockStore(sample->sample);

    return hr;
}

static const IMFTrackedSampleVtbl tracked_video_sample_vtbl =
{
    tracked_video_sample_QueryInterface,
    tracked_video_sample_AddRef,
    tracked_video_sample_Release,
    tracked_video_sample_SetAllocator,
};

static HRESULT WINAPI desired_video_sample_QueryInterface(IMFDesiredSample *iface, REFIID riid, void **obj)
{
    struct video_sample *sample = impl_from_IMFDesiredSample(iface);
    return IMFSample_QueryInterface(&sample->IMFSample_iface, riid, obj);
}

static ULONG WINAPI desired_video_sample_AddRef(IMFDesiredSample *iface)
{
    struct video_sample *sample = impl_from_IMFDesiredSample(iface);
    return IMFSample_AddRef(&sample->IMFSample_iface);
}

static ULONG WINAPI desired_video_sample_Release(IMFDesiredSample *iface)
{
    struct video_sample *sample = impl_from_IMFDesiredSample(iface);
    return IMFSample_Release(&sample->IMFSample_iface);
}

static HRESULT WINAPI desired_video_sample_GetDesiredSampleTimeAndDuration(IMFDesiredSample *iface,
        LONGLONG *sample_time, LONGLONG *sample_duration)
{
    struct video_sample *sample = impl_from_IMFDesiredSample(iface);
    HRESULT hr = S_OK;

    TRACE("%p, %p, %p.\n", iface, sample_time, sample_duration);

    if (!sample_time || !sample_duration)
        return E_POINTER;

    IMFSample_LockStore(sample->sample);
    if (sample->desired_set)
    {
        *sample_time = sample->desired_time;
        *sample_duration = sample->desired_duration;
    }
    else
        hr = MF_E_NOT_AVAILABLE;
    IMFSample_UnlockStore(sample->sample);

    return hr;
}

static void WINAPI desired_video_sample_SetDesiredSampleTimeAndDuration(IMFDesiredSample *iface,
        LONGLONG sample_time, LONGLONG sample_duration)
{
    struct video_sample *sample = impl_from_IMFDesiredSample(iface);

    TRACE("%p, %s, %s.\n", iface, debugstr_time(sample_time), debugstr_time(sample_duration));

    IMFSample_LockStore(sample->sample);
    sample->desired_set = TRUE;
    sample->desired_time = sample_time;
    sample->desired_duration = sample_duration;
    IMFSample_UnlockStore(sample->sample);
}

static void WINAPI desired_video_sample_Clear(IMFDesiredSample *iface)
{
    struct video_sample *sample = impl_from_IMFDesiredSample(iface);

    TRACE("%p.\n", iface);

    IMFSample_LockStore(sample->sample);
    sample->desired_set = FALSE;
    IMFSample_UnlockStore(sample->sample);
}

static const IMFDesiredSampleVtbl desired_video_sample_vtbl =
{
    desired_video_sample_QueryInterface,
    desired_video_sample_AddRef,
    desired_video_sample_Release,
    desired_video_sample_GetDesiredSampleTimeAndDuration,
    desired_video_sample_SetDesiredSampleTimeAndDuration,
    desired_video_sample_Clear,
};

static HRESULT WINAPI surface_buffer_QueryInterface(IMFMediaBuffer *iface, REFIID riid, void **obj)
{
    struct surface_buffer *buffer = impl_from_IMFMediaBuffer(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IMFMediaBuffer) || IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = &buffer->IMFMediaBuffer_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFGetService))
    {
        *obj = &buffer->IMFGetService_iface;
    }
    else
    {
        WARN("Unsupported interface %s.\n", debugstr_guid(riid));
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI surface_buffer_AddRef(IMFMediaBuffer *iface)
{
    struct surface_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    ULONG refcount = InterlockedIncrement(&buffer->refcount);

    TRACE("%p, refcount %u.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI surface_buffer_Release(IMFMediaBuffer *iface)
{
    struct surface_buffer *buffer = impl_from_IMFMediaBuffer(iface);
    ULONG refcount = InterlockedDecrement(&buffer->refcount);

    TRACE("%p, refcount %u.\n", iface, refcount);

    if (!refcount)
    {
        IUnknown_Release(buffer->surface);
        heap_free(buffer);
    }

    return refcount;
}

static HRESULT WINAPI surface_buffer_Lock(IMFMediaBuffer *iface, BYTE **data, DWORD *maxlength, DWORD *length)
{
    TRACE("%p, %p, %p, %p.\n", iface, data, maxlength, length);

    return E_NOTIMPL;
}

static HRESULT WINAPI surface_buffer_Unlock(IMFMediaBuffer *iface)
{
    TRACE("%p.\n", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI surface_buffer_GetCurrentLength(IMFMediaBuffer *iface, DWORD *length)
{
    struct surface_buffer *buffer = impl_from_IMFMediaBuffer(iface);

    TRACE("%p.\n", iface);

    *length = buffer->length;

    return S_OK;
}

static HRESULT WINAPI surface_buffer_SetCurrentLength(IMFMediaBuffer *iface, DWORD length)
{
    struct surface_buffer *buffer = impl_from_IMFMediaBuffer(iface);

    TRACE("%p.\n", iface);

    buffer->length = length;

    return S_OK;
}

static HRESULT WINAPI surface_buffer_GetMaxLength(IMFMediaBuffer *iface, DWORD *length)
{
    TRACE("%p.\n", iface);

    return E_NOTIMPL;
}

static const IMFMediaBufferVtbl surface_buffer_vtbl =
{
    surface_buffer_QueryInterface,
    surface_buffer_AddRef,
    surface_buffer_Release,
    surface_buffer_Lock,
    surface_buffer_Unlock,
    surface_buffer_GetCurrentLength,
    surface_buffer_SetCurrentLength,
    surface_buffer_GetMaxLength,
};

static HRESULT WINAPI surface_buffer_gs_QueryInterface(IMFGetService *iface, REFIID riid, void **obj)
{
    struct surface_buffer *buffer = impl_from_IMFGetService(iface);
    return IMFMediaBuffer_QueryInterface(&buffer->IMFMediaBuffer_iface, riid, obj);
}

static ULONG WINAPI surface_buffer_gs_AddRef(IMFGetService *iface)
{
    struct surface_buffer *buffer = impl_from_IMFGetService(iface);
    return IMFMediaBuffer_AddRef(&buffer->IMFMediaBuffer_iface);
}

static ULONG WINAPI surface_buffer_gs_Release(IMFGetService *iface)
{
    struct surface_buffer *buffer = impl_from_IMFGetService(iface);
    return IMFMediaBuffer_Release(&buffer->IMFMediaBuffer_iface);
}

static HRESULT WINAPI surface_buffer_gs_GetService(IMFGetService *iface, REFGUID service, REFIID riid, void **obj)
{
    struct surface_buffer *buffer = impl_from_IMFGetService(iface);

    TRACE("%p, %s, %s, %p.\n", iface, debugstr_guid(service), debugstr_guid(riid), obj);

    if (IsEqualGUID(service, &MR_BUFFER_SERVICE))
        return IUnknown_QueryInterface(buffer->surface, riid, obj);

    return E_NOINTERFACE;
}

static const IMFGetServiceVtbl surface_buffer_gs_vtbl =
{
    surface_buffer_gs_QueryInterface,
    surface_buffer_gs_AddRef,
    surface_buffer_gs_Release,
    surface_buffer_gs_GetService,
};

static HRESULT create_surface_buffer(IUnknown *surface, IMFMediaBuffer **buffer)
{
    struct surface_buffer *object;

    if (!(object = heap_alloc_zero(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IMFMediaBuffer_iface.lpVtbl = &surface_buffer_vtbl;
    object->IMFGetService_iface.lpVtbl = &surface_buffer_gs_vtbl;
    object->refcount = 1;
    object->surface = surface;
    IUnknown_AddRef(object->surface);

    *buffer = &object->IMFMediaBuffer_iface;

    return S_OK;
}

HRESULT WINAPI MFCreateVideoSampleFromSurface(IUnknown *surface, IMFSample **sample)
{
    struct video_sample *object;
    IMFMediaBuffer *buffer = NULL;
    HRESULT hr;

    TRACE("%p, %p.\n", surface, sample);

    if (!(object = heap_alloc_zero(sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IMFSample_iface.lpVtbl = &video_sample_vtbl;
    object->IMFTrackedSample_iface.lpVtbl = &tracked_video_sample_vtbl;
    object->IMFDesiredSample_iface.lpVtbl = &desired_video_sample_vtbl;
    object->refcount = 1;

    if (FAILED(hr = MFCreateSample(&object->sample)))
    {
        heap_free(object);
        return hr;
    }

    if (surface && FAILED(hr = create_surface_buffer(surface, &buffer)))
    {
        IMFSample_Release(&object->IMFSample_iface);
        return hr;
    }

    if (buffer)
        IMFSample_AddBuffer(object->sample, buffer);

    *sample = &object->IMFSample_iface;

    return S_OK;
}
