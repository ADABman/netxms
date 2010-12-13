/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2010 Victor Kirhenshtein
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
** File: package.cpp
**
**/

#include "nxcore.h"


//
// Check if package with specific parameters already installed
//

BOOL IsPackageInstalled(TCHAR *pszName, TCHAR *pszVersion, TCHAR *pszPlatform)
{
   DB_RESULT hResult;
   TCHAR szQuery[1024], *pszEscName, *pszEscVersion, *pszEscPlatform;
   BOOL bResult = FALSE;

   pszEscName = EncodeSQLString(pszName);
   pszEscVersion = EncodeSQLString(pszVersion);
   pszEscPlatform = EncodeSQLString(pszPlatform);
   _sntprintf(szQuery, 1024, _T("SELECT pkg_id FROM agent_pkg WHERE ")
                             _T("pkg_name='%s' AND version='%s' AND platform='%s'"),
              pszEscName, pszEscVersion, pszEscPlatform);
   free(pszEscName);
   free(pszEscVersion);
   free(pszEscPlatform);

   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult != NULL)
   {
      bResult = (DBGetNumRows(hResult) > 0);
      DBFreeResult(hResult);
   }
   return bResult;
}


//
// Check if given package ID is valid
//

BOOL IsValidPackageId(DWORD dwPkgId)
{
   DB_RESULT hResult;
   TCHAR szQuery[256];
   BOOL bResult = FALSE;

   _sntprintf(szQuery, 256, _T("SELECT pkg_name FROM agent_pkg WHERE pkg_id=%d"), dwPkgId);
   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult != NULL)
   {
      bResult = (DBGetNumRows(hResult) > 0);
      DBFreeResult(hResult);
   }
   return bResult;
}


//
// Check if package file with given name exist
//

BOOL IsPackageFileExist(TCHAR *pszFileName)
{
   TCHAR szFullPath[MAX_PATH];

   _tcscpy(szFullPath, g_szDataDir);
   _tcscat(szFullPath, DDIR_PACKAGES);
   _tcscat(szFullPath, FS_PATH_SEPARATOR);
   _tcscat(szFullPath, pszFileName);
   return (_taccess(szFullPath, 0) == 0);
}


//
// Uninstall (remove) package from server
//

DWORD UninstallPackage(DWORD dwPkgId)
{
   TCHAR szQuery[256], szFileName[MAX_PATH], szBuffer[MAX_DB_STRING];
   DB_RESULT hResult;
   DWORD dwResult;

   _sntprintf(szQuery, 256, _T("SELECT pkg_file FROM agent_pkg WHERE pkg_id=%d"), dwPkgId);
   hResult = DBSelect(g_hCoreDB, szQuery);
   if (hResult != NULL)
   {
      if (DBGetNumRows(hResult) > 0)
      {
         // Delete file from directory
         _tcscpy(szFileName, g_szDataDir);
         _tcscat(szFileName, DDIR_PACKAGES);
         _tcscat(szFileName, FS_PATH_SEPARATOR);
         _tcscat(szFileName, CHECK_NULL_EX(DBGetField(hResult, 0, 0, szBuffer, MAX_DB_STRING)));
         if (_tunlink(szFileName) == 0)
         {
            // Delete record from database
            _sntprintf(szQuery, 256, _T("DELETE FROM agent_pkg WHERE pkg_id=%d"), dwPkgId);
            DBQuery(g_hCoreDB, szQuery);
            dwResult = RCC_SUCCESS;
         }
         else
         {
            dwResult = RCC_IO_ERROR;
         }
      }
      else
      {
         dwResult = RCC_INVALID_PACKAGE_ID;
      }
      DBFreeResult(hResult);
   }
   else
   {
      dwResult = RCC_DB_FAILURE;
   }
   return dwResult;
}


//
// Package deployment worker thread
//

static THREAD_RESULT THREAD_CALL DeploymentThread(void *pArg)
{
   DT_STARTUP_INFO *pStartup = (DT_STARTUP_INFO *)pArg;
   Node *pNode;
   CSCPMessage msg;
   BOOL bSuccess;
   AgentConnection *pAgentConn;
   const TCHAR *pszErrorMsg = _T("");
   DWORD dwMaxWait;

   // Read configuration
   dwMaxWait = ConfigReadULong(_T("AgentUpgradeWaitTime"), 600);
   if (dwMaxWait % 20 != 0)
      dwMaxWait += 20 - (dwMaxWait % 20);

   // Prepare notification message
   msg.SetCode(CMD_INSTALLER_INFO);
   msg.SetId(pStartup->dwRqId);

   while(1)
   {
      // Get node object for upgrade
      pNode = (Node *)pStartup->pQueue->Get();
      if (pNode == NULL)
         break;   // Queue is empty, exit
      bSuccess = FALSE;

      // Preset node id in notification message
      msg.SetVariable(VID_OBJECT_ID, pNode->Id());

      // Check if node is a management server itself
      if (!(pNode->getFlags() & NF_IS_LOCAL_MGMT))
      {
         // Change deployment status to "Initializing"
         msg.SetVariable(VID_DEPLOYMENT_STATUS, (WORD)DEPLOYMENT_STATUS_INITIALIZE);
         pStartup->pSession->sendMessage(&msg);

         // Create agent connection
         pAgentConn = pNode->createAgentConnection();
         if (pAgentConn != NULL)
         {
            BOOL bCheckOK = FALSE;
            TCHAR szBuffer[256];

            // Check if package can be deployed on target node
            if (!_tcsicmp(pStartup->szPlatform, _T("src")))
            {
               // Source package, check if target node
               // supports source packages
               if (pAgentConn->GetParameter(_T("Agent.SourcePackageSupport"), 32, szBuffer) == ERR_SUCCESS)
               {
                  bCheckOK = (_tcstol(szBuffer, NULL, 0) != 0);
               }
            }
            else
            {
               // Binary package, check target platform
               if (pAgentConn->GetParameter(_T("System.PlatformName"), 256, szBuffer) == ERR_SUCCESS)
               {
                  bCheckOK = !_tcsicmp(szBuffer, pStartup->szPlatform);
               }
            }

            if (bCheckOK)
            {
               // Change deployment status to "File Transfer"
               msg.SetVariable(VID_DEPLOYMENT_STATUS, (WORD)DEPLOYMENT_STATUS_TRANSFER);
               pStartup->pSession->sendMessage(&msg);

               // Upload package file to agent
               _tcscpy(szBuffer, g_szDataDir);
               _tcscat(szBuffer, DDIR_PACKAGES);
               _tcscat(szBuffer, FS_PATH_SEPARATOR);
               _tcscat(szBuffer, pStartup->szPkgFile);
               if (pAgentConn->UploadFile(szBuffer) == ERR_SUCCESS)
               {
                  if (pAgentConn->StartUpgrade(pStartup->szPkgFile) == ERR_SUCCESS)
                  {
                     BOOL bConnected = FALSE;
                     DWORD i;

                     // Disconnect from agent
                     pAgentConn->disconnect();

                     // Change deployment status to "Package installation"
                     msg.SetVariable(VID_DEPLOYMENT_STATUS, (WORD)DEPLOYMENT_STATUS_INSTALLATION);
                     pStartup->pSession->sendMessage(&msg);

                     // Wait for agent's restart
                     ThreadSleep(20);
                     for(i = 20; i < dwMaxWait; i += 20)
                     {
                        ThreadSleep(20);
                        if (pAgentConn->connect(g_pServerKey))
                        {
                           bConnected = TRUE;
                           break;   // Connected successfully
                        }
                     }

                     // Last attempt to reconnect
                     if (!bConnected)
                        bConnected = pAgentConn->connect(g_pServerKey);

                     if (bConnected)
                     {
                        // Check version
                        if (pAgentConn->GetParameter(_T("Agent.Version"), MAX_AGENT_VERSION_LEN, szBuffer) == ERR_SUCCESS)
                        {
                           if (!_tcsicmp(szBuffer, pStartup->szVersion))
                           {
                              bSuccess = TRUE;
                           }
                           else
                           {
                              pszErrorMsg = _T("Agent's version doesn't match package version after upgrade");
                           }
                        }
                        else
                        {
                           pszErrorMsg = _T("Unable to get agent's version after upgrade");
                        }
                     }
                     else
                     {
                        pszErrorMsg = _T("Unable to contact agent after upgrade");
                     }
                  }
                  else
                  {
                     pszErrorMsg = _T("Unable to start upgrade process");
                  }
               }
               else
               {
                  pszErrorMsg = _T("File transfer failed");
               }
            }
            else
            {
               pszErrorMsg = _T("Package is not compatible with target machine");
            }

            delete pAgentConn;
         }
         else
         {
            pszErrorMsg = _T("Unable to connect to agent");
         }
      }
      else
      {
         pszErrorMsg = _T("Management server cannot deploy agent to itself");
      }

      // Finish node processing
      msg.SetVariable(VID_DEPLOYMENT_STATUS, 
         bSuccess ? (WORD)DEPLOYMENT_STATUS_COMPLETED : (WORD)DEPLOYMENT_STATUS_FAILED);
      msg.SetVariable(VID_ERROR_MESSAGE, pszErrorMsg);
      pStartup->pSession->sendMessage(&msg);
      pNode->DecRefCount();
   }
   return THREAD_OK;
}


//
// Package deployment thread
//

THREAD_RESULT THREAD_CALL DeploymentManager(void *pArg)
{
   DT_STARTUP_INFO *pStartup = (DT_STARTUP_INFO *)pArg;
   DWORD i, dwNumThreads;
   CSCPMessage msg;
   Queue *pQueue;
   THREAD *pThreadList;

   // Wait for parent initialization completion
   MutexLock(pStartup->mutex, INFINITE);
   MutexUnlock(pStartup->mutex);

   // Sanity check
   if (pStartup->dwNumNodes == 0)
   {
      pStartup->pSession->decRefCount();
      return THREAD_OK;
   }

   // Read number of upgrade threads
   dwNumThreads = ConfigReadInt(_T("NumberOfUpgradeThreads"), 10);
   if (dwNumThreads > pStartup->dwNumNodes)
      dwNumThreads = pStartup->dwNumNodes;

   // Create processing queue
   pQueue = new Queue;
   pStartup->pQueue = pQueue;

   // Send initial status for each node and queue them for deployment
   msg.SetCode(CMD_INSTALLER_INFO);
   msg.SetId(pStartup->dwRqId);
   for(i = 0; i < pStartup->dwNumNodes; i++)
   {
      pQueue->Put(pStartup->ppNodeList[i]);
      msg.SetVariable(VID_OBJECT_ID, pStartup->ppNodeList[i]->Id());
      msg.SetVariable(VID_DEPLOYMENT_STATUS, (WORD)DEPLOYMENT_STATUS_PENDING);
      pStartup->pSession->sendMessage(&msg);
      msg.DeleteAllVariables();
   }

   // Start worker threads
   pThreadList = (THREAD *)malloc(sizeof(THREAD) * dwNumThreads);
   for(i = 0; i < dwNumThreads; i++)
      pThreadList[i] = ThreadCreateEx(DeploymentThread, 0, pStartup);

   // Wait for all worker threads termination
   for(i = 0; i < dwNumThreads; i++)
      ThreadJoin(pThreadList[i]);

   // Send final notification to client
   msg.SetVariable(VID_DEPLOYMENT_STATUS, (WORD)DEPLOYMENT_STATUS_FINISHED);
   pStartup->pSession->sendMessage(&msg);
   pStartup->pSession->decRefCount();

   // Cleanup
   MutexDestroy(pStartup->mutex);
   safe_free(pStartup->ppNodeList);
   free(pStartup);
   free(pThreadList);
   delete pQueue;

   return THREAD_OK;
}
