/* WinRT Windows.Devices.Enumeration implementation
 *
 * Copyright 2021 Gijs Vermeulen
 * Copyright 2022 Julian Klemann for CodeWeavers
 * Copyright 2025 Vibhav Pant
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
#include <assert.h>

#include "initguid.h"
#include "private.h"
#include "devpropdef.h"
#include "devfiltertypes.h"
#include "devquery.h"
#include "aqs.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(enumeration);

struct devquery_params
{
    IUnknown IUnknown_iface;
    struct aqs_expr *expr;
    LONG ref;
};

static inline struct devquery_params *impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct devquery_params, IUnknown_iface );
}

static HRESULT WINAPI devquery_params_QueryInterface( IUnknown *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return S_OK;
}

static ULONG WINAPI devquery_params_AddRef( IUnknown *iface )
{
    struct devquery_params *impl = impl_from_IUnknown( iface );

    TRACE( "iface %p\n", iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI devquery_params_Release( IUnknown *iface )
{
    struct devquery_params *impl = impl_from_IUnknown( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p\n", iface );

    if (!ref)
    {
        free_aqs_expr( impl->expr );
        free( impl );
    }
    return ref;
}

static const IUnknownVtbl devquery_params_vtbl =
{
    /* IUnknown */
    devquery_params_QueryInterface,
    devquery_params_AddRef,
    devquery_params_Release,
};

static HRESULT devquery_params_create( struct aqs_expr *expr, IUnknown **out )
{
    struct devquery_params *impl;

    *out = NULL;
    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;

    impl->IUnknown_iface.lpVtbl = &devquery_params_vtbl;
    impl->ref = 1;
    impl->expr = expr;
    *out = &impl->IUnknown_iface;
    return S_OK;
}

struct device_watcher
{
    IDeviceWatcher IDeviceWatcher_iface;
    struct weak_reference_source weak_reference_source;

    struct list added_handlers;
    struct list enumerated_handlers;
    struct list stopped_handlers;
    IUnknown *query_params;
    BOOL aqs_all_whitespace;

    CRITICAL_SECTION cs;
    DeviceWatcherStatus status;
    HDEVQUERY query;
};

static inline struct device_watcher *impl_from_IDeviceWatcher( IDeviceWatcher *iface )
{
    return CONTAINING_RECORD( iface, struct device_watcher, IDeviceWatcher_iface );
}

static HRESULT WINAPI device_watcher_QueryInterface( IDeviceWatcher *iface, REFIID iid, void **out )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );

    TRACE( "iface %p, iid %s, out %p stub!\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IDeviceWatcher ))
    {
        IInspectable_AddRef( (*out = &impl->IDeviceWatcher_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWeakReferenceSource ))
    {
        *out = &impl->weak_reference_source.IWeakReferenceSource_iface;
        IWeakReferenceSource_AddRef(*out);
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI device_watcher_AddRef( IDeviceWatcher *iface )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    ULONG ref = weak_reference_strong_add_ref( &impl->weak_reference_source );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI device_watcher_Release( IDeviceWatcher *iface )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    ULONG ref = weak_reference_strong_release( &impl->weak_reference_source );
    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        typed_event_handlers_clear( &impl->added_handlers );
        typed_event_handlers_clear( &impl->enumerated_handlers );
        typed_event_handlers_clear( &impl->stopped_handlers );
        IUnknown_Release( impl->query_params );
        impl->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &impl->cs );
        if (impl->query) DevCloseObjectQuery( impl->query );
        free( impl );
    }

    return ref;
}

static HRESULT WINAPI device_watcher_GetIids( IDeviceWatcher *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_watcher_GetRuntimeClassName( IDeviceWatcher *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_watcher_GetTrustLevel( IDeviceWatcher *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_watcher_add_Added( IDeviceWatcher *iface, ITypedEventHandler_DeviceWatcher_DeviceInformation *handler,
                                                EventRegistrationToken *token )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    return typed_event_handlers_append( &impl->added_handlers, (ITypedEventHandler_IInspectable_IInspectable *)handler, token );
}

static HRESULT WINAPI device_watcher_remove_Added( IDeviceWatcher *iface, EventRegistrationToken token )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return typed_event_handlers_remove( &impl->added_handlers, &token );
}

static HRESULT WINAPI device_watcher_add_Updated( IDeviceWatcher *iface, ITypedEventHandler_DeviceWatcher_DeviceInformationUpdate *handler,
                                                  EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    return S_OK;
}

static HRESULT WINAPI device_watcher_remove_Updated( IDeviceWatcher *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_watcher_add_Removed( IDeviceWatcher *iface, ITypedEventHandler_DeviceWatcher_DeviceInformationUpdate *handler,
                                                  EventRegistrationToken *token )
{
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_watcher_remove_Removed( IDeviceWatcher *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_watcher_add_EnumerationCompleted( IDeviceWatcher *iface, ITypedEventHandler_DeviceWatcher_IInspectable *handler,
                                                               EventRegistrationToken *token )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    TRACE( "iface %p, handler %p, token %p\n", iface, handler, token );
    return typed_event_handlers_append( &impl->enumerated_handlers, (ITypedEventHandler_IInspectable_IInspectable *)handler, token );
}

static HRESULT WINAPI device_watcher_remove_EnumerationCompleted( IDeviceWatcher *iface, EventRegistrationToken token )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return typed_event_handlers_remove( &impl->enumerated_handlers, &token );
}

static HRESULT WINAPI device_watcher_add_Stopped( IDeviceWatcher *iface, ITypedEventHandler_DeviceWatcher_IInspectable *handler, EventRegistrationToken *token )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    return typed_event_handlers_append( &impl->stopped_handlers, (ITypedEventHandler_IInspectable_IInspectable *)handler, token );
}

static HRESULT WINAPI device_watcher_remove_Stopped( IDeviceWatcher *iface, EventRegistrationToken token )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    return typed_event_handlers_remove( &impl->stopped_handlers, &token );
}

static HRESULT WINAPI device_watcher_get_Status( IDeviceWatcher *iface, DeviceWatcherStatus *status )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );

    TRACE( "iface %p, status %p\n", iface, status );

    EnterCriticalSection( &impl->cs );
    *status = impl->status;
    LeaveCriticalSection( &impl->cs );

    return S_OK;
}

static const char *debugstr_DEV_QUERY_RESULT_ACTION_DATA( const DEV_QUERY_RESULT_ACTION_DATA *data )
{
    const DEV_OBJECT *obj = &data->Data.DeviceObject;
    if (!data) return wine_dbg_sprintf( "(null)" );
    if (data->Action == DevQueryResultStateChange)
        return wine_dbg_sprintf( "{%d {%d}}", data->Action, data->Data.State );
    return wine_dbg_sprintf( "{%d {{%d %s %lu %p}}}", data->Action, obj->ObjectType, debugstr_w( obj->pszObjectId ), obj->cPropertyCount, obj->pProperties );
}

static void WINAPI device_object_query_callback( HDEVQUERY query, void *data,
                                                 const DEV_QUERY_RESULT_ACTION_DATA *action_data )
{
    struct device_watcher *watcher;
    IWeakReference *weak = data;
    IDeviceWatcher *iface;
    HRESULT hr;

    TRACE( "query %p, data %p, action_data %s.\n", query, data, debugstr_DEV_QUERY_RESULT_ACTION_DATA( action_data ) );

    if (FAILED(hr = IWeakReference_Resolve( weak, &IID_IDeviceWatcher, (IInspectable **)&iface )) || !iface)
    {
        if (action_data->Action == DevQueryResultStateChange &&
            (action_data->Data.State == DevQueryStateClosed || action_data->Data.State == DevQueryStateAborted))
            IWeakReference_Release( weak ); /* No more callbacks are expected, so we can release the weak ref. */
        return;
    }
    watcher = impl_from_IDeviceWatcher( iface );

    switch (action_data->Action)
    {
    case DevQueryResultStateChange:
        switch (action_data->Data.State)
        {
        case DevQueryStateClosed:
            EnterCriticalSection( &watcher->cs );
            watcher->status = DeviceWatcherStatus_Stopped;
            LeaveCriticalSection( &watcher->cs );
            typed_event_handlers_notify( &watcher->stopped_handlers, (IInspectable *)iface, NULL );
            IWeakReference_Release( weak );
            break;
        case DevQueryStateAborted:
            EnterCriticalSection( &watcher->cs );
            watcher->status = DeviceWatcherStatus_Aborted;
            DevCloseObjectQuery( watcher->query );
            watcher->query = NULL;
            LeaveCriticalSection( &watcher->cs );
            IWeakReference_Release( weak );
            break;
        case DevQueryStateEnumCompleted:
            EnterCriticalSection( &watcher->cs );
            watcher->status = DeviceWatcherStatus_EnumerationCompleted;
            LeaveCriticalSection( &watcher->cs );

            typed_event_handlers_notify( &watcher->enumerated_handlers, (IInspectable *)iface, NULL );
            break;
        default:
            FIXME( "Unhandled DEV_QUERY_STATE value: %d\n", action_data->Data.State );
            break;
        }
        break;
    case DevQueryResultAdd:
    {
        IDeviceInformation *info;
        if (FAILED(hr = device_information_create( action_data->Data.DeviceObject.pszObjectId, &info )))
            break;
        typed_event_handlers_notify( &watcher->added_handlers, (IInspectable *)iface, (IInspectable *)info );
        IDeviceInformation_Release( info );
        break;
    }
    default:
        FIXME( "Unhandled DEV_QUERY_RESULT_ACTION value: %d\n", action_data->Action );
        break;
    }

    IDeviceWatcher_Release( iface );
}

static HRESULT WINAPI device_watcher_Start( IDeviceWatcher *iface )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    HRESULT hr = S_OK;

    TRACE( "iface %p\n", iface );

    if (impl->aqs_all_whitespace) return E_INVALIDARG;

    EnterCriticalSection( &impl->cs );
    switch (impl->status)
    {
    case DeviceWatcherStatus_EnumerationCompleted: hr = E_ILLEGAL_METHOD_CALL; break;
    case DeviceWatcherStatus_Started: hr = E_ILLEGAL_METHOD_CALL; break;
    case DeviceWatcherStatus_Stopping: hr = E_ILLEGAL_METHOD_CALL; break;
    default: assert( FALSE ); break;
    case DeviceWatcherStatus_Aborted:
    case DeviceWatcherStatus_Created:
    case DeviceWatcherStatus_Stopped:
    {
        const struct devquery_params *query_params = impl_from_IUnknown( impl->query_params );
        const DEVPROP_FILTER_EXPRESSION *filters = NULL;
        ULONG filters_len = 0;
        IWeakReference *weak;
        HRESULT hr;

        if (query_params->expr)
        {
            filters = query_params->expr->filters;
            filters_len = query_params->expr->len;
        }

        IWeakReferenceSource_GetWeakReference( &impl->weak_reference_source.IWeakReferenceSource_iface, &weak );
        hr = DevCreateObjectQuery( DevObjectTypeDeviceInterfaceDisplay, DevQueryFlagAsyncClose, 0, NULL, filters_len, filters, device_object_query_callback,
                                   weak, &impl->query );
        if (FAILED(hr))
        {
            ERR( "Failed to create device query: %#lx\n", hr );
            IWeakReference_Release( weak );
            break;
        }
        impl->status = DeviceWatcherStatus_Started;
        break;
    }
    }
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static HRESULT WINAPI device_watcher_Stop( IDeviceWatcher *iface )
{
    struct device_watcher *impl = impl_from_IDeviceWatcher( iface );
    HRESULT hr = S_OK;

    TRACE( "iface %p\n", iface );

    EnterCriticalSection( &impl->cs );
    switch (impl->status)
    {
    case DeviceWatcherStatus_Aborted: break;
    case DeviceWatcherStatus_Created: hr = E_ILLEGAL_METHOD_CALL; break;
    case DeviceWatcherStatus_Stopped: hr = E_ILLEGAL_METHOD_CALL; break;
    case DeviceWatcherStatus_Stopping: hr = E_ILLEGAL_METHOD_CALL; break;
    default: assert( FALSE ); break;
    case DeviceWatcherStatus_EnumerationCompleted:
    case DeviceWatcherStatus_Started:
        impl->status = DeviceWatcherStatus_Stopping;
        DevCloseObjectQuery( impl->query );
        impl->query = NULL;
        break;
    }
    LeaveCriticalSection( &impl->cs );

    return hr;
}

static const struct IDeviceWatcherVtbl device_watcher_vtbl =
{
    device_watcher_QueryInterface,
    device_watcher_AddRef,
    device_watcher_Release,
    /* IInspectable methods */
    device_watcher_GetIids,
    device_watcher_GetRuntimeClassName,
    device_watcher_GetTrustLevel,
    /* IDeviceWatcher methods */
    device_watcher_add_Added,
    device_watcher_remove_Added,
    device_watcher_add_Updated,
    device_watcher_remove_Updated,
    device_watcher_add_Removed,
    device_watcher_remove_Removed,
    device_watcher_add_EnumerationCompleted,
    device_watcher_remove_EnumerationCompleted,
    device_watcher_add_Stopped,
    device_watcher_remove_Stopped,
    device_watcher_get_Status,
    device_watcher_Start,
    device_watcher_Stop,
};

static HRESULT device_watcher_create( HSTRING filter, IDeviceWatcher **out )
{
    struct device_watcher *impl;
    struct aqs_expr *expr;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IDeviceWatcher_iface.lpVtbl = &device_watcher_vtbl;
    if (FAILED(hr = weak_reference_source_init( &impl->weak_reference_source, (IUnknown *)&impl->IDeviceWatcher_iface )))
    {
        free( impl );
        return hr;
    }
    /* If the filter string is all whitespaces, we return E_INVALIDARG in IDeviceWatcher_Start, not here. */
    if (FAILED(hr = aqs_parse_query( WindowsGetStringRawBuffer( filter, NULL ), &expr, &impl->aqs_all_whitespace )) && !impl->aqs_all_whitespace)
    {
        weak_reference_strong_release( &impl->weak_reference_source );
        free( impl );
        return hr;
    }
    if (FAILED(hr = devquery_params_create( expr, &impl->query_params )))
    {
        free_aqs_expr( expr );
        weak_reference_strong_release( &impl->weak_reference_source );
        free( impl );
        return hr;
    }

    list_init( &impl->added_handlers );
    list_init( &impl->enumerated_handlers );
    list_init( &impl->stopped_handlers );

    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": device_watcher.cs");
    impl->status = DeviceWatcherStatus_Created;

    *out = &impl->IDeviceWatcher_iface;
    TRACE( "created DeviceWatcher %p\n", *out );
    return S_OK;
}

struct device_information_statics
{
    IActivationFactory IActivationFactory_iface;
    IDeviceInformationStatics IDeviceInformationStatics_iface;
    IDeviceInformationStatics2 IDeviceInformationStatics2_iface;
    LONG ref;
};

static inline struct device_information_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct device_information_statics, IActivationFactory_iface );
}

static HRESULT WINAPI activation_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct device_information_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p stub!\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IInspectable_AddRef( (*out = &impl->IActivationFactory_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDeviceInformationStatics ))
    {
        IInspectable_AddRef( (*out = &impl->IDeviceInformationStatics_iface) );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IDeviceInformationStatics2 ))
    {
        IInspectable_AddRef( (*out = &impl->IDeviceInformationStatics2_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI activation_factory_AddRef( IActivationFactory *iface )
{
    struct device_information_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI activation_factory_Release( IActivationFactory *iface )
{
    struct device_information_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI activation_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    activation_factory_QueryInterface,
    activation_factory_AddRef,
    activation_factory_Release,
    /* IInspectable methods */
    activation_factory_GetIids,
    activation_factory_GetRuntimeClassName,
    activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    activation_factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( device_statics, IDeviceInformationStatics, struct device_information_statics, IActivationFactory_iface );

static HRESULT WINAPI device_statics_CreateFromIdAsync( IDeviceInformationStatics *iface, HSTRING id,
                                                        IAsyncOperation_DeviceInformation **op )
{
    FIXME( "iface %p, id %s, op %p stub!\n", iface, debugstr_hstring(id), op );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_statics_CreateFromIdAsyncAdditionalProperties( IDeviceInformationStatics *iface, HSTRING id,
                                                                            IIterable_HSTRING *additional_properties,
                                                                            IAsyncOperation_DeviceInformation **op )
{
    FIXME( "iface %p, id %s, additional_properties %p, op %p stub!\n", iface, debugstr_hstring(id), additional_properties, op );
    return E_NOTIMPL;
}

static HRESULT find_all_async( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    static const struct vector_iids iids =
    {
        .vector = &IID_IVector_IInspectable,
        .view = &IID_IVectorView_DeviceInformation,
        .iterable = &IID_IIterable_DeviceInformation,
        .iterator = &IID_IIterator_DeviceInformation,
    };
    const DEVPROP_FILTER_EXPRESSION *filters = NULL;
    IVectorView_DeviceInformation *view;
    struct devquery_params *params;
    IVector_IInspectable *vector;
    ULONG filters_len = 0, len, i;
    const DEV_OBJECT *objects;
    HRESULT hr;

    TRACE( "invoker %p, param %p, result %p\n", invoker, param, result );

    params = impl_from_IUnknown( param );
    if (params->expr)
    {
        filters = params->expr->filters;
        filters_len = params->expr->len;
    }
    if (FAILED(hr = vector_create( &iids, (void *)&vector ))) return hr;
    if (FAILED(hr = DevGetObjects( DevObjectTypeDeviceInterfaceDisplay, DevQueryFlagNone, 0, NULL, filters_len, filters, &len, &objects )))
    {
        IVector_IInspectable_Release( vector );
        ERR("DevGetObjects failed, hr %#lx\n", hr);
        return hr;
    }
    for (i = 0; i < len && SUCCEEDED(hr); i++)
    {
        IDeviceInformation *info;
        if (SUCCEEDED(hr = device_information_create( objects[i].pszObjectId, &info )))
        {
            hr = IVector_IInspectable_Append( vector, (IInspectable *)info );
            IDeviceInformation_Release( info );
        }
    }
    DevFreeObjects( len, objects );
    if (SUCCEEDED(hr)) hr = IVector_IInspectable_GetView( vector, (void *)&view );
    IVector_IInspectable_Release( vector );
    if (FAILED(hr)) return hr;

    result->vt = VT_UNKNOWN;
    result->punkVal = (IUnknown *)view;
    return hr;
}

static HRESULT WINAPI device_statics_FindAllAsync( IDeviceInformationStatics *iface,
                                                   IAsyncOperation_DeviceInformationCollection **op )
{
    TRACE( "iface %p, op %p\n", iface, op );
    return IDeviceInformationStatics_FindAllAsyncAqsFilter( iface, NULL, op );
}

static HRESULT WINAPI device_statics_FindAllAsyncDeviceClass( IDeviceInformationStatics *iface, DeviceClass class,
                                                              IAsyncOperation_DeviceInformationCollection **op )
{
    FIXME( "iface %p, class %d, op %p stub!\n", iface, class, op );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_statics_FindAllAsyncAqsFilter( IDeviceInformationStatics *iface, HSTRING filter,
                                                            IAsyncOperation_DeviceInformationCollection **op )
{
    struct aqs_expr *expr;
    IUnknown *params;
    HRESULT hr;

    TRACE( "iface %p, aqs %p, op %p\n", iface, debugstr_hstring(filter), op );

    if (FAILED(hr = aqs_parse_query(WindowsGetStringRawBuffer( filter, NULL ), &expr, NULL ))) return hr;
    if (FAILED(hr = devquery_params_create( expr, &params )))
    {
        free_aqs_expr( expr );
        return hr;
    }
    return async_operation_inspectable_create( &IID_IAsyncOperation_DeviceInformationCollection, (IUnknown *)iface, (IUnknown *)params,
                                               find_all_async, (IAsyncOperation_IInspectable **)op );
}

static HRESULT WINAPI device_statics_FindAllAsyncAqsFilterAndAdditionalProperties( IDeviceInformationStatics *iface, HSTRING filter,
                                                                                   IIterable_HSTRING *additional_properties,
                                                                                   IAsyncOperation_DeviceInformationCollection **op )
{
    FIXME( "iface %p, aqs %p, additional_properties %p, op %p stub!\n", iface, debugstr_hstring(filter), additional_properties, op );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_statics_CreateWatcher( IDeviceInformationStatics *iface, IDeviceWatcher **watcher )
{
    TRACE( "iface %p, watcher %p\n", iface, watcher );
    return device_watcher_create( NULL, watcher );
}

static HRESULT WINAPI device_statics_CreateWatcherDeviceClass( IDeviceInformationStatics *iface, DeviceClass class, IDeviceWatcher **watcher )
{
    FIXME( "iface %p, class %d, watcher %p stub!\n", iface, class, watcher );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_statics_CreateWatcherAqsFilter( IDeviceInformationStatics *iface, HSTRING filter, IDeviceWatcher **watcher )
{
    TRACE( "iface %p, filter %s, watcher %p\n", iface, debugstr_hstring(filter), watcher );
    return device_watcher_create( filter, watcher );
}

static HRESULT WINAPI device_statics_CreateWatcherAqsFilterAndAdditionalProperties( IDeviceInformationStatics *iface, HSTRING filter,
                                                                                    IIterable_HSTRING *additional_properties, IDeviceWatcher **watcher )
{
    FIXME( "iface %p, aqs %p, additional_properties %p, watcher %p stub!\n", iface, debugstr_hstring(filter), additional_properties, watcher );
    return E_NOTIMPL;
}

static const struct IDeviceInformationStaticsVtbl device_statics_vtbl =
{
    device_statics_QueryInterface,
    device_statics_AddRef,
    device_statics_Release,
    /* IInspectable methods */
    device_statics_GetIids,
    device_statics_GetRuntimeClassName,
    device_statics_GetTrustLevel,
    /* IDeviceInformationStatics methods */
    device_statics_CreateFromIdAsync,
    device_statics_CreateFromIdAsyncAdditionalProperties,
    device_statics_FindAllAsync,
    device_statics_FindAllAsyncDeviceClass,
    device_statics_FindAllAsyncAqsFilter,
    device_statics_FindAllAsyncAqsFilterAndAdditionalProperties,
    device_statics_CreateWatcher,
    device_statics_CreateWatcherDeviceClass,
    device_statics_CreateWatcherAqsFilter,
    device_statics_CreateWatcherAqsFilterAndAdditionalProperties,
};

DEFINE_IINSPECTABLE( device_statics2, IDeviceInformationStatics2, struct device_information_statics, IActivationFactory_iface );

static HRESULT WINAPI device_statics2_GetAqsFilterFromDeviceClass( IDeviceInformationStatics2 *iface, DeviceClass device_class,
                                                                   HSTRING *filter )
{
    FIXME( "iface %p, device_class %u, filter %p stub!\n", iface, device_class, filter );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_statics2_CreateFromIdAsync( IDeviceInformationStatics2 *iface, HSTRING device_id,
                                                         IIterable_HSTRING *additional_properties, DeviceInformationKind kind,
                                                         IAsyncOperation_DeviceInformation **async_operation )
{
    FIXME( "iface %p, device_id %s, additional_properties %p, kind %u, async_operation %p stub!\n",
            iface, debugstr_hstring( device_id ), additional_properties, kind, async_operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_statics2_FindAllAsync( IDeviceInformationStatics2 *iface, HSTRING filter,
                                                    IIterable_HSTRING *additional_properties, DeviceInformationKind kind,
                                                    IAsyncOperation_DeviceInformationCollection **async_operation )
{
    FIXME( "iface %p, filter %s, additional_properties %p, kind %u, async_operation %p stub!\n",
            iface, debugstr_hstring( filter ), additional_properties, kind, async_operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI device_statics2_CreateWatcher( IDeviceInformationStatics2 *iface, HSTRING filter,
                                                     IIterable_HSTRING *additional_properties, DeviceInformationKind kind,
                                                     IDeviceWatcher **watcher )
{
    FIXME( "iface %p, filter %s, additional_properties %p, kind %u, watcher %p semi-stub!\n",
            iface, debugstr_hstring( filter ), additional_properties, kind, watcher );
    return device_watcher_create( filter, watcher );
}

static const struct IDeviceInformationStatics2Vtbl device_statics2_vtbl =
{
    device_statics2_QueryInterface,
    device_statics2_AddRef,
    device_statics2_Release,
    /* IInspectable methods */
    device_statics2_GetIids,
    device_statics2_GetRuntimeClassName,
    device_statics2_GetTrustLevel,
    /* IDeviceInformationStatics2 methods */
    device_statics2_GetAqsFilterFromDeviceClass,
    device_statics2_CreateFromIdAsync,
    device_statics2_FindAllAsync,
    device_statics2_CreateWatcher
};

static struct device_information_statics device_information_statics =
{
    {&activation_factory_vtbl},
    {&device_statics_vtbl},
    {&device_statics2_vtbl},
    1
};

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "classid %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Devices_Enumeration_DeviceInformation ))
        IActivationFactory_QueryInterface( &device_information_statics.IActivationFactory_iface,
                &IID_IActivationFactory, (void **)factory );
    else if (!wcscmp( buffer, RuntimeClass_Windows_Devices_Enumeration_DeviceAccessInformation ))
        IActivationFactory_QueryInterface( device_access_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
