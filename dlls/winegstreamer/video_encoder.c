/* Generic Video Encoder Transform
 *
 * Copyright 2024 Ziqing Hui for CodeWeavers
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

#include "gst_private.h"

#include "mfapi.h"
#include "mferror.h"
#include "mfobjects.h"
#include "mftransform.h"
#include "mediaerr.h"
#include "wmcodecdsp.h"

#include "wine/debug.h"

#include "initguid.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);
WINE_DECLARE_DEBUG_CHANNEL(winediag);

struct video_encoder
{
    IMFTransform IMFTransform_iface;
    LONG refcount;

    const GUID *const *output_types;
    UINT output_type_count;

    IMFMediaType *output_type;

    IMFAttributes *attributes;
};

static inline struct video_encoder *impl_from_IMFTransform(IMFTransform *iface)
{
    return CONTAINING_RECORD(iface, struct video_encoder, IMFTransform_iface);
}

static HRESULT WINAPI transform_QueryInterface(IMFTransform *iface, REFIID iid, void **out)
{
    struct video_encoder *encoder = impl_from_IMFTransform(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IMFTransform) || IsEqualGUID(iid, &IID_IUnknown))
        *out = &encoder->IMFTransform_iface;
    else
    {
        *out = NULL;
        WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI transform_AddRef(IMFTransform *iface)
{
    struct video_encoder *encoder = impl_from_IMFTransform(iface);
    ULONG refcount = InterlockedIncrement(&encoder->refcount);

    TRACE("iface %p increasing refcount to %lu.\n", encoder, refcount);

    return refcount;
}

static ULONG WINAPI transform_Release(IMFTransform *iface)
{
    struct video_encoder *encoder = impl_from_IMFTransform(iface);
    ULONG refcount = InterlockedDecrement(&encoder->refcount);

    TRACE("iface %p decreasing refcount to %lu.\n", encoder, refcount);

    if (!refcount)
    {
        if (encoder->output_type)
            IMFMediaType_Release(encoder->output_type);
        IMFAttributes_Release(encoder->attributes);
        free(encoder);
    }

    return refcount;
}

static HRESULT WINAPI transform_GetStreamLimits(IMFTransform *iface, DWORD *input_minimum,
        DWORD *input_maximum, DWORD *output_minimum, DWORD *output_maximum)
{
    TRACE("iface %p, input_minimum %p, input_maximum %p, output_minimum %p, output_maximum %p.\n",
            iface, input_minimum, input_maximum, output_minimum, output_maximum);
    *input_minimum = *input_maximum = *output_minimum = *output_maximum = 1;
    return S_OK;
}

static HRESULT WINAPI transform_GetStreamCount(IMFTransform *iface, DWORD *inputs, DWORD *outputs)
{
    TRACE("iface %p, inputs %p, outputs %p.\n", iface, inputs, outputs);
    *inputs = *outputs = 1;
    return S_OK;
}

static HRESULT WINAPI transform_GetStreamIDs(IMFTransform *iface, DWORD input_size, DWORD *inputs,
        DWORD output_size, DWORD *outputs)
{
    FIXME("iface %p, input_size %lu, inputs %p, output_size %lu, outputs %p.\n", iface,
            input_size, inputs, output_size, outputs);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputStreamInfo(IMFTransform *iface, DWORD id, MFT_INPUT_STREAM_INFO *info)
{
    FIXME("iface %p, id %#lx, info %p.\n", iface, id, info);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStreamInfo(IMFTransform *iface, DWORD id, MFT_OUTPUT_STREAM_INFO *info)
{
    FIXME("iface %p, id %#lx, info %p.\n", iface, id, info);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetAttributes(IMFTransform *iface, IMFAttributes **attributes)
{
    struct video_encoder *encoder = impl_from_IMFTransform(iface);

    TRACE("iface %p, attributes %p.\n", iface, attributes);

    if (!attributes)
        return E_POINTER;

    IMFAttributes_AddRef((*attributes = encoder->attributes));
    return S_OK;
}

static HRESULT WINAPI transform_GetInputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    FIXME("iface %p, id %#lx, attributes %p.\n", iface, id, attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStreamAttributes(IMFTransform *iface, DWORD id, IMFAttributes **attributes)
{
    FIXME("iface %p, id %#lx, attributes %p.\n", iface, id, attributes);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_DeleteInputStream(IMFTransform *iface, DWORD id)
{
    FIXME("iface %p, id %#lx.\n", iface, id);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_AddInputStreams(IMFTransform *iface, DWORD streams, DWORD *ids)
{
    FIXME("iface %p, streams %lu, ids %p.\n", iface, streams, ids);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetInputAvailableType(IMFTransform *iface, DWORD id, DWORD index,
        IMFMediaType **type)
{
    FIXME("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputAvailableType(IMFTransform *iface, DWORD id,
        DWORD index, IMFMediaType **type)
{
    struct video_encoder *encoder = impl_from_IMFTransform(iface);

    TRACE("iface %p, id %#lx, index %#lx, type %p.\n", iface, id, index, type);

    *type = NULL;
    if (index >= encoder->output_type_count)
        return MF_E_NO_MORE_TYPES;
    return MFCreateVideoMediaTypeFromSubtype(encoder->output_types[index], (IMFVideoMediaType **)type);
}

static HRESULT WINAPI transform_SetInputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    FIXME("iface %p, id %#lx, type %p, flags %#lx.\n", iface, id, type, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_SetOutputType(IMFTransform *iface, DWORD id, IMFMediaType *type, DWORD flags)
{
    struct video_encoder *encoder = impl_from_IMFTransform(iface);
    UINT32 uint32_value;
    UINT64 uint64_value;
    GUID major, subtype;
    ULONG i;

    TRACE("iface %p, id %#lx, type %p, flags %#lx.\n", iface, id, type, flags);

    if (!type)
    {
        if (encoder->output_type)
        {
            IMFMediaType_Release(encoder->output_type);
            encoder->output_type = NULL;
        }
        return S_OK;
    }

    if (FAILED(IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major))
            || FAILED(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype)))
        return E_INVALIDARG;

    if (!IsEqualGUID(&major, &MFMediaType_Video))
        return MF_E_INVALIDMEDIATYPE;

    for (i = 0; i < encoder->output_type_count; ++i)
        if (IsEqualGUID(&subtype, encoder->output_types[i]))
            break;
    if (i == encoder->output_type_count)
        return MF_E_INVALIDMEDIATYPE;

    if (FAILED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_SIZE, &uint64_value)))
        return MF_E_INVALIDMEDIATYPE;

    if (flags & MFT_SET_TYPE_TEST_ONLY)
        return S_OK;

    if (FAILED(IMFMediaType_GetUINT64(type, &MF_MT_FRAME_RATE, &uint64_value))
            || FAILED(IMFMediaType_GetUINT32(type, &MF_MT_AVG_BITRATE, &uint32_value))
            || FAILED(IMFMediaType_GetUINT32(type, &MF_MT_INTERLACE_MODE, &uint32_value)))
        return MF_E_INVALIDMEDIATYPE;

    if (encoder->output_type)
        IMFMediaType_Release(encoder->output_type);
    IMFMediaType_AddRef((encoder->output_type = type));

    /* FIXME: Add MF_MT_MPEG_SEQUENCE_HEADER attribute. */

    return S_OK;
}

static HRESULT WINAPI transform_GetInputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    FIXME("iface %p, id %#lx, type %p\n", iface, id, type);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputCurrentType(IMFTransform *iface, DWORD id, IMFMediaType **type)
{
    struct video_encoder *encoder = impl_from_IMFTransform(iface);
    HRESULT hr;

    TRACE("iface %p, id %#lx, type %p\n", iface, id, type);

    if (!encoder->output_type)
        return MF_E_TRANSFORM_TYPE_NOT_SET;

    if (FAILED(hr = MFCreateMediaType(type)))
        return hr;

    return IMFMediaType_CopyAllItems(encoder->output_type, (IMFAttributes *)*type);
}

static HRESULT WINAPI transform_GetInputStatus(IMFTransform *iface, DWORD id, DWORD *flags)
{
    FIXME("iface %p, id %#lx, flags %p.\n", iface, id, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_GetOutputStatus(IMFTransform *iface, DWORD *flags)
{
    FIXME("iface %p, flags %p stub!\n", iface, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_SetOutputBounds(IMFTransform *iface, LONGLONG lower, LONGLONG upper)
{
    FIXME("iface %p, lower %I64d, upper %I64d.\n", iface, lower, upper);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessEvent(IMFTransform *iface, DWORD id, IMFMediaEvent *event)
{
    FIXME("iface %p, id %#lx, event %p stub!\n", iface, id, event);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessMessage(IMFTransform *iface, MFT_MESSAGE_TYPE message, ULONG_PTR param)
{
    FIXME("iface %p, message %#x, param %Ix.\n", iface, message, param);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessInput(IMFTransform *iface, DWORD id, IMFSample *sample, DWORD flags)
{
    FIXME("iface %p, id %#lx, sample %p, flags %#lx.\n", iface, id, sample, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI transform_ProcessOutput(IMFTransform *iface, DWORD flags, DWORD count,
        MFT_OUTPUT_DATA_BUFFER *samples, DWORD *status)
{
    FIXME("iface %p, flags %#lx, count %lu, samples %p, status %p.\n", iface, flags, count, samples, status);
    return E_NOTIMPL;
}

static const IMFTransformVtbl transform_vtbl =
{
    transform_QueryInterface,
    transform_AddRef,
    transform_Release,
    transform_GetStreamLimits,
    transform_GetStreamCount,
    transform_GetStreamIDs,
    transform_GetInputStreamInfo,
    transform_GetOutputStreamInfo,
    transform_GetAttributes,
    transform_GetInputStreamAttributes,
    transform_GetOutputStreamAttributes,
    transform_DeleteInputStream,
    transform_AddInputStreams,
    transform_GetInputAvailableType,
    transform_GetOutputAvailableType,
    transform_SetInputType,
    transform_SetOutputType,
    transform_GetInputCurrentType,
    transform_GetOutputCurrentType,
    transform_GetInputStatus,
    transform_GetOutputStatus,
    transform_SetOutputBounds,
    transform_ProcessEvent,
    transform_ProcessMessage,
    transform_ProcessInput,
    transform_ProcessOutput,
};

static HRESULT video_encoder_create(const GUID *const *output_types, UINT output_type_count,
        struct video_encoder **out)
{
    struct video_encoder *encoder;
    HRESULT hr;

    if (!(encoder = calloc(1, sizeof(*encoder))))
        return E_OUTOFMEMORY;

    encoder->IMFTransform_iface.lpVtbl = &transform_vtbl;
    encoder->refcount = 1;

    encoder->output_types = output_types;
    encoder->output_type_count = output_type_count;

    if (FAILED(hr = MFCreateAttributes(&encoder->attributes, 16)))
        goto failed;
    if (FAILED(hr = IMFAttributes_SetUINT32(encoder->attributes, &MFT_ENCODER_SUPPORTS_CONFIG_EVENT, TRUE)))
        goto failed;

    *out = encoder;
    TRACE("Created video encoder %p\n", encoder);
    return S_OK;

failed:
    if (encoder->attributes)
        IMFAttributes_Release(encoder->attributes);
    free(encoder);
    return hr;
}

static const GUID *const h264_encoder_output_types[] =
{
    &MFVideoFormat_H264,
};

HRESULT h264_encoder_create(REFIID riid, void **out)
{
    const MFVIDEOFORMAT input_format =
    {
        .dwSize = sizeof(MFVIDEOFORMAT),
        .videoInfo = {.dwWidth = 1920, .dwHeight = 1080},
        .guidFormat = MFVideoFormat_NV12,
    };
    const MFVIDEOFORMAT output_format =
    {
        .dwSize = sizeof(MFVIDEOFORMAT),
        .videoInfo = {.dwWidth = 1920, .dwHeight = 1080},
        .guidFormat = MFVideoFormat_H264,
    };
    struct video_encoder *encoder;
    HRESULT hr;

    TRACE("riid %s, out %p.\n", debugstr_guid(riid), out);

    if (FAILED(hr = check_video_transform_support(&input_format, &output_format)))
    {
        ERR_(winediag)("GStreamer doesn't support H.264 encoding, please install appropriate plugins\n");
        return hr;
    }

    if (FAILED(hr = video_encoder_create(h264_encoder_output_types, ARRAY_SIZE(h264_encoder_output_types),
            &encoder)))
        return hr;

    TRACE("Created h264 encoder transform %p.\n", &encoder->IMFTransform_iface);

    hr = IMFTransform_QueryInterface(&encoder->IMFTransform_iface, riid, out);
    IMFTransform_Release(&encoder->IMFTransform_iface);
    return hr;
}
