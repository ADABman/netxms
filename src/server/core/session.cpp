/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2010 Victor Kirhenshtein
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
** File: session.cpp
**
**/

#include "nxcore.h"

#ifdef _WIN32
#include <psapi.h>
#define write	_write
#define close	_close
#else
#include <dirent.h>
#endif

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif


// WARNING! this hack works only for d2i_X509(); be carefull when adding new code
#ifdef OPENSSL_CONST
# undef OPENSSL_CONST
#endif
#if OPENSSL_VERSION_NUMBER >= 0x0090800fL
# define OPENSSL_CONST const
#else
# define OPENSSL_CONST
#endif


//
// Constants
//

#define TRAP_CREATE     1
#define TRAP_UPDATE     2
#define TRAP_DELETE     3

#define RAW_MSG_SIZE    262144


//
// Externals
//

extern Queue g_statusPollQueue;
extern Queue g_configPollQueue;
extern Queue g_routePollQueue;
extern Queue g_discoveryPollQueue;
extern Queue g_nodePollerQueue;
extern Queue g_conditionPollerQueue;
extern Queue *g_pItemQueue;

void UnregisterSession(DWORD dwIndex);
void ResetDiscoveryPoller(void);


//
// Node poller start data
//

typedef struct
{
   ClientSession *pSession;
   Node *pNode;
   int iPollType;
   DWORD dwRqId;
} POLLER_START_DATA;


//
// Additional processing thread strt data
//

typedef struct
{
	ClientSession *pSession;
	CSCPMessage *pMsg;
} PROCTHREAD_START_DATA;


//
// Object tool acl entry
//

typedef struct
{
   DWORD dwToolId;
   DWORD dwUserId;
} OBJECT_TOOL_ACL;


//
// Graph ACL entry
//

struct GRAPH_ACL_ENTRY
{
	DWORD dwGraphId;
	DWORD dwUserId;
	DWORD dwAccess;
};


//
// Additional message processing thread starters
//

#define CALL_IN_NEW_THREAD(func, msg) \
{ \
	PROCTHREAD_START_DATA *pData = (PROCTHREAD_START_DATA *)malloc(sizeof(PROCTHREAD_START_DATA)); \
	pData->pSession = this; \
	pData->pMsg = msg; \
	msg = NULL; /* prevent deletion by main processing thread*/ \
	m_dwRefCount++; \
	ThreadCreate(ThreadStarter_##func, 0, pData); \
}

#define DEFINE_THREAD_STARTER(func) \
THREAD_RESULT THREAD_CALL ClientSession::ThreadStarter_##func(void *pArg) \
{ \
   ((PROCTHREAD_START_DATA *)pArg)->pSession->func(((PROCTHREAD_START_DATA *)pArg)->pMsg); \
	((PROCTHREAD_START_DATA *)pArg)->pSession->m_dwRefCount--; \
	delete ((PROCTHREAD_START_DATA *)pArg)->pMsg; \
	free(pArg); \
   return THREAD_OK; \
}

DEFINE_THREAD_STARTER(GetCollectedData)
DEFINE_THREAD_STARTER(ClearDCIData)
DEFINE_THREAD_STARTER(QueryL2Topology)
DEFINE_THREAD_STARTER(SendEventLog)
DEFINE_THREAD_STARTER(SendSyslog)
DEFINE_THREAD_STARTER(createObject)
DEFINE_THREAD_STARTER(getServerFile)
DEFINE_THREAD_STARTER(queryServerLog)
DEFINE_THREAD_STARTER(getServerLogQueryData)
DEFINE_THREAD_STARTER(executeAction)
DEFINE_THREAD_STARTER(findNodeConnection)
DEFINE_THREAD_STARTER(findMacAddress)


//
// Client communication read thread starter
//

THREAD_RESULT THREAD_CALL ClientSession::ReadThreadStarter(void *pArg)
{
   ((ClientSession *)pArg)->ReadThread();

   // When ClientSession::ReadThread exits, all other session
   // threads are already stopped, so we can safely destroy
   // session object
   UnregisterSession(((ClientSession *)pArg)->getIndex());
   delete (ClientSession *)pArg;
   return THREAD_OK;
}


//
// Client communication write thread starter
//

THREAD_RESULT THREAD_CALL ClientSession::WriteThreadStarter(void *pArg)
{
   ((ClientSession *)pArg)->WriteThread();
   return THREAD_OK;
}


//
// Received message processing thread starter
//

THREAD_RESULT THREAD_CALL ClientSession::ProcessingThreadStarter(void *pArg)
{
   ((ClientSession *)pArg)->ProcessingThread();
   return THREAD_OK;
}


//
// Information update processing thread starter
//

THREAD_RESULT THREAD_CALL ClientSession::UpdateThreadStarter(void *pArg)
{
   ((ClientSession *)pArg)->UpdateThread();
   return THREAD_OK;
}


//
// Forced node poll thread starter
//

THREAD_RESULT THREAD_CALL ClientSession::PollerThreadStarter(void *pArg)
{
   ((POLLER_START_DATA *)pArg)->pSession->PollerThread(
      ((POLLER_START_DATA *)pArg)->pNode,
      ((POLLER_START_DATA *)pArg)->iPollType,
      ((POLLER_START_DATA *)pArg)->dwRqId);
   ((POLLER_START_DATA *)pArg)->pSession->decRefCount();
   free(pArg);
   return THREAD_OK;
}


//
// Client session class constructor
//

ClientSession::ClientSession(SOCKET hSocket, DWORD dwHostAddr)
{
   m_pSendQueue = new Queue;
   m_pMessageQueue = new Queue;
   m_pUpdateQueue = new Queue;
   m_hSocket = hSocket;
   m_dwIndex = INVALID_INDEX;
   m_iState = SESSION_STATE_INIT;
   m_pMsgBuffer = (CSCP_BUFFER *)malloc(sizeof(CSCP_BUFFER));
   m_pCtx = NULL;
   m_hWriteThread = INVALID_THREAD_HANDLE;
   m_hProcessingThread = INVALID_THREAD_HANDLE;
   m_hUpdateThread = INVALID_THREAD_HANDLE;
   m_mutexSendEvents = MutexCreate();
   m_mutexSendSyslog = MutexCreate();
   m_mutexSendTrapLog = MutexCreate();
   m_mutexSendObjects = MutexCreate();
   m_mutexSendAlarms = MutexCreate();
   m_mutexSendActions = MutexCreate();
   m_mutexSendAuditLog = MutexCreate();
   m_mutexSendSituations = MutexCreate();
   m_mutexPollerInit = MutexCreate();
   m_dwFlags = 0;
   m_dwHostAddr = dwHostAddr;
	IpToStr(dwHostAddr, m_szWorkstation);
   _tcscpy(m_szUserName, _T("<not logged in>"));
   m_dwUserId = INVALID_INDEX;
   m_dwOpenDCIListSize = 0;
   m_pOpenDCIList = NULL;
   m_ppEPPRuleList = NULL;
   m_hCurrFile = -1;
   m_dwFileRqId = 0;
   m_dwRefCount = 0;
   m_dwEncryptionRqId = 0;
   m_condEncryptionSetup = INVALID_CONDITION_HANDLE;
   m_dwActiveChannels = 0;
}


//
// Destructor
//

ClientSession::~ClientSession()
{
   if (m_hSocket != -1)
      closesocket(m_hSocket);
   delete m_pSendQueue;
   delete m_pMessageQueue;
   delete m_pUpdateQueue;
   safe_free(m_pMsgBuffer);
   MutexDestroy(m_mutexSendEvents);
   MutexDestroy(m_mutexSendSyslog);
   MutexDestroy(m_mutexSendTrapLog);
   MutexDestroy(m_mutexSendObjects);
   MutexDestroy(m_mutexSendAlarms);
   MutexDestroy(m_mutexSendActions);
   MutexDestroy(m_mutexSendAuditLog);
   MutexDestroy(m_mutexSendSituations);
   MutexDestroy(m_mutexPollerInit);
   safe_free(m_pOpenDCIList);
   if (m_ppEPPRuleList != NULL)
   {
      DWORD i;

      if (m_dwFlags & CSF_EPP_UPLOAD)  // Aborted in the middle of EPP transfer
         for(i = 0; i < m_dwRecordsUploaded; i++)
            delete m_ppEPPRuleList[i];
      free(m_ppEPPRuleList);
   }
   DestroyEncryptionContext(m_pCtx);
   if (m_condEncryptionSetup != INVALID_CONDITION_HANDLE)
      ConditionDestroy(m_condEncryptionSetup);
}


//
// Start all threads
//

void ClientSession::run(void)
{
   m_hWriteThread = ThreadCreateEx(WriteThreadStarter, 0, this);
   m_hProcessingThread = ThreadCreateEx(ProcessingThreadStarter, 0, this);
   m_hUpdateThread = ThreadCreateEx(UpdateThreadStarter, 0, this);
   ThreadCreate(ReadThreadStarter, 0, this);
}


//
// Print debug information
//

void ClientSession::DebugPrintf(int level, const TCHAR *format, ...)
{
   if (level <= g_nDebugLevel)
   {
      va_list args;
		TCHAR buffer[4096];

      va_start(args, format);
      _vsntprintf(buffer, 4096, format, args);
      va_end(args);
		DbgPrintf(level, _T("[CLSN-%d] %s"), m_dwIndex, buffer);
   }
}


//
// ReadThread()
//

void ClientSession::ReadThread(void)
{
   CSCP_MESSAGE *pRawMsg;
   CSCPMessage *pMsg;
   BYTE *pDecryptionBuffer = NULL;
   TCHAR szBuffer[256];
   int iErr;
   DWORD i;
   NetObj *pObject;
   WORD wFlags;

   // Initialize raw message receiving function
   RecvNXCPMessage(0, NULL, m_pMsgBuffer, 0, NULL, NULL, 0);

   pRawMsg = (CSCP_MESSAGE *)malloc(RAW_MSG_SIZE);
#ifdef _WITH_ENCRYPTION
   pDecryptionBuffer = (BYTE *)malloc(RAW_MSG_SIZE);
#endif
   while(1)
   {
      if ((iErr = RecvNXCPMessage(m_hSocket, pRawMsg, 
                                  m_pMsgBuffer, RAW_MSG_SIZE, 
                                  &m_pCtx, pDecryptionBuffer, INFINITE)) <= 0)
         break;

      // Check if message is too large
      if (iErr == 1)
      {
         DebugPrintf(4, _T("Received message %s is too large (%d bytes)"),
                     NXCPMessageCodeName(ntohs(pRawMsg->wCode), szBuffer),
                     ntohl(pRawMsg->dwSize));
         continue;
      }

      // Check for decryption error
      if (iErr == 2)
      {
         DebugPrintf(4, _T("Unable to decrypt received message"));
         continue;
      }

      // Check that actual received packet size is equal to encoded in packet
      if ((int)ntohl(pRawMsg->dwSize) != iErr)
      {
         DebugPrintf(4, _T("Actual message size doesn't match wSize value (%d,%d)"), iErr, ntohl(pRawMsg->dwSize));
         continue;   // Bad packet, wait for next
      }

      // Special handling for raw messages
      wFlags = ntohs(pRawMsg->wFlags);
      if (wFlags & MF_BINARY)
      {
         // Convert message header to host format
         pRawMsg->dwId = ntohl(pRawMsg->dwId);
         pRawMsg->wCode = ntohs(pRawMsg->wCode);
         pRawMsg->dwNumVars = ntohl(pRawMsg->dwNumVars);
         DebugPrintf(6, _T("Received raw message %s"), NXCPMessageCodeName(pRawMsg->wCode, szBuffer));

         if ((pRawMsg->wCode == CMD_FILE_DATA) || 
             (pRawMsg->wCode == CMD_ABORT_FILE_TRANSFER))
         {
            if ((m_hCurrFile != -1) && (m_dwFileRqId == pRawMsg->dwId))
            {
               if (pRawMsg->wCode == CMD_FILE_DATA)
               {
                  if (write(m_hCurrFile, pRawMsg->df, pRawMsg->dwNumVars) == (int)pRawMsg->dwNumVars)
                  {
                     if (wFlags & MF_END_OF_FILE)
                     {
                        CSCPMessage msg;

                        close(m_hCurrFile);
                        m_hCurrFile = -1;
                  
                        msg.SetCode(CMD_REQUEST_COMPLETED);
                        msg.SetId(pRawMsg->dwId);
                        msg.SetVariable(VID_RCC, RCC_SUCCESS);
                        sendMessage(&msg);

                        onFileUpload(TRUE);
                     }
                  }
                  else
                  {
                     // I/O error
                     CSCPMessage msg;

                     close(m_hCurrFile);
                     m_hCurrFile = -1;
               
                     msg.SetCode(CMD_REQUEST_COMPLETED);
                     msg.SetId(pRawMsg->dwId);
                     msg.SetVariable(VID_RCC, RCC_IO_ERROR);
                     sendMessage(&msg);

                     onFileUpload(FALSE);
                  }
               }
               else
               {
                  // Abort current file transfer because of client's problem
                  close(m_hCurrFile);
                  m_hCurrFile = -1;
               
                  onFileUpload(FALSE);
               }
            }
            else
            {
               DebugPrintf(4, _T("Out of state message (ID: %d)"), pRawMsg->dwId);
            }
         }
      }
      else
      {
         // Create message object from raw message
         pMsg = new CSCPMessage(pRawMsg);
         if ((pMsg->GetCode() == CMD_SESSION_KEY) && (pMsg->GetId() == m_dwEncryptionRqId))
         {
            m_dwEncryptionResult = SetupEncryptionContext(pMsg, &m_pCtx, NULL, g_pServerKey, NXCP_VERSION);
            ConditionSet(m_condEncryptionSetup);
            m_dwEncryptionRqId = 0;
            delete pMsg;
         }
         else if (pMsg->GetCode() == CMD_KEEPALIVE)
			{
		      DebugPrintf(6, _T("Received message %s"), NXCPMessageCodeName(pMsg->GetCode(), szBuffer));
				RespondToKeepalive(pMsg->GetId());
				delete pMsg;
			}
			else
         {
            m_pMessageQueue->Put(pMsg);
         }
      }
   }
   if (iErr < 0)
      nxlog_write(MSG_SESSION_CLOSED, EVENTLOG_WARNING_TYPE, "e", WSAGetLastError());
   free(pRawMsg);
#ifdef _WITH_ENCRYPTION
   free(pDecryptionBuffer);
#endif

	// Finish update thread first
   m_pUpdateQueue->Put(INVALID_POINTER_VALUE);
   ThreadJoin(m_hUpdateThread);

   // Notify other threads to exit
	while((pRawMsg = (CSCP_MESSAGE *)m_pSendQueue->Get()) != NULL)
		free(pRawMsg);
   m_pSendQueue->Put(INVALID_POINTER_VALUE);
	while((pMsg = (CSCPMessage *)m_pMessageQueue->Get()) != NULL)
		delete pMsg;
   m_pMessageQueue->Put(INVALID_POINTER_VALUE);

   // Wait for other threads to finish
   ThreadJoin(m_hWriteThread);
   ThreadJoin(m_hProcessingThread);

   // Abort current file upload operation, if any
   if (m_hCurrFile != -1)
   {
      close(m_hCurrFile);
      m_hCurrFile = -1;
      onFileUpload(FALSE);
   }

   // Remove all locks created by this session
   RemoveAllSessionLocks(m_dwIndex);
   for(i = 0; i < m_dwOpenDCIListSize; i++)
   {
      pObject = FindObjectById(m_pOpenDCIList[i]);
      if (pObject != NULL)
         if ((pObject->Type() == OBJECT_NODE) ||
             (pObject->Type() == OBJECT_CLUSTER) ||
             (pObject->Type() == OBJECT_TEMPLATE))
            ((Template *)pObject)->UnlockDCIList(m_dwIndex);
   }

   // Waiting while reference count becomes 0
   if (m_dwRefCount > 0)
   {
      DebugPrintf(3, _T("Waiting for pending requests..."));
      do
      {
         ThreadSleep(1);
      } while(m_dwRefCount > 0);
   }

	WriteAuditLog(AUDIT_SECURITY, TRUE, m_dwUserId, m_szWorkstation, 0, _T("User logged out (client: %s)"), m_szClientInfo);
   DebugPrintf(3, _T("Session closed"));
}


//
// WriteThread()
//

void ClientSession::WriteThread(void)
{
   CSCP_MESSAGE *pRawMsg;
   CSCP_ENCRYPTED_MESSAGE *pEnMsg;
   TCHAR szBuffer[128];
   BOOL bResult;

   while(1)
   {
      pRawMsg = (CSCP_MESSAGE *)m_pSendQueue->GetOrBlock();
      if (pRawMsg == INVALID_POINTER_VALUE)    // Session termination indicator
         break;

      DebugPrintf(6, _T("Sending message %s"), NXCPMessageCodeName(ntohs(pRawMsg->wCode), szBuffer));
      if (m_pCtx != NULL)
      {
         pEnMsg = CSCPEncryptMessage(m_pCtx, pRawMsg);
         if (pEnMsg != NULL)
         {
            bResult = (SendEx(m_hSocket, (char *)pEnMsg, ntohl(pEnMsg->dwSize), 0) == (int)ntohl(pEnMsg->dwSize));
            free(pEnMsg);
         }
         else
         {
            bResult = FALSE;
         }
      }
      else
      {
         bResult = (SendEx(m_hSocket, (const char *)pRawMsg, ntohl(pRawMsg->dwSize), 0) == (int)ntohl(pRawMsg->dwSize));
      }
      free(pRawMsg);

      if (!bResult)
      {
         closesocket(m_hSocket);
         m_hSocket = -1;
         break;
      }
   }
}


//
// Update processing thread
//

void ClientSession::UpdateThread()
{
   UPDATE_INFO *pUpdate;
   CSCPMessage msg;

   while(1)
   {
      pUpdate = (UPDATE_INFO *)m_pUpdateQueue->GetOrBlock();
      if (pUpdate == INVALID_POINTER_VALUE)    // Session termination indicator
         break;

      switch(pUpdate->dwCategory)
      {
         case INFO_CAT_EVENT:
            MutexLock(m_mutexSendEvents, INFINITE);
            sendMessage((CSCPMessage *)pUpdate->pData);
            MutexUnlock(m_mutexSendEvents);
            delete (CSCPMessage *)pUpdate->pData;
            break;
         case INFO_CAT_SYSLOG_MSG:
            MutexLock(m_mutexSendSyslog, INFINITE);
            msg.SetCode(CMD_SYSLOG_RECORDS);
            CreateMessageFromSyslogMsg(&msg, (NX_SYSLOG_RECORD *)pUpdate->pData);
            sendMessage(&msg);
            MutexUnlock(m_mutexSendSyslog);
            free(pUpdate->pData);
            break;
         case INFO_CAT_SNMP_TRAP:
            MutexLock(m_mutexSendTrapLog, INFINITE);
            sendMessage((CSCPMessage *)pUpdate->pData);
            MutexUnlock(m_mutexSendTrapLog);
            delete (CSCPMessage *)pUpdate->pData;
            break;
         case INFO_CAT_AUDIT_RECORD:
            MutexLock(m_mutexSendAuditLog, INFINITE);
            sendMessage((CSCPMessage *)pUpdate->pData);
            MutexUnlock(m_mutexSendAuditLog);
            delete (CSCPMessage *)pUpdate->pData;
            break;
         case INFO_CAT_OBJECT_CHANGE:
            MutexLock(m_mutexSendObjects, INFINITE);
            msg.SetCode(CMD_OBJECT_UPDATE);
            ((NetObj *)pUpdate->pData)->CreateMessage(&msg);
            if (m_dwFlags & CSF_SYNC_OBJECT_COMMENTS)
               ((NetObj *)pUpdate->pData)->CommentsToMessage(&msg);
            sendMessage(&msg);
            MutexUnlock(m_mutexSendObjects);
            msg.DeleteAllVariables();
            ((NetObj *)pUpdate->pData)->DecRefCount();
            break;
         case INFO_CAT_ALARM:
            MutexLock(m_mutexSendAlarms, INFINITE);
            msg.SetCode(CMD_ALARM_UPDATE);
            msg.SetVariable(VID_NOTIFICATION_CODE, pUpdate->dwCode);
            FillAlarmInfoMessage(&msg, (NXC_ALARM *)pUpdate->pData);
            sendMessage(&msg);
            MutexUnlock(m_mutexSendAlarms);
            msg.DeleteAllVariables();
            free(pUpdate->pData);
            break;
         case INFO_CAT_ACTION:
            MutexLock(m_mutexSendActions, INFINITE);
            msg.SetCode(CMD_ACTION_DB_UPDATE);
            msg.SetVariable(VID_NOTIFICATION_CODE, pUpdate->dwCode);
            msg.SetVariable(VID_ACTION_ID, ((NXC_ACTION *)pUpdate->pData)->dwId);
            if (pUpdate->dwCode != NX_NOTIFY_ACTION_DELETED)
               FillActionInfoMessage(&msg, (NXC_ACTION *)pUpdate->pData);
            sendMessage(&msg);
            MutexUnlock(m_mutexSendActions);
            msg.DeleteAllVariables();
            free(pUpdate->pData);
            break;
         case INFO_CAT_SITUATION:
            MutexLock(m_mutexSendSituations, INFINITE);
            sendMessage((CSCPMessage *)pUpdate->pData);
            MutexUnlock(m_mutexSendSituations);
            delete (CSCPMessage *)pUpdate->pData;
            break;
         default:
            break;
      }

      free(pUpdate);
   }
}


//
// Message processing thread
//

void ClientSession::ProcessingThread()
{
   CSCPMessage *pMsg;
   TCHAR szBuffer[128];
   DWORD i;
	int status;

   while(1)
   {
      pMsg = (CSCPMessage *)m_pMessageQueue->GetOrBlock();
      if (pMsg == INVALID_POINTER_VALUE)    // Session termination indicator
         break;

      m_wCurrentCmd = pMsg->GetCode();
      DebugPrintf(6, _T("Received message %s"), NXCPMessageCodeName(m_wCurrentCmd, szBuffer));
      if (!(m_dwFlags & CSF_AUTHENTICATED) && 
          (m_wCurrentCmd != CMD_LOGIN) && 
          (m_wCurrentCmd != CMD_GET_SERVER_INFO) &&
          (m_wCurrentCmd != CMD_REQUEST_ENCRYPTION) &&
          (m_wCurrentCmd != CMD_GET_MY_CONFIG))
      {
         delete pMsg;
         continue;
      }

      m_iState = SESSION_STATE_PROCESSING;
      switch(m_wCurrentCmd)
      {
         case CMD_LOGIN:
            Login(pMsg);
            break;
         case CMD_GET_SERVER_INFO:
            SendServerInfo(pMsg->GetId());
            break;
         case CMD_KEEPALIVE:
            RespondToKeepalive(pMsg->GetId());
            break;
         case CMD_GET_MY_CONFIG:
            SendConfigForAgent(pMsg);
            break;
         case CMD_GET_OBJECTS:
            SendAllObjects(pMsg);
            break;
         case CMD_GET_EVENTS:
            CALL_IN_NEW_THREAD(SendEventLog, pMsg);
            break;
         case CMD_GET_CONFIG_VARLIST:
            SendAllConfigVars(pMsg->GetId());
            break;
         case CMD_SET_CONFIG_VARIABLE:
            SetConfigVariable(pMsg);
            break;
         case CMD_DELETE_CONFIG_VARIABLE:
            DeleteConfigVariable(pMsg);
            break;
			case CMD_CONFIG_GET_CLOB:
				getConfigCLOB(pMsg);
				break;
			case CMD_CONFIG_SET_CLOB:
				setConfigCLOB(pMsg);
				break;
         case CMD_LOAD_EVENT_DB:
            sendEventDB(pMsg->GetId());
            break;
         case CMD_SET_EVENT_INFO:
            modifyEventTemplate(pMsg);
            break;
         case CMD_DELETE_EVENT_TEMPLATE:
            deleteEventTemplate(pMsg);
            break;
         case CMD_GENERATE_EVENT_CODE:
            generateEventCode(pMsg->GetId());
            break;
         case CMD_MODIFY_OBJECT:
            ModifyObject(pMsg);
            break;
         case CMD_SET_OBJECT_MGMT_STATUS:
            changeObjectMgmtStatus(pMsg);
            break;
         case CMD_LOAD_USER_DB:
            SendUserDB(pMsg->GetId());
            break;
         case CMD_CREATE_USER:
            CreateUser(pMsg);
            break;
         case CMD_UPDATE_USER:
            UpdateUser(pMsg);
            break;
         case CMD_DELETE_USER:
            DeleteUser(pMsg);
            break;
         case CMD_LOCK_USER_DB:
            LockUserDB(pMsg->GetId(), TRUE);
            break;
         case CMD_UNLOCK_USER_DB:
            LockUserDB(pMsg->GetId(), FALSE);
            break;
         case CMD_SET_PASSWORD:
            SetPassword(pMsg);
            break;
         case CMD_GET_NODE_DCI_LIST:
            OpenNodeDCIList(pMsg);
            break;
         case CMD_UNLOCK_NODE_DCI_LIST:
            CloseNodeDCIList(pMsg);
            break;
         case CMD_CREATE_NEW_DCI:
         case CMD_MODIFY_NODE_DCI:
         case CMD_DELETE_NODE_DCI:
            ModifyNodeDCI(pMsg);
            break;
         case CMD_SET_DCI_STATUS:
            ChangeDCIStatus(pMsg);
            break;
         case CMD_COPY_DCI:
            CopyDCI(pMsg);
            break;
         case CMD_APPLY_TEMPLATE:
            ApplyTemplate(pMsg);
            break;
         case CMD_GET_DCI_DATA:
            CALL_IN_NEW_THREAD(GetCollectedData, pMsg);
            break;
			case CMD_CLEAR_DCI_DATA:
				CALL_IN_NEW_THREAD(ClearDCIData, pMsg);
				break;
         case CMD_OPEN_EPP:
            OpenEPP(pMsg->GetId());
            break;
         case CMD_CLOSE_EPP:
            CloseEPP(pMsg->GetId());
            break;
         case CMD_SAVE_EPP:
            SaveEPP(pMsg);
            break;
         case CMD_EPP_RECORD:
            ProcessEPPRecord(pMsg);
            break;
         case CMD_GET_MIB_TIMESTAMP:
            SendMIBTimestamp(pMsg->GetId());
            break;
         case CMD_GET_MIB:
            SendMIB(pMsg->GetId());
            break;
         case CMD_CREATE_OBJECT:
            CALL_IN_NEW_THREAD(createObject, pMsg);
            break;
         case CMD_BIND_OBJECT:
            changeObjectBinding(pMsg, TRUE);
            break;
         case CMD_UNBIND_OBJECT:
            changeObjectBinding(pMsg, FALSE);
            break;
         case CMD_ADD_CLUSTER_NODE:
            addClusterNode(pMsg);
            break;
         case CMD_GET_ALL_ALARMS:
            SendAllAlarms(pMsg->GetId(), pMsg->GetVariableShort(VID_IS_ACK));
            break;
         case CMD_GET_ALARM:
            break;
         case CMD_ACK_ALARM:
            AcknowledgeAlarm(pMsg);
            break;
         case CMD_TERMINATE_ALARM:
            TerminateAlarm(pMsg);
            break;
         case CMD_DELETE_ALARM:
            DeleteAlarm(pMsg);
            break;
         case CMD_CREATE_ACTION:
            CreateAction(pMsg);
            break;
         case CMD_MODIFY_ACTION:
            UpdateAction(pMsg);
            break;
         case CMD_DELETE_ACTION:
            DeleteAction(pMsg);
            break;
         case CMD_LOAD_ACTIONS:
            SendAllActions(pMsg->GetId());
            break;
         case CMD_GET_CONTAINER_CAT_LIST:
            SendContainerCategories(pMsg->GetId());
            break;
         case CMD_DELETE_OBJECT:
            deleteObject(pMsg);
            break;
         case CMD_POLL_NODE:
            ForcedNodePoll(pMsg);
            break;
         case CMD_TRAP:
            OnTrap(pMsg);
            break;
         case CMD_WAKEUP_NODE:
            OnWakeUpNode(pMsg);
            break;
         case CMD_CREATE_TRAP:
            EditTrap(TRAP_CREATE, pMsg);
            break;
         case CMD_MODIFY_TRAP:
            EditTrap(TRAP_UPDATE, pMsg);
            break;
         case CMD_DELETE_TRAP:
            EditTrap(TRAP_DELETE, pMsg);
            break;
         case CMD_LOAD_TRAP_CFG:
            SendAllTraps(pMsg->GetId());
            break;
			case CMD_GET_TRAP_CFG_RO:
				SendAllTraps2(pMsg->GetId());
				break;
         case CMD_QUERY_PARAMETER:
            QueryParameter(pMsg);
            break;
         case CMD_LOCK_PACKAGE_DB:
            LockPackageDB(pMsg->GetId(), TRUE);
            break;
         case CMD_UNLOCK_PACKAGE_DB:
            LockPackageDB(pMsg->GetId(), FALSE);
            break;
         case CMD_GET_PACKAGE_LIST:
            SendAllPackages(pMsg->GetId());
            break;
         case CMD_INSTALL_PACKAGE:
            InstallPackage(pMsg);
            break;
         case CMD_REMOVE_PACKAGE:
            RemovePackage(pMsg);
            break;
         case CMD_GET_PARAMETER_LIST:
            SendParametersList(pMsg);
            break;
         case CMD_DEPLOY_PACKAGE:
            DeployPackage(pMsg);
            break;
         case CMD_GET_LAST_VALUES:
            SendLastValues(pMsg);
            break;
         case CMD_GET_USER_VARIABLE:
            GetUserVariable(pMsg);
            break;
         case CMD_SET_USER_VARIABLE:
            SetUserVariable(pMsg);
            break;
         case CMD_DELETE_USER_VARIABLE:
            DeleteUserVariable(pMsg);
            break;
         case CMD_ENUM_USER_VARIABLES:
            EnumUserVariables(pMsg);
            break;
         case CMD_COPY_USER_VARIABLE:
            CopyUserVariable(pMsg);
            break;
         case CMD_CHANGE_IP_ADDR:
            ChangeObjectIP(pMsg);
            break;
         case CMD_REQUEST_ENCRYPTION:
            setupEncryption(pMsg->GetId());
            break;
         case CMD_GET_AGENT_CONFIG:
            GetAgentConfig(pMsg);
            break;
         case CMD_UPDATE_AGENT_CONFIG:
            UpdateAgentConfig(pMsg);
            break;
         case CMD_EXECUTE_ACTION:
            CALL_IN_NEW_THREAD(executeAction, pMsg);
            break;
         case CMD_GET_OBJECT_TOOLS:
            sendObjectTools(pMsg->GetId());
            break;
         case CMD_EXEC_TABLE_TOOL:
            execTableTool(pMsg);
            break;
         case CMD_GET_OBJECT_TOOL_DETAILS:
            sendObjectToolDetails(pMsg);
            break;
         case CMD_UPDATE_OBJECT_TOOL:
            updateObjectTool(pMsg);
            break;
         case CMD_DELETE_OBJECT_TOOL:
            deleteObjectTool(pMsg);
            break;
         case CMD_GENERATE_OBJECT_TOOL_ID:
            generateObjectToolId(pMsg->GetId());
            break;
         case CMD_CHANGE_SUBSCRIPTION:
            ChangeSubscription(pMsg);
            break;
         case CMD_GET_SYSLOG:
            CALL_IN_NEW_THREAD(SendSyslog, pMsg);
            break;
         case CMD_GET_SERVER_STATS:
            SendServerStats(pMsg->GetId());
            break;
         case CMD_GET_SCRIPT_LIST:
            sendScriptList(pMsg->GetId());
            break;
         case CMD_GET_SCRIPT:
            sendScript(pMsg);
            break;
         case CMD_UPDATE_SCRIPT:
            updateScript(pMsg);
            break;
         case CMD_RENAME_SCRIPT:
            renameScript(pMsg);
            break;
         case CMD_DELETE_SCRIPT:
            deleteScript(pMsg);
            break;
         case CMD_GET_SESSION_LIST:
            SendSessionList(pMsg->GetId());
            break;
         case CMD_KILL_SESSION:
            KillSession(pMsg);
            break;
         case CMD_GET_TRAP_LOG:
            SendTrapLog(pMsg);
            break;
         case CMD_START_SNMP_WALK:
            StartSnmpWalk(pMsg);
            break;
			case CMD_GET_MAP_LIST:
				SendMapList(pMsg->GetId());
				break;
         case CMD_RESOLVE_MAP_NAME:
            ResolveMapName(pMsg);
            break;
         case CMD_CREATE_MAP:
            CreateMap(pMsg);
            break;
         case CMD_RENAME_MAP:
            RenameMap(pMsg);
            break;
         case CMD_SAVE_MAP:
            SaveMap(pMsg);
            break;
			case CMD_DELETE_MAP:
				DeleteMap(pMsg);
				break;
         case CMD_SUBMAP_DATA:
            ProcessSubmapData(pMsg);
            break;
         case CMD_LOAD_MAP:
            LoadMap(pMsg);
            break;
         case CMD_UPLOAD_SUBMAP_BK_IMAGE:
            RecvSubmapBkImage(pMsg);
            break;
         case CMD_GET_SUBMAP_BK_IMAGE:
            SendSubmapBkImage(pMsg);
            break;
         case CMD_RESOLVE_DCI_NAMES:
            ResolveDCINames(pMsg);
            break;
			case CMD_GET_DCI_INFO:
				SendDCIInfo(pMsg);
				break;
			case CMD_GET_DCI_THRESHOLDS:
				sendDCIThresholds(pMsg);
				break;
         case CMD_GET_DCI_EVENTS_LIST:
            sendDCIEventList(pMsg);
            break;
			case CMD_GET_PERFTAB_DCI_LIST:
				sendPerfTabDCIList(pMsg);
				break;
         case CMD_PUSH_DCI_DATA:
            PushDCIData(pMsg);
            break;
         case CMD_GET_AGENT_CFG_LIST:
            SendAgentCfgList(pMsg->GetId());
            break;
         case CMD_OPEN_AGENT_CONFIG:
            OpenAgentConfig(pMsg);
            break;
         case CMD_SAVE_AGENT_CONFIG:
            SaveAgentConfig(pMsg);
            break;
         case CMD_DELETE_AGENT_CONFIG:
            DeleteAgentConfig(pMsg);
            break;
         case CMD_SWAP_AGENT_CONFIGS:
            SwapAgentConfigs(pMsg);
            break;
         case CMD_GET_OBJECT_COMMENTS:
            SendObjectComments(pMsg);
            break;
         case CMD_UPDATE_OBJECT_COMMENTS:
            UpdateObjectComments(pMsg);
            break;
         case CMD_GET_ADDR_LIST:
            GetAddrList(pMsg);
            break;
         case CMD_SET_ADDR_LIST:
            SetAddrList(pMsg);
            break;
         case CMD_RESET_COMPONENT:
            ResetComponent(pMsg);
            break;
         case CMD_EXPORT_CONFIGURATION:
            exportConfiguration(pMsg);
            break;
         case CMD_IMPORT_CONFIGURATION:
            importConfiguration(pMsg);
            break;
			case CMD_GET_GRAPH_LIST:
				SendGraphList(pMsg->GetId());
				break;
			case CMD_DEFINE_GRAPH:
				DefineGraph(pMsg);
				break;
			case CMD_DELETE_GRAPH:
				DeleteGraph(pMsg);
				break;
			case CMD_ADD_CA_CERTIFICATE:
				AddCACertificate(pMsg);
				break;
			case CMD_DELETE_CERTIFICATE:
				DeleteCertificate(pMsg);
				break;
			case CMD_UPDATE_CERT_COMMENTS:
				UpdateCertificateComments(pMsg);
				break;
			case CMD_GET_CERT_LIST:
				SendCertificateList(pMsg->GetId());
				break;
			case CMD_QUERY_L2_TOPOLOGY:
				CALL_IN_NEW_THREAD(QueryL2Topology, pMsg);
				break;
			case CMD_SEND_SMS:
				SendSMS(pMsg);
				break;
			case CMD_GET_COMMUNITY_LIST:
				SendCommunityList(pMsg->GetId());
				break;
			case CMD_UPDATE_COMMUNITY_LIST:
				UpdateCommunityList(pMsg);
				break;
			case CMD_GET_USM_CREDENTIALS:
				sendUsmCredentials(pMsg->GetId());
				break;
			case CMD_UPDATE_USM_CREDENTIALS:
				updateUsmCredentials(pMsg);
				break;
			case CMD_GET_SITUATION_LIST:
				SendSituationList(pMsg->GetId());
				break;
			case CMD_CREATE_SITUATION:
				CreateSituation(pMsg);
				break;
			case CMD_UPDATE_SITUATION:
				UpdateSituation(pMsg);
				break;
			case CMD_DELETE_SITUATION:
				DeleteSituation(pMsg);
				break;
			case CMD_DEL_SITUATION_INSTANCE:
				DeleteSituationInstance(pMsg);
				break;
			case CMD_WEBMAP_ADD:
				WebMapAdd(pMsg);
				break;
			case CMD_WEBMAP_UPDATE_PROPS:
				WebMapUpdateProps(pMsg);
				break;
			case CMD_WEBMAP_UPDATE_DATA:
				WebMapUpdateData(pMsg);
				break;
			case CMD_WEBMAP_DELETE:
				WebMapDelete(pMsg);
				break;
			case CMD_WEBMAP_GET_DATA:
				WebMapGetData(pMsg);
				break;
			case CMD_WEBMAP_GET_LIST:
				WebMapGetList(pMsg->GetId());
				break;
			case CMD_REGISTER_AGENT:
				registerAgent(pMsg);
				break;
			case CMD_GET_SERVER_FILE:
				CALL_IN_NEW_THREAD(getServerFile, pMsg);
				break;
			case CMD_TEST_DCI_TRANSFORMATION:
				testDCITransformation(pMsg);
				break;
			case CMD_GET_JOB_LIST:
				sendJobList(pMsg->GetId());
				break;
			case CMD_CANCEL_JOB:
				cancelJob(pMsg);
				break;
			case CMD_DEPLOY_AGENT_POLICY:
				deployAgentPolicy(pMsg, false);
				break;
			case CMD_UNINSTALL_AGENT_POLICY:
				deployAgentPolicy(pMsg, true);
				break;
			case CMD_GET_CURRENT_USER_ATTR:
				getUserCustomAttribute(pMsg);
				break;
			case CMD_SET_CURRENT_USER_ATTR:
				setUserCustomAttribute(pMsg);
				break;
			case CMD_OPEN_SERVER_LOG:
				openServerLog(pMsg);
				break;
			case CMD_CLOSE_SERVER_LOG:
				closeServerLog(pMsg);
				break;
			case CMD_QUERY_LOG:
				CALL_IN_NEW_THREAD(queryServerLog, pMsg);
				break;
			case CMD_GET_LOG_DATA:
				CALL_IN_NEW_THREAD(getServerLogQueryData, pMsg);
				break;
			case CMD_FIND_NODE_CONNECTION:
				CALL_IN_NEW_THREAD(findNodeConnection, pMsg);
				break;
			case CMD_FIND_MAC_LOCATION:
				CALL_IN_NEW_THREAD(findMacAddress, pMsg);
				break;
         default:
            // Pass message to loaded modules
            for(i = 0; i < g_dwNumModules; i++)
				{
					if (g_pModuleList[i].pfCommandHandler != NULL)
					{
						status = g_pModuleList[i].pfCommandHandler(m_wCurrentCmd, pMsg, this);
						if (status != NXMOD_COMMAND_IGNORED)
						{
							if (status == NXMOD_COMMAND_ACCEPTED_ASYNC)
							{
								pMsg = NULL;	// Prevent deletion
								m_dwRefCount++;
							}
							break;   // Message was processed by the module
						}
					}
				}
            if (i == g_dwNumModules)
            {
               CSCPMessage response;

               response.SetId(pMsg->GetId());
               response.SetCode(CMD_REQUEST_COMPLETED);
               response.SetVariable(VID_RCC, RCC_NOT_IMPLEMENTED);
               sendMessage(&response);
            }
            break;
      }
      delete pMsg;
      m_iState = (m_dwFlags & CSF_AUTHENTICATED) ? SESSION_STATE_IDLE : SESSION_STATE_INIT;
   }
}


//
// Respond to client's keepalive message
//

void ClientSession::RespondToKeepalive(DWORD dwRqId)
{
   CSCPMessage msg;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_RCC, RCC_SUCCESS);
   sendMessage(&msg);
}


//
// Process received file
//

void ClientSession::onFileUpload(BOOL bSuccess)
{
   // Do processing specific to command initiated file upload
   switch(m_dwUploadCommand)
   {
      case CMD_INSTALL_PACKAGE:
         if (!bSuccess)
         {
            TCHAR szQuery[256];

            _sntprintf(szQuery, 256, _T("DELETE FROM agent_pkg WHERE pkg_id=%d"), m_dwUploadData);
            DBQuery(g_hCoreDB, szQuery);
         }
         break;
      default:
         break;
   }

   // Remove received file in case of failure
   if (!bSuccess)
      _tunlink(m_szCurrFileName);
}


//
// Send file to client
//

BOOL ClientSession::sendFile(const TCHAR *file, DWORD dwRqId)
{
	return SendFileOverNXCP(m_hSocket, dwRqId, file, m_pCtx, 0, NULL, NULL);
}


//
// Send server information to client
//

void ClientSession::SendServerInfo(DWORD dwRqId)
{
   CSCPMessage msg;
	TCHAR szBuffer[1024];
	String strURL;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

	// Generate challenge for certificate authentication
#ifdef _WITH_ENCRYPTION
	RAND_bytes(m_challenge, CLIENT_CHALLENGE_SIZE);
#else
	memset(m_challenge, 0, CLIENT_CHALLENGE_SIZE);
#endif

   // Fill message with server info
   msg.SetVariable(VID_RCC, RCC_SUCCESS);
   msg.SetVariable(VID_SERVER_VERSION, NETXMS_VERSION_STRING);
   msg.SetVariable(VID_SERVER_ID, (BYTE *)&g_qwServerId, sizeof(QWORD));
   msg.SetVariable(VID_SUPPORTED_ENCRYPTION, (DWORD)0);
   msg.SetVariable(VID_PROTOCOL_VERSION, (DWORD)CLIENT_PROTOCOL_VERSION);
	msg.SetVariable(VID_CHALLENGE, m_challenge, CLIENT_CHALLENGE_SIZE);

#if defined(_WIN32)
	TIME_ZONE_INFORMATION tz;
	WCHAR wst[4], wdt[8], *curr;
	int i;

	GetTimeZoneInformation(&tz);

	// Create 3 letter abbreviation for standard name
	for(i = 0, curr = tz.StandardName; (*curr != 0) && (i < 3); curr++)
		if (iswupper(*curr))
			wst[i++] = *curr;
	while(i < 3)
		wst[i++] = L'X';
	wst[i] = 0;

	// Create abbreviation for DST name
	for(i = 0, curr = tz.DaylightName; (*curr != 0) && (i < 7); curr++)
		if (iswupper(*curr))
			wdt[i++] = *curr;
	while(i < 3)
		wdt[i++] = L'X';
	wdt[i] = 0;

#ifdef UNICODE
	swprintf(szBuffer, 1024, L"%s%c%02d%s", wst, (tz.Bias >= 0) ? '+' : '-',
	         abs(tz.Bias) / 60, (tz.DaylightBias != 0) ? wdt : L"");
#else
	sprintf(szBuffer, "%S%c%02d%S", wst, (tz.Bias >= 0) ? '+' : '-',
	        abs(tz.Bias) / 60, (tz.DaylightBias != 0) ? wdt : L"");
#endif
#elif HAVE_DECL_TIMEZONE
	sprintf(szBuffer, "%s%c%02d%s", tzname[0], (timezone >= 0) ? '+' : '-',
	        abs(timezone) / 3600, (tzname[1] != NULL) ? tzname[1] : "");
#elif HAVE_TM_GMTOFF
	time_t t;
	struct tm *loc;
	int gmtOffset;
#if HAVE_LOCALTIME_R
	struct tm tmbuff;
#endif

	t = time(NULL);
#if HAVE_LOCALTIME_R
	loc = localtime_r(&t, &tmbuff);
#else
	loc = localtime(&t);
#endif
	gmtOffset = -loc->tm_gmtoff / 3600;
	if (loc->tm_isdst)
		gmtOffset++;
	sprintf(szBuffer, "%s%c%02d%s", tzname[0], (gmtOffset >= 0) ? '+' : '-',
	        abs(gmtOffset), (tzname[1] != NULL) ? tzname[1] : "");
#else
	szBuffer[0] = 0;
#endif
	msg.SetVariable(VID_TIMEZONE, szBuffer);
	DebugPrintf(2, _T("Server time zone: %s"), szBuffer);

	ConfigReadStr(_T("WindowsConsoleUpgradeURL"), szBuffer, 1024,
	              _T("http://www.netxms.org/download/netxms-console-%version%.exe"));
	strURL = szBuffer;
	strURL.translate(_T("%version%"), NETXMS_VERSION_STRING);
	msg.SetVariable(VID_CONSOLE_UPGRADE_URL, (const TCHAR *)strURL);

   // Send response
   sendMessage(&msg);
}


//
// Authenticate client
//

void ClientSession::Login(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szLogin[MAX_USER_NAME], szPassword[MAX_DB_STRING], szBuffer[32];
	int nAuthType;
   bool changePasswd = false, intruderLockout = false;
   DWORD dwResult;
#ifdef _WITH_ENCRYPTION
	X509 *pCert;
#endif

   // Prepare response message
   msg.SetCode(CMD_LOGIN_RESP);
   msg.SetId(pRequest->GetId());

   // Get client info string
   if (pRequest->IsVariableExist(VID_CLIENT_INFO))
   {
      TCHAR szClientInfo[32], szOSInfo[32], szLibVersion[16];
      
      pRequest->GetVariableStr(VID_CLIENT_INFO, szClientInfo, 32);
      pRequest->GetVariableStr(VID_OS_INFO, szOSInfo, 32);
      pRequest->GetVariableStr(VID_LIBNXCL_VERSION, szLibVersion, 16);
      _sntprintf(m_szClientInfo, 96, _T("%s (%s; libnxcl %s)"),
                 szClientInfo, szOSInfo, szLibVersion);
   }

   if (!(m_dwFlags & CSF_AUTHENTICATED))
   {
      pRequest->GetVariableStr(VID_LOGIN_NAME, szLogin, MAX_USER_NAME);
		nAuthType = (int)pRequest->GetVariableShort(VID_AUTH_TYPE);
		switch(nAuthType)
		{
			case NETXMS_AUTH_TYPE_PASSWORD:
				pRequest->GetVariableStr(VID_PASSWORD, szPassword, MAX_DB_STRING);
				dwResult = AuthenticateUser(szLogin, szPassword, 0, NULL, NULL, &m_dwUserId,
													 &m_dwSystemAccess, &changePasswd, &intruderLockout);
				break;
			case NETXMS_AUTH_TYPE_CERTIFICATE:
#ifdef _WITH_ENCRYPTION
				pCert = CertificateFromLoginMessage(pRequest);
				if (pCert != NULL)
				{
					BYTE signature[256];
					DWORD dwSigLen;

					dwSigLen = pRequest->GetVariableBinary(VID_SIGNATURE, signature, 256);
					dwResult = AuthenticateUser(szLogin, (TCHAR *)signature, dwSigLen, pCert,
														 m_challenge, &m_dwUserId, &m_dwSystemAccess,
														 &changePasswd, &intruderLockout);
					X509_free(pCert);
				}
				else
				{
					dwResult = RCC_BAD_CERTIFICATE;
				}
#else
				dwResult = RCC_NOT_IMPLEMENTED;
#endif
				break;
			default:
				dwResult = RCC_UNSUPPORTED_AUTH_TYPE;
				break;
		}

      if (dwResult == RCC_SUCCESS)
      {
         m_dwFlags |= CSF_AUTHENTICATED;
         _sntprintf(m_szUserName, MAX_SESSION_NAME, _T("%s@%s"), szLogin, IpToStr(m_dwHostAddr, szBuffer));
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         msg.SetVariable(VID_USER_SYS_RIGHTS, m_dwSystemAccess);
         msg.SetVariable(VID_USER_ID, m_dwUserId);
			msg.SetVariable(VID_CHANGE_PASSWD_FLAG, (WORD)changePasswd);
         msg.SetVariable(VID_DBCONN_STATUS, (WORD)((g_dwFlags & AF_DB_CONNECTION_LOST) ? FALSE : TRUE));
         DebugPrintf(3, _T("User %s authenticated"), m_szUserName);
			WriteAuditLog(AUDIT_SECURITY, TRUE, m_dwUserId, m_szWorkstation, 0,
			              _T("User \"%s\" logged in (client info: %s)"), szLogin, m_szClientInfo);
      }
      else
      {
         msg.SetVariable(VID_RCC, dwResult);
			WriteAuditLog(AUDIT_SECURITY, FALSE, m_dwUserId, m_szWorkstation, 0,
			              _T("User \"%s\" login failed with error code %d (client info: %s)"),
							  szLogin, dwResult, m_szClientInfo);
			if (intruderLockout)
			{
				WriteAuditLog(AUDIT_SECURITY, FALSE, m_dwUserId, m_szWorkstation, 0,
								  _T("User account \"%s\" temporary disabled due to excess count of failed authentication attempts"), szLogin);
			}
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send event configuration to client
//

void ClientSession::sendEventDB(DWORD dwRqId)
{
   DB_ASYNC_RESULT hResult;
   CSCPMessage msg;
   TCHAR szBuffer[1024];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   if (checkSysAccessRights(SYSTEM_ACCESS_VIEW_EVENT_DB | SYSTEM_ACCESS_EDIT_EVENT_DB | SYSTEM_ACCESS_EPP))
   {
      if (!(g_dwFlags & AF_DB_CONNECTION_LOST))
      {
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         sendMessage(&msg);
         msg.DeleteAllVariables();

         // Prepare data message
         msg.SetCode(CMD_EVENT_DB_RECORD);
         msg.SetId(dwRqId);

         hResult = DBAsyncSelect(g_hCoreDB, _T("SELECT event_code,event_name,severity,flags,message,description FROM event_cfg"));
         if (hResult != NULL)
         {
            while(DBFetch(hResult))
            {
               msg.SetVariable(VID_EVENT_CODE, DBGetFieldAsyncULong(hResult, 0));
               msg.SetVariable(VID_NAME, DBGetFieldAsync(hResult, 1, szBuffer, 1024));
               msg.SetVariable(VID_SEVERITY, DBGetFieldAsyncULong(hResult, 2));
               msg.SetVariable(VID_FLAGS, DBGetFieldAsyncULong(hResult, 3));

               DBGetFieldAsync(hResult, 4, szBuffer, 1024);
               DecodeSQLString(szBuffer);
               msg.SetVariable(VID_MESSAGE, szBuffer);

               DBGetFieldAsync(hResult, 5, szBuffer, 1024);
               DecodeSQLString(szBuffer);
               msg.SetVariable(VID_DESCRIPTION, szBuffer);

               sendMessage(&msg);
               msg.DeleteAllVariables();
            }
            DBFreeAsyncResult(g_hCoreDB, hResult);
         }

         // End-of-list indicator
         msg.SetVariable(VID_EVENT_CODE, (DWORD)0);
			msg.SetEndOfSequence();
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_CONNECTION_LOST);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Update event template
//

static void SendEventDBChangeNotification(ClientSession *session, void *arg)
{
	if (session->isAuthenticated() && session->checkSysAccessRights(SYSTEM_ACCESS_VIEW_EVENT_DB | SYSTEM_ACCESS_EDIT_EVENT_DB | SYSTEM_ACCESS_EPP))
		session->sendMessage((CSCPMessage *)arg);
}

void ClientSession::modifyEventTemplate(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   // Prepare reply message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check access rights
   if (checkSysAccessRights(SYSTEM_ACCESS_EDIT_EVENT_DB))
   {
      TCHAR szQuery[4096], szName[MAX_EVENT_NAME];
      DWORD dwEventCode;
      BOOL bEventExist = FALSE;
      DB_RESULT hResult;

      // Check if event with specific code exists
      dwEventCode = pRequest->GetVariableLong(VID_EVENT_CODE);
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT event_code FROM event_cfg WHERE event_code=%d"), dwEventCode);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) > 0)
            bEventExist = TRUE;
         DBFreeResult(hResult);
      }

      // Check that we are not trying to create event below 100000
      if (bEventExist || (dwEventCode >= FIRST_USER_EVENT_ID))
      {
         // Prepare and execute SQL query
         pRequest->GetVariableStr(VID_NAME, szName, MAX_EVENT_NAME);
         if (IsValidObjectName(szName))
         {
            TCHAR szMessage[MAX_DB_STRING], *pszDescription, *pszEscMsg, *pszEscDescr;

            pRequest->GetVariableStr(VID_MESSAGE, szMessage, MAX_DB_STRING);
            pszEscMsg = EncodeSQLString(szMessage);

            pszDescription = pRequest->GetVariableStr(VID_DESCRIPTION);
            pszEscDescr = EncodeSQLString(pszDescription);
            safe_free(pszDescription);

            if (bEventExist)
            {
               _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("UPDATE event_cfg SET event_name='%s',severity=%d,flags=%d,message='%s',description='%s' WHERE event_code=%d"),
                       szName, pRequest->GetVariableLong(VID_SEVERITY), pRequest->GetVariableLong(VID_FLAGS),
                       pszEscMsg, pszEscDescr, dwEventCode);
            }
            else
            {
               _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("INSERT INTO event_cfg (event_code,event_name,severity,flags,")
                                _T("message,description) VALUES (%d,'%s',%d,%d,'%s','%s')"),
                       dwEventCode, szName, pRequest->GetVariableLong(VID_SEVERITY),
                       pRequest->GetVariableLong(VID_FLAGS), pszEscMsg, pszEscDescr);
            }

            free(pszEscMsg);
            free(pszEscDescr);

            if (DBQuery(g_hCoreDB, szQuery))
            {
               msg.SetVariable(VID_RCC, RCC_SUCCESS);
               ReloadEvents();
               NotifyClientSessions(NX_NOTIFY_EVENTDB_CHANGED, 0);

					CSCPMessage nmsg(pRequest);
					nmsg.SetCode(CMD_EVENT_DB_UPDATE);
					nmsg.SetVariable(VID_NOTIFICATION_CODE, (WORD)NX_NOTIFY_ETMPL_CHANGED);
					EnumerateClientSessions(SendEventDBChangeNotification, &nmsg);
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_EVENT_CODE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Delete event template
//

void ClientSession::deleteEventTemplate(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwEventCode;

   // Prepare reply message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwEventCode = pRequest->GetVariableLong(VID_EVENT_CODE);

   // Check access rights
   if (checkSysAccessRights(SYSTEM_ACCESS_EDIT_EVENT_DB) && (dwEventCode >= FIRST_USER_EVENT_ID))
   {
      TCHAR szQuery[256];

      _sntprintf(szQuery, 256, _T("DELETE FROM event_cfg WHERE event_code=%d"), dwEventCode);
      if (DBQuery(g_hCoreDB, szQuery))
      {
         DeleteEventTemplateFromList(dwEventCode);
         NotifyClientSessions(NX_NOTIFY_EVENTDB_CHANGED, 0);

			CSCPMessage nmsg;
			nmsg.SetCode(CMD_EVENT_DB_UPDATE);
			nmsg.SetVariable(VID_NOTIFICATION_CODE, (WORD)NX_NOTIFY_ETMPL_DELETED);
			nmsg.SetVariable(VID_EVENT_CODE, dwEventCode);
			EnumerateClientSessions(SendEventDBChangeNotification, &nmsg);

         msg.SetVariable(VID_RCC, RCC_SUCCESS);

			WriteAuditLog(AUDIT_SYSCFG, TRUE, m_dwUserId, m_szWorkstation, 0,
							  _T("Event template %d deleted"), dwEventCode);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Generate event code for new event template
//

void ClientSession::generateEventCode(DWORD dwRqId)
{
   CSCPMessage msg;

   // Prepare reply message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   // Check access rights
   if (checkSysAccessRights(SYSTEM_ACCESS_EDIT_EVENT_DB))
   {
      msg.SetVariable(VID_EVENT_CODE, CreateUniqueId(IDG_EVENT));
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send all objects to client
//

void ClientSession::SendAllObjects(CSCPMessage *pRequest)
{
   DWORD i, dwTimeStamp;
   CSCPMessage msg;

   // Send confirmation message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());
   msg.SetVariable(VID_RCC, RCC_SUCCESS);
   sendMessage(&msg);
   msg.DeleteAllVariables();

   // Change _T("sync comments") flag
   if (pRequest->GetVariableShort(VID_SYNC_COMMENTS))
      m_dwFlags |= CSF_SYNC_OBJECT_COMMENTS;
   else
      m_dwFlags &= ~CSF_SYNC_OBJECT_COMMENTS;

   // Get client's last known time stamp
   dwTimeStamp = pRequest->GetVariableLong(VID_TIMESTAMP);

   MutexLock(m_mutexSendObjects, INFINITE);

   // Prepare message
   msg.SetCode(CMD_OBJECT);

   // Send objects, one per message
   RWLockReadLock(g_rwlockIdIndex, INFINITE);
   for(i = 0; i < g_dwIdIndexSize; i++)
      if ((((NetObj *)g_pIndexById[i].pObject)->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ)) &&
          (((NetObj *)g_pIndexById[i].pObject)->TimeStamp() >= dwTimeStamp) &&
          (!(((NetObj *)g_pIndexById[i].pObject)->IsHidden())))
      {
         ((NetObj *)g_pIndexById[i].pObject)->CreateMessage(&msg);
         if (m_dwFlags & CSF_SYNC_OBJECT_COMMENTS)
            ((NetObj *)g_pIndexById[i].pObject)->CommentsToMessage(&msg);
         sendMessage(&msg);
         msg.DeleteAllVariables();
      }
   RWLockUnlock(g_rwlockIdIndex);

   // Send end of list notification
   msg.SetCode(CMD_OBJECT_LIST_END);
   sendMessage(&msg);

   MutexUnlock(m_mutexSendObjects);
}


//
// Send event log records to client
//

void ClientSession::SendEventLog(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DB_ASYNC_RESULT hResult = NULL;
   DB_RESULT hTempResult;
   DWORD dwRqId, dwMaxRecords, dwNumRows, dwId;
   TCHAR szQuery[1024], szBuffer[1024];
   WORD wRecOrder;

   dwRqId = pRequest->GetId();
   dwMaxRecords = pRequest->GetVariableLong(VID_MAX_RECORDS);
   wRecOrder = ((g_nDBSyntax == DB_SYNTAX_MSSQL) || (g_nDBSyntax == DB_SYNTAX_ORACLE)) ? RECORD_ORDER_REVERSED : RECORD_ORDER_NORMAL;

   // Prepare confirmation message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   MutexLock(m_mutexSendEvents, INFINITE);

   // Retrieve events from database
   switch(g_nDBSyntax)
   {
      case DB_SYNTAX_MYSQL:
      case DB_SYNTAX_PGSQL:
      case DB_SYNTAX_SQLITE:
         hTempResult = DBSelect(g_hCoreDB, _T("SELECT count(*) FROM event_log"));
         if (hTempResult != NULL)
         {
            if (DBGetNumRows(hTempResult) > 0)
            {
               dwNumRows = DBGetFieldULong(hTempResult, 0, 0);
            }
            else
            {
               dwNumRows = 0;
            }
            DBFreeResult(hTempResult);
         }
         _sntprintf(szQuery, 1024,
                    _T("SELECT event_id,event_code,event_timestamp,event_source,")
                    _T("event_severity,event_message,user_tag FROM event_log ")
                    _T("ORDER BY event_id LIMIT %u OFFSET %u"),
                    dwMaxRecords, dwNumRows - min(dwNumRows, dwMaxRecords));
         break;
      case DB_SYNTAX_MSSQL:
         _sntprintf(szQuery, 1024,
                    _T("SELECT TOP %u event_id,event_code,event_timestamp,event_source,")
                    _T("event_severity,event_message,user_tag FROM event_log ")
                    _T("ORDER BY event_id DESC"), dwMaxRecords);
         break;
      case DB_SYNTAX_ORACLE:
         _sntprintf(szQuery, 1024,
                    _T("SELECT event_id,event_code,event_timestamp,event_source,")
                    _T("event_severity,event_message,user_tag FROM event_log ")
                    _T("WHERE ROWNUM <= %u ORDER BY event_id DESC"), dwMaxRecords);
         break;
      default:
         szQuery[0] = 0;
         break;
   }
   hResult = DBAsyncSelect(g_hCoreDB, szQuery);
   if (hResult != NULL)
   {
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
   	sendMessage(&msg);
   	msg.DeleteAllVariables();
	   msg.SetCode(CMD_EVENTLOG_RECORDS);
	   
      for(dwId = VID_EVENTLOG_MSG_BASE, dwNumRows = 0; DBFetch(hResult); dwNumRows++)
      {
         if (dwNumRows == 10)
         {
            msg.SetVariable(VID_NUM_RECORDS, dwNumRows);
            msg.SetVariable(VID_RECORDS_ORDER, wRecOrder);
            sendMessage(&msg);
            msg.DeleteAllVariables();
            dwNumRows = 0;
            dwId = VID_EVENTLOG_MSG_BASE;
         }
         msg.SetVariable(dwId++, DBGetFieldAsyncUInt64(hResult, 0));
         msg.SetVariable(dwId++, DBGetFieldAsyncULong(hResult, 1));
         msg.SetVariable(dwId++, DBGetFieldAsyncULong(hResult, 2));
         msg.SetVariable(dwId++, DBGetFieldAsyncULong(hResult, 3));
         msg.SetVariable(dwId++, (WORD)DBGetFieldAsyncLong(hResult, 4));
         DBGetFieldAsync(hResult, 5, szBuffer, 1024);
         msg.SetVariable(dwId++, szBuffer);
         DBGetFieldAsync(hResult, 6, szBuffer, 1024);
         msg.SetVariable(dwId++, szBuffer);
         msg.SetVariable(dwId++, (DWORD)0);	// Do not send parameters
      }
      DBFreeAsyncResult(g_hCoreDB, hResult);

      // Send remaining records with End-Of-Sequence notification
      msg.SetVariable(VID_NUM_RECORDS, dwNumRows);
      msg.SetVariable(VID_RECORDS_ORDER, wRecOrder);
      msg.SetEndOfSequence();
      sendMessage(&msg);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
   	sendMessage(&msg);
	}

   MutexUnlock(m_mutexSendEvents);
}


//
// Send all configuration variables to client
//

void ClientSession::SendAllConfigVars(DWORD dwRqId)
{
   DWORD i, dwId, dwNumRecords;
   CSCPMessage msg;
   DB_RESULT hResult;
   TCHAR szBuffer[MAX_DB_STRING];

   // Prepare message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   // Check user rights
   if ((m_dwUserId == 0) || (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG))
   {
      // Retrieve configuration variables from database
      hResult = DBSelect(g_hCoreDB, _T("SELECT var_name,var_value,need_server_restart FROM config WHERE is_visible=1"));
      if (hResult != NULL)
      {
         // Send events, one per message
         dwNumRecords = DBGetNumRows(hResult);
         msg.SetVariable(VID_NUM_VARIABLES, dwNumRecords);
         for(i = 0, dwId = VID_VARLIST_BASE; i < dwNumRecords; i++)
         {
            msg.SetVariable(dwId++, DBGetField(hResult, i, 0, szBuffer, MAX_DB_STRING));
            DBGetField(hResult, i, 1, szBuffer, MAX_DB_STRING);
            DecodeSQLString(szBuffer);
            msg.SetVariable(dwId++, szBuffer);
            msg.SetVariable(dwId++, (WORD)DBGetFieldLong(hResult, i, 2));
         }
         DBFreeResult(hResult);
      }
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Set configuration variable's value
//

void ClientSession::SetConfigVariable(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szName[MAX_OBJECT_NAME], szValue[MAX_DB_STRING];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if ((m_dwUserId == 0) || (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG))
   {
      pRequest->GetVariableStr(VID_NAME, szName, MAX_OBJECT_NAME);
      pRequest->GetVariableStr(VID_VALUE, szValue, MAX_DB_STRING);
      if (ConfigWriteStr(szName, szValue, TRUE))
		{
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
			WriteAuditLog(AUDIT_SYSCFG, TRUE, m_dwUserId, m_szWorkstation, 0,
							  _T("Server configuration variable \"%s\" set to \"%s\""), szName, szValue);
		}
      else
		{
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Delete configuration variable
//

void ClientSession::DeleteConfigVariable(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szName[MAX_OBJECT_NAME], szQuery[1024];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if ((m_dwUserId == 0) || (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG))
   {
      pRequest->GetVariableStr(VID_NAME, szName, MAX_OBJECT_NAME);
      _sntprintf(szQuery, 1024, _T("DELETE FROM config WHERE var_name='%s'"), szName);
      if (DBQuery(g_hCoreDB, szQuery))
		{
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
			WriteAuditLog(AUDIT_SYSCFG, TRUE, m_dwUserId, m_szWorkstation, 0,
							  _T("Server configuration variable \"%s\" deleted"), szName);
		}
      else
		{
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Set configuration clob
//

void ClientSession::setConfigCLOB(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR name[MAX_OBJECT_NAME], *value;
   
	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);
	
	if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
	{
      pRequest->GetVariableStr(VID_NAME, name, MAX_OBJECT_NAME);
		value = pRequest->GetVariableStr(VID_VALUE);
		if (value != NULL)
		{
			if (ConfigWriteCLOB(name, value, TRUE))
			{
				msg.SetVariable(VID_RCC, RCC_SUCCESS);
				free(value);
				WriteAuditLog(AUDIT_SYSCFG, TRUE, m_dwUserId, m_szWorkstation, 0,
								  _T("Server configuration variable \"%s\" set to \"%s\""), name, value);
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_REQUEST);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Get value of configuration clob
//

void ClientSession::getConfigCLOB(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR name[MAX_OBJECT_NAME], *value;
   
	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);
	
	if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
	{
      pRequest->GetVariableStr(VID_NAME, name, MAX_OBJECT_NAME);
		value = ConfigReadCLOB(name, NULL);
		if (value != NULL)
		{
			msg.SetVariable(VID_VALUE, value);
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
			free(value);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_UNKNOWN_VARIABLE);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Close session forcibly
//

void ClientSession::kill()
{
   // We shutdown socket connection, which will cause
   // read thread to stop, and other threads will follow
   shutdown(m_hSocket, 2);
}


//
// Handler for new events
//

void ClientSession::onNewEvent(Event *pEvent)
{
   UPDATE_INFO *pUpdate;
   CSCPMessage *msg;

   if (isAuthenticated() && (m_dwActiveChannels & NXC_CHANNEL_EVENTS))
   {
      pUpdate = (UPDATE_INFO *)malloc(sizeof(UPDATE_INFO));
      pUpdate->dwCategory = INFO_CAT_EVENT;
      msg = new CSCPMessage;
      msg->SetCode(CMD_EVENTLOG_RECORDS);
      pEvent->prepareMessage(msg);
      pUpdate->pData = msg;
      m_pUpdateQueue->Put(pUpdate);
   }
}


//
// Handler for object changes
//

void ClientSession::onObjectChange(NetObj *pObject)
{
   UPDATE_INFO *pUpdate;

   if (isAuthenticated() && (m_dwActiveChannels & NXC_CHANNEL_OBJECTS))
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
      {
         pUpdate = (UPDATE_INFO *)malloc(sizeof(UPDATE_INFO));
         pUpdate->dwCategory = INFO_CAT_OBJECT_CHANGE;
         pUpdate->pData = pObject;
         pObject->IncRefCount();
         m_pUpdateQueue->Put(pUpdate);
      }
}


//
// Send notification message to server
//

void ClientSession::notify(DWORD dwCode, DWORD dwData)
{
   CSCPMessage msg;

   msg.SetCode(CMD_NOTIFY);
   msg.SetVariable(VID_NOTIFICATION_CODE, dwCode);
   msg.SetVariable(VID_NOTIFICATION_DATA, dwData);
   sendMessage(&msg);
}


//
// Modify object
//

void ClientSession::ModifyObject(CSCPMessage *pRequest)
{
   DWORD dwObjectId, dwResult = RCC_SUCCESS;
   NetObj *pObject;
   CSCPMessage msg;

   // Prepare reply message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwObjectId = pRequest->GetVariableLong(VID_OBJECT_ID);
   pObject = FindObjectById(dwObjectId);
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
      {
         // If user attempts to change object's ACL, check
         // if he has OBJECT_ACCESS_ACL permission
         if (pRequest->IsVariableExist(VID_ACL_SIZE))
            if (!pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_ACL))
               dwResult = RCC_ACCESS_DENIED;

			// If user attempts to rename object, check object's name
			if (pRequest->IsVariableExist(VID_OBJECT_NAME))
			{
				TCHAR name[256];
				pRequest->GetVariableStr(VID_OBJECT_NAME, name, 256);
				if (!IsValidObjectName(name, TRUE))
					dwResult = RCC_INVALID_OBJECT_NAME;
			}

         // If allowed, change object and set completion code
         if (dwResult == RCC_SUCCESS)
            dwResult = pObject->ModifyFromMessage(pRequest);
         msg.SetVariable(VID_RCC, dwResult);

			if (dwResult == RCC_SUCCESS)
			{
				WriteAuditLog(AUDIT_OBJECTS, TRUE, m_dwUserId, m_szWorkstation, dwObjectId,
								  _T("Object modified from client"));
			}
			else
			{
				WriteAuditLog(AUDIT_OBJECTS, FALSE, m_dwUserId, m_szWorkstation, dwObjectId,
								  _T("Failed to modify object from client - error %d"), dwResult);
			}
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
			WriteAuditLog(AUDIT_OBJECTS, FALSE, m_dwUserId, m_szWorkstation, dwObjectId,
							  _T("Failed to modify object from client - access denied"), dwResult);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send users database to client
//

void ClientSession::SendUserDB(DWORD dwRqId)
{
   CSCPMessage msg;
	UserDatabaseObject **users;
   int i, userCount;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_RCC, RCC_SUCCESS);
   sendMessage(&msg);
	msg.DeleteAllVariables();

   // Send user database
	users = OpenUserDatabase(&userCount);
   for(i = 0; i < userCount; i++)
   {
		msg.SetCode((users[i]->getId() & GROUP_FLAG) ? CMD_GROUP_DATA : CMD_USER_DATA);
		users[i]->fillMessage(&msg);
      sendMessage(&msg);
      msg.DeleteAllVariables();
   }
	CloseUserDatabase();

   // Send end-of-database notification
   msg.SetCode(CMD_USER_DB_EOF);
   sendMessage(&msg);
}


//
// Create new user
//

void ClientSession::CreateUser(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (!(m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS))
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   else if (!(m_dwFlags & CSF_USER_DB_LOCKED))
   {
      // User database have to be locked before any
      // changes to user database can be made
      msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
   }
   else
   {
      DWORD dwResult, dwUserId;
      BOOL bIsGroup;
      TCHAR szUserName[MAX_USER_NAME];

      pRequest->GetVariableStr(VID_USER_NAME, szUserName, MAX_USER_NAME);
      if (IsValidObjectName(szUserName))
      {
         bIsGroup = pRequest->GetVariableShort(VID_IS_GROUP);
         dwResult = CreateNewUser(szUserName, bIsGroup, &dwUserId);
         msg.SetVariable(VID_RCC, dwResult);
         if (dwResult == RCC_SUCCESS)
            msg.SetVariable(VID_USER_ID, dwUserId);   // Send id of new user to client
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
      }
   }

   // Send response
   sendMessage(&msg);
}


//
// Update existing user's data
//

void ClientSession::UpdateUser(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (!(m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS))
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   else if (!(m_dwFlags & CSF_USER_DB_LOCKED))
   {
      // User database have to be locked before any
      // changes to user database can be made
      msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
   }
   else
   {
      msg.SetVariable(VID_RCC, ModifyUserDatabaseObject(pRequest));
   }

   // Send response
   sendMessage(&msg);
}


//
// Delete user
//

void ClientSession::DeleteUser(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwUserId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (!(m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS))
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   else if (!(m_dwFlags & CSF_USER_DB_LOCKED))
   {
      // User database have to be locked before any
      // changes to user database can be made
      msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
   }
   else
   {
      // Get Id of user to be deleted
      dwUserId = pRequest->GetVariableLong(VID_USER_ID);

      if ((dwUserId != 0) && (dwUserId != GROUP_EVERYONE))
      {
         DWORD dwResult;

         dwResult = DeleteUserDatabaseObject(dwUserId);
         msg.SetVariable(VID_RCC, dwResult);
      }
      else
      {
         // System administrator account and _T("everyone") group cannot be deleted
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }

   // Send response
   sendMessage(&msg);
}


//
// Lock/unlock user database
//

void ClientSession::LockUserDB(DWORD dwRqId, BOOL bLock)
{
   CSCPMessage msg;
   TCHAR szBuffer[256];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS)
   {
      if (bLock)
      {
         if (!LockComponent(CID_USER_DB, m_dwIndex, m_szUserName, NULL, szBuffer))
         {
            msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
            msg.SetVariable(VID_LOCKED_BY, szBuffer);
         }
         else
         {
            m_dwFlags |= CSF_USER_DB_LOCKED;
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
      }
      else
      {
         if (m_dwFlags & CSF_USER_DB_LOCKED)
         {
            UnlockComponent(CID_USER_DB);
            m_dwFlags &= ~CSF_USER_DB_LOCKED;
         }
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
   }
   else
   {
      // Current user has no rights for user account management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Notify client on user database update
//

void ClientSession::onUserDBUpdate(int code, DWORD id, UserDatabaseObject *object)
{
   CSCPMessage msg;

   if (isAuthenticated())
   {
      msg.SetCode(CMD_USER_DB_UPDATE);
      msg.SetId(0);
      msg.SetVariable(VID_UPDATE_TYPE, (WORD)code);

      switch(code)
      {
         case USER_DB_CREATE:
         case USER_DB_MODIFY:
				object->fillMessage(&msg);
            break;
         default:
            msg.SetVariable(VID_USER_ID, id);
            break;
      }

      sendMessage(&msg);
   }
}


//
// Change management status for the object
//

void ClientSession::changeObjectMgmtStatus(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwObjectId;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get object id and check access rights
   dwObjectId = pRequest->GetVariableLong(VID_OBJECT_ID);
   pObject = FindObjectById(dwObjectId);
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
      {
			if ((pObject->Type() != OBJECT_TEMPLATE) &&
				 (pObject->Type() != OBJECT_TEMPLATEGROUP) &&
				 (pObject->Type() != OBJECT_TEMPLATEROOT))
			{
				BOOL bIsManaged;

				bIsManaged = (BOOL)pRequest->GetVariableShort(VID_MGMT_STATUS);
				pObject->SetMgmtStatus(bIsManaged);
				msg.SetVariable(VID_RCC, RCC_SUCCESS);
			}
			else
			{
	         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
			}
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


// 
// Set user's password
//

void ClientSession::SetPassword(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwUserId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwUserId = pRequest->GetVariableLong(VID_USER_ID);

   if (((m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS) &&
        !((dwUserId == 0) && (m_dwUserId != 0))) ||   // Only administrator can change password for UID 0
       (dwUserId == m_dwUserId))     // User can change password for itself
   {
      DWORD dwResult;
      TCHAR newPassword[256], oldPassword[256];

      pRequest->GetVariableStr(VID_PASSWORD, newPassword, 256);
		if (pRequest->IsVariableExist(VID_OLD_PASSWORD))
			pRequest->GetVariableStr(VID_OLD_PASSWORD, oldPassword, 256);
		else
			oldPassword[0] = 0;
      dwResult = SetUserPassword(dwUserId, newPassword, oldPassword, dwUserId == m_dwUserId);
      msg.SetVariable(VID_RCC, dwResult);
   }
   else
   {
      // Current user has no rights to change password for specific user
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send node's DCIs to client and lock data collection settings
//

void ClientSession::OpenNodeDCIList(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwObjectId;
   NetObj *pObject;
   BOOL bSuccess = FALSE;
   TCHAR szLockInfo[MAX_SESSION_NAME];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   dwObjectId = pRequest->GetVariableLong(VID_OBJECT_ID);
   pObject = FindObjectById(dwObjectId);
   if (pObject != NULL)
   {
      if ((pObject->Type() == OBJECT_NODE) ||
          (pObject->Type() == OBJECT_CLUSTER) ||
          (pObject->Type() == OBJECT_TEMPLATE))
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
         {
            // Try to lock DCI list
            if (((Template *)pObject)->LockDCIList(m_dwIndex, m_szUserName, szLockInfo))
            {
               bSuccess = TRUE;
               msg.SetVariable(VID_RCC, RCC_SUCCESS);

               // Modify list of open nodes DCI lists
               m_pOpenDCIList = (DWORD *)realloc(m_pOpenDCIList, sizeof(DWORD) * (m_dwOpenDCIListSize + 1));
               m_pOpenDCIList[m_dwOpenDCIListSize] = dwObjectId;
               m_dwOpenDCIListSize++;
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
               msg.SetVariable(VID_LOCKED_BY, szLockInfo);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);

   // If DCI list was successfully locked, send it to client
   if (bSuccess)
      ((Template *)pObject)->SendItemsToClient(this, pRequest->GetId());
}


//
// Unlock node's data collection settings
//

void ClientSession::CloseNodeDCIList(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwObjectId;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   dwObjectId = pRequest->GetVariableLong(VID_OBJECT_ID);
   pObject = FindObjectById(dwObjectId);
   if (pObject != NULL)
   {
      if ((pObject->Type() == OBJECT_NODE) ||
          (pObject->Type() == OBJECT_CLUSTER) ||
          (pObject->Type() == OBJECT_TEMPLATE))
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
         {
            BOOL bSuccess;

            // Try to unlock DCI list
            bSuccess = ((Template *)pObject)->UnlockDCIList(m_dwIndex);
            msg.SetVariable(VID_RCC, bSuccess ? RCC_SUCCESS : RCC_OUT_OF_STATE_REQUEST);

            // Modify list of open nodes DCI lists
            if (bSuccess)
            {
               DWORD i;

               for(i = 0; i < m_dwOpenDCIListSize; i++)
                  if (m_pOpenDCIList[i] == dwObjectId)
                  {
                     m_dwOpenDCIListSize--;
                     memmove(&m_pOpenDCIList[i], &m_pOpenDCIList[i + 1],
                             sizeof(DWORD) * (m_dwOpenDCIListSize - i));
                     break;
                  }
            }

            // Queue template update
            if ((pObject->Type() == OBJECT_TEMPLATE) ||
					 (pObject->Type() == OBJECT_CLUSTER))
               ((Template *)pObject)->queueUpdate();
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Create, modify, or delete data collection item for node
//

void ClientSession::ModifyNodeDCI(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwObjectId;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   dwObjectId = pRequest->GetVariableLong(VID_OBJECT_ID);
   pObject = FindObjectById(dwObjectId);
   if (pObject != NULL)
   {
      if ((pObject->Type() == OBJECT_NODE) ||
          (pObject->Type() == OBJECT_CLUSTER) ||
          (pObject->Type() == OBJECT_TEMPLATE))
      {
         if (((Template *)pObject)->IsLockedBySession(m_dwIndex))
         {
            if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
            {
               DWORD i, dwItemId, dwNumMaps, *pdwMapId, *pdwMapIndex;
               DCItem *pItem;
               BOOL bSuccess;

               switch(pRequest->GetCode())
               {
                  case CMD_CREATE_NEW_DCI:
                     // Create dummy DCI
                     pItem = new DCItem(CreateUniqueId(IDG_ITEM), _T("no name"), DS_INTERNAL, 
                                        DCI_DT_INT, 60, 30, (Node *)pObject);
                     pItem->setStatus(ITEM_STATUS_DISABLED, false);
                     if ((bSuccess = ((Template *)pObject)->addItem(pItem)))
                     {
                        msg.SetVariable(VID_RCC, RCC_SUCCESS);
                        // Return new item id to client
                        msg.SetVariable(VID_DCI_ID, pItem->getId());
                     }
                     else  // Unable to add item to node
                     {
                        delete pItem;
                        msg.SetVariable(VID_RCC, RCC_DUPLICATE_DCI);
                     }
                     break;
                  case CMD_MODIFY_NODE_DCI:
                     dwItemId = pRequest->GetVariableLong(VID_DCI_ID);
                     bSuccess = ((Template *)pObject)->updateItem(dwItemId, pRequest, &dwNumMaps,
                                                                  &pdwMapIndex, &pdwMapId);
                     if (bSuccess)
                     {
                        msg.SetVariable(VID_RCC, RCC_SUCCESS);

                        // Send index to id mapping for newly created thresholds to client
                        msg.SetVariable(VID_DCI_NUM_MAPS, dwNumMaps);
                        for(i = 0; i < dwNumMaps; i++)
                        {
                           pdwMapId[i] = htonl(pdwMapId[i]);
                           pdwMapIndex[i] = htonl(pdwMapIndex[i]);
                        }
                        msg.SetVariable(VID_DCI_MAP_IDS, (BYTE *)pdwMapId, sizeof(DWORD) * dwNumMaps);
                        msg.SetVariable(VID_DCI_MAP_INDEXES, (BYTE *)pdwMapIndex, sizeof(DWORD) * dwNumMaps);
                        safe_free(pdwMapId);
                        safe_free(pdwMapIndex);
                     }
                     else
                     {
                        msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
                     }
                     break;
                  case CMD_DELETE_NODE_DCI:
                     dwItemId = pRequest->GetVariableLong(VID_DCI_ID);
                     bSuccess = ((Template *)pObject)->deleteItem(dwItemId, TRUE);
                     msg.SetVariable(VID_RCC, bSuccess ? RCC_SUCCESS : RCC_INVALID_DCI_ID);
                     break;
               }
               if (bSuccess)
                  ((Template *)pObject)->SetDCIModificationFlag();
            }
            else  // User doesn't have MODIFY rights on object
            {
               msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
            }
         }
         else  // Nodes DCI list not locked by this session
         {
            msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
         }
      }
      else     // Object is not a node
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
      }
   }
   else  // No object with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Change status for one or more DCIs
//

void ClientSession::ChangeDCIStatus(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if ((pObject->Type() == OBJECT_NODE) ||
          (pObject->Type() == OBJECT_CLUSTER) ||
          (pObject->Type() == OBJECT_TEMPLATE))
      {
         if (((Template *)pObject)->IsLockedBySession(m_dwIndex))
         {
            if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
            {
               DWORD dwNumItems, *pdwItemList;
               int iStatus;

               iStatus = pRequest->GetVariableShort(VID_DCI_STATUS);
               dwNumItems = pRequest->GetVariableLong(VID_NUM_ITEMS);
               pdwItemList = (DWORD *)malloc(sizeof(DWORD) * dwNumItems);
               pRequest->GetVariableInt32Array(VID_ITEM_LIST, dwNumItems, pdwItemList);
               if (((Template *)pObject)->setItemStatus(dwNumItems, pdwItemList, iStatus))
                  msg.SetVariable(VID_RCC, RCC_SUCCESS);
               else
                  msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
               free(pdwItemList);
            }
            else  // User doesn't have MODIFY rights on object
            {
               msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
            }
         }
         else  // Nodes DCI list not locked by this session
         {
            msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
         }
      }
      else     // Object is not a node
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else  // No object with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Clear all collected data for DCI
//

void ClientSession::ClearDCIData(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
	DWORD dwItemId;
	DCItem *dci;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_DELETE))
         {
				dwItemId = pRequest->GetVariableLong(VID_DCI_ID);
				DebugPrintf(4, _T("ClearDCIData: request for DCI %d at node %d"), dwItemId, pObject->Id()); 
            dci = ((Template *)pObject)->getItemById(dwItemId);
				if (dci != NULL)
				{
					msg.SetVariable(VID_RCC, dci->deleteAllData() ? RCC_SUCCESS : RCC_DB_FAILURE);
					DebugPrintf(4, _T("ClearDCIData: DCI %d at node %d"), dwItemId, pObject->Id()); 
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
					DebugPrintf(4, _T("ClearDCIData: DCI %d at node %d not found"), dwItemId, pObject->Id()); 
				}
         }
         else  // User doesn't have DELETE rights on object
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else     // Object is not a node
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else  // No object with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Copy or move DCI from one node or template to another
//

void ClientSession::CopyDCI(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pSource, *pDestination;
   TCHAR szLockInfo[MAX_SESSION_NAME];
   BOOL bMove;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get source and destination
   pSource = FindObjectById(pRequest->GetVariableLong(VID_SOURCE_OBJECT_ID));
   pDestination = FindObjectById(pRequest->GetVariableLong(VID_DESTINATION_OBJECT_ID));
   if ((pSource != NULL) && (pDestination != NULL))
   {
      // Check object types
      if (((pSource->Type() == OBJECT_NODE) || (pSource->Type() == OBJECT_TEMPLATE) || (pSource->Type() == OBJECT_CLUSTER)) && 
          ((pDestination->Type() == OBJECT_NODE) || (pDestination->Type() == OBJECT_TEMPLATE) || (pDestination->Type() == OBJECT_CLUSTER)))
      {
         if (((Template *)pSource)->IsLockedBySession(m_dwIndex))
         {
            bMove = pRequest->GetVariableShort(VID_MOVE_FLAG);
            // Check access rights
            if ((pSource->CheckAccessRights(m_dwUserId, bMove ? (OBJECT_ACCESS_READ | OBJECT_ACCESS_MODIFY) : OBJECT_ACCESS_READ)) &&
                (pDestination->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY)))
            {
               // Attempt to lock destination's DCI list
               if ((pDestination->Id() == pSource->Id()) ||
                   (((Template *)pDestination)->LockDCIList(m_dwIndex, m_szUserName, szLockInfo)))
               {
                  DWORD i, *pdwItemList, dwNumItems;
                  const DCItem *pSrcItem;
                  DCItem *pDstItem;
                  int iErrors = 0;

                  // Get list of items to be copied/moved
                  dwNumItems = pRequest->GetVariableLong(VID_NUM_ITEMS);
                  pdwItemList = (DWORD *)malloc(sizeof(DWORD) * dwNumItems);
                  pRequest->GetVariableInt32Array(VID_ITEM_LIST, dwNumItems, pdwItemList);

                  // Copy items
                  for(i = 0; i < dwNumItems; i++)
                  {
                     pSrcItem = ((Template *)pSource)->getItemById(pdwItemList[i]);
                     if (pSrcItem != NULL)
                     {
                        pDstItem = new DCItem(pSrcItem);
								pDstItem->setTemplateId(0, 0);
                        pDstItem->changeBinding(CreateUniqueId(IDG_ITEM),
                                                (Template *)pDestination, FALSE);
                        if (((Template *)pDestination)->addItem(pDstItem))
                        {
                           if (bMove)
                           {
                              // Delete original item
                              if (!((Template *)pSource)->deleteItem(pdwItemList[i], TRUE))
                              {
                                 iErrors++;
                              }
                           }
                        }
                        else
                        {
                           delete pDstItem;
                           iErrors++;
                        }
                     }
                     else
                     {
                        iErrors++;
                     }
                  }

                  // Cleanup
                  free(pdwItemList);
                  if (pDestination->Id() != pSource->Id())
                     ((Template *)pDestination)->UnlockDCIList(m_dwIndex);
                  msg.SetVariable(VID_RCC, (iErrors == 0) ? RCC_SUCCESS : RCC_DCI_COPY_ERRORS);

                  // Queue template update
                  if (pDestination->Type() == OBJECT_TEMPLATE)
                     ((Template *)pDestination)->queueUpdate();
               }
               else  // Destination's DCI list already locked by someone else
               {
                  msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
                  msg.SetVariable(VID_LOCKED_BY, szLockInfo);
               }
            }
            else  // User doesn't have enough rights on object(s)
            {
               msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
            }
         }
         else  // Source node DCI list not locked by this session
         {
            msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
         }
      }
      else     // Object(s) is not a node
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
      }
   }
   else  // No object(s) with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send list of thresholds for DCI
//

void ClientSession::sendDCIThresholds(CSCPMessage *request)
{
   CSCPMessage msg;
   NetObj *object;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(request->GetId());

   // Get node id and check object class and access rights
   object = FindObjectById(request->GetVariableLong(VID_OBJECT_ID));
   if (object != NULL)
   {
      if (object->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
      {
			if (object->Type() == OBJECT_NODE)
			{
				DCItem *dci = ((Node *)object)->getItemById(request->GetVariableLong(VID_DCI_ID));
				if (dci != NULL)
				{
					dci->fillMessageWithThresholds(&msg);
					msg.SetVariable(VID_RCC, RCC_SUCCESS);
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
				}
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else  // No object with given ID
   {
		msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Get collected data
//

void ClientSession::GetCollectedData(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwObjectId;
   NetObj *pObject;
   BOOL bSuccess = FALSE;
   static DWORD m_dwRowSize[] = { 8, 8, 12, 12, 516, 12 };
#if !defined(UNICODE) || defined(UNICODE_UCS4)
   TCHAR szBuffer[MAX_DCI_STRING_VALUE];
#endif

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   dwObjectId = pRequest->GetVariableLong(VID_OBJECT_ID);
   pObject = FindObjectById(dwObjectId);
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
      {
			if (pObject->Type() == OBJECT_NODE)
			{
				if (!(g_dwFlags & AF_DB_CONNECTION_LOST))
				{
					DB_RESULT hResult;
					DWORD i, dwItemId, dwMaxRows, dwTimeFrom, dwTimeTo;
					DWORD dwNumRows = 0;
					TCHAR szQuery[512], szCond[256];
					int iPos = 0, iType;
					DCI_DATA_HEADER *pData = NULL;
					DCI_DATA_ROW *pCurr;
					DCItem *dci;

					// Get request parameters
					dwItemId = pRequest->GetVariableLong(VID_DCI_ID);
					dwMaxRows = pRequest->GetVariableLong(VID_MAX_ROWS);
					dwTimeFrom = pRequest->GetVariableLong(VID_TIME_FROM);
					dwTimeTo = pRequest->GetVariableLong(VID_TIME_TO);

					dci = ((Node *)pObject)->getItemById(dwItemId);
					if (dci != NULL)
					{
						if ((dwMaxRows == 0) || (dwMaxRows > MAX_DCI_DATA_RECORDS))
							dwMaxRows = MAX_DCI_DATA_RECORDS;

						szCond[0] = 0;
						if (dwTimeFrom != 0)
						{
							_sntprintf(szCond, 256, _T(" AND idata_timestamp>=%d"), dwTimeFrom);
							iPos = (int)_tcslen(szCond);
						}
						if (dwTimeTo != 0)
						{
							_sntprintf(&szCond[iPos], 256 - iPos, _T(" AND idata_timestamp<=%d"), dwTimeTo);
						}

						// Get item's data type to determine actual row size
						iType = dci->getDataType();
						// Create database-dependent query for fetching N rows
						switch(g_nDBSyntax)
						{
							case DB_SYNTAX_MSSQL:
								_sntprintf(szQuery, 512, _T("SELECT TOP %d idata_timestamp,idata_value FROM idata_%d WHERE item_id=%d%s ORDER BY idata_timestamp DESC"),
											 dwMaxRows, dwObjectId, dwItemId, szCond);
								break;
							case DB_SYNTAX_ORACLE:
								_sntprintf(szQuery, 512, _T("SELECT idata_timestamp,idata_value FROM idata_%d WHERE item_id=%d%s AND ROWNUM <= %d ORDER BY idata_timestamp DESC"),
											 dwObjectId, dwItemId, szCond, dwMaxRows);
								break;
							case DB_SYNTAX_MYSQL:
							case DB_SYNTAX_PGSQL:
							case DB_SYNTAX_SQLITE:
								_sntprintf(szQuery, 512, _T("SELECT idata_timestamp,idata_value FROM idata_%d WHERE item_id=%d%s ORDER BY idata_timestamp DESC LIMIT %d"),
											 dwObjectId, dwItemId, szCond, dwMaxRows);
								break;
							default:
								_sntprintf(szQuery, 512, _T("SELECT idata_timestamp,idata_value FROM idata_%d WHERE item_id=%d%s ORDER BY idata_timestamp DESC LIMIT %d"),
											 dwObjectId, dwItemId, szCond, dwMaxRows);
								break;
						}
						hResult = DBSelect(g_hCoreDB, szQuery);
						if (hResult != NULL)
						{
							// Send CMD_REQUEST_COMPLETED message
							msg.SetVariable(VID_RCC, RCC_SUCCESS);
							dci->fillMessageWithThresholds(&msg);
							sendMessage(&msg);

							dwNumRows = DBGetNumRows(hResult);

							// Allocate memory for data and prepare data header
							pData = (DCI_DATA_HEADER *)malloc(dwNumRows * m_dwRowSize[iType] + sizeof(DCI_DATA_HEADER));
							pData->dwDataType = htonl((DWORD)iType);
							pData->dwItemId = htonl(dwItemId);

							// Fill memory block with records
							pCurr = (DCI_DATA_ROW *)(((char *)pData) + sizeof(DCI_DATA_HEADER));
							for(i = 0; i < dwNumRows; i++)
							{
								pCurr->dwTimeStamp = htonl(DBGetFieldULong(hResult, i, 0));
								switch(iType)
								{
									case DCI_DT_INT:
									case DCI_DT_UINT:
										pCurr->value.dwInteger = htonl(DBGetFieldULong(hResult, i, 1));
										break;
									case DCI_DT_INT64:
									case DCI_DT_UINT64:
										pCurr->value.qwInt64 = htonq(DBGetFieldUInt64(hResult, i, 1));
										break;
									case DCI_DT_FLOAT:
										pCurr->value.dFloat = htond(DBGetFieldDouble(hResult, i, 1));
										break;
									case DCI_DT_STRING:
#ifdef UNICODE
#ifdef UNICODE_UCS4
										DBGetField(hResult, i, 1, szBuffer, MAX_DCI_STRING_VALUE);
										ucs4_to_ucs2(szBuffer, -1, pCurr->value.szString, MAX_DCI_STRING_VALUE);
#else
										DBGetField(hResult, i, 1, pCurr->value.szString, MAX_DCI_STRING_VALUE);
#endif                        
#else
										DBGetField(hResult, i, 1, szBuffer, MAX_DCI_STRING_VALUE);
										mb_to_ucs2(szBuffer, -1, pCurr->value.szString, MAX_DCI_STRING_VALUE);
#endif
										SwapWideString(pCurr->value.szString);
										break;
								}
								pCurr = (DCI_DATA_ROW *)(((char *)pCurr) + m_dwRowSize[iType]);
							}
							DBFreeResult(hResult);
							pData->dwNumRows = htonl(dwNumRows);

							// Prepare and send raw message with fetched data
							m_pSendQueue->Put(
								CreateRawNXCPMessage(CMD_DCI_DATA, pRequest->GetId(), 0,
															dwNumRows * m_dwRowSize[iType] + sizeof(DCI_DATA_HEADER),
															pData, NULL));
							free(pData);
							bSuccess = TRUE;
						}
						else
						{
							msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
						}
					}
					else
					{
						msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
					}
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_DB_CONNECTION_LOST);
				}
			}
			else
			{
	         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
			}
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else  // No object with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   if (!bSuccess)
      sendMessage(&msg);
}


//
// Send latest collected values for all DCIs of given node
//

void ClientSession::SendLastValues(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
      {
         if (pObject->Type() == OBJECT_NODE)
         {
            msg.SetVariable(VID_RCC, ((Node *)pObject)->getLastValues(&msg));
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else  // No object with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Open event processing policy
//

void ClientSession::OpenEPP(DWORD dwRqId)
{
   CSCPMessage msg;
   TCHAR szBuffer[256];
   BOOL bSuccess = FALSE;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   if (m_dwSystemAccess & SYSTEM_ACCESS_EPP)
   {
      if (!LockComponent(CID_EPP, m_dwIndex, m_szUserName, NULL, szBuffer))
      {
         msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
         msg.SetVariable(VID_LOCKED_BY, szBuffer);
      }
      else
      {
         m_dwFlags |= CSF_EPP_LOCKED;
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         msg.SetVariable(VID_NUM_RULES, g_pEventPolicy->NumRules());
         bSuccess = TRUE;
      }
   }
   else
   {
      // Current user has no rights for event policy management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);

   // Send policy to client
   if (bSuccess)
      g_pEventPolicy->SendToClient(this, dwRqId);
}


//
// Close event processing policy
//

void ClientSession::CloseEPP(DWORD dwRqId)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   if (m_dwSystemAccess & SYSTEM_ACCESS_EPP)
   {
      if (m_dwFlags & CSF_EPP_LOCKED)
      {
         UnlockComponent(CID_EPP);
         m_dwFlags &= ~CSF_EPP_LOCKED;
      }
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
   }
   else
   {
      // Current user has no rights for event policy management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Save event processing policy
//

void ClientSession::SaveEPP(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_EPP)
   {
      if (m_dwFlags & CSF_EPP_LOCKED)
      {
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         m_dwNumRecordsToUpload = pRequest->GetVariableLong(VID_NUM_RULES);
         m_dwRecordsUploaded = 0;
         if (m_dwNumRecordsToUpload == 0)
         {
            g_pEventPolicy->ReplacePolicy(0, NULL);
            g_pEventPolicy->SaveToDB();
         }
         else
         {
            m_dwFlags |= CSF_EPP_UPLOAD;
            m_ppEPPRuleList = (EPRule **)malloc(sizeof(EPRule *) * m_dwNumRecordsToUpload);
            memset(m_ppEPPRuleList, 0, sizeof(EPRule *) * m_dwNumRecordsToUpload);
         }
         DebugPrintf(5, _T("Accepted EPP upload request for %d rules"), m_dwNumRecordsToUpload);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
      }
   }
   else
   {
      // Current user has no rights for event policy management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Process EPP rule received from client
//

void ClientSession::ProcessEPPRecord(CSCPMessage *pRequest)
{
   if (!(m_dwFlags & CSF_EPP_LOCKED))
   {
      CSCPMessage msg;

      msg.SetCode(CMD_REQUEST_COMPLETED);
      msg.SetId(pRequest->GetId());
      msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
      sendMessage(&msg);
   }
   else
   {
      if (m_dwRecordsUploaded < m_dwNumRecordsToUpload)
      {
         m_ppEPPRuleList[m_dwRecordsUploaded] = new EPRule(pRequest);
         m_dwRecordsUploaded++;
         if (m_dwRecordsUploaded == m_dwNumRecordsToUpload)
         {
            CSCPMessage msg;

            // All records received, replace event policy...
            DebugPrintf(5, _T("Replacing event processing policy with a new one at %p (%d rules)"),
                        m_ppEPPRuleList, m_dwNumRecordsToUpload);
            g_pEventPolicy->ReplacePolicy(m_dwNumRecordsToUpload, m_ppEPPRuleList);
            g_pEventPolicy->SaveToDB();
            m_ppEPPRuleList = NULL;
            
            // ... and send final confirmation
            msg.SetCode(CMD_REQUEST_COMPLETED);
            msg.SetId(pRequest->GetId());
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            sendMessage(&msg);

            m_dwFlags &= ~CSF_EPP_UPLOAD;
         }
      }
   }
}


//
// Send compiled MIB file to client
//

void ClientSession::SendMIB(DWORD dwRqId)
{
   TCHAR szBuffer[MAX_PATH];

   // Send compiled MIB file
   _tcscpy(szBuffer, g_szDataDir);
   _tcscat(szBuffer, DFILE_COMPILED_MIB);
   SendFileOverNXCP(m_hSocket, dwRqId, szBuffer, m_pCtx, 0, NULL, NULL);
}


//
// Send timestamp of compiled MIB file to client
//

void ClientSession::SendMIBTimestamp(DWORD dwRqId)
{
   CSCPMessage msg;
   TCHAR szBuffer[MAX_PATH];
   DWORD dwResult, dwTimeStamp;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   _tcscpy(szBuffer, g_szDataDir);
   _tcscat(szBuffer, DFILE_COMPILED_MIB);
   dwResult = SNMPGetMIBTreeTimestamp(szBuffer, &dwTimeStamp);
   if (dwResult == SNMP_ERR_SUCCESS)
   {
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
      msg.SetVariable(VID_TIMESTAMP, dwTimeStamp);
		msg.SetVariable(VID_FILE_SIZE, FileSize(szBuffer));
   }
   else
   {
      switch(dwResult)
      {
         case SNMP_ERR_FILE_IO:
            msg.SetVariable(VID_RCC, RCC_FILE_IO_ERROR);
            break;
         case SNMP_ERR_BAD_FILE_HEADER:
            msg.SetVariable(VID_RCC, RCC_CORRUPTED_MIB_FILE);
            break;
         default:
            msg.SetVariable(VID_RCC, RCC_INTERNAL_ERROR);
            break;
      }
   }

   // Send response
   sendMessage(&msg);
}


//
// Create new object
//

void ClientSession::createObject(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject, *pParent;
   int iClass, iServiceType;
   TCHAR szObjectName[MAX_OBJECT_NAME];
   TCHAR *pszRequest, *pszResponse, *pszComments;
   DWORD dwIpAddr;
   WORD wIpProto, wIpPort;
   BOOL bParentAlwaysValid = FALSE;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   iClass = pRequest->GetVariableShort(VID_OBJECT_CLASS);

   // Find parent object
   pParent = FindObjectById(pRequest->GetVariableLong(VID_PARENT_ID));
   if (iClass == OBJECT_NODE)
   {
      dwIpAddr = pRequest->GetVariableLong(VID_IP_ADDRESS);
      if ((pParent == NULL) && (dwIpAddr != 0))
      {
         pParent = FindSubnetForNode(dwIpAddr);
         bParentAlwaysValid = TRUE;
      }
   }
   if ((pParent != NULL) || (iClass == OBJECT_NODE))
   {
      // User should have create access to parent object
      if ((pParent != NULL) ? 
            pParent->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_CREATE) :
            g_pEntireNet->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_CREATE))
      {
         // Parent object should be of valid type
         if (bParentAlwaysValid || IsValidParentClass(iClass, (pParent != NULL) ? pParent->Type() : -1))
         {
            pRequest->GetVariableStr(VID_OBJECT_NAME, szObjectName, MAX_OBJECT_NAME);
            if (IsValidObjectName(szObjectName, TRUE))
            {
               // Create new object
               switch(iClass)
               {
                  case OBJECT_NODE:
                     pObject = PollNewNode(dwIpAddr,
                                           pRequest->GetVariableLong(VID_IP_NETMASK),
                                           pRequest->GetVariableLong(VID_CREATION_FLAGS),
                                           szObjectName,
                                           pRequest->GetVariableLong(VID_PROXY_NODE),
                                           pRequest->GetVariableLong(VID_SNMP_PROXY),
														 (pParent != NULL) ? ((pParent->Type() == OBJECT_CLUSTER) ? (Cluster *)pParent : NULL) : NULL);
                     break;
                  case OBJECT_CONTAINER:
                     pObject = new Container(szObjectName, 
                                             pRequest->GetVariableLong(VID_CATEGORY));
                     NetObjInsert(pObject, TRUE);
                     break;
                  case OBJECT_TEMPLATEGROUP:
                     pObject = new TemplateGroup(szObjectName);
                     NetObjInsert(pObject, TRUE);
							pObject->CalculateCompoundStatus();	// Force status change to NORMAL
                     break;
                  case OBJECT_TEMPLATE:
                     pObject = new Template(szObjectName);
                     NetObjInsert(pObject, TRUE);
							pObject->CalculateCompoundStatus();	// Force status change to NORMAL
                     break;
                  case OBJECT_POLICYGROUP:
                     pObject = new PolicyGroup(szObjectName);
                     NetObjInsert(pObject, TRUE);
							pObject->CalculateCompoundStatus();	// Force status change to NORMAL
                     break;
                  case OBJECT_AGENTPOLICY_CONFIG:
                     pObject = new AgentPolicyConfig(szObjectName);
                     NetObjInsert(pObject, TRUE);
							pObject->CalculateCompoundStatus();	// Force status change to NORMAL
                     break;
                  case OBJECT_CLUSTER:
                     pObject = new Cluster(szObjectName);
                     NetObjInsert(pObject, TRUE);
                     break;
                  case OBJECT_NETWORKSERVICE:
                     iServiceType = (int)pRequest->GetVariableShort(VID_SERVICE_TYPE);
                     wIpProto = pRequest->GetVariableShort(VID_IP_PROTO);
                     wIpPort = pRequest->GetVariableShort(VID_IP_PORT);
                     pszRequest = pRequest->GetVariableStr(VID_SERVICE_REQUEST);
                     pszResponse = pRequest->GetVariableStr(VID_SERVICE_RESPONSE);
                     pObject = new NetworkService(iServiceType, wIpProto, wIpPort, 
                                                  pszRequest, pszResponse, (Node *)pParent);
                     pObject->SetName(szObjectName);
                     NetObjInsert(pObject, TRUE);
                     break;
                  case OBJECT_VPNCONNECTOR:
                     pObject = new VPNConnector(TRUE);
                     pObject->SetName(szObjectName);
                     NetObjInsert(pObject, TRUE);
                     break;
                  case OBJECT_CONDITION:
                     pObject = new Condition(TRUE);
                     pObject->SetName(szObjectName);
                     NetObjInsert(pObject, TRUE);
                     break;
                  case OBJECT_NETWORKMAPGROUP:
                     pObject = new NetworkMapGroup(szObjectName);
                     NetObjInsert(pObject, TRUE);
							pObject->CalculateCompoundStatus();	// Force status change to NORMAL
                     break;
						case OBJECT_NETWORKMAP:
							pObject = new NetworkMap((int)pRequest->GetVariableShort(VID_MAP_TYPE), pRequest->GetVariableLong(VID_SEED_OBJECT));
                     pObject->SetName(szObjectName);
                     NetObjInsert(pObject, TRUE);
							break;
                  default:
                     break;
               }

               // If creation was successful do binding and set comments if needed
               if (pObject != NULL)
               {
                  if (pParent != NULL)    // parent can be NULL for nodes
                  {
                     pParent->AddChild(pObject);
                     pObject->AddParent(pParent);
                     pParent->CalculateCompoundStatus();
							if (pParent->Type() == OBJECT_CLUSTER)
							{
								((Cluster *)pParent)->ApplyToNode((Node *)pObject);
							}
                  }
						
						pszComments = pRequest->GetVariableStr(VID_COMMENTS);
						if (pszComments != NULL)
							pObject->SetComments(pszComments);

                  pObject->Unhide();
                  msg.SetVariable(VID_RCC, RCC_SUCCESS);
                  msg.SetVariable(VID_OBJECT_ID, pObject->Id());
               }
               else
               {
						// :DIRTY HACK:
						// PollNewNode will return NULL only if IP already
						// in use. some new() can fail there too, but server will
						// crash in that case
						if (iClass == OBJECT_NODE)
						{
                  	msg.SetVariable(VID_RCC, RCC_ALREADY_EXIST);
						}
						else
						{
                  	msg.SetVariable(VID_RCC, RCC_OBJECT_CREATION_FAILED);
						}
               }
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Add cluster node
//

void ClientSession::addClusterNode(CSCPMessage *request)
{
   CSCPMessage msg;
	NetObj *cluster, *node;

	msg.SetId(request->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

   cluster = FindObjectById(request->GetVariableLong(VID_PARENT_ID));
   node = FindObjectById(request->GetVariableLong(VID_CHILD_ID));
	if ((cluster != NULL) && (node != NULL))
	{
		if ((cluster->Type() == OBJECT_CLUSTER) && (node->Type() == OBJECT_NODE))
		{
			if (((Node *)node)->getMyCluster() == NULL)
			{
				if (cluster->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY) &&
					 node->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
				{
					((Cluster *)cluster)->ApplyToNode((Node *)node);
					((Node *)node)->setRecheckCapsFlag();
					((Node *)node)->forceConfigurationPoll();

					msg.SetVariable(VID_RCC, RCC_SUCCESS);
					WriteAuditLog(AUDIT_OBJECTS, TRUE, m_dwUserId, m_szWorkstation, cluster->Id(),
									  _T("Node %s [%d] added to cluster %s [%d]"), 
									  node->Name(), node->Id(), cluster->Name(), cluster->Id());
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
					WriteAuditLog(AUDIT_OBJECTS, FALSE, m_dwUserId, m_szWorkstation, cluster->Id(),
									  _T("Access denied on adding node %s [%d] to cluster %s [%d]"), 
									  node->Name(), node->Id(), cluster->Name(), cluster->Id());
				}
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_CLUSTER_MEMBER_ALREADY);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
	}

   // Send response
   sendMessage(&msg);
}


//
// Bind/unbind object
//

void ClientSession::changeObjectBinding(CSCPMessage *pRequest, BOOL bBind)
{
   CSCPMessage msg;
   NetObj *pParent, *pChild;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get parent and child objects
   pParent = FindObjectById(pRequest->GetVariableLong(VID_PARENT_ID));
   pChild = FindObjectById(pRequest->GetVariableLong(VID_CHILD_ID));

   // Check access rights and change binding
   if ((pParent != NULL) && (pChild != NULL))
   {
      // User should have modify access to both objects
      if ((pParent->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY)) &&
          (pChild->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY)))
      {
         // Parent object should be container or service root,
			// or template group/root for templates
         // For unbind, it can also be template or cluster
         if ((pParent->Type() == OBJECT_CONTAINER) ||
             (pParent->Type() == OBJECT_SERVICEROOT) ||
             ((pParent->Type() == OBJECT_TEMPLATE) && (!bBind)) ||
             ((pParent->Type() == OBJECT_CLUSTER) && (!bBind)) ||
				 ((pParent->Type() == OBJECT_TEMPLATEGROUP) && (pChild->Type() == OBJECT_TEMPLATE)) ||
				 ((pParent->Type() == OBJECT_TEMPLATEROOT) && (pChild->Type() == OBJECT_TEMPLATE)))
         {
            if (bBind)
            {
               // Prevent loops
               if (!pChild->IsChild(pParent->Id()))
               {
                  pParent->AddChild(pChild);
                  pChild->AddParent(pParent);
                  pParent->CalculateCompoundStatus();
                  msg.SetVariable(VID_RCC, RCC_SUCCESS);
               }
               else
               {
                  msg.SetVariable(VID_RCC, RCC_OBJECT_LOOP);
               }
            }
            else
            {
               pParent->DeleteChild(pChild);
               pChild->DeleteParent(pParent);
               if ((pParent->Type() == OBJECT_TEMPLATE) &&
                   (pChild->Type() == OBJECT_NODE))
               {
                  ((Template *)pParent)->queueRemoveFromNode(pChild->Id(), pRequest->GetVariableShort(VID_REMOVE_DCI));
               }
               else if ((pParent->Type() == OBJECT_CLUSTER) &&
                        (pChild->Type() == OBJECT_NODE))
               {
                  ((Cluster *)pParent)->queueRemoveFromNode(pChild->Id(), TRUE);
						((Node *)pChild)->setRecheckCapsFlag();
						((Node *)pChild)->forceConfigurationPoll();
               }
               msg.SetVariable(VID_RCC, RCC_SUCCESS);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Worker thread for object deletion
//

static THREAD_RESULT THREAD_CALL DeleteObjectWorker(void *arg)
{
	((NetObj *)arg)->Delete(FALSE);
	return THREAD_OK;
}


//
// Delete object
//

void ClientSession::deleteObject(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Find object to be deleted
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      // Check if it is a built-in object, like _T("Entire Network")
      if (pObject->Id() >= 10)
      {
         // Check access rights
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_DELETE))
         {
				ThreadCreate(DeleteObjectWorker, 0, pObject);
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Process changes in alarms
//

void ClientSession::onAlarmUpdate(DWORD dwCode, NXC_ALARM *pAlarm)
{
   UPDATE_INFO *pUpdate;
   NetObj *pObject;

   if (isAuthenticated() && (m_dwActiveChannels & NXC_CHANNEL_ALARMS))
   {
      pObject = FindObjectById(pAlarm->dwSourceObject);
      if (pObject != NULL)
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ_ALARMS))
         {
            pUpdate = (UPDATE_INFO *)malloc(sizeof(UPDATE_INFO));
            pUpdate->dwCategory = INFO_CAT_ALARM;
            pUpdate->dwCode = dwCode;
            pUpdate->pData = nx_memdup(pAlarm, sizeof(NXC_ALARM));
            m_pUpdateQueue->Put(pUpdate);
         }
   }
}


//
// Send all alarms to client
//

void ClientSession::SendAllAlarms(DWORD dwRqId, BOOL bIncludeAck)
{
   MutexLock(m_mutexSendAlarms, INFINITE);
   g_alarmMgr.SendAlarmsToClient(dwRqId, bIncludeAck, this);
   MutexUnlock(m_mutexSendAlarms);
}


//
// Acknowledge alarm
//

void ClientSession::AcknowledgeAlarm(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD dwAlarmId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get alarm id and it's source object
   dwAlarmId = pRequest->GetVariableLong(VID_ALARM_ID);
   pObject = g_alarmMgr.GetAlarmSourceObject(dwAlarmId);
   if (pObject != NULL)
   {
      // User should have _T("acknowledge alarm") right to the object
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_ACK_ALARMS))
      {
         msg.SetVariable(VID_RCC, g_alarmMgr.AckById(dwAlarmId, m_dwUserId));
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      // Normally, for existing alarms pObject will not be NULL,
      // so we assume that alarm id is invalid
      msg.SetVariable(VID_RCC, RCC_INVALID_ALARM_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Terminate alarm
//

void ClientSession::TerminateAlarm(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD dwAlarmId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get alarm id and it's source object
   dwAlarmId = pRequest->GetVariableLong(VID_ALARM_ID);
   pObject = g_alarmMgr.GetAlarmSourceObject(dwAlarmId);
   if (pObject != NULL)
   {
      // User should have _T("terminate alarm") right to the object
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_TERM_ALARMS))
      {
         msg.SetVariable(VID_RCC, g_alarmMgr.TerminateById(dwAlarmId, m_dwUserId));
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      // Normally, for existing alarms pObject will not be NULL,
      // so we assume that alarm id is invalid
      msg.SetVariable(VID_RCC, RCC_INVALID_ALARM_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Delete alarm
//

void ClientSession::DeleteAlarm(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD dwAlarmId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get alarm id and it's source object
   dwAlarmId = pRequest->GetVariableLong(VID_ALARM_ID);
   pObject = g_alarmMgr.GetAlarmSourceObject(dwAlarmId);
   if (pObject != NULL)
   {
      // User should have _T("terminate alarm") right to the object
      // and system right _T("delete alarms")
      if ((pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_TERM_ALARMS)) &&
          (m_dwSystemAccess & SYSTEM_ACCESS_DELETE_ALARMS))
      {
         g_alarmMgr.DeleteAlarm(dwAlarmId);
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      // Normally, for existing alarms pObject will not be NULL,
      // so we assume that alarm id is invalid
      msg.SetVariable(VID_RCC, RCC_INVALID_ALARM_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Create new action
//

void ClientSession::CreateAction(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_ACTIONS)
   {
      DWORD dwResult, dwActionId;
      TCHAR szActionName[MAX_OBJECT_NAME];

      pRequest->GetVariableStr(VID_ACTION_NAME, szActionName, MAX_OBJECT_NAME);
      if (IsValidObjectName(szActionName, TRUE))
      {
         dwResult = CreateNewAction(szActionName, &dwActionId);
         msg.SetVariable(VID_RCC, dwResult);
         if (dwResult == RCC_SUCCESS)
            msg.SetVariable(VID_ACTION_ID, dwActionId);   // Send id of new action to client
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Update existing action's data
//

void ClientSession::UpdateAction(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_ACTIONS)
   {
      msg.SetVariable(VID_RCC, ModifyActionFromMessage(pRequest));
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Delete action
//

void ClientSession::DeleteAction(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwActionId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_ACTIONS)
   {
      // Get Id of action to be deleted
      dwActionId = pRequest->GetVariableLong(VID_ACTION_ID);
      if (!g_pEventPolicy->ActionInUse(dwActionId))
      {
         msg.SetVariable(VID_RCC, DeleteActionFromDB(dwActionId));
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACTION_IN_USE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Process changes in actions
//

void ClientSession::onActionDBUpdate(DWORD dwCode, NXC_ACTION *pAction)
{
   UPDATE_INFO *pUpdate;

   if (isAuthenticated())
   {
      if ((m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_ACTIONS) ||
			 (m_dwSystemAccess & SYSTEM_ACCESS_EPP))
      {
         pUpdate = (UPDATE_INFO *)malloc(sizeof(UPDATE_INFO));
         pUpdate->dwCategory = INFO_CAT_ACTION;
         pUpdate->dwCode = dwCode;
         pUpdate->pData = nx_memdup(pAction, sizeof(NXC_ACTION));
         m_pUpdateQueue->Put(pUpdate);
      }
   }
}


//
// Send all actions to client
//

void ClientSession::SendAllActions(DWORD dwRqId)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   // Check user rights
   if ((m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_ACTIONS) ||
       (m_dwSystemAccess & SYSTEM_ACCESS_EPP))
   {
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
      sendMessage(&msg);
      MutexLock(m_mutexSendActions, INFINITE);
      SendActionsToClient(this, dwRqId);
      MutexUnlock(m_mutexSendActions);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      sendMessage(&msg);
   }
}


//
// Send list of configured container categories to client
//

void ClientSession::SendContainerCategories(DWORD dwRqId)
{
   CSCPMessage msg;
   DWORD i;

   // Prepare response message
   msg.SetCode(CMD_CONTAINER_CAT_DATA);
   msg.SetId(dwRqId);

   for(i = 0; i < g_dwNumCategories; i++)
   {
      msg.SetVariable(VID_CATEGORY_ID, g_pContainerCatList[i].dwCatId);
      msg.SetVariable(VID_CATEGORY_NAME, g_pContainerCatList[i].szName);
      //msg.SetVariable(VID_IMAGE_ID, g_pContainerCatList[i].dwImageId);
      msg.SetVariable(VID_DESCRIPTION, g_pContainerCatList[i].pszDescription);
      sendMessage(&msg);
      msg.DeleteAllVariables();
   }

   // Send end-of-list indicator
   msg.SetVariable(VID_CATEGORY_ID, (DWORD)0);
   sendMessage(&msg);
}


//
// Perform a forced node poll
//

void ClientSession::ForcedNodePoll(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   POLLER_START_DATA *pData;
   NetObj *pObject;

   pData = (POLLER_START_DATA *)malloc(sizeof(POLLER_START_DATA));
   pData->pSession = this;
   MutexLock(m_mutexPollerInit, INFINITE);

   // Prepare response message
   pData->dwRqId = pRequest->GetId();
   msg.SetCode(CMD_POLLING_INFO);
   msg.SetId(pData->dwRqId);

   // Get polling type
   pData->iPollType = pRequest->GetVariableShort(VID_POLL_TYPE);

   // Find object to be polled
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      // We can do polls only for node objects
      if ((pObject->Type() == OBJECT_NODE) &&
          ((pData->iPollType == POLL_STATUS) ||
			  (pData->iPollType == POLL_CONFIGURATION) ||
			  (pData->iPollType == POLL_INTERFACE_NAMES)))
      {
         // Check access rights
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
         {
            ((Node *)pObject)->IncRefCount();
            m_dwRefCount++;

            pData->pNode = (Node *)pObject;
            ThreadCreate(PollerThreadStarter, 0, pData);
            msg.SetVariable(VID_RCC, RCC_OPERATION_IN_PROGRESS);
            msg.SetVariable(VID_POLLER_MESSAGE, _T("Poll request accepted\r\n"));
				pData = NULL;
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
   MutexUnlock(m_mutexPollerInit);
	safe_free(pData);
}


//
// Send message from poller to client
//

void ClientSession::sendPollerMsg(DWORD dwRqId, const TCHAR *pszMsg)
{
   CSCPMessage msg;

   msg.SetCode(CMD_POLLING_INFO);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_RCC, RCC_OPERATION_IN_PROGRESS);
   msg.SetVariable(VID_POLLER_MESSAGE, pszMsg);
   sendMessage(&msg);
}


//
// Node poller thread
//

void ClientSession::PollerThread(Node *pNode, int iPollType, DWORD dwRqId)
{
   CSCPMessage msg;

   // Wait while parent thread finishes initialization
   MutexLock(m_mutexPollerInit, INFINITE);
   MutexUnlock(m_mutexPollerInit);

   switch(iPollType)
   {
      case POLL_STATUS:
         pNode->statusPoll(this, dwRqId, -1);
         break;
      case POLL_CONFIGURATION:
			pNode->setRecheckCapsFlag();
         pNode->configurationPoll(this, dwRqId, -1, 0);
         break;
      case POLL_INTERFACE_NAMES:
         pNode->updateInterfaceNames(this, dwRqId);
         break;
      default:
         sendPollerMsg(dwRqId, _T("Invalid poll type requested\r\n"));
         break;
   }
   pNode->DecRefCount();

   msg.SetCode(CMD_POLLING_INFO);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_RCC, RCC_SUCCESS);
   sendMessage(&msg);
}


//
// Receive event from user
//

void ClientSession::OnTrap(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwObjectId, dwEventCode;
   int i, iNumArgs;
   NetObj *pObject;
   TCHAR *pszArgList[32], szUserTag[MAX_USERTAG_LENGTH] = _T("");
   char szFormat[] = "ssssssssssssssssssssssssssssssss";
   BOOL bSuccess;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Find event's source object
   dwObjectId = pRequest->GetVariableLong(VID_OBJECT_ID);
   if (dwObjectId != 0)
      pObject = FindObjectById(dwObjectId);  // Object is specified explicitely
   else
      pObject = FindNodeByIP(m_dwHostAddr);  // Client is the source
   if (pObject != NULL)
   {
      // User should have SEND_EVENTS access right to object
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_SEND_EVENTS))
      {
         dwEventCode = pRequest->GetVariableLong(VID_EVENT_CODE);
			pRequest->GetVariableStr(VID_USER_TAG, szUserTag, MAX_USERTAG_LENGTH);
         iNumArgs = pRequest->GetVariableShort(VID_NUM_ARGS);
         if (iNumArgs > 32)
            iNumArgs = 32;
         for(i = 0; i < iNumArgs; i++)
            pszArgList[i] = pRequest->GetVariableStr(VID_EVENT_ARG_BASE + i);

         szFormat[iNumArgs] = 0;
         bSuccess = PostEventWithTag(dwEventCode, pObject->Id(), szUserTag, (iNumArgs > 0) ? szFormat : NULL,
                                     pszArgList[0], pszArgList[1], pszArgList[2], pszArgList[3],
                                     pszArgList[4], pszArgList[5], pszArgList[6], pszArgList[7],
                                     pszArgList[8], pszArgList[9], pszArgList[10], pszArgList[11],
                                     pszArgList[12], pszArgList[13], pszArgList[14], pszArgList[15],
                                     pszArgList[16], pszArgList[17], pszArgList[18], pszArgList[19],
                                     pszArgList[20], pszArgList[21], pszArgList[22], pszArgList[23],
                                     pszArgList[24], pszArgList[25], pszArgList[26], pszArgList[27],
                                     pszArgList[28], pszArgList[29], pszArgList[30], pszArgList[31]);
         
         // Cleanup
         for(i = 0; i < iNumArgs; i++)
            safe_free(pszArgList[i]);

         msg.SetVariable(VID_RCC, bSuccess ? RCC_SUCCESS : RCC_INVALID_EVENT_CODE);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Wake up node
//

void ClientSession::OnWakeUpNode(CSCPMessage *pRequest)
{
   NetObj *pObject;
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Find node or interface object
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if ((pObject->Type() == OBJECT_NODE) ||
          (pObject->Type() == OBJECT_INTERFACE))
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_CONTROL))
         {
            DWORD dwResult;

            if (pObject->Type() == OBJECT_NODE)
               dwResult = ((Node *)pObject)->wakeUp();
            else
               dwResult = ((Interface *)pObject)->wakeUp();
            msg.SetVariable(VID_RCC, dwResult);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Query specific parameter from node
//

void ClientSession::QueryParameter(CSCPMessage *pRequest)
{
   NetObj *pObject;
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Find node object
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
         {
            DWORD dwResult;
            TCHAR szBuffer[256], szName[MAX_PARAM_NAME];

            pRequest->GetVariableStr(VID_NAME, szName, MAX_PARAM_NAME);
            dwResult = ((Node *)pObject)->GetItemForClient(pRequest->GetVariableShort(VID_DCI_SOURCE_TYPE),
                                                           szName, szBuffer, 256);
            msg.SetVariable(VID_RCC, dwResult);
            if (dwResult == RCC_SUCCESS)
               msg.SetVariable(VID_VALUE, szBuffer);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Edit trap configuration record
//

void ClientSession::EditTrap(int iOperation, CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwTrapId, dwResult;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check access rights
   if (checkSysAccessRights(SYSTEM_ACCESS_CONFIGURE_TRAPS))
   {
      switch(iOperation)
      {
         case TRAP_CREATE:
            dwResult = CreateNewTrap(&dwTrapId);
            msg.SetVariable(VID_RCC, dwResult);
            if (dwResult == RCC_SUCCESS)
               msg.SetVariable(VID_TRAP_ID, dwTrapId);   // Send id of new trap to client
            break;
         case TRAP_UPDATE:
            msg.SetVariable(VID_RCC, UpdateTrapFromMsg(pRequest));
            break;
         case TRAP_DELETE:
            dwTrapId = pRequest->GetVariableLong(VID_TRAP_ID);
            msg.SetVariable(VID_RCC, DeleteTrap(dwTrapId));
            break;
         default:
				msg.SetVariable(VID_RCC, RCC_INVALID_ARGUMENT);
            break;
      }
   }
   else
   {
      // Current user has no rights for trap management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send all trap configuration records to client
//

void ClientSession::SendAllTraps(DWORD dwRqId)
{
   CSCPMessage msg;
   BOOL bSuccess = FALSE;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

	if (checkSysAccessRights(SYSTEM_ACCESS_CONFIGURE_TRAPS))
   {
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
      sendMessage(&msg);
      bSuccess = TRUE;
      SendTrapsToClient(this, dwRqId);
   }
   else
   {
      // Current user has no rights for trap management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   if (!bSuccess)
      sendMessage(&msg);
}


//
// Send all trap configuration records to client
// Send all in one message with less information and don't need a lock
//

void ClientSession::SendAllTraps2(DWORD dwRqId)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

	if (checkSysAccessRights(SYSTEM_ACCESS_CONFIGURE_TRAPS))
   {
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
      CreateTrapCfgMessage(msg);
   }
   else
   {
      // Current user has no rights for trap management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Lock/unlock package database
//

void ClientSession::LockPackageDB(DWORD dwRqId, BOOL bLock)
{
   CSCPMessage msg;
   TCHAR szBuffer[256];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_PACKAGES)
   {
      if (bLock)
      {
         if (!LockComponent(CID_PACKAGE_DB, m_dwIndex, m_szUserName, NULL, szBuffer))
         {
            msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
            msg.SetVariable(VID_LOCKED_BY, szBuffer);
         }
         else
         {
            m_dwFlags |= CSF_PACKAGE_DB_LOCKED;
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
      }
      else
      {
         if (m_dwFlags & CSF_PACKAGE_DB_LOCKED)
         {
            UnlockComponent(CID_PACKAGE_DB);
            m_dwFlags &= ~CSF_PACKAGE_DB_LOCKED;
         }
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
   }
   else
   {
      // Current user has no rights for trap management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send list of all installed packages to client
//

void ClientSession::SendAllPackages(DWORD dwRqId)
{
   CSCPMessage msg;
   DB_ASYNC_RESULT hResult;
   BOOL bSuccess = FALSE;
   TCHAR szBuffer[MAX_DB_STRING];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_PACKAGES)
   {
      if (m_dwFlags & CSF_PACKAGE_DB_LOCKED)
      {
         hResult = DBAsyncSelect(g_hCoreDB, _T("SELECT pkg_id,version,platform,pkg_file,pkg_name,description FROM agent_pkg"));
         if (hResult != NULL)
         {
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            sendMessage(&msg);
            bSuccess = TRUE;

            msg.SetCode(CMD_PACKAGE_INFO);
            msg.DeleteAllVariables();

            while(DBFetch(hResult))
            {
               msg.SetVariable(VID_PACKAGE_ID, DBGetFieldAsyncULong(hResult, 0));
               msg.SetVariable(VID_PACKAGE_VERSION, DBGetFieldAsync(hResult, 1, szBuffer, MAX_DB_STRING));
               msg.SetVariable(VID_PLATFORM_NAME, DBGetFieldAsync(hResult, 2, szBuffer, MAX_DB_STRING));
               msg.SetVariable(VID_FILE_NAME, DBGetFieldAsync(hResult, 3, szBuffer, MAX_DB_STRING));
               msg.SetVariable(VID_PACKAGE_NAME, DBGetFieldAsync(hResult, 4, szBuffer, MAX_DB_STRING));
               DBGetFieldAsync(hResult, 5, szBuffer, MAX_DB_STRING);
               DecodeSQLString(szBuffer);
               msg.SetVariable(VID_DESCRIPTION, szBuffer);
               sendMessage(&msg);
               msg.DeleteAllVariables();
            }

            msg.SetVariable(VID_PACKAGE_ID, (DWORD)0);
            sendMessage(&msg);

            DBFreeAsyncResult(g_hCoreDB, hResult);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
      }
   }
   else
   {
      // Current user has no rights for package management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   if (!bSuccess)
      sendMessage(&msg);
}


//
// Install package to server
//

void ClientSession::InstallPackage(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szPkgName[MAX_PACKAGE_NAME_LEN], szDescription[MAX_DB_STRING];
   TCHAR szPkgVersion[MAX_AGENT_VERSION_LEN], szFileName[MAX_DB_STRING];
   TCHAR szPlatform[MAX_PLATFORM_NAME_LEN], *pszCleanFileName, *pszEscDescr;
   TCHAR szQuery[2048];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_PACKAGES)
   {
      if (m_dwFlags & CSF_PACKAGE_DB_LOCKED)
      {
         pRequest->GetVariableStr(VID_PACKAGE_NAME, szPkgName, MAX_PACKAGE_NAME_LEN);
         pRequest->GetVariableStr(VID_DESCRIPTION, szDescription, MAX_DB_STRING);
         pRequest->GetVariableStr(VID_FILE_NAME, szFileName, MAX_DB_STRING);
         pRequest->GetVariableStr(VID_PACKAGE_VERSION, szPkgVersion, MAX_AGENT_VERSION_LEN);
         pRequest->GetVariableStr(VID_PLATFORM_NAME, szPlatform, MAX_PLATFORM_NAME_LEN);

         // Remove possible path specification from file name
         pszCleanFileName = GetCleanFileName(szFileName);

         if (IsValidObjectName(pszCleanFileName) && 
             IsValidObjectName(szPkgName) &&
             IsValidObjectName(szPkgVersion) &&
             IsValidObjectName(szPlatform))
         {
            // Check if same package already exist
            if (!IsPackageInstalled(szPkgName, szPkgVersion, szPlatform))
            {
               // Check for duplicate file name
               if (!IsPackageFileExist(pszCleanFileName))
               {
                  // Prepare for file receive
                  if (m_hCurrFile == -1)
                  {
                     _tcscpy(m_szCurrFileName, g_szDataDir);
                     _tcscat(m_szCurrFileName, DDIR_PACKAGES);
                     _tcscat(m_szCurrFileName, FS_PATH_SEPARATOR);
                     _tcscat(m_szCurrFileName, pszCleanFileName);
                     m_hCurrFile = _topen(m_szCurrFileName, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR);
                     if (m_hCurrFile != -1)
                     {
                        m_dwFileRqId = pRequest->GetId();
                        m_dwUploadCommand = CMD_INSTALL_PACKAGE;
                        m_dwUploadData = CreateUniqueId(IDG_PACKAGE);
                        msg.SetVariable(VID_RCC, RCC_SUCCESS);
                        msg.SetVariable(VID_PACKAGE_ID, m_dwUploadData);

                        // Create record in database
                        pszEscDescr = EncodeSQLString(szDescription);
                        _sntprintf(szQuery, 2048, _T("INSERT INTO agent_pkg (pkg_id,pkg_name,"
                                                     _T("version,description,platform,pkg_file) ")
                                                     _T("VALUES (%d,'%s','%s','%s','%s','%s')")),
                                   m_dwUploadData, szPkgName, szPkgVersion, pszEscDescr,
                                   szPlatform, pszCleanFileName);
                        free(pszEscDescr);
                        DBQuery(g_hCoreDB, szQuery);
                     }
                     else
                     {
                        msg.SetVariable(VID_RCC, RCC_IO_ERROR);
                     }
                  }
                  else
                  {
                     msg.SetVariable(VID_RCC, RCC_RESOURCE_BUSY);
                  }
               }
               else
               {
                  msg.SetVariable(VID_RCC, RCC_PACKAGE_FILE_EXIST);
               }
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_DUPLICATE_PACKAGE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
      }
   }
   else
   {
      // Current user has no rights for package management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Remove package from server
//

void ClientSession::RemovePackage(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwPkgId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_PACKAGES)
   {
      if (m_dwFlags & CSF_PACKAGE_DB_LOCKED)
      {
         dwPkgId = pRequest->GetVariableLong(VID_PACKAGE_ID);
         msg.SetVariable(VID_RCC, UninstallPackage(dwPkgId));
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
      }
   }
   else
   {
      // Current user has no rights for package management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send list of parameters supported by given node to client
//

void ClientSession::SendParametersList(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      switch(pObject->Type())
      {
         case OBJECT_NODE:
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            ((Node *)pObject)->WriteParamListToMessage(&msg);
            break;
         case OBJECT_CLUSTER:
         case OBJECT_TEMPLATE:
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            WriteFullParamListToMessage(&msg);
            break;
         default:
            msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
            break;
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Deplay package to node(s)
//

void ClientSession::DeployPackage(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD i, j, dwNumObjects, *pdwObjectList, dwNumNodes, dwPkgId;
   Node **ppNodeList;
   NetObj *pObject;
   TCHAR szQuery[256], szPkgFile[MAX_PATH];
   TCHAR szVersion[MAX_AGENT_VERSION_LEN], szPlatform[MAX_PLATFORM_NAME_LEN];
   DB_RESULT hResult;
   BOOL bSuccess = TRUE;
   MUTEX hMutex;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_PACKAGES)
   {
      dwNumNodes = 0;
      ppNodeList = NULL;

      // Get package ID
      dwPkgId = pRequest->GetVariableLong(VID_PACKAGE_ID);
      if (IsValidPackageId(dwPkgId))
      {
         // Read package information
         _sntprintf(szQuery, 256, _T("SELECT platform,pkg_file,version FROM agent_pkg WHERE pkg_id=%d"), dwPkgId);
         hResult = DBSelect(g_hCoreDB, szQuery);
         if (hResult != NULL)
         {
            if (DBGetNumRows(hResult) > 0)
            {
               DBGetField(hResult, 0, 0, szPlatform, MAX_PLATFORM_NAME_LEN);
               DBGetField(hResult, 0, 1, szPkgFile, MAX_PATH);
               DBGetField(hResult, 0, 2, szVersion, MAX_AGENT_VERSION_LEN);

               // Create list of nodes to be upgraded
               dwNumObjects = pRequest->GetVariableLong(VID_NUM_OBJECTS);
               pdwObjectList = (DWORD *)malloc(sizeof(DWORD) * dwNumObjects);
               pRequest->GetVariableInt32Array(VID_OBJECT_LIST, dwNumObjects, pdwObjectList);
               for(i = 0; i < dwNumObjects; i++)
               {
                  pObject = FindObjectById(pdwObjectList[i]);
                  if (pObject != NULL)
                  {
                     if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
                     {
                        if (pObject->Type() == OBJECT_NODE)
                        {
                           // Check if this node already in the list
                           for(j = 0; j < dwNumNodes; j++)
                              if (ppNodeList[j]->Id() == pdwObjectList[i])
                                 break;
                           if (j == dwNumNodes)
                           {
                              pObject->IncRefCount();
                              ppNodeList = (Node **)realloc(ppNodeList, sizeof(Node *) * (dwNumNodes + 1));
                              ppNodeList[dwNumNodes] = (Node *)pObject;
                              dwNumNodes++;
                           }
                        }
                        else
                        {
                           pObject->AddChildNodesToList(&dwNumNodes, &ppNodeList, m_dwUserId);
                        }
                     }
                     else
                     {
                        msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
                        bSuccess = FALSE;
                        break;
                     }
                  }
                  else
                  {
                     msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
                     bSuccess = FALSE;
                     break;
                  }
               }
               safe_free(pdwObjectList);
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
               bSuccess = FALSE;
            }
            DBFreeResult(hResult);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
            bSuccess = FALSE;
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_PACKAGE_ID);
         bSuccess = FALSE;
      }

      // On success, start upgrade thread
      if (bSuccess)
      {
         DT_STARTUP_INFO *pInfo;

         hMutex = MutexCreate();
         MutexLock(hMutex, INFINITE);

         pInfo = (DT_STARTUP_INFO *)malloc(sizeof(DT_STARTUP_INFO));
         pInfo->dwNumNodes = dwNumNodes;
         pInfo->ppNodeList = ppNodeList;
         pInfo->pSession = this;
         pInfo->mutex = hMutex;
         pInfo->dwRqId = pRequest->GetId();
         pInfo->dwPackageId = dwPkgId;
         _tcscpy(pInfo->szPkgFile, szPkgFile);
         _tcscpy(pInfo->szPlatform, szPlatform);
         _tcscpy(pInfo->szVersion, szVersion);

         m_dwRefCount++;
         ThreadCreate(DeploymentManager, 0, pInfo);
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
      else
      {
         for(i = 0; i < dwNumNodes; i++)
            ppNodeList[i]->DecRefCount();
         safe_free(ppNodeList);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      bSuccess = FALSE;
   }

   // Send response
   sendMessage(&msg);

   // Allow deployment thread to run
   if (bSuccess)
      MutexUnlock(hMutex);
}


//
// Apply data collection template to node
//

void ClientSession::ApplyTemplate(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pSource, *pDestination;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get source and destination
   pSource = FindObjectById(pRequest->GetVariableLong(VID_SOURCE_OBJECT_ID));
   pDestination = FindObjectById(pRequest->GetVariableLong(VID_DESTINATION_OBJECT_ID));
   if ((pSource != NULL) && (pDestination != NULL))
   {
      // Check object types
      if ((pSource->Type() == OBJECT_TEMPLATE) && 
          (pDestination->Type() == OBJECT_NODE))
      {
         TCHAR szLockInfo[MAX_SESSION_NAME];
         BOOL bLockSucceed = FALSE;

         // Acquire DCI lock if needed
         if (!((Template *)pSource)->IsLockedBySession(m_dwIndex))
         {
            bLockSucceed = ((Template *)pSource)->LockDCIList(m_dwIndex, m_szUserName, szLockInfo);
         }
         else
         {
            bLockSucceed = TRUE;
         }

         if (bLockSucceed)
         {
            // Check access rights
            if ((pSource->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ)) &&
                (pDestination->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY)))
            {
               // Attempt to lock destination's DCI list
               if (((Node *)pDestination)->LockDCIList(m_dwIndex, m_szUserName, szLockInfo))
               {
                  BOOL bErrors;

                  bErrors = ((Template *)pSource)->ApplyToNode((Node *)pDestination);
                  ((Template *)pDestination)->UnlockDCIList(m_dwIndex);
                  msg.SetVariable(VID_RCC, bErrors ? RCC_DCI_COPY_ERRORS : RCC_SUCCESS);
               }
               else  // Destination's DCI list already locked by someone else
               {
                  msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
                  msg.SetVariable(VID_LOCKED_BY, szLockInfo);
               }
            }
            else  // User doesn't have enough rights on object(s)
            {
               msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
            }

            ((Template *)pSource)->UnlockDCIList(m_dwIndex);
         }
         else  // Source node DCI list not locked by this session
         {
            msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
            msg.SetVariable(VID_LOCKED_BY, szLockInfo);
         }
      }
      else     // Object(s) is not a node
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
      }
   }
   else  // No object(s) with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Get user variable
//

void ClientSession::GetUserVariable(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szVarName[MAX_VARIABLE_NAME], szQuery[MAX_VARIABLE_NAME + 256];
   DB_RESULT hResult;
   DWORD dwUserId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwUserId = pRequest->IsVariableExist(VID_USER_ID) ? pRequest->GetVariableLong(VID_USER_ID) : m_dwUserId;
   if ((dwUserId == m_dwUserId) || (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS))
   {
      // Try to read variable from database
      pRequest->GetVariableStr(VID_NAME, szVarName, MAX_VARIABLE_NAME);
      _sntprintf(szQuery, MAX_VARIABLE_NAME + 256,
                 _T("SELECT var_value FROM user_profiles WHERE user_id=%d AND var_name='%s'"),
                 dwUserId, szVarName);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) > 0)
         {
            TCHAR *pszData;

            pszData = DBGetField(hResult, 0, 0, NULL, 0);
            DecodeSQLString(pszData);
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            msg.SetVariable(VID_VALUE, pszData);
            free(pszData);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_VARIABLE_NOT_FOUND);
         }
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Set user variable
//

void ClientSession::SetUserVariable(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szVarName[MAX_VARIABLE_NAME], szQuery[8192], *pszValue, *pszRawValue;
   DB_RESULT hResult;
   BOOL bExist = FALSE;
   DWORD dwUserId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwUserId = pRequest->IsVariableExist(VID_USER_ID) ? pRequest->GetVariableLong(VID_USER_ID) : m_dwUserId;
   if ((dwUserId == m_dwUserId) || (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS))
   {
      // Check variable name
      pRequest->GetVariableStr(VID_NAME, szVarName, MAX_VARIABLE_NAME);
      if (IsValidObjectName(szVarName))
      {
         // Check if variable already exist in database
         _sntprintf(szQuery, MAX_VARIABLE_NAME + 256,
                    _T("SELECT var_name FROM user_profiles WHERE user_id=%d AND var_name='%s'"),
                    dwUserId, szVarName);
         hResult = DBSelect(g_hCoreDB, szQuery);
         if (hResult != NULL)
         {
            if (DBGetNumRows(hResult) > 0)
            {
               bExist = TRUE;
            }
            DBFreeResult(hResult);
         }

         // Update value in database
         pszRawValue = pRequest->GetVariableStr(VID_VALUE);
         pszValue = EncodeSQLString(pszRawValue);
         free(pszRawValue);
         if (bExist)
            _sntprintf(szQuery, 8192,
                       _T("UPDATE user_profiles SET var_value='%s' WHERE user_id=%d AND var_name='%s'"),
                       pszValue, dwUserId, szVarName);
         else
            _sntprintf(szQuery, 8192,
                       _T("INSERT INTO user_profiles (user_id,var_name,var_value) VALUES (%d,'%s','%s')"),
                       dwUserId, szVarName, pszValue);
         free(pszValue);
         if (DBQuery(g_hCoreDB, szQuery))
         {
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Enum user variables
//

void ClientSession::EnumUserVariables(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szPattern[MAX_VARIABLE_NAME], szQuery[256], szName[MAX_DB_STRING];
   DWORD i, dwNumRows, dwNumVars, dwId, dwUserId;
   DB_RESULT hResult;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwUserId = pRequest->IsVariableExist(VID_USER_ID) ? pRequest->GetVariableLong(VID_USER_ID) : m_dwUserId;
   if ((dwUserId == m_dwUserId) || (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS))
   {
      pRequest->GetVariableStr(VID_SEARCH_PATTERN, szPattern, MAX_VARIABLE_NAME);
      _sntprintf(szQuery, 256, _T("SELECT var_name FROM user_profiles WHERE user_id=%d"), dwUserId);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         dwNumRows = DBGetNumRows(hResult);
         for(i = 0, dwNumVars = 0, dwId = VID_VARLIST_BASE; i < dwNumRows; i++)
         {
            DBGetField(hResult, i, 0, szName, MAX_DB_STRING);
            if (MatchString(szPattern, szName, FALSE))
            {
               dwNumVars++;
               msg.SetVariable(dwId++, szName);
            }
         }
         msg.SetVariable(VID_NUM_VARIABLES, dwNumVars);
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Delete user variable(s)
//

void ClientSession::DeleteUserVariable(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szVarName[MAX_VARIABLE_NAME], szQuery[MAX_VARIABLE_NAME + 256];
   DWORD dwUserId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwUserId = pRequest->IsVariableExist(VID_USER_ID) ? pRequest->GetVariableLong(VID_USER_ID) : m_dwUserId;
   if ((dwUserId == m_dwUserId) || (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS))
   {
      // Try to delete variable from database
      pRequest->GetVariableStr(VID_NAME, szVarName, MAX_VARIABLE_NAME);
      TranslateStr(szVarName, _T("*"), _T("%"));
      _sntprintf(szQuery, MAX_VARIABLE_NAME + 256,
                 _T("DELETE FROM user_profiles WHERE user_id=%d AND var_name LIKE '%s'"),
                 dwUserId, szVarName);
      if (DBQuery(g_hCoreDB, szQuery))
      {
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Copy or move user variable(s) to another user
//

void ClientSession::CopyUserVariable(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szVarName[MAX_VARIABLE_NAME], szCurrVar[MAX_VARIABLE_NAME],
         szQuery[32768], *pszValue;
   DWORD dwSrcUserId, dwDstUserId;
   int i, nRows;
   BOOL bMove, bExist;
   DB_RESULT hResult, hResult2;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS)
   {
      dwSrcUserId = pRequest->IsVariableExist(VID_USER_ID) ? pRequest->GetVariableLong(VID_USER_ID) : m_dwUserId;
      dwDstUserId = pRequest->GetVariableLong(VID_DST_USER_ID);
      bMove = (BOOL)pRequest->GetVariableShort(VID_MOVE_FLAG);
      pRequest->GetVariableStr(VID_NAME, szVarName, MAX_VARIABLE_NAME);
      TranslateStr(szVarName, _T("*"), _T("%"));
      _sntprintf(szQuery, 8192,
                 _T("SELECT var_name,var_value FROM user_profiles WHERE user_id=%d AND var_name LIKE '%s'"),
                 dwSrcUserId, szVarName);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         nRows = DBGetNumRows(hResult);
         for(i = 0; i < nRows; i++)
         {
            DBGetField(hResult, i, 0, szCurrVar, MAX_VARIABLE_NAME);

            // Check if variable already exist in database
            _sntprintf(szQuery, 32768,
                       _T("SELECT var_name FROM user_profiles WHERE user_id=%d AND var_name='%s'"),
                       dwDstUserId, szCurrVar);
            hResult2 = DBSelect(g_hCoreDB, szQuery);
            if (hResult2 != NULL)
            {
               bExist = (DBGetNumRows(hResult2) > 0);
               DBFreeResult(hResult2);
            }
            else
            {
               bExist = FALSE;
            }
            
            pszValue = DBGetField(hResult, i, 1, NULL, 0);
            if (bExist)
               _sntprintf(szQuery, 32768,
                          _T("UPDATE user_profiles SET var_value='%s' WHERE user_id=%d AND var_name='%s'"),
                          pszValue, dwDstUserId, szCurrVar);
            else
               _sntprintf(szQuery, 32768,
                          _T("INSERT INTO user_profiles (user_id,var_name,var_value) VALUES (%d,'%s','%s')"),
                          dwDstUserId, szCurrVar, pszValue);
            free(pszValue);
            DBQuery(g_hCoreDB, szQuery);

            if (bMove)
            {
               _sntprintf(szQuery, 32768,
                          _T("DELETE FROM user_profiles WHERE user_id=%d AND var_name='%s'"),
                          dwSrcUserId, szCurrVar);
               DBQuery(g_hCoreDB, szQuery);
            }
         }
         DBFreeResult(hResult);
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Change object's IP address
//

void ClientSession::ChangeObjectIP(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD dwIpAddr;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get object id and check prerequisites
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
         {
            dwIpAddr = pRequest->GetVariableLong(VID_IP_ADDRESS);

            // Check if we already have object with given IP
            if ((FindNodeByIP(dwIpAddr) == NULL) &&
                (FindSubnetByIP(dwIpAddr) == NULL))
            {
               ((Node *)pObject)->changeIPAddress(dwIpAddr);
               msg.SetVariable(VID_RCC, RCC_SUCCESS);
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_ADDRESS_IN_USE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Setup encryption with client
//

void ClientSession::setupEncryption(DWORD dwRqId)
{
   CSCPMessage msg;

#ifdef _WITH_ENCRYPTION
   m_dwEncryptionRqId = dwRqId;
   m_dwEncryptionResult = RCC_TIMEOUT;
   if (m_condEncryptionSetup == INVALID_CONDITION_HANDLE)
      m_condEncryptionSetup = ConditionCreate(FALSE);

   // Send request for session key
   PrepareKeyRequestMsg(&msg, g_pServerKey);
   msg.SetId(dwRqId);
   sendMessage(&msg);
   msg.DeleteAllVariables();

   // Wait for encryption setup
   ConditionWait(m_condEncryptionSetup, 3000);

   // Send response
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_RCC, m_dwEncryptionResult);
#else    /* _WITH_ENCRYPTION not defined */
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_RCC, RCC_NO_ENCRYPTION_SUPPORT);
#endif

   sendMessage(&msg);
}


//
// Get agent's configuration file
//

void ClientSession::GetAgentConfig(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD dwResult, dwSize;
   TCHAR *pszConfig;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get object id and check prerequisites
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
         {
            AgentConnection *pConn;

            pConn = ((Node *)pObject)->createAgentConnection();
            if (pConn != NULL)
            {
               dwResult = pConn->GetConfigFile(&pszConfig, &dwSize);
               delete pConn;
               switch(dwResult)
               {
                  case ERR_SUCCESS:
                     msg.SetVariable(VID_RCC, RCC_SUCCESS);
                     msg.SetVariable(VID_CONFIG_FILE, pszConfig);
                     free(pszConfig);
                     break;
                  case ERR_ACCESS_DENIED:
                     msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
                     break;
                  default:
                     msg.SetVariable(VID_RCC, RCC_COMM_FAILURE);
                     break;
               }
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_COMM_FAILURE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Update agent's configuration file
//

void ClientSession::UpdateAgentConfig(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD dwResult;
   TCHAR *pszConfig;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get object id and check prerequisites
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_CONTROL))
         {
            AgentConnection *pConn;

            pConn = ((Node *)pObject)->createAgentConnection();
            if (pConn != NULL)
            {
               pszConfig = pRequest->GetVariableStr(VID_CONFIG_FILE);
               dwResult = pConn->updateConfigFile(pszConfig);
               free(pszConfig);

               if ((pRequest->GetVariableShort(VID_APPLY_FLAG) != 0) &&
                   (dwResult == ERR_SUCCESS))
               {
                  dwResult = pConn->ExecAction(_T("Agent.Restart"), 0, NULL);
               }

               switch(dwResult)
               {
                  case ERR_SUCCESS:
                     msg.SetVariable(VID_RCC, RCC_SUCCESS);
                     break;
                  case ERR_ACCESS_DENIED:
                     msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
                     break;
                  case ERR_IO_FAILURE:
                     msg.SetVariable(VID_RCC, RCC_IO_ERROR);
                     break;
                  case ERR_MALFORMED_COMMAND:
                     msg.SetVariable(VID_RCC, RCC_INTERNAL_ERROR);
                     break;
                  default:
                     msg.SetVariable(VID_RCC, RCC_COMM_FAILURE);
                     break;
               }
               delete pConn;
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_COMM_FAILURE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Execute action on client
//

void ClientSession::executeAction(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD dwResult;
   TCHAR szAction[MAX_PARAM_NAME];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get object id and check prerequisites
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_CONTROL))
         {
            AgentConnection *pConn;

            pConn = ((Node *)pObject)->createAgentConnection();
            if (pConn != NULL)
            {
               pRequest->GetVariableStr(VID_ACTION_NAME, szAction, MAX_PARAM_NAME);
               dwResult = pConn->ExecAction(szAction, 0, NULL);

               switch(dwResult)
               {
                  case ERR_SUCCESS:
                     msg.SetVariable(VID_RCC, RCC_SUCCESS);
                     break;
                  case ERR_ACCESS_DENIED:
                     msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
                     break;
                  case ERR_IO_FAILURE:
                     msg.SetVariable(VID_RCC, RCC_IO_ERROR);
                     break;
                  case ERR_EXEC_FAILED:
                     msg.SetVariable(VID_RCC, RCC_EXEC_FAILED);
                     break;
                  default:
                     msg.SetVariable(VID_RCC, RCC_COMM_FAILURE);
                     break;
               }
               delete pConn;
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_COMM_FAILURE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send tool list to client
//

void ClientSession::sendObjectTools(DWORD dwRqId)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD i, j, dwAclSize, dwNumTools, dwNumMsgRec, dwToolId, dwId;
   OBJECT_TOOL_ACL *pAccessList;
   TCHAR *pszStr, szBuffer[MAX_DB_STRING];

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   hResult = DBSelect(g_hCoreDB, _T("SELECT tool_id,user_id FROM object_tools_acl"));
   if (hResult != NULL)
   {
      dwAclSize = DBGetNumRows(hResult);
      pAccessList = (OBJECT_TOOL_ACL *)malloc(sizeof(OBJECT_TOOL_ACL) * dwAclSize);
      for(i = 0; i < dwAclSize; i++)
      {
         pAccessList[i].dwToolId = DBGetFieldULong(hResult, i, 0);
         pAccessList[i].dwUserId = DBGetFieldULong(hResult, i, 1);
      }
      DBFreeResult(hResult);

      hResult = DBSelect(g_hCoreDB, _T("SELECT tool_id,tool_name,tool_type,tool_data,flags,description,matching_oid,confirmation_text FROM object_tools"));
      if (hResult != NULL)
      {
         dwNumTools = DBGetNumRows(hResult);
         for(i = 0, dwId = VID_OBJECT_TOOLS_BASE, dwNumMsgRec = 0; i < dwNumTools; i++)
         {
            dwToolId = DBGetFieldULong(hResult, i, 0);
            if ((m_dwUserId != 0) && (!(m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_TOOLS)))
            {
               for(j = 0; j < dwAclSize; j++)
               {
                  if (pAccessList[j].dwToolId == dwToolId)
                  {
                     if ((pAccessList[j].dwUserId == m_dwUserId) ||
                         (pAccessList[j].dwUserId == GROUP_EVERYONE))
                        break;
                     if (pAccessList[j].dwUserId & GROUP_FLAG)
                        if (CheckUserMembership(m_dwUserId, pAccessList[j].dwUserId))
                           break;
                  }
               }
            }

            if ((m_dwUserId == 0) ||
                (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_TOOLS) ||
                (j < dwAclSize))   // User has access to this tool
            {
               msg.SetVariable(dwId, dwToolId);

               // name
               DBGetField(hResult, i, 1, szBuffer, MAX_DB_STRING);
               DecodeSQLStringAndSetVariable(&msg, dwId + 1, szBuffer);

               msg.SetVariable(dwId + 2, (WORD)DBGetFieldLong(hResult, i, 2));

               // data
               pszStr = DBGetField(hResult, i, 3, NULL, 0);
               DecodeSQLStringAndSetVariable(&msg, dwId + 3, pszStr);
               free(pszStr);

               msg.SetVariable(dwId + 4, DBGetFieldULong(hResult, i, 4));

               // description
               DBGetField(hResult, i, 5, szBuffer, MAX_DB_STRING);
               DecodeSQLStringAndSetVariable(&msg, dwId + 5, szBuffer);

               // matching OID
               DBGetField(hResult, i, 6, szBuffer, MAX_DB_STRING);
               DecodeSQLStringAndSetVariable(&msg, dwId + 6, szBuffer);

               // confirmation text
               DBGetField(hResult, i, 7, szBuffer, MAX_DB_STRING);
               DecodeSQLStringAndSetVariable(&msg, dwId + 7, szBuffer);

               dwNumMsgRec++;
               dwId += 10;
            }
         }
         msg.SetVariable(VID_NUM_TOOLS, dwNumMsgRec);

         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }

      free(pAccessList);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send tool list to client
//

void ClientSession::sendObjectToolDetails(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD dwToolId, dwId, *pdwAcl;
   TCHAR *pszStr, szQuery[1024], szBuffer[MAX_DB_STRING];
   int i, iNumRows, nType;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_TOOLS)
   {
      dwToolId = pRequest->GetVariableLong(VID_TOOL_ID);
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT tool_name,tool_type,tool_data,description,flags,matching_oid,confirmation_text FROM object_tools WHERE tool_id=%d"), dwToolId);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) > 0)
         {
				msg.SetVariable(VID_TOOL_ID, dwToolId);

            DBGetField(hResult, 0, 0, szBuffer, MAX_DB_STRING);
            DecodeSQLStringAndSetVariable(&msg, VID_NAME, szBuffer);

            nType = DBGetFieldLong(hResult, 0, 1);
            msg.SetVariable(VID_TOOL_TYPE, (WORD)nType);

            pszStr = DBGetField(hResult, 0, 2, NULL, 0);
            DecodeSQLStringAndSetVariable(&msg, VID_TOOL_DATA, pszStr);
            free(pszStr);

            DBGetField(hResult, 0, 3, szBuffer, MAX_DB_STRING);
            DecodeSQLStringAndSetVariable(&msg, VID_DESCRIPTION, szBuffer);

            msg.SetVariable(VID_FLAGS, DBGetFieldULong(hResult, 0, 4));

            DBGetField(hResult, 0, 5, szBuffer, MAX_DB_STRING);
            DecodeSQLStringAndSetVariable(&msg, VID_TOOL_OID, szBuffer);

            DBGetField(hResult, 0, 6, szBuffer, MAX_DB_STRING);
            DecodeSQLStringAndSetVariable(&msg, VID_CONFIRMATION_TEXT, szBuffer);

            DBFreeResult(hResult);

            // Access list
            _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT user_id FROM object_tools_acl WHERE tool_id=%d"), dwToolId);
            hResult = DBSelect(g_hCoreDB, szQuery);
            if (hResult != NULL)
            {
               iNumRows = DBGetNumRows(hResult);
               msg.SetVariable(VID_ACL_SIZE, (DWORD)iNumRows);
               if (iNumRows > 0)
               {
                  pdwAcl = (DWORD *)malloc(sizeof(DWORD) * iNumRows);
                  for(i = 0; i < iNumRows; i++)
                     pdwAcl[i] = DBGetFieldULong(hResult, i, 0);
                  msg.SetVariableToInt32Array(VID_ACL, iNumRows, pdwAcl);
                  free(pdwAcl);
               }
               DBFreeResult(hResult);

               // Column information for table tools
               if ((nType == TOOL_TYPE_TABLE_SNMP) || (nType == TOOL_TYPE_TABLE_AGENT))
               {
                  _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT col_name,col_oid,col_format,col_substr ")
                                     _T("FROM object_tools_table_columns WHERE tool_id=%d ")
                                     _T("ORDER BY col_number"), dwToolId);
                  hResult = DBSelect(g_hCoreDB, szQuery);
                  if (hResult != NULL)
                  {
                     iNumRows = DBGetNumRows(hResult);
                     msg.SetVariable(VID_NUM_COLUMNS, (WORD)iNumRows);
                     for(i = 0, dwId = VID_COLUMN_INFO_BASE; i < iNumRows; i++)
                     {
                        DBGetField(hResult, i, 0, szBuffer, MAX_DB_STRING);
                        DecodeSQLStringAndSetVariable(&msg, dwId++, szBuffer);
                        msg.SetVariable(dwId++, DBGetField(hResult, i, 1, szBuffer, MAX_DB_STRING));
                        msg.SetVariable(dwId++, (WORD)DBGetFieldLong(hResult, i, 2));
                        msg.SetVariable(dwId++, (WORD)DBGetFieldLong(hResult, i, 3));
                     }
                     DBFreeResult(hResult);
                     msg.SetVariable(VID_RCC, RCC_SUCCESS);
                  }
                  else
                  {
                     msg.DeleteAllVariables();
                     msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
                  }
               }
            }
            else
            {
               msg.DeleteAllVariables();
               msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
            }
         }
         else
         {
            DBFreeResult(hResult);
            msg.SetVariable(VID_RCC, RCC_INVALID_TOOL_ID);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      // Current user has no rights for object tools management
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Update object tool
//

void ClientSession::updateObjectTool(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_TOOLS)
   {
      msg.SetVariable(VID_RCC, UpdateObjectToolFromMessage(pRequest));
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Delete object tool
//

void ClientSession::deleteObjectTool(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwToolId;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check user rights
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_TOOLS)
   {
      dwToolId = pRequest->GetVariableLong(VID_TOOL_ID);
      msg.SetVariable(VID_RCC, DeleteObjectToolFromDB(dwToolId));
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Generate ID for new object tool
//

void ClientSession::generateObjectToolId(DWORD dwRqId)
{
   CSCPMessage msg;

   // Prepare reply message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   // Check access rights
   if (checkSysAccessRights(SYSTEM_ACCESS_MANAGE_TOOLS))
   {
      msg.SetVariable(VID_TOOL_ID, CreateUniqueId(IDG_OBJECT_TOOL));
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   // Send response
   sendMessage(&msg);
}


//
// Execute table tool (either SNMP or agent table)
//

void ClientSession::execTableTool(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwToolId;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Check if tool exist and has correct type
   dwToolId = pRequest->GetVariableLong(VID_TOOL_ID);
   if (IsTableTool(dwToolId))
   {
      // Check access
      if (CheckObjectToolAccess(dwToolId, m_dwUserId))
      {
         pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
         if (pObject != NULL)
         {
            if (pObject->Type() == OBJECT_NODE)
            {
               msg.SetVariable(VID_RCC,
                               ExecuteTableTool(dwToolId, (Node *)pObject,
                                                pRequest->GetId(), this));
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_TOOL_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Change current subscription
//

void ClientSession::ChangeSubscription(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwFlags;

   dwFlags = pRequest->GetVariableLong(VID_FLAGS);
   if (pRequest->GetVariableShort(VID_OPERATION) != 0)
   {
      m_dwActiveChannels |= dwFlags;   // Subscribe
   }
   else
   {
      m_dwActiveChannels &= ~dwFlags;   // Unsubscribe
   }

   // Send response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());
   msg.SetVariable(VID_RCC, RCC_SUCCESS);
   sendMessage(&msg);
}


//
// Send server statistics
//

void ClientSession::SendServerStats(DWORD dwRqId)
{
   CSCPMessage msg;
   DWORD i, dwNumItems;
#ifdef _WIN32
   PROCESS_MEMORY_COUNTERS mc;
#endif

   // Prepare response
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_RCC, RCC_SUCCESS);

   // Server version, etc.
   msg.SetVariable(VID_SERVER_VERSION, NETXMS_VERSION_STRING);
   msg.SetVariable(VID_SERVER_UPTIME, (DWORD)(time(NULL) - g_tServerStartTime));

   // Number of objects and DCIs
   RWLockReadLock(g_rwlockNodeIndex, INFINITE);
   for(i = 0, dwNumItems = 0; i < g_dwNodeAddrIndexSize; i++)
      dwNumItems += ((Node *)g_pNodeIndexByAddr[i].pObject)->getItemCount();
   RWLockUnlock(g_rwlockNodeIndex);
   msg.SetVariable(VID_NUM_ITEMS, dwNumItems);
   msg.SetVariable(VID_NUM_OBJECTS, g_dwIdIndexSize);
   msg.SetVariable(VID_NUM_NODES, g_dwNodeAddrIndexSize);

   // Client sessions
   msg.SetVariable(VID_NUM_SESSIONS, (DWORD)GetSessionCount());

   // Alarms
   g_alarmMgr.GetAlarmStats(&msg);

   // Process info
#ifdef _WIN32
   mc.cb = sizeof(PROCESS_MEMORY_COUNTERS);
   if (GetProcessMemoryInfo(GetCurrentProcess(), &mc, sizeof(PROCESS_MEMORY_COUNTERS)))
   {
      msg.SetVariable(VID_NETXMSD_PROCESS_WKSET, (DWORD)(mc.WorkingSetSize / 1024));
      msg.SetVariable(VID_NETXMSD_PROCESS_VMSIZE, (DWORD)(mc.PagefileUsage / 1024));
   }
#endif

	// Queues
	msg.SetVariable(VID_QSIZE_CONDITION_POLLER, g_conditionPollerQueue.Size());
	msg.SetVariable(VID_QSIZE_CONF_POLLER, g_configPollQueue.Size());
	msg.SetVariable(VID_QSIZE_DCI_POLLER, g_pItemQueue->Size());
	msg.SetVariable(VID_QSIZE_DBWRITER, g_pLazyRequestQueue->Size());
	msg.SetVariable(VID_QSIZE_EVENT, g_pEventQueue->Size());
	msg.SetVariable(VID_QSIZE_DISCOVERY, g_discoveryPollQueue.Size());
	msg.SetVariable(VID_QSIZE_NODE_POLLER, g_nodePollerQueue.Size());
	msg.SetVariable(VID_QSIZE_ROUTE_POLLER, g_routePollQueue.Size());
	msg.SetVariable(VID_QSIZE_STATUS_POLLER, g_statusPollQueue.Size());

   // Send response
   sendMessage(&msg);
}


//
// Send script list
//

void ClientSession::sendScriptList(DWORD dwRqId)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD i, dwNumScripts, dwId;
   TCHAR szBuffer[MAX_DB_STRING];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SCRIPTS)
   {
      hResult = DBSelect(g_hCoreDB, _T("SELECT script_id,script_name FROM script_library"));
      if (hResult != NULL)
      {
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         dwNumScripts = DBGetNumRows(hResult);
         msg.SetVariable(VID_NUM_SCRIPTS, dwNumScripts);
         for(i = 0, dwId = VID_SCRIPT_LIST_BASE; i < dwNumScripts; i++)
         {
            msg.SetVariable(dwId++, DBGetFieldULong(hResult, i, 0));
            msg.SetVariable(dwId++, DBGetField(hResult, i, 1, szBuffer, MAX_DB_STRING));
         }
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Send script
//

void ClientSession::sendScript(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD dwScriptId;
   TCHAR *pszCode, szQuery[256];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SCRIPTS)
   {
      dwScriptId = pRequest->GetVariableLong(VID_SCRIPT_ID);
      _sntprintf(szQuery, 256, _T("SELECT script_name,script_code FROM script_library WHERE script_id=%d"), dwScriptId);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) > 0)
         {
				TCHAR name[MAX_DB_STRING];

            msg.SetVariable(VID_NAME, DBGetField(hResult, 0, 0, name, MAX_DB_STRING));
            
				pszCode = DBGetField(hResult, 0, 1, NULL, 0);
            msg.SetVariable(VID_SCRIPT_CODE, pszCode);
            free(pszCode);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_SCRIPT_ID);
         }
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Update script
//

void ClientSession::updateScript(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR *pszCode, *pszQuery, szName[MAX_DB_STRING];
   DWORD dwScriptId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SCRIPTS)
   {
      dwScriptId = pRequest->GetVariableLong(VID_SCRIPT_ID);
      pRequest->GetVariableStr(VID_NAME, szName, MAX_DB_STRING);
      if (IsValidScriptName(szName))
      {
         pszCode = pRequest->GetVariableStr(VID_SCRIPT_CODE);
         if (pszCode != NULL)
         {
				size_t qlen = _tcslen(pszCode) + MAX_DB_STRING + 256;
            pszQuery = (TCHAR *)malloc(qlen * sizeof(TCHAR));
            if (dwScriptId == 0)
            {
               // New script
               dwScriptId = CreateUniqueId(IDG_SCRIPT);
               _sntprintf(pszQuery, qlen, _T("INSERT INTO script_library (script_id,script_name,script_code) VALUES (%d,%s,%s)"),
                         dwScriptId, (const TCHAR *)DBPrepareString(g_hCoreDB, szName),
								 (const TCHAR *)DBPrepareString(g_hCoreDB, pszCode));
            }
            else
            {
               _sntprintf(pszQuery, qlen, _T("UPDATE script_library SET script_name=%s,script_code=%s WHERE script_id=%d"),
                         (const TCHAR *)DBPrepareString(g_hCoreDB, szName),
								 (const TCHAR *)DBPrepareString(g_hCoreDB, pszCode), dwScriptId);
            }
            if (DBQuery(g_hCoreDB, pszQuery))
            {
               ReloadScript(dwScriptId);
               msg.SetVariable(VID_RCC, RCC_SUCCESS);
               msg.SetVariable(VID_SCRIPT_ID, dwScriptId);
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
            }
            free(pszQuery);
            free(pszCode);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_REQUEST);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_SCRIPT_NAME);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Rename script
//

void ClientSession::renameScript(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szQuery[4096], szName[MAX_DB_STRING];
   DWORD dwScriptId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SCRIPTS)
   {
      dwScriptId = pRequest->GetVariableLong(VID_SCRIPT_ID);
      pRequest->GetVariableStr(VID_NAME, szName, MAX_DB_STRING);
      if (IsValidScriptName(szName))
      {
         if (IsValidScriptId(dwScriptId))
         {
            _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("UPDATE script_library SET script_name=%s WHERE script_id=%d"),
                      (const TCHAR *)DBPrepareString(g_hCoreDB, szName), dwScriptId);
            if (DBQuery(g_hCoreDB, szQuery))
            {
               ReloadScript(dwScriptId);
               msg.SetVariable(VID_RCC, RCC_SUCCESS);
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_SCRIPT_ID);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_SCRIPT_NAME);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Delete script
//

void ClientSession::deleteScript(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szQuery[256];
   DWORD dwScriptId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SCRIPTS)
   {
      dwScriptId = pRequest->GetVariableLong(VID_SCRIPT_ID);
      if (IsValidScriptId(dwScriptId))
      {
         _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM script_library WHERE script_id=%d"), dwScriptId);
         if (DBQuery(g_hCoreDB, szQuery))
         {
            g_pScriptLibrary->lock();
            g_pScriptLibrary->deleteScript(dwScriptId);
            g_pScriptLibrary->unlock();
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_SCRIPT_ID);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Copy session data to message
//

static void CopySessionData(ClientSession *pSession, void *pArg)
{
   DWORD dwId, dwIndex;

   dwIndex = ((CSCPMessage *)pArg)->GetVariableLong(VID_NUM_SESSIONS);
   ((CSCPMessage *)pArg)->SetVariable(VID_NUM_SESSIONS, dwIndex + 1);

   dwId = VID_SESSION_DATA_BASE + dwIndex * 100;
   ((CSCPMessage *)pArg)->SetVariable(dwId++, pSession->getIndex());
   ((CSCPMessage *)pArg)->SetVariable(dwId++, (WORD)pSession->getCipher());
   ((CSCPMessage *)pArg)->SetVariable(dwId++, (TCHAR *)pSession->getUserName());
   ((CSCPMessage *)pArg)->SetVariable(dwId++, (TCHAR *)pSession->getClientInfo());
}


//
// Send list of connected client sessions
//

void ClientSession::SendSessionList(DWORD dwRqId)
{
   CSCPMessage msg;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SESSIONS)
   {
      msg.SetVariable(VID_NUM_SESSIONS, (DWORD)0);
      EnumerateClientSessions(CopySessionData, &msg);
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Forcibly terminate client's session
//

void ClientSession::KillSession(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());
   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SESSIONS)
   {
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// Handler for new syslog messages
//

void ClientSession::onSyslogMessage(NX_SYSLOG_RECORD *pRec)
{
   UPDATE_INFO *pUpdate;

   if (isAuthenticated() && (m_dwActiveChannels & NXC_CHANNEL_SYSLOG))
   {
      pUpdate = (UPDATE_INFO *)malloc(sizeof(UPDATE_INFO));
      pUpdate->dwCategory = INFO_CAT_SYSLOG_MSG;
      pUpdate->pData = nx_memdup(pRec, sizeof(NX_SYSLOG_RECORD));
      m_pUpdateQueue->Put(pUpdate);
   }
}


//
// Get latest syslog records
//

void ClientSession::SendSyslog(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwMaxRecords, dwNumRows, dwId;
   DB_RESULT hTempResult;
   DB_ASYNC_RESULT hResult;
   TCHAR szQuery[1024], szBuffer[1024];
   WORD wRecOrder;

   wRecOrder = ((g_nDBSyntax == DB_SYNTAX_MSSQL) || (g_nDBSyntax == DB_SYNTAX_ORACLE)) ? RECORD_ORDER_REVERSED : RECORD_ORDER_NORMAL;
   dwMaxRecords = pRequest->GetVariableLong(VID_MAX_RECORDS);

   // Prepare confirmation message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   MutexLock(m_mutexSendSyslog, INFINITE);

   // Retrieve events from database
   switch(g_nDBSyntax)
   {
      case DB_SYNTAX_MYSQL:
      case DB_SYNTAX_PGSQL:
      case DB_SYNTAX_SQLITE:
         hTempResult = DBSelect(g_hCoreDB, _T("SELECT count(*) FROM syslog"));
         if (hTempResult != NULL)
         {
            if (DBGetNumRows(hTempResult) > 0)
            {
               dwNumRows = DBGetFieldULong(hTempResult, 0, 0);
            }
            else
            {
               dwNumRows = 0;
            }
            DBFreeResult(hTempResult);
         }
         _sntprintf(szQuery, 1024,
                    _T("SELECT msg_id,msg_timestamp,facility,severity,")
                    _T("source_object_id,hostname,msg_tag,msg_text FROM syslog ")
                    _T("ORDER BY msg_id LIMIT %u OFFSET %u"),
                    dwMaxRecords, dwNumRows - min(dwNumRows, dwMaxRecords));
         break;
      case DB_SYNTAX_MSSQL:
         _sntprintf(szQuery, 1024,
                    _T("SELECT TOP %d msg_id,msg_timestamp,facility,severity,")
                    _T("source_object_id,hostname,msg_tag,msg_text FROM syslog ")
                    _T("ORDER BY msg_id DESC"), dwMaxRecords);
         break;
      case DB_SYNTAX_ORACLE:
         _sntprintf(szQuery, 1024,
                    _T("SELECT msg_id,msg_timestamp,facility,severity,")
                    _T("source_object_id,hostname,msg_tag,msg_text FROM syslog ")
                    _T("WHERE ROWNUM <= %u ORDER BY msg_id DESC"), dwMaxRecords);
         break;
      default:
         szQuery[0] = 0;
         break;
   }
   hResult = DBAsyncSelect(g_hCoreDB, szQuery);
   if (hResult != NULL)
   {
		msg.SetVariable(VID_RCC, RCC_SUCCESS);
		sendMessage(&msg);
		msg.DeleteAllVariables();
		msg.SetCode(CMD_SYSLOG_RECORDS);
		
      // Send records, up to 10 per message
      for(dwId = VID_SYSLOG_MSG_BASE, dwNumRows = 0; DBFetch(hResult); dwNumRows++)
      {
         if (dwNumRows == 10)
         {
            msg.SetVariable(VID_NUM_RECORDS, dwNumRows);
            msg.SetVariable(VID_RECORDS_ORDER, wRecOrder);
            sendMessage(&msg);
            msg.DeleteAllVariables();
            dwNumRows = 0;
            dwId = VID_SYSLOG_MSG_BASE;
         }
         msg.SetVariable(dwId++, DBGetFieldAsyncUInt64(hResult, 0));
         msg.SetVariable(dwId++, DBGetFieldAsyncULong(hResult, 1));
         msg.SetVariable(dwId++, (WORD)DBGetFieldAsyncLong(hResult, 2));
         msg.SetVariable(dwId++, (WORD)DBGetFieldAsyncLong(hResult, 3));
         msg.SetVariable(dwId++, DBGetFieldAsyncULong(hResult, 4));
         msg.SetVariable(dwId++, DBGetFieldAsync(hResult, 5, szBuffer, 1024));
         msg.SetVariable(dwId++, DBGetFieldAsync(hResult, 6, szBuffer, 1024));
         msg.SetVariable(dwId++, DBGetFieldAsync(hResult, 7, szBuffer, 1024));
      }
      DBFreeAsyncResult(g_hCoreDB, hResult);

      // Send remaining records with End-Of-Sequence notification
      msg.SetVariable(VID_NUM_RECORDS, dwNumRows);
      msg.SetVariable(VID_RECORDS_ORDER, wRecOrder);
      msg.SetEndOfSequence();
      sendMessage(&msg);
   }
   else
   {
		msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		sendMessage(&msg);
	}

   MutexUnlock(m_mutexSendSyslog);
}


//
// Handler for new traps
//

void ClientSession::onNewSNMPTrap(CSCPMessage *pMsg)
{
   UPDATE_INFO *pUpdate;

   if (isAuthenticated() && (m_dwActiveChannels & NXC_CHANNEL_SNMP_TRAPS))
   {
      pUpdate = (UPDATE_INFO *)malloc(sizeof(UPDATE_INFO));
      pUpdate->dwCategory = INFO_CAT_SNMP_TRAP;
      pUpdate->pData = new CSCPMessage(pMsg);
      m_pUpdateQueue->Put(pUpdate);
   }
}


//
// Send collected trap log
//

void ClientSession::SendTrapLog(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szBuffer[4096], szQuery[1024];
   DWORD dwId, dwNumRows, dwMaxRecords;
   DB_RESULT hTempResult;
   DB_ASYNC_RESULT hResult;
   WORD wRecOrder;

   wRecOrder = ((g_nDBSyntax == DB_SYNTAX_MSSQL) || (g_nDBSyntax == DB_SYNTAX_ORACLE)) ? RECORD_ORDER_REVERSED : RECORD_ORDER_NORMAL;
   dwMaxRecords = pRequest->GetVariableLong(VID_MAX_RECORDS);

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_VIEW_TRAP_LOG)
   {
      msg.SetVariable(VID_RCC, RCC_SUCCESS);
      sendMessage(&msg);
      msg.DeleteAllVariables();
      msg.SetCode(CMD_TRAP_LOG_RECORDS);

      MutexLock(m_mutexSendTrapLog, INFINITE);

      // Retrieve trap log records from database
      switch(g_nDBSyntax)
      {
         case DB_SYNTAX_MYSQL:
         case DB_SYNTAX_PGSQL:
         case DB_SYNTAX_SQLITE:
            hTempResult = DBSelect(g_hCoreDB, _T("SELECT count(*) FROM snmp_trap_log"));
            if (hTempResult != NULL)
            {
               if (DBGetNumRows(hTempResult) > 0)
               {
                  dwNumRows = DBGetFieldULong(hTempResult, 0, 0);
               }
               else
               {
                  dwNumRows = 0;
               }
               DBFreeResult(hTempResult);
            }
            _sntprintf(szQuery, 1024,
                       _T("SELECT trap_id,trap_timestamp,ip_addr,object_id,")
                       _T("trap_oid,trap_varlist FROM snmp_trap_log ")
                       _T("ORDER BY trap_id LIMIT %u OFFSET %u"),
                       dwMaxRecords, dwNumRows - min(dwNumRows, dwMaxRecords));
            break;
         case DB_SYNTAX_MSSQL:
            _sntprintf(szQuery, 1024,
                       _T("SELECT TOP %u trap_id,trap_timestamp,ip_addr,object_id,")
                       _T("trap_oid,trap_varlist FROM snmp_trap_log ")
                       _T("ORDER BY trap_id DESC"), dwMaxRecords);
            break;
         case DB_SYNTAX_ORACLE:
            _sntprintf(szQuery, 1024,
                       _T("SELECT trap_id,trap_timestamp,ip_addr,object_id,")
                       _T("trap_oid,trap_varlist FROM snmp_trap_log ")
                       _T("WHERE ROWNUM <= %u ORDER BY trap_id DESC"),
                       dwMaxRecords);
            break;
         default:
            szQuery[0] = 0;
            break;
      }
      hResult = DBAsyncSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         // Send events, one per message
         for(dwId = VID_TRAP_LOG_MSG_BASE, dwNumRows = 0; DBFetch(hResult); dwNumRows++)
         {
            if (dwNumRows == 10)
            {
               msg.SetVariable(VID_NUM_RECORDS, dwNumRows);
               msg.SetVariable(VID_RECORDS_ORDER, wRecOrder);
               sendMessage(&msg);
               msg.DeleteAllVariables();
               dwNumRows = 0;
               dwId = VID_TRAP_LOG_MSG_BASE;
            }
            msg.SetVariable(dwId++, DBGetFieldAsyncUInt64(hResult, 0));
            msg.SetVariable(dwId++, DBGetFieldAsyncULong(hResult, 1));
            msg.SetVariable(dwId++, DBGetFieldAsyncIPAddr(hResult, 2));
            msg.SetVariable(dwId++, DBGetFieldAsyncULong(hResult, 3));
            msg.SetVariable(dwId++, DBGetFieldAsync(hResult, 4, szBuffer, 256));
            msg.SetVariable(dwId++, DBGetFieldAsync(hResult, 5, szBuffer, 4096));
         }
         DBFreeAsyncResult(g_hCoreDB, hResult);

         // Send remaining records with End-Of-Sequence notification
         msg.SetVariable(VID_NUM_RECORDS, dwNumRows);
         msg.SetVariable(VID_RECORDS_ORDER, wRecOrder);
         msg.SetEndOfSequence();
      }

      MutexUnlock(m_mutexSendTrapLog);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }
   sendMessage(&msg);
}


//
// SNMP walker thread's startup parameters
//

typedef struct
{
   ClientSession *pSession;
   DWORD dwRqId;
   NetObj *pObject;
   TCHAR szBaseOID[MAX_OID_LEN * 4];
} WALKER_THREAD_ARGS;


//
// Arguments for SnmpEnumerate callback
//

typedef struct
{
   CSCPMessage *pMsg;
   DWORD dwId;
   DWORD dwNumVars;
   ClientSession *pSession;
} WALKER_ENUM_CALLBACK_ARGS;


//
// SNMP walker enumeration callback
//

static DWORD WalkerCallback(DWORD dwVersion, SNMP_Variable *pVar,
                            SNMP_Transport *pTransport, void *pArg)
{
   CSCPMessage *pMsg = ((WALKER_ENUM_CALLBACK_ARGS *)pArg)->pMsg;
   TCHAR szBuffer[4096];

   pMsg->SetVariable(((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwId++, (TCHAR *)pVar->GetName()->GetValueAsText());
   pMsg->SetVariable(((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwId++, pVar->GetType());
   pMsg->SetVariable(((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwId++, pVar->GetValueAsString(szBuffer, 4096));
   ((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwNumVars++;
   if (((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwNumVars == 50)
   {
      pMsg->SetVariable(VID_NUM_VARIABLES, ((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwNumVars);
      ((WALKER_ENUM_CALLBACK_ARGS *)pArg)->pSession->sendMessage(pMsg);
      ((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwNumVars = 0;
      ((WALKER_ENUM_CALLBACK_ARGS *)pArg)->dwId = VID_SNMP_WALKER_DATA_BASE;
      pMsg->DeleteAllVariables();
   }
   return SNMP_ERR_SUCCESS;
}


//
// SNMP walker thread
//

static THREAD_RESULT THREAD_CALL WalkerThread(void *pArg)
{
   CSCPMessage msg;
   WALKER_ENUM_CALLBACK_ARGS args;

   msg.SetCode(CMD_SNMP_WALK_DATA);
   msg.SetId(((WALKER_THREAD_ARGS *)pArg)->dwRqId);

   args.pMsg = &msg;
   args.dwId = VID_SNMP_WALKER_DATA_BASE;
   args.dwNumVars = 0;
   args.pSession = ((WALKER_THREAD_ARGS *)pArg)->pSession;
   ((Node *)(((WALKER_THREAD_ARGS *)pArg)->pObject))->CallSnmpEnumerate(
         ((WALKER_THREAD_ARGS *)pArg)->szBaseOID, WalkerCallback, &args);
   msg.SetVariable(VID_NUM_VARIABLES, args.dwNumVars);
   msg.SetEndOfSequence();
   ((WALKER_THREAD_ARGS *)pArg)->pSession->sendMessage(&msg);

   ((WALKER_THREAD_ARGS *)pArg)->pSession->decRefCount();
   ((WALKER_THREAD_ARGS *)pArg)->pObject->DecRefCount();
   free(pArg);
   return THREAD_OK;
}


//
// Start SNMP walk
//

void ClientSession::StartSnmpWalk(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
   WALKER_THREAD_ARGS *pArg;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
         {
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            
            pObject->IncRefCount();
            m_dwRefCount++;

            pArg = (WALKER_THREAD_ARGS *)malloc(sizeof(WALKER_THREAD_ARGS));
            pArg->pSession = this;
            pArg->pObject = pObject;
            pArg->dwRqId = pRequest->GetId();
            pRequest->GetVariableStr(VID_SNMP_OID, pArg->szBaseOID, MAX_OID_LEN * 4);
            
            ThreadCreate(WalkerThread, 0, pArg);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }
   sendMessage(&msg);
}


//
// Send map list
//

void ClientSession::SendMapList(DWORD dwRqId)
{
   CSCPMessage msg;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

	CreateMapListMessage(msg, m_dwUserId);

   sendMessage(&msg);
}


//
// Resolve map name to ID
//

void ClientSession::ResolveMapName(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szName[MAX_DB_STRING];
   DWORD dwMapId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   pRequest->GetVariableStr(VID_NAME, szName, MAX_DB_STRING);
   msg.SetVariable(VID_RCC, GetMapIdFromName(szName, &dwMapId));
   msg.SetVariable(VID_MAP_ID, dwMapId);

   sendMessage(&msg);
}


//
// Save map
//

void ClientSession::SaveMap(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwMapId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (!(m_dwFlags & CSF_RECEIVING_MAP_DATA))
   {
      dwMapId = pRequest->GetVariableLong(VID_MAP_ID);
      LockMaps();
      m_pActiveMap = FindMapByID(dwMapId);
      if (m_pActiveMap != NULL)
      {
         if (m_pActiveMap->CheckUserRights(m_dwUserId, MAP_ACCESS_WRITE))
         {
            m_pActiveMap->IncRefCount();
            m_pActiveMap->Lock();
            m_pActiveMap->ModifyFromMessage(pRequest);
            m_pActiveMap->Unlock();
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            m_dwFlags |= CSF_RECEIVING_MAP_DATA;
            m_dwMapSaveRqId = pRequest->GetId();
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_MAP_ID);
      }
      UnlockMaps();
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_OPERATION_IN_PROGRESS);
   }

   sendMessage(&msg);
}


//
// Process incoming submap data
//

void ClientSession::ProcessSubmapData(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwId, dwResult;
   nxSubmapSrv *pSubmap;
   BOOL bSend = FALSE;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if ((m_dwFlags & CSF_RECEIVING_MAP_DATA) &&
       (m_dwMapSaveRqId == pRequest->GetId()))
   {
      dwId = pRequest->GetVariableLong(VID_OBJECT_ID);
      pSubmap = (nxSubmapSrv *)m_pActiveMap->GetSubmap(dwId);
      if (pSubmap != NULL)
      {
         pSubmap->ModifyFromMessage(pRequest);
      }
      if (pRequest->IsEndOfSequence())
      {
         dwResult = m_pActiveMap->SaveToDB();
         m_pActiveMap->DecRefCount();
         m_dwFlags &= ~CSF_RECEIVING_MAP_DATA;
         msg.SetVariable(VID_RCC, dwResult);
         bSend = TRUE;
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_OUT_OF_STATE_REQUEST);
      bSend = TRUE;
   }

   if (bSend)
      sendMessage(&msg);
}


//
// Create map
//

void ClientSession::CreateMap(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szName[MAX_DB_STRING];
   DWORD dwMapId, dwResult, dwRootObj;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_MAPS)
	{
		pRequest->GetVariableStr(VID_NAME, szName, MAX_DB_STRING);
		if (IsValidObjectName(szName, TRUE))
		{
			dwRootObj = pRequest->GetVariableLong(VID_OBJECT_ID);
			dwResult = CreateNewMap(dwRootObj, szName, &dwMapId);
			msg.SetVariable(VID_RCC, dwResult);
			if (dwResult == RCC_SUCCESS)
				msg.SetVariable(VID_MAP_ID, dwMapId);
		}
		else
		{
	      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
		}
	}
	else
	{
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Rename map
//

void ClientSession::RenameMap(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwMapId;
   nxMapSrv *pMap;
	TCHAR szName[MAX_DB_STRING];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_MAPS)
	{
		dwMapId = pRequest->GetVariableLong(VID_MAP_ID);
		LockMaps();
		pMap = FindMapByID(dwMapId);
		if (pMap != NULL)
		{
			pRequest->GetVariableStr(VID_NAME, szName, MAX_DB_STRING);
			pMap->SetName(szName);
			pMap->SaveToDB();
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_MAP_ID);
		}
		UnlockMaps();
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Save map
//

void ClientSession::DeleteMap(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwMapId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_MAPS)
	{
		dwMapId = pRequest->GetVariableLong(VID_MAP_ID);
		DebugPrintf(4, _T("Requesting map deletion (mapId=%d)"), dwMapId);
		msg.SetVariable(VID_RCC, ::DeleteMap(dwMapId));
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Load map
//

void ClientSession::LoadMap(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD i, dwMapId;
   nxMapSrv *pMap;
   nxSubmap *pSubmap;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwMapId = pRequest->GetVariableLong(VID_MAP_ID);
   LockMaps();
   pMap = FindMapByID(dwMapId);
   if (pMap != NULL)
   {
      if (pMap->CheckUserRights(m_dwUserId, MAP_ACCESS_READ))
      {
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         pMap->CreateMessage(&msg);

         for(i = 0; i < pMap->GetSubmapCount(); i++)
         {
            sendMessage(&msg);
            msg.SetCode(CMD_SUBMAP_DATA);
            msg.DeleteAllVariables();
            pSubmap = pMap->GetSubmapByIndex(i);
            if (pSubmap != NULL)
               pSubmap->CreateMessage(&msg);
         }
         msg.SetEndOfSequence();
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_MAP_ID);
   }
   UnlockMaps();

   sendMessage(&msg);
}


//
// Send submap's background image to client
//

void ClientSession::SendSubmapBkImage(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwMapId, dwSubmapId;
   TCHAR szBuffer[MAX_PATH];
   nxMapSrv *pMap;
   BOOL bSuccess = FALSE;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwMapId = pRequest->GetVariableLong(VID_MAP_ID);
   dwSubmapId = pRequest->GetVariableLong(VID_OBJECT_ID);

   LockMaps();

   pMap = FindMapByID(dwMapId);
   if (pMap != NULL)
   {
      if (pMap->CheckUserRights(m_dwUserId, MAP_ACCESS_READ))
      {
         _sntprintf(szBuffer, MAX_PATH, 
                    _T("%s") DDIR_BACKGROUNDS FS_PATH_SEPARATOR _T("%08X.%08X"),
                    g_szDataDir, dwMapId, dwSubmapId);
         if (_taccess(szBuffer, 4) == 0)
         {
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            bSuccess = TRUE;
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_IO_ERROR);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_MAP_ID);
   }

   UnlockMaps();

   // Send response message
   sendMessage(&msg);

   // Send bitmap file
   if (bSuccess)
   {
      SendFileOverNXCP(m_hSocket, pRequest->GetId(), szBuffer, m_pCtx, 0, NULL, NULL);
   }
}


//
// Receive submap's background image from client
//

void ClientSession::RecvSubmapBkImage(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwMapId, dwSubmapId;
   nxMapSrv *pMap;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwMapId = pRequest->GetVariableLong(VID_MAP_ID);
   dwSubmapId = pRequest->GetVariableLong(VID_OBJECT_ID);

   LockMaps();

   pMap = FindMapByID(dwMapId);
   if (pMap != NULL)
   {
      if (pMap->CheckUserRights(m_dwUserId, MAP_ACCESS_WRITE))
      {
         // Prepare for file receive
         if (m_hCurrFile == -1)
         {
            _sntprintf(m_szCurrFileName, MAX_PATH, 
                       _T("%s") DDIR_BACKGROUNDS FS_PATH_SEPARATOR _T("%08X.%08X"),
                       g_szDataDir, dwMapId, dwSubmapId);
            m_hCurrFile = _topen(m_szCurrFileName, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR);
            if (m_hCurrFile != -1)
            {
               m_dwFileRqId = pRequest->GetId();
               m_dwUploadCommand = CMD_UPLOAD_SUBMAP_BK_IMAGE;
               m_dwUploadData = 0;
               msg.SetVariable(VID_RCC, RCC_SUCCESS);
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_IO_ERROR);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_RESOURCE_BUSY);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_MAP_ID);
   }

   UnlockMaps();

   // Send response message
   sendMessage(&msg);
}


//
// Resolve single DCI name
//

DWORD ClientSession::ResolveDCIName(DWORD dwNode, DWORD dwItem, TCHAR **ppszName)
{
   NetObj *pObject;
   DCItem *pItem;
   DWORD dwResult;

   pObject = FindObjectById(dwNode);
   if (pObject != NULL)
   {
		if ((pObject->Type() == OBJECT_NODE) ||
			 (pObject->Type() == OBJECT_CLUSTER) ||
			 (pObject->Type() == OBJECT_TEMPLATE))
		{
			if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
			{
				pItem = ((Template *)pObject)->getItemById(dwItem);
				if (pItem != NULL)
				{
					*ppszName = (TCHAR *)pItem->getDescription();
					dwResult = RCC_SUCCESS;
				}
				else
				{
					dwResult = RCC_INVALID_DCI_ID;
				}
			}
			else
			{
				dwResult = RCC_ACCESS_DENIED;
			}
		}
		else
		{
	      dwResult = RCC_INCOMPATIBLE_OPERATION;
		}
   }
   else
   {
      dwResult = RCC_INVALID_OBJECT_ID;
   }
   return dwResult;
}


//
// Resolve DCI identifiers to names
//

void ClientSession::ResolveDCINames(CSCPMessage *pRequest)
{
   DWORD i, dwId, dwNumDCI, *pdwNodeList, *pdwDCIList, dwResult = RCC_INVALID_ARGUMENT;
   TCHAR *pszName;
   CSCPMessage msg;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwNumDCI = pRequest->GetVariableLong(VID_NUM_ITEMS);
   pdwNodeList = (DWORD *)malloc(sizeof(DWORD) * dwNumDCI);
   pdwDCIList = (DWORD *)malloc(sizeof(DWORD) * dwNumDCI);
   pRequest->GetVariableInt32Array(VID_NODE_LIST, dwNumDCI, pdwNodeList);
   pRequest->GetVariableInt32Array(VID_DCI_LIST, dwNumDCI, pdwDCIList);
   
   for(i = 0, dwId = VID_DCI_LIST_BASE; i < dwNumDCI; i++)
   {
      dwResult = ResolveDCIName(pdwNodeList[i], pdwDCIList[i], &pszName);
      if (dwResult != RCC_SUCCESS)
         break;
      msg.SetVariable(dwId++, pszName);
   }

   free(pdwNodeList);
   free(pdwDCIList);

   msg.SetVariable(VID_RCC, dwResult);
   sendMessage(&msg);
}


//
// Send list of available agent configs to client
//

void ClientSession::SendAgentCfgList(DWORD dwRqId)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD i, dwId, dwNumRows;
   TCHAR szText[MAX_DB_STRING];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_AGENT_CFG)
   {
      hResult = DBSelect(g_hCoreDB, _T("SELECT config_id,config_name,sequence_number FROM agent_configs"));
      if (hResult != NULL)
      {
         dwNumRows = DBGetNumRows(hResult);
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         msg.SetVariable(VID_NUM_RECORDS, dwNumRows);
         for(i = 0, dwId = VID_AGENT_CFG_LIST_BASE; i < dwNumRows; i++, dwId += 7)
         {
            msg.SetVariable(dwId++, DBGetFieldULong(hResult, i, 0));
            DBGetField(hResult, i, 1, szText, MAX_DB_STRING);
            DecodeSQLString(szText);
            msg.SetVariable(dwId++, szText);
            msg.SetVariable(dwId++, DBGetFieldULong(hResult, i, 2));
         }
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Open (get all data) server-stored agent's config
//

void ClientSession::OpenAgentConfig(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD dwCfgId;
   TCHAR *pszStr, szQuery[256], szBuffer[MAX_DB_STRING];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_AGENT_CFG)
   {
      dwCfgId = pRequest->GetVariableLong(VID_CONFIG_ID);
      _sntprintf(szQuery, 256, _T("SELECT config_name,config_file,config_filter,sequence_number FROM agent_configs WHERE config_id=%d"), dwCfgId);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) > 0)
         {
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            msg.SetVariable(VID_CONFIG_ID, dwCfgId);
            DecodeSQLStringAndSetVariable(&msg, VID_NAME, DBGetField(hResult, 0, 0, szBuffer, MAX_DB_STRING));
            pszStr = DBGetField(hResult, 0, 1, NULL, 0);
            DecodeSQLStringAndSetVariable(&msg, VID_CONFIG_FILE, pszStr);
            free(pszStr);
            pszStr = DBGetField(hResult, 0, 2, NULL, 0);
            DecodeSQLStringAndSetVariable(&msg, VID_FILTER, pszStr);
            free(pszStr);
            msg.SetVariable(VID_SEQUENCE_NUMBER, DBGetFieldULong(hResult, 0, 3));
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_CONFIG_ID);
         }
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Save changes to server-stored agent's configuration
//

void ClientSession::SaveAgentConfig(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD dwCfgId, dwSeqNum;
   TCHAR szQuery[256], szName[MAX_DB_STRING], *pszFilter, *pszText;
   TCHAR *pszQuery, *pszEscFilter, *pszEscText, *pszEscName;
   BOOL bCreate;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_AGENT_CFG)
   {
      dwCfgId = pRequest->GetVariableLong(VID_CONFIG_ID);
      _sntprintf(szQuery, 256, _T("SELECT config_name FROM agent_configs WHERE config_id=%d"), dwCfgId);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         bCreate = (DBGetNumRows(hResult) == 0);
         DBFreeResult(hResult);

         pRequest->GetVariableStr(VID_NAME, szName, MAX_DB_STRING);
         pszEscName = EncodeSQLString(szName);

         pszFilter = pRequest->GetVariableStr(VID_FILTER);
         pszEscFilter = EncodeSQLString(pszFilter);
         free(pszFilter);

         pszText = pRequest->GetVariableStr(VID_CONFIG_FILE);
         pszEscText = EncodeSQLString(pszText);
         free(pszText);

			size_t qlen = _tcslen(pszEscText) + _tcslen(pszEscFilter) + _tcslen(pszEscName) + 256;
         pszQuery = (TCHAR *)malloc(qlen * sizeof(TCHAR));
         if (bCreate)
         {
            if (dwCfgId == 0)
            {
               // Request for new ID creation
               dwCfgId = CreateUniqueId(IDG_AGENT_CONFIG);
               msg.SetVariable(VID_CONFIG_ID, dwCfgId);

               // Request sequence number
               hResult = DBSelect(g_hCoreDB, _T("SELECT max(sequence_number) FROM agent_configs"));
               if (hResult != NULL)
               {
                  if (DBGetNumRows(hResult) > 0)
                     dwSeqNum = DBGetFieldULong(hResult, 0, 0) + 1;
                  else
                     dwSeqNum = 1;
                  DBFreeResult(hResult);
               }
               else
               {
                  dwSeqNum = 1;
               }
               msg.SetVariable(VID_SEQUENCE_NUMBER, dwSeqNum);
            }
            _sntprintf(pszQuery, qlen, _T("INSERT INTO agent_configs (config_id,config_name,config_filter,config_file,sequence_number) VALUES (%d,'%s','%s','%s',%d)"),
                      dwCfgId, pszEscName, pszEscFilter, pszEscText, dwSeqNum);
         }
         else
         {
            _sntprintf(pszQuery, qlen, _T("UPDATE agent_configs SET config_name='%s',config_filter='%s',config_file='%s',sequence_number=%d WHERE config_id=%d"),
                      pszEscName, pszEscFilter, pszEscText,
                      pRequest->GetVariableLong(VID_SEQUENCE_NUMBER), dwCfgId);
         }
         free(pszEscName);
         free(pszEscText);
         free(pszEscFilter);

         if (DBQuery(g_hCoreDB, pszQuery))
         {
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
         }
         free(pszQuery);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Delete agent's configuration
//

void ClientSession::DeleteAgentConfig(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   DWORD dwCfgId;
   TCHAR szQuery[256];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_AGENT_CFG)
   {
      dwCfgId = pRequest->GetVariableLong(VID_CONFIG_ID);
      _sntprintf(szQuery, 256, _T("SELECT config_name FROM agent_configs WHERE config_id=%d"), dwCfgId);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) > 0)
         {
            _sntprintf(szQuery, 256, _T("DELETE FROM agent_configs WHERE config_id=%d"), dwCfgId);
            if (DBQuery(g_hCoreDB, szQuery))
            {
               msg.SetVariable(VID_RCC, RCC_SUCCESS);
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_CONFIG_ID);
         }
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Swap sequence numbers of two agent configs
//

void ClientSession::SwapAgentConfigs(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DB_RESULT hResult;
   TCHAR szQuery[256];
   BOOL bRet;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_AGENT_CFG)
   {
      _sntprintf(szQuery, 256, _T("SELECT config_id,sequence_number FROM agent_configs WHERE config_id=%d OR config_id=%d"),
                 pRequest->GetVariableLong(VID_CONFIG_ID), pRequest->GetVariableLong(VID_CONFIG_ID_2));
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) >= 2)
         {
            if (DBBegin(g_hCoreDB))
            {
               _sntprintf(szQuery, 256, _T("UPDATE agent_configs SET sequence_number=%d WHERE config_id=%d"),
                          DBGetFieldULong(hResult, 1, 1), DBGetFieldULong(hResult, 0, 0));
               bRet = DBQuery(g_hCoreDB, szQuery);
               if (bRet)
               {
                  _sntprintf(szQuery, 256, _T("UPDATE agent_configs SET sequence_number=%d WHERE config_id=%d"),
                             DBGetFieldULong(hResult, 0, 1), DBGetFieldULong(hResult, 1, 0));
                  bRet = DBQuery(g_hCoreDB, szQuery);
               }

               if (bRet)
               {
                  DBCommit(g_hCoreDB);
                  msg.SetVariable(VID_RCC, RCC_SUCCESS);
               }
               else
               {
                  DBRollback(g_hCoreDB);
                  msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
               }
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_CONFIG_ID);
         }
         DBFreeResult(hResult);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Send config to agent on request
//

void ClientSession::SendConfigForAgent(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szPlatform[MAX_DB_STRING], szError[256], szBuffer[256], *pszText;
   WORD wMajor, wMinor, wRelease;
   int i, nNumRows;
   DB_RESULT hResult;
   NXSL_Program *pScript;
   NXSL_Value *ppArgList[5], *pValue;
   DWORD dwCfgId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   pRequest->GetVariableStr(VID_PLATFORM_NAME, szPlatform, MAX_DB_STRING);
   wMajor = pRequest->GetVariableShort(VID_VERSION_MAJOR);
   wMinor = pRequest->GetVariableShort(VID_VERSION_MINOR);
   wRelease = pRequest->GetVariableShort(VID_VERSION_RELEASE);
   DbgPrintf(3, _T("Finding config for agent at %s: platform=\"%s\", version=\"%d.%d.%d\""),
             IpToStr(m_dwHostAddr, szBuffer), szPlatform, (int)wMajor, (int)wMinor, (int)wRelease);

   hResult = DBSelect(g_hCoreDB, _T("SELECT config_id,config_file,config_filter FROM agent_configs ORDER BY sequence_number"));
   if (hResult != NULL)
   {
      nNumRows = DBGetNumRows(hResult);
      for(i = 0; i < nNumRows; i++)
      {
         dwCfgId = DBGetFieldULong(hResult, i, 0);

         // Compile script
         pszText = DBGetField(hResult, i, 2, NULL, 0);
         DecodeSQLString(pszText);
         pScript = (NXSL_Program *)NXSLCompile(pszText, szError, 256);
         free(pszText);

         if (pScript != NULL)
         {
            // Set arguments:
            // $1 - IP address
            // $2 - platform
            // $3 - major version number
            // $4 - minor version number
            // $5 - release number
            ppArgList[0] = new NXSL_Value(IpToStr(m_dwHostAddr, szBuffer));
            ppArgList[1] = new NXSL_Value(szPlatform);
            ppArgList[2] = new NXSL_Value((LONG)wMajor);
            ppArgList[3] = new NXSL_Value((LONG)wMinor);
            ppArgList[4] = new NXSL_Value((LONG)wRelease);

            // Run script
            DbgPrintf(3, _T("Running configuration matching script %d"), dwCfgId);
            if (pScript->run(new NXSL_ServerEnv, 5, ppArgList) == 0)
            {
               pValue = pScript->getResult();
               if (pValue->getValueAsInt32() != 0)
               {
                  DbgPrintf(3, _T("Configuration script %d matched for agent %s, sending config"),
                            dwCfgId, IpToStr(m_dwHostAddr, szBuffer));
                  msg.SetVariable(VID_RCC, (WORD)0);
                  pszText = DBGetField(hResult, i, 1, NULL, 0);
                  DecodeSQLStringAndSetVariable(&msg, VID_CONFIG_FILE, pszText);
                  msg.SetVariable(VID_CONFIG_ID, dwCfgId);
                  free(pszText);
                  break;
               }
               else
               {
                  DbgPrintf(3, _T("Configuration script %d not matched for agent %s"),
                            dwCfgId, IpToStr(m_dwHostAddr, szBuffer));
               }
            }
            else
            {
               _sntprintf(szError, 256, _T("AgentCfg::%d"), dwCfgId);
               PostEvent(EVENT_SCRIPT_ERROR, g_dwMgmtNode, "ssd", szError,
                         pScript->getErrorText(), 0);
            }
            delete pScript;
         }
         else
         {
            _sntprintf(szBuffer, 256, _T("AgentCfg::%d"), dwCfgId);
            PostEvent(EVENT_SCRIPT_ERROR, g_dwMgmtNode, "ssd", szBuffer, szError, 0);
         }
      }
      DBFreeResult(hResult);

      if (i == nNumRows)
         msg.SetVariable(VID_RCC, (WORD)1);  // No matching configs found
   }
   else
   {
      msg.SetVariable(VID_RCC, (WORD)1);  // DB Failure
   }

   sendMessage(&msg);
}


//
// Send object comments to client
//

void ClientSession::SendObjectComments(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
      {
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         pObject->CommentsToMessage(&msg);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   sendMessage(&msg);
}


//
// Update object comments from client
//

void ClientSession::UpdateObjectComments(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_MODIFY))
      {
         pObject->SetComments(pRequest->GetVariableStr(VID_COMMENTS));
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   sendMessage(&msg);
}


//
// Push DCI data
//

void ClientSession::PushDCIData(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD i, dwObjectId, dwNumItems, dwId, dwItemId;
   NetObj *pObject;
   Node **ppNodeList = NULL;
   DCItem *pItem, **ppItemList = NULL;
   TCHAR szName[256], **ppValueList;
   BOOL bOK;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   dwNumItems = pRequest->GetVariableLong(VID_NUM_ITEMS);
   if (dwNumItems > 0)
   {
      ppNodeList = (Node **)malloc(sizeof(Node *) * dwNumItems);
      ppItemList = (DCItem **)malloc(sizeof(DCItem *) * dwNumItems);
      ppValueList = (TCHAR **)malloc(sizeof(TCHAR *) * dwNumItems);
      memset(ppValueList, 0, sizeof(TCHAR *) * dwNumItems);

      for(i = 0, dwId = VID_PUSH_DCI_DATA_BASE, bOK = TRUE; (i < dwNumItems) && bOK; i++)
      {
         bOK = FALSE;

         // Find object either by ID or name (id ID==0)
         dwObjectId = pRequest->GetVariableLong(dwId++);
         if (dwObjectId != 0)
         {
            pObject = FindObjectById(dwObjectId);
         }
         else
         {
            pRequest->GetVariableStr(dwId++, szName, 256);
            pObject = FindObjectByName(szName, OBJECT_NODE);
         }

         // Validate object
         if (pObject != NULL)
         {
            if (pObject->Type() == OBJECT_NODE)
            {
               if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_PUSH_DATA))
               {
                  ppNodeList[i] = (Node *)pObject;

                  // Object OK, find DCI by ID or name (if ID==0)
                  dwItemId = pRequest->GetVariableLong(dwId++);
                  if (dwItemId != 0)
                  {
                     pItem = ppNodeList[i]->getItemById(dwItemId);
                  }
                  else
                  {
                     pRequest->GetVariableStr(dwId++, szName, 256);
                     pItem = ppNodeList[i]->getItemByName(szName);
                  }

                  if (pItem != NULL)
                  {
                     if (pItem->getDataSource() == DS_PUSH_AGENT)
                     {
                        ppItemList[i] = pItem;
                        ppValueList[i] = pRequest->GetVariableStr(dwId++);
                        bOK = TRUE;
                     }
                     else
                     {
                        msg.SetVariable(VID_RCC, RCC_NOT_PUSH_DCI);
                     }
                  }
                  else
                  {
                     msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
                  }
               }
               else
               {
                  msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
               }
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
         }
      }

      // If all items was checked OK, push data
      if (bOK)
      {
         time_t t;

         time(&t);
         for(i = 0; i < dwNumItems; i++)
         {
				if (_tcslen(ppValueList[i]) >= MAX_DCI_STRING_VALUE)
					ppValueList[i][MAX_DCI_STRING_VALUE - 1] = 0;
				ppNodeList[i]->processNewDciValue(ppItemList[i], t, ppValueList[i]);
				ppItemList[i]->setLastPollTime(t);
         }
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
      else
      {
         msg.SetVariable(VID_FAILED_DCI_INDEX, i - 1);
      }

      // Cleanup
      for(i = 0; i < dwNumItems; i++)
         safe_free(ppValueList[i]);
      free(ppItemList);
      free(ppNodeList);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_ARGUMENT);
   }

   sendMessage(&msg);
}


//
// Get address list
//

void ClientSession::GetAddrList(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szQuery[256];
   DWORD i, dwNumRec, dwId;
   DB_RESULT hResult;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
   {
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT addr_type,addr1,addr2 FROM address_lists WHERE list_type=%d"),
                pRequest->GetVariableLong(VID_ADDR_LIST_TYPE));
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         dwNumRec = DBGetNumRows(hResult);
         msg.SetVariable(VID_NUM_RECORDS, dwNumRec);
         for(i = 0, dwId = VID_ADDR_LIST_BASE; i < dwNumRec; i++, dwId += 7)
         {
            msg.SetVariable(dwId++, DBGetFieldULong(hResult, i, 0));
            msg.SetVariable(dwId++, DBGetFieldIPAddr(hResult, i, 1));
            msg.SetVariable(dwId++, DBGetFieldIPAddr(hResult, i, 2));
         }
         DBFreeResult(hResult);
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Set address list
//

void ClientSession::SetAddrList(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD i, dwId, dwNumRec, dwListType;
   TCHAR szQuery[256], szIpAddr1[24], szIpAddr2[24];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
   {
      dwListType = pRequest->GetVariableLong(VID_ADDR_LIST_TYPE);
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM address_lists WHERE list_type=%d"), dwListType);
      DBBegin(g_hCoreDB);
      if (DBQuery(g_hCoreDB, szQuery))
      {
         dwNumRec = pRequest->GetVariableLong(VID_NUM_RECORDS);
         for(i = 0, dwId = VID_ADDR_LIST_BASE; i < dwNumRec; i++, dwId += 10)
         {
            _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("INSERT INTO address_lists (list_type,addr_type,addr1,addr2,community_id) VALUES (%d,%d,'%s','%s',0)"),
                      dwListType, pRequest->GetVariableLong(dwId),
                      IpToStr(pRequest->GetVariableLong(dwId + 1), szIpAddr1),
                      IpToStr(pRequest->GetVariableLong(dwId + 2), szIpAddr2));
            if (!DBQuery(g_hCoreDB, szQuery))
               break;
         }

         if (i == dwNumRec)
         {
            DBCommit(g_hCoreDB);
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
         else
         {
            DBRollback(g_hCoreDB);
            msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
         }
      }
      else
      {
         DBRollback(g_hCoreDB);
         msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Reset server component
//

void ClientSession::ResetComponent(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD dwCode;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
   {
      dwCode = pRequest->GetVariableLong(VID_COMPONENT_ID);
      switch(dwCode)
      {
         case SRV_COMPONENT_DISCOVERY_MGR:
            ResetDiscoveryPoller();
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
            break;
         default:
            msg.SetVariable(VID_RCC, RCC_INVALID_ARGUMENT);
            break;
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Send list of events used by template's or node's DCIs
//

void ClientSession::sendDCIEventList(CSCPMessage *request)
{
   CSCPMessage msg;
   NetObj *pObject;
   DWORD *pdwEventList, dwCount;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(request->GetId());

   pObject = FindObjectById(request->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
      {
         if ((pObject->Type() == OBJECT_NODE) ||
             (pObject->Type() == OBJECT_CLUSTER) ||
             (pObject->Type() == OBJECT_TEMPLATE))
         {
            pdwEventList = ((Template *)pObject)->getDCIEventsList(&dwCount);
            if (pdwEventList != NULL)
            {
               msg.SetVariable(VID_NUM_EVENTS, dwCount);
               msg.SetVariableToInt32Array(VID_EVENT_LIST, dwCount, pdwEventList);
               free(pdwEventList);
            }
            else
            {
               msg.SetVariable(VID_NUM_EVENTS, (DWORD)0);
            }
            msg.SetVariable(VID_RCC, RCC_SUCCESS);
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   sendMessage(&msg);
}


//
// Export server configuration (event, templates, etc.)
//

void ClientSession::exportConfiguration(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD i, dwCount, dwNumTemplates;
   DWORD *pdwList, *pdwTemplateList;
   NetObj *pObject;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if ((m_dwSystemAccess & (SYSTEM_ACCESS_CONFIGURE_TRAPS | SYSTEM_ACCESS_VIEW_EVENT_DB)) == (SYSTEM_ACCESS_CONFIGURE_TRAPS | SYSTEM_ACCESS_VIEW_EVENT_DB))
   {
      dwNumTemplates = pRequest->GetVariableLong(VID_NUM_OBJECTS);
      if (dwNumTemplates > 0)
      {
         pdwTemplateList = (DWORD *)malloc(sizeof(DWORD) * dwNumTemplates);
         pRequest->GetVariableInt32Array(VID_OBJECT_LIST, dwNumTemplates, pdwTemplateList);
      }
      else
      {
         pdwTemplateList = NULL;
      }

      for(i = 0; i < dwNumTemplates; i++)
      {
         pObject = FindObjectById(pdwTemplateList[i]);
         if (pObject != NULL)
         {
            if (pObject->Type() == OBJECT_TEMPLATE)
            {
               if (!pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
               {
                  msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
                  break;
               }
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
               break;
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
            break;
         }
      }

      if (i == dwNumTemplates)   // All objects passed test
      {
         String str;
			TCHAR *temp;

         str = _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<configuration>\n\t<formatVersion>3</formatVersion>\n\t<description>");
			temp = pRequest->GetVariableStr(VID_DESCRIPTION);
			str.addDynamicString(EscapeStringForXML(temp, -1));
			free(temp);
         str += _T("</description>\n");

         // Write events
         str += _T("\t<events>\n");
         dwCount = pRequest->GetVariableLong(VID_NUM_EVENTS);
         pdwList = (DWORD *)malloc(sizeof(DWORD) * dwCount);
         pRequest->GetVariableInt32Array(VID_EVENT_LIST, dwCount, pdwList);
         for(i = 0; i < dwCount; i++)
            CreateNXMPEventRecord(str, pdwList[i]);
         safe_free(pdwList);
         str += _T("\t</events>\n");

         // Write templates
         str += _T("\t<templates>\n");
         for(i = 0; i < dwNumTemplates; i++)
         {
            pObject = FindObjectById(pdwTemplateList[i]);
            if (pObject != NULL)
            {
               ((Template *)pObject)->CreateNXMPRecord(str);
            }
         }
         str += _T("\t</templates>\n");

         // Write traps
         str += _T("\t<traps>\n");
         dwCount = pRequest->GetVariableLong(VID_NUM_TRAPS);
         pdwList = (DWORD *)malloc(sizeof(DWORD) * dwCount);
         pRequest->GetVariableInt32Array(VID_TRAP_LIST, dwCount, pdwList);
         for(i = 0; i < dwCount; i++)
            CreateNXMPTrapRecord(str, pdwList[i]);
         safe_free(pdwList);
         str += _T("\t</traps>\n");

			// Close document
			str += _T("</configuration>\n");

         // Put result into message
         msg.SetVariable(VID_RCC, RCC_SUCCESS);
         msg.SetVariable(VID_NXMP_CONTENT, (const TCHAR *)str);
      }

      safe_free(pdwTemplateList);
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Import server configuration (events, templates, etc.)
//

void ClientSession::importConfiguration(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR szLockInfo[MAX_SESSION_NAME], szError[1024];
   DWORD dwFlags;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   if ((m_dwSystemAccess & (SYSTEM_ACCESS_CONFIGURE_TRAPS | SYSTEM_ACCESS_EDIT_EVENT_DB | SYSTEM_ACCESS_EPP)) == (SYSTEM_ACCESS_CONFIGURE_TRAPS | SYSTEM_ACCESS_EDIT_EVENT_DB | SYSTEM_ACCESS_EPP))
   {
      char *content = pRequest->GetVariableStrUTF8(VID_NXMP_CONTENT);
      if (content != NULL)
      {
			Config *config = new Config();
         if (config->loadXmlConfigFromMemory(content, (int)strlen(content), NULL, "configuration"))
         {
            // Lock all required components
            if (LockComponent(CID_EPP, m_dwIndex, m_szUserName, NULL, szLockInfo))
            {
               m_dwFlags |= CSF_EPP_LOCKED;

               // Validate and import configuration
               dwFlags = pRequest->GetVariableLong(VID_FLAGS);
               if (ValidateConfig(config, dwFlags, szError, 1024))
               {
                  msg.SetVariable(VID_RCC, ImportConfig(config, dwFlags));
               }
               else
               {
                  msg.SetVariable(VID_RCC, RCC_CONFIG_VALIDATION_ERROR);
                  msg.SetVariable(VID_ERROR_TEXT, szError);
               }

					UnlockComponent(CID_EPP);
               m_dwFlags &= ~CSF_EPP_LOCKED;
            }
            else
            {
               msg.SetVariable(VID_RCC, RCC_COMPONENT_LOCKED);
               msg.SetVariable(VID_COMPONENT, (WORD)NXMP_LC_EPP);
               msg.SetVariable(VID_LOCKED_BY, szLockInfo);
            }
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_CONFIG_PARSE_ERROR);
         }
			delete config;
         free(content);
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_INVALID_ARGUMENT);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
   }

   sendMessage(&msg);
}


//
// Send basic DCI info to client
//

void ClientSession::SendDCIInfo(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;
	DCItem *pItem;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
      {
         if ((pObject->Type() == OBJECT_NODE) ||
             (pObject->Type() == OBJECT_CLUSTER) ||
             (pObject->Type() == OBJECT_TEMPLATE))
         {
				pItem = ((Template *)pObject)->getItemById(pRequest->GetVariableLong(VID_DCI_ID));
				if (pItem != NULL)
				{
					msg.SetVariable(VID_TEMPLATE_ID, pItem->getTemplateId());
					msg.SetVariable(VID_RESOURCE_ID, pItem->getResourceId());
					msg.SetVariable(VID_DCI_DATA_TYPE, (WORD)pItem->getDataType());
					msg.SetVariable(VID_DCI_SOURCE_TYPE, (WORD)pItem->getDataSource());
					msg.SetVariable(VID_NAME, pItem->getName());
					msg.SetVariable(VID_DESCRIPTION, pItem->getDescription());
	            msg.SetVariable(VID_RCC, RCC_SUCCESS);
				}
				else
				{
			      msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
				}
         }
         else
         {
            msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
         }
      }
      else
      {
         msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
      }
   }
   else
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   sendMessage(&msg);
}


//
// Check access to the graph
//

static BOOL CheckGraphAccess(GRAPH_ACL_ENTRY *pACL, int nACLSize, DWORD dwGraphId,
									  DWORD dwUserId, DWORD dwDesiredAccess)
{
	int i;

	for(i = 0; i < nACLSize; i++)
	{
		if (pACL[i].dwGraphId == dwGraphId)
		{
			if ((pACL[i].dwUserId == dwUserId) ||
				 ((pACL[i].dwUserId & GROUP_FLAG) && CheckUserMembership(dwUserId, pACL[i].dwUserId)))
			{
				if ((pACL[i].dwAccess & dwDesiredAccess) == dwDesiredAccess)
					return TRUE;
			}
		}
	}
	return FALSE;
}


//
// Load graph's ACL - load for all graphs if dwGraphId is 0
//

static GRAPH_ACL_ENTRY *LoadGraphACL(DWORD dwGraphId, int *pnACLSize)
{
	int i, nSize;
	GRAPH_ACL_ENTRY *pACL = NULL;
	DB_RESULT hResult;

	if (dwGraphId == 0)
	{
		hResult = DBSelect(g_hCoreDB, _T("SELECT graph_id,user_id,user_rights FROM graph_acl"));
	}
	else
	{
		TCHAR szQuery[256];

		_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT graph_id,user_id,user_rights FROM graph_acl WHERE graph_id=%d"), dwGraphId);
		hResult = DBSelect(g_hCoreDB, szQuery);
	}
	if (hResult != NULL)
	{
		nSize = DBGetNumRows(hResult);
		if (nSize > 0)
		{
			pACL = (GRAPH_ACL_ENTRY *)malloc(sizeof(GRAPH_ACL_ENTRY) * nSize);
			for(i = 0; i < nSize; i++)
			{
				pACL[i].dwGraphId = DBGetFieldULong(hResult, i, 0);
				pACL[i].dwUserId = DBGetFieldULong(hResult, i, 1);
				pACL[i].dwAccess = DBGetFieldULong(hResult, i, 2);
			}
		}
		*pnACLSize = nSize;
		DBFreeResult(hResult);
	}
	else
	{
		*pnACLSize = -1;	// Database error
	}
	return pACL;
}


//
// Send list of available graphs to client
//

void ClientSession::SendGraphList(DWORD dwRqId)
{
   CSCPMessage msg;
	DB_RESULT hResult;
	GRAPH_ACL_ENTRY *pACL = NULL;
	int i, j, nRows, nACLSize;
	DWORD dwId, dwNumGraphs, dwGraphId, dwOwner;
	DWORD *pdwUsers, *pdwRights, dwGraphACLSize;
	TCHAR *pszStr;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

	pACL = LoadGraphACL(0, &nACLSize);
	if (nACLSize != -1)
	{
		hResult = DBSelect(g_hCoreDB, _T("SELECT graph_id,owner_id,name,config FROM graphs"));
		if (hResult != NULL)
		{
			pdwUsers = (DWORD *)malloc(sizeof(DWORD) * nACLSize);
			pdwRights = (DWORD *)malloc(sizeof(DWORD) * nACLSize);
			nRows = DBGetNumRows(hResult);
			for(i = 0, dwNumGraphs = 0, dwId = VID_GRAPH_LIST_BASE; i < nRows; i++)
			{
				dwGraphId = DBGetFieldULong(hResult, i, 0);
				dwOwner = DBGetFieldULong(hResult, i, 1);
				if ((m_dwUserId == 0) ||
				    (m_dwUserId == dwOwner) ||
				    CheckGraphAccess(pACL, nACLSize, dwGraphId, m_dwUserId, NXGRAPH_ACCESS_READ))
				{
					msg.SetVariable(dwId++, dwGraphId);
					msg.SetVariable(dwId++, dwOwner);
					pszStr = DBGetField(hResult, i, 2, NULL, 0);
					if (pszStr != NULL)
					{
						DecodeSQLStringAndSetVariable(&msg, dwId++, pszStr);
						free(pszStr);
					}
					pszStr = DBGetField(hResult, i, 3, NULL, 0);
					if (pszStr != NULL)
					{
						DecodeSQLStringAndSetVariable(&msg, dwId++, pszStr);
						free(pszStr);
					}

					// ACL for graph
					for(j = 0, dwGraphACLSize = 0; j < nACLSize; j++)
					{
						if (pACL[j].dwGraphId == dwGraphId)
						{
							pdwUsers[dwGraphACLSize] = pACL[j].dwUserId;
							pdwRights[dwGraphACLSize] = pACL[j].dwAccess;
							dwGraphACLSize++;
						}
					}
					msg.SetVariable(dwId++, dwGraphACLSize);
					msg.SetVariableToInt32Array(dwId++, dwGraphACLSize, pdwUsers);
					msg.SetVariableToInt32Array(dwId++, dwGraphACLSize, pdwRights);

					dwId += 3;
					dwNumGraphs++;
				}
			}
			DBFreeResult(hResult);
			free(pdwUsers);
			free(pdwRights);
			msg.SetVariable(VID_NUM_GRAPHS, dwNumGraphs);
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
		safe_free(pACL);
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
	}

   sendMessage(&msg);
}


//
// Define graph
//

void ClientSession::DefineGraph(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	BOOL bNew, bSuccess;
	DWORD dwId, dwGraphId, dwOwner, dwUserId, dwAccess;
	TCHAR szQuery[16384], *pszEscName, *pszEscData, *pszTemp;
	GRAPH_ACL_ENTRY *pACL = NULL;
	int i, nACLSize;
	DB_RESULT hResult;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	dwGraphId = pRequest->GetVariableLong(VID_GRAPH_ID);
	if (dwGraphId == 0)
	{
		// New graph
		dwGraphId = CreateUniqueId(IDG_GRAPH);
		bNew = TRUE;
		bSuccess = TRUE;
	}
	else
	{
		bNew = FALSE;
		bSuccess = FALSE;

		// Check existence and access rights
		_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT owner_id FROM graphs WHERE graph_id=%d"), dwGraphId);
		hResult = DBSelect(g_hCoreDB, szQuery);
		if (hResult != NULL)
		{
			if (DBGetNumRows(hResult) > 0)
			{
				dwOwner = DBGetFieldULong(hResult, 0, 0);
				pACL = LoadGraphACL(dwGraphId, &nACLSize);
				if (nACLSize != -1)
				{
					if ((m_dwUserId == 0) ||
						 (m_dwUserId == dwOwner) ||
						 CheckGraphAccess(pACL, nACLSize, dwGraphId, m_dwUserId, NXGRAPH_ACCESS_WRITE))
					{
						bSuccess = TRUE;
					}
					else
					{
						msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
					}
					safe_free(pACL);
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
				}
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_INVALID_GRAPH_ID);
			}
			DBFreeResult(hResult);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}

	// Create/update graph
	if (bSuccess)
	{
		DebugPrintf(5, _T("%s graph %d"), bNew ? _T("Creating") : _T("Updating"), dwGraphId);
		bSuccess = FALSE;
		if (DBBegin(g_hCoreDB))
		{
			pRequest->GetVariableStr(VID_NAME, szQuery, 256);
			pszEscName = EncodeSQLString(szQuery);
			pszTemp = pRequest->GetVariableStr(VID_GRAPH_CONFIG);
			if (pszTemp != NULL)
			{
				pszEscData = EncodeSQLString(CHECK_NULL(pszTemp));
				free(pszTemp);
			}
			if (bNew)
			{
				_sntprintf(szQuery, 16384, _T("INSERT INTO graphs (graph_id,owner_id,name,config) VALUES (%d,%d,'%s','%s')"),
				           dwGraphId, m_dwUserId, pszEscName, pszEscData);
			}
			else
			{
				_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM graph_acl WHERE graph_id=%d"), dwGraphId);
				DBQuery(g_hCoreDB, szQuery);

				_sntprintf(szQuery, 16384, _T("UPDATE graphs SET name='%s',config='%s' WHERE graph_id=%d"),
				           pszEscName, pszEscData, dwGraphId);
			}
			free(pszEscName);
			free(pszEscData);

			if (DBQuery(g_hCoreDB, szQuery))
			{
				// Insert new ACL
				nACLSize = (int)pRequest->GetVariableLong(VID_ACL_SIZE);
				for(i = 0, dwId = VID_GRAPH_ACL_BASE, bSuccess = TRUE; i < nACLSize; i++)
				{
					dwUserId = pRequest->GetVariableLong(dwId++);
					dwAccess = pRequest->GetVariableLong(dwId++);
					_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("INSERT INTO graph_acl (graph_id,user_id,user_rights) VALUES (%d,%d,%d)"),
					          dwGraphId, dwUserId, dwAccess);
					if (!DBQuery(g_hCoreDB, szQuery))
					{
						bSuccess = FALSE;
						msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
						break;
					}
				}
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
			}

			if (bSuccess)
			{
				DBCommit(g_hCoreDB);
				msg.SetVariable(VID_RCC, RCC_SUCCESS);
				notify(NX_NOTIFY_GRAPHS_CHANGED);
			}
			else
			{
				DBRollback(g_hCoreDB);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}

   sendMessage(&msg);
}


//
// Delete graph
//

void ClientSession::DeleteGraph(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	DWORD dwGraphId, dwOwner;
	GRAPH_ACL_ENTRY *pACL = NULL;
	int nACLSize;
	DB_RESULT hResult;
	TCHAR szQuery[256];

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	dwGraphId = pRequest->GetVariableLong(VID_GRAPH_ID);
	_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT owner_id FROM graphs WHERE graph_id=%d"), dwGraphId);
	hResult = DBSelect(g_hCoreDB, szQuery);
	if (hResult != NULL)
	{
		if (DBGetNumRows(hResult) > 0)
		{
			dwOwner = DBGetFieldULong(hResult, 0, 0);
			pACL = LoadGraphACL(dwGraphId, &nACLSize);
			if (nACLSize != -1)
			{
				if ((m_dwUserId == 0) ||
				    (m_dwUserId == dwOwner) ||
				    CheckGraphAccess(pACL, nACLSize, dwGraphId, m_dwUserId, NXGRAPH_ACCESS_READ))
				{
					_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM graphs WHERE graph_id=%d"), dwGraphId);
					if (DBQuery(g_hCoreDB, szQuery))
					{
						_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM graph_acl WHERE graph_id=%d"), dwGraphId);
						DBQuery(g_hCoreDB, szQuery);
						msg.SetVariable(VID_RCC, RCC_SUCCESS);
						notify(NX_NOTIFY_GRAPHS_CHANGED);
					}
					else
					{
						msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
					}
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
				}
				safe_free(pACL);
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_GRAPH_ID);
		}
		DBFreeResult(hResult);
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
	}

   sendMessage(&msg);
}


//
// Send list of system DCIs
//

void ClientSession::sendPerfTabDCIList(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	NetObj *pObject;

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
	if (pObject != NULL)
	{
		if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
		{
			if (pObject->Type() == OBJECT_NODE)
			{
				msg.SetVariable(VID_RCC, ((Node *)pObject)->getPerfTabDCIList(&msg));
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
	}

   sendMessage(&msg);
}


//
// Add CA certificate
//

void ClientSession::AddCACertificate(CSCPMessage *pRequest)
{
   CSCPMessage msg;
#ifdef _WITH_ENCRYPTION
	DWORD dwLen, dwQLen, dwCertId;
	BYTE *pData;
	OPENSSL_CONST BYTE *p;
	X509 *pCert;
	TCHAR *pszQuery, *pszEscSubject, *pszComments, *pszEscComments;
#endif

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if ((m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS) &&
	    (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG))
	{
#ifdef _WITH_ENCRYPTION
		dwLen = pRequest->GetVariableBinary(VID_CERTIFICATE, NULL, 0);
		if (dwLen > 0)
		{
			pData = (BYTE *)malloc(dwLen);
			pRequest->GetVariableBinary(VID_CERTIFICATE, pData, dwLen);

			// Validate certificate
			p = pData;
			pCert = d2i_X509(NULL, &p, dwLen);
			if (pCert != NULL)
			{
#ifdef UNICODE
				WCHAR *wname = WideStringFromMBString(CHECK_NULL_A(pCert->name));
				pszEscSubject = EncodeSQLString(wname);
				free(wname);
#else
				pszEscSubject = EncodeSQLString(CHECK_NULL(pCert->name));
#endif
				X509_free(pCert);
				pszComments = pRequest->GetVariableStr(VID_COMMENTS);
				pszEscComments = EncodeSQLString(pszComments);
				free(pszComments);
				dwCertId = CreateUniqueId(IDG_CERTIFICATE);
				dwQLen = dwLen * 2 + _tcslen(pszEscComments) + _tcslen(pszEscSubject) + 256;
				pszQuery = (TCHAR *)malloc(dwQLen * sizeof(TCHAR));
				_sntprintf(pszQuery, dwQLen, _T("INSERT INTO certificates (cert_id,cert_type,subject,comments,cert_data) VALUES (%d,%d,'%s','%s','"),
				           dwCertId, CERT_TYPE_TRUSTED_CA, pszEscSubject, pszEscComments);
				free(pszEscSubject);
				free(pszEscComments);
				BinToStr(pData, dwLen, &pszQuery[_tcslen(pszQuery)]);
				_tcscat(pszQuery, _T("')"));
				if (DBQuery(g_hCoreDB, pszQuery))
				{
					msg.SetVariable(VID_RCC, RCC_SUCCESS);
					ReloadCertificates();
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
				}
				free(pszQuery);
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_BAD_CERTIFICATE);
			}
			free(pData);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_REQUEST);
		}
#else
		msg.SetVariable(VID_RCC, RCC_NO_ENCRYPTION_SUPPORT);
#endif
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

	sendMessage(&msg);
}


//
// Delete certificate
//

void ClientSession::DeleteCertificate(CSCPMessage *pRequest)
{
   CSCPMessage msg;
#ifdef _WITH_ENCRYPTION
	DWORD dwCertId;
	TCHAR szQuery[256];
#endif

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if ((m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS) &&
	    (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG))
	{
#ifdef _WITH_ENCRYPTION
		dwCertId = pRequest->GetVariableLong(VID_CERTIFICATE_ID);
		_sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM certificates WHERE cert_id=%d"), dwCertId);
		if (DBQuery(g_hCoreDB, szQuery))
		{
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
			ReloadCertificates();
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
#else
		msg.SetVariable(VID_RCC, RCC_NO_ENCRYPTION_SUPPORT);
#endif
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

	sendMessage(&msg);
}


//
// Update certificate's comments
//

void ClientSession::UpdateCertificateComments(CSCPMessage *pRequest)
{
	DWORD dwCertId, qlen;
	TCHAR *pszQuery, *pszComments, *pszEscComments;
	DB_RESULT hResult;
   CSCPMessage msg;

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if ((m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS) &&
	    (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG))
	{
		dwCertId = pRequest->GetVariableLong(VID_CERTIFICATE_ID);
		pszComments= pRequest->GetVariableStr(VID_COMMENTS);
		if (pszComments != NULL)
		{
			pszEscComments = EncodeSQLString(pszComments);
			free(pszComments);
			qlen = _tcslen(pszEscComments) + 256;
			pszQuery = (TCHAR *)malloc(qlen * sizeof(TCHAR));
			_sntprintf(pszQuery, qlen, _T("SELECT subject FROM certificates WHERE cert_id=%d"), dwCertId);
			hResult = DBSelect(g_hCoreDB, pszQuery);
			if (hResult != NULL)
			{
				if (DBGetNumRows(hResult) > 0)
				{
					_sntprintf(pszQuery, qlen, _T("UPDATE certificates SET comments='%s' WHERE cert_id=%d"), pszEscComments, dwCertId);
					if (DBQuery(g_hCoreDB, pszQuery))
					{
						msg.SetVariable(VID_RCC, RCC_SUCCESS);
					}
					else
					{
						msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
					}
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_INVALID_CERT_ID);
				}
				DBFreeResult(hResult);
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
			}
			free(pszEscComments);
			free(pszQuery);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_REQUEST);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

	sendMessage(&msg);
}


//
// Send list of installed certificates to client
//

void ClientSession::SendCertificateList(DWORD dwRqId)
{
   CSCPMessage msg;
	DB_RESULT hResult;
	int i, nRows;
	DWORD dwId;
	TCHAR *pszText;

	msg.SetId(dwRqId);
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if ((m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_USERS) &&
	    (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG))
	{
		hResult = DBSelect(g_hCoreDB, _T("SELECT cert_id,cert_type,comments,subject FROM certificates"));
		if (hResult != NULL)
		{
			nRows = DBGetNumRows(hResult);
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
			msg.SetVariable(VID_NUM_CERTIFICATES, (DWORD)nRows);
			for(i = 0, dwId = VID_CERT_LIST_BASE; i < nRows; i++, dwId += 6)
			{
				msg.SetVariable(dwId++, DBGetFieldULong(hResult, i, 0));
				msg.SetVariable(dwId++, (WORD)DBGetFieldLong(hResult, i, 1));

				pszText = DBGetField(hResult, i, 2, NULL, 0);
				if (pszText != NULL)
				{
					DecodeSQLString(pszText);
					msg.SetVariable(dwId++, pszText);
				}
				else
				{
					msg.SetVariable(dwId++, _T(""));
				}

				pszText = DBGetField(hResult, i, 3, NULL, 0);
				if (pszText != NULL)
				{
					DecodeSQLString(pszText);
					msg.SetVariable(dwId++, pszText);
				}
				else
				{
					msg.SetVariable(dwId++, _T(""));
				}
			}
			DBFreeResult(hResult);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

	sendMessage(&msg);
}


//
// Query layer 2 topology from device
//

void ClientSession::QueryL2Topology(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	NetObj *pObject;
	DWORD dwResult;

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
	if (pObject != NULL)
	{
		if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
		{
			if (pObject->Type() == OBJECT_NODE)
			{
				if (((Node *)pObject)->isBridge())
				{
					nxmap_ObjList *pTopology;

					pTopology = ((Node *)pObject)->GetL2Topology();
					if (pTopology == NULL)
					{
						pTopology = ((Node *)pObject)->BuildL2Topology(&dwResult);
					}
					else
					{
						dwResult = RCC_SUCCESS;
					}
					if (pTopology != NULL)
					{
						msg.SetVariable(VID_RCC, RCC_SUCCESS);
						pTopology->CreateMessage(&msg);
						delete pTopology;
					}
					else
					{
						msg.SetVariable(VID_RCC, dwResult);
					}
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_NO_L2_TOPOLOGY_SUPPORT);
				}
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
	}

	sendMessage(&msg);
}


//
// Send SMS
//

void ClientSession::SendSMS(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	TCHAR phone[256], message[256];

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if ((m_dwSystemAccess & SYSTEM_ACCESS_SEND_SMS) && ConfigReadInt(_T("AllowDirectSMS"), 0))
	{
		pRequest->GetVariableStr(VID_RCPT_ADDR, phone, 256);
		pRequest->GetVariableStr(VID_MESSAGE, message, 256);
		PostSMS(phone, message);
		msg.SetVariable(VID_RCC, RCC_SUCCESS);
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

	sendMessage(&msg);
}


//
// Send SNMP community list
//

void ClientSession::SendCommunityList(DWORD dwRqId)
{
   CSCPMessage msg;
	int i, count;
	DWORD id;
	TCHAR buffer[256];
	DB_RESULT hResult;

	msg.SetId(dwRqId);
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
	{
		hResult = DBSelect(g_hCoreDB, _T("SELECT community FROM snmp_communities"));
		if (hResult != NULL)
		{
			count = DBGetNumRows(hResult);
			msg.SetVariable(VID_NUM_STRINGS, (DWORD)count);
			for(i = 0, id = VID_STRING_LIST_BASE; i < count; i++)
			{
				DBGetField(hResult, i, 0, buffer, 256);
				msg.SetVariable(id++, buffer);
			}
			DBFreeResult(hResult);
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Update SNMP community list
//

void ClientSession::UpdateCommunityList(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	TCHAR value[256], query[1024];
	int i, count;
	DWORD id;

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
	{
		if (DBBegin(g_hCoreDB))
		{
			DBQuery(g_hCoreDB, _T("DELETE FROM snmp_communities"));
			count = pRequest->GetVariableLong(VID_NUM_STRINGS);
			for(i = 0, id = VID_STRING_LIST_BASE; i < count; i++)
			{
				pRequest->GetVariableStr(id++, value, 256);
				String escValue = DBPrepareString(g_hCoreDB, value);
				_sntprintf(query, 1024, _T("INSERT INTO snmp_communities (id,community) VALUES(%d,%s)"), i + 1, (const TCHAR *)escValue);
				if (!DBQuery(g_hCoreDB, query))
					break;
			}

			if (i == count)
			{
				DBCommit(g_hCoreDB);
				msg.SetVariable(VID_RCC, RCC_SUCCESS);
			}
			else
			{
				DBRollback(g_hCoreDB);
				msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Send situation list to client
//

void ClientSession::SendSituationList(DWORD dwRqId)
{
   CSCPMessage msg;

	msg.SetId(dwRqId);
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SITUATIONS)
	{
		MutexLock(m_mutexSendSituations, INFINITE);
		SendSituationListToClient(this, &msg);
		MutexUnlock(m_mutexSendSituations);
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
		sendMessage(&msg);
	}
}


//
// Create new situation
//

void ClientSession::CreateSituation(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   Situation *st;
   TCHAR name[MAX_DB_STRING];

	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);
	
	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SITUATIONS)
	{
		pRequest->GetVariableStr(VID_NAME, name, MAX_DB_STRING);
		st = ::CreateSituation(name);
		if (st != NULL)
		{
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
			msg.SetVariable(VID_SITUATION_ID, st->GetId());
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INTERNAL_ERROR);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Update situation
//

void ClientSession::UpdateSituation(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   Situation *st;
   
	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);
	
	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SITUATIONS)
	{
		st = FindSituationById(pRequest->GetVariableLong(VID_SITUATION_ID));
		if (st != NULL)
		{
			st->UpdateFromMessage(pRequest);
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_SITUATION_ID);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Delete situation
//

void ClientSession::DeleteSituation(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   
	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);
	
	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SITUATIONS)
	{
		msg.SetVariable(VID_RCC, ::DeleteSituation(pRequest->GetVariableLong(VID_SITUATION_ID)));
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Delete situation instance
//

void ClientSession::DeleteSituationInstance(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   Situation *st;
   
	msg.SetId(pRequest->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);
	
	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_SITUATIONS)
	{
		st = FindSituationById(pRequest->GetVariableLong(VID_SITUATION_ID));
		if (st != NULL)
		{
			TCHAR instance[MAX_DB_STRING];
			
			pRequest->GetVariableStr(VID_SITUATION_INSTANCE, instance, MAX_DB_STRING);
			msg.SetVariable(VID_RCC, st->DeleteInstance(instance) ? RCC_SUCCESS : RCC_INSTANCE_NOT_FOUND);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_SITUATION_ID);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Handler for situation chage
//

void ClientSession::onSituationChange(CSCPMessage *msg)
{
   UPDATE_INFO *pUpdate;

   if (isAuthenticated() && (m_dwActiveChannels & NXC_CHANNEL_SITUATIONS))
   {
      pUpdate = (UPDATE_INFO *)malloc(sizeof(UPDATE_INFO));
      pUpdate->dwCategory = INFO_CAT_SITUATION;
      pUpdate->pData = new CSCPMessage(msg);
      m_pUpdateQueue->Put(pUpdate);
   }
}


//
// Add new webmap
//

void ClientSession::WebMapAdd(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR name[64], *props;
   DWORD mapId, rcc;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_MAPS)
	{
		pRequest->GetVariableStr(VID_NAME, name, 64);
		if (IsValidObjectName(name, TRUE))
		{
			props = pRequest->GetVariableStr(VID_PROPERTIES);
			rcc = CreateWebMap(name, props, &mapId);
			msg.SetVariable(VID_RCC, rcc);
			if (rcc == RCC_SUCCESS)
				msg.SetVariable(VID_MAP_ID, mapId);
			safe_free(props);
		}
		else
		{
	      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_NAME);
		}
	}
	else
	{
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Update webmap data
//

void ClientSession::WebMapUpdateData(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR *data;
   DWORD mapId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_MAPS)
	{
		mapId = pRequest->GetVariableLong(VID_MAP_ID);
		data = pRequest->GetVariableStr(VID_MAP_DATA);
	   msg.SetVariable(VID_RCC, UpdateWebMapData(mapId, data));
		free(data);
	}
	else
	{
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Update webmap properties
//

void ClientSession::WebMapUpdateProps(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   TCHAR *props, name[64];
   DWORD mapId;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_MAPS)
	{
		mapId = pRequest->GetVariableLong(VID_MAP_ID);
		props = pRequest->GetVariableStr(VID_PROPERTIES);
		pRequest->GetVariableStr(VID_NAME, name, 64);
	   msg.SetVariable(VID_RCC, UpdateWebMapProperties(mapId, name, props));
		free(props);
	}
	else
	{
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Delete webmap
//

void ClientSession::WebMapDelete(CSCPMessage *pRequest)
{
   CSCPMessage msg;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_MANAGE_MAPS)
	{
		msg.SetVariable(VID_RCC, DeleteWebMap(pRequest->GetVariableLong(VID_MAP_ID)));
	}
	else
	{
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Get webmap's data
//

void ClientSession::WebMapGetData(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   DWORD mapId;
	TCHAR *data, query[256];
	DB_RESULT hResult;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	mapId = pRequest->GetVariableLong(VID_MAP_ID);
	_sntprintf(query, 256, _T("SELECT data FROM web_maps WHERE id=%d"), mapId);
	hResult = DBSelect(g_hCoreDB, query);
	if (hResult != NULL)
	{
		if (DBGetNumRows(hResult) > 0)
		{
			data = DBGetField(hResult, 0, 0, NULL, 0);
			if (data != NULL)
				DecodeSQLString(data);
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
			msg.SetVariable(VID_MAP_DATA, CHECK_NULL_EX(data));
			safe_free(data);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_MAP_ID);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
	}

   sendMessage(&msg);
}


//
// Get list of webmaps
//

void ClientSession::WebMapGetList(DWORD dwRqId)
{
   CSCPMessage msg;
	DB_RESULT hResult;
	DWORD i, id, count;
	TCHAR name[256], *props;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(dwRqId);

	hResult = DBSelect(g_hCoreDB, _T("SELECT id,title,properties FROM web_maps"));
	if (hResult != NULL)
	{
		count = DBGetNumRows(hResult);
		msg.SetVariable(VID_NUM_MAPS, count);
		for(i = 0, id = VID_WEBMAP_LIST_BASE; i < count; i++)
		{
			msg.SetVariable(id++, DBGetFieldULong(hResult, i, 0));
			DBGetField(hResult, i, 1, name, 256);
			msg.SetVariable(id++, name);
			props = DBGetField(hResult, i, 2, NULL, 0);
			if (props != NULL)
			{
				DecodeSQLString(props);
				msg.SetVariable(id++, props);
				free(props);
			}
			else
			{
				msg.SetVariable(id++, _T(""));
			}
			id += 7;
		}
		msg.SetVariable(VID_RCC, RCC_SUCCESS);
		DBFreeResult(hResult);
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
	}

   sendMessage(&msg);
}


//
// Register agent
//

void ClientSession::registerAgent(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	Node *node;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_REGISTER_AGENTS)
	{
		if (ConfigReadInt(_T("EnableAgentRegistration"), 0))
		{
			node = FindNodeByIP(ntohl(m_dwHostAddr));
			if (node != NULL)
			{
				// Node already exist, force configuration poll
				node->setRecheckCapsFlag();
				node->forceConfigurationPoll();
			}
			else
			{
				NEW_NODE *info;

				info = (NEW_NODE *)malloc(sizeof(NEW_NODE));
				info->dwIpAddr = ntohl(m_dwHostAddr);
				info->dwNetMask = 0;
				info->ignoreFilter = TRUE;		// Ignore discovery filters and add node anyway
				g_nodePollerQueue.Put(info);
			}
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
		}
		else
		{
	      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
		}
	}
	else
	{
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Get file from server
//

void ClientSession::getServerFile(CSCPMessage *pRequest)
{
   CSCPMessage msg;
	TCHAR name[MAX_PATH], fname[MAX_PATH];
	int i;

   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

	if (m_dwSystemAccess & SYSTEM_ACCESS_READ_FILES)
	{
		pRequest->GetVariableStr(VID_FILE_NAME, name, MAX_PATH);
		for(i = (int)_tcslen(name) - 1; i >= 0; i--)
			if ((name[i] == _T('\\')) || (name[i] == '/'))
				break;
		i++;
      _tcscpy(fname, g_szDataDir);
      _tcscat(fname, DDIR_SHARED_FILES);
      _tcscat(fname, FS_PATH_SEPARATOR);
      _tcscat(fname, name);
		DebugPrintf(4, _T("Requested file %s"), name);
		if (_taccess(fname, 0) == 0)
		{
			DebugPrintf(5, _T("Sending file %s"), name);
			if (SendFileOverNXCP(m_hSocket, pRequest->GetId(), fname, m_pCtx, 0, NULL, NULL))
			{
				DebugPrintf(5, _T("File %s was succesfully sent"), name);
		      msg.SetVariable(VID_RCC, RCC_SUCCESS);
			}
			else
			{
				DebugPrintf(5, _T("Unable to send file %s: SendFileOverNXCP() failed"), name);
		      msg.SetVariable(VID_RCC, RCC_IO_ERROR);
			}
		}
		else
		{
			DebugPrintf(5, _T("Unable to send file %s: access() failed"), name);
	      msg.SetVariable(VID_RCC, RCC_IO_ERROR);
		}
	}
	else
	{
      msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}

   sendMessage(&msg);
}


//
// Test DCI transformation script
//

void ClientSession::testDCITransformation(CSCPMessage *pRequest)
{
   CSCPMessage msg;
   NetObj *pObject;

   // Prepare response message
   msg.SetCode(CMD_REQUEST_COMPLETED);
   msg.SetId(pRequest->GetId());

   // Get node id and check object class and access rights
   pObject = FindObjectById(pRequest->GetVariableLong(VID_OBJECT_ID));
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
      {
         if (pObject->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
         {
				DCItem *dci;

				dci = ((Node *)pObject)->getItemById(pRequest->GetVariableLong(VID_DCI_ID));
				if (dci != NULL)
				{
					BOOL success;
					TCHAR *script, value[256], result[256];

					script = pRequest->GetVariableStr(VID_SCRIPT);
					if (script != NULL)
					{
						pRequest->GetVariableStr(VID_VALUE, value, sizeof(value) / sizeof(TCHAR));
						success = dci->testTransformation(script, value, result, sizeof(result) / sizeof(TCHAR));
						free(script);
						msg.SetVariable(VID_RCC, RCC_SUCCESS);
						msg.SetVariable(VID_EXECUTION_STATUS, (WORD)success);
						msg.SetVariable(VID_EXECUTION_RESULT, result);
					}
					else
					{
		            msg.SetVariable(VID_RCC, RCC_INVALID_ARGUMENT);
					}
				}
				else
				{
	            msg.SetVariable(VID_RCC, RCC_INVALID_DCI_ID);
				}
         }
         else  // User doesn't have READ rights on object
         {
            msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
         }
      }
      else     // Object is not a node
      {
         msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
      }
   }
   else  // No object with given ID
   {
      msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
   }

   // Send response
   sendMessage(&msg);
}


//
// Send list of server jobs
//

void ClientSession::sendJobList(DWORD dwRqId)
{
	CSCPMessage msg;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(dwRqId);
	msg.SetVariable(VID_RCC, RCC_SUCCESS);
	GetJobList(&msg);
	sendMessage(&msg);
}


//
// Cancel server job
//

void ClientSession::cancelJob(CSCPMessage *pRequest)
{
	CSCPMessage msg;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(pRequest->GetId());
	msg.SetVariable(VID_RCC, ::CancelJob(m_dwUserId, pRequest));
	sendMessage(&msg);
}


//
// Deploy agent policy
//

void ClientSession::deployAgentPolicy(CSCPMessage *request, bool uninstallFlag)
{
	CSCPMessage msg;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(request->GetId());

	DWORD policyId = request->GetVariableLong(VID_POLICY_ID);
	DWORD targetId = request->GetVariableLong(VID_OBJECT_ID);

	NetObj *policy = FindObjectById(policyId);
	if ((policy != NULL) && (policy->Type() >= OBJECT_AGENTPOLICY))
	{
		NetObj *target = FindObjectById(targetId);
		if ((target != NULL) && (target->Type() == OBJECT_NODE))
		{
			if (target->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_CONTROL) &&
			    policy->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
			{
				if (((Node *)target)->isNativeAgent())
				{
					ServerJob *job;
					if (uninstallFlag)
						job = new PolicyUninstallJob((Node *)target, (AgentPolicy *)policy, m_dwUserId);
					else
						job = new PolicyDeploymentJob((Node *)target, (AgentPolicy *)policy, m_dwUserId);
					if (AddJob(job))
					{
						msg.SetVariable(VID_RCC, RCC_SUCCESS);
					}
					else
					{
						delete job;
						msg.SetVariable(VID_RCC, RCC_INTERNAL_ERROR);
					}
				}
				else
				{
					msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
				}
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_INVALID_POLICY_ID);
	}

	sendMessage(&msg);
}


//
// Get custom attribute for current user
//

void ClientSession::getUserCustomAttribute(CSCPMessage *request)
{
	CSCPMessage msg;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(request->GetId());

	TCHAR *name = request->GetVariableStr(VID_NAME);
	if ((name != NULL) && (*name == _T('.')))
	{
		const TCHAR *value = GetUserDbObjectAttr(m_dwUserId, name);
		msg.SetVariable(VID_VALUE, CHECK_NULL_EX(value));
		msg.SetVariable(VID_RCC, RCC_SUCCESS);
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	safe_free(name);

	sendMessage(&msg);
}


//
// Set custom attribute for current user
//

void ClientSession::setUserCustomAttribute(CSCPMessage *request)
{
	CSCPMessage msg;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(request->GetId());

	TCHAR *name = request->GetVariableStr(VID_NAME);
	if ((name != NULL) && (*name == _T('.')))
	{
		TCHAR *value = request->GetVariableStr(VID_VALUE);
		SetUserDbObjectAttr(m_dwUserId, name, CHECK_NULL_EX(value));
		msg.SetVariable(VID_RCC, RCC_SUCCESS);
		safe_free(value);
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	safe_free(name);

	sendMessage(&msg);
}


//
// Open server log
//

void ClientSession::openServerLog(CSCPMessage *request)
{
	CSCPMessage msg;
	TCHAR name[256];
	DWORD rcc;
	DWORD handle;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(request->GetId());

	request->GetVariableStr(VID_LOG_NAME, name, 256);
	handle = OpenLog(name, this, &rcc);
	if (handle != -1)
	{
		msg.SetVariable(VID_RCC, RCC_SUCCESS);
		msg.SetVariable(VID_LOG_HANDLE, (DWORD)handle);

		LogHandle *log = AcquireLogHandleObject(this, handle);
		log->getColumnInfo(msg);
		log->unlock();
	}
	else
	{
		msg.SetVariable(VID_RCC, rcc);
	}

	sendMessage(&msg);
}


//
// Close server log
//

void ClientSession::closeServerLog(CSCPMessage *request)
{
	CSCPMessage msg;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(request->GetId());

	int handle = (int)request->GetVariableLong(VID_LOG_HANDLE);
	msg.SetVariable(VID_RCC, CloseLog(this, handle));

	sendMessage(&msg);
}


//
// Query server log
//

void ClientSession::queryServerLog(CSCPMessage *request)
{
	CSCPMessage msg;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(request->GetId());

	int handle = (int)request->GetVariableLong(VID_LOG_HANDLE);
	LogHandle *log = AcquireLogHandleObject(this, handle);
	if (log != NULL)
	{
		INT64 rowCount;
		msg.SetVariable(VID_RCC, log->query(new LogFilter(request), &rowCount) ? RCC_SUCCESS : RCC_DB_FAILURE);
		msg.SetVariable(VID_NUM_ROWS, (QWORD)rowCount);
		log->unlock();
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_INVALID_LOG_HANDLE);
	}

	sendMessage(&msg);
}


//
// Get log data from query result
//

void ClientSession::getServerLogQueryData(CSCPMessage *request)
{
	CSCPMessage msg;
	Table *data = NULL;

	msg.SetCode(CMD_REQUEST_COMPLETED);
	msg.SetId(request->GetId());

	int handle = (int)request->GetVariableLong(VID_LOG_HANDLE);
	LogHandle *log = AcquireLogHandleObject(this, handle);
	if (log != NULL)
	{
		INT64 startRow = request->GetVariableInt64(VID_START_ROW);
		INT64 numRows = request->GetVariableInt64(VID_NUM_ROWS);
		data = log->getData(startRow, numRows);
		log->unlock();
		if (data != NULL)
		{
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_INVALID_LOG_HANDLE);
	}

	sendMessage(&msg);

	if (data != NULL)
	{
		msg.SetCode(CMD_LOG_DATA);
		int offset = 0;
		do
		{
			msg.DeleteAllVariables();
			offset = data->fillMessage(msg, offset, 200);
			sendMessage(&msg);
		} while(offset < data->getNumRows());
		delete data;
	}
}


//
// Send SNMP v3 USM credentials
//

void ClientSession::sendUsmCredentials(DWORD dwRqId)
{
   CSCPMessage msg;
	int i, count;
	DWORD id;
	TCHAR buffer[MAX_DB_STRING];
	DB_RESULT hResult;

	msg.SetId(dwRqId);
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
	{
		hResult = DBSelect(g_hCoreDB, _T("SELECT user_name,auth_method,priv_method,auth_password,priv_password FROM usm_credentials"));
		if (hResult != NULL)
		{
			count = DBGetNumRows(hResult);
			msg.SetVariable(VID_NUM_RECORDS, (DWORD)count);
			for(i = 0, id = VID_USM_CRED_LIST_BASE; i < count; i++, id += 5)
			{
				DBGetField(hResult, i, 0, buffer, MAX_DB_STRING);	// security name
				msg.SetVariable(id++, buffer);

				msg.SetVariable(id++, (WORD)DBGetFieldLong(hResult, i, 1));	// auth method
				msg.SetVariable(id++, (WORD)DBGetFieldLong(hResult, i, 2));	// priv method

				DBGetField(hResult, i, 0, buffer, MAX_DB_STRING);	// auth password
				msg.SetVariable(id++, buffer);

				DBGetField(hResult, i, 0, buffer, MAX_DB_STRING);	// priv password
				msg.SetVariable(id++, buffer);
			}
			DBFreeResult(hResult);
			msg.SetVariable(VID_RCC, RCC_SUCCESS);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Update SNMP v3 USM credentials
//

void ClientSession::updateUsmCredentials(CSCPMessage *request)
{
   CSCPMessage msg;

	msg.SetId(request->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	if (m_dwSystemAccess & SYSTEM_ACCESS_SERVER_CONFIG)
	{
		DB_HANDLE hdb = DBConnectionPoolAcquireConnection();
		if (hdb != NULL)
		{
			if (DBBegin(hdb))
			{
				TCHAR query[4096];
				DWORD id;
				int i = -1;
				int count = (int)request->GetVariableLong(VID_NUM_RECORDS);

				if (DBQuery(hdb, _T("DELETE FROM usm_credentials")))
				{
					for(i = 0, id = VID_USM_CRED_LIST_BASE; i < count; i++, id += 5)
					{
						TCHAR name[MAX_DB_STRING], authPasswd[MAX_DB_STRING], privPasswd[MAX_DB_STRING];

						request->GetVariableStr(id++, name, MAX_DB_STRING);
						int authMethod = (int)request->GetVariableShort(id++);
						int privMethod = (int)request->GetVariableShort(id++);
						request->GetVariableStr(id++, authPasswd, MAX_DB_STRING);
						request->GetVariableStr(id++, privPasswd, MAX_DB_STRING);
						_sntprintf(query, 4096, _T("INSERT INTO usm_credentials (id,user_name,auth_method,priv_method,auth_password,priv_password) VALUES(%d,%s,%d,%d,%s,%s)"),
									  i + 1, (const TCHAR *)DBPrepareString(g_hCoreDB, name), authMethod, privMethod,
									  (const TCHAR *)DBPrepareString(g_hCoreDB, authPasswd), (const TCHAR *)DBPrepareString(g_hCoreDB, privPasswd));
						if (!DBQuery(hdb, query))
							break;
					}
				}

				if (i == count)
				{
					DBCommit(hdb);
					msg.SetVariable(VID_RCC, RCC_SUCCESS);
				}
				else
				{
					DBRollback(hdb);
					msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
				}
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
			}
			DBConnectionPoolReleaseConnection(hdb);
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_DB_FAILURE);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
	}
	
	sendMessage(&msg);
}


//
// Find connection port for the node
//

void ClientSession::findNodeConnection(CSCPMessage *request)
{
   CSCPMessage msg;

	msg.SetId(request->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	DWORD objectId = request->GetVariableLong(VID_OBJECT_ID);
	NetObj *object = FindObjectById(objectId);
	if ((object != NULL) && !object->IsDeleted())
	{
		if (object->CheckAccessRights(m_dwUserId, OBJECT_ACCESS_READ))
		{
			DebugPrintf(5, _T("findNodeConnection: objectId=%d class=%d name=\"%s\""), objectId, object->Type(), object->Name());
			Interface *iface = NULL;
			DWORD localNodeId, localIfId;
			if (object->Type() == OBJECT_NODE)
			{
				localNodeId = objectId;
				iface = ((Node *)object)->findConnectionPoint(&localIfId);
				msg.SetVariable(VID_RCC, RCC_SUCCESS);
			}
			else if (object->Type() == OBJECT_INTERFACE)
			{
				localNodeId = ((Interface *)object)->getParentNode()->Id();
				localIfId = objectId;
				iface = FindInterfaceConnectionPoint(((Interface *)object)->MacAddr());
				msg.SetVariable(VID_RCC, RCC_SUCCESS);
			}
			else
			{
				msg.SetVariable(VID_RCC, RCC_INCOMPATIBLE_OPERATION);
			}

			DebugPrintf(5, _T("findNodeConnection: iface=%p"), iface);
			if (iface != NULL)
			{
				msg.SetVariable(VID_OBJECT_ID, iface->getParentNode()->Id());
				msg.SetVariable(VID_INTERFACE_ID, iface->Id());
				msg.SetVariable(VID_IF_INDEX, iface->IfIndex());
				msg.SetVariable(VID_LOCAL_NODE_ID, localNodeId);
				msg.SetVariable(VID_LOCAL_INTERFACE_ID, localIfId);
				DebugPrintf(5, _T("findNodeConnection: nodeId=%d ifId=%d ifIndex=%d"), iface->getParentNode()->Id(), iface->Id(), iface->IfIndex());
			}
		}
		else
		{
			msg.SetVariable(VID_RCC, RCC_ACCESS_DENIED);
		}
	}
	else
	{
		msg.SetVariable(VID_RCC, RCC_INVALID_OBJECT_ID);
	}
	
	sendMessage(&msg);
}


//
// Find connection port for given MAC address
//

void ClientSession::findMacAddress(CSCPMessage *request)
{
   CSCPMessage msg;
	BYTE macAddr[6];

	msg.SetId(request->GetId());
	msg.SetCode(CMD_REQUEST_COMPLETED);

	request->GetVariableBinary(VID_MAC_ADDR, macAddr, 6);
	Interface *iface = FindInterfaceConnectionPoint(macAddr);
	msg.SetVariable(VID_RCC, RCC_SUCCESS);

	DebugPrintf(5, _T("findMacAddress: iface=%p"), iface);
	if (iface != NULL)
	{
		DWORD localNodeId, localIfId;

		Interface *localIf = FindInterfaceByMAC(macAddr);
		if (localIf != NULL)
		{
			localIfId = localIf->Id();
			localNodeId = localIf->getParentNode()->Id();
		}
		else
		{
			localIfId = 0;
			localNodeId = 0;
		}

		msg.SetVariable(VID_OBJECT_ID, iface->getParentNode()->Id());
		msg.SetVariable(VID_INTERFACE_ID, iface->Id());
		msg.SetVariable(VID_IF_INDEX, iface->IfIndex());
		msg.SetVariable(VID_LOCAL_NODE_ID, localNodeId);
		msg.SetVariable(VID_LOCAL_INTERFACE_ID, localIfId);
		DebugPrintf(5, _T("findMacAddress: nodeId=%d ifId=%d ifIndex=%d"), iface->getParentNode()->Id(), iface->Id(), iface->IfIndex());
	}
	
	sendMessage(&msg);
}
