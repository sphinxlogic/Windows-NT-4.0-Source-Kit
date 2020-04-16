/*++

Copyright (c) 1991  Microsoft Corporation

Module Name:

    tcpcmd.h

Abstract:

    Common header file for all tcpcmd programs.

Author:

    Mike Massa (mikemas)           Jan 31, 1992

Revision History:

    Who         When        What
    --------    --------    ----------------------------------------------
    mikemas     01-31-92     created

Notes:

--*/

#ifndef TCPCMD_INCLUDED
#define TCPCMD_INCLUDED

#ifndef WIN16
#include <nt.h>
#include <ntrtl.h>
#include <nturtl.h>
#endif // WIN16

#define NOGDI
#define NOMINMAX
#include <windows.h>
#include <winsock.h>
#ifndef WIN16
#include <sys/stropts.h>
#include <ntstapi.h>
#endif // WIN16
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

//
// global variable declarations
//
extern int   optind;
extern int   opterr;
extern char *optarg;


//
// function prototypes
//

char *
GetFileFromPath(
        char *);

HANDLE
OpenStream(
        char *);

int
lwccmp(
        char *,
        char *);

long
netnumber(
        char *);

long
hostnumber(
        char *);

void
blkfree(
        char * FAR *);

struct in_addr *
resolve_host(
        char *);

int
resolve_port(
        char *,
        char *);

void
setsignal(
        void (*)());

char *
tempfile(
        char *);

char *
udp_alloc(
        unsigned int);

void
udp_close(
        SOCKET);

void
udp_free(
        char *);

SOCKET
udp_open(
        int *);

int
udp_port(void);

int
udp_port_used(
        int);

int
udp_read(
        SOCKET,
        char *,
        int,
        struct in_addr *,
        int *,
        int);

int
udp_write(
        SOCKET,
        char *,
        int,
        struct in_addr,
        int);

void
gate_ioctl(
        HANDLE,
        int,
        int,
        int,
        long,
        long);

void
get_route_table(void);

int
tcpcmd_send(
    SOCKET  s,        // socket descriptor
    char          *buf,      // data buffer
    int            len,      // length of data buffer
    int            flags     // transmission flags
    );

void
s_perror(
        char *yourmsg,  // your message to be displayed
        int  lerrno     // errno to be converted
        );


void fatal(char *    message);

#ifndef WIN16
struct netent *getnetbyname(IN char *name);
unsigned long inet_network(IN char *cp);
#endif // WIN16

#define perror(string)  s_perror(string, (int)GetLastError())

#define HZ              1000
#define TCGETA  0x4
#define TCSETA  0x10
#define ECHO    17
#define SIGPIPE 99

// NLS Stuff

#define STDERR 2
#define STDOUT 1

unsigned NlsPutMsg(unsigned, unsigned, ... );
void NlsPerror (unsigned, int);
void ConvertArgvToOem(int, char *[]);

#endif //TCPCMD_INCLUDED
