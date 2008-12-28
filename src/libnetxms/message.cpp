/* $Id$ */
/* 
** NetXMS - Network Management System
** NetXMS Foundation Library
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
** File: message.cpp
**
**/

#include "libnetxms.h"

#if HAVE_LIBEXPAT
#include <expat.h>
#endif


//
// Parser state for creating CSCPMessage object from XML
//

#define XML_STATE_INIT		-1
#define XML_STATE_END		-2
#define XML_STATE_ERROR    -255
#define XML_STATE_NXCP		0
#define XML_STATE_MESSAGE	1
#define XML_STATE_VARIABLE	2
#define XML_STATE_VALUE		3

typedef struct
{
	CSCPMessage *msg;
	int state;
	int valueLen;
	char *value;
	int varType;
	DWORD varId;
} XML_PARSER_STATE;


//
// Calculate variable size
//

static int VariableSize(CSCP_DF *pVar, BOOL bNetworkByteOrder)
{
   int nSize;

   switch(pVar->bType)
   {
      case CSCP_DT_INTEGER:
         nSize = 12;
         break;
      case CSCP_DT_INT64:
      case CSCP_DT_FLOAT:
         nSize = 16;
         break;
      case CSCP_DT_INT16:
         nSize = 8;
         break;
      case CSCP_DT_STRING:
      case CSCP_DT_BINARY:
         if (bNetworkByteOrder)
            nSize = ntohl(pVar->df_string.dwLen) + 12;
         else
            nSize = pVar->df_string.dwLen + 12;
         break;
      default:
         nSize = 8;
         break;
   }
   return nSize;
}


//
// Default constructor for CSCPMessage class
//

CSCPMessage::CSCPMessage(int nVersion)
{
   m_wCode = 0;
   m_dwId = 0;
   m_dwNumVar = 0;
   m_ppVarList = NULL;
   m_wFlags = 0;
   m_nVersion = nVersion;
}


//
// Create a copy of prepared CSCP message
//

CSCPMessage::CSCPMessage(CSCPMessage *pMsg)
{
   DWORD i;

   m_wCode = pMsg->m_wCode;
   m_dwId = pMsg->m_dwId;
   m_wFlags = pMsg->m_wFlags;
   m_nVersion = pMsg->m_nVersion;
   m_dwNumVar = pMsg->m_dwNumVar;
   m_ppVarList = (CSCP_DF **)malloc(sizeof(CSCP_DF *) * m_dwNumVar);
   for(i = 0; i < m_dwNumVar; i++)
   {
      m_ppVarList[i] = (CSCP_DF *)nx_memdup(pMsg->m_ppVarList[i],
                                            VariableSize(pMsg->m_ppVarList[i], FALSE));
   }
}


//
// Create CSCPMessage object from received message
//

CSCPMessage::CSCPMessage(CSCP_MESSAGE *pMsg, int nVersion)
{
   DWORD i, dwPos, dwSize, dwVar;
   CSCP_DF *pVar;
   int iVarSize;

   m_wFlags = ntohs(pMsg->wFlags);
   m_wCode = ntohs(pMsg->wCode);
   m_dwId = ntohl(pMsg->dwId);
   dwSize = ntohl(pMsg->dwSize);
   m_dwNumVar = ntohl(pMsg->dwNumVars);
   m_ppVarList = (CSCP_DF **)malloc(sizeof(CSCP_DF *) * m_dwNumVar);
   m_nVersion = nVersion;
   
   // Parse data fields
   for(dwPos = CSCP_HEADER_SIZE, dwVar = 0; dwVar < m_dwNumVar; dwVar++)
   {
      pVar = (CSCP_DF *)(((BYTE *)pMsg) + dwPos);

      // Validate position inside message
      if (dwPos > dwSize - 8)
         break;
      if ((dwPos > dwSize - 12) && 
          ((pVar->bType == CSCP_DT_STRING) || (pVar->bType == CSCP_DT_BINARY)))
         break;

      // Calculate and validate variable size
      iVarSize = VariableSize(pVar, TRUE);
      if (dwPos + iVarSize > dwSize)
         break;

      // Create new entry
      m_ppVarList[dwVar] = (CSCP_DF *)malloc(iVarSize);
      memcpy(m_ppVarList[dwVar], pVar, iVarSize);

      // Convert numeric values to host format
      m_ppVarList[dwVar]->dwVarId = ntohl(m_ppVarList[dwVar]->dwVarId);
      switch(pVar->bType)
      {
         case CSCP_DT_INTEGER:
            m_ppVarList[dwVar]->df_int32 = ntohl(m_ppVarList[dwVar]->df_int32);
            break;
         case CSCP_DT_INT64:
            m_ppVarList[dwVar]->df_int64 = ntohq(m_ppVarList[dwVar]->df_int64);
            break;
         case CSCP_DT_INT16:
            m_ppVarList[dwVar]->df_int16 = ntohs(m_ppVarList[dwVar]->df_int16);
            break;
         case CSCP_DT_FLOAT:
            m_ppVarList[dwVar]->df_real = ntohd(m_ppVarList[dwVar]->df_real);
            break;
         case CSCP_DT_STRING:
#if !(WORDS_BIGENDIAN)
            m_ppVarList[dwVar]->df_string.dwLen = ntohl(m_ppVarList[dwVar]->df_string.dwLen);
            for(i = 0; i < m_ppVarList[dwVar]->df_string.dwLen / 2; i++)
               m_ppVarList[dwVar]->df_string.szValue[i] = ntohs(m_ppVarList[dwVar]->df_string.szValue[i]);
#endif
            break;
         case CSCP_DT_BINARY:
            m_ppVarList[dwVar]->df_string.dwLen = ntohl(m_ppVarList[dwVar]->df_string.dwLen);
            break;
      }

      // Starting from version 2, all variables should be 8-byte aligned
      if (m_nVersion >= 2)
         dwPos += iVarSize + ((8 - (iVarSize % 8)) & 7);
      else
         dwPos += iVarSize;
   }

   // Cut unfilled variables, if any
   m_dwNumVar = dwVar;
}


//
// Create CSCPMessage object from XML document
//

#if HAVE_LIBEXPAT

static void StartElement(void *userData, const char *name, const char **attrs)
{
	if (!strcmp(name, "nxcp"))
	{
		((XML_PARSER_STATE *)userData)->state = XML_STATE_NXCP;
	}
	else if (!strcmp(name, "message"))
	{
		((XML_PARSER_STATE *)userData)->state = XML_STATE_MESSAGE;
	}
	else if (!strcmp(name, "variable"))
	{
		((XML_PARSER_STATE *)userData)->state = XML_STATE_VARIABLE;
	}
	else if (!strcmp(name, "value"))
	{
		((XML_PARSER_STATE *)userData)->valueLen = 1;
		((XML_PARSER_STATE *)userData)->value = NULL;
		((XML_PARSER_STATE *)userData)->state = XML_STATE_VALUE;
	}
	else
	{
		((XML_PARSER_STATE *)userData)->state = XML_STATE_ERROR;
	}
	if (((XML_PARSER_STATE *)userData)->state != XML_STATE_ERROR)
		((XML_PARSER_STATE *)userData)->msg->ProcessXMLToken(userData, attrs);
}

static void EndElement(void *userData, const char *name)
{
	if (!strcmp(name, "nxcp"))
	{
		((XML_PARSER_STATE *)userData)->state = XML_STATE_END;
	}
	else if (!strcmp(name, "message"))
	{
		((XML_PARSER_STATE *)userData)->state = XML_STATE_NXCP;
	}
	else if (!strcmp(name, "variable"))
	{
		((XML_PARSER_STATE *)userData)->state = XML_STATE_MESSAGE;
	}
	else if (!strcmp(name, "value"))
	{
		((XML_PARSER_STATE *)userData)->msg->ProcessXMLData(userData);
		safe_free(((XML_PARSER_STATE *)userData)->value);
		((XML_PARSER_STATE *)userData)->state = XML_STATE_VARIABLE;
	}
}

static void CharData(void *userData, const XML_Char *s, int len)
{
	XML_PARSER_STATE *ps = (XML_PARSER_STATE *)userData;

	if (ps->state != XML_STATE_VALUE)
		return;

	ps->value = (char *)realloc(ps->value, ps->valueLen + len);
	memcpy(&ps->value[ps->valueLen - 1], s, len);
	ps->valueLen += len;
	ps->value[ps->valueLen - 1] = 0;
}

#endif

CSCPMessage::CSCPMessage(char *xml)
{
#if HAVE_LIBEXPAT
	XML_Parser parser = XML_ParserCreate(NULL);
	XML_PARSER_STATE state;

	// Default values
   m_wCode = 0;
   m_dwId = 0;
   m_dwNumVar = 0;
   m_ppVarList = NULL;
   m_wFlags = 0;
   m_nVersion = NXCP_VERSION;

	// Parse XML
	state.msg = this;
	state.state = -1;
	XML_SetUserData(parser, &state);
	XML_SetElementHandler(parser, StartElement, EndElement);
	XML_SetCharacterDataHandler(parser, CharData);
	if (XML_Parse(parser, xml, (int)strlen(xml), TRUE) == XML_STATUS_ERROR)
	{
/*fprintf(stderr,
        "%s at line %d\n",
        XML_ErrorString(XML_GetErrorCode(parser)),
        XML_GetCurrentLineNumber(parser));*/
	}
	XML_ParserFree(parser);

#else

	// Default values
   m_wCode = 0;
   m_dwId = 0;
   m_dwNumVar = 0;
   m_ppVarList = NULL;
   m_wFlags = 0;
   m_nVersion = NXCP_VERSION;
#endif
}

#if HAVE_LIBEXPAT

void CSCPMessage::ProcessXMLToken(void *state, const char **attrs)
{
	XML_PARSER_STATE *ps = (XML_PARSER_STATE *)state;
	const char *type;
	static const char *types[] = { "int32", "string", "int64", "int16", "binary", "float", NULL };

	switch(ps->state)
	{
		case XML_STATE_NXCP:
			m_nVersion = XMLGetAttrInt(attrs, "version", m_nVersion);
			break;
		case XML_STATE_MESSAGE:
			m_dwId = XMLGetAttrDWORD(attrs, "id", m_dwId);
			m_wCode = (WORD)XMLGetAttrDWORD(attrs, "code", m_wCode);
			break;
		case XML_STATE_VARIABLE:
			ps->varId = XMLGetAttrDWORD(attrs, "id", 0);
			type = XMLGetAttr(attrs, "type");
			if (type != NULL)
			{
				int i;

				for(i = 0; types[i] != NULL; i++)
					if (!stricmp(types[i], type))
					{
						ps->varType = i;
						break;
					}
			}
			break;
		default:
			break;
	}
}

void CSCPMessage::ProcessXMLData(void *state)
{
	XML_PARSER_STATE *ps = (XML_PARSER_STATE *)state;
	char *binData;
	size_t binLen;
#ifdef UNICODE
	WCHAR *temp;
#endif

	if (ps->value == NULL)
		return;

	switch(ps->varType)
	{
		case CSCP_DT_INTEGER:
			SetVariable(ps->varId, strtoul(ps->value, NULL, 0));
			break;
		case CSCP_DT_INT16:
			SetVariable(ps->varId, (WORD)strtoul(ps->value, NULL, 0));
			break;
		case CSCP_DT_INT64:
			SetVariable(ps->varId, (QWORD)strtoull(ps->value, NULL, 0));
			break;
		case CSCP_DT_FLOAT:
			SetVariable(ps->varId, strtod(ps->value, NULL));
			break;
		case CSCP_DT_STRING:
#ifdef UNICODE
			temp = WideStringFromUTF8String(ps->value);
			SetVariable(ps->varId, temp);
			free(temp);
#else
			SetVariable(ps->varId, ps->value);
#endif
			break;
		case CSCP_DT_BINARY:
			if (base64_decode_alloc(ps->value, ps->valueLen, &binData, &binLen))
			{
				if (binData != NULL)
				{
					SetVariable(ps->varId, (BYTE *)binData, (DWORD)binLen);
					free(binData);
				}
			}
			break;
	}
}

#endif


//
// Destructor for CSCPMessage
//

CSCPMessage::~CSCPMessage()
{
   DeleteAllVariables();
}


//
// Find variable by name
//

DWORD CSCPMessage::FindVariable(DWORD dwVarId)
{
   DWORD i;

   for(i = 0; i < m_dwNumVar; i++)
      if (m_ppVarList[i] != NULL)
         if (m_ppVarList[i]->dwVarId == dwVarId)
            return i;
   return INVALID_INDEX;
}


//
// Set variable
// Argument dwSize (data size) is used only for DT_BINARY type
//

void *CSCPMessage::Set(DWORD dwVarId, BYTE bType, const void *pValue, DWORD dwSize)
{
   DWORD dwIndex, dwLength;
   CSCP_DF *pVar;
#if !defined(UNICODE_UCS2) || !defined(UNICODE)
   UCS2CHAR *pBuffer;
#endif

   // Create CSCP_DF structure
   switch(bType)
   {
      case CSCP_DT_INTEGER:
         pVar = (CSCP_DF *)malloc(12);
         pVar->df_int32 = *((const DWORD *)pValue);
         break;
      case CSCP_DT_INT16:
         pVar = (CSCP_DF *)malloc(8);
         pVar->df_int16 = *((const WORD *)pValue);
         break;
      case CSCP_DT_INT64:
         pVar = (CSCP_DF *)malloc(16);
         pVar->df_int64 = *((const QWORD *)pValue);
         break;
      case CSCP_DT_FLOAT:
         pVar = (CSCP_DF *)malloc(16);
         pVar->df_real = *((const double *)pValue);
         break;
      case CSCP_DT_STRING:
         dwLength = (DWORD)_tcslen((const TCHAR *)pValue);
         pVar = (CSCP_DF *)malloc(12 + dwLength * 2);
         pVar->df_string.dwLen = dwLength * 2;
#ifdef UNICODE         
#ifdef UNICODE_UCS2
         memcpy(pVar->df_string.szValue, pValue, pVar->df_string.dwLen);
#else		/* assume UNICODE_UCS4 */
         pBuffer = (UCS2CHAR *)malloc(dwLength * 2 + 2);
         ucs4_to_ucs2((WCHAR *)pValue, dwLength, pBuffer, dwLength + 1);
         memcpy(pVar->df_string.szValue, pBuffer, pVar->df_string.dwLen);
         free(pBuffer);
#endif         
#else		/* not UNICODE */
         pBuffer = (UCS2CHAR *)malloc(dwLength * 2 + 2);
         mb_to_ucs2((const char *)pValue, dwLength, pBuffer, dwLength + 1);
         memcpy(pVar->df_string.szValue, pBuffer, pVar->df_string.dwLen);
         free(pBuffer);
#endif
         break;
      case CSCP_DT_BINARY:
         pVar = (CSCP_DF *)malloc(12 + dwSize);
         pVar->df_string.dwLen = dwSize;
         if ((pVar->df_string.dwLen > 0) && (pValue != NULL))
            memcpy(pVar->df_string.szValue, pValue, pVar->df_string.dwLen);
         break;
      default:
         return NULL;  // Invalid data type, unable to handle
   }
   pVar->dwVarId = dwVarId;
   pVar->bType = bType;

   // Check if variable exists
   dwIndex = FindVariable(pVar->dwVarId);
   if (dwIndex == INVALID_INDEX) // Add new variable to list
   {
      m_ppVarList = (CSCP_DF **)realloc(m_ppVarList, sizeof(CSCP_DF *) * (m_dwNumVar + 1));
      m_ppVarList[m_dwNumVar] = pVar;
      m_dwNumVar++;
   }
   else  // Replace existing variable
   {
      free(m_ppVarList[dwIndex]);
      m_ppVarList[dwIndex] = pVar;
   }

   return (bType == CSCP_DT_INT16) ? ((void *)((BYTE *)pVar + 6)) : ((void *)((BYTE *)pVar + 8));
}


//
// Get variable value
//

void *CSCPMessage::Get(DWORD dwVarId, BYTE bType)
{
   DWORD dwIndex;

   // Find variable
   dwIndex = FindVariable(dwVarId);
   if (dwIndex == INVALID_INDEX)
      return NULL;      // No such variable

   // Check data type
   if (m_ppVarList[dwIndex]->bType != bType)
      return NULL;

   return (bType == CSCP_DT_INT16) ?
             ((void *)((BYTE *)m_ppVarList[dwIndex] + 6)) : 
             ((void *)((BYTE *)m_ppVarList[dwIndex] + 8));
}


//
// Get integer variable
//

DWORD CSCPMessage::GetVariableLong(DWORD dwVarId)
{
   char *pValue;

   pValue = (char *)Get(dwVarId, CSCP_DT_INTEGER);
   return pValue ? *((DWORD *)pValue) : 0;
}


//
// Get 16-bit integer variable
//

WORD CSCPMessage::GetVariableShort(DWORD dwVarId)
{
   void *pValue;

   pValue = Get(dwVarId, CSCP_DT_INT16);
   return pValue ? *((WORD *)pValue) : 0;
}


//
// Get 16-bit integer variable as signel 32-bit integer
//

LONG CSCPMessage::GetVariableShortAsInt32(DWORD dwVarId)
{
   void *pValue;

   pValue = Get(dwVarId, CSCP_DT_INT16);
   return pValue ? *((short *)pValue) : 0;
}


//
// Get 64-bit integer variable
//

QWORD CSCPMessage::GetVariableInt64(DWORD dwVarId)
{
   char *pValue;

   pValue = (char *)Get(dwVarId, CSCP_DT_INT64);
   return pValue ? *((QWORD *)pValue) : 0;
}


//
// Get 64-bit floating point variable
//

double CSCPMessage::GetVariableDouble(DWORD dwVarId)
{
   char *pValue;

   pValue = (char *)Get(dwVarId, CSCP_DT_FLOAT);
   return pValue ? *((double *)pValue) : 0;
}


//
// Get string variable
// If szBuffer is NULL, memory block of required size will be allocated
// for result; if szBuffer is not NULL, entire result or part of it will
// be placed to szBuffer and pointer to szBuffer will be returned.
// Note: dwBufSize is buffer size in characters, not bytes!
//

TCHAR *CSCPMessage::GetVariableStr(DWORD dwVarId, TCHAR *pszBuffer, DWORD dwBufSize)
{
   void *pValue;
   TCHAR *pStr = NULL;
   DWORD dwLen;

   if ((pszBuffer != NULL) && (dwBufSize == 0))
      return NULL;   // non-sense combination

   pValue = Get(dwVarId, CSCP_DT_STRING);
   if (pValue != NULL)
   {
      if (pszBuffer == NULL)
      {
#if defined(UNICODE) && defined(UNICODE_UCS4)
         pStr = (TCHAR *)malloc(*((DWORD *)pValue) * 2 + 4);
#elif defined(UNICODE) && defined(UNICODE_UCS2)
         pStr = (TCHAR *)malloc(*((DWORD *)pValue) + 2);
#else
         pStr = (TCHAR *)malloc(*((DWORD *)pValue) / 2 + 1);
#endif
      }
      else
      {
         pStr = pszBuffer;
      }

      dwLen = (pszBuffer == NULL) ? (*((DWORD *)pValue) / 2) : min(*((DWORD *)pValue) / 2, dwBufSize - 1);
#if defined(UNICODE) && defined(UNICODE_UCS4)
		ucs2_to_ucs4((UCS2CHAR *)((BYTE *)pValue + 4), dwLen, pStr, dwLen + 1);
#elif defined(UNICODE) && defined(UNICODE_UCS2)
      memcpy(pStr, (BYTE *)pValue + 4, dwLen * 2);
#else
		ucs2_to_mb((UCS2CHAR *)((BYTE *)pValue + 4), dwLen, pStr, dwLen + 1);
#endif
      pStr[dwLen] = 0;
   }
   else
   {
      if (pszBuffer != NULL)
      {
         pStr = pszBuffer;
         pStr[0] = 0;
      }
   }
   return pStr;
}


//
// Get binary (byte array) variable
// Result will be placed to the buffer provided (no more than dwBufSize bytes,
// and actual size of data will be returned
// If pBuffer is NULL, just actual data length is returned
//

DWORD CSCPMessage::GetVariableBinary(DWORD dwVarId, BYTE *pBuffer, DWORD dwBufSize)
{
   void *pValue;
   DWORD dwSize;

   pValue = Get(dwVarId, CSCP_DT_BINARY);
   if (pValue != NULL)
   {
      dwSize = *((DWORD *)pValue);
      if (pBuffer != NULL)
         memcpy(pBuffer, (BYTE *)pValue + 4, min(dwBufSize, dwSize));
   }
   else
   {
      dwSize = 0;
   }
   return dwSize;
}


//
// Build protocol message ready to be send over the wire
//

CSCP_MESSAGE *CSCPMessage::CreateMessage(void)
{
   DWORD dwSize;
   int iVarSize;
   DWORD i, j;
   CSCP_MESSAGE *pMsg;
   CSCP_DF *pVar;

   // Calculate message size
   for(i = 0, dwSize = CSCP_HEADER_SIZE; i < m_dwNumVar; i++)
   {
      iVarSize = VariableSize(m_ppVarList[i], FALSE);
      if (m_nVersion >= 2)
         dwSize += iVarSize + ((8 - (iVarSize % 8)) & 7);
      else
         dwSize += iVarSize;
   }

   // Message should be aligned to 8 bytes boundary
   // This is always the case starting from version 2 because
   // all variables padded to be _kratnimi_ 8 bytes
   if (m_nVersion < 2)
      dwSize += (8 - (dwSize % 8)) & 7;

   // Create message
   pMsg = (CSCP_MESSAGE *)malloc(dwSize);
   pMsg->wCode = htons(m_wCode);
   pMsg->wFlags = htons(m_wFlags);
   pMsg->dwSize = htonl(dwSize);
   pMsg->dwId = htonl(m_dwId);
   pMsg->dwNumVars = htonl(m_dwNumVar);

   // Fill data fields
   for(i = 0, pVar = (CSCP_DF *)((char *)pMsg + CSCP_HEADER_SIZE); i < m_dwNumVar; i++)
   {
      iVarSize = VariableSize(m_ppVarList[i], FALSE);
      memcpy(pVar, m_ppVarList[i], iVarSize);

      // Convert numeric values to network format
      pVar->dwVarId = htonl(pVar->dwVarId);
      switch(pVar->bType)
      {
         case CSCP_DT_INTEGER:
            pVar->df_int32 = htonl(pVar->df_int32);
            break;
         case CSCP_DT_INT64:
            pVar->df_int64 = htonq(pVar->df_int64);
            break;
         case CSCP_DT_INT16:
            pVar->df_int16 = htons(pVar->df_int16);
            break;
         case CSCP_DT_FLOAT:
            pVar->df_real = htond(pVar->df_real);
            break;
         case CSCP_DT_STRING:
#if !(WORDS_BIGENDIAN)
            for(j = 0; j < pVar->df_string.dwLen / 2; j++)
               pVar->df_string.szValue[j] = htons(pVar->df_string.szValue[j]);
            pVar->df_string.dwLen = htonl(pVar->df_string.dwLen);
#endif
            break;
         case CSCP_DT_BINARY:
            pVar->df_string.dwLen = htonl(pVar->df_string.dwLen);
            break;
      }

      if (m_nVersion >= 2)
         pVar = (CSCP_DF *)((char *)pVar + iVarSize + ((8 - (iVarSize % 8)) & 7));
      else
         pVar = (CSCP_DF *)((char *)pVar + iVarSize);
   }

   return pMsg;
}


//
// Delete all variables
//

void CSCPMessage::DeleteAllVariables(void)
{
   if (m_ppVarList != NULL)
   {
      DWORD i;

      for(i = 0; i < m_dwNumVar; i++)
         safe_free(m_ppVarList[i]);
      free(m_ppVarList);

      m_ppVarList = NULL;
      m_dwNumVar = 0;
   }
}


//
// Set binary variable to an array of DWORDs
//

void CSCPMessage::SetVariableToInt32Array(DWORD dwVarId, DWORD dwNumElements, DWORD *pdwData)
{
   DWORD i, *pdwBuffer;

   pdwBuffer = (DWORD *)Set(dwVarId, CSCP_DT_BINARY, pdwData, dwNumElements * sizeof(DWORD));
   if (pdwBuffer != NULL)
   {
      pdwBuffer++;   // First DWORD is a length field
      for(i = 0; i < dwNumElements; i++)  // Convert DWORDs to network byte order
         pdwBuffer[i] = htonl(pdwBuffer[i]);
   }
}


//
// Get binary variable as an array of DWORDs
//

DWORD CSCPMessage::GetVariableInt32Array(DWORD dwVarId, DWORD dwNumElements, DWORD *pdwBuffer)
{
   DWORD i, dwSize;

   dwSize = GetVariableBinary(dwVarId, (BYTE *)pdwBuffer, dwNumElements * sizeof(DWORD));
   dwSize /= sizeof(DWORD);   // Convert bytes to elements
   for(i = 0; i < dwSize; i++)
      pdwBuffer[i] = ntohl(pdwBuffer[i]);
   return dwSize;
}


//
// Set binary variable from file
//

BOOL CSCPMessage::SetVariableFromFile(DWORD dwVarId, const TCHAR *pszFileName)
{
   FILE *pFile;
   BYTE *pBuffer;
   DWORD dwSize;
   BOOL bResult = FALSE;

   dwSize = (DWORD)FileSize(pszFileName);
   pFile = _tfopen(pszFileName, _T("rb"));
   if (pFile != NULL)
   {
      pBuffer = (BYTE *)Set(dwVarId, CSCP_DT_BINARY, NULL, dwSize);
      if (pBuffer != NULL)
      {
         if (fread(pBuffer + sizeof(DWORD), 1, dwSize, pFile) == dwSize)
            bResult = TRUE;
      }
      fclose(pFile);
   }
   return bResult;
}


//
// Create XML document
//

char *CSCPMessage::CreateXML(void)
{
	String xml;
	DWORD i;
	char *out, *bdata;
	size_t blen;
	TCHAR *tempStr;
#if !defined(UNICODE) || defined(UNICODE_UCS4)
	int bytes;
#endif
	static const TCHAR *dtString[] = { _T("int32"), _T("string"), _T("int64"), _T("int16"), _T("binary"), _T("float") };

	xml.AddFormattedString(_T("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<nxcp version=\"%d\">\r\n   <message code=\"%d\" id=\"%d\">\r\n"), m_nVersion, m_wCode, m_dwId);
	for(i = 0; i < m_dwNumVar; i++)
	{
		xml.AddFormattedString(_T("      <variable id=\"%d\" type=\"%s\">\r\n         <value>"),
		                       m_ppVarList[i]->dwVarId, dtString[m_ppVarList[i]->bType]);
		switch(m_ppVarList[i]->bType)
		{
			case CSCP_DT_INTEGER:
				xml.AddFormattedString(_T("%d"), m_ppVarList[i]->data.dwInteger);
				break;
			case CSCP_DT_INT16:
				xml.AddFormattedString(_T("%d"), m_ppVarList[i]->wInt16);
				break;
			case CSCP_DT_INT64:
				xml.AddFormattedString(INT64_FMT, m_ppVarList[i]->data.qwInt64);
				break;
			case CSCP_DT_STRING:
#ifdef UNICODE
#ifdef UNICODE_UCS2
				xml.AddDynamicString(EscapeStringForXML((TCHAR *)m_ppVarList[i]->data.string.szValue, m_ppVarList[i]->data.string.dwLen / 2));
#else
				tempStr = (WCHAR *)malloc(m_ppVarList[i]->data.string.dwLen * 2);
				bytes = ucs2_to_ucs4(m_ppVarList[i]->data.string.szValue, m_ppVarList[i]->data.string.dwLen / 2, tempStr, m_ppVarList[i]->data.string.dwLen / 2);
				xml.AddDynamicString(EscapeStringForXML(tempStr, bytes));
				free(tempStr);
#endif
#else		/* not UNICODE */
#ifdef UNICODE_UCS2
				bytes = WideCharToMultiByte(CP_UTF8, 0, (UCS2CHAR *)m_ppVarList[i]->data.string.szValue,
				                            m_ppVarList[i]->data.string.dwLen / 2, NULL, 0, NULL, NULL);
				tempStr = (char *)malloc(bytes + 1);
				bytes = WideCharToMultiByte(CP_UTF8, 0, (UCS2CHAR *)m_ppVarList[i]->data.string.szValue,
				                            m_ppVarList[i]->data.string.dwLen / 2, tempStr, bytes + 1, NULL, NULL);
				xml.AddDynamicString(EscapeStringForXML(tempStr, bytes));
				free(tempStr);
#else
				tempStr = (char *)malloc(m_ppVarList[i]->data.string.dwLen);
				bytes = ucs2_to_utf8(m_ppVarList[i]->data.string.szValue, m_ppVarList[i]->data.string.dwLen / 2, tempStr, m_ppVarList[i]->data.string.dwLen);
				xml.AddDynamicString(EscapeStringForXML(tempStr, bytes));
				free(tempStr);
#endif
#endif	/* UNICODE */
				break;
			case CSCP_DT_BINARY:
				blen = base64_encode_alloc((char *)m_ppVarList[i]->data.string.szValue,
				                           m_ppVarList[i]->data.string.dwLen, &bdata);
				if ((blen != 0) && (bdata != NULL))
				{
#ifdef UNICODE
					tempStr = (WCHAR *)malloc((blen + 1) * sizeof(WCHAR));
					MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, bdata, blen, tempStr, blen);
					tempStr[blen] = 0;
					xml.AddDynamicString(tempStr);
#else
					xml.AddString(bdata, (DWORD)blen);
#endif
				}
				safe_free(bdata);
				break;
			default:
				break;
		}
		xml += _T("</value>\r\n      </variable>\r\n");
	}
	xml += _T("   </message>\r\n</nxcp>\r\n");

#ifdef UNICODE
	out = UTF8StringFromWideString(xml);
#else
	out = strdup(xml);
#endif
	return out;
}
