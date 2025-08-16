/*
 * Copyright 2022 Rémi Bernon for CodeWeavers
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
 *
 */

#include "stdarg.h"
#include "stddef.h"

#define COBJMACROS
#include "windef.h"
#include "winbase.h"

#include "winstring.h"
#include "roapi.h"

#include "initguid.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Foundation_Numerics
#include "windows.foundation.h"
#define WIDL_using_Windows_Devices_Power
#define WIDL_using_Windows_Gaming_Input
#include "windows.gaming.input.h"

#include "wine/debug.h"
#include "wine/list.h"

#include "joy_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(joycpl);

struct iface
{
    struct list entry;
    IGameController *iface;
};

struct device
{
    struct list entry;
    IRawGameController *device;
};

struct raw_controller_state
{
    UINT64 timestamp;
    DOUBLE axes[6];
    INT32 axes_count;
    boolean buttons[32];
    INT32 button_count;
    GameControllerSwitchPosition switches[4];
    INT32 switches_count;
};

struct device_state
{
    const GUID *iid;
    union
    {
        struct raw_controller_state raw_controller;
        GamepadReading gamepad;
    };
};

static CRITICAL_SECTION state_cs;
static CRITICAL_SECTION_DEBUG state_cs_debug =
{
    0, 0, &state_cs,
    { &state_cs_debug.ProcessLocksList, &state_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": state_cs") }
};
static CRITICAL_SECTION state_cs = { &state_cs_debug, -1, 0, 0, 0, 0 };

static struct device_state device_state;
static struct list devices = LIST_INIT( devices );
static struct list ifaces = LIST_INIT( ifaces );
static IGameController *iface_selected;

static HWND dialog_hwnd;
static HANDLE state_event;

static void set_device_state( struct device_state *state )
{
    BOOL modified;

    EnterCriticalSection( &state_cs );
    modified = memcmp( &device_state, state, sizeof(*state) );
    device_state = *state;
    LeaveCriticalSection( &state_cs );

    if (modified) SendMessageW( dialog_hwnd, WM_USER, 0, 0 );
}

static void get_device_state( struct device_state *state )
{
    EnterCriticalSection( &state_cs );
    *state = device_state;
    LeaveCriticalSection( &state_cs );
}

static void set_selected_interface( IGameController *iface )
{
    IGameController *previous;

    EnterCriticalSection( &state_cs );

    if ((previous = iface_selected)) IGameController_Release( previous );
    if ((iface_selected = iface)) IGameController_AddRef( iface );

    LeaveCriticalSection( &state_cs );
}

static IGameController *get_selected_interface(void)
{
    IGameController *iface;

    EnterCriticalSection( &state_cs );
    iface = iface_selected;
    if (iface) IGameController_AddRef( iface );
    LeaveCriticalSection( &state_cs );

    return iface;
}

static void clear_interfaces(void)
{
    struct iface *entry, *next;

    set_selected_interface( NULL );

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &ifaces, struct iface, entry )
    {
        list_remove( &entry->entry );
        IGameController_Release( entry->iface );
        free( entry );
    }
}

static void clear_devices(void)
{
    struct device *entry, *next;

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &devices, struct device, entry )
    {
        list_remove( &entry->entry );
        IRawGameController_Release( entry->device );
        free( entry );
    }
}

static DWORD WINAPI input_thread_proc( void *param )
{
    union
    {
        struct raw_controller_state raw_controller;
        GamepadReading gamepad;
    } previous = {0};

    HANDLE stop_event = param;

    while (WaitForSingleObject( stop_event, 20 ) == WAIT_TIMEOUT)
    {
        IGameController *iface;

        if (!(iface = get_selected_interface()))
            memset( &previous, 0, sizeof(previous) );
        else
        {
            IRawGameController *raw_controller;
            IGamepad *gamepad;

            if (SUCCEEDED(IGameController_QueryInterface( iface, &IID_IRawGameController, (void **)&raw_controller )))
            {
                struct device_state state = {.iid = &IID_IRawGameController};
                struct raw_controller_state *current = &state.raw_controller;
                IRawGameController_get_AxisCount( raw_controller, &current->axes_count );
                IRawGameController_get_ButtonCount( raw_controller, &current->button_count );
                IRawGameController_get_SwitchCount( raw_controller, &current->switches_count );
                IRawGameController_GetCurrentReading( raw_controller, ARRAY_SIZE(current->buttons), current->buttons,
                                                      ARRAY_SIZE(current->switches), current->switches,
                                                      ARRAY_SIZE(current->axes), current->axes, &current->timestamp );
                IRawGameController_Release( raw_controller );
                set_device_state( &state );
            }

            if (SUCCEEDED(IGameController_QueryInterface( iface, &IID_IGamepad, (void **)&gamepad )))
            {
                struct device_state state = {.iid = &IID_IGamepad};
                IGamepad_GetCurrentReading( gamepad, &state.gamepad );
                IGamepad_Release( gamepad );
                set_device_state( &state );
            }

            IGameController_Release( iface );
        }
    }

    return 0;
}

LRESULT CALLBACK test_wgi_axes_window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    TRACE( "hwnd %p, msg %#x, wparam %#Ix, lparam %#Ix\n", hwnd, msg, wparam, lparam );

    if (msg == WM_PAINT)
    {
        static const WCHAR *names[] = { L"#0", L"#1", L"#2", L"#3", L"#4", L"#5", L"#6", L"#7" };
        struct device_state state;
        UINT32 count;

        get_device_state( &state );

        count = min( state.raw_controller.axes_count, ARRAY_SIZE(state.raw_controller.axes) );
        paint_axes_view( hwnd, count, state.raw_controller.axes, names );
        return 0;
    }

    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

LRESULT CALLBACK test_wgi_povs_window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    TRACE( "hwnd %p, msg %#x, wparam %#Ix, lparam %#Ix\n", hwnd, msg, wparam, lparam );

    if (msg == WM_PAINT)
    {
        struct device_state state;
        UINT32 count, povs[ARRAY_SIZE(state.raw_controller.switches)];

        get_device_state( &state );

        for (int i = 0; i < ARRAY_SIZE(state.raw_controller.switches); i++)
        {
            if (i >= state.raw_controller.switches_count) povs[i] = -1;
            else switch (state.raw_controller.switches[i])
            {
            case GameControllerSwitchPosition_Center: povs[i] = -1; break;
            case GameControllerSwitchPosition_Up: povs[i] = 0; break;
            case GameControllerSwitchPosition_UpRight: povs[i] = 4500; break;
            case GameControllerSwitchPosition_Right: povs[i] = 9000; break;
            case GameControllerSwitchPosition_DownRight: povs[i] = 13500; break;
            case GameControllerSwitchPosition_Down: povs[i] = 18000; break;
            case GameControllerSwitchPosition_DownLeft: povs[i] = 22500; break;
            case GameControllerSwitchPosition_Left: povs[i] = 27000; break;
            case GameControllerSwitchPosition_UpLeft: povs[i] = 31500; break;
            }
        }

        count = min( state.raw_controller.switches_count, ARRAY_SIZE(povs) );
        paint_povs_view( hwnd, count, povs );
        return 0;
    }

    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

LRESULT CALLBACK test_wgi_buttons_window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    TRACE( "hwnd %p, msg %#x, wparam %#Ix, lparam %#Ix\n", hwnd, msg, wparam, lparam );

    if (msg == WM_PAINT)
    {
        struct device_state state;
        UINT32 count;

        get_device_state( &state );

        count = min( state.raw_controller.button_count, ARRAY_SIZE(state.raw_controller.buttons) );
        paint_buttons_view( hwnd, count, state.raw_controller.buttons );
        return 0;
    }

    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

LRESULT CALLBACK test_wgi_gamepad_window_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    TRACE( "hwnd %p, msg %#x, wparam %#Ix, lparam %#Ix\n", hwnd, msg, wparam, lparam );

    if (msg == WM_PAINT)
    {
        struct device_state state;
        XINPUT_STATE xstate = {0};

        get_device_state( &state );

        xstate.Gamepad.sThumbLX = state.gamepad.LeftThumbstickX * 0x7fff;
        xstate.Gamepad.sThumbLY = state.gamepad.LeftThumbstickY * 0x7fff;
        if (state.gamepad.Buttons & GamepadButtons_LeftThumbstick) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;

        xstate.Gamepad.sThumbRX = state.gamepad.RightThumbstickX * 0x7fff;
        xstate.Gamepad.sThumbRY = state.gamepad.RightThumbstickY * 0x7fff;
        if (state.gamepad.Buttons & GamepadButtons_RightThumbstick) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

        xstate.Gamepad.bLeftTrigger = state.gamepad.LeftTrigger * 0xff;
        xstate.Gamepad.bRightTrigger = state.gamepad.RightTrigger * 0xff;

        if (state.gamepad.Buttons & GamepadButtons_DPadUp) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
        if (state.gamepad.Buttons & GamepadButtons_LeftShoulder) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
        if (state.gamepad.Buttons & GamepadButtons_RightShoulder) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        if (state.gamepad.Buttons & GamepadButtons_Y) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_Y;

        if (state.gamepad.Buttons & GamepadButtons_DPadLeft) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (state.gamepad.Buttons & GamepadButtons_DPadRight) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        if (state.gamepad.Buttons & GamepadButtons_X) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_X;
        if (state.gamepad.Buttons & GamepadButtons_B) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_B;

        if (state.gamepad.Buttons & GamepadButtons_DPadDown) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (state.gamepad.Buttons & GamepadButtons_View) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;
        if (state.gamepad.Buttons & GamepadButtons_Menu) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_START;
        if (state.gamepad.Buttons & GamepadButtons_A) xstate.Gamepad.wButtons |= XINPUT_GAMEPAD_A;

        paint_gamepad_view( hwnd, &xstate );
        return 0;
    }

    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void handle_wgi_interface_change( HWND hwnd )
{
    IGameController *iface;
    struct list *entry;
    int i;

    set_selected_interface( NULL );

    i = SendDlgItemMessageW( hwnd, IDC_WGI_INTERFACE, CB_GETCURSEL, 0, 0 );
    if (i < 0) return;

    entry = list_head( &ifaces );
    while (i-- && entry) entry = list_next( &ifaces, entry );
    if (!entry) return;

    iface = LIST_ENTRY( entry, struct iface, entry )->iface;
    set_selected_interface( iface );
}

static HRESULT check_gamepad_interface( IRawGameController *device, IGameController **iface )
{
    const WCHAR *class_name = RuntimeClass_Windows_Gaming_Input_Gamepad;
    IGameController *controller;
    IGamepadStatics2 *statics;
    IGamepad *gamepad = NULL;
    HSTRING str;

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    RoGetActivationFactory( str, &IID_IGamepadStatics2, (void **)&statics );
    WindowsDeleteString( str );
    if (!statics) return E_NOINTERFACE;

    if (SUCCEEDED(IRawGameController_QueryInterface( device, &IID_IGameController, (void **)&controller )))
    {
        IGamepadStatics2_FromGameController( statics, controller, &gamepad );
        IGameController_Release( controller );
    }

    IGamepadStatics2_Release( statics );
    if (!gamepad) return E_NOINTERFACE;

    IGamepad_QueryInterface( gamepad, &IID_IGameController, (void **)iface );
    IGamepad_Release( gamepad );
    return S_OK;
}

static HRESULT check_racing_wheel_interface( IRawGameController *device, IGameController **iface )
{
    const WCHAR *class_name = RuntimeClass_Windows_Gaming_Input_RacingWheel;
    IRacingWheelStatics2 *statics;
    IGameController *controller;
    IRacingWheel *wheel = NULL;
    HSTRING str;

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    RoGetActivationFactory( str, &IID_IRacingWheelStatics2, (void **)&statics );
    WindowsDeleteString( str );
    if (!statics) return E_NOINTERFACE;

    if (SUCCEEDED(IRawGameController_QueryInterface( device, &IID_IGameController, (void **)&controller )))
    {
        IRacingWheelStatics2_FromGameController( statics, controller, &wheel );
        IGameController_Release( controller );
    }

    IRacingWheelStatics2_Release( statics );
    if (!wheel) return E_NOINTERFACE;

    IRacingWheel_QueryInterface( wheel, &IID_IGameController, (void **)iface );
    IRacingWheel_Release( wheel );
    return S_OK;
}

static void update_wgi_interface( HWND hwnd, IRawGameController *device )
{
    IGameController *controller;
    struct iface *iface;

    clear_interfaces();

    if (SUCCEEDED(IRawGameController_QueryInterface( device, &IID_IGameController,
                                                     (void **)&controller )))
    {
        if (!(iface = calloc( 1, sizeof(*iface)))) goto done;
        list_add_tail( &ifaces, &iface->entry );
        iface->iface = controller;
    }
    if (SUCCEEDED(check_gamepad_interface( device, &controller )))
    {
        if (!(iface = calloc( 1, sizeof(*iface)))) goto done;
        list_add_tail( &ifaces, &iface->entry );
        iface->iface = controller;
    }
    if (SUCCEEDED(check_racing_wheel_interface( device, &controller )))
    {
        if (!(iface = calloc( 1, sizeof(*iface)))) goto done;
        list_add_tail( &ifaces, &iface->entry );
        iface->iface = controller;
    }
    controller = NULL;

    SendDlgItemMessageW( hwnd, IDC_WGI_INTERFACE, CB_RESETCONTENT, 0, 0 );

    LIST_FOR_EACH_ENTRY( iface, &ifaces, struct iface, entry )
    {
        HSTRING name;

        if (FAILED(IGameController_GetRuntimeClassName( iface->iface, &name ))) continue;

        SendDlgItemMessageW( hwnd, IDC_WGI_INTERFACE, CB_ADDSTRING, 0,
                             (LPARAM)(wcsrchr( WindowsGetStringRawBuffer( name, NULL ), '.' ) + 1) );

        WindowsDeleteString( name );
    }

done:
    if (controller) IGameController_Release( controller );

    SendDlgItemMessageW( hwnd, IDC_WGI_INTERFACE, CB_SETCURSEL, 0, 0 );
}

static void handle_wgi_devices_change( HWND hwnd )
{
    IRawGameController *device;
    struct list *entry;
    int i;

    i = SendDlgItemMessageW( hwnd, IDC_WGI_DEVICES, CB_GETCURSEL, 0, 0 );
    if (i < 0) return;

    entry = list_head( &devices );
    while (i-- && entry) entry = list_next( &devices, entry );
    if (!entry) return;

    device = LIST_ENTRY( entry, struct device, entry )->device;
    update_wgi_interface( hwnd, device );
}

static void update_wgi_devices( HWND hwnd )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Gaming_Input_RawGameController;
    IVectorView_RawGameController *controllers;
    IRawGameControllerStatics *statics;
    struct device *entry;
    HSTRING str;
    UINT size;

    clear_devices();

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    RoGetActivationFactory( str, &IID_IRawGameControllerStatics, (void **)&statics );
    WindowsDeleteString( str );

    IRawGameControllerStatics_get_RawGameControllers( statics, &controllers );
    IVectorView_RawGameController_get_Size( controllers, &size );

    while (size--)
    {
        struct device *entry;
        if (!(entry = calloc( 1, sizeof(struct device) ))) break;
        IVectorView_RawGameController_GetAt( controllers, size, &entry->device );
        list_add_tail( &devices, &entry->entry );
    }

    IVectorView_RawGameController_Release( controllers );
    IRawGameControllerStatics_Release( statics );

    SendDlgItemMessageW( hwnd, IDC_WGI_DEVICES, CB_RESETCONTENT, 0, 0 );

    LIST_FOR_EACH_ENTRY( entry, &devices, struct device, entry )
    {
        IRawGameController2 *controller2;

        if (SUCCEEDED(IRawGameController_QueryInterface( entry->device, &IID_IRawGameController2,
                                                         (void **)&controller2 )))
        {
            HSTRING name;

            IRawGameController2_get_DisplayName( controller2, &name );
            SendDlgItemMessageW( hwnd, IDC_WGI_DEVICES, CB_ADDSTRING, 0,
                                 (LPARAM)WindowsGetStringRawBuffer( name, NULL ) );
            WindowsDeleteString( name );

            IRawGameController2_Release( controller2 );
        }
        else
        {
            UINT16 vid = -1, pid = -1;
            WCHAR buffer[256];

            IRawGameController_get_HardwareVendorId( entry->device, &vid );
            IRawGameController_get_HardwareProductId( entry->device, &pid );

            swprintf( buffer, ARRAY_SIZE(buffer), L"%04x:%04x", vid, pid );
            SendDlgItemMessageW( hwnd, IDC_WGI_DEVICES, CB_ADDSTRING, 0, (LPARAM)buffer );
        }
    }
}

static void update_device_views( HWND hwnd )
{
    HWND gamepad, rumble, axes, povs, buttons;
    struct device_state state;

    get_device_state( &state );

    gamepad = GetDlgItem( hwnd, IDC_WGI_GAMEPAD );
    rumble = GetDlgItem( hwnd, IDC_WGI_RUMBLE );
    axes = GetDlgItem( hwnd, IDC_WGI_AXES );
    povs = GetDlgItem( hwnd, IDC_WGI_POVS );
    buttons = GetDlgItem( hwnd, IDC_WGI_BUTTONS );

    if (!IsEqualGUID( state.iid, &IID_IRawGameController ))
    {
        ShowWindow( axes, SW_HIDE );
        ShowWindow( povs, SW_HIDE );
        ShowWindow( buttons, SW_HIDE );
    }
    else
    {
        InvalidateRect( axes, NULL, TRUE );
        InvalidateRect( povs, NULL, TRUE );
        InvalidateRect( buttons, NULL, TRUE );
        ShowWindow( axes, SW_SHOW );
        ShowWindow( povs, SW_SHOW );
        ShowWindow( buttons, SW_SHOW );
    }

    if (!IsEqualGUID( state.iid, &IID_IGamepad ))
    {
        ShowWindow( gamepad, SW_HIDE );
        ShowWindow( rumble, SW_HIDE );
    }
    else
    {
        InvalidateRect( gamepad, NULL, TRUE );
        InvalidateRect( rumble, NULL, TRUE );
        ShowWindow( gamepad, SW_SHOW );
        ShowWindow( rumble, SW_SHOW );
    }
}

static void create_device_views( HWND hwnd )
{
    HINSTANCE instance = (HINSTANCE)GetWindowLongPtrW( hwnd, GWLP_HINSTANCE );
    HWND gamepad, rumble, axes, povs, buttons;
    LONG margin;
    RECT rect;

    gamepad = GetDlgItem( hwnd, IDC_WGI_GAMEPAD );
    rumble = GetDlgItem( hwnd, IDC_WGI_RUMBLE );
    axes = GetDlgItem( hwnd, IDC_WGI_AXES );
    povs = GetDlgItem( hwnd, IDC_WGI_POVS );
    buttons = GetDlgItem( hwnd, IDC_WGI_BUTTONS );

    ShowWindow( gamepad, SW_HIDE );
    ShowWindow( rumble, SW_HIDE );
    ShowWindow( axes, SW_HIDE );
    ShowWindow( povs, SW_HIDE );
    ShowWindow( buttons, SW_HIDE );

    GetClientRect( axes, &rect );
    rect.top += 10;

    margin = (rect.bottom - rect.top) * 10 / 100;
    InflateRect( &rect, -margin, -margin );

    CreateWindowW( L"JoyCplWGIAxes", NULL, WS_CHILD | WS_VISIBLE, rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top, axes, NULL, NULL, instance );

    GetClientRect( povs, &rect );
    rect.top += 10;

    margin = (rect.bottom - rect.top) * 10 / 100;
    InflateRect( &rect, -margin, -margin );

    CreateWindowW( L"JoyCplWGIPOVs", NULL, WS_CHILD | WS_VISIBLE, rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top, povs, NULL, NULL, instance );

    GetClientRect( buttons, &rect );
    rect.top += 10;

    margin = (rect.bottom - rect.top) * 5 / 100;
    InflateRect( &rect, -margin, -margin );

    CreateWindowW( L"JoyCplWGIButtons", NULL, WS_CHILD | WS_VISIBLE, rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top, buttons, NULL, NULL, instance );

    GetClientRect( gamepad, &rect );
    rect.top += 10;

    margin = (rect.bottom - rect.top) * 15 / 100;
    InflateRect( &rect, -margin, -margin );

    CreateWindowW( L"JoyCplWGIGamepad", NULL, WS_CHILD | WS_VISIBLE, rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top, gamepad, NULL, NULL, instance );
}

extern INT_PTR CALLBACK test_wgi_dialog_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    static HANDLE thread, thread_stop;

    TRACE( "hwnd %p, msg %#x, wparam %#Ix, lparam %#Ix\n", hwnd, msg, wparam, lparam );

    switch (msg)
    {
    case WM_INITDIALOG:
        create_device_views( hwnd );
        return TRUE;

    case WM_COMMAND:
        switch (wparam)
        {
        case MAKEWPARAM( IDC_WGI_DEVICES, CBN_SELCHANGE ):
            handle_wgi_devices_change( hwnd );
            handle_wgi_interface_change( hwnd );
            break;

        case MAKEWPARAM( IDC_WGI_INTERFACE, CBN_SELCHANGE ):
            handle_wgi_interface_change( hwnd );
            break;
        }
        return TRUE;


    case WM_NOTIFY:
        switch (((NMHDR *)lparam)->code)
        {
        case PSN_SETACTIVE:
            RoInitialize( RO_INIT_MULTITHREADED );

            dialog_hwnd = hwnd;
            state_event = CreateEventW( NULL, FALSE, FALSE, NULL );
            thread_stop = CreateEventW( NULL, FALSE, FALSE, NULL );

            update_wgi_devices( hwnd );

            SendDlgItemMessageW( hwnd, IDC_WGI_DEVICES, CB_SETCURSEL, 0, 0 );
            handle_wgi_devices_change( hwnd );

            SendDlgItemMessageW( hwnd, IDC_WGI_INTERFACE, CB_SETCURSEL, 0, 0 );
            handle_wgi_interface_change( hwnd );

            thread_stop = CreateEventW( NULL, FALSE, FALSE, NULL );
            thread = CreateThread( NULL, 0, input_thread_proc, (void *)thread_stop, 0, NULL );
            break;

        case PSN_RESET:
        case PSN_KILLACTIVE:
            SetEvent( thread_stop );
            MsgWaitForMultipleObjects( 1, &thread, FALSE, INFINITE, 0 );
            CloseHandle( state_event );
            CloseHandle( thread_stop );
            CloseHandle( thread );

            clear_devices();

            RoUninitialize();
            break;
        }
        return TRUE;

    case WM_USER:
        update_device_views( hwnd );
        return TRUE;
    }

    return FALSE;
}
