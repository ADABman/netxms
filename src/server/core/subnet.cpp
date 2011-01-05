/* 
** NetXMS - Network Management System
** Copyright (C) 2003, 2004 Victor Kirhenshtein
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
** $module: subnet.cpp
**
**/

#include "nxcore.h"


//
// Subnet class default constructor
//

Subnet::Subnet()
       :NetObj()
{
   m_dwIpNetMask = 0;
   m_dwZoneGUID = 0;
	m_bSyntheticMask = false;
}


//
// Subnet class constructor
//

Subnet::Subnet(DWORD dwAddr, DWORD dwNetMask, DWORD dwZone, bool bSyntheticMask)
{
   TCHAR szBuffer[32];

   m_dwIpAddr = dwAddr;
   m_dwIpNetMask = dwNetMask;
   _sntprintf(m_szName, MAX_OBJECT_NAME, _T("%s/%d"), IpToStr(dwAddr, szBuffer), BitsInMask(dwNetMask));
   m_dwZoneGUID = dwZone;
	m_bSyntheticMask = bSyntheticMask;
}


//
// Subnet class destructor
//

Subnet::~Subnet()
{
}


//
// Create object from database data
//

BOOL Subnet::CreateFromDB(DWORD dwId)
{
   TCHAR szQuery[256];
   DB_RESULT hResult;

   m_dwId = dwId;

   if (!LoadCommonProperties())
      return FALSE;

   _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT ip_addr,ip_netmask,zone_guid,synthetic_mask FROM subnets WHERE id=%d"), dwId);
   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult == 0)
      return FALSE;     // Query failed

   if (DBGetNumRows(hResult) == 0)
   {
      DBFreeResult(hResult);
      return FALSE;
   }

   m_dwIpAddr = DBGetFieldIPAddr(hResult, 0, 0);
   m_dwIpNetMask = DBGetFieldIPAddr(hResult, 0, 1);
   m_dwZoneGUID = DBGetFieldULong(hResult, 0, 2);
	m_bSyntheticMask = DBGetFieldLong(hResult, 0, 3) ? true : false;

   DBFreeResult(hResult);

   // Load access list
   LoadACLFromDB();

   return TRUE;
}


//
// Save subnet object to database
//

BOOL Subnet::SaveToDB(DB_HANDLE hdb)
{
   TCHAR szQuery[1024], szIpAddr[16], szNetMask[16];
   DB_RESULT hResult;
   DWORD i;
   BOOL bNewObject = TRUE;

   // Lock object's access
   LockData();

   SaveCommonProperties(hdb);

   // Check for object's existence in database
   _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("SELECT id FROM subnets WHERE id=%d"), m_dwId);
   hResult = DBSelect(hdb, szQuery);
   if (hResult != 0)
   {
      if (DBGetNumRows(hResult) > 0)
         bNewObject = FALSE;
      DBFreeResult(hResult);
   }

   // Form and execute INSERT or UPDATE query
   if (bNewObject)
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), 
		           _T("INSERT INTO subnets (id,ip_addr,ip_netmask,zone_guid,synthetic_mask) VALUES (%d,'%s','%s',%d,%d)"),
                 m_dwId, IpToStr(m_dwIpAddr, szIpAddr),
					  IpToStr(m_dwIpNetMask, szNetMask), m_dwZoneGUID, m_bSyntheticMask ? 1 : 0);
   else
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), 
		           _T("UPDATE subnets SET ip_addr='%s',ip_netmask='%s',zone_guid=%d,synthetic_mask=%d WHERE id=%d"),
                 IpToStr(m_dwIpAddr, szIpAddr),
					  IpToStr(m_dwIpNetMask, szNetMask), m_dwZoneGUID, m_bSyntheticMask ? 1 : 0, m_dwId);
   DBQuery(hdb, szQuery);

   // Update node to subnet mapping
   _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM nsmap WHERE subnet_id=%d"), m_dwId);
   DBQuery(hdb, szQuery);
   LockChildList(FALSE);
   for(i = 0; i < m_dwChildCount; i++)
   {
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("INSERT INTO nsmap (subnet_id,node_id) VALUES (%d,%d)"), m_dwId, m_pChildList[i]->Id());
      DBQuery(hdb, szQuery);
   }
   UnlockChildList();

   // Save access list
   SaveACLToDB(hdb);

   // Clear modifications flag and unlock object
   m_bIsModified = FALSE;
   UnlockData();

   return TRUE;
}


//
// Delete subnet object from database
//

BOOL Subnet::DeleteFromDB(void)
{
   TCHAR szQuery[256];
   BOOL bSuccess;

   bSuccess = NetObj::DeleteFromDB();
   if (bSuccess)
   {
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM subnets WHERE id=%d"), m_dwId);
      QueueSQLRequest(szQuery);
      _sntprintf(szQuery, sizeof(szQuery) / sizeof(TCHAR), _T("DELETE FROM nsmap WHERE subnet_id=%d"), m_dwId);
      QueueSQLRequest(szQuery);
   }
   return bSuccess;
}


//
// Create CSCP message with object's data
//

void Subnet::CreateMessage(CSCPMessage *pMsg)
{
   NetObj::CreateMessage(pMsg);
   pMsg->SetVariable(VID_IP_NETMASK, m_dwIpNetMask);
   pMsg->SetVariable(VID_ZONE_GUID, m_dwZoneGUID);
	pMsg->SetVariable(VID_SYNTHETIC_MASK, (WORD)(m_bSyntheticMask ? 1 : 0));
}


//
// Set correct netmask for subnet
//

void Subnet::setCorrectMask(DWORD dwAddr, DWORD dwMask)
{
	TCHAR szName[128], szBuffer[32];

	LockData();
	
	// Check if name is default
	_sntprintf(szName, 128, _T("%s/%d"), IpToStr(m_dwIpAddr, szBuffer), BitsInMask(m_dwIpNetMask));
	if (!_tcsicmp(szName, m_szName))
	{
		// Change name
		_sntprintf(m_szName, 128, _T("%s/%d"), IpToStr(dwAddr, szBuffer), BitsInMask(dwMask));
	}
	
	m_dwIpAddr = dwAddr;
	m_dwIpNetMask = dwMask;
	m_bSyntheticMask = false;

	Modify();
	UnlockData();
}
