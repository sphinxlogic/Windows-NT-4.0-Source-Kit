/*

Copyright (c) 1992  Microsoft Corporation

Module Name:

	sda.c

Abstract:

	This module contains the session data area manipulation routines.

Author:

	Jameel Hyder (microsoft!jameelh)


Revision History:
	25 Apr 1992		Initial Version

Notes:	Tab stop: 4

--*/

#define	_SDA_LOCALS
#define	FILENUM	FILE_SDA

#include <afp.h>
#include <scavengr.h>
#include <atalkio.h>
#include <access.h>

#ifdef ALLOC_PRAGMA
#pragma alloc_text( INIT, AfpSdaInit)
#pragma alloc_text( PAGE, afpCloseSessionAndFreeSda)
#pragma alloc_text( PAGE, AfpSdaCheckSession)
#pragma alloc_text( PAGE_AFP, AfpAdmWSessionClose)
#endif

/***	AfpSdaInit
 *
 *	Initialize Sda Data structures. Called at init time.
 */
NTSTATUS
AfpSdaInit(
	VOID
)
{
	ASSERT(sizeof(SDA) <= 512);
	INITIALIZE_SPIN_LOCK(&AfpSdaLock);

	return STATUS_SUCCESS;
}


/***	AfpSdaReferenceSessionById
 *
 *	Reference a session by its id. The SDA cannot be referenced if it is
 *	marked closed. The SDA should be referenced before using it.
 *
 *	LOCKS:		AfpSdaLock (SPIN), sda_Lock (SPIN)
 *	LOCK_ORDER:	sda_Lock after AfpSdaLock
 */
PSDA FASTCALL
AfpSdaReferenceSessionById(
	IN DWORD	SessId
)
{
	PSDA	pSda;
	KIRQL	OldIrql;

	ACQUIRE_SPIN_LOCK(&AfpSdaLock, &OldIrql);

	for (pSda = AfpSessionList;
		 (pSda != NULL) && (pSda->sda_SessionId >= SessId);
		 pSda = pSda->sda_Next)
	{
		if (pSda->sda_SessionId == SessId)
		{
			ACQUIRE_SPIN_LOCK_AT_DPC(&pSda->sda_Lock);

			ASSERT((pSda->sda_RefCount > 0) &&
				   (pSda->sda_SessionId != 0));

			pSda->sda_RefCount ++;

			DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO,
				("AfpSdaReferenceSessionById: New Count %d\n", pSda->sda_RefCount));

			RELEASE_SPIN_LOCK_FROM_DPC(&pSda->sda_Lock);
			break;
		}
	}

	RELEASE_SPIN_LOCK(&AfpSdaLock, OldIrql);

	return pSda;
}



/***	afpSdaCloseSessionAndFreeSda
 *
 *	Run by the scavenger in the worker context. This is initiated as part of
 *	session cleanup.
 */
LOCAL AFPSTATUS FASTCALL
afpCloseSessionAndFreeSda(
	IN	PSDA	pSda
)
{
	POPENFORKSESS	pOpenForkSess;

	DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO,
			("afpCloseSessionAndFreeSda: Cleaning up session %lx\n",
			pSda->sda_SessionId));

	PAGED_CODE( );

#ifdef	OPTIMIZE_GUEST_LOGONS
	if (pSda->sda_ClientType != SDA_CLIENT_GUEST)
	{
#endif
#ifndef	INHERIT_DIRECTORY_PERMS
		// First free up paged memory used by the sda
		if (pSda->sda_pSecDesc != NULL)
		{
			if (pSda->sda_pSecDesc->Dacl != NULL)
				AfpFreeMemory(pSda->sda_pSecDesc->Dacl);
			AfpFreeMemory(pSda->sda_pSecDesc);
		}
#endif
		if ((pSda->sda_UserSid != NULL) && (pSda->sda_UserSid != &AfpSidWorld))
			AfpFreeMemory(pSda->sda_UserSid);

		if ((pSda->sda_GroupSid != NULL) && (pSda->sda_GroupSid != &AfpSidWorld))
			AfpFreeMemory(pSda->sda_GroupSid);

		if (pSda->sda_pGroups != NULL)
			AfpFreeMemory(pSda->sda_pGroups);
#ifdef	OPTIMIZE_GUEST_LOGONS
	}
#endif

	// Free any chains of fork entries that we created
	pOpenForkSess = pSda->sda_OpenForkSess.ofs_Link;
	while (pOpenForkSess != NULL)
	{
		POPENFORKSESS	pFree;

		pFree = pOpenForkSess;
		pOpenForkSess = pOpenForkSess->ofs_Link;
		AfpFreeMemory(pFree);
	}

	// Next log-out the user
	DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO,
			("afpCloseSessionAndFreeSda: Closing User Token\n"));

#ifdef	OPTIMIZE_GUEST_LOGON
	if ((pSda->sda_UserToken != NULL) && (pSda->sda_ClientType != SDA_CLIENT_GUEST))
#else
	if (pSda->sda_UserToken != NULL)
#endif
		NtClose(pSda->sda_UserToken);

	// Make sure there are no resource leaks
	ASSERT (pSda->sda_cOpenVolumes == 0);
	ASSERT (pSda->sda_pConnDesc == 0);
	ASSERT (pSda->sda_cOpenForks == 0);

	// Free the sda memory, finally
	AfpFreeMemory(pSda);

	// If the server is stopping and the count of sessions has gone to zero
	// clear the termination confirmation event to unblock the admin thread
	if (((AfpServerState == AFP_STATE_STOP_PENDING) ||
		 (AfpServerState == AFP_STATE_SHUTTINGDOWN)) &&
		(AfpNumSessions == 0))
	{
		DBGPRINT(DBG_COMP_ADMINAPI, DBG_LEVEL_WARN,
			("afpSdaCloseSessionAndFreeSda: Unblocking server stop\n"));
		KeSetEvent(&AfpStopConfirmEvent, IO_NETWORK_INCREMENT, False);
	}

	DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO, ("afpCloseSessionAndFreeSda: Done\n"));

	return AFP_ERR_NONE;
}



/***	AfpSdaDereferenceSession
 *
 *	Dereference this SDA. This is called in response to either a AfpLogout
 *	request, a forced shutdown of this session by either SessionClose() api,
 *	the server shutdown or the session shutdown from the client end.
 *	The Sda should not be dereferenced until the entire request is processed
 *	i.e. the reply has ALSO COMPLETED.
 *
 *	NOTE: We unlink the session from the list but do not update the count till
 *		  the entire cleanup is complete.
 *
 *	LOCKS:		AfpSdaLock (SPIN), sda_Lock (SPIN), AfpStatisticsLock (SPIN)
 */
VOID FASTCALL
AfpSdaDereferenceSession(
	IN PSDA	pSda
)
{
	PSDA *		ppSda;
	KIRQL		OldIrql;
	LONG		RefCount;

	ASSERT(VALID_SDA(pSda));
	ASSERT((pSda->sda_RefCount > 0) &&
		   (pSda->sda_SessionId != 0));

	ACQUIRE_SPIN_LOCK(&pSda->sda_Lock, &OldIrql);
	RefCount = -- (pSda->sda_RefCount);
	RELEASE_SPIN_LOCK(&pSda->sda_Lock, OldIrql);


	DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO,
			("AfpSdaDereferenceSession: Session %ld, New Count %ld\n",
			pSda->sda_SessionId, RefCount));

	// If there are more references, then we are done.
	if (RefCount > 0)
		return;

        ASSERT(RefCount == 0);     // catch refcount going below 0!

	ASSERT ((pSda->sda_ReplyBuf == NULL) &&
			!(pSda->sda_Flags & SDA_REQUEST_IN_PROCESS));

	DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO,
				("AfpSdaDereferenceSession: Closing down session %ld\n",
				pSda->sda_SessionId));

	// Cancel the scavenger event for checking this user's kickoff time
	AfpScavengerKillEvent(AfpSdaCheckSession,
						 (PVOID)(pSda->sda_SessionId));

	// First unlink the Sda from the sessions list
	ACQUIRE_SPIN_LOCK(&AfpSdaLock, &OldIrql);

	for (ppSda = &AfpSessionList;
		 *ppSda != NULL;
		 ppSda = &(*ppSda)->sda_Next)
		if (pSda == *ppSda)
			break;

	ASSERT(*ppSda == pSda);

	*ppSda = pSda->sda_Next;

	// Update the count of active sessions
	ASSERT (AfpNumSessions > 0);
	--AfpNumSessions;

	RELEASE_SPIN_LOCK(&AfpSdaLock, OldIrql);

	// Free all buffers associated with the Sda
	// Now free the SDA memory
	if (pSda->sda_WSName.Buffer != NULL)
		AfpFreeMemory(pSda->sda_WSName.Buffer);

	if ((pSda->sda_ClientType != SDA_CLIENT_GUEST) &&
		(pSda->sda_UserName.Buffer != NULL))
		AfpFreeMemory(pSda->sda_UserName.Buffer);

	if (pSda->sda_Challenge != NULL)
		AfpFreeMemory(pSda->sda_Challenge);

	if (pSda->sda_DomainName.Buffer != NULL)
		AfpFreeMemory(pSda->sda_DomainName.Buffer);

	if (pSda->sda_Message != NULL)
		AfpFreeMemory(pSda->sda_Message);

	// Finally update statistics
	INTERLOCKED_ADD_ULONG((PLONG)&AfpServerStatistics.stat_CurrentSessions,
						  (ULONG)-1,
						  &AfpStatisticsLock);
	// The balance of the clean-up has to happen at LOW_LEVEL in the context
	// of the worker thread. So queue it up
	if ((OldIrql == DISPATCH_LEVEL) ||
		(PsGetCurrentProcess() != AfpProcessObject))
	{
		DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO,
				("AfpSdaDereferenceSession: Queuing Close&Free to Scavenger\n"));

		AfpScavengerScheduleEvent(afpCloseSessionAndFreeSda,
								  (PVOID)pSda,
								  0,
								  True);
	}
	else
	{
		afpCloseSessionAndFreeSda(pSda);
	}
}


/***	AfpSdaCreateNewSession
 *
 *	A new session has been initiated. Allocate and initialize a Sda and return a
 *	pointer to it. The new sda is linked into the list of active sessions.
 *
 *	LOCKS:		AfpSdaLock (SPIN), AfpServerGlobalLock (SPIN)
 *	LOCK_ORDER:	AfpServerGlobalLock after AfpSdaLock
 */
PSDA FASTCALL
AfpSdaCreateNewSession(
	IN	PVOID	SessionHandle
)
{
	KIRQL	OldIrql;
	PSDA	*ppSda, pSda = NULL;

	if ((DWORD)AfpNumSessions < AfpServerMaxSessions)
	{
		if ((pSda = (PSDA)AfpAllocNonPagedMemory(SDA_SIZE)) == NULL)
			return NULL;

		// Initialize SDA fields
		RtlZeroMemory(pSda, sizeof(SDA));
#if DBG
		pSda->Signature = SDA_SIGNATURE;
#endif
		INITIALIZE_SPIN_LOCK(&pSda->sda_Lock);

		// Reference this Session
		pSda->sda_RefCount = 1;

		pSda->sda_Flags = SDA_USER_NOT_LOGGEDIN;
		pSda->sda_SessHandle = SessionHandle;

		InitializeListHead(&pSda->sda_DeferredQueue);
		AfpInitializeWorkItem(&pSda->sda_WorkItem,
							  AfpStartApiProcessing,
							  pSda);
		AfpGetCurrentTimeInMacFormat(&pSda->sda_TimeLoggedOn);
		pSda->sda_tTillKickOff = MAXLONG;

		// Initialize space for request/response
		pSda->sda_SizeNameXSpace = SDA_SIZE - sizeof(SDA);
		pSda->sda_NameXSpace = (PBYTE)pSda + sizeof(SDA);

		ACQUIRE_SPIN_LOCK(&AfpSdaLock, &OldIrql);

		// Assign a new session id to this session
		pSda->sda_SessionId = afpNextSessionId ++;

		ASSERT(pSda->sda_SessionId != 0);

		// Link the Sda into the active session list
		for (ppSda = &AfpSessionList;
			 *ppSda != NULL;
			 ppSda = &((*ppSda)->sda_Next))
		{
			if ((*ppSda)->sda_SessionId < pSda->sda_SessionId)
				break;
		}

		pSda->sda_Next = *ppSda;
		*ppSda = pSda;

		// Update the count of active sessions
		AfpNumSessions++;

		RELEASE_SPIN_LOCK(&AfpSdaLock, OldIrql);

		// Finally update statistics
		ACQUIRE_SPIN_LOCK(&AfpStatisticsLock, &OldIrql);
		AfpServerStatistics.stat_CurrentSessions ++;
		AfpServerStatistics.stat_TotalSessions ++;
		if (AfpServerStatistics.stat_CurrentSessions >
										AfpServerStatistics.stat_MaxSessions)
		AfpServerStatistics.stat_MaxSessions =
										AfpServerStatistics.stat_CurrentSessions;
		RELEASE_SPIN_LOCK(&AfpStatisticsLock, OldIrql);
	}

	return pSda;
}


/***	AfpSdaCloseSession
 *
 *	Setup to close the session
 */
AFPSTATUS FASTCALL
AfpSdaCloseSession(
	IN	PSDA	pSda
)
{
	LONG	ConnRef;
	DWORD	ForkRef, MaxOForkRefNum;

	DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO,
			("AfpSdaCloseSession: Entered for session %lx\n",
			pSda->sda_SessionId));

	// We should be coming in from the scavenger worker in its context i.e. at LOW_LEVEL.
	ASSERT (KeGetCurrentIrql() < DISPATCH_LEVEL);

	// Close down the open forks from this session
    MaxOForkRefNum = pSda->sda_MaxOForkRefNum;
	for (ForkRef = 1; ForkRef <= MaxOForkRefNum; ForkRef++)
	{
		POPENFORKENTRY	pOpenForkEntry;
		KIRQL			OldIrql;

		KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
		pOpenForkEntry = AfpForkReferenceByRefNum(pSda, ForkRef);
		KeLowerIrql(OldIrql);

		if (pOpenForkEntry != NULL)
		{
			DBGPRINT(DBG_COMP_VOLUME, DBG_LEVEL_INFO,
					("AfpSdaCloseSession: Forcing close of fork %lx, id %lx\n",
					ForkRef, pOpenForkEntry->ofe_ForkId));

			AfpForkClose(pOpenForkEntry);

			AfpForkDereference(pOpenForkEntry);
		}
	}

	// Now close down all open volumes.
	// Note that AfpConnectionClose will unlink the ConnDesc from the Sda
	for (ConnRef = 1; ConnRef <= afpLargestVolIdInUse; ConnRef++)
	{
		PCONNDESC	pConnDesc;

		if ((pConnDesc = AfpConnectionReference(pSda, ConnRef)) != NULL)
		{
			AfpConnectionClose(pConnDesc);
			AfpConnectionDereference(pConnDesc);
		}
	}

	DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_INFO, ("AfpCloseSession: Done\n"));

	ASSERT (pSda->sda_Flags & SDA_SESSION_CLOSED);

	// Remove the creation reference. Must be the last
	ASSERT(pSda->sda_RefCount == 1);

	AfpSdaDereferenceSession(pSda);

	return AFP_ERR_NONE;
}


/***	AfpAdmWSessionClose
 *
 *	Close a session forcibly. This is an admin operation and must be queued
 *	up since this can potentially cause filesystem operations that are valid
 *	only in the system process context.
 *
 *	LOCKS:		AfpSdaLock (SPIN), sda_Lock (SPIN)
 *	LOCK_ORDER:	sda_Lock after AfpSdaLock
 */
NTSTATUS
AfpAdmWSessionClose(
	IN	OUT	PVOID	InBuf		OPTIONAL,
	IN	LONG		OutBufLen	OPTIONAL,
	OUT	PVOID		OutBuf		OPTIONAL
)
{
	AFPSTATUS			Status = AFPERR_InvalidId;
	DWORD				SessId;
	PSDA				pSda;
	KIRQL				OldIrql;
	PAFP_SESSION_INFO	pSessInfo = (PAFP_SESSION_INFO)InBuf;
	USHORT				AttnWord;
	BOOLEAN				Shoot = False;

	AttnWord = ATTN_USER_DISCONNECT;
	if ((AfpServerState == AFP_STATE_SHUTTINGDOWN) ||
        (AfpServerState == AFP_STATE_STOP_PENDING))
		AttnWord = ATTN_SERVER_SHUTDOWN;

	if ((SessId = pSessInfo->afpsess_id) != 0)
	{
		if ((pSda = AfpSdaReferenceSessionById(SessId)) != NULL)
		{
			Status = AFP_ERR_NONE;

			ACQUIRE_SPIN_LOCK(&pSda->sda_Lock, &OldIrql);

			if ((pSda->sda_Flags & (SDA_CLOSING | SDA_SESSION_CLOSED | SDA_CLIENT_CLOSE)) == 0)
			{
				Shoot = True;
				pSda->sda_Flags |= SDA_CLOSING | SDA_SESSION_CLOSED;
			}

			RELEASE_SPIN_LOCK(&pSda->sda_Lock, OldIrql);

			if (Shoot)
			{
				// Tell the client (s)he's being shot
				AfpSpSendAttention(pSda, AttnWord, True);

				AfpSpCloseSession(pSda->sda_SessHandle);
			}
			AfpSdaDereferenceSession(pSda);
		}
	}
	else
	{
		PSDA	pSdaNext;

		// Here we want to block incoming sessions while we kill the existing ones
        if (AfpServerBoundToAsp)
        {
            AfpSpDisableListens();
        }

		Status = AFP_ERR_NONE;
		ACQUIRE_SPIN_LOCK(&AfpSdaLock, &OldIrql);

		for (pSda = AfpSessionList; pSda != NULL; pSda = pSdaNext)
		{
			ACQUIRE_SPIN_LOCK_AT_DPC(&pSda->sda_Lock);

			Shoot = False;
			pSdaNext = pSda->sda_Next;
			if ((pSda->sda_Flags & (SDA_CLOSING | SDA_SESSION_CLOSED | SDA_CLIENT_CLOSE)) == 0)
			{
				pSda->sda_Flags |= SDA_CLOSING | SDA_SESSION_CLOSED;
				pSda->sda_RefCount ++;
				Shoot = True;
			}

			RELEASE_SPIN_LOCK_FROM_DPC(&pSda->sda_Lock);

			if (Shoot)
			{
				RELEASE_SPIN_LOCK(&AfpSdaLock, OldIrql);

				// Tell the client (s)he's being shot
                if (AfpServerBoundToAsp)
                {
				    AfpSpSendAttention(pSda, AttnWord, True);

				    AfpSpCloseSession(pSda->sda_SessHandle);
                }

				AfpSdaDereferenceSession(pSda);

				ACQUIRE_SPIN_LOCK(&AfpSdaLock, &OldIrql);
				pSdaNext = AfpSessionList;
			}
		}
		RELEASE_SPIN_LOCK(&AfpSdaLock, OldIrql);

		// Enable new incoming sessions - but only if we are not paused
		if ( (AfpServerBoundToAsp) &&
             ((AfpServerState & (AFP_STATE_PAUSED | AFP_STATE_PAUSE_PENDING)) == 0))
		{
			AfpSpEnableListens();
		}
	}

	return Status;
}


/***	AfpSdaCheckSession
 *
 *	Check if a session needs to be forcibly closed. If we are called by the
 *  scavenger for the first time and our scavenger interval is not
 *  SESSION_CHECK_TIME, we need to reschedule ourselves to run at this
 *	interval.
 *
 *	Since the sda_tTillKickOff is only used here, it needs
 *	no protection.
 */
AFPSTATUS FASTCALL
AfpSdaCheckSession(
	IN	PVOID	pContext
)
{
	PSDA				pSda;
	DWORD				SessionId = (DWORD)pContext;
	AFPSTATUS			Status = AFP_ERR_REQUEUE;
	BOOLEAN				RequeueOurself = False;

	PAGED_CODE( );

	if ((pSda = AfpSdaReferenceSessionById(SessionId)) != NULL)
	{
		if (pSda->sda_tTillKickOff > SESSION_WARN_TIME)
		{
			// This is the first time this routine has run, and it was
			// originally scheduled to run at a time greater than
			// SESSION_WARN_TIME.  Therefore, we need to reschedule this
			// scavenger event ourselves with a new SESSION_CHECK_TIME
			// interval.  This is the only time we need to do this.
			pSda->sda_tTillKickOff = SESSION_WARN_TIME;
			Status = AFP_ERR_NONE;
			RequeueOurself = True;
		}
		else if (pSda->sda_tTillKickOff > 0)
			pSda->sda_tTillKickOff -= SESSION_CHECK_TIME;

		ASSERT(pSda->sda_tTillKickOff <= SESSION_WARN_TIME);

		DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_WARN,
				("AfpSdaCheckSession: Below warn level %ld\n",
				pSda->sda_tTillKickOff));

		// If we are at 0, kick this user out. Else just
		// send him a friendly warning.
		if (pSda->sda_tTillKickOff == 0)
		{
			AFP_SESSION_INFO	SessionInfo;

			DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_WARN,
					("AfpSdaCheckSession: Booting session %ld\n", SessionId));

			SessionInfo.afpsess_id = SessionId;
			AfpAdmWSessionClose(&SessionInfo, 0, NULL);
			Status = AFP_ERR_NONE;		// Do not reschedule
		}
		else
		{
			DBGPRINT(DBG_COMP_SDA, DBG_LEVEL_WARN,
					("AfpSdaCheckSession: Warning session %ld\n", SessionId));

			AfpSpSendAttention(pSda,
							   (USHORT)(ATTN_USER_DISCONNECT |
										((pSda->sda_tTillKickOff/60) & ATTN_TIME_MASK)),
								True);
		}

		if (RequeueOurself)
		{
			AfpScavengerScheduleEvent(
							AfpSdaCheckSession,
							(PVOID)pSda->sda_SessionId,
							SESSION_CHECK_TIME,
							True);
		}

		AfpSdaDereferenceSession(pSda);
	}

	return Status;
}
