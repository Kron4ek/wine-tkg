/*
 * Copyright 2012, 2014 Michael Stefaniuc
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

#include <stdarg.h>
#include <windef.h>
#include <wine/test.h>
#include <ole2.h>
#include <dmusici.h>
#include <dmusicf.h>
#include <audioclient.h>
#include <guiddef.h>

static ULONG get_refcount(void *iface)
{
    IUnknown *unknown = iface;
    IUnknown_AddRef(unknown);
    return IUnknown_Release(unknown);
}

#define check_interface(a, b, c) check_interface_(__LINE__, a, b, c)
static void check_interface_(unsigned int line, void *iface_ptr, REFIID iid, BOOL supported)
{
    ULONG expect_ref = get_refcount(iface_ptr);
    IUnknown *iface = iface_ptr;
    HRESULT hr, expected;
    IUnknown *unk;

    expected = supported ? S_OK : E_NOINTERFACE;
    hr = IUnknown_QueryInterface(iface, iid, (void **)&unk);
    ok_(__FILE__, line)(hr == expected, "got hr %#lx, expected %#lx.\n", hr, expected);
    if (SUCCEEDED(hr))
    {
        LONG ref = get_refcount(unk);
        ok_(__FILE__, line)(ref == expect_ref + 1, "got %ld\n", ref);
        IUnknown_Release(unk);
        ref = get_refcount(iface_ptr);
        ok_(__FILE__, line)(ref == expect_ref, "got %ld\n", ref);
    }
}

struct test_tool
{
    IDirectMusicTool IDirectMusicTool_iface;
    LONG ref;

    IDirectMusicGraph *graph;
    const DWORD *types;
    DWORD types_count;

    HANDLE message_event;
    DMUS_PMSG *message;
};

static struct test_tool *impl_from_IDirectMusicTool(IDirectMusicTool *iface)
{
    return CONTAINING_RECORD(iface, struct test_tool, IDirectMusicTool_iface);
}

static HRESULT WINAPI test_tool_QueryInterface(IDirectMusicTool *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown)
            || IsEqualGUID(iid, &IID_IDirectMusicTool))
    {
        IDirectMusicTool_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    ok(IsEqualGUID(iid, &IID_IDirectMusicTool8) || IsEqualGUID(iid, &IID_IPersistStream),
            "got iid %s\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI test_tool_AddRef(IDirectMusicTool *iface)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    return InterlockedIncrement(&tool->ref);
}

static ULONG WINAPI test_tool_Release(IDirectMusicTool *iface)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    ULONG ref = InterlockedDecrement(&tool->ref);

    if (!ref)
    {
        if (tool->graph) IDirectMusicGraph_Release(tool->graph);
        ok(!tool->message, "got %p\n", tool->message);
        CloseHandle(tool->message_event);
        free(tool);
    }

    return ref;
}

static HRESULT WINAPI test_tool_Init(IDirectMusicTool *iface, IDirectMusicGraph *graph)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    if ((tool->graph = graph)) IDirectMusicGraph_AddRef(tool->graph);
    return S_OK;
}

static HRESULT WINAPI test_tool_GetMsgDeliveryType(IDirectMusicTool *iface, DWORD *type)
{
    *type = DMUS_PMSGF_TOOL_IMMEDIATE;
    return S_OK;
}

static HRESULT WINAPI test_tool_GetMediaTypeArraySize(IDirectMusicTool *iface, DWORD *size)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    *size = tool->types_count;
    return S_OK;
}

static HRESULT WINAPI test_tool_GetMediaTypes(IDirectMusicTool *iface, DWORD **types, DWORD size)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    UINT i;
    for (i = 0; i < tool->types_count; i++) (*types)[i] = tool->types[i];
    return S_OK;
}

static HRESULT WINAPI test_tool_ProcessPMsg(IDirectMusicTool *iface, IDirectMusicPerformance *performance, DMUS_PMSG *msg)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    DMUS_PMSG *clone;
    HRESULT hr;

    hr = IDirectMusicPerformance8_ClonePMsg((IDirectMusicPerformance8 *)performance, msg, &clone);
    ok(hr == S_OK, "got %#lx\n", hr);

    clone = InterlockedExchangePointer((void **)&tool->message, clone);
    ok(!clone, "got %p\n", clone);
    SetEvent(tool->message_event);

    hr = IDirectMusicGraph_StampPMsg(msg->pGraph, msg);
    ok(hr == S_OK, "got %#lx\n", hr);

    return DMUS_S_REQUEUE;
}

static HRESULT WINAPI test_tool_Flush(IDirectMusicTool *iface, IDirectMusicPerformance *performance,
        DMUS_PMSG *msg, REFERENCE_TIME time)
{
    ok(0, "unexpected %s\n", __func__);
    return S_OK;
}

static IDirectMusicToolVtbl test_tool_vtbl =
{
    test_tool_QueryInterface,
    test_tool_AddRef,
    test_tool_Release,
    test_tool_Init,
    test_tool_GetMsgDeliveryType,
    test_tool_GetMediaTypeArraySize,
    test_tool_GetMediaTypes,
    test_tool_ProcessPMsg,
    test_tool_Flush,
};

static HRESULT test_tool_create(const DWORD *types, DWORD types_count,
        IDirectMusicTool **ret_iface)
{
    struct test_tool *tool;

    *ret_iface = NULL;
    if (!(tool = calloc(1, sizeof(*tool)))) return E_OUTOFMEMORY;
    tool->IDirectMusicTool_iface.lpVtbl = &test_tool_vtbl;
    tool->ref = 1;

    tool->types = types;
    tool->types_count = types_count;
    tool->message_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(!!tool->message_event, "CreateEventW failed, error %lu\n", GetLastError());

    *ret_iface = &tool->IDirectMusicTool_iface;
    return S_OK;
}

static HRESULT test_tool_get_graph(IDirectMusicTool *iface, IDirectMusicGraph **graph)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    if ((*graph = tool->graph)) IDirectMusicGraph_AddRef(tool->graph);
    return tool->graph ? S_OK : DMUS_E_NOT_FOUND;
}

static DWORD test_tool_wait_message(IDirectMusicTool *iface, DWORD timeout, DMUS_PMSG **msg)
{
    struct test_tool *tool = impl_from_IDirectMusicTool(iface);
    DWORD ret;

    ret = WaitForSingleObject(tool->message_event, timeout);
    *msg = InterlockedExchangePointer((void **)&tool->message, NULL);

    return ret;
}

static BOOL missing_dmime(void)
{
    IDirectMusicSegment8 *dms;
    HRESULT hr = CoCreateInstance(&CLSID_DirectMusicSegment, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicSegment, (void**)&dms);

    if (hr == S_OK && dms)
    {
        IDirectMusicSegment8_Release(dms);
        return FALSE;
    }
    return TRUE;
}

static void test_COM_audiopath(void)
{
    IDirectMusicAudioPath *dmap;
    IUnknown *unk;
    IDirectMusicPerformance8 *performance;
    IDirectSoundBuffer *dsound;
    IDirectSoundBuffer8 *dsound8;
    IDirectSoundNotify *notify;
    IDirectSound3DBuffer *dsound3d;
    IKsPropertySet *propset;
    ULONG refcount;
    HRESULT hr;
    DWORD buffer = 0;

    hr = CoCreateInstance(&CLSID_DirectMusicPerformance, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicPerformance8, (void**)&performance);
    ok(hr == S_OK || broken(hr == E_NOINTERFACE), "DirectMusicPerformance create failed: %#lx\n", hr);
    if (!performance) {
        win_skip("IDirectMusicPerformance8 not available\n");
        return;
    }
    hr = IDirectMusicPerformance8_InitAudio(performance, NULL, NULL, NULL,
            DMUS_APATH_SHARED_STEREOPLUSREVERB, 64, DMUS_AUDIOF_ALL, NULL);
    ok(hr == S_OK || hr == DSERR_NODRIVER ||
       broken(hr == AUDCLNT_E_ENDPOINT_CREATE_FAILED), /* Win 10 testbot */
       "DirectMusicPerformance_InitAudio failed: %#lx\n", hr);
    if (FAILED(hr)) {
        skip("Audio failed to initialize\n");
        return;
    }
    hr = IDirectMusicPerformance8_GetDefaultAudioPath(performance, &dmap);
    ok(hr == S_OK, "DirectMusicPerformance_GetDefaultAudioPath failed: %#lx\n", hr);

    /* IDirectMusicObject and IPersistStream are not supported */
    hr = IDirectMusicAudioPath_QueryInterface(dmap, &IID_IDirectMusicObject, (void**)&unk);
    todo_wine ok(FAILED(hr) && !unk, "Unexpected IDirectMusicObject interface: hr=%#lx, iface=%p\n",
            hr, unk);
    if (unk) IUnknown_Release(unk);
    hr = IDirectMusicAudioPath_QueryInterface(dmap, &IID_IPersistStream, (void**)&unk);
    todo_wine ok(FAILED(hr) && !unk, "Unexpected IPersistStream interface: hr=%#lx, iface=%p\n",
            hr, unk);
    if (unk) IUnknown_Release(unk);

    /* Same refcount for all DirectMusicAudioPath interfaces */
    refcount = IDirectMusicAudioPath_AddRef(dmap);
    ok(refcount == 3, "refcount == %lu, expected 3\n", refcount);

    hr = IDirectMusicAudioPath_QueryInterface(dmap, &IID_IUnknown, (void**)&unk);
    ok(hr == S_OK, "QueryInterface for IID_IUnknown failed: %#lx\n", hr);
    ok(unk == (IUnknown*)dmap, "got %p, %p\n", unk, dmap);
    refcount = IUnknown_AddRef(unk);
    ok(refcount == 5, "refcount == %lu, expected 5\n", refcount);
    refcount = IUnknown_Release(unk);

    hr = IDirectMusicAudioPath_GetObjectInPath(dmap, DMUS_PCHANNEL_ALL, DMUS_PATH_BUFFER, buffer, &GUID_NULL,
                0, &IID_IDirectSoundBuffer, (void**)&dsound);
    ok(hr == S_OK, "Failed: %#lx\n", hr);
    IDirectSoundBuffer_Release(dsound);

    hr = IDirectMusicAudioPath_GetObjectInPath(dmap, DMUS_PCHANNEL_ALL, DMUS_PATH_BUFFER, buffer, &GUID_NULL,
                0, &IID_IDirectSoundBuffer8, (void**)&dsound8);
    ok(hr == S_OK, "Failed: %#lx\n", hr);
    IDirectSoundBuffer8_Release(dsound8);

    hr = IDirectMusicAudioPath_GetObjectInPath(dmap, DMUS_PCHANNEL_ALL, DMUS_PATH_BUFFER, buffer, &GUID_NULL,
                0, &IID_IDirectSoundNotify, (void**)&notify);
    ok(hr == E_NOINTERFACE, "Failed: %#lx\n", hr);

    hr = IDirectMusicAudioPath_GetObjectInPath(dmap, DMUS_PCHANNEL_ALL, DMUS_PATH_BUFFER, buffer, &GUID_NULL,
                0, &IID_IDirectSound3DBuffer, (void**)&dsound3d);
    ok(hr == E_NOINTERFACE, "Failed: %#lx\n", hr);

    hr = IDirectMusicAudioPath_GetObjectInPath(dmap, DMUS_PCHANNEL_ALL, DMUS_PATH_BUFFER, buffer, &GUID_NULL,
                0, &IID_IKsPropertySet, (void**)&propset);
    todo_wine ok(hr == S_OK, "Failed: %#lx\n", hr);
    if (propset)
        IKsPropertySet_Release(propset);

    hr = IDirectMusicAudioPath_GetObjectInPath(dmap, DMUS_PCHANNEL_ALL, DMUS_PATH_BUFFER, buffer, &GUID_NULL,
                0, &IID_IUnknown, (void**)&unk);
    ok(hr == S_OK, "Failed: %#lx\n", hr);
    IUnknown_Release(unk);

    hr = IDirectMusicAudioPath_GetObjectInPath(dmap, DMUS_PCHANNEL_ALL, DMUS_PATH_BUFFER, buffer, &GUID_NULL,
                0, &GUID_NULL, (void**)&unk);
    ok(hr == E_NOINTERFACE, "Failed: %#lx\n", hr);

    while (IDirectMusicAudioPath_Release(dmap) > 1); /* performance has a reference too */
    IDirectMusicPerformance8_CloseDown(performance);
    IDirectMusicPerformance8_Release(performance);
}

static void test_COM_audiopathconfig(void)
{
    IDirectMusicAudioPath *dmap = (IDirectMusicAudioPath*)0xdeadbeef;
    IDirectMusicObject *dmo;
    IPersistStream *ps;
    IUnknown *unk;
    ULONG refcount;
    HRESULT hr;

    /* COM aggregation */
    hr = CoCreateInstance(&CLSID_DirectMusicAudioPathConfig, (IUnknown *)0xdeadbeef, CLSCTX_INPROC_SERVER,
            &IID_IUnknown, (void**)&dmap);
    if (hr == REGDB_E_CLASSNOTREG) {
        win_skip("DirectMusicAudioPathConfig not registered\n");
        return;
    }
    ok(hr == CLASS_E_NOAGGREGATION,
            "DirectMusicAudioPathConfig create failed: %#lx, expected CLASS_E_NOAGGREGATION\n", hr);
    ok(!dmap, "dmap = %p\n", dmap);

    /* IDirectMusicAudioPath not supported */
    hr = CoCreateInstance(&CLSID_DirectMusicAudioPathConfig, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicAudioPath, (void**)&dmap);
    todo_wine ok(FAILED(hr) && !dmap,
            "Unexpected IDirectMusicAudioPath interface: hr=%#lx, iface=%p\n", hr, dmap);

    /* IDirectMusicObject and IPersistStream supported */
    hr = CoCreateInstance(&CLSID_DirectMusicAudioPathConfig, NULL, CLSCTX_INPROC_SERVER,
            &IID_IPersistStream, (void**)&ps);
    ok(hr == S_OK, "DirectMusicObject create failed: %#lx, expected S_OK\n", hr);
    IPersistStream_Release(ps);
    hr = CoCreateInstance(&CLSID_DirectMusicAudioPathConfig, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicObject, (void**)&dmo);
    ok(hr == S_OK, "DirectMusicObject create failed: %#lx, expected S_OK\n", hr);

    /* Same refcount for all DirectMusicObject interfaces */
    refcount = IDirectMusicObject_AddRef(dmo);
    ok(refcount == 2, "refcount == %lu, expected 2\n", refcount);

    hr = IDirectMusicObject_QueryInterface(dmo, &IID_IPersistStream, (void**)&ps);
    ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
    refcount = IPersistStream_AddRef(ps);
    ok(refcount == 4, "refcount == %lu, expected 4\n", refcount);
    IPersistStream_Release(ps);

    hr = IDirectMusicObject_QueryInterface(dmo, &IID_IUnknown, (void**)&unk);
    ok(hr == S_OK, "QueryInterface for IID_IUnknown failed: %#lx\n", hr);
    refcount = IUnknown_AddRef(unk);
    ok(refcount == 5, "refcount == %lu, expected 5\n", refcount);
    refcount = IUnknown_Release(unk);

    /* IDirectMusicAudioPath still not supported */
    hr = IDirectMusicObject_QueryInterface(dmo, &IID_IDirectMusicAudioPath, (void**)&dmap);
    todo_wine ok(FAILED(hr) && !dmap,
            "Unexpected IDirectMusicAudioPath interface: hr=%#lx, iface=%p\n", hr, dmap);

    while (IDirectMusicObject_Release(dmo));
}


static void test_COM_graph(void)
{
    IDirectMusicGraph *dmg = (IDirectMusicGraph*)0xdeadbeef;
    IDirectMusicObject *dmo;
    IPersistStream *ps;
    IUnknown *unk;
    ULONG refcount;
    HRESULT hr;

    /* COM aggregation */
    hr = CoCreateInstance(&CLSID_DirectMusicGraph, (IUnknown *)0xdeadbeef, CLSCTX_INPROC_SERVER,
            &IID_IUnknown, (void**)&dmg);
    ok(hr == CLASS_E_NOAGGREGATION,
            "DirectMusicGraph create failed: %#lx, expected CLASS_E_NOAGGREGATION\n", hr);
    ok(!dmg, "dmg = %p\n", dmg);

    /* Invalid RIID */
    hr = CoCreateInstance(&CLSID_DirectMusicGraph, NULL, CLSCTX_INPROC_SERVER, &IID_IClassFactory,
            (void**)&dmg);
    ok(hr == E_NOINTERFACE, "DirectMusicGraph create failed: %#lx, expected E_NOINTERFACE\n", hr);

    /* Same refcount for all DirectMusicGraph interfaces */
    hr = CoCreateInstance(&CLSID_DirectMusicGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicGraph, (void**)&dmg);
    ok(hr == S_OK, "DirectMusicGraph create failed: %#lx, expected S_OK\n", hr);
    refcount = IDirectMusicGraph_AddRef(dmg);
    ok(refcount == 2, "refcount == %lu, expected 2\n", refcount);

    hr = IDirectMusicGraph_QueryInterface(dmg, &IID_IDirectMusicObject, (void**)&dmo);
    if (hr == E_NOINTERFACE) {
        win_skip("DirectMusicGraph without IDirectMusicObject\n");
        return;
    }
    ok(hr == S_OK, "QueryInterface for IID_IDirectMusicObject failed: %#lx\n", hr);
    refcount = IDirectMusicObject_AddRef(dmo);
    ok(refcount == 4, "refcount == %lu, expected 4\n", refcount);
    refcount = IDirectMusicObject_Release(dmo);

    hr = IDirectMusicGraph_QueryInterface(dmg, &IID_IPersistStream, (void**)&ps);
    ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
    refcount = IPersistStream_AddRef(ps);
    ok(refcount == 5, "refcount == %lu, expected 5\n", refcount);
    refcount = IPersistStream_Release(ps);

    hr = IDirectMusicGraph_QueryInterface(dmg, &IID_IUnknown, (void**)&unk);
    ok(hr == S_OK, "QueryInterface for IID_IUnknown failed: %#lx\n", hr);
    refcount = IUnknown_AddRef(unk);
    ok(refcount == 6, "refcount == %lu, expected 6\n", refcount);
    refcount = IUnknown_Release(unk);

    while (IDirectMusicGraph_Release(dmg));
}

static void test_COM_segment(void)
{
    IDirectMusicSegment8 *dms = (IDirectMusicSegment8*)0xdeadbeef;
    IDirectMusicObject *dmo;
    IPersistStream *stream;
    IUnknown *unk;
    ULONG refcount;
    HRESULT hr;

    /* COM aggregation */
    hr = CoCreateInstance(&CLSID_DirectMusicSegment, (IUnknown *)0xdeadbeef, CLSCTX_INPROC_SERVER,
            &IID_IUnknown, (void**)&dms);
    ok(hr == CLASS_E_NOAGGREGATION,
            "DirectMusicSegment create failed: %#lx, expected CLASS_E_NOAGGREGATION\n", hr);
    ok(!dms, "dms = %p\n", dms);

    /* Invalid RIID */
    hr = CoCreateInstance(&CLSID_DirectMusicSegment, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectSound, (void**)&dms);
    ok(hr == E_NOINTERFACE, "DirectMusicSegment create failed: %#lx, expected E_NOINTERFACE\n", hr);

    /* Same refcount */
    hr = CoCreateInstance(&CLSID_DirectMusicSegment, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicSegment8, (void**)&dms);
    if (hr == E_NOINTERFACE) {
        win_skip("DirectMusicSegment without IDirectMusicSegment8\n");
        return;
    }
    ok(hr == S_OK, "DirectMusicSegment create failed: %#lx, expected S_OK\n", hr);
    refcount = IDirectMusicSegment8_AddRef(dms);
    ok (refcount == 2, "refcount == %lu, expected 2\n", refcount);
    hr = IDirectMusicSegment8_QueryInterface(dms, &IID_IDirectMusicObject, (void**)&dmo);
    ok(hr == S_OK, "QueryInterface for IID_IDirectMusicObject failed: %#lx\n", hr);
    IDirectMusicSegment8_AddRef(dms);
    refcount = IDirectMusicSegment8_Release(dms);
    ok (refcount == 3, "refcount == %lu, expected 3\n", refcount);
    hr = IDirectMusicSegment8_QueryInterface(dms, &IID_IPersistStream, (void**)&stream);
    ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
    refcount = IDirectMusicSegment8_Release(dms);
    ok (refcount == 3, "refcount == %lu, expected 3\n", refcount);
    hr = IDirectMusicSegment8_QueryInterface(dms, &IID_IUnknown, (void**)&unk);
    ok(hr == S_OK, "QueryInterface for IID_IUnknown failed: %#lx\n", hr);
    refcount = IUnknown_Release(unk);
    ok (refcount == 3, "refcount == %lu, expected 3\n", refcount);
    refcount = IDirectMusicObject_Release(dmo);
    ok (refcount == 2, "refcount == %lu, expected 2\n", refcount);
    refcount = IPersistStream_Release(stream);
    ok (refcount == 1, "refcount == %lu, expected 1\n", refcount);
    refcount = IDirectMusicSegment8_Release(dms);
    ok (refcount == 0, "refcount == %lu, expected 0\n", refcount);
}

static void test_COM_segmentstate(void)
{
    IDirectMusicSegmentState8 *dmss8 = (IDirectMusicSegmentState8*)0xdeadbeef;
    IUnknown *unk;
    ULONG refcount;
    HRESULT hr;

    /* COM aggregation */
    hr = CoCreateInstance(&CLSID_DirectMusicSegmentState, (IUnknown *)0xdeadbeef, CLSCTX_INPROC_SERVER,
            &IID_IUnknown, (void**)&dmss8);
    ok(hr == CLASS_E_NOAGGREGATION,
            "DirectMusicSegmentState8 create failed: %#lx, expected CLASS_E_NOAGGREGATION\n", hr);
    ok(!dmss8, "dmss8 = %p\n", dmss8);

    /* Invalid RIID */
    hr = CoCreateInstance(&CLSID_DirectMusicSegmentState, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicObject, (void**)&dmss8);
    ok(hr == E_NOINTERFACE, "DirectMusicSegmentState8 create failed: %#lx, expected E_NOINTERFACE\n", hr);

    /* Same refcount for all DirectMusicSegmentState interfaces */
    hr = CoCreateInstance(&CLSID_DirectMusicSegmentState, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicSegmentState8, (void**)&dmss8);
    if (hr == E_NOINTERFACE) {
        win_skip("DirectMusicSegmentState without IDirectMusicSegmentState8\n");
        return;
    }
    ok(hr == S_OK, "DirectMusicSegmentState8 create failed: %#lx, expected S_OK\n", hr);
    refcount = IDirectMusicSegmentState8_AddRef(dmss8);
    ok(refcount == 2, "refcount == %lu, expected 2\n", refcount);

    hr = IDirectMusicSegmentState8_QueryInterface(dmss8, &IID_IUnknown, (void**)&unk);
    ok(hr == S_OK, "QueryInterface for IID_IUnknown failed: %#lx\n", hr);
    refcount = IUnknown_AddRef(unk);
    ok(refcount == 4, "refcount == %lu, expected 4\n", refcount);
    refcount = IUnknown_Release(unk);

    hr = IDirectMusicSegmentState8_QueryInterface(dmss8, &IID_IUnknown, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);

    while (IDirectMusicSegmentState8_Release(dmss8));
}

static void test_COM_track(void)
{
    IDirectMusicTrack *dmt;
    IDirectMusicTrack8 *dmt8;
    IPersistStream *ps;
    IUnknown *unk;
    ULONG refcount;
    HRESULT hr;
#define X(class)        &CLSID_ ## class, #class
    const struct {
        REFCLSID clsid;
        const char *name;
        BOOL has_dmt8;
    } class[] = {
        { X(DirectMusicLyricsTrack), TRUE },
        { X(DirectMusicMarkerTrack), FALSE },
        { X(DirectMusicParamControlTrack), TRUE },
        { X(DirectMusicSegmentTriggerTrack), TRUE },
        { X(DirectMusicSeqTrack), TRUE },
        { X(DirectMusicSysExTrack), TRUE },
        { X(DirectMusicTempoTrack), TRUE },
        { X(DirectMusicTimeSigTrack), FALSE },
        { X(DirectMusicWaveTrack), TRUE }
    };
#undef X
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(class); i++) {
        trace("Testing %s\n", class[i].name);
        /* COM aggregation */
        dmt8 = (IDirectMusicTrack8*)0xdeadbeef;
        hr = CoCreateInstance(class[i].clsid, (IUnknown *)0xdeadbeef, CLSCTX_INPROC_SERVER, &IID_IUnknown,
                (void**)&dmt8);
        if (hr == REGDB_E_CLASSNOTREG) {
            win_skip("%s not registered\n", class[i].name);
            continue;
        }
        ok(hr == CLASS_E_NOAGGREGATION,
                "%s create failed: %#lx, expected CLASS_E_NOAGGREGATION\n", class[i].name, hr);
        ok(!dmt8, "dmt8 = %p\n", dmt8);

        /* Invalid RIID */
        hr = CoCreateInstance(class[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectMusicObject,
                (void**)&dmt8);
        ok(hr == E_NOINTERFACE, "%s create failed: %#lx, expected E_NOINTERFACE\n", class[i].name, hr);

        /* Same refcount for all DirectMusicTrack interfaces */
        hr = CoCreateInstance(class[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectMusicTrack,
                (void**)&dmt);
        ok(hr == S_OK, "%s create failed: %#lx, expected S_OK\n", class[i].name, hr);
        refcount = IDirectMusicTrack_AddRef(dmt);
        ok(refcount == 2, "refcount == %lu, expected 2\n", refcount);

        hr = IDirectMusicTrack_QueryInterface(dmt, &IID_IPersistStream, (void**)&ps);
        ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
        refcount = IPersistStream_AddRef(ps);
        ok(refcount == 4, "refcount == %lu, expected 4\n", refcount);
        IPersistStream_Release(ps);

        hr = IDirectMusicTrack_QueryInterface(dmt, &IID_IUnknown, (void**)&unk);
        ok(hr == S_OK, "QueryInterface for IID_IUnknown failed: %#lx\n", hr);
        refcount = IUnknown_AddRef(unk);
        ok(refcount == 5, "refcount == %lu, expected 5\n", refcount);
        refcount = IUnknown_Release(unk);

        hr = IDirectMusicTrack_QueryInterface(dmt, &IID_IDirectMusicTrack8, (void**)&dmt8);
        if (class[i].has_dmt8) {
            ok(hr == S_OK, "QueryInterface for IID_IDirectMusicTrack8 failed: %#lx\n", hr);
            refcount = IDirectMusicTrack8_AddRef(dmt8);
            ok(refcount == 6, "refcount == %lu, expected 6\n", refcount);
            refcount = IDirectMusicTrack8_Release(dmt8);
        } else {
            ok(hr == E_NOINTERFACE, "QueryInterface for IID_IDirectMusicTrack8 failed: %#lx\n", hr);
            refcount = IDirectMusicTrack_AddRef(dmt);
            ok(refcount == 5, "refcount == %lu, expected 5\n", refcount);
        }

        while (IDirectMusicTrack_Release(dmt));
    }
}

static void test_audiopathconfig(void)
{
    IDirectMusicObject *dmo;
    IPersistStream *ps;
    CLSID class = { 0 };
    ULARGE_INTEGER size;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectMusicAudioPathConfig, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicObject, (void**)&dmo);
    if (hr == REGDB_E_CLASSNOTREG) {
        win_skip("DirectMusicAudioPathConfig not registered\n");
        return;
    }
    ok(hr == S_OK, "DirectMusicAudioPathConfig create failed: %#lx, expected S_OK\n", hr);

    /* IPersistStream */
    hr = IDirectMusicObject_QueryInterface(dmo, &IID_IPersistStream, (void**)&ps);
    ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
    hr = IPersistStream_GetClassID(ps, &class);
    ok(hr == S_OK, "IPersistStream_GetClassID failed: %#lx\n", hr);
    ok(IsEqualGUID(&class, &CLSID_DirectMusicAudioPathConfig),
            "Expected class CLSID_DirectMusicAudioPathConfig got %s\n", wine_dbgstr_guid(&class));

    /* Unimplemented IPersistStream methods */
    hr = IPersistStream_IsDirty(ps);
    ok(hr == S_FALSE, "IPersistStream_IsDirty failed: %#lx\n", hr);
    hr = IPersistStream_GetSizeMax(ps, &size);
    ok(hr == E_NOTIMPL, "IPersistStream_GetSizeMax failed: %#lx\n", hr);
    hr = IPersistStream_Save(ps, NULL, TRUE);
    ok(hr == E_NOTIMPL, "IPersistStream_Save failed: %#lx\n", hr);

    while (IDirectMusicObject_Release(dmo));
}

static void test_graph(void)
{
    IDirectMusicTool *tool1, *tool2, *tmp_tool;
    IDirectMusicGraph *graph, *tmp_graph;
    IPersistStream *ps;
    CLSID class = { 0 };
    ULARGE_INTEGER size;
    DMUS_PMSG msg;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectMusicGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicGraph, (void**)&graph);
    ok(hr == S_OK, "DirectMusicGraph create failed: %#lx, expected S_OK\n", hr);

    /* IPersistStream */
    hr = IDirectMusicGraph_QueryInterface(graph, &IID_IPersistStream, (void**)&ps);
    ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
    hr = IPersistStream_GetClassID(ps, &class);
    ok(hr == S_OK || broken(hr == E_NOTIMPL) /* win2k */, "IPersistStream_GetClassID failed: %#lx\n", hr);
    if (hr == S_OK)
        ok(IsEqualGUID(&class, &CLSID_DirectMusicGraph),
                "Expected class CLSID_DirectMusicGraph got %s\n", wine_dbgstr_guid(&class));

    /* Unimplemented IPersistStream methods */
    hr = IPersistStream_IsDirty(ps);
    ok(hr == S_FALSE, "IPersistStream_IsDirty failed: %#lx\n", hr);
    hr = IPersistStream_GetSizeMax(ps, &size);
    ok(hr == E_NOTIMPL, "IPersistStream_GetSizeMax failed: %#lx\n", hr);
    hr = IPersistStream_Save(ps, NULL, TRUE);
    ok(hr == E_NOTIMPL, "IPersistStream_Save failed: %#lx\n", hr);

    IDirectMusicGraph_Release(graph);


    hr = test_tool_create(NULL, 0, &tool1);
    ok(hr == S_OK, "got %#lx\n", hr);
    trace("created tool1 %p\n", tool1);
    hr = test_tool_create(NULL, 0, &tool2);
    ok(hr == S_OK, "got %#lx\n", hr);
    trace("created tool2 %p\n", tool2);

    hr = CoCreateInstance(&CLSID_DirectMusicGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicGraph, (void **)&graph);
    ok(hr == S_OK, "got %#lx\n", hr);


    hr = IDirectMusicGraph_InsertTool(graph, NULL, NULL, 0, -1);
    ok(hr == E_POINTER, "got %#lx\n", hr);

    /* InsertTool initializes the tool */
    hr = IDirectMusicGraph_InsertTool(graph, tool1, NULL, 0, -1);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = test_tool_get_graph(tool1, &tmp_graph);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(graph == tmp_graph, "got %#lx\n", hr);
    IDirectMusicGraph_Release(tmp_graph);

    hr = IDirectMusicGraph_InsertTool(graph, tool2, NULL, 0, 1);
    ok(hr == S_OK, "got %#lx\n", hr);

    hr = IDirectMusicGraph_GetTool(graph, 0, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    hr = IDirectMusicGraph_GetTool(graph, 0, &tmp_tool);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(tool1 == tmp_tool, "got %p\n", tmp_tool);
    if (hr == S_OK) IDirectMusicTool_Release(tmp_tool);
    hr = IDirectMusicGraph_GetTool(graph, 1, &tmp_tool);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(tool2 == tmp_tool, "got %p\n", tmp_tool);
    if (hr == S_OK) IDirectMusicTool_Release(tmp_tool);
    hr = IDirectMusicGraph_GetTool(graph, 2, &tmp_tool);
    ok(hr == DMUS_E_NOT_FOUND, "got %#lx\n", hr);

    /* cannot insert the tool twice */
    hr = IDirectMusicGraph_InsertTool(graph, tool1, NULL, 0, -1);
    ok(hr == DMUS_E_ALREADY_EXISTS, "got %#lx\n", hr);

    /* test removing the first tool */
    hr = IDirectMusicGraph_RemoveTool(graph, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    hr = IDirectMusicGraph_RemoveTool(graph, tool1);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_RemoveTool(graph, tool1);
    ok(hr == DMUS_E_NOT_FOUND, "got %#lx\n", hr);

    hr = IDirectMusicGraph_GetTool(graph, 0, &tmp_tool);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(tool2 == tmp_tool, "got %p\n", tmp_tool);
    if (hr == S_OK) IDirectMusicTool_Release(tmp_tool);
    hr = IDirectMusicGraph_GetTool(graph, 1, &tmp_tool);
    ok(hr == DMUS_E_NOT_FOUND, "got %#lx\n", hr);

    hr = IDirectMusicGraph_InsertTool(graph, tool1, NULL, 0, -1);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_GetTool(graph, 0, &tmp_tool);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(tool1 == tmp_tool, "got %p\n", tmp_tool);
    if (hr == S_OK) IDirectMusicTool_Release(tmp_tool);


    /* Test basic IDirectMusicGraph_StampPMsg usage */
    hr = IDirectMusicGraph_StampPMsg(graph, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    memset(&msg, 0, sizeof(msg));
    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == graph, "got %p\n", msg.pGraph);
    ok(msg.pTool == tool1, "got %p\n", msg.pTool);

    ok(!msg.dwSize, "got %ld\n", msg.dwSize);
    ok(!msg.rtTime, "got %I64d\n", msg.rtTime);
    ok(!msg.mtTime, "got %ld\n", msg.mtTime);
    ok(msg.dwFlags == DMUS_PMSGF_TOOL_IMMEDIATE, "got %#lx\n", msg.dwFlags);
    ok(!msg.dwPChannel, "got %ld\n", msg.dwPChannel);
    ok(!msg.dwVirtualTrackID, "got %ld\n", msg.dwVirtualTrackID);
    ok(!msg.dwType, "got %#lx\n", msg.dwType);
    ok(!msg.dwVoiceID, "got %ld\n", msg.dwVoiceID);
    ok(!msg.dwGroupID, "got %ld\n", msg.dwGroupID);
    ok(!msg.punkUser, "got %p\n", msg.punkUser);

    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == graph, "got %p\n", msg.pGraph);
    ok(msg.pTool == tool2, "got %p\n", msg.pTool);
    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == DMUS_S_LAST_TOOL, "got %#lx\n", hr);
    ok(msg.pGraph == graph, "got %p\n", msg.pGraph);
    ok(!msg.pTool, "got %p\n", msg.pTool);
    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == graph, "got %p\n", msg.pGraph);
    ok(msg.pTool == tool1, "got %p\n", msg.pTool);
    IDirectMusicGraph_Release(msg.pGraph);
    msg.pGraph = NULL;
    IDirectMusicGraph_Release(msg.pTool);
    msg.pTool = NULL;


    /* test StampPMsg with the wrong graph or innexistant tools */
    hr = CoCreateInstance(&CLSID_DirectMusicGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicGraph, (void **)&tmp_graph);
    ok(hr == S_OK, "got %#lx\n", hr);

    msg.pGraph = tmp_graph;
    IDirectMusicGraph_AddRef(msg.pGraph);
    msg.pTool = tool1;
    IDirectMusicTool_AddRef(msg.pTool);
    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == tmp_graph, "got %p\n", msg.pGraph);
    ok(msg.pTool == tool2, "got %p\n", msg.pTool);
    IDirectMusicGraph_Release(msg.pGraph);
    msg.pGraph = NULL;

    msg.pGraph = graph;
    IDirectMusicGraph_AddRef(msg.pGraph);
    hr = IDirectMusicGraph_StampPMsg(tmp_graph, &msg);
    ok(hr == DMUS_S_LAST_TOOL, "got %#lx\n", hr);
    ok(msg.pGraph == graph, "got %p\n", msg.pGraph);
    ok(msg.pTool == NULL, "got %p\n", msg.pTool);

    msg.pTool = tool2;
    IDirectMusicTool_AddRef(msg.pTool);
    hr = IDirectMusicGraph_InsertTool(tmp_graph, tool1, NULL, 0, 0);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_InsertTool(tmp_graph, tool2, NULL, 0, 0);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_StampPMsg(tmp_graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == graph, "got %p\n", msg.pGraph);
    ok(msg.pTool == tool1, "got %p\n", msg.pTool);
    IDirectMusicGraph_Release(msg.pGraph);
    msg.pGraph = NULL;

    hr = IDirectMusicGraph_RemoveTool(graph, tool1);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_StampPMsg(tmp_graph, &msg);
    ok(hr == DMUS_S_LAST_TOOL, "got %#lx\n", hr);
    ok(msg.pGraph == NULL, "got %p\n", msg.pGraph);
    ok(msg.pTool == NULL, "got %p\n", msg.pTool);

    IDirectMusicGraph_Release(tmp_graph);


    IDirectMusicGraph_Release(graph);
    IDirectMusicTool_Release(tool2);
    IDirectMusicTool_Release(tool1);
}

static void test_segment(void)
{
    IDirectMusicSegment *dms;
    IPersistStream *ps;
    CLSID class = { 0 };
    ULARGE_INTEGER size;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectMusicSegment, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicSegment, (void**)&dms);
    ok(hr == S_OK, "DirectMusicSegment create failed: %#lx, expected S_OK\n", hr);

    /* IPersistStream */
    hr = IDirectMusicSegment_QueryInterface(dms, &IID_IPersistStream, (void**)&ps);
    ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
    hr = IPersistStream_GetClassID(ps, &class);
    ok(hr == S_OK || broken(hr == E_NOTIMPL) /* win2k */, "IPersistStream_GetClassID failed: %#lx\n", hr);
    if (hr == S_OK)
        ok(IsEqualGUID(&class, &CLSID_DirectMusicSegment),
                "Expected class CLSID_DirectMusicSegment got %s\n", wine_dbgstr_guid(&class));

    /* Unimplemented IPersistStream methods */
    hr = IPersistStream_IsDirty(ps);
    ok(hr == S_FALSE, "IPersistStream_IsDirty failed: %#lx\n", hr);
    hr = IPersistStream_GetSizeMax(ps, &size);
    ok(hr == E_NOTIMPL, "IPersistStream_GetSizeMax failed: %#lx\n", hr);
    hr = IPersistStream_Save(ps, NULL, TRUE);
    ok(hr == E_NOTIMPL, "IPersistStream_Save failed: %#lx\n", hr);

    while (IDirectMusicSegment_Release(dms));
}

static void _add_track(IDirectMusicSegment8 *seg, REFCLSID class, const char *name, DWORD group)
{
    IDirectMusicTrack *track;
    HRESULT hr;

    hr = CoCreateInstance(class, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectMusicTrack,
            (void**)&track);
    ok(hr == S_OK, "%s create failed: %#lx, expected S_OK\n", name, hr);
    hr = IDirectMusicSegment8_InsertTrack(seg, track, group);
    if (group)
        ok(hr == S_OK, "Inserting %s failed: %#lx, expected S_OK\n", name, hr);
    else
        ok(hr == E_INVALIDARG, "Inserting %s failed: %#lx, expected E_INVALIDARG\n", name, hr);
    IDirectMusicTrack_Release(track);
}

#define add_track(seg, class, group) _add_track(seg, &CLSID_DirectMusic ## class, #class, group)

static void _expect_track(IDirectMusicSegment8 *seg, REFCLSID expect, const char *name, DWORD group,
        DWORD index, BOOL ignore_guid)
{
    IDirectMusicTrack *track;
    IPersistStream *ps;
    CLSID class;
    HRESULT hr;

    if (ignore_guid)
        hr = IDirectMusicSegment8_GetTrack(seg, &GUID_NULL, group, index, &track);
    else
        hr = IDirectMusicSegment8_GetTrack(seg, expect, group, index, &track);
    if (!expect) {
        ok(hr == DMUS_E_NOT_FOUND, "GetTrack failed: %#lx, expected DMUS_E_NOT_FOUND\n", hr);
        return;
    }

    ok(hr == S_OK, "GetTrack failed: %#lx, expected S_OK\n", hr);
    hr = IDirectMusicTrack_QueryInterface(track, &IID_IPersistStream, (void**)&ps);
    ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
    hr = IPersistStream_GetClassID(ps, &class);
    ok(hr == S_OK, "IPersistStream_GetClassID failed: %#lx\n", hr);
    ok(IsEqualGUID(&class, expect), "For group %#lx index %lu: Expected class %s got %s\n",
            group, index, name, wine_dbgstr_guid(&class));

    IPersistStream_Release(ps);
    IDirectMusicTrack_Release(track);
}

#define expect_track(seg, class, group, index) \
    _expect_track(seg, &CLSID_DirectMusic ## class, #class, group, index, TRUE)
#define expect_guid_track(seg, class, group, index) \
    _expect_track(seg, &CLSID_DirectMusic ## class, #class, group, index, FALSE)

static void test_gettrack(void)
{
    IDirectMusicSegment8 *seg;
    IDirectMusicTrack *track;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectMusicSegment, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicSegment8, (void**)&seg);
    ok(hr == S_OK, "DirectMusicSegment create failed: %#lx, expected S_OK\n", hr);

    add_track(seg, LyricsTrack, 0x0);         /* failure */
    add_track(seg, LyricsTrack, 0x1);         /* idx 0 group 1 */
    add_track(seg, ParamControlTrack, 0x3);   /* idx 1 group 1, idx 0 group 2 */
    add_track(seg, SegmentTriggerTrack, 0x2); /* idx 1 group 2 */
    add_track(seg, SeqTrack, 0x1);            /* idx 2 group 1 */
    add_track(seg, TempoTrack, 0x7);          /* idx 3 group 1, idx 2 group 2, idx 0 group 3 */
    add_track(seg, WaveTrack, 0xffffffff);    /* idx 4 group 1, idx 3 group 2, idx 1 group 3 */

    /* Ignore GUID in GetTrack */
    hr = IDirectMusicSegment8_GetTrack(seg, &GUID_NULL, 0, 0, &track);
    ok(hr == DMUS_E_NOT_FOUND, "GetTrack failed: %#lx, expected DMUS_E_NOT_FOUND\n", hr);

    expect_track(seg, LyricsTrack, 0x1, 0);
    expect_track(seg, ParamControlTrack, 0x1, 1);
    expect_track(seg, SeqTrack, 0x1, 2);
    expect_track(seg, TempoTrack, 0x1, 3);
    expect_track(seg, WaveTrack, 0x1, 4);
    _expect_track(seg, NULL, "", 0x1, 5, TRUE);
    _expect_track(seg, NULL, "", 0x1, DMUS_SEG_ANYTRACK, TRUE);
    expect_track(seg, ParamControlTrack, 0x2, 0);
    expect_track(seg, WaveTrack, 0x80000000, 0);
    expect_track(seg, SegmentTriggerTrack, 0x3, 2);  /* groups 1+2 combined index */
    expect_track(seg, SeqTrack, 0x3, 3);             /* groups 1+2 combined index */
    expect_track(seg, TempoTrack, 0x7, 4);           /* groups 1+2+3 combined index */
    expect_track(seg, TempoTrack, 0xffffffff, 4);    /* all groups combined index */
    _expect_track(seg, NULL, "", 0xffffffff, DMUS_SEG_ANYTRACK, TRUE);

    /* Use the GUID in GetTrack */
    hr = IDirectMusicSegment8_GetTrack(seg, &CLSID_DirectMusicLyricsTrack, 0, 0, &track);
    ok(hr == DMUS_E_NOT_FOUND, "GetTrack failed: %#lx, expected DMUS_E_NOT_FOUND\n", hr);

    expect_guid_track(seg, LyricsTrack, 0x1, 0);
    expect_guid_track(seg, ParamControlTrack, 0x1, 0);
    expect_guid_track(seg, SeqTrack, 0x1, 0);
    expect_guid_track(seg, TempoTrack, 0x1, 0);
    expect_guid_track(seg, ParamControlTrack, 0x2, 0);
    expect_guid_track(seg, SegmentTriggerTrack, 0x3, 0);
    expect_guid_track(seg, SeqTrack, 0x3, 0);
    expect_guid_track(seg, TempoTrack, 0x7, 0);
    expect_guid_track(seg, TempoTrack, 0xffffffff, 0);

    IDirectMusicSegment8_Release(seg);
}

static void test_segment_param(void)
{
    IDirectMusicSegment8 *seg;
    char buf[64];
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectMusicSegment, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicSegment8, (void **)&seg);
    ok(hr == S_OK, "DirectMusicSegment create failed: %#lx, expected S_OK\n", hr);

    add_track(seg, LyricsTrack, 0x1);         /* no params */
    add_track(seg, SegmentTriggerTrack, 0x1); /* all params "supported" */

    hr = IDirectMusicSegment8_GetParam(seg, NULL, 0x1, 0, 0, NULL, buf);
    ok(hr == E_POINTER, "GetParam failed: %#lx, expected E_POINTER\n", hr);
    hr = IDirectMusicSegment8_SetParam(seg, NULL, 0x1, 0, 0, buf);
    todo_wine ok(hr == E_POINTER, "SetParam failed: %#lx, expected E_POINTER\n", hr);

    hr = IDirectMusicSegment8_GetParam(seg, &GUID_Valid_Start_Time, 0x1, 0, 0, NULL, buf);
    ok(hr == DMUS_E_GET_UNSUPPORTED, "GetParam failed: %#lx, expected DMUS_E_GET_UNSUPPORTED\n", hr);
    hr = IDirectMusicSegment8_GetParam(seg, &GUID_Valid_Start_Time, 0x1, 1, 0, NULL, buf);
    ok(hr == DMUS_E_TRACK_NOT_FOUND, "GetParam failed: %#lx, expected DMUS_E_TRACK_NOT_FOUND\n", hr);
    hr = IDirectMusicSegment8_GetParam(seg, &GUID_Valid_Start_Time, 0x1, DMUS_SEG_ANYTRACK, 0,
            NULL, buf);
    ok(hr == DMUS_E_GET_UNSUPPORTED, "GetParam failed: %#lx, expected DMUS_E_GET_UNSUPPORTED\n", hr);

    hr = IDirectMusicSegment8_SetParam(seg, &GUID_Valid_Start_Time, 0x1, 0, 0, buf);
    ok(hr == S_OK, "SetParam failed: %#lx, expected S_OK\n", hr);
    hr = IDirectMusicSegment8_SetParam(seg, &GUID_Valid_Start_Time, 0x1, 1, 0, buf);
    todo_wine ok(hr == DMUS_E_TRACK_NOT_FOUND,
            "SetParam failed: %#lx, expected DMUS_E_TRACK_NOT_FOUND\n", hr);
    hr = IDirectMusicSegment8_SetParam(seg, &GUID_Valid_Start_Time, 0x1, DMUS_SEG_ALLTRACKS,
            0, buf);
    ok(hr == S_OK, "SetParam failed: %#lx, expected S_OK\n", hr);

    IDirectMusicSegment8_Release(seg);
}

static void expect_getparam(IDirectMusicTrack *track, REFGUID type, const char *name,
        HRESULT expect)
{
    HRESULT hr;
    char buf[64] = { 0 };

    hr = IDirectMusicTrack_GetParam(track, type, 0, NULL, buf);
    ok(hr == expect, "GetParam(%s) failed: %#lx, expected %#lx\n", name, hr, expect);
}

static void expect_setparam(IDirectMusicTrack *track, REFGUID type, const char *name,
        HRESULT expect)
{
    HRESULT hr;
    char buf[64] = { 0 };

    hr = IDirectMusicTrack_SetParam(track, type, 0, buf);
    ok(hr == expect, "SetParam(%s) failed: %#lx, expected %#lx\n", name, hr, expect);
}

static void test_track(void)
{
    IDirectMusicTrack *dmt;
    IDirectMusicTrack8 *dmt8;
    IPersistStream *ps;
    CLSID classid;
    ULARGE_INTEGER size;
    HRESULT hr;
#define X(guid)        &guid, #guid
    const struct {
        REFGUID type;
        const char *name;
    } param_types[] = {
        { X(GUID_BandParam) },
        { X(GUID_ChordParam) },
        { X(GUID_Clear_All_Bands) },
        { X(GUID_CommandParam) },
        { X(GUID_CommandParam2) },
        { X(GUID_CommandParamNext) },
        { X(GUID_ConnectToDLSCollection) },
        { X(GUID_Disable_Auto_Download) },
        { X(GUID_DisableTempo) },
        { X(GUID_DisableTimeSig) },
        { X(GUID_Download) },
        { X(GUID_DownloadToAudioPath) },
        { X(GUID_Enable_Auto_Download) },
        { X(GUID_EnableTempo) },
        { X(GUID_EnableTimeSig) },
        { X(GUID_IDirectMusicBand) },
        { X(GUID_IDirectMusicChordMap) },
        { X(GUID_IDirectMusicStyle) },
        { X(GUID_MuteParam) },
        { X(GUID_Play_Marker) },
        { X(GUID_RhythmParam) },
        { X(GUID_SeedVariations) },
        { X(GUID_StandardMIDIFile) },
        { X(GUID_TempoParam) },
        { X(GUID_TimeSignature) },
        { X(GUID_Unload) },
        { X(GUID_UnloadFromAudioPath) },
        { X(GUID_Valid_Start_Time) },
        { X(GUID_Variations) },
        { X(GUID_NULL) }
    };
#undef X
#define X(class)        &CLSID_ ## class, #class
    const struct {
        REFCLSID clsid;
        const char *name;
        /* bitfield with supported param types */
        unsigned int has_params;
    } class[] = {
        { X(DirectMusicLyricsTrack), 0 },
        { X(DirectMusicMarkerTrack), 0x8080000 },
        { X(DirectMusicParamControlTrack), 0 },
        { X(DirectMusicSegmentTriggerTrack), 0x3fffffff },
        { X(DirectMusicSeqTrack), ~0 },         /* param methods not implemented */
        { X(DirectMusicSysExTrack), ~0 },       /* param methods not implemented */
        { X(DirectMusicTempoTrack), 0x802100 },
        { X(DirectMusicTimeSigTrack), 0x1004200 },
        { X(DirectMusicWaveTrack), 0x6001c80 }
    };
#undef X
    unsigned int i, j;

    for (i = 0; i < ARRAY_SIZE(class); i++) {
        trace("Testing %s\n", class[i].name);
        hr = CoCreateInstance(class[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectMusicTrack,
                (void**)&dmt);
        ok(hr == S_OK, "%s create failed: %#lx, expected S_OK\n", class[i].name, hr);

        /* IDirectMusicTrack */
        if (class[i].has_params != ~0) {
            for (j = 0; j < ARRAY_SIZE(param_types); j++) {
                hr = IDirectMusicTrack_IsParamSupported(dmt, param_types[j].type);
                if (class[i].has_params & (1 << j)) {
                    ok(hr == S_OK, "IsParamSupported(%s) failed: %#lx, expected S_OK\n",
                            param_types[j].name, hr);
                    if (class[i].clsid == &CLSID_DirectMusicSegmentTriggerTrack) {
                        expect_getparam(dmt, param_types[j].type, param_types[j].name,
                                DMUS_E_GET_UNSUPPORTED);
                        expect_setparam(dmt, param_types[j].type, param_types[j].name, S_OK);
                    } else if (class[i].clsid == &CLSID_DirectMusicMarkerTrack)
                        expect_setparam(dmt, param_types[j].type, param_types[j].name,
                                DMUS_E_SET_UNSUPPORTED);
                    else if (class[i].clsid == &CLSID_DirectMusicWaveTrack)
                        expect_getparam(dmt, param_types[j].type, param_types[j].name,
                                DMUS_E_GET_UNSUPPORTED);
                } else {
                    ok(hr == DMUS_E_TYPE_UNSUPPORTED,
                            "IsParamSupported(%s) failed: %#lx, expected DMUS_E_TYPE_UNSUPPORTED\n",
                            param_types[j].name, hr);
                    expect_getparam(dmt, param_types[j].type, param_types[j].name,
                            DMUS_E_GET_UNSUPPORTED);
                    if (class[i].clsid == &CLSID_DirectMusicWaveTrack)
                        expect_setparam(dmt, param_types[j].type, param_types[j].name,
                                DMUS_E_TYPE_UNSUPPORTED);
                    else
                        expect_setparam(dmt, param_types[j].type, param_types[j].name,
                                DMUS_E_SET_UNSUPPORTED);
                }

                /* GetParam / SetParam for IsParamSupported supported types */
                if (class[i].clsid == &CLSID_DirectMusicTimeSigTrack) {
                    expect_getparam(dmt, &GUID_DisableTimeSig, "GUID_DisableTimeSig",
                                DMUS_E_GET_UNSUPPORTED);
                    expect_getparam(dmt, &GUID_EnableTimeSig, "GUID_EnableTimeSig",
                                DMUS_E_GET_UNSUPPORTED);
                    expect_setparam(dmt, &GUID_TimeSignature, "GUID_TimeSignature",
                                DMUS_E_SET_UNSUPPORTED);
                } else if (class[i].clsid == &CLSID_DirectMusicTempoTrack) {
                    expect_getparam(dmt, &GUID_DisableTempo, "GUID_DisableTempo",
                                DMUS_E_GET_UNSUPPORTED);
                    expect_getparam(dmt, &GUID_EnableTempo, "GUID_EnableTempo",
                                DMUS_E_GET_UNSUPPORTED);
                }
            }
        } else {
            hr = IDirectMusicTrack_GetParam(dmt, NULL, 0, NULL, NULL);
            ok(hr == E_NOTIMPL, "IDirectMusicTrack_GetParam failed: %#lx\n", hr);
            hr = IDirectMusicTrack_SetParam(dmt, NULL, 0, NULL);
            ok(hr == E_NOTIMPL, "IDirectMusicTrack_SetParam failed: %#lx\n", hr);
            hr = IDirectMusicTrack_IsParamSupported(dmt, NULL);
            ok(hr == E_NOTIMPL, "IDirectMusicTrack_IsParamSupported failed: %#lx\n", hr);

            hr = IDirectMusicTrack_IsParamSupported(dmt, &GUID_IDirectMusicStyle);
            ok(hr == E_NOTIMPL, "got: %#lx\n", hr);
        }
        if (class[i].clsid != &CLSID_DirectMusicMarkerTrack &&
                class[i].clsid != &CLSID_DirectMusicTimeSigTrack) {
            hr = IDirectMusicTrack_AddNotificationType(dmt, NULL);
            ok(hr == E_NOTIMPL, "IDirectMusicTrack_AddNotificationType failed: %#lx\n", hr);
            hr = IDirectMusicTrack_RemoveNotificationType(dmt, NULL);
            ok(hr == E_NOTIMPL, "IDirectMusicTrack_RemoveNotificationType failed: %#lx\n", hr);
        }
        hr = IDirectMusicTrack_Clone(dmt, 0, 0, NULL);
        todo_wine ok(hr == E_POINTER, "IDirectMusicTrack_Clone failed: %#lx\n", hr);

        /* IDirectMusicTrack8 */
        hr = IDirectMusicTrack_QueryInterface(dmt, &IID_IDirectMusicTrack8, (void**)&dmt8);
        if (hr == S_OK) {
            hr = IDirectMusicTrack8_PlayEx(dmt8, NULL, 0, 0, 0, 0, NULL, NULL, 0);
            todo_wine ok(hr == E_POINTER, "IDirectMusicTrack8_PlayEx failed: %#lx\n", hr);
            if (class[i].has_params == ~0) {
                hr = IDirectMusicTrack8_GetParamEx(dmt8, NULL, 0, NULL, NULL, NULL, 0);
                ok(hr == E_NOTIMPL, "IDirectMusicTrack8_GetParamEx failed: %#lx\n", hr);
                hr = IDirectMusicTrack8_SetParamEx(dmt8, NULL, 0, NULL, NULL, 0);
                ok(hr == E_NOTIMPL, "IDirectMusicTrack8_SetParamEx failed: %#lx\n", hr);
            }
            hr = IDirectMusicTrack8_Compose(dmt8, NULL, 0, NULL);
            ok(hr == E_NOTIMPL, "IDirectMusicTrack8_Compose failed: %#lx\n", hr);
            hr = IDirectMusicTrack8_Join(dmt8, NULL, 0, NULL, 0, NULL);
            if (class[i].clsid == &CLSID_DirectMusicTempoTrack)
                todo_wine ok(hr == E_POINTER, "IDirectMusicTrack8_Join failed: %#lx\n", hr);
            else
                ok(hr == E_NOTIMPL, "IDirectMusicTrack8_Join failed: %#lx\n", hr);
            IDirectMusicTrack8_Release(dmt8);
        }

        /* IPersistStream */
        hr = IDirectMusicTrack_QueryInterface(dmt, &IID_IPersistStream, (void**)&ps);
        ok(hr == S_OK, "QueryInterface for IID_IPersistStream failed: %#lx\n", hr);
        hr = IPersistStream_GetClassID(ps, &classid);
        ok(hr == S_OK, "IPersistStream_GetClassID failed: %#lx\n", hr);
        ok(IsEqualGUID(&classid, class[i].clsid),
                "Expected class %s got %s\n", class[i].name, wine_dbgstr_guid(&classid));
        hr = IPersistStream_IsDirty(ps);
        ok(hr == S_FALSE, "IPersistStream_IsDirty failed: %#lx\n", hr);

        /* Unimplemented IPersistStream methods */
        hr = IPersistStream_GetSizeMax(ps, &size);
        ok(hr == E_NOTIMPL, "IPersistStream_GetSizeMax failed: %#lx\n", hr);
        hr = IPersistStream_Save(ps, NULL, TRUE);
        ok(hr == E_NOTIMPL, "IPersistStream_Save failed: %#lx\n", hr);

        while (IDirectMusicTrack_Release(dmt));
    }
}

struct chunk {
    FOURCC id;
    DWORD size;
    FOURCC type;
};

#define CHUNK_HDR_SIZE (sizeof(FOURCC) + sizeof(DWORD))

/* Generate a RIFF file format stream from an array of FOURCC ids.
   RIFF and LIST need to be followed by the form type respectively list type,
   followed by the chunks of the list and terminated with 0. */
static IStream *gen_riff_stream(const FOURCC *ids)
{
    static const LARGE_INTEGER zero;
    int level = -1;
    DWORD *sizes[4];    /* Stack for the sizes of RIFF and LIST chunks */
    char riff[1024];
    char *p = riff;
    struct chunk *ck;
    IStream *stream;

    do {
        ck = (struct chunk *)p;
        ck->id = *ids++;
        switch (ck->id) {
            case 0:
                *sizes[level] = p - (char *)sizes[level] - sizeof(DWORD);
                level--;
                break;
            case FOURCC_LIST:
            case FOURCC_RIFF:
                level++;
                sizes[level] = &ck->size;
                ck->type = *ids++;
                p += sizeof(*ck);
                break;
            case DMUS_FOURCC_GUID_CHUNK:
                ck->size = sizeof(GUID_NULL);
                p += CHUNK_HDR_SIZE;
                memcpy(p, &GUID_NULL, sizeof(GUID_NULL));
                p += ck->size;
                break;
            case DMUS_FOURCC_VERSION_CHUNK:
            {
                DMUS_VERSION ver = {5, 8};

                ck->size = sizeof(ver);
                p += CHUNK_HDR_SIZE;
                memcpy(p, &ver, sizeof(ver));
                p += ck->size;
                break;
            }
            default:
            {
                /* Just convert the FOURCC id to a WCHAR string */
                WCHAR *s;

                ck->size = 5 * sizeof(WCHAR);
                p += CHUNK_HDR_SIZE;
                s = (WCHAR *)p;
                s[0] = (char)(ck->id);
                s[1] = (char)(ck->id >> 8);
                s[2] = (char)(ck->id >> 16);
                s[3] = (char)(ck->id >> 24);
                s[4] = 0;
                p += ck->size;
            }
        }
    } while (level >= 0);

    ck = (struct chunk *)riff;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    IStream_Write(stream, riff, ck->size + CHUNK_HDR_SIZE, NULL);
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);

    return stream;
}

static void test_parsedescriptor(void)
{
    IDirectMusicObject *dmo;
    IStream *stream;
    DMUS_OBJECTDESC desc;
    HRESULT hr;
    DWORD valid;
    unsigned int i;
    /* fourcc ~0 will be replaced later on */
    FOURCC alldesc[] =
    {
        FOURCC_RIFF, ~0, DMUS_FOURCC_CATEGORY_CHUNK, FOURCC_LIST, DMUS_FOURCC_UNFO_LIST,
        DMUS_FOURCC_UNAM_CHUNK, DMUS_FOURCC_UCOP_CHUNK, DMUS_FOURCC_UCMT_CHUNK,
        DMUS_FOURCC_USBJ_CHUNK, 0, DMUS_FOURCC_VERSION_CHUNK, DMUS_FOURCC_GUID_CHUNK, 0
    };
    FOURCC dupes[] =
    {
        FOURCC_RIFF, ~0, DMUS_FOURCC_CATEGORY_CHUNK, DMUS_FOURCC_CATEGORY_CHUNK,
        DMUS_FOURCC_VERSION_CHUNK, DMUS_FOURCC_VERSION_CHUNK, DMUS_FOURCC_GUID_CHUNK,
        DMUS_FOURCC_GUID_CHUNK, FOURCC_LIST, DMUS_FOURCC_UNFO_LIST, DMUS_FOURCC_UNAM_CHUNK, 0,
        FOURCC_LIST, DMUS_FOURCC_UNFO_LIST, mmioFOURCC('I','N','A','M'), 0, 0
    };
    FOURCC empty[] = {FOURCC_RIFF, ~0, 0};
    FOURCC inam[] = {FOURCC_RIFF, ~0, FOURCC_LIST, ~0, mmioFOURCC('I','N','A','M'), 0, 0};
    FOURCC noriff[] = {mmioFOURCC('J','U','N','K'), 0};
#define X(class)        &CLSID_ ## class, #class
#define Y(form)         form, #form
    const struct {
        REFCLSID clsid;
        const char *class;
        FOURCC form;
        const char *name;
        BOOL needs_size;
    } forms[] = {
        { X(DirectMusicSegment), Y(DMUS_FOURCC_SEGMENT_FORM), FALSE },
        { X(DirectMusicSegment), Y(mmioFOURCC('W','A','V','E')), FALSE },
        { X(DirectMusicAudioPathConfig), Y(DMUS_FOURCC_AUDIOPATH_FORM), TRUE },
        { X(DirectMusicGraph), Y(DMUS_FOURCC_TOOLGRAPH_FORM), TRUE },
    };
#undef X
#undef Y

    for (i = 0; i < ARRAY_SIZE(forms); i++) {
        trace("Testing %s / %s\n", forms[i].class, forms[i].name);
        hr = CoCreateInstance(forms[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IDirectMusicObject,
                (void **)&dmo);
        if (hr != S_OK) {
            win_skip("Could not create %s object: %#lx\n", forms[i].class, hr);
            return;
        }

        /* Nothing loaded */
        memset(&desc, 0, sizeof(desc));
        hr = IDirectMusicObject_GetDescriptor(dmo, &desc);
        if (forms[i].needs_size) {
            todo_wine ok(hr == E_INVALIDARG, "GetDescriptor failed: %#lx, expected E_INVALIDARG\n", hr);
            desc.dwSize = sizeof(desc);
            hr = IDirectMusicObject_GetDescriptor(dmo, &desc);
        }
        ok(hr == S_OK, "GetDescriptor failed: %#lx, expected S_OK\n", hr);
        ok(desc.dwValidData == DMUS_OBJ_CLASS, "Got valid data %#lx, expected DMUS_OBJ_CLASS\n",
                desc.dwValidData);
        ok(IsEqualGUID(&desc.guidClass, forms[i].clsid), "Got class guid %s, expected CLSID_%s\n",
                wine_dbgstr_guid(&desc.guidClass), forms[i].class);

        /* Empty RIFF stream */
        empty[1] = forms[i].form;
        stream = gen_riff_stream(empty);
        memset(&desc, 0, sizeof(desc));
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        if (forms[i].needs_size) {
            ok(hr == E_INVALIDARG, "ParseDescriptor failed: %#lx, expected E_INVALIDARG\n", hr);
            desc.dwSize = sizeof(desc);
            hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        }
        ok(hr == S_OK, "ParseDescriptor failed: %#lx, expected S_OK\n", hr);
        ok(desc.dwValidData == DMUS_OBJ_CLASS, "Got valid data %#lx, expected DMUS_OBJ_CLASS\n",
                desc.dwValidData);
        ok(IsEqualGUID(&desc.guidClass, forms[i].clsid), "Got class guid %s, expected CLSID_%s\n",
                wine_dbgstr_guid(&desc.guidClass), forms[i].class);

        /* NULL pointers */
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectMusicObject_ParseDescriptor(dmo, NULL, &desc);
        ok(hr == E_POINTER, "ParseDescriptor failed: %#lx, expected E_POINTER\n", hr);
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, NULL);
        if (forms[i].needs_size)
            ok(hr == E_INVALIDARG, "ParseDescriptor failed: %#lx, expected E_INVALIDARG\n", hr);
        else
            ok(hr == E_POINTER, "ParseDescriptor failed: %#lx, expected E_POINTER\n", hr);
        hr = IDirectMusicObject_ParseDescriptor(dmo, NULL, NULL);
        ok(hr == E_POINTER, "ParseDescriptor failed: %#lx, expected E_POINTER\n", hr);
        IStream_Release(stream);

        /* Wrong form */
        empty[1] = DMUS_FOURCC_CONTAINER_FORM;
        stream = gen_riff_stream(empty);
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        if (forms[i].needs_size)
            ok(hr == DMUS_E_CHUNKNOTFOUND,
                    "ParseDescriptor failed: %#lx, expected DMUS_E_CHUNKNOTFOUND\n", hr);
        else
            ok(hr == E_FAIL, "ParseDescriptor failed: %#lx, expected E_FAIL\n", hr);
        ok(!desc.dwValidData, "Got valid data %#lx, expected 0\n", desc.dwValidData);
        IStream_Release(stream);

        /* Not a RIFF stream */
        stream = gen_riff_stream(noriff);
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        if (forms[i].needs_size)
            ok(hr == DMUS_E_CHUNKNOTFOUND,
                    "ParseDescriptor failed: %#lx, expected DMUS_E_CHUNKNOTFOUND\n", hr);
        else
            ok(hr == E_FAIL, "ParseDescriptor failed: %#lx, expected E_FAIL\n", hr);
        ok(!desc.dwValidData, "Got valid data %#lx, expected 0\n", desc.dwValidData);
        IStream_Release(stream);

        /* All desc chunks */
        alldesc[1] = forms[i].form;
        stream = gen_riff_stream(alldesc);
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        ok(hr == S_OK, "ParseDescriptor failed: %#lx, expected S_OK\n", hr);
        valid = DMUS_OBJ_OBJECT | DMUS_OBJ_CLASS | DMUS_OBJ_VERSION;
        if (forms[i].form != mmioFOURCC('W','A','V','E'))
            valid |= DMUS_OBJ_NAME | DMUS_OBJ_CATEGORY;
        ok(desc.dwValidData == valid, "Got valid data %#lx, expected %#lx\n", desc.dwValidData, valid);
        ok(IsEqualGUID(&desc.guidClass, forms[i].clsid), "Got class guid %s, expected CLSID_%s\n",
                wine_dbgstr_guid(&desc.guidClass), forms[i].class);
        ok(IsEqualGUID(&desc.guidObject, &GUID_NULL), "Got object guid %s, expected GUID_NULL\n",
                wine_dbgstr_guid(&desc.guidClass));
        ok(desc.vVersion.dwVersionMS == 5 && desc.vVersion.dwVersionLS == 8,
            "Got version %lu.%lu, expected 5.8\n", desc.vVersion.dwVersionMS,
            desc.vVersion.dwVersionLS);
        if (forms[i].form != mmioFOURCC('W','A','V','E'))
            ok(!lstrcmpW(desc.wszName, L"UNAM"), "Got name '%s', expected 'UNAM'\n",
                    wine_dbgstr_w(desc.wszName));
        IStream_Release(stream);

        /* Duplicated chunks */
        dupes[1] = forms[i].form;
        stream = gen_riff_stream(dupes);
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        ok(hr == S_OK, "ParseDescriptor failed: %#lx, expected S_OK\n", hr);
        ok(desc.dwValidData == valid, "Got valid data %#lx, expected %#lx\n", desc.dwValidData, valid);
        IStream_Release(stream);

        /* UNFO list with INAM */
        inam[1] = forms[i].form;
        inam[3] = DMUS_FOURCC_UNFO_LIST;
        stream = gen_riff_stream(inam);
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        ok(hr == S_OK, "ParseDescriptor failed: %#lx, expected S_OK\n", hr);
        ok(desc.dwValidData == DMUS_OBJ_CLASS, "Got valid data %#lx, expected DMUS_OBJ_CLASS\n",
                desc.dwValidData);
        IStream_Release(stream);

        /* INFO list with INAM */
        inam[3] = DMUS_FOURCC_INFO_LIST;
        stream = gen_riff_stream(inam);
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectMusicObject_ParseDescriptor(dmo, stream, &desc);
        ok(hr == S_OK, "ParseDescriptor failed: %#lx, expected S_OK\n", hr);
        valid = DMUS_OBJ_CLASS;
        if (forms[i].form == mmioFOURCC('W','A','V','E'))
            valid |= DMUS_OBJ_NAME;
        ok(desc.dwValidData == valid, "Got valid data %#lx, expected %#lx\n", desc.dwValidData, valid);
        if (forms[i].form == mmioFOURCC('W','A','V','E'))
            ok(!lstrcmpW(desc.wszName, L"I"), "Got name '%s', expected 'I'\n",
                    wine_dbgstr_w(desc.wszName));
        IStream_Release(stream);

        IDirectMusicObject_Release(dmo);
    }
}

static void test_performance_tool(void)
{
    IDirectMusicPerformance *performance;
    IDirectMusicGraph *graph;
    IDirectMusicTool *tool;
    DWORD value, types[1];
    DMUS_PMSG msg = {0};
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectMusicPerformance, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicPerformance, (void **)&performance);
    ok(hr == S_OK, "got %#lx\n", hr);

    check_interface(performance, &IID_IDirectMusicTool8, FALSE);

    hr = IDirectMusicPerformance_QueryInterface(performance, &IID_IDirectMusicTool, (void **)&tool);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_QueryInterface(performance, &IID_IDirectMusicGraph, (void **)&graph);
    ok(hr == S_OK, "got %#lx\n", hr);

    hr = IDirectMusicTool_Init(tool, graph);
    ok(hr == E_NOTIMPL, "got %#lx\n", hr);
    value = 0xdeadbeef;
    hr = IDirectMusicTool_GetMsgDeliveryType(tool, &value);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(value == DMUS_PMSGF_TOOL_IMMEDIATE, "got %#lx\n", value);
    value = 0xdeadbeef;
    hr = IDirectMusicTool_GetMediaTypeArraySize(tool, &value);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(value == 0, "got %#lx\n", value);
    hr = IDirectMusicTool_GetMediaTypes(tool, (DWORD **)&types, 64);
    ok(hr == E_NOTIMPL, "got %#lx\n", hr);
    hr = IDirectMusicTool_ProcessPMsg(tool, performance, &msg);
    todo_wine ok(hr == DMUS_S_FREE, "got %#lx\n", hr);
    hr = IDirectMusicTool_Flush(tool, performance, &msg, 0);
    todo_wine ok(hr == S_OK, "got %#lx\n", hr);

    IDirectMusicGraph_Release(graph);
    IDirectMusicTool_Release(tool);

    IDirectMusicPerformance_Release(performance);
}

static void test_performance_graph(void)
{
    IDirectMusicPerformance *performance;
    IDirectMusicGraph *graph, *tmp_graph;
    IDirectMusicTool *tool, *tmp_tool;
    DMUS_PMSG msg;
    HRESULT hr;

    hr = test_tool_create(NULL, 0, &tool);
    ok(hr == S_OK, "got %#lx\n", hr);

    hr = CoCreateInstance(&CLSID_DirectMusicPerformance, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicPerformance, (void **)&performance);
    ok(hr == S_OK, "got %#lx\n", hr);


    /* performance exposes a graph interface but it's not an actual toolgraph */
    hr = IDirectMusicPerformance_QueryInterface(performance, &IID_IDirectMusicGraph, (void **)&graph);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_InsertTool(graph, (IDirectMusicTool *)tool, NULL, 0, -1);
    ok(hr == E_NOTIMPL, "got %#lx\n", hr);
    hr = IDirectMusicGraph_GetTool(graph, 0, &tmp_tool);
    ok(hr == E_NOTIMPL, "got %#lx\n", hr);
    hr = IDirectMusicGraph_RemoveTool(graph, tool);
    ok(hr == E_NOTIMPL, "got %#lx\n", hr);

    /* test IDirectMusicGraph_StampPMsg usage */
    hr = IDirectMusicGraph_StampPMsg(graph, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    memset(&msg, 0, sizeof(msg));
    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == NULL, "got %p\n", msg.pGraph);
    ok(msg.pTool != NULL, "got %p\n", msg.pTool);
    check_interface(msg.pTool, &IID_IDirectMusicPerformance, TRUE);

    ok(!msg.dwSize, "got %ld\n", msg.dwSize);
    ok(!msg.rtTime, "got %I64d\n", msg.rtTime);
    ok(!msg.mtTime, "got %ld\n", msg.mtTime);
    ok(msg.dwFlags == DMUS_PMSGF_TOOL_QUEUE, "got %#lx\n", msg.dwFlags);
    ok(!msg.dwPChannel, "got %ld\n", msg.dwPChannel);
    ok(!msg.dwVirtualTrackID, "got %ld\n", msg.dwVirtualTrackID);
    ok(!msg.dwType, "got %#lx\n", msg.dwType);
    ok(!msg.dwVoiceID, "got %ld\n", msg.dwVoiceID);
    ok(!msg.dwGroupID, "got %ld\n", msg.dwGroupID);
    ok(!msg.punkUser, "got %p\n", msg.punkUser);

    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == NULL, "got %p\n", msg.pGraph);
    ok(msg.pTool != NULL, "got %p\n", msg.pTool);
    check_interface(msg.pTool, &IID_IDirectMusicPerformance, TRUE);

    IDirectMusicTool_Release(msg.pTool);
    msg.pTool = NULL;

    IDirectMusicGraph_Release(graph);


    /* performance doesn't have a default embedded toolgraph */
    hr = IDirectMusicPerformance_GetGraph(performance, &graph);
    ok(hr == DMUS_E_NOT_FOUND, "got %#lx\n", hr);


    /* test adding a graph to the performance */
    hr = CoCreateInstance(&CLSID_DirectMusicGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicGraph, (void **)&graph);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_SetGraph(performance, graph);
    ok(hr == S_OK, "got %#lx\n", hr);

    hr = IDirectMusicPerformance_GetGraph(performance, &tmp_graph);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(tmp_graph == graph, "got %p\n", graph);
    IDirectMusicGraph_Release(tmp_graph);

    hr = IDirectMusicGraph_InsertTool(graph, (IDirectMusicTool *)tool, NULL, 0, -1);
    ok(hr == S_OK, "got %#lx\n", hr);

    IDirectMusicGraph_Release(graph);


    /* test IDirectMusicGraph_StampPMsg usage */
    hr = IDirectMusicPerformance_QueryInterface(performance, &IID_IDirectMusicGraph, (void **)&graph);
    ok(hr == S_OK, "got %#lx\n", hr);

    memset(&msg, 0, sizeof(msg));
    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == graph, "got %p\n", msg.pGraph);
    ok(msg.pTool == tool, "got %p\n", msg.pTool);

    ok(!msg.dwSize, "got %ld\n", msg.dwSize);
    ok(!msg.rtTime, "got %I64d\n", msg.rtTime);
    ok(!msg.mtTime, "got %ld\n", msg.mtTime);
    ok(msg.dwFlags == DMUS_PMSGF_TOOL_IMMEDIATE, "got %#lx\n", msg.dwFlags);
    ok(!msg.dwPChannel, "got %ld\n", msg.dwPChannel);
    ok(!msg.dwVirtualTrackID, "got %ld\n", msg.dwVirtualTrackID);
    ok(!msg.dwType, "got %#lx\n", msg.dwType);
    ok(!msg.dwVoiceID, "got %ld\n", msg.dwVoiceID);
    ok(!msg.dwGroupID, "got %ld\n", msg.dwGroupID);
    ok(!msg.punkUser, "got %p\n", msg.punkUser);

    hr = IDirectMusicGraph_StampPMsg(graph, &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg.pGraph == NULL, "got %p\n", msg.pGraph);
    ok(msg.pTool != NULL, "got %p\n", msg.pTool);
    check_interface(msg.pTool, &IID_IDirectMusicPerformance, TRUE);

    IDirectMusicTool_Release(msg.pTool);
    msg.pTool = NULL;

    IDirectMusicGraph_Release(graph);


    IDirectMusicPerformance_Release(performance);
    IDirectMusicTool_Release(tool);
}

static void test_performance_time(void)
{
    IDirectMusicPerformance *performance;
    REFERENCE_TIME init_time, time;
    IReferenceClock *clock;
    MUSIC_TIME music_time;
    IDirectMusic *dmusic;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DirectMusicPerformance, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicPerformance, (void **)&performance);
    ok(hr == S_OK, "got %#lx\n", hr);


    hr = IDirectMusicPerformance_MusicToReferenceTime(performance, 0, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    time = 0xdeadbeef;
    hr = IDirectMusicPerformance_MusicToReferenceTime(performance, 0, &time);
    ok(hr == DMUS_E_NO_MASTER_CLOCK, "got %#lx\n", hr);
    ok(time == 0, "got %I64d\n", time);

    hr = IDirectMusicPerformance_ReferenceToMusicTime(performance, 0, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    music_time = 0xdeadbeef;
    hr = IDirectMusicPerformance_ReferenceToMusicTime(performance, 0, &music_time);
    ok(hr == DMUS_E_NO_MASTER_CLOCK, "got %#lx\n", hr);
    ok(music_time == 0, "got %ld\n", music_time);


    dmusic = NULL;
    hr = IDirectMusicPerformance_Init(performance, &dmusic, NULL, NULL);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusic_GetMasterClock(dmusic, NULL, &clock);
    ok(hr == S_OK, "got %#lx\n", hr);
    IDirectMusic_Release(dmusic);
    hr = IReferenceClock_GetTime(clock, &init_time);
    ok(hr == S_OK, "got %#lx\n", hr);
    IReferenceClock_Release(clock);


    time = 0xdeadbeef;
    hr = IDirectMusicPerformance_MusicToReferenceTime(performance, 0, &time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(time - init_time <= 100 * 10000, "got %I64d\n", time - init_time);
    init_time = time;

    time = 0xdeadbeef;
    hr = IDirectMusicPerformance_MusicToReferenceTime(performance, 1, &time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(time - init_time >= 6505, "got %I64d\n", time - init_time);
    ok(time - init_time <= 6515, "got %I64d\n", time - init_time);
    time = 0xdeadbeef;
    hr = IDirectMusicPerformance_MusicToReferenceTime(performance, 1000, &time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(time - init_time >= 1000 * 6505, "got %I64d\n", time - init_time);
    ok(time - init_time <= 1000 * 6515, "got %I64d\n", time - init_time);
    time = 0xdeadbeef;
    hr = IDirectMusicPerformance_MusicToReferenceTime(performance, 2000, &time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(time - init_time >= 2000 * 6505, "got %I64d\n", time - init_time);
    ok(time - init_time <= 2000 * 6515, "got %I64d\n", time - init_time);

    music_time = 0xdeadbeef;
    hr = IDirectMusicPerformance_ReferenceToMusicTime(performance, init_time, &music_time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(music_time == 0, "got %ld\n", music_time);
    music_time = 0xdeadbeef;
    hr = IDirectMusicPerformance_ReferenceToMusicTime(performance, init_time + 1000 * 6510, &music_time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(music_time == 1000, "got %ld\n", music_time);
    music_time = 0xdeadbeef;
    hr = IDirectMusicPerformance_ReferenceToMusicTime(performance, init_time + 2000 * 6510, &music_time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(music_time == 2000, "got %ld\n", music_time);

    time = 0xdeadbeef;
    music_time = 0xdeadbeef;
    hr = IDirectMusicPerformance_GetTime(performance, &time, &music_time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(time - init_time <= 200 * 10000, "got %I64d\n", time - init_time);
    ok(music_time == (time - init_time) / 6510, "got %ld\n", music_time);


    hr = IDirectMusicPerformance_CloseDown(performance);
    ok(hr == S_OK, "got %#lx\n", hr);

    IDirectMusicPerformance_Release(performance);

}

static void test_performance_pmsg(void)
{
    static const DWORD delivery_flags[] = {DMUS_PMSGF_TOOL_IMMEDIATE, DMUS_PMSGF_TOOL_QUEUE, DMUS_PMSGF_TOOL_ATTIME};
    static const DWORD message_types[] = {DMUS_PMSGT_MIDI, DMUS_PMSGT_USER};
    IDirectMusicPerformance *performance;
    IDirectMusicGraph *graph;
    IDirectMusicTool *tool;
    DMUS_PMSG *msg, *clone;
    REFERENCE_TIME time;
    HRESULT hr;
    DWORD ret;
    UINT i;

    hr = test_tool_create(message_types, ARRAY_SIZE(message_types), &tool);
    ok(hr == S_OK, "got %#lx\n", hr);

    hr = CoCreateInstance(&CLSID_DirectMusicPerformance, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicPerformance, (void **)&performance);
    ok(hr == S_OK, "got %#lx\n", hr);


    hr = IDirectMusicPerformance_AllocPMsg(performance, 0, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_AllocPMsg(performance, sizeof(DMUS_PMSG) - 1, &msg);
    ok(hr == E_INVALIDARG, "got %#lx\n", hr);

    hr = IDirectMusicPerformance_AllocPMsg(performance, sizeof(DMUS_PMSG), &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg->dwSize == sizeof(DMUS_PMSG), "got %ld\n", msg->dwSize);
    ok(!msg->rtTime, "got %I64d\n", msg->rtTime);
    ok(!msg->mtTime, "got %ld\n", msg->mtTime);
    ok(!msg->dwFlags, "got %#lx\n", msg->dwFlags);
    ok(!msg->dwPChannel, "got %ld\n", msg->dwPChannel);
    ok(!msg->dwVirtualTrackID, "got %ld\n", msg->dwVirtualTrackID);
    ok(!msg->dwType, "got %#lx\n", msg->dwType);
    ok(!msg->dwVoiceID, "got %ld\n", msg->dwVoiceID);
    ok(!msg->dwGroupID, "got %ld\n", msg->dwGroupID);
    ok(!msg->punkUser, "got %p\n", msg->punkUser);

    hr = IDirectMusicPerformance_SendPMsg(performance, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_SendPMsg(performance, msg);
    ok(hr == DMUS_E_NO_MASTER_CLOCK, "got %#lx\n", hr);

    hr = IDirectMusicPerformance_FreePMsg(performance, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_FreePMsg(performance, msg);
    ok(hr == S_OK, "got %#lx\n", hr);


    hr = IDirectMusicPerformance_Init(performance, NULL, 0, 0);
    ok(hr == S_OK, "got %#lx\n", hr);

    hr = CoCreateInstance(&CLSID_DirectMusicGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IDirectMusicGraph, (void **)&graph);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_SetGraph(performance, graph);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_InsertTool(graph, (IDirectMusicTool *)tool, NULL, 0, -1);
    ok(hr == S_OK, "got %#lx\n", hr);
    IDirectMusicGraph_Release(graph);

    hr = IDirectMusicPerformance_AllocPMsg(performance, sizeof(DMUS_PMSG), &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg->dwSize == sizeof(DMUS_PMSG), "got %ld\n", msg->dwSize);
    ok(!msg->rtTime, "got %I64d\n", msg->rtTime);
    ok(!msg->mtTime, "got %ld\n", msg->mtTime);
    ok(!msg->dwFlags, "got %#lx\n", msg->dwFlags);
    ok(!msg->dwPChannel, "got %ld\n", msg->dwPChannel);
    ok(!msg->dwVirtualTrackID, "got %ld\n", msg->dwVirtualTrackID);
    ok(!msg->pTool, "got %p\n", msg->pTool);
    ok(!msg->pGraph, "got %p\n", msg->pGraph);
    ok(!msg->dwType, "got %#lx\n", msg->dwType);
    ok(!msg->dwVoiceID, "got %ld\n", msg->dwVoiceID);
    ok(!msg->dwGroupID, "got %ld\n", msg->dwGroupID);
    ok(!msg->punkUser, "got %p\n", msg->punkUser);
    hr = IDirectMusicPerformance_SendPMsg(performance, msg);
    ok(hr == E_INVALIDARG, "got %#lx\n", hr);

    hr = IDirectMusicPerformance8_ClonePMsg((IDirectMusicPerformance8 *)performance, msg, NULL);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    hr = IDirectMusicPerformance8_ClonePMsg((IDirectMusicPerformance8 *)performance, NULL, &clone);
    ok(hr == E_POINTER, "got %#lx\n", hr);
    clone = NULL;
    hr = IDirectMusicPerformance8_ClonePMsg((IDirectMusicPerformance8 *)performance, msg, &clone);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(clone != NULL, "got %p\n", clone);

    msg->mtTime = 500;
    msg->dwFlags = DMUS_PMSGF_MUSICTIME;
    hr = IDirectMusicPerformance_SendPMsg(performance, msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_SendPMsg(performance, msg);
    ok(hr == DMUS_E_ALREADY_SENT, "got %#lx\n", hr);
    hr = IDirectMusicPerformance_FreePMsg(performance, msg);
    ok(hr == DMUS_E_CANNOT_FREE, "got %#lx\n", hr);

    hr = IDirectMusicPerformance_FreePMsg(performance, clone);
    ok(hr == S_OK, "got %#lx\n", hr);


    time = 0xdeadbeef;
    hr = IDirectMusicPerformance_MusicToReferenceTime(performance, msg->mtTime, &time);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg->dwSize == sizeof(DMUS_PMSG), "got %ld\n", msg->dwSize);
    todo_wine ok(msg->rtTime == time, "got %I64d\n", msg->rtTime);
    ok(msg->mtTime == 500, "got %ld\n", msg->mtTime);
    todo_wine ok(msg->dwFlags & DMUS_PMSGF_REFTIME, "got %#lx\n", msg->dwFlags);
    todo_wine ok(msg->dwFlags & (DMUS_PMSGF_TOOL_QUEUE | DMUS_PMSGF_TOOL_IMMEDIATE), "got %#lx\n", msg->dwFlags);
    ok(!msg->dwPChannel, "got %ld\n", msg->dwPChannel);
    ok(!msg->dwVirtualTrackID, "got %ld\n", msg->dwVirtualTrackID);
    ok(!msg->pTool, "got %p\n", msg->pTool);
    ok(!msg->pGraph, "got %p\n", msg->pGraph);
    ok(!msg->dwType, "got %#lx\n", msg->dwType);
    ok(!msg->dwVoiceID, "got %ld\n", msg->dwVoiceID);
    ok(!msg->dwGroupID, "got %ld\n", msg->dwGroupID);
    ok(!msg->punkUser, "got %p\n", msg->punkUser);


    /* SendPMsg skips all the tools unless messages are stamped beforehand */

    hr = IDirectMusicPerformance_AllocPMsg(performance, sizeof(DMUS_PMSG), &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg->dwSize == sizeof(DMUS_PMSG), "got %ld\n", msg->dwSize);
    msg->rtTime = time;
    msg->dwFlags = DMUS_PMSGF_REFTIME;
    msg->dwType = DMUS_PMSGT_USER;
    hr = IDirectMusicPerformance_SendPMsg(performance, msg);
    ok(hr == S_OK, "got %#lx\n", hr);

    ret = test_tool_wait_message(tool, 10, &msg);
    ok(ret == WAIT_TIMEOUT, "got %#lx\n", ret);
    ok(!msg, "got %p\n", msg);


    hr = IDirectMusicPerformance_AllocPMsg(performance, sizeof(DMUS_PMSG), &msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    ok(msg->dwSize == sizeof(DMUS_PMSG), "got %ld\n", msg->dwSize);
    msg->rtTime = time;
    msg->dwFlags = DMUS_PMSGF_REFTIME;
    msg->dwType = DMUS_PMSGT_USER;

    hr = IDirectMusicPerformance_QueryInterface(performance, &IID_IDirectMusicGraph, (void **)&graph);
    ok(hr == S_OK, "got %#lx\n", hr);
    hr = IDirectMusicGraph_StampPMsg(graph, msg);
    ok(hr == S_OK, "got %#lx\n", hr);
    IDirectMusicGraph_Release(graph);

    hr = IDirectMusicPerformance_SendPMsg(performance, msg);
    ok(hr == S_OK, "got %#lx\n", hr);

    ret = test_tool_wait_message(tool, 50, &msg);
    todo_wine ok(!ret, "got %#lx\n", ret);
    todo_wine ok(msg != NULL, "got %p\n", msg);
    if (!msg) hr = S_OK;
    else hr = IDirectMusicPerformance_FreePMsg(performance, msg);
    ok(hr == S_OK, "got %#lx\n", hr);


    for (i = 0; i < ARRAY_SIZE(delivery_flags); i++)
    {
        DWORD duration = 0;

        hr = IDirectMusicPerformance_GetTime(performance, &time, NULL);
        ok(hr == S_OK, "got %#lx\n", hr);
        hr = IDirectMusicPerformance_AllocPMsg(performance, sizeof(DMUS_PMSG), &msg);
        ok(hr == S_OK, "got %#lx\n", hr);
        ok(msg->dwSize == sizeof(DMUS_PMSG), "got %ld\n", msg->dwSize);
        msg->rtTime = time + 150 * 10000;
        msg->dwFlags = DMUS_PMSGF_REFTIME;
        msg->dwType = DMUS_PMSGT_USER;

        hr = IDirectMusicPerformance_QueryInterface(performance, &IID_IDirectMusicGraph, (void **)&graph);
        ok(hr == S_OK, "got %#lx\n", hr);
        hr = IDirectMusicGraph_StampPMsg(graph, msg);
        ok(hr == S_OK, "got %#lx\n", hr);
        IDirectMusicGraph_Release(graph);

        msg->dwFlags &= ~(DMUS_PMSGF_TOOL_IMMEDIATE | DMUS_PMSGF_TOOL_QUEUE | DMUS_PMSGF_TOOL_ATTIME);
        msg->dwFlags |= delivery_flags[i];

        duration -= GetTickCount();
        hr = IDirectMusicPerformance_SendPMsg(performance, msg);
        ok(hr == S_OK, "got %#lx\n", hr);
        msg = NULL;
        ret = test_tool_wait_message(tool, 1000, &msg);
        todo_wine ok(!ret, "got %#lx\n", ret);
        todo_wine ok(msg != NULL, "got %p\n", msg);
        duration += GetTickCount();

        if (msg) hr = IDirectMusicPerformance_FreePMsg(performance, msg);
        ok(hr == S_OK, "got %#lx\n", hr);

        switch (delivery_flags[i])
        {
        case DMUS_PMSGF_TOOL_IMMEDIATE: todo_wine ok(duration <= 50, "got %lu\n", duration); break;
        case DMUS_PMSGF_TOOL_QUEUE: todo_wine ok(duration >= 50 && duration <= 100, "got %lu\n", duration); break;
        case DMUS_PMSGF_TOOL_ATTIME: todo_wine ok(duration >= 150 && duration <= 500, "got %lu\n", duration); break;
        }
    }


    hr = IDirectMusicPerformance_CloseDown(performance);
    ok(hr == S_OK, "got %#lx\n", hr);


    IDirectMusicPerformance_Release(performance);
}

START_TEST(dmime)
{
    CoInitialize(NULL);

    if (missing_dmime())
    {
        skip("dmime not available\n");
        CoUninitialize();
        return;
    }
    test_COM_audiopath();
    test_COM_audiopathconfig();
    test_COM_graph();
    test_COM_segment();
    test_COM_segmentstate();
    test_COM_track();
    test_audiopathconfig();
    test_graph();
    test_segment();
    test_gettrack();
    test_segment_param();
    test_track();
    test_parsedescriptor();
    test_performance_tool();
    test_performance_graph();
    test_performance_time();
    test_performance_pmsg();

    CoUninitialize();
}
