/* 
** Project X - Network Management System
** Copyright (C) 2003 Victor Kirhenshtein
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
** $module: tools.cpp
**
**/

#include "nxcore.h"

#ifdef _WIN32
# include <io.h>
#else
# ifdef HAVE_SYS_UTSNAME_H
#  include <sys/utsname.h>
# endif
#endif


//
// Get system error string by call to FormatMessage
//

#ifdef _WIN32

char NXCORE_EXPORTABLE *GetSystemErrorText(DWORD error)
{
   char *msgBuf;
   static char staticBuffer[1024];

   if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                     FORMAT_MESSAGE_FROM_SYSTEM | 
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                     NULL,error,
                     MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT), // Default language
                     (LPSTR)&msgBuf,0,NULL)>0)
   {
      msgBuf[strcspn(msgBuf,"\r\n")]=0;
      strncpy(staticBuffer,msgBuf,1023);
      LocalFree(msgBuf);
   }
   else
   {
      sprintf(staticBuffer,"MSG 0x%08X - Unable to find message text",error);
   }

   return staticBuffer;
}

#endif   /* _WIN32 */


//
// Clean interface list from unneeded entries
//

void CleanInterfaceList(INTERFACE_LIST *pIfList)
{
   int i;

   if (pIfList == NULL)
      return;

   // Delete loopback interface(s) from list
   for(i = 0; i < pIfList->iNumEntries; i++)
      if ((pIfList->pInterfaces[i].dwIpAddr & 0xFF000000) == 0x7F000000)
      {
         pIfList->iNumEntries--;
         memmove(&pIfList->pInterfaces[i], &pIfList->pInterfaces[i + 1],
                 sizeof(INTERFACE_INFO) * (pIfList->iNumEntries - i));
         i--;
      }
}


//
// Get system information string
//

void GetSysInfoStr(char *pszBuffer)
{
#ifdef _WIN32
   DWORD dwSize;
   char computerName[MAX_COMPUTERNAME_LENGTH + 1], osVersion[256];
   SYSTEM_INFO sysInfo;
   OSVERSIONINFO versionInfo;

   dwSize = MAX_COMPUTERNAME_LENGTH + 1;
   GetComputerName(computerName, &dwSize);

   versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
   GetVersionEx(&versionInfo);
   GetSystemInfo(&sysInfo);

   switch(versionInfo.dwPlatformId)
   {
      case VER_PLATFORM_WIN32_WINDOWS:
         sprintf(osVersion,"Windows %s-%s",versionInfo.dwMinorVersion == 0 ? "95" :
            (versionInfo.dwMinorVersion == 10 ? "98" :
               (versionInfo.dwMinorVersion == 90 ? "Me" : "Unknown")),versionInfo.szCSDVersion);
         break;
      case VER_PLATFORM_WIN32_NT:
         if (versionInfo.dwMajorVersion != 5)
            sprintf(osVersion,"Windows NT %d.%d %s",versionInfo.dwMajorVersion,
                    versionInfo.dwMinorVersion,versionInfo.szCSDVersion);
         else      // Windows 2000, Windows XP or Windows Server 2003
            sprintf(osVersion,"Windows %s%s%s",versionInfo.dwMinorVersion == 0 ? "2000" : 
                    (versionInfo.dwMinorVersion == 1 ? "XP" : "Server 2003"),
                       versionInfo.szCSDVersion[0] == 0 ? "" : " ", versionInfo.szCSDVersion);
         break;
      default:
         strcpy(osVersion,"Windows [Unknown Version]");
         break;
   }

   sprintf(pszBuffer, "%s %s Build %d", computerName, osVersion, versionInfo.dwBuildNumber);
#else
   /* TODO: add UNIX code here */
# ifdef HAVE_SYS_UTSNAME_H
	struct utsname uName;
	if (uname(&uName) == 0)
	{
		sprintf(pszBuffer, "%s %s Release %d", uName.nodename, uName.sysname, uName.release);
	}
	else
	{
		// size=512 was taken from locks.cpp
#if HAVE_STRERROR_R
		strerror_r(errno, pszBuffer, 512);
#else
		strncpy(pszBuffer, strerror(errno), 512);
#endif
	}
# else
   printf("GetSysInfoStr: code not implemented\n");
   strcpy(pszBuffer, "UNIX");
# endif // HAVE_SYS_UTSNAME_H

#endif
}


//
// Get IP address for local machine
//

DWORD GetLocalIpAddr(void)
{
   INTERFACE_LIST *pIfList;
   DWORD dwAddr = 0;
   int i;

   pIfList = GetLocalInterfaceList();
   if (pIfList != NULL)
   {
      CleanInterfaceList(pIfList);
      
      // Find first interface with IP address
      for(i = 0; i < pIfList->iNumEntries; i++)
         if (pIfList->pInterfaces[i].dwIpAddr != 0)
         {
            dwAddr = pIfList->pInterfaces[i].dwIpAddr;
            break;
         }
      DestroyInterfaceList(pIfList);
   }
   return dwAddr;
}


//
// Execute external command
//

BOOL ExecCommand(char *pszCommand)
{
   BOOL bSuccess = TRUE;

#ifdef _WIN32
   STARTUPINFO si;
   PROCESS_INFORMATION pi;

   // Fill in process startup info structure
   memset(&si, 0, sizeof(STARTUPINFO));
   si.cb = sizeof(STARTUPINFO);
   si.dwFlags = 0;

   // Create new process
   if (!CreateProcess(NULL, pszCommand, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi))
   {
      WriteLog(MSG_CREATE_PROCESS_FAILED, EVENTLOG_ERROR_TYPE, "se", pszCommand, GetLastError());
      bSuccess = FALSE;
   }
   else
   {
      // Close all handles
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
   }
#else
	bSuccess = FALSE;
	{
		int nPid;
		char *pCmd[128];
		int i, nCount = 0;
		char *pTmp;
		struct stat st;
		int state = 0;

		pTmp = strdup(pszCommand);
		if (pTmp != NULL)
		{
			pCmd[nCount++] = pTmp;
			int nLen = strlen(pTmp);
			for (int i = 0; i < nLen; i++)
			{
				switch(pTmp[i])
				{
					case ' ':
						if (state == 0)
						{
							pTmp[i] = 0;
							if (pTmp[i + 1] != 0)
							{
								pCmd[nCount++] = pTmp + i + 1;
							}
						}
						break;
					case '"':
						state == 0 ? state++ : state--;

						memmove(pTmp + i, pTmp + i + 1, nLen - i);
						i--;
						break;
					case '\\':
						if (pTmp[i+1] == '"')
						{
							memmove(pTmp + i, pTmp + i + 1, nLen - i);
						}
						break;
					default:
						break;
				}
			}
			pCmd[nCount] = NULL;

			if (stat(pCmd[0], &st) == 0 &&
					st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
			{
				switch ((nPid = fork()))
				{
					case -1:
						perror("fork()");
						break;
					case 0: // child
						{
							int sd = open("/dev/null", O_RDWR);
							if (sd == -1)
							{
								sd = open("/dev/null", O_RDONLY);
							}
							close(0);
							close(1);
							close(2);
							dup2(sd, 0);
							dup2(sd, 1);
							dup2(sd, 2);
							close(sd);
							execv(pCmd[0], pCmd);
							// should not be reached
							//_exit((errno == EACCES || errno == ENOEXEC) ? 126 : 127);
							_exit(127);
						}
						break;
					default: // parent
						bSuccess = TRUE;
						break;
				}
			}

			free(pTmp);
		}
	}
#endif

   return bSuccess;
}


//
// Send Wake-on-LAN packet (magic packet) to given IP address
// with given MAC address inside
//

BOOL SendMagicPacket(DWORD dwIpAddr, BYTE *pbMacAddr, int iNumPackets)
{
   BYTE *pCurr, bPacketData[96];
   SOCKET hSocket;
   struct sockaddr_in addr;
   BOOL bResult = TRUE;
   int i;
   
   // Create data area
   for(i = 0, pCurr = bPacketData; i < 16; i++, pCurr += 6)
      memcpy(pCurr, pbMacAddr, 6);

   // Create socket
   hSocket = socket(AF_INET, SOCK_DGRAM, 0);
   if (hSocket == -1)
      return FALSE;

   memset(&addr, 0, sizeof(struct sockaddr_in));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = dwIpAddr;
   addr.sin_port = 53;

   // Send requested number of packets
   for(i = 0; i < iNumPackets; i++)
      if (sendto(hSocket, (char *)bPacketData, 96, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
         bResult = FALSE;

   // Cleanup
   closesocket(hSocket);
   return bResult;
}
