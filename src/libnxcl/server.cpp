/* 
** NetXMS - Network Management System
** Client Library
** Copyright (C) 2003-2010 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: server.cpp
**
**/

#include "libnxcl.h"


//
// Get list of configuration variables
//

DWORD LIBNXCL_EXPORTABLE NXCGetServerVariables(NXC_SESSION hSession, 
                                               NXC_SERVER_VARIABLE **ppVarList, 
                                               DWORD *pdwNumVars)
{
   CSCPMessage msg, *pResponse;
   DWORD i, dwId, dwRqId, dwRetCode;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   *pdwNumVars = 0;
   *ppVarList = NULL;

   // Build request message
   msg.SetCode(CMD_GET_CONFIG_VARLIST);
   msg.SetId(dwRqId);

   // Send request
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   // Wait for response
   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId, 30000);
   if (pResponse != NULL)
   {
      dwRetCode = pResponse->GetVariableLong(VID_RCC);
      if (dwRetCode == RCC_SUCCESS)
      {
         *pdwNumVars = pResponse->GetVariableLong(VID_NUM_VARIABLES);
         *ppVarList = (NXC_SERVER_VARIABLE *)malloc(sizeof(NXC_SERVER_VARIABLE) * (*pdwNumVars));

         for(i = 0, dwId = VID_VARLIST_BASE; i < *pdwNumVars; i++)
         {
            pResponse->GetVariableStr(dwId++, (*ppVarList)[i].szName, MAX_OBJECT_NAME);
            pResponse->GetVariableStr(dwId++, (*ppVarList)[i].szValue, MAX_DB_STRING);
            (*ppVarList)[i].bNeedRestart = pResponse->GetVariableShort(dwId++) ? TRUE : FALSE;
         }
      }
      delete pResponse;
   }
   else
   {
      dwRetCode = RCC_TIMEOUT;
   }

   return dwRetCode;
}


//
// Set value of server's variable
//

DWORD LIBNXCL_EXPORTABLE NXCSetServerVariable(NXC_SESSION hSession, const TCHAR *pszVarName,
                                              const TCHAR *pszValue)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_SET_CONFIG_VARIABLE);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_NAME, pszVarName);
   msg.SetVariable(VID_VALUE, pszValue);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Delete server's variable
//

DWORD LIBNXCL_EXPORTABLE NXCDeleteServerVariable(NXC_SESSION hSession, const TCHAR *pszVarName)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_DELETE_CONFIG_VARIABLE);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_NAME, pszVarName);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Get server statistics
//

DWORD LIBNXCL_EXPORTABLE NXCGetServerStats(NXC_SESSION hSession, NXC_SERVER_STATS *pStats)
{
   CSCPMessage msg, *pResponse;
   DWORD dwRqId, dwRetCode;

   memset(pStats, 0, sizeof(NXC_SERVER_STATS));
   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_GET_SERVER_STATS);
   msg.SetId(dwRqId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId);
   if (pResponse != NULL)
   {
      dwRetCode = pResponse->GetVariableLong(VID_RCC);
      if (dwRetCode == RCC_SUCCESS)
      {
         pStats->dwNumAlarms = pResponse->GetVariableLong(VID_NUM_ALARMS);
         pResponse->GetVariableInt32Array(VID_ALARMS_BY_SEVERITY, 5, pStats->dwAlarmsBySeverity);
         pStats->dwNumClientSessions = pResponse->GetVariableLong(VID_NUM_SESSIONS);
         pStats->dwNumDCI = pResponse->GetVariableLong(VID_NUM_ITEMS);
         pStats->dwNumNodes = pResponse->GetVariableLong(VID_NUM_NODES);
         pStats->dwNumObjects = pResponse->GetVariableLong(VID_NUM_OBJECTS);
         pStats->dwServerProcessVMSize = pResponse->GetVariableLong(VID_NETXMSD_PROCESS_VMSIZE);
         pStats->dwServerProcessWorkSet = pResponse->GetVariableLong(VID_NETXMSD_PROCESS_WKSET);
         pStats->dwServerUptime = pResponse->GetVariableLong(VID_SERVER_UPTIME);
         pResponse->GetVariableStr(VID_SERVER_VERSION, pStats->szServerVersion, MAX_DB_STRING);
			pStats->dwQSizeConditionPoller = pResponse->GetVariableLong(VID_QSIZE_CONDITION_POLLER);
			pStats->dwQSizeConfPoller = pResponse->GetVariableLong(VID_QSIZE_CONF_POLLER);
			pStats->dwQSizeDBWriter = pResponse->GetVariableLong(VID_QSIZE_DBWRITER);
			pStats->dwQSizeDCIPoller = pResponse->GetVariableLong(VID_QSIZE_DCI_POLLER);
			pStats->dwQSizeDiscovery = pResponse->GetVariableLong(VID_QSIZE_DISCOVERY);
			pStats->dwQSizeEvents = pResponse->GetVariableLong(VID_QSIZE_EVENT);
			pStats->dwQSizeNodePoller = pResponse->GetVariableLong(VID_QSIZE_NODE_POLLER);
			pStats->dwQSizeRoutePoller = pResponse->GetVariableLong(VID_QSIZE_ROUTE_POLLER);
			pStats->dwQSizeStatusPoller = pResponse->GetVariableLong(VID_QSIZE_STATUS_POLLER);
      }
      delete pResponse;
   }
   else
   {
      dwRetCode = RCC_TIMEOUT;
   }
   return dwRetCode;
}


//
// Get address list
//

DWORD LIBNXCL_EXPORTABLE NXCGetAddrList(NXC_SESSION hSession, DWORD dwListType,
                                        DWORD *pdwAddrCount, NXC_ADDR_ENTRY **ppAddrList)
{
   DWORD i, dwRetCode, dwRqId, dwId;
   CSCPMessage msg, *pResponse;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_GET_ADDR_LIST);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_ADDR_LIST_TYPE, dwListType);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId);
   if (pResponse != NULL)
   {
      dwRetCode = pResponse->GetVariableLong(VID_RCC);
      if (dwRetCode == RCC_SUCCESS)
      {
         *pdwAddrCount = pResponse->GetVariableLong(VID_NUM_RECORDS);
         *ppAddrList = (NXC_ADDR_ENTRY *)malloc(sizeof(NXC_ADDR_ENTRY) * (*pdwAddrCount));
         for(i = 0, dwId = VID_ADDR_LIST_BASE; i < *pdwAddrCount; i++, dwId += 7)
         {
            (*ppAddrList)[i].dwType = pResponse->GetVariableLong(dwId++);
            (*ppAddrList)[i].dwAddr1 = pResponse->GetVariableLong(dwId++);
            (*ppAddrList)[i].dwAddr2 = pResponse->GetVariableLong(dwId++);
         }
      }
      delete pResponse;
   }
   else
   {
      dwRetCode = RCC_TIMEOUT;
   }
   return dwRetCode;
}


//
// Set address list
//

DWORD LIBNXCL_EXPORTABLE NXCSetAddrList(NXC_SESSION hSession, DWORD dwListType,
                                        DWORD dwAddrCount, NXC_ADDR_ENTRY *pAddrList)
{
   DWORD i, dwRqId, dwId;
   CSCPMessage msg;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_SET_ADDR_LIST);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_ADDR_LIST_TYPE, dwListType);
   msg.SetVariable(VID_NUM_RECORDS, dwAddrCount);
   for(i = 0, dwId = VID_ADDR_LIST_BASE; i < dwAddrCount; i++, dwId += 7)
   {
      msg.SetVariable(dwId++, pAddrList[i].dwType);
      msg.SetVariable(dwId++, pAddrList[i].dwAddr1);
      msg.SetVariable(dwId++, pAddrList[i].dwAddr2);
   }  
   ((NXCL_Session *)hSession)->SendMsg(&msg);
   
   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Reset server's component
//

DWORD LIBNXCL_EXPORTABLE NXCResetServerComponent(NXC_SESSION hSession, DWORD dwComponent)
{
   DWORD dwRqId;
   CSCPMessage msg;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_RESET_COMPONENT);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_COMPONENT_ID, dwComponent);
   ((NXCL_Session *)hSession)->SendMsg(&msg);
   
   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Get value of CLOB config variable
//

DWORD LIBNXCL_EXPORTABLE NXCGetServerConfigCLOB(NXC_SESSION hSession, const TCHAR *name, TCHAR **value)
{
   CSCPMessage msg, *pResponse;
   DWORD dwRqId, dwRetCode;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();
	*value = NULL;

   // Build request message
   msg.SetCode(CMD_CONFIG_GET_CLOB);
   msg.SetId(dwRqId);
	msg.SetVariable(VID_NAME, name);

   // Send request
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   // Wait for response
   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId);
   if (pResponse != NULL)
   {
      dwRetCode = pResponse->GetVariableLong(VID_RCC);
      if (dwRetCode == RCC_SUCCESS)
      {
			*value = pResponse->GetVariableStr(VID_VALUE);
      }
      delete pResponse;
   }
   else
   {
      dwRetCode = RCC_TIMEOUT;
   }

   return dwRetCode;
}


//
// Set value of CLOB config variable
//

DWORD LIBNXCL_EXPORTABLE NXCSetServerConfigCLOB(NXC_SESSION hSession, const TCHAR *name, const TCHAR *value)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   // Build request message
   msg.SetCode(CMD_CONFIG_SET_CLOB);
   msg.SetId(dwRqId);
	msg.SetVariable(VID_NAME, name);
	msg.SetVariable(VID_VALUE, value);

   // Send request
   ((NXCL_Session *)hSession)->SendMsg(&msg);
   
   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Run server script for object
//

DWORD LIBNXCL_EXPORTABLE NXCExecuteServerCommand(NXC_SESSION hSession, DWORD nodeId, const TCHAR *command)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   // Build request message
	msg.SetCode(CMD_EXECUTE_SERVER_COMMAND);
   msg.SetId(dwRqId);
	msg.SetVariable(VID_OBJECT_ID, name);
	msg.SetVariable(VID_COMMAND, value);

   // Send request
   ((NXCL_Session *)hSession)->SendMsg(&msg);
   
   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}
