/*
 * HTTP network byte stream implementation
 *
 * Copyright 2024 Torge Matthies for CodeWeavers
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

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winhttp.h"
#include "nserror.h"
#include "mfidl.h"
#include "rpcproxy.h"
#include "rtworkq.h"
#include "bcrypt.h"
#include "pathcch.h"

#include "mfplat_private.h"

#include "wine/debug.h"
#include "wine/list.h"
#include "wine/mfinternal.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

struct bytestream_http;

static HRESULT send_characteristics_changed_event(struct bytestream_http *object);

static HRESULT send_buffering_event(struct bytestream_http *object, BOOL buffering);
static HRESULT update_buffering_status(struct bytestream_http *object);

static HRESULT do_read(struct bytestream_http *object, BYTE *buffer, ULONG size, ULONG *read_len);

static HRESULT create_temp_file(HANDLE *h);

static HRESULT open_connection(struct bytestream_http *object, ULONGLONG range_start, ULONGLONG range_end);
static void close_connection(struct bytestream_http *object, BOOL wait);

static BOOL is_range_available(struct bytestream_http *object, ULONGLONG start, ULONGLONG count);
static ULONGLONG find_range_end(struct bytestream_http *object, ULONGLONG start);
static ULONGLONG find_hole_end(struct bytestream_http *object, ULONGLONG start);
static void mark_range_valid(struct bytestream_http *object, ULONGLONG start, ULONGLONG count);

struct downloaded_range
{
    struct list entry;
    ULONGLONG start;
    ULONGLONG end;
};

struct bytestream_http
{
    struct attributes attributes;
    IMFByteStream IMFByteStream_iface;
    IMFByteStreamCacheControl IMFByteStreamCacheControl_iface;
    IMFByteStreamBuffering IMFByteStreamBuffering_iface;
    IMFMediaEventGenerator IMFMediaEventGenerator_iface;
    IMFByteStreamTimeSeek IMFByteStreamTimeSeek_iface;
    IPropertyStore IPropertyStore_iface;
    IMFGetService IMFGetService_iface;

    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;

    HRESULT error;

    IRtwqAsyncCallback read_callback;
    struct list pending;
    ULONGLONG position;
    BOOL has_pending_read;

    WCHAR *url;

    IPropertyStore *propstore;
    IMFMediaEventQueue *event_queue;

    BOOL closing_connnection;
    HINTERNET session;
    HINTERNET connection;
    HINTERNET request;

    BOOL parsed_headers;
    const WCHAR *content_type;
    BOOL has_length;
    ULONGLONG content_length;
    ULONGLONG last_modified;
    ULONGLONG content_range_start;
    ULONGLONG content_range_end;

    BOOL buffering_enabled;
    BOOL buffering;

    struct list downloaded_ranges;
    HANDLE cache_file;

    ULONGLONG current_write_pos;
    BOOL read_active;
    DWORD bytes_read;
    BYTE buffer[1024*1024];
};

static struct bytestream_http *impl_from_IMFAttributes(IMFAttributes *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, attributes.IMFAttributes_iface);
}

static struct bytestream_http *impl_from_IMFByteStream(IMFByteStream *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, IMFByteStream_iface);
}

static struct bytestream_http *impl_from_IMFByteStreamCacheControl(IMFByteStreamCacheControl *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, IMFByteStreamCacheControl_iface);
}

static struct bytestream_http *impl_from_IMFByteStreamBuffering(IMFByteStreamBuffering *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, IMFByteStreamBuffering_iface);
}

static struct bytestream_http *impl_from_IMFMediaEventGenerator(IMFMediaEventGenerator *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, IMFMediaEventGenerator_iface);
}

static struct bytestream_http *impl_from_IMFByteStreamTimeSeek(IMFByteStreamTimeSeek *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, IMFByteStreamTimeSeek_iface);
}

static struct bytestream_http *impl_from_IPropertyStore(IPropertyStore *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, IPropertyStore_iface);
}

static struct bytestream_http *impl_from_IMFGetService(IMFGetService *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, IMFGetService_iface);
}

enum async_stream_op_type
{
    ASYNC_STREAM_OP_READ,
};

struct async_stream_op
{
    IUnknown IUnknown_iface;
    LONG refcount;
    union
    {
        const BYTE *src;
        BYTE *dest;
    } u;
    QWORD position;
    ULONG requested_length;
    ULONG actual_length;
    IMFAsyncResult *caller;
    struct list entry;
    enum async_stream_op_type type;
};

static struct async_stream_op *impl_async_stream_op_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct async_stream_op, IUnknown_iface);
}

static HRESULT WINAPI async_stream_op_QueryInterface(IUnknown *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported %s.\n", debugstr_guid(riid));
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI async_stream_op_AddRef(IUnknown *iface)
{
    struct async_stream_op *op = impl_async_stream_op_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&op->refcount);

    TRACE("%p, refcount %ld.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI async_stream_op_Release(IUnknown *iface)
{
    struct async_stream_op *op = impl_async_stream_op_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&op->refcount);

    TRACE("%p, refcount %ld.\n", iface, refcount);

    if (!refcount)
    {
        if (op->caller)
            IMFAsyncResult_Release(op->caller);
        free(op);
    }

    return refcount;
}

static const IUnknownVtbl async_stream_op_vtbl =
{
    async_stream_op_QueryInterface,
    async_stream_op_AddRef,
    async_stream_op_Release,
};

static HRESULT WINAPI bytestream_http_QueryInterface(IMFByteStream *iface, REFIID riid, void **out)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), out);

    if (IsEqualIID(riid, &IID_IMFByteStream) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *out = &object->IMFByteStream_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFAttributes))
    {
        *out = &object->attributes.IMFAttributes_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFByteStream))
    {
        *out = &object->IMFByteStream_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFByteStreamCacheControl))
    {
        *out = &object->IMFByteStreamCacheControl_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFByteStreamBuffering))
    {
        *out = &object->IMFByteStreamBuffering_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFMediaEventGenerator))
    {
        *out = &object->IMFMediaEventGenerator_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFByteStreamTimeSeek))
    {
        *out = &object->IMFByteStreamTimeSeek_iface;
    }
    else if (IsEqualIID(riid, &IID_IPropertyStore))
    {
        *out = &object->IPropertyStore_iface;
    }
    else if (IsEqualIID(riid, &IID_IMFGetService))
    {
        *out = &object->IMFGetService_iface;
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

static ULONG WINAPI bytestream_http_AddRef(IMFByteStream *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);
    ULONG refcount = InterlockedIncrement(&object->attributes.ref);

    TRACE("%p, refcount %ld.\n", iface, refcount);

    return refcount;
}

static ULONG WINAPI bytestream_http_Release(IMFByteStream *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);
    ULONG refcount = InterlockedDecrement(&object->attributes.ref);
    struct async_stream_op *cur, *cur2;

    TRACE("%p, refcount %ld.\n", iface, refcount);

    if (!refcount)
    {
        close_connection(object, FALSE);
        clear_attributes_object(&object->attributes);
        LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &object->pending, struct async_stream_op, entry)
        {
            list_remove(&cur->entry);
            IUnknown_Release(&cur->IUnknown_iface);
        }
        if (object->event_queue)
            IMFMediaEventQueue_Release(object->event_queue);
        if (object->propstore)
            IPropertyStore_Release(object->propstore);
        if (object->cache_file && object->cache_file != INVALID_HANDLE_VALUE)
            CloseHandle(object->cache_file);
        DeleteCriticalSection(&object->cs);
        free((void *)object->content_type);
        free((void *)object->url);
        free(object);
    }

    return refcount;
}

static struct bytestream_http *impl_from_read_callback_IRtwqAsyncCallback(IRtwqAsyncCallback *iface)
{
    return CONTAINING_RECORD(iface, struct bytestream_http, read_callback);
}

static HRESULT bytestream_http_create_io_request(struct bytestream_http *object, enum async_stream_op_type type,
        const BYTE *data, ULONG size, IMFAsyncCallback *callback, IUnknown *state)
{
    struct async_stream_op *op;
    IRtwqAsyncResult *request;
    HRESULT hr;

    if (type != ASYNC_STREAM_OP_READ)
        return E_NOTIMPL;

    op = malloc(sizeof(*op));
    if (!op)
        return E_OUTOFMEMORY;

    op->IUnknown_iface.lpVtbl = &async_stream_op_vtbl;
    op->refcount = 1;
    op->u.src = data;
    op->position = object->position;
    op->requested_length = size;
    op->type = type;
    if (FAILED(hr = RtwqCreateAsyncResult((IUnknown *)&object->IMFByteStream_iface, (IRtwqAsyncCallback *)callback,
            state, (IRtwqAsyncResult **)&op->caller)))
    {
        goto failed;
    }

    if (FAILED(hr = RtwqCreateAsyncResult(&op->IUnknown_iface, &object->read_callback, NULL, &request)))
        goto failed;

    RtwqPutWorkItem(MFASYNC_CALLBACK_QUEUE_IO, 0, request);
    IRtwqAsyncResult_Release(request);

failed:
    IUnknown_Release(&op->IUnknown_iface);
    return hr;
}

static HRESULT bytestream_http_complete_io_request(struct bytestream_http *object, enum async_stream_op_type type,
        IMFAsyncResult *result, ULONG *actual_length)
{
    struct async_stream_op *op = NULL, *cur;
    HRESULT hr;

    EnterCriticalSection(&object->cs);
    LIST_FOR_EACH_ENTRY(cur, &object->pending, struct async_stream_op, entry)
    {
        if (cur->caller == result && cur->type == type)
        {
            op = cur;
            list_remove(&cur->entry);
            break;
        }
    }
    LeaveCriticalSection(&object->cs);

    if (!op)
        return E_INVALIDARG;

    if (SUCCEEDED(hr = IMFAsyncResult_GetStatus(result)))
        *actual_length = op->actual_length;

    IUnknown_Release(&op->IUnknown_iface);

    return hr;
}

static HRESULT WINAPI bytestream_http_callback_QueryInterface(IRtwqAsyncCallback *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IRtwqAsyncCallback) ||
            IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IRtwqAsyncCallback_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported %s.\n", debugstr_guid(riid));
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI bytestream_http_read_callback_AddRef(IRtwqAsyncCallback *iface)
{
    struct bytestream_http *object = impl_from_read_callback_IRtwqAsyncCallback(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_read_callback_Release(IRtwqAsyncCallback *iface)
{
    struct bytestream_http *object = impl_from_read_callback_IRtwqAsyncCallback(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

static HRESULT WINAPI bytestream_http_callback_GetParameters(IRtwqAsyncCallback *iface, DWORD *flags, DWORD *queue)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI bytestream_http_read_callback_Invoke(IRtwqAsyncCallback *iface, IRtwqAsyncResult *result)
{
    struct bytestream_http *object = impl_from_read_callback_IRtwqAsyncCallback(iface);
    struct async_stream_op *op;
    IUnknown *resobj;
    HRESULT hr;

    if (FAILED(hr = IRtwqAsyncResult_GetObject(result, &resobj)))
        return hr;

    op = impl_async_stream_op_from_IUnknown(resobj);

    EnterCriticalSection(&object->cs);
    hr = do_read(object, op->u.dest, op->requested_length, &op->actual_length);
    if(FAILED(hr)) TRACE("Read failed: %#lx\n", hr);
    IMFAsyncResult_SetStatus(op->caller, hr);
    list_add_tail(&object->pending, &op->entry);
    LeaveCriticalSection(&object->cs);

    MFInvokeCallback(op->caller);

    return S_OK;
}

static const IRtwqAsyncCallbackVtbl bytestream_http_read_callback_vtbl =
{
    bytestream_http_callback_QueryInterface,
    bytestream_http_read_callback_AddRef,
    bytestream_http_read_callback_Release,
    bytestream_http_callback_GetParameters,
    bytestream_http_read_callback_Invoke,
};

static HRESULT WINAPI bytestream_http_GetCapabilities(IMFByteStream *iface, DWORD *capabilities)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);

    TRACE("%p, %p.\n", iface, capabilities);

    EnterCriticalSection(&object->cs);
    *capabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_SEEKABLE;
    if (!object->has_length || !is_range_available(object, 0, object->content_length))
        *capabilities |= MFBYTESTREAM_IS_PARTIALLY_DOWNLOADED;
    LeaveCriticalSection(&object->cs);
    return S_OK;
}

static HRESULT WINAPI bytestream_http_SetLength(IMFByteStream *iface, QWORD length)
{
    TRACE("%p, %s\n", iface, wine_dbgstr_longlong(length));

    return E_NOTIMPL;
}

static HRESULT WINAPI bytestream_http_GetCurrentPosition(IMFByteStream *iface, QWORD *position)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);

    TRACE("%p, %p.\n", iface, position);

    if (!position)
        return E_INVALIDARG;

    EnterCriticalSection(&object->cs);
    *position = object->position;
    LeaveCriticalSection(&object->cs);
    return S_OK;
}

static HRESULT WINAPI bytestream_http_GetLength(IMFByteStream *iface, QWORD *length)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);

    TRACE("%p, %p.\n", iface, length);

    EnterCriticalSection(&object->cs);

    if (!object->has_length)
    {
        LeaveCriticalSection(&object->cs);
        return MF_E_BYTESTREAM_UNKNOWN_LENGTH;
    }

    *length = object->content_length;

    LeaveCriticalSection(&object->cs);
    return S_OK;
}

static HRESULT WINAPI bytestream_http_IsEndOfStream(IMFByteStream *iface, BOOL *ret)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);

    TRACE("%p, %p.\n", iface, ret);

    EnterCriticalSection(&object->cs);
    *ret = object->position >= object->content_length;
    LeaveCriticalSection(&object->cs);
    return S_OK;
}

static HRESULT send_characteristics_changed_event(struct bytestream_http *object)
{
    PROPVARIANT data = {.vt = VT_EMPTY};
    GUID ext_type = GUID_NULL;
    HRESULT hr;

    TRACE("%p.\n", object);

    if (FAILED(hr = IMFMediaEventQueue_QueueEventParamVar(object->event_queue, MEByteStreamCharacteristicsChanged,
                                                          &ext_type, S_OK, &data)))
        ERR("Failed to enqueue event %x, hr %#lx.\n", MEByteStreamCharacteristicsChanged, hr);

    return hr;
}

static HRESULT do_read(struct bytestream_http *object, BYTE *buffer, ULONG size, ULONG *read_len)
{
    DWORD prev_caps = 0, new_caps = 0;
    LARGE_INTEGER position;
    HRESULT hr = S_OK;
    ULONGLONG end;
    BOOL ret;

    position.QuadPart = object->position;
    if (!object->error && !is_range_available(object, position.QuadPart, size))
    {
        while (!object->error && object->has_pending_read)
            SleepConditionVariableCS(&object->cv, &object->cs, INFINITE);

        object->has_pending_read = TRUE;

        if (!object->request || object->closing_connnection || object->current_write_pos > position.QuadPart ||
            object->current_write_pos + 12288 <= position.QuadPart)
        {
            end = find_hole_end(object, position.QuadPart);
            if (FAILED(hr = open_connection(object, position.QuadPart, end)))
            {
                object->has_pending_read = FALSE;
                return hr;
            }
        }

        while (!object->error && !is_range_available(object, position.QuadPart, size))
            SleepConditionVariableCS(&object->cv, &object->cs, INFINITE);

        object->has_pending_read = FALSE;
    }

    if (object->error)
    {
        hr = object->error;
        return hr;
    }

    if (object->has_length && position.QuadPart + size > object->content_length)
        size = object->content_length - position.QuadPart;
    if ((ret = SetFilePointerEx(object->cache_file, position, NULL, FILE_BEGIN)))
    {
        if ((ret = ReadFile(object->cache_file, buffer, size, read_len, NULL)))
        {
            bytestream_http_GetCapabilities(&object->IMFByteStream_iface, &prev_caps);

            object->position += *read_len;

            bytestream_http_GetCapabilities(&object->IMFByteStream_iface, &new_caps);
            if (prev_caps != new_caps)
                send_characteristics_changed_event(object);
        }
    }

    if (!ret)
        hr = HRESULT_FROM_WIN32(GetLastError());

    return hr;
}

static HRESULT WINAPI bytestream_http_Read(IMFByteStream *iface, BYTE *buffer, ULONG size, ULONG *read_len)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);
    HRESULT hr;

    TRACE("%p, %p, %lu, %p.\n", iface, buffer, size, read_len);

    EnterCriticalSection(&object->cs);
    hr = do_read(object, buffer, size, read_len);
    LeaveCriticalSection(&object->cs);
    return hr;
}

static HRESULT WINAPI bytestream_http_BeginRead(IMFByteStream *iface, BYTE *data, ULONG size, IMFAsyncCallback *callback,
        IUnknown *state)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);

    TRACE("%p, %p, %lu, %p, %p.\n", iface, data, size, callback, state);

    return bytestream_http_create_io_request(object, ASYNC_STREAM_OP_READ, data, size, callback, state);
}

static HRESULT WINAPI bytestream_http_EndRead(IMFByteStream *iface, IMFAsyncResult *result, ULONG *byte_read)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);

    TRACE("%p, %p, %p.\n", iface, result, byte_read);

    return bytestream_http_complete_io_request(object, ASYNC_STREAM_OP_READ, result, byte_read);
}

static HRESULT WINAPI bytestream_http_Write(IMFByteStream *iface, const BYTE *data, ULONG size, ULONG *written)
{
    TRACE("%p, %p, %lu, %p\n", iface, data, size, written);

    return E_NOTIMPL;
}

static HRESULT WINAPI bytestream_http_BeginWrite(IMFByteStream *iface, const BYTE *data, ULONG size,
        IMFAsyncCallback *callback, IUnknown *state)
{
    TRACE("%p, %p, %lu, %p, %p.\n", iface, data, size, callback, state);

    return E_NOTIMPL;
}

static HRESULT WINAPI bytestream_http_EndWrite(IMFByteStream *iface, IMFAsyncResult *result, ULONG *written)
{
    TRACE("%p, %p, %p.\n", iface, result, written);

    return E_NOTIMPL;
}

static HRESULT WINAPI bytestream_http_Seek(IMFByteStream *iface, MFBYTESTREAM_SEEK_ORIGIN origin, LONGLONG offset,
                                           DWORD flags, QWORD *current)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);
    HRESULT hr = S_OK;

    TRACE("%p, %u, %s, %#lx, %p.\n", iface, origin, wine_dbgstr_longlong(offset), flags, current);

    EnterCriticalSection(&object->cs);

    switch (origin)
    {
        case msoBegin:
            object->position = offset;
            break;
        case msoCurrent:
            object->position += offset;
            break;
        default:
            WARN("Unknown origin mode %d.\n", origin);
            hr = E_INVALIDARG;
    }

    *current = object->position;

    LeaveCriticalSection(&object->cs);

    return hr;
}

static HRESULT WINAPI bytestream_http_Flush(IMFByteStream *iface)
{
    TRACE("%p\n", iface);

    return S_OK;
}

static HRESULT WINAPI bytestream_http_Close(IMFByteStream *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStream(iface);
    struct async_stream_op *cur, *cur2;

    TRACE("%p\n", iface);

    EnterCriticalSection(&object->cs);
    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &object->pending, struct async_stream_op, entry)
    {
        list_remove(&cur->entry);
        IUnknown_Release(&cur->IUnknown_iface);
    }
    object->error = MF_E_SHUTDOWN;
    close_connection(object, TRUE);
    IMFMediaEventQueue_Shutdown(object->event_queue);
    LeaveCriticalSection(&object->cs);
    return S_OK;
}

static HRESULT WINAPI bytestream_http_SetCurrentPosition(IMFByteStream *iface, QWORD position)
{
    QWORD new_pos;

    TRACE("%p, %s.\n", iface, wine_dbgstr_longlong(position));

    return IMFByteStream_Seek(iface, msoBegin, position, 0, &new_pos);
}

static const IMFByteStreamVtbl bytestream_http_vtbl =
{
    bytestream_http_QueryInterface,
    bytestream_http_AddRef,
    bytestream_http_Release,
    bytestream_http_GetCapabilities,
    bytestream_http_GetLength,
    bytestream_http_SetLength,
    bytestream_http_GetCurrentPosition,
    bytestream_http_SetCurrentPosition,
    bytestream_http_IsEndOfStream,
    bytestream_http_Read,
    bytestream_http_BeginRead,
    bytestream_http_EndRead,
    bytestream_http_Write,
    bytestream_http_BeginWrite,
    bytestream_http_EndWrite,
    bytestream_http_Seek,
    bytestream_http_Flush,
    bytestream_http_Close,
};

static HRESULT WINAPI bytestream_http_attributes_QueryInterface(IMFAttributes *iface, REFIID riid, void **out)
{
    struct bytestream_http *object = impl_from_IMFAttributes(iface);
    return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, out);
}

static ULONG WINAPI bytestream_http_attributes_AddRef(IMFAttributes *iface)
{
    struct bytestream_http *object = impl_from_IMFAttributes(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_attributes_Release(IMFAttributes *iface)
{
    struct bytestream_http *object = impl_from_IMFAttributes(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

HRESULT WINAPI mfattributes_GetItem(IMFAttributes *iface, REFGUID key, PROPVARIANT *value);
HRESULT WINAPI mfattributes_GetItemType(IMFAttributes *iface, REFGUID key, MF_ATTRIBUTE_TYPE *type);
HRESULT WINAPI mfattributes_CompareItem(IMFAttributes *iface, REFGUID key, REFPROPVARIANT value, BOOL *result);
HRESULT WINAPI mfattributes_Compare(IMFAttributes *iface, IMFAttributes *theirs,
                                    MF_ATTRIBUTES_MATCH_TYPE match_type, BOOL *ret);
HRESULT WINAPI mfattributes_GetUINT32(IMFAttributes *iface, REFGUID key, UINT32 *value);
HRESULT WINAPI mfattributes_GetUINT64(IMFAttributes *iface, REFGUID key, UINT64 *value);
HRESULT WINAPI mfattributes_GetDouble(IMFAttributes *iface, REFGUID key, double *value);
HRESULT WINAPI mfattributes_GetGUID(IMFAttributes *iface, REFGUID key, GUID *value);
HRESULT WINAPI mfattributes_GetStringLength(IMFAttributes *iface, REFGUID key, UINT32 *length);
HRESULT WINAPI mfattributes_GetString(IMFAttributes *iface, REFGUID key, WCHAR *value,
                                      UINT32 size, UINT32 *length);
HRESULT WINAPI mfattributes_GetAllocatedString(IMFAttributes *iface, REFGUID key, WCHAR **value, UINT32 *length);
HRESULT WINAPI mfattributes_GetBlobSize(IMFAttributes *iface, REFGUID key, UINT32 *size);
HRESULT WINAPI mfattributes_GetBlob(IMFAttributes *iface, REFGUID key, UINT8 *buf,
                                    UINT32 bufsize, UINT32 *blobsize);
HRESULT WINAPI mfattributes_GetAllocatedBlob(IMFAttributes *iface, REFGUID key, UINT8 **buf, UINT32 *size);
HRESULT WINAPI mfattributes_GetUnknown(IMFAttributes *iface, REFGUID key, REFIID riid, void **out);
HRESULT WINAPI mfattributes_SetItem(IMFAttributes *iface, REFGUID key, REFPROPVARIANT value);
HRESULT WINAPI mfattributes_DeleteItem(IMFAttributes *iface, REFGUID key);
HRESULT WINAPI mfattributes_DeleteAllItems(IMFAttributes *iface);
HRESULT WINAPI mfattributes_SetUINT32(IMFAttributes *iface, REFGUID key, UINT32 value);
HRESULT WINAPI mfattributes_SetUINT64(IMFAttributes *iface, REFGUID key, UINT64 value);
HRESULT WINAPI mfattributes_SetDouble(IMFAttributes *iface, REFGUID key, double value);
HRESULT WINAPI mfattributes_SetGUID(IMFAttributes *iface, REFGUID key, REFGUID value);
HRESULT WINAPI mfattributes_SetString(IMFAttributes *iface, REFGUID key, const WCHAR *value);
HRESULT WINAPI mfattributes_SetBlob(IMFAttributes *iface, REFGUID key, const UINT8 *buf, UINT32 size);
HRESULT WINAPI mfattributes_SetUnknown(IMFAttributes *iface, REFGUID key, IUnknown *unknown);
HRESULT WINAPI mfattributes_LockStore(IMFAttributes *iface);
HRESULT WINAPI mfattributes_UnlockStore(IMFAttributes *iface);
HRESULT WINAPI mfattributes_GetCount(IMFAttributes *iface, UINT32 *count);
HRESULT WINAPI mfattributes_GetItemByIndex(IMFAttributes *iface, UINT32 index, GUID *key, PROPVARIANT *value);
HRESULT WINAPI mfattributes_CopyAllItems(IMFAttributes *iface, IMFAttributes *dest);

static const IMFAttributesVtbl bytestream_http_attributes_vtbl =
{
    bytestream_http_attributes_QueryInterface,
    bytestream_http_attributes_AddRef,
    bytestream_http_attributes_Release,
    mfattributes_GetItem,
    mfattributes_GetItemType,
    mfattributes_CompareItem,
    mfattributes_Compare,
    mfattributes_GetUINT32,
    mfattributes_GetUINT64,
    mfattributes_GetDouble,
    mfattributes_GetGUID,
    mfattributes_GetStringLength,
    mfattributes_GetString,
    mfattributes_GetAllocatedString,
    mfattributes_GetBlobSize,
    mfattributes_GetBlob,
    mfattributes_GetAllocatedBlob,
    mfattributes_GetUnknown,
    mfattributes_SetItem,
    mfattributes_DeleteItem,
    mfattributes_DeleteAllItems,
    mfattributes_SetUINT32,
    mfattributes_SetUINT64,
    mfattributes_SetDouble,
    mfattributes_SetGUID,
    mfattributes_SetString,
    mfattributes_SetBlob,
    mfattributes_SetUnknown,
    mfattributes_LockStore,
    mfattributes_UnlockStore,
    mfattributes_GetCount,
    mfattributes_GetItemByIndex,
    mfattributes_CopyAllItems
};

static HRESULT WINAPI bytestream_http_IMFByteStreamCacheControl_QueryInterface(IMFByteStreamCacheControl *iface,
        REFIID riid, void **obj)
{
    struct bytestream_http *object = impl_from_IMFByteStreamCacheControl(iface);
    return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, obj);
}

static ULONG WINAPI bytestream_http_IMFByteStreamCacheControl_AddRef(IMFByteStreamCacheControl *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamCacheControl(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_IMFByteStreamCacheControl_Release(IMFByteStreamCacheControl *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamCacheControl(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

static HRESULT WINAPI bytestream_http_IMFByteStreamCacheControl_StopBackgroundTransfer(IMFByteStreamCacheControl *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamCacheControl(iface);

    TRACE("%p.\n", iface);

    IMFByteStreamBuffering_StopBuffering(&object->IMFByteStreamBuffering_iface);
    return S_OK;
}

static const IMFByteStreamCacheControlVtbl bytestream_http_IMFByteStreamCacheControl_vtbl =
{
    bytestream_http_IMFByteStreamCacheControl_QueryInterface,
    bytestream_http_IMFByteStreamCacheControl_AddRef,
    bytestream_http_IMFByteStreamCacheControl_Release,
    bytestream_http_IMFByteStreamCacheControl_StopBackgroundTransfer,
};

static HRESULT WINAPI bytestream_http_IMFByteStreamBuffering_QueryInterface(IMFByteStreamBuffering *iface,
        REFIID riid, void **obj)
{
    struct bytestream_http *object = impl_from_IMFByteStreamBuffering(iface);
    return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, obj);
}

static ULONG WINAPI bytestream_http_IMFByteStreamBuffering_AddRef(IMFByteStreamBuffering *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamBuffering(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_IMFByteStreamBuffering_Release(IMFByteStreamBuffering *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamBuffering(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

static HRESULT WINAPI bytestream_http_IMFByteStreamBuffering_SetBufferingParams(IMFByteStreamBuffering *iface,
        MFBYTESTREAM_BUFFERING_PARAMS *params)
{
    FIXME("%p, %p: semi-stub.\n", iface, params);

    if (!params || (params->cBuckets > 0 && !params->prgBuckets))
        return E_INVALIDARG;

    /* It's not clear to me how these parameters are used, it's probably fine to not implement them */
    return E_NOTIMPL;
}

static HRESULT send_buffering_event(struct bytestream_http *object, BOOL buffering)
{
    MediaEventType event_type = buffering ? MEBufferingStarted : MEBufferingStopped;
    PROPVARIANT data = {.vt = VT_EMPTY};
    GUID ext_type = GUID_NULL;
    HRESULT hr;

    TRACE("%p, %d.\n", object, buffering);

    if (!object->buffering_enabled)
        return S_FALSE;

    if (FAILED(hr = IMFMediaEventQueue_QueueEventParamVar(object->event_queue, event_type, &ext_type, S_OK, &data)))
        ERR("Failed to enqueue event %lx, hr %#lx.\n", event_type, hr);

    return hr;
}

static HRESULT update_buffering_status(struct bytestream_http *object)
{
    BOOL prev = object->buffering;

    TRACE("%p.\n", object);

    object->buffering = is_range_available(object, object->position, 65536);
    if (object->buffering == prev)
        return S_OK;
    return send_buffering_event(object, object->buffering);
}

static HRESULT WINAPI bytestream_http_IMFByteStreamBuffering_EnableBuffering(IMFByteStreamBuffering *iface,
        BOOL enable)
{
    struct bytestream_http *object = impl_from_IMFByteStreamBuffering(iface);

    TRACE("%p, %d.\n", iface, enable);

    EnterCriticalSection(&object->cs);
    if (!enable && object->buffering)
        send_buffering_event(object, FALSE);
    object->buffering_enabled = enable;
    if (enable && object->buffering)
        send_buffering_event(object, TRUE);
    LeaveCriticalSection(&object->cs);
    return S_OK;
}

static HRESULT WINAPI bytestream_http_IMFByteStreamBuffering_StopBuffering(IMFByteStreamBuffering *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamBuffering(iface);

    TRACE("%p.\n", iface);

    EnterCriticalSection(&object->cs);

    if (!object->buffering_enabled || !object->request)
    {
        LeaveCriticalSection(&object->cs);
        return S_FALSE;
    }

    if (!object->has_pending_read)
    {
        close_connection(object, TRUE);
        if (object->buffering)
        {
            object->buffering = FALSE;
            send_buffering_event(object, FALSE);
        }
    }

    LeaveCriticalSection(&object->cs);
    return S_OK;
}

static const IMFByteStreamBufferingVtbl bytestream_http_IMFByteStreamBuffering_vtbl =
{
    bytestream_http_IMFByteStreamBuffering_QueryInterface,
    bytestream_http_IMFByteStreamBuffering_AddRef,
    bytestream_http_IMFByteStreamBuffering_Release,
    bytestream_http_IMFByteStreamBuffering_SetBufferingParams,
    bytestream_http_IMFByteStreamBuffering_EnableBuffering,
    bytestream_http_IMFByteStreamBuffering_StopBuffering,
};

static HRESULT WINAPI bytestream_http_IMFByteStreamTimeSeek_QueryInterface(IMFByteStreamTimeSeek *iface,
        REFIID riid, void **obj)
{
    struct bytestream_http *object = impl_from_IMFByteStreamTimeSeek(iface);
    return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, obj);
}

static ULONG WINAPI bytestream_http_IMFByteStreamTimeSeek_AddRef(IMFByteStreamTimeSeek *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamTimeSeek(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_IMFByteStreamTimeSeek_Release(IMFByteStreamTimeSeek *iface)
{
    struct bytestream_http *object = impl_from_IMFByteStreamTimeSeek(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

static HRESULT WINAPI bytestream_http_IMFByteStreamTimeSeek_IsTimeSeekSupported(IMFByteStreamTimeSeek *iface,
        BOOL *result)
{
    TRACE("%p, %p.\n", iface, result);

    if (result)
        *result = FALSE;
    return S_FALSE;
}

static HRESULT WINAPI bytestream_http_IMFByteStreamTimeSeek_TimeSeek(IMFByteStreamTimeSeek *iface, QWORD position)
{
    TRACE("%p, %s.\n", iface, wine_dbgstr_longlong(position));

    return MF_E_INVALIDREQUEST;
}

static HRESULT WINAPI bytestream_http_IMFByteStreamTimeSeek_GetTimeSeekResult(IMFByteStreamTimeSeek *iface,
        QWORD *start_time, QWORD *stop_time, QWORD *duration)
{
    TRACE("%p, %p, %p, %p.\n", iface, start_time, stop_time, duration);

    if (!start_time || !stop_time || !duration)
        return E_INVALIDARG;

    *start_time = 0;
    *stop_time = 0;
    *duration = 0;
    return MF_E_INVALIDREQUEST;
}

static const IMFByteStreamTimeSeekVtbl bytestream_http_IMFByteStreamTimeSeek_vtbl =
{
    bytestream_http_IMFByteStreamTimeSeek_QueryInterface,
    bytestream_http_IMFByteStreamTimeSeek_AddRef,
    bytestream_http_IMFByteStreamTimeSeek_Release,
    bytestream_http_IMFByteStreamTimeSeek_IsTimeSeekSupported,
    bytestream_http_IMFByteStreamTimeSeek_TimeSeek,
    bytestream_http_IMFByteStreamTimeSeek_GetTimeSeekResult,
};

static HRESULT WINAPI bytestream_http_IMFMediaEventGenerator_QueryInterface(IMFMediaEventGenerator *iface, REFIID riid,
        void **obj)
{
    struct bytestream_http *object = impl_from_IMFMediaEventGenerator(iface);
    return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, obj);
}

static ULONG WINAPI bytestream_http_IMFMediaEventGenerator_AddRef(IMFMediaEventGenerator *iface)
{
    struct bytestream_http *object = impl_from_IMFMediaEventGenerator(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_IMFMediaEventGenerator_Release(IMFMediaEventGenerator *iface)
{
    struct bytestream_http *object = impl_from_IMFMediaEventGenerator(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

static HRESULT WINAPI bytestream_http_IMFMediaEventGenerator_GetEvent(IMFMediaEventGenerator *iface, DWORD flags,
        IMFMediaEvent **event)
{
    struct bytestream_http *object = impl_from_IMFMediaEventGenerator(iface);

    TRACE("%p, %#lx, %p.\n", iface, flags, event);

    return IMFMediaEventQueue_GetEvent(object->event_queue, flags, event);
}

static HRESULT WINAPI bytestream_http_IMFMediaEventGenerator_BeginGetEvent(IMFMediaEventGenerator *iface,
        IMFAsyncCallback *callback, IUnknown *state)
{
    struct bytestream_http *object = impl_from_IMFMediaEventGenerator(iface);

    TRACE("%p, %p, %p.\n", iface, callback, state);

    return IMFMediaEventQueue_BeginGetEvent(object->event_queue, callback, state);
}

static HRESULT WINAPI bytestream_http_IMFMediaEventGenerator_EndGetEvent(IMFMediaEventGenerator *iface,
        IMFAsyncResult *result, IMFMediaEvent **event)
{
    struct bytestream_http *object = impl_from_IMFMediaEventGenerator(iface);

    TRACE("%p, %p, %p.\n", iface, result, event);

    return IMFMediaEventQueue_EndGetEvent(object->event_queue, result, event);
}

static HRESULT WINAPI bytestream_http_IMFMediaEventGenerator_QueueEvent(IMFMediaEventGenerator *iface,
        MediaEventType type, REFGUID ext_type, HRESULT hr, const PROPVARIANT *value)
{
    struct bytestream_http *object = impl_from_IMFMediaEventGenerator(iface);

    TRACE("%p, %ld, %s, %#lx, %s.\n", iface, type, debugstr_guid(ext_type), hr, debugstr_propvar(value));

    return IMFMediaEventQueue_QueueEventParamVar(object->event_queue, type, ext_type, hr, value);
}

static const IMFMediaEventGeneratorVtbl bytestream_http_IMFMediaEventGenerator_vtbl =
{
    bytestream_http_IMFMediaEventGenerator_QueryInterface,
    bytestream_http_IMFMediaEventGenerator_AddRef,
    bytestream_http_IMFMediaEventGenerator_Release,
    bytestream_http_IMFMediaEventGenerator_GetEvent,
    bytestream_http_IMFMediaEventGenerator_BeginGetEvent,
    bytestream_http_IMFMediaEventGenerator_EndGetEvent,
    bytestream_http_IMFMediaEventGenerator_QueueEvent,
};

static HRESULT WINAPI bytestream_http_IPropertyStore_QueryInterface(IPropertyStore *iface,
        REFIID riid, void **obj)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);
    return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, obj);
}

static ULONG WINAPI bytestream_http_IPropertyStore_AddRef(IPropertyStore *iface)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_IPropertyStore_Release(IPropertyStore *iface)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

static HRESULT WINAPI bytestream_http_IPropertyStore_GetCount(IPropertyStore *iface, DWORD *count)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);

    TRACE("%p, %p.\n", iface, count);

    return IPropertyStore_GetCount(object->propstore, count);
}

static HRESULT WINAPI bytestream_http_IPropertyStore_GetAt(IPropertyStore *iface, DWORD prop, PROPERTYKEY *key)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);

    TRACE("%p, %lu, %p.\n", iface, prop, key);

    return IPropertyStore_GetAt(object->propstore, prop, key);
}

static HRESULT WINAPI bytestream_http_IPropertyStore_GetValue(IPropertyStore *iface, REFPROPERTYKEY key, PROPVARIANT *value)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);

    TRACE("%p, %p, %p.\n", iface, key, value);

    return IPropertyStore_GetValue(object->propstore, key, value);
}

static HRESULT WINAPI bytestream_http_IPropertyStore_SetValue(IPropertyStore *iface, REFPROPERTYKEY key, REFPROPVARIANT value)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);

    TRACE("%p, %p, %p.\n", iface, key, value);

    return IPropertyStore_SetValue(object->propstore, key, value);
}

static HRESULT WINAPI bytestream_http_IPropertyStore_Commit(IPropertyStore *iface)
{
    struct bytestream_http *object = impl_from_IPropertyStore(iface);

    TRACE("%p.\n", iface);

    return IPropertyStore_Commit(object->propstore);
}

static const IPropertyStoreVtbl bytestream_http_IPropertyStore_vtbl =
{
    bytestream_http_IPropertyStore_QueryInterface,
    bytestream_http_IPropertyStore_AddRef,
    bytestream_http_IPropertyStore_Release,
    bytestream_http_IPropertyStore_GetCount,
    bytestream_http_IPropertyStore_GetAt,
    bytestream_http_IPropertyStore_GetValue,
    bytestream_http_IPropertyStore_SetValue,
    bytestream_http_IPropertyStore_Commit,
};

static HRESULT WINAPI bytestream_http_IMFGetService_QueryInterface(IMFGetService *iface, REFIID riid, void **obj)
{
    struct bytestream_http *object = impl_from_IMFGetService(iface);
    return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, obj);
}

static ULONG WINAPI bytestream_http_IMFGetService_AddRef(IMFGetService *iface)
{
    struct bytestream_http *object = impl_from_IMFGetService(iface);
    return IMFByteStream_AddRef(&object->IMFByteStream_iface);
}

static ULONG WINAPI bytestream_http_IMFGetService_Release(IMFGetService *iface)
{
    struct bytestream_http *object = impl_from_IMFGetService(iface);
    return IMFByteStream_Release(&object->IMFByteStream_iface);
}

static HRESULT WINAPI bytestream_http_IMFGetService_GetService(IMFGetService *iface, REFGUID service,
        REFIID riid, void **obj)
{
    struct bytestream_http *object = impl_from_IMFGetService(iface);

    TRACE("%p, %s, %s, %p.\n", iface, debugstr_guid(service), debugstr_guid(riid), obj);

    if (IsEqualGUID(service, &MFNETSOURCE_STATISTICS_SERVICE) && IsEqualIID(riid, &IID_IPropertyStore))
        return IMFByteStream_QueryInterface(&object->IMFByteStream_iface, riid, obj);

    return MF_E_UNSUPPORTED_SERVICE;
}

static const IMFGetServiceVtbl bytestream_http_IMFGetService_vtbl =
{
    bytestream_http_IMFGetService_QueryInterface,
    bytestream_http_IMFGetService_AddRef,
    bytestream_http_IMFGetService_Release,
    bytestream_http_IMFGetService_GetService,
};

static BOOL is_range_available(struct bytestream_http *object, ULONGLONG start, ULONGLONG count)
{
    struct downloaded_range *entry;
    ULONGLONG end = start + count;

    if (object->has_length && end > object->content_length)
        end = object->content_length;

    LIST_FOR_EACH_ENTRY(entry, &object->downloaded_ranges, struct downloaded_range, entry)
    {
        if (entry->start <= start && entry->end >= end)
            return TRUE;
        if (entry->start >= end)
            break;
    }
    return FALSE;
}

static ULONGLONG find_range_end(struct bytestream_http *object, ULONGLONG start)
{
    struct downloaded_range *entry;

    LIST_FOR_EACH_ENTRY(entry, &object->downloaded_ranges, struct downloaded_range, entry)
    {
        if (entry->start <= start && entry->end >= start)
            return entry->end;
    }
    return -1ULL;
}

static ULONGLONG find_hole_end(struct bytestream_http *object, ULONGLONG start)
{
    struct downloaded_range *entry;

    LIST_FOR_EACH_ENTRY(entry, &object->downloaded_ranges, struct downloaded_range, entry)
    {
        if (entry->start >= start)
            return entry->start;
    }
    return -1ULL;
}

static void mark_range_valid(struct bytestream_http *object, ULONGLONG start, ULONGLONG count)
{
    struct downloaded_range *entry, *new_entry;
    ULONGLONG end = start + count;

    TRACE("%p %I64u %I64u\n", object, start, end);

    LIST_FOR_EACH_ENTRY(entry, &object->downloaded_ranges, struct downloaded_range, entry)
    {
        struct downloaded_range *next = LIST_ENTRY(entry->entry.next, struct downloaded_range, entry);

        if (start >= entry->start && start <= entry->end)
        {
            if (end > entry->end)
                entry->end = end - entry->start;

            if (&next->entry != &object->downloaded_ranges && next->start <= entry->end)
            {
                entry->end = next->end;
                list_remove(&next->entry);
                free(next);
            }

            return;
        }
        else if (&next->entry != &object->downloaded_ranges && start < next->start)
            break;
    }

    new_entry = calloc(1, sizeof(*new_entry));
    list_init(&new_entry->entry);
    new_entry->start = start;
    new_entry->end = end;
    list_add_before(&entry->entry, &new_entry->entry);
}

static BOOL parse_content_range(struct bytestream_http *object, WCHAR *value)
{
    static const WCHAR bytesW[] = L"bytes ";

    ULONGLONG start, end, len;
    WCHAR *ptr;

    if (wcsncmp(value, bytesW, ARRAY_SIZE(bytesW) - 1) || value[6] == '-')
        return FALSE;
    ptr = value + 6;
    if (!wcsncmp(ptr, L"*/", 2))
    {
        start = 0;
        end = -1ULL;
        ptr++;
    }
    else
    {
        start = wcstoull(ptr, &ptr, 10);
        if (*ptr != '-')
            return FALSE;
        if (ptr[1] == '/')
            end = -1ULL;
        else
            end = wcstoull(ptr + 1, &ptr, 10);
    }
    if (*ptr != '/' || ptr[1] == 0)
        return FALSE;
    if (ptr[1] != '*')
    {
        len = wcstoull(ptr + 1, &ptr, 10);
        if (*ptr != 0)
            return FALSE;
        object->has_length = TRUE;
        object->content_length = len;
    }
    object->content_range_start = start;
    object->content_range_end = end;
    return TRUE;
}

static inline void handle_winhttp_error(struct bytestream_http *object, DWORD err)
{
    HRESULT hr = HRESULT_FROM_WIN32(err);

    TRACE("error %#lx\n", err);

    if (err == ERROR_INVALID_HANDLE && object->closing_connnection)
        return;

    if (err == ERROR_WINHTTP_NAME_NOT_RESOLVED || err == ERROR_WINHTTP_CANNOT_CONNECT)
        hr = NS_E_SERVER_NOT_FOUND;
    else if (err == ERROR_WINHTTP_CONNECTION_ERROR || err == ERROR_WINHTTP_TIMEOUT)
        hr = NS_E_CONNECTION_FAILURE;

    if (!object->error)
    {
        object->error = hr;
        WakeAllConditionVariable(&object->cv);
    }
}

static inline void handle_http_error(struct bytestream_http *object, DWORD status)
{
    HRESULT hr = map_http_error(status);

    TRACE("status %ld error %#lx\n", status, hr);

    if (!object->error)
    {
        object->error = hr;
        WakeAllConditionVariable(&object->cv);
    }
}

static BOOL parse_headers(struct bytestream_http *object, HINTERNET handle)
{
    WCHAR buffer[1024];
    DWORD len;

    if (!object->content_type)
    {
        len = sizeof(buffer);
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                                buffer, &len, WINHTTP_NO_HEADER_INDEX))
        {
            object->content_type = wcsdup(buffer);
        }
        else if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND)
        {
            object->content_type = NULL;
        }
        else
        {
            handle_winhttp_error(object, GetLastError());
            return FALSE;
        }
    }

    if (object->content_length == -1ULL)
    {
        len = sizeof(buffer);
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                buffer, &len, WINHTTP_NO_HEADER_INDEX))
        {
            WCHAR *end = buffer;
            object->content_length = wcstoull(buffer, &end, 10);
            if (end == buffer)
            {
                handle_winhttp_error(object, ERROR_WINHTTP_INVALID_HEADER);
                WakeAllConditionVariable(&object->cv);
                return FALSE;
            }
            else
                object->has_length = TRUE;
        }
        else if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND)
        {
            object->content_length = -1ULL;
        }
        else
        {
            handle_winhttp_error(object, GetLastError());
            return FALSE;
        }
    }

    if (object->last_modified == -1ULL)
    {
        len = sizeof(buffer);
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_LAST_MODIFIED, WINHTTP_HEADER_NAME_BY_INDEX,
                                buffer, &len, WINHTTP_NO_HEADER_INDEX))
        {
            ULARGE_INTEGER ul = {0};
            SYSTEMTIME st = {0};
            FILETIME ft = {0};

            if (!WinHttpTimeToSystemTime(buffer, &st) || !SystemTimeToFileTime(&st, &ft))
            {
                handle_winhttp_error(object, ERROR_WINHTTP_INVALID_HEADER);
                return FALSE;
            }
            ul.u.LowPart = ft.dwLowDateTime;
            ul.u.HighPart = ft.dwHighDateTime;
            object->last_modified = ul.QuadPart;
        }
        else if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND)
        {
            object->last_modified = -1ULL;
        }
        else
        {
            handle_winhttp_error(object, GetLastError());
            return FALSE;
        }
    }

    len = sizeof(buffer);
    if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_RANGE, WINHTTP_HEADER_NAME_BY_INDEX,
                            buffer, &len, WINHTTP_NO_HEADER_INDEX))
    {
        parse_content_range(object, buffer);
        object->current_write_pos = object->content_range_start;
    }
    else if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND)
    {
        object->content_range_start = 0;
        object->content_range_end = -1ULL;
        object->current_write_pos = 0;
    }
    else
    {
        handle_winhttp_error(object, GetLastError());
        return FALSE;
    }
    return TRUE;
}

static void CALLBACK progress_callback_http(HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID buf, DWORD buflen)
{
    struct bytestream_http *object = (void *)context;

    TRACE("%p, %lx, %p, %lu\n", handle, status, buf, buflen);

    switch (status)
    {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    {
        EnterCriticalSection(&object->cs);

        if (object->error || object->closing_connnection)
        {
            LeaveCriticalSection(&object->cs);
            return;
        }

        if (!WinHttpReceiveResponse(handle, NULL))
            handle_winhttp_error(object, GetLastError());

        LeaveCriticalSection(&object->cs);
        break;
    }

    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
    {
        DWORD status = 0, len = sizeof(status);

        EnterCriticalSection(&object->cs);

        if (object->error || object->closing_connnection)
        {
            LeaveCriticalSection(&object->cs);
            return;
        }

        if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX))
            handle_winhttp_error(object, GetLastError());
        else if (status < 200 || status >= 300)
            handle_http_error(object, status);
        else if (parse_headers(object, handle) && !WinHttpQueryDataAvailable(handle, NULL))
            handle_winhttp_error(object, GetLastError());

        object->parsed_headers = TRUE;
        LeaveCriticalSection(&object->cs);
        WakeAllConditionVariable(&object->cv);
        break;
    }

    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
    {
        DWORD *len = buf;
        DWORD to_read = *len < sizeof(object->buffer) ? *len : sizeof(object->buffer);
        ULONGLONG end;
        BOOL ret;

        EnterCriticalSection(&object->cs);

        if (object->error || object->closing_connnection)
        {
            LeaveCriticalSection(&object->cs);
            return;
        }

        if (*len > 0 && !object->read_active)
        {
            object->read_active = TRUE;
            LeaveCriticalSection(&object->cs);
            ret = WinHttpReadData(handle, object->buffer, to_read, &object->bytes_read);
            EnterCriticalSection(&object->cs);
            if (!ret)
            {
                object->read_active = FALSE;
                handle_winhttp_error(object, GetLastError());
            }
        }
        if (*len == 0)
        {
            end = find_range_end(object, object->current_write_pos);
            if (end == -1ULL && (object->content_range_end == -1ULL ||
                    object->current_write_pos == object->content_range_end))
            {
                if (!object->has_length || object->current_write_pos > object->content_length)
                    object->content_length = object->current_write_pos;
                if (!object->has_length)
                {
                    object->has_length = TRUE;
                    send_characteristics_changed_event(object);
                }
                WakeAllConditionVariable(&object->cv);
            }
        }

        LeaveCriticalSection(&object->cs);
        break;
    }

    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
    {
        OVERLAPPED ovl = {0};
        DWORD written = 0, prev_caps = 0, new_caps = 0;
        BOOL ret;

        EnterCriticalSection(&object->cs);

        object->read_active = FALSE;

        if (object->error || object->closing_connnection)
        {
            LeaveCriticalSection(&object->cs);
            return;
        }

        ovl.Internal = -1;
        ovl.InternalHigh = -1;
        ovl.Offset = (DWORD)object->current_write_pos;
        ovl.OffsetHigh = (DWORD)(object->current_write_pos >> 32);
        if (!WriteFile(object->cache_file, buf, buflen, &written, &ovl) || written < buflen)
        {
            if (!object->error)
                object->error = HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            bytestream_http_GetCapabilities(&object->IMFByteStream_iface, &prev_caps);

            mark_range_valid(object, object->current_write_pos, written);
            object->current_write_pos += written;

            update_buffering_status(object);

            bytestream_http_GetCapabilities(&object->IMFByteStream_iface, &new_caps);
            if (prev_caps != new_caps)
                send_characteristics_changed_event(object);

            LeaveCriticalSection(&object->cs);
            ret = WinHttpQueryDataAvailable(handle, NULL);
            EnterCriticalSection(&object->cs);
            if (!ret)
                handle_winhttp_error(object, GetLastError());
        }

        LeaveCriticalSection(&object->cs);
        WakeAllConditionVariable(&object->cv);
        break;
    }

    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
    {
        WINHTTP_ASYNC_RESULT *result = buf;

        EnterCriticalSection(&object->cs);

        if (result->dwResult == API_READ_DATA)
            object->read_active = FALSE;

        if (object->error || object->closing_connnection)
        {
            LeaveCriticalSection(&object->cs);
            return;
        }

        handle_winhttp_error(object, result->dwError);

        LeaveCriticalSection(&object->cs);
        WakeAllConditionVariable(&object->cv);
        break;
    }

    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
    {
        BOOL release = TRUE;

        EnterCriticalSection(&object->cs);

        if (handle == object->request)
        {
            object->request = NULL;
            object->read_active = FALSE;
        }
        else if (handle == object->connection)
            object->connection = NULL;
        else if (handle == object->session)
            object->session = NULL;
        else
            release = FALSE;

        LeaveCriticalSection(&object->cs);
        WakeAllConditionVariable(&object->cv);

        if (release)
            IMFByteStream_Release(&object->IMFByteStream_iface);
        break;
    }
    }
}

/* Taken from MFCreateTempFile */
static HRESULT create_temp_file(HANDLE *h)
{
    WCHAR name[24], tmppath[MAX_PATH], *path;
    HRESULT hr = S_OK;
    ULONG64 rnd;
    size_t len;

    BCryptGenRandom(NULL, (UCHAR *)&rnd, sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    swprintf(name, ARRAY_SIZE(name), L"MFP%llX.TMP", rnd);
    GetTempPathW(ARRAY_SIZE(tmppath), tmppath);

    len = wcslen(tmppath) + wcslen(name) + 2;
    if (!(path = malloc(len * sizeof(*path))))
        return E_OUTOFMEMORY;

    wcscpy(path, tmppath);
    PathCchAppend(path, len, name);

    *h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | FILE_FLAG_DELETE_ON_CLOSE, 0, NULL,
                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (*h == INVALID_HANDLE_VALUE)
        hr = HRESULT_FROM_WIN32(GetLastError());

    free(path);

    return hr;
}

static void close_connection(struct bytestream_http *object, BOOL wait)
{
    if (wait)
    {
        while (object->closing_connnection)
            SleepConditionVariableCS(&object->cv, &object->cs, INFINITE);
        object->closing_connnection = TRUE;
    }
    if (object->request && !WinHttpCloseHandle(object->request))
        object->request = NULL;
    if (object->connection && !WinHttpCloseHandle(object->connection))
        object->connection = NULL;
    if (object->session && !WinHttpCloseHandle(object->session))
        object->session = NULL;
    if (wait)
    {
        while (object->request || object->connection || object->session)
            SleepConditionVariableCS(&object->cv, &object->cs, INFINITE);
        object->closing_connnection = FALSE;
    }
    object->content_range_start = -1ULL;
    object->content_range_end = -1ULL;
}

static HRESULT open_connection(struct bytestream_http *object, ULONGLONG range_start, ULONGLONG range_end)
{
    static const WCHAR *accept_all[] = { L"*/*", NULL };

    WCHAR host[MAX_PATH], range[256];
    URL_COMPONENTS uc;
    DWORD flags;

    close_connection(object, TRUE);

    range_start -= range_start % 1024;
    if (range_end != -1ULL) range_end -= 1;

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize      = sizeof(uc);
    uc.lpszHostName      = host;
    uc.dwHostNameLength  = ARRAY_SIZE(host);
    uc.dwUrlPathLength   = ~0U;
    uc.dwSchemeLength    = ~0U;
    if (!WinHttpCrackUrl(object->url, 0, 0, &uc))
    {
        uc.lpszHostName = malloc(uc.dwHostNameLength * sizeof(WCHAR));
        if (!uc.lpszHostName)
            return E_OUTOFMEMORY;
        if (!WinHttpCrackUrl(object->url, 0, 0, &uc))
            goto error;
    }

    object->session = WinHttpOpen(L"NSPlayer/12.00.19041.4894 WMFSDK/12.00.19041.4894",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                  WINHTTP_FLAG_ASYNC);
    if (!object->session)
        goto error;
    IMFByteStream_AddRef(&object->IMFByteStream_iface);
    WinHttpSetStatusCallback(object->session, progress_callback_http,
                             WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING, 0);
    if (!WinHttpSetOption(object->session, WINHTTP_OPTION_CONTEXT_VALUE, &object, sizeof(object)))
        goto error;

    object->connection = WinHttpConnect(object->session, uc.lpszHostName, uc.nPort, 0);
    if (!object->connection)
        goto error;
    IMFByteStream_AddRef(&object->IMFByteStream_iface);

    flags = WINHTTP_FLAG_REFRESH;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS)
        flags |= WINHTTP_FLAG_SECURE;

    object->request = WinHttpOpenRequest(object->connection, NULL, uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER, accept_all, flags);
    if (!object->request)
        goto error;
    IMFByteStream_AddRef(&object->IMFByteStream_iface);

    WinHttpAddRequestHeaders(object->request, L"GetContentFeatures.DLNA.ORG: 1", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    /* TODO: Implement persistent caching of downloaded data */
    /*if (object->last_modified != -1ULL)
    {
        WCHAR date[WINHTTP_TIME_FORMAT_BUFSIZE], if_modified_since[WINHTTP_TIME_FORMAT_BUFSIZE + 19];
        ULARGE_INTEGER ul = {0};
        SYSTEMTIME st = {0};
        FILETIME ft = {0};

        ul.QuadPart = object->last_modified;
        ft.dwLowDateTime = ul.u.LowPart;
        ft.dwHighDateTime = ul.u.HighPart;
        if (FileTimeToSystemTime(&ft, &st) && WinHttpTimeFromSystemTime(&st, date))
        {
            swprintf(if_modified_since, ARRAY_SIZE(if_modified_since), L"If-Modified-Since: %s", date);
            WinHttpAddRequestHeaders(object->request, if_modified_since, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
        }
    }*/

    if (range_end == -1ULL)
        swprintf(range, ARRAY_SIZE(range), L"Range: bytes=%I64u-", range_start);
    else
        swprintf(range, ARRAY_SIZE(range), L"Range: bytes=%I64u-%I64u", range_start, range_end);
    if (!WinHttpAddRequestHeaders(object->request, range, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD))
        goto error;

    if (!WinHttpSendRequest(object->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                            (DWORD_PTR)object))
        goto error;

    if (uc.lpszHostName != host)
        free(uc.lpszHostName);
    return S_OK;

error:
    handle_winhttp_error(object, GetLastError());
    if (uc.lpszHostName != host)
        free(uc.lpszHostName);
    close_connection(object, TRUE);
    WakeAllConditionVariable(&object->cv);
    return object->error;
}

static WCHAR *normalize_uri_scheme(const WCHAR *url)
{
    struct
    {
        const WCHAR *scheme;
        const WCHAR *replacement;
    } scheme_replacements[] =
    {
        { L"httpd:", L"http:" },
        { L"httpsd:", L"https:" },
        { L"mms:", L"http:" }
    };
    int i;

    for (i = 0; i < ARRAY_SIZE(scheme_replacements); i++)
    {
        size_t scheme_len = wcslen(scheme_replacements[i].scheme);
        if (!wcsncmp(url, scheme_replacements[i].scheme, scheme_len))
        {
            size_t replacement_len = wcslen(scheme_replacements[i].replacement);
            size_t url_len = wcslen(url);
            WCHAR *new_url = malloc((url_len - scheme_len + replacement_len + 1) * sizeof(*new_url));
            if (!new_url)
                return NULL;
            wcscpy(new_url, scheme_replacements[i].replacement);
            wcscat(new_url, url + scheme_len);
            return new_url;
        }
    }
    return wcsdup(url);
}

static WCHAR *recreate_url(const WCHAR *url)
{
    WCHAR host[MAX_PATH], port_str[6];
    URL_COMPONENTS uc;
    WCHAR *out;

    url = normalize_uri_scheme(url);
    if (!url)
        return NULL;

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize      = sizeof(uc);
    uc.lpszHostName      = host;
    uc.dwHostNameLength  = ARRAY_SIZE(host);
    uc.dwUrlPathLength   = ~0U;
    uc.dwSchemeLength    = ~0U;
    if (!WinHttpCrackUrl(url, 0, 0, &uc))
    {
        uc.lpszHostName = malloc(uc.dwHostNameLength * sizeof(WCHAR));
        if (!uc.lpszHostName)
            goto error;
        if (!WinHttpCrackUrl(url, 0, 0, &uc))
            goto error;
    }

    out = malloc((8 + wcslen(uc.lpszHostName) + 1 + 5 + wcslen(uc.lpszUrlPath) + 1) * sizeof(WCHAR));
    if (!out)
        goto error;

    out[0] = 0;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS)
        wcscat(out, L"https://");
    else
        wcscat(out, L"http://");
    wcscat(out, uc.lpszHostName);
    wcscat(out, L":");
    swprintf(port_str, ARRAY_SIZE(port_str), L"%u", uc.nPort);
    wcscat(out, port_str);
    wcscat(out, uc.lpszUrlPath);
    free((void *)url);
    return out;

error:
    if (uc.lpszHostName != host)
        free(uc.lpszHostName);
    free((void *)url);
    return NULL;
}

HRESULT create_http_bytestream(const WCHAR *url, void **out)
{
    struct bytestream_http *object;
    ULARGE_INTEGER ul = {0};
    FILETIME ft = {0};
    HRESULT hr;

    TRACE("%s, %p.\n", debugstr_w(url), out);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = init_attributes_object(&object->attributes, 0)))
    {
        free(object);
        return hr;
    }
    object->IMFByteStream_iface.lpVtbl = &bytestream_http_vtbl;
    object->attributes.IMFAttributes_iface.lpVtbl = &bytestream_http_attributes_vtbl;
    object->IMFByteStreamCacheControl_iface.lpVtbl = &bytestream_http_IMFByteStreamCacheControl_vtbl;
    object->IMFByteStreamBuffering_iface.lpVtbl = &bytestream_http_IMFByteStreamBuffering_vtbl;
    object->IMFMediaEventGenerator_iface.lpVtbl = &bytestream_http_IMFMediaEventGenerator_vtbl;
    object->IMFByteStreamTimeSeek_iface.lpVtbl = &bytestream_http_IMFByteStreamTimeSeek_vtbl;
    object->IPropertyStore_iface.lpVtbl = &bytestream_http_IPropertyStore_vtbl;
    object->IMFGetService_iface.lpVtbl = &bytestream_http_IMFGetService_vtbl;
    InitializeCriticalSection(&object->cs);
    InitializeConditionVariable(&object->cv);
    object->read_callback.lpVtbl = &bytestream_http_read_callback_vtbl;
    list_init(&object->pending);
    object->url = recreate_url(url);
    object->content_length = -1ULL;
    object->last_modified = -1ULL;
    object->content_range_end = -1ULL;
    object->buffering = TRUE;
    list_init(&object->downloaded_ranges);

    if (!object->url)
        goto error;

    if (FAILED(hr = CreatePropertyStore(&object->propstore)) ||
        FAILED(hr = MFCreateEventQueue(&object->event_queue)))
        goto error;

    if (FAILED(hr = create_temp_file(&object->cache_file)))
    {
        ERR("Couldn't create temp file: %lx\n", hr);
        goto error;
    }

    EnterCriticalSection(&object->cs);

    if (FAILED(hr = open_connection(object, 0ULL, -1ULL)))
        goto error;

    while (!object->parsed_headers && !object->error)
        SleepConditionVariableCS(&object->cv, &object->cs, INFINITE);

    if (object->error)
    {
        hr = object->error;
        goto error;
    }

    IMFAttributes_SetString(&object->attributes.IMFAttributes_iface, &MF_BYTESTREAM_EFFECTIVE_URL, object->url);
    IMFAttributes_SetString(&object->attributes.IMFAttributes_iface, &MF_BYTESTREAM_CONTENT_TYPE,
                            object->content_type ? object->content_type : L"application/octet-stream");
    if (object->last_modified != -1ULL)
    {
        ul.QuadPart = object->last_modified;
        ft.dwLowDateTime = ul.u.LowPart;
        ft.dwHighDateTime = ul.u.HighPart;
        IMFAttributes_SetBlob(&object->attributes.IMFAttributes_iface, &MF_BYTESTREAM_LAST_MODIFIED_TIME, (void *)&ft,
                              sizeof(ft));
    }

    LeaveCriticalSection(&object->cs);

    *out = &object->IMFByteStream_iface;
    return S_OK;

error:
    LeaveCriticalSection(&object->cs);
    IMFByteStream_Release(&object->IMFByteStream_iface);

    if (hr == NS_E_SERVER_NOT_FOUND && wcsncmp(url, L"http:", 5))
        hr = WININET_E_NAME_NOT_RESOLVED;
    else if (!wcsncmp(url, L"mms:", 4))
        hr = MF_E_UNSUPPORTED_BYTESTREAM_TYPE;
    return hr;
}

WINAPI HRESULT __wine_create_http_bytestream(const WCHAR *url, void **out)
{
    return create_http_bytestream(url, out);
}
