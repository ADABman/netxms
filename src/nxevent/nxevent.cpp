/* 
** NetXMS - Network Management System
** Command line event sender
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
** $module: nxevent.cpp
**
**/

#include "nxevent.h"


//
// Static data
//

static WORD m_wServerPort = 4701;
static BOOL m_bDebug = FALSE;
static TCHAR m_szServer[MAX_DB_STRING] = _T("127.0.0.1");
static TCHAR m_szLogin[MAX_DB_STRING] = _T("guest");
static TCHAR m_szPassword[MAX_DB_STRING] = _T("");
static DWORD m_dwEventCode = 0;
static DWORD m_dwObjectId = 0;
static DWORD m_dwTimeOut = 3;


//
// Callback function for debug printing
//

static void DebugCallback(char *pMsg)
{
   printf("*debug* %s\n", pMsg);
}


//
// Send event to server
//

static void SendEvent(int iNumArgs, TCHAR **pArgList)
{
   DWORD dwResult;

   if (!NXCInitialize())
   {
      _tprintf(_T("Failed to initialize NetXMS client library\n"));
   }
   else
   {
      if (m_bDebug)
         NXCSetDebugCallback(DebugCallback);
      NXCSetCommandTimeout(m_dwTimeOut * 1000);

      dwResult = NXCConnect(m_szServer, m_szLogin, m_szPassword);
      if (dwResult != RCC_SUCCESS)
      {
         _tprintf(_T("Unable to connect to server: %s\n"), NXCGetErrorText(dwResult));
      }
      else
      {
         dwResult = NXCSendEvent(m_dwEventCode, m_dwObjectId, iNumArgs, pArgList);
         if (dwResult != RCC_SUCCESS)
            _tprintf(_T("Unable to send event: %s\n"), NXCGetErrorText(dwResult));
         NXCDisconnect();
      }
   }
}


//
// Entry point
//

int main(int argc, char *argv[])
{
   int ch;
   BOOL bStart = TRUE;

   // Parse command line
   opterr = 1;
   while((ch = getopt(argc, argv, "dho:p:P:u:vw:")) != -1)
   {
      switch(ch)
      {
         case 'h':   // Display help and exit
            printf("Usage: nxevent [<options>] <server> <event_id> [<param_1> [... <param_N>]]\n"
                   "Valid options are:\n"
                   "   -d            : Turn on debug mode.\n"
                   "   -h            : Display help and exit.\n"
                   "   -o <id>       : Specify source object ID.\n"
                   "   -p <port>     : Specify server's port number. Default is %d.\n"
                   "   -P <password> : Specify user's password. Default is empty password.\n"
                   "   -u <user>     : Login to server as <user>. Default is \"guest\".\n"
                   "   -v            : Display version and exit.\n"
                   "   -w <seconds>  : Specify command timeout (default is 3 seconds).\n"
                   "\n", m_wServerPort);
            bStart = FALSE;
            break;
         case 'd':
            m_bDebug = TRUE;
            break;
         case 'o':
            m_dwObjectId = _tcstoul(optarg, NULL, 0);
            break;
         case 'u':
            _tcsncpy(m_szLogin, optarg, MAX_DB_STRING);
            break;
         case 'P':
            _tcsncpy(m_szPassword, optarg, MAX_DB_STRING);
            break;
         case 'w':
            m_dwTimeOut = _tcstoul(optarg, NULL, 0);
            if ((m_dwTimeOut < 1) || (m_dwTimeOut > 120))
            {
               _tprintf(_T("Invalid timeout %s\n"), optarg);
               bStart = FALSE;
            }
            break;
         case 'v':
            printf("NetXMS Event Sender  Version " NETXMS_VERSION_STRING "\n");
            bStart = FALSE;
            break;
         case '?':
            bStart = FALSE;
            break;
         default:
            break;
      }
   }

   // Do requested action if everything is OK
   if (bStart)
   {
      if (argc - optind < 2)
      {
         _tprintf(_T("Required arguments missing. Use nxevent -h for help.\n"));
      }
      else
      {
#ifdef _WIN32
         WSADATA wsaData;

         if (WSAStartup(2, &wsaData) != 0)
         {
            _tprintf(_T("Unable to initialize Windows sockets\n"));
            return 4;
         }
#endif
         strncpy(m_szServer, argv[optind], 256);
         m_dwEventCode = _tcstoul(argv[optind + 1], NULL, 0);
         SendEvent(argc - optind - 2, &argv[optind + 2]);
      }
   }

   return 0;
}
