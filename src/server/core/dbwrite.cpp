/* 
** NetXMS - Network Management System
** Copyright (C) 2003, 2004, 2005, 2006 Victor Kirhenshtein
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
** File: dbwrite.cpp
**
**/

#include "nxcore.h"


//
// Constants
//

#define MAX_DB_WRITERS     16


//
// Global variables
//

Queue *g_pLazyRequestQueue = NULL;


//
// Static data
//

static int m_iNumWriters = 1;
static THREAD m_hWriteThreadList[MAX_DB_WRITERS];


//
// Put SQL request into queue for later execution
//

void NXCORE_EXPORTABLE QueueSQLRequest(char *szQuery)
{
   g_pLazyRequestQueue->Put(strdup(szQuery));
}


//
// Database "lazy" write thread
//

static THREAD_RESULT THREAD_CALL DBWriteThread(void *pArg)
{
   char *pQuery;
   DB_HANDLE hdb;

   if (g_dwFlags & AF_ENABLE_MULTIPLE_DB_CONN)
   {
      hdb = DBConnect();
      if (hdb == NULL)
      {
         WriteLog(MSG_DB_CONNFAIL, EVENTLOG_ERROR_TYPE, NULL);
         return THREAD_OK;
      }
   }
   else
   {
      hdb = g_hCoreDB;
   }

   while(1)
   {
      pQuery = (char *)g_pLazyRequestQueue->GetOrBlock();
      if (pQuery == INVALID_POINTER_VALUE)   // End-of-job indicator
         break;

      DBQuery(hdb, pQuery);
      free(pQuery);
   }

   if (g_dwFlags & AF_ENABLE_MULTIPLE_DB_CONN)
   {
      DBDisconnect(hdb);
   }
   return THREAD_OK;
}


//
// Start writer thread
//

void StartDBWriter(void)
{
   int i;

   if (g_dwFlags & AF_ENABLE_MULTIPLE_DB_CONN)
   {
      m_iNumWriters = ConfigReadInt("NumberOfDatabaseWriters", 1);
      if (m_iNumWriters < 1)
         m_iNumWriters = 1;
      if (m_iNumWriters > MAX_DB_WRITERS)
         m_iNumWriters = MAX_DB_WRITERS;
   }

   for(i = 0; i < m_iNumWriters; i++)
      m_hWriteThreadList[i] = ThreadCreateEx(DBWriteThread, 0, NULL);
}


//
// Stop writer thread and wait while all queries will be executed
//

void StopDBWriter(void)
{
   int i;

   for(i = 0; i < m_iNumWriters; i++)
      g_pLazyRequestQueue->Put(INVALID_POINTER_VALUE);
   for(i = 0; i < m_iNumWriters; i++)
      ThreadJoin(m_hWriteThreadList[i]);
}
