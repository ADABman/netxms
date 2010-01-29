/* 
** NetXMS multiplatform core agent
** Copyright (C) 2003, 2004, 2005, 2006, 2007 Victor Kirhenshtein
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
** File: service.cpp
**
**/

#include "nxagentd.h"


//
// Global variables
//

TCHAR g_windowsServiceName[MAX_PATH] = DEFAULT_AGENT_SERVICE_NAME;
TCHAR g_windowsServiceDisplayName[MAX_PATH] = _T("NetXMS Agent");
TCHAR g_windowsEventSourceName[MAX_PATH] = DEFAULT_AGENT_EVENT_SOURCE;


//
// Static data
//

static SERVICE_STATUS_HANDLE serviceHandle;


//
// Service control handler
//

static VOID WINAPI ServiceCtrlHandler(DWORD ctrlCode)
{
   SERVICE_STATUS status;

   status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
   status.dwCurrentState = SERVICE_RUNNING;
   status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
   status.dwWin32ExitCode = 0;
   status.dwServiceSpecificExitCode = 0;
   status.dwCheckPoint = 0;
   status.dwWaitHint = 0;

   switch(ctrlCode)
   {
      case SERVICE_CONTROL_STOP:
      case SERVICE_CONTROL_SHUTDOWN:
         status.dwCurrentState = SERVICE_STOP_PENDING;
         status.dwWaitHint = 4000;
         SetServiceStatus(serviceHandle, &status);

			DebugPrintf(INVALID_INDEX, 1, _T("Service control code %d - calling Shutdown()"), ctrlCode);
         Shutdown();

         status.dwCurrentState = SERVICE_STOPPED;
         status.dwWaitHint = 0;
         break;
      default:
         break;
   }

   SetServiceStatus(serviceHandle, &status);
}


//
// Service main
//

static VOID WINAPI AgentServiceMain(DWORD argc, LPTSTR *argv)
{
   SERVICE_STATUS status;

   serviceHandle = RegisterServiceCtrlHandler(g_windowsServiceName, ServiceCtrlHandler);

   // Now we start service initialization
   status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
   status.dwCurrentState = SERVICE_START_PENDING;
   status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
   status.dwWin32ExitCode = 0;
   status.dwServiceSpecificExitCode = 0;
   status.dwCheckPoint = 0;
   status.dwWaitHint = 10000 + g_dwStartupDelay * 1000;
   SetServiceStatus(serviceHandle, &status);

   // Actual initialization
   if (!Initialize())
   {
      Shutdown();

      // Now service is stopped
      status.dwCurrentState = SERVICE_STOPPED;
      status.dwWaitHint = 0;
      SetServiceStatus(serviceHandle, &status);
      return;
   }

   // Now service is running
   status.dwCurrentState = SERVICE_RUNNING;
   status.dwWaitHint = 0;
   SetServiceStatus(serviceHandle, &status);

   Main();
}


//
// Initialize service
//

void InitService(void)
{
   static SERVICE_TABLE_ENTRY serviceTable[2]={ { g_windowsServiceName, AgentServiceMain }, { NULL, NULL } };
   char szErrorText[256];

   if (!StartServiceCtrlDispatcher(serviceTable))
      printf("StartServiceCtrlDispatcher() failed: %s\n",
             GetSystemErrorText(GetLastError(), szErrorText, 256));
}


//
// Create service
//

void InstallService(char *execName, char *confFile)
{
   SC_HANDLE mgr, service;
   TCHAR cmdLine[8192], szErrorText[256];

   mgr = OpenSCManager(NULL, NULL, GENERIC_WRITE);
   if (mgr == NULL)
   {
      printf("ERROR: Cannot connect to Service Manager (%s)\n",
             GetSystemErrorText(GetLastError(), szErrorText, 256));
      return;
   }

   _sntprintf(cmdLine, 8192, _T("\"%s\" -d -c \"%s\" -n \"%s\" -e \"%s\" -D %d"),
	           execName, confFile, g_windowsServiceName, g_windowsEventSourceName, g_debugLevel);
	
	if (g_szPlatformSuffix[0] != 0)
	{
		strcat(cmdLine, " -P \"");
		strcat(cmdLine, g_szPlatformSuffix);
		strcat(cmdLine, "\"");
	}
	if (g_dwFlags & AF_CENTRAL_CONFIG)
	{
		strcat(cmdLine, " -M \"");
		strcat(cmdLine, g_szConfigServer);
		strcat(cmdLine, "\"");
	}
   
	service = CreateService(mgr, g_windowsServiceName, g_windowsServiceDisplayName, GENERIC_READ, SERVICE_WIN32_OWN_PROCESS,
                           SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, cmdLine, NULL, NULL, NULL, NULL, NULL);
   if (service == NULL)
   {
      DWORD code = GetLastError();

      if (code == ERROR_SERVICE_EXISTS)
         printf("ERROR: Service named '%s' already exist\n", g_windowsServiceName);
      else
         printf("ERROR: Cannot create service (%s)\n", GetSystemErrorText(code, szErrorText, 256));
   }
   else
   {
      printf("Service \"%s\" created successfully\n", g_windowsServiceName);
      CloseServiceHandle(service);

		InstallEventSource(execName);
   }

   CloseServiceHandle(mgr);
}


//
// Remove service
//

void RemoveService(void)
{
   SC_HANDLE mgr, service;
   char szErrorText[256];

   mgr = OpenSCManager(NULL,NULL,GENERIC_WRITE);
   if (mgr==NULL)
   {
      printf("ERROR: Cannot connect to Service Manager (%s)\n",
             GetSystemErrorText(GetLastError(), szErrorText, 256));
      return;
   }

   service=OpenService(mgr, g_windowsServiceName, DELETE);
   if (service==NULL)
   {
      printf("ERROR: Cannot open service named '%s' (%s)\n", g_windowsServiceName,
             GetSystemErrorText(GetLastError(), szErrorText, 256));
   }
   else
   {
      if (DeleteService(service))
         printf("Win32 Agent service deleted successfully\n");
      else
         printf("ERROR: Cannot remove service named '%s' (%s)\n", g_windowsServiceName,
                GetSystemErrorText(GetLastError(), szErrorText, 256));

      CloseServiceHandle(service);
   }

   CloseServiceHandle(mgr);

   RemoveEventSource();
}


//
// Start service
//

void StartAgentService(void)
{
   SC_HANDLE mgr, service;
   char szErrorText[256];

   mgr = OpenSCManager(NULL, NULL, GENERIC_WRITE);
   if (mgr == NULL)
   {
      printf("ERROR: Cannot connect to Service Manager (%s)\n",
             GetSystemErrorText(GetLastError(), szErrorText, 256));
      return;
   }

   service = OpenService(mgr, g_windowsServiceName, SERVICE_START);
   if (service == NULL)
   {
      printf("ERROR: Cannot open service named '%s' (%s)\n", g_windowsServiceName,
             GetSystemErrorText(GetLastError(), szErrorText, 256));
   }
   else
   {
      if (StartService(service, 0, NULL))
         printf("Win32 Agent service started successfully\n");
      else
         printf("ERROR: Cannot start service named '%s' (%s)\n", g_windowsServiceName,
                GetSystemErrorText(GetLastError(), szErrorText, 256));

      CloseServiceHandle(service);
   }

   CloseServiceHandle(mgr);
}


//
// Stop service
//

void StopAgentService(void)
{
   SC_HANDLE mgr, service;
   char szErrorText[256];

   mgr = OpenSCManager(NULL, NULL, GENERIC_WRITE);
   if (mgr == NULL)
   {
      printf("ERROR: Cannot connect to Service Manager (%s)\n", 
             GetSystemErrorText(GetLastError(), szErrorText, 256));
      return;
   }

   service = OpenService(mgr, g_windowsServiceName, SERVICE_STOP);
   if (service == NULL)
   {
      printf("ERROR: Cannot open service named '%s' (%s)\n", g_windowsServiceName,
             GetSystemErrorText(GetLastError(), szErrorText, 256));
   }
   else
   {
      SERVICE_STATUS status;

      if (ControlService(service, SERVICE_CONTROL_STOP, &status))
         printf("Win32 Agent service stopped successfully\n");
      else
         printf("ERROR: Cannot stop service named '%s' (%s)\n", g_windowsServiceName,
                GetSystemErrorText(GetLastError(), szErrorText, 256));

      CloseServiceHandle(service);
   }

   CloseServiceHandle(mgr);
}


//
// Install event source
//

void InstallEventSource(char *path)
{
   HKEY hKey;
   DWORD dwTypes = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
   TCHAR szErrorText[256], key[MAX_PATH];

	_sntprintf(key, MAX_PATH, _T("System\\CurrentControlSet\\Services\\EventLog\\System\\%s"), g_windowsEventSourceName);
   if (ERROR_SUCCESS != RegCreateKeyEx(HKEY_LOCAL_MACHINE, key,
         0,NULL,REG_OPTION_NON_VOLATILE,KEY_SET_VALUE,NULL,&hKey,NULL))
   {
      printf("Unable to create registry key: %s\n",
             GetSystemErrorText(GetLastError(), szErrorText, 256));
      return;
   }

   RegSetValueEx(hKey, "TypesSupported", 0, REG_DWORD, (BYTE *)&dwTypes, sizeof(DWORD));
   RegSetValueEx(hKey, "EventMessageFile", 0, REG_EXPAND_SZ, (BYTE *)path, (DWORD)(strlen(path) + 1));

   RegCloseKey(hKey);
   printf("Event source \"%s\" installed successfully\n", g_windowsEventSourceName);
}


//
// Remove event source
//

void RemoveEventSource(void)
{
   TCHAR szErrorText[256], key[MAX_PATH];

	_sntprintf(key, MAX_PATH, _T("System\\CurrentControlSet\\Services\\EventLog\\System\\%s"), g_windowsEventSourceName);
   if (ERROR_SUCCESS == RegDeleteKey(HKEY_LOCAL_MACHINE, key))
   {
      _tprintf(_T("Event source \"%s\" uninstalled successfully\n"), g_windowsEventSourceName);
   }
   else
   {
      _tprintf(_T("Unable to uninstall event source \"%s\": %s\n"), g_windowsEventSourceName,
               GetSystemErrorText(GetLastError(), szErrorText, 256));
   }
}


//
// Wait for service
//

BOOL WaitForService(DWORD dwDesiredState)
{
   SC_HANDLE mgr, service;
   char szErrorText[256];
   BOOL bResult = FALSE;

   mgr = OpenSCManager(NULL, NULL, GENERIC_WRITE);
   if (mgr == NULL)
   {
      printf("ERROR: Cannot connect to Service Manager (%s)\n", 
             GetSystemErrorText(GetLastError(), szErrorText, 256));
      return FALSE;
   }

   service = OpenService(mgr, g_windowsServiceName, SERVICE_QUERY_STATUS);
   if (service == NULL)
   {
      printf("ERROR: Cannot open service named '%s' (%s)\n", g_windowsServiceName,
             GetSystemErrorText(GetLastError(), szErrorText, 256));
   }
   else
   {
      SERVICE_STATUS status;

      while(1)
      {
         if (QueryServiceStatus(service, &status))
         {
            if (status.dwCurrentState == dwDesiredState)
            {
               bResult = TRUE;
               break;
            }
         }
         else
         {
            break;   // Error
         }
         Sleep(200);
      }

      CloseServiceHandle(service);
   }

   CloseServiceHandle(mgr);
   return bResult;
}
