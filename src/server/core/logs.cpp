/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2009 Victor Kirhenshtein
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
** File: logs.cpp
**
**/

#include "nxcore.h"


//
// Defined logs
//

static NXCORE_LOG s_logs[] =
{
	{ _T("EventLog"), _T("event_log"), SYSTEM_ACCESS_VIEW_EVENT_LOG,
		{
			{ "event_timestamp", "Time", LC_TIMESTAMP },
			{ "event_source", "Source", LC_OBJECT_ID },
			{ "event_code", "Event", LC_EVENT_CODE },
			{ "event_severity", "Severity", LC_SEVERITY },
			{ "event_message", "Message", LC_TEXT },
			{ NULL, NULL, 0 }
		}
	},
	{ NULL, NULL, 0 }
};


//
// Registered log handles
//

struct LOG_HANDLE_REGISTRATION
{
	LogHandle *handle;
	DWORD sessionId;	
};
int s_regListSize = 0;
LOG_HANDLE_REGISTRATION *s_regList = NULL;
MUTEX s_regListMutex = INVALID_MUTEX_HANDLE;


//
// Init log access
//

void InitLogAccess()
{
	s_regListMutex = MutexCreate();
}


//
// Register log handle
//

static int RegisterLogHandle(LogHandle *handle, ClientSession *session)
{
	int i;

	MutexLock(s_regListMutex, INFINITE);

	for(i = 0; i < s_regListSize; i++)
		if (s_regList[i].handle == NULL)
			break;
	if (i == s_regListSize)
	{
		s_regListSize += 10;
		s_regList = (LOG_HANDLE_REGISTRATION *)realloc(s_regList, sizeof(LOG_HANDLE_REGISTRATION) * s_regListSize);
	}

	s_regList[i].handle = handle;
	s_regList[i].sessionId = session->getIndex();

	MutexUnlock(s_regListMutex);
	return i;
}


//
// Open log by name
//

int OpenLog(const TCHAR *name, ClientSession *session, DWORD *rcc)
{
	for(int i = 0; s_logs[i].name != NULL; i++)
	{	
		if (!_tcsicmp(name, s_logs[i].name))
		{
			if (session->checkSysAccessRights(s_logs[i].requiredAccess))
			{
				*rcc = RCC_SUCCESS;
				LogHandle *handle = new LogHandle(&s_logs[i]);
				return RegisterLogHandle(handle, session);
			}
			else
			{
				*rcc = RCC_ACCESS_DENIED;
				return -1;
			}
		}
	}
	*rcc = RCC_UNKNOWN_LOG_NAME;
	return -1;
}


//
// Close log
//

DWORD CloseLog(ClientSession *session, int logHandle)
{
	DWORD rcc = RCC_INVALID_LOG_HANDLE;

	MutexLock(s_regListMutex, INFINITE);

	if ((logHandle >= 0) && (logHandle < s_regListSize) &&
	    (s_regList[logHandle].sessionId == session->getIndex()) &&
		 (s_regList[logHandle].handle != NULL))
	{
		s_regList[logHandle].handle->lock();
		s_regList[logHandle].handle->unlock();
		delete s_regList[logHandle].handle;
		s_regList[logHandle].handle = NULL;
		rcc = RCC_SUCCESS;
	}

	MutexUnlock(s_regListMutex);
	return rcc;
}


//
// Acqure log handle object
// Caller must call LogHandle::unlock() when it finish work with acquired object
//

LogHandle *AcquireLogHandleObject(ClientSession *session, int logHandle)
{
	LogHandle *object = NULL;

	MutexLock(s_regListMutex, INFINITE);

	if ((logHandle >= 0) && (logHandle < s_regListSize) &&
	    (s_regList[logHandle].sessionId == session->getIndex()) &&
		 (s_regList[logHandle].handle != NULL))
	{
		object = s_regList[logHandle].handle;
		object->lock();
	}

	MutexUnlock(s_regListMutex);
	return object;
}
