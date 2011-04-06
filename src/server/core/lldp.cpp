/* 
** NetXMS - Network Management System
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
** File: lldp.cpp
**
**/

#include "nxcore.h"


//
// Find remote interface
//

static Interface *FindRemoteInterface(Node *node, DWORD idType, BYTE *id, size_t idLen)
{
	TCHAR ifName[130];
	Interface *ifc;

	switch(idType)
	{
		case 3:	// MAC address
			return node->findInterfaceByMAC(id);
		case 4:	// Network address
			if (id[0] == 1)	// IPv4
			{
				DWORD ipAddr;
				memcpy(&ipAddr, &id[1], sizeof(DWORD));
				return node->findInterfaceByIP(ntohl(ipAddr));
			}
			return NULL;
		case 5:	// Interface name
#ifdef UNICODE
			MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, (char *)id, idLen, ifName, 128);
			ifName[min(idLen, 127)] = 0;
#else
			{
				int len = min(idLen, 127);
				memcpy(ifName, id, len);
				ifName[len] = 0;
			}
#endif
			ifc = node->findInterface(ifName);	/* TODO: find by cached ifName value */
			if ((ifc == NULL) && !_tcsncmp(node->getObjectId(), _T(".1.3.6.1.4.1.1916.2"), 19))
			{
				// Hack for Extreme Networks switches
				// Must be moved into driver
				memmove(&ifName[2], ifName, (_tcslen(ifName) + 1) * sizeof(TCHAR));
				ifName[0] = _T('1');
				ifName[1] = _T(':');
				ifc = node->findInterface(ifName);
			}
			return ifc;
		default:
			return NULL;
	}
}


//
// Topology table walker's callback for LLDP topology table
//

static DWORD LLDPTopoHandler(DWORD snmpVersion, SNMP_Variable *var, SNMP_Transport *transport, void *arg)
{
	LinkLayerNeighbors *nbs = (LinkLayerNeighbors *)arg;
	Node *node = (Node *)nbs->getData();
	SNMP_ObjectId *oid = var->GetName();

	// Get additional info for current record
	DWORD newOid[128];
	memcpy(newOid, oid->GetValue(), oid->Length() * sizeof(DWORD));
   SNMP_PDU *pRqPDU = new SNMP_PDU(SNMP_GET_REQUEST, SnmpNewRequestId(), snmpVersion);

	newOid[oid->Length() - 4] = 4;	// lldpRemChassisIdSubtype
	pRqPDU->bindVariable(new SNMP_Variable(newOid, oid->Length()));

	newOid[oid->Length() - 4] = 7;	// lldpRemPortId
	pRqPDU->bindVariable(new SNMP_Variable(newOid, oid->Length()));

	newOid[oid->Length() - 4] = 6;	// lldpRemPortIdSubtype
	pRqPDU->bindVariable(new SNMP_Variable(newOid, oid->Length()));

	SNMP_PDU *pRespPDU;
   DWORD rcc = transport->doRequest(pRqPDU, &pRespPDU, g_dwSNMPTimeout, 3);
	delete pRqPDU;
	if (rcc == SNMP_ERR_SUCCESS)
   {
		// Build LLDP ID for remote system
		TCHAR remoteId[256];
		_sntprintf(remoteId, 256, _T("%d@"), (int)pRespPDU->getVariable(0)->GetValueAsInt());
		BinToStr(var->GetValue(), var->GetValueLength(), &remoteId[_tcslen(remoteId)]);

		Node *remoteNode = FindNodeByLLDPId(remoteId);
		if (remoteNode != NULL)
		{
			BYTE remoteIfId[1024];
			size_t remoteIfIdLen = pRespPDU->getVariable(1)->getRawValue(remoteIfId, 1024);
			Interface *ifRemote = FindRemoteInterface(remoteNode, pRespPDU->getVariable(2)->GetValueAsUInt(), remoteIfId, remoteIfIdLen);

			LL_NEIGHBOR_INFO info;

			info.objectId = remoteNode->Id();
			info.ifRemote = (ifRemote != NULL) ? ifRemote->getIfIndex() : 0;
			info.isPtToPt = true;
			info.protocol = LL_PROTO_LLDP;

			// Index to lldpRemTable is lldpRemTimeMark, lldpRemLocalPortNum, lldpRemIndex
			DWORD localPort = oid->GetValue()[oid->Length() - 2];

			// Determine interface index from local port number. It can be
			// either ifIndex or dot1dBasePort, as described in LLDP MIB:
			//         A port number has no mandatory relationship to an
			//         InterfaceIndex object (of the interfaces MIB, IETF RFC 2863).
			//         If the LLDP agent is a IEEE 802.1D, IEEE 802.1Q bridge, the
			//         LldpPortNumber will have the same value as the dot1dBasePort
			//         object (defined in IETF RFC 1493) associated corresponding
			//         bridge port.  If the system hosting LLDP agent is not an
			//         IEEE 802.1D or an IEEE 802.1Q bridge, the LldpPortNumber
			//         will have the same value as the corresponding interface's
			//         InterfaceIndex object.
			if (node->isBridge())
			{
				Interface *localIf = node->findBridgePort(localPort);
				if (localIf != NULL)
					info.ifLocal = localIf->getIfIndex();
			}
			else
			{
				info.ifLocal = localPort;
			}

			nbs->addConnection(&info);
		}
	}
	return SNMP_ERR_SUCCESS;
}


//
// Add LLDP-discovered neighbors
//

void AddLLDPNeighbors(Node *node, LinkLayerNeighbors *nbs)
{
	if (!(node->getFlags() & NF_IS_LLDP))
		return;

	DbgPrintf(5, _T("LLDP: collecting topology information for node %s [%d]"), node->Name(), node->Id());
	nbs->setData(node);
	node->CallSnmpEnumerate(_T(".1.0.8802.1.1.2.1.4.1.1.5"), LLDPTopoHandler, nbs);
	DbgPrintf(5, _T("LLDP: finished collecting topology information for node %s [%d]"), node->Name(), node->Id());
}
