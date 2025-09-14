/* WinRT Windows.Media.Playback.BackgroundMediaPlayer Implementation
 *
 * Copyright (C) 2025 Mohamad Al-Jaf
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

#include "initguid.h"
#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(playback);

static CRITICAL_SECTION media_player_cs;
static CRITICAL_SECTION_DEBUG media_player_cs_debug =
{
    0, 0, &media_player_cs,
    { &media_player_cs_debug.ProcessLocksList, &media_player_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": media_player_cs") }
};
static CRITICAL_SECTION media_player_cs = { &media_player_cs_debug, -1, 0, 0, 0, 0 };

struct background_media_player_statics
{
    IActivationFactory IActivationFactory_iface;
    IBackgroundMediaPlayerStatics IBackgroundMediaPlayerStatics_iface;
    LONG ref;

    IMediaPlayer *media_player;
};

static inline struct background_media_player_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct background_media_player_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct background_media_player_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IBackgroundMediaPlayerStatics ))
    {
        *out = &impl->IBackgroundMediaPlayerStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct background_media_player_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct background_media_player_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        EnterCriticalSection( &media_player_cs );
        if (!impl->ref && impl->media_player)
        {
            IMediaPlayer_Release( impl->media_player );
            impl->media_player = NULL;
        }
        LeaveCriticalSection( &media_player_cs );
    }

    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    /* IUnknown methods */
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

DEFINE_IINSPECTABLE( background_media_player_statics, IBackgroundMediaPlayerStatics, struct background_media_player_statics, IActivationFactory_iface )

static HRESULT get_media_player( IMediaPlayer **media_player )
{
    static const WCHAR *media_player_name = L"Windows.Media.Playback.MediaPlayer";
    IInspectable *inspectable = NULL;
    HSTRING str = NULL;
    HRESULT hr;

    if (FAILED(hr = WindowsCreateString( media_player_name, wcslen( media_player_name ), &str ))) return hr;
    if (SUCCEEDED(hr = RoActivateInstance( str, &inspectable )))
    {
        hr = IInspectable_QueryInterface( inspectable, &IID_IMediaPlayer, (void **)media_player );
        IInspectable_Release( inspectable );
    }

    WindowsDeleteString( str );
    return hr;
}

static HRESULT WINAPI background_media_player_statics_get_Current( IBackgroundMediaPlayerStatics *iface, IMediaPlayer **player )
{
    struct background_media_player_statics *impl = impl_from_IBackgroundMediaPlayerStatics( iface );
    HRESULT hr;

    TRACE( "iface %p, player %p\n", iface, player );

    EnterCriticalSection( &media_player_cs );

    if (!impl->media_player && FAILED(hr = get_media_player( &impl->media_player )))
    {
        *player = NULL;
        LeaveCriticalSection( &media_player_cs );
        return hr;
    }

    *player = impl->media_player;
    IMediaPlayer_AddRef( *player );
    LeaveCriticalSection( &media_player_cs );
    return S_OK;
}

static HRESULT WINAPI background_media_player_statics_add_MessageReceivedFromBackground( IBackgroundMediaPlayerStatics *iface,
                                                                                         IEventHandler_MediaPlayerDataReceivedEventArgs *value,
                                                                                         EventRegistrationToken *token )
{
    FIXME( "iface %p, value %p, token %p stub!\n", iface, value, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI background_media_player_statics_remove_MessageReceivedFromBackground( IBackgroundMediaPlayerStatics *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI background_media_player_statics_add_MessageReceivedFromForeground( IBackgroundMediaPlayerStatics *iface,
                                                                                         IEventHandler_MediaPlayerDataReceivedEventArgs *value,
                                                                                         EventRegistrationToken *token )
{
    FIXME( "iface %p, value %p, token %p stub!\n", iface, value, token );
    return E_NOTIMPL;
}

static HRESULT WINAPI background_media_player_statics_remove_MessageReceivedFromForeground( IBackgroundMediaPlayerStatics *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI background_media_player_statics_SendMessageToBackground( IBackgroundMediaPlayerStatics *iface, IPropertySet *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI background_media_player_statics_SendMessageToForeground( IBackgroundMediaPlayerStatics *iface, IPropertySet *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI background_media_player_statics_IsMediaPlaying( IBackgroundMediaPlayerStatics *iface, boolean *value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI background_media_player_statics_Shutdown( IBackgroundMediaPlayerStatics *iface )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}

static const struct IBackgroundMediaPlayerStaticsVtbl background_media_player_statics_vtbl =
{
    /* IUnknown methods */
    background_media_player_statics_QueryInterface,
    background_media_player_statics_AddRef,
    background_media_player_statics_Release,
    /* IInspectable methods */
    background_media_player_statics_GetIids,
    background_media_player_statics_GetRuntimeClassName,
    background_media_player_statics_GetTrustLevel,
    /* IBackgroundMediaPlayerStatics methods */
    background_media_player_statics_get_Current,
    background_media_player_statics_add_MessageReceivedFromBackground,
    background_media_player_statics_remove_MessageReceivedFromBackground,
    background_media_player_statics_add_MessageReceivedFromForeground,
    background_media_player_statics_remove_MessageReceivedFromForeground,
    background_media_player_statics_SendMessageToBackground,
    background_media_player_statics_SendMessageToForeground,
    background_media_player_statics_IsMediaPlaying,
    background_media_player_statics_Shutdown,
};

static struct background_media_player_statics background_media_player_statics =
{
    {&factory_vtbl},
    {&background_media_player_statics_vtbl},
    1,
};

static IActivationFactory *background_media_player_factory = &background_media_player_statics.IActivationFactory_iface;

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    FIXME( "clsid %s, riid %s, out %p stub!\n", debugstr_guid( clsid ), debugstr_guid( riid ), out );
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    const WCHAR *buffer = WindowsGetStringRawBuffer( classid, NULL );

    TRACE( "class %s, factory %p.\n", debugstr_hstring( classid ), factory );

    *factory = NULL;

    if (!wcscmp( buffer, RuntimeClass_Windows_Media_Playback_BackgroundMediaPlayer ))
        IActivationFactory_QueryInterface( background_media_player_factory, &IID_IActivationFactory, (void **)factory );

    if (*factory) return S_OK;
    return CLASS_E_CLASSNOTAVAILABLE;
}
