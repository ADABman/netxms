/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2011 Victor Kirhenshtein
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
** File: client.cpp
**
**/

#include "nxcore.h"


//
// Constants
//

#define MAX_CLIENT_SESSIONS   128


//
// Static data
//

static ClientSession *m_pSessionList[MAX_CLIENT_SESSIONS];
static RWLOCK m_rwlockSessionListAccess;


//
// Register new session in list
//

static BOOL RegisterSession(ClientSession *pSession)
{
   DWORD i;

   RWLockWriteLock(m_rwlockSessionListAccess, INFINITE);
   for(i = 0; i < MAX_CLIENT_SESSIONS; i++)
      if (m_pSessionList[i] == NULL)
      {
         m_pSessionList[i] = pSession;
         pSession->setIndex(i);
         RWLockUnlock(m_rwlockSessionListAccess);
         return TRUE;
      }

   RWLockUnlock(m_rwlockSessionListAccess);
   nxlog_write(MSG_TOO_MANY_SESSIONS, EVENTLOG_WARNING_TYPE, NULL);
   return FALSE;
}


//
// Unregister session
//

void UnregisterSession(DWORD dwIndex)
{
   RWLockWriteLock(m_rwlockSessionListAccess, INFINITE);
   m_pSessionList[dwIndex] = NULL;
   RWLockUnlock(m_rwlockSessionListAccess);
}


//
// Keep-alive thread
//

static THREAD_RESULT THREAD_CALL ClientKeepAliveThread(void *)
{
   int i, iSleepTime;
   CSCPMessage msg;

   // Read configuration
   iSleepTime = ConfigReadInt(_T("KeepAliveInterval"), 60);

   // Prepare keepalive message
   msg.SetCode(CMD_KEEPALIVE);
   msg.SetId(0);

   while(1)
   {
      if (SleepAndCheckForShutdown(iSleepTime))
         break;

      msg.SetVariable(VID_TIMESTAMP, (DWORD)time(NULL));
      RWLockReadLock(m_rwlockSessionListAccess, INFINITE);
      for(i = 0; i < MAX_CLIENT_SESSIONS; i++)
         if (m_pSessionList[i] != NULL)
            if (m_pSessionList[i]->isAuthenticated())
               m_pSessionList[i]->sendMessage(&msg);
      RWLockUnlock(m_rwlockSessionListAccess);
   }

   DbgPrintf(1, _T("Client keep-alive thread terminated"));
   return THREAD_OK;
}


//
// Initialize client listener(s)
//

void InitClientListeners()
{
   // Create session list access rwlock
   m_rwlockSessionListAccess = RWLockCreate();

   // Start client keep-alive thread
   ThreadCreate(ClientKeepAliveThread, 0, NULL);
}


//
// Listener thread
//

THREAD_RESULT THREAD_CALL ClientListener(void *arg)
{
   SOCKET sock, sockClient;
   struct sockaddr_in servAddr;
   int errorCount = 0;
   socklen_t iSize;
   WORD wListenPort;
   ClientSession *pSession;

   // Read configuration
   wListenPort = (WORD)ConfigReadInt(_T("ClientListenerPort"), SERVER_LISTEN_PORT);

   // Create socket
   if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      nxlog_write(MSG_SOCKET_FAILED, EVENTLOG_ERROR_TYPE, "s", _T("ClientListener"));
      return THREAD_OK;
   }

	SetSocketExclusiveAddrUse(sock);
	SetSocketReuseFlag(sock);

   // Fill in local address structure
   memset(&servAddr, 0, sizeof(struct sockaddr_in));
   servAddr.sin_family = AF_INET;
   servAddr.sin_addr.s_addr = ResolveHostName(g_szListenAddress);
   servAddr.sin_port = htons(wListenPort);

   // Bind socket
   if (bind(sock, (struct sockaddr *)&servAddr, sizeof(struct sockaddr_in)) != 0)
   {
      nxlog_write(MSG_BIND_ERROR, EVENTLOG_ERROR_TYPE, "dse", wListenPort, _T("ClientListener"), WSAGetLastError());
      closesocket(sock);
      /* TODO: we should initiate shutdown procedure here */
      return THREAD_OK;
   }

   // Set up queue
   listen(sock, SOMAXCONN);
	nxlog_write(MSG_LISTENING_FOR_CLIENTS, EVENTLOG_INFORMATION_TYPE, "ad", ntohl(servAddr.sin_addr.s_addr), wListenPort);

   // Wait for connection requests
   while(!IsShutdownInProgress())
   {
      iSize = sizeof(struct sockaddr_in);
      if ((sockClient = accept(sock, (struct sockaddr *)&servAddr, &iSize)) == -1)
      {
         int error;

#ifdef _WIN32
         error = WSAGetLastError();
         if (error != WSAEINTR)
#else
         error = errno;
         if (error != EINTR)
#endif
            nxlog_write(MSG_ACCEPT_ERROR, EVENTLOG_ERROR_TYPE, "e", error);
         errorCount++;
         if (errorCount > 1000)
         {
            nxlog_write(MSG_TOO_MANY_ACCEPT_ERRORS, EVENTLOG_WARNING_TYPE, NULL);
            errorCount = 0;
         }
         ThreadSleepMs(500);
      }

      errorCount = 0;     // Reset consecutive errors counter

      // Create new session structure and threads
      pSession = new ClientSession(sockClient, (struct sockaddr *)&servAddr);
      if (!RegisterSession(pSession))
      {
         delete pSession;
      }
      else
      {
         pSession->run();
      }
   }

   closesocket(sock);
   return THREAD_OK;
}


//
// Listener thread - IPv6
//

#ifdef WITH_IPV6

THREAD_RESULT THREAD_CALL ClientListenerIPv6(void *arg)
{
   SOCKET sock, sockClient;
   struct sockaddr_in6 servAddr;
   int errorCount = 0;
   socklen_t iSize;
   WORD wListenPort;
   ClientSession *pSession;

   // Read configuration
   wListenPort = (WORD)ConfigReadInt(_T("ClientListenerPort"), SERVER_LISTEN_PORT);

   // Create socket
   if ((sock = socket(AF_INET6, SOCK_STREAM, 0)) == -1)
   {
      nxlog_write(MSG_SOCKET_FAILED, EVENTLOG_ERROR_TYPE, "s", _T("ClientListener"));
      return THREAD_OK;
   }

	SetSocketExclusiveAddrUse(sock);
	SetSocketReuseFlag(sock);

   // Fill in local address structure
   memset(&servAddr, 0, sizeof(struct sockaddr_in6));
   servAddr.sin6_family = AF_INET6;
   //servAddr.sin6_addr.s6_addr = ResolveHostName(g_szListenAddress);
   servAddr.sin6_port = htons(wListenPort);

   // Bind socket
   if (bind(sock, (struct sockaddr *)&servAddr, sizeof(struct sockaddr_in6)) != 0)
   {
      nxlog_write(MSG_BIND_ERROR, EVENTLOG_ERROR_TYPE, "dse", wListenPort, _T("ClientListener"), WSAGetLastError());
      closesocket(sock);
      /* TODO: we should initiate shutdown procedure here */
      return THREAD_OK;
   }

   // Set up queue
   listen(sock, SOMAXCONN);
	nxlog_write(MSG_LISTENING_FOR_CLIENTS, EVENTLOG_INFORMATION_TYPE, "Ad", servAddr.sin6_addr.s6_addr, wListenPort);

   // Wait for connection requests
   while(!IsShutdownInProgress())
   {
      iSize = sizeof(struct sockaddr_in6);
      if ((sockClient = accept(sock, (struct sockaddr *)&servAddr, &iSize)) == -1)
      {
         int error;

#ifdef _WIN32
         error = WSAGetLastError();
         if (error != WSAEINTR)
#else
         error = errno;
         if (error != EINTR)
#endif
            nxlog_write(MSG_ACCEPT_ERROR, EVENTLOG_ERROR_TYPE, "e", error);
         errorCount++;
         if (errorCount > 1000)
         {
            nxlog_write(MSG_TOO_MANY_ACCEPT_ERRORS, EVENTLOG_WARNING_TYPE, NULL);
            errorCount = 0;
         }
         ThreadSleepMs(500);
      }

      errorCount = 0;     // Reset consecutive errors counter

      // Create new session structure and threads
      pSession = new ClientSession(sockClient, (struct sockaddr *)&servAddr);
      if (!RegisterSession(pSession))
      {
         delete pSession;
      }
      else
      {
         pSession->run();
      }
   }

   closesocket(sock);
   return THREAD_OK;
}

#endif


//
// Dump client sessions to screen
//

void DumpSessions(CONSOLE_CTX pCtx)
{
   int i, iCount;
   TCHAR szBuffer[256];
   static const TCHAR *pszStateName[] = { _T("init"), _T("idle"), _T("processing") };
   static const TCHAR *pszCipherName[] = { _T("NONE"), _T("AES-256"), _T("BLOWFISH"), _T("IDEA"), _T("3DES") };

   ConsolePrintf(pCtx, _T("ID  STATE                    CIPHER   USER [CLIENT]\n"));
   RWLockReadLock(m_rwlockSessionListAccess, INFINITE);
   for(i = 0, iCount = 0; i < MAX_CLIENT_SESSIONS; i++)
      if (m_pSessionList[i] != NULL)
      {
         ConsolePrintf(pCtx, _T("%-3d %-24s %-8s %s [%s]\n"), i, 
                       (m_pSessionList[i]->getState() != SESSION_STATE_PROCESSING) ?
                         pszStateName[m_pSessionList[i]->getState()] :
                         NXCPMessageCodeName(m_pSessionList[i]->getCurrentCmd(), szBuffer),
					        pszCipherName[m_pSessionList[i]->getCipher() + 1],
                       m_pSessionList[i]->getUserName(),
                       m_pSessionList[i]->getClientInfo());
         iCount++;
      }
   RWLockUnlock(m_rwlockSessionListAccess);
   ConsolePrintf(pCtx, _T("\n%d active session%s\n\n"), iCount, iCount == 1 ? _T("") : _T("s"));
}


//
// Enumerate active sessions
//

void NXCORE_EXPORTABLE EnumerateClientSessions(void (*pHandler)(ClientSession *, void *), void *pArg)
{
   int i;

   RWLockReadLock(m_rwlockSessionListAccess, INFINITE);
   for(i = 0; i < MAX_CLIENT_SESSIONS; i++)
      if (m_pSessionList[i] != NULL)
         pHandler(m_pSessionList[i], pArg);
   RWLockUnlock(m_rwlockSessionListAccess);
}


//
// Send user database update notification to all clients
//

void SendUserDBUpdate(int code, DWORD id, UserDatabaseObject *object)
{
   int i;

   RWLockReadLock(m_rwlockSessionListAccess, INFINITE);
   for(i = 0; i < MAX_CLIENT_SESSIONS; i++)
      if (m_pSessionList[i] != NULL)
         m_pSessionList[i]->onUserDBUpdate(code, id, object);
   RWLockUnlock(m_rwlockSessionListAccess);
}


//
// Send notification to all active sessions
//

void NXCORE_EXPORTABLE NotifyClientSessions(DWORD dwCode, DWORD dwData)
{
   int i;

   RWLockReadLock(m_rwlockSessionListAccess, INFINITE);
   for(i = 0; i < MAX_CLIENT_SESSIONS; i++)
      if (m_pSessionList[i] != NULL)
         m_pSessionList[i]->notify(dwCode, dwData);
   RWLockUnlock(m_rwlockSessionListAccess);
}


//
// Get number of active sessions
//

int GetSessionCount()
{
   int i, nCount;

   RWLockReadLock(m_rwlockSessionListAccess, INFINITE);
   for(i = 0, nCount = 0; i < MAX_CLIENT_SESSIONS; i++)
      if (m_pSessionList[i] != NULL)
         nCount++;
   RWLockUnlock(m_rwlockSessionListAccess);
   return nCount;
}
