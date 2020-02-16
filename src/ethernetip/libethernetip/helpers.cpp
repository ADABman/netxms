/*
** NetXMS - Network Management System
** Ethernet/IP support library
** Copyright (C) 2003-2020 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: helpers.cpp
**
**/

#include "libethernetip.h"

/**
 * Send request to device and wait for response
 */
static CIP_Message *DoRequest(SOCKET s, const CIP_Message &request, uint32_t timeout, EtherNetIP_CallStatus *callStatus, EtherNetIP_Status *eipStatus)
{
   size_t bytes = request.getSize();
   if (SendEx(s, request.getBytes(), bytes, 0, nullptr) != bytes)
   {
      *callStatus = EIP_CALL_COMM_ERROR;
      return nullptr;
   }

   EtherNetIP_MessageReceiver receiver(s);
   CIP_Message *response = receiver.readMessage(timeout);
   if (response == nullptr)
   {
      *callStatus = EIP_CALL_TIMEOUT;
      return nullptr;
   }

   if (response->getCommand() != request.getCommand())
   {
      *callStatus = EIP_CALL_BAD_RESPONSE;
      delete response;
      return nullptr;
   }

   if (response->getStatus() != EIP_STATUS_SUCCESS)
   {
      *callStatus = EIP_CALL_DEVICE_ERROR;
      *eipStatus = response->getStatus();
      delete response;
      return nullptr;
   }

   return response;
}

/**
 * Helper function for reading device identity via Ethernet/IP
 */
CIP_Identity LIBETHERNETIP_EXPORTABLE *EtherNetIP_ListIdentity(const InetAddress& addr, uint16_t port, uint32_t timeout, EtherNetIP_CallStatus *callStatus, EtherNetIP_Status *eipStatus)
{
   SOCKET s = ConnectToHost(addr, port, timeout);
   if (s == INVALID_SOCKET)
   {
      *callStatus = EIP_CALL_CONNECT_FAILED;
      return nullptr;
   }

   CIP_Message request(CIP_LIST_IDENTITY, 0);
   CIP_Message *response = DoRequest(s, request, timeout, callStatus, eipStatus);
   shutdown(s, SHUT_RDWR);
   closesocket(s);
   if (response == nullptr)
      return nullptr;

   CIP_Identity *identity = nullptr;
   *callStatus = EIP_CALL_BAD_RESPONSE;   // Default value for missing identity item

   CPF_Item item;
   while(response->nextItem(&item))
   {
      if (item.type != 0x0C)
         continue;

      *callStatus = EIP_CALL_SUCCESS;

      TCHAR productName[128];
      size_t stateFieldOffset;
      if (response->readDataAsLengthPrefixString(item.offset + 32, productName, 128))
      {
         stateFieldOffset = 33 + _tcslen(productName);
         Trim(productName);
      }
      else
      {
         stateFieldOffset = 33;
         productName[0] = 0;
      }

      identity = static_cast<CIP_Identity*>(MemAlloc(sizeof(CIP_Identity) + (_tcslen(productName) + 1) * sizeof(TCHAR)));
      identity->productName = reinterpret_cast<TCHAR*>(reinterpret_cast<uint8_t*>(identity) + sizeof(CIP_Identity));
      _tcscpy(identity->productName, productName);

      identity->deviceType = response->readDataAsUInt16(item.offset + 20);
      identity->ipAddress = response->readDataAsInetAddress(item.offset + 6);
      identity->productCode = response->readDataAsUInt16(item.offset + 22);
      identity->productRevisionMajor = response->readDataAsUInt8(item.offset + 24);
      identity->productRevisionMinor = response->readDataAsUInt8(item.offset + 25);
      identity->protocolVersion = response->readDataAsUInt16(item.offset);
      identity->serialNumber = response->readDataAsUInt16(item.offset + 28);
      identity->state = response->readDataAsUInt8(item.offset + stateFieldOffset);
      identity->status = response->readDataAsUInt16(item.offset + 26);
      identity->tcpPort = response->readDataAsUInt16(item.offset + 4);
      identity->vendor = response->readDataAsUInt16(item.offset + 18);

      break;
   }

   delete response;
   return identity;
}
