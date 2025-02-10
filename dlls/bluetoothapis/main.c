/*
 * Bluetooth APIs
 *
 * Copyright 2016 Austin English
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

#include <stdarg.h>
#include <winternl.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winreg.h>
#include <winnls.h>

#include "bthsdpdef.h"
#include "bluetoothapis.h"
#include "setupapi.h"
#include "winioctl.h"

#include "initguid.h"
#include "bthdef.h"
#include "bthioctl.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(bluetoothapis);

struct bluetooth_find_radio_handle
{
    HDEVINFO devinfo;
    DWORD idx;
};

/*********************************************************************
 *  BluetoothFindFirstDevice
 */
HBLUETOOTH_DEVICE_FIND WINAPI BluetoothFindFirstDevice(BLUETOOTH_DEVICE_SEARCH_PARAMS *params,
                                                       BLUETOOTH_DEVICE_INFO *info)
{
    FIXME("(%p %p): stub!\n", params, info);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

/*********************************************************************
 *  BluetoothFindFirstRadio
 */
HBLUETOOTH_RADIO_FIND WINAPI BluetoothFindFirstRadio( BLUETOOTH_FIND_RADIO_PARAMS *params, HANDLE *radio )
{
    struct bluetooth_find_radio_handle *find;
    HANDLE device_ret;
    DWORD err;

    TRACE( "(%p, %p)\n", params, radio );

    if (!params)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return NULL;
    }
    if (params->dwSize != sizeof( *params ))
    {
        SetLastError( ERROR_REVISION_MISMATCH );
        return NULL;
    }
    if (!(find = calloc( 1, sizeof( *find ) )))
    {
        SetLastError( ERROR_OUTOFMEMORY );
        return NULL;
    }

    find->devinfo = SetupDiGetClassDevsW( &GUID_BTHPORT_DEVICE_INTERFACE, NULL, NULL,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE );
    if (find->devinfo == INVALID_HANDLE_VALUE)
    {
        free( find );
        return NULL;
    }

    if (BluetoothFindNextRadio( find, &device_ret ))
    {
        *radio = device_ret;
        return find;
    }

    err = GetLastError();
    BluetoothFindRadioClose( find );
    SetLastError( err );
    return NULL;
}

/*********************************************************************
 *  BluetoothFindRadioClose
 */
BOOL WINAPI BluetoothFindRadioClose( HBLUETOOTH_RADIO_FIND find_handle )
{
    struct bluetooth_find_radio_handle *find = find_handle;

    TRACE( "(%p)\n", find_handle );

    if (!find_handle)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }

    SetupDiDestroyDeviceInfoList( find->devinfo );
    free( find );
    SetLastError( ERROR_SUCCESS );
    return TRUE;
}

/*********************************************************************
 *  BluetoothFindDeviceClose
 */
BOOL WINAPI BluetoothFindDeviceClose(HBLUETOOTH_DEVICE_FIND find)
{
    FIXME("(%p): stub!\n", find);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*********************************************************************
 *  BluetoothFindNextRadio
 */
BOOL WINAPI BluetoothFindNextRadio( HBLUETOOTH_RADIO_FIND find_handle, HANDLE *radio )
{
    char buffer[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + MAX_PATH * sizeof( WCHAR )];
    struct bluetooth_find_radio_handle *find = find_handle;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *iface_detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W *)buffer;
    SP_DEVICE_INTERFACE_DATA iface_data;
    HANDLE device_ret;
    BOOL found;

    TRACE( "(%p, %p)\n", find_handle, radio );

    if (!find_handle)
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }

    iface_detail->cbSize = sizeof( *iface_detail );
    iface_data.cbSize = sizeof( iface_data );
    found = FALSE;
    while (SetupDiEnumDeviceInterfaces( find->devinfo, NULL, &GUID_BTHPORT_DEVICE_INTERFACE, find->idx++,
                                        &iface_data ))
    {
        if (!SetupDiGetDeviceInterfaceDetailW( find->devinfo, &iface_data, iface_detail, sizeof( buffer ), NULL,
                                               NULL ))
            continue;
        device_ret = CreateFileW( iface_detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL );
        if (device_ret != INVALID_HANDLE_VALUE)
        {
            found = TRUE;
            break;
        }
    }

    if (found)
        *radio = device_ret;

    return found;
}

/*********************************************************************
 *  BluetoothGetRadioInfo
 */
DWORD WINAPI BluetoothGetRadioInfo( HANDLE radio, PBLUETOOTH_RADIO_INFO info )
{
    BOOL ret;
    DWORD bytes = 0;
    BTH_LOCAL_RADIO_INFO radio_info = {0};

    TRACE( "(%p, %p)\n", radio, info );

    if (!radio || !info)
        return ERROR_INVALID_PARAMETER;
    if (info->dwSize != sizeof( *info ))
        return ERROR_REVISION_MISMATCH;

    ret = DeviceIoControl( radio, IOCTL_BTH_GET_LOCAL_INFO, NULL, 0, &radio_info, sizeof( radio_info ), &bytes, NULL );
    if (!ret)
        return GetLastError();

    if (radio_info.localInfo.flags & BDIF_ADDRESS)
        info->address.ullLong = RtlUlonglongByteSwap( radio_info.localInfo.address );
    if (radio_info.localInfo.flags & BDIF_COD)
        info->ulClassofDevice = radio_info.localInfo.classOfDevice;
    if (radio_info.localInfo.flags & BDIF_NAME)
        MultiByteToWideChar( CP_ACP, 0, radio_info.localInfo.name, -1, info->szName, ARRAY_SIZE( info->szName ) );

    info->lmpSubversion = radio_info.radioInfo.lmpSubversion;
    info->manufacturer = radio_info.radioInfo.mfg;

    return ERROR_SUCCESS;
}

#define LOCAL_RADIO_DISCOVERABLE 0x0001
#define LOCAL_RADIO_CONNECTABLE  0x0002

/*********************************************************************
 *  BluetoothIsConnectable
 */
BOOL WINAPI BluetoothIsConnectable( HANDLE radio )
{
    TRACE( "(%p)\n", radio );

    if (!radio)
    {
        BLUETOOTH_FIND_RADIO_PARAMS params = {.dwSize = sizeof( params )};
        HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio( &params, &radio );

        if (!find)
            return FALSE;
        for (;;)
        {
            if (BluetoothIsConnectable( radio ))
            {
                CloseHandle( radio );
                BluetoothFindRadioClose( find );
                return TRUE;
            }

            CloseHandle( radio );
            if (!BluetoothFindNextRadio( find, &radio ))
            {
                BluetoothFindRadioClose( find );
                return FALSE;
            }
        }
    }
    else
    {
        BTH_LOCAL_RADIO_INFO radio_info = {0};
        DWORD bytes;
        DWORD ret;

        ret = DeviceIoControl( radio, IOCTL_BTH_GET_LOCAL_INFO, NULL, 0, &radio_info, sizeof( radio_info ), &bytes,
                               NULL );
        if (!ret)
        {
            ERR( "DeviceIoControl failed: %#lx\n", GetLastError() );
            return FALSE;
        }
        return !!(radio_info.flags & LOCAL_RADIO_CONNECTABLE);
    }
}

/*********************************************************************
 *  BluetoothIsDiscoverable
 */
BOOL WINAPI BluetoothIsDiscoverable( HANDLE radio )
{
    TRACE( "(%p)\n", radio );

    if (!radio)
    {
        BLUETOOTH_FIND_RADIO_PARAMS params = {.dwSize = sizeof( params )};
        HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio( &params, &radio );

        if (!find)
            return FALSE;
        for (;;)
        {
            if (BluetoothIsDiscoverable( radio ))
            {
                CloseHandle( radio );
                BluetoothFindRadioClose( find );
                return TRUE;
            }

            CloseHandle( radio );
            if (!BluetoothFindNextRadio( find, &radio ))
            {
                BluetoothFindRadioClose( find );
                return FALSE;
            }
        }
    }
    else
    {
        BTH_LOCAL_RADIO_INFO radio_info = {0};
        DWORD bytes;
        DWORD ret;

        ret = DeviceIoControl( radio, IOCTL_BTH_GET_LOCAL_INFO, NULL, 0, &radio_info, sizeof( radio_info ), &bytes,
                               NULL );
        if (!ret)
        {
            ERR( "DeviceIoControl failed: %#lx\n", GetLastError() );
            return FALSE;
        }
        return !!(radio_info.flags & LOCAL_RADIO_DISCOVERABLE);
    }
}

/*********************************************************************
 *  BluetoothFindNextDevice
 */
BOOL WINAPI BluetoothFindNextDevice(HBLUETOOTH_DEVICE_FIND find, BLUETOOTH_DEVICE_INFO *info)
{
    FIXME("(%p, %p): stub!\n", find, info);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

/*********************************************************************
 *  BluetoothRegisterForAuthenticationEx
 */
DWORD WINAPI BluetoothRegisterForAuthenticationEx(const BLUETOOTH_DEVICE_INFO *info, HBLUETOOTH_AUTHENTICATION_REGISTRATION *out,
                                                  PFN_AUTHENTICATION_CALLBACK_EX callback, void *param)
{
    FIXME("(%p, %p, %p, %p): stub!\n", info, out, callback, param);
    return ERROR_CALL_NOT_IMPLEMENTED;
}

/*********************************************************************
 *  BluetoothUnregisterAuthentication
 */
BOOL WINAPI BluetoothUnregisterAuthentication(HBLUETOOTH_AUTHENTICATION_REGISTRATION handle)
{
    FIXME("(%p): stub!\n", handle);
    if (!handle) SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
