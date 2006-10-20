/* 
** NetXMS - Network Management System
** Client Library
** Copyright (C) 2004 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** $module: libnxcl.h
**
**/

#ifndef _libnxcl_h_
#define _libnxcl_h_

#include <nms_common.h>
#include <nxclapi.h>
#include <nms_util.h>
#include <nms_threads.h>
#include <nxcpapi.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


//
// Constants
//

#define OBJECT_CACHE_MAGIC 0x8899DEBC

#define MAX_SERVER_NAME    64
#define MAX_LOGIN_NAME     64
#define MAX_PASSWORD_LEN   64

#define VALIDATE_STRING(s) (((s) == NULL) ? _T("") : (s))


//
// Session flags
//

#define NXC_SF_USERDB_LOADED     0x0001
#define NXC_SF_HAS_OBJECT_CACHE  0x0002
#define NXC_SF_CHANGE_PASSWD     0x0004
#define NXC_SF_CONN_BROKEN       0x0008
#define NXC_SF_BAD_DBCONN        0x0010


//
// Sync operations
//

#define SYNC_EVENTS     0
#define SYNC_OBJECTS    1
#define SYNC_SYSLOG     2
#define SYNC_TRAP_LOG   3
#define SYNC_EVENT_DB   4
#define SYNC_USER_DB    5
#define SYNC_DCI_LIST   6

#define SYNC_OP_COUNT   7


//
// Index structure
//

typedef struct
{
   DWORD dwKey;
   NXC_OBJECT *pObject;
} INDEX;


//
// Object cache file header structure
//

typedef struct
{
   DWORD dwMagic;
   DWORD dwStructSize;  // sizeof(NXC_OBJECT)
   DWORD dwTimeStamp;
   DWORD dwNumObjects;
   BYTE bsServerId[8];
} OBJECT_CACHE_HEADER;


//
// Session class
//

class NXCL_Session
{
   friend THREAD_RESULT THREAD_CALL NetReceiver(NXCL_Session *pSession);

   // Attributes
private:
   DWORD m_dwFlags;
   DWORD m_dwMsgId;
   DWORD m_dwState;
   DWORD m_dwTimeStamp;    // Last known server's timestamp
   DWORD m_dwNumObjects;
   INDEX *m_pIndexById;
   MUTEX m_mutexIndexAccess;
   SOCKET m_hSocket;
   CSCP_ENCRYPTION_CONTEXT *m_pCtx;
   MsgWaitQueue m_msgWaitQueue;
   DWORD m_dwReceiverBufferSize;
   NXC_DCI_LIST *m_pItemList;
   THREAD m_hRecvThread;
   THREAD m_hWatchdogThread;
   CONDITION m_condStopThreads;
   void *m_pClientData;       // Client-defined data

   DWORD m_dwUserId;          // Id of logged-in user
   DWORD m_dwSystemAccess;    // System access rights for current user

   int m_hCurrFile;
   DWORD m_dwFileRqId;
   DWORD m_dwFileRqCompletion;
   CONDITION m_condFileRq;
   MUTEX m_mutexFileRq;

   NXC_EVENT_TEMPLATE **m_ppEventTemplates;
   DWORD m_dwNumTemplates;
   MUTEX m_mutexEventAccess;

   DWORD m_dwNumUsers;
   NXC_USER *m_pUserList;

   MUTEX m_mutexSyncOpAccess[SYNC_OP_COUNT];
   DWORD m_dwSyncExitCode[SYNC_OP_COUNT];
#ifdef _WIN32
   HANDLE m_condSyncOp[SYNC_OP_COUNT];
#else
   pthread_cond_t m_condSyncOp[SYNC_OP_COUNT];
   pthread_mutex_t m_mutexSyncOp[SYNC_OP_COUNT];
   BOOL m_bSyncFinished[SYNC_OP_COUNT];
#endif

public:
   DWORD m_dwCommandTimeout;
   NXC_EVENT_HANDLER m_pEventHandler;
   BYTE m_bsServerId[8];

   // Methods
private:
   void DestroyAllObjects(void);
   void ProcessDCI(CSCPMessage *pMsg);
   void DestroyEventDB(void);
   void DestroyUserDB(void);
   void ProcessUserDBRecord(CSCPMessage *pMsg);
   void ProcessUserDBUpdate(CSCPMessage *pMsg);

   void ProcessObjectUpdate(CSCPMessage *pMsg);
   void AddObject(NXC_OBJECT *pObject, BOOL bSortIndex);
   void LoadObjectsFromCache(TCHAR *pszCacheFile);

   void WatchdogThread(void);
   static THREAD_RESULT THREAD_CALL WatchdogThreadStarter(void *pArg);

public:
   NXCL_Session();
   ~NXCL_Session();

   void Attach(SOCKET hSocket) { m_hSocket = hSocket; }
   void Disconnect(void);

   void SetRecvThread(THREAD hThread) { m_hRecvThread = hThread; }
   void StartWatchdogThread(void);

   BOOL SendMsg(CSCPMessage *pMsg);
   CSCPMessage *WaitForMessage(WORD wCode, DWORD dwId, DWORD dwTimeOut = 0);
   CSCP_MESSAGE *WaitForRawMessage(WORD wCode, DWORD dwId, DWORD dwTimeOut = 0);
   DWORD WaitForRCC(DWORD dwRqId, DWORD dwTimeOut = 0);
   DWORD CreateRqId(void) { return m_dwMsgId++; }
   DWORD SendFile(DWORD dwRqId, TCHAR *pszFileName);
   DWORD SimpleCommand(WORD wCmd);

   void CallEventHandler(DWORD dwEvent, DWORD dwCode, void *pArg);

   DWORD WaitForSync(int nSyncOp, DWORD dwTimeOut);
   void PrepareForSync(int nSyncOp);
   void CompleteSync(int nSyncOp, DWORD dwRetCode);
   void UnlockSyncOp(int nSyncOp) { MutexUnlock(m_mutexSyncOpAccess[nSyncOp]); }

   DWORD OpenNodeDCIList(DWORD dwNodeId, NXC_DCI_LIST **ppItemList);

   DWORD LoadEventDB(void);
   void AddEventTemplate(NXC_EVENT_TEMPLATE *pEventTemplate, BOOL bLock);
   void DeleteEDBRecord(DWORD dwEventCode);
   BOOL GetEventDB(NXC_EVENT_TEMPLATE ***pppTemplateList, DWORD *pdwNumRecords);
   const TCHAR *GetEventName(DWORD dwId);
   BOOL GetEventNameEx(DWORD dwId, TCHAR *pszBuffer, DWORD dwBufSize);
   int GetEventSeverity(DWORD dwId);
   BOOL GetEventText(DWORD dwId, TCHAR *pszBuffer, DWORD dwBufSize);

   DWORD LoadUserDB(void);
   BOOL GetUserDB(NXC_USER **ppUserList, DWORD *pdwNumUsers);
   NXC_USER *FindUserById(DWORD dwId);

   DWORD SyncObjects(TCHAR *pszCacheFile);
   void LockObjectIndex(void) { MutexLock(m_mutexIndexAccess, INFINITE); }
   void UnlockObjectIndex(void) { MutexUnlock(m_mutexIndexAccess); }
   NXC_OBJECT *FindObjectById(DWORD dwId, BOOL bLock);
   NXC_OBJECT *FindObjectByName(TCHAR *pszName);
   void EnumerateObjects(BOOL (* pHandler)(NXC_OBJECT *));
   NXC_OBJECT *GetRootObject(DWORD dwId, DWORD dwIndex);
   void *GetObjectIndex(DWORD *pdwNumObjects);

   void SetTimeStamp(DWORD dwTimeStamp) { m_dwTimeStamp = dwTimeStamp; }
   DWORD GetTimeStamp(void) { return m_dwTimeStamp; }

   DWORD SetSubscriptionStatus(DWORD dwChannels, int nOperation);

   DWORD PrepareFileTransfer(TCHAR *pszFile, DWORD dwRqId);
   DWORD WaitForFileTransfer(DWORD dwTimeout);
   void AbortFileTransfer(void);

   void SetClientData(void *pData) { m_pClientData = pData; }
   void *GetClientData(void) { return m_pClientData; }

   void OnNotify(CSCPMessage *pMsg);
   void ParseLoginMessage(CSCPMessage *pMsg);
   DWORD GetCurrentUserId(void) { return m_dwUserId; }
   DWORD GetCurrentSystemAccess(void) { return m_dwSystemAccess; }
   BOOL NeedPasswordChange(void) { return (m_dwFlags & NXC_SF_CHANGE_PASSWD) ? TRUE : FALSE; }
   BOOL IsDBConnLost(void) { return (m_dwFlags & NXC_SF_BAD_DBCONN) ? TRUE : FALSE; }
};

inline void NXCL_Session::CallEventHandler(DWORD dwEvent, DWORD dwCode, void *pArg)
{
   if (m_pEventHandler != NULL)
      m_pEventHandler(this, dwEvent, dwCode, pArg);
}


//
// Functions
//

void DestroyObject(NXC_OBJECT *pObject);
void UpdateUserFromMessage(CSCPMessage *pMsg, NXC_USER *pUser);

void ProcessAlarmUpdate(NXCL_Session *pSession, CSCPMessage *pMsg);
void ProcessEvent(NXCL_Session *pSession, CSCPMessage *pMsg, CSCP_MESSAGE *pRawMsg);
void ProcessSyslogRecords(NXCL_Session *pSession, CSCPMessage *pMsg);
void ProcessTrapLogRecords(NXCL_Session *pSession, CSCPMessage *pMsg);
void ProcessActionUpdate(NXCL_Session *pSession, CSCPMessage *pMsg);
void ProcessEventDBRecord(NXCL_Session *pSession, CSCPMessage *pMsg);
void ProcessUserDBUpdate(CSCPMessage *pMsg);
void ProcessDCI(NXCL_Session *pSession, CSCPMessage *pMsg);

void DebugPrintf(TCHAR *szFormat, ...);


//
// Global variables
//

extern NXC_DEBUG_CALLBACK g_pDebugCallBack;


#endif   /* _libnxcl_h_ */
