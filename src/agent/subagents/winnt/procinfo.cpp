/* 
** Windows NT/2000/XP/2003 NetXMS subagent
** Copyright (C) 2003, 2004, 2005 Victor Kirhenshtein
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
** File: procinfo.cpp
** Win32 specific process information parameters
**
**/

#include "winnt_subagent.h"
#include <winternl.h>


//
// Convert process time from FILETIME structure (100-nanosecond units) to __uint64 (milliseconds)
//

static unsigned __int64 ConvertProcessTime(FILETIME *lpft)
{
   unsigned __int64 i;

   memcpy(&i, lpft, sizeof(unsigned __int64));
   i /= 10000;      // Convert 100-nanosecond units to milliseconds
   return i;
}


//
// Check if attribute supported or not
//

static BOOL IsAttributeSupported(int attr)
{
   switch(attr)
   {
      case PROCINFO_GDI_OBJ:
      case PROCINFO_USER_OBJ:
         if (imp_GetGuiResources == NULL)
            return FALSE;     // No appropriate function available, probably we are running on NT4
         break;
      case PROCINFO_IO_READ_B:
      case PROCINFO_IO_READ_OP:
      case PROCINFO_IO_WRITE_B:
      case PROCINFO_IO_WRITE_OP:
      case PROCINFO_IO_OTHER_B:
      case PROCINFO_IO_OTHER_OP:
         if (imp_GetProcessIoCounters == NULL)
            return FALSE;     // No appropriate function available, probably we are running on NT4
         break;
      default:
         break;
   }

   return TRUE;
}


//
// Get specific process attribute
//

static unsigned __int64 GetProcessAttribute(HANDLE hProcess, int attr, int type, int count,
                                            unsigned __int64 lastValue)
{
   unsigned __int64 value;  
   PROCESS_MEMORY_COUNTERS mc;
   IO_COUNTERS ioCounters;
   FILETIME ftCreate, ftExit, ftKernel, ftUser;

   // Get value for current process instance
   switch(attr)
   {
      case PROCINFO_VMSIZE:
         GetProcessMemoryInfo(hProcess, &mc, sizeof(PROCESS_MEMORY_COUNTERS));
         value = mc.PagefileUsage;
         break;
      case PROCINFO_WKSET:
         GetProcessMemoryInfo(hProcess, &mc, sizeof(PROCESS_MEMORY_COUNTERS));
         value = mc.WorkingSetSize;
         break;
      case PROCINFO_PF:
         GetProcessMemoryInfo(hProcess, &mc, sizeof(PROCESS_MEMORY_COUNTERS));
         value = mc.PageFaultCount;
         break;
      case PROCINFO_KTIME:
      case PROCINFO_UTIME:
         GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser);
         value = ConvertProcessTime(attr == PROCINFO_KTIME ? &ftKernel : &ftUser);
         break;
      case PROCINFO_CPUTIME:
         GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser);
         value = ConvertProcessTime(&ftKernel) + ConvertProcessTime(&ftUser);
         break;
      case PROCINFO_GDI_OBJ:
      case PROCINFO_USER_OBJ:
         value = imp_GetGuiResources(hProcess, attr == PROCINFO_GDI_OBJ ? GR_GDIOBJECTS : GR_USEROBJECTS);
         break;
      case PROCINFO_IO_READ_B:
         imp_GetProcessIoCounters(hProcess, &ioCounters);
         value = ioCounters.ReadTransferCount;
         break;
      case PROCINFO_IO_READ_OP:
         imp_GetProcessIoCounters(hProcess, &ioCounters);
         value = ioCounters.ReadOperationCount;
         break;
      case PROCINFO_IO_WRITE_B:
         imp_GetProcessIoCounters(hProcess, &ioCounters);
         value = ioCounters.WriteTransferCount;
         break;
      case PROCINFO_IO_WRITE_OP:
         imp_GetProcessIoCounters(hProcess, &ioCounters);
         value = ioCounters.WriteOperationCount;
         break;
      case PROCINFO_IO_OTHER_B:
         imp_GetProcessIoCounters(hProcess, &ioCounters);
         value = ioCounters.OtherTransferCount;
         break;
      case PROCINFO_IO_OTHER_OP:
         imp_GetProcessIoCounters(hProcess, &ioCounters);
         value = ioCounters.OtherOperationCount;
         break;
      default:       // Unknown attribute
         AgentWriteLog(EVENTLOG_ERROR_TYPE, "GetProcessAttribute(): Unexpected attribute: 0x%02X", attr);
         value = 0;
         break;
   }

   // Recalculate final value according to selected type
   if (count == 1)     // First instance
   {
      return value;
   }

   switch(type)
   {
      case INFOTYPE_MIN:
         return min(lastValue, value);
      case INFOTYPE_MAX:
         return max(lastValue, value);
      case INFOTYPE_AVG:
         return (lastValue * (count - 1) + value) / count;
      case INFOTYPE_SUM:
         return lastValue + value;
      default:
         AgentWriteLog(EVENTLOG_ERROR_TYPE, "GetProcessAttribute(): Unexpected type: 0x%02X", type);
         return 0;
   }
}


//
// Get command line of the process, specified by process ID
//

static BOOL GetProcessCommandLine(DWORD dwPId, TCHAR *pszCmdLine, DWORD dwLen)
{
	SIZE_T dummy;
	BOOL bRet = FALSE;
#ifdef __64BIT__
	const DWORD desiredAccess = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
#else
	const DWORD desiredAccess = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | 
	                            PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION;
#endif

	HANDLE hProcess = OpenProcess(desiredAccess, FALSE, dwPId);
	if(hProcess == INVALID_HANDLE_VALUE)
		return FALSE;

#ifdef __64BIT__
	PROCESS_BASIC_INFORMATION pbi;
	PEB peb;
	RTL_USER_PROCESS_PARAMETERS pp;
	WCHAR *buffer;
	NTSTATUS status;
	ULONG size;
	
	status = NtQueryInformationProcess(hProcess, ProcessBasicInformation, &pbi, sizeof(PROCESS_BASIC_INFORMATION), &size);
	if (status == 0)	// STATUS_SUCCESS
	{
		if (ReadProcessMemory(hProcess, pbi.PebBaseAddress, &peb, sizeof(PEB), &dummy))
		{
			if (ReadProcessMemory(hProcess, peb.ProcessParameters, &pp, sizeof(RTL_USER_PROCESS_PARAMETERS), &dummy))
			{
				size_t bufSize = (pp.CommandLine.Length + 1) * sizeof(WCHAR);
				buffer = (WCHAR *)malloc(bufSize);
				if (ReadProcessMemory(hProcess, pp.CommandLine.Buffer, buffer, bufSize, &dummy))
				{
					WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR, buffer, pp.CommandLine.Length, pszCmdLine, dwLen, NULL, NULL);
					pszCmdLine[dwLen - 1] = 0;
					bRet = TRUE;
				}
				free(buffer);
			}
		}
	}

#else
	DWORD dwAddressOfCommandLine;
	FARPROC pfnGetCommandLineA;
	HANDLE hThread;

	pfnGetCommandLineA = GetProcAddress(GetModuleHandle("KERNEL32.DLL"), "GetCommandLineA"); 

	hThread = CreateRemoteThread(hProcess, 0, 0, (LPTHREAD_START_ROUTINE)pfnGetCommandLineA, 0, 0, 0);
	if (hThread != INVALID_HANDLE_VALUE)
	{
		WaitForSingleObject(hThread, INFINITE);
		GetExitCodeThread(hThread, &dwAddressOfCommandLine);
		bRet = ReadProcessMemory(hProcess, (PVOID)dwAddressOfCommandLine, pszCmdLine, dwLen, &dummy);
		CloseHandle(hThread);
	}
#endif

	CloseHandle(hProcess);
	return bRet;
}


//
// Callback function for GetProcessWindows
//

static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
   DWORD dwPID;
	TCHAR szBuffer[MAX_PATH];

   if (GetWindowLong(hWnd,GWL_STYLE) & WS_VISIBLE) 
   {
		WINDOW_LIST *pList = (WINDOW_LIST *)lParam;
		if (pList == NULL)
			return FALSE;

		GetWindowThreadProcessId(hWnd, &dwPID);
		if (dwPID == pList->dwPID)
		{
			GetWindowText(hWnd, szBuffer, MAX_PATH - 1);
			szBuffer[MAX_PATH - 1] = 0;
			pList->pWndList->add(szBuffer);
		}
	}

   return TRUE;
}


//
// Get list of windows belonging to given process
//

static StringList *GetProcessWindows(DWORD dwPID)
{
	WINDOW_LIST list;
	
	list.dwPID = dwPID;
	list.pWndList = new StringList;

	EnumWindows(EnumWindowsProc, (LPARAM)&list);
	return list.pWndList;
}


//
// Match process to search criteria
//

static BOOL MatchProcess(DWORD dwPID, HANDLE hProcess, HMODULE hModule, BOOL bExtMatch,
								 char *pszModName, char *pszCmdLine, char *pszWindowName)
{
   char szBaseName[MAX_PATH];
	int i;
	BOOL bRet;

   GetModuleBaseName(hProcess, hModule, szBaseName, sizeof(szBaseName));
	if (bExtMatch)	// Extended version
	{
		char commandLine[MAX_PATH];
		BOOL bProcMatch, bCmdMatch, bWindowMatch;

		bProcMatch = bCmdMatch = bWindowMatch = TRUE;

		if (pszCmdLine[0] != 0)		// not empty, check if match
		{
			memset(commandLine, 0, MAX_PATH);	
			GetProcessCommandLine(dwPID, commandLine, MAX_PATH);
			bCmdMatch = RegexpMatch(commandLine, pszCmdLine, FALSE);
		}

		if (pszModName[0] != 0)
		{
			bProcMatch = RegexpMatch(szBaseName, pszModName, FALSE);
		}

		if (pszWindowName[0] != 0)
		{
			StringList *pWndList;

			pWndList = GetProcessWindows(dwPID);
			for(i = 0, bWindowMatch = FALSE; i < pWndList->getSize(); i++)
			{
				if (RegexpMatch(pWndList->getValue(i), pszWindowName, FALSE))
				{
					bWindowMatch = TRUE;
					break;
				}
			}
			delete pWndList;
		}

	   bRet = bProcMatch && bCmdMatch && bWindowMatch;
	}
	else
	{
		bRet = !stricmp(szBaseName, pszModName);
	}
	return bRet;
}


//
// Get process-specific information
// Parameter has the following syntax:
//    Process.XXX(<process>,<type>,<cmdline>,<window>)
// where
//    XXX        - requested process attribute (see documentation for list of valid attributes)
//    <process>  - process name (same as in Process.Count() parameter)
//    <type>     - representation type (meaningful when more than one process with the same
//                 name exists). Valid values are:
//         min - minimal value among all processes named <process>
//         max - maximal value among all processes named <process>
//         avg - average value for all processes named <process>
//         sum - sum of values for all processes named <process>
//    <cmdline>  - command line
//    <window>   - window title
//

LONG H_ProcInfo(const char *cmd, const char *arg, char *value)
{
   char buffer[256], procName[MAX_PATH], cmdLine[MAX_PATH], windowTitle[MAX_PATH];
   int attr, type, i, procCount, counter;
   unsigned __int64 attrVal;
   DWORD *pdwProcList, dwSize;
   HMODULE *modList;
   static char *typeList[]={ "min", "max", "avg", "sum", NULL };

   // Check attribute
   attr = CAST_FROM_POINTER(arg, int);
   if (!IsAttributeSupported(attr))
      return SYSINFO_RC_UNSUPPORTED;     // Unsupported attribute

   // Get parameter type arguments
   AgentGetParameterArg(cmd, 2, buffer, 255);
   if (buffer[0] == 0)     // Omited type
   {
      type = INFOTYPE_SUM;
   }
   else
   {
      for(type = 0; typeList[type] != NULL; type++)
         if (!stricmp(typeList[type], buffer))
            break;
      if (typeList[type] == NULL)
         return SYSINFO_RC_UNSUPPORTED;     // Unsupported type
   }

   // Get process name
   AgentGetParameterArg(cmd, 1, procName, MAX_PATH - 1);
	AgentGetParameterArg(cmd, 3, cmdLine, MAX_PATH - 1);
	AgentGetParameterArg(cmd, 4, windowTitle, MAX_PATH - 1);

   // Gather information
   attrVal = 0;
   pdwProcList = (DWORD *)malloc(MAX_PROCESSES * sizeof(DWORD));
   modList = (HMODULE *)malloc(MAX_MODULES * sizeof(HMODULE));
   EnumProcesses(pdwProcList, sizeof(DWORD) * MAX_PROCESSES, &dwSize);
   procCount = dwSize / sizeof(DWORD);
   for(i = 0, counter = 0; i < procCount; i++)
   {
      HANDLE hProcess;

      hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pdwProcList[i]);
      if (hProcess != NULL)
      {
         if (EnumProcessModules(hProcess, modList, sizeof(HMODULE) * MAX_MODULES, &dwSize))
         {
            if (dwSize >= sizeof(HMODULE))     // At least one module exist
            {
					if (MatchProcess(pdwProcList[i], hProcess, modList[0],
					                 (cmdLine[0] != 0) || (windowTitle[0] != 0),
					                 procName, cmdLine, windowTitle))
					{
                  counter++;  // Number of processes with specific name
                  attrVal = GetProcessAttribute(hProcess, attr, type, counter, attrVal);
					}
            }
         }
         CloseHandle(hProcess);
      }
   }

   // Cleanup
   free(pdwProcList);
   free(modList);

   if (counter == 0)    // No processes with given name
      return SYSINFO_RC_ERROR;

   ret_uint64(value, attrVal);
   return SYSINFO_RC_SUCCESS;
}


//
// Handler for System.ProcessCount
//

LONG H_ProcCount(const char *cmd, const char *arg, char *value)
{
   DWORD dwSize, *pdwProcList;
   PERFORMANCE_INFORMATION pi;

   // On Windows XP and higher, use new method
   if (imp_GetPerformanceInfo != NULL)
   {
      pi.cb = sizeof(PERFORMANCE_INFORMATION);
      if (!imp_GetPerformanceInfo(&pi, sizeof(PERFORMANCE_INFORMATION)))
         return SYSINFO_RC_ERROR;
      ret_uint(value, pi.ProcessCount);
   }
   else
   {
      pdwProcList = (DWORD *)malloc(sizeof(DWORD) * MAX_PROCESSES);
      EnumProcesses(pdwProcList, sizeof(DWORD) * MAX_PROCESSES, &dwSize);
      free(pdwProcList);
      ret_int(value, dwSize / sizeof(DWORD));
   }
   return SYSINFO_RC_SUCCESS;
}


//
// Handler for Process.Count(*) and Process.CountEx(*)
//

LONG H_ProcCountSpecific(const char *cmd, const char *arg, char *value)
{
   DWORD dwSize = 0, *pdwProcList;
   int i, counter, procCount;
   char procName[MAX_PATH], cmdLine[MAX_PATH], windowTitle[MAX_PATH];
   HANDLE hProcess;
   HMODULE modList[MAX_MODULES];

   AgentGetParameterArg(cmd, 1, procName, MAX_PATH - 1);
	if (*arg == 'E')
	{
		AgentGetParameterArg(cmd, 2, cmdLine, MAX_PATH - 1);
		AgentGetParameterArg(cmd, 3, windowTitle, MAX_PATH - 1);

		// Check if all 3 parameters are empty
		if ((procName[0] == 0) && (cmdLine[0] == 0) && (windowTitle[0] == 0))
			return SYSINFO_RC_UNSUPPORTED;
	}

   pdwProcList = (DWORD *)malloc(sizeof(DWORD) * MAX_PROCESSES);
   EnumProcesses(pdwProcList, sizeof(DWORD) * MAX_PROCESSES, &dwSize);
   procCount = dwSize / sizeof(DWORD);
   for(i = 0, counter = 0; i < procCount; i++)
   {
      hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pdwProcList[i]);
      if (hProcess != NULL)
      {
         if (EnumProcessModules(hProcess, modList, sizeof(HMODULE) * MAX_MODULES, &dwSize))
         {
            if (dwSize >= sizeof(HMODULE))     // At least one module exist
            {
					if (MatchProcess(pdwProcList[i], hProcess, modList[0], *arg == 'E',
					                 procName, cmdLine, windowTitle))
						counter++;
				}
         }
         CloseHandle(hProcess);
      }
   }
   ret_int(value, counter);
   free(pdwProcList);
   return SYSINFO_RC_SUCCESS;
}


//
// Handler for System.ProcessList enum
//

LONG H_ProcessList(const char *cmd, const char *arg, StringList *value)
{
   DWORD i, dwSize, dwNumProc, *pdwProcList;
   LONG iResult = SYSINFO_RC_SUCCESS;
   TCHAR szBuffer[MAX_PATH + 64];
   HMODULE phModList[MAX_MODULES];
   HANDLE hProcess;

   pdwProcList = (DWORD *)malloc(sizeof(DWORD) * MAX_PROCESSES);
   if (EnumProcesses(pdwProcList, sizeof(DWORD) * MAX_PROCESSES, &dwSize))
   {
      dwNumProc = dwSize / sizeof(DWORD);
      for(i = 0; i < dwNumProc; i++)
      {
         hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pdwProcList[i]);
         if (hProcess != NULL)
         {
            if (EnumProcessModules(hProcess, phModList, sizeof(HMODULE) * MAX_MODULES, &dwSize))
            {
               if (dwSize >= sizeof(HMODULE))     // At least one module exist
               {
                  TCHAR szBaseName[MAX_PATH];

                  GetModuleBaseName(hProcess, phModList[0], szBaseName, MAX_PATH);
                  _sntprintf(szBuffer, MAX_PATH + 64, _T("%u %s"), pdwProcList[i], szBaseName);
						value->add(szBuffer);
               }
               else
               {
                  _sntprintf(szBuffer, MAX_PATH + 64, _T("%u <unknown>"), pdwProcList[i]);
                  value->add(szBuffer);
               }
            }
            else
            {
               if (pdwProcList[i] == 4)
               {
                  value->add(_T("4 System"));
               }
               else
               {
                  _sntprintf(szBuffer, MAX_PATH + 64, _T("%u <unknown>"), pdwProcList[i]);
                  value->add(szBuffer);
               }
            }
            CloseHandle(hProcess);
         }
         else
         {
            if (pdwProcList[i] == 0)
            {
               value->add(_T("0 System Idle Process"));
            }
            else
            {
               _sntprintf(szBuffer, MAX_PATH + 64, _T("%lu <unaccessible>"), pdwProcList[i]);
               value->add(szBuffer);
            }
         }
      }
   }
   else
   {
      iResult = SYSINFO_RC_ERROR;
   }

   free(pdwProcList);
   return iResult;
}
