/* 
** NetXMS - Network Management System
** Copyright (C) 2003, 2004, 2005, 2006 Victor Kirhenshtein
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
** File: vpnconn.cpp
**
**/

#include "nxcore.h"


//
// Default constructor for VPNConnector object
//

VPNConnector::VPNConnector()
             :NetObj()
{
   m_dwPeerGateway = 0;
   m_dwNumLocalNets = 0;
   m_dwNumRemoteNets = 0;
   m_pLocalNetList = NULL;
   m_pRemoteNetList = NULL;
}


//
// Constructor for new VPNConnector object
//

VPNConnector::VPNConnector(BOOL bIsHidden)
             :NetObj()
{
   m_dwPeerGateway = 0;
   m_dwNumLocalNets = 0;
   m_dwNumRemoteNets = 0;
   m_pLocalNetList = NULL;
   m_pRemoteNetList = NULL;
   m_bIsHidden = bIsHidden;
}


//
// VPNConnector class destructor
//

VPNConnector::~VPNConnector()
{
   safe_free(m_pLocalNetList);
   safe_free(m_pRemoteNetList);
}


//
// Create object from database data
//

BOOL VPNConnector::CreateFromDB(DWORD dwId)
{
   TCHAR szQuery[256];
   DB_RESULT hResult;
   DWORD i, dwNodeId;
   NetObj *pObject;
   BOOL bResult = FALSE;

   m_dwId = dwId;

   if (!LoadCommonProperties())
      return FALSE;

   // Load network lists
   _sntprintf(szQuery, 256, _T("SELECT ip_addr,ip_netmask FROM vpn_connector_networks WHERE vpn_id=%d AND network_type=0"), m_dwId);
   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult == NULL)
      return FALSE;     // Query failed
   m_dwNumLocalNets = DBGetNumRows(hResult);
   m_pLocalNetList = (IP_NETWORK *)malloc(sizeof(IP_NETWORK) * m_dwNumLocalNets);
   for(i = 0; i < m_dwNumLocalNets; i++)
   {
      m_pLocalNetList[i].dwAddr = DBGetFieldIPAddr(hResult, i, 0);
      m_pLocalNetList[i].dwMask = DBGetFieldIPAddr(hResult, i, 1);
   }
   DBFreeResult(hResult);

   _sntprintf(szQuery, 256, _T("SELECT ip_addr,ip_netmask FROM vpn_connector_networks WHERE vpn_id=%d AND network_type=1"), m_dwId);
   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult == NULL)
      return FALSE;     // Query failed
   m_dwNumRemoteNets = DBGetNumRows(hResult);
   m_pRemoteNetList = (IP_NETWORK *)malloc(sizeof(IP_NETWORK) * m_dwNumRemoteNets);
   for(i = 0; i < m_dwNumRemoteNets; i++)
   {
      m_pRemoteNetList[i].dwAddr = DBGetFieldIPAddr(hResult, i, 0);
      m_pRemoteNetList[i].dwMask = DBGetFieldIPAddr(hResult, i, 1);
   }
   DBFreeResult(hResult);
   
   // Load custom properties
   _sntprintf(szQuery, 256, _T("SELECT node_id,peer_gateway FROM vpn_connectors WHERE id=%d"), dwId);
   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult == NULL)
      return FALSE;     // Query failed

   if (DBGetNumRows(hResult) != 0)
   {
      dwNodeId = DBGetFieldULong(hResult, 0, 0);
      m_dwPeerGateway = DBGetFieldULong(hResult, 0, 1);

      // Link VPN connector to node
      if (!m_bIsDeleted)
      {
         pObject = FindObjectById(dwNodeId);
         if (pObject == NULL)
         {
            WriteLog(MSG_INVALID_NODE_ID_EX, EVENTLOG_ERROR_TYPE,
                     "dds", dwId, dwNodeId, "VPN connector");
         }
         else if (pObject->Type() != OBJECT_NODE)
         {
            WriteLog(MSG_NODE_NOT_NODE, EVENTLOG_ERROR_TYPE, "dd", dwId, dwNodeId);
         }
         else
         {
            pObject->AddChild(this);
            AddParent(pObject);
            bResult = TRUE;
         }
      }
      else
      {
         bResult = TRUE;
      }
   }

   DBFreeResult(hResult);

   // Load access list
   LoadACLFromDB();

   return bResult;
}


//
// Save interface object to database
//

BOOL VPNConnector::SaveToDB(DB_HANDLE hdb)
{
   char szQuery[1024], szIpAddr[16], szNetMask[16];
   BOOL bNewObject = TRUE;
   Node *pNode;
   DWORD i, dwNodeId;
   DB_RESULT hResult;

   // Lock object's access
   LockData();

   SaveCommonProperties(hdb);

   // Check for object's existence in database
   sprintf(szQuery, "SELECT id FROM vpn_connectors WHERE id=%d", m_dwId);
   hResult = DBSelect(hdb, szQuery);
   if (hResult != 0)
   {
      if (DBGetNumRows(hResult) > 0)
         bNewObject = FALSE;
      DBFreeResult(hResult);
   }

   // Determine owning node's ID
   pNode = GetParentNode();
   if (pNode != NULL)
      dwNodeId = pNode->Id();
   else
      dwNodeId = 0;

   // Form and execute INSERT or UPDATE query
   if (bNewObject)
      sprintf(szQuery, "INSERT INTO vpn_connectors (id,node_id,peer_gateway) "
                       "VALUES (%d,%d,%d)",
              m_dwId, dwNodeId, m_dwPeerGateway);
   else
      sprintf(szQuery, "UPDATE vpn_connectors SET node_id=%d,"
                       "peer_gateway=%d WHERE id=%d",
              dwNodeId, m_dwPeerGateway, m_dwId);
   DBQuery(hdb, szQuery);

   // Save network list
   sprintf(szQuery, "DELETE FROM vpn_connector_networks WHERE vpn_id=%d", m_dwId);
   DBQuery(hdb, szQuery);
   for(i = 0; i < m_dwNumLocalNets; i++)
   {
      sprintf(szQuery, "INSERT INTO vpn_connector_networks (vpn_id,network_type,"
                       "ip_addr,ip_netmask) VALUES (%d,0,'%s','%s')",
              m_dwId, IpToStr(m_pLocalNetList[i].dwAddr, szIpAddr),
              IpToStr(m_pLocalNetList[i].dwMask, szNetMask));
      DBQuery(hdb, szQuery);
   }
   for(i = 0; i < m_dwNumRemoteNets; i++)
   {
      sprintf(szQuery, "INSERT INTO vpn_connector_networks (vpn_id,network_type,"
                       "ip_addr,ip_netmask) VALUES (%d,1,'%s','%s')",
              m_dwId, IpToStr(m_pRemoteNetList[i].dwAddr, szIpAddr),
              IpToStr(m_pRemoteNetList[i].dwMask, szNetMask));
      DBQuery(hdb, szQuery);
   }

   // Save access list
   SaveACLToDB(hdb);

   // Clear modifications flag and unlock object
   m_bIsModified = FALSE;
   UnlockData();

   return TRUE;
}


//
// Delete VPN connector object from database
//

BOOL VPNConnector::DeleteFromDB(void)
{
   char szQuery[128];
   BOOL bSuccess;

   bSuccess = NetObj::DeleteFromDB();
   if (bSuccess)
   {
      sprintf(szQuery, "DELETE FROM vpn_connectors WHERE id=%d", m_dwId);
      QueueSQLRequest(szQuery);
      sprintf(szQuery, "DELETE FROM vpn_connector_networks WHERE vpn_id=%d", m_dwId);
      QueueSQLRequest(szQuery);
   }
   return bSuccess;
}


//
// Get connector's parent node
//

Node *VPNConnector::GetParentNode(void)
{
   DWORD i;
   Node *pNode = NULL;

   LockParentList(FALSE);
   for(i = 0; i < m_dwParentCount; i++)
      if (m_pParentList[i]->Type() == OBJECT_NODE)
      {
         pNode = (Node *)m_pParentList[i];
         break;
      }
   UnlockParentList();
   return pNode;
}


//
// Create CSCP message with object's data
//

void VPNConnector::CreateMessage(CSCPMessage *pMsg)
{
   DWORD i, dwId;

   NetObj::CreateMessage(pMsg);
   pMsg->SetVariable(VID_PEER_GATEWAY, m_dwPeerGateway);
   pMsg->SetVariable(VID_NUM_LOCAL_NETS, m_dwNumLocalNets);
   pMsg->SetVariable(VID_NUM_REMOTE_NETS, m_dwNumRemoteNets);

   for(i = 0, dwId = VID_VPN_NETWORK_BASE; i < m_dwNumLocalNets; i++)
   {
      pMsg->SetVariable(dwId++, m_pLocalNetList[i].dwAddr);
      pMsg->SetVariable(dwId++, m_pLocalNetList[i].dwMask);
   }

   for(i = 0; i < m_dwNumRemoteNets; i++)
   {
      pMsg->SetVariable(dwId++, m_pRemoteNetList[i].dwAddr);
      pMsg->SetVariable(dwId++, m_pRemoteNetList[i].dwMask);
   }
}


//
// Modify object from message
//

DWORD VPNConnector::ModifyFromMessage(CSCPMessage *pRequest, BOOL bAlreadyLocked)
{
   DWORD i, dwId;

   if (!bAlreadyLocked)
      LockData();

   // Peer gateway
   if (pRequest->IsVariableExist(VID_PEER_GATEWAY))
      m_dwPeerGateway = pRequest->GetVariableLong(VID_PEER_GATEWAY);

   // Network list
   if ((pRequest->IsVariableExist(VID_NUM_LOCAL_NETS)) &&
       (pRequest->IsVariableExist(VID_NUM_REMOTE_NETS)))
   {
      m_dwNumLocalNets = pRequest->GetVariableLong(VID_NUM_LOCAL_NETS);
      if (m_dwNumLocalNets > 0)
      {
         m_pLocalNetList = (IP_NETWORK *)realloc(m_pLocalNetList, sizeof(IP_NETWORK) * m_dwNumLocalNets);
         for(i = 0, dwId = VID_VPN_NETWORK_BASE; i < m_dwNumLocalNets; i++)
         {
            m_pLocalNetList[i].dwAddr = pRequest->GetVariableLong(dwId++);
            m_pLocalNetList[i].dwMask = pRequest->GetVariableLong(dwId++);
         }
      }
      else
      {
         safe_free(m_pLocalNetList);
         m_pLocalNetList = NULL;
      }

      m_dwNumRemoteNets = pRequest->GetVariableLong(VID_NUM_REMOTE_NETS);
      if (m_dwNumRemoteNets > 0)
      {
         m_pRemoteNetList = (IP_NETWORK *)realloc(m_pRemoteNetList, sizeof(IP_NETWORK) * m_dwNumRemoteNets);
         for(i = 0; i < m_dwNumRemoteNets; i++)
         {
            m_pRemoteNetList[i].dwAddr = pRequest->GetVariableLong(dwId++);
            m_pRemoteNetList[i].dwMask = pRequest->GetVariableLong(dwId++);
         }
      }
      else
      {
         safe_free(m_pRemoteNetList);
         m_pRemoteNetList = NULL;
      }
   }

   return NetObj::ModifyFromMessage(pRequest, TRUE);
}


//
// Check if given address falls into one of the local nets
//

BOOL VPNConnector::IsLocalAddr(DWORD dwIpAddr)
{
   DWORD i;
   BOOL bResult = FALSE;

   LockData();

   for(i = 0; i < m_dwNumLocalNets; i++)
      if ((dwIpAddr & m_pLocalNetList[i].dwMask) == m_pLocalNetList[i].dwAddr)
      {
         bResult = TRUE;
         break;
      }

   UnlockData();
   return bResult;
}


//
// Check if given address falls into one of the remote nets
//

BOOL VPNConnector::IsRemoteAddr(DWORD dwIpAddr)
{
   DWORD i;
   BOOL bResult = FALSE;

   LockData();

   for(i = 0; i < m_dwNumRemoteNets; i++)
      if ((dwIpAddr & m_pRemoteNetList[i].dwMask) == m_pRemoteNetList[i].dwAddr)
      {
         bResult = TRUE;
         break;
      }

   UnlockData();
   return bResult;
}


//
// Get address of peer gateway
//

DWORD VPNConnector::GetPeerGatewayAddr(void)
{
   NetObj *pObject;
   DWORD dwAddr = 0;

   pObject = FindObjectById(m_dwPeerGateway);
   if (pObject != NULL)
   {
      if (pObject->Type() == OBJECT_NODE)
         dwAddr = pObject->IpAddr();
   }
   return dwAddr;
}
