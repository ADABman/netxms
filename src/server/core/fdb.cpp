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
** File: fdb.cpp
**
**/

#include "nxcore.h"


//
// Constructor
//

ForwardingDatabase::ForwardingDatabase()
{
	m_fdb = NULL;
	m_fdbSize = 0;
	m_fdbAllocated = 0;
	m_portMap = NULL;
	m_pmSize = 0;
	m_pmAllocated = 0;
	m_timestamp = time(NULL);
	m_refCount = 1;
}


//
// Destructor
//

ForwardingDatabase::~ForwardingDatabase()
{
	safe_free(m_fdb);
	safe_free(m_portMap);
}


//
// Decrement reference count. If it reaches 0, destroy object.
//

void ForwardingDatabase::decRefCount()
{
	m_refCount--;
	if (m_refCount == 0)
		delete this;
}


//
// Add port mapping entry
//

void ForwardingDatabase::addPortMapping(PORT_MAPPING_ENTRY *entry)
{
	if (m_pmSize == m_pmAllocated)
	{
		m_pmAllocated += 32;
		m_portMap = (PORT_MAPPING_ENTRY *)realloc(m_portMap, sizeof(PORT_MAPPING_ENTRY) * m_pmAllocated);
	}
	memcpy(&m_portMap[m_pmSize], entry, sizeof(PORT_MAPPING_ENTRY));
	m_pmSize++;
}


//
// Get interface index for given port number
//

DWORD ForwardingDatabase::ifIndexFromPort(DWORD port)
{
	for(int i = 0; i < m_pmSize; i++)
		if (m_portMap[i].port == port)
			return m_portMap[i].ifIndex;
	return 0;
}


//
// Add entry
//

void ForwardingDatabase::addEntry(FDB_ENTRY *entry)
{
	if (m_fdbSize == m_fdbAllocated)
	{
		m_fdbAllocated += 32;
		m_fdb = (FDB_ENTRY *)realloc(m_fdb, sizeof(FDB_ENTRY) * m_fdbAllocated);
	}
	memcpy(&m_fdb[m_fdbSize], entry, sizeof(FDB_ENTRY));
	m_fdb[m_fdbSize].ifIndex = ifIndexFromPort(entry->port);
	m_fdbSize++;
}


//
// Find MAC address
// Returns interface index or 0 if MAC address not found
//

DWORD ForwardingDatabase::findMacAddress(const BYTE *macAddr)
{
	for(int i = 0; i < m_fdbSize; i++)
		if (!memcmp(macAddr, m_fdb[i].macAddr, MAC_ADDR_LENGTH))
			return m_fdb[i].ifIndex;
	return 0;
}


//
// Check if port has only one MAC in FDB
//

bool ForwardingDatabase::isSingleMacOnPort(DWORD ifIndex)
{
	int count = 0;
	for(int i = 0; i < m_fdbSize; i++)
		if (m_fdb[i].ifIndex == ifIndex)
		{
			count++;
			if (count > 1)
				return false;
		}
	return count == 1;
}


//
// FDB walker's callback
//

static DWORD FDBHandler(DWORD dwVersion, SNMP_Variable *pVar, SNMP_Transport *pTransport, void *arg)
{
   SNMP_ObjectId *pOid = pVar->GetName();
   TCHAR szOid[MAX_OID_LEN * 4], szSuffix[MAX_OID_LEN * 4];
   SNMPConvertOIDToText(pOid->Length() - 11, (DWORD *)&(pOid->GetValue())[11], szSuffix, MAX_OID_LEN * 4);

	// Get port number
   SNMP_PDU *pRqPDU = new SNMP_PDU(SNMP_GET_REQUEST, SnmpNewRequestId(), dwVersion);

	_tcscpy(szOid, _T(".1.3.6.1.2.1.17.4.3.1.2"));	// Port number
   _tcscat(szOid, szSuffix);
	pRqPDU->bindVariable(new SNMP_Variable(szOid));

   SNMP_PDU *pRespPDU;
   DWORD rcc = pTransport->doRequest(pRqPDU, &pRespPDU, g_dwSNMPTimeout, 3);
	delete pRqPDU;

	if (rcc == SNMP_ERR_SUCCESS)
   {
		int port = pRespPDU->getVariable(0)->GetValueAsInt();
		if (port > 0)
		{
			FDB_ENTRY entry;

			memset(&entry, 0, sizeof(FDB_ENTRY));
			entry.port = (DWORD)port;
			pVar->getRawValue(entry.macAddr, MAC_ADDR_LENGTH);
			Node *node = FindNodeByMAC(entry.macAddr);
			entry.nodeObject = (node != NULL) ? node->Id() : 0;
			((ForwardingDatabase *)arg)->addEntry(&entry);
		}
      delete pRespPDU;
	}

	return rcc;
}


//
// dot1dBasePortTable walker's callback
//

static DWORD Dot1dPortTableHandler(DWORD dwVersion, SNMP_Variable *pVar, SNMP_Transport *pTransport, void *arg)
{
   SNMP_ObjectId *pOid = pVar->GetName();
   TCHAR szOid[MAX_OID_LEN * 4], szSuffix[MAX_OID_LEN * 4];
   SNMPConvertOIDToText(pOid->Length() - 11, (DWORD *)&(pOid->GetValue())[11], szSuffix, MAX_OID_LEN * 4);

	// Get interface index
   SNMP_PDU *pRqPDU = new SNMP_PDU(SNMP_GET_REQUEST, SnmpNewRequestId(), dwVersion);

	_tcscpy(szOid, _T(".1.3.6.1.2.1.17.1.4.1.2"));	// Interface index
   _tcscat(szOid, szSuffix);
	pRqPDU->bindVariable(new SNMP_Variable(szOid));

   SNMP_PDU *pRespPDU;
   DWORD rcc = pTransport->doRequest(pRqPDU, &pRespPDU, g_dwSNMPTimeout, 3);
	delete pRqPDU;

	if (rcc == SNMP_ERR_SUCCESS)
   {
		PORT_MAPPING_ENTRY pm;

		pm.port = pVar->GetValueAsUInt();
		pm.ifIndex = pRespPDU->getVariable(0)->GetValueAsInt();
		((ForwardingDatabase *)arg)->addPortMapping(&pm);
      delete pRespPDU;
	}

	return rcc;
}


//
// Get switch forwarding database from node
//

ForwardingDatabase *GetSwitchForwardingDatabase(Node *node)
{
	if (!node->isBridge())
		return NULL;

	ForwardingDatabase *fdb = new ForwardingDatabase();
	node->CallSnmpEnumerate(_T(".1.3.6.1.2.1.17.1.4.1.1"), Dot1dPortTableHandler, fdb);
	node->CallSnmpEnumerate(_T(".1.3.6.1.2.1.17.4.3.1.1"), FDBHandler, fdb);
	return fdb;
}
