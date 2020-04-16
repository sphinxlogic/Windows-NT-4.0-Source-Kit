/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

    cachesub.c

Abstract:

    This module implements the common subroutines for the Cache subsystem.

Author:

    Tom Miller      [TomM]      4-May-1990

Revision History:

--*/

#include "cc.h"

extern POBJECT_TYPE IoFileObjectType;

//
//  The Bug check file id for this module
//

#define BugCheckFileId                   (CACHE_BUG_CHECK_CACHESUB)

//
//  Define our debug constant
//

#define me 0x00000002

//
//  Define those errors which should be retried
//

#define RetryError(STS) (((STS) == STATUS_VERIFY_REQUIRED) || ((STS) == STATUS_FILE_LOCK_CONFLICT))

ULONG CcMaxDirtyWrite = 0x10000;

//
//  Local support routines
//

BOOLEAN
CcFindBcb (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN OUT PLARGE_INTEGER BeyondLastByte,
    OUT PBCB *Bcb
    );

PBCB
CcAllocateInitializeBcb (
    IN OUT PSHARED_CACHE_MAP SharedCacheMap OPTIONAL,
    IN OUT PBCB AfterBcb,
    IN PLARGE_INTEGER FileOffset,
    IN PLARGE_INTEGER Length
    );

NTSTATUS
CcSetValidData(
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER ValidDataLength
    );

BOOLEAN
CcAcquireByteRangeForWrite (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER TargetOffset OPTIONAL,
    IN ULONG TargetLength,
    OUT PLARGE_INTEGER FileOffset,
    OUT PULONG Length,
    OUT PBCB *FirstBcb
    );

VOID
CcReleaseByteRangeFromWrite (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN PBCB FirstBcb,
    IN BOOLEAN VerifyRequired
    );


//
//  Internal support routine
//

BOOLEAN
CcPinFileData (
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN BOOLEAN ReadOnly,
    IN BOOLEAN WriteOnly,
    IN BOOLEAN Wait,
    OUT PBCB *Bcb,
    OUT PVOID *BaseAddress,
    OUT PLARGE_INTEGER BeyondLastByte
    )

/*++

Routine Description:

    This routine locks the specified range of file data into memory.

    Note that the data desired by the caller (or the first part of it)
    may be in one of three states:

        No Bcb exists which describes the data

        A Bcb exists describing the data, but it is not mapped
        (BcbOut->BaseAddress == NULL)

        A Bcb exists describing the data, and it is mapped

    Given the above three states, and given that the caller may call
    with either Wait == FALSE or Wait == TRUE, this routine has basically
    six cases.  What has to be done, and the order in which things must be
    done varies quite a bit with each of these six cases.  The most
    straight-forward implementation of this routine, with the least amount
    of branching, is achieved by determining which of the six cases applies,
    and dispatching fairly directly to that case.  The handling of the
    cases is summarized in the following table:

                Wait == TRUE                Wait == FALSE
                ------------                -------------

    no Bcb      Case 1:                     Case 2:

                CcAllocateInitializeBcb     CcMapAndRead (exit if FALSE)
                Acquire Bcb Exclusive       CcAllocateInitializeBcb
                Release BcbList SpinLock    Acquire Bcb Shared if not ReadOnly
                CcMapAndRead w/ Wait        Release BcbList SpinLock
                Convert/Release Bcb Resource

    Bcb not     Case 3:                     Case 4:
    mapped
                Increment PinCount          Acquire Bcb Exclusive (exit if FALSE)
                Release BcbList SpinLock    CcMapAndRead (exit if FALSE)
                Acquire Bcb Excl. w/ Wait   Increment PinCount
                if still not mapped         Convert/Release Bcb Resource
                    CcMapAndRead w/ Wait    Release BcbList SpinLock
                Convert/Release Bcb Resource

    Bcb mapped  Case 5:                     Case 6:

                Increment PinCount          if not ReadOnly
                Release BcbList SpinLock        Acquire Bcb shared (exit if FALSE)
                if not ReadOnly             Increment PinCount
                    Acquire Bcb Shared      Release BcbList SpinLock

    It is important to note that most changes to this routine will affect
    multiple cases from above.

Arguments:

    FileObject - Pointer to File Object for file

    FileOffset - Offset in file at which map should begin

    Length - Length of desired map in bytes

    ReadOnly - Supplies TRUE if caller will only read the mapped data (i.e.,
               TRUE for CcCopyRead, CcMapData and CcMdlRead and FALSE for
               everyone else)

    WriteOnly - The specified range of bytes will only be written.

    Wait - Supplies TRUE if it is ok to block the caller's thread
           Supplies 3 if it is ok to block the caller's thread and the Bcb should
             be exclusive
           Supplies FALSE if it is not ok to block the caller's thread

    Bcb - Returns a pointer to the Bcb representing the pinned data.

    BaseAddress - Returns base address of desired data

    BeyondLastByte - Returns the File Offset of the first byte beyond the
                     last accessible byte.

Return Value:

    FALSE - if Wait was supplied as TRUE, and it was impossible to lock all
            of the data without blocking
    TRUE - if the desired data, is being returned

Raises:

    STATUS_INSUFFICIENT_RESOURCES - If a pool allocation failure occurs.
        This can only occur if Wait was specified as TRUE.  (If Wait is
        specified as FALSE, and an allocation failure occurs, this
        routine simply returns FALSE.)

--*/

{
    PSHARED_CACHE_MAP SharedCacheMap;
    LARGE_INTEGER TrialBound;
    KIRQL OldIrql;
    PBCB BcbOut = NULL;
    ULONG ZeroFlags = 0;
    BOOLEAN SpinLockAcquired = FALSE;
    BOOLEAN UnmapBcb = FALSE;
    BOOLEAN Result = FALSE;

    ULONG ActivePage;
    ULONG PageIsDirty;
    PVACB ActiveVacb = NULL;

    DebugTrace(+1, me, "CcPinFileData:\n", 0 );
    DebugTrace( 0, me, "    FileObject = %08lx\n", FileObject );
    DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                          FileOffset->HighPart );
    DebugTrace( 0, me, "    Length = %08lx\n", Length );
    DebugTrace( 0, me, "    Wait = %02lx\n", Wait );

    //
    //  Get pointer to SharedCacheMap via File Object.
    //

    SharedCacheMap = *(PSHARED_CACHE_MAP *)((PCHAR)FileObject->SectionObjectPointer
                                            + sizeof(PVOID));

    //
    //  See if we have an active Vacb, that we need to free.
    //

    GetActiveVacb( SharedCacheMap, OldIrql, ActiveVacb, ActivePage, PageIsDirty );

    //
    //  If there is an end of a page to be zeroed, then free that page now,
    //  so it does not cause our data to get zeroed.  If there is an active
    //  page, free it so we have the correct ValidDataGoal.
    //

    if ((ActiveVacb != NULL) || (SharedCacheMap->NeedToZero != NULL)) {

        CcFreeActiveVacb( SharedCacheMap, ActiveVacb, ActivePage, PageIsDirty );
    }

    //
    //  Make sure the calling file system is not asking to map beyond the
    //  end of the section, for example, that it did not forget to do
    //  CcExtendCacheSection.
    //

    ASSERT( ( FileOffset->QuadPart + (LONGLONG)Length ) <=
                     SharedCacheMap->SectionSize.QuadPart );

    //
    //  Initially clear output
    //

    *Bcb = NULL;
    *BaseAddress = NULL;

    //
    //  Acquire Bcb List Exclusive to look for Bcb
    //

    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );
    SpinLockAcquired = TRUE;

    //
    //  Use try to guarantee cleanup on the way out.
    //

    try {

        BOOLEAN Found;
        LARGE_INTEGER FOffset;
        LARGE_INTEGER TLength;
        PVOID BAddress;
        PVACB Vacb;

        //
        //  Search for Bcb describing the largest matching "prefix" byte range,
        //  or where to insert it.
        //

        TrialBound.QuadPart = FileOffset->QuadPart + (LONGLONG)Length;
        Found = CcFindBcb( SharedCacheMap, FileOffset, &TrialBound, &BcbOut );


        //
        //  Cases 1 and 2 - Bcb was not found.
        //
        //  First caculate data to pin down.
        //

        if (!Found) {

            //
            //  Not found, calculate data to pin down.
            //
            //  Round local copy of FileOffset down to page boundary, and
            //  round copies of size and minimum size up.  Also make sure that
            //  we keep the length from crossing the end of the SharedCacheMap.
            //

            FOffset = *FileOffset;
            TLength.QuadPart = TrialBound.QuadPart - FOffset.QuadPart;

            TLength.LowPart += FOffset.LowPart & (PAGE_SIZE - 1);

            //
            //  At this point we can calculate the ReadOnly flag for
            //  the purposes of whether to use the Bcb resource, and
            //  we can calculate the ZeroFlags.
            //

            if ((!ReadOnly  && !FlagOn(SharedCacheMap->Flags, PIN_ACCESS)) || WriteOnly) {

                //
                //  We can always zero middle pages, if any.
                //

                ZeroFlags = ZERO_MIDDLE_PAGES;

                if (((FOffset.LowPart & (PAGE_SIZE - 1)) == 0) &&
                    (Length >= PAGE_SIZE)) {
                    ZeroFlags |= ZERO_FIRST_PAGE;
                }

                if ((TLength.LowPart & (PAGE_SIZE - 1)) == 0) {
                    ZeroFlags |= ZERO_LAST_PAGE;
                }
            }

            //
            //  We treat Bcbs as ReadOnly (do not acquire resource) if they
            //  are in sections for which we have not disabled modified writing.
            //

            if (!FlagOn(SharedCacheMap->Flags, MODIFIED_WRITE_DISABLED)) {
                ReadOnly = TRUE;
            }

            TLength.LowPart = ROUND_TO_PAGES( TLength.LowPart );

            FOffset.LowPart &= ~(PAGE_SIZE - 1);

            //
            //  Even if we are readonly, we can still zero pages entirely
            //  beyond valid data length.
            //

            if (FOffset.QuadPart >= SharedCacheMap->ValidDataGoal.QuadPart) {

                ZeroFlags |= ZERO_FIRST_PAGE | ZERO_MIDDLE_PAGES | ZERO_LAST_PAGE;

            } else if ((FOffset.QuadPart + (LONGLONG)PAGE_SIZE) >=
                                SharedCacheMap->ValidDataGoal.QuadPart) {

                ZeroFlags |= ZERO_MIDDLE_PAGES | ZERO_LAST_PAGE;
            }

            //
            //  We will get into trouble if we try to read more than we
            //  can map by one Vacb.  So make sure that our lengths stay
            //  within a Vacb.
            //

            if (TLength.LowPart > VACB_MAPPING_GRANULARITY) {

                TLength.LowPart = VACB_MAPPING_GRANULARITY;
            }

            if ((FOffset.LowPart & ~(VACB_MAPPING_GRANULARITY - 1))

                    !=

                ((FOffset.LowPart + TLength.LowPart - 1) &
                ~(VACB_MAPPING_GRANULARITY - 1))) {

                TLength.LowPart = VACB_MAPPING_GRANULARITY -
                                  (FOffset.LowPart & (VACB_MAPPING_GRANULARITY - 1));
            }


            //
            //  Case 1 - Bcb was not found and Wait is TRUE.
            //
            //  Note that it is important to minimize the time that the Bcb
            //  List spin lock is held, as well as guarantee we do not take
            //  any faults while holding this lock.
            //
            //  If we can (and perhaps will) wait, then it is important to
            //  allocate the Bcb acquire it exclusive and free the Bcb List.
            //  We then procede to read in the data, and anyone else finding
            //  our Bcb will have to wait shared to insure that the data is
            //  in.
            //

            if (Wait) {

                BcbOut = CcAllocateInitializeBcb ( SharedCacheMap,
                                                   BcbOut,
                                                   &FOffset,
                                                   &TLength );

                if (BcbOut == NULL) {
                    DebugTrace( 0, 0, "Bcb allocation failure\n", 0 );
                    ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                    SpinLockAcquired = FALSE;
                    ExRaiseStatus( STATUS_INSUFFICIENT_RESOURCES );
                }

                //
                //  Now just acquire the newly-allocated Bcb shared, and
                //  release the spin lock.
                //

                if (!ReadOnly) {
                    if (Wait == 3) {
                        (VOID)ExAcquireResourceExclusive( &BcbOut->Resource, TRUE );
                    } else {
                        (VOID)ExAcquireSharedStarveExclusive( &BcbOut->Resource, TRUE );
                    }
                }
                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                SpinLockAcquired = FALSE;

                //
                //  Now read in the data.
                //
                //  We set UnmapBcb to be TRUE for the duration of this call,
                //  so that if we get an exception, we will call CcUnpinFileData
                //  and probably delete the Bcb.
                //

                UnmapBcb = TRUE;
                (VOID)CcMapAndRead( SharedCacheMap,
                                    &FOffset,
                                    TLength.LowPart,
                                    ZeroFlags,
                                    TRUE,
                                    &Vacb,
                                    &BAddress );

                UnmapBcb = FALSE;

                //
                //  Now we have to reacquire the Bcb List spinlock to load
                //  up the mapping if we are the first one, else we collided
                //  with someone else who loaded the mapping first, and we
                //  will just free our mapping.  It is guaranteed that the
                //  data will be mapped to the same place.
                //

                ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

                if (BcbOut->BaseAddress == NULL) {

                    BcbOut->BaseAddress = BAddress;
                    BcbOut->Vacb = Vacb;

                } else {
                    CcFreeVirtualAddress( Vacb );
                }

                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

                //
                //  Calculate Base Address of the data we want.
                //

                *BaseAddress = (PCHAR)BcbOut->BaseAddress +
                               (ULONG)( FileOffset->QuadPart - BcbOut->FileOffset.QuadPart );

                //
                //  Success!
                //

                try_return( Result = TRUE );
            }


            //
            //  Case 2 - Bcb was not found and Wait is FALSE
            //
            //  If we cannot wait, then we go immediately see if the data is
            //  there (CcMapAndRead), and then only set up the Bcb and release
            //  the spin lock if the data is there.  Note here we call
            //  CcMapAndRead while holding the spin lock, because we know we
            //  will not fault and not block before returning.
            //

            else {

                //
                //  Now try to allocate and initialize the Bcb.  If we
                //  fail to allocate one, then return FALSE, since we know that
                //  Wait = FALSE.  The caller may get lucky if he calls
                //  us back with Wait = TRUE.
                //

                BcbOut = CcAllocateInitializeBcb ( SharedCacheMap,
                                                   BcbOut,
                                                   &FOffset,
                                                   &TLength );

                if (BcbOut == NULL) {

                    try_return( Result = FALSE );
                }

                //
                //  If we are not ReadOnly, we must acquire the newly-allocated
                //  resource shared, and then we can free the spin lock.
                //

                if (!ReadOnly) {
                    ExAcquireSharedStarveExclusive( &BcbOut->Resource, TRUE );
                }
                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                SpinLockAcquired = FALSE;

                //
                //  Note that since this call has Wait = FALSE, it cannot
                //  get an exception (see procedure header).
                //

                UnmapBcb = TRUE;
                if (!CcMapAndRead( SharedCacheMap,
                                   &FOffset,
                                   TLength.LowPart,
                                   ZeroFlags,
                                   FALSE,
                                   &Vacb,
                                   &BAddress )) {

                    try_return( Result = FALSE );
                }
                UnmapBcb = FALSE;

                //
                //  Now we have to reacquire the Bcb List spinlock to load
                //  up the mapping if we are the first one, else we collided
                //  with someone else who loaded the mapping first, and we
                //  will just free our mapping.  It is guaranteed that the
                //  data will be mapped to the same place.
                //

                ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

                if (BcbOut->BaseAddress == NULL) {

                    BcbOut->BaseAddress = BAddress;
                    BcbOut->Vacb = Vacb;

                } else {
                    CcFreeVirtualAddress( Vacb );
                }

                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

                //
                //  Calculate Base Address of the data we want.
                //

                *BaseAddress = (PCHAR)BcbOut->BaseAddress +
                               (ULONG)( FileOffset->QuadPart - BcbOut->FileOffset.QuadPart );

                //
                //  Success!
                //

                try_return( Result = TRUE );
            }

        } else {

            //
            //  We treat Bcbs as ReadOnly (do not acquire resource) if they
            //  are in sections for which we have not disabled modified writing.
            //

            if (!FlagOn(SharedCacheMap->Flags, MODIFIED_WRITE_DISABLED)) {
                ReadOnly = TRUE;
            }
        }


        //
        //  Cases 3 and 4 - Bcb is there but not mapped
        //

        if (BcbOut->BaseAddress == NULL) {

            //
            //  It is too complicated to attempt to calculate any ZeroFlags in this
            //  case, because we have to not only do the tests above, but also
            //  compare to the byte range in the Bcb since we will be passing
            //  those parameters to CcMapAndRead.  Also, the probability of hitting
            //  some window where zeroing is of any advantage is quite small.
            //

            //
            //  Set up to just reread the Bcb exactly as the data in it is
            //  described.
            //

            FOffset = BcbOut->FileOffset;
            TLength.QuadPart = (LONGLONG)BcbOut->ByteLength;

            //
            //  Case 3 - Bcb is there but not mapped and Wait is TRUE
            //
            //  Increment the PinCount, and then release the BcbList
            //  SpinLock so that we can wait to acquire the Bcb exclusive.
            //  Once we have the Bcb exclusive, map and read it in if no
            //  one beats us to it.  Someone may have beat us to it since
            //  we had to release the SpinLock above.
            //

            if (Wait) {

                BcbOut->PinCount += 1;

                //
                //  Now we have to release the BcbList SpinLock in order to
                //  acquire the Bcb shared.
                //

                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                SpinLockAcquired = FALSE;
                if (!ReadOnly) {
                    if (Wait == 3) {
                        (VOID)ExAcquireResourceExclusive( &BcbOut->Resource, TRUE );
                    } else {
                        (VOID)ExAcquireSharedStarveExclusive( &BcbOut->Resource, TRUE );
                    }
                }

                //
                //  Now procede to map and read the data in.
                //
                //  Now read in the data.
                //
                //  We set UnmapBcb to be TRUE for the duration of this call,
                //  so that if we get an exception, we will call CcUnpinFileData
                //  and probably delete the Bcb.
                //

                UnmapBcb = TRUE;
                (VOID)CcMapAndRead( SharedCacheMap,
                                    &FOffset,
                                    TLength.LowPart,
                                    ZeroFlags,
                                    TRUE,
                                    &Vacb,
                                    &BAddress );
                UnmapBcb = FALSE;

                //
                //  Now we have to reacquire the Bcb List spinlock to load
                //  up the mapping if we are the first one, else we collided
                //  with someone else who loaded the mapping first, and we
                //  will just free our mapping.  It is guaranteed that the
                //  data will be mapped to the same place.
                //

                ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

                if (BcbOut->BaseAddress == NULL) {

                    BcbOut->BaseAddress = BAddress;
                    BcbOut->Vacb = Vacb;

                } else {
                    CcFreeVirtualAddress( Vacb );
                }

                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

                //
                //
                //  Calculate Base Address of the data we want.
                //

                *BaseAddress = (PCHAR)BcbOut->BaseAddress +
                               (ULONG)( FileOffset->QuadPart - BcbOut->FileOffset.QuadPart );

                //
                //  Success!
                //

                try_return( Result = TRUE );
            }


            //
            //  Case 4 - Bcb is there but not mapped, and Wait is FALSE
            //
            //  Since we cannot wait, we go immediately see if the data is
            //  there (CcMapAndRead), and then only set up the Bcb and release
            //  the spin lock if the data is there.  Note here we call
            //  CcMapAndRead while holding the spin lock, because we know we
            //  will not fault and not block before returning.
            //

            else {

                if (!ReadOnly && !ExAcquireSharedStarveExclusive( &BcbOut->Resource, FALSE )) {
                    try_return( Result = FALSE );
                }

                BcbOut->PinCount += 1;

                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                SpinLockAcquired = FALSE;

                //
                //  Note that since this call has Wait = FALSE, it cannot
                //  get an exception (see procedure header).
                //

                UnmapBcb = TRUE;
                if (!CcMapAndRead( SharedCacheMap,
                                   &BcbOut->FileOffset,
                                   BcbOut->ByteLength,
                                   ZeroFlags,
                                   FALSE,
                                   &Vacb,
                                   &BAddress )) {

                    try_return( Result = FALSE );
                }
                UnmapBcb = FALSE;

                //
                //  Now we have to reacquire the Bcb List spinlock to load
                //  up the mapping if we are the first one, else we collided
                //  with someone else who loaded the mapping first, and we
                //  will just free our mapping.  It is guaranteed that the
                //  data will be mapped to the same place.
                //

                ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

                if (BcbOut->BaseAddress == NULL) {

                    BcbOut->BaseAddress = BAddress;
                    BcbOut->Vacb = Vacb;

                } else {
                    CcFreeVirtualAddress( Vacb );
                }

                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

                //
                //  Calculate Base Address of the data we want.
                //

                *BaseAddress = (PCHAR)BcbOut->BaseAddress +
                               (ULONG)( FileOffset->QuadPart - BcbOut->FileOffset.QuadPart );

                //
                //  Success!
                //

                try_return( Result = TRUE );
            }
        }


        //
        //  Cases 5 and 6 - Bcb is there and it is mapped
        //

        else {

            //
            //  Case 5 - Bcb is there and mapped, and Wait is TRUE
            //
            //  We can just increment the PinCount, release the SpinLock
            //  and then acquire the Bcb Shared if we are not ReadOnly.
            //

            if (Wait) {

                BcbOut->PinCount += 1;
                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                SpinLockAcquired = FALSE;

                //
                //  Acquire Bcb Resource shared to insure that it is in memory.
                //

                if (!ReadOnly) {
                    if (Wait == 3) {
                        (VOID)ExAcquireResourceExclusive( &BcbOut->Resource, TRUE );
                    } else {
                        (VOID)ExAcquireSharedStarveExclusive( &BcbOut->Resource, TRUE );
                    }
                }
            }

            //
            //  Case 6 - Bcb is there and mapped, and Wait is FALSE
            //
            //  If we are not ReadOnly, we have to first see if we can
            //  acquire the Bcb shared before incrmenting the PinCount,
            //  since we will have to return FALSE if we cannot acquire the
            //  resource.
            //

            else {

                //
                //  Acquire Bcb Resource shared to insure that it is in memory.
                //

                if (!ReadOnly && !ExAcquireSharedStarveExclusive( &BcbOut->Resource, FALSE )) {
                    try_return( Result = FALSE );
                }
                BcbOut->PinCount += 1;
                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                SpinLockAcquired = FALSE;
            }

            //
            //  Calculate Base Address of the data we want.
            //

            *BaseAddress = (PCHAR)BcbOut->BaseAddress +
                           (ULONG)( FileOffset->QuadPart - BcbOut->FileOffset.QuadPart );

            //
            //  Success!
            //

            try_return( Result = TRUE );
        }


    try_exit: NOTHING;

    }

    finally {

        //
        //  Release the spinlock if it is acquired.
        //

        if (SpinLockAcquired) {
            ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
        }

        //
        //  An abnormal termination can occur on an allocation failure,
        //  or on a failure to map and read the buffer.  The latter
        //  operation is performed with UnmapBcb = TRUE, so that we
        //  know to make the unmap call.
        //

        if (UnmapBcb) {
            CcUnpinFileData( BcbOut, ReadOnly, UNPIN );
            BcbOut = NULL;
        }

        if (Result) {

            *Bcb = BcbOut;
            if (BcbOut != NULL) {
                *BeyondLastByte = BcbOut->BeyondLastByte;
            }
            else {
                *BeyondLastByte = *FileOffset;
            }
        }

        DebugTrace( 0, me, "    <Bcb = %08lx\n", *Bcb );
        DebugTrace( 0, me, "    <BaseAddress = %08lx\n", *BaseAddress );
        DebugTrace(-1, me, "CcPinFileData -> %02lx\n", Result );
    }

    return Result;
}


//
//  Internal Support Routine
//

VOID
FASTCALL
CcUnpinFileData (
    IN OUT PBCB Bcb,
    IN BOOLEAN ReadOnly,
    IN UNMAP_ACTIONS UnmapAction
    )

/*++

Routine Description:

    This routine umaps and unlocks the specified buffer, which was previously
    locked and mapped by calling CcPinFileData.

Arguments:

    Bcb - Pointer previously returned from CcPinFileData.  As may be
          seen above, this pointer may be either a Bcb or a Vacb.

    ReadOnly - must specify same value as when data was mapped

    UnmapAction - UNPIN or SET_CLEAN

Return Value:

    None

--*/

{
    KIRQL OldIrql;
    PSHARED_CACHE_MAP SharedCacheMap;

    DebugTrace(+1, me, "CcUnpinFileData >Bcb = %08lx\n", Bcb );

    //
    //  Note, since we have to allocate so many Vacbs, we do not use
    //  a node type code.  However, the Vacb starts with a BaseAddress,
    //  so we assume that the low byte of the Bcb node type code has
    //  some bits set, which a page-aligned Base Address cannot.
    //

    ASSERT( (CACHE_NTC_BCB & 0xFF) != 0 );

    if (Bcb->NodeTypeCode != CACHE_NTC_BCB) {

        ASSERT(((PVACB)Bcb)->SharedCacheMap->NodeTypeCode == CACHE_NTC_SHARED_CACHE_MAP);

        CcFreeVirtualAddress( (PVACB)Bcb );

        DebugTrace(-1, me, "CcUnpinFileData -> VOID (simple release)\n", 0 );

        return;
    }

    SharedCacheMap = Bcb->SharedCacheMap;

    //
    //  We treat Bcbs as ReadOnly (do not acquire resource) if they
    //  are in sections for which we have not disabled modified writing.
    //

    if (!FlagOn(SharedCacheMap->Flags, MODIFIED_WRITE_DISABLED)) {
        ReadOnly = TRUE;
    }

    //
    //  Synchronize
    //

    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

    switch (UnmapAction) {

    case UNPIN:

        ASSERT( Bcb->PinCount > 0 );

        Bcb->PinCount -= 1;
        break;

    case SET_CLEAN:

        if (Bcb->Dirty) {

            ULONG Pages = Bcb->ByteLength >> PAGE_SHIFT;

            //
            //  Reverse the rest of the actions taken when the Bcb was set dirty.
            //

            Bcb->Dirty = FALSE;
            SharedCacheMap->DirtyPages -= Pages;
            CcTotalDirtyPages -= Pages;

            //
            //  Normally we need to reduce CcPagesYetToWrite appropriately.
            //

            if (CcPagesYetToWrite > Pages) {
                CcPagesYetToWrite -= Pages;
            } else {
                CcPagesYetToWrite = 0;
            }

            //
            //  Remove SharedCacheMap from dirty list if nothing more dirty,
            //  and someone still has the cache map opened.
            //

            if ((SharedCacheMap->DirtyPages == 0) &&
                (SharedCacheMap->OpenCount != 0)) {

                RemoveEntryList( &SharedCacheMap->SharedCacheMapLinks );
                InsertTailList( &CcCleanSharedCacheMapList,
                                &SharedCacheMap->SharedCacheMapLinks );
            }
        }

        break;

    default:
        CcBugCheck( UnmapAction, 0, 0 );
    }

    //
    //  If we brought it to 0, then we have to kill it.
    //

    if (Bcb->PinCount == 0) {

        //
        //  If the Bcb is Dirty, we only release the resource and unmap now.
        //

        if (Bcb->Dirty) {

            if (Bcb->BaseAddress != NULL) {

                //
                //  Capture CcFreeVirtualAddress parameters to locals so that we can
                //  reset Bcb->BaseAddress and release the spinlock before
                //  unmapping.
                //

                PVOID BaseAddress = Bcb->BaseAddress;
                ULONG ByteLength = Bcb->ByteLength;
                PVACB Vacb = Bcb->Vacb;

                Bcb->BaseAddress = NULL;
                Bcb->Vacb = NULL;

                if (!ReadOnly) {
                    ExReleaseResource( &Bcb->Resource );
                }

                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

                CcFreeVirtualAddress( Vacb );
            }
            else {

                if (!ReadOnly) {
                    ExReleaseResource( &Bcb->Resource );
                }
                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
            }
        }

        //
        //  Otherwise, we also delete the Bcb.
        //

        else {

            RemoveEntryList( &Bcb->BcbLinks );

            if (Bcb->BaseAddress != NULL) {

                CcFreeVirtualAddress( Bcb->Vacb );
            }

            //
            //  Debug routines used to remove Bcbs from the global list
            //

#if LIST_DBG

            ExAcquireSpinLockAtDpcLevel( &CcBcbSpinLock );

            if (Bcb->CcBcbLinks.Flink != NULL) {

                RemoveEntryList( &Bcb->CcBcbLinks );
                CcBcbCount -= 1;
            }

            ExReleaseSpinLockFromDpcLevel( &CcBcbSpinLock );

#endif
#if DBG
            if (!ReadOnly) {
                ExReleaseResource( &Bcb->Resource );
            }

            //
            //  ASSERT that the resource is unowned.
            //

            ASSERT( Bcb->Resource.ActiveCount == 0 );
#endif
            CcDeallocateBcb( Bcb );

            ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
        }
    }

    //
    //  Else we just have to release our Shared access, if we are not
    //  readonly.  We don't need to do this above, since we deallocate
    //  the entire Bcb there.
    //

    else {

        if (!ReadOnly) {
            ExReleaseResource( &Bcb->Resource );
        }

        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
    }

    DebugTrace(-1, me, "CcUnpinFileData -> VOID\n", 0 );

    return;
}


VOID
CcSetReadAheadGranularity (
    IN PFILE_OBJECT FileObject,
    IN ULONG Granularity
    )

/*++

Routine Description:

    This routine may be called to set the read ahead granularity used by
    the Cache Manager.  The default is PAGE_SIZE.  The number is decremented
    and stored as a mask.

Arguments:

    FileObject - File Object for which granularity shall be set

    Granularity - new granularity, which must be an even power of 2 and
                  >= PAGE_SIZE

Return Value:

    None
--*/

{
    ((PPRIVATE_CACHE_MAP)FileObject->PrivateCacheMap)->ReadAheadMask = Granularity - 1;
}


VOID
CcScheduleReadAhead (
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine is called by Copy Read and Mdl Read file system routines to
    perform common Read Ahead processing.  The input parameters describe
    the current read which has just been completed, or perhaps only started
    in the case of Mdl Reads.  Based on these parameters, an
    assessment is made on how much data should be read ahead, and whether
    that data has already been read ahead.

    The processing is divided into two parts:

        CALCULATE READ AHEAD REQUIREMENTS   (CcScheduleReadAhead)

        PERFORM READ AHEAD                  (CcPerformReadAhead)

    File systems should always call CcReadAhead, which will conditionally
    call CcScheduleReadAhead (if the read is large enough).  If such a call
    determines that there is read ahead work to do, and no read ahead is
    currently active, then it will set ReadAheadActive and schedule read
    ahead to be peformed by the Lazy Writer, who will call CcPeformReadAhead.

Arguments:

    FileObject - supplies pointer to FileObject on which readahead should be
                 considered.

    FileOffset - supplies the FileOffset at which the last read just occurred.

    Length - supplies the length of the last read.

Return Value:

    None
--*/

{
    LARGE_INTEGER NewOffset;
    LARGE_INTEGER NewBeyond;
    LARGE_INTEGER FileOffset1, FileOffset2;
    KIRQL OldIrql;
    PSHARED_CACHE_MAP SharedCacheMap;
    PPRIVATE_CACHE_MAP PrivateCacheMap;
    PWORK_QUEUE_ENTRY WorkQueueEntry;
    ULONG ReadAheadSize;
    BOOLEAN Changed = FALSE;

    DebugTrace(+1, me, "CcScheduleReadAhead:\n", 0 );
    DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                          FileOffset->HighPart );
    DebugTrace( 0, me, "    Length = %08lx\n", Length );

    SharedCacheMap = *(PSHARED_CACHE_MAP *)((PCHAR)FileObject->SectionObjectPointer
                                            + sizeof(PVOID));
    PrivateCacheMap = FileObject->PrivateCacheMap;

    if ((PrivateCacheMap == NULL) ||
        (SharedCacheMap == NULL) ||
        FlagOn(SharedCacheMap->Flags, DISABLE_READ_AHEAD)) {

        DebugTrace(-1, me, "CcScheduleReadAhead -> VOID (Nooped)\n", 0 );

        return;
    }

    //
    //  Round boundaries of transfer up to some greater granularity, so that
    //  sequential reads will be recognized even if a few bytes are skipped
    //  between records.
    //

    NewOffset = *FileOffset;
    NewBeyond.QuadPart = FileOffset->QuadPart + (LONGLONG)Length;

    //
    //  Find the next read ahead boundary beyond the current read.
    //

    ReadAheadSize = (Length + PrivateCacheMap->ReadAheadMask) & ~PrivateCacheMap->ReadAheadMask;
    FileOffset2.QuadPart = NewBeyond.QuadPart + (LONGLONG)ReadAheadSize;
    FileOffset2.LowPart &= ~PrivateCacheMap->ReadAheadMask;

    //
    //  CALCULATE READ AHEAD REQUIREMENTS
    //

    //
    //  Take out the ReadAhead spinlock to synchronize our read ahead decision.
    //

    ExAcquireSpinLock( &PrivateCacheMap->ReadAheadSpinLock, &OldIrql );

    //
    //  Read Ahead Case 0.
    //
    //  Sequential-only hint in the file object.  For this case we will
    //  try and always keep two read ahead granularities read ahead from
    //  and including the end of the current transfer.  This case has the
    //  lowest overhead, and the code is completely immune to how the
    //  caller skips around.  Sequential files use ReadAheadOffset[1] in
    //  the PrivateCacheMap as their "high water mark".
    //

    if (FlagOn(FileObject->Flags, FO_SEQUENTIAL_ONLY)) {

        //
        //  If the next boundary is greater than or equal to the high-water mark,
        //  then read ahead.
        //

        if (FileOffset2.QuadPart >= PrivateCacheMap->ReadAheadOffset[1].QuadPart) {

            //
            //  On the first read if we are using a large read ahead granularity,
            //  and the read did not get it all, we will just get the rest of the
            //  first data we want.
            //

            if ((FileOffset->QuadPart == 0)

                    &&

                (PrivateCacheMap->ReadAheadMask > (PAGE_SIZE - 1))

                    &&

                ((Length + PAGE_SIZE - 1) <= PrivateCacheMap->ReadAheadMask)) {

                FileOffset1.QuadPart = (LONGLONG)( ROUND_TO_PAGES(Length) );
                PrivateCacheMap->ReadAheadLength[0] = ReadAheadSize - FileOffset1.LowPart;
                FileOffset2.QuadPart = (LONGLONG)ReadAheadSize;

            //
            //  Calculate the next read ahead boundary.
            //

            } else {

                FileOffset1.QuadPart = PrivateCacheMap->ReadAheadOffset[1].QuadPart +
                                       (LONGLONG)ReadAheadSize;

                //
                //  If the end of the current read is actually beyond where we would
                //  normally do our read ahead, then we have fallen behind, and we must
                //  advance to that spot.
                //

                if (FileOffset2.QuadPart > FileOffset1.QuadPart) {
                    FileOffset1 = FileOffset2;
                }
                PrivateCacheMap->ReadAheadLength[0] = ReadAheadSize;
                FileOffset2.QuadPart = FileOffset1.QuadPart + (LONGLONG)ReadAheadSize;
            }

            //
            //  Now issue the next two read aheads.
            //

            PrivateCacheMap->ReadAheadOffset[0] = FileOffset1;

            PrivateCacheMap->ReadAheadOffset[1] = FileOffset2;
            PrivateCacheMap->ReadAheadLength[1] = ReadAheadSize;

            Changed = TRUE;
        }

    //
    //  Read Ahead Case 1.
    //
    //  If this is the third of three sequential reads, then we will see if
    //  we can read ahead.  Note that if the first read to a file is to
    //  offset 0, it passes this test.
    //

    } else if ((NewOffset.HighPart == PrivateCacheMap->BeyondLastByte2.HighPart)

            &&

        ((NewOffset.LowPart & ~NOISE_BITS)
           == (PrivateCacheMap->BeyondLastByte2.LowPart & ~NOISE_BITS))

            &&

        (PrivateCacheMap->FileOffset2.HighPart
           == PrivateCacheMap->BeyondLastByte1.HighPart)

            &&

        ((PrivateCacheMap->FileOffset2.LowPart & ~NOISE_BITS)
           == (PrivateCacheMap->BeyondLastByte1.LowPart & ~NOISE_BITS))) {

        //
        //  On the first read if we are using a large read ahead granularity,
        //  and the read did not get it all, we will just get the rest of the
        //  first data we want.
        //

        if ((FileOffset->QuadPart == 0)

                &&

            (PrivateCacheMap->ReadAheadMask > (PAGE_SIZE - 1))

                &&

            ((Length + PAGE_SIZE - 1) <= PrivateCacheMap->ReadAheadMask)) {

            FileOffset2.QuadPart = (LONGLONG)( ROUND_TO_PAGES(Length) );
        }

        //
        //  Round read offset to next read ahead boundary.
        //

        else {
            FileOffset2.QuadPart = NewBeyond.QuadPart + (LONGLONG)ReadAheadSize;

            FileOffset2.LowPart &= ~PrivateCacheMap->ReadAheadMask;
        }

        //
        //  Set read ahead length to be the same as for the most recent read,
        //  up to our max.
        //

        if (FileOffset2.QuadPart != PrivateCacheMap->ReadAheadOffset[1].QuadPart) {

            ASSERT( FileOffset2.HighPart >= 0 );

            Changed = TRUE;
            PrivateCacheMap->ReadAheadOffset[1] = FileOffset2;
            PrivateCacheMap->ReadAheadLength[1] = ReadAheadSize;
        }
    }

    //
    //  Read Ahead Case 2.
    //
    //  If this is the third read following a particular stride, then we
    //  will see if we can read ahead.  One example of an application that
    //  might do this is a spreadsheet.  Note that this code even works
    //  for negative strides.
    //

    else if ( ( NewOffset.QuadPart -
                PrivateCacheMap->FileOffset2.QuadPart ) ==
              ( PrivateCacheMap->FileOffset2.QuadPart -
                PrivateCacheMap->FileOffset1.QuadPart )) {

        //
        //  According to the current stride, the next offset will be:
        //
        //      NewOffset + (NewOffset - FileOffset2)
        //
        //  which is the same as:
        //
        //      (NewOffset * 2) - FileOffset2
        //

        FileOffset2.QuadPart = ( NewOffset.QuadPart << 1 ) - PrivateCacheMap->FileOffset2.QuadPart;

        //
        //  If our stride is going backwards through the file, we
        //  have to detect the case where the next step would wrap.
        //

        if (FileOffset2.HighPart >= 0) {

            //
            //  The read ahead length must be extended by the same amount that
            //  we will round the PrivateCacheMap->ReadAheadOffset down.
            //

            Length += FileOffset2.LowPart & (PAGE_SIZE - 1);

            //
            //  Now round the PrivateCacheMap->ReadAheadOffset down.
            //

            FileOffset2.LowPart &= ~(PAGE_SIZE - 1);
            PrivateCacheMap->ReadAheadOffset[1] = FileOffset2;

            //
            //  Round to page boundary.
            //

            PrivateCacheMap->ReadAheadLength[1] = ROUND_TO_PAGES(Length);
            Changed = TRUE;
        }
    }

    //
    //  Get out if the ReadAhead requirements did not change.
    //

    if (!Changed || PrivateCacheMap->ReadAheadActive) {

        DebugTrace( 0, me, "Read ahead already in progress or no change\n", 0 );

        ExReleaseSpinLock( &PrivateCacheMap->ReadAheadSpinLock, OldIrql );
        return;
    }

    //
    //  Otherwise, we will proceed and try to schedule the read ahead
    //  ourselves.
    //

    PrivateCacheMap->ReadAheadActive = TRUE;

    //
    //  Release spin lock on way out
    //

    ExReleaseSpinLock( &PrivateCacheMap->ReadAheadSpinLock, OldIrql );

    //
    //  Queue the read ahead request to the Lazy Writer's work queue.
    //

    DebugTrace( 0, me, "Queueing read ahead to worker thread\n", 0 );

    WorkQueueEntry = CcAllocateWorkQueueEntry();

    //
    //  If we failed to allocate a work queue entry, then, we will
    //  quietly bag it.  Read ahead is only an optimization, and
    //  no one ever requires that it occur.
    //

    if (WorkQueueEntry != NULL) {

        //
        //  We must reference this file object so that it cannot go away
        //  until we finish Read Ahead processing in the Worker Thread.
        //

        ObReferenceObject ( FileObject );

        //
        //  Increment open count to make sure the SharedCacheMap stays around.
        //

        ExAcquireFastLock( &CcMasterSpinLock, &OldIrql );
        SharedCacheMap->OpenCount += 1;
        ExReleaseFastLock( &CcMasterSpinLock, OldIrql );

        WorkQueueEntry->Function = (UCHAR)ReadAhead;
        WorkQueueEntry->Parameters.Read.FileObject = FileObject;

        CcPostWorkQueue( WorkQueueEntry, &CcExpressWorkQueue );
    }

    //
    //  If we failed to allocate a Work Queue Entry, or all of the pages
    //  are resident we must set the active flag false.
    //

    else {

        ExAcquireFastLock( &PrivateCacheMap->ReadAheadSpinLock, &OldIrql );
        PrivateCacheMap->ReadAheadActive = FALSE;
        ExReleaseFastLock( &PrivateCacheMap->ReadAheadSpinLock, OldIrql );
    }

    DebugTrace(-1, me, "CcScheduleReadAhead -> VOID\n", 0 );

    return;
}


VOID
FASTCALL
CcPerformReadAhead (
    IN PFILE_OBJECT FileObject
    )

/*++

Routine Description:

    This routine is called by the Lazy Writer to perform read ahead which
    has been scheduled for this file by CcScheduleReadAhead.

Arguments:

    FileObject - supplies pointer to FileObject on which readahead should be
                 considered.

Return Value:

    None
--*/

{
    KIRQL OldIrql;
    PSHARED_CACHE_MAP SharedCacheMap;
    PPRIVATE_CACHE_MAP PrivateCacheMap;
    ULONG i;
    LARGE_INTEGER ReadAheadOffset[2];
    ULONG ReadAheadLength[2];
    PCACHE_MANAGER_CALLBACKS Callbacks;
    PVOID Context;
    ULONG SavedState;
    BOOLEAN Done;
    BOOLEAN HitEof = FALSE;
    BOOLEAN ReadAheadPerformed = FALSE;
    BOOLEAN FaultOccurred = FALSE;
    PETHREAD Thread = PsGetCurrentThread();
    PVACB Vacb = NULL;

    DebugTrace(+1, me, "CcPerformReadAhead:\n", 0 );
    DebugTrace( 0, me, "    FileObject = %08lx\n", FileObject );

    MmSavePageFaultReadAhead( Thread, &SavedState );

    try {

        //
        //  Since we have the open count biased, we can safely access the
        //  SharedCacheMap.
        //

        SharedCacheMap = FileObject->SectionObjectPointer->SharedCacheMap;

        Callbacks = SharedCacheMap->Callbacks;
        Context = SharedCacheMap->LazyWriteContext;

        //
        //  After the first time, keep looping as long as there are new
        //  read ahead requirements.  (We will skip out below.)
        //

        while (TRUE) {

            //
            //  Get SharedCacheMap and PrivateCacheMap.  If either are now NULL, get
            //  out.
            //

            ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

            PrivateCacheMap = FileObject->PrivateCacheMap;

            //
            //  Now capture the information that we need, so that we can drop the
            //  SharedList Resource.  This information is advisory only anyway, and
            //  the caller must guarantee that the FileObject is referenced.
            //

            if (PrivateCacheMap != NULL) {

                ExAcquireSpinLockAtDpcLevel( &PrivateCacheMap->ReadAheadSpinLock );

                //
                //  We are done when the lengths are 0
                //

                Done = ((PrivateCacheMap->ReadAheadLength[0] |
                         PrivateCacheMap->ReadAheadLength[1]) == 0);

                ReadAheadOffset[0] = PrivateCacheMap->ReadAheadOffset[0];
                ReadAheadOffset[1] = PrivateCacheMap->ReadAheadOffset[1];
                ReadAheadLength[0] = PrivateCacheMap->ReadAheadLength[0];
                ReadAheadLength[1] = PrivateCacheMap->ReadAheadLength[1];
                PrivateCacheMap->ReadAheadLength[0] = 0;
                PrivateCacheMap->ReadAheadLength[1] = 0;

                ExReleaseSpinLockFromDpcLevel( &PrivateCacheMap->ReadAheadSpinLock );
            }

            ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

            //
            //  Acquire the file shared.
            //

            (*Callbacks->AcquireForReadAhead)( Context, TRUE );

            if ((PrivateCacheMap == NULL) || Done) {

                try_return( NOTHING );
            }

            //
            //  PERFORM READ AHEAD
            //
            //
            //  Now loop until everything is read in.  The Read ahead is accomplished
            //  by touching the pages with an appropriate ReadAhead parameter in MM.
            //

            i = 0;

            do {

                LARGE_INTEGER Offset, SavedOffset;
                ULONG Length, SavedLength;

                Offset = ReadAheadOffset[i];
                Length = ReadAheadLength[i];
                SavedOffset = Offset;
                SavedLength = Length;

                if ((Length != 0)

                        &&

                    ( Offset.QuadPart <= SharedCacheMap->FileSize.QuadPart )) {

                    ReadAheadPerformed = TRUE;

                    //
                    //  Keep length within file and MAX_READ_AHEAD
                    //

                    if ( ( Offset.QuadPart + (LONGLONG)Length ) >= SharedCacheMap->FileSize.QuadPart ) {

                        Length = (ULONG)( SharedCacheMap->FileSize.QuadPart - Offset.QuadPart );
                        HitEof = TRUE;

                    }
                    if (Length > MAX_READ_AHEAD) {
                        Length = MAX_READ_AHEAD;
                    }

                    //
                    //  Now loop to read all of the desired data in.  This loop
                    //  is more or less like the same loop to read data in
                    //  CcCopyRead, except that we do not copy anything, just
                    //  unmap as soon as it is in.
                    //

                    while (Length != 0) {

                        ULONG ReceivedLength;
                        PVOID CacheBuffer;
                        ULONG PagesToGo;

                        //
                        //  Call local routine to Map or Access the file data.
                        //  If we cannot map the data because of a Wait condition,
                        //  return FALSE.
                        //
                        //  Since this routine is intended to be called from
                        //  the finally handler from file system read modules,
                        //  it is imperative that it not raise any exceptions.
                        //  Therefore, if any expected exception is raised, we
                        //  will simply get out.
                        //

                        CacheBuffer = CcGetVirtualAddress( SharedCacheMap,
                                                           Offset,
                                                           &Vacb,
                                                           &ReceivedLength );

                        //
                        //  If we got more than we need, make sure to only transfer
                        //  the right amount.
                        //

                        if (ReceivedLength > Length) {
                            ReceivedLength = Length;
                        }

                        //
                        //  Now loop to touch all of the pages, calling MM to insure
                        //  that if we fault, we take in exactly the number of pages
                        //  we need.
                        //

                        PagesToGo = COMPUTE_PAGES_SPANNED( CacheBuffer,
                                                           ReceivedLength );

                        CcMissCounter = &CcReadAheadIos;

                        while (PagesToGo) {

                            MmSetPageFaultReadAhead( Thread, (PagesToGo - 1) );
                            FaultOccurred = (BOOLEAN)!MmCheckCachedPageState(CacheBuffer, FALSE);

                            CacheBuffer = (PCHAR)CacheBuffer + PAGE_SIZE;
                            PagesToGo -= 1;
                        }
                        CcMissCounter = &CcThrowAway;

                        //
                        //  Calculate how much data we have left to go.
                        //

                        Length -= ReceivedLength;

                        //
                        //  Assume we did not get all the data we wanted, and set
                        //  Offset to the end of the returned data.
                        //

                        Offset.QuadPart = Offset.QuadPart + (LONGLONG)ReceivedLength;

                        //
                        //  It was only a page, so we can just leave this loop
                        //  After freeing the address.
                        //

                        CcFreeVirtualAddress( Vacb );
                        Vacb = NULL;
                    }
                }
                i += 1;
            } while (i <= 1);

            //
            //  Release the file
            //

            (*Callbacks->ReleaseFromReadAhead)( Context );
        }

    try_exit: NOTHING;
    }
    finally {

        MmResetPageFaultReadAhead(Thread, SavedState);
        CcMissCounter = &CcThrowAway;

        //
        //  If we got an error faulting a single page in, release the Vacb
        //  here.  It is important to free any mapping before dropping the
        //  resource to prevent purge problems.
        //

        if (Vacb != NULL) {
            CcFreeVirtualAddress( Vacb );
        }

        //
        //  Release the file
        //

        (*Callbacks->ReleaseFromReadAhead)( Context );

        //
        //  To show we are done, we must make sure the PrivateCacheMap is
        //  still there.
        //

        ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

        PrivateCacheMap = FileObject->PrivateCacheMap;

        //
        //  Show readahead is going inactive.
        //

        if (PrivateCacheMap != NULL) {

            ExAcquireSpinLockAtDpcLevel( &PrivateCacheMap->ReadAheadSpinLock );
            PrivateCacheMap->ReadAheadActive = FALSE;

            //
            //  If he said sequential only and we smashed into Eof, then
            //  let's reset the highwater mark in case he wants to read the
            //  file sequentially again.
            //

            if (HitEof && FlagOn(FileObject->Flags, FO_SEQUENTIAL_ONLY)) {
                PrivateCacheMap->ReadAheadOffset[1].LowPart =
                PrivateCacheMap->ReadAheadOffset[1].HighPart = 0;
            }

            //
            //  If no faults occurred, turn read ahead off.
            //

            if (ReadAheadPerformed && !FaultOccurred) {
                PrivateCacheMap->ReadAheadEnabled = FALSE;
            }

            ExReleaseSpinLockFromDpcLevel( &PrivateCacheMap->ReadAheadSpinLock );
        }

        //
        //  Free SharedCacheMap list
        //

        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

        ObDereferenceObject( FileObject );

        //
        //  Serialize again to decrement the open count.
        //

        ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

        SharedCacheMap->OpenCount -= 1;

        if ((SharedCacheMap->OpenCount == 0) &&
            !FlagOn(SharedCacheMap->Flags, WRITE_QUEUED) &&
            (SharedCacheMap->DirtyPages == 0)) {

            //
            //  Move to the dirty list.
            //

            RemoveEntryList( &SharedCacheMap->SharedCacheMapLinks );
            InsertTailList( &CcDirtySharedCacheMapList.SharedCacheMapLinks,
                            &SharedCacheMap->SharedCacheMapLinks );

            //
            //  Make sure the Lazy Writer will wake up, because we
            //  want him to delete this SharedCacheMap.
            //

            LazyWriter.OtherWork = TRUE;
            if (!LazyWriter.ScanActive) {
                CcScheduleLazyWriteScan();
            }
        }

        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
    }

    DebugTrace(-1, me, "CcPerformReadAhead -> VOID\n", 0 );

    return;
}


VOID
CcSetDirtyInMask (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length
    )

/*++

Routine Description:

    This routine may be called to set a range of pages dirty in a user data
    file, by just setting the corresponding bits in the mask bcb.

Arguments:

    SharedCacheMap - SharedCacheMap where the pages are to be set dirty.

    FileOffset - FileOffset of first page to set dirty

    Length - Used in conjunction with FileOffset to determine how many pages
             to set dirty.

Return Value:

    None

--*/

{
    KIRQL OldIrql;
    PULONG MaskPtr;
    ULONG Mask;
    PMBCB Mbcb;
    ULONG FirstPage;
    ULONG LastPage;
    LARGE_INTEGER BeyondLastByte;

    //
    //  Here is the maximum size file supported by this implementation.
    //

    ASSERT((FileOffset->HighPart & ~(PAGE_SIZE - 1)) == 0);

    //
    //  Initialize our locals.
    //

    FirstPage = (ULONG)((FileOffset->LowPart >> PAGE_SHIFT) |
                        (FileOffset->HighPart << (32 - PAGE_SHIFT)));
    LastPage = FirstPage +
                     ((ULONG)((FileOffset->LowPart & (PAGE_SIZE - 1)) + Length - 1) >> PAGE_SHIFT);
    BeyondLastByte.LowPart = (LastPage + 1) << PAGE_SHIFT;
    BeyondLastByte.HighPart = (LONG)(LastPage >> (32 - PAGE_SHIFT));

    //
    //  We have to acquire the shared cache map list, because we
    //  may be changing lists.
    //

    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

    //
    //  If there is no Mbcb, or it is not big enough, we will have to allocate one.
    //

    Mbcb = SharedCacheMap->Mbcb;
    if ((Mbcb == NULL) || (LastPage >= (Mbcb->Bitmap.SizeOfBitMap - 1))) {

        PMBCB NewMbcb;
        ULONG RoundedBcbSize = ((sizeof(BCB) + 7) & ~7);
        ULONG SizeInBytes = ((LastPage + 1 + 1 + 7) / 8) + sizeof(MBCB);

        //
        //  If the size needed is not larger than a Bcb, then get one from the
        //  Bcb zone.
        //

        if (SizeInBytes <= RoundedBcbSize) {

            NewMbcb = (PMBCB)CcAllocateInitializeBcb( NULL, NULL, NULL, NULL );

            if (NewMbcb != NULL) {
                NewMbcb->Bitmap.SizeOfBitMap = (RoundedBcbSize - sizeof(MBCB)) * 8;
            } else {
                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                ExRaiseStatus( STATUS_INSUFFICIENT_RESOURCES );
            }

        //
        //  Otherwise, we will allocate one from the pool.  We throw in a fudge
        //  factor of 1 below to account for any bits that may shift off the end,
        //  plus 4 to insure a long word of 0's at the end for scanning, and then
        //  round up to a quad word boundary that we will get anyway.
        //

        } else {

            ULONG SizeToAllocate = (ULONG)(((SharedCacheMap->SectionSize.LowPart >> (PAGE_SHIFT + 3)) |
                                           (SharedCacheMap->SectionSize.HighPart << (32 - (PAGE_SHIFT + 3)))) +
                                   sizeof(MBCB) + 1 + 7) & ~7;

            NewMbcb = ExAllocatePool( NonPagedPool, SizeToAllocate );

            if (NewMbcb != NULL) {
                RtlZeroMemory( NewMbcb, SizeToAllocate );
                NewMbcb->Bitmap.SizeOfBitMap = (SizeToAllocate - sizeof(MBCB)) * 8;
            } else {
                ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
                ExRaiseStatus( STATUS_INSUFFICIENT_RESOURCES );
            }
        }

        //
        //  Set in the node type, "empty" FirstDirtyPage state, and the address
        //  of the bitmap.
        //

        NewMbcb->NodeTypeCode = CACHE_NTC_MBCB;
        NewMbcb->FirstDirtyPage = MAXULONG;
        NewMbcb->Bitmap.Buffer = (PULONG)(NewMbcb + 1);

        //
        //  If there already was an Mbcb, we need to copy the relevant data from
        //  it and deallocate it.
        //

        if (Mbcb != NULL) {

            NewMbcb->DirtyPages = Mbcb->DirtyPages;
            NewMbcb->FirstDirtyPage = Mbcb->FirstDirtyPage;
            NewMbcb->LastDirtyPage = Mbcb->LastDirtyPage;
            NewMbcb->ResumeWritePage = Mbcb->ResumeWritePage;
            RtlCopyMemory( NewMbcb + 1, Mbcb + 1, Mbcb->Bitmap.SizeOfBitMap / 8 );

            CcDeallocateBcb( (PBCB)Mbcb );
        }

        //
        //  Finally, set to use our new Mbcb.
        //

        SharedCacheMap->Mbcb = Mbcb = NewMbcb;
    }

    //
    //  If this is the first dirty page for this cache map, there is some work
    //  to do.
    //

    if (SharedCacheMap->DirtyPages == 0) {

        //
        //  If the lazy write scan is not active, then start it.
        //

        if (!LazyWriter.ScanActive) {
            CcScheduleLazyWriteScan();
        }

        //
        //  Move to the dirty list.
        //

        RemoveEntryList( &SharedCacheMap->SharedCacheMapLinks );
        InsertTailList( &CcDirtySharedCacheMapList.SharedCacheMapLinks,
                        &SharedCacheMap->SharedCacheMapLinks );

        Mbcb->ResumeWritePage = FirstPage;
    }

    //
    //  Now update the first and last dirty page indices and the bitmap.
    //

    if (FirstPage < Mbcb->FirstDirtyPage) {
        Mbcb->FirstDirtyPage = FirstPage;
    }

    if (LastPage > Mbcb->LastDirtyPage) {
        Mbcb->LastDirtyPage = LastPage;
    }

    MaskPtr = &Mbcb->Bitmap.Buffer[FirstPage / 32];
    Mask = 1 << (FirstPage % 32);

    //
    //  Loop to set all of the bits and adjust the DirtyPage totals.
    //

    for ( ; FirstPage <= LastPage; FirstPage++) {

        if ((*MaskPtr & Mask) == 0) {

            CcTotalDirtyPages += 1;
            SharedCacheMap->DirtyPages += 1;
            Mbcb->DirtyPages += 1;
            *MaskPtr |= Mask;
        }

        Mask <<= 1;

        if (Mask == 0) {

            MaskPtr += 1;
            Mask = 1;
        }
    }

    //
    //  See if we need to advance our goal for ValidDataLength.
    //

    BeyondLastByte.QuadPart = FileOffset->QuadPart + (LONGLONG)Length;

    if ( BeyondLastByte.QuadPart > SharedCacheMap->ValidDataGoal.QuadPart ) {

        SharedCacheMap->ValidDataGoal = BeyondLastByte;
    }

    ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
}


VOID
CcSetDirtyPinnedData (
    IN PVOID BcbVoid,
    IN PLARGE_INTEGER Lsn OPTIONAL
    )

/*++

Routine Description:

    This routine may be called to set a Bcb (returned by CcPinFileData)
    dirty, and a candidate for the Lazy Writer.  All Bcbs should be set
    dirty by calling this routine, even if they are to be flushed
    another way.

Arguments:

    Bcb - Supplies a pointer to a pinned (by CcPinFileData) Bcb, to
          be set dirty.

    Lsn - Lsn to be remembered with page.

Return Value:

    None

--*/

{
    PBCB Bcbs[2];
    PBCB *BcbPtrPtr;
    KIRQL OldIrql;
    PSHARED_CACHE_MAP SharedCacheMap;

    DebugTrace(+1, me, "CcSetDirtyPinnedData: Bcb = %08lx\n", BcbVoid );

    //
    //  Assume this is a normal Bcb, and set up for loop below.
    //

    Bcbs[0] = (PBCB)BcbVoid;
    Bcbs[1] = NULL;
    BcbPtrPtr = &Bcbs[0];

    //
    //  If it is an overlap Bcb, then point into the Bcb vector
    //  for the loop.
    //

    if (Bcbs[0]->NodeTypeCode == CACHE_NTC_OBCB) {
        BcbPtrPtr = &((POBCB)Bcbs[0])->Bcbs[0];
    }

    //
    //  Loop to set all Bcbs dirty
    //

    while (*BcbPtrPtr != NULL) {

        Bcbs[0] = *(BcbPtrPtr++);

        //
        //  Should be no ReadOnly Bcbs
        //

        ASSERT(((ULONG)Bcbs[0] & 1) != 1);

        SharedCacheMap = Bcbs[0]->SharedCacheMap;

        //
        //  We have to acquire the shared cache map list, because we
        //  may be changing lists.
        //

        ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

        if (!Bcbs[0]->Dirty) {

            ULONG Pages = Bcbs[0]->ByteLength >> PAGE_SHIFT;

            //
            //  Set dirty to keep the Bcb from going away until
            //  it is set Undirty, and assign the next modification time stamp.
            //

            Bcbs[0]->Dirty = TRUE;

            //
            //  Initialize the OldestLsn field.
            //

            if (ARGUMENT_PRESENT(Lsn)) {
                Bcbs[0]->OldestLsn = *Lsn;
                Bcbs[0]->NewestLsn = *Lsn;
            }

            //
            //  Move it to the dirty list if these are the first dirty pages,
            //  and this is not disabled for write behind.
            //
            //  Increase the count of dirty bytes in the shared cache map.
            //

            if ((SharedCacheMap->DirtyPages == 0) &&
                !FlagOn(SharedCacheMap->Flags, DISABLE_WRITE_BEHIND)) {

                //
                //  If the lazy write scan is not active, then start it.
                //

                if (!LazyWriter.ScanActive) {
                    CcScheduleLazyWriteScan();
                }

                RemoveEntryList( &SharedCacheMap->SharedCacheMapLinks );
                InsertTailList( &CcDirtySharedCacheMapList.SharedCacheMapLinks,
                                &SharedCacheMap->SharedCacheMapLinks );
            }

            SharedCacheMap->DirtyPages += Pages;
            CcTotalDirtyPages += Pages;
        }

        //
        //  If this Lsn happens to be older/newer than the ones we have stored, then
        //  change it.
        //

        if (ARGUMENT_PRESENT(Lsn)) {

            if ((Bcbs[0]->OldestLsn.QuadPart == 0) || (Lsn->QuadPart < Bcbs[0]->OldestLsn.QuadPart)) {
                Bcbs[0]->OldestLsn = *Lsn;
            }

            if (Lsn->QuadPart > Bcbs[0]->NewestLsn.QuadPart) {
                Bcbs[0]->NewestLsn = *Lsn;
            }
        }

        //
        //  See if we need to advance our goal for ValidDataLength.
        //

        if ( Bcbs[0]->BeyondLastByte.QuadPart > SharedCacheMap->ValidDataGoal.QuadPart ) {

            SharedCacheMap->ValidDataGoal = Bcbs[0]->BeyondLastByte;
        }

        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
    }

    DebugTrace(-1, me, "CcSetDirtyPinnedData -> VOID\n", 0 );
}


NTSTATUS
CcSetValidData(
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER ValidDataLength
    )

/*++

Routine Description:

    This routine is used to call the File System to update ValidDataLength
    for a file.

Arguments:

    FileObject - A pointer to a referenced file object describing which file
        the read should be performed from.

    ValidDataLength - Pointer to new ValidDataLength.

Return Value:

    Status of operation.

--*/

{
    PIO_STACK_LOCATION IrpSp;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;
    FILE_END_OF_FILE_INFORMATION Buffer;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;

    DebugTrace(+1, me, "CcSetValidData:\n", 0 );
    DebugTrace( 0, me, "    FileObject = %08lx\n", FileObject );
    DebugTrace2(0, me, "    ValidDataLength = %08lx, %08lx\n",
                ValidDataLength->LowPart, ValidDataLength->HighPart );

    //
    //  Copy ValidDataLength to our buffer.
    //

    Buffer.EndOfFile = *ValidDataLength;

    //
    //  Initialize the event.
    //

    KeInitializeEvent( &Event, NotificationEvent, FALSE );

    //
    //  Begin by getting a pointer to the device object that the file resides
    //  on.
    //

    DeviceObject = IoGetRelatedDeviceObject( FileObject );

    //
    //  Allocate an I/O Request Packet (IRP) for this in-page operation.
    //

    Irp = IoAllocateIrp( DeviceObject->StackSize, FALSE );
    if (Irp == NULL) {

        DebugTrace(-1, me, "CcSetValidData-> STATUS_INSUFFICIENT_RESOURCES\n", 0 );

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    //  Get a pointer to the first stack location in the packet.  This location
    //  will be used to pass the function codes and parameters to the first
    //  driver.
    //

    IrpSp = IoGetNextIrpStackLocation( Irp );

    //
    //  Fill in the IRP according to this request, setting the flags to
    //  just cause IO to set the event and deallocate the Irp.
    //

    Irp->Flags = IRP_PAGING_IO | IRP_SYNCHRONOUS_PAGING_IO;
    Irp->RequestorMode = KernelMode;
    Irp->UserIosb = &IoStatus;
    Irp->UserEvent = &Event;
    Irp->Tail.Overlay.OriginalFileObject = FileObject;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->AssociatedIrp.SystemBuffer = &Buffer;

    //
    //  Fill in the normal read parameters.
    //

    IrpSp->MajorFunction = IRP_MJ_SET_INFORMATION;
    IrpSp->FileObject = FileObject;
    IrpSp->DeviceObject = DeviceObject;
    IrpSp->Parameters.SetFile.Length = sizeof(FILE_END_OF_FILE_INFORMATION);
    IrpSp->Parameters.SetFile.FileInformationClass = FileEndOfFileInformation;
    IrpSp->Parameters.SetFile.FileObject = NULL;
    IrpSp->Parameters.SetFile.AdvanceOnly = TRUE;

    //
    //  Queue the packet to the appropriate driver based on whether or not there
    //  is a VPB associated with the device.  This routine should not raise.
    //

    Status = IoCallDriver( DeviceObject, Irp );

    //
    //  If pending is returned (which is a successful status),
    //  we must wait for the request to complete.
    //

    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject( &Event,
                               Executive,
                               KernelMode,
                               FALSE,
                               (PLARGE_INTEGER)NULL);
    }

    //
    //  If we got an error back in Status, then the Iosb
    //  was not written, so we will just copy the status
    //  there, then test the final status after that.
    //

    if (!NT_SUCCESS(Status)) {
        IoStatus.Status = Status;
    }

    DebugTrace(-1, me, "CcSetValidData-> %08lx\n", IoStatus.Status );

    return IoStatus.Status;
}


//
//  Internal Support Routine
//

BOOLEAN
CcAcquireByteRangeForWrite (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER TargetOffset OPTIONAL,
    IN ULONG TargetLength,
    OUT PLARGE_INTEGER FileOffset,
    OUT PULONG Length,
    OUT PBCB *FirstBcb
    )

/*++

Routine Description:

    This routine is called by the Lazy Writer to try to find a contiguous
    range of bytes from the specified SharedCacheMap that are dirty and
    should be flushed.  After flushing, these bytes should be released
    by calling CcReleaseByteRangeFromWrite.

Arguments:

    SharedCacheMap - for the file for which the dirty byte range is sought

    TargetOffset - If specified, then only the specified range is
                   to be flushed.

    TargetLength - If target offset specified, this completes the range.
                   In any case, this field is zero for the Lazy Writer,
                   and nonzero for explicit flush calls.

    FileOffset - Returns the offset for the beginning of the dirty byte
                 range to flush

    Length - Returns the length of bytes in the range.

    FirstBcb - Returns the first Bcb in the list for the range, to be used
               when calling CcReleaseByteRangeFromWrite, or NULL if dirty
               pages were found in the mask Bcb.

Return Value:

    FALSE - if no dirty byte range could be found to match the necessary
            criteria.

    TRUE - if a dirty byte range is being returned.

--*/

{
    KIRQL OldIrql;
    PMBCB Mbcb;
    PBCB Bcb;
    LARGE_INTEGER LsnToFlushTo = {0, 0};

    DebugTrace(+1, me, "CcAcquireByteRangeForWrite:\n", 0);
    DebugTrace( 0, me, "    SharedCacheMap = %08lx\n", SharedCacheMap);

    //
    //  Initially clear outputs.
    //

    FileOffset->QuadPart = 0;
    *Length = 0;

    //
    //  We must acquire the CcMasterSpinLock.
    //

    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

    //
    //  See if there is a simple Mask Bcb, and if there is anything dirty in
    //  it.  If so we will simply handle that case here by processing the bitmap.
    //

    Mbcb = SharedCacheMap->Mbcb;

    if ((Mbcb != NULL) &&
        (Mbcb->DirtyPages != 0) &&
        ((Mbcb->PagesToWrite != 0) || (TargetLength != 0))) {

        PULONG EndPtr;
        PULONG MaskPtr;
        ULONG Mask;
        ULONG FirstDirtyPage;
        ULONG OriginalFirstDirtyPage;

        //
        //  If a target range was specified (outside call to CcFlush for a range),
        //  then calculate FirstPage and EndPtr based on these inputs.
        //

        if (ARGUMENT_PRESENT(TargetOffset)) {

            FirstDirtyPage = (ULONG)(TargetOffset->QuadPart >> PAGE_SHIFT);
            EndPtr = &Mbcb->Bitmap.Buffer[(ULONG)((TargetOffset->QuadPart + TargetLength - 1) >> PAGE_SHIFT) / 32];

            //
            //  We do not grow the bitmap with the file, only as we set dirty
            //  pages, so it is possible that the caller is off the end.  If
            //  If even the first page is off the end, we will catch it below.
            //

            if (EndPtr > &Mbcb->Bitmap.Buffer[Mbcb->LastDirtyPage / 32]) {

                EndPtr = &Mbcb->Bitmap.Buffer[Mbcb->LastDirtyPage / 32];
            }

        //
        //  Otherwise, for the Lazy Writer pick up where we left off.
        //

        } else {

            //
            //  If a length was specified, then it is an explicit flush, and
            //  we want to start with the first dirty page.
            //

            FirstDirtyPage = Mbcb->FirstDirtyPage;

            //
            //  Otherwise, it is the Lazy Writer, so pick up at the resume
            //  point so long as that is beyond the FirstDirtyPage.
            //

            if ((TargetLength == 0) && (Mbcb->ResumeWritePage >= FirstDirtyPage)) {
                FirstDirtyPage = Mbcb->ResumeWritePage;
            }
            EndPtr = &Mbcb->Bitmap.Buffer[Mbcb->LastDirtyPage / 32];
        }

        //
        //  Form a few other inputs for our dirty page scan.
        //

        MaskPtr = &Mbcb->Bitmap.Buffer[FirstDirtyPage / 32];
        Mask = (ULONG)(-1 << (FirstDirtyPage % 32));
        OriginalFirstDirtyPage = FirstDirtyPage;

        //
        //  Because of the possibility of getting stuck on a "hot spot" which gets
        //  modified over and over, we want to be very careful to resume exactly
        //  at the recorded resume point.  If there is nothing there, then we
        //  fall into the loop below to scan for nozero long words in the bitmap,
        //  starting at the next longword.
        //

        if ((MaskPtr > EndPtr) || (*MaskPtr & Mask) == 0) {

            MaskPtr += 1;
            Mask = (ULONG)-1;
            FirstDirtyPage = (FirstDirtyPage + 32) & ~31;

            //
            //  If we go beyond the end, then we must wrap back to the first
            //  dirty page.  We will just go back to the start of the first
            //  longword.
            //

            if (MaskPtr > EndPtr) {

                //
                //  If this is an explicit flush, get out when we hit the end
                //  of the range.
                //

                if (TargetLength != 0) {

                    goto Scan_Bcbs;
                }

                MaskPtr = &Mbcb->Bitmap.Buffer[Mbcb->FirstDirtyPage / 32];
                FirstDirtyPage = Mbcb->FirstDirtyPage & ~31;
                OriginalFirstDirtyPage = Mbcb->FirstDirtyPage;

                //
                //  We can also backup the last dirty page hint to our
                //  resume point.
                //

                ASSERT(Mbcb->ResumeWritePage >= Mbcb->FirstDirtyPage);

                Mbcb->LastDirtyPage = Mbcb->ResumeWritePage - 1;
            }

            //
            //  To scan the bitmap faster, we scan for entire long words which are
            //  nonzero.
            //

            while (*MaskPtr == 0) {

                MaskPtr += 1;
                FirstDirtyPage += 32;

                //
                //  If we go beyond the end, then we must wrap back to the first
                //  dirty page.  We will just go back to the start of the first
                //  longword.
                //

                if (MaskPtr > EndPtr) {

                    //
                    //  If this is an explicit flush, get out when we hit the end
                    //  of the range.
                    //

                    if (TargetLength != 0) {

                        goto Scan_Bcbs;
                    }

                    MaskPtr = &Mbcb->Bitmap.Buffer[Mbcb->FirstDirtyPage / 32];
                    FirstDirtyPage = Mbcb->FirstDirtyPage & ~31;
                    OriginalFirstDirtyPage = Mbcb->FirstDirtyPage;

                    //
                    //  We can also backup the last dirty page hint to our
                    //  resume point.
                    //

                    ASSERT(Mbcb->ResumeWritePage >= Mbcb->FirstDirtyPage);

                    Mbcb->LastDirtyPage = Mbcb->ResumeWritePage - 1;
                }
            }
        }

        //
        //  Calculate the first set bit in the mask that we hit on.
        //

        Mask = ~Mask + 1;

        //
        //  Now loop to find the first set bit.
        //

        while ((*MaskPtr & Mask) == 0) {

            Mask <<= 1;
            FirstDirtyPage += 1;
        }

        //
        //  If a TargetOffset was specified, then make sure we do not start
        //  beyond the specified range.
        //

        if (ARGUMENT_PRESENT(TargetOffset)  &&
            (FirstDirtyPage >= ((TargetOffset->QuadPart + TargetLength + PAGE_SIZE - 1) >> PAGE_SHIFT))) {

            goto Scan_Bcbs;
        }

        //
        //  Now loop to count the set bits at that point, clearing them as we
        //  go because we plan to write the corresponding pages.  Stop as soon
        //  as we find a clean page, or we reach our maximum write size.  Of
        //  course we want to ignore long word boundaries and keep trying to
        //  extend the write.  We do not check for wrapping around the end of
        //  the bitmap here, because we guarantee some zero bits at the end
        //  in CcSetDirtyInMask.
        //

        while (((*MaskPtr & Mask) != 0) && (*Length < (MAX_WRITE_BEHIND / PAGE_SIZE)) &&
               (!ARGUMENT_PRESENT(TargetOffset) || ((FirstDirtyPage + *Length) <
                                                    (ULONG)((TargetOffset->QuadPart + TargetLength + PAGE_SIZE - 1) >> PAGE_SHIFT)))) {

            ASSERT(MaskPtr <= (&Mbcb->Bitmap.Buffer[Mbcb->LastDirtyPage / 32]));

            *MaskPtr -= Mask;
            *Length += 1;
            Mask <<= 1;

            if (Mask == 0) {

                MaskPtr += 1;
                Mask = 1;

                if (MaskPtr > EndPtr) {
                    break;
                }
            }
        }

        //
        //  Now reduce the count of pages we were supposed to write this time,
        //  possibly clearing this count.
        //

        if (*Length < Mbcb->PagesToWrite) {

            Mbcb->PagesToWrite -= *Length;

        } else {

            Mbcb->PagesToWrite = 0;
        }

        //
        //  Reduce the dirty page counts by the number of pages we just cleared.
        //

        ASSERT(Mbcb->DirtyPages >= *Length);

        CcTotalDirtyPages -= *Length;
        SharedCacheMap->DirtyPages -= *Length;
        Mbcb->DirtyPages -= *Length;

        //
        //  Normally we need to reduce CcPagesYetToWrite appropriately.
        //

        if (CcPagesYetToWrite > *Length) {
            CcPagesYetToWrite -= *Length;
        } else {
            CcPagesYetToWrite = 0;
        }

        //
        //  If we took out the last dirty page, then move the SharedCacheMap
        //  back to the clean list.
        //

        if (SharedCacheMap->DirtyPages == 0) {

            RemoveEntryList( &SharedCacheMap->SharedCacheMapLinks );
            InsertTailList( &CcCleanSharedCacheMapList,
                            &SharedCacheMap->SharedCacheMapLinks );
        }

        //
        //  If the number of dirty pages for the Mcb went to zero, we can reset
        //  our hint fields now.
        //

        if (Mbcb->DirtyPages == 0) {

            Mbcb->FirstDirtyPage = MAXULONG;
            Mbcb->LastDirtyPage = 0;
            Mbcb->ResumeWritePage = 0;

        //
        //  Otherwise we have to update the hint fields.
        //

        } else {

            //
            //  Advance the first dirty page hint if we can.
            //

            if (Mbcb->FirstDirtyPage == OriginalFirstDirtyPage) {

                Mbcb->FirstDirtyPage = FirstDirtyPage + *Length;
            }

            //
            //  Set to resume the next scan at the next bit for
            //  the Lazy Writer.
            //

            if (TargetLength == 0) {

                Mbcb->ResumeWritePage = FirstDirtyPage + *Length;
            }
        }

        //
        //  We can save a callback by letting our caller know when
        //  we have no more pages to write.
        //

        if (IsListEmpty(&SharedCacheMap->BcbList)) {
            SharedCacheMap->PagesToWrite = Mbcb->PagesToWrite;
        }

        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

        //
        //  Now form all of our outputs.  We calculated *Length as a page count,
        //  but our caller wants it in bytes.
        //

        *Length <<= PAGE_SHIFT;
        FileOffset->QuadPart = (LONGLONG)FirstDirtyPage << PAGE_SHIFT;
        *FirstBcb = NULL;

        DebugTrace2(0, me, "    <FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                               FileOffset->HighPart );
        DebugTrace( 0, me, "    <Length = %08lx\n", *Length );
        DebugTrace(-1, me, "CcAcquireByteRangeForWrite -> TRUE\n", 0 );

        return TRUE;
    }

    //
    //  We get here if there is no Mbcb or no dirty pages in it.  Note that we
    //  wouldn't even be here if there were no dirty pages in this SharedCacheMap.
    //

    //
    //  Now point to last Bcb in List, and loop until we hit one of the
    //  breaks below or the beginning of the list.
    //

Scan_Bcbs:

    //
    //  Use while TRUE to handle case where the current target range wraps
    //  (escape is at the bottom).
    //

    while (TRUE) {

        Bcb = CONTAINING_RECORD( SharedCacheMap->BcbList.Blink, BCB, BcbLinks );

        //
        //  If this is a large file, and we are to resume from a nonzero FileOffset,
        //  call CcFindBcb to get a quicker start.
        //

        if ((SharedCacheMap->SectionSize.QuadPart > BEGIN_BCB_LIST_ARRAY) &&
            !ARGUMENT_PRESENT(TargetOffset) &&
            (SharedCacheMap->BeyondLastFlush != 0)) {

            LARGE_INTEGER TempQ;

            TempQ.QuadPart = SharedCacheMap->BeyondLastFlush + PAGE_SIZE;

            //
            //  Position ourselves.  If we did not find a Bcb for the BeyondLastFlush
            //  page, then a lower FileOffset was returned, so we want to move forward
            //  one.
            //

            if (!CcFindBcb( SharedCacheMap,
                            (PLARGE_INTEGER)&SharedCacheMap->BeyondLastFlush,
                            &TempQ,
                            &Bcb )) {
                Bcb = CONTAINING_RECORD( Bcb->BcbLinks.Blink, BCB, BcbLinks );
            }
        }

        while (&Bcb->BcbLinks != &SharedCacheMap->BcbList) {

            //
            //  Skip over this item if it is a listhead.
            //

            if (Bcb->NodeTypeCode != CACHE_NTC_BCB) {

                Bcb = CONTAINING_RECORD( Bcb->BcbLinks.Blink, BCB, BcbLinks );
                continue;
            }

            //
            //  If we are doing a specified range, then get out if we hit a
            //  higher Bcb.
            //

            if (ARGUMENT_PRESENT(TargetOffset) &&
                ((TargetOffset->QuadPart + TargetLength) <= Bcb->FileOffset.QuadPart)) {

                break;
            }

            //
            //  If we have not started a run, then see if this Bcb is a candidate
            //  to start one.
            //

            if (*Length == 0) {

                //
                //  Else see if the Bcb is dirty, and is in our specified range, if
                //  there is one.
                //

                if (!Bcb->Dirty ||
                    (ARGUMENT_PRESENT(TargetOffset) && (TargetOffset->QuadPart >= Bcb->BeyondLastByte.QuadPart)) ||
                    (!ARGUMENT_PRESENT(TargetOffset) && (Bcb->FileOffset.QuadPart < SharedCacheMap->BeyondLastFlush))) {

                    Bcb = CONTAINING_RECORD( Bcb->BcbLinks.Blink, BCB, BcbLinks );
                    continue;
                }
            }

            //
            //  Else, if we have started a run, then if this guy cannot be
            //  appended to the run, then break.  Note that we ignore the
            //  Bcb's modification time stamp here to simplify the test.
            //
            //  If the Bcb is currently pinned, then there is no sense in causing
            //  contention, so we will skip over this guy as well.
            //

            else {
                if (!Bcb->Dirty || ( Bcb->FileOffset.QuadPart != ( FileOffset->QuadPart + (LONGLONG)*Length))
                    || (*Length + Bcb->ByteLength > MAX_WRITE_BEHIND)
                    || (Bcb->PinCount != 0)) {

                    break;
                }
            }

            //
            //  Increment PinCount to prevent Bcb from going away once the
            //  SpinLock is released, or we set it clean for the case where
            //  modified write is allowed.
            //

            Bcb->PinCount += 1;

            //
            //  Release the SpinLock before waiting on the resource.
            //

            ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

            if (FlagOn(SharedCacheMap->Flags, MODIFIED_WRITE_DISABLED) &&
                !FlagOn(SharedCacheMap->Flags, DISABLE_WRITE_BEHIND)) {

                //
                //  Now acquire the Bcb exclusive, so that we know that nobody
                //  has it pinned and thus no one can be modifying the described
                //  buffer.  To acquire the first Bcb in a run, we can afford
                //  to wait, because we are not holding any resources.  However
                //  if we already have a Bcb, then we better not wait, because
                //  someone could have this Bcb pinned, and then wait for the
                //  Bcb we already have exclusive.
                //
                //  For streams for which we have not disabled modified page
                //  writing, we do not need to acquire this resource, and the
                //  foreground processing will not be acquiring the Bcb either.
                //

                if (!ExAcquireResourceExclusive( &Bcb->Resource,
                                                 (BOOLEAN)(*Length == 0) )) {

                    DebugTrace( 0, me, "Could not acquire 2nd Bcb\n", 0 );

                    //
                    //  Release the Bcb count we took out above.  We say
                    //  ReadOnly = TRUE since we do not own the resource,
                    //  and SetClean = FALSE because we just want to decement
                    //  the count.
                    //

                    CcUnpinFileData( Bcb, TRUE, UNPIN );

                    //
                    //  When we leave the loop, we have to have the spin lock
                    //

                    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );
                    break;
                }

                ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

                //
                //  If someone has the file open WriteThrough, then the Bcb may no
                //  longer be dirty.  If so, call CcUnpinFileData to decrement the
                //  PinCount we incremented and free the resource.
                //

                if (!Bcb->Dirty) {

                    //
                    //  Release the spinlock so that we can call CcUnpinFileData
                    //

                    ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

                    CcUnpinFileData( Bcb, FALSE, UNPIN );

                    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

                    //
                    //  Now if we already have some data we can just break to return
                    //  it, otherwise we have to restart the scan, since our Bcb
                    //  may have gone away.
                    //

                    if (*Length != 0) {
                        break;
                    }
                    else {

                        Bcb = CONTAINING_RECORD( SharedCacheMap->BcbList.Blink, BCB, BcbLinks );
                        continue;
                    }
                }

            //
            //  If we are not in the disable modified write mode (normal user data)
            //  then we must set the buffer clean before doing the write, since we
            //  are unsynchronized with anyone producing dirty data.  That way if we,
            //  for example, are writing data out while it is actively being changed,
            //  at least the changer will mark the buffer dirty afterwards and cause
            //  us to write it again later.
            //

            } else {

                CcUnpinFileData( Bcb, TRUE, SET_CLEAN );

                ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );
            }

            DebugTrace( 0, me, "Adding Bcb = %08lx to run\n", Bcb );

            //
            //  Update all of our return values.  Note that FirstBcb refers to the
            //  FirstBcb in terms of how the Bcb list is ordered.  Since the Bcb list
            //  is ordered by descending file offsets, FirstBcb will actually return
            //  the Bcb with the highest FileOffset.
            //

            if (*Length == 0) {
                *FileOffset = Bcb->FileOffset;
            }
            *FirstBcb = Bcb;
            *Length += Bcb->ByteLength;

            //
            //  If there is a log file flush callback for this stream, then we must
            //  remember the largest Lsn we are about to flush.
            //

            if ((SharedCacheMap->FlushToLsnRoutine != NULL) &&
                (Bcb->NewestLsn.QuadPart > LsnToFlushTo.QuadPart)) {

                LsnToFlushTo = Bcb->NewestLsn;
            }

            Bcb = CONTAINING_RECORD( Bcb->BcbLinks.Blink, BCB, BcbLinks );
        }

        //
        //  If we found something, update our range last flush range and reduce
        //  PagesToWrite.
        //

        if (*Length != 0) {

            //
            //  If this is the Lazy Writer, then update BeyondLastFlush and
            //  the PagesToWrite target.
            //

            if (!ARGUMENT_PRESENT(TargetOffset)) {

                SharedCacheMap->BeyondLastFlush = FileOffset->QuadPart + *Length;

                if (SharedCacheMap->PagesToWrite > (*Length >> PAGE_SHIFT)) {
                    SharedCacheMap->PagesToWrite -= (*Length >> PAGE_SHIFT);
                } else {
                    SharedCacheMap->PagesToWrite = 0;
                }
            }

            break;

        //
        //  Else, if we scanned the entire file, get out - nothing to write now.
        //

        } else if ((SharedCacheMap->BeyondLastFlush == 0) || ARGUMENT_PRESENT(TargetOffset)) {
            break;
        }

        //
        //  Otherwise, we may have not found anything because there is nothing
        //  beyond the last flush.  In that case it is time to wrap back to 0
        //  and keep scanning.
        //

        SharedCacheMap->BeyondLastFlush = 0;
    }



    //
    //  Now release the spinlock file while we go off and do the I/O
    //

    ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

    //
    //  If we need to flush to some Lsn, this is the time to do it now
    //  that we have found the largest Lsn and freed the spin lock.
    //

    if (LsnToFlushTo.QuadPart != 0) {

        try {

            (*SharedCacheMap->FlushToLsnRoutine) ( SharedCacheMap->LogHandle,
                                                   LsnToFlushTo );
        } except( CcExceptionFilter( GetExceptionCode() )) {

            //
            //  If there was an error, it will be raised.  We cannot
            //  write anything until we successfully flush the log
            //  file, so we will release everything here and just
            //  return with 0 bytes.
            //

            LARGE_INTEGER LastOffset;
            PBCB NextBcb;

            //
            //  Now loop to free up all of the Bcbs.  Set the time
            //  stamps to 0, so that we are guaranteed to try to
            //  flush them again on the next sweep.
            //

            do {
                NextBcb = CONTAINING_RECORD( (*FirstBcb)->BcbLinks.Flink, BCB, BcbLinks );

                //
                //  Skip over any listheads.
                //

                if ((*FirstBcb)->NodeTypeCode == CACHE_NTC_BCB) {

                    LastOffset = (*FirstBcb)->FileOffset;

                    CcUnpinFileData( *FirstBcb, FALSE, UNPIN );
                }

                *FirstBcb = NextBcb;
            } while (FileOffset->QuadPart != LastOffset.QuadPart);

            //
            //  Show we did not acquire anything.
            //

            *Length = 0;
        }
    }

    //
    //  If we got anything, return TRUE.
    //

    DebugTrace2(0, me, "    <FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                           FileOffset->HighPart );
    DebugTrace( 0, me, "    <Length = %08lx\n", *Length );
    DebugTrace(-1, me, "CcAcquireByteRangeForWrite -> %02lx\n", *Length != 0 );

    return ((BOOLEAN)(*Length != 0));
}


//
//  Internal Support Routine
//

VOID
CcReleaseByteRangeFromWrite (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN PBCB FirstBcb,
    IN BOOLEAN VerifyRequired
    )

/*++

Routine Description:

    This routine is called by the Lazy Writer to free a range of bytes and
    clear all dirty bits, for a byte range returned by CcAcquireByteRangeForWrite.

Arguments:

    SharedCacheMap - As supplied to CcAcquireByteRangeForWrite

    FileOffset - As returned from CcAcquireByteRangeForWrite

    Length - As returned from CcAcquirebyteRangeForWrite

    FirstBcb - As returned from CcAcquireByteRangeForWrite

    VerifyRequired - supplied as TRUE if a verify required error was received.
                     In this case we must mark/leave the data dirty so that
                     we will try to write it again.

Return Value:

    None

--*/

{
    LARGE_INTEGER LastOffset;
    PBCB NextBcb;

    DebugTrace(+1, me, "CcReleaseByteRangeFromWrite:\n", 0);
    DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                          FileOffset->HighPart );

    //
    //  If it is a mask Mbcb we are getting, then we only have to check
    //  for VerifyRequired.
    //

    if (FirstBcb == NULL) {

        ASSERT(Length != 0);

        if (VerifyRequired) {
            CcSetDirtyInMask( SharedCacheMap, FileOffset, Length );
        }

        DebugTrace(-1, me, "CcReleaseByteRangeFromWrite -> VOID\n", 0);

        return;
    }

    //
    //  Now loop to free up all of the Bcbs.  If modified writing is disabled
    //  for each Bcb, then we are to set it clean here, since we are synchronized
    //  with callers who set the data dirty.  Otherwise we only have the Bcb pinned
    //  so it will not go away, and we only unpin it here.
    //

    do {
        NextBcb = CONTAINING_RECORD( FirstBcb->BcbLinks.Flink, BCB, BcbLinks );

        //
        //  Skip over any listheads.
        //

        if (FirstBcb->NodeTypeCode == CACHE_NTC_BCB) {

            LastOffset = FirstBcb->FileOffset;

            //
            //  If this is file system metadata (we disabled modified writing),
            //  then this is the time to mark the buffer clean, so long as we
            //  did not get verify required.
            //

            if (FlagOn(SharedCacheMap->Flags, MODIFIED_WRITE_DISABLED)) {

                CcUnpinFileData( FirstBcb,
                                 BooleanFlagOn(SharedCacheMap->Flags, DISABLE_WRITE_BEHIND),
                                 SET_CLEAN );
            }

            //
            //  If we got verify required, we have to mark the buffer dirty again
            //  so we will try again later.  Note we have to make this call again
            //  to make sure the right thing happens with time stamps.
            //

            if (VerifyRequired) {
                CcSetDirtyPinnedData( FirstBcb, NULL );
            }

            //
            //  Finally remove a pin count left over from CcAcquireByteRangeForWrite.
            //

            CcUnpinFileData( FirstBcb, TRUE, UNPIN );
        }

        FirstBcb = NextBcb;
    } while (FileOffset->QuadPart != LastOffset.QuadPart);

    DebugTrace(-1, me, "CcReleaseByteRangeFromWrite -> VOID\n", 0);
}


//
//  Internal Support Routine
//

NTSTATUS
FASTCALL
CcWriteBehind (
    IN PSHARED_CACHE_MAP SharedCacheMap
    )

/*++

Routine Description:

    This routine may be called with Wait = FALSE to see if write behind
    is required, or with Wait = TRUE to perform write behind as required.

    The code is very similar to the the code that the Lazy Writer performs
    for each SharedCacheMap.  The main difference is in the call to
    CcAcquireByteRangeForWrite.  Write Behind does not care about time
    stamps (passing ULONG to accept all time stamps), but it will never
    dump the first (highest byte offset) buffer in the list if the last
    byte of that buffer is not yet written.  The Lazy Writer does exactly
    the opposite, in the sense that it is totally time-driven, and will
    even dump a partially modified buffer if it sits around long enough.

Arguments:

    SharedCacheMap - Pointer to SharedCacheMap to be written

Return Value:

    FALSE - if write behind is required, but the caller supplied
            Wait = FALSE

    TRUE - if write behind is complete or not required

--*/

{
    IO_STATUS_BLOCK IoStatus;
    KIRQL OldIrql;
    ULONG ActivePage;
    ULONG PageIsDirty;
    PMBCB Mbcb;
    NTSTATUS Status;
    ULONG FileExclusive = FALSE;
    PVACB ActiveVacb = NULL;

    DebugTrace(+1, me, "CcWriteBehind\n", 0 );
    DebugTrace( 0, me, "    SharedCacheMap = %08lx\n", SharedCacheMap );

    //
    //  First we have to acquire the file for LazyWrite, to avoid
    //  deadlocking with writers to the file.  We do this via the
    //  CallBack procedure specified to CcInitializeCacheMap.
    //

    (*SharedCacheMap->Callbacks->AcquireForLazyWrite)
                        ( SharedCacheMap->LazyWriteContext, TRUE );

    //
    //  See if there is a previous active page to clean up, but only
    //  do so now if it is the last dirty page or no users have the
    //  file open.  We will free it below after dropping the spinlock.
    //

    ExAcquireFastLock( &CcMasterSpinLock, &OldIrql );

    if ((SharedCacheMap->DirtyPages <= 1) || (SharedCacheMap->OpenCount == 0)) {
        GetActiveVacbAtDpcLevel( SharedCacheMap, ActiveVacb, ActivePage, PageIsDirty );
    }

    //
    //  Increment open count so that our caller's views stay available
    //  for CcGetVacbMiss.  We could be tying up all of the views, and
    //  still need to write file sizes.
    //

    SharedCacheMap->OpenCount += 1;

    //
    //  If there is a mask bcb, then we need to establish a target for
    //  it to flush.
    //

    if ((Mbcb = SharedCacheMap->Mbcb) != 0) {

        //
        //  Set a target of pages to write, assuming that any Active
        //  Vacb will increase the number.
        //

        Mbcb->PagesToWrite = Mbcb->DirtyPages + ((ActiveVacb != NULL) ? 1 : 0);

        if (Mbcb->PagesToWrite > CcPagesYetToWrite) {

            Mbcb->PagesToWrite = CcPagesYetToWrite;
        }
    }

    ExReleaseFastLock( &CcMasterSpinLock, OldIrql );

    //
    //  Now free the active Vacb, if we found one.
    //

    if (ActiveVacb != NULL) {

        CcFreeActiveVacb( SharedCacheMap, ActiveVacb, ActivePage, PageIsDirty );
    }

    //
    //  Now perform the lazy writing for this file via a special call
    //  to CcFlushCache.  He recognizes us by the &CcNoDelay input to
    //  FileOffset, which signifies a Lazy Write, but is subsequently
    //  ignored.
    //

    CcFlushCache( SharedCacheMap->FileObject->SectionObjectPointer,
                  &CcNoDelay,
                  1,
                  &IoStatus );

    //
    //  No need for the Lazy Write resource now.
    //

    (*SharedCacheMap->Callbacks->ReleaseFromLazyWrite)
                        ( SharedCacheMap->LazyWriteContext );

    //
    //  Check if we need to put up a popup.
    //

    if (!NT_SUCCESS(IoStatus.Status) && !RetryError(IoStatus.Status)) {

        //
        //  We lost writebehind data. Try to get the filename. If we can't,
        //  then just raise the error returned by the failing write
        //

        POBJECT_NAME_INFORMATION FileNameInfo;
        NTSTATUS QueryStatus;
        ULONG whocares;

        FileNameInfo = ExAllocatePool(PagedPool,1024);

        if ( FileNameInfo ) {
            QueryStatus = ObQueryNameString( SharedCacheMap->FileObject,
                                             FileNameInfo,
                                             1024,
                                             &whocares );

            if ( !NT_SUCCESS(QueryStatus) ) {
                ExFreePool(FileNameInfo);
                FileNameInfo = NULL;
            }
        }

        if ( FileNameInfo ) {
            IoRaiseInformationalHardError( STATUS_LOST_WRITEBEHIND_DATA,&FileNameInfo->Name, NULL );
            ExFreePool(FileNameInfo);
        } else {
            if ( SharedCacheMap->FileObject->FileName.Length &&
                 SharedCacheMap->FileObject->FileName.MaximumLength &&
                 SharedCacheMap->FileObject->FileName.Buffer ) {

                IoRaiseInformationalHardError( STATUS_LOST_WRITEBEHIND_DATA,&SharedCacheMap->FileObject->FileName, NULL );
            }
        }

    //
    //  See if there is any deferred writes we can post.
    //

    } else if (!IsListEmpty(&CcDeferredWrites)) {
        CcPostDeferredWrites();
    }

    //
    //  Now acquire CcMasterSpinLock again to
    //  see if we need to call CcUninitialize before returning.
    //

    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

    //
    //  If the the current ValidDataGoal is greater (or equal) than ValidDataLength,
    //  then we must see if we have advanced beyond the current ValidDataLength.
    //
    //  If we have NEVER written anything out from this shared cache map, then
    //  there is no need to check anything associtated with valid data length
    //  here.  We will come by here again when, and if, anybody actually
    //  modifies the file and we lazy write some data.
    //

    Status = STATUS_SUCCESS;
    if (FlagOn(SharedCacheMap->Flags, LAZY_WRITE_OCCURRED) &&
        (SharedCacheMap->ValidDataGoal.QuadPart >= SharedCacheMap->ValidDataLength.QuadPart) &&
        (SharedCacheMap->ValidDataLength.QuadPart != MAXLONGLONG) &&
        (SharedCacheMap->FileSize.QuadPart != 0)) {

        LARGE_INTEGER NewValidDataLength = {0,0};

        //
        //  If the Bcb List is completely empty, then we must have written
        //  everything, and then new ValidDataLength is equal to ValidDataGoal.
        //

        if (SharedCacheMap->DirtyPages == 0) {

            NewValidDataLength = SharedCacheMap->ValidDataGoal;
        }

        //
        //  Else we will look at the last Bcb in the descending-order Bcb
        //  list, and see if it describes data beyond ValidDataGoal.
        //
        //  (This test is logically too conservative.  For example, the last Bcb
        //  may not even be dirty (in which case we should look at its
        //  predecessor), or we may have earlier written valid data to this
        //  byte range (which also means if we knew this we could look at
        //  the predessor).  This simply means that the Lazy Writer may not
        //  successfully get ValidDataLength updated in a file being randomly
        //  accessed until the level of file access dies down, or at the latest
        //  until the file is closed.  However, security will never be
        //  compromised.)
        //

        else {

            PBCB LastBcb;
            PMBCB Mbcb = SharedCacheMap->Mbcb;

            if ((Mbcb != NULL) && (Mbcb->DirtyPages != 0)) {

                NewValidDataLength.QuadPart = (LONGLONG)Mbcb->FirstDirtyPage << PAGE_SHIFT;
            }

            LastBcb = CONTAINING_RECORD( SharedCacheMap->BcbList.Flink,
                                         BCB,
                                         BcbLinks );

            while (&LastBcb->BcbLinks != &SharedCacheMap->BcbList) {

                if ((LastBcb->NodeTypeCode == CACHE_NTC_BCB) && LastBcb->Dirty) {
                    break;
                }

                LastBcb = CONTAINING_RECORD( LastBcb->BcbLinks.Flink,
                                             BCB,
                                             BcbLinks );
            }

            //
            //  Check the Base of the last entry.
            //

            if ((&LastBcb->BcbLinks != &SharedCacheMap->BcbList) &&
                (LastBcb->FileOffset.QuadPart < NewValidDataLength.QuadPart )) {

                NewValidDataLength = LastBcb->FileOffset;
            }
        }

        //
        //  If New ValidDataLength has been written, then we have to
        //  call the file system back to update it.  We must temporarily
        //  drop our global list while we do this, which is safe to do since
        //  we have not cleared WRITE_QUEUED.
        //
        //  Note we keep calling any time we wrote the last page of the file,
        //  to solve the "famous" AFS Server problem.  The file system will
        //  truncate our valid data call to whatever is currently valid.  But
        //  then if he writes a little more, we do not want to stop calling
        //  back.
        //

        if ( NewValidDataLength.QuadPart >= SharedCacheMap->ValidDataLength.QuadPart ) {

            ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

            //
            //  Call file system to set new valid data.  We have no
            //  one to tell if this doesn't work.
            //

            Status = CcSetValidData( SharedCacheMap->FileObject,
                                     &NewValidDataLength );

            ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );
            if (NT_SUCCESS(Status)) {
                SharedCacheMap->ValidDataLength = NewValidDataLength;
#ifdef TOMM
            } else if ((Status != STATUS_INSUFFICIENT_RESOURCES) && !RetryError(Status)) {
                DbgPrint("Unexpected status from CcSetValidData: %08lx, FileObject: %08lx\n",
                         Status,
                         SharedCacheMap->FileObject);
                DbgBreakPoint();
#endif TOMM
            }
        }
    }

    //
    //  Show we are done.
    //

    SharedCacheMap->OpenCount -= 1;

    //
    //  Make an approximate guess about whether we will call CcDeleteSharedCacheMap or not
    //  to truncate the file. If we fail to acquire here, then we will not delete below,
    //  and just catch it on a subsequent pass.
    //

    if (FlagOn(SharedCacheMap->Flags, TRUNCATE_REQUIRED) && (SharedCacheMap->OpenCount == 0)) {
        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
        FsRtlAcquireFileExclusive( SharedCacheMap->FileObject );
        FileExclusive = TRUE;
        ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );
    }

    //
    //  Otherwise see if we are to delete this SharedCacheMap.  Note
    //  we go ahead and release the Resource first, because with
    //  OpenCount == 0 and an empty Bcb list, no one will be trying
    //  to access this SharedCacheMap but us.  Also, by releasing first
    //  we avoid a deadlock with the file system when the FileObject is
    //  dereferenced.  Note that CcDeleteSharedCacheMap requires that
    //  the CcMasterSpinLock already be acquired, and it
    //  releases it.  We have to clear the indirect pointer in this
    //  case, because no one else will do it.
    //
    //  Also do not delete the SharedCacheMap if we got an error on
    //  the ValidDataLength callback.  If we get a resource allocation
    //  failure or a retryable error (due to log file full?), we have
    //  no one to tell, so we must just loop back and try again.  Of
    //  course all I/O errors are just too bad.
    //

    if ((SharedCacheMap->OpenCount == 0)

            &&

        ((SharedCacheMap->DirtyPages == 0) || ((SharedCacheMap->FileSize.QuadPart == 0) &&
                                               !FlagOn(SharedCacheMap->Flags, PIN_ACCESS)))

            &&

        (FileExclusive || !FlagOn(SharedCacheMap->Flags, TRUNCATE_REQUIRED))

            &&

        (NT_SUCCESS(Status) || ((Status != STATUS_INSUFFICIENT_RESOURCES) && !RetryError(Status)))) {

        CcDeleteSharedCacheMap( SharedCacheMap, OldIrql, FileExclusive );
    }

    //
    //  In the normal case, we just release the resource on the way out.
    //

    else {

        //
        //  Now release the file if we have it.
        //

        if (FileExclusive) {
            ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
            FsRtlReleaseFile( SharedCacheMap->FileObject );
            ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );
        }

        ClearFlag(SharedCacheMap->Flags, WRITE_QUEUED);
        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
    }

    DebugTrace(-1, me, "CcWriteBehind->VOID\n", 0 );

    return IoStatus.Status;
}


VOID
CcFlushCache (
    IN PSECTION_OBJECT_POINTERS SectionObjectPointer,
    IN PLARGE_INTEGER FileOffset OPTIONAL,
    IN ULONG Length,
    OUT PIO_STATUS_BLOCK IoStatus OPTIONAL
    )

/*++

Routine Description:

    This routine may be called to flush dirty data from the cache to the
    cached file on disk.  Any byte range within the file may be flushed,
    or the entire file may be flushed by omitting the FileOffset parameter.

    This routine does not take a Wait parameter; the caller should assume
    that it will always block.

Arguments:

    SectionObjectPointer - A pointer to the Section Object Pointers
                           structure in the nonpaged Fcb.


    FileOffset - If this parameter is supplied (not NULL), then only the
                 byte range specified by FileOffset and Length are flushed.
                 If &CcNoDelay is specified, then this signifies the call
                 from the Lazy Writer, and the lazy write scan should resume
                 as normal from the last spot where it left off in the file.

    Length - Defines the length of the byte range to flush, starting at
             FileOffset.  This parameter is ignored if FileOffset is
             specified as NULL.

    IoStatus - The I/O status resulting from the flush operation.

Return Value:

    None.

--*/

{
    LARGE_INTEGER NextFileOffset, TargetOffset;
    ULONG NextLength;
    PBCB FirstBcb;
    KIRQL OldIrql;
    PSHARED_CACHE_MAP SharedCacheMap;
    IO_STATUS_BLOCK TrashStatus;
    PVOID TempVa;
    ULONG RemainingLength, TempLength;
    NTSTATUS PopupStatus;
    BOOLEAN HotSpot;
    ULONG BytesWritten = 0;
    BOOLEAN PopupRequired = FALSE;
    BOOLEAN VerifyRequired = FALSE;
    BOOLEAN IsLazyWriter = FALSE;
    BOOLEAN FreeActiveVacb = FALSE;
    PVACB ActiveVacb = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    DebugTrace(+1, me, "CcFlushCache:\n", 0 );
    DebugTrace( 0, mm, "    SectionObjectPointer = %08lx\n", SectionObjectPointer );
    DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n",
                            ARGUMENT_PRESENT(FileOffset) ? FileOffset->LowPart
                                                         : 0,
                            ARGUMENT_PRESENT(FileOffset) ? FileOffset->HighPart
                                                         : 0 );
    DebugTrace( 0, me, "    Length = %08lx\n", Length );

    //
    //  If IoStatus passed a Null pointer, set up to through status away.
    //

    if (!ARGUMENT_PRESENT(IoStatus)) {
        IoStatus = &TrashStatus;
    }
    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = 0;

    //
    //  See if this is the Lazy Writer.  Since he wants to use this common
    //  routine, which is also a public routine callable by file systems,
    //  the Lazy Writer shows his call by specifying CcNoDelay as the file offset!
    //
    //  Also, in case we do not write anything because we see only HotSpot(s),
    //  initialize the Status to indicate a retryable error, so CcWorkerThread
    //  knows we did not make any progress.  Of course any actual flush will
    //  overwrite this code.
    //

    if (FileOffset == &CcNoDelay) {
        IoStatus->Status = STATUS_VERIFY_REQUIRED;
        IsLazyWriter = TRUE;
        FileOffset = NULL;
    }

    //
    //  If there is nothing to do, return here.
    //

    if (ARGUMENT_PRESENT(FileOffset) && (Length == 0)) {

        DebugTrace(-1, me, "CcFlushCache -> VOID\n", 0 );
        return;
    }

    //
    //  See if the file is cached.
    //

    ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

    SharedCacheMap = SectionObjectPointer->SharedCacheMap;

    if (SharedCacheMap != NULL) {

        //
        //  Increment the open count to keep it from going away.
        //

        SharedCacheMap->OpenCount += 1;

        if ((SharedCacheMap->NeedToZero != NULL) || (SharedCacheMap->ActiveVacb != NULL)) {

            ULONG FirstPage = 0;
            ULONG LastPage = MAXULONG;

            if (ARGUMENT_PRESENT(FileOffset)) {

                FirstPage = (ULONG)(FileOffset->QuadPart >> PAGE_SHIFT);
                LastPage = (ULONG)((FileOffset->QuadPart + Length - 1) >> PAGE_SHIFT);
            }

            //
            //  Make sure we do not flush the active page without zeroing any
            //  uninitialized data.  Also, it is very important to free the active
            //  page if it is the one to be flushed, so that we get the dirty
            //  bit out to the Pfn.
            //

            if (((((LONGLONG)LastPage + 1) << PAGE_SHIFT) > SharedCacheMap->ValidDataGoal.QuadPart) ||

                ((SharedCacheMap->NeedToZero != NULL) &&
                 (FirstPage <= SharedCacheMap->NeedToZeroPage) &&
                 (LastPage >= SharedCacheMap->NeedToZeroPage)) ||

                ((SharedCacheMap->ActiveVacb != NULL) &&
                 (FirstPage <= SharedCacheMap->ActivePage) &&
                 (LastPage >= SharedCacheMap->ActivePage))) {

                GetActiveVacbAtDpcLevel( SharedCacheMap, ActiveVacb, RemainingLength, TempLength );
                FreeActiveVacb = TRUE;
            }
        }
    }

    ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );

    if (FreeActiveVacb) {
        CcFreeActiveVacb( SharedCacheMap, ActiveVacb, RemainingLength, TempLength );
    }

    //
    //  Scan for dirty pages if there is a shared cache map.
    //

    if (SharedCacheMap != NULL) {

        //
        //  If FileOffset was not specified then set to flush entire region
        //  and set valid data length to the goal so that we will not get
        //  any more call backs.
        //

        if (!IsLazyWriter && !ARGUMENT_PRESENT(FileOffset)) {

            SharedCacheMap->ValidDataLength = SharedCacheMap->ValidDataGoal;
        }

        //
        //  If this is an explicit flush, initialize our offset to scan for.
        //

        if (ARGUMENT_PRESENT(FileOffset)) {
            TargetOffset = *FileOffset;
        }

        //
        //  Assume we want to pass the explicit flush flag in Length.
        //  But overwrite it if a length really was specified.  On
        //  subsequent loops, NextLength will have some nonzero value.
        //

        NextLength = 1;
        if (Length != 0) {
            NextLength = Length;
        }

        //
        //  Loop as long as we find buffers to flush for this
        //  SharedCacheMap, and we are not trying to delete the guy.
        //

        while (((SharedCacheMap->PagesToWrite != 0) || !IsLazyWriter)

                    &&
               ((SharedCacheMap->FileSize.QuadPart != 0) ||
                FlagOn(SharedCacheMap->Flags, PIN_ACCESS))

                    &&

               !VerifyRequired

                    &&

               CcAcquireByteRangeForWrite ( SharedCacheMap,
                                            IsLazyWriter ? NULL : (ARGUMENT_PRESENT(FileOffset) ?
                                                                    &TargetOffset : NULL),
                                            IsLazyWriter ? 0: NextLength,
                                            &NextFileOffset,
                                            &NextLength,
                                            &FirstBcb )) {

            //
            //  Assume this range is not a hot spot.
            //

            HotSpot = FALSE;

            //
            //  We defer calling Mm to set address range modified until here, to take
            //  overhead out of the main line path, and to reduce the number of TBIS
            //  on a multiprocessor.
            //

            RemainingLength = NextLength;

            do {

                //
                //  See if the next file offset is mapped.  (If not, the dirty bit
                //  was propagated on the unmap.)
                //

                if ((TempVa = CcGetVirtualAddressIfMapped( SharedCacheMap,
                                                           NextFileOffset.QuadPart + NextLength - RemainingLength,
                                                           &ActiveVacb,
                                                           &TempLength)) != NULL) {

                    //
                    //  Reduce TempLength to RemainingLength if necessary, and
                    //  call MM.
                    //

                    if (TempLength > RemainingLength) {
                        TempLength = RemainingLength;
                    }

                    //
                    //  Clear the Dirty bit (if set) in the PTE and set the
                    //  Pfn modified.  Assume if the Pte was dirty, that this may
                    //  be a hot spot.  Do not do hot spots for metadata, and unless
                    //  they are within ValidDataLength as reported to the file system
                    //  via CcSetValidData.
                    //

                    HotSpot = (BOOLEAN)((MmSetAddressRangeModified(TempVa, TempLength) || HotSpot) &&
                                        ((NextFileOffset.QuadPart + NextLength) <
                                         (SharedCacheMap->ValidDataLength.QuadPart)) &&
                                        ((SharedCacheMap->LazyWritePassCount & 0xF) != 0) && IsLazyWriter) &&
                                        !FlagOn(SharedCacheMap->Flags, MODIFIED_WRITE_DISABLED);

                    CcFreeVirtualAddress( ActiveVacb );

                } else {

                    //
                    //  Reduce TempLength to RemainingLength if necessary.
                    //

                    if (TempLength > RemainingLength) {
                        TempLength = RemainingLength;
                    }
                }

                //
                //  Reduce RemainingLength by what we processed.
                //

                RemainingLength -= TempLength;

            //
            //  Loop until done.
            //

            } while (RemainingLength != 0);

            CcLazyWriteHotSpots += HotSpot;

            //
            //  Now flush, now flush if we do not think it is a hot spot.
            //

            if (!HotSpot) {

                MmFlushSection( SharedCacheMap->FileObject->SectionObjectPointer,
                                &NextFileOffset,
                                NextLength,
                                IoStatus,
                                !IsLazyWriter );

                if (NT_SUCCESS(IoStatus->Status)) {

                    ExAcquireFastLock( &CcMasterSpinLock, &OldIrql );
                    SetFlag(SharedCacheMap->Flags, LAZY_WRITE_OCCURRED);
                    ExReleaseFastLock( &CcMasterSpinLock, OldIrql );

                    //
                    //  Increment performance counters
                    //

                    if (IsLazyWriter) {

                        CcLazyWriteIos += 1;
                        CcLazyWritePages += (NextLength + PAGE_SIZE - 1) >> PAGE_SHIFT;
                    }

                } else {

                    LARGE_INTEGER Offset = NextFileOffset;
                    ULONG RetryLength = NextLength;

                    DebugTrace2( 0, 0, "I/O Error on Cache Flush: %08lx, %08lx\n",
                                 IoStatus->Status, IoStatus->Information );

                    if (RetryError(IoStatus->Status)) {

                        VerifyRequired = TRUE;

                    //
                    //  Loop to write each page individually, starting with one
                    //  more try on the page that got the error, in case that page
                    //  or any page beyond it can be successfully written
                    //  individually.  Note that Offset and RetryLength are
                    //  guaranteed to be in integral pages, but the Information
                    //  field from the failed request is not.
                    //
                    //  We ignore errors now, and give it one last shot, before
                    //  setting the pages clean (see below).
                    //

                    } else {

                        do {

                            DebugTrace2( 0, 0, "Trying page at offset %08lx, %08lx\n",
                                         Offset.LowPart, Offset.HighPart );

                            MmFlushSection ( SharedCacheMap->FileObject->SectionObjectPointer,
                                             &Offset,
                                             PAGE_SIZE,
                                             IoStatus,
                                             !IsLazyWriter );

                            DebugTrace2( 0, 0, "I/O status = %08lx, %08lx\n",
                                         IoStatus->Status, IoStatus->Information );

                            if (NT_SUCCESS(IoStatus->Status)) {
                                ExAcquireFastLock( &CcMasterSpinLock, &OldIrql );
                                SetFlag(SharedCacheMap->Flags, LAZY_WRITE_OCCURRED);
                                ExReleaseFastLock( &CcMasterSpinLock, OldIrql );
                            }

                            if ((!NT_SUCCESS(IoStatus->Status)) && !RetryError(IoStatus->Status)) {

                                PopupRequired = TRUE;
                                PopupStatus = IoStatus->Status;
                            }

                            VerifyRequired = VerifyRequired || RetryError(IoStatus->Status);

                            Offset.QuadPart = Offset.QuadPart + (LONGLONG)PAGE_SIZE;
                            RetryLength -= PAGE_SIZE;

                        } while(RetryLength > 0);
                    }
                }
            }

            //
            //  Now release the Bcb resources and set them clean.  Note we do not check
            //  here for errors, and just returned in the I/O status.  Errors on writes
            //  are rare to begin with.  Nonetheless, our strategy is to rely on
            //  one or more of the following (depending on the file system) to prevent
            //  errors from getting to us.
            //
            //      - Retries and/or other forms of error recovery in the disk driver
            //      - Mirroring driver
            //      - Hot fixing in the noncached path of the file system
            //
            //  In the unexpected case that a write error does get through, we
            //  *currently* just set the Bcbs clean anyway, rather than let
            //  Bcbs and pages accumulate which cannot be written.  Note we did
            //  a popup above to at least notify the guy.
            //
            //  Set the pages dirty again if we either saw a HotSpot or got
            //  verify required.
            //

            CcReleaseByteRangeFromWrite ( SharedCacheMap,
                                          &NextFileOffset,
                                          NextLength,
                                          FirstBcb,
                                          (BOOLEAN)(HotSpot || VerifyRequired) );

            //
            //  See if there is any deferred writes we should post.
            //

            BytesWritten += NextLength;
            if ((BytesWritten >= 0x40000) && !IsListEmpty(&CcDeferredWrites)) {
                CcPostDeferredWrites();
                BytesWritten = 0;
            }

            //
            //  Now for explicit flushes, we should advance our range.
            //

            if (ARGUMENT_PRESENT(FileOffset)) {

                NextFileOffset.QuadPart += NextLength;

                //
                //  Done yet?
                //

                if ((FileOffset->QuadPart + Length) <= NextFileOffset.QuadPart) {
                    break;
                }

                //
                //  Calculate new target range
                //

                NextLength = (ULONG)((FileOffset->QuadPart + Length) - NextFileOffset.QuadPart);
                TargetOffset = NextFileOffset;
            }
        }
    }

    //
    //  If there is a user-mapped file, then we perform the "service" of
    //  flushing even data not written via the file system.  To do this
    //  we simply reissue the original flush, sigh.
    //

    if ((SharedCacheMap == NULL)

            ||

        FlagOn(((PFSRTL_COMMON_FCB_HEADER)(SharedCacheMap->FileObject->FsContext))->Flags,
               FSRTL_FLAG_USER_MAPPED_FILE) && !IsLazyWriter) {

        //
        //  Call MM to flush the section through our view.
        //

        DebugTrace( 0, mm, "MmFlushSection:\n", 0 );
        DebugTrace( 0, mm, "    SectionObjectPointer = %08lx\n", SectionObjectPointer );
        DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n",
                                ARGUMENT_PRESENT(FileOffset) ? FileOffset->LowPart
                                                             : 0,
                                ARGUMENT_PRESENT(FileOffset) ? FileOffset->HighPart
                                                             : 0 );
        DebugTrace( 0, mm, "    RegionSize = %08lx\n", Length );

        try {

            Status = MmFlushSection( SectionObjectPointer,
                                     FileOffset,
                                     Length,
                                     IoStatus,
                                     TRUE );

        } except( CcExceptionFilter( IoStatus->Status = GetExceptionCode() )) {

            KdPrint(("CACHE MANAGER: MmFlushSection raised %08lx\n", IoStatus->Status));
        }

        DebugTrace2(0, mm, "    <IoStatus = %08lx, %08lx\n",
                    IoStatus->Status, IoStatus->Information );
    }

    //
    //  Now we can get rid of the open count, and clean up as required.
    //

    if (SharedCacheMap != NULL) {

        //
        //  Serialize again to decrement the open count.
        //

        ExAcquireSpinLock( &CcMasterSpinLock, &OldIrql );

        SharedCacheMap->OpenCount -= 1;

        if ((SharedCacheMap->OpenCount == 0) &&
            !FlagOn(SharedCacheMap->Flags, WRITE_QUEUED) &&
            (SharedCacheMap->DirtyPages == 0)) {

            //
            //  Move to the dirty list.
            //

            RemoveEntryList( &SharedCacheMap->SharedCacheMapLinks );
            InsertTailList( &CcDirtySharedCacheMapList.SharedCacheMapLinks,
                            &SharedCacheMap->SharedCacheMapLinks );

            //
            //  Make sure the Lazy Writer will wake up, because we
            //  want him to delete this SharedCacheMap.
            //

            LazyWriter.OtherWork = TRUE;
            if (!LazyWriter.ScanActive) {
                CcScheduleLazyWriteScan();
            }
        }

        ExReleaseSpinLock( &CcMasterSpinLock, OldIrql );
    }

    //
    //  Make sure and return the first error to our caller.  In the
    //  case of the Lazy Writer, a popup will be issued.
    //

    if (PopupRequired) {
        IoStatus->Status = PopupStatus;
    }

    //
    //  Let the Lazy writer know if we did anything, so he can

    DebugTrace(-1, me, "CcFlushCache -> VOID\n", 0 );

    return;
}


VOID
CcRepinBcb (
    IN PVOID Bcb
    )

/*++

Routine Description:

    This routine may be called by a file system to pin a Bcb an additional
    time in order to reserve it for Write Through or error recovery.
    Typically the file system would do this the first time that it sets a
    pinned buffer dirty while processing a WriteThrough request, or any
    time that it determines that a buffer will be required for WriteThrough.

    The call to this routine must be followed by a call to CcUnpinRepinnedBcb.
    CcUnpinRepinnedBcb should normally be called during request completion
    after all other resources have been released.  CcUnpinRepinnedBcb
    synchronously writes the buffer (for WriteThrough requests) and performs
    the matching unpin for this call.

Arguments:

    Bcb - Supplies a pointer to a previously pinned Bcb

Return Value:

    None.

--*/

{
    KIRQL OldIrql;

    ExAcquireFastLock( &CcMasterSpinLock, &OldIrql );

    ((PBCB)Bcb)->PinCount += 1;

    ExReleaseFastLock( &CcMasterSpinLock, OldIrql );
}


VOID
CcUnpinRepinnedBcb (
    IN PVOID Bcb,
    IN BOOLEAN WriteThrough,
    OUT PIO_STATUS_BLOCK IoStatus
    )

/*++

Routine Description:

    This routine may be called to Write a previously pinned buffer
    through to the file.  It must have been preceded by a call to
    CcRepinBcb.  As this routine must acquire the Bcb
    resource exclusive, the caller must be extremely careful to avoid
    deadlocks.  Ideally the caller owns no resources at all when it
    calls this routine, or else the caller should guarantee that it
    has nothing else pinned in this same file.  (The latter rule is
    the one used to avoid deadlocks in calls from CcCopyWrite and
    CcMdlWrite.)

Arguments:

    Bcb - Pointer to a Bcb which was previously specified in a call
          to CcRepinBcb.

    WriteThrough - TRUE if the Bcb should be written through.

    IoStatus - Returns the I/O status for the operation.

Return Value:

    None.

--*/

{
    PSHARED_CACHE_MAP SharedCacheMap = ((PBCB)Bcb)->SharedCacheMap;

    DebugTrace(+1, me, "CcUnpinRepinnedBcb\n", 0 );
    DebugTrace( 0, me, "    Bcb = %08lx\n", Bcb );
    DebugTrace( 0, me, "    WriteThrough = %02lx\n", WriteThrough );

    //
    //  Set status to success for non write through case.
    //

    IoStatus->Status = STATUS_SUCCESS;

    if (WriteThrough) {

        //
        //  Acquire Bcb exclusive to eliminate possible modifiers of the buffer,
        //  since we are about to write its buffer.
        //

        if (FlagOn(SharedCacheMap->Flags, MODIFIED_WRITE_DISABLED)) {
            ExAcquireResourceExclusive( &((PBCB)Bcb)->Resource, TRUE );
        }

        //
        //  Now, there is a chance that the LazyWriter has already written
        //  it, since the resource was free.  We will only write it if it
        //  is still dirty.
        //

        if (((PBCB)Bcb)->Dirty) {

            //
            //  First we make sure that the dirty bit in the PFN database is set.
            //

            ASSERT( ((PBCB)Bcb)->BaseAddress != NULL );
            MmSetAddressRangeModified( ((PBCB)Bcb)->BaseAddress,
                                       ((PBCB)Bcb)->ByteLength );

            //
            //  Now release the Bcb resource and set it clean.  Note we do not check
            //  here for errors, and just return the I/O status.  Errors on writes
            //  are rare to begin with.  Nonetheless, our strategy is to rely on
            //  one or more of the following (depending on the file system) to prevent
            //  errors from getting to us.
            //
            //      - Retries and/or other forms of error recovery in the disk driver
            //      - Mirroring driver
            //      - Hot fixing in the noncached path of the file system
            //
            //  In the unexpected case that a write error does get through, we
            //  report it to our caller, but go ahead and set the Bcb clean.  There
            //  seems to be no point in letting Bcbs (and pages in physical memory)
            //  accumulate which can never go away because we get an unrecoverable I/O
            //  error.
            //

            //
            //  We specify TRUE here for ReadOnly so that we will keep the
            //  resource during the flush.
            //

            CcUnpinFileData( (PBCB)Bcb, TRUE, SET_CLEAN );

            //
            //  Write it out.
            //

            MmFlushSection( ((PBCB)Bcb)->SharedCacheMap->FileObject->SectionObjectPointer,
                            &((PBCB)Bcb)->FileOffset,
                            ((PBCB)Bcb)->ByteLength,
                            IoStatus,
                            TRUE );

            //
            //  If we got verify required, we have to mark the buffer dirty again
            //  so we will try again later.
            //

            if (RetryError(IoStatus->Status)) {
                CcSetDirtyPinnedData( (PBCB)Bcb, NULL );
            }

            //
            //  Now remove the final pin count now that we have set it clean.
            //

            CcUnpinFileData( (PBCB)Bcb, FALSE, UNPIN );

            //
            //  See if there is any deferred writes we can post.
            //

            if (!IsListEmpty(&CcDeferredWrites)) {
                CcPostDeferredWrites();
            }
        }
        else {

            //
            //  Lazy Writer got there first, just free the resource and unpin.
            //

            CcUnpinFileData( (PBCB)Bcb, FALSE, UNPIN );

        }

        DebugTrace2(0, me, "    <IoStatus = %08lx, %08lx\n", IoStatus->Status,
                                                             IoStatus->Information );
    }

    //
    //  Non-WriteThrough case
    //

    else {

        CcUnpinFileData( (PBCB)Bcb, TRUE, UNPIN );

        //
        //  Set status to success for non write through case.
        //

        IoStatus->Status = STATUS_SUCCESS;
    }

    DebugTrace(-1, me, "CcUnpinRepinnedBcb -> VOID\n", 0 );
}


//
//  Internal Support Routine
//

BOOLEAN
CcFindBcb (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN OUT PLARGE_INTEGER BeyondLastByte,
    OUT PBCB *Bcb
    )

/*++

Routine Description:

    This routine is called to find a Bcb describing the specified byte range
    of a file.  It returns TRUE if it could at least find a Bcb which describes
    the beginning of the specified byte range, or else FALSE if the first
    part of the byte range is not present.  In the latter case, the requested
    byte range (TrialLength) is truncated if there is currently a Bcb which
    describes bytes beyond the beginning of the byte range.

    The caller may see if the entire byte range is being returned by examining
    the Bcb, and the caller (or caller's caller) may then make subsequent
    calls if the data is not all returned.

    The BcbList SpinLock must be currently acquired.

Arguments:

    SharedCacheMap - Supplies a pointer to the SharedCacheMap for the file
                     in which the byte range is desired.

    FileOffset - Supplies the file offset for the beginning of the desired
                 byte range.

    BeyondLastByte - Supplies the file offset of the ending of the desired
                  byte range + 1.  Note that this offset will be truncated
                  on return if the Bcb was not found, but bytes beyond the
                  beginning of the Bcb are contained in another Bcb.

    Bcb - returns a Bcb describing the beginning of the byte range if also
          returning TRUE, or else the point in the Bcb list to insert after.

Return Value:

    FALSE - if no Bcb describes the beginning of the desired byte range

    TRUE - if a Bcb is being returned describing at least an initial
           part of the byte range.

--*/

{
    PLIST_ENTRY BcbList;
    PBCB Bcbt;
    BOOLEAN Found = FALSE;

    DebugTrace(+1, me, "CcFindBcb:\n", 0 );
    DebugTrace( 0, me, "    SharedCacheMap = %08lx\n", SharedCacheMap );
    DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                          FileOffset->HighPart );
    DebugTrace2(0, me, "    TrialLength = %08lx, %08lx\n", TrialLength->LowPart,
                                                           TrialLength->HighPart );

    //
    //  We want to terminate scans by testing the NodeTypeCode field from the
    //  BcbLinks, so we want to see the SharedCacheMap signature from the same
    //  offset.
    //

    ASSERT(FIELD_OFFSET(SHARED_CACHE_MAP, BcbList) == FIELD_OFFSET(BCB, BcbLinks));

    //
    //  Similarly, when we hit one of the BcbListHeads in the array, small negative
    //  offsets are all structure pointers, so we are counting on the Bcb signature
    //  to have some non-Ulong address bits set.
    //

    ASSERT((CACHE_NTC_BCB & 3) != 0);

    //
    //  Get address of Bcb listhead that is *after* the Bcb we are looking for,
    //  for backwards scan.
    //

    BcbList = &SharedCacheMap->BcbList;
    if ((FileOffset->QuadPart + SIZE_PER_BCB_LIST) < SharedCacheMap->SectionSize.QuadPart) {
        BcbList = GetBcbListHead( SharedCacheMap, FileOffset->QuadPart + SIZE_PER_BCB_LIST );
    }

    //
    //  Search for an entry that overlaps the specified range, or until we hit
    //  a listhead.
    //

    Bcbt = CONTAINING_RECORD(BcbList->Flink, BCB, BcbLinks);

    //
    //  First see if we really have to do Large arithmetic or not, and
    //  then use either a 32-bit loop or a 64-bit loop to search for
    //  the Bcb.
    //

    if (FileOffset->HighPart == 0) {

        //
        //  32-bit - loop until we get back to a listhead.
        //

        while (Bcbt->NodeTypeCode == CACHE_NTC_BCB) {

            //
            //  Since the Bcb list is in descending order, we first check
            //  if we are completely beyond the current entry, and if so
            //  get out.
            //

            if (FileOffset->LowPart >= Bcbt->BeyondLastByte.LowPart) {
                break;
            }

            //
            //  Next check if the first byte we are looking for is
            //  contained in the current Bcb.  If so, we either have
            //  a partial hit and must truncate to the exact amount
            //  we have found, or we may have a complete hit.  In
            //  either case we break with Found == TRUE.
            //

            if (FileOffset->LowPart >= Bcbt->FileOffset.LowPart) {
                Found = TRUE;
                break;
            }

            //
            //  Now we know we must loop back and keep looking, but we
            //  still must check for the case where the tail end of the
            //  bytes we are looking for are described by the current
            //  Bcb.  If so we must truncate what we are looking for,
            //  because this routine is only supposed to return bytes
            //  from the start of the desired range.
            //

            if (BeyondLastByte->LowPart >= Bcbt->FileOffset.LowPart) {
                BeyondLastByte->LowPart = Bcbt->FileOffset.LowPart;
            }

            //
            //  Advance to next entry in list (which is possibly back to
            //  the listhead) and loop back.
            //

            Bcbt = CONTAINING_RECORD( Bcbt->BcbLinks.Flink,
                                      BCB,
                                      BcbLinks );

        }

    } else {

        //
        //  64-bit - Loop until we get back to a listhead.
        //

        while (Bcbt->NodeTypeCode == CACHE_NTC_BCB) {

            //
            //  Since the Bcb list is in descending order, we first check
            //  if we are completely beyond the current entry, and if so
            //  get out.
            //

            if (FileOffset->QuadPart >= Bcbt->BeyondLastByte.QuadPart) {
                break;
            }

            //
            //  Next check if the first byte we are looking for is
            //  contained in the current Bcb.  If so, we either have
            //  a partial hit and must truncate to the exact amount
            //  we have found, or we may have a complete hit.  In
            //  either case we break with Found == TRUE.
            //

            if (FileOffset->QuadPart >= Bcbt->FileOffset.QuadPart) {
                Found = TRUE;
                break;
            }

            //
            //  Now we know we must loop back and keep looking, but we
            //  still must check for the case where the tail end of the
            //  bytes we are looking for are described by the current
            //  Bcb.  If so we must truncate what we are looking for,
            //  because this routine is only supposed to return bytes
            //  from the start of the desired range.
            //

            if (BeyondLastByte->QuadPart >= Bcbt->FileOffset.QuadPart) {
                BeyondLastByte->QuadPart = Bcbt->FileOffset.QuadPart;
            }

            //
            //  Advance to next entry in list (which is possibly back to
            //  the listhead) and loop back.
            //

            Bcbt = CONTAINING_RECORD( Bcbt->BcbLinks.Flink,
                                      BCB,
                                      BcbLinks );

        }
    }

    *Bcb = Bcbt;

    DebugTrace2(0, me, "    <TrialLength = %08lx, %08lx\n", TrialLength->LowPart,
                                                            TrialLength->HighPart );
    DebugTrace( 0, me, "    <Bcb = %08lx\n", *Bcb );
    DebugTrace(-1, me, "CcFindBcb -> %02lx\n", Found );

    return Found;
}


//
//  Internal Support Routine
//

PBCB
CcAllocateInitializeBcb (
    IN OUT PSHARED_CACHE_MAP SharedCacheMap OPTIONAL,
    IN OUT PBCB AfterBcb,
    IN PLARGE_INTEGER FileOffset,
    IN PLARGE_INTEGER TrialLength
    )

/*++

Routine Description:

    This routine allocates and initializes a Bcb to describe the specified
    byte range, and inserts it into the Bcb List of the specified Shared
    Cache Map.  The Bcb List spin lock must currently be acquired.

    CcMasterSpinLock must be acquired on entry.

Arguments:

    SharedCacheMap - Supplies the SharedCacheMap for the new Bcb.

    AfterBcb - Supplies where in the descending-order BcbList the new Bcb
               should be inserted: either the ListHead (masquerading as
               a Bcb) or a Bcb.

    FileOffset - Supplies File Offset for the desired data.

    TrialLength - Supplies length of desired data.

Return Value:

    Address of the allocated and initialized Bcb

--*/

{
    PBCB Bcb;
    CSHORT NodeIsInZone;
    ULONG RoundedBcbSize = (sizeof(BCB) + 7) & ~7;

    //
    //  Loop until we have a new Work Queue Entry
    //

    while (TRUE) {

        PVOID Segment;
        ULONG SegmentSize;

        Bcb = ExAllocateFromZone( &LazyWriter.BcbZone );

        if (Bcb != NULL) {
            NodeIsInZone = 1;
            break;
        }

        //
        //  Allocation failure - on large systems, extend zone
        //

        if ( MmQuerySystemSize() == MmLargeSystem ) {

            SegmentSize = sizeof(ZONE_SEGMENT_HEADER) + RoundedBcbSize * 32;

            if ((Segment = ExAllocatePool( NonPagedPool, SegmentSize)) == NULL) {

                return NULL;
            }

            if (!NT_SUCCESS(ExExtendZone( &LazyWriter.BcbZone, Segment, SegmentSize ))) {
                CcBugCheck( 0, 0, 0 );
            }
        } else {
            if ((Bcb = ExAllocatePool( NonPagedPool, sizeof(BCB))) == NULL) {
                return NULL;
            }
            NodeIsInZone = 0;
            break;
        }
    }

    //
    //  Initialize the newly allocated Bcb.  First zero it, then fill in
    //  nonzero fields.
    //

    RtlZeroMemory( Bcb, RoundedBcbSize );

    Bcb->NodeIsInZone = NodeIsInZone;

    //
    //  For Mbcb's, SharedCacheMap is NULL, and the rest of this initialization
    //  is not desired.
    //

    if (SharedCacheMap != NULL) {

        Bcb->NodeTypeCode = CACHE_NTC_BCB;
        Bcb->FileOffset = *FileOffset;
        Bcb->ByteLength = TrialLength->LowPart;
        Bcb->BeyondLastByte.QuadPart = FileOffset->QuadPart + TrialLength->QuadPart;
        Bcb->PinCount += 1;
        ExInitializeResource( &Bcb->Resource );
        Bcb->SharedCacheMap = SharedCacheMap;

        //
        //  Now insert the Bcb in the Bcb List
        //

        InsertTailList( &AfterBcb->BcbLinks, &Bcb->BcbLinks );

        //
        //  If this resource was no write behind, let Ex know that the
        //  resource will never be acquired exclusive.  Also disable
        //  boost (I know this is useless, but KenR said I had to do it).
        //

        if (SharedCacheMap &&
            FlagOn(SharedCacheMap->Flags, DISABLE_WRITE_BEHIND)) {
#if DBG
            SetFlag(Bcb->Resource.Flag, ResourceNeverExclusive);
#endif
            ExDisableResourceBoost( &Bcb->Resource );
        }


    }

    return Bcb;
}


//
//  Internal support routine
//

VOID
FASTCALL
CcDeallocateBcb (
    IN PBCB Bcb
    )

/*++

Routine Description:

    This routine deallocates a Bcb to the BcbZone.  It must
    already be removed from the BcbList.

    CcMasterSpinLock must be acquired on entry.

Arguments:

    Bcb - the Bcb to deallocate

Return Value:

    None

--*/

{
    //
    //  Deallocate Resource structures
    //

    if (Bcb->NodeTypeCode == CACHE_NTC_BCB) {

        ExDeleteResource( &Bcb->Resource );
    }

    if ( Bcb->NodeIsInZone ) {

        //
        //  Synchronize access to the BcbZone
        //

        ExFreeToZone( &LazyWriter.BcbZone,
                      Bcb );
    } else {
        ExFreePool(Bcb);
    }
    return;
}


//
//  Internal Support Routine
//

BOOLEAN
CcMapAndRead(
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG ZeroFlags,
    IN BOOLEAN Wait,
    OUT PVACB *Vacb,
    OUT PVOID *BaseAddress
    )

/*++

Routine Description:

    This routine may be called to insure that the specified data is mapped,
    read into memory and locked.  If TRUE is returned, then the
    correct I/O status for the transfer is also returned, along with
    a system-space address for the data.

Arguments:

    SharedCacheMap - Supplies the address of the SharedCacheMap for the
                     data.

    FileOffset - Supplies the file offset of the desired data.

    Length - Supplies the total amount of data desired.

    ZeroFlags - Defines which pages may be zeroed if not resident.

    Wait - Supplies FALSE if the caller is not willing to block for the
           data, or TRUE if the caller is willing to block.

    Vacb - Returns the address of the Vacb which is mapping the enclosing
           virtual address range.

    BaseAddress - Returns the system base address at which the data may
                  be accessed.

Return Value:

    FALSE - if the caller supplied Wait = FALSE and the data could not
            be returned without blocking.

    TRUE - if the data is being returned.

    Note: this routine may raise an exception due to a map or read failure,
          however, this can only happen if Wait was specified as TRUE, since
          mapping and reading will not be performed if the caller cannot wait.

--*/

{
    ULONG ReceivedLength;
    ULONG ZeroCase;
    ULONG SavedState;
    BOOLEAN Result = FALSE;
    PETHREAD Thread = PsGetCurrentThread();

    DebugTrace(+1, me, "CcMapAndRead:\n", 0 );
    DebugTrace( 0, me, "    SharedCacheMap = %08lx\n", SharedCacheMap );
    DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                          FileOffset->HighPart );
    DebugTrace( 0, me, "    Length = %08lx\n", Length );

    *BaseAddress = NULL;
    *Vacb = NULL;

    *BaseAddress = CcGetVirtualAddress( SharedCacheMap,
                                        *FileOffset,
                                        Vacb,
                                        &ReceivedLength );

    ASSERT( ReceivedLength >= Length );

    MmSavePageFaultReadAhead( Thread, &SavedState );


    //
    //  try around everything for cleanup.
    //

    try {

        PVOID CacheBuffer;
        ULONG PagesToGo;

        //
        //  If we got more than we need, make sure to only use
        //  the right amount.
        //

        if (ReceivedLength > Length) {
            ReceivedLength = Length;
        }

        //
        //  Now loop to touch all of the pages, calling MM to insure
        //  that if we fault, we take in exactly the number of pages
        //  we need.
        //

        CacheBuffer = *BaseAddress;
        PagesToGo = COMPUTE_PAGES_SPANNED( CacheBuffer,
                                           ReceivedLength );

        //
        //  Loop to touch or zero the pages.
        //

        ZeroCase = ZERO_FIRST_PAGE;

        while (PagesToGo) {

            //
            //  If we cannot zero this page, or Mm failed to return
            //  a zeroed page, then just fault it in.
            //

            MmSetPageFaultReadAhead( Thread, (PagesToGo - 1) );

            if (!FlagOn(ZeroFlags, ZeroCase) ||
                !MmCheckCachedPageState(CacheBuffer, TRUE)) {

                //
                //  If we get here, it is almost certainly due to the fact
                //  that we can not take a zero page.  MmCheckCachedPageState
                //  will so rarely return FALSE, that we will not worry
                //  about it.  We will only check if the page is there if
                //  Wait is FALSE, so that we can do the right thing.
                //

                if (!MmCheckCachedPageState(CacheBuffer, FALSE) && !Wait) {
                    try_return( Result = FALSE );
                }
            }

            CacheBuffer = (PCHAR)CacheBuffer + PAGE_SIZE;
            PagesToGo -= 1;

            if (PagesToGo == 1) {
                ZeroCase = ZERO_LAST_PAGE;
            } else {
                ZeroCase = ZERO_MIDDLE_PAGES;
            }
        }

        try_return( Result = TRUE );

    try_exit: NOTHING;
    }

    //
    //  Cleanup on the way out.
    //

    finally {

        MmResetPageFaultReadAhead(Thread, SavedState);

        //
        //  If not successful, cleanup on the way out.  Most of the errors
        //  can only occur as the result of an abnormal termination after
        //  successfully checking and locking the pages.
        //

        if (Result == FALSE) {

            CcFreeVirtualAddress( *Vacb );
            *Vacb = NULL;
            *BaseAddress = NULL;
        }
    }

    DebugTrace( 0, me, "    <Vacb = %08lx\n", *Vacb );
    DebugTrace( 0, me, "    <BaseAddress = %08lx\n", *BaseAddress );
    DebugTrace(-1, me, "CcMapAndRead -> %02lx\n", Result );

    return Result;
}


//
//  Internal Support Routine
//

VOID
CcFreeActiveVacb (
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PVACB ActiveVacb OPTIONAL,
    IN ULONG ActivePage,
    IN ULONG PageIsDirty
    )

/*++

Routine Description:

    This routine may be called to zero the end of a locked page or
    free the ActiveVacb for a Shared Cache Map, if there is one.
    Note that some callers are not synchronized with foreground
    activity, and may therefore not have an ActiveVacb.  Examples
    of unsynchronized callers are CcZeroEndOfLastPage (which is
    called by MM) and any flushing done by CcWriteBehind.

Arguments:

    SharedCacheMap - SharedCacheMap to examine for page to be zeroed.

    ActiveVacb - Vacb to free

    ActivePage - Page that was used

    PageIsDirty - ACTIVE_PAGE_IS_DIRTY if the active page is dirty

Return Value:

    None

--*/

{
    LARGE_INTEGER ActiveOffset;
    PVOID ActiveAddress;
    ULONG BytesLeftInPage;
    KIRQL OldIrql;

    //
    //  If the page was locked, then unlock it.
    //

    if (SharedCacheMap->NeedToZero != NULL) {

        //
        //  Zero the rest of the page under spinlock control,
        //  and then clear the address field.  This field makes
        //  zero->nonzero transitions only when the file is exclusive,
        //  but it can make nonzero->zero transitions any time the
        //  spinlock is not held.
        //

        ExAcquireFastLock( &SharedCacheMap->ActiveVacbSpinLock, &OldIrql );

        //
        //  The address could already be gone.
        //

        ActiveAddress = SharedCacheMap->NeedToZero;
        if (ActiveAddress != NULL) {

            BytesLeftInPage = PAGE_SIZE - ((((ULONG)ActiveAddress - 1) & (PAGE_SIZE - 1)) + 1);
            RtlZeroBytes( ActiveAddress, BytesLeftInPage );
            SharedCacheMap->NeedToZero = NULL;
        }
        ExReleaseFastLock( &SharedCacheMap->ActiveVacbSpinLock, OldIrql );

        //
        //  Now call MM to unlock the address.  Note we will never store the
        //  address at the start of the page, but we can sometimes store
        //  the start of the next page when we have exactly filled the page.
        //

        if (ActiveAddress != NULL) {
            MmUnlockCachedPage( (PVOID)((PCHAR)ActiveAddress - 1) );
        }
    }

    //
    //  See if caller actually has an ActiveVacb
    //

    if (ActiveVacb != NULL) {

        //
        //  See if the page is dirty
        //

        if (PageIsDirty) {

            ActiveOffset.QuadPart = (LONGLONG)ActivePage << PAGE_SHIFT;
            ActiveAddress = (PVOID)((PCHAR)ActiveVacb->BaseAddress +
                                    (ActiveOffset.LowPart  & (VACB_MAPPING_GRANULARITY - 1)));

            //
            //  Tell the Lazy Writer to write the page.
            //

            CcSetDirtyInMask( SharedCacheMap, &ActiveOffset, PAGE_SIZE );

            //
            //  Now we need to clear the flag and decrement some counts if there is
            //  no other active Vacb which snuck in.
            //

            ExAcquireFastLock( &CcMasterSpinLock, &OldIrql );
            ExAcquireSpinLockAtDpcLevel( &SharedCacheMap->ActiveVacbSpinLock );
            if ((SharedCacheMap->ActiveVacb == NULL) &&
                FlagOn(SharedCacheMap->Flags, ACTIVE_PAGE_IS_DIRTY)) {

                ClearFlag(SharedCacheMap->Flags, ACTIVE_PAGE_IS_DIRTY);
                SharedCacheMap->DirtyPages -= 1;
                CcTotalDirtyPages -= 1;
            }
            ExReleaseSpinLockFromDpcLevel( &SharedCacheMap->ActiveVacbSpinLock );
            ExReleaseFastLock( &CcMasterSpinLock, OldIrql );
        }

        //
        //  Now free the Vacb.
        //

        CcFreeVirtualAddress( ActiveVacb );
    }
}


//
//  Internal Support Routine
//

VOID
CcMapAndCopy(
    IN PSHARED_CACHE_MAP SharedCacheMap,
    IN PVOID UserBuffer,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG ZeroFlags,
    IN BOOLEAN WriteThrough
    )

/*++

Routine Description:

    This routine may be called to copy the specified user data to the
    cache via a special Mm routine which copies the data to uninitialized
    pages and returns.

Arguments:

    SharedCacheMap - Supplies the address of the SharedCacheMap for the
                     data.

    UserBuffer - unsafe buffer supplying the user's data to be written

    FileOffset - Supplies the file offset to be modified

    Length - Supplies the total amount of data

    ZeroFlags - Defines which pages may be zeroed if not resident.

    WriteThrough - Supplies whether the data is to be written through or not

Return Value:

    None

--*/

{
    ULONG ReceivedLength;
    ULONG ZeroCase;
    PVOID CacheBuffer;
    PVOID SavedMappedBuffer;
    ULONG SavedMappedLength;
    ULONG ActivePage;
    KIRQL OldIrql;
    LARGE_INTEGER PFileOffset;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    ULONG SavedState;
    BOOLEAN MorePages;
    ULONG SavedTotalLength = Length;
    LARGE_INTEGER LocalOffset = *FileOffset;
    ULONG PageOffset = FileOffset->LowPart & (PAGE_SIZE - 1);
    PVACB Vacb = NULL;
    PETHREAD Thread = PsGetCurrentThread();

    //
    //  Initialize SavePage to TRUE to skip the finally clause on zero-length
    //  writes.
    //

    BOOLEAN SavePage = TRUE;

    DebugTrace(+1, me, "CcMapAndCopy:\n", 0 );
    DebugTrace( 0, me, "    SharedCacheMap = %08lx\n", SharedCacheMap );
    DebugTrace2(0, me, "    FileOffset = %08lx, %08lx\n", FileOffset->LowPart,
                                                          FileOffset->HighPart );
    DebugTrace( 0, me, "    Length = %08lx\n", Length );

    MmSavePageFaultReadAhead( Thread, &SavedState );

    //
    //  try around everything for cleanup.
    //

    try {

        while (Length != 0) {

            CacheBuffer = CcGetVirtualAddress( SharedCacheMap,
                                               LocalOffset,
                                               &Vacb,
                                               &ReceivedLength );

            //
            //  If we got more than we need, make sure to only use
            //  the right amount.
            //

            if (ReceivedLength > Length) {
                ReceivedLength = Length;
            }
            SavedMappedBuffer = CacheBuffer;
            SavedMappedLength = ReceivedLength;
            Length -= ReceivedLength;

            //
            //  Now loop to touch all of the pages, calling MM to insure
            //  that if we fault, we take in exactly the number of pages
            //  we need.
            //

            CacheBuffer = (PVOID)((PCHAR)CacheBuffer - PageOffset);
            ReceivedLength += PageOffset;

            //
            //  Loop to touch or zero the pages.
            //

            ZeroCase = ZERO_FIRST_PAGE;

            //
            //  Set up offset to page for use below.
            //

            PFileOffset = LocalOffset;
            PFileOffset.LowPart -= PageOffset;

            while (TRUE) {

                //
                //  Calculate whether we wish to save an active page
                //  or not.
                //

                SavePage = ((Length == 0) &&
                            (ReceivedLength < PAGE_SIZE) &&
                            (SavedTotalLength <= (PAGE_SIZE / 2)) &&
                            !WriteThrough &&
                            (SharedCacheMap->FileObject->SectionObjectPointer->ImageSectionObject == NULL) &&
                            (SharedCacheMap->Mbcb != NULL) &&
                            ((ULONG)((ULONGLONG)PFileOffset.QuadPart >> PAGE_SHIFT) <
                             (SharedCacheMap->Mbcb->Bitmap.SizeOfBitMap - 1)));

                MorePages = (ReceivedLength > PAGE_SIZE);

                //
                //  Copy the data to the user buffer.
                //

                try {

                    //
                    //  It is possible that there is a locked page
                    //  hanging around, and so we need to nuke it here.
                    //

                    if (SharedCacheMap->NeedToZero != NULL) {
                        CcFreeActiveVacb( SharedCacheMap, NULL, 0, 0 );
                    }

                    Status = STATUS_SUCCESS;
                    if (FlagOn(ZeroFlags, ZeroCase)) {

                        Status = MmCopyToCachedPage( CacheBuffer,
                                                     UserBuffer,
                                                     PageOffset,
                                                     MorePages ?
                                                       (PAGE_SIZE - PageOffset) :
                                                       (ReceivedLength - PageOffset),
                                                     SavePage );

                        if (!NT_SUCCESS(Status)) {

                            ExRaiseStatus( FsRtlNormalizeNtstatus( Status,
                                                                   STATUS_INVALID_USER_BUFFER ));
                        }

                    //
                    //  Otherwise, we have to actually copy the data ourselves.
                    //

                    } else {

                        MmSetPageFaultReadAhead( Thread,
                                                 (MorePages && FlagOn(ZeroFlags, ZERO_LAST_PAGE)) ? 1 : 0);

                        RtlCopyBytes( (PVOID)((PCHAR)CacheBuffer + PageOffset),
                                      UserBuffer,
                                      MorePages ?
                                        (PAGE_SIZE - PageOffset) :
                                        (ReceivedLength - PageOffset) );

                        MmResetPageFaultReadAhead( Thread, SavedState );

                    }

                } except( CcCopyReadExceptionFilter( GetExceptionInformation(),
                                                     &Status ) ) {

                    //
                    //  If we got an access violation, then the user buffer went
                    //  away.  Otherwise we must have gotten an I/O error trying
                    //  to bring the data in.
                    //

                    if (Status == STATUS_ACCESS_VIOLATION) {
                        ExRaiseStatus( STATUS_INVALID_USER_BUFFER );
                    }
                    else {
                        ExRaiseStatus( FsRtlNormalizeNtstatus( Status,
                                                               STATUS_UNEXPECTED_IO_ERROR ));
                    }
                }

                //
                //  Now get out quickly if it is a small write and we want
                //  to save the page.
                //

                if (SavePage) {

                    ActivePage = (ULONG)( (ULONGLONG)Vacb->Overlay.FileOffset.QuadPart >> PAGE_SHIFT ) +
                                 (((PCHAR)CacheBuffer - (PCHAR)Vacb->BaseAddress) >>
                                   PAGE_SHIFT);

                    PFileOffset.LowPart += ReceivedLength;

                    //
                    //  If the cache page was not locked, then clear the address
                    //  to zero from.
                    //

                    if (Status == STATUS_CACHE_PAGE_LOCKED) {

                        ExAcquireFastLock( &SharedCacheMap->ActiveVacbSpinLock, &OldIrql );

                        ASSERT(SharedCacheMap->NeedToZero == NULL);

                        SharedCacheMap->NeedToZero = (PVOID)((PCHAR)CacheBuffer +
                                                             (PFileOffset.LowPart & (PAGE_SIZE - 1)));
                        SharedCacheMap->NeedToZeroPage = ActivePage;
                        ExReleaseFastLock( &SharedCacheMap->ActiveVacbSpinLock, OldIrql );
                    }

                    SetActiveVacb( SharedCacheMap,
                                   OldIrql,
                                   Vacb,
                                   ActivePage,
                                   ACTIVE_PAGE_IS_DIRTY );

                    try_return( NOTHING );
                }

                //
                //  If it looks like we may save a page and exit on the next loop,
                //  then we must make sure to mark the current page dirty.  Note
                //  that Cc[Fast]CopyWrite will finish the last part of any page
                //  before allowing us to free the Active Vacb above, therefore
                //  this case only occurs for a small random write.
                //

                if ((SavedTotalLength <= (PAGE_SIZE / 2)) && !WriteThrough) {

                    CcSetDirtyInMask( SharedCacheMap, &PFileOffset, ReceivedLength );
                }

                UserBuffer = (PVOID)((PCHAR)UserBuffer + (PAGE_SIZE - PageOffset));
                PageOffset = 0;

                //
                //  If there is more than a page to go (including what we just
                //  copied), then adjust our buffer pointer and counts, and
                //  determine if we are to the last page yet.
                //

                if (MorePages) {

                    CacheBuffer = (PCHAR)CacheBuffer + PAGE_SIZE;
                    ReceivedLength -= PAGE_SIZE;

                    //
                    //  Update our offset to the page.  Note that 32-bit
                    //  add is ok since we cannot cross a Vacb boundary
                    //  and we reinitialize this offset before entering
                    //  this loop again.
                    //

                    PFileOffset.LowPart += PAGE_SIZE;

                    if (ReceivedLength > PAGE_SIZE) {
                        ZeroCase = ZERO_MIDDLE_PAGES;
                    } else {
                        ZeroCase = ZERO_LAST_PAGE;
                    }

                } else {

                    break;
                }
            }

            //
            //  If there is still more to write (ie. we are going to step
            //  onto the next vacb) AND we just dirtied more than 64K, then
            //  do a vicarious MmFlushSection here.  This prevents us from
            //  creating unlimited dirty pages while holding the file
            //  resource exclusive.  We also do not need to set the pages
            //  dirty in the mask in this case.
            //

            if (Length > CcMaxDirtyWrite) {

                MmSetAddressRangeModified( SavedMappedBuffer, SavedMappedLength );
                MmFlushSection( SharedCacheMap->FileObject->SectionObjectPointer,
                                &LocalOffset,
                                SavedMappedLength,
                                &IoStatus,
                                TRUE );

                if (!NT_SUCCESS(IoStatus.Status)) {
                    ExRaiseStatus( FsRtlNormalizeNtstatus( IoStatus.Status,
                                                           STATUS_UNEXPECTED_IO_ERROR ));
                }

            //
            //  For write through files, call Mm to propagate the dirty bits
            //  here while we have the view mapped, so we know the flush will
            //  work below.  Again - do not set dirty in the mask.
            //

            } else if (WriteThrough) {

                MmSetAddressRangeModified( SavedMappedBuffer, SavedMappedLength );

            //
            //  For the normal case, just set the pages dirty for the Lazy Writer
            //  now.
            //

            } else {

                CcSetDirtyInMask( SharedCacheMap, &LocalOffset, SavedMappedLength );
            }

            CcFreeVirtualAddress( Vacb );
            Vacb = NULL;

            //
            //  If we have to loop back to get at least a page, it will be ok to
            //  zero the first page.  If we are not getting at least a page, we
            //  must make sure we clear the ZeroFlags if we cannot zero the last
            //  page.
            //

            if (Length >= PAGE_SIZE) {
                ZeroFlags |= ZERO_FIRST_PAGE;
            } else if ((ZeroFlags & ZERO_LAST_PAGE) == 0) {
                ZeroFlags = 0;
            }

            //
            //  Note that if ReceivedLength (and therefore SavedMappedLength)
            //  was truncated to the transfer size then the new LocalOffset
            //  computed below is not correct.  This is not an issue since
            //  in that case (Length == 0) and we would never get here.
            //

            LocalOffset.QuadPart = LocalOffset.QuadPart + (LONGLONG)SavedMappedLength;
        }
    try_exit: NOTHING;
    }

    //
    //  Cleanup on the way out.
    //

    finally {

        MmResetPageFaultReadAhead( Thread, SavedState );

        //
        //  We have no work to do if we have squirreled away the Vacb.
        //

        if (!SavePage || AbnormalTermination()) {

            //
            //  Make sure we do not leave anything mapped or dirty in the PTE
            //  on the way out.
            //

            if (Vacb != NULL) {

                CcFreeVirtualAddress( Vacb );
            }

            //
            //  Either flush the whole range because of write through, or
            //  mark it dirty for the lazy writer.
            //

            if (WriteThrough) {

                MmFlushSection ( SharedCacheMap->FileObject->SectionObjectPointer,
                                 FileOffset,
                                 SavedTotalLength,
                                 &IoStatus,
                                 TRUE );

                if (!NT_SUCCESS(IoStatus.Status)) {
                    ExRaiseStatus( FsRtlNormalizeNtstatus( IoStatus.Status,
                                                           STATUS_UNEXPECTED_IO_ERROR ));
                }

                //
                //  Advance ValidDataGoal
                //

                LocalOffset.QuadPart = FileOffset->QuadPart + (LONGLONG)SavedTotalLength;
                if (LocalOffset.QuadPart > SharedCacheMap->ValidDataGoal.QuadPart) {
                    SharedCacheMap->ValidDataGoal = LocalOffset;
                }
            }
        }
    }

    DebugTrace(-1, me, "CcMapAndCopy -> %02lx\n", Result );

    return;
}


#ifdef CCDBG
VOID
CcDump (
    IN PVOID Ptr
    )

{
    PVOID Junk = Ptr;
}
#endif
