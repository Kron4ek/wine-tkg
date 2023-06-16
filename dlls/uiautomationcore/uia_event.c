/*
 * Copyright 2023 Connor McAdams for CodeWeavers
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

#include "uia_private.h"

#include "wine/debug.h"
#include "wine/rbtree.h"
#include "assert.h"

WINE_DEFAULT_DEBUG_CHANNEL(uiautomation);

static SAFEARRAY *uia_desktop_node_rt_id;
static BOOL WINAPI uia_init_desktop_rt_id(INIT_ONCE *once, void *param, void **ctx)
{
    SAFEARRAY *sa;

    if ((sa = SafeArrayCreateVector(VT_I4, 0, 2)))
    {
        if (SUCCEEDED(write_runtime_id_base(sa, GetDesktopWindow())))
            uia_desktop_node_rt_id = sa;
        else
            SafeArrayDestroy(sa);
    }

    return !!uia_desktop_node_rt_id;
}

static SAFEARRAY *uia_get_desktop_rt_id(void)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

    if (!uia_desktop_node_rt_id)
        InitOnceExecuteOnce(&once, uia_init_desktop_rt_id, NULL, NULL);

    return uia_desktop_node_rt_id;
}

/*
 * UI Automation event map.
 */
static struct uia_event_map
{
    struct rb_tree event_map;
    LONG event_count;
} uia_event_map;

struct uia_event_map_entry
{
    struct rb_entry entry;
    LONG refs;

    int event_id;

    /*
     * List of registered events for this event ID. Events are only removed
     * from the list when the event map entry reference count hits 0 and the
     * entry is destroyed. This avoids dealing with mid-list removal while
     * iterating over the list when an event is raised. Rather than remove
     * an event from the list, we mark an event as being defunct so it is
     * ignored.
     */
    struct list events_list;
};

static CRITICAL_SECTION event_map_cs;
static CRITICAL_SECTION_DEBUG event_map_cs_debug =
{
    0, 0, &event_map_cs,
    { &event_map_cs_debug.ProcessLocksList, &event_map_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": event_map_cs") }
};
static CRITICAL_SECTION event_map_cs = { &event_map_cs_debug, -1, 0, 0, 0, 0 };

static int uia_event_map_id_compare(const void *key, const struct rb_entry *entry)
{
    struct uia_event_map_entry *event_entry = RB_ENTRY_VALUE(entry, struct uia_event_map_entry, entry);
    int event_id = *((int *)key);

    return (event_entry->event_id > event_id) - (event_entry->event_id < event_id);
}

static struct uia_event_map_entry *uia_get_event_map_entry_for_event(int event_id)
{
    struct uia_event_map_entry *map_entry = NULL;
    struct rb_entry *rb_entry;

    if (uia_event_map.event_count && (rb_entry = rb_get(&uia_event_map.event_map, &event_id)))
        map_entry = RB_ENTRY_VALUE(rb_entry, struct uia_event_map_entry, entry);

    return map_entry;
}

static HRESULT uia_event_map_add_event(struct uia_event *event)
{
    const int subtree_scope = TreeScope_Element | TreeScope_Descendants;
    struct uia_event_map_entry *event_entry;

    if (((event->scope & subtree_scope) == subtree_scope) && event->runtime_id &&
            !uia_compare_safearrays(uia_get_desktop_rt_id(), event->runtime_id, UIAutomationType_IntArray))
        event->desktop_subtree_event = TRUE;

    EnterCriticalSection(&event_map_cs);

    if (!(event_entry = uia_get_event_map_entry_for_event(event->event_id)))
    {
        if (!(event_entry = heap_alloc_zero(sizeof(*event_entry))))
        {
            LeaveCriticalSection(&event_map_cs);
            return E_OUTOFMEMORY;
        }

        event_entry->event_id = event->event_id;
        list_init(&event_entry->events_list);

        if (!uia_event_map.event_count)
            rb_init(&uia_event_map.event_map, uia_event_map_id_compare);
        rb_put(&uia_event_map.event_map, &event->event_id, &event_entry->entry);
        uia_event_map.event_count++;
    }

    IWineUiaEvent_AddRef(&event->IWineUiaEvent_iface);
    list_add_head(&event_entry->events_list, &event->event_list_entry);
    InterlockedIncrement(&event_entry->refs);

    event->event_map_entry = event_entry;
    LeaveCriticalSection(&event_map_cs);

    return S_OK;
}

static void uia_event_map_entry_release(struct uia_event_map_entry *entry)
{
    ULONG ref = InterlockedDecrement(&entry->refs);

    if (!ref)
    {
        struct list *cursor, *cursor2;

        EnterCriticalSection(&event_map_cs);

        /*
         * Someone grabbed this while we were waiting to enter the CS, abort
         * destruction.
         */
        if (InterlockedCompareExchange(&entry->refs, 0, 0) != 0)
        {
            LeaveCriticalSection(&event_map_cs);
            return;
        }

        rb_remove(&uia_event_map.event_map, &entry->entry);
        uia_event_map.event_count--;
        LeaveCriticalSection(&event_map_cs);

        /* Release all events in the list. */
        LIST_FOR_EACH_SAFE(cursor, cursor2, &entry->events_list)
        {
            struct uia_event *event = LIST_ENTRY(cursor, struct uia_event, event_list_entry);

            IWineUiaEvent_Release(&event->IWineUiaEvent_iface);
        }

        heap_free(entry);
    }
}

/*
 * IWineUiaEvent interface.
 */
static inline struct uia_event *impl_from_IWineUiaEvent(IWineUiaEvent *iface)
{
    return CONTAINING_RECORD(iface, struct uia_event, IWineUiaEvent_iface);
}

static HRESULT WINAPI uia_event_QueryInterface(IWineUiaEvent *iface, REFIID riid, void **ppv)
{
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IWineUiaEvent) || IsEqualIID(riid, &IID_IUnknown))
        *ppv = iface;
    else
        return E_NOINTERFACE;

    IWineUiaEvent_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI uia_event_AddRef(IWineUiaEvent *iface)
{
    struct uia_event *event = impl_from_IWineUiaEvent(iface);
    ULONG ref = InterlockedIncrement(&event->ref);

    TRACE("%p, refcount %ld\n", event, ref);
    return ref;
}

static ULONG WINAPI uia_event_Release(IWineUiaEvent *iface)
{
    struct uia_event *event = impl_from_IWineUiaEvent(iface);
    ULONG ref = InterlockedDecrement(&event->ref);

    TRACE("%p, refcount %ld\n", event, ref);
    if (!ref)
    {
        int i;

        /*
         * If this event has an event_map_entry, it should've been released
         * before hitting a reference count of 0.
         */
        assert(!event->event_map_entry);

        SafeArrayDestroy(event->runtime_id);
        uia_cache_request_destroy(&event->cache_req);
        for (i = 0; i < event->event_advisers_count; i++)
            IWineUiaEventAdviser_Release(event->event_advisers[i]);
        heap_free(event->event_advisers);
        heap_free(event);
    }

    return ref;
}

static HRESULT WINAPI uia_event_advise_events(IWineUiaEvent *iface, BOOL advise_added)
{
    struct uia_event *event = impl_from_IWineUiaEvent(iface);
    HRESULT hr;
    int i;

    TRACE("%p, %d\n", event, advise_added);

    for (i = 0; i < event->event_advisers_count; i++)
    {
        hr = IWineUiaEventAdviser_advise(event->event_advisers[i], advise_added, (UINT_PTR)event);
        if (FAILED(hr))
            return hr;
    }

    /*
     * Once we've advised of removal, no need to keep the advisers around.
     * We can also release our reference to the event map.
     */
    if (!advise_added)
    {
        InterlockedIncrement(&event->event_defunct);
        uia_event_map_entry_release(event->event_map_entry);
        event->event_map_entry = NULL;

        for (i = 0; i < event->event_advisers_count; i++)
            IWineUiaEventAdviser_Release(event->event_advisers[i]);
        heap_free(event->event_advisers);
        event->event_advisers_count = event->event_advisers_arr_size = 0;
    }

    return S_OK;
}

static const IWineUiaEventVtbl uia_event_vtbl = {
    uia_event_QueryInterface,
    uia_event_AddRef,
    uia_event_Release,
    uia_event_advise_events,
};

static struct uia_event *unsafe_impl_from_IWineUiaEvent(IWineUiaEvent *iface)
{
    if (!iface || (iface->lpVtbl != &uia_event_vtbl))
        return NULL;

    return CONTAINING_RECORD(iface, struct uia_event, IWineUiaEvent_iface);
}

static HRESULT create_uia_event(struct uia_event **out_event, int event_id, int scope, UiaEventCallback *cback,
        SAFEARRAY *runtime_id)
{
    struct uia_event *event = heap_alloc_zero(sizeof(*event));

    *out_event = NULL;
    if (!event)
        return E_OUTOFMEMORY;

    event->IWineUiaEvent_iface.lpVtbl = &uia_event_vtbl;
    event->ref = 1;
    event->runtime_id = runtime_id;
    event->event_id = event_id;
    event->scope = scope;
    event->cback = cback;

    *out_event = event;
    return S_OK;
}

/*
 * IWineUiaEventAdviser interface.
 */
struct uia_event_adviser {
    IWineUiaEventAdviser IWineUiaEventAdviser_iface;
    LONG ref;

    IRawElementProviderAdviseEvents *advise_events;
    DWORD git_cookie;
};

static inline struct uia_event_adviser *impl_from_IWineUiaEventAdviser(IWineUiaEventAdviser *iface)
{
    return CONTAINING_RECORD(iface, struct uia_event_adviser, IWineUiaEventAdviser_iface);
}

static HRESULT WINAPI uia_event_adviser_QueryInterface(IWineUiaEventAdviser *iface, REFIID riid, void **ppv)
{
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IWineUiaEventAdviser) || IsEqualIID(riid, &IID_IUnknown))
        *ppv = iface;
    else
        return E_NOINTERFACE;

    IWineUiaEventAdviser_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI uia_event_adviser_AddRef(IWineUiaEventAdviser *iface)
{
    struct uia_event_adviser *adv_events = impl_from_IWineUiaEventAdviser(iface);
    ULONG ref = InterlockedIncrement(&adv_events->ref);

    TRACE("%p, refcount %ld\n", adv_events, ref);
    return ref;
}

static ULONG WINAPI uia_event_adviser_Release(IWineUiaEventAdviser *iface)
{
    struct uia_event_adviser *adv_events = impl_from_IWineUiaEventAdviser(iface);
    ULONG ref = InterlockedDecrement(&adv_events->ref);

    TRACE("%p, refcount %ld\n", adv_events, ref);
    if (!ref)
    {
        if (adv_events->git_cookie)
        {
            if (FAILED(unregister_interface_in_git(adv_events->git_cookie)))
                WARN("Failed to revoke advise events interface from GIT\n");
        }
        IRawElementProviderAdviseEvents_Release(adv_events->advise_events);
        heap_free(adv_events);
    }

    return ref;
}

static HRESULT WINAPI uia_event_adviser_advise(IWineUiaEventAdviser *iface, BOOL advise_added, LONG_PTR huiaevent)
{
    struct uia_event_adviser *adv_events = impl_from_IWineUiaEventAdviser(iface);
    struct uia_event *event_data = (struct uia_event *)huiaevent;
    IRawElementProviderAdviseEvents *advise_events;
    HRESULT hr;

    TRACE("%p, %d, %#Ix\n", adv_events, advise_added, huiaevent);

    if (adv_events->git_cookie)
    {
        hr = get_interface_in_git(&IID_IRawElementProviderAdviseEvents, adv_events->git_cookie,
                (IUnknown **)&advise_events);
        if (FAILED(hr))
            return hr;
    }
    else
    {
        advise_events = adv_events->advise_events;
        IRawElementProviderAdviseEvents_AddRef(advise_events);
    }

    if (advise_added)
        hr = IRawElementProviderAdviseEvents_AdviseEventAdded(advise_events, event_data->event_id, NULL);
    else
        hr = IRawElementProviderAdviseEvents_AdviseEventRemoved(advise_events, event_data->event_id, NULL);

    IRawElementProviderAdviseEvents_Release(advise_events);
    return hr;
}

static const IWineUiaEventAdviserVtbl uia_event_adviser_vtbl = {
    uia_event_adviser_QueryInterface,
    uia_event_adviser_AddRef,
    uia_event_adviser_Release,
    uia_event_adviser_advise,
};

HRESULT uia_event_add_provider_event_adviser(IRawElementProviderAdviseEvents *advise_events, struct uia_event *event)
{
    struct uia_event_adviser *adv_events;
    IRawElementProviderSimple *elprov;
    enum ProviderOptions prov_opts;
    HRESULT hr;

    hr = IRawElementProviderAdviseEvents_QueryInterface(advise_events, &IID_IRawElementProviderSimple,
            (void **)&elprov);
    if (FAILED(hr))
    {
        ERR("Failed to get IRawElementProviderSimple from advise events\n");
        return E_FAIL;
    }

    hr = IRawElementProviderSimple_get_ProviderOptions(elprov, &prov_opts);
    IRawElementProviderSimple_Release(elprov);
    if (FAILED(hr))
        return hr;

    if (!(adv_events = heap_alloc_zero(sizeof(*adv_events))))
        return E_OUTOFMEMORY;

    if (prov_opts & ProviderOptions_UseComThreading)
    {
        hr = register_interface_in_git((IUnknown *)advise_events, &IID_IRawElementProviderAdviseEvents,
                &adv_events->git_cookie);
        if (FAILED(hr))
        {
            heap_free(adv_events);
            return hr;
        }
    }

    adv_events->IWineUiaEventAdviser_iface.lpVtbl = &uia_event_adviser_vtbl;
    adv_events->ref = 1;
    adv_events->advise_events = advise_events;
    IRawElementProviderAdviseEvents_AddRef(advise_events);

    if (!uia_array_reserve((void **)&event->event_advisers, &event->event_advisers_arr_size,
                event->event_advisers_count + 1, sizeof(*event->event_advisers)))
    {
        IWineUiaEventAdviser_Release(&adv_events->IWineUiaEventAdviser_iface);
        return E_OUTOFMEMORY;
    }

    event->event_advisers[event->event_advisers_count] = &adv_events->IWineUiaEventAdviser_iface;
    event->event_advisers_count++;

    return S_OK;
}

/***********************************************************************
 *          UiaAddEvent (uiautomationcore.@)
 */
HRESULT WINAPI UiaAddEvent(HUIANODE huianode, EVENTID event_id, UiaEventCallback *callback, enum TreeScope scope,
        PROPERTYID *prop_ids, int prop_ids_count, struct UiaCacheRequest *cache_req, HUIAEVENT *huiaevent)
{
    const struct uia_event_info *event_info = uia_event_info_from_id(event_id);
    struct uia_event *event;
    SAFEARRAY *sa;
    HRESULT hr;

    TRACE("(%p, %d, %p, %#x, %p, %d, %p, %p)\n", huianode, event_id, callback, scope, prop_ids, prop_ids_count,
            cache_req, huiaevent);

    if (!huianode || !callback || !cache_req || !huiaevent)
        return E_INVALIDARG;

    if (!event_info)
        WARN("No event information for event ID %d\n", event_id);

    *huiaevent = NULL;
    if (event_info && (event_info->event_arg_type == EventArgsType_PropertyChanged))
    {
        FIXME("Property changed event registration currently unimplemented\n");
        return E_NOTIMPL;
    }

    hr = UiaGetRuntimeId(huianode, &sa);
    if (FAILED(hr))
        return hr;

    hr = create_uia_event(&event, event_id, scope, callback, sa);
    if (FAILED(hr))
    {
        SafeArrayDestroy(sa);
        return hr;
    }

    hr = uia_cache_request_clone(&event->cache_req, cache_req);
    if (FAILED(hr))
        goto exit;

    hr = attach_event_to_uia_node(huianode, event);
    if (FAILED(hr))
        goto exit;

    hr = IWineUiaEvent_advise_events(&event->IWineUiaEvent_iface, TRUE);
    if (FAILED(hr))
        goto exit;

    hr = uia_event_map_add_event(event);
    if (FAILED(hr))
        goto exit;

    *huiaevent = (HUIAEVENT)event;

exit:
    if (FAILED(hr))
        IWineUiaEvent_Release(&event->IWineUiaEvent_iface);

    return hr;
}

/***********************************************************************
 *          UiaRemoveEvent (uiautomationcore.@)
 */
HRESULT WINAPI UiaRemoveEvent(HUIAEVENT huiaevent)
{
    struct uia_event *event = unsafe_impl_from_IWineUiaEvent((IWineUiaEvent *)huiaevent);
    HRESULT hr;

    TRACE("(%p)\n", event);

    if (!event)
        return E_INVALIDARG;

    hr = IWineUiaEvent_advise_events(&event->IWineUiaEvent_iface, FALSE);
    IWineUiaEvent_Release(&event->IWineUiaEvent_iface);
    if (FAILED(hr))
        WARN("advise_events failed with hr %#lx\n", hr);

    return S_OK;
}

static HRESULT uia_event_invoke(HUIANODE node, struct UiaEventArgs *args, struct uia_event *event)
{
    SAFEARRAY *out_req;
    BSTR tree_struct;
    HRESULT hr;

    hr = UiaGetUpdatedCache(node, &event->cache_req, NormalizeState_View, NULL, &out_req,
            &tree_struct);
    if (SUCCEEDED(hr))
    {
        event->cback(args, out_req, tree_struct);
        SafeArrayDestroy(out_req);
        SysFreeString(tree_struct);
    }

    return hr;
}

/*
 * Check if the provider that raised the event matches this particular event.
 */
static HRESULT uia_event_check_match(HUIANODE node, SAFEARRAY *rt_id, struct UiaEventArgs *args, struct uia_event *event)
{
    struct UiaPropertyCondition prop_cond = { ConditionType_Property, UIA_RuntimeIdPropertyId };
    struct uia_node *node_data = impl_from_IWineUiaNode((IWineUiaNode *)node);
    HRESULT hr = S_OK;

    /* Event is no longer valid. */
    if (InterlockedCompareExchange(&event->event_defunct, 0, 0) != 0)
        return S_OK;

    /* Can't match an event that doesn't have a runtime ID, early out. */
    if (!event->runtime_id)
        return S_OK;

    if (event->desktop_subtree_event)
        return uia_event_invoke(node, args, event);

    if (rt_id && !uia_compare_safearrays(rt_id, event->runtime_id, UIAutomationType_IntArray))
    {
        if (event->scope & TreeScope_Element)
            hr = uia_event_invoke(node, args, event);
        return hr;
    }

    if (!(event->scope & (TreeScope_Descendants | TreeScope_Children)))
        return S_OK;

    V_VT(&prop_cond.Value) = VT_I4 | VT_ARRAY;
    V_ARRAY(&prop_cond.Value) = event->runtime_id;

    IWineUiaNode_AddRef(&node_data->IWineUiaNode_iface);
    while (1)
    {
        HUIANODE node2 = NULL;

        hr = navigate_uia_node(node_data, NavigateDirection_Parent, &node2);
        IWineUiaNode_Release(&node_data->IWineUiaNode_iface);
        if (FAILED(hr) || !node2)
            return hr;

        node_data = impl_from_IWineUiaNode((IWineUiaNode *)node2);
        hr = uia_condition_check(node2, (struct UiaCondition *)&prop_cond);
        if (FAILED(hr))
            break;

        if (uia_condition_matched(hr))
        {
            hr = uia_event_invoke(node, args, event);
            break;
        }

        if (!(event->scope & TreeScope_Descendants))
            break;
    }
    IWineUiaNode_Release(&node_data->IWineUiaNode_iface);

    return hr;
}

static HRESULT uia_raise_event(IRawElementProviderSimple *elprov, struct UiaEventArgs *args)
{
    struct uia_event_map_entry *event_entry;
    enum ProviderOptions prov_opts = 0;
    struct list *cursor, *cursor2;
    HUIANODE node;
    SAFEARRAY *sa;
    HRESULT hr;

    hr = IRawElementProviderSimple_get_ProviderOptions(elprov, &prov_opts);
    if (FAILED(hr))
        return hr;

    EnterCriticalSection(&event_map_cs);
    if ((event_entry = uia_get_event_map_entry_for_event(args->EventId)))
        InterlockedIncrement(&event_entry->refs);
    LeaveCriticalSection(&event_map_cs);

    if (!event_entry)
        return S_OK;

    /*
     * For events raised on server-side providers, we don't want to add any
     * clientside HWND providers.
     */
    if (prov_opts & ProviderOptions_ServerSideProvider)
        hr = create_uia_node_from_elprov(elprov, &node, FALSE);
    else
        hr = create_uia_node_from_elprov(elprov, &node, TRUE);
    if (FAILED(hr))
    {
        uia_event_map_entry_release(event_entry);
        return hr;
    }

    hr = UiaGetRuntimeId(node, &sa);
    if (FAILED(hr))
    {
        uia_event_map_entry_release(event_entry);
        UiaNodeRelease(node);
        return hr;
    }

    LIST_FOR_EACH_SAFE(cursor, cursor2, &event_entry->events_list)
    {
        struct uia_event *event = LIST_ENTRY(cursor, struct uia_event, event_list_entry);

        hr = uia_event_check_match(node, sa, args, event);
        if (FAILED(hr))
            break;
    }

    uia_event_map_entry_release(event_entry);
    SafeArrayDestroy(sa);
    UiaNodeRelease(node);

    return hr;
}

/***********************************************************************
 *          UiaRaiseAutomationEvent (uiautomationcore.@)
 */
HRESULT WINAPI UiaRaiseAutomationEvent(IRawElementProviderSimple *elprov, EVENTID id)
{
    const struct uia_event_info *event_info = uia_event_info_from_id(id);
    struct UiaEventArgs args = { EventArgsType_Simple, id };
    HRESULT hr;

    TRACE("(%p, %d)\n", elprov, id);

    if (!elprov)
        return E_INVALIDARG;

    if (!event_info || event_info->event_arg_type != EventArgsType_Simple)
    {
        if (!event_info)
            FIXME("No event info structure for event id %d\n", id);
        else
            WARN("Wrong event raising function for event args type %d\n", event_info->event_arg_type);

        return S_OK;
    }

    hr = uia_raise_event(elprov, &args);
    if (FAILED(hr))
        return hr;

    return S_OK;
}
