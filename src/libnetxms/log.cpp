/* 
** NetXMS - Network Management System
** Utility Library
** Copyright (C) 2003-2010 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published
** by the Free Software Foundation; either version 3 of the License, or
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
** File: log.cpp
**
**/

#include "libnetxms.h"

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif


//
// Static data
//

#ifdef _WIN32
static HANDLE m_eventLogHandle = NULL;
static HMODULE m_msgModuleHandle = NULL;
#else
static unsigned int m_numMessages;
static const TCHAR **m_messages;
#endif
static TCHAR m_logFileName[MAX_PATH] = _T("");
static FILE *m_logFileHandle = NULL;
static MUTEX m_mutexLogAccess = INVALID_MUTEX_HANDLE;
static DWORD m_flags = 0;
static int m_maxLogSize = 4096 * 1024;	// 4 MB
static int m_logHistorySize = 4;		// Keep up 4 previous log files


//
// Set log rotation policy
// Setting log size to 0 disables log rotation
//

BOOL LIBNETXMS_EXPORTABLE nxlog_set_rotation_policy(int maxLogSize, int historySize)
{
	// Validate parameters
	if (((maxLogSize != 0) && (maxLogSize < 1024)) || (historySize < 0) || (historySize > 9))
		return FALSE;

	m_maxLogSize = maxLogSize;
	m_logHistorySize = historySize;

	return TRUE;
}


//
// Rotate log
//

static BOOL RotateLog(BOOL needLock)
{
	int i;
	TCHAR oldName[MAX_PATH], newName[MAX_PATH];

	if (m_flags & NXLOG_USE_SYSLOG)
		return FALSE;	// Cannot rotate system logs

	if (needLock)
		MutexLock(m_mutexLogAccess, INFINITE);

	if ((m_logFileHandle != NULL) && (m_flags & NXLOG_IS_OPEN))
	{
		fclose(m_logFileHandle);
		m_flags &= ~NXLOG_IS_OPEN;
	}

	// Delete old files
	for(i = 9; i >= m_logHistorySize; i--)
	{
		_sntprintf(oldName, MAX_PATH, _T("%s.%d"), m_logFileName, i);
		_tunlink(oldName);
	}

	// Shift file names
	for(; i >= 0; i--)
	{
		_sntprintf(oldName, MAX_PATH, _T("%s.%d"), m_logFileName, i);
		_sntprintf(newName, MAX_PATH, _T("%s.%d"), m_logFileName, i + 1);
		_trename(oldName, newName);
	}

	// Rename current log to name.0
	_sntprintf(newName, MAX_PATH, _T("%s.0"), m_logFileName);
	_trename(m_logFileName, newName);

	// Reopen log
   m_logFileHandle = _tfopen(m_logFileName, _T("w"));
   if (m_logFileHandle != NULL)
   {
		time_t t;
      struct tm *loc;
#if HAVE_LOCALTIME_R
      struct tm ltmBuffer;
#endif
		TCHAR buffer[32];

		m_flags |= NXLOG_IS_OPEN;
      t = time(NULL);
#if HAVE_LOCALTIME_R
      loc = localtime_r(&t, &ltmBuffer);
#else
      loc = localtime(&t);
#endif
      _tcsftime(buffer, 32, _T("%d-%b-%Y %H:%M:%S"), loc);
      _ftprintf(m_logFileHandle, _T("[%s] Log file truncated.\n"), buffer);
	}

	if (needLock)
		MutexUnlock(m_mutexLogAccess);

	return (m_flags & NXLOG_IS_OPEN) ? TRUE : FALSE;
}


//
// API for application to force log rotation
//

BOOL LIBNETXMS_EXPORTABLE nxlog_rotate()
{
	return RotateLog(TRUE);
}


//
// Initialize log
//

BOOL LIBNETXMS_EXPORTABLE nxlog_open(const TCHAR *logName, DWORD flags,
                                     const TCHAR *msgModule, unsigned int msgCount, const TCHAR **messages)
{
	m_flags = flags & 0x7FFFFFFF;
#ifdef _WIN32
	m_msgModuleHandle = GetModuleHandle(msgModule);
#else
	m_numMessages = msgCount;
	m_messages = messages;
#endif

   if (m_flags & NXLOG_USE_SYSLOG)
   {
#ifdef _WIN32
      m_eventLogHandle = RegisterEventSource(NULL, logName);
		if (m_eventLogHandle != NULL)
			m_flags |= NXLOG_IS_OPEN;
#else
#ifdef UNICODE
		char *mbBuffer;

		mbBuffer = MBStringFromWideString(logName);
      openlog(mbBuffer, LOG_PID, LOG_DAEMON);
		free(mbBuffer);
#else
      openlog(logName, LOG_PID, LOG_DAEMON);
#endif
		m_flags |= NXLOG_IS_OPEN;
#endif
   }
   else
   {
      TCHAR buffer[32];
      struct tm *loc;
#if HAVE_LOCALTIME_R
      struct tm ltmBuffer;
#endif
      time_t t;

		nx_strncpy(m_logFileName, logName, MAX_PATH);
      m_logFileHandle = _tfopen(logName, _T("a"));
      if (m_logFileHandle != NULL)
      {
			m_flags |= NXLOG_IS_OPEN;
         t = time(NULL);
#if HAVE_LOCALTIME_R
         loc = localtime_r(&t, &ltmBuffer);
#else
         loc = localtime(&t);
#endif
         _tcsftime(buffer, 32, _T("%d-%b-%Y %H:%M:%S"), loc);
         _ftprintf(m_logFileHandle, _T("\n[%s] Log file opened\n"), buffer);
      }

      m_mutexLogAccess = MutexCreate();
   }
	return (m_flags & NXLOG_IS_OPEN) ? TRUE : FALSE;
}


//
// Close log
//

void LIBNETXMS_EXPORTABLE nxlog_close(void)
{
   if (m_flags & NXLOG_IS_OPEN)
   {
      if (m_flags & NXLOG_USE_SYSLOG)
      {
#ifdef _WIN32
         DeregisterEventSource(m_eventLogHandle);
#else
         closelog();
#endif
      }
      else
      {
         if (m_logFileHandle != NULL)
            fclose(m_logFileHandle);
         if (m_mutexLogAccess != INVALID_MUTEX_HANDLE)
            MutexDestroy(m_mutexLogAccess);
      }
	   m_flags &= ~NXLOG_IS_OPEN;
   }
}


//
// Write record to log file
//

static void WriteLogToFile(TCHAR *message)
{
   TCHAR buffer[64];
   time_t t;
   struct tm *loc;
#if HAVE_LOCALTIME_R
      struct tm ltmBuffer;
#endif

   // Prevent simultaneous write to log file
   MutexLock(m_mutexLogAccess, INFINITE);

   t = time(NULL);
#if HAVE_LOCALTIME_R
   loc = localtime_r(&t, &ltmBuffer);
#else
   loc = localtime(&t);
#endif
   _tcsftime(buffer, 32, _T("[%d-%b-%Y %H:%M:%S]"), loc);
   if (m_logFileHandle != NULL)
	{
      _ftprintf(m_logFileHandle, _T("%s %s"), buffer, message);
		fflush(m_logFileHandle);
	}
   if (m_flags & NXLOG_PRINT_TO_STDOUT)
      _tprintf(_T("%s %s"), buffer, message);

	// Check log size
	if (m_maxLogSize != 0)
	{
		struct stat st;

		fstat(fileno(m_logFileHandle), &st);
		if (st.st_size >= m_maxLogSize)
			RotateLog(FALSE);
	}

   MutexUnlock(m_mutexLogAccess);
}


//
// Format message (UNIX version)
//

#ifndef _WIN32

static TCHAR *FormatMessageUX(DWORD dwMsgId, TCHAR **ppStrings)
{
   const TCHAR *p;
   TCHAR *pMsg;
   int i, iSize, iLen;

   if (dwMsgId >= m_numMessages)
   {
      // No message with this ID
      pMsg = (TCHAR *)malloc(64 * sizeof(TCHAR));
      _sntprintf(pMsg, 64, _T("MSG 0x%08X - Unable to find message text\n"), (unsigned int)dwMsgId);
   }
   else
   {
      iSize = (_tcslen(m_messages[dwMsgId]) + 2) * sizeof(TCHAR);
      pMsg = (TCHAR *)malloc(iSize);

      for(i = 0, p = m_messages[dwMsgId]; *p != 0; p++)
         if (*p == _T('%'))
         {
            p++;
            if ((*p >= _T('1')) && (*p <= _T('9')))
            {
               iLen = _tcslen(ppStrings[*p - _T('1')]);
               iSize += iLen * sizeof(TCHAR);
               pMsg = (TCHAR *)realloc(pMsg, iSize);
               _tcscpy(&pMsg[i], ppStrings[*p - _T('1')]);
               i += iLen;
            }
            else
            {
               if (*p == 0)   // Handle single % character at the string end
                  break;
               pMsg[i++] = *p;
            }
         }
         else
         {
            pMsg[i++] = *p;
         }
      pMsg[i++] = _T('\n');
      pMsg[i] = 0;
   }

   return pMsg;
}

#endif   /* ! _WIN32 */


//
// Write log record
// Parameters:
// msg    - Message ID
// wType  - Message type (see ReportEvent() for details)
// format - Parameter format string, each parameter represented by one character.
//          The following format characters can be used:
//             s - String
//             d - Decimal integer
//             x - Hex integer
//             e - System error code (will appear in log as textual description)
//             a - IP address in network byte order
//

void LIBNETXMS_EXPORTABLE nxlog_write(DWORD msg, WORD wType, const char *format, ...)
{
   va_list args;
   TCHAR *strings[16], *pMsg, szBuffer[256];
   int numStrings = 0;
   DWORD error;
	time_t t;
	struct tm *loc;
#if HAVE_LOCALTIME_R
	struct tm ltmBuffer;
#endif
#if defined(UNICODE) && !defined(_WIN32)
	char *mbMsg;
#endif

	if (!(m_flags & NXLOG_IS_OPEN))
		return;

   memset(strings, 0, sizeof(TCHAR *) * 16);

   if (format != NULL)
   {
      va_start(args, format);

      for(; (format[numStrings] != 0) && (numStrings < 16); numStrings++)
      {
         switch(format[numStrings])
         {
            case 's':
               strings[numStrings] = _tcsdup(va_arg(args, TCHAR *));
               break;
            case 'd':
               strings[numStrings] = (TCHAR *)malloc(16 * sizeof(TCHAR));
               _sntprintf(strings[numStrings], 16, _T("%d"), (int)(va_arg(args, LONG)));
               break;
            case 'x':
               strings[numStrings] = (TCHAR *)malloc(16 * sizeof(TCHAR));
               _sntprintf(strings[numStrings], 16, _T("0x%08X"), (unsigned int)(va_arg(args, DWORD)));
               break;
            case 'a':
               strings[numStrings] = (TCHAR *)malloc(20 * sizeof(TCHAR));
               IpToStr(va_arg(args, DWORD), strings[numStrings]);
               break;
            case 'e':
               error = va_arg(args, DWORD);
#ifdef _WIN32
               if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                                 FORMAT_MESSAGE_FROM_SYSTEM | 
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, error,
                                 MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT), // Default language
                                 (LPTSTR)&pMsg,0,NULL)>0)
               {
                  pMsg[_tcscspn(pMsg, _T("\r\n"))] = 0;
                  strings[numStrings] = (TCHAR *)malloc(_tcslen(pMsg) + 1);
                  _tcscpy(strings[numStrings], pMsg);
                  LocalFree(pMsg);
               }
               else
               {
                  strings[numStrings] = (TCHAR *)malloc(64 * sizeof(TCHAR));
                  _sntprintf(strings[numStrings], 64, _T("MSG 0x%08X - Unable to find message text"), error);
               }
#else   /* _WIN32 */
#if HAVE_STRERROR_R
#if HAVE_POSIX_STRERROR_R
					strings[numStrings] = (TCHAR *)malloc(256 * sizeof(TCHAR));
					_tcserror_r((int)error, strings[numStrings], 256);
#else
					strings[numStrings] = _tcsdup(_tcserror_r((int)error, szBuffer, 256));
#endif
#else
					strings[numStrings] = _tcsdup(_tcserror((int)error));
#endif
#endif
               break;
            default:
               strings[numStrings] = (TCHAR *)malloc(32 * sizeof(TCHAR));
               _sntprintf(strings[numStrings], 32, _T("BAD FORMAT (0x%08X)"), (unsigned int)(va_arg(args, DWORD)));
               break;
         }
      }
      va_end(args);
   }

#ifdef _WIN32
   if (!(m_flags & NXLOG_USE_SYSLOG) || (m_flags & NXLOG_PRINT_TO_STDOUT))
   {
      LPVOID lpMsgBuf;

      if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                        FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                        m_msgModuleHandle, msg, 0, (LPTSTR)&lpMsgBuf, 0, (va_list *)strings)>0)
      {
         char *pCR;

         // Replace trailing CR/LF pair with LF
         pCR = strchr((char *)lpMsgBuf, '\r');
         if (pCR != NULL)
         {
            *pCR = '\n';
            pCR++;
            *pCR = 0;
         }
			if (m_flags & NXLOG_USE_SYSLOG)
			{
				t = time(NULL);
				loc = localtime(&t);
				_tcsftime(szBuffer, 32, _T("[%d-%b-%Y %H:%M:%S]"), loc);
				_tprintf(_T("%s %s"), szBuffer, (TCHAR *)lpMsgBuf);
			}
			else
			{
				WriteLogToFile((TCHAR *)lpMsgBuf);
			}
         LocalFree(lpMsgBuf);
      }
      else
      {
         TCHAR message[64];

         _sntprintf(message, 64, _T("MSG 0x%08X - Unable to find message text"), msg);
			if (m_flags & NXLOG_USE_SYSLOG)
			{
				t = time(NULL);
				loc = localtime(&t);
				_tcsftime(szBuffer, 32, _T("[%d-%b-%Y %H:%M:%S]"), loc);
				_tprintf(_T("%s %s"), szBuffer, message);
			}
			else
			{
	         WriteLogToFile(message);
			}
      }
   }

   if (m_flags & NXLOG_USE_SYSLOG)
	{
      ReportEvent(m_eventLogHandle, (wType == EVENTLOG_DEBUG_TYPE) ? EVENTLOG_INFORMATION_TYPE : wType, 0, msg, NULL, numStrings, 0, (const TCHAR **)strings, NULL);
	}
#else  /* _WIN32 */
   pMsg = FormatMessageUX(msg, strings);
   if (m_flags & NXLOG_USE_SYSLOG)
   {
      int level;

      switch(wType)
      {
         case EVENTLOG_ERROR_TYPE:
            level = LOG_ERR;
            break;
         case EVENTLOG_WARNING_TYPE:
            level = LOG_WARNING;
            break;
         case EVENTLOG_INFORMATION_TYPE:
            level = LOG_NOTICE;
            break;
         case EVENTLOG_DEBUG_TYPE:
            level = LOG_DEBUG;
            break;
         default:
            level = LOG_INFO;
            break;
      }
#ifdef UNICODE
		mbMsg = MBStringFromWideString(pMsg);
      syslog(level, "%s", mbMsg);
		free(mbMsg);
#else
      syslog(level, "%s", pMsg);
#endif

		if (m_flags & NXLOG_PRINT_TO_STDOUT)
		{
			t = time(NULL);
#if HAVE_LOCALTIME_R
			loc = localtime_r(&t, &ltmBuffer);
#else
			loc = localtime(&t);
#endif
			_tcsftime(szBuffer, 32, _T("[%d-%b-%Y %H:%M:%S]"), loc);
			_tprintf(_T("%s %s"), szBuffer, pMsg);
		}
   }
   else
   {
      WriteLogToFile(pMsg);
   }
   free(pMsg);
#endif /* _WIN32 */

   while(--numStrings >= 0)
      free(strings[numStrings]);
}
