/*
** NetXMS SMS sender subagent
** Copyright (C) 2007 Victor Kirhenshtein
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
** File: main.cpp
**
**/

#include "sms.h"


//
// Global variables
//

TCHAR g_szDeviceModel[256] = _T("<unknown>");


//
// Static variables
//

static TCHAR m_szDevice[MAX_PATH];


//
// Handler for SMS.SerialConfig and SMS.DeviceModel
//

static LONG H_StringConst(const TCHAR *pszParam, const TCHAR *pArg, TCHAR *pValue)
{
	ret_string(pValue, pArg);
	return SYSINFO_RC_SUCCESS;
}


//
// Handler for SMS.Send action
//

static LONG H_SendSMS(const TCHAR *pszAction, NETXMS_VALUES_LIST *pArgs, const TCHAR *pData)
{
	if (pArgs->dwNumStrings < 2)
		return ERR_BAD_ARGUMENTS;

	return SendSMS(pArgs->ppStringList[0], pArgs->ppStringList[1]) ? ERR_SUCCESS : ERR_INTERNAL_ERROR;
}


//
// Subagent initialization
//

static BOOL SubAgentInit(Config *config)
{
	// Parse configuration
	const TCHAR *value = config->getValue(_T("/SMS/Device"));
	if (value != NULL)
	{
		nx_strncpy(m_szDevice, value, MAX_PATH);
		if (!InitSender(m_szDevice))
			return FALSE;
	}
	else
	{
		NxWriteAgentLog(EVENTLOG_ERROR_TYPE, _T("SMS: device not specified"));
	}

	return value != NULL;
}


//
// Called by master agent at unload
//

static void SubAgentShutdown(void)
{
	ShutdownSender();
}


//
// Subagent information
//

static NETXMS_SUBAGENT_PARAM m_parameters[] =
{
	{ _T("SMS.DeviceModel"), H_StringConst, g_szDeviceModel, DCI_DT_STRING, _T("Current serial port configuration") },
	{ _T("SMS.SerialConfig"), H_StringConst, m_szDevice, DCI_DT_STRING, _T("Current serial port configuration") }
};
static NETXMS_SUBAGENT_ACTION m_actions[] =
{
	{ _T("SMS.Send"), H_SendSMS, NULL, _T("Send SMS") }
};

static NETXMS_SUBAGENT_INFO m_info =
{
	NETXMS_SUBAGENT_INFO_MAGIC,
	_T("SMS"), _T(NETXMS_VERSION_STRING),
	SubAgentInit, SubAgentShutdown, NULL,
	sizeof(m_parameters) / sizeof(NETXMS_SUBAGENT_PARAM),
	m_parameters,
	0, NULL,
	sizeof(m_actions) / sizeof(NETXMS_SUBAGENT_ACTION),
	m_actions,
};


//
// Entry point for NetXMS agent
//

DECLARE_SUBAGENT_ENTRY_POINT(SMS)
{
	*ppInfo = &m_info;
	return TRUE;
}


//
// DLL entry point
//

#ifdef _WIN32

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(hInstance);
	return TRUE;
}

#endif
