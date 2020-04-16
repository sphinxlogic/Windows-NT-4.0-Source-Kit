/*++

Copyright (c) 1994  Microsoft Corporation

Module Name:

    cachedef.h

Abstract:

    contains global data declerations.

Author:

    Madan Appiah (madana)  12-Apr-1995

Environment:

    User Mode - Win32

Revision History:

--*/

#ifndef _GLOBAL_
#define _GLOBAL_

#ifdef __cplusplus
extern "C" {
#endif

//
// global variables.
//

extern CRITICAL_SECTION GlobalCacheCritSect;

extern BOOL GlobalCacheInitialized;
extern DWORD GlobalCacheReferenceCount;

extern BOOL GlobalCacheDirUpdateInProgress;

extern DWORD GlobalFreshnessInterval;
extern DWORD GlobalCleanupInterval;
extern DWORD GlobalCleanupFactor;
extern DWORD GlobalCleanupTime;
extern DWORD GlobalPersistent;
extern DWORD GlobalMapFileGrowSize;
extern URL_CONTAINERS *GlobalUrlContainers;

extern HANDLE GlobalScavengerHandle;
extern HANDLE GlobalCacheShutdownEvent;

extern SYSTEM_INFO GlobalSystemInfo;

extern MEMORY *CacheHeap;


#ifdef __cplusplus
}
#endif

#endif  // _GLOBAL_
