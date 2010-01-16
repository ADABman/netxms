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
** File: events.cpp
**
**/

#include "nxcore.h"


//
// Global variables
//

Queue *g_pEventQueue = NULL;
EventPolicy *g_pEventPolicy = NULL;
const char *g_szStatusText[] = { "NORMAL", "WARNING", "MINOR", "MAJOR", "CRITICAL", "UNKNOWN", "UNMANAGED", "DISABLED", "TESTING" };
const char *g_szStatusTextSmall[] = { "Normal", "Warning", "Minor", "Major", "Critical", "Unknown", "Unmanaged", "Disabled", "Testing" };


//
// Static data
//

static DWORD m_dwNumTemplates = 0;
static EVENT_TEMPLATE *m_pEventTemplates = NULL;
static RWLOCK m_rwlockTemplateAccess;


//
// Default constructor for event
//

Event::Event()
{
   m_qwId = 0;
	m_szName[0] = 0;
   m_qwRootId = 0;
   m_dwCode = 0;
   m_dwSeverity = 0;
   m_dwSource = 0;
   m_dwFlags = 0;
   m_dwNumParameters = 0;
   m_ppszParameters = NULL;
   m_pszMessageText = NULL;
   m_pszMessageTemplate = NULL;
   m_tTimeStamp = 0;
	m_pszUserTag = NULL;
	m_pszCustomMessage = NULL;
}


//
// Construct event from template
//

Event::Event(EVENT_TEMPLATE *pTemplate, DWORD dwSourceId, const TCHAR *pszUserTag, const char *szFormat, va_list args)
{
	_tcscpy(m_szName, pTemplate->szName);
   m_tTimeStamp = time(NULL);
   m_qwId = CreateUniqueEventId();
   m_qwRootId = 0;
   m_dwCode = pTemplate->dwCode;
   m_dwSeverity = pTemplate->dwSeverity;
   m_dwFlags = pTemplate->dwFlags;
   m_dwSource = dwSourceId;
   m_pszMessageText = NULL;
	m_pszUserTag = (pszUserTag != NULL) ? _tcsdup(pszUserTag) : NULL;
	m_pszCustomMessage = NULL;

   // Create parameters
   if (szFormat != NULL)
   {
      DWORD i;

      m_dwNumParameters = (DWORD)strlen(szFormat);
      m_ppszParameters = (char **)malloc(sizeof(char *) * m_dwNumParameters);

      for(i = 0; i < m_dwNumParameters; i++)
      {
         switch(szFormat[i])
         {
            case 's':
               m_ppszParameters[i] = strdup(va_arg(args, char *));
               break;
            case 'd':
               m_ppszParameters[i] = (char *)malloc(16);
               sprintf(m_ppszParameters[i], "%d", va_arg(args, LONG));
               break;
            case 'D':
               m_ppszParameters[i] = (char *)malloc(32);
               sprintf(m_ppszParameters[i], INT64_FMT, va_arg(args, INT64));
               break;
            case 'x':
            case 'i':
               m_ppszParameters[i] = (char *)malloc(16);
               sprintf(m_ppszParameters[i], "0x%08X", va_arg(args, DWORD));
               break;
            case 'a':
               m_ppszParameters[i] = (char *)malloc(16);
               IpToStr(va_arg(args, DWORD), m_ppszParameters[i]);
               break;
            default:
               m_ppszParameters[i] = (char *)malloc(64);
               sprintf(m_ppszParameters[i], "BAD FORMAT \"%c\" [value = 0x%08X]", szFormat[i], va_arg(args, DWORD));
               break;
         }
      }
   }
   else
   {
      m_dwNumParameters = 0;
      m_ppszParameters = NULL;
   }

   m_pszMessageTemplate = _tcsdup(pTemplate->pszMessageTemplate);
}


//
// Destructor for event
//

Event::~Event()
{
   safe_free(m_pszMessageText);
   safe_free(m_pszMessageTemplate);
	safe_free(m_pszUserTag);
	safe_free(m_pszCustomMessage);
   if (m_ppszParameters != NULL)
   {
      DWORD i;

      for(i = 0; i < m_dwNumParameters; i++)
         safe_free(m_ppszParameters[i]);
      free(m_ppszParameters);
   }
}


//
// Create message text from template
//

void Event::expandMessageText()
{
   if (m_pszMessageTemplate != NULL)
   {
      if (m_pszMessageText != NULL)
      {
         free(m_pszMessageText);
         m_pszMessageText = NULL;
      }
      m_pszMessageText = expandText(m_pszMessageTemplate);
      free(m_pszMessageTemplate);
      m_pszMessageTemplate = NULL;
   }
}


//
// Substitute % macros in given text with actual values
//

TCHAR *Event::expandText(const TCHAR *pszTemplate, const TCHAR *pszAlarmMsg)
{
   const TCHAR *pCurr;
   DWORD dwPos, dwSize, dwParam;
   NetObj *pObject;
   struct tm *lt;
   TCHAR *pText, szBuffer[4], scriptName[256];
	int i;

   pObject = FindObjectById(m_dwSource);
   if (pObject == NULL)    // Normally shouldn't happen
   {
      pObject = g_pEntireNet;
   }
   dwSize = (DWORD)_tcslen(pszTemplate) + 1;
   pText = (TCHAR *)malloc(dwSize * sizeof(TCHAR));
   for(pCurr = pszTemplate, dwPos = 0; *pCurr != 0; pCurr++)
   {
      switch(*pCurr)
      {
         case '%':   // Metacharacter
            pCurr++;
            if (*pCurr == 0)
            {
               pCurr--;
               break;   // Abnormal loop termination
            }
            switch(*pCurr)
            {
               case '%':
                  pText[dwPos++] = '%';
                  break;
               case 'n':   // Name of event source
                  dwSize += (DWORD)_tcslen(pObject->Name());
                  pText = (char *)realloc(pText, dwSize);
                  strcpy(&pText[dwPos], pObject->Name());
                  dwPos += (DWORD)_tcslen(pObject->Name());
                  break;
               case 'a':   // IP address of event source
                  dwSize += 16;
                  pText = (char *)realloc(pText, dwSize);
                  IpToStr(pObject->IpAddr(), &pText[dwPos]);
                  dwPos = (DWORD)_tcslen(pText);
                  break;
               case 'i':   // Source object identifier
                  dwSize += 10;
                  pText = (char *)realloc(pText, dwSize);
                  sprintf(&pText[dwPos], "0x%08X", m_dwSource);
                  dwPos = (DWORD)_tcslen(pText);
                  break;
               case 't':   // Event's timestamp
                  dwSize += 32;
                  pText = (char *)realloc(pText, dwSize);
                  lt = localtime(&m_tTimeStamp);
                  strftime(&pText[dwPos], 32, "%d-%b-%Y %H:%M:%S", lt);
                  dwPos = (DWORD)_tcslen(pText);
                  break;
               case 'T':   // Event's timestamp as number of seconds since epoch
                  dwSize += 16;
                  pText = (char *)realloc(pText, dwSize);
                  sprintf(&pText[dwPos], "%lu", m_tTimeStamp);
                  dwPos = (DWORD)_tcslen(pText);
                  break;
               case 'c':   // Event code
                  dwSize += 16;
                  pText = (char *)realloc(pText, dwSize);
                  sprintf(&pText[dwPos], "%u", m_dwCode);
                  dwPos = (DWORD)_tcslen(pText);
                  break;
               case 'N':   // Event name
                  dwSize += (DWORD)_tcslen(m_szName);
                  pText = (char *)realloc(pText, dwSize);
                  strcpy(&pText[dwPos], m_szName);
                  dwPos += (DWORD)_tcslen(m_szName);
                  break;
               case 's':   // Severity code
                  dwSize += 3;
                  pText = (char *)realloc(pText, dwSize);
                  sprintf(&pText[dwPos], "%d", (int)m_dwSeverity);
                  dwPos = (DWORD)_tcslen(pText);
                  break;
               case 'S':   // Severity text
                  dwSize += (DWORD)_tcslen(g_szStatusTextSmall[m_dwSeverity]);
                  pText = (char *)realloc(pText, dwSize);
                  strcpy(&pText[dwPos], g_szStatusTextSmall[m_dwSeverity]);
                  dwPos += (DWORD)_tcslen(g_szStatusTextSmall[m_dwSeverity]);
                  break;
               case 'v':   // NetXMS server version
                  dwSize += (DWORD)_tcslen(NETXMS_VERSION_STRING);
                  pText = (char *)realloc(pText, dwSize);
                  strcpy(&pText[dwPos], NETXMS_VERSION_STRING);
                  dwPos += (DWORD)_tcslen(NETXMS_VERSION_STRING);
                  break;
               case 'm':
                  if (m_pszMessageText != NULL)
                  {
                     dwSize += (DWORD)_tcslen(m_pszMessageText);
                     pText = (char *)realloc(pText, dwSize);
                     strcpy(&pText[dwPos], m_pszMessageText);
                     dwPos += (DWORD)_tcslen(m_pszMessageText);
                  }
                  break;
               case 'M':	// Custom message (usually set by matching script in EPP)
                  if (m_pszCustomMessage != NULL)
                  {
                     dwSize += (DWORD)_tcslen(m_pszCustomMessage);
                     pText = (char *)realloc(pText, dwSize);
                     strcpy(&pText[dwPos], m_pszCustomMessage);
                     dwPos += (DWORD)_tcslen(m_pszCustomMessage);
                  }
                  break;
               case 'A':   // Associated alarm message
                  if (pszAlarmMsg != NULL)
                  {
                     dwSize += (DWORD)_tcslen(pszAlarmMsg);
                     pText = (char *)realloc(pText, dwSize);
                     strcpy(&pText[dwPos], pszAlarmMsg);
                     dwPos += (DWORD)_tcslen(pszAlarmMsg);
                  }
                  break;
               case 'u':	// User tag
                  if (m_pszUserTag != NULL)
                  {
                     dwSize += (DWORD)_tcslen(m_pszUserTag);
                     pText = (char *)realloc(pText, dwSize);
                     strcpy(&pText[dwPos], m_pszUserTag);
                     dwPos += (DWORD)_tcslen(m_pszUserTag);
                  }
                  break;
               case '0':
               case '1':
               case '2':
               case '3':
               case '4':
               case '5':
               case '6':
               case '7':
               case '8':
               case '9':
                  szBuffer[0] = *pCurr;
                  if (isdigit(*(pCurr + 1)))
                  {
                     pCurr++;
                     szBuffer[1] = *pCurr;
                     szBuffer[2] = 0;
                  }
                  else
                  {
                     szBuffer[1] = 0;
                  }
                  dwParam = atol(szBuffer);
                  if ((dwParam > 0) && (dwParam <= m_dwNumParameters))
                  {
                     dwParam--;
                     dwSize += (DWORD)_tcslen(m_ppszParameters[dwParam]);
                     pText = (char *)realloc(pText, dwSize);
                     strcpy(&pText[dwPos], m_ppszParameters[dwParam]);
                     dwPos += (DWORD)_tcslen(m_ppszParameters[dwParam]);
                  }
                  break;
					case '[':	// Script
						for(i = 0, pCurr++; (*pCurr != ']') && (*pCurr != 0) && (i < 255); pCurr++)
						{
							scriptName[i++] = *pCurr;
						}
						if (*pCurr == 0)	// no terminating ]
						{
							pCurr--;
						}
						else
						{
							NXSL_Program *script;
							NXSL_ServerEnv *pEnv;

							scriptName[i] = 0;
							StrStrip(scriptName);

							g_pScriptLibrary->lock();
							script = g_pScriptLibrary->findScript(scriptName);
							if (script != NULL)
							{
								pEnv = new NXSL_ServerEnv;
								if (pObject->Type() == OBJECT_NODE)
									script->setGlobalVariable(_T("$node"), new NXSL_Value(new NXSL_Object(&g_nxslNodeClass, pObject)));
								script->setGlobalVariable(_T("$event"), new NXSL_Value(new NXSL_Object(&g_nxslEventClass, this)));
								if (pszAlarmMsg != NULL)
									script->setGlobalVariable(_T("$alarmMessage"), new NXSL_Value(pszAlarmMsg));

								if (script->run(pEnv) == 0)
								{
									NXSL_Value *result = script->getResult();
									if (result != NULL)
									{
										const TCHAR *temp = result->getValueAsCString();
										if (temp != NULL)
										{
											dwSize += (DWORD)_tcslen(temp);
											pText = (char *)realloc(pText, dwSize);
											_tcscpy(&pText[dwPos], temp);
											dwPos += (DWORD)_tcslen(temp);
											DbgPrintf(4, "Event::ExpandText(%d, \"%s\"): Script %s executed successfully",
														 m_dwCode, pszTemplate, scriptName);
										}
									}
								}
								else
								{
									DbgPrintf(4, "Event::ExpandText(%d, \"%s\"): Script %s execution error: %s",
												 m_dwCode, pszTemplate, scriptName, script->getErrorText());
									PostEvent(EVENT_SCRIPT_ERROR, g_dwMgmtNode, _T("ssd"), scriptName,
												 script->getErrorText(), 0);
								}
							}
							else
							{
								DbgPrintf(4, "Event::ExpandText(%d, \"%s\"): Cannot find script %s", m_dwCode, pszTemplate, scriptName);
							}
							g_pScriptLibrary->unlock();
						}
						break;
					case '{':	// Custom attribute
						for(i = 0, pCurr++; (*pCurr != '}') && (*pCurr != 0) && (i < 255); pCurr++)
						{
							scriptName[i++] = *pCurr;
						}
						if (*pCurr == 0)	// no terminating }
						{
							pCurr--;
						}
						else
						{
							scriptName[i] = 0;
							StrStrip(scriptName);
							const TCHAR *temp = pObject->GetCustomAttribute(scriptName);
							if (temp != NULL)
							{
								dwSize += (DWORD)_tcslen(temp);
								pText = (char *)realloc(pText, dwSize);
								_tcscpy(&pText[dwPos], temp);
								dwPos += (DWORD)_tcslen(temp);
							}
						}
						break;
               default:    // All other characters are invalid, ignore
                  break;
            }
            break;
         case '\\':  // Escape character
            pCurr++;
            if (*pCurr == 0)
            {
               pCurr--;
               break;   // Abnormal loop termination
            }
            switch(*pCurr)
            {
               case 't':
                  pText[dwPos++] = '\t';
                  break;
               case 'n':
                  pText[dwPos++] = 0x0D;
                  pText[dwPos++] = 0x0A;
                  break;
               default:
                  pText[dwPos++] = *pCurr;
                  break;
            }
            break;
         default:
            pText[dwPos++] = *pCurr;
            break;
      }
   }
   pText[dwPos] = 0;
   return pText;
}


//
// Fill message with event data
//

void Event::prepareMessage(CSCPMessage *pMsg)
{
	DWORD dwId = VID_EVENTLOG_MSG_BASE;

	pMsg->SetVariable(VID_NUM_RECORDS, (DWORD)1);
	pMsg->SetVariable(VID_RECORDS_ORDER, (WORD)RECORD_ORDER_NORMAL);
	pMsg->SetVariable(dwId++, m_qwId);
	pMsg->SetVariable(dwId++, m_dwCode);
	pMsg->SetVariable(dwId++, (DWORD)m_tTimeStamp);
	pMsg->SetVariable(dwId++, m_dwSource);
	pMsg->SetVariable(dwId++, (WORD)m_dwSeverity);
	pMsg->SetVariable(dwId++, CHECK_NULL(m_pszMessageText));
	pMsg->SetVariable(dwId++, CHECK_NULL(m_pszUserTag));
	pMsg->SetVariable(dwId++, m_dwNumParameters);
	for(DWORD i = 0; i < m_dwNumParameters; i++)
		pMsg->SetVariable(dwId++, m_ppszParameters[i]);
}


//
// Load event configuration from database
//

static BOOL LoadEvents(void)
{
   DB_RESULT hResult;
   DWORD i;
   BOOL bSuccess = FALSE;

   hResult = DBSelect(g_hCoreDB, "SELECT event_code,severity,flags,message,description,event_name FROM event_cfg ORDER BY event_code");
   if (hResult != NULL)
   {
      m_dwNumTemplates = DBGetNumRows(hResult);
      m_pEventTemplates = (EVENT_TEMPLATE *)malloc(sizeof(EVENT_TEMPLATE) * m_dwNumTemplates);
      for(i = 0; i < m_dwNumTemplates; i++)
      {
         m_pEventTemplates[i].dwCode = DBGetFieldULong(hResult, i, 0);
         m_pEventTemplates[i].dwSeverity = DBGetFieldLong(hResult, i, 1);
         m_pEventTemplates[i].dwFlags = DBGetFieldLong(hResult, i, 2);
         m_pEventTemplates[i].pszMessageTemplate = DBGetField(hResult, i, 3, NULL, 0);
         DecodeSQLString(m_pEventTemplates[i].pszMessageTemplate);
         m_pEventTemplates[i].pszDescription = DBGetField(hResult, i, 4, NULL, 0);
         DecodeSQLString(m_pEventTemplates[i].pszDescription);
         DBGetField(hResult, i, 5, m_pEventTemplates[i].szName, MAX_EVENT_NAME);
      }

      DBFreeResult(hResult);
      bSuccess = TRUE;
   }
   else
   {
      nxlog_write(MSG_EVENT_LOAD_ERROR, EVENTLOG_ERROR_TYPE, NULL);
   }

   return bSuccess;
}


//
// Inilialize event handling subsystem
//

BOOL InitEventSubsystem(void)
{
   BOOL bSuccess;

   // Create template access mutex
   m_rwlockTemplateAccess = RWLockCreate();

   // Create event queue
   g_pEventQueue = new Queue;

   // Load events from database
   bSuccess = LoadEvents();

   // Create and initialize event processing policy
   if (bSuccess)
   {
      g_pEventPolicy = new EventPolicy;
      if (!g_pEventPolicy->LoadFromDB())
      {
         bSuccess = FALSE;
         nxlog_write(MSG_EPP_LOAD_FAILED, EVENTLOG_ERROR_TYPE, NULL);
         delete g_pEventPolicy;
      }
   }

   return bSuccess;
}


//
// Shutdown event subsystem
//

void ShutdownEventSubsystem(void)
{
   delete g_pEventQueue;
   delete g_pEventPolicy;

   if (m_pEventTemplates != NULL)
   {
      DWORD i;
      for(i = 0; i < m_dwNumTemplates; i++)
      {
         safe_free(m_pEventTemplates[i].pszDescription);
         safe_free(m_pEventTemplates[i].pszMessageTemplate);
      }
      free(m_pEventTemplates);
   }
   m_dwNumTemplates = 0;
   m_pEventTemplates = NULL;

   RWLockDestroy(m_rwlockTemplateAccess);
}


//
// Reload event templates from database
//

void ReloadEvents(void)
{
   DWORD i;

   RWLockWriteLock(m_rwlockTemplateAccess, INFINITE);
   if (m_pEventTemplates != NULL)
   {
      for(i = 0; i < m_dwNumTemplates; i++)
      {
         safe_free(m_pEventTemplates[i].pszDescription);
         safe_free(m_pEventTemplates[i].pszMessageTemplate);
      }
      free(m_pEventTemplates);
   }
   m_dwNumTemplates = 0;
   m_pEventTemplates = NULL;
   LoadEvents();
   RWLockUnlock(m_rwlockTemplateAccess);
}


//
// Delete event template from list
//

void DeleteEventTemplateFromList(DWORD dwEventCode)
{
   DWORD i;

   RWLockWriteLock(m_rwlockTemplateAccess, INFINITE);
   for(i = 0; i < m_dwNumTemplates; i++)
   {
      if (m_pEventTemplates[i].dwCode == dwEventCode)
      {
         m_dwNumTemplates--;
         safe_free(m_pEventTemplates[i].pszDescription);
         safe_free(m_pEventTemplates[i].pszMessageTemplate);
         memmove(&m_pEventTemplates[i], &m_pEventTemplates[i + 1],
                 sizeof(EVENT_TEMPLATE) * (m_dwNumTemplates - i));
         break;
      }
   }
   RWLockUnlock(m_rwlockTemplateAccess);
}


//
// Perform binary search on event template by id
// Returns INULL if key not found or pointer to appropriate template
//

static EVENT_TEMPLATE *FindEventTemplate(DWORD dwCode)
{
   DWORD dwFirst, dwLast, dwMid;

   dwFirst = 0;
   dwLast = m_dwNumTemplates - 1;

   if ((dwCode < m_pEventTemplates[0].dwCode) || (dwCode > m_pEventTemplates[dwLast].dwCode))
      return NULL;

   while(dwFirst < dwLast)
   {
      dwMid = (dwFirst + dwLast) / 2;
      if (dwCode == m_pEventTemplates[dwMid].dwCode)
         return &m_pEventTemplates[dwMid];
      if (dwCode < m_pEventTemplates[dwMid].dwCode)
         dwLast = dwMid - 1;
      else
         dwFirst = dwMid + 1;
   }

   if (dwCode == m_pEventTemplates[dwLast].dwCode)
      return &m_pEventTemplates[dwLast];

   return NULL;
}


//
// Post event to the queue.
// Arguments:
// dwEventCode - Event code
// dwSourceId  - Event source object ID
// szFormat    - Parameter format string, each parameter represented by one character.
//    The following format characters can be used:
//        s - String
//        d - Decimal integer
//        D - 64-bit decimal integer
//        x - Hex integer
//        a - IP address
//        i - Object ID
// PostEventEx will put events to specified queue, and PostEvent to system
// event queue. Both functions uses RealPostEvent to do real job.
//

static BOOL RealPostEvent(Queue *pQueue, DWORD dwEventCode, DWORD dwSourceId,
                          const TCHAR *pszUserTag, const char *pszFormat, va_list args)
{
   EVENT_TEMPLATE *pEventTemplate;
   Event *pEvent;
   BOOL bResult = FALSE;

   RWLockReadLock(m_rwlockTemplateAccess, INFINITE);

   // Find event template
   if (m_dwNumTemplates > 0)    // Is there any templates?
   {
      pEventTemplate = FindEventTemplate(dwEventCode);
      if (pEventTemplate != NULL)
      {
         // Template found, create new event
         pEvent = new Event(pEventTemplate, dwSourceId, pszUserTag, pszFormat, args);

         // Add new event to queue
         pQueue->Put(pEvent);

         bResult = TRUE;
      }
   }

   RWLockUnlock(m_rwlockTemplateAccess);
   return bResult;
}

BOOL PostEvent(DWORD dwEventCode, DWORD dwSourceId, const char *pszFormat, ...)
{
   va_list args;
   BOOL bResult;

   va_start(args, pszFormat);
   bResult = RealPostEvent(g_pEventQueue, dwEventCode, dwSourceId, NULL, pszFormat, args);
   va_end(args);
   return bResult;
}

BOOL PostEventWithTag(DWORD dwEventCode, DWORD dwSourceId, const TCHAR *pszUserTag, const char *pszFormat, ...)
{
   va_list args;
   BOOL bResult;

   va_start(args, pszFormat);
   bResult = RealPostEvent(g_pEventQueue, dwEventCode, dwSourceId, pszUserTag, pszFormat, args);
   va_end(args);
   return bResult;
}

BOOL PostEventEx(Queue *pQueue, DWORD dwEventCode, DWORD dwSourceId, 
                 const char *pszFormat, ...)
{
   va_list args;
   BOOL bResult;

   va_start(args, pszFormat);
   bResult = RealPostEvent(pQueue, dwEventCode, dwSourceId, NULL, pszFormat, args);
   va_end(args);
   return bResult;
}


//
// Resend events from specific queue to system event queue
//

void ResendEvents(Queue *pQueue)
{
   void *pEvent;

   while(1)
   {
      pEvent = pQueue->Get();
      if (pEvent == NULL)
         break;
      g_pEventQueue->Put(pEvent);
   }
}


//
// Create NXMP record for event
//

void CreateNXMPEventRecord(String &str, DWORD dwCode)
{
   EVENT_TEMPLATE *p;
   String strText, strDescr;

   RWLockReadLock(m_rwlockTemplateAccess, INFINITE);

   // Find event template
   if (m_dwNumTemplates > 0)    // Is there any templates?
   {
      p = FindEventTemplate(dwCode);
      if (p != NULL)
      {
         str.addFormattedString(_T("\t\t<event id=\"%d\">\n")
			                       _T("\t\t\t<name>%s</name>\n")
                                _T("\t\t\t<code>%d</code>\n")
                                _T("\t\t\t<severity>%d</severity>\n")
                                _T("\t\t\t<flags>%d</flags>\n")
                                _T("\t\t\t<message>%s</message>\n")
                                _T("\t\t\t<description>%s</description>\n")
                                _T("\t\t</event>\n"),
										  p->dwCode, (const TCHAR *)EscapeStringForXML2(p->szName), p->dwCode, p->dwSeverity,
                                p->dwFlags, (const TCHAR *)EscapeStringForXML2(p->pszMessageTemplate),
										  (const TCHAR *)EscapeStringForXML2(p->pszDescription));
      }
   }

   RWLockUnlock(m_rwlockTemplateAccess);
}


//
// Resolve event name
//

BOOL EventNameFromCode(DWORD dwCode, TCHAR *pszBuffer)
{
   EVENT_TEMPLATE *p;
   BOOL bRet = FALSE;

   RWLockReadLock(m_rwlockTemplateAccess, INFINITE);

   // Find event template
   if (m_dwNumTemplates > 0)    // Is there any templates?
   {
      p = FindEventTemplate(dwCode);
      if (p != NULL)
      {
         _tcscpy(pszBuffer, p->szName);
         bRet = TRUE;
      }
      else
      {
         _tcscpy(pszBuffer, _T("UNKNOWN_EVENT"));
      }
   }
   else
   {
      _tcscpy(pszBuffer, _T("UNKNOWN_EVENT"));
   }

   RWLockUnlock(m_rwlockTemplateAccess);
   return bRet;
}


//
// Find event template by code - suitable for external call
//

EVENT_TEMPLATE *FindEventTemplateByCode(DWORD dwCode)
{
   EVENT_TEMPLATE *p = NULL;

   RWLockReadLock(m_rwlockTemplateAccess, INFINITE);
   p = FindEventTemplate(dwCode);
   RWLockUnlock(m_rwlockTemplateAccess);
   return p;
}


//
// Find event template by name - suitable for external call
//

EVENT_TEMPLATE *FindEventTemplateByName(const TCHAR *pszName)
{
   EVENT_TEMPLATE *p = NULL;
   DWORD i;

   RWLockReadLock(m_rwlockTemplateAccess, INFINITE);
   for(i = 0; i < m_dwNumTemplates; i++)
   {
      if (!_tcscmp(m_pEventTemplates[i].szName, pszName))
      {
         p = &m_pEventTemplates[i];
         break;
      }
   }
   RWLockUnlock(m_rwlockTemplateAccess);
   return p;
}


//
// Translate event name to code
// If event with given name does not exist, returns supplied default value
//

DWORD EventCodeFromName(const TCHAR *name, DWORD defaultValue)
{
	EVENT_TEMPLATE *p = FindEventTemplateByName(name);
	return (p != NULL) ? p->dwCode : defaultValue;
}
