/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2009 Victor Kirhenshtein
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
** File: agent_policy.cpp
**
**/

#include "nxcore.h"


//
// Agent policy default constructor
//

AgentPolicyConfig::AgentPolicyConfig()
                  : AgentPolicy(AGENT_POLICY_CONFIG)
{
	m_fileName[0] = 0;
	m_fileContent = NULL;
}


//
// Constructor for user-initiated object creation
//

AgentPolicyConfig::AgentPolicyConfig(const TCHAR *name)
                  : AgentPolicy(name, AGENT_POLICY_CONFIG)
{
	m_fileName[0] = 0;
	m_fileContent = NULL;
}


//
// Destructor
//

AgentPolicyConfig::~AgentPolicyConfig()
{
	safe_free(m_fileContent);
}


//
// Save to database
//

BOOL AgentPolicyConfig::SaveToDB(DB_HANDLE hdb)
{
	LockData();

	BOOL success = SavePolicyCommonProperties(hdb);
	if (success)
	{
		TCHAR *data = EncodeSQLString(CHECK_NULL_EX(m_fileContent));
		int len = _tcslen(data) + MAX_POLICY_CONFIG_NAME + 256;
		TCHAR *query = (TCHAR *)malloc(len * sizeof(TCHAR));

		_sntprintf(query, len, _T("SELECT file_name FROM ap_config_files WHERE policy_id=%d"), m_dwId);
		DB_RESULT hResult = DBSelect(hdb, query);
		if (hResult != NULL)
		{
			BOOL isNew = (DBGetNumRows(hResult) > 0);
			DBFreeResult(hResult);

			if (isNew)
				_sntprintf(query, len, _T("INSERT INTO ap_config_files (policy_id,file_name,file_content) VALUES (%d,'%s','%s')"),
				           m_dwId, m_fileName, data);
			else
				_sntprintf(query, len, _T("UPDATE ap_config_files SET file_name='%s',file_content='%s' WHERE policy_id=%d"),
				           m_fileName, data, m_dwId);
			success = DBQuery(hdb, query);
		}
		free(query);
		free(data);
	}

	// Clear modifications flag and unlock object
	if (success)
		m_bIsModified = FALSE;
   UnlockData();

   return success;
}


//
// Delete from database
//

BOOL AgentPolicyConfig::DeleteFromDB()
{
	AgentPolicy::DeleteFromDB();

	TCHAR query[256];

	_sntprintf(query, 256, _T("DELETE FROM ap_config_files WHERE policy_id=%d"), m_dwId);
	QueueSQLRequest(query);

	return TRUE;
}


//
// Load from database
//

BOOL AgentPolicyConfig::CreateFromDB(DWORD dwId)
{
	BOOL success = FALSE;

	if (AgentPolicy::CreateFromDB(dwId))
	{
		TCHAR query[256];

		_sntprintf(query, 256, _T("SELECT file_name,file_content FROM ap_config_files WHERE policy_id=%d"), dwId);
		DB_RESULT hResult = DBSelect(g_hCoreDB, query);
		if (hResult != NULL)
		{
			if (DBGetNumRows(hResult) > 0)
			{
				DBGetField(hResult, 0, 0, m_fileName, MAX_POLICY_CONFIG_NAME);
				m_fileContent = DBGetField(hResult, 0, 1, NULL, 0);
			}
			DBFreeResult(hResult);
		}
	}
	return success;
}


//
// Create NXCP message with policy data
//

void AgentPolicyConfig::CreateMessage(CSCPMessage *msg)
{
	AgentPolicy::CreateMessage(msg);
	msg->SetVariable(VID_CONFIG_FILE_NAME, m_fileName);
	msg->SetVariable(VID_CONFIG_FILE_DATA, CHECK_NULL_EX(m_fileContent));
}


//
// Modify policy from message
//

DWORD AgentPolicyConfig::ModifyFromMessage(CSCPMessage *pRequest, BOOL bAlreadyLocked)
{
   if (!bAlreadyLocked)
      LockData();

	if (pRequest->IsVariableExist(VID_CONFIG_FILE_NAME))
		pRequest->GetVariableStr(VID_CONFIG_FILE_NAME, m_fileName, MAX_POLICY_CONFIG_NAME);

	if (pRequest->IsVariableExist(VID_CONFIG_FILE_DATA))
	{
		safe_free(m_fileContent);
		m_fileContent = pRequest->GetVariableStr(VID_CONFIG_FILE_DATA);
	}

	return AgentPolicy::ModifyFromMessage(pRequest, TRUE);
}


//
// Create deployment message
//

bool AgentPolicyConfig::createDeploymentMessage(CSCPMessage *msg)
{
	if (!AgentPolicy::createDeploymentMessage(msg))
		return false;

	if ((m_fileName[0] == 0) || (m_fileContent == NULL))
		return false;  // Policy cannot be deployed

	msg->SetVariable(VID_CONFIG_FILE_NAME, m_fileName);
	msg->SetVariable(VID_CONFIG_FILE_DATA, m_fileContent);
	return true;
}
