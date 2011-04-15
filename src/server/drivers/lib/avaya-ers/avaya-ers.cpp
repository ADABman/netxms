/* 
** NetXMS - Network Management System
** Generic driver for Avaya ERS switches (former Nortel)
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
** File: avaya-ers.cpp
**
**/

#include "avaya-ers.h"


/**
 * Get slot size from object's custom attributes. Default implementation always return constant value 64.
 *
 * @param attributes object's custom attributes
 * @return slot size
 */
DWORD AvayaERSDriver::getSlotSize(StringMap *attributes)
{
	return 64;
}

/**
 * Handler for VLAN enumeration on Avaya ERS
 */
static DWORD HandlerVlanList(DWORD dwVersion, SNMP_Variable *pVar, SNMP_Transport *pTransport, void *pArg)
{
   DWORD oidName[MAX_OID_LEN], dwResult;
   VlanList *vlanList = (VlanList *)pArg;

   DWORD dwNameLen = pVar->GetName()->Length();
	VlanInfo *vlan = new VlanInfo(pVar->GetValueAsInt(), VLAN_PRM_SLOTPORT);

   // Get VLAN name
   memcpy(oidName, pVar->GetName()->GetValue(), dwNameLen * sizeof(DWORD));
   oidName[dwNameLen - 2] = 2;
   TCHAR buffer[256];
	dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen, buffer, 256, SG_STRING_RESULT);
   if (dwResult != SNMP_ERR_SUCCESS)
	{
		delete vlan;
      return dwResult;
	}
	vlan->setName(buffer);

   // Get member ports
	// From RapidCity MIB:
	//   The string is 32 octets long, for a total of 256 bits. Each bit 
	//   corresponds to a port, as represented by its ifIndex value . When a 
	//   bit has the value one(1), the corresponding port is a member of the 
	//   set. When a bit has the value zero(0), the corresponding port is not 
	//   a member of the set. The encoding is such that the most significant 
	//   bit of octet #1 corresponds to ifIndex 0, while the least significant 
	//   bit of octet #32 corresponds to ifIndex 255." 
	// Note: on newer devices port list can be longer
   oidName[dwNameLen - 2] = 12;
	BYTE portMask[256];
	memset(portMask, 0, sizeof(portMask));
   dwResult = SnmpGet(dwVersion, pTransport, NULL, oidName, dwNameLen, portMask, 256, SG_RAW_RESULT);
   if (dwResult != SNMP_ERR_SUCCESS)
	{
		delete vlan;
      return dwResult;
	}

	DWORD slotSize = CAST_FROM_POINTER(vlanList->getData(), DWORD);
	DWORD ifIndex = 0;

	for(int i = 0; i < 256; i++)
	{
		BYTE mask = 0x80;
		while(mask > 0)
		{
			if (portMask[i] & mask)
			{
				DWORD slot = ifIndex / slotSize + 1;
				DWORD port = ifIndex % slotSize;
				vlan->add(slot, port);
			}
			mask >>= 1;
			ifIndex++;
		}
	}

	vlanList->add(vlan);
   return SNMP_ERR_SUCCESS;
}

/**
 * Get VLANs 
 */
VlanList *AvayaERSDriver::getVlans(SNMP_Transport *snmp, StringMap *attributes)
{
	VlanList *list = new VlanList();
	DWORD slotSize = getSlotSize(attributes);
	list->setData(CAST_TO_POINTER(slotSize, void *));
	if (SnmpEnumerate(snmp->getSnmpVersion(), snmp, _T(".1.3.6.1.4.1.2272.1.3.2.1.1"), HandlerVlanList, list, FALSE) != SNMP_ERR_SUCCESS)
	{
		delete_and_null(list);
	}
	return list;
}

/**
 * DLL entry point
 */
#ifdef _WIN32

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(hInstance);
	return TRUE;
}

#endif
