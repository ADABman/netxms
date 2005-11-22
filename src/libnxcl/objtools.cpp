/* 
** NetXMS - Network Management System
** Client Library
** Copyright (C) 2004, 2005 Victor Kirhenshtein
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
** $module: objtools.cpp
**
**/

#include "libnxcl.h"


//
// Get list of object tools
//

DWORD LIBNXCL_EXPORTABLE NXCGetObjectTools(NXC_SESSION hSession, DWORD *pdwNumTools,
                                           NXC_OBJECT_TOOL **ppToolList)
{
   CSCPMessage msg, *pResponse;
   DWORD i, dwResult, dwRqId, dwId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_GET_OBJECT_TOOLS);
   msg.SetId(dwRqId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   *pdwNumTools = 0;
   *ppToolList = NULL;

   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId);
   if (pResponse != NULL)
   {
      dwResult = pResponse->GetVariableLong(VID_RCC);
      if (dwResult == RCC_SUCCESS)
      {
         *pdwNumTools = pResponse->GetVariableLong(VID_NUM_TOOLS);
         *ppToolList = (NXC_OBJECT_TOOL *)malloc(sizeof(NXC_OBJECT_TOOL) * (*pdwNumTools));
         memset(*ppToolList, 0, sizeof(NXC_OBJECT_TOOL) * (*pdwNumTools));
         for(i = 0, dwId = VID_OBJECT_TOOLS_BASE; i < *pdwNumTools; i++, dwId += 10)
         {
            (*ppToolList)[i].dwId = pResponse->GetVariableLong(dwId);
            pResponse->GetVariableStr(dwId + 1, (*ppToolList)[i].szName, MAX_DB_STRING);
            (*ppToolList)[i].wType = pResponse->GetVariableShort(dwId + 2);
            (*ppToolList)[i].pszData = pResponse->GetVariableStr(dwId + 3);
            (*ppToolList)[i].dwFlags = pResponse->GetVariableLong(dwId + 4);
            pResponse->GetVariableStr(dwId + 5, (*ppToolList)[i].szDescription, MAX_DB_STRING);
            (*ppToolList)[i].pszMatchingOID = pResponse->GetVariableStr(dwId + 6);
         }
      }
      delete pResponse;
   }
   else
   {
      dwResult = RCC_TIMEOUT;
   }
   return dwResult;
}


//
// Destroy list of object tools previously created by NXCGetObjectTools
//

void LIBNXCL_EXPORTABLE NXCDestroyObjectToolList(DWORD dwNumTools, NXC_OBJECT_TOOL *pList)
{
   DWORD i;

   if (pList != NULL)
   {
      for(i = 0; i < dwNumTools; i++)
      {
         safe_free(pList[i].pszData);
         safe_free(pList[i].pszMatchingOID);
      }
      free(pList);
   }
}


//
// Execute table tool
//

DWORD LIBNXCL_EXPORTABLE NXCExecuteTableTool(NXC_SESSION hSession, DWORD dwNodeId,
                                             DWORD dwToolId, NXC_TABLE_DATA **ppData)
{
   CSCPMessage msg, *pResponse;
   DWORD i, dwResult, dwRqId, dwId, dwNumRecords;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_EXEC_TABLE_TOOL);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_OBJECT_ID, dwNodeId);
   msg.SetVariable(VID_TOOL_ID, dwToolId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   *ppData = NULL;

   dwResult = ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
   if (dwResult == RCC_SUCCESS)
   {
      pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_TABLE_DATA, dwRqId, 180000);
      if (pResponse != NULL)
      {
         dwResult = pResponse->GetVariableLong(VID_RCC);
         if (dwResult == RCC_SUCCESS)
         {
            *ppData = (NXC_TABLE_DATA *)malloc(sizeof(NXC_TABLE_DATA));
            (*ppData)->pszTitle = pResponse->GetVariableStr(VID_TABLE_TITLE);
            (*ppData)->dwNumCols = pResponse->GetVariableLong(VID_NUM_COLUMNS);
            (*ppData)->dwNumRows = pResponse->GetVariableLong(VID_NUM_ROWS);
            (*ppData)->pnColFormat = (LONG *)malloc(sizeof(LONG) * (*ppData)->dwNumCols);
            (*ppData)->ppszColNames = (TCHAR **)malloc(sizeof(TCHAR *) * (*ppData)->dwNumCols);
            dwNumRecords = (*ppData)->dwNumCols * (*ppData)->dwNumRows;
            (*ppData)->ppszData = (TCHAR **)malloc(sizeof(TCHAR *) * dwNumRecords);

            // Column information
            for(i = 0; i < (*ppData)->dwNumCols; i++)
            {
               (*ppData)->ppszColNames[i] = pResponse->GetVariableStr(VID_COLUMN_NAME_BASE + i);
               (*ppData)->pnColFormat[i] = pResponse->GetVariableLong(VID_COLUMN_FMT_BASE + i);
            }

            // Column data
            for(i = 0, dwId = VID_ROW_DATA_BASE; i < dwNumRecords; i++, dwId++)
               (*ppData)->ppszData[i] = pResponse->GetVariableStr(dwId);
         }
         delete pResponse;
      }
      else
      {
         dwResult = RCC_TIMEOUT;
      }
   }

   return dwResult;
}


//
// Destroy received table data
//

void LIBNXCL_EXPORTABLE NXCDestroyTableData(NXC_TABLE_DATA *pData)
{
   DWORD i;

   if (pData == NULL)
      return;

   for(i = 0; i < pData->dwNumCols; i++)
      safe_free(pData->ppszColNames[i]);
   safe_free(pData->ppszColNames);
   safe_free(pData->pnColFormat);

   for(i = 0; i < pData->dwNumCols * pData->dwNumRows; i++)
      safe_free(pData->ppszData[i]);
   safe_free(pData->ppszData);
   safe_free(pData->pszTitle);
   free(pData);
}


//
// Lock object tools configuration
//

DWORD LIBNXCL_EXPORTABLE NXCLockObjectTools(NXC_SESSION hSession)
{
   return ((NXCL_Session *)hSession)->SimpleCommand(CMD_LOCK_OBJECT_TOOLS);
}


//
// Unlock object tools configuration
//

DWORD LIBNXCL_EXPORTABLE NXCUnlockObjectTools(NXC_SESSION hSession)
{
   return ((NXCL_Session *)hSession)->SimpleCommand(CMD_UNLOCK_OBJECT_TOOLS);
}


//
// Delete object tool
//

DWORD LIBNXCL_EXPORTABLE NXCDeleteObjectTool(NXC_SESSION hSession, DWORD dwToolId)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_DELETE_OBJECT_TOOL);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_TOOL_ID, dwToolId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Get object tool details
//

DWORD LIBNXCL_EXPORTABLE NXCGetObjectToolDetails(NXC_SESSION hSession, DWORD dwToolId,
                                                 NXC_OBJECT_TOOL_DETAILS **ppData)
{
   CSCPMessage msg, *pResponse;
   DWORD dwRqId, dwResult;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_GET_OBJECT_TOOL_DETAILS);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_TOOL_ID, dwToolId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId);
   if (pResponse != NULL)
   {
      dwResult = pResponse->GetVariableLong(VID_RCC);
      if (dwResult == RCC_SUCCESS)
      {
         *ppData = (NXC_OBJECT_TOOL_DETAILS *)malloc(sizeof(NXC_OBJECT_TOOL_DETAILS));
         memset(*ppData, 0, sizeof(NXC_OBJECT_TOOL_DETAILS));
         (*ppData)->dwId = dwToolId;
         (*ppData)->dwFlags = pResponse->GetVariableLong(VID_FLAGS);
         (*ppData)->wType = pResponse->GetVariableShort(VID_TOOL_TYPE);
         (*ppData)->pszData = pResponse->GetVariableStr(VID_TOOL_DATA);
         pResponse->GetVariableStr(VID_NAME, (*ppData)->szName, MAX_DB_STRING);
         pResponse->GetVariableStr(VID_DESCRIPTION, (*ppData)->szDescription, MAX_DB_STRING);
         (*ppData)->pszMatchingOID = pResponse->GetVariableStr(VID_TOOL_OID);
         (*ppData)->dwACLSize = pResponse->GetVariableLong(VID_ACL_SIZE);
         (*ppData)->pdwACL = (DWORD *)malloc(sizeof(DWORD) * (*ppData)->dwACLSize);
         pResponse->GetVariableInt32Array(VID_ACL, (*ppData)->dwACLSize, (*ppData)->pdwACL);
         if (((*ppData)->wType == TOOL_TYPE_TABLE_SNMP) ||
             ((*ppData)->wType == TOOL_TYPE_TABLE_AGENT))
         {
            DWORD i, dwId;

            (*ppData)->wNumColumns = pResponse->GetVariableShort(VID_NUM_COLUMNS);
            (*ppData)->pColList = (NXC_OBJECT_TOOL_COLUMN *)malloc(sizeof(NXC_OBJECT_TOOL_COLUMN) * (*ppData)->wNumColumns);
            for(i = 0, dwId = VID_COLUMN_INFO_BASE; i < (DWORD)(*ppData)->wNumColumns; i++)
            {
               pResponse->GetVariableStr(dwId++, (*ppData)->pColList[i].szName, MAX_DB_STRING);
               pResponse->GetVariableStr(dwId++, (*ppData)->pColList[i].szOID, MAX_DB_STRING);
               (*ppData)->pColList[i].nFormat = (int)pResponse->GetVariableShort(dwId++);
               (*ppData)->pColList[i].nSubstr = (int)pResponse->GetVariableShort(dwId++);
            }
         }
      }
      delete pResponse;
   }
   else
   {
      dwResult = RCC_TIMEOUT;
   }
   return dwResult;
}


//
// Destroy object tool detailed info structure
//

void LIBNXCL_EXPORTABLE NXCDestroyObjectToolDetails(NXC_OBJECT_TOOL_DETAILS *pData)
{
   if (pData != NULL)
   {
      safe_free(pData->pszData);
      safe_free(pData->pszMatchingOID);
      safe_free(pData->pColList);
      safe_free(pData->pdwACL);
      free(pData);
   }
}


//
// Generate ID for new object tool
//

DWORD LIBNXCL_EXPORTABLE NXCGenerateObjectToolId(NXC_SESSION hSession, DWORD *pdwToolId)
{
   CSCPMessage msg, *pResponse;
   DWORD dwRqId, dwRetCode;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   // Prepare message
   msg.SetCode(CMD_GENERATE_OBJECT_TOOL_ID);
   msg.SetId(dwRqId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);
   
   // Wait for reply
   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId);
   if (pResponse != NULL)
   {
      dwRetCode = pResponse->GetVariableLong(VID_RCC);
      if (dwRetCode == RCC_SUCCESS)
         *pdwToolId = pResponse->GetVariableLong(VID_TOOL_ID);
   }
   else
   {
      dwRetCode = RCC_TIMEOUT;
   }
   return dwRetCode;
}


//
// Update object tool configuration
//

DWORD LIBNXCL_EXPORTABLE NXCUpdateObjectTool(NXC_SESSION hSession,
                                             NXC_OBJECT_TOOL_DETAILS *pData)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   // Prepare message
   msg.SetCode(CMD_UPDATE_OBJECT_TOOL);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_TOOL_ID, pData->dwId);
   msg.SetVariable(VID_NAME, pData->szName);
   msg.SetVariable(VID_TOOL_TYPE, pData->wType);
   msg.SetVariable(VID_FLAGS, pData->dwFlags);
   msg.SetVariable(VID_DESCRIPTION, pData->szDescription);
   msg.SetVariable(VID_TOOL_DATA, pData->pszData);
   msg.SetVariable(VID_ACL_SIZE, pData->dwACLSize);
   msg.SetVariableToInt32Array(VID_ACL, pData->dwACLSize, pData->pdwACL);
   if ((pData->wType == TOOL_TYPE_TABLE_SNMP) ||
       (pData->wType == TOOL_TYPE_TABLE_AGENT))
   {
      int i;
      DWORD dwId;

      msg.SetVariable(VID_NUM_COLUMNS, pData->wNumColumns);
      for(i = 0, dwId = VID_COLUMN_INFO_BASE; i < (int)pData->wNumColumns; i++)
      {
         msg.SetVariable(dwId++, pData->pColList[i].szName);
         msg.SetVariable(dwId++, pData->pColList[i].szOID);
         msg.SetVariable(dwId++, (WORD)pData->pColList[i].nFormat);
         msg.SetVariable(dwId++, (WORD)pData->pColList[i].nSubstr);
      }
   }

   ((NXCL_Session *)hSession)->SendMsg(&msg);

   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Check if given object tool is appropriate for given node
//

BOOL LIBNXCL_EXPORTABLE NXCIsAppropriateTool(NXC_OBJECT_TOOL *pTool, NXC_OBJECT *pObject)
{
   BOOL bResult;

   if (pObject == NULL)
      return FALSE;

   if (pObject->iClass == OBJECT_NODE)
   {
      bResult = TRUE;

      if ((pTool->dwFlags & TF_REQUIRES_SNMP) && (!(pObject->node.dwFlags & NF_IS_SNMP)))
         bResult = FALSE;
      if ((pTool->dwFlags & TF_REQUIRES_AGENT) &&
          (!(pObject->node.dwFlags & NF_IS_NATIVE_AGENT)))
         bResult = FALSE;
      if (pTool->dwFlags & TF_REQUIRES_OID_MATCH)
      {
         TCHAR *pszPattern;

         pszPattern = CHECK_NULL_EX(pTool->pszMatchingOID);
         if (*pszPattern == 0)
            pszPattern = _T("*");
         if (!MatchString(pszPattern, pObject->node.szObjectId, TRUE))
            bResult = FALSE;
      }
   }
   else
   {
      bResult = FALSE;
   }
   return bResult;
}
