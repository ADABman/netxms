/*
** NetXMS subagent for SunOS/Solaris
** Copyright (C) 2004 Victor Kirhenshtein
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
** $module: net.cpp
**
**/

#include "sunos_subagent.h"
#include <net/if.h>
#include <sys/sockio.h>


//
// Determine interface type by it's name
//

static int InterfaceTypeFromName(char *pszName)
{
   int iType = 0;

   switch(pszName[0])
   {
      case 'l':   // le / lo / lane (ATM LAN Emulation)
         switch(pszName[1])
         {
            case 'o':
               iType = 24;
               break;
            case 'e':
               iType = 6;
               break;
            case 'a':
               iType = 37;
               break;
         }
         break;
      case 'g':   // (gigabit ethernet card)
         iType = 6;
         break;
      case 'h':   // hme (SBus card)
      case 'e':   // eri (PCI card)
      case 'b':   // be
      case 'd':   // dmfe -- found on netra X1
         iType = 6;
         break;
      case 'f':   // fa (Fore ATM)
         iType = 37;
         break;
      case 'q':   // qe (QuadEther)/qa (Fore ATM)/qfe (QuadFastEther)
         switch(pszName[1])
         {
            case 'a':
               iType = 37;
               break;
            case 'e':
            case 'f':
               iType = 6;
               break;
         }
         break;
   }
   return iType;
}


//
// Get interface's hardware address
//

static BOOL GetInterfaceHWAddr(char *pszIfName, char *pszMacAddr)
{
   BYTE macAddr[6];
	int i;

   if (mac_addr_dlpi(pszIfName, macAddr) == 0)
	{
		for(i = 0; i < 6; i++)
			sprintf(&pszMacAddr[i << 1], "%02X", macAddr[i]);
	}
	else
	{
   	strcpy(pszMacAddr, "000000000000");
	}
   return TRUE;
}


//
// Interface list
//

LONG H_NetIfList(char *pszParam, char *pArg, NETXMS_VALUES_LIST *pValue)
{
	int nRet = SYSINFO_RC_ERROR;
	struct lifnum ln;
	struct lifconf lc;
   struct lifreq rq;
	int i, nFd;

	nFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (nFd >= 0)
	{
		ln.lifn_family = AF_INET;
		ln.lifn_flags = 0;
		if (ioctl(nFd, SIOCGLIFNUM, &ln) == 0)
		{
			lc.lifc_family = AF_INET;
			lc.lifc_flags = 0;
			lc.lifc_len = sizeof(struct lifreq) * ln.lifn_count;
			lc.lifc_buf = (caddr_t)malloc(lc.lifc_len);
			if (ioctl(nFd, SIOCGLIFCONF, &lc) == 0)
			{
				for (i = 0; i < ln.lifn_count; i++)
				{
					char szOut[256];
					char szIpAddr[32];
					char szMacAddr[32];
					int nMask;

					nRet = SYSINFO_RC_SUCCESS;

					strcpy(rq.lifr_name, lc.lifc_req[i].lifr_name);
					if (ioctl(nFd, SIOCGLIFADDR, &rq) == 0)
					{
						strncpy(szIpAddr, inet_ntoa(((struct sockaddr_in *)&rq.lifr_addr)->sin_addr), sizeof(szIpAddr));
					}
					else
					{
						nRet = SYSINFO_RC_ERROR;
						break;
					}

					if (ioctl(nFd, SIOCGLIFNETMASK, &rq) == 0)
					{
						nMask = BitsInMask(htonl(((struct sockaddr_in *)&rq.lifr_addr)->sin_addr.s_addr));
					}
					else
					{
						nRet = SYSINFO_RC_ERROR;
						break;
					}

					if (!GetInterfaceHWAddr(lc.lifc_req[i].lifr_name, szMacAddr))
					{
						nRet = SYSINFO_RC_ERROR;
						break;
					}

					if (ioctl(nFd, SIOCGLIFINDEX, &rq) == 0)
					{
						snprintf(szOut, sizeof(szOut), "%d %s/%d %d %s %s",
								   rq.lifr_index, szIpAddr, nMask,
									InterfaceTypeFromName(lc.lifc_req[i].lifr_name),
									szMacAddr, lc.lifc_req[i].lifr_name);
						NxAddResultString(pValue, szOut);
					}
					else
					{
						nRet = SYSINFO_RC_ERROR;
						break;
					}
				}
			}

			free(lc.lifc_buf);
		}
		close(nFd);
	}

	return nRet;
}


//
// Get interface description
//

LONG H_NetIfDescription(char *pszParam, char *pArg, char *pValue)
{
	char *eptr, szIfName[IF_NAMESIZE];
	int nIndex, nFd;
	struct lifreq rq;
   LONG nRet = SYSINFO_RC_ERROR;

   NxGetParameterArg(pszParam, 1, szIfName, IF_NAMESIZE);
   if (szIfName[0] == 0)
   {
      nRet = SYSINFO_RC_UNSUPPORTED;
   }
   else
   {
      // Determine if parameter is index or name
      nIndex = strtol(szIfName, &eptr, 10);
      if (*eptr == 0)
      {
         // It's an index, determine name
			if (if_indextoname(nIndex, szIfName) == NULL)
      		nRet = SYSINFO_RC_UNSUPPORTED;
      }
	}

	if (nRet != SYSINFO_RC_UNSUPPORTED)
	{
		ret_string(pValue, szIfName);
		nRet = SYSINFO_RC_SUCCESS;
	}

	return nRet;
}


//
// Get interface administrative status
//

LONG H_NetIfAdminStatus(char *pszParam, char *pArg, char *pValue)
{
	char *eptr, szIfName[IF_NAMESIZE];
	int nIndex, nFd;
	struct lifreq rq;
   LONG nRet = SYSINFO_RC_ERROR;

   NxGetParameterArg(pszParam, 1, szIfName, IF_NAMESIZE);
   if (szIfName[0] == 0)
   {
      nRet = SYSINFO_RC_UNSUPPORTED;
   }
   else
   {
      // Determine if parameter is index or name
      nIndex = strtol(szIfName, &eptr, 10);
      if (*eptr == 0)
      {
         // It's an index, determine name
			if (if_indextoname(nIndex, szIfName) == NULL)
      		nRet = SYSINFO_RC_UNSUPPORTED;
      }
	}

	if (nRet != SYSINFO_RC_UNSUPPORTED)
	{
		nFd = socket(AF_INET, SOCK_DGRAM, 0);
		if (nFd >= 0)
		{			  
			strcpy(rq.lifr_name, szIfName);
			if (ioctl(nFd, SIOCGLIFFLAGS, &rq) == 0)
			{
				ret_int(pValue, (rq.lifr_flags & IFF_UP) ? 1 : 0);
				nRet = SYSINFO_RC_SUCCESS;
			}
			close(nFd);				  
		}
	}

	return nRet;
}


//
// Get interface statistics
//

LONG H_NetInterfaceStats(char *pszParam, char *pArg, char *pValue)
{
   kstat_ctl_t *kc;
   kstat_t *kp;
   kstat_named_t *kn;
	char *ptr, *eptr, szIfName[IF_NAMESIZE], szDevice[IF_NAMESIZE];
	int nInstance, nIndex;
   LONG nRet = SYSINFO_RC_ERROR;

   NxGetParameterArg(pszParam, 1, szIfName, IF_NAMESIZE);
   if (szIfName[0] == 0)
   {
      nRet = SYSINFO_RC_UNSUPPORTED;
   }
   else
   {
      // Determine if parameter is index or name
      nIndex = strtol(szIfName, &eptr, 10);
      if (*eptr == 0)
      {
         // It's an index, determine name
			if (if_indextoname(nIndex, szIfName) == NULL)
      		nRet = SYSINFO_RC_UNSUPPORTED;
      }
	}

	if (nRet != SYSINFO_RC_UNSUPPORTED)
	{
		// Parse interface name and create device name and instance number
		for(ptr = szIfName; (*ptr != 0) && (!isdigit(*ptr)); ptr++);
		memcpy(szDevice, szIfName, ptr - szIfName);
		szDevice[(int)(ptr - szIfName)] = 0;
		for(eptr = ptr; (*eptr != 0) && isdigit(*eptr); eptr++);
		*eptr = 0;
		nInstance = atoi(ptr);

   	// Open kstat
   	kc = kstat_open();
   	if (kc != NULL)
   	{
      	kp = kstat_lookup(kc, szDevice, nInstance, szIfName);
      	if (kp != NULL)
      	{
         	if(kstat_read(kc, kp, 0) != -1)
         	{
					kn = (kstat_named_t *)kstat_data_lookup(kp, pArg);
					if (kn != NULL)
					{
						switch(kn->data_type)
						{
							case KSTAT_DATA_CHAR:
								ret_string(pValue, kn->value.c);
								break;
							case KSTAT_DATA_INT32:
								ret_int(pValue, kn->value.i32);
								break;
							case KSTAT_DATA_UINT32:
								ret_uint(pValue, kn->value.ui32);
								break;
							case KSTAT_DATA_INT64:
								ret_int64(pValue, kn->value.i64);
								break;
							case KSTAT_DATA_UINT64:
								ret_uint64(pValue, kn->value.ui64);
								break;
							case KSTAT_DATA_FLOAT:
								ret_double(pValue, kn->value.f);
								break;
							case KSTAT_DATA_DOUBLE:
								ret_double(pValue, kn->value.d);
								break;
							default:
								ret_int(pValue, 0);
								break;
						}
						nRet = SYSINFO_RC_SUCCESS;
					}
         	}
      	}
      	kstat_close(kc);
   	}
	}

   return nRet;
}
