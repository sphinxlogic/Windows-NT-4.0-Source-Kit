;/*++
;
;Copyright (c) 1994 Microsoft Corporation
;
;Module Name:
;
;    RnrMsg.h
;
;Abstract:
;
;    This file is generated by the MC tool from the RNRMSG.MC message
;    file.
;
;Author:
;
;    Charles K. Moore (keithmo)   24-July-1994
;
;Revision History:
;
;--*/
;
;#ifndef _RNRMSG_H_
;#define _RNRMSG_H_
;

SeverityNames=(Success=0x0
               Informational=0x1
               Warning=0x2
               Error=0x3
              )

MessageId=1 Severity=Error SymbolicName=RNR_EVENT_SYSTEM_CALL_FAILED
Language=English
A call to a system service failed unexpectedly.  The data is the error.
.

MessageId=2 Severity=Error SymbolicName=RNR_EVENT_CANNOT_INITIALIZE_WINSOCK
Language=English
Cannot initialize the Windows Sockets library.  The data is the error.
.

MessageId=3 Severity=Error SymbolicName=RNR_EVENT_CANNOT_GET_GUID
Language=English
Cannot determine the GUID for this service.  The data is the error.
.

MessageId=4 Severity=Error SymbolicName=RNR_EVENT_CANNOT_OPEN_LISTENING_SOCKETS
Language=English
Cannot open the listening sockets to accept connections.  The data is the error.
.

MessageId=5 Severity=Error SymbolicName=RNR_EVENT_CANNOT_ADVERTISE_SERVICE
Language=English
Cannot advertise the service.  The data is the error.
.

MessageId=6 Severity=Error SymbolicName=RNR_EVENT_CANNOT_CREATE_LISTEN_THREAD
Language=English
Cannot create the listening thread.  The data is the error.
.

MessageId=7 Severity=Error SymbolicName=RNR_EVENT_SELECT_FAILURE
Language=English
The select() API failed unexpectedly.  The data is the error.
.

MessageId=8 Severity=Error SymbolicName=RNR_EVENT_ACCEPT_FAILURE
Language=English
The accept() API failed unexpectedly.  The data is the error.
.

MessageId=9 Severity=Error SymbolicName=RNR_EVENT_RECEIVE_FAILURE
Language=English
The send() API failed unexpectedly.  The data is the error.
.

MessageId=10 Severity=Error SymbolicName=RNR_EVENT_SEND_FAILURE
Language=English
The recv() API failed unexpectedly.  The data is the error.
.

MessageId=11 Severity=Error SymbolicName=RNR_EVENT_CREATE_EVENT_FAILURE
Language=English
Cannot create client thread event.  The data is the error.
.

MessageId=12 Severity=Error SymbolicName=RNR_EVENT_CANNOT_CREATE_CLIENT
Language=English
Cannot create client context data.  The data is the error.
.

MessageId=13 Severity=Error SymbolicName=RNR_EVENT_CANNOT_CREATE_WORKER_THREAD
Language=English
Cannot create client worker thread.  The data is the error.
.

MessageId=14 Severity=Error SymbolicName=RNR_EVENT_TOO_MANY_CLIENTS
Language=English
Too many connected clients.
.

;
;#endif  // _RNRMSG_H_
;

