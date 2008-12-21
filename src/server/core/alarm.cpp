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
** File: alarm.cpp
**
**/

#include "nxcore.h"


//
// Global instance of alarm manager
//

AlarmManager g_alarmMgr;


//
// Fill CSCP message with alarm data
//

void FillAlarmInfoMessage(CSCPMessage *pMsg, NXC_ALARM *pAlarm)
{
   pMsg->SetVariable(VID_ALARM_ID, pAlarm->dwAlarmId);
   pMsg->SetVariable(VID_ACK_BY_USER, pAlarm->dwAckByUser);
   pMsg->SetVariable(VID_TERMINATED_BY_USER, pAlarm->dwTermByUser);
   pMsg->SetVariable(VID_EVENT_CODE, pAlarm->dwSourceEventCode);
   pMsg->SetVariable(VID_EVENT_ID, pAlarm->qwSourceEventId);
   pMsg->SetVariable(VID_OBJECT_ID, pAlarm->dwSourceObject);
   pMsg->SetVariable(VID_CREATION_TIME, pAlarm->dwCreationTime);
   pMsg->SetVariable(VID_LAST_CHANGE_TIME, pAlarm->dwLastChangeTime);
   pMsg->SetVariable(VID_ALARM_KEY, pAlarm->szKey);
   pMsg->SetVariable(VID_ALARM_MESSAGE, pAlarm->szMessage);
   pMsg->SetVariable(VID_STATE, (WORD)pAlarm->nState);
   pMsg->SetVariable(VID_CURRENT_SEVERITY, (WORD)pAlarm->nCurrentSeverity);
   pMsg->SetVariable(VID_ORIGINAL_SEVERITY, (WORD)pAlarm->nOriginalSeverity);
   pMsg->SetVariable(VID_HELPDESK_STATE, (WORD)pAlarm->nHelpDeskState);
   pMsg->SetVariable(VID_HELPDESK_REF, pAlarm->szHelpDeskRef);
   pMsg->SetVariable(VID_REPEAT_COUNT, pAlarm->dwRepeatCount);
	pMsg->SetVariable(VID_ALARM_TIMEOUT, pAlarm->dwTimeout);
	pMsg->SetVariable(VID_ALARM_TIMEOUT_EVENT, pAlarm->dwTimeoutEvent);
}


//
// Alarm manager constructor
//

AlarmManager::AlarmManager()
{
   m_dwNumAlarms = 0;
   m_pAlarmList = NULL;
   m_mutex = MutexCreate();
	m_condShutdown = ConditionCreate(FALSE);
	m_hWatchdogThread = INVALID_THREAD_HANDLE;
}


//
// Alarm manager destructor
//

AlarmManager::~AlarmManager()
{
   safe_free(m_pAlarmList);
   MutexDestroy(m_mutex);
	ConditionSet(m_condShutdown);
	ThreadJoin(m_hWatchdogThread);
}


//
// Watchdog thread starter
//

static THREAD_RESULT THREAD_CALL WatchdogThreadStarter(void *pArg)
{
	((AlarmManager *)pArg)->WatchdogThread();
	return THREAD_OK;
}


//
// Initialize alarm manager at system startup
//

BOOL AlarmManager::Init(void)
{
   DB_RESULT hResult;
   DWORD i;

   // Load unacknowledged alarms into memory
   hResult = DBSelect(g_hCoreDB, "SELECT alarm_id,source_object_id,"
                                 "source_event_code,source_event_id,message,"
                                 "original_severity,current_severity,"
                                 "alarm_key,creation_time,last_change_time,"
                                 "hd_state,hd_ref,ack_by,repeat_count,"
                                 "alarm_state,timeout,timeout_event "
                                 "FROM alarms WHERE alarm_state<2");
   if (hResult == NULL)
      return FALSE;

   m_dwNumAlarms = DBGetNumRows(hResult);
   if (m_dwNumAlarms > 0)
   {
      m_pAlarmList = (NXC_ALARM *)malloc(sizeof(NXC_ALARM) * m_dwNumAlarms);
      memset(m_pAlarmList, 0, sizeof(NXC_ALARM) * m_dwNumAlarms);
      for(i = 0; i < m_dwNumAlarms; i++)
      {
         m_pAlarmList[i].dwAlarmId = DBGetFieldULong(hResult, i, 0);
         m_pAlarmList[i].dwSourceObject = DBGetFieldULong(hResult, i, 1);
         m_pAlarmList[i].dwSourceEventCode = DBGetFieldULong(hResult, i, 2);
         m_pAlarmList[i].qwSourceEventId = DBGetFieldUInt64(hResult, i, 3);
         DBGetField(hResult, i, 4, m_pAlarmList[i].szMessage, MAX_DB_STRING);
         DecodeSQLString(m_pAlarmList[i].szMessage);
         m_pAlarmList[i].nOriginalSeverity = (BYTE)DBGetFieldLong(hResult, i, 5);
         m_pAlarmList[i].nCurrentSeverity = (BYTE)DBGetFieldLong(hResult, i, 6);
         DBGetField(hResult, i, 7, m_pAlarmList[i].szKey, MAX_DB_STRING);
         DecodeSQLString(m_pAlarmList[i].szKey);
         m_pAlarmList[i].dwCreationTime = DBGetFieldULong(hResult, i, 8);
         m_pAlarmList[i].dwLastChangeTime = DBGetFieldULong(hResult, i, 9);
         m_pAlarmList[i].nHelpDeskState = (BYTE)DBGetFieldLong(hResult, i, 10);
         DBGetField(hResult, i, 11, m_pAlarmList[i].szHelpDeskRef, MAX_HELPDESK_REF_LEN);
         DecodeSQLString(m_pAlarmList[i].szHelpDeskRef);
         m_pAlarmList[i].dwAckByUser = DBGetFieldULong(hResult, i, 12);
         m_pAlarmList[i].dwRepeatCount = DBGetFieldULong(hResult, i, 13);
         m_pAlarmList[i].nState = (BYTE)DBGetFieldLong(hResult, i, 14);
         m_pAlarmList[i].dwTimeout = DBGetFieldULong(hResult, i, 15);
         m_pAlarmList[i].dwTimeoutEvent = DBGetFieldULong(hResult, i, 16);
      }
   }

   DBFreeResult(hResult);

	m_hWatchdogThread = ThreadCreateEx(WatchdogThreadStarter, 0, this);
   return TRUE;
}


//
// Create new alarm
//

void AlarmManager::NewAlarm(TCHAR *pszMsg, TCHAR *pszKey, int nState,
                            int iSeverity, DWORD dwTimeout,
									 DWORD dwTimeoutEvent, Event *pEvent)
{
   NXC_ALARM alarm;
   TCHAR *pszExpMsg, *pszExpKey, *pszEscRef, szQuery[2048];
   DWORD i, dwObjectId = 0;
   BOOL bNewAlarm = TRUE;

   // Expand alarm's message and key
   pszExpMsg = pEvent->ExpandText(pszMsg);
   pszExpKey = pEvent->ExpandText(pszKey);

   // Check if we have a duplicate alarm
   if ((nState != ALARM_STATE_TERMINATED) && (*pszExpKey != 0))
   {
      Lock();

      for(i = 0; i < m_dwNumAlarms; i++)
         if (!_tcscmp(pszExpKey, m_pAlarmList[i].szKey))
         {
            m_pAlarmList[i].dwRepeatCount++;
            m_pAlarmList[i].dwLastChangeTime = (DWORD)time(NULL);
            m_pAlarmList[i].dwSourceObject = pEvent->SourceId();
            m_pAlarmList[i].nState = nState;
            m_pAlarmList[i].nCurrentSeverity = iSeverity;
				m_pAlarmList[i].dwTimeout = dwTimeout;
				m_pAlarmList[i].dwTimeoutEvent = dwTimeoutEvent;
            nx_strncpy(m_pAlarmList[i].szMessage, pszExpMsg, MAX_DB_STRING);
            
            NotifyClients(NX_NOTIFY_ALARM_CHANGED, &m_pAlarmList[i]);
            UpdateAlarmInDB(&m_pAlarmList[i]);

            bNewAlarm = FALSE;
            break;
         }

      Unlock();
   }

   if (bNewAlarm)
   {
      // Create new alarm structure
      memset(&alarm, 0, sizeof(NXC_ALARM));
      alarm.dwAlarmId = CreateUniqueId(IDG_ALARM);
      alarm.qwSourceEventId = pEvent->Id();
      alarm.dwSourceEventCode = pEvent->Code();
      alarm.dwSourceObject = pEvent->SourceId();
      alarm.dwCreationTime = (DWORD)time(NULL);
      alarm.dwLastChangeTime = alarm.dwCreationTime;
      alarm.nState = nState;
      alarm.nOriginalSeverity = iSeverity;
      alarm.nCurrentSeverity = iSeverity;
      alarm.dwRepeatCount = 1;
      alarm.nHelpDeskState = ALARM_HELPDESK_IGNORED;
		alarm.dwTimeout = dwTimeout;
		alarm.dwTimeoutEvent = dwTimeoutEvent;
      nx_strncpy(alarm.szMessage, pszExpMsg, MAX_DB_STRING);
      nx_strncpy(alarm.szKey, pszExpKey, MAX_DB_STRING);
      free(pszExpMsg);
      free(pszExpKey);

      // Add new alarm to active alarm list if needed
      if (alarm.nState != ALARM_STATE_TERMINATED)
      {
         Lock();

         m_dwNumAlarms++;
         m_pAlarmList = (NXC_ALARM *)realloc(m_pAlarmList, sizeof(NXC_ALARM) * m_dwNumAlarms);
         memcpy(&m_pAlarmList[m_dwNumAlarms - 1], &alarm, sizeof(NXC_ALARM));
         dwObjectId = alarm.dwSourceObject;

         Unlock();
      }

      // Save alarm to database
      pszExpMsg = EncodeSQLString(alarm.szMessage);
      pszExpKey = EncodeSQLString(alarm.szKey);
      pszEscRef = EncodeSQLString(alarm.szHelpDeskRef);
      sprintf(szQuery, "INSERT INTO alarms (alarm_id,creation_time,last_change_time,"
                       "source_object_id,source_event_code,message,original_severity,"
                       "current_severity,alarm_key,alarm_state,ack_by,hd_state,"
                       "hd_ref,repeat_count,term_by,timeout,timeout_event,source_event_id) VALUES "
                       "(%d,%d,%d,%d,%d,'%s',%d,%d,'%s',%d,%d,%d,'%s',%d,%d,%d,%d,"
#ifdef _WIN32
                       "%I64d)",
#else
                       "%lld)",
#endif
              alarm.dwAlarmId, alarm.dwCreationTime, alarm.dwLastChangeTime,
              alarm.dwSourceObject, alarm.dwSourceEventCode, pszExpMsg,
              alarm.nOriginalSeverity, alarm.nCurrentSeverity, pszExpKey,
              alarm.nState, alarm.dwAckByUser, alarm.nHelpDeskState, pszEscRef,
              alarm.dwRepeatCount, alarm.dwTermByUser, alarm.dwTimeout,
				  alarm.dwTimeoutEvent, alarm.qwSourceEventId);
      free(pszExpMsg);
      free(pszExpKey);
      free(pszEscRef);
      QueueSQLRequest(szQuery);

      // Notify connected clients about new alarm
      NotifyClients(NX_NOTIFY_NEW_ALARM, &alarm);
   }

   // Update status of related object if needed
   if ((dwObjectId != 0) && (alarm.nState != ALARM_STATE_TERMINATED))
      UpdateObjectStatus(dwObjectId);
}


//
// Acknowledge alarm with given ID
//

DWORD AlarmManager::AckById(DWORD dwAlarmId, DWORD dwUserId)
{
   DWORD i, dwObject, dwRet = RCC_INVALID_ALARM_ID;

   Lock();
   for(i = 0; i < m_dwNumAlarms; i++)
      if (m_pAlarmList[i].dwAlarmId == dwAlarmId)
      {
         if (m_pAlarmList[i].nState == ALARM_STATE_OUTSTANDING)
         {
            m_pAlarmList[i].nState = ALARM_STATE_ACKNOWLEDGED;
            m_pAlarmList[i].dwAckByUser = dwUserId;
            m_pAlarmList[i].dwLastChangeTime = (DWORD)time(NULL);
            dwObject = m_pAlarmList[i].dwSourceObject;
            NotifyClients(NX_NOTIFY_ALARM_CHANGED, &m_pAlarmList[i]);
            UpdateAlarmInDB(&m_pAlarmList[i]);
            dwRet = RCC_SUCCESS;
         }
         else
         {
            dwRet = RCC_ALARM_NOT_OUTSTANDING;
         }
         break;
      }
   Unlock();

   if (dwRet == RCC_SUCCESS)
      UpdateObjectStatus(dwObject);
   return dwRet;
}


//
// Terminate alarm with given ID
// Should return RCC which can be sent to client
//

DWORD AlarmManager::TerminateById(DWORD dwAlarmId, DWORD dwUserId)
{
   DWORD i, dwObject, dwRet = RCC_INVALID_ALARM_ID;

   Lock();
   for(i = 0; i < m_dwNumAlarms; i++)
      if (m_pAlarmList[i].dwAlarmId == dwAlarmId)
      {
         // If alarm is open in helpdesk, it cannot be terminated
         if (m_pAlarmList[i].nHelpDeskState != ALARM_HELPDESK_OPEN)
         {
            dwObject = m_pAlarmList[i].dwSourceObject;
            m_pAlarmList[i].dwTermByUser = dwUserId;
            m_pAlarmList[i].dwLastChangeTime = (DWORD)time(NULL);
            m_pAlarmList[i].nState = ALARM_STATE_TERMINATED;
            NotifyClients(NX_NOTIFY_ALARM_TERMINATED, &m_pAlarmList[i]);
            UpdateAlarmInDB(&m_pAlarmList[i]);
            m_dwNumAlarms--;
            memmove(&m_pAlarmList[i], &m_pAlarmList[i + 1], sizeof(NXC_ALARM) * (m_dwNumAlarms - i));
            dwRet = RCC_SUCCESS;
         }
         else
         {
            dwRet = RCC_ALARM_OPEN_IN_HELPDESK;
         }
         break;
      }
   Unlock();

   if (dwRet == RCC_SUCCESS)
      UpdateObjectStatus(dwObject);
   return dwRet;
}


//
// Terminate all alarms with given key
//

void AlarmManager::TerminateByKey(char *pszKey)
{
   DWORD i, j, dwNumObjects, *pdwObjectList, dwCurrTime;

   pdwObjectList = (DWORD *)malloc(sizeof(DWORD) * m_dwNumAlarms);

   Lock();
   dwCurrTime = (DWORD)time(NULL);
   for(i = 0, dwNumObjects = 0; i < m_dwNumAlarms; i++)
      if ((!strcmp(pszKey, m_pAlarmList[i].szKey)) &&
         (m_pAlarmList[i].nHelpDeskState != ALARM_HELPDESK_OPEN))
      {
         // Add alarm's source object to update list
         for(j = 0; j < dwNumObjects; j++)
         {
            if (pdwObjectList[j] == m_pAlarmList[i].dwSourceObject)
               break;
         }
         if (j == dwNumObjects)
         {
            pdwObjectList[dwNumObjects++] = m_pAlarmList[i].dwSourceObject;
         }

         // Terminate alarm
         m_pAlarmList[i].nState = ALARM_STATE_TERMINATED;
         m_pAlarmList[i].dwLastChangeTime = dwCurrTime;
         m_pAlarmList[i].dwTermByUser = 0;
         NotifyClients(NX_NOTIFY_ALARM_TERMINATED, &m_pAlarmList[i]);
         UpdateAlarmInDB(&m_pAlarmList[i]);
         m_dwNumAlarms--;
         memmove(&m_pAlarmList[i], &m_pAlarmList[i + 1], sizeof(NXC_ALARM) * (m_dwNumAlarms - i));
         i--;
      }
   Unlock();

   // Update status of objects
   for(i = 0; i < dwNumObjects; i++)
      UpdateObjectStatus(pdwObjectList[i]);
   free(pdwObjectList);
}


//
// Delete alarm with given ID
//

void AlarmManager::DeleteAlarm(DWORD dwAlarmId)
{
   DWORD i, dwObject;
   char szQuery[256];

   // Delete alarm from in-memory list
   Lock();
   for(i = 0; i < m_dwNumAlarms; i++)
      if (m_pAlarmList[i].dwAlarmId == dwAlarmId)
      {
         dwObject = m_pAlarmList[i].dwSourceObject;
         NotifyClients(NX_NOTIFY_ALARM_DELETED, &m_pAlarmList[i]);
         m_dwNumAlarms--;
         memmove(&m_pAlarmList[i], &m_pAlarmList[i + 1], sizeof(NXC_ALARM) * (m_dwNumAlarms - i));
         break;
      }
   Unlock();

   // Delete from database
   sprintf(szQuery, "DELETE FROM alarms WHERE alarm_id=%d", dwAlarmId);
   //DBQuery(g_hCoreDB, szQuery);
   QueueSQLRequest(szQuery);

   UpdateObjectStatus(dwObject);
}


//
// Update alarm information in database
//

void AlarmManager::UpdateAlarmInDB(NXC_ALARM *pAlarm)
{
   char szQuery[1024], *pszEscRef;

   pszEscRef = EncodeSQLString(pAlarm->szHelpDeskRef);
   sprintf(szQuery, "UPDATE alarms SET alarm_state=%d,ack_by=%d,term_by=%d,"
                    "last_change_time=%d,current_severity=%d,repeat_count=%d,"
                    "hd_state=%d,hd_ref='%s',timeout=%d,timeout_event=%d WHERE alarm_id=%d",
           pAlarm->nState, pAlarm->dwAckByUser, pAlarm->dwTermByUser,
           pAlarm->dwLastChangeTime, pAlarm->nCurrentSeverity,
           pAlarm->dwRepeatCount, pAlarm->nHelpDeskState, pszEscRef,
           pAlarm->dwTimeout, pAlarm->dwTimeoutEvent, pAlarm->dwAlarmId);
   free(pszEscRef);
   QueueSQLRequest(szQuery);
}


//
// Callback for client session enumeration
//

void AlarmManager::SendAlarmNotification(ClientSession *pSession, void *pArg)
{
   pSession->OnAlarmUpdate(((AlarmManager *)pArg)->m_dwNotifyCode,
                           ((AlarmManager *)pArg)->m_pNotifyAlarmInfo);
}


//
// Notify connected clients about changes
//

void AlarmManager::NotifyClients(DWORD dwCode, NXC_ALARM *pAlarm)
{
   m_dwNotifyCode = dwCode;
   m_pNotifyAlarmInfo = pAlarm;
   EnumerateClientSessions(SendAlarmNotification, this);
}


//
// Send all alarms to client
//

void AlarmManager::SendAlarmsToClient(DWORD dwRqId, BOOL bIncludeAck, ClientSession *pSession)
{
   DWORD i, dwUserId;
   NetObj *pObject;
   CSCPMessage msg;

   dwUserId = pSession->GetUserId();

   // Prepare message
   msg.SetCode(CMD_ALARM_DATA);
   msg.SetId(dwRqId);

   if (bIncludeAck)
   {
      // Request for all alarms including acknowledged,
      // so we have to load them from database
   }
   else
   {
      // Unacknowledged alarms can be sent directly from memory
      Lock();

      for(i = 0; i < m_dwNumAlarms; i++)
      {
         pObject = FindObjectById(m_pAlarmList[i].dwSourceObject);
         if (pObject != NULL)
         {
            if (pObject->CheckAccessRights(dwUserId, OBJECT_ACCESS_READ_ALARMS))
            {
               FillAlarmInfoMessage(&msg, &m_pAlarmList[i]);
               pSession->SendMessage(&msg);
               msg.DeleteAllVariables();
            }
         }
      }

      Unlock();
   }

   // Send end-of-list indicator
   msg.SetVariable(VID_ALARM_ID, (DWORD)0);
   pSession->SendMessage(&msg);
}


//
// Get source object for given alarm id
//

NetObj *AlarmManager::GetAlarmSourceObject(DWORD dwAlarmId)
{
   DWORD i, dwObjectId = 0;
   char szQuery[256];
   DB_RESULT hResult;

   // First, look at our in-memory list
   Lock();
   for(i = 0; i < m_dwNumAlarms; i++)
      if (m_pAlarmList[i].dwAlarmId == dwAlarmId)
      {
         dwObjectId = m_pAlarmList[i].dwSourceObject;
         break;
      }
   Unlock();

   // If not found, search database
   if (i == m_dwNumAlarms)
   {
      sprintf(szQuery, "SELECT source_object_id FROM alarms WHERE alarm_id=%d", dwAlarmId);
      hResult = DBSelect(g_hCoreDB, szQuery);
      if (hResult != NULL)
      {
         if (DBGetNumRows(hResult) > 0)
         {
            dwObjectId = DBGetFieldULong(hResult, 0, 0);
         }
         DBFreeResult(hResult);
      }
   }

   return FindObjectById(dwObjectId);
}


//
// Get most critical status among active alarms for given object
// Will return STATUS_UNKNOWN if there are no active alarms
//

int AlarmManager::GetMostCriticalStatusForObject(DWORD dwObjectId)
{
   DWORD i;
   int iStatus = STATUS_UNKNOWN;

   Lock();

   for(i = 0; i < m_dwNumAlarms; i++)
   {
      if ((m_pAlarmList[i].dwSourceObject == dwObjectId) &&
          ((m_pAlarmList[i].nCurrentSeverity > iStatus) || (iStatus == STATUS_UNKNOWN)))
      {
         iStatus = (int)m_pAlarmList[i].nCurrentSeverity;
      }
   }

   Unlock();
   return iStatus;
}


//
// Update object status after alarm acknowledgement or deletion
//

void AlarmManager::UpdateObjectStatus(DWORD dwObjectId)
{
   NetObj *pObject;

   pObject = FindObjectById(dwObjectId);
   if (pObject != NULL)
      pObject->CalculateCompoundStatus();
}


//
// Fill message with alarm stats
//

void AlarmManager::GetAlarmStats(CSCPMessage *pMsg)
{
   DWORD i, dwCount[5];

   Lock();
   pMsg->SetVariable(VID_NUM_ALARMS, m_dwNumAlarms);
   memset(dwCount, 0, sizeof(DWORD) * 5);
   for(i = 0; i < m_dwNumAlarms; i++)
      dwCount[m_pAlarmList[i].nCurrentSeverity]++;
   Unlock();
   pMsg->SetVariableToInt32Array(VID_ALARMS_BY_SEVERITY, 5, dwCount);
}


//
// Watchdog thread
//

void AlarmManager::WatchdogThread(void)
{
	DWORD i;
	time_t now;

	while(1)
	{
		if (ConditionWait(m_condShutdown, 1000))
			break;

		Lock();
		now = time(NULL);
	   for(i = 0; i < m_dwNumAlarms; i++)
		{
			if ((m_pAlarmList[i].dwTimeout > 0) &&
				 (m_pAlarmList[i].nState == ALARM_STATE_OUTSTANDING) &&
				 (((time_t)m_pAlarmList[i].dwLastChangeTime + (time_t)m_pAlarmList[i].dwTimeout) < now))
			{
				DbgPrintf(5, _T("Alarm timeout: alarm_id=%d, last_change=%d, timeout=%d, now=%d"),
				          m_pAlarmList[i].dwAlarmId, m_pAlarmList[i].dwLastChangeTime,
							 m_pAlarmList[i].dwTimeout, now);

				PostEvent(m_pAlarmList[i].dwTimeoutEvent, m_pAlarmList[i].dwSourceObject, "dssd",
				          m_pAlarmList[i].dwAlarmId, m_pAlarmList[i].szMessage,
							 m_pAlarmList[i].szKey, m_pAlarmList[i].dwSourceEventCode);
				m_pAlarmList[i].dwTimeout = 0;	// Disable repeated timeout events
				UpdateAlarmInDB(&m_pAlarmList[i]);
			}
		}
		Unlock();
	}
}
