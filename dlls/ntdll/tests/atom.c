/* Unit test suite for Ntdll atom API functions
 *
 * Copyright 2003 Gyorgy 'Nog' Jeney
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
 * NOTES
 * We use function pointers here as there is no import library for NTDLL on
 * windows.
 */

#include <stdio.h>
#include <stdarg.h>

#include "ntstatus.h"
/* Define WIN32_NO_STATUS so MSVC does not give us duplicate macro 
 * definition errors when we get to winnt.h
 */
#define WIN32_NO_STATUS

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winnls.h"
#include "winuser.h"
#include "wine/test.h"
#include "winternl.h"

static const WCHAR EmptyAtom[] = {0};
static const WCHAR testAtom1[] = {'H','e','l','l','o',' ','W','o','r','l','d',0};
static const WCHAR testAtom2[] = {'H','e','l','l','o',' ','W','o','r','l','d','2',0};
static const WCHAR testAtom3[] = {'H','e','l','l','o',' ','W','o','r','l','d','3',0};

static const WCHAR testAtom1Cap[] = {'H','E','L','L','O',' ','W','O','R','L','D',0};
static const WCHAR testAtom1Low[] = {'h','e','l','l','o',' ','w','o','r','l','d',0};

static const WCHAR testAtomInt[] = {'#','1','3','2',0};
static const WCHAR testAtomIntInv[] = {'#','2','3','4','z',0};
static const WCHAR testAtomOTT[] = {'#','1','2','3',0};

static DWORD WINAPI RtlAtomTestThread(LPVOID Table)
{
    RTL_ATOM_TABLE AtomTable = *(PRTL_ATOM_TABLE)Table;
    RTL_ATOM Atom;
    NTSTATUS res;
    ULONG RefCount = 0, PinCount = 0, Len = 0;
    WCHAR Name[64];

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &Atom );
    ok(!res, "Unable to find atom from another thread, retval: %lx\n", res);

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom2, &Atom );
    ok(!res, "Unable to lookup pinned atom in table, retval: %lx\n", res);

    res = RtlQueryAtomInAtomTable( AtomTable, Atom, &RefCount, &PinCount, Name, &Len );
    ok(res == STATUS_BUFFER_TOO_SMALL, "We got wrong retval: %lx\n", res);

    Len = 64;
    res = RtlQueryAtomInAtomTable( AtomTable, Atom, &RefCount, &PinCount, Name, &Len );
    ok(!res, "Failed with long enough buffer, retval: %lx\n", res);
    ok(RefCount == 1, "Refcount was not 1 but %lx\n", RefCount);
    ok(PinCount == 1, "Pincount was not 1 but %lx\n", PinCount);
    ok(!lstrcmpW(Name, testAtom2), "We found wrong atom!!\n");
    ok((lstrlenW(testAtom2) * sizeof(WCHAR)) == Len, "Returned wrong length %ld\n", Len);

    Len = 64;
    res = RtlQueryAtomInAtomTable( AtomTable, Atom, NULL, NULL, Name, &Len );
    ok(!res, "RtlQueryAtomInAtomTable with optional args invalid failed, retval: %lx\n", res);
    ok(!lstrcmpW(Name, testAtom2), "Found Wrong atom!\n");
    ok((lstrlenW(testAtom2) * sizeof(WCHAR)) == Len, "Returned wrong length %ld\n", Len);

    res = RtlPinAtomInAtomTable( AtomTable, Atom );
    ok(!res, "Unable to pin atom in atom table, retval: %lx\n", res);

    return 0;
}

static void test_NtAtom(void)
{
    RTL_ATOM_TABLE AtomTable = NULL;
    NTSTATUS res;
    RTL_ATOM Atom1, Atom2, Atom3, testEAtom, testAtom;
    HANDLE testThread;
    ULONG RefCount = 0, PinCount = 0, Len = 0;
    WCHAR Name[64];

    /* If we pass a non-null string to create atom table, then it thinks that we
     * have passed it an already allocated atom table */
    res = RtlCreateAtomTable( 0, &AtomTable );
    ok(!res, "RtlCreateAtomTable should succeed with an atom table size of 0\n");
    res = RtlDestroyAtomTable( AtomTable );
    ok( !res, "We could create the atom table, but we couldn't destroy it! retval: %lx\n", res );

    AtomTable = NULL;
    res = RtlCreateAtomTable( 37, &AtomTable );
    ok(!res, "We're unable to create an atom table with a valid table size retval: %lx\n", res);
    ok( *(DWORD *)AtomTable == 0x6d6f7441, "wrong signature %lx\n", *(DWORD *)AtomTable );

    res = RtlAddAtomToAtomTable( AtomTable, testAtom1, &Atom1 );
    ok( !res, "We were unable to add a simple atom to the atom table, retval: %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1Cap, &testAtom );
    ok( !res, "We were unable to find capital version of the atom, retval: %lx\n", res );
    ok( Atom1 == testAtom, "Found wrong atom in table when querying capital atom\n" );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1Low, &testAtom );
    ok( !res, "Unable to find lowercase version of the atom, retval: %lx\n", res );
    ok( testAtom == Atom1, "Found wrong atom when querying lowercase atom\n" );

    res = RtlAddAtomToAtomTable( AtomTable, EmptyAtom, &testEAtom );
    ok( res == STATUS_OBJECT_NAME_INVALID, "Got wrong retval, retval: %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &testAtom );
    ok( !res, "Failed to find totally legitimate atom, retval: %lx\n", res );
    ok( testAtom == Atom1, "Found wrong atom!\n" );

    res = RtlAddAtomToAtomTable( AtomTable, testAtom2, &Atom2 );
    ok( !res, "Unable to add other legitimate atom to table, retval: %lx\n", res );

    res = RtlPinAtomInAtomTable( AtomTable, Atom2 );
    ok( !res, "Unable to pin atom in atom table, retval: %lx\n", res );

    testThread = CreateThread( NULL, 0, RtlAtomTestThread, &AtomTable, 0, NULL );
    WaitForSingleObject( testThread, INFINITE );
    CloseHandle( testThread );

    Len = 64;
    res = RtlQueryAtomInAtomTable( AtomTable, Atom2, &RefCount, &PinCount, Name, &Len );
    ok( !res, "Unable to query atom in atom table, retval: %lx\n", res );
    ok( RefCount == 1, "RefCount is not 1 but %lx\n", RefCount );
    ok( PinCount == 1, "PinCount is not 1 but %lx\n", PinCount );
    ok( !lstrcmpW( Name, testAtom2 ), "We found wrong atom\n" );
    ok( (lstrlenW( testAtom2 ) * sizeof(WCHAR)) == Len, "Returned wrong length %ld\n", Len );

    res = RtlEmptyAtomTable( AtomTable, FALSE );
    ok( !res, "Unable to empty atom table, retval %lx\n", res );

    Len = 64;
    res = RtlQueryAtomInAtomTable( AtomTable, Atom2, &RefCount, &PinCount, Name, &Len );
    ok( !res, "It seems RtlEmptyAtomTable deleted our pinned atom eaven though we asked it not to, retval: %lx\n", res );
    ok( RefCount == 1, "RefCount is not 1 but %lx\n", RefCount );
    ok( PinCount == 1, "PinCount is not 1 but %lx\n", PinCount );
    ok( !lstrcmpW( Name, testAtom2 ), "We found wrong atom\n" );
    ok( (lstrlenW( testAtom2 ) * sizeof(WCHAR)) == Len, "Returned wrong length %ld\n", Len );

    Len = 8;
    Name[0] = Name[1] = Name[2] = Name[3] = Name[4] = 0x1337;
    res = RtlQueryAtomInAtomTable( AtomTable, Atom2, NULL, NULL, Name, &Len );
    ok( !res, "query atom %lx\n", res );
    ok( Len == 6, "wrong length %lu\n", Len );
    ok( !memcmp( Name, testAtom2, Len ), "wrong atom string\n" );
    ok( !Name[3], "wrong string termination\n" );
    ok( Name[4] == 0x1337, "buffer overwrite\n" );

    Len = lstrlenW( testAtom2 ) * sizeof(WCHAR);
    memset( Name, '.', sizeof(Name) );
    res = RtlQueryAtomInAtomTable( AtomTable, Atom2, NULL, NULL, Name, &Len );
    ok( !res, "query atom %lx\n", res );
    ok( Len == (lstrlenW( testAtom2 ) - 1) * sizeof(WCHAR), "wrong length %lu\n", Len );
    ok( !memcmp( testAtom2, Name, (lstrlenW( testAtom2 ) - 1) * sizeof(WCHAR) ),
        "wrong atom name\n" );
    ok( Name[lstrlenW( testAtom2 ) - 1] == '\0', "wrong char\n" );
    ok( Name[lstrlenW( testAtom2 )] == ('.' << 8) + '.', "wrong char\n" );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom2, &testAtom );
    ok( !res, "We can't find our pinned atom!! retval: %lx\n", res );
    ok( testAtom == Atom2, "We found wrong atom!!!\n" );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &testAtom );
    ok( res == STATUS_OBJECT_NAME_NOT_FOUND, "We found the atom in our table eaven though we asked RtlEmptyAtomTable to remove it, retval: %lx\n",
        res );

    res = RtlAddAtomToAtomTable( AtomTable, testAtom3, &Atom3 );
    ok( !res, "Unable to add atom to table, retval: %lx\n", res );

    res = RtlEmptyAtomTable( AtomTable, TRUE );
    ok( !res, "Unable to empty atom table, retval: %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom2, &testAtom );
    ok( res == STATUS_OBJECT_NAME_NOT_FOUND, "The pinned atom should be removed, retval: %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom3, &testAtom );
    ok( res == STATUS_OBJECT_NAME_NOT_FOUND, "Non pinned atom should also be removed, retval: %lx\n", res );

    res = RtlDestroyAtomTable( AtomTable );
    ok( !res, "Can't destroy atom table, retval: %lx\n", res );

    AtomTable = NULL;
    res = RtlCreateAtomTable( 37, &AtomTable );
    ok(!res, "Unable to create atom table, retval: %lx\n", res);

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &testAtom );
    ok( res == STATUS_OBJECT_NAME_NOT_FOUND,
        "Didn't get expected retval with querying an empty atom table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, testAtom1, &Atom1 );
    ok( !res, "Unable to add atom to atom table, retval %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &testAtom );
    ok( !res, "Can't find previously added atom in table, retval: %lx\n", res );
    ok( testAtom == Atom1, "Found wrong atom! retval: %lx\n", res );

    res = RtlDeleteAtomFromAtomTable( AtomTable, Atom1 );
    ok( !res, "Unable to delete atom from table, retval: %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &testAtom );
    ok( res == STATUS_OBJECT_NAME_NOT_FOUND, "Able to find previously deleted atom in table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, testAtom1, &Atom1 );
    ok( !res, "Unable to add atom to atom table, retval: %lx\n", res );

    Len = 0;
    res = RtlQueryAtomInAtomTable( AtomTable, Atom1, NULL, NULL, Name, &Len );
    ok( res == STATUS_BUFFER_TOO_SMALL, "Got wrong retval, retval: %lx\n", res );
    ok( lstrlenW( testAtom1 ) * sizeof(WCHAR) == Len, "Got wrong length %lx\n", Len );

    res = RtlPinAtomInAtomTable( AtomTable, Atom1 );
    ok( !res, "Unable to pin atom in atom table, retval: %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &testAtom );
    ok( !res, "Unable to find atom in atom table, retval: %lx\n", res );
    ok( testAtom == Atom1, "Wrong atom found\n" );

    res = RtlDeleteAtomFromAtomTable( AtomTable, Atom1 );
    ok( res == STATUS_WAS_LOCKED, "Unable to delete atom from table, retval: %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, testAtom1, &testAtom );
    ok( !res, "Able to find deleted atom in table\n" );

    res = RtlDestroyAtomTable( AtomTable );
    ok( !res, "Unable to destroy atom table\n" );
}

/* Test Adding integer atoms to atom table */
static void test_NtIntAtom(void)
{
    struct atom_test
    {
        const WCHAR *str;
        NTSTATUS status;
        UINT value;
    }
    integral_atom_tests[] =
    {
        {(PWSTR)0,     STATUS_INVALID_PARAMETER, 0},
        {L"#",         STATUS_SUCCESS, 0xc000},
        {L"0",         STATUS_SUCCESS, 0xc000},
        {L"#0",        STATUS_INVALID_PARAMETER, 0},
        {L"#0000",     STATUS_INVALID_PARAMETER, 0},
        {L"0x0",       STATUS_SUCCESS, 0xc000},
        {(PWSTR)1,     STATUS_SUCCESS, 1},
        {L"1",         STATUS_SUCCESS, 0xc000},
        {L"#1",        STATUS_SUCCESS, 1},
        {L"0x1",       STATUS_SUCCESS, 0xc000},
        {L"#01",       STATUS_SUCCESS, 1},
        {L"#00001",    STATUS_SUCCESS, 1},
        {L"#00001",    STATUS_SUCCESS, 1},
        {L"#000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
          "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
          "00000000000000000000000000000000000000000000000000000000000000000000000000000001", STATUS_SUCCESS, 1},
        {(PWSTR)123,   STATUS_SUCCESS, 123},
        {L"123",       STATUS_SUCCESS, 0xc000},
        {L"#123",      STATUS_SUCCESS, 123},
        {L"#0123",     STATUS_SUCCESS, 123},
        {L"#0000123",  STATUS_SUCCESS, 123},
        {L"#7b",       STATUS_SUCCESS, 0xc000},
        {L"#0x7b",     STATUS_SUCCESS, 0xc000},
        {L"# 123",     STATUS_SUCCESS, 0xc000},
        {L"#+123",     STATUS_SUCCESS, 0xc000},
        {(PWSTR)49151, STATUS_SUCCESS, 49151},
        {L"#49151",    STATUS_SUCCESS, 49151},
        {L"#0xbfff",   STATUS_SUCCESS, 0xc000},
        {(PWSTR)49152, STATUS_ACCESS_VIOLATION, 0xdead},
        {L"#49152",    STATUS_INVALID_PARAMETER, 0},
        {L"#c000",     STATUS_SUCCESS, 0xc000},
        {L"#0xc000",   STATUS_SUCCESS, 0xc000},
        {L"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
          "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
          "0000000000000000000000000000000000000000000000000000000", STATUS_SUCCESS, 0xc000},
        {L"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
          "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
          "00000000000000000000000000000000000000000000000000000000", STATUS_INVALID_PARAMETER, 0xdead},
    };
    NTSTATUS res;
    RTL_ATOM_TABLE AtomTable;
    RTL_ATOM testAtom;
    ULONG RefCount = 0, PinCount = 0;
    INT_PTR i;
    WCHAR Name[64];
    ULONG Len;

    AtomTable = NULL;
    res = RtlCreateAtomTable( 37, &AtomTable );
    ok(!res, "Unable to create atom table, %lx\n", res);

    /* According to the kernel32 functions, integer atoms are only allowed from
     * 0x0001 to 0xbfff and not 0xc000 to 0xffff, which is correct */
    res = RtlAddAtomToAtomTable( AtomTable, NULL, &testAtom );
    ok( res == STATUS_INVALID_PARAMETER, "Didn't get expected result from adding 0 int atom, retval: %lx\n", res );
    for (i = 1; i <= 0xbfff; i++)
    {
        res = RtlAddAtomToAtomTable( AtomTable, (LPWSTR)i, &testAtom );
        ok( !res, "Unable to add valid integer atom %Ii, retval: %lx\n", i, res );
    }

    for (i = 1; i <= 0xbfff; i++)
    {
        res = RtlLookupAtomInAtomTable( AtomTable, (LPWSTR)i, &testAtom );
        ok( !res, "Unable to find int atom %Ii, retval: %lx\n", i, res );
        res = RtlPinAtomInAtomTable( AtomTable, testAtom );
        ok( !res, "Unable to pin int atom %Ii, retval: %lx\n", i, res );
    }

    for (i = 0xc000; i <= 0xffff; i++)
    {
        res = RtlAddAtomToAtomTable( AtomTable, (LPWSTR)i, &testAtom );
        ok( res, "Able to illeageal integer atom %Ii, retval: %lx\n", i, res );
    }

    res = RtlDestroyAtomTable( AtomTable );
    ok( !res, "Unable to destroy atom table, retval: %lx\n", res );

    AtomTable = NULL;
    res = RtlCreateAtomTable( 37, &AtomTable );
    ok( !res, "Unable to create atom table, %lx\n", res );

    res = RtlLookupAtomInAtomTable( AtomTable, (PWSTR)123, &testAtom );
    ok( !res, "Unable to query atom in atom table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, testAtomInt, &testAtom );
    ok( !res, "Unable to add int atom to table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, testAtomIntInv, &testAtom );
    ok( !res, "Unable to add int atom to table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, (PWSTR)123, &testAtom );
    ok( !res, "Unable to add int atom to table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, (PWSTR)123, &testAtom );
    ok( !res, "Unable to re-add int atom to table, retval: %lx\n", res );

    Len = 64;
    res = RtlQueryAtomInAtomTable( AtomTable, testAtom, &RefCount, &PinCount, Name, &Len );
    ok( !res, "Unable to query atom in atom table, retval: %lx\n", res );
    ok( PinCount == 1, "Expected pincount 1 but got %lx\n", PinCount );
    ok( RefCount == 1, "Expected refcount 1 but got %lx\n", RefCount );
    ok( !lstrcmpW( testAtomOTT, Name ), "Got wrong atom name\n" );
    ok( (lstrlenW( testAtomOTT ) * sizeof(WCHAR)) == Len, "Got wrong len %ld\n", Len );

    res = RtlPinAtomInAtomTable( AtomTable, testAtom );
    ok( !res, "Unable to pin int atom, retval: %lx\n", res );

    res = RtlPinAtomInAtomTable( AtomTable, testAtom );
    ok( !res, "Unable to pin int atom, retval: %lx\n", res );

    res = RtlQueryAtomInAtomTable( AtomTable, testAtom, &RefCount, &PinCount, NULL, NULL );
    ok( !res, "Unable to query atom in atom table, retval: %lx\n", res );
    ok( PinCount == 1, "Expected pincount 1 but got %lx\n", PinCount );
    ok( RefCount == 1, "Expected refcount 1 but got %lx\n", RefCount );

    res = RtlDestroyAtomTable( AtomTable );
    ok( !res, "Unable to destroy atom table, retval: %lx\n", res );


    AtomTable = NULL;
    res = RtlCreateAtomTable( 37, &AtomTable );
    ok(!res, "Unable to create atom table, %lx\n", res);

    res = RtlQueryAtomInAtomTable( AtomTable, 0, &RefCount, &PinCount, NULL, NULL );
    ok( res == STATUS_INVALID_PARAMETER, "got status %#lx\n", res );
    res = RtlQueryAtomInAtomTable( AtomTable, 1, &RefCount, &PinCount, NULL, NULL );
    ok( !res, "got status %#lx\n", res );
    ok( PinCount == 1, "Expected pincount 1 but got %lx\n", PinCount );
    ok( RefCount == 1, "Expected refcount 1 but got %lx\n", RefCount );
    res = RtlQueryAtomInAtomTable( AtomTable, 0xbfff, &RefCount, &PinCount, NULL, NULL );
    ok( !res, "got status %#lx\n", res );
    ok( PinCount == 1, "Expected pincount 1 but got %lx\n", PinCount );
    ok( RefCount == 1, "Expected refcount 1 but got %lx\n", RefCount );
    res = RtlQueryAtomInAtomTable( AtomTable, 0xc000, &RefCount, &PinCount, NULL, NULL );
    ok( res == STATUS_INVALID_HANDLE, "got status %#lx\n", res );

    for (int i = 0; i < ARRAY_SIZE(integral_atom_tests); i++)
    {
        struct atom_test *test = integral_atom_tests + i;

        winetest_push_context( "%s", debugstr_w(test->str) );

        testAtom = 0xdead;
        res = RtlAddAtomToAtomTable( AtomTable, test->str, &testAtom );
        todo_wine_if( test->status == STATUS_ACCESS_VIOLATION )
        ok( res == test->status, "RtlAddAtomToAtomTable returned %#lx\n", res );
        ok( testAtom == test->value, "got %#x\n", testAtom );
        res = RtlDeleteAtomFromAtomTable( AtomTable, testAtom );
        if (testAtom && testAtom != 0xdead) ok( !res, "RtlDeleteAtomFromAtomTable returned %#lx\n", res );
        else ok( res == STATUS_INVALID_HANDLE, "RtlDeleteAtomFromAtomTable returned %#lx\n", res );

        winetest_pop_context();
    }

    res = RtlDestroyAtomTable( AtomTable );
    ok( !res, "Unable to destroy atom table, retval: %lx\n", res );
}

/* Tests to see how the pincount and refcount actually works */
static void test_NtRefPinAtom(void)
{
    RTL_ATOM_TABLE AtomTable;
    RTL_ATOM Atom;
    ULONG PinCount = 0, RefCount = 0;
    NTSTATUS res;

    AtomTable = NULL;
    res = RtlCreateAtomTable( 37, &AtomTable );
    ok(!res, "Unable to create atom table, %lx\n", res);

    res = RtlAddAtomToAtomTable( AtomTable, testAtom1, &Atom );
    ok( !res, "Unable to add our atom to the atom table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, testAtom1, &Atom );
    ok( !res, "Unable to add our atom to the atom table, retval: %lx\n", res );

    res = RtlAddAtomToAtomTable( AtomTable, testAtom1, &Atom );
    ok( !res, "Unable to add our atom to the atom table, retval: %lx\n", res );

    res = RtlQueryAtomInAtomTable( AtomTable, Atom, &RefCount, &PinCount, NULL, NULL );
    ok( !res, "Unable to query atom in atom table, retval: %lx\n", res );
    ok( PinCount == 0, "Expected pincount 0 but got %lx\n", PinCount );
    ok( RefCount == 3, "Expected refcount 3 but got %lx\n", RefCount );

    res = RtlPinAtomInAtomTable( AtomTable, Atom );
    ok( !res, "Unable to pin atom in atom table, retval: %lx\n", res );

    res = RtlPinAtomInAtomTable( AtomTable, Atom );
    ok( !res, "Unable to pin atom in atom table, retval: %lx\n", res );

    res = RtlPinAtomInAtomTable( AtomTable, Atom );
    ok( !res, "Unable to pin atom in atom table, retval: %lx\n", res );

    res = RtlQueryAtomInAtomTable( AtomTable, Atom, &RefCount, &PinCount, NULL, NULL );
    ok( !res, "Unable to query atom in atom table, retval: %lx\n", res );
    ok( PinCount == 1, "Expected pincount 1 but got %lx\n", PinCount );
    ok( RefCount == 3, "Expected refcount 3 but got %lx\n", RefCount );

    res = RtlDestroyAtomTable( AtomTable );
    ok( !res, "Unable to destroy atom table, retval: %lx\n", res );
}

static void test_Global(void)
{
    NTSTATUS    res;
    RTL_ATOM    atom;
    ULONG       ptr[(sizeof(ATOM_BASIC_INFORMATION) + 255 * sizeof(WCHAR)) / sizeof(ULONG)];
    ATOM_BASIC_INFORMATION*     abi = (ATOM_BASIC_INFORMATION*)ptr;
    ULONG       ptr_size = sizeof(ptr);

    res = NtAddAtom( testAtom1, lstrlenW( testAtom1 ) * sizeof(WCHAR), &atom );
    ok(!res, "Added atom (%lx)\n", res);

    memset( ptr, 0xcc, sizeof(ptr) );
    res = NtQueryInformationAtom( atom, AtomBasicInformation, (void *)ptr, ptr_size, NULL );
    ok(!res, "atom lookup\n");
    ok(!lstrcmpW(abi->Name, testAtom1), "ok strings\n");
    ok(abi->NameLength == lstrlenW(testAtom1) * sizeof(WCHAR), "wrong string length\n");
    ok(abi->Name[lstrlenW(testAtom1)] == 0, "wrong string termination %x\n", abi->Name[lstrlenW(testAtom1)]);
    ok(abi->Name[lstrlenW(testAtom1) + 1] == 0xcccc, "buffer overwrite %x\n", abi->Name[lstrlenW(testAtom1) + 1]);

    ptr_size = sizeof(ATOM_BASIC_INFORMATION);
    res = NtQueryInformationAtom( atom, AtomBasicInformation, (void *)ptr, ptr_size, NULL );
    ok(res == STATUS_BUFFER_TOO_SMALL, "wrong return status (%lx)\n", res);
    ok(abi->NameLength == lstrlenW(testAtom1) * sizeof(WCHAR) || broken(abi->NameLength == sizeof(WCHAR)), /* nt4 */
       "string length %u\n",abi->NameLength);

    memset( ptr, 0xcc, sizeof(ptr) );
    ptr_size = sizeof(ATOM_BASIC_INFORMATION) + lstrlenW(testAtom1) * sizeof(WCHAR);
    res = NtQueryInformationAtom( atom, AtomBasicInformation, (void *)ptr, ptr_size, NULL );
    ok(!res, "atom lookup %lx\n", res);
    ok(!lstrcmpW(abi->Name, testAtom1), "strings don't match\n");
    ok(abi->NameLength == lstrlenW(testAtom1) * sizeof(WCHAR), "wrong string length\n");
    ok(abi->Name[lstrlenW(testAtom1)] == 0, "buffer overwrite %x\n", abi->Name[lstrlenW(testAtom1)]);
    ok(abi->Name[lstrlenW(testAtom1) + 1] == 0xcccc, "buffer overwrite %x\n", abi->Name[lstrlenW(testAtom1) + 1]);

    memset( ptr, 0xcc, sizeof(ptr) );
    ptr_size = sizeof(ATOM_BASIC_INFORMATION) + 4 * sizeof(WCHAR);
    res = NtQueryInformationAtom( atom, AtomBasicInformation, (void *)ptr, ptr_size, NULL );
    ok(!res, "couldn't find atom\n");
    ok(abi->NameLength == 8, "wrong string length %u\n", abi->NameLength);
    ok(!memcmp(abi->Name, testAtom1, 8), "strings don't match\n");

    NtDeleteAtom( atom );
}

START_TEST(atom)
{
    /* Global atom table seems to be available to GUI apps only in
       Win7, so let's turn this app into a GUI app */
    GetDesktopWindow();

    test_NtAtom();
    test_NtIntAtom();
    test_NtRefPinAtom();
    test_Global();
}
