/* 
** NetXMS - Network Management System
** Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Victor Kirhenshtein
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
** File: snmptrap.cpp
**
**/

#include "nxcore.h"


//
// Constants
//

#define MAX_PACKET_LENGTH     65536


//
// Static data
//

static MUTEX m_mutexTrapCfgAccess = NULL;
static NXC_TRAP_CFG_ENTRY *m_pTrapCfg = NULL;
static DWORD m_dwNumTraps = 0;
static BOOL m_bLogAllTraps = FALSE;
static INT64 m_qnTrapId = 1;


//
// Load trap configuration from database
//

static BOOL LoadTrapCfg(void)
{
   DB_RESULT hResult;
   BOOL bResult = TRUE;
   TCHAR *pszOID, szQuery[256], szBuffer[MAX_DB_STRING];
   DWORD i, j, pdwBuffer[MAX_OID_LEN];

   // Load traps
   hResult = DBSelect(g_hCoreDB, _T("SELECT trap_id,snmp_oid,event_code,description,user_tag FROM snmp_trap_cfg"));
   if (hResult != NULL)
   {
      m_dwNumTraps = DBGetNumRows(hResult);
      m_pTrapCfg = (NXC_TRAP_CFG_ENTRY *)malloc(sizeof(NXC_TRAP_CFG_ENTRY) * m_dwNumTraps);
      memset(m_pTrapCfg, 0, sizeof(NXC_TRAP_CFG_ENTRY) * m_dwNumTraps);
      for(i = 0; i < m_dwNumTraps; i++)
      {
         m_pTrapCfg[i].dwId = DBGetFieldULong(hResult, i, 0);
         m_pTrapCfg[i].dwOidLen = SNMPParseOID(DBGetField(hResult, i, 1, szBuffer, MAX_DB_STRING),
                                               pdwBuffer, MAX_OID_LEN);
         if (m_pTrapCfg[i].dwOidLen > 0)
         {
            m_pTrapCfg[i].pdwObjectId = (DWORD *)nx_memdup(pdwBuffer, m_pTrapCfg[i].dwOidLen * sizeof(DWORD));
         }
         else
         {
            nxlog_write(MSG_INVALID_TRAP_OID, EVENTLOG_ERROR_TYPE, _T("s"),
                        DBGetField(hResult, i, 1, szBuffer, MAX_DB_STRING));
            bResult = FALSE;
         }
         m_pTrapCfg[i].dwEventCode = DBGetFieldULong(hResult, i, 2);
         DBGetField(hResult, i, 3, m_pTrapCfg[i].szDescription, MAX_DB_STRING);
         DecodeSQLString(m_pTrapCfg[i].szDescription);
         DBGetField(hResult, i, 4, m_pTrapCfg[i].szUserTag, MAX_USERTAG_LENGTH);
         DecodeSQLString(m_pTrapCfg[i].szUserTag);
      }
      DBFreeResult(hResult);

      // Load parameter mappings
      for(i = 0; i < m_dwNumTraps; i++)
      {
         _sntprintf(szQuery, 256, _T("SELECT snmp_oid,description FROM snmp_trap_pmap ")
                                  _T("WHERE trap_id=%d ORDER BY parameter"),
                    m_pTrapCfg[i].dwId);
         hResult = DBSelect(g_hCoreDB, szQuery);
         if (hResult != NULL)
         {
            m_pTrapCfg[i].dwNumMaps = DBGetNumRows(hResult);
            m_pTrapCfg[i].pMaps = (NXC_OID_MAP *)malloc(sizeof(NXC_OID_MAP) * m_pTrapCfg[i].dwNumMaps);
            for(j = 0; j < m_pTrapCfg[i].dwNumMaps; j++)
            {
               pszOID = DBGetField(hResult, j, 0, szBuffer, MAX_DB_STRING);
               if (!_tcsncmp(pszOID, _T("POS:"), 4))
               {
                  m_pTrapCfg[i].pMaps[j].dwOidLen = _tcstoul(&pszOID[4], NULL, 10) | 0x80000000;
                  m_pTrapCfg[i].pMaps[j].pdwObjectId = NULL;
               }
               else
               {
                  m_pTrapCfg[i].pMaps[j].dwOidLen = SNMPParseOID(pszOID, pdwBuffer, MAX_OID_LEN);
                  if (m_pTrapCfg[i].pMaps[j].dwOidLen > 0)
                  {
                     m_pTrapCfg[i].pMaps[j].pdwObjectId = 
                        (DWORD *)nx_memdup(pdwBuffer, m_pTrapCfg[i].pMaps[j].dwOidLen * sizeof(DWORD));
                  }
                  else
                  {
                     nxlog_write(MSG_INVALID_TRAP_ARG_OID, EVENTLOG_ERROR_TYPE, _T("sd"), 
                              DBGetField(hResult, j, 0, szBuffer, MAX_DB_STRING), m_pTrapCfg[i].dwId);
                     bResult = FALSE;
                  }
               }
               DBGetField(hResult, j, 1, m_pTrapCfg[i].pMaps[j].szDescription, MAX_DB_STRING);
               DecodeSQLString(m_pTrapCfg[i].pMaps[j].szDescription);
            }
            DBFreeResult(hResult);
         }
         else
         {
            bResult = FALSE;
         }
      }
   }
   else
   {
      bResult = FALSE;
   }
   return bResult;
}


//
// Initialize trap handling
//

void InitTraps(void)
{
   DB_RESULT hResult;

   m_mutexTrapCfgAccess = MutexCreate();
   LoadTrapCfg();
   m_bLogAllTraps = ConfigReadInt(_T("LogAllSNMPTraps"), FALSE);

   hResult = DBSelect(g_hCoreDB, "SELECT max(trap_id) FROM snmp_trap_log");
   if (hResult != NULL)
   {
      if (DBGetNumRows(hResult) > 0)
         m_qnTrapId = DBGetFieldInt64(hResult, 0, 0) + 1;
      DBFreeResult(hResult);
   }
}


//
// Generate event for matched trap
//

static void GenerateTrapEvent(DWORD dwObjectId, DWORD dwIndex, SNMP_PDU *pdu)
{
   TCHAR *pszArgList[32], szBuffer[256];
   char szFormat[] = "sssssssssssssssssssssssssssssssss";
   DWORD i, j;
   SNMP_Variable *pVar;
   int iResult;

	// Extract varbinds from trap and add them as event's parameters
   for(i = 0; i < m_pTrapCfg[dwIndex].dwNumMaps; i++)
   {
      if (m_pTrapCfg[dwIndex].pMaps[i].dwOidLen & 0x80000000)
      {
			// Extract by varbind position
         pVar = pdu->getVariable((m_pTrapCfg[dwIndex].pMaps[i].dwOidLen & 0x7FFFFFFF) - 1);
         if (pVar != NULL)
         {
            pszArgList[i] = _tcsdup(pVar->GetValueAsString(szBuffer, 256));
         }
         else
         {
            pszArgList[i] = _tcsdup(_T(""));
         }
      }
      else
      {
			// Extract by varbind OID
         for(j = 0; j < pdu->getNumVariables(); j++)
         {
            iResult = pdu->getVariable(j)->GetName()->Compare(
                  m_pTrapCfg[dwIndex].pMaps[i].pdwObjectId,
                  m_pTrapCfg[dwIndex].pMaps[i].dwOidLen);
            if ((iResult == OID_EQUAL) || (iResult == OID_SHORTER))
            {
               pszArgList[i] = _tcsdup(pdu->getVariable(j)->GetValueAsString(szBuffer, 256));
               break;
            }
         }
         if (j == pdu->getNumVariables())
            pszArgList[i] = _tcsdup(_T(""));
      }
   }

   szFormat[m_pTrapCfg[dwIndex].dwNumMaps + 1] = 0;
   PostEventWithTag(m_pTrapCfg[dwIndex].dwEventCode, dwObjectId,
	                 m_pTrapCfg[dwIndex].szUserTag,
	                 szFormat, pdu->getTrapId()->GetValueAsText(),
                    pszArgList[0], pszArgList[1], pszArgList[2], pszArgList[3],
                    pszArgList[4], pszArgList[5], pszArgList[6], pszArgList[7],
                    pszArgList[8], pszArgList[9], pszArgList[10], pszArgList[11],
                    pszArgList[12], pszArgList[13], pszArgList[14], pszArgList[15],
                    pszArgList[16], pszArgList[17], pszArgList[18], pszArgList[19],
                    pszArgList[20], pszArgList[21], pszArgList[22], pszArgList[23],
                    pszArgList[24], pszArgList[25], pszArgList[26], pszArgList[27],
                    pszArgList[28], pszArgList[29], pszArgList[30], pszArgList[31]);

   for(i = 0; i < m_pTrapCfg[dwIndex].dwNumMaps; i++)
      free(pszArgList[i]);
}


//
// Handler for EnumerateSessions()
//

static void BroadcastNewTrap(ClientSession *pSession, void *pArg)
{
   pSession->onNewSNMPTrap((CSCPMessage *)pArg);
}


//
// Process trap
//

static void ProcessTrap(SNMP_PDU *pdu, struct sockaddr_in *pOrigin)
{
   DWORD i, dwOriginAddr, dwBufPos, dwBufSize, dwMatchLen, dwMatchIdx;
   TCHAR *pszTrapArgs, szBuffer[4096];
   SNMP_Variable *pVar;
   Node *pNode;
	BOOL processed = FALSE;
   int iResult;

   dwOriginAddr = ntohl(pOrigin->sin_addr.s_addr);
   DbgPrintf(4, "Received SNMP trap %s from %s", 
             pdu->getTrapId()->GetValueAsText(), IpToStr(dwOriginAddr, szBuffer));

   // Match IP address to object
   pNode = FindNodeByIP(dwOriginAddr);

   // Write trap to log if required
   if (m_bLogAllTraps)
   {
      CSCPMessage msg;
      TCHAR szQuery[8192];
      DWORD dwTimeStamp = (DWORD)time(NULL);

      dwBufSize = pdu->getNumVariables() * 4096 + 16;
      pszTrapArgs = (TCHAR *)malloc(sizeof(TCHAR) * dwBufSize);
      pszTrapArgs[0] = 0;
      for(i = 0, dwBufPos = 0; i < pdu->getNumVariables(); i++)
      {
         pVar = pdu->getVariable(i);
         dwBufPos += _sntprintf(&pszTrapArgs[dwBufPos], dwBufSize - dwBufPos, _T("%s%s == '%s'"),
                                (i == 0) ? _T("") : _T("; "),
                                pVar->GetName()->GetValueAsText(), 
                                pVar->GetValueAsString(szBuffer, 3000));
      }

      // Write new trap to database
      _sntprintf(szQuery, 8192, _T("INSERT INTO snmp_trap_log (trap_id,trap_timestamp,")
                                _T("ip_addr,object_id,trap_oid,trap_varlist) VALUES ")
                                _T("(") INT64_FMT _T(",%d,'%s',%d,'%s',%s)"),
                 m_qnTrapId, dwTimeStamp, IpToStr(dwOriginAddr, szBuffer),
                 (pNode != NULL) ? pNode->Id() : (DWORD)0, pdu->getTrapId()->GetValueAsText(),
                 (const TCHAR *)DBPrepareString(pszTrapArgs));
      QueueSQLRequest(szQuery);

      // Notify connected clients
      msg.SetCode(CMD_TRAP_LOG_RECORDS);
      msg.SetVariable(VID_NUM_RECORDS, (DWORD)1);
      msg.SetVariable(VID_RECORDS_ORDER, (WORD)RECORD_ORDER_NORMAL);
      msg.SetVariable(VID_TRAP_LOG_MSG_BASE, (QWORD)m_qnTrapId);
      msg.SetVariable(VID_TRAP_LOG_MSG_BASE + 1, dwTimeStamp);
      msg.SetVariable(VID_TRAP_LOG_MSG_BASE + 2, dwOriginAddr);
      msg.SetVariable(VID_TRAP_LOG_MSG_BASE + 3, (pNode != NULL) ? pNode->Id() : (DWORD)0);
      msg.SetVariable(VID_TRAP_LOG_MSG_BASE + 4, (TCHAR *)pdu->getTrapId()->GetValueAsText());
      msg.SetVariable(VID_TRAP_LOG_MSG_BASE + 5, pszTrapArgs);
      EnumerateClientSessions(BroadcastNewTrap, &msg);
      free(pszTrapArgs);

      m_qnTrapId++;
   }

   // Process trap if it is coming from host registered in database
   if (pNode != NULL)
   {
      // Pass trap to loaded modules
      for(i = 0; i < g_dwNumModules; i++)
         if (g_pModuleList[i].pfTrapHandler != NULL)
            if (g_pModuleList[i].pfTrapHandler(pdu, pNode))
				{
					processed = TRUE;
               break;   // Trap was processed by the module
				}

      // Find if we have this trap in our list
      MutexLock(m_mutexTrapCfgAccess, INFINITE);

      // Try to find closest match
      for(i = 0, dwMatchLen = 0; i < m_dwNumTraps; i++)
      {
         if (m_pTrapCfg[i].dwOidLen > 0)
         {
            iResult = pdu->getTrapId()->Compare(m_pTrapCfg[i].pdwObjectId, m_pTrapCfg[i].dwOidLen);
            if (iResult == OID_EQUAL)
            {
               dwMatchLen = m_pTrapCfg[i].dwOidLen;
               dwMatchIdx = i;
               break;   // Find exact match
            }
            if (iResult == OID_SHORTER)
            {
               if (m_pTrapCfg[i].dwOidLen > dwMatchLen)
               {
                  dwMatchLen = m_pTrapCfg[i].dwOidLen;
                  dwMatchIdx = i;
               }
            }
         }
      }

      if (dwMatchLen > 0)
      {
         GenerateTrapEvent(pNode->Id(), dwMatchIdx, pdu);
      }
      else     // Process unmatched traps
      {
         // Handle unprocessed traps
         if (!processed)
         {
            // Build trap's parameters string
            dwBufSize = pdu->getNumVariables() * 4096 + 16;
            pszTrapArgs = (TCHAR *)malloc(sizeof(TCHAR) * dwBufSize);
            pszTrapArgs[0] = 0;
            for(i = 0, dwBufPos = 0; i < pdu->getNumVariables(); i++)
            {
               pVar = pdu->getVariable(i);
               dwBufPos += _sntprintf(&pszTrapArgs[dwBufPos], dwBufSize - dwBufPos, _T("%s%s == '%s'"),
                                      (i == 0) ? _T("") : _T("; "),
                                      pVar->GetName()->GetValueAsText(), 
                                      pVar->GetValueAsString(szBuffer, 512));
            }

            // Generate default event for unmatched traps
            PostEvent(EVENT_SNMP_UNMATCHED_TRAP, pNode->Id(), "ss", 
                      pdu->getTrapId()->GetValueAsText(), pszTrapArgs);
            free(pszTrapArgs);
         }
      }
      MutexUnlock(m_mutexTrapCfgAccess);
   }
}


//
// Context finder - tries to find SNMPv3 security context by IP address
//

static SNMP_SecurityContext *ContextFinder(struct sockaddr *addr, socklen_t addrLen)
{
	DWORD ipAddr = ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	Node *node = FindNodeByIP(ipAddr);
	TCHAR buffer[32];
	DbgPrintf(6, _T("SNMPTrapReceiver: looking for SNMP security context for node %s %s"),
	          IpToStr(ipAddr, buffer), (node != NULL) ? node->Name() : _T("<unknown>"));
	return (node != NULL) ? node->getSnmpSecurityContext() : NULL;
}


//
// SNMP trap receiver thread
//

THREAD_RESULT THREAD_CALL SNMPTrapReceiver(void *pArg)
{
   SOCKET hSocket;
   struct sockaddr_in addr;
   int iBytes;
   socklen_t nAddrLen;
   SNMP_UDPTransport *pTransport;
   SNMP_PDU *pdu;

   hSocket = socket(AF_INET, SOCK_DGRAM, 0);
   if (hSocket == -1)
   {
      nxlog_write(MSG_SOCKET_FAILED, EVENTLOG_ERROR_TYPE, "s", "SNMPTrapReceiver");
      return THREAD_OK;
   }

	SetSocketReuseFlag(hSocket);

   // Fill in local address structure
   memset(&addr, 0, sizeof(struct sockaddr_in));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = ResolveHostName(g_szListenAddress);
   addr.sin_port = htons(162);

   // Bind socket
   if (bind(hSocket, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) != 0)
   {
      nxlog_write(MSG_BIND_ERROR, EVENTLOG_ERROR_TYPE, "dse", 162, "SNMPTrapReceiver", WSAGetLastError());
      closesocket(hSocket);
      return THREAD_OK;
   }
	nxlog_write(MSG_LISTENING_FOR_SNMP, EVENTLOG_INFORMATION_TYPE, "ad", ntohl(addr.sin_addr.s_addr), 162);

   pTransport = new SNMP_UDPTransport(hSocket);
	pTransport->enableEngineIdAutoupdate(true);
   DbgPrintf(1, _T("SNMP Trap Receiver started"));

   // Wait for packets
   while(!ShutdownInProgress())
   {
      nAddrLen = sizeof(struct sockaddr_in);
      iBytes = pTransport->readMessage(&pdu, 2000, (struct sockaddr *)&addr, &nAddrLen, ContextFinder);
      if ((iBytes > 0) && (pdu != NULL))
      {
			DbgPrintf(6, _T("SNMPTrapReceiver: received PDU of type %d"), pdu->getCommand());
         if (pdu->getCommand() == SNMP_TRAP)
            ProcessTrap(pdu, &addr);
         delete pdu;
      }
      else
      {
         // Sleep on error
         ThreadSleepMs(100);
      }
   }

   delete pTransport;
   DbgPrintf(1, _T("SNMP Trap Receiver terminated"));
   return THREAD_OK;
}


//
// Send all trap configuration records to client
//

void SendTrapsToClient(ClientSession *pSession, DWORD dwRqId)
{
   DWORD i, j, dwId1, dwId2, dwId3;
   CSCPMessage msg;

   // Prepare message
   msg.SetCode(CMD_TRAP_CFG_RECORD);
   msg.SetId(dwRqId);

   MutexLock(m_mutexTrapCfgAccess, INFINITE);
   for(i = 0; i < m_dwNumTraps; i++)
   {
      msg.SetVariable(VID_TRAP_ID, m_pTrapCfg[i].dwId);
      msg.SetVariable(VID_TRAP_OID_LEN, m_pTrapCfg[i].dwOidLen); 
      msg.SetVariableToInt32Array(VID_TRAP_OID, m_pTrapCfg[i].dwOidLen, m_pTrapCfg[i].pdwObjectId);
      msg.SetVariable(VID_EVENT_CODE, m_pTrapCfg[i].dwEventCode);
      msg.SetVariable(VID_DESCRIPTION, m_pTrapCfg[i].szDescription);
      msg.SetVariable(VID_USER_TAG, m_pTrapCfg[i].szUserTag);
      msg.SetVariable(VID_TRAP_NUM_MAPS, m_pTrapCfg[i].dwNumMaps);
      for(j = 0, dwId1 = VID_TRAP_PLEN_BASE, dwId2 = VID_TRAP_PNAME_BASE, dwId3 = VID_TRAP_PDESCR_BASE; 
          j < m_pTrapCfg[i].dwNumMaps; j++, dwId1++, dwId2++, dwId3++)
      {
         msg.SetVariable(dwId1, m_pTrapCfg[i].pMaps[j].dwOidLen);
         if ((m_pTrapCfg[i].pMaps[j].dwOidLen & 0x80000000) == 0)
            msg.SetVariableToInt32Array(dwId2, m_pTrapCfg[i].pMaps[j].dwOidLen, m_pTrapCfg[i].pMaps[j].pdwObjectId);
         msg.SetVariable(dwId3, m_pTrapCfg[i].pMaps[j].szDescription);
      }
      pSession->sendMessage(&msg);
      msg.DeleteAllVariables();
   }
   MutexUnlock(m_mutexTrapCfgAccess);

   msg.SetVariable(VID_TRAP_ID, (DWORD)0);
   pSession->sendMessage(&msg);
}


//
// Prepare single message with all trap configuration records
//

void CreateTrapCfgMessage(CSCPMessage &msg)
{
   DWORD i, dwId;

   MutexLock(m_mutexTrapCfgAccess, INFINITE);
	msg.SetVariable(VID_NUM_TRAPS, m_dwNumTraps);
   for(i = 0, dwId = VID_TRAP_INFO_BASE; i < m_dwNumTraps; i++, dwId += 5)
   {
      msg.SetVariable(dwId++, m_pTrapCfg[i].dwId);
      msg.SetVariable(dwId++, m_pTrapCfg[i].dwOidLen); 
      msg.SetVariableToInt32Array(dwId++, m_pTrapCfg[i].dwOidLen, m_pTrapCfg[i].pdwObjectId);
      msg.SetVariable(dwId++, m_pTrapCfg[i].dwEventCode);
      msg.SetVariable(dwId++, m_pTrapCfg[i].szDescription);
   }
   MutexUnlock(m_mutexTrapCfgAccess);
}


//
// Delete trap configuration record
//

DWORD DeleteTrap(DWORD dwId)
{
   DWORD i, j, dwResult = RCC_INVALID_TRAP_ID;
   TCHAR szQuery[256];

   MutexLock(m_mutexTrapCfgAccess, INFINITE);
   
   for(i = 0; i < m_dwNumTraps; i++)
   {
      if (m_pTrapCfg[i].dwId == dwId)
      {
         // Free allocated resources
         for(j = 0; j < m_pTrapCfg[i].dwNumMaps; j++)
            safe_free(m_pTrapCfg[i].pMaps[j].pdwObjectId);
         safe_free(m_pTrapCfg[i].pMaps);
         safe_free(m_pTrapCfg[i].pdwObjectId);

         // Remove trap entry from list
         m_dwNumTraps--;
         memmove(&m_pTrapCfg[i], &m_pTrapCfg[i + 1], sizeof(NXC_TRAP_CFG_ENTRY) * (m_dwNumTraps - i));

         // Remove trap entry from database
         _stprintf(szQuery, _T("DELETE FROM snmp_trap_cfg WHERE trap_id=%d"), dwId);
         QueueSQLRequest(szQuery);
         _stprintf(szQuery, _T("DELETE FROM snmp_trap_pmap WHERE trap_id=%d"), dwId);
         QueueSQLRequest(szQuery);
         dwResult = RCC_SUCCESS;
         break;
      }
   }

   MutexUnlock(m_mutexTrapCfgAccess);
   return dwResult;
}


//
// Save parameter mapping to database
//

static BOOL SaveParameterMapping(NXC_TRAP_CFG_ENTRY *pTrap)
{
	TCHAR szQuery[1024], szOID[1024], *pszEscDescr;
	BOOL bRet;
	DWORD i;

   _sntprintf(szQuery, 1024, _T("DELETE FROM snmp_trap_pmap WHERE trap_id=%d"), pTrap->dwId);
   bRet = DBQuery(g_hCoreDB, szQuery);
   if (bRet)
   {
      for(i = 0; i < pTrap->dwNumMaps; i++)
      {
         if ((pTrap->pMaps[i].dwOidLen & 0x80000000) == 0)
         {
            SNMPConvertOIDToText(pTrap->pMaps[i].dwOidLen,
                                 pTrap->pMaps[i].pdwObjectId,
                                 szOID, 1024);
         }
         else
         {
            _stprintf(szOID, _T("POS:%d"), pTrap->pMaps[i].dwOidLen & 0x7FFFFFFF);
         }
         pszEscDescr = EncodeSQLString(pTrap->pMaps[i].szDescription);
         _sntprintf(szQuery, 1024, _T("INSERT INTO snmp_trap_pmap (trap_id,parameter,")
                                   _T("snmp_oid,description) VALUES (%d,%d,'%s','%s')"),
                    pTrap->dwId, i + 1, szOID, pszEscDescr);
         free(pszEscDescr);
         bRet = DBQuery(g_hCoreDB, szQuery);
         if (!bRet)
            break;
      }
   }
	return bRet;
}


//
// Create new trap configuration record
//

DWORD CreateNewTrap(DWORD *pdwTrapId)
{
   DWORD dwResult = RCC_SUCCESS;
   TCHAR szQuery[256];

   MutexLock(m_mutexTrapCfgAccess, INFINITE);
   
   *pdwTrapId = CreateUniqueId(IDG_SNMP_TRAP);
   m_pTrapCfg = (NXC_TRAP_CFG_ENTRY *)realloc(m_pTrapCfg, sizeof(NXC_TRAP_CFG_ENTRY) * (m_dwNumTraps + 1));
   memset(&m_pTrapCfg[m_dwNumTraps], 0, sizeof(NXC_TRAP_CFG_ENTRY));
   m_pTrapCfg[m_dwNumTraps].dwId = *pdwTrapId;
   m_pTrapCfg[m_dwNumTraps].dwEventCode = EVENT_SNMP_UNMATCHED_TRAP;
   m_dwNumTraps++;

   MutexUnlock(m_mutexTrapCfgAccess);

   _stprintf(szQuery, _T("INSERT INTO snmp_trap_cfg (trap_id,snmp_oid,event_code,description,user_tag) ")
                      _T("VALUES (%d,'',%d,'#00','#00')"), *pdwTrapId, (DWORD)EVENT_SNMP_UNMATCHED_TRAP);
   if (!DBQuery(g_hCoreDB, szQuery))
      dwResult = RCC_DB_FAILURE;

   return dwResult;
}


//
// Create new trap configuration record from NXMP data
//

DWORD CreateNewTrap(NXC_TRAP_CFG_ENTRY *pTrap)
{
   DWORD i, dwResult;
   TCHAR szQuery[4096], szOID[1024], *pszEscDescr, *pszEscTag;
	BOOL bSuccess;

   MutexLock(m_mutexTrapCfgAccess, INFINITE);
   
   m_pTrapCfg = (NXC_TRAP_CFG_ENTRY *)realloc(m_pTrapCfg, sizeof(NXC_TRAP_CFG_ENTRY) * (m_dwNumTraps + 1));
   memcpy(&m_pTrapCfg[m_dwNumTraps], pTrap, sizeof(NXC_TRAP_CFG_ENTRY));
   m_pTrapCfg[m_dwNumTraps].dwId = CreateUniqueId(IDG_SNMP_TRAP);
	m_pTrapCfg[m_dwNumTraps].pdwObjectId = (DWORD *)nx_memdup(pTrap->pdwObjectId, sizeof(DWORD) * pTrap->dwOidLen);
	m_pTrapCfg[m_dwNumTraps].pMaps = (NXC_OID_MAP *)nx_memdup(pTrap->pMaps, sizeof(NXC_OID_MAP) * pTrap->dwNumMaps);
	for(i = 0; i < m_pTrapCfg[m_dwNumTraps].dwNumMaps; i++)
	{
      if ((m_pTrapCfg[m_dwNumTraps].pMaps[i].dwOidLen & 0x80000000) == 0)
			m_pTrapCfg[m_dwNumTraps].pMaps[i].pdwObjectId = (DWORD *)nx_memdup(pTrap->pMaps[i].pdwObjectId, sizeof(DWORD) * pTrap->pMaps[i].dwOidLen);
	}

	// Write new trap to database
   SNMPConvertOIDToText(m_pTrapCfg[m_dwNumTraps].dwOidLen, m_pTrapCfg[m_dwNumTraps].pdwObjectId, szOID, 1024);
	pszEscDescr = EncodeSQLString(m_pTrapCfg[m_dwNumTraps].szDescription);
	pszEscTag = EncodeSQLString(m_pTrapCfg[m_dwNumTraps].szUserTag);
   _stprintf(szQuery, _T("INSERT INTO snmp_trap_cfg (trap_id,snmp_oid,event_code,description,user_tag) ")
                      _T("VALUES (%d,'%s',%d,'%s','%s')"), m_pTrapCfg[m_dwNumTraps].dwId,
	          szOID, m_pTrapCfg[m_dwNumTraps].dwEventCode, pszEscDescr, pszEscTag);
	free(pszEscDescr);
	free(pszEscTag);

	if(DBBegin(g_hCoreDB))
   {
      bSuccess = DBQuery(g_hCoreDB, szQuery);
      if (bSuccess)
      {
			bSuccess = SaveParameterMapping(&m_pTrapCfg[m_dwNumTraps]);
      }
      if (bSuccess)
         DBCommit(g_hCoreDB);
      else
         DBRollback(g_hCoreDB);
      dwResult = bSuccess ? RCC_SUCCESS : RCC_DB_FAILURE;
   }
   else
   {
      dwResult = RCC_DB_FAILURE;
   }

   m_dwNumTraps++;
   MutexUnlock(m_mutexTrapCfgAccess);

   return dwResult;
}


//
// Update trap configuration record from message
//

DWORD UpdateTrapFromMsg(CSCPMessage *pMsg)
{
   DWORD i, j, dwId1, dwId2, dwId3, dwTrapId, dwResult = RCC_INVALID_TRAP_ID;
   TCHAR szQuery[1024], szOID[1024], *pszEscDescr, *pszEscTag;
   BOOL bSuccess;

   dwTrapId = pMsg->GetVariableLong(VID_TRAP_ID);

   MutexLock(m_mutexTrapCfgAccess, INFINITE);
   for(i = 0; i < m_dwNumTraps; i++)
   {
      if (m_pTrapCfg[i].dwId == dwTrapId)
      {
         // Read trap configuration from event
         m_pTrapCfg[i].dwEventCode = pMsg->GetVariableLong(VID_EVENT_CODE);
         m_pTrapCfg[i].dwOidLen = pMsg->GetVariableLong(VID_TRAP_OID_LEN);
         m_pTrapCfg[i].pdwObjectId = (DWORD *)realloc(m_pTrapCfg[i].pdwObjectId, sizeof(DWORD) * m_pTrapCfg[i].dwOidLen);
         pMsg->GetVariableInt32Array(VID_TRAP_OID, m_pTrapCfg[i].dwOidLen, m_pTrapCfg[i].pdwObjectId);
         pMsg->GetVariableStr(VID_DESCRIPTION, m_pTrapCfg[i].szDescription, MAX_DB_STRING);
         pMsg->GetVariableStr(VID_USER_TAG, m_pTrapCfg[i].szUserTag, MAX_USERTAG_LENGTH);

         // Destroy current parameter mapping
         for(j = 0; j < m_pTrapCfg[i].dwNumMaps; j++)
            safe_free(m_pTrapCfg[i].pMaps[j].pdwObjectId);
         safe_free(m_pTrapCfg[i].pMaps);

         // Read new mappings from message
         m_pTrapCfg[i].dwNumMaps = pMsg->GetVariableLong(VID_TRAP_NUM_MAPS);
         m_pTrapCfg[i].pMaps = (NXC_OID_MAP *)malloc(sizeof(NXC_OID_MAP) * m_pTrapCfg[i].dwNumMaps);
         for(j = 0, dwId1 = VID_TRAP_PLEN_BASE, dwId2 = VID_TRAP_PNAME_BASE, dwId3 = VID_TRAP_PDESCR_BASE; 
             j < m_pTrapCfg[i].dwNumMaps; j++, dwId1++, dwId2++, dwId3++)
         {
            m_pTrapCfg[i].pMaps[j].dwOidLen = pMsg->GetVariableLong(dwId1);
            if ((m_pTrapCfg[i].pMaps[j].dwOidLen & 0x80000000) == 0)
            {
               m_pTrapCfg[i].pMaps[j].pdwObjectId = 
                  (DWORD *)malloc(sizeof(DWORD) * m_pTrapCfg[i].pMaps[j].dwOidLen);
               pMsg->GetVariableInt32Array(dwId2, m_pTrapCfg[i].pMaps[j].dwOidLen, 
                                           m_pTrapCfg[i].pMaps[j].pdwObjectId);
            }
            else
            {
               m_pTrapCfg[i].pMaps[j].pdwObjectId = NULL;
            }
            pMsg->GetVariableStr(dwId3, m_pTrapCfg[i].pMaps[j].szDescription, MAX_DB_STRING);
         }

         // Update database
         pszEscDescr = EncodeSQLString(m_pTrapCfg[i].szDescription);
         pszEscTag = EncodeSQLString(m_pTrapCfg[i].szUserTag);
         SNMPConvertOIDToText(m_pTrapCfg[i].dwOidLen, m_pTrapCfg[i].pdwObjectId, szOID, 1024);
         _sntprintf(szQuery, 1024, _T("UPDATE snmp_trap_cfg SET snmp_oid='%s',event_code=%d,description='%s',user_tag='%s' WHERE trap_id=%d"),
                    szOID, m_pTrapCfg[i].dwEventCode, pszEscDescr, pszEscTag, m_pTrapCfg[i].dwId);
         free(pszEscDescr);
			free(pszEscTag);
         if(DBBegin(g_hCoreDB))
         {
            bSuccess = DBQuery(g_hCoreDB, szQuery);
            if (bSuccess)
            {
					bSuccess = SaveParameterMapping(&m_pTrapCfg[i]);
            }
            if (bSuccess)
               DBCommit(g_hCoreDB);
            else
               DBRollback(g_hCoreDB);
            dwResult = bSuccess ? RCC_SUCCESS : RCC_DB_FAILURE;
         }
         else
         {
            dwResult = RCC_DB_FAILURE;
         }
         break;
      }
   }

   MutexUnlock(m_mutexTrapCfgAccess);
   return dwResult;
}


//
// Create trap record in NXMP file
//

void CreateNXMPTrapRecord(String &str, DWORD dwId)
{
	DWORD i, j;
	TCHAR szBuffer[1024];
	String strTemp;

   MutexLock(m_mutexTrapCfgAccess, INFINITE);
   for(i = 0; i < m_dwNumTraps; i++)
   {
      if (m_pTrapCfg[i].dwId == dwId)
      {
			str.addFormattedString(_T("\t@TRAP %s\n\t{\n"),
			                       SNMPConvertOIDToText(m_pTrapCfg[i].dwOidLen,
			                                            m_pTrapCfg[i].pdwObjectId,
																	  szBuffer, 1024));
			strTemp = m_pTrapCfg[i].szDescription;
			strTemp.escapeCharacter(_T('\\'), _T('\\'));
			strTemp.escapeCharacter(_T('"'), _T('\\'));
			strTemp.translate(_T("\r"), _T("\\r"));
			strTemp.translate(_T("\n"), _T("\\n"));
			str.addFormattedString(_T("\t\tDESCRIPTION=\"%s\";\n"), (const TCHAR *)strTemp);

			strTemp = m_pTrapCfg[i].szUserTag;
			strTemp.escapeCharacter(_T('\\'), _T('\\'));
			strTemp.escapeCharacter(_T('"'), _T('\\'));
			strTemp.translate(_T("\r"), _T("\\r"));
			strTemp.translate(_T("\n"), _T("\\n"));
			str.addFormattedString(_T("\t\tUSERTAG=\"%s\";\n"), (const TCHAR *)strTemp);

		   ResolveEventName(m_pTrapCfg[i].dwEventCode, szBuffer);
			str.addFormattedString(_T("\t\tEVENT=%s;\n"), szBuffer);
			if (m_pTrapCfg[i].dwNumMaps > 0)
			{
				str += _T("\t\t@PARAMETERS\n\t\t{\n");
				for(j = 0; j < m_pTrapCfg[i].dwNumMaps; j++)
				{
					strTemp = m_pTrapCfg[i].pMaps[j].szDescription;
					strTemp.escapeCharacter(_T('\\'), _T('\\'));
					strTemp.escapeCharacter(_T('"'), _T('\\'));
					strTemp.translate(_T("\r"), _T("\\r"));
					strTemp.translate(_T("\n"), _T("\\n"));
               if ((m_pTrapCfg[i].pMaps[j].dwOidLen & 0x80000000) == 0)
					{
						str.addFormattedString(_T("\t\t\t%s=\"%s\";\n"),
						                       SNMPConvertOIDToText(m_pTrapCfg[i].pMaps[j].dwOidLen,
													                       m_pTrapCfg[i].pMaps[j].pdwObjectId,
																				  szBuffer, 1024),
						                       (const TCHAR *)strTemp);
					}
					else
					{
						str.addFormattedString(_T("\t\t\tPOS:%d=\"%s\";\n"),
						                       m_pTrapCfg[i].pMaps[j].dwOidLen & 0x7FFFFFFF,
						                       (const TCHAR *)strTemp);
					}
				}
				str += _T("\t\t}\n");
			}
			str += _T("\t}\n");
			break;
		}
	}
   MutexUnlock(m_mutexTrapCfgAccess);
}
