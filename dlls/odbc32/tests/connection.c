/*
 * Copyright 2018 Alistair Leslie-Hughes
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

#include <wine/test.h>
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "sqlext.h"
#include "sqlucode.h"
#include "odbcinst.h"

static void test_SQLAllocEnv(void)
{
    SQLRETURN ret;
    SQLHENV sqlenv, sqlenv2;

    ret = SQLAllocEnv(NULL);
    ok(ret == SQL_ERROR, "got %d\n", ret);

    ret = SQLAllocEnv(&sqlenv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);

    ret = SQLAllocEnv(&sqlenv2);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
    ok(sqlenv != sqlenv2, "got %d\n", ret);

    ret = SQLFreeEnv(sqlenv2);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);

    ret = SQLFreeEnv(sqlenv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);

    ret = SQLFreeEnv(sqlenv);
    todo_wine ok(ret == SQL_INVALID_HANDLE, "got %d\n", ret);

    ret = SQLFreeEnv(SQL_NULL_HENV);
    todo_wine ok(ret == SQL_INVALID_HANDLE, "got %d\n", ret);
}

void test_SQLGetEnvAttr(void)
{
    SQLRETURN ret;
    SQLHENV sqlenv;
    SQLINTEGER value, length;

    ret = SQLAllocEnv(&sqlenv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);

    value = 5;
    length = 12;
    ret = SQLGetEnvAttr(SQL_NULL_HENV, SQL_ATTR_CONNECTION_POOLING, &value, sizeof(SQLINTEGER), &length);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
    ok(value == 0, "got %d\n", value);
    todo_wine ok(length == 12, "got %d\n", length);

    value = 5;
    length = 13;
    ret = SQLGetEnvAttr(SQL_NULL_HENV, SQL_ATTR_CONNECTION_POOLING, &value, 0, &length);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
    ok(value == 0, "got %d\n", value);
    todo_wine ok(length == 13, "got %d\n", length);

    value = 5;
    length = 12;
    ret = SQLGetEnvAttr(sqlenv, SQL_ATTR_CONNECTION_POOLING, &value, sizeof(SQLINTEGER), &length);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
    ok(value == 0, "got %d\n", value);
    ok(length == 12, "got %d\n", length);

    value = 5;
    length = 12;
    ret = SQLGetEnvAttr(sqlenv, SQL_ATTR_CONNECTION_POOLING, &value, 2, &length);
    todo_wine ok(ret == SQL_SUCCESS, "got %d\n", ret);
    todo_wine ok(value == 0, "got %d\n", value);
    ok(length == 12, "got %d\n", length);

    ret = SQLFreeEnv(sqlenv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
}

static void test_SQLDriver(void)
{
    SQLHENV henv = SQL_NULL_HENV;
    SQLRETURN ret;
    SQLCHAR driver[256];
    SQLCHAR attr[256];
    SQLSMALLINT driver_ret;
    SQLSMALLINT attr_ret;
    SQLUSMALLINT direction;

    ret = SQLAllocEnv(&henv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
    ok(henv != SQL_NULL_HENV, "NULL handle\n");

    direction = SQL_FETCH_FIRST;

    while(SQL_SUCCEEDED(ret = SQLDrivers(henv, direction, driver, sizeof(driver),
            &driver_ret, attr, sizeof(attr), &attr_ret)))
    {
        direction = SQL_FETCH_NEXT;

        trace("%s - %s\n", driver, attr);
    }
    todo_wine ok(ret == SQL_NO_DATA, "got %d\n", ret);

    ret = SQLFreeEnv(henv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
}

static void test_SQLGetDiagRec(void)
{
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC connection;
    SQLRETURN ret;
    WCHAR version[11];
    WCHAR       SqlState[6], Msg[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER    NativeError;
    SQLSMALLINT   MsgLen;

    ret = SQLAllocEnv(&henv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
    ok(henv != SQL_NULL_HENV, "NULL handle\n");

    ret = SQLAllocConnect(henv, &connection);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);

    ret = SQLGetInfoW(connection, SQL_ODBC_VER, version, 22, NULL);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
    trace("ODBC_VER=%s\n", wine_dbgstr_w(version));

    ret = SQLFreeConnect(connection);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);

    NativeError = 88;
    ret = SQLGetDiagRecW( SQL_HANDLE_ENV, henv, 1, SqlState, &NativeError, Msg, sizeof(Msg), &MsgLen);
    todo_wine ok(ret == SQL_NO_DATA, "got %d\n", ret);
    ok(NativeError == 88, "got %d\n", NativeError);

    ret = SQLFreeEnv(henv);
    ok(ret == SQL_SUCCESS, "got %d\n", ret);
}

START_TEST(connection)
{
    test_SQLAllocEnv();
    test_SQLGetEnvAttr();
    test_SQLDriver();
    test_SQLGetDiagRec();
}
