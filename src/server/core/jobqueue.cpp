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
** File: jobmgr.cpp
**
**/

#include "nxcore.h"


//
// Constructor
//

ServerJobQueue::ServerJobQueue()
{
	m_jobCount = 0;
	m_jobList = NULL;
	m_accessMutex = MutexCreate();
}


//
// Destructor
//

ServerJobQueue::~ServerJobQueue()
{
	int i;

	for(i = 0; i < m_jobCount; i++)
	{
		m_jobList[i]->cancel();
		delete m_jobList[i];
	}
	safe_free(m_jobList);

	MutexDestroy(m_accessMutex);
}


//
// Add job
//

void ServerJobQueue::add(ServerJob *job)
{
	MutexLock(m_accessMutex, INFINITE);
	m_jobList = (ServerJob **)realloc(m_jobList, sizeof(ServerJob *) * (m_jobCount + 1));
	m_jobList[m_jobCount] = job;
	m_jobCount++;
	job->setOwningQueue(this);
	MutexUnlock(m_accessMutex);

	DbgPrintf(4, _T("Job %d added to queue (node=%d, type=%s, description=\"%s\")"),
	          job->getId(), job->getRemoteNode(), job->getType(), job->getDescription());

	runNext();
}


//
// Run next job if possible
//

void ServerJobQueue::runNext()
{
	int i;

	MutexLock(m_accessMutex, INFINITE);
	for(i = 0; i < m_jobCount; i++)
		if (m_jobList[i]->getStatus() != JOB_ON_HOLD)
			break;
	if ((i < m_jobCount) && (m_jobList[i]->getStatus() == JOB_PENDING))
		m_jobList[i]->start();
	MutexUnlock(m_accessMutex);
}


//
// Handler for job completion
//

void ServerJobQueue::jobCompleted(ServerJob *job)
{
	int i;

	MutexLock(m_accessMutex, INFINITE);
	for(i = 0; i < m_jobCount; i++)
		if (m_jobList[i] == job)
		{
			if ((job->getStatus() == JOB_COMPLETED) ||
				 (job->getStatus() == JOB_CANCEL_PENDING))
			{
				// Delete and remove from list
				delete job;
				m_jobCount--;
				memmove(&m_jobList[i], &m_jobList[i + 1], sizeof(ServerJob *) * (m_jobCount - i));
			}
			break;
		}
	MutexUnlock(m_accessMutex);

	runNext();
}


//
// Cancel job
//

bool ServerJobQueue::cancel(DWORD jobId)
{
	int i;
	bool success = false;

	MutexLock(m_accessMutex, INFINITE);
	for(i = 0; i < m_jobCount; i++)
		if (m_jobList[i]->getId()  == jobId)
		{
			if (m_jobList[i]->cancel())
			{
				DbgPrintf(4, _T("Job %d cancelled (node=%d, type=%s, description=\"%s\")"),
							 m_jobList[i]->getId(), m_jobList[i]->getRemoteNode(), m_jobList[i]->getType(), m_jobList[i]->getDescription());

				if (m_jobList[i]->getStatus() != JOB_CANCEL_PENDING)
				{
					// Delete and remove from list
					delete m_jobList[i];
					m_jobCount--;
					memmove(&m_jobList[i], &m_jobList[i + 1], sizeof(ServerJob *) * (m_jobCount - i));
				}
				success = true;
			}
			break;
		}
	MutexUnlock(m_accessMutex);

	runNext();
	return success;
}


//
// Find job by ID
//

ServerJob *ServerJobQueue::findJob(DWORD jobId)
{
	int i;
	ServerJob *job = NULL;

	MutexLock(m_accessMutex, INFINITE);
	for(i = 0; i < m_jobCount; i++)
		if (m_jobList[i]->getId()  == jobId)
		{
			job = m_jobList[i];
			break;
		}
	MutexUnlock(m_accessMutex);

	return job;
}


//
// Fill NXCP message with jobs' information
// Increments base variable id; returns number of jobs added to message
//

DWORD ServerJobQueue::fillMessage(CSCPMessage *msg, DWORD *varIdBase)
{
	DWORD id = *varIdBase;
	int i;

	MutexLock(m_accessMutex, INFINITE);
	for(i = 0; i < m_jobCount; i++, id += 3)
	{
		msg->SetVariable(id++, m_jobList[i]->getId());
		msg->SetVariable(id++, m_jobList[i]->getType());
		msg->SetVariable(id++, m_jobList[i]->getDescription());
		msg->SetVariable(id++, m_jobList[i]->getRemoteNode());
		msg->SetVariable(id++, (WORD)m_jobList[i]->getStatus());
		msg->SetVariable(id++, (WORD)m_jobList[i]->getProgress());
		msg->SetVariable(id++, m_jobList[i]->getFailureMessage());
	}
	MutexUnlock(m_accessMutex);
	*varIdBase = id;
	return i;
}
