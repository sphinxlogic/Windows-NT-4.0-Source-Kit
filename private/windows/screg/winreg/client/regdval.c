/*++

Copyright (c) 1992  Microsoft Corporation

Module Name:

    Regdval.c

Abstract:

    This module contains the client side wrappers for the Win32 Registry
    APIs to delete values from a key.  That is:

        - RegDeleteValueA
        - RegDeleteValueW

Author:

    David J. Gilman (davegi) 18-Mar-1992

Notes:

    See the notes in server\regdval.c.

--*/

#include <rpc.h>
#include "regrpc.h"
#include "client.h"

LONG
APIENTRY
RegDeleteValueA (
    HKEY hKey,
    LPCSTR lpValueName
    )

/*++

Routine Description:

    Win32 ANSI RPC wrapper for deleting a value.

    RegDeleteValueA converts the lpValueName argument to a counted Unicode
    string and then calls BaseRegDeleteValue.

--*/

{
    PUNICODE_STRING     ValueName;
    ANSI_STRING         AnsiString;
    NTSTATUS            Status;

#if DBG
    if ( BreakPointOnEntry ) {
        DbgBreakPoint();
    }
#endif

    //
    // Limit the capabilities associated with HKEY_PERFORMANCE_DATA.
    //

    if( hKey == HKEY_PERFORMANCE_DATA ) {
        return ERROR_INVALID_HANDLE;
    }

    hKey = MapPredefinedHandle( hKey );
    if( hKey == NULL ) {
        return ERROR_INVALID_HANDLE;
    }

    //
    // Convert the value name to a counted Unicode string using the static
    // Unicode string in the TEB.
    //

    ValueName = &NtCurrentTeb( )->StaticUnicodeString;
    ASSERT( ValueName != NULL );
    RtlInitAnsiString( &AnsiString, lpValueName );
    Status = RtlAnsiStringToUnicodeString(
                ValueName,
                &AnsiString,
                FALSE
                );

    if( ! NT_SUCCESS( Status )) {
        return RtlNtStatusToDosError( Status );
    }

    //
    //  Add terminating NULL to Length so that RPC transmits it
    //

    ValueName->Length += sizeof( UNICODE_NULL );

    //
    // Call the Base API, passing it the supplied parameters and the
    // counted Unicode strings.
    //

    if( IsLocalHandle( hKey )) {

        return (LONG)LocalBaseRegDeleteValue (
                    hKey,
                    ValueName
                    );
    } else {

        return (LONG)BaseRegDeleteValue (
                    DereferenceRemoteHandle( hKey ),
                    ValueName
                    );
    }
}

LONG
APIENTRY
RegDeleteValueW (
    HKEY hKey,
    LPCWSTR lpValueName
    )

/*++

Routine Description:

    Win32 Unicode RPC wrapper for deleting a value.

    RegDeleteValueW converts the lpValueName argument to a counted Unicode
    string and then calls BaseRegDeleteValue.

--*/

{
    UNICODE_STRING      ValueName;

#if DBG
    if ( BreakPointOnEntry ) {
        DbgBreakPoint();
    }
#endif

    //
    // Limit the capabilities associated with HKEY_PERFORMANCE_DATA.
    //

    if( hKey == HKEY_PERFORMANCE_DATA ) {
        return ERROR_INVALID_HANDLE;
    }

    hKey = MapPredefinedHandle( hKey );
    if( hKey == NULL ) {
        return ERROR_INVALID_HANDLE;
    }

    //
    // Convert the value name to a counted Unicode string.
    //

    RtlInitUnicodeString( &ValueName, lpValueName );

    //
    //  Add terminating NULL to Length so that RPC transmits it
    //

    ValueName.Length += sizeof( UNICODE_NULL );

    //
    // Call the Base API, passing it the supplied parameters and the
    // counted Unicode strings.
    //

    if( IsLocalHandle( hKey )) {

        return (LONG)LocalBaseRegDeleteValue (
                    hKey,
                    &ValueName
                    );
    } else {

        return (LONG)BaseRegDeleteValue (
                    DereferenceRemoteHandle( hKey ),
                    &ValueName
                    );
    }
}
