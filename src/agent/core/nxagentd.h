/* 
** NetXMS multiplatform core agent
** Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be usefu,,
** but ITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: nxagentd.h
**
**/

#ifndef _nxagentd_h_
#define _nxagentd_h_

#include <nms_common.h>
#include <nms_util.h>
#include <nms_threads.h>
#include <nms_agent.h>
#include <nms_cscp.h>
#include <stdio.h>
#include <stdarg.h>
#include <nxqueue.h>
#include <nxlog.h>
#include "messages.h"

#define LIBNXCL_NO_DECLARATIONS
#include <nxclapi.h>

#ifdef _NETWARE
#undef SEVERITY_CRITICAL
#endif


//
// Version
//

#ifdef _DEBUG
#define DEBUG_SUFFIX          "-debug"
#else
#define DEBUG_SUFFIX          ""
#endif
#define AGENT_VERSION_STRING  NETXMS_VERSION_STRING DEBUG_SUFFIX


//
// Default files
//

#if defined(_WIN32)
#define AGENT_DEFAULT_CONFIG     "C:\\nxagentd.conf"
#define AGENT_DEFAULT_CONFIG_D   "C:\\nxagentd.conf.d"
#define AGENT_DEFAULT_LOG        "C:\\nxagentd.log"
#define AGENT_DEFAULT_FILE_STORE "C:\\"
#define AGENT_DEFAULT_DATA_DIR   "C:\\"
#elif defined(_NETWARE)
#define AGENT_DEFAULT_CONFIG     "SYS:ETC/nxagentd.conf"
#define AGENT_DEFAULT_CONFIG_D   "SYS:ETC/nxagentd.conf.d"
#define AGENT_DEFAULT_LOG        "SYS:ETC/nxagentd.log"
#define AGENT_DEFAULT_FILE_STORE "SYS:\\"
#define AGENT_DEFAULT_DATA_DIR   "SYS:ETC"
#elif defined(_IPSO)
#define AGENT_DEFAULT_CONFIG     "/opt/netxms/etc/nxagentd.conf"
#define AGENT_DEFAULT_CONFIG_D   "/opt/netxms/etc/nxagentd.conf.d"
#define AGENT_DEFAULT_LOG        "/opt/netxms/log/nxagentd.log"
#define AGENT_DEFAULT_FILE_STORE "/opt/netxms/store"
#define AGENT_DEFAULT_DATA_DIR   "/opt/netxms/data"
#else
#define AGENT_DEFAULT_CONFIG     "{search}"
#define AGENT_DEFAULT_CONFIG_D   "{search}"
#define AGENT_DEFAULT_LOG        "/var/log/nxagentd"
#define AGENT_DEFAULT_FILE_STORE "/tmp"
#define AGENT_DEFAULT_DATA_DIR   "/var/opt/netxms/agent"
#endif

#define REGISTRY_FILE_NAME       "registry.dat"


//
// Constants
//

#ifdef _WIN32
#define DEFAULT_AGENT_SERVICE_NAME    _T("NetXMSAgentdW32")
#define DEFAULT_AGENT_EVENT_SOURCE    _T("NetXMS Win32 Agent")
#define NXAGENTD_SYSLOG_NAME          g_windowsEventSourceName
#else
#define NXAGENTD_SYSLOG_NAME          _T("nxagentd")
#endif

#define MAX_PSUFFIX_LENGTH 32
#define MAX_SERVERS        32

#define AF_DAEMON                   0x00000001
#define AF_USE_SYSLOG               0x00000002
//obsolete: #define AF_DEBUG                    0x00000004
#define AF_REQUIRE_AUTH             0x00000008
#define AF_LOG_UNRESOLVED_SYMBOLS   0x00000010
#define AF_ENABLE_ACTIONS           0x00000020
#define AF_REQUIRE_ENCRYPTION       0x00000040
#define AF_HIDE_WINDOW              0x00000080
#define AF_ENABLE_AUTOLOAD          0x00000100
#define AF_ENABLE_PROXY             0x00000200
#define AF_CENTRAL_CONFIG           0x00000400
#define AF_ENABLE_SNMP_PROXY			0x00000800
#define AF_SHUTDOWN                 0x00001000
#define AF_RUNNING_ON_NT4           0x00002000
#define AF_REGISTER                 0x00004000
#define AF_ENABLE_WATCHDOG          0x00008000
#define AF_CATCH_EXCEPTIONS         0x00010000
#define AF_WRITE_FULL_DUMP          0x00020000


#ifdef _WIN32


//
// Attributes for H_NetIPStats and H_NetInterfacStats
//

#define NETINFO_IP_FORWARDING        1

#define NETINFO_IF_BYTES_IN          1
#define NETINFO_IF_BYTES_OUT         2
#define NETINFO_IF_DESCR             3
#define NETINFO_IF_IN_ERRORS         4
#define NETINFO_IF_OPER_STATUS       5
#define NETINFO_IF_OUT_ERRORS        6
#define NETINFO_IF_PACKETS_IN        7
#define NETINFO_IF_PACKETS_OUT       8
#define NETINFO_IF_SPEED             9
#define NETINFO_IF_ADMIN_STATUS      10
#define NETINFO_IF_MTU               11


//
// Request types for H_MemoryInfo
//

#define MEMINFO_PHYSICAL_FREE       1
#define MEMINFO_PHYSICAL_FREE_PCT   2
#define MEMINFO_PHYSICAL_TOTAL      3
#define MEMINFO_PHYSICAL_USED       4
#define MEMINFO_PHYSICAL_USED_PCT   5
#define MEMINFO_VIRTUAL_FREE        6
#define MEMINFO_VIRTUAL_FREE_PCT    7
#define MEMINFO_VIRTUAL_TOTAL       8
#define MEMINFO_VIRTUAL_USED        9
#define MEMINFO_VIRTUAL_USED_PCT    10


//
// Request types for H_DiskInfo
//

#define DISKINFO_FREE_BYTES      1
#define DISKINFO_USED_BYTES      2
#define DISKINFO_TOTAL_BYTES     3
#define DISKINFO_FREE_SPACE_PCT  4
#define DISKINFO_USED_SPACE_PCT  5

#endif   /* _WIN32 */


//
// Request types for H_DirInfo
//

#define DIRINFO_FILE_COUNT       1
#define DIRINFO_FILE_SIZE        2


//
// Request types for H_FileTime
//
 
#define FILETIME_ATIME           1
#define FILETIME_MTIME           2
#define FILETIME_CTIME           3


//
// Action types
//

#define AGENT_ACTION_EXEC        1
#define AGENT_ACTION_SUBAGENT    2
#define AGENT_ACTION_SHELLEXEC	3


//
// Action definition structure
//

typedef struct
{
   TCHAR szName[MAX_PARAM_NAME];
   int iType;
   union
   {
      char *pszCmdLine;
      struct __subagentAction
      {
         LONG (*fpHandler)(const TCHAR *, StringList *, const TCHAR *);
         const TCHAR *pArg;
         TCHAR szSubagentName[MAX_PATH];
      } sa;
   } handler;
   TCHAR szDescription[MAX_DB_STRING];
} ACTION;


//
// Loaded subagent information
//

struct SUBAGENT
{
   HMODULE hModule;              // Subagent's module handle
   NETXMS_SUBAGENT_INFO *pInfo;  // Information provided by subagent
   char szName[MAX_PATH];        // Name of the module
#ifdef _NETWARE
   char szEntryPoint[256];       // Name of agent's entry point
#endif
};


//
// Server information
//

struct SERVER_INFO
{
   DWORD dwIpAddr;
   BOOL bMasterServer;
   BOOL bControlServer;
};


//
// Communication session
//

class CommSession
{
private:
   SOCKET m_hSocket;
   Queue *m_pSendQueue;
   Queue *m_pMessageQueue;
   CSCP_BUFFER *m_pMsgBuffer;
   THREAD m_hWriteThread;
   THREAD m_hProcessingThread;
   THREAD m_hProxyReadThread;
   DWORD m_dwHostAddr;        // IP address of connected host (network byte order)
   DWORD m_dwIndex;
   BOOL m_bIsAuthenticated;
   BOOL m_bMasterServer;
   BOOL m_bControlServer;
   BOOL m_bProxyConnection;
   BOOL m_bAcceptTraps;
   int m_hCurrFile;
   DWORD m_dwFileRqId;
   CSCP_ENCRYPTION_CONTEXT *m_pCtx;
   time_t m_ts;               // Last activity timestamp
   SOCKET m_hProxySocket;     // Socket for proxy connection

   BOOL sendRawMessage(CSCP_MESSAGE *pMsg, CSCP_ENCRYPTION_CONTEXT *pCtx);
   void authenticate(CSCPMessage *pRequest, CSCPMessage *pMsg);
   void getConfig(CSCPMessage *pMsg);
   void updateConfig(CSCPMessage *pRequest, CSCPMessage *pMsg);
   void getParameter(CSCPMessage *pRequest, CSCPMessage *pMsg);
   void getList(CSCPMessage *pRequest, CSCPMessage *pMsg);
   void action(CSCPMessage *pRequest, CSCPMessage *pMsg);
   void recvFile(CSCPMessage *pRequest, CSCPMessage *pMsg);
   DWORD upgrade(CSCPMessage *pRequest);
   DWORD setupProxyConnection(CSCPMessage *pRequest);

   void readThread();
   void writeThread();
   void processingThread();
   void proxyReadThread();

   static THREAD_RESULT THREAD_CALL readThreadStarter(void *);
   static THREAD_RESULT THREAD_CALL writeThreadStarter(void *);
   static THREAD_RESULT THREAD_CALL processingThreadStarter(void *);
   static THREAD_RESULT THREAD_CALL proxyReadThreadStarter(void *);

public:
   CommSession(SOCKET hSocket, DWORD dwHostAddr, BOOL bMasterServer, BOOL bControlServer);
   ~CommSession();

   void run();
   void disconnect();

   void sendMessage(CSCPMessage *pMsg) { m_pSendQueue->Put(pMsg->CreateMessage()); }
   void sendRawMessage(CSCP_MESSAGE *pMsg) { m_pSendQueue->Put(nx_memdup(pMsg, ntohl(pMsg->dwSize))); }
	bool sendFile(DWORD requestId, const TCHAR *file, long offset);

	DWORD getServerAddress() { return m_dwHostAddr; }

   DWORD getIndex() { return m_dwIndex; }
   void setIndex(DWORD dwIndex) { if (m_dwIndex == INVALID_INDEX) m_dwIndex = dwIndex; }

   time_t getTimeStamp() { return m_ts; }
	void updateTimeStamp() { m_ts = time(NULL); }

   BOOL canAcceptTraps() { return m_bAcceptTraps; }
};


//
// Functions
//

BOOL Initialize();
void Shutdown();
void Main();

void ConsolePrintf(const char *pszFormat, ...);
void DebugPrintf(DWORD dwSessionId, int level, const char *pszFormat, ...);

void BuildFullPath(TCHAR *pszFileName, TCHAR *pszFullPath);

BOOL DownloadConfig(TCHAR *pszServer);

BOOL InitParameterList();
void AddParameter(const char *szName, LONG (* fpHandler)(const char *, const char *, char *), const char *pArg,
                  int iDataType, const char *pszDescription);
void AddPushParameter(const TCHAR *name, int dataType, const TCHAR *description);
void AddEnum(const char *szName, LONG (* fpHandler)(const char *, const char *, StringList *), const char *pArg);
BOOL AddExternalParameter(char *pszCfgLine, BOOL bShellExec);
DWORD GetParameterValue(DWORD dwSessionId, char *pszParam, char *pszValue);
DWORD GetEnumValue(DWORD dwSessionId, char *pszParam, StringList *pValue);
void GetParameterList(CSCPMessage *pMsg);

BOOL LoadSubAgent(char *szModuleName);
void UnloadAllSubAgents();
BOOL InitSubAgent(HMODULE hModule, TCHAR *pszModuleName,
                  BOOL (* SubAgentInit)(NETXMS_SUBAGENT_INFO **, TCHAR *),
                  TCHAR *pszEntryPoint);
BOOL ProcessCmdBySubAgent(DWORD dwCommand, CSCPMessage *pRequest, CSCPMessage *pResponse, void *session);

BOOL AddAction(const char *pszName, int iType, const char *pArg, 
               LONG (*fpHandler)(const TCHAR *, StringList *, const TCHAR *),
               const char *pszSubAgent, const char *pszDescription);
BOOL AddActionFromConfig(char *pszLine, BOOL bShellExec);
DWORD ExecAction(char *pszAction, StringList *pArgs);

DWORD ExecuteCommand(char *pszCommand, StringList *pArgs, pid_t *pid);
DWORD ExecuteShellCommand(char *pszCommand, StringList *pArgs);

BOOL WaitForProcess(const TCHAR *name);

DWORD UpgradeAgent(TCHAR *pszPkgFile);

void SendTrap(DWORD dwEventCode, int iNumArgs, TCHAR **ppArgList);
void SendTrap(DWORD dwEventCode, const char *pszFormat, ...);
void SendTrap(DWORD dwEventCode, const char *pszFormat, va_list args);

Config *OpenRegistry();
void CloseRegistry(bool modified);

#ifdef _WIN32

void InitService();
void InstallService(char *execName, char *confFile);
void RemoveService();
void StartAgentService();
void StopAgentService();
BOOL WaitForService(DWORD dwDesiredState);
void InstallEventSource(char *path);
void RemoveEventSource();

char *GetPdhErrorText(DWORD dwError, char *pszBuffer, int iBufSize);

#endif


//
// Global variables
//

extern DWORD g_dwFlags;
extern char g_szLogFile[];
extern char g_szSharedSecret[];
extern char g_szConfigFile[];
extern char g_szFileStore[];
extern char g_szConfigServer[];
extern char g_szRegistrar[];
extern char g_szListenAddress[];
extern TCHAR g_szConfigIncludeDir[];
extern WORD g_wListenPort;
extern SERVER_INFO g_pServerList[];
extern DWORD g_dwServerCount;
extern time_t g_tmAgentStartTime;
extern char g_szPlatformSuffix[];
extern DWORD g_dwStartupDelay;
extern DWORD g_dwIdleTimeout;
extern DWORD g_dwMaxSessions;
extern DWORD g_dwExecTimeout;
extern DWORD g_dwSNMPTimeout;
extern DWORD g_debugLevel;

extern Config *g_config;

extern DWORD g_dwAcceptErrors;
extern DWORD g_dwAcceptedConnections;
extern DWORD g_dwRejectedConnections;

extern CommSession **g_pSessionList;
extern MUTEX g_hSessionListAccess;

#ifdef _WIN32
extern BOOL (__stdcall *imp_GlobalMemoryStatusEx)(LPMEMORYSTATUSEX);
extern DWORD (__stdcall *imp_HrLanConnectionNameFromGuidOrPath)(LPWSTR, LPWSTR, LPWSTR, LPDWORD);

extern TCHAR g_windowsEventSourceName[];
#endif   /* _WIN32 */

#endif   /* _nxagentd_h_ */
