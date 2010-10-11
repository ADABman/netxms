/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2010 Victor Kirhenshtein
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
** File: dcitem.cpp
**
**/

#include "nxcore.h"


//
// Get DCI object
// First argument is a node object (usually passed to script via $node variable),
// and second is DCI ID
//

static int F_GetDCIObject(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	NXSL_Object *object;
	Node *node;
	DCItem *dci;

	if (!argv[0]->isObject())
		return NXSL_ERR_NOT_OBJECT;

	if (!argv[1]->isInteger())
		return NXSL_ERR_NOT_INTEGER;

	object = argv[0]->getValueAsObject();
	if (_tcscmp(object->getClass()->getName(), g_nxslNodeClass.getName()))
		return NXSL_ERR_BAD_CLASS;

	node = (Node *)object->getData();
	dci = node->getItemById(argv[1]->getValueAsUInt32());
	if (dci != NULL)
	{
		*ppResult = new NXSL_Value(new NXSL_Object(&g_nxslDciClass, dci));
	}
	else
	{
		*ppResult = new NXSL_Value;	// Return NULL if DCI not found
	}

	return 0;
}


//
// Get DCI value from within transformation script
// First argument is a node object (passed to script via $node variable),
// and second is DCI ID
//

static int F_GetDCIValue(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	NXSL_Object *object;
	Node *node;
	DCItem *dci;

	if (!argv[0]->isObject())
		return NXSL_ERR_NOT_OBJECT;

	if (!argv[1]->isInteger())
		return NXSL_ERR_NOT_INTEGER;

	object = argv[0]->getValueAsObject();
	if (_tcscmp(object->getClass()->getName(), g_nxslNodeClass.getName()))
		return NXSL_ERR_BAD_CLASS;

	node = (Node *)object->getData();
	dci = node->getItemById(argv[1]->getValueAsUInt32());
	if (dci != NULL)
	{
		*ppResult = dci->getValueForNXSL(F_LAST, 1);
	}
	else
	{
		*ppResult = new NXSL_Value;	// Return NULL if DCI not found
	}

	return 0;
}


//
// Find DCI by name
//

static int F_FindDCIByName(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	NXSL_Object *object;
	Node *node;
	DCItem *dci;

	if (!argv[0]->isObject())
		return NXSL_ERR_NOT_OBJECT;

	if (!argv[1]->isString())
		return NXSL_ERR_NOT_STRING;

	object = argv[0]->getValueAsObject();
	if (_tcscmp(object->getClass()->getName(), g_nxslNodeClass.getName()))
		return NXSL_ERR_BAD_CLASS;

	node = (Node *)object->getData();
	dci = node->getItemByName(argv[1]->getValueAsCString());
	*ppResult = (dci != NULL) ? new NXSL_Value(dci->getId()) : new NXSL_Value((DWORD)0);
	return 0;
}


//
// Find DCI by description
//

static int F_FindDCIByDescription(int argc, NXSL_Value **argv, NXSL_Value **ppResult, NXSL_Program *program)
{
	NXSL_Object *object;
	Node *node;
	DCItem *dci;

	if (!argv[0]->isObject())
		return NXSL_ERR_NOT_OBJECT;

	if (!argv[1]->isString())
		return NXSL_ERR_NOT_STRING;

	object = argv[0]->getValueAsObject();
	if (_tcscmp(object->getClass()->getName(), g_nxslNodeClass.getName()))
		return NXSL_ERR_BAD_CLASS;

	node = (Node *)object->getData();
	dci = node->getItemByDescription(argv[1]->getValueAsCString());
	*ppResult = (dci != NULL) ? new NXSL_Value(dci->getId()) : new NXSL_Value((DWORD)0);
	return 0;
}


//
// Additional functions or use within transformation scripts
//

static NXSL_ExtFunction m_nxslDCIFunctions[] =
{
   { "FindDCIByName", F_FindDCIByName, 2 },
   { "FindDCIByDescription", F_FindDCIByDescription, 2 },
   { "GetDCIObject", F_GetDCIObject, 2 },
   { "GetDCIValue", F_GetDCIValue, 2 }
};

void RegisterDCIFunctions(NXSL_Environment *pEnv)
{
	pEnv->registerFunctionSet(sizeof(m_nxslDCIFunctions) / sizeof(NXSL_ExtFunction), m_nxslDCIFunctions);
}


//
// Default constructor for DCItem
//

DCItem::DCItem()
{
   m_dwId = 0;
   m_dwTemplateId = 0;
   m_dwTemplateItemId = 0;
   m_dwNumThresholds = 0;
   m_ppThresholdList = NULL;
   m_iBusy = 0;
   m_iDataType = DCI_DT_INT;
   m_iPollingInterval = 3600;
   m_iRetentionTime = 0;
   m_iDeltaCalculation = DCM_ORIGINAL_VALUE;
   m_iSource = DS_INTERNAL;
   m_iStatus = ITEM_STATUS_NOT_SUPPORTED;
   m_szName[0] = 0;
   m_szDescription[0] = 0;
   m_szInstance[0] = 0;
	m_systemTag[0] = 0;
   m_tLastPoll = 0;
   m_pszScript = NULL;
   m_pScript = NULL;
   m_pNode = NULL;
   m_hMutex = MutexCreateRecursive();
   m_dwCacheSize = 0;
   m_ppValueCache = NULL;
   m_tPrevValueTimeStamp = 0;
   m_bCacheLoaded = FALSE;
   m_iAdvSchedule = 0;
   m_dwNumSchedules = 0;
   m_ppScheduleList = NULL;
   m_tLastCheck = 0;
   m_iProcessAllThresholds = 0;
   m_dwErrorCount = 0;
	m_dwResourceId = 0;
	m_dwProxyNode = 0;
	m_nBaseUnits = DCI_BASEUNITS_OTHER;
	m_nMultiplier = 1;
	m_pszCustomUnitName = NULL;
	m_pszPerfTabSettings = NULL;
}


//
// Create DCItem from another DCItem
//

DCItem::DCItem(const DCItem *pSrc)
{
   DWORD i;

   m_dwId = pSrc->m_dwId;
   m_dwTemplateId = pSrc->m_dwTemplateId;
   m_dwTemplateItemId = pSrc->m_dwTemplateItemId;
   m_iBusy = 0;
   m_iDataType = pSrc->m_iDataType;
   m_iPollingInterval = pSrc->m_iPollingInterval;
   m_iRetentionTime = pSrc->m_iRetentionTime;
   m_iDeltaCalculation = pSrc->m_iDeltaCalculation;
   m_iSource = pSrc->m_iSource;
   m_iStatus = pSrc->m_iStatus;
   m_tLastPoll = 0;
	_tcscpy(m_szName, pSrc->m_szName);
	_tcscpy(m_szDescription, pSrc->m_szDescription);
	_tcscpy(m_szInstance, pSrc->m_szInstance);
	_tcscpy(m_systemTag, pSrc->m_systemTag);
   m_pszScript = NULL;
   m_pScript = NULL;
   setTransformationScript(pSrc->m_pszScript);
   m_pNode = NULL;
   m_hMutex = MutexCreateRecursive();
   m_dwCacheSize = 0;
   m_ppValueCache = NULL;
   m_tPrevValueTimeStamp = 0;
   m_bCacheLoaded = FALSE;
   m_tLastCheck = 0;
   m_dwErrorCount = 0;
   m_iAdvSchedule = pSrc->m_iAdvSchedule;
	m_dwResourceId = pSrc->m_dwResourceId;
	m_dwProxyNode = pSrc->m_dwProxyNode;
	m_nBaseUnits = pSrc->m_nBaseUnits;
	m_nMultiplier = pSrc->m_nMultiplier;
	m_pszCustomUnitName = (pSrc->m_pszCustomUnitName != NULL) ? _tcsdup(pSrc->m_pszCustomUnitName) : NULL;
	m_pszPerfTabSettings = (pSrc->m_pszPerfTabSettings != NULL) ? _tcsdup(pSrc->m_pszPerfTabSettings) : NULL;

   // Copy schedules
   m_dwNumSchedules = pSrc->m_dwNumSchedules;
   m_ppScheduleList = (TCHAR **)malloc(sizeof(TCHAR *) * m_dwNumSchedules);
   for(i = 0; i < m_dwNumSchedules; i++)
      m_ppScheduleList[i] = _tcsdup(pSrc->m_ppScheduleList[i]);

   // Copy thresholds
   m_dwNumThresholds = pSrc->m_dwNumThresholds;
   m_ppThresholdList = (Threshold **)malloc(sizeof(Threshold *) * m_dwNumThresholds);
   for(i = 0; i < m_dwNumThresholds; i++)
   {
      m_ppThresholdList[i] = new Threshold(pSrc->m_ppThresholdList[i]);
      m_ppThresholdList[i]->createId();
   }

   m_iProcessAllThresholds = pSrc->m_iProcessAllThresholds;
}


//
// Constructor for creating DCItem from database
// Assumes that fields in SELECT query are in following order:
// item_id,name,source,datatype,polling_interval,retention_time,status,
// delta_calculation,transformation,template_id,description,instance,
// template_item_id,adv_schedule,all_thresholds,resource_id,proxy_node,
// base_units,unit_multiplier,custom_units_name,perftab_settings,system_tag
//

DCItem::DCItem(DB_RESULT hResult, int iRow, Template *pNode)
{
   TCHAR *pszTmp, szQuery[256], szBuffer[MAX_DB_STRING];
   DB_RESULT hTempResult;
   DWORD i;

   m_dwId = DBGetFieldULong(hResult, iRow, 0);
   DBGetField(hResult, iRow, 1, m_szName, MAX_ITEM_NAME);
   DecodeSQLString(m_szName);
   m_iSource = (BYTE)DBGetFieldLong(hResult, iRow, 2);
   m_iDataType = (BYTE)DBGetFieldLong(hResult, iRow, 3);
   m_iPollingInterval = DBGetFieldLong(hResult, iRow, 4);
   m_iRetentionTime = DBGetFieldLong(hResult, iRow, 5);
   m_iStatus = (BYTE)DBGetFieldLong(hResult, iRow, 6);
   m_iDeltaCalculation = (BYTE)DBGetFieldLong(hResult, iRow, 7);
   m_pszScript = NULL;
   m_pScript = NULL;
   pszTmp = DBGetField(hResult, iRow, 8, NULL, 0);
   DecodeSQLString(pszTmp);
   setTransformationScript(pszTmp);
   free(pszTmp);
   m_dwTemplateId = DBGetFieldULong(hResult, iRow, 9);
   DBGetField(hResult, iRow, 10, m_szDescription, MAX_DB_STRING);
   DecodeSQLString(m_szDescription);
   DBGetField(hResult, iRow, 11, m_szInstance, MAX_DB_STRING);
   DecodeSQLString(m_szInstance);
   m_dwTemplateItemId = DBGetFieldULong(hResult, iRow, 12);
   m_iBusy = 0;
   m_tLastPoll = 0;
   m_dwNumThresholds = 0;
   m_ppThresholdList = NULL;
   m_pNode = pNode;
   m_hMutex = MutexCreateRecursive();
   m_dwCacheSize = 0;
   m_ppValueCache = NULL;
   m_tPrevValueTimeStamp = 0;
   m_bCacheLoaded = FALSE;
   m_tLastCheck = 0;
   m_dwErrorCount = 0;
   m_iAdvSchedule = (BYTE)DBGetFieldLong(hResult, iRow, 13);
   m_iProcessAllThresholds = (BYTE)DBGetFieldLong(hResult, iRow, 14);
	m_dwResourceId = DBGetFieldULong(hResult, iRow, 15);
	m_dwProxyNode = DBGetFieldULong(hResult, iRow, 16);
	m_nBaseUnits = DBGetFieldLong(hResult, iRow, 17);
	m_nMultiplier = DBGetFieldLong(hResult, iRow, 18);
	m_pszCustomUnitName = DBGetField(hResult, iRow, 19, NULL, 0);
	m_pszPerfTabSettings = DBGetField(hResult, iRow, 20, NULL, 0);
	DBGetField(hResult, iRow, 21, m_systemTag, MAX_DB_STRING);

   if (m_iAdvSchedule)
   {
      _sntprintf(szQuery, 256, _T("SELECT schedule FROM dci_schedules WHERE item_id=%d"), m_dwId);
      hTempResult = DBSelect(g_hCoreDB, szQuery);
      if (hTempResult != NULL)
      {
         m_dwNumSchedules = DBGetNumRows(hTempResult);
         m_ppScheduleList = (TCHAR **)malloc(sizeof(TCHAR *) * m_dwNumSchedules);
         for(i = 0; i < m_dwNumSchedules; i++)
         {
            m_ppScheduleList[i] = DBGetField(hTempResult, i, 0, NULL, 0);
            DecodeSQLString(m_ppScheduleList[i]);
         }
         DBFreeResult(hTempResult);
      }
      else
      {
         m_dwNumSchedules = 0;
         m_ppScheduleList = NULL;
      }
   }
   else
   {
      m_dwNumSchedules = 0;
      m_ppScheduleList = NULL;
   }

   // Load last raw value from database
   _sntprintf(szQuery, 256, _T("SELECT raw_value,last_poll_time FROM raw_dci_values WHERE item_id=%d"), m_dwId);
   hTempResult = DBSelect(g_hCoreDB, szQuery);
   if (hTempResult != NULL)
   {
      if (DBGetNumRows(hTempResult) > 0)
      {
         m_prevRawValue = DBGetField(hTempResult, 0, 0, szBuffer, MAX_DB_STRING);
         m_tPrevValueTimeStamp = DBGetFieldULong(hTempResult, 0, 1);
         m_tLastPoll = m_tPrevValueTimeStamp;
      }
      DBFreeResult(hTempResult);
   }
}


//
// Constructor for creating new DCItem from scratch
//

DCItem::DCItem(DWORD dwId, const TCHAR *szName, int iSource, int iDataType, 
               int iPollingInterval, int iRetentionTime, Template *pNode,
               const TCHAR *pszDescription, const TCHAR *systemTag)
{
   m_dwId = dwId;
   m_dwTemplateId = 0;
   m_dwTemplateItemId = 0;
   nx_strncpy(m_szName, szName, MAX_ITEM_NAME);
   if (pszDescription != NULL)
      nx_strncpy(m_szDescription, pszDescription, MAX_DB_STRING);
   else
      strcpy(m_szDescription, m_szName);
   m_szInstance[0] = 0;
	nx_strncpy(m_systemTag, CHECK_NULL_EX(systemTag), MAX_DB_STRING);
   m_iSource = iSource;
   m_iDataType = iDataType;
   m_iPollingInterval = iPollingInterval;
   m_iRetentionTime = iRetentionTime;
   m_iDeltaCalculation = DCM_ORIGINAL_VALUE;
   m_iStatus = ITEM_STATUS_ACTIVE;
   m_iBusy = 0;
   m_iProcessAllThresholds = 0;
   m_tLastPoll = 0;
   m_pszScript = NULL;
   m_pScript = NULL;
   m_dwNumThresholds = 0;
   m_ppThresholdList = NULL;
   m_pNode = pNode;
   m_hMutex = MutexCreateRecursive();
   m_dwCacheSize = 0;
   m_ppValueCache = NULL;
   m_tPrevValueTimeStamp = 0;
   m_bCacheLoaded = FALSE;
   m_iAdvSchedule = 0;
   m_dwNumSchedules = 0;
   m_ppScheduleList = NULL;
   m_tLastCheck = 0;
   m_dwErrorCount = 0;
	m_dwResourceId = 0;
	m_dwProxyNode = 0;
	m_nBaseUnits = DCI_BASEUNITS_OTHER;
	m_nMultiplier = 1;
	m_pszCustomUnitName = NULL;
	m_pszPerfTabSettings = NULL;

   updateCacheSize();
}


//
// Create DCItem from import file
//

DCItem::DCItem(ConfigEntry *config, Template *owner)
{
   m_dwId = CreateUniqueId(IDG_ITEM);
   m_dwTemplateId = 0;
   m_dwTemplateItemId = 0;
	nx_strncpy(m_szName, config->getSubEntryValue(_T("name"), 0, _T("unnamed")), MAX_ITEM_NAME);
   nx_strncpy(m_szDescription, config->getSubEntryValue(_T("description"), 0, m_szName), MAX_DB_STRING);
   nx_strncpy(m_szInstance, config->getSubEntryValue(_T("instance"), 0, _T("")), MAX_DB_STRING);
	nx_strncpy(m_systemTag, config->getSubEntryValue(_T("systemTag"), 0, _T("")), MAX_DB_STRING);
	m_iSource = (BYTE)config->getSubEntryValueInt(_T("origin"));
   m_iDataType = (BYTE)config->getSubEntryValueInt(_T("dataType"));
   m_iPollingInterval = config->getSubEntryValueInt(_T("interval"));
   m_iRetentionTime = config->getSubEntryValueInt(_T("retention"));
   m_iDeltaCalculation = (BYTE)config->getSubEntryValueInt(_T("delta"));
   m_iStatus = ITEM_STATUS_ACTIVE;
   m_iBusy = 0;
   m_iProcessAllThresholds = (BYTE)config->getSubEntryValueInt(_T("allThresholds"));
   m_tLastPoll = 0;
   m_pNode = owner;
   m_hMutex = MutexCreateRecursive();
   m_dwCacheSize = 0;
   m_ppValueCache = NULL;
   m_tPrevValueTimeStamp = 0;
   m_bCacheLoaded = FALSE;
   m_tLastCheck = 0;
   m_dwErrorCount = 0;
	m_dwResourceId = 0;
	m_dwProxyNode = 0;
	m_nBaseUnits = DCI_BASEUNITS_OTHER;
	m_nMultiplier = 1;
	m_pszCustomUnitName = NULL;
	m_pszPerfTabSettings = NULL;
   
	m_iAdvSchedule = (BYTE)config->getSubEntryValueInt(_T("advancedSchedule"));
	ConfigEntry *schedules = config->findEntry(_T("schedules"));
	if (schedules != NULL)
		schedules = schedules->findEntry(_T("schedule"));
	if (schedules != NULL)
	{
		m_dwNumSchedules = (DWORD)schedules->getValueCount();
		m_ppScheduleList = (TCHAR **)malloc(sizeof(TCHAR *) * m_dwNumSchedules);
		for(int i = 0; i < (int)m_dwNumSchedules; i++)
		{
			m_ppScheduleList[i] = _tcsdup(schedules->getValue(i));
		}
	}
	else
	{
		m_dwNumSchedules = 0;
		m_ppScheduleList = NULL;
	}
   
	m_pszScript = NULL;
	m_pScript = NULL;
	setTransformationScript(config->getSubEntryValue(_T("transformation")));
   
	ConfigEntry *thresholdsRoot = config->findEntry(_T("thresholds"));
	if (thresholdsRoot != NULL)
	{
		ConfigEntryList *thresholds = thresholdsRoot->getSubEntries(_T("threshold#*"));
		m_dwNumThresholds = (DWORD)thresholds->getSize();
		m_ppThresholdList = (Threshold **)malloc(sizeof(Threshold *) * m_dwNumThresholds);
		for(int i = 0; i < thresholds->getSize(); i++)
		{
			m_ppThresholdList[i] = new Threshold(thresholds->getEntry(i), this);
		}
		delete thresholds;
	}
	else
	{
		m_dwNumThresholds = 0;
		m_ppThresholdList = NULL;
	}

   updateCacheSize();
}


//
// Destructor for DCItem
//

DCItem::~DCItem()
{
   DWORD i;

   for(i = 0; i < m_dwNumThresholds; i++)
      delete m_ppThresholdList[i];
   safe_free(m_ppThresholdList);
   for(i = 0; i < m_dwNumSchedules; i++)
      free(m_ppScheduleList[i]);
   safe_free(m_ppScheduleList);
   safe_free(m_pszScript);
   delete m_pScript;
	safe_free(m_pszCustomUnitName);
	safe_free(m_pszPerfTabSettings);
   clearCache();
   MutexDestroy(m_hMutex);
}


//
// Delete all thresholds
//

void DCItem::deleteAllThresholds()
{
	lock();
   for(DWORD i = 0; i < m_dwNumThresholds; i++)
      delete m_ppThresholdList[i];
   safe_free_and_null(m_ppThresholdList);
	m_dwNumThresholds = 0;
	unlock();
}


//
// Clear data cache
//

void DCItem::clearCache(void)
{
   DWORD i;

   for(i = 0; i < m_dwCacheSize; i++)
      delete m_ppValueCache[i];
   safe_free(m_ppValueCache);
   m_ppValueCache = NULL;
   m_dwCacheSize = 0;
}


//
// Load data collection items thresholds from database
//

BOOL DCItem::loadThresholdsFromDB(void)
{
   DWORD i;
   char szQuery[256];
   DB_RESULT hResult;
   BOOL bResult = FALSE;

   sprintf(szQuery, "SELECT threshold_id,fire_value,rearm_value,check_function,"
                    "check_operation,parameter_1,parameter_2,event_code,current_state,"
                    "rearm_event_code,repeat_interval FROM thresholds WHERE item_id=%d "
                    "ORDER BY sequence_number", m_dwId);
   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult != NULL)
   {
      m_dwNumThresholds = DBGetNumRows(hResult);
      if (m_dwNumThresholds > 0)
      {
         m_ppThresholdList = (Threshold **)malloc(sizeof(Threshold *) * m_dwNumThresholds);
         for(i = 0; i < m_dwNumThresholds; i++)
            m_ppThresholdList[i] = new Threshold(hResult, i, this);
      }
      DBFreeResult(hResult);
      bResult = TRUE;
   }

   //updateCacheSize();

   return bResult;
}


//
// Save to database
//

BOOL DCItem::saveToDB(DB_HANDLE hdb)
{
   TCHAR *pszEscName, *pszEscScript, *pszEscDescr, *pszEscInstance;
	TCHAR *pszQuery;
   DB_RESULT hResult;
   BOOL bNewObject = TRUE, bResult;

   lock();

   pszEscName = EncodeSQLString(m_szName);
   pszEscScript = EncodeSQLString(CHECK_NULL_EX(m_pszScript));
   pszEscDescr = EncodeSQLString(m_szDescription);
   pszEscInstance = EncodeSQLString(m_szInstance);
	String escCustomUnitName = DBPrepareString(g_hCoreDB, m_pszCustomUnitName);
	String escPerfTabSettings = DBPrepareString(g_hCoreDB, m_pszPerfTabSettings);
	pszQuery = (TCHAR *)malloc(sizeof(TCHAR) * (_tcslen(pszEscScript) + escPerfTabSettings.getSize() + 2048));

   // Check for object's existence in database
   sprintf(pszQuery, "SELECT item_id FROM items WHERE item_id=%d", m_dwId);
   hResult = DBSelect(hdb, pszQuery);
   if (hResult != 0)
   {
      if (DBGetNumRows(hResult) > 0)
         bNewObject = FALSE;
      DBFreeResult(hResult);
   }

   // Prepare and execute query
   if (bNewObject)
      sprintf(pszQuery, "INSERT INTO items (item_id,node_id,template_id,name,description,source,"
                        "datatype,polling_interval,retention_time,status,delta_calculation,"
                        "transformation,instance,template_item_id,adv_schedule,"
                        "all_thresholds,resource_id,proxy_node,base_units,unit_multiplier,"
								"custom_units_name,perftab_settings,system_tag) VALUES "
						 	   "(%d,%d,%d,'%s','%s',%d,%d,%d,%d,%d,%d,'%s','%s',%d,%d,%d,%d,%d,%d,%d,%s,%s,%s)",
                        m_dwId, (m_pNode == NULL) ? (DWORD)0 : m_pNode->Id(), m_dwTemplateId,
                        pszEscName, pszEscDescr, m_iSource, m_iDataType, m_iPollingInterval,
                        m_iRetentionTime, m_iStatus, m_iDeltaCalculation,
                        pszEscScript, pszEscInstance, m_dwTemplateItemId,
                        m_iAdvSchedule, m_iProcessAllThresholds, m_dwResourceId,
							   m_dwProxyNode, m_nBaseUnits, m_nMultiplier, (const TCHAR *)escCustomUnitName,
								(const TCHAR *)escPerfTabSettings, (const TCHAR *)DBPrepareString(g_hCoreDB, m_systemTag));
   else
      sprintf(pszQuery, "UPDATE items SET node_id=%d,template_id=%d,name='%s',source=%d,"
                        "datatype=%d,polling_interval=%d,retention_time=%d,status=%d,"
                        "delta_calculation=%d,transformation='%s',description='%s',"
                        "instance='%s',template_item_id=%d,adv_schedule=%d,"
                        "all_thresholds=%d,resource_id=%d,proxy_node=%d,base_units=%d,"
								"unit_multiplier=%d,custom_units_name=%s,perftab_settings=%s,"
	                     "system_tag=%s WHERE item_id=%d",
                        (m_pNode == NULL) ? 0 : m_pNode->Id(), m_dwTemplateId,
                        pszEscName, m_iSource, m_iDataType, m_iPollingInterval,
                        m_iRetentionTime, m_iStatus, m_iDeltaCalculation, pszEscScript,
                        pszEscDescr, pszEscInstance, m_dwTemplateItemId,
                        m_iAdvSchedule, m_iProcessAllThresholds, m_dwResourceId,
							   m_dwProxyNode, m_nBaseUnits, m_nMultiplier, (const TCHAR *)escCustomUnitName,
								(const TCHAR *)escPerfTabSettings, (const TCHAR *)DBPrepareString(g_hCoreDB, m_systemTag), m_dwId);
   bResult = DBQuery(hdb, pszQuery);
   free(pszEscName);
   free(pszEscScript);
   free(pszEscDescr);
   free(pszEscInstance);

   // Save thresholds
   if (bResult)
   {
      DWORD i;

      for(i = 0; i < m_dwNumThresholds; i++)
         m_ppThresholdList[i]->saveToDB(hdb, i);
   }

   // Delete non-existing thresholds
   sprintf(pszQuery, "SELECT threshold_id FROM thresholds WHERE item_id=%d", m_dwId);
   hResult = DBSelect(hdb, pszQuery);
   if (hResult != NULL)
   {
      int i, iNumRows;
      DWORD j, dwId;

      iNumRows = DBGetNumRows(hResult);
      for(i = 0; i < iNumRows; i++)
      {
         dwId = DBGetFieldULong(hResult, i, 0);
         for(j = 0; j < m_dwNumThresholds; j++)
            if (m_ppThresholdList[j]->getId() == dwId)
               break;
         if (j == m_dwNumThresholds)
         {
            sprintf(pszQuery, "DELETE FROM thresholds WHERE threshold_id=%d", dwId);
            DBQuery(hdb, pszQuery);
         }
      }
      DBFreeResult(hResult);
   }

   // Create record in raw_dci_values if needed
   sprintf(pszQuery, "SELECT item_id FROM raw_dci_values WHERE item_id=%d", m_dwId);
   hResult = DBSelect(hdb, pszQuery);
   if (hResult != NULL)
   {
      if (DBGetNumRows(hResult) == 0)
      {
         _sntprintf(pszQuery, 1024, _T("INSERT INTO raw_dci_values (item_id,raw_value,last_poll_time) VALUES (%d,%s,%ld)"),
                 m_dwId, (const TCHAR *)DBPrepareString(hdb, m_prevRawValue.String()), (long)m_tPrevValueTimeStamp);
         DBQuery(hdb, pszQuery);
      }
      DBFreeResult(hResult);
   }

   // Save schedules
   sprintf(pszQuery, "DELETE FROM dci_schedules WHERE item_id=%d", m_dwId);
   DBQuery(hdb, pszQuery);
   if (m_iAdvSchedule)
   {
      TCHAR *pszEscSchedule;
      DWORD i;

      for(i = 0; i < m_dwNumSchedules; i++)
      {
         pszEscSchedule = EncodeSQLString(m_ppScheduleList[i]);
         sprintf(pszQuery, "INSERT INTO dci_schedules (item_id,schedule_id,schedule) VALUES (%d,%d,'%s')",
                 m_dwId, i + 1, pszEscSchedule);
         free(pszEscSchedule);
         DBQuery(hdb, pszQuery);
      }
   }

   unlock();
   free(pszQuery);
   return bResult;
}


//
// Check last value for threshold breaches
//

void DCItem::checkThresholds(ItemValue &value)
{
   DWORD i, iResult, dwInterval;
   ItemValue checkValue;
	time_t now = time(NULL);

   for(i = 0; i < m_dwNumThresholds; i++)
   {
      iResult = m_ppThresholdList[i]->check(value, m_ppValueCache, checkValue);
      switch(iResult)
      {
         case THRESHOLD_REACHED:
            PostEvent(m_ppThresholdList[i]->getEventCode(), m_pNode->Id(), "ssssisd", m_szName,
                      m_szDescription, m_ppThresholdList[i]->getStringValue(), 
                      (const char *)checkValue, m_dwId, m_szInstance, 0);
				m_ppThresholdList[i]->setLastEventTimestamp();
            if (!m_iProcessAllThresholds)
               i = m_dwNumThresholds;  // Stop processing
            break;
         case THRESHOLD_REARMED:
            PostEvent(m_ppThresholdList[i]->getRearmEventCode(), m_pNode->Id(), "ssis", m_szName, 
                      m_szDescription, m_dwId, m_szInstance);
            break;
         case NO_ACTION:
            if (m_ppThresholdList[i]->isReached())
				{
					// Check if we need to re-sent threshold violation event
					if (m_ppThresholdList[i]->getRepeatInterval() == -1)
						dwInterval = g_dwThresholdRepeatInterval;
					else
						dwInterval = (DWORD)m_ppThresholdList[i]->getRepeatInterval();
					if ((dwInterval != 0) && (m_ppThresholdList[i]->getLastEventTimestamp() + (time_t)dwInterval < now))
					{
						PostEvent(m_ppThresholdList[i]->getEventCode(), m_pNode->Id(), "ssssisd", m_szName,
									 m_szDescription, m_ppThresholdList[i]->getStringValue(), 
									 (const char *)checkValue, m_dwId, m_szInstance, 1);
						m_ppThresholdList[i]->setLastEventTimestamp();
					}

               if (!m_iProcessAllThresholds)
					{
						i = m_dwNumThresholds;  // Threshold condition still true, stop processing
					}
				}
            break;
      }
   }
}


//
// Create CSCP message with item data
//

void DCItem::createMessage(CSCPMessage *pMsg)
{
   DWORD i, dwId;

   lock();
   pMsg->SetVariable(VID_DCI_ID, m_dwId);
   pMsg->SetVariable(VID_TEMPLATE_ID, m_dwTemplateId);
   pMsg->SetVariable(VID_NAME, m_szName);
   pMsg->SetVariable(VID_DESCRIPTION, m_szDescription);
   pMsg->SetVariable(VID_INSTANCE, m_szInstance);
   pMsg->SetVariable(VID_SYSTEM_TAG, m_systemTag);
   pMsg->SetVariable(VID_POLLING_INTERVAL, (DWORD)m_iPollingInterval);
   pMsg->SetVariable(VID_RETENTION_TIME, (DWORD)m_iRetentionTime);
   pMsg->SetVariable(VID_DCI_SOURCE_TYPE, (WORD)m_iSource);
   pMsg->SetVariable(VID_DCI_DATA_TYPE, (WORD)m_iDataType);
   pMsg->SetVariable(VID_DCI_STATUS, (WORD)m_iStatus);
   pMsg->SetVariable(VID_DCI_DELTA_CALCULATION, (WORD)m_iDeltaCalculation);
   pMsg->SetVariable(VID_DCI_FORMULA, CHECK_NULL_EX(m_pszScript));
   pMsg->SetVariable(VID_ALL_THRESHOLDS, (WORD)m_iProcessAllThresholds);
	pMsg->SetVariable(VID_RESOURCE_ID, m_dwResourceId);
	pMsg->SetVariable(VID_PROXY_NODE, m_dwProxyNode);
	pMsg->SetVariable(VID_BASE_UNITS, (WORD)m_nBaseUnits);
	pMsg->SetVariable(VID_MULTIPLIER, (DWORD)m_nMultiplier);
	if (m_pszCustomUnitName != NULL)
		pMsg->SetVariable(VID_CUSTOM_UNITS_NAME, m_pszCustomUnitName);
	if (m_pszPerfTabSettings != NULL)
		pMsg->SetVariable(VID_PERFTAB_SETTINGS, m_pszPerfTabSettings);
   pMsg->SetVariable(VID_NUM_THRESHOLDS, m_dwNumThresholds);
   for(i = 0, dwId = VID_DCI_THRESHOLD_BASE; i < m_dwNumThresholds; i++, dwId += 10)
      m_ppThresholdList[i]->createMessage(pMsg, dwId);
   pMsg->SetVariable(VID_ADV_SCHEDULE, (WORD)m_iAdvSchedule);
   if (m_iAdvSchedule)
   {
      pMsg->SetVariable(VID_NUM_SCHEDULES, m_dwNumSchedules);
      for(i = 0, dwId = VID_DCI_SCHEDULE_BASE; i < m_dwNumSchedules; i++, dwId++)
         pMsg->SetVariable(dwId, m_ppScheduleList[i]);
   }
   unlock();
}


//
// Delete item and collected data from database
//

void DCItem::deleteFromDB(void)
{
   char szQuery[256];

   sprintf(szQuery, "DELETE FROM items WHERE item_id=%d", m_dwId);
   QueueSQLRequest(szQuery);
   sprintf(szQuery, "DELETE FROM idata_%d WHERE item_id=%d", m_pNode->Id(), m_dwId);
   QueueSQLRequest(szQuery);
   sprintf(szQuery, "DELETE FROM thresholds WHERE item_id=%d", m_dwId);
   QueueSQLRequest(szQuery);
   sprintf(szQuery, "DELETE FROM dci_schedules WHERE item_id=%d", m_dwId);
   QueueSQLRequest(szQuery);
}


//
// Update item from CSCP message
//

void DCItem::updateFromMessage(CSCPMessage *pMsg, DWORD *pdwNumMaps, 
                               DWORD **ppdwMapIndex, DWORD **ppdwMapId)
{
   DWORD i, j, dwNum, dwId;
   Threshold **ppNewList;
   TCHAR *pszStr;

   lock();

   pMsg->GetVariableStr(VID_NAME, m_szName, MAX_ITEM_NAME);
   pMsg->GetVariableStr(VID_DESCRIPTION, m_szDescription, MAX_DB_STRING);
   pMsg->GetVariableStr(VID_INSTANCE, m_szInstance, MAX_DB_STRING);
   pMsg->GetVariableStr(VID_SYSTEM_TAG, m_systemTag, MAX_DB_STRING);
   m_iSource = (BYTE)pMsg->GetVariableShort(VID_DCI_SOURCE_TYPE);
   m_iDataType = (BYTE)pMsg->GetVariableShort(VID_DCI_DATA_TYPE);
   m_iPollingInterval = pMsg->GetVariableLong(VID_POLLING_INTERVAL);
   m_iRetentionTime = pMsg->GetVariableLong(VID_RETENTION_TIME);
   setStatus(pMsg->GetVariableShort(VID_DCI_STATUS), true);
   m_iDeltaCalculation = (BYTE)pMsg->GetVariableShort(VID_DCI_DELTA_CALCULATION);
   m_iProcessAllThresholds = (BYTE)pMsg->GetVariableShort(VID_ALL_THRESHOLDS);
	m_dwResourceId = pMsg->GetVariableLong(VID_RESOURCE_ID);
	m_dwProxyNode = pMsg->GetVariableLong(VID_PROXY_NODE);
   pszStr = pMsg->GetVariableStr(VID_DCI_FORMULA);
   setTransformationScript(pszStr);
   free(pszStr);
	m_nBaseUnits = pMsg->GetVariableShort(VID_BASE_UNITS);
	m_nMultiplier = (int)pMsg->GetVariableLong(VID_MULTIPLIER);
	safe_free(m_pszCustomUnitName);
	m_pszCustomUnitName = pMsg->GetVariableStr(VID_CUSTOM_UNITS_NAME);
	safe_free(m_pszPerfTabSettings);
	m_pszPerfTabSettings = pMsg->GetVariableStr(VID_PERFTAB_SETTINGS);

   // Update schedules
   for(i = 0; i < m_dwNumSchedules; i++)
      free(m_ppScheduleList[i]);
   m_iAdvSchedule = (BYTE)pMsg->GetVariableShort(VID_ADV_SCHEDULE);
   if (m_iAdvSchedule)
   {
      m_dwNumSchedules = pMsg->GetVariableLong(VID_NUM_SCHEDULES);
      m_ppScheduleList = (TCHAR **)realloc(m_ppScheduleList, sizeof(TCHAR *) * m_dwNumSchedules);
      for(i = 0, dwId = VID_DCI_SCHEDULE_BASE; i < m_dwNumSchedules; i++, dwId++)
      {
         pszStr = pMsg->GetVariableStr(dwId);
         if (pszStr != NULL)
         {
            m_ppScheduleList[i] = pszStr;
         }
         else
         {
            m_ppScheduleList[i] = _tcsdup(_T("(null)"));
         }
      }
   }
   else
   {
      if (m_ppScheduleList != NULL)
      {
         free(m_ppScheduleList);
         m_ppScheduleList = NULL;
      }
      m_dwNumSchedules = 0;
   }

   // Update thresholds
   dwNum = pMsg->GetVariableLong(VID_NUM_THRESHOLDS);
   DWORD *newThresholds = (DWORD *)malloc(sizeof(DWORD) * dwNum);
   *ppdwMapIndex = (DWORD *)malloc(dwNum * sizeof(DWORD));
   *ppdwMapId = (DWORD *)malloc(dwNum * sizeof(DWORD));
   *pdwNumMaps = 0;

   // Read all new threshold ids from message
   for(i = 0, dwId = VID_DCI_THRESHOLD_BASE; i < dwNum; i++, dwId += 10)
   {
      newThresholds[i] = pMsg->GetVariableLong(dwId);
   }
   
   // Check if some thresholds was deleted, and reposition others if needed
   ppNewList = (Threshold **)malloc(sizeof(Threshold *) * dwNum);
   for(i = 0; i < m_dwNumThresholds; i++)
   {
      for(j = 0; j < dwNum; j++)
         if (m_ppThresholdList[i]->getId() == newThresholds[j])
            break;
      if (j == dwNum)
      {
         // No threshold with that id in new list, delete it
         delete m_ppThresholdList[i];
         m_dwNumThresholds--;
         memmove(&m_ppThresholdList[i], &m_ppThresholdList[i + 1], sizeof(Threshold *) * (m_dwNumThresholds - i));
         i--;
      }
      else
      {
         // Move existing thresholds to appropriate positions in new list
         ppNewList[j] = m_ppThresholdList[i];
      }
   }
   safe_free(m_ppThresholdList);
   m_ppThresholdList = ppNewList;
   m_dwNumThresholds = dwNum;

   // Add or update thresholds
   for(i = 0, dwId = VID_DCI_THRESHOLD_BASE; i < dwNum; i++, dwId += 10)
   {
      if (newThresholds[i] == 0)    // New threshold?
      {
         m_ppThresholdList[i] = new Threshold(this);
         m_ppThresholdList[i]->createId();

         // Add index -> id mapping
         (*ppdwMapIndex)[*pdwNumMaps] = i;
         (*ppdwMapId)[*pdwNumMaps] = m_ppThresholdList[i]->getId();
         (*pdwNumMaps)++;
      }
      m_ppThresholdList[i]->updateFromMessage(pMsg, dwId);
   }
      
   safe_free(newThresholds);
   updateCacheSize();
   unlock();
}


//
// Process new value
//

void DCItem::processNewValue(time_t tmTimeStamp, const TCHAR *pszOriginalValue)
{
   TCHAR szQuery[MAX_LINE_SIZE + 128];
   ItemValue rawValue, *pValue;

   lock();

   // Normally m_pNode shouldn't be NULL for polled items, but who knows...
   if (m_pNode == NULL)
   {
      unlock();
      return;
   }

   m_dwErrorCount = 0;

   // Save raw value into database
   _sntprintf(szQuery, MAX_LINE_SIZE + 128, _T("UPDATE raw_dci_values SET raw_value=%s,last_poll_time=%ld WHERE item_id=%d"),
              (const TCHAR *)DBPrepareString(g_hCoreDB, pszOriginalValue), (long)tmTimeStamp, m_dwId);
   QueueSQLRequest(szQuery);

   // Create new ItemValue object and transform it as needed
   pValue = new ItemValue(pszOriginalValue, (DWORD)tmTimeStamp);
   if (m_tPrevValueTimeStamp == 0)
      m_prevRawValue = *pValue;  // Delta should be zero for first poll
   rawValue = *pValue;
   transform(*pValue, tmTimeStamp - m_tPrevValueTimeStamp);
   m_prevRawValue = rawValue;
   m_tPrevValueTimeStamp = tmTimeStamp;

   // Save transformed value to database
   _sntprintf(szQuery, MAX_LINE_SIZE + 128, _T("INSERT INTO idata_%d (item_id,idata_timestamp,idata_value) VALUES (%d,%ld,%s)"),
	           m_pNode->Id(), m_dwId, (long)tmTimeStamp, (const TCHAR *)DBPrepareString(g_hCoreDB, pValue->String()));
   QueueSQLRequest(szQuery);

   // Check thresholds and add value to cache
   checkThresholds(*pValue);

   if (m_dwCacheSize > 0)
   {
      delete m_ppValueCache[m_dwCacheSize - 1];
      memmove(&m_ppValueCache[1], m_ppValueCache, sizeof(ItemValue *) * (m_dwCacheSize - 1));
      m_ppValueCache[0] = pValue;
   }
   else
   {
      delete pValue;
   }

   unlock();
}


//
// Process new data collection error
//

void DCItem::processNewError(void)
{
   DWORD i, iResult;

   lock();

   // Normally m_pNode shouldn't be NULL for polled items, but who knows...
   if (m_pNode == NULL)
   {
      unlock();
      return;
   }

   m_dwErrorCount++;

   for(i = 0; i < m_dwNumThresholds; i++)
   {
      iResult = m_ppThresholdList[i]->checkError(m_dwErrorCount);
      switch(iResult)
      {
         case THRESHOLD_REACHED:
            PostEvent(m_ppThresholdList[i]->getEventCode(), m_pNode->Id(), "ssssis", m_szName,
                      m_szDescription, _T(""), _T(""), m_dwId, m_szInstance);
            if (!m_iProcessAllThresholds)
               i = m_dwNumThresholds;  // Stop processing
            break;
         case THRESHOLD_REARMED:
            PostEvent(m_ppThresholdList[i]->getRearmEventCode(), m_pNode->Id(), "ssis", m_szName,
                      m_szDescription, m_dwId, m_szInstance);
            break;
         case NO_ACTION:
            if ((m_ppThresholdList[i]->isReached()) &&
                (!m_iProcessAllThresholds))
               i = m_dwNumThresholds;  // Threshold condition still true, stop processing
            break;
      }
   }

   unlock();
}


//
// Transform received value
//

void DCItem::transform(ItemValue &value, time_t nElapsedTime)
{
   switch(m_iDeltaCalculation)
   {
      case DCM_SIMPLE:
         switch(m_iDataType)
         {
            case DCI_DT_INT:
               value = (LONG)value - (LONG)m_prevRawValue;
               break;
            case DCI_DT_UINT:
               value = (DWORD)value - (DWORD)m_prevRawValue;
               break;
            case DCI_DT_INT64:
               value = (INT64)value - (INT64)m_prevRawValue;
               break;
            case DCI_DT_UINT64:
               value = (QWORD)value - (QWORD)m_prevRawValue;
               break;
            case DCI_DT_FLOAT:
               value = (double)value - (double)m_prevRawValue;
               break;
            case DCI_DT_STRING:
               value = (LONG)((_tcscmp((const TCHAR *)value, (const TCHAR *)m_prevRawValue) == 0) ? 0 : 1);
               break;
            default:
               // Delta calculation is not supported for other types
               break;
         }
         break;
      case DCM_AVERAGE_PER_MINUTE:
         nElapsedTime /= 60;  // Convert to minutes
      case DCM_AVERAGE_PER_SECOND:
         // Check elapsed time to prevent divide-by-zero exception
         if (nElapsedTime == 0)
            nElapsedTime++;

         switch(m_iDataType)
         {
            case DCI_DT_INT:
               value = ((LONG)value - (LONG)m_prevRawValue) / (LONG)nElapsedTime;
               break;
            case DCI_DT_UINT:
               value = ((DWORD)value - (DWORD)m_prevRawValue) / (DWORD)nElapsedTime;
               break;
            case DCI_DT_INT64:
               value = ((INT64)value - (INT64)m_prevRawValue) / (INT64)nElapsedTime;
               break;
            case DCI_DT_UINT64:
               value = ((QWORD)value - (QWORD)m_prevRawValue) / (QWORD)nElapsedTime;
               break;
            case DCI_DT_FLOAT:
               value = ((double)value - (double)m_prevRawValue) / (double)nElapsedTime;
               break;
            case DCI_DT_STRING:
               // I don't see any meaning in "average delta per second (minute)" for string
               // values, so result will be 0 if there are no difference between
               // current and previous values, and 1 otherwise
               value = (LONG)((strcmp((const TCHAR *)value, (const TCHAR *)m_prevRawValue) == 0) ? 0 : 1);
               break;
            default:
               // Delta calculation is not supported for other types
               break;
         }
         break;
      default:    // Default is no transformation
         break;
   }

   if (m_pScript != NULL)
   {
      NXSL_Value *pValue;
      NXSL_ServerEnv *pEnv;

      pValue = new NXSL_Value((char *)((const char *)value));
      pEnv = new NXSL_ServerEnv;
      m_pScript->setGlobalVariable(_T("$node"), new NXSL_Value(new NXSL_Object(&g_nxslNodeClass, m_pNode)));
	
      if (m_pScript->run(pEnv, 1, &pValue) == 0)
      {
         pValue = m_pScript->getResult();
         if (pValue != NULL)
         {
            switch(m_iDataType)
            {
               case DCI_DT_INT:
                  value = pValue->getValueAsInt32();
                  break;
               case DCI_DT_UINT:
                  value = pValue->getValueAsUInt32();
                  break;
               case DCI_DT_INT64:
                  value = pValue->getValueAsInt64();
                  break;
               case DCI_DT_UINT64:
                  value = pValue->getValueAsUInt64();
                  break;
               case DCI_DT_FLOAT:
                  value = pValue->getValueAsReal();
                  break;
               case DCI_DT_STRING:
                  value = CHECK_NULL_EX(pValue->getValueAsCString());
                  break;
               default:
                  break;
            }
         }
      }
      else
      {
         TCHAR szBuffer[1024];

         _sntprintf(szBuffer, 1024, _T("DCI::%s::%d"),
                    (m_pNode != NULL) ? m_pNode->Name() : _T("(null)"), m_dwId);
         PostEvent(EVENT_SCRIPT_ERROR, g_dwMgmtNode, _T("ssd"), szBuffer,
                   m_pScript->getErrorText(), m_dwId);
      }
   }
}


//
// Set new ID and node/template association
//

void DCItem::changeBinding(DWORD dwNewId, Template *pNewNode, BOOL doMacroExpansion)
{
   DWORD i;

   lock();
   m_pNode = pNewNode;
	if (dwNewId != 0)
	{
		m_dwId = dwNewId;
		for(i = 0; i < m_dwNumThresholds; i++)
			m_ppThresholdList[i]->bindToItem(m_dwId);
	}

	if (doMacroExpansion)
	{
		expandMacros(m_szName, m_szName, MAX_ITEM_NAME);
		expandMacros(m_szDescription, m_szDescription, MAX_DB_STRING);
		expandMacros(m_szInstance, m_szInstance, MAX_DB_STRING);
	}

   clearCache();
   updateCacheSize();
   unlock();
}


//
// Update required cache size depending on thresholds
// dwCondId is an identifier of calling condition object id. If it is not 0,
// GetCacheSizeForDCI should be called with bNoLock == TRUE for appropriate
// condition object
//

void DCItem::updateCacheSize(DWORD dwCondId)
{
   DWORD i, dwSize, dwRequiredSize;

   // Sanity check
   if (m_pNode == NULL)
   {
      DbgPrintf(3, _T("DCItem::updateCacheSize() called for DCI %d when m_pNode == NULL"), m_dwId);
      return;
   }

   // Minimum cache size is 1 for nodes (so GetLastValue can work)
   // and it is always 0 for templates
   if (m_pNode->Type() == OBJECT_NODE)
   {
      dwRequiredSize = 1;

      // Calculate required cache size
      for(i = 0; i < m_dwNumThresholds; i++)
         if (dwRequiredSize < m_ppThresholdList[i]->getRequiredCacheSize())
            dwRequiredSize = m_ppThresholdList[i]->getRequiredCacheSize();

      RWLockReadLock(g_rwlockConditionIndex, INFINITE);
      for(i = 0; i < g_dwConditionIndexSize; i++)
      {
         dwSize = ((Condition *)g_pConditionIndex[i].pObject)->GetCacheSizeForDCI(m_dwId, dwCondId == g_pConditionIndex[i].dwKey);
         if (dwSize > dwRequiredSize)
            dwRequiredSize = dwSize;
      }
      RWLockUnlock(g_rwlockConditionIndex);
   }
   else
   {
      dwRequiredSize = 0;
   }

   // Update cache if needed
   if (dwRequiredSize < m_dwCacheSize)
   {
      // Destroy unneeded values
      if (m_dwCacheSize > 0)
         for(i = dwRequiredSize; i < m_dwCacheSize; i++)
            delete m_ppValueCache[i];

      m_dwCacheSize = dwRequiredSize;
      if (m_dwCacheSize > 0)
      {
         m_ppValueCache = (ItemValue **)realloc(m_ppValueCache, sizeof(ItemValue *) * m_dwCacheSize);
      }
      else
      {
         safe_free(m_ppValueCache);
         m_ppValueCache = NULL;
      }
   }
   else if (dwRequiredSize > m_dwCacheSize)
   {
      // Expand cache
      m_ppValueCache = (ItemValue **)realloc(m_ppValueCache, sizeof(ItemValue *) * dwRequiredSize);
      for(i = m_dwCacheSize; i < dwRequiredSize; i++)
         m_ppValueCache[i] = NULL;

      // Load missing values from database
      if (m_pNode != NULL)
      {
         DB_ASYNC_RESULT hResult;
         char szBuffer[MAX_DB_STRING];
         BOOL bHasData;

         switch(g_nDBSyntax)
         {
            case DB_SYNTAX_MSSQL:
               sprintf(szBuffer, "SELECT TOP %d idata_value,idata_timestamp FROM idata_%d "
                                 "WHERE item_id=%d ORDER BY idata_timestamp DESC",
                       dwRequiredSize, m_pNode->Id(), m_dwId);
               break;
            case DB_SYNTAX_ORACLE:
               sprintf(szBuffer, "SELECT idata_value,idata_timestamp FROM idata_%d "
                                 "WHERE item_id=%d AND ROWNUM <= %d ORDER BY idata_timestamp DESC",
                       m_pNode->Id(), m_dwId, dwRequiredSize);
               break;
            case DB_SYNTAX_MYSQL:
            case DB_SYNTAX_PGSQL:
            case DB_SYNTAX_SQLITE:
               sprintf(szBuffer, "SELECT idata_value,idata_timestamp FROM idata_%d "
                                 "WHERE item_id=%d ORDER BY idata_timestamp DESC LIMIT %d",
                       m_pNode->Id(), m_dwId, dwRequiredSize);
               break;
            default:
               sprintf(szBuffer, "SELECT idata_value,idata_timestamp FROM idata_%d "
                                 "WHERE item_id=%d ORDER BY idata_timestamp DESC",
                       m_pNode->Id(), m_dwId);
               break;
         }
         hResult = DBAsyncSelect(g_hCoreDB, szBuffer);
         if (hResult != NULL)
         {
            // Skip already cached values
            for(i = 0, bHasData = TRUE; i < m_dwCacheSize; i++)
               bHasData = DBFetch(hResult);

            // Create new cache entries
            for(; (i < dwRequiredSize) && bHasData; i++)
            {
               bHasData = DBFetch(hResult);
               if (bHasData)
               {
                  DBGetFieldAsync(hResult, 0, szBuffer, MAX_DB_STRING);
                  m_ppValueCache[i] = new ItemValue(szBuffer, DBGetFieldAsyncULong(hResult, 1));
               }
               else
               {
                  m_ppValueCache[i] = new ItemValue(_T(""), 1);   // Empty value
               }
            }

            // Fill up cache with empty values if we don't have enough values in database
            for(; i < dwRequiredSize; i++)
               m_ppValueCache[i] = new ItemValue(_T(""), 1);

            DBFreeAsyncResult(g_hCoreDB, hResult);
         }
         else
         {
            // Error reading data from database, fill cache with empty values
            for(i = m_dwCacheSize; i < dwRequiredSize; i++)
               m_ppValueCache[i] = new ItemValue(_T(""), 1);
         }
      }
      m_dwCacheSize = dwRequiredSize;
   }
   m_bCacheLoaded = TRUE;
}


//
// Put last value into CSCP message
//

void DCItem::getLastValue(CSCPMessage *pMsg, DWORD dwId)
{
	lock();
   pMsg->SetVariable(dwId++, m_dwId);
   pMsg->SetVariable(dwId++, m_szName);
   pMsg->SetVariable(dwId++, m_szDescription);
   pMsg->SetVariable(dwId++, (WORD)m_iSource);
   if (m_dwCacheSize > 0)
   {
      pMsg->SetVariable(dwId++, (WORD)m_iDataType);
      pMsg->SetVariable(dwId++, (TCHAR *)m_ppValueCache[0]->String());
      pMsg->SetVariable(dwId++, m_ppValueCache[0]->GetTimeStamp());
   }
   else
   {
      pMsg->SetVariable(dwId++, (WORD)DCI_DT_NULL);
      pMsg->SetVariable(dwId++, _T(""));
      pMsg->SetVariable(dwId++, (DWORD)0);
   }
   pMsg->SetVariable(dwId++, (WORD)m_iStatus);
	unlock();
}


//
// Get item's last value for use in NXSL
//

NXSL_Value *DCItem::getValueForNXSL(int nFunction, int nPolls)
{
   NXSL_Value *pValue;

	lock();
   switch(nFunction)
   {
      case F_LAST:
         pValue = (m_dwCacheSize > 0) ? new NXSL_Value((TCHAR *)m_ppValueCache[0]->String()) : new NXSL_Value;
         break;
      case F_DIFF:
         if (m_dwCacheSize >= 2)
         {
            ItemValue result;

            CalculateItemValueDiff(result, m_iDataType, *m_ppValueCache[0], *m_ppValueCache[1]);
            pValue = new NXSL_Value((TCHAR *)result.String());
         }
         else
         {
            pValue = new NXSL_Value;
         }
         break;
      case F_AVERAGE:
         if (m_dwCacheSize > 0)
         {
            ItemValue result;

            CalculateItemValueAverage(result, m_iDataType,
                                      min(m_dwCacheSize, (DWORD)nPolls), m_ppValueCache);
            pValue = new NXSL_Value((TCHAR *)result.String());
         }
         else
         {
            pValue = new NXSL_Value;
         }
         break;
      case F_DEVIATION:
         if (m_dwCacheSize > 0)
         {
            ItemValue result;

            CalculateItemValueMD(result, m_iDataType,
                                 min(m_dwCacheSize, (DWORD)nPolls), m_ppValueCache);
            pValue = new NXSL_Value((TCHAR *)result.String());
         }
         else
         {
            pValue = new NXSL_Value;
         }
         break;
      case F_ERROR:
         pValue = new NXSL_Value((LONG)((m_dwErrorCount >= (DWORD)nPolls) ? 1 : 0));
         break;
      default:
         pValue = new NXSL_Value;
         break;
   }
	unlock();
   return pValue;
}


//
// Clean expired data
//

void DCItem::deleteExpiredData()
{
   TCHAR szQuery[256];
   time_t now;

   now = time(NULL);
   lock();
   _sntprintf(szQuery, 256, _T("DELETE FROM idata_%d WHERE (item_id=%d) AND (idata_timestamp<%ld)"),
              (int)m_pNode->Id(), (int)m_dwId, (long)(now - (time_t)m_iRetentionTime * 86400));
   unlock();
   QueueSQLRequest(szQuery);
}


//
// Delete all collected data
//

BOOL DCItem::deleteAllData()
{
   TCHAR szQuery[256];
	BOOL success;

   lock();
   _sntprintf(szQuery, 256, _T("DELETE FROM idata_%d WHERE item_id=%d"), m_pNode->Id(), m_dwId);
   success = DBQuery(g_hCoreDB, szQuery);
	clearCache();
	updateCacheSize();
   unlock();
	return success;
}


//
// Prepare item for deletion
//

void DCItem::prepareForDeletion()
{
	DbgPrintf(9, _T("DCItem::prepareForDeletion for DCI %d"), m_dwId);

	lock();

   m_iStatus = ITEM_STATUS_DISABLED;   // Prevent future polls

   // Wait until current poll ends, if any
   // while() is not a very good solution, and probably need to be
   // rewrited using conditions
   while(m_iBusy)
   {
      unlock();
		DbgPrintf(9, _T("DCItem::prepareForDeletion: DCI %d busy"), m_dwId);
      ThreadSleepMs(100);
      lock();
   }

   unlock();
	DbgPrintf(9, _T("DCItem::prepareForDeletion: completed for DCI %d"), m_dwId);
}


//
// Match schedule element
// NOTE: We assume that pattern can be modified during processing
//

static BOOL MatchScheduleElement(TCHAR *pszPattern, int nValue)
{
   TCHAR *ptr, *curr;
   int nStep, nCurr, nPrev;
   BOOL bRun = TRUE, bRange = FALSE;

   // Check if step was specified
   ptr = _tcschr(pszPattern, _T('/'));
   if (ptr != NULL)
   {
      *ptr = 0;
      ptr++;
      nStep = atoi(ptr);
   }
   else
   {
      nStep = 1;
   }

   if (*pszPattern == _T('*'))
      goto check_step;

   for(curr = pszPattern; bRun; curr = ptr + 1)
   {
      for(ptr = curr; (*ptr != 0) && (*ptr != '-') && (*ptr != ','); ptr++);
      switch(*ptr)
      {
         case '-':
            if (bRange)
               return FALSE;  // Form like 1-2-3 is invalid
            bRange = TRUE;
            *ptr = 0;
            nPrev = atoi(curr);
            break;
         case 0:
            bRun = FALSE;
         case ',':
            *ptr = 0;
            nCurr = atoi(curr);
            if (bRange)
            {
               if ((nValue >= nPrev) && (nValue <= nCurr))
                  goto check_step;
               bRange = FALSE;
            }
            else
            {
               if (nValue == nCurr)
                  return TRUE;
            }
            break;
      }
   }

   return FALSE;

check_step:
   return (nValue % nStep) == 0;
}


//
// Match schedule to current time
//

static BOOL MatchSchedule(struct tm *pCurrTime, TCHAR *pszSchedule)
{
   TCHAR *pszCurr, szValue[256];

   // Minute
   pszCurr = ExtractWord(pszSchedule, szValue);
   if (!MatchScheduleElement(szValue, pCurrTime->tm_min))
         return FALSE;

   // Hour
   pszCurr = ExtractWord(pszCurr, szValue);
   if (!MatchScheduleElement(szValue, pCurrTime->tm_hour))
         return FALSE;

   // Day of month
   pszCurr = ExtractWord(pszCurr, szValue);
   if (!MatchScheduleElement(szValue, pCurrTime->tm_mday))
         return FALSE;

   // Month
   pszCurr = ExtractWord(pszCurr, szValue);
   if (!MatchScheduleElement(szValue, pCurrTime->tm_mon + 1))
         return FALSE;

   // Day of week
   ExtractWord(pszCurr, szValue);
   TranslateStr(szValue, _T("7"), _T("0"));
   return MatchScheduleElement(szValue, pCurrTime->tm_wday);
}


//
// Check if associated cluster resource is active. Returns TRUE also if
// DCI has no resource association
//

BOOL DCItem::matchClusterResource()
{
	Cluster *pCluster;

	if (m_dwResourceId == 0)
		return TRUE;

	pCluster = ((Node *)m_pNode)->getMyCluster();
	if (pCluster == NULL)
		return FALSE;	// Has association, but cluster object cannot be found

	return pCluster->isResourceOnNode(m_dwResourceId, m_pNode->Id());
}


//
// Set DCI status
//

void DCItem::setStatus(int status, bool generateEvent)
{
	if (generateEvent && (m_pNode != NULL) && (m_iStatus != (BYTE)status))
	{
		static DWORD eventCode[3] = { EVENT_DCI_ACTIVE, EVENT_DCI_DISABLED, EVENT_DCI_UNSUPPORTED };
		static const TCHAR *originName[5] = { _T("Internal"), _T("NetXMS Agent"), _T("SNMP"), _T("CheckPoint SNMP"), _T("Push") };

		PostEvent(eventCode[status], m_pNode->Id(), "dssds", m_dwId, m_szName, m_szDescription,
		          m_iSource, originName[m_iSource]);
	}
	m_iStatus = (BYTE)status;
}


//
// Check if DCI have to be polled
//

BOOL DCItem::isReadyForPolling(time_t currTime)
{
   BOOL bResult;

   lock();
   if ((m_iStatus != ITEM_STATUS_DISABLED) && (!m_iBusy) && 
       m_bCacheLoaded && (m_iSource != DS_PUSH_AGENT) &&
		 (matchClusterResource()))
   {
      if (m_iAdvSchedule)
      {
         DWORD i;
         struct tm tmCurrLocal, tmLastLocal;

         memcpy(&tmCurrLocal, localtime(&currTime), sizeof(struct tm));
         memcpy(&tmLastLocal, localtime(&m_tLastCheck), sizeof(struct tm));
         for(i = 0, bResult = FALSE; i < m_dwNumSchedules; i++)
         {
            if (MatchSchedule(&tmCurrLocal, m_ppScheduleList[i]))
            {
               if ((currTime - m_tLastCheck >= 60) ||
                   (tmCurrLocal.tm_min != tmLastLocal.tm_min))
               {
                  bResult = TRUE;
                  break;
               }
            }
         }
         m_tLastCheck = currTime;
      }
      else
      {
			if (m_iStatus == ITEM_STATUS_NOT_SUPPORTED)
		      bResult = (m_tLastPoll + m_iPollingInterval * 10 <= currTime);
			else
		      bResult = (m_tLastPoll + m_iPollingInterval <= currTime);
      }
   }
   else
   {
      bResult = FALSE;
   }
   unlock();
   return bResult;
}


//
// Update from template item
//

void DCItem::updateFromTemplate(DCItem *pItem)
{
   DWORD i, dwCount;

   lock();

   m_iDataType = pItem->m_iDataType;
   m_iPollingInterval = pItem->m_iPollingInterval;
   m_iRetentionTime = pItem->m_iRetentionTime;
   m_iDeltaCalculation = pItem->m_iDeltaCalculation;
   m_iSource = pItem->m_iSource;
   setStatus(pItem->m_iStatus, true);
   m_iProcessAllThresholds = pItem->m_iProcessAllThresholds;
	m_dwProxyNode = pItem->m_dwProxyNode;
   setTransformationScript(pItem->m_pszScript);
   m_iAdvSchedule = pItem->m_iAdvSchedule;

	safe_free(m_pszPerfTabSettings);
	m_pszPerfTabSettings = (pItem->m_pszPerfTabSettings != NULL) ? _tcsdup(pItem->m_pszPerfTabSettings) : NULL;

	m_nBaseUnits = pItem->m_nBaseUnits;
	m_nMultiplier = pItem->m_nMultiplier;
	safe_free(m_pszCustomUnitName);
	m_pszCustomUnitName = (pItem->m_pszCustomUnitName != NULL) ? _tcsdup(pItem->m_pszCustomUnitName) : NULL;

   // Copy schedules
   for(i = 0; i < m_dwNumSchedules; i++)
      safe_free(m_ppScheduleList[i]);
   safe_free(m_ppScheduleList);
   m_dwNumSchedules = pItem->m_dwNumSchedules;
   m_ppScheduleList = (TCHAR **)malloc(sizeof(TCHAR *) * m_dwNumSchedules);
   for(i = 0; i < m_dwNumSchedules; i++)
      m_ppScheduleList[i] = _tcsdup(pItem->m_ppScheduleList[i]);

   // Copy thresholds
   // ***************************
   // First, skip matching thresholds
   dwCount = min(m_dwNumThresholds, pItem->m_dwNumThresholds);
   for(i = 0; i < dwCount; i++)
      if (!m_ppThresholdList[i]->compare(pItem->m_ppThresholdList[i]))
         break;
   dwCount = i;   // First unmatched threshold's position

   // Delete all original thresholds starting from first unmatched
   for(; i < m_dwNumThresholds; i++)
      delete m_ppThresholdList[i];

   // (Re)create thresholds starting from first unmatched
   m_dwNumThresholds = pItem->m_dwNumThresholds;
   m_ppThresholdList = (Threshold **)realloc(m_ppThresholdList, sizeof(Threshold *) * m_dwNumThresholds);
   for(i = dwCount; i < m_dwNumThresholds; i++)
   {
      m_ppThresholdList[i] = new Threshold(pItem->m_ppThresholdList[i]);
      m_ppThresholdList[i]->createId();
      m_ppThresholdList[i]->bindToItem(m_dwId);
   }

   expandMacros(pItem->m_szName, m_szName, MAX_ITEM_NAME);
   expandMacros(pItem->m_szDescription, m_szDescription, MAX_DB_STRING);
   expandMacros(pItem->m_szInstance, m_szInstance, MAX_DB_STRING);
	expandMacros(pItem->m_systemTag, m_systemTag, MAX_DB_STRING);

   updateCacheSize();
   
   unlock();
}


//
// Set new formula
//

void DCItem::setTransformationScript(const TCHAR *pszScript)
{
   safe_free(m_pszScript);
   delete m_pScript;
   if (pszScript != NULL)
   {
      m_pszScript = _tcsdup(pszScript);
      StrStrip(m_pszScript);
      if (m_pszScript[0] != 0)
      {
			/* TODO: add compilation error handling */
         m_pScript = (NXSL_Program *)NXSLCompile(m_pszScript, NULL, 0);
      }
      else
      {
         m_pScript = NULL;
      }
   }
   else
   {
      m_pszScript = NULL;
      m_pScript = NULL;
   }
}


//
// Get list of used events
//

void DCItem::getEventList(DWORD **ppdwList, DWORD *pdwSize)
{
   DWORD i, j;

   lock();

   if (m_dwNumThresholds > 0)
   {
      *ppdwList = (DWORD *)realloc(*ppdwList, sizeof(DWORD) * (*pdwSize + m_dwNumThresholds * 2));
      j = *pdwSize;
      *pdwSize += m_dwNumThresholds * 2;
      for(i = 0; i < m_dwNumThresholds; i++)
      {
         (*ppdwList)[j++] = m_ppThresholdList[i]->getEventCode();
			(*ppdwList)[j++] = m_ppThresholdList[i]->getRearmEventCode();
      }
   }

   unlock();
}


//
// Create management pack record
//

void DCItem::createNXMPRecord(String &str)
{
	DWORD i;

   lock();
   
   str.addFormattedString(_T("\t\t\t\t<dci id=\"%d\">\n")
                          _T("\t\t\t\t\t<name>%s</name>\n")
                          _T("\t\t\t\t\t<description>%s</description>\n")
                          _T("\t\t\t\t\t<dataType>%d</dataType>\n")
                          _T("\t\t\t\t\t<origin>%d</origin>\n")
                          _T("\t\t\t\t\t<interval>%d</interval>\n")
                          _T("\t\t\t\t\t<retention>%d</retention>\n")
                          _T("\t\t\t\t\t<instance>%s</instance>\n")
                          _T("\t\t\t\t\t<systemTag>%s</systemTag>\n")
                          _T("\t\t\t\t\t<delta>%d</delta>\n")
                          _T("\t\t\t\t\t<advancedSchedule>%d</advancedSchedule>\n")
                          _T("\t\t\t\t\t<allThresholds>%d</allThresholds>\n"),
								  m_dwId, (const TCHAR *)EscapeStringForXML2(m_szName),
                          (const TCHAR *)EscapeStringForXML2(m_szDescription),
                          m_iDataType, m_iSource, m_iPollingInterval, m_iRetentionTime,
                          (const TCHAR *)EscapeStringForXML2(m_szInstance),
                          (const TCHAR *)EscapeStringForXML2(m_systemTag),
								  m_iDeltaCalculation, m_iAdvSchedule,
                          m_iProcessAllThresholds);

	if (m_pszScript != NULL)
	{
		str += _T("\t\t\t\t\t<transformation>");
		str.addDynamicString(EscapeStringForXML(m_pszScript, -1));
		str += _T("</transformation>\n");
	}

   if (m_iAdvSchedule)
   {
      str += _T("\t\t\t\t\t<schedules>\n");
      for(i = 0; i < m_dwNumSchedules; i++)
         str.addFormattedString(_T("\t\t\t\t\t\t<schedule>%s</schedule>\n"), (const TCHAR *)EscapeStringForXML2(m_ppScheduleList[i]));
      str += _T("\t\t\t\t\t</schedules>\n");
   }

   str += _T("\t\t\t\t\t<thresholds>\n");
   for(i = 0; i < m_dwNumThresholds; i++)
   {
      m_ppThresholdList[i]->createNXMPRecord(str, i + 1);
   }
   str += _T("\t\t\t\t\t</thresholds>\n");

   unlock();
   str += _T("\t\t\t\t</dci>\n");
}


//
// Modify item - intended for updating items in system templates
//

void DCItem::systemModify(const TCHAR *pszName, int nOrigin, int nRetention, int nInterval, int nDataType)
{
   m_iDataType = nDataType;
   m_iPollingInterval = nInterval;
   m_iRetentionTime = nRetention;
   m_iSource = nOrigin;
	nx_strncpy(m_szName, pszName, MAX_ITEM_NAME);
}


//
// Finish DCI parsing from management pack:
// 1. Generate unique ID for DCI
// 2. Generate unique ID for thresholds
// 3. Associate all thresholds
//

void DCItem::finishMPParsing(void)
{
	DWORD i;

	m_dwId = CreateUniqueId(IDG_ITEM);
	for(i = 0; i < m_dwNumThresholds; i++)
	{
		m_ppThresholdList[i]->createId();
		m_ppThresholdList[i]->associate(this);
	}
}


//
// Add threshold to the list
//

void DCItem::addThreshold(Threshold *pThreshold)
{
	m_dwNumThresholds++;
	m_ppThresholdList = (Threshold **)realloc(m_ppThresholdList, sizeof(Threshold *) * m_dwNumThresholds);
   m_ppThresholdList[m_dwNumThresholds - 1] = pThreshold;
}


//
// Add schedule
//

void DCItem::addSchedule(const TCHAR *pszSchedule)
{
	m_dwNumSchedules++;
	m_ppScheduleList = (TCHAR **)realloc(m_ppScheduleList, sizeof(TCHAR *) * m_dwNumSchedules);
	m_ppScheduleList[m_dwNumSchedules - 1] = _tcsdup(pszSchedule);
}


//
// Enumerate all thresholds
//

BOOL DCItem::enumThresholds(BOOL (* pfCallback)(Threshold *, DWORD, void *), void *pArg)
{
	DWORD i;
	BOOL bRet = TRUE;

	lock();
	for(i = 0; i < m_dwNumThresholds; i++)
	{
		if (!pfCallback(m_ppThresholdList[i], i, pArg))
		{
			bRet = FALSE;
			break;
		}
	}
	unlock();
	return bRet;
}


//
// Expand macros in text
//

void DCItem::expandMacros(const TCHAR *src, TCHAR *dst, size_t dstLen)
{
	String temp;
	TCHAR *head, *rest, *macro;
	int index = 0, index2;

	temp = src;
	while((index = temp.find(_T("%{"), index)) != String::npos)
	{
		head = temp.subStr(0, index);
		index2 = temp.find(_T("}"), index);
		if (index2 == String::npos)
		{
			free(head);
			break;	// Missing closing }
		}
		rest = temp.subStr(index2 + 1, -1);
		macro = temp.subStr(index + 2, index2 - index - 2);
		StrStrip(macro);

		temp = head;
		if (!_tcscmp(macro, _T("node_id")))
		{
			if (m_pNode != NULL)
			{
				temp.addFormattedString(_T("%d"), m_pNode->Id());
			}
			else
			{
				temp += _T("(error)");
			}
		}
		else if (!_tcscmp(macro, _T("node_name")))
		{
			if (m_pNode != NULL)
			{
				temp += m_pNode->Name();
			}
			else
			{
				temp += _T("(error)");
			}
		}
		else if (!_tcscmp(macro, _T("node_primary_ip")))
		{
			if (m_pNode != NULL)
			{
				TCHAR ipAddr[32];

				temp += IpToStr(m_pNode->IpAddr(), ipAddr);
			}
			else
			{
				temp += _T("(error)");
			}
		}
		else if (!_tcsncmp(macro, _T("script:"), 7))
		{
			NXSL_Program *script;
			NXSL_ServerEnv *pEnv;

	      g_pScriptLibrary->lock();
			script = g_pScriptLibrary->findScript(&macro[7]);
			if (script != NULL)
			{
				pEnv = new NXSL_ServerEnv;
				if (m_pNode != NULL)
					script->setGlobalVariable(_T("$node"), new NXSL_Value(new NXSL_Object(&g_nxslNodeClass, m_pNode)));

				if (script->run(pEnv) == 0)
				{
					NXSL_Value *result = script->getResult();
					if (result != NULL)
						temp += CHECK_NULL_EX(result->getValueAsCString());
		         DbgPrintf(4, "DCItem::expandMacros(%d,\"%s\"): Script %s executed successfully", m_dwId, src, &macro[7]);
				}
				else
				{
		         DbgPrintf(4, "DCItem::expandMacros(%d,\"%s\"): Script %s execution error: %s",
					          m_dwId, src, &macro[7], script->getErrorText());
					PostEvent(EVENT_SCRIPT_ERROR, g_dwMgmtNode, _T("ssd"), &macro[7],
								 script->getErrorText(), m_dwId);
				}
			}
			else
			{
	         DbgPrintf(4, "DCItem::expandMacros(%d,\"%s\"): Cannot find script %s", m_dwId, src, &macro[7]);
			}
	      g_pScriptLibrary->unlock();
		}
		temp += rest;
		
		free(head);
		free(rest);
		free(macro);
	}
	nx_strncpy(dst, temp, dstLen);
}


//
// Test DCI's transformation script
// Runs 
//

BOOL DCItem::testTransformation(const TCHAR *script, const TCHAR *value, TCHAR *buffer, size_t bufSize)
{
	BOOL success = FALSE;
	NXSL_Program *pScript;

	pScript = NXSLCompile(script, buffer, (int)bufSize);
   if (pScript != NULL)
   {
      NXSL_Value *pValue;
      NXSL_ServerEnv *pEnv;

      pValue = new NXSL_Value(value);
      pEnv = new NXSL_ServerEnv;
      pScript->setGlobalVariable(_T("$node"), new NXSL_Value(new NXSL_Object(&g_nxslNodeClass, m_pNode)));
	
	 	lock();
		if (pScript->run(pEnv, 1, &pValue) == 0)
      {
         pValue = pScript->getResult();
         if (pValue != NULL)
         {
				if (pValue->isNull())
				{
					nx_strncpy(buffer, _T("(null)"), bufSize);
				}
				else if (pValue->isObject())
				{
					nx_strncpy(buffer, _T("(object)"), bufSize);
				}
				else if (pValue->isArray())
				{
					nx_strncpy(buffer, _T("(array)"), bufSize);
				}
				else
				{
					const TCHAR *strval;

					strval = pValue->getValueAsCString();
					nx_strncpy(buffer, CHECK_NULL(strval), bufSize);
				}
			}
			else
			{
				nx_strncpy(buffer, _T("(null)"), bufSize);
			}
			success = TRUE;
      }
      else
      {
			nx_strncpy(buffer, pScript->getErrorText(), bufSize);
      }
		unlock();
   }
	return success;
}


//
// Fill NXCP message with thresholds
//

void DCItem::fillMessageWithThresholds(CSCPMessage *msg)
{
	DWORD i, id;

	lock();

	msg->SetVariable(VID_NUM_THRESHOLDS, m_dwNumThresholds);
	for(i = 0, id = VID_DCI_THRESHOLD_BASE; i < m_dwNumThresholds; i++, id += 10)
	{
		m_ppThresholdList[i]->createMessage(msg, id);
	}

	unlock();
}
