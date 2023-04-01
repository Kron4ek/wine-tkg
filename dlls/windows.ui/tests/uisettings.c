/*
 * Copyright (C) 2023 Mohamad Al-Jaf
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
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_UI
#include "windows.ui.h"
#define WIDL_using_Windows_UI_ViewManagement
#include "windows.ui.viewmanagement.h"

#include "wine/test.h"

static const WCHAR *subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
static const WCHAR *name = L"AppsUseLightTheme";
static const HKEY root = HKEY_CURRENT_USER;

#define check_interface( obj, iid, exp ) check_interface_( __LINE__, obj, iid, exp )
static void check_interface_( unsigned int line, void *obj, const IID *iid, BOOL supported )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr, expected_hr;

    expected_hr = supported ? S_OK : E_NOINTERFACE;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_( __FILE__, line )( hr == expected_hr, "Got hr %#lx, expected %#lx.\n", hr, expected_hr );
    if (SUCCEEDED(hr))
        IUnknown_Release( unk );
}

static DWORD get_app_theme(void)
{
    DWORD ret = 0, len = sizeof(ret), type;
    HKEY hkey;

    if (RegOpenKeyExW( root, subkey, 0, KEY_QUERY_VALUE, &hkey )) return 1;
    if (RegQueryValueExW( hkey, name, NULL, &type, (BYTE *)&ret, &len ) || type != REG_DWORD) ret = 1;
    RegCloseKey( hkey );
    return ret;
}

static DWORD set_app_theme( DWORD mode )
{
    DWORD ret = 1, len = sizeof(ret);
    HKEY hkey;

    if (RegOpenKeyExW( root, subkey, 0, KEY_SET_VALUE, &hkey )) return 0;
    if (RegSetValueExW( hkey, name, 0, REG_DWORD, (const BYTE *)&mode, len )) ret = 0;
    RegCloseKey( hkey );
    return ret;
}

static void reset_color( Color *value )
{
    value->A = 1;
    value->R = 1;
    value->G = 1;
    value->B = 1;
}

static void test_UISettings(void)
{
    static const WCHAR *uisettings_name = L"Windows.UI.ViewManagement.UISettings";
    IActivationFactory *factory;
    IUISettings3 *uisettings3;
    IInspectable *inspectable;
    DWORD default_theme;
    UIColorType type;
    Color value;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( uisettings_name, wcslen( uisettings_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( uisettings_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, FALSE );
    check_interface( factory, &IID_IUISettings3, FALSE );

    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "Got unexpected hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IUISettings3, (void **)&uisettings3 );
    ok( hr == S_OK || broken( hr == E_NOINTERFACE ), "Got unexpected hr %#lx.\n", hr );
    if (FAILED(hr))
    {
        win_skip( "IUISettings3 not supported.\n" );
        goto skip_uisettings3;
    }

    check_interface( inspectable, &IID_IAgileObject, TRUE );

    default_theme = get_app_theme();

    /* Light Theme */
    if (!set_app_theme( 1 )) goto done;

    reset_color( &value );
    type = UIColorType_Foreground;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 0 && value.G == 0 && value.B == 0,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

    reset_color( &value );
    type = UIColorType_Background;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 255 && value.G == 255 && value.B == 255,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

    /* Dark Theme */
    if (!set_app_theme( 0 )) goto done;

    reset_color( &value );
    type = UIColorType_Foreground;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 255 && value.G == 255 && value.B == 255,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

    reset_color( &value );
    type = UIColorType_Background;
    hr = IUISettings3_GetColorValue( uisettings3, type, &value );
    ok( hr == S_OK, "GetColorValue returned %#lx\n", hr );
    ok( value.A == 255 && value.R == 0 && value.G == 0 && value.B == 0,
        "got unexpected value.A == %d value.R == %d value.G == %d value.B == %d\n", value.A, value.R, value.G, value.B );

done:
    set_app_theme( default_theme );
    IUISettings3_Release( uisettings3 );

skip_uisettings3:
    IInspectable_Release( inspectable );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

START_TEST(uisettings)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_UISettings();

    RoUninitialize();
}
