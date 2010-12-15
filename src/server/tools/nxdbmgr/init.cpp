/* 
** nxdbmgr - NetXMS database manager
** Copyright (C) 2004-2010 Victor Kirhenshtein
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
** File: init.cpp
**
**/

#include "nxdbmgr.h"


//
// Check if query is empty
//

static BOOL IsEmptyQuery(const char *pszQuery)
{
   const char *ptr;

   for(ptr = pszQuery; *ptr != 0; ptr++)
      if ((*ptr != ' ') && (*ptr != '\t') && (*ptr != '\r') && (*ptr != '\n'))
         return FALSE;
   return TRUE;
}


//
// Find end of query in batch
//

static BYTE *FindEndOfQuery(BYTE *pStart, BYTE *pBatchEnd)
{
   BYTE *ptr;
   int iState;

   for(ptr = pStart, iState = 0; (ptr < pBatchEnd) && (iState != -1); ptr++)
   {
      switch(iState)
      {
         case 0:
            if (*ptr == '\'')
               iState = 1;
            if (*ptr == ';')
               iState = -1;
            break;
         case 1:
            if (*ptr == '\'')
               iState = 0;
            break;
      }
   }

   *(ptr - 1) = 0;
   return ptr + 1;
}


//
// Execute SQL batch file
//

BOOL ExecSQLBatch(const char *pszFile)
{
   BYTE *pBatch, *pQuery, *pNext;
   DWORD dwSize;
   BOOL bResult = FALSE;

   pBatch = LoadFileA(pszFile, &dwSize);
   if (pBatch != NULL)
   {
      for(pQuery = pBatch; pQuery < pBatch + dwSize; pQuery = pNext)
      {
         pNext = FindEndOfQuery(pQuery, pBatch + dwSize);
         if (!IsEmptyQuery((char *)pQuery))
         {
#ifdef UNICODE
				WCHAR *wcQuery = WideStringFromMBString((char *)pQuery);
            bResult = SQLQuery(wcQuery);
				free(wcQuery);
#else
            bResult = SQLQuery(pQuery);
#endif
            if (!bResult)
               pNext = pBatch + dwSize;
         }
      }
      free(pBatch);
   }
   else
   {
      _tprintf(_T("ERROR: Cannot load SQL command file %hs\n"), pszFile);
   }
   return bResult;
}


//
// Initialize database
//

void InitDatabase(const char *pszInitFile)
{
   uuid_t guid;
   TCHAR szQuery[256], szGUID[64];

   _tprintf(_T("Initializing database...\n"));
   if (!ExecSQLBatch(pszInitFile))
      goto init_failed;

   // Generate GUID for user "admin"
   uuid_generate(guid);
   _sntprintf(szQuery, 256, _T("UPDATE users SET guid='%s' WHERE id=0"),
              uuid_to_string(guid, szGUID));
   if (!SQLQuery(szQuery))
      goto init_failed;

   // Generate GUID for "everyone" group
   uuid_generate(guid);
   _sntprintf(szQuery, 256, _T("UPDATE user_groups SET guid='%s' WHERE id=%d"),
              uuid_to_string(guid, szGUID), GROUP_EVERYONE);
   if (!SQLQuery(szQuery))
      goto init_failed;

   _tprintf(_T("Database initialized successfully\n"));
   return;

init_failed:
   _tprintf(_T("Database initialization failed\n"));
}
