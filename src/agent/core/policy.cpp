/* 
** NetXMS multiplatform core agent
** Copyright (C) 2003-2010 Victor Kirhenshtein
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
** File: policy.cpp
**
**/

#include "nxagentd.h"

#ifdef _WIN32
#define write _write
#define close _close
#endif

#define POLICY_REGISTRY_PATH _T("/policyRegistry")


//
// Register policy in persistent storage
//

static void RegisterPolicy(CommSession *session, DWORD type, uuid_t guid)
{
	TCHAR path[256], buffer[64];
	int tail;

	_sntprintf(path, 256, _T("/policyRegistry/%s/"), uuid_to_string(guid, buffer));
	tail = (int)_tcslen(path);

	Config *registry = OpenRegistry();

	_tcscpy(&path[tail], _T("type"));
	registry->setValue(path, type);

	_tcscpy(&path[tail], _T("server"));
	registry->setValue(path, IpToStr(session->getServerAddress(), buffer));

	CloseRegistry(true);
}


//
// Register policy in persistent storage
//

static void UnregisterPolicy(DWORD type, uuid_t guid)
{
	TCHAR path[256], buffer[64];

	_sntprintf(path, 256, _T("/policyRegistry/%s"), uuid_to_string(guid, buffer));
	Config *registry = OpenRegistry();
	registry->deleteEntry(path);
	CloseRegistry(true);
}


//
// Deploy configuration file
//

static DWORD DeployConfig(DWORD session, uuid_t guid, CSCPMessage *msg)
{
	TCHAR path[MAX_PATH], name[64], tail;
	int fh;
	DWORD rcc;

	tail = g_szConfigIncludeDir[_tcslen(g_szConfigIncludeDir) - 1];
	_sntprintf(path, MAX_PATH, _T("%s%s%s.conf"), g_szConfigIncludeDir,
	           ((tail != '\\') && (tail != '/')) ? FS_PATH_SEPARATOR : _T(""),
				  uuid_to_string(guid, name));

	fh = _topen(path, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR);
	if (fh != -1)
	{
		DWORD size = msg->GetVariableBinary(VID_CONFIG_FILE_DATA, 0, NULL);
		BYTE *data = (BYTE *)malloc(size);
		if (data != NULL)
		{
			msg->GetVariableBinary(VID_CONFIG_FILE_DATA, data, size);
			if (write(fh, data, size) == size)
			{
		      DebugPrintf(session, 3, _T("Configuration file %s saved successfully"), path);
				rcc = ERR_SUCCESS;
			}
			else
			{
				rcc = ERR_IO_FAILURE;
			}
			free(data);
		}
		else
		{
			rcc = ERR_MEM_ALLOC_FAILED;
		}
		close(fh);
	}
	else
	{
		DebugPrintf(session, 2, _T("DeployConfig(): Error opening file %s for writing (%s)"), path, _tcserror(errno));
		rcc = ERR_FILE_OPEN_ERROR;
	}

	return rcc;
}


//
// Deploy policy on agent
//

DWORD DeployPolicy(CommSession *session, CSCPMessage *request)
{
	DWORD type, rcc;
	uuid_t guid;

	type = request->GetVariableShort(VID_POLICY_TYPE);
	request->GetVariableBinary(VID_GUID, guid, UUID_LENGTH);

	switch(type)
	{
		case AGENT_POLICY_CONFIG:
			rcc = DeployConfig(session->getIndex(), guid, request);
			break;
		default:
			rcc = ERR_BAD_ARGUMENTS;
			break;
	}

	if (rcc == RCC_SUCCESS)
		RegisterPolicy(session, type, guid);

	DebugPrintf(session->getIndex(), 3, _T("Policy deployment: TYPE=%d RCC=%d"), type, rcc);
	return rcc;
}


//
// Remove configuration file
//

static DWORD RemoveConfig(DWORD session, uuid_t guid,  CSCPMessage *msg)
{
	TCHAR path[MAX_PATH], name[64], tail;
	DWORD rcc;

	tail = g_szConfigIncludeDir[_tcslen(g_szConfigIncludeDir) - 1];
	_sntprintf(path, MAX_PATH, _T("%s%s%s.conf"), g_szConfigIncludeDir,
	           ((tail != '\\') && (tail != '/')) ? FS_PATH_SEPARATOR : _T(""),
				  uuid_to_string(guid, name));

	if (_tremove(path) != 0)
	{
		rcc = (errno == ENOENT) ? ERR_SUCCESS : ERR_IO_FAILURE;
	}
	else
	{
		rcc = ERR_SUCCESS;
	}
	return rcc;
}


//
// Uninstall policy from agent
//

DWORD UninstallPolicy(CommSession *session, CSCPMessage *request)
{
	DWORD type, rcc;
	uuid_t guid;

	type = request->GetVariableShort(VID_POLICY_TYPE);
	request->GetVariableBinary(VID_GUID, guid, UUID_LENGTH);

	switch(type)
	{
		case AGENT_POLICY_CONFIG:
			rcc = RemoveConfig(session->getIndex(), guid, request);
			break;
		default:
			rcc = ERR_BAD_ARGUMENTS;
			break;
	}

	if (rcc == RCC_SUCCESS)
		UnregisterPolicy(type, guid);

	DebugPrintf(session->getIndex(), 3, _T("Policy uninstall: TYPE=%d RCC=%d"), type, rcc);
	return rcc;
}
