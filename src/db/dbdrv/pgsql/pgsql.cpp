/* 
** PostgreSQL Database Driver
** Copyright (C) 2003 - 2010 Victor Kirhenshtein and Alex Kirhenshtein
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
** File: pgsql.cpp
**
**/

#include "pgsqldrv.h"

#ifdef _WIN32
#pragma warning(disable : 4996)
#endif

DECLARE_DRIVER_HEADER("PGSQL")


//
// Prepare string for using in SQL query - enclose in quotes and escape as needed
//

extern "C" WCHAR EXPORT *DrvPrepareStringW(const WCHAR *str)
{
	int len = (int)wcslen(str) + 3;   // + two quotes and \0 at the end
	int bufferSize = len + 128;
	WCHAR *out = (WCHAR *)malloc(bufferSize * sizeof(WCHAR));
	out[0] = L'\'';

	const WCHAR *src = str;
	int outPos;
	for(outPos = 1; *src != 0; src++)
	{
		long chval = *src;
		if (chval < 32)
		{
			WCHAR buffer[8];

			swprintf(buffer, 8, L"\\%03o", chval);
			len += 4;
			if (len >= bufferSize)
			{
				bufferSize += 128;
				out = (WCHAR *)realloc(out, bufferSize * sizeof(WCHAR));
			}
			memcpy(&out[outPos], buffer, 4 * sizeof(WCHAR));
			outPos += 4;
		}
		else if (*src == L'\'')
		{
			len++;
			if (len >= bufferSize)
			{
				bufferSize += 128;
				out = (WCHAR *)realloc(out, bufferSize * sizeof(WCHAR));
			}
			out[outPos++] = L'\'';
			out[outPos++] = L'\'';
		}
		else if (*src == L'\\')
		{
			len++;
			if (len >= bufferSize)
			{
				bufferSize += 128;
				out = (WCHAR *)realloc(out, bufferSize * sizeof(WCHAR));
			}
			out[outPos++] = L'\\';
			out[outPos++] = L'\\';
		}
		else
		{
			out[outPos++] = *src;
		}
	}
	out[outPos++] = L'\'';
	out[outPos++] = 0;

	return out;
}

extern "C" char EXPORT *DrvPrepareStringA(const char *str)
{
	int len = (int)_tcslen(str) + 3;   // + two quotes and \0 at the end
	int bufferSize = len + 128;
	char *out = (char *)malloc(bufferSize);
	out[0] = '\'';

	const char *src = str;
	int outPos;
	for(outPos = 1; *src != 0; src++)
	{
		long chval = (long)(*((unsigned char *)src));
		if (chval < 32)
		{
			TCHAR buffer[8];

			snprintf(buffer, 8, "\\%03o", chval);
			len += 4;
			if (len >= bufferSize)
			{
				bufferSize += 128;
				out = (char *)realloc(out, bufferSize);
			}
			memcpy(&out[outPos], buffer, 4);
			outPos += 4;
		}
		else if (*src == '\'')
		{
			len++;
			if (len >= bufferSize)
			{
				bufferSize += 128;
				out = (char *)realloc(out, bufferSize);
			}
			out[outPos++] = '\'';
			out[outPos++] = '\'';
		}
		else if (*src == '\\')
		{
			len++;
			if (len >= bufferSize)
			{
				bufferSize += 128;
				out = (char *)realloc(out, bufferSize);
			}
			out[outPos++] = '\\';
			out[outPos++] = '\\';
		}
		else
		{
			out[outPos++] = *src;
		}
	}
	out[outPos++] = '\'';
	out[outPos++] = 0;

	return out;
}


//
// Initialize driver
//

extern "C" BOOL EXPORT DrvInit(const char *cmdLine)
{
	return TRUE;
}

//
// Unload handler
//

extern "C" void EXPORT DrvUnload()
{
}


//
// Connect to database
//

extern "C" DBDRV_CONNECTION EXPORT DrvConnect(
		char *szHost,
		char *szLogin,
		char *szPassword,
		char *szDatabase)
{
	PG_CONN *pConn;

	if (szDatabase == NULL || *szDatabase == 0)
	{
		return NULL;
	}

	pConn = (PG_CONN *)malloc(sizeof(PG_CONN));
	if (pConn != NULL)
	{
		// should be replaced with PQconnectdb();
		pConn->pHandle = PQsetdbLogin(szHost, NULL, NULL, NULL, 
				szDatabase, szLogin, szPassword);

		if (PQstatus(pConn->pHandle) == CONNECTION_BAD)
		{
			free(pConn);
			pConn = NULL;
		}
		else
		{
			PGresult	*pResult;

			pResult = PQexec(pConn->pHandle, "SET standard_conforming_strings TO off");
			PQclear(pResult);
			
			pResult = PQexec(pConn->pHandle, "SET escape_string_warning TO off");
			PQclear(pResult);

			PQsetClientEncoding(pConn->pHandle, "UTF8");

   		pConn->mutexQueryLock = MutexCreate();
         pConn->pFetchBuffer = NULL;
		}
	}

   return (DBDRV_CONNECTION)pConn;
}


//
// Disconnect from database
//

extern "C" void EXPORT DrvDisconnect(DBDRV_CONNECTION pConn)
{
	if (pConn != NULL)
	{
   	PQfinish(((PG_CONN *)pConn)->pHandle);
     	MutexDestroy(((PG_CONN *)pConn)->mutexQueryLock);
      free(pConn);
	}
}


//
// Perform non-SELECT query
//

static BOOL UnsafeDrvQuery(PG_CONN *pConn, const char *szQuery, WCHAR *errorText)
{
	PGresult	*pResult;

	pResult = PQexec(pConn->pHandle, szQuery);

	if (pResult == NULL)
	{
		if (errorText != NULL)
			wcsncpy(errorText, L"Internal error (pResult is NULL in UnsafeDrvQuery)", DBDRV_MAX_ERROR_TEXT);
		return FALSE;
	}

	if (PQresultStatus(pResult) != PGRES_COMMAND_OK)
	{
		if (errorText != NULL)
		{
			MultiByteToWideChar(CP_UTF8, 0, PQerrorMessage(pConn->pHandle), -1, errorText, DBDRV_MAX_ERROR_TEXT);
			errorText[DBDRV_MAX_ERROR_TEXT - 1] = 0;
			RemoveTrailingCRLFW(errorText);
		}
		PQclear(pResult);
		return FALSE;
	}

	PQclear(pResult);
	if (errorText != NULL)
		*errorText = 0;
   return TRUE;
}

extern "C" DWORD EXPORT DrvQuery(PG_CONN *pConn, WCHAR *pwszQuery, WCHAR *errorText)
{
	DWORD dwRet = DBERR_INVALID_HANDLE;
   char *pszQueryUTF8;

	if ((pConn != NULL) && (pwszQuery != NULL))
	{
      pszQueryUTF8 = UTF8StringFromWideString(pwszQuery);
		MutexLock(pConn->mutexQueryLock, INFINITE);
		if (UnsafeDrvQuery(pConn, pszQueryUTF8, errorText))
      {
         dwRet = DBERR_SUCCESS;
      }
      else
      {
         dwRet = (PQstatus(pConn->pHandle) == CONNECTION_BAD) ? DBERR_CONNECTION_LOST : DBERR_OTHER_ERROR;
      }
		MutexUnlock(pConn->mutexQueryLock);
      free(pszQueryUTF8);
	}

	return dwRet;
}


//
// Perform SELECT query
//

static DBDRV_RESULT UnsafeDrvSelect(PG_CONN *pConn, const char *szQuery, WCHAR *errorText)
{
	PGresult	*pResult;

	pResult = PQexec(((PG_CONN *)pConn)->pHandle, szQuery);

	if (pResult == NULL)
	{
		if (errorText != NULL)
			wcsncpy(errorText, L"Internal error (pResult is NULL in UnsafeDrvSelect)", DBDRV_MAX_ERROR_TEXT);
		return NULL;
	}

	if ((PQresultStatus(pResult) != PGRES_COMMAND_OK) &&
	    (PQresultStatus(pResult) != PGRES_TUPLES_OK))
	{
		
		if (errorText != NULL)
		{
			MultiByteToWideChar(CP_UTF8, 0, PQerrorMessage(pConn->pHandle), -1, errorText, DBDRV_MAX_ERROR_TEXT);
			errorText[DBDRV_MAX_ERROR_TEXT - 1] = 0;
			RemoveTrailingCRLFW(errorText);
		}
		PQclear(pResult);
		return NULL;
	}

	if (errorText != NULL)
		*errorText = 0;
   return (DBDRV_RESULT)pResult;
}

extern "C" DBDRV_RESULT EXPORT DrvSelect(PG_CONN *pConn, WCHAR *pwszQuery, DWORD *pdwError, WCHAR *errorText)
{
	DBDRV_RESULT pResult;
   char *pszQueryUTF8;

   if (pConn == NULL)
   {
      *pdwError = DBERR_INVALID_HANDLE;
      return NULL;
   }

   pszQueryUTF8 = UTF8StringFromWideString(pwszQuery);
	MutexLock(pConn->mutexQueryLock, INFINITE);
	pResult = UnsafeDrvSelect(pConn, pszQueryUTF8, errorText);
   if (pResult != NULL)
   {
      *pdwError = DBERR_SUCCESS;
   }
   else
   {
      *pdwError = (PQstatus(pConn->pHandle) == CONNECTION_BAD) ? DBERR_CONNECTION_LOST : DBERR_OTHER_ERROR;
   }
	MutexUnlock(pConn->mutexQueryLock);
   free(pszQueryUTF8);

   return pResult;
}


//
// Get field length from result
//

extern "C" LONG EXPORT DrvGetFieldLength(DBDRV_RESULT pResult, int nRow, int nColumn)
{
   char *pszValue;

	if (pResult == NULL)
      return -1;

   pszValue = PQgetvalue((PGresult *)pResult, nRow, nColumn);
   return (pszValue != NULL) ? (LONG)strlen(pszValue) : (LONG)-1;
}


//
// Get field value from result
//

extern "C" WCHAR EXPORT *DrvGetField(DBDRV_RESULT pResult, int nRow, int nColumn,
                                     WCHAR *pBuffer, int nBufLen)
{
	if (pResult == NULL)
      return NULL;

   MultiByteToWideChar(CP_UTF8, 0, PQgetvalue((PGresult *)pResult, nRow, nColumn),
                       -1, pBuffer, nBufLen);
   pBuffer[nBufLen - 1] = 0;
	return pBuffer;
}


//
// Get number of rows in result
//

extern "C" int EXPORT DrvGetNumRows(DBDRV_RESULT pResult)
{
	return (pResult != NULL) ? PQntuples((PGresult *)pResult) : 0;
}


//
// Get column count in query result
//

extern "C" int EXPORT DrvGetColumnCount(DBDRV_RESULT hResult)
{
	return (hResult != NULL) ? PQnfields((PGresult *)hResult) : 0;
}


//
// Get column name in query result
//

extern "C" const char EXPORT *DrvGetColumnName(DBDRV_RESULT hResult, int column)
{
	return (hResult != NULL) ? PQfname((PGresult *)hResult, column) : NULL;
}


//
// Free SELECT results
//

extern "C" void EXPORT DrvFreeResult(DBDRV_RESULT pResult)
{
	if (pResult != NULL)
	{
   	PQclear((PGresult *)pResult);
	}
}


//
// Perform asynchronous SELECT query
//

extern "C" DBDRV_ASYNC_RESULT EXPORT DrvAsyncSelect(PG_CONN *pConn, WCHAR *pwszQuery, DWORD *pdwError, WCHAR *errorText)
{
	BOOL bSuccess = FALSE;
   char *pszReq, *pszQueryUTF8;
   static char szDeclareCursor[] = "DECLARE cur1 CURSOR FOR ";

	if (pConn == NULL)
		return NULL;

	MutexLock(pConn->mutexQueryLock, INFINITE);

	if (UnsafeDrvQuery(pConn, "BEGIN", errorText))
   {
      pszQueryUTF8 = UTF8StringFromWideString(pwszQuery);
   	pszReq = (char *)malloc(strlen(pszQueryUTF8) + sizeof(szDeclareCursor));
	   if (pszReq != NULL)
	   {
		   strcpy(pszReq, szDeclareCursor);
		   strcat(pszReq, pszQueryUTF8);
		   if (UnsafeDrvQuery(pConn, pszReq, errorText))
		   {
			   bSuccess = TRUE;
		   }
		   free(pszReq);
	   }
      free(pszQueryUTF8);

      if (!bSuccess)
         UnsafeDrvQuery(pConn, "ROLLBACK", NULL);
   }

	if (bSuccess)
   {
      *pdwError = DBERR_SUCCESS;
   }
   else
	{
      *pdwError = (PQstatus(pConn->pHandle) == CONNECTION_BAD) ? DBERR_CONNECTION_LOST : DBERR_OTHER_ERROR;
		MutexUnlock(pConn->mutexQueryLock);
		return NULL;
	}

	return (DBDRV_ASYNC_RESULT)pConn;
}


//
// Fetch next result line from asynchronous SELECT results
//

extern "C" BOOL EXPORT DrvFetch(DBDRV_ASYNC_RESULT pConn)
{
   BOOL bResult = TRUE;
   
   if (pConn == NULL)
   {
      bResult = FALSE;
   }
   else
   {
      if (((PG_CONN *)pConn)->pFetchBuffer != NULL)
         PQclear(((PG_CONN *)pConn)->pFetchBuffer);
		((PG_CONN *)pConn)->pFetchBuffer =
			(PGresult *)UnsafeDrvSelect((PG_CONN *)pConn, "FETCH cur1", NULL);
		if (((PG_CONN *)pConn)->pFetchBuffer != NULL)
      {
         if (DrvGetNumRows(((PG_CONN *)pConn)->pFetchBuffer) <= 0)
		   {
            PQclear(((PG_CONN *)pConn)->pFetchBuffer);
            ((PG_CONN *)pConn)->pFetchBuffer = NULL;
			   bResult = FALSE;
		   }
      }
      else
      {
         bResult = FALSE;
      }
   }
   return bResult;
}


//
// Get field length from async quety result
//

extern "C" LONG EXPORT DrvGetFieldLengthAsync(PG_CONN *pConn, int nColumn)
{
	if ((pConn == NULL) || (pConn->pFetchBuffer == NULL))
	{
		return 0;
	}

	// validate column index
	if (nColumn >= PQnfields(pConn->pFetchBuffer))
	{
		return 0;
	}

	char *value = PQgetvalue(pConn->pFetchBuffer, 0, nColumn);
	if (value == NULL)
	{
		return 0;
	}

	return strlen(value);
}


//
// Get field from current row in async query result
//

extern "C" WCHAR EXPORT *DrvGetFieldAsync(
		PG_CONN *pConn,
		int nColumn,
		WCHAR *pBuffer,
		int nBufSize)
{
	char *pszResult;

	if ((pConn == NULL) || (pConn->pFetchBuffer == NULL))
	{
		return NULL;
	}

	// validate column index
	if (nColumn >= PQnfields(pConn->pFetchBuffer))
	{
		return NULL;
	}

	// FIXME: correct processing of binary fields
	// PQfformat not supported in 7.3
#ifdef HAVE_PQFFORMAT
	if (PQfformat(pConn->pFetchBuffer, nColumn) != 0)
#else
	if (PQbinaryTuples(pConn->pFetchBuffer) != 0)
#endif
	{
		//fprintf(stderr, "db:postgres:binary fields not supported\n");
		//fflush(stderr);
		// abort();
		return NULL;
	}

	pszResult = PQgetvalue(pConn->pFetchBuffer, 0, nColumn);
	if (pszResult == NULL)
	{
		return NULL;
	}

	// Now get column data
   MultiByteToWideChar(CP_UTF8, 0, pszResult, -1, pBuffer, nBufSize);
   pBuffer[nBufSize - 1] = 0;

   return pBuffer;
}


//
// Get column count in async query result
//

extern "C" int EXPORT DrvGetColumnCountAsync(DBDRV_ASYNC_RESULT hResult)
{
	return ((hResult != NULL) && (((PG_CONN *)hResult)->pFetchBuffer != NULL))? PQnfields(((PG_CONN *)hResult)->pFetchBuffer) : 0;
}


//
// Get column name in async query result
//

extern "C" const char EXPORT *DrvGetColumnNameAsync(DBDRV_ASYNC_RESULT hResult, int column)
{
	return ((hResult != NULL) && (((PG_CONN *)hResult)->pFetchBuffer != NULL))? PQfname(((PG_CONN *)hResult)->pFetchBuffer, column) : NULL;
}


//
// Destroy result of async query
//

extern "C" void EXPORT DrvFreeAsyncResult(PG_CONN *pConn)
{
   if (pConn != NULL)
   {
      if (pConn->pFetchBuffer != NULL)
      {
		   PQclear(pConn->pFetchBuffer);
         pConn->pFetchBuffer = NULL;
      }
		UnsafeDrvQuery(pConn, "CLOSE cur1", NULL);
		UnsafeDrvQuery(pConn, "COMMIT", NULL);
   }
	MutexUnlock(pConn->mutexQueryLock);
}


//
// Begin transaction
//

extern "C" DWORD EXPORT DrvBegin(PG_CONN *pConn)
{
   DWORD dwResult;

	if (pConn == NULL)
      return DBERR_INVALID_HANDLE;

	MutexLock(pConn->mutexQueryLock, INFINITE);
	if (UnsafeDrvQuery(pConn, "BEGIN", NULL))
   {
      dwResult = DBERR_SUCCESS;
   }
   else
   {
      dwResult = (PQstatus(pConn->pHandle) == CONNECTION_BAD) ? DBERR_CONNECTION_LOST : DBERR_OTHER_ERROR;
   }
	MutexUnlock(pConn->mutexQueryLock);
   return dwResult;
}


//
// Commit transaction
//

extern "C" DWORD EXPORT DrvCommit(PG_CONN *pConn)
{
   BOOL bRet;

	if (pConn == NULL)
      return DBERR_INVALID_HANDLE;

	MutexLock(pConn->mutexQueryLock, INFINITE);
	bRet = UnsafeDrvQuery(pConn, "COMMIT", NULL);
	MutexUnlock(pConn->mutexQueryLock);
   return bRet ? DBERR_SUCCESS : DBERR_OTHER_ERROR;
}


//
// Rollback transaction
//

extern "C" DWORD EXPORT DrvRollback(PG_CONN *pConn)
{
   BOOL bRet;

	if (pConn == NULL)
      return DBERR_INVALID_HANDLE;

	MutexLock(pConn->mutexQueryLock, INFINITE);
	bRet = UnsafeDrvQuery(pConn, "ROLLBACK", NULL);
	MutexUnlock(pConn->mutexQueryLock);
   return bRet ? DBERR_SUCCESS : DBERR_OTHER_ERROR;
}


//
// DLL Entry point
//

#ifdef _WIN32

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
   if (dwReason == DLL_PROCESS_ATTACH)
      DisableThreadLibraryCalls(hInstance);
   return TRUE;
}

#endif
