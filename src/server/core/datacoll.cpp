/*
** NetXMS - Network Management System
** Copyright (C) 2003-2019 Victor Kirhenshtein
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
** File: datacoll.cpp
**
**/

#include "nxcore.h"
#include <gauge_helpers.h>

/**
 * Interval between DCI polling
 */
#define ITEM_POLLING_INTERVAL             1

/**
 * Thread pool for data collectors
 */
ThreadPool *g_dataCollectorThreadPool = NULL;

/**
 * DCI cache loader queue
 */
Queue g_dciCacheLoaderQueue;

/**
 * Average time to queue DCI
 */
UINT32 g_averageDCIQueuingTime = 0;

/**
 * Collect data for DCI
 */
static void *GetItemData(DataCollectionTarget *dcTarget, DCItem *pItem, TCHAR *pBuffer, UINT32 *error)
{
   if (dcTarget->getObjectClass() == OBJECT_CLUSTER)
   {
      if (pItem->isAggregateOnCluster())
      {
         *error = static_cast<Cluster*>(dcTarget)->collectAggregatedData(pItem, pBuffer);
      }
      else
      {
         *error = DCE_IGNORE;
      }
   }
   else
   {
      switch(pItem->getDataSource())
      {
         case DS_INTERNAL:    // Server internal parameters (like status)
            *error = dcTarget->getInternalItem(pItem->getName(), MAX_LINE_SIZE, pBuffer);
            break;
         case DS_SNMP_AGENT:
			   if (dcTarget->getObjectClass() == OBJECT_NODE)
				   *error = ((Node *)dcTarget)->getItemFromSNMP(pItem->getSnmpPort(), pItem->getName(), MAX_LINE_SIZE,
					   pBuffer, pItem->isInterpretSnmpRawValue() ? (int)pItem->getSnmpRawValueType() : SNMP_RAWTYPE_NONE);
			   else
				   *error = DCE_NOT_SUPPORTED;
            break;
         case DS_DEVICE_DRIVER:
            if (dcTarget->getObjectClass() == OBJECT_NODE)
               *error = ((Node *)dcTarget)->getItemFromDeviceDriver(pItem->getName(), pBuffer, MAX_LINE_SIZE);
            else
               *error = DCE_NOT_SUPPORTED;
            break;
         case DS_CHECKPOINT_AGENT:
			   if (dcTarget->getObjectClass() == OBJECT_NODE)
	            *error = ((Node *)dcTarget)->getItemFromCheckPointSNMP(pItem->getName(), MAX_LINE_SIZE, pBuffer);
			   else
				   *error = DCE_NOT_SUPPORTED;
            break;
         case DS_NATIVE_AGENT:
			   if (dcTarget->getObjectClass() == OBJECT_NODE)
	            *error = ((Node *)dcTarget)->getItemFromAgent(pItem->getName(), MAX_LINE_SIZE, pBuffer);
			   else if (dcTarget->getObjectClass() == OBJECT_SENSOR)
               *error = ((Sensor *)dcTarget)->getItemFromAgent(pItem->getName(), MAX_LINE_SIZE, pBuffer);
			   else
				   *error = DCE_NOT_SUPPORTED;
            break;
         case DS_WINPERF:
			   if (dcTarget->getObjectClass() == OBJECT_NODE)
			   {
				   TCHAR name[MAX_PARAM_NAME];
				   _sntprintf(name, MAX_PARAM_NAME, _T("PDH.CounterValue(\"%s\",%d)"), (const TCHAR *)EscapeStringForAgent(pItem->getName()), pItem->getSampleCount());
	            *error = ((Node *)dcTarget)->getItemFromAgent(name, MAX_LINE_SIZE, pBuffer);
			   }
			   else
			   {
				   *error = DCE_NOT_SUPPORTED;
			   }
            break;
         case DS_SSH:
            if (dcTarget->getObjectClass() == OBJECT_NODE)
            {
               Node *proxy = (Node *)FindObjectById(static_cast<Node*>(dcTarget)->getEffectiveSshProxy(), OBJECT_NODE);
               if (proxy != NULL)
               {
                  TCHAR name[MAX_PARAM_NAME], ipAddr[64];
                  _sntprintf(name, MAX_PARAM_NAME, _T("SSH.Command(%s,\"%s\",\"%s\",\"%s\")"),
                             ((Node *)dcTarget)->getIpAddress().toString(ipAddr),
                             (const TCHAR *)EscapeStringForAgent(static_cast<Node*>(dcTarget)->getSshLogin()),
                             (const TCHAR *)EscapeStringForAgent(static_cast<Node*>(dcTarget)->getSshPassword()),
                             (const TCHAR *)EscapeStringForAgent(pItem->getName()));
                  *error = proxy->getItemFromAgent(name, MAX_LINE_SIZE, pBuffer);
               }
               else
               {
                  *error = DCE_COMM_ERROR;
               }
            }
            else
            {
               *error = DCE_NOT_SUPPORTED;
            }
            break;
         case DS_SMCLP:
            if (dcTarget->getObjectClass() == OBJECT_NODE)
            {
	            *error = ((Node *)dcTarget)->getItemFromSMCLP(pItem->getName(), pBuffer, MAX_LINE_SIZE);
            }
            else
            {
               *error = DCE_NOT_SUPPORTED;
            }
            break;
         case DS_SCRIPT:
            *error = dcTarget->getScriptItem(pItem->getName(), MAX_LINE_SIZE, pBuffer, (DataCollectionTarget *)pItem->getOwner());
            break;
		   default:
			   *error = DCE_NOT_SUPPORTED;
			   break;
      }
   }
	return pBuffer;
}

/**
 * Collect data for table
 */
static void *GetTableData(DataCollectionTarget *dcTarget, DCTable *table, UINT32 *error)
{
	Table *result = NULL;
   if (dcTarget->getObjectClass() == OBJECT_CLUSTER)
   {
      if (table->isAggregateOnCluster())
      {
         *error = ((Cluster *)dcTarget)->collectAggregatedData(table, &result);
      }
      else
      {
         *error = DCE_IGNORE;
      }
   }
   else
   {
      switch(table->getDataSource())
      {
		   case DS_NATIVE_AGENT:
			   if (dcTarget->getObjectClass() == OBJECT_NODE)
            {
				   *error = ((Node *)dcTarget)->getTableFromAgent(table->getName(), &result);
               if ((*error == DCE_SUCCESS) && (result != NULL))
                  table->updateResultColumns(result);
            }
			   else
            {
				   *error = DCE_NOT_SUPPORTED;
            }
			   break;
         case DS_SNMP_AGENT:
			   if (dcTarget->getObjectClass() == OBJECT_NODE)
            {
               *error = ((Node *)dcTarget)->getTableFromSNMP(table->getSnmpPort(), table->getName(), table->getColumns(), &result);
               if ((*error == DCE_SUCCESS) && (result != NULL))
                  table->updateResultColumns(result);
            }
			   else
            {
				   *error = DCE_NOT_SUPPORTED;
            }
            break;
         case DS_SCRIPT:
            *error = dcTarget->getScriptTable(table->getName(), &result, (DataCollectionTarget *)table->getOwner());
            break;
		   default:
			   *error = DCE_NOT_SUPPORTED;
			   break;
	   }
   }
	return result;
}

/**
 * Data collector
 */
void DataCollector(void *arg)
{
   DCObject *pItem = static_cast<DCObject*>(arg);
   DataCollectionTarget *target = static_cast<DataCollectionTarget*>(pItem->getOwner());

   if (pItem->isScheduledForDeletion())
   {
      nxlog_debug(7, _T("DataCollector(): about to destroy DC object %d \"%s\" owner=%d"),
                  pItem->getId(), pItem->getName(), (target != NULL) ? (int)target->getId() : -1);
      pItem->deleteFromDatabase();
      delete pItem;
      if (target != NULL)
         target->decRefCount();
      return;
   }

   if (target == NULL)
   {
      nxlog_debug(3, _T("DataCollector: attempt to collect information for non-existing node (DCI=%d \"%s\")"),
                  pItem->getId(), pItem->getName());

      // Update item's last poll time and clear busy flag so item can be polled again
      pItem->setLastPollTime(time(NULL));
      pItem->clearBusyFlag();
      return;
   }

   if (IsShutdownInProgress())
   {
      pItem->clearBusyFlag();
      return;
   }

   DbgPrintf(8, _T("DataCollector(): processing DC object %d \"%s\" owner=%d sourceNode=%d"),
             pItem->getId(), pItem->getName(), (target != NULL) ? (int)target->getId() : -1, pItem->getSourceNode());
   UINT32 sourceNodeId = target->getEffectiveSourceNode(pItem);
   if (sourceNodeId != 0)
   {
      Node *sourceNode = (Node *)FindObjectById(sourceNodeId, OBJECT_NODE);
      if (sourceNode != NULL)
      {
         if (((target->getObjectClass() == OBJECT_CHASSIS) && (((Chassis *)target)->getControllerId() == sourceNodeId)) ||
             sourceNode->isTrustedNode(target->getId()))
         {
            target = sourceNode;
            target->incRefCount();
         }
         else
         {
            // Change item's status to "not supported"
            pItem->setStatus(ITEM_STATUS_NOT_SUPPORTED, true);
            target->decRefCount();
            target = NULL;
         }
      }
      else
      {
         target->decRefCount();
         target = NULL;
      }
   }

   time_t currTime = time(NULL);
   if (target != NULL)
   {
      if (!IsShutdownInProgress())
      {
         void *data;
         TCHAR buffer[MAX_LINE_SIZE];
         UINT32 error;
         switch(pItem->getType())
         {
            case DCO_TYPE_ITEM:
               data = GetItemData(target, (DCItem *)pItem, buffer, &error);
               break;
            case DCO_TYPE_TABLE:
               data = GetTableData(target, (DCTable *)pItem, &error);
               break;
            default:
               data = NULL;
               error = DCE_NOT_SUPPORTED;
               break;
         }

         // Transform and store received value into database or handle error
         switch(error)
         {
            case DCE_SUCCESS:
               if (pItem->getStatus() == ITEM_STATUS_NOT_SUPPORTED)
                  pItem->setStatus(ITEM_STATUS_ACTIVE, true);
               if (!((DataCollectionTarget *)pItem->getOwner())->processNewDCValue(pItem, currTime, data))
               {
                  // value processing failed, convert to data collection error
                  pItem->processNewError(false);
               }
               break;
            case DCE_COLLECTION_ERROR:
               if (pItem->getStatus() == ITEM_STATUS_NOT_SUPPORTED)
                  pItem->setStatus(ITEM_STATUS_ACTIVE, true);
               pItem->processNewError(false);
               break;
            case DCE_NO_SUCH_INSTANCE:
               if (pItem->getStatus() == ITEM_STATUS_NOT_SUPPORTED)
                  pItem->setStatus(ITEM_STATUS_ACTIVE, true);
               pItem->processNewError(true);
               break;
            case DCE_COMM_ERROR:
               pItem->processNewError(false);
               break;
            case DCE_NOT_SUPPORTED:
               // Change item's status
               pItem->setStatus(ITEM_STATUS_NOT_SUPPORTED, true);
               break;
         }

         // Send session notification when force poll is performed
         if (pItem->getPollingSession() != NULL)
         {
            ClientSession *session = pItem->processForcePoll();
            session->notify(NX_NOTIFY_FORCE_DCI_POLL, pItem->getOwnerId());
            session->decRefCount();
         }
      }

      // Decrement node's usage counter
      target->decRefCount();
      if ((pItem->getSourceNode() != 0) && (pItem->getOwner() != NULL))
      {
         pItem->getOwner()->decRefCount();
      }
   }
   else     /* target == NULL */
   {
      DataCollectionOwner *n = pItem->getOwner();
      nxlog_debug(5, _T("DataCollector: attempt to collect information for non-existing or inaccessible node (DCI=%d \"%s\" target=%d sourceNode=%d)"),
                  pItem->getId(), pItem->getName(), (n != NULL) ? (int)n->getId() : -1, sourceNodeId);
   }

   // Update item's last poll time and clear busy flag so item can be polled again
   pItem->setLastPollTime(currTime);
   pItem->clearBusyFlag();
}

/**
 * Callback for queueing DCIs
 */
static void QueueItems(NetObj *object, void *data)
{
   if (IsShutdownInProgress())
      return;

   WatchdogNotify(*((UINT32 *)data));
	nxlog_debug(8, _T("ItemPoller: calling DataCollectionTarget::queueItemsForPolling for object %s [%d]"),
				   object->getName(), object->getId());
	((DataCollectionTarget *)object)->queueItemsForPolling();
}

/**
 * Item poller thread: check nodes' items and put into the
 * data collector queue when data polling required
 */
static THREAD_RESULT THREAD_CALL ItemPoller(void *pArg)
{
   ThreadSetName("ItemPoller");

   UINT32 watchdogId = WatchdogAddThread(_T("Item Poller"), 10);
   GaugeData<UINT32> queuingTime(ITEM_POLLING_INTERVAL, 300);

   while(!IsShutdownInProgress())
   {
      if (SleepAndCheckForShutdown(ITEM_POLLING_INTERVAL))
         break;      // Shutdown has arrived
      WatchdogNotify(watchdogId);
		DbgPrintf(8, _T("ItemPoller: wakeup"));

      INT64 startTime = GetCurrentTimeMs();
		g_idxNodeById.forEach(QueueItems, &watchdogId);
		g_idxClusterById.forEach(QueueItems, &watchdogId);
		g_idxMobileDeviceById.forEach(QueueItems, &watchdogId);
      g_idxChassisById.forEach(QueueItems, &watchdogId);
		g_idxSensorById.forEach(QueueItems, &watchdogId);

		queuingTime.update(static_cast<UINT32>(GetCurrentTimeMs() - startTime));
		g_averageDCIQueuingTime = static_cast<UINT32>(queuingTime.getAverage());
   }
   DbgPrintf(1, _T("Item poller thread terminated"));
   return THREAD_OK;
}

/**
 * DCI cache loader
 */
THREAD_RESULT THREAD_CALL CacheLoader(void *arg)
{
   ThreadSetName("CacheLoader");
   nxlog_debug_tag(_T("obj.dc.cache"), 2, _T("DCI cache loader thread started"));
   while(true)
   {
      DCObjectInfo *ref = (DCObjectInfo *)g_dciCacheLoaderQueue.getOrBlock();
      if (ref == INVALID_POINTER_VALUE)
         break;

      NetObj *object = FindObjectById(ref->getOwnerId());
      if ((object != NULL) && object->isDataCollectionTarget())
      {
         object->incRefCount();
         DCObject *dci = static_cast<DataCollectionTarget*>(object)->getDCObjectById(ref->getId(), 0, true);
         if ((dci != NULL) && (dci->getType() == DCO_TYPE_ITEM))
         {
            nxlog_debug_tag(_T("obj.dc.cache"), 6, _T("Loading cache for DCI %s [%d] on %s [%d]"),
                     ref->getName(), ref->getId(), object->getName(), object->getId());
            static_cast<DCItem*>(dci)->reloadCache();
         }
         object->decRefCount();
      }
      delete ref;
   }
   nxlog_debug_tag(_T("obj.dc.cache"), 2, _T("DCI cache loader thread stopped"));
   return THREAD_OK;
}

/**
 * Threads
 */
static THREAD s_itemPollerThread = INVALID_THREAD_HANDLE;
static THREAD s_cacheLoaderThread = INVALID_THREAD_HANDLE;

/**
 * Initialize data collection subsystem
 */
void InitDataCollector()
{
   g_dataCollectorThreadPool = ThreadPoolCreate(_T("DATACOLL"),
            ConfigReadInt(_T("ThreadPool.DataCollector.BaseSize"), 10),
            ConfigReadInt(_T("ThreadPool.DataCollector.MaxSize"), 250),
            128 * 1024);

   s_itemPollerThread = ThreadCreateEx(ItemPoller, 0, NULL);
   s_cacheLoaderThread = ThreadCreateEx(CacheLoader, 0, NULL);
}

/**
 * Stop data collection
 */
void StopDataCollection()
{
   ThreadJoin(s_itemPollerThread);
   ThreadJoin(s_cacheLoaderThread);
   ThreadPoolDestroy(g_dataCollectorThreadPool);
}

/**
 * Callback data for WriteFullParamListToMessage
 */
struct WriteFullParamListToMessage_CallbackData
{
   int origin;
   ObjectArray<AgentParameterDefinition> *parameters;
};

/**
 * Update parameter list from node
 */
static void UpdateParamList(NetObj *object, void *data)
{
   WriteFullParamListToMessage_CallbackData *cd = static_cast<WriteFullParamListToMessage_CallbackData*>(data);

	ObjectArray<AgentParameterDefinition> *paramList = static_cast<Node*>(object)->openParamList(cd->origin);
	if ((paramList != NULL) && (paramList->size() > 0))
	{
		for(int i = 0; i < paramList->size(); i++)
		{
			int j;
			for(j = 0; j < cd->parameters->size(); j++)
			{
				if (!_tcsicmp(paramList->get(i)->getName(), cd->parameters->get(j)->getName()))
					break;
			}

			if (j == cd->parameters->size())
			{
			   cd->parameters->add(new AgentParameterDefinition(paramList->get(i)));
			}
		}
	}
	static_cast<Node*>(object)->closeParamList();
}

/**
 * Update table list from node
 */
static void UpdateTableList(NetObj *object, void *data)
{
	ObjectArray<AgentTableDefinition> *fullList = (ObjectArray<AgentTableDefinition> *)data;

   ObjectArray<AgentTableDefinition> *tableList = static_cast<Node*>(object)->openTableList();
	if ((tableList != NULL) && (tableList->size() > 0))
	{
		for(int i = 0; i < tableList->size(); i++)
		{
			int j;
			for(j = 0; j < fullList->size(); j++)
			{
				if (!_tcsicmp(tableList->get(i)->getName(), fullList->get(j)->getName()))
					break;
			}

			if (j == fullList->size())
			{
            fullList->add(new AgentTableDefinition(tableList->get(i)));
			}
		}
	}
	static_cast<Node*>(object)->closeTableList();
}

/**
 * Write full (from all nodes) agent parameters list to NXCP message
 */
void WriteFullParamListToMessage(NXCPMessage *pMsg, int origin, WORD flags)
{
   // Gather full parameter list
	if (flags & 0x01)
	{
		ObjectArray<AgentParameterDefinition> fullList(64, 64, true);
		WriteFullParamListToMessage_CallbackData data;
		data.origin = origin;
		data.parameters = &fullList;
		g_idxNodeById.forEach(UpdateParamList, &data);

		// Put list into the message
		pMsg->setField(VID_NUM_PARAMETERS, (UINT32)fullList.size());
      UINT32 varId = VID_PARAM_LIST_BASE;
		for(int i = 0; i < fullList.size(); i++)
		{
         varId += fullList.get(i)->fillMessage(pMsg, varId);
		}
	}

   // Gather full table list
	if (flags & 0x02)
	{
		ObjectArray<AgentTableDefinition> fullList(64, 64, true);
		g_idxNodeById.forEach(UpdateTableList, &fullList);

		// Put list into the message
		pMsg->setField(VID_NUM_TABLES, (UINT32)fullList.size());
      UINT32 varId = VID_TABLE_LIST_BASE;
		for(int i = 0; i < fullList.size(); i++)
		{
         varId += fullList.get(i)->fillMessage(pMsg, varId);
		}
	}
}

/**
 * Get type of data collection object
 */
int GetDCObjectType(UINT32 nodeId, UINT32 dciId)
{
   Node *node = (Node *)FindObjectById(nodeId, OBJECT_NODE);
   if (node != NULL)
   {
      DCObject *dco = node->getDCObjectById(dciId, 0);
      if (dco != NULL)
      {
         return dco->getType();
      }
   }
   return DCO_TYPE_ITEM;   // default
}
