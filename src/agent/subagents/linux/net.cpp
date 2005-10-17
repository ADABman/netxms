/* $Id: net.cpp,v 1.10 2005-10-17 20:45:46 victor Exp $ */

/* 
** NetXMS subagent for GNU/Linux
** Copyright (C) 2004 Alex Kirhenshtein
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
**/

#include <nms_common.h>
#include <nms_agent.h>

#include <linux/sysctl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "net.h"


LONG H_NetIpForwarding(char *pszParam, char *pArg, char *pValue)
{
	int nVer = (int)pArg;
	int nRet = SYSINFO_RC_ERROR;
	FILE *hFile;
	char *pFileName = NULL;

	switch (nVer)
	{
	case 4:
		pFileName = "/proc/sys/net/ipv4/conf/all/forwarding";
		break;
	case 6:
		pFileName = "/proc/sys/net/ipv6/conf/all/forwarding";
		break;
	}

	if (pFileName != NULL)
	{
		hFile = fopen(pFileName, "r");
		if (hFile != NULL)
		{
			char szBuff[4];

			if (fgets(szBuff, sizeof(szBuff), hFile) != NULL)
			{
				nRet = SYSINFO_RC_SUCCESS;
				switch (szBuff[0])
				{
				case '0':
					ret_int(pValue, 0);
					break;
				case '1':
					ret_int(pValue, 1);
					break;
				default:
					nRet = SYSINFO_RC_ERROR;
					break;
				}
			}
			fclose(hFile);
		}
	}

	return nRet;
}

LONG H_NetArpCache(char *pszParam, char *pArg, NETXMS_VALUES_LIST *pValue)
{
	int nRet = SYSINFO_RC_ERROR;
	FILE *hFile;

	hFile = fopen("/proc/net/arp", "r");
	if (hFile != NULL)
	{
		char szBuff[256];
		int nFd;

		nFd = socket(AF_INET, SOCK_DGRAM, 0);
		if (nFd > 0)
		{
			nRet = SYSINFO_RC_SUCCESS;

			fgets(szBuff, sizeof(szBuff), hFile); // skip first line

			while (fgets(szBuff, sizeof(szBuff), hFile) != NULL)
			{
				int nIP1, nIP2, nIP3, nIP4;
				int nMAC1, nMAC2, nMAC3, nMAC4, nMAC5, nMAC6;
				char szTmp1[256];
				char szTmp2[256];
				char szTmp3[256];
				char szIf[256];

				if (sscanf(szBuff,
						"%d.%d.%d.%d %s %s %02X:%02X:%02X:%02X:%02X:%02X %s %s",
						&nIP1, &nIP2, &nIP3, &nIP4,
						szTmp1, szTmp2,
						&nMAC1, &nMAC2, &nMAC3, &nMAC4, &nMAC5, &nMAC6,
						szTmp3, szIf) == 14)
				{
					int nIndex;
					struct ifreq irq;

					if (nMAC1 == 0 && nMAC2 == 0 &&
						nMAC3 == 0 && nMAC4 == 0 &&
						nMAC5 == 0 && nMAC6 == 0)
					{
						// incomplete
						continue;
					}

					nx_strncpy(irq.ifr_name, szIf, IFNAMSIZ);
					if (ioctl(nFd, SIOCGIFINDEX, &irq) != 0)
					{
						perror("ioctl()");
						nIndex = 0;
					}
					else
					{
						nIndex = irq.ifr_ifindex;
					}
					
					snprintf(szBuff, sizeof(szBuff),
							"%02X%02X%02X%02X%02X%02X %d.%d.%d.%d %d",
							nMAC1, nMAC2, nMAC3, nMAC4, nMAC5, nMAC6,
							nIP1, nIP2, nIP3, nIP4,
							nIndex);

					NxAddResultString(pValue, szBuff);
				}
			}

			close(nFd);
		}
		
		fclose(hFile);
	}

	return nRet;
}

LONG H_NetRoutingTable(char *pszParam, char *pArg, NETXMS_VALUES_LIST *pValue)
{
	int nRet = SYSINFO_RC_ERROR;
	FILE *hFile;
	int nFd;

	nFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (nFd <= 0)
	{
		return SYSINFO_RC_ERROR;
	}

	hFile = fopen("/proc/net/route", "r");
	if (hFile == NULL)
	{
		close(nFd);
		return SYSINFO_RC_ERROR;
	}

	char szLine[256];

	if (fgets(szLine, sizeof(szLine), hFile) != NULL)
	{
		if (!strncmp(szLine,
					"Iface\tDestination\tGateway \tFlags\tRefCnt\t"
					"Use\tMetric\tMask", 55))
		{
			nRet = SYSINFO_RC_SUCCESS;

			while (fgets(szLine, sizeof(szLine), hFile) != NULL)
			{
				char szIF[64];
				int nTmp, nType = 0;
				unsigned long nDestination, nGateway, nMask;

				if (sscanf(szLine,
							"%s\t%08X\t%08X\t%d\t%d\t%d\t%d\t%08X",
							szIF,
							&nDestination,
							&nGateway,
							&nTmp, &nTmp, &nTmp, &nTmp,
							&nMask) == 8)
				{
					int nIndex;
					struct ifreq irq;

					nx_strncpy(irq.ifr_name, szIF, IFNAMSIZ);
					if (ioctl(nFd, SIOCGIFINDEX, &irq) != 0)
					{
						perror("ioctl()");
						nIndex = 0;
					}
					else
					{
						nIndex = irq.ifr_ifindex;
					}

					char szBuf1[64], szBuf2[64];
					snprintf(szLine, sizeof(szLine), "%s/%d %s %d %d",
							IpToStr(ntohl(nDestination), szBuf1),
							BitsInMask(htonl(nMask)),
							IpToStr(ntohl(nGateway), szBuf2),
							nIndex,
							nType);
					NxAddResultString(pValue, szLine);
				}
			}
		}
	}

	close(nFd);
	fclose(hFile);

	return nRet;
}

LONG H_NetIfList(char *pszParam, char *pArg, NETXMS_VALUES_LIST *pValue)
{
	int nRet = SYSINFO_RC_ERROR;
   struct if_nameindex *pIndex;
   struct ifreq irq;
   struct sockaddr_in *sa;
	int nFd;

	int s;
	char *buff;
	struct ifconf ifc;
	int i;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s > 0)
	{
		ifc.ifc_len = 0;
		ifc.ifc_buf = NULL;
		if (ioctl(s, SIOCGIFCONF, (caddr_t)&ifc) == 0)
		{
			buff = (char *)malloc(ifc.ifc_len);
			if (buff != NULL)
			{
				ifc.ifc_buf = buff;

				if (ioctl(s, SIOCGIFCONF, (caddr_t)&ifc) == 0)
				{
					for (i = 0; i < ifc.ifc_len / sizeof(struct ifreq); i++)
					{
						if( ifc.ifc_req[i].ifr_addr.sa_family == AF_INET )
						{
							struct sockaddr_in *addr;
							struct ifreq irq;
							int mask = 0;
							char szOut[1024];
							int index = if_nametoindex(ifc.ifc_req[i].ifr_name);
							char szMacAddr[16];


							nRet = SYSINFO_RC_SUCCESS;

							strcpy(irq.ifr_name, ifc.ifc_req[i].ifr_name);

							if (ioctl(s, SIOCGIFNETMASK, &irq) == 0)
							{
								mask = 33 - ffs(htonl(((struct sockaddr_in *)
													&irq.ifr_addr)->sin_addr.s_addr));
							}

							strcpy(irq.ifr_name, ifc.ifc_req[i].ifr_name);
							strcpy(szMacAddr, "000000000000");
							if (ioctl(s, SIOCGIFHWADDR, &irq) == 0)
							{
								for (int z = 0; z < 6; z++)
								{
									sprintf(&szMacAddr[z << 1], "%02X",
											(unsigned char)irq.ifr_hwaddr.sa_data[z]);
								}
							}

							addr = (struct sockaddr_in *)&(ifc.ifc_req[i].ifr_addr);

							snprintf(szOut, sizeof(szOut), "%d %s/%d %d %s %s",
									index,
									inet_ntoa(addr->sin_addr),
									mask,
									IFTYPE_OTHER,
									szMacAddr,
									ifc.ifc_req[i].ifr_name);
							NxAddResultString(pValue, szOut);
						}
					}
				}
				else
				{
					//perror("sysctl-2()");
				}

				free(buff);
			}
			else
			{
				//perror("malloc()");
			}
		}
		else
		{
			//perror("sysctl-1()");
		}

		close(s);
	}
	else
	{
		//perror("socket()");
	}


	return nRet;
}

LONG H_NetIfInfoFromIOCTL(char *pszParam, char *pArg, char *pValue)
{
   char *eptr, szBuffer[256];
   LONG nRet = SYSINFO_RC_SUCCESS;
   struct ifreq ifr;
   int fd;

   if (!NxGetParameterArg(pszParam, 1, szBuffer, 256))
      return SYSINFO_RC_UNSUPPORTED;

   fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (fd == -1)
   {
      return 1;
   }

   // Check if we have interface name or index
   ifr.ifr_ifindex = strtol(szBuffer, &eptr, 10);
   if (*eptr == 0)
   {
      // Index passed as argument, convert to name
      if (ioctl(fd, SIOCGIFNAME, &ifr) != 0)
         nRet = SYSINFO_RC_ERROR;
   }
   else
   {
      // Name passed as argument
      nx_strncpy(ifr.ifr_name, szBuffer, IFNAMSIZ);
   }

   // Get interface information
   if (nRet == SYSINFO_RC_SUCCESS)
   {
      switch((int)pArg)
      {
         case IF_INFO_ADMIN_STATUS:
            if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0)
            {
               ret_int(pValue, (ifr.ifr_flags & IFF_UP) ? 1 : 2);
            }
            else
            {
               nRet = SYSINFO_RC_ERROR;
            }
            break;
         case IF_INFO_OPER_STATUS:
            if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0)
            {
               // IFF_RUNNING should be set only if interface can
               // transmit/receive data, but in fact looks like it
               // always set. I have unverified information that
               // newer kernels set this flag correctly.
               ret_int(pValue, (ifr.ifr_flags & IFF_RUNNING) ? 1 : 0);
            }
            else
            {
               nRet = SYSINFO_RC_ERROR;
            }
            break;
         case IF_INFO_DESCRIPTION:
            ret_string(pValue, ifr.ifr_name);
            break;
         default:
            nRet = SYSINFO_RC_UNSUPPORTED;
            break;
      }
   }

   // Cleanup
   close(fd);

   return nRet;
}

static LONG ValueFromLine(char *pszLine, int nPos, char *pValue)
{
   int i;
   char *eptr, *pszWord, szBuffer[256];
   DWORD dwValue;
   LONG nRet = SYSINFO_RC_ERROR;

   for(i = 0, pszWord = pszLine; i <= nPos; i++)
      pszWord = ExtractWord(pszWord, szBuffer);
   dwValue = strtoul(szBuffer, &eptr, 0);
   if (*eptr == 0)
   {
      ret_uint(pValue, dwValue);
      nRet = SYSINFO_RC_SUCCESS;
   }
   return nRet;
}

LONG H_NetIfInfoFromProc(char *pszParam, char *pArg, char *pValue)
{
   char *ptr, szBuffer[256], szName[IFNAMSIZ];
   LONG nIndex, nRet = SYSINFO_RC_SUCCESS;
   FILE *fp;

   if (!NxGetParameterArg(pszParam, 1, szBuffer, 256))
      return SYSINFO_RC_UNSUPPORTED;

   // Check if we have interface name or index
   nIndex = strtol(szBuffer, &ptr, 10);
   if (*ptr == 0)
   {
      // Index passed as argument, convert to name
      if (if_indextoname(nIndex, szName) == NULL)
         nRet = SYSINFO_RC_ERROR;
   }
   else
   {
      // Name passed as argument
      nx_strncpy(szName, szBuffer, IFNAMSIZ);
   }

   // Get interface information
   if (nRet == SYSINFO_RC_SUCCESS)
   {
      // If name is an alias (i.e. eth0:1), remove alias number
      ptr = strchr(szName, ':');
      if (ptr != NULL)
         *ptr = 0;

      fp = fopen("/proc/net/dev", "r");
      if (fp != NULL)
      {
         while(1)
         {
            fgets(szBuffer, 256, fp);
            if (feof(fp))
            {
               nRet = SYSINFO_RC_ERROR;   // Interface record not found
               break;
            }

            // We expect line in form interface:stats
            StrStrip(szBuffer);
            ptr = strchr(szBuffer, ':');
            if (ptr == NULL)
               continue;
            *ptr = 0;

            if (!stricmp(szBuffer, szName))
            {
               ptr++;
               break;
            }
         }
         fclose(fp);
      }
      else
      {
         nRet = SYSINFO_RC_ERROR;
      }

      if (nRet == SYSINFO_RC_SUCCESS)
      {
         StrStrip(ptr);
         switch((int)pArg)
         {
            case IF_INFO_BYTES_IN:
               nRet = ValueFromLine(ptr, 0, pValue);
               break;
            case IF_INFO_PACKETS_IN:
               nRet = ValueFromLine(ptr, 1, pValue);
               break;
            case IF_INFO_IN_ERRORS:
               nRet = ValueFromLine(ptr, 2, pValue);
               break;
            case IF_INFO_BYTES_OUT:
               nRet = ValueFromLine(ptr, 8, pValue);
               break;
            case IF_INFO_PACKETS_OUT:
               nRet = ValueFromLine(ptr, 9, pValue);
               break;
            case IF_INFO_OUT_ERRORS:
               nRet = ValueFromLine(ptr, 10, pValue);
               break;
            default:
               nRet = SYSINFO_RC_UNSUPPORTED;
               break;
         }
      }
   }

   return nRet;
}

///////////////////////////////////////////////////////////////////////////////
/*

$Log: not supported by cvs2svn $
Revision 1.9  2005/09/08 16:26:31  alk
Net.InterfaceList now use alternative way and works with virtual
interfaces under 2.6

Revision 1.8  2005/08/22 00:11:47  alk
Net.IP.RoutingTable added

Revision 1.7  2005/06/12 17:57:24  victor
Net.Interface.AdminStatus should return 2 for disabled interfaces

Revision 1.6  2005/06/11 16:28:24  victor
Implemented all Net.Interface.* parameters except Net.Interface.Speed

Revision 1.5  2005/06/09 12:15:43  victor
Added support for Net.Interface.AdminStatus and Net.Interface.Link parameters

Revision 1.4  2005/01/05 12:21:24  victor
- Added wrappers for new and delete from gcc2 libraries
- sys/stat.h and fcntl.h included in nms_common.h

Revision 1.3  2004/11/25 08:01:27  victor
Processing of interface list will be stopped on error

Revision 1.2  2004/10/23 22:53:23  alk
ArpCache: ignore incomplete entries

Revision 1.1  2004/10/22 22:08:34  alk
source restructured;
implemented:
	Net.IP.Forwarding
	Net.IP6.Forwarding
	Process.Count(*)
	Net.ArpCache
	Net.InterfaceList (if-type not implemented yet)
	System.ProcessList


*/
