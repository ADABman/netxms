/* 
** NetXMS - Network Management System
** Driver for Avaya ERS 8xxx switches (former Nortel/Bay Networks Passport)
** Copyright (C) 2003-2011 Victor Kirhenshtein
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
** File: vlan.cpp
**
**/

#include "ers8000.h"


//
// VLAN information structure
//

struct VLAN_INFO
{
   TCHAR szName[MAX_OBJECT_NAME];
   DWORD dwVlanId;
   DWORD dwIfIndex;
   BYTE bMacAddr[MAC_ADDR_LENGTH];
};

struct VLAN_LIST
{
   DWORD dwNumVlans;
   VLAN_INFO *pList;
};


/**
 * Handler for VLAN enumeration on Passport
 */
static DWORD HandlerVlanList(DWORD dwVersion, SNMP_Variable *pVar,
                             SNMP_Transport *pTransport, void *pArg)
{
   DWORD dwIndex, oidName[MAX_OID_LEN], dwNameLen, dwResult;
   VLAN_LIST *pVlanList = (VLAN_LIST *)pArg;
   BYTE szBuffer[256];

   dwNameLen = pVar->GetName()->Length();

   // Extend VLAN list and set ID of new VLAN
   dwIndex = pVlanList->dwNumVlans;
   pVlanList->dwNumVlans++;
   pVlanList->pList = (VLAN_INFO *)realloc(pVlanList->pList, sizeof(VLAN_INFO) * pVlanList->dwNumVlans);
   pVlanList->pList[dwIndex].dwVlanId = pVar->GetValueAsUInt();

   // Get VLAN name
   memcpy(oidName, pVar->GetName()->GetValue(), dwNameLen * sizeof(DWORD));
   oidName[dwNameLen - 2] = 2;
   dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen, 
                      pVlanList->pList[dwIndex].szName, MAX_OBJECT_NAME, 0);
   if (dwResult != SNMP_ERR_SUCCESS)
      return dwResult;

   // Get VLAN interface index
   oidName[dwNameLen - 2] = 6;
   dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen, 
                      &pVlanList->pList[dwIndex].dwIfIndex, sizeof(DWORD), 0);
   if (dwResult != SNMP_ERR_SUCCESS)
      return dwResult;

   // Get VLAN MAC address
   oidName[dwNameLen - 2] = 19;
   memset(pVlanList->pList[dwIndex].bMacAddr, 0, MAC_ADDR_LENGTH);
   memset(szBuffer, 0, MAC_ADDR_LENGTH);
   dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen, szBuffer, 256, SG_RAW_RESULT);
   if (dwResult == SNMP_ERR_SUCCESS)
      memcpy(pVlanList->pList[dwIndex].bMacAddr, szBuffer, MAC_ADDR_LENGTH);
   return dwResult;
}

/**
 * Handler for VLAN enumeration on Passport
 */
static DWORD HandlerPassportIfList(DWORD dwVersion, SNMP_Variable *pVar,
                                  SNMP_Transport *pTransport, void *pArg)
{
   InterfaceList *pIfList = (InterfaceList *)pArg;
   VLAN_LIST *pVlanList = (VLAN_LIST *)pIfList->getData();
   DWORD oidName[MAX_OID_LEN], dwVlanIndex, dwIfIndex, dwNameLen, dwResult;

   dwIfIndex = pVar->GetValueAsUInt();
   for(dwVlanIndex = 0; dwVlanIndex < pVlanList->dwNumVlans; dwVlanIndex++)
      if (pVlanList->pList[dwVlanIndex].dwIfIndex == dwIfIndex)
         break;

   // Create new interface only if we have VLAN with same interface index
   if (dwVlanIndex < pVlanList->dwNumVlans)
   {
		NX_INTERFACE_INFO iface;

		memset(&iface, 0, sizeof(NX_INTERFACE_INFO));
      iface.dwIndex = dwIfIndex;
      _tcscpy(iface.szName, pVlanList->pList[dwVlanIndex].szName);
      iface.dwType = IFTYPE_OTHER;
      memcpy(iface.bMacAddr, pVlanList->pList[dwVlanIndex].bMacAddr, MAC_ADDR_LENGTH);
      
      dwNameLen = pVar->GetName()->Length();

      // Get IP address
      memcpy(oidName, pVar->GetName()->GetValue(), dwNameLen * sizeof(DWORD));
      oidName[dwNameLen - 6] = 2;
      dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen,
                         &iface.dwIpAddr, sizeof(DWORD), 0);

      if (dwResult == SNMP_ERR_SUCCESS)
      {
         // Get netmask
         oidName[dwNameLen - 6] = 3;
         dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen,
                            &iface.dwIpNetMask, sizeof(DWORD), 0);
      }

		pIfList->add(&iface);
   }
   else
   {
      dwResult = SNMP_ERR_SUCCESS;  // Ignore interfaces without matching VLANs
   }
   return dwResult;
}

/**
 * Get list of VLAN interfaces from Nortel Passport 8000/Accelar switch
 *
 * @param pTransport SNMP transport
 * @param pIfList interface list to be populated
 */
void GetVLANInterfaces(SNMP_Transport *pTransport, InterfaceList *pIfList)
{
   VLAN_LIST vlanList;

   // Get VLAN list
   memset(&vlanList, 0, sizeof(VLAN_LIST));
   SnmpEnumerate(pTransport->getSnmpVersion(), pTransport, _T(".1.3.6.1.4.1.2272.1.3.2.1.1"), HandlerVlanList, &vlanList, FALSE);

   // Get interfaces
   pIfList->setData(&vlanList);
   SnmpEnumerate(pTransport->getSnmpVersion(), pTransport, _T(".1.3.6.1.4.1.2272.1.8.2.1.1"), 
                 HandlerPassportIfList, pIfList, FALSE);
   safe_free(vlanList.pList);
}
