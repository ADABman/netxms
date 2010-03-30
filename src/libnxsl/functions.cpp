/* 
** NetXMS - Network Management System
** NetXMS Scripting Language Interpreter
** Copyright (C) 2005-2010 Victor Kirhenshtein
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
** File: functions.cpp
**
**/

#include "libnxsl.h"
#include <math.h>


//
// Global data
//

const char *g_szTypeNames[] = { "null", "object", "string", "real", "int32",
                                "int64", "uint32", "uint64" };



//
// Type of value
//

int F_typeof(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   *ppResult = new NXSL_Value(g_szTypeNames[argv[0]->getDataType()]);
   return 0;
}


//
// Class of an object
//

int F_classof(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	if (!argv[0]->isObject())
		return NXSL_ERR_NOT_OBJECT;
		
   *ppResult = new NXSL_Value(argv[0]->getValueAsObject()->getClass()->getName());
   return 0;
}


//
// Absolute value
//

int F_abs(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int nRet;

   if (argv[0]->isNumeric())
   {
      if (argv[0]->isReal())
      {
         *ppResult = new NXSL_Value(fabs(argv[0]->getValueAsReal()));
      }
      else
      {
         *ppResult = new NXSL_Value(argv[0]);
         if (!argv[0]->isUnsigned())
            if ((*ppResult)->getValueAsInt64() < 0)
               (*ppResult)->negate();
      }
      nRet = 0;
   }
   else
   {
      nRet = NXSL_ERR_NOT_NUMBER;
   }
   return nRet;
}


//
// Calculates x raised to the power of y
//

int F_pow(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int nRet;

   if ((argv[0]->isNumeric()) && (argv[1]->isNumeric()))
   {
      *ppResult = new NXSL_Value(pow(argv[0]->getValueAsReal(), argv[1]->getValueAsReal()));
      nRet = 0;
   }
   else
   {
      nRet = NXSL_ERR_NOT_NUMBER;
   }
   return nRet;
}


//
// Convert string to uppercase
//

int F_upper(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int nRet;
   DWORD i, dwLen;
   TCHAR *pStr;

   if (argv[0]->isString())
   {
      *ppResult = new NXSL_Value(argv[0]);
      pStr = (TCHAR *)(*ppResult)->getValueAsString(&dwLen);
      for(i = 0; i < dwLen; i++, pStr++)
         *pStr = toupper(*pStr);
      nRet = 0;
   }
   else
   {
      nRet = NXSL_ERR_NOT_STRING;
   }
   return nRet;
}


//
// Convert string to lowercase
//

int F_lower(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int nRet;
   DWORD i, dwLen;
   TCHAR *pStr;

   if (argv[0]->isString())
   {
      *ppResult = new NXSL_Value(argv[0]);
      pStr = (TCHAR *)(*ppResult)->getValueAsString(&dwLen);
      for(i = 0; i < dwLen; i++, pStr++)
         *pStr = tolower(*pStr);
      nRet = 0;
   }
   else
   {
      nRet = NXSL_ERR_NOT_STRING;
   }
   return nRet;
}


//
// String length
//

int F_length(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int nRet;
   DWORD dwLen;

   if (argv[0]->isString())
   {
      argv[0]->getValueAsString(&dwLen);
      *ppResult = new NXSL_Value(dwLen);
      nRet = 0;
   }
   else
   {
      nRet = NXSL_ERR_NOT_STRING;
   }
   return nRet;
}


//
// Minimal value from the list of values
//

int F_min(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int i;
   NXSL_Value *pCurr;

   if (argc == 0)
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;

   pCurr = argv[0];
   for(i = 1; i < argc; i++)
   {
      if (!argv[i]->isNumeric())
         return NXSL_ERR_NOT_NUMBER;

      if (argv[i]->LT(pCurr))
         pCurr = argv[i];
   }
   *ppResult = new NXSL_Value(pCurr);
   return 0;
}


//
// Maximal value from the list of values
//

int F_max(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int i;
   NXSL_Value *pCurr;

   if (argc == 0)
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;

   pCurr = argv[0];
   for(i = 0; i < argc; i++)
   {
      if (!argv[i]->isNumeric())
         return NXSL_ERR_NOT_NUMBER;

      if (argv[i]->GT(pCurr))
         pCurr = argv[i];
   }
   *ppResult = new NXSL_Value(pCurr);
   return 0;
}


//
// Check if IP address is within given range
//

int F_AddrInRange(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int nRet;
   DWORD dwAddr, dwStart, dwEnd;

   if (argv[0]->isString() && argv[1]->isString() && argv[2]->isString())
   {
      dwAddr = ntohl(inet_addr(argv[0]->getValueAsCString()));
      dwStart = ntohl(inet_addr(argv[1]->getValueAsCString()));
      dwEnd = ntohl(inet_addr(argv[2]->getValueAsCString()));
      *ppResult = new NXSL_Value((LONG)(((dwAddr >= dwStart) && (dwAddr <= dwEnd)) ? 1 : 0));
      nRet = 0;
   }
   else
   {
      nRet = NXSL_ERR_NOT_STRING;
   }
   return nRet;
}


//
// Check if IP address is within given subnet
//

int F_AddrInSubnet(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   int nRet;
   DWORD dwAddr, dwSubnet, dwMask;

   if (argv[0]->isString() && argv[1]->isString() && argv[2]->isString())
   {
      dwAddr = ntohl(inet_addr(argv[0]->getValueAsCString()));
      dwSubnet = ntohl(inet_addr(argv[1]->getValueAsCString()));
      dwMask = ntohl(inet_addr(argv[2]->getValueAsCString()));
      *ppResult = new NXSL_Value((LONG)(((dwAddr & dwMask) == dwSubnet) ? 1 : 0));
      nRet = 0;
   }
   else
   {
      nRet = NXSL_ERR_NOT_STRING;
   }
   return nRet;
}


//
// Convert time_t into string
// PATCH: by Edgar Chupit
//

int F_strftime(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{   
   TCHAR buffer[512];
   time_t tTime;
	struct tm *ptm;
	BOOL bLocalTime;

   if ((argc == 0) || (argc > 3))
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;

   if (!argv[0]->isString())
      return NXSL_ERR_NOT_STRING;
	if (argc > 1)
	{
		if (!argv[1]->isNumeric() && !argv[1]->isNull())
			return NXSL_ERR_NOT_NUMBER;
		tTime = (argv[1]->isNull()) ? time(NULL) : (time_t)argv[1]->getValueAsUInt64();

		if (argc > 2)
		{
			if (!argv[2]->isInteger())
				return NXSL_ERR_BAD_CONDITION;
			bLocalTime = argv[2]->getValueAsInt32() ? TRUE : FALSE;
		}
		else
		{
			// No third argument, assume localtime
			bLocalTime = TRUE;
		}
	}
	else
	{
		// No second argument
		tTime = time(NULL);
	}

   ptm = bLocalTime ? localtime(&tTime) : gmtime(&tTime);
   _tcsftime(buffer, 512, argv[0]->getValueAsCString(), ptm);
   *ppResult = new NXSL_Value(buffer);   
   
   return 0;
}


//
// Convert seconds since uptime to string
// PATCH: by Edgar Chupit
//

int F_SecondsToUptime(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   DWORD d, h, n;

   if (!argv[0]->isNumeric())
      return NXSL_ERR_NOT_NUMBER;

   QWORD arg = argv[0]->getValueAsUInt64();

   d = (DWORD)(arg / 86400);
   arg -= d * 86400;
   h = (DWORD)(arg / 3600);
   arg -= h * 3600;
   n = (DWORD)(arg / 60);
   arg -= n * 60;
   if (arg > 0)
       n++;

   char pResult[MAX_PATH];
   pResult[0] = 0;

   snprintf(pResult, MAX_PATH, "%u days, %2u:%02u", d, h, n);

   *ppResult = new NXSL_Value(pResult);
   return 0;
}


//
// Get current time
//

int F_time(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
   *ppResult = new NXSL_Value((DWORD)time(NULL));
   return 0;
}


//
// NXSL "TIME" class
//

class NXSL_TimeClass : public NXSL_Class
{
public:
   NXSL_TimeClass();

   virtual NXSL_Value *getAttr(NXSL_Object *pObject, const TCHAR *pszAttr);
	virtual void onObjectDelete(NXSL_Object *object);
};


//
// Implementation of "TIME" class
//

NXSL_TimeClass::NXSL_TimeClass()
               :NXSL_Class()
{
   strcpy(m_szName, "TIME");
}

NXSL_Value *NXSL_TimeClass::getAttr(NXSL_Object *pObject, const TCHAR *pszAttr)
{
   struct tm *st;
   NXSL_Value *value;

   st = (struct tm *)pObject->getData();
   if (!strcmp(pszAttr, "sec") || !strcmp(pszAttr, "tm_sec"))
   {
      value = new NXSL_Value((LONG)st->tm_sec);
   }
   else if (!strcmp(pszAttr, "min") || !strcmp(pszAttr, "tm_min"))
   {
      value = new NXSL_Value((LONG)st->tm_min);
   }
   else if (!strcmp(pszAttr, "hour") || !strcmp(pszAttr, "tm_hour"))
   {
      value = new NXSL_Value((LONG)st->tm_hour);
   }
   else if (!strcmp(pszAttr, "mday") || !strcmp(pszAttr, "tm_mday"))
   {
      value = new NXSL_Value((LONG)st->tm_mday);
   }
   else if (!strcmp(pszAttr, "mon") || !strcmp(pszAttr, "tm_mon"))
   {
      value = new NXSL_Value((LONG)st->tm_mon);
   }
   else if (!strcmp(pszAttr, "year") || !strcmp(pszAttr, "tm_year"))
   {
      value = new NXSL_Value((LONG)(st->tm_year + 1900));
   }
   else if (!strcmp(pszAttr, "yday") || !strcmp(pszAttr, "tm_yday"))
   {
      value = new NXSL_Value((LONG)st->tm_yday);
   }
   else if (!strcmp(pszAttr, "wday") || !strcmp(pszAttr, "tm_wday"))
   {
      value = new NXSL_Value((LONG)st->tm_wday);
   }
   else if (!strcmp(pszAttr, "isdst") || !strcmp(pszAttr, "tm_isdst"))
   {
      value = new NXSL_Value((LONG)st->tm_isdst);
   }
	else
	{
		value = NULL;	// Error
	}
   return value;
}

void NXSL_TimeClass::onObjectDelete(NXSL_Object *object)
{
	safe_free(object->getData());
}


//
// NXSL "TIME" class object
//

static NXSL_TimeClass m_nxslTimeClass;


//
// Return parsed local time
//

int F_localtime(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	struct tm *p;
	time_t t;

   if (argc == 0)
	{
		t = time(NULL);
	}
	else if (argc == 1)
	{
		if (!argv[0]->isInteger())
			return NXSL_ERR_NOT_INTEGER;

		t = argv[0]->getValueAsUInt32();
	}
	else
	{
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;
	}

	p = localtime(&t);
	*ppResult = new NXSL_Value(new NXSL_Object(&m_nxslTimeClass, nx_memdup(p, sizeof(struct tm))));
	return 0;
}


//
// Return parsed UTC time
//

int F_gmtime(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	struct tm *p;
	time_t t;

   if (argc == 0)
	{
		t = time(NULL);
	}
	else if (argc == 1)
	{
		if (!argv[0]->isInteger())
			return NXSL_ERR_NOT_INTEGER;

		t = argv[0]->getValueAsUInt32();
	}
	else
	{
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;
	}

	p = gmtime(&t);
	*ppResult = new NXSL_Value(new NXSL_Object(&m_nxslTimeClass, nx_memdup(p, sizeof(struct tm))));
	return 0;
}


//
// Get substring of a string
// Possible usage:
//    substr(string, start) - all characters from position 'start'
//    substr(string, start, n) - n characters from position 'start'
//    substr(string, NULL, n) - first n characters
//

int F_substr(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	int nStart, nCount;
	const TCHAR *pBase;
	DWORD dwLen;

   if ((argc < 2) || (argc > 3))
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;

   if (!argv[0]->isString())
      return NXSL_ERR_NOT_STRING;

	if (argv[1]->isNull())
	{
		nStart = 0;
	}
	else if (argv[1]->isInteger())
	{
		nStart = argv[1]->getValueAsInt32();
		if (nStart > 0)
			nStart--;
		else
			nStart = 0;
	}
	else
	{
		return NXSL_ERR_NOT_INTEGER;
	}

	if (argc == 3)
	{
		if (!argv[2]->isInteger())
			return NXSL_ERR_NOT_INTEGER;
		nCount = argv[2]->getValueAsInt32();
		if (nCount < 0)
			nCount = 0;
	}
	else
	{
		nCount = -1;
	}

	pBase = argv[0]->getValueAsString(&dwLen);
	if ((DWORD)nStart < dwLen)
	{
		pBase += nStart;
		dwLen -= nStart;
		if ((nCount == -1) || ((DWORD)nCount > dwLen))
		{
			nCount = dwLen;
		}
		*ppResult = new NXSL_Value(pBase, (DWORD)nCount);
	}
	else
	{
		*ppResult = new NXSL_Value("");
	}

	return 0;
}


//
// Convert decimal value to hexadecimal string
//   d2x(value)          -> hex value
//   d2x(value, padding) -> hex value padded vith zeros
//

int F_d2x(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	TCHAR buffer[128], format[32];

   if ((argc < 1) || (argc > 2))
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;

   if (!argv[0]->isInteger())
      return NXSL_ERR_NOT_INTEGER;

   if ((argc == 2) && (!argv[1]->isInteger()))
      return NXSL_ERR_NOT_INTEGER;

	if (argc == 1)
	{
		_tcscpy(format, _T("%X"));
	}
	else
	{
		_sntprintf(format, 32, _T("%%0%dX"), argv[1]->getValueAsInt32());
	}
	_sntprintf(buffer, 128, format, argv[0]->getValueAsUInt32());
	*ppResult = new NXSL_Value(buffer);
	return 0;
}


//
// left() - take leftmost part of a string and pad or truncate it as necessary
// Format: left(string, len, [pad])
//

int F_left(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	const TCHAR *str;
	TCHAR *newStr, pad;
	LONG newLen;
	DWORD i, len;

   if ((argc < 2) || (argc > 3))
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;

   if (!argv[1]->isInteger())
      return NXSL_ERR_NOT_INTEGER;

   if ((!argv[0]->isString()) ||
		 ((argc == 3) && (!argv[2]->isString())))
		return NXSL_ERR_NOT_STRING;

	if (argc == 3)
	{
		pad = *(argv[2]->getValueAsCString());
		if (pad == 0)
			pad = _T(' ');
	}
	else
	{
		pad = _T(' ');
	}

	newLen = argv[1]->getValueAsInt32();
	if (newLen < 0)
		newLen = 0;

   str = argv[0]->getValueAsString(&len);
	if (len > (DWORD)newLen)
		len = (DWORD)newLen;
	newStr = (TCHAR *)malloc(newLen * sizeof(TCHAR));
	memcpy(newStr, str, len * sizeof(TCHAR));
   for(i = len; i < (DWORD)newLen; i++)
      newStr[i] = pad;
   *ppResult = new NXSL_Value(newStr, newLen);
	free(newStr);
	return 0;
}


//
// right() - take rightmost part of a string and pad or truncate it as necessary
// Format: right(string, len, [pad])
//

int F_right(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	const TCHAR *str;
	TCHAR *newStr, pad;
	LONG newLen;
	DWORD i, len, shift;

   if ((argc < 2) || (argc > 3))
      return NXSL_ERR_INVALID_ARGUMENT_COUNT;

   if (!argv[1]->isInteger())
      return NXSL_ERR_NOT_INTEGER;

   if ((!argv[0]->isString()) ||
		 ((argc == 3) && (!argv[2]->isString())))
		return NXSL_ERR_NOT_STRING;

	if (argc == 3)
	{
		pad = *(argv[2]->getValueAsCString());
		if (pad == 0)
			pad = _T(' ');
	}
	else
	{
		pad = _T(' ');
	}

	newLen = argv[1]->getValueAsInt32();
	if (newLen < 0)
		newLen = 0;

   str = argv[0]->getValueAsString(&len);
	if (len > (DWORD)newLen)
	{
		shift = len - (DWORD)newLen;
		len = (DWORD)newLen;
	}
	else
	{
		shift = 0;
	}
	newStr = (TCHAR *)malloc(newLen * sizeof(TCHAR));
	memcpy(&newStr[(DWORD)newLen - len], &str[shift], len * sizeof(TCHAR));
   for(i = 0; i < (DWORD)newLen - len; i++)
      newStr[i] = pad;
   *ppResult = new NXSL_Value(newStr, newLen);
	free(newStr);
	return 0;
}


//
// Exit from script
//

int F_exit(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	if (argc > 1)
		return NXSL_ERR_INVALID_ARGUMENT_COUNT;

	*ppResult = (argc == 0) ? new NXSL_Value((LONG)0) : new NXSL_Value(argv[0]);
   return NXSL_STOP_SCRIPT_EXECUTION;
}


//
// Trim whitespace characters from the string
//

int F_trim(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	if (!argv[0]->isString())
		return NXSL_ERR_NOT_STRING;

	DWORD len;
	const TCHAR *string = argv[0]->getValueAsString(&len);
	
	int i;
	for(i = 0; (i < (int)len) && (string[i] == _T(' ') || string[i] == _T('\t')); i++);
	int startPos = i;
	if (len > 0)
		for(i = (int)len - 1; (i >= startPos) && (string[i] == _T(' ') || string[i] == _T('\t')); i--);

	*ppResult = new NXSL_Value(&string[startPos], i - startPos + 1);
	return 0;
}


//
// Trim trailing whitespace characters from the string
//

int F_rtrim(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	if (!argv[0]->isString())
		return NXSL_ERR_NOT_STRING;

	DWORD len;
	const TCHAR *string = argv[0]->getValueAsString(&len);
	
	int i;
	for(i = (int)len - 1; (i >= 0) && (string[i] == _T(' ') || string[i] == _T('\t')); i--);

	*ppResult = new NXSL_Value(string, i + 1);
	return 0;
}


//
// Trim leading whitespace characters from the string
//

int F_ltrim(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	if (!argv[0]->isString())
		return NXSL_ERR_NOT_STRING;

	DWORD len;
	const TCHAR *string = argv[0]->getValueAsString(&len);
	
	int i;
	for(i = 0; (i < (int)len) && (string[i] == _T(' ') || string[i] == _T('\t')); i++);

	*ppResult = new NXSL_Value(&string[i], (int)len - i);
	return 0;
}


//
// Trace
//

int F_trace(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	if (!argv[0]->isInteger())
		return NXSL_ERR_NOT_INTEGER;

	if (!argv[1]->isString())
		return NXSL_ERR_NOT_STRING;

	program->trace(argv[0]->getValueAsInt32(), argv[1]->getValueAsCString());
	*ppResult = new NXSL_Value();
	return 0;
}
