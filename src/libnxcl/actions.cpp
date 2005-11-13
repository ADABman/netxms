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
** $module: actions.cpp
**
**/

#include "libnxcl.h"


//
// Fill action record from message
//

static void ActionFromMsg(CSCPMessage *pMsg, NXC_ACTION *pAction)
{
   pAction->bIsDisabled = pMsg->GetVariableShort(VID_IS_DISABLED);
   pAction->iType = pMsg->GetVariableShort(VID_ACTION_TYPE);
   pAction->pszData = pMsg->GetVariableStr(VID_ACTION_DATA);
   pMsg->GetVariableStr(VID_EMAIL_SUBJECT, pAction->szEmailSubject, MAX_EMAIL_SUBJECT_LEN);
   pMsg->GetVariableStr(VID_ACTION_NAME, pAction->szName, MAX_OBJECT_NAME);
   pMsg->GetVariableStr(VID_RCPT_ADDR, pAction->szRcptAddr, MAX_RCPT_ADDR_LEN);
}


//
// Process CMD_ACTION_DB_UPDATE message
//

void ProcessActionUpdate(NXCL_Session *pSession, CSCPMessage *pMsg)
{
   NXC_ACTION action;
   DWORD dwCode;

   dwCode = pMsg->GetVariableLong(VID_NOTIFICATION_CODE);
   action.dwId = pMsg->GetVariableLong(VID_ACTION_ID);
   if (dwCode != NX_NOTIFY_ACTION_DELETED)
      ActionFromMsg(pMsg, &action);
   pSession->CallEventHandler(NXC_EVENT_NOTIFICATION, dwCode, &action);
}


//
// Load all actions from server
//

DWORD LIBNXCL_EXPORTABLE NXCLoadActions(NXC_SESSION hSession, DWORD *pdwNumActions, NXC_ACTION **ppActionList)
{
   CSCPMessage msg, *pResponse;
   DWORD dwRqId, dwRetCode = RCC_SUCCESS, dwNumActions = 0, dwActionId = 0;
   NXC_ACTION *pList = NULL;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_LOAD_ACTIONS);
   msg.SetId(dwRqId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   dwRetCode = ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
   if (dwRetCode == RCC_SUCCESS)
   {
      do
      {
         pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_ACTION_DATA, dwRqId);
         if (pResponse != NULL)
         {
            dwActionId = pResponse->GetVariableLong(VID_ACTION_ID);
            if (dwActionId != 0)  // 0 is end of list indicator
            {
               pList = (NXC_ACTION *)realloc(pList, sizeof(NXC_ACTION) * (dwNumActions + 1));
               pList[dwNumActions].dwId = dwActionId;
               ActionFromMsg(pResponse, &pList[dwNumActions]);
               dwNumActions++;
            }
            delete pResponse;
         }
         else
         {
            dwRetCode = RCC_TIMEOUT;
            dwActionId = 0;
         }
      }
      while(dwActionId != 0);
   }

   // Destroy results on failure or save on success
   if (dwRetCode == RCC_SUCCESS)
   {
      *ppActionList = pList;
      *pdwNumActions = dwNumActions;
   }
   else
   {
      safe_free(pList);
      *ppActionList = NULL;
      *pdwNumActions = 0;
   }

   return dwRetCode;
}


//
// Lock action configuration database
//

DWORD LIBNXCL_EXPORTABLE NXCLockActionDB(NXC_SESSION hSession)
{
   return ((NXCL_Session *)hSession)->SimpleCommand(CMD_LOCK_ACTION_DB);
}


//
// Unlock action configuration database
//

DWORD LIBNXCL_EXPORTABLE NXCUnlockActionDB(NXC_SESSION hSession)
{
   return ((NXCL_Session *)hSession)->SimpleCommand(CMD_UNLOCK_ACTION_DB);
}


//
// Create new action on server
//

DWORD LIBNXCL_EXPORTABLE NXCCreateAction(NXC_SESSION hSession, TCHAR *pszName, DWORD *pdwNewId)
{
   CSCPMessage msg, *pResponse;
   DWORD dwRetCode, dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_CREATE_ACTION);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_ACTION_NAME, pszName);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   pResponse = ((NXCL_Session *)hSession)->WaitForMessage(CMD_REQUEST_COMPLETED, dwRqId);
   if (pResponse != NULL)
   {
      dwRetCode = pResponse->GetVariableLong(VID_RCC);
      if (dwRetCode == RCC_SUCCESS)
         *pdwNewId = pResponse->GetVariableLong(VID_ACTION_ID);
      delete pResponse;
   }
   else
   {
      dwRetCode = RCC_TIMEOUT;
   }
   return dwRetCode;
}


//
// Delete action by ID
//

DWORD LIBNXCL_EXPORTABLE NXCDeleteAction(NXC_SESSION hSession, DWORD dwActionId)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   msg.SetCode(CMD_DELETE_ACTION);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_ACTION_ID, dwActionId);
   ((NXCL_Session *)hSession)->SendMsg(&msg);

   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}


//
// Modify action record
//

DWORD LIBNXCL_EXPORTABLE NXCModifyAction(NXC_SESSION hSession, NXC_ACTION *pAction)
{
   CSCPMessage msg;
   DWORD dwRqId;

   dwRqId = ((NXCL_Session *)hSession)->CreateRqId();

   // Fill in request
   msg.SetCode(CMD_MODIFY_ACTION);
   msg.SetId(dwRqId);
   msg.SetVariable(VID_IS_DISABLED, (WORD)pAction->bIsDisabled);
   msg.SetVariable(VID_ACTION_ID, pAction->dwId);
   msg.SetVariable(VID_ACTION_TYPE, (WORD)pAction->iType);
   msg.SetVariable(VID_ACTION_DATA, pAction->pszData);
   msg.SetVariable(VID_EMAIL_SUBJECT, pAction->szEmailSubject);
   msg.SetVariable(VID_ACTION_NAME, pAction->szName);
   msg.SetVariable(VID_RCPT_ADDR, pAction->szRcptAddr);

   ((NXCL_Session *)hSession)->SendMsg(&msg);

   // Wait for response
   return ((NXCL_Session *)hSession)->WaitForRCC(dwRqId);
}
