/*
 * Copyright 2024 Hans Leidekker for CodeWeavers
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

#include "windef.h"
#include "winbase.h"
#include "sql.h"
#include "sqlext.h"

#include <wine/test.h>

static void test_SQLAllocHandle( void )
{
    SQLHANDLE handle;
    SQLHENV env, env2;
    SQLHDBC con;
    SQLRETURN ret;

    handle = (void *)0xdeadbeef;
    ret = SQLAllocHandle( SQL_HANDLE_ENV, SQL_NULL_HANDLE, &handle );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( handle != (void *)0xdeadbeef, "handle not set\n" );
    ret = SQLFreeHandle( SQL_HANDLE_ENV, handle );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ret = SQLFreeHandle( SQL_HANDLE_ENV, 0 );
    ok( ret == SQL_INVALID_HANDLE, "got %d\n", ret );

    env = (void *)0xdeadbeef;
    ret = SQLAllocEnv( &env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( env != (void *)0xdeadbeef, "env not set\n" );

    env2 = (void *)0xdeadbeef;
    ret = SQLAllocEnv( &env2 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( env2 != (void *)0xdeadbeef, "env2 not set\n" );
    ok( env2 != env, "environment is the same\n" );

    con = (void *)0xdeadbeef;
    ret = SQLAllocConnect( env, &con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( con != (void *)0xdeadbeef, "con not set\n" );

    ret = SQLFreeConnect( con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ret = SQLFreeConnect( 0 );
    ok( ret == SQL_INVALID_HANDLE, "got %d\n", ret );
    ret = SQLFreeEnv( env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ret = SQLFreeEnv( env2 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ret = SQLFreeEnv( 0 );
    ok( ret == SQL_INVALID_HANDLE, "got %d\n", ret );
}

static void diag( SQLHANDLE handle, SQLSMALLINT type )
{
    SQLINTEGER err;
    SQLSMALLINT len;
    SQLCHAR state[5], msg[256];
    SQLRETURN ret;

    memset( state, 0, sizeof(state) );
    err = -1;
    len = 0;
    ret = SQLGetDiagRec( type, handle, 1, state, &err, msg, sizeof(msg), &len );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    trace( "state %s, err %d, msg %s len %d\n", state, err, msg, len );
}

static void test_SQLConnect( void )
{
    SQLHENV env;
    SQLHDBC con;
    SQLRETURN ret;
    SQLINTEGER size, version;
    SQLUINTEGER timeout;
    SQLSMALLINT len;
    char str[32];

    ret = SQLAllocEnv( &env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    version = -1;
    size = -1;
    ret = SQLGetEnvAttr( env, SQL_ATTR_ODBC_VERSION, &version, sizeof(version), &size );
    if (ret == SQL_ERROR) diag( env, SQL_HANDLE_ENV );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( version != -1, "version not set\n" );
    ok( size == -1, "size set\n" );
    trace( "ODBC version %d\n", version );

    ret = SQLAllocConnect( env, &con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLConnect( con, (SQLCHAR *)"winetest", 8, NULL, 0, NULL, 0 );
    if (ret == SQL_ERROR) diag( con, SQL_HANDLE_DBC );
    if (ret != SQL_SUCCESS)
    {
        SQLFreeConnect( con );
        SQLFreeEnv( env );
        skip( "data source winetest not available\n" );
        return;
    }

    timeout = 0xdeadbeef;
    size = -1;
    ret = SQLGetConnectAttr( con, SQL_ATTR_CONNECTION_TIMEOUT, &timeout, sizeof(timeout), &size );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( timeout != 0xdeadbeef, "timeout not set\n" );
    ok( size == -1, "size set\n" );

    len = -1;
    memset( str, 0, sizeof(str) );
    ret = SQLGetInfo( con, SQL_ODBC_VER, str, sizeof(str), &len );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( str[0], "empty string\n" );
    ok( len != -1, "len not set\n" );
    trace( "version %s\n", str );

    ret = SQLDisconnect( con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLFreeConnect( con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLFreeEnv( env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
}

static void test_SQLDataSources( void )
{
    SQLHENV env;
    SQLRETURN ret;
    SQLSMALLINT len, len2;
    SQLCHAR server[256], desc[256];

    ret = SQLAllocEnv( &env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    len = len2 = -1;
    memset( server, 0, sizeof(server) );
    memset( desc, 0, sizeof(desc) );
    ret = SQLDataSources( env, SQL_FETCH_FIRST, server, sizeof(server), &len, desc, sizeof(desc), &len2 );
    ok( ret == SQL_SUCCESS || ret == SQL_NO_DATA, "got %d\n", ret );
    if (ret == SQL_SUCCESS)
    {
        ok( len, "unexpected len\n" );
        ok( len2, "unexpected len\n" );
        ok( server[0], "empty string\n" );
        ok( desc[0], "empty string\n" );
        trace( "server %s len %d desc %s len %d\n", server, len, desc, len2 );
    }

    ret = SQLFreeEnv( env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
}

static void test_SQLDrivers( void )
{
    SQLHENV env;
    SQLRETURN ret;
    SQLSMALLINT len, len2;
    SQLCHAR desc[256], attrs[256];

    ret = SQLAllocEnv( &env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    len = len2 = 0;
    memset( desc, 0, sizeof(desc) );
    memset( attrs, 0, sizeof(attrs) );
    ret = SQLDrivers( env, SQL_FETCH_FIRST, desc, sizeof(desc), &len, attrs, sizeof(attrs), &len2 );
    ok( ret == SQL_SUCCESS || ret == SQL_NO_DATA, "got %d\n", ret );
    if (ret == SQL_SUCCESS)
    {
        ok( len, "unexpected len\n" );
        ok( len2, "unexpected len\n" );
        ok( desc[0], "empty string\n" );
        ok( attrs[0], "empty string\n" );
        trace( "desc %s len %d attrs %s len %d\n", desc, len, attrs, len2 );
    }

    ret = SQLFreeEnv( env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
}

static void test_SQLExecDirect( void )
{
    SQLHENV env;
    SQLHDBC con;
    SQLHSTMT stmt;
    SQLRETURN ret;
    SQLLEN count, len_id, len_name;
    SQLINTEGER id;
    SQLCHAR name[32];

    ret = SQLAllocEnv( &env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLAllocConnect( env, &con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLConnect( con, (SQLCHAR *)"winetest", 8, NULL, 0, NULL, 0 );
    if (ret != SQL_SUCCESS)
    {
        SQLFreeConnect( con );
        SQLFreeEnv( env );
        skip( "data source winetest not available\n" );
        return;
    }
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLAllocStmt( con, &stmt );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    SQLExecDirect( stmt, (SQLCHAR *)"DROP TABLE winetest", ARRAYSIZE("DROP TABLE winetest") - 1 );
    ret = SQLExecDirect( stmt, (SQLCHAR *)"CREATE TABLE winetest ( Id int, Name varchar(255) )",
                         ARRAYSIZE("CREATE TABLE winetest ( Id int, Name varchar(255) )") - 1 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    if (ret == SQL_ERROR) diag( stmt, SQL_HANDLE_STMT );

    ret = SQLExecDirect( stmt, (SQLCHAR *)"INSERT INTO winetest VALUES (0, 'John')",
                         ARRAYSIZE("INSERT INTO winetest VALUES (0, 'John')") - 1 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    if (ret == SQL_ERROR) diag( stmt, SQL_HANDLE_STMT );

    ret = SQLExecDirect( stmt, (SQLCHAR *)"SELECT * FROM winetest", ARRAYSIZE("SELECT * FROM winetest") - 1 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    if (ret == SQL_ERROR) diag( stmt, SQL_HANDLE_STMT );

    count = 0xdeadbeef;
    ret = SQLRowCount( stmt, &count );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( count != 0xdeadbeef, "got %d\n", (int)count );

    ret = SQLFetch( stmt );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    id = -1;
    len_id = 0;
    ret = SQLGetData( stmt, 1, SQL_C_SLONG, &id, sizeof(id), &len_id );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( !id, "id not set\n" );
    ok( len_id == sizeof(id), "got %d\n", (int)len_id );

    ret = SQLFreeStmt( stmt, 0 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLAllocStmt( con, &stmt );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLExecDirect( stmt, (SQLCHAR *)"SELECT * FROM winetest", ARRAYSIZE("SELECT * FROM winetest") - 1 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    if (ret == SQL_ERROR) diag( stmt, SQL_HANDLE_STMT );

    id = -1;
    len_id = 0;
    ret = SQLBindCol( stmt, 1, SQL_C_SLONG, &id, sizeof(id), &len_id );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    if (ret == SQL_ERROR) diag( stmt, SQL_HANDLE_STMT );

    memset( name, 0, sizeof(name) );
    len_name = 0;
    ret = SQLBindCol( stmt, 2, SQL_C_CHAR, name, sizeof(name), &len_name );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    if (ret == SQL_ERROR) diag( stmt, SQL_HANDLE_STMT );

    ret = SQLFetch( stmt );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
    ok( !id, "id not set\n" );
    ok( len_id == sizeof(id), "got %d\n", (int)len_id );
    ok( !strcmp( (const char *)name, "John" ), "got %s\n", name );
    ok( len_name == sizeof("John") - 1, "got %d\n", (int)len_name );

    ret = SQLFreeStmt( stmt, SQL_UNBIND );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLAllocStmt( con, &stmt );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLExecDirect( stmt, (SQLCHAR *)"DROP TABLE winetest", ARRAYSIZE("DROP TABLE winetest") - 1 );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLFreeStmt( stmt, SQL_UNBIND );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLDisconnect( con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLFreeConnect( con );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );

    ret = SQLFreeEnv( env );
    ok( ret == SQL_SUCCESS, "got %d\n", ret );
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

START_TEST(odbc32)
{
    test_SQLAllocHandle();
    test_SQLConnect();
    test_SQLDataSources();
    test_SQLDrivers();
    test_SQLExecDirect();
    test_SQLGetEnvAttr();
}
