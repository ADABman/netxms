/* $Id$ */
/* 
** NetXMS - Network Management System
** Copyright (C) 2003, 2004, 2005, 2006, 2007 Victor Kirhenshtein
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
** File: dcithreshold.cpp
**
**/

#include "nxcore.h"


//
// Default constructor
//

Threshold::Threshold(DCItem *pRelatedItem)
{
   m_id = 0;
   m_itemId = pRelatedItem->Id();
   m_eventCode = EVENT_THRESHOLD_REACHED;
   m_rearmEventCode = EVENT_THRESHOLD_REARMED;
   m_function = F_LAST;
   m_operation = OP_EQ;
   m_dataType = pRelatedItem->DataType();
   m_param1 = 1;
   m_param2 = 0;
   m_isReached = FALSE;
	m_repeatInterval = -1;
	m_lastEventTimestamp = 0;
	m_numMatches = 0;
}


//
// Constructor for NXMP parser
//

Threshold::Threshold()
{
   m_id = 0;
   m_itemId = 0;
   m_eventCode = EVENT_THRESHOLD_REACHED;
   m_rearmEventCode = EVENT_THRESHOLD_REARMED;
   m_function = F_LAST;
   m_operation = OP_EQ;
   m_dataType = 0;
   m_param1 = 1;
   m_param2 = 0;
   m_isReached = FALSE;
	m_repeatInterval = -1;
	m_lastEventTimestamp = 0;
	m_numMatches = 0;
}


//
// Create from another threshold object
//

Threshold::Threshold(Threshold *src)
{
   m_id = src->m_id;
   m_itemId = src->m_itemId;
   m_eventCode = src->m_eventCode;
   m_rearmEventCode = src->m_rearmEventCode;
   m_value = src->m_value;
   m_function = src->m_function;
   m_operation = src->m_operation;
   m_dataType = src->m_dataType;
   m_param1 = src->m_param1;
   m_param2 = src->m_param2;
   m_isReached = FALSE;
	m_repeatInterval = src->m_repeatInterval;
	m_lastEventTimestamp = 0;
	m_numMatches = 0;
}


//
// Constructor for creating object from database
// This constructor assumes that SELECT query look as following:
// SELECT threshold_id,fire_value,rearm_value,check_function,check_operation,
//        parameter_1,parameter_2,event_code,current_state,
//        rearm_event_code,repeat_interval FROM thresholds
//

Threshold::Threshold(DB_RESULT hResult, int iRow, DCItem *pRelatedItem)
{
   TCHAR szBuffer[MAX_DB_STRING];

   m_id = DBGetFieldULong(hResult, iRow, 0);
   m_itemId = pRelatedItem->Id();
   m_eventCode = DBGetFieldULong(hResult, iRow, 7);
   m_rearmEventCode = DBGetFieldULong(hResult, iRow, 9);
   DBGetField(hResult, iRow, 1, szBuffer, MAX_DB_STRING);
   DecodeSQLString(szBuffer);
   m_value = szBuffer;
   m_function = (BYTE)DBGetFieldLong(hResult, iRow, 3);
   m_operation = (BYTE)DBGetFieldLong(hResult, iRow, 4);
   m_dataType = pRelatedItem->DataType();
   m_param1 = DBGetFieldLong(hResult, iRow, 5);
	if ((m_function == F_LAST) && (m_param1 < 1))
		m_param1 = 1;
   m_param2 = DBGetFieldLong(hResult, iRow, 6);
   m_isReached = DBGetFieldLong(hResult, iRow, 8);
	m_repeatInterval = DBGetFieldLong(hResult, iRow, 10);
	m_lastEventTimestamp = 0;
	m_numMatches = 0;
}


//
// Destructor
//

Threshold::~Threshold()
{
}


//
// Create new unique id for object
//

void Threshold::createId(void)
{
   m_id = CreateUniqueId(IDG_THRESHOLD); 
}


//
// Save threshold to database
//

BOOL Threshold::saveToDB(DB_HANDLE hdb, DWORD dwIndex)
{
   TCHAR *pszEscValue, szQuery[512];
   DB_RESULT hResult;
   BOOL bNewObject = TRUE;

   // Check for object's existence in database
   _stprintf(szQuery, _T("SELECT threshold_id FROM thresholds WHERE threshold_id=%d"), m_id);
   hResult = DBSelect(hdb, szQuery);
   if (hResult != 0)
   {
      if (DBGetNumRows(hResult) > 0)
         bNewObject = FALSE;
      DBFreeResult(hResult);
   }

   // Prepare and execute query
   pszEscValue = EncodeSQLString(m_value.String());
   if (bNewObject)
      sprintf(szQuery, "INSERT INTO thresholds (threshold_id,item_id,fire_value,rearm_value,"
                       "check_function,check_operation,parameter_1,parameter_2,event_code,"
                       "sequence_number,current_state,rearm_event_code,repeat_interval) VALUES "
                       "(%d,%d,'%s','#00',%d,%d,%d,%d,%d,%d,%d,%d,%d)", 
              m_id, m_itemId, pszEscValue, m_function, m_operation, m_param1,
              m_param2, m_eventCode, dwIndex, m_isReached, m_rearmEventCode,
				  m_repeatInterval);
   else
      sprintf(szQuery, "UPDATE thresholds SET item_id=%d,fire_value='%s',check_function=%d,"
                       "check_operation=%d,parameter_1=%d,parameter_2=%d,event_code=%d,"
                       "sequence_number=%d,current_state=%d,"
                       "rearm_event_code=%d,repeat_interval=%d WHERE threshold_id=%d",
              m_itemId, pszEscValue, m_function, m_operation, m_param1,
              m_param2, m_eventCode, dwIndex, m_isReached,
              m_rearmEventCode, m_repeatInterval, m_id);
   free(pszEscValue);
   return DBQuery(hdb, szQuery);
}


//
// Check threshold
// Function will return the following codes:
//    THRESHOLD_REACHED - when item's value match the threshold condition while previous check doesn't
//    THRESHOLD_REARMED - when item's value doesn't match the threshold condition while previous check do
//    NO_ACTION - when there are no changes in item's value match to threshold's condition
//

int Threshold::check(ItemValue &value, ItemValue **ppPrevValues, ItemValue &fvalue)
{
   BOOL bMatch = FALSE;
   int iResult, iDataType = m_dataType;

   // Execute function on value
   switch(m_function)
   {
      case F_LAST:         // Check last value only
         fvalue = value;
         break;
      case F_AVERAGE:      // Check average value for last n polls
         calculateAverageValue(&fvalue, value, ppPrevValues);
         break;
      case F_DEVIATION:    // Check mean absolute deviation
         calculateMDValue(&fvalue, value, ppPrevValues);
         break;
      case F_DIFF:
         calculateDiff(&fvalue, value, ppPrevValues);
         if (m_dataType == DCI_DT_STRING)
            iDataType = DCI_DT_INT;  // diff() for strings is an integer
         break;
      case F_ERROR:        // Check for collection error
         fvalue = (DWORD)0;
         break;
      default:
         break;
   }

   // Run comparision operation on function result and threshold value
   if (m_function == F_ERROR)
   {
      // Threshold::Check() can be called only for valid values, which
      // means that error thresholds cannot be active
      bMatch = FALSE;
   }
   else
   {
      switch(m_operation)
      {
         case OP_LE:    // Less
            switch(iDataType)
            {
               case DCI_DT_INT:
                  bMatch = ((LONG)fvalue < (LONG)m_value);
                  break;
               case DCI_DT_UINT:
                  bMatch = ((DWORD)fvalue < (DWORD)m_value);
                  break;
               case DCI_DT_INT64:
                  bMatch = ((INT64)fvalue < (INT64)m_value);
                  break;
               case DCI_DT_UINT64:
                  bMatch = ((QWORD)fvalue < (QWORD)m_value);
                  break;
               case DCI_DT_FLOAT:
                  bMatch = ((double)fvalue < (double)m_value);
                  break;
            }
            break;
         case OP_LE_EQ: // Less or equal
            switch(iDataType)
            {
               case DCI_DT_INT:
                  bMatch = ((LONG)fvalue <= (LONG)m_value);
                  break;
               case DCI_DT_UINT:
                  bMatch = ((DWORD)fvalue <= (DWORD)m_value);
                  break;
               case DCI_DT_INT64:
                  bMatch = ((INT64)fvalue <= (INT64)m_value);
                  break;
               case DCI_DT_UINT64:
                  bMatch = ((QWORD)fvalue <= (QWORD)m_value);
                  break;
               case DCI_DT_FLOAT:
                  bMatch = ((double)fvalue <= (double)m_value);
                  break;
            }
            break;
         case OP_EQ:    // Equal
            switch(iDataType)
            {
               case DCI_DT_INT:
                  bMatch = ((LONG)fvalue == (LONG)m_value);
                  break;
               case DCI_DT_UINT:
                  bMatch = ((DWORD)fvalue == (DWORD)m_value);
                  break;
               case DCI_DT_INT64:
                  bMatch = ((INT64)fvalue == (INT64)m_value);
                  break;
               case DCI_DT_UINT64:
                  bMatch = ((QWORD)fvalue == (QWORD)m_value);
                  break;
               case DCI_DT_FLOAT:
                  bMatch = ((double)fvalue == (double)m_value);
                  break;
               case DCI_DT_STRING:
                  bMatch = !strcmp(fvalue.String(), m_value.String());
                  break;
            }
            break;
         case OP_GT_EQ: // Greater or equal
            switch(iDataType)
            {
               case DCI_DT_INT:
                  bMatch = ((LONG)fvalue >= (LONG)m_value);
                  break;
               case DCI_DT_UINT:
                  bMatch = ((DWORD)fvalue >= (DWORD)m_value);
                  break;
               case DCI_DT_INT64:
                  bMatch = ((INT64)fvalue >= (INT64)m_value);
                  break;
               case DCI_DT_UINT64:
                  bMatch = ((QWORD)fvalue >= (QWORD)m_value);
                  break;
               case DCI_DT_FLOAT:
                  bMatch = ((double)fvalue >= (double)m_value);
                  break;
            }
            break;
         case OP_GT:    // Greater
            switch(iDataType)
            {
               case DCI_DT_INT:
                  bMatch = ((LONG)fvalue > (LONG)m_value);
                  break;
               case DCI_DT_UINT:
                  bMatch = ((DWORD)fvalue > (DWORD)m_value);
                  break;
               case DCI_DT_INT64:
                  bMatch = ((INT64)fvalue > (INT64)m_value);
                  break;
               case DCI_DT_UINT64:
                  bMatch = ((QWORD)fvalue > (QWORD)m_value);
                  break;
               case DCI_DT_FLOAT:
                  bMatch = ((double)fvalue > (double)m_value);
                  break;
            }
            break;
         case OP_NE:    // Not equal
            switch(iDataType)
            {
               case DCI_DT_INT:
                  bMatch = ((LONG)fvalue != (LONG)m_value);
                  break;
               case DCI_DT_UINT:
                  bMatch = ((DWORD)fvalue != (DWORD)m_value);
                  break;
               case DCI_DT_INT64:
                  bMatch = ((INT64)fvalue != (INT64)m_value);
                  break;
               case DCI_DT_UINT64:
                  bMatch = ((QWORD)fvalue != (QWORD)m_value);
                  break;
               case DCI_DT_FLOAT:
                  bMatch = ((double)fvalue != (double)m_value);
                  break;
               case DCI_DT_STRING:
                  bMatch = strcmp(fvalue.String(), m_value.String());
                  break;
            }
            break;
         case OP_LIKE:
            // This operation can be performed only on strings
            if (m_dataType == DCI_DT_STRING)
               bMatch = MatchString(m_value.String(), fvalue.String(), TRUE);
            break;
         case OP_NOTLIKE:
            // This operation can be performed only on strings
            if (m_dataType == DCI_DT_STRING)
               bMatch = !MatchString(m_value.String(), fvalue.String(), TRUE);
            break;
         default:
            break;
      }
   }

	// Check for number of consecutive matches
	if (m_function == F_LAST)
	{
		if (bMatch)
		{
			m_numMatches++;
			if (m_numMatches < m_param1)
				bMatch = FALSE;
		}
		else
		{
			m_numMatches = 0;
		}
	}

   iResult = (bMatch & !m_isReached) ? THRESHOLD_REACHED :
                ((!bMatch & m_isReached) ? THRESHOLD_REARMED : NO_ACTION);
   m_isReached = bMatch;
   if (iResult != NO_ACTION)
   {
      TCHAR szQuery[256];

      // Update threshold status in database
      _sntprintf(szQuery, 256,
                 _T("UPDATE thresholds SET current_state=%d WHERE threshold_id=%d"),
                 m_isReached, m_id);
      QueueSQLRequest(szQuery);
   }
   return iResult;
}


//
// Check for collection error thresholds
// Return same values as Check()
//

int Threshold::checkError(DWORD dwErrorCount)
{
   int nResult;
   BOOL bMatch;

   if (m_function != F_ERROR)
      return NO_ACTION;

   bMatch = ((DWORD)m_param1 <= dwErrorCount);
   nResult = (bMatch & !m_isReached) ? THRESHOLD_REACHED :
                ((!bMatch & m_isReached) ? THRESHOLD_REARMED : NO_ACTION);
   m_isReached = bMatch;
   if (nResult != NO_ACTION)
   {
      TCHAR szQuery[256];

      // Update threshold status in database
      _sntprintf(szQuery, 256,
                 _T("UPDATE thresholds SET current_state=%d WHERE threshold_id=%d"),
                 m_isReached, m_id);
      QueueSQLRequest(szQuery);
   }
   return nResult;
}


//
// Fill DCI_THRESHOLD with object's data ready to send over the network
//

void Threshold::createMessage(CSCPMessage *msg, DWORD baseId)
{
	DWORD varId = baseId;

	msg->SetVariable(varId++, m_id);
	msg->SetVariable(varId++, m_eventCode);
	msg->SetVariable(varId++, m_rearmEventCode);
	msg->SetVariable(varId++, (WORD)m_function);
	msg->SetVariable(varId++, (WORD)m_operation);
	msg->SetVariable(varId++, (DWORD)m_param1);
	msg->SetVariable(varId++, (DWORD)m_param2);
	msg->SetVariable(varId++, (DWORD)m_repeatInterval);
	msg->SetVariable(varId++, m_value.String());
}


//
// Update threshold object from DCI_THRESHOLD structure
//

void Threshold::updateFromMessage(CSCPMessage *msg, DWORD baseId)
{
	TCHAR buffer[MAX_DCI_STRING_VALUE];
	DWORD varId = baseId + 1;	// Skip ID field

	m_eventCode = msg->GetVariableLong(varId++);
	m_rearmEventCode = msg->GetVariableLong(varId++);
   m_function = (BYTE)msg->GetVariableShort(varId++);
   m_operation = (BYTE)msg->GetVariableShort(varId++);
   m_param1 = (int)msg->GetVariableLong(varId++);
   m_param2 = (int)msg->GetVariableLong(varId++);
	m_repeatInterval = (int)msg->GetVariableLong(varId++);
	m_value = msg->GetVariableStr(varId++, buffer, MAX_DCI_STRING_VALUE);
}


//
// Calculate average value for parameter
//

#define CALC_AVG_VALUE(vtype) \
{ \
   vtype var; \
   var = (vtype)lastValue; \
   for(i = 1, nValueCount = 1; i < m_param1; i++) \
   { \
      if (ppPrevValues[i - 1]->GetTimeStamp() != 1) \
      { \
         var += (vtype)(*ppPrevValues[i - 1]); \
         nValueCount++; \
      } \
   } \
   *pResult = var / (vtype)nValueCount; \
}

void Threshold::calculateAverageValue(ItemValue *pResult, ItemValue &lastValue, ItemValue **ppPrevValues)
{
   int i, nValueCount;

   switch(m_dataType)
   {
      case DCI_DT_INT:
         CALC_AVG_VALUE(LONG);
         break;
      case DCI_DT_UINT:
         CALC_AVG_VALUE(DWORD);
         break;
      case DCI_DT_INT64:
         CALC_AVG_VALUE(INT64);
         break;
      case DCI_DT_UINT64:
         CALC_AVG_VALUE(QWORD);
         break;
      case DCI_DT_FLOAT:
         CALC_AVG_VALUE(double);
         break;
      case DCI_DT_STRING:
         *pResult = _T("");   // Average value for string is meaningless
         break;
      default:
         break;
   }
}


//
// Calculate mean absolute deviation for parameter
//

#define CALC_MD_VALUE(vtype) \
{ \
   vtype mean, dev; \
   mean = (vtype)lastValue; \
   for(i = 1, nValueCount = 1; i < m_param1; i++) \
   { \
      if (ppPrevValues[i - 1]->GetTimeStamp() != 1) \
      { \
         mean += (vtype)(*ppPrevValues[i - 1]); \
         nValueCount++; \
      } \
   } \
   mean /= (vtype)nValueCount; \
   dev = ABS((vtype)lastValue - mean); \
   for(i = 1, nValueCount = 1; i < m_param1; i++) \
   { \
      if (ppPrevValues[i - 1]->GetTimeStamp() != 1) \
      { \
         dev += ABS((vtype)(*ppPrevValues[i - 1]) - mean); \
         nValueCount++; \
      } \
   } \
   *pResult = dev / (vtype)nValueCount; \
}

void Threshold::calculateMDValue(ItemValue *pResult, ItemValue &lastValue, ItemValue **ppPrevValues)
{
   int i, nValueCount;

   switch(m_dataType)
   {
      case DCI_DT_INT:
#define ABS(x) ((x) < 0 ? -(x) : (x))
         CALC_MD_VALUE(LONG);
         break;
      case DCI_DT_INT64:
         CALC_MD_VALUE(INT64);
         break;
      case DCI_DT_FLOAT:
         CALC_MD_VALUE(double);
         break;
      case DCI_DT_UINT:
#undef ABS
#define ABS(x) (x)
         CALC_MD_VALUE(DWORD);
         break;
      case DCI_DT_UINT64:
         CALC_MD_VALUE(QWORD);
         break;
      case DCI_DT_STRING:
         *pResult = _T("");   // Mean deviation for string is meaningless
         break;
      default:
         break;
   }
}

#undef ABS


//
// Calculate difference between last and previous value
//

void Threshold::calculateDiff(ItemValue *pResult, ItemValue &lastValue, ItemValue **ppPrevValues)
{
   CalculateItemValueDiff(*pResult, m_dataType, lastValue, *ppPrevValues[0]);
}


//
// Compare to another threshold
//

BOOL Threshold::compare(Threshold *pThr)
{
   BOOL bMatch;

   switch(m_dataType)
   {
      case DCI_DT_INT:
         bMatch = ((LONG)pThr->m_value == (LONG)m_value);
         break;
      case DCI_DT_UINT:
         bMatch = ((DWORD)pThr->m_value == (DWORD)m_value);
         break;
      case DCI_DT_INT64:
         bMatch = ((INT64)pThr->m_value == (INT64)m_value);
         break;
      case DCI_DT_UINT64:
         bMatch = ((QWORD)pThr->m_value == (QWORD)m_value);
         break;
      case DCI_DT_FLOAT:
         bMatch = ((double)pThr->m_value == (double)m_value);
         break;
      case DCI_DT_STRING:
         bMatch = !strcmp(pThr->m_value.String(), m_value.String());
         break;
      default:
         bMatch = TRUE;
         break;
   }
   return bMatch &&
          (pThr->m_eventCode == m_eventCode) &&
          (pThr->m_rearmEventCode == m_rearmEventCode) &&
          (pThr->m_dataType == m_dataType) &&
          (pThr->m_function == m_function) &&
          (pThr->m_operation == m_operation) &&
          (pThr->m_param1 == m_param1) &&
          (pThr->m_param2 == m_param2) &&
			 (pThr->m_repeatInterval == m_repeatInterval);
}


//
// Create management pack record
//

void Threshold::createNXMPRecord(String &str)
{
   TCHAR szEvent1[MAX_EVENT_NAME], szEvent2[MAX_EVENT_NAME];
   String strValue;

   strValue = (TCHAR *)m_value.String();
   ResolveEventName(m_eventCode, szEvent1);
   ResolveEventName(m_rearmEventCode, szEvent2);
   str.AddFormattedString(_T("\t\t\t\t\t@THRESHOLD\n\t\t\t\t\t{\n")
                          _T("\t\t\t\t\t\tFUNCTION=%d;\n")
                          _T("\t\t\t\t\t\tCONDITION=%d;\n")
                          _T("\t\t\t\t\t\tVALUE=\"%s\";\n")
                          _T("\t\t\t\t\t\tACTIVATION_EVENT=\"%s\";\n")
                          _T("\t\t\t\t\t\tDEACTIVATION_EVENT=\"%s\";\n")
                          _T("\t\t\t\t\t\tPARAM1=%d;\n")
                          _T("\t\t\t\t\t\tPARAM2=%d;\n")
                          _T("\t\t\t\t\t\tREPEAT_INTERVAL=%d;\n")
                          _T("\t\t\t\t\t}\n"),
                          m_function, m_operation, (TCHAR *)strValue,
                          szEvent1, szEvent2, m_param1, m_param2,
								  m_repeatInterval);
}


//
// Made an association with DCI (used by management pack parser)
//

void Threshold::associate(DCItem *pItem)
{
   m_itemId = pItem->Id();
   m_dataType = pItem->DataType();
}
