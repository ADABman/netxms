/* 
** NetXMS - Network Management System
** nxalarm - manage alarms from command line
** Copyright (C) 2003, 2004, 2005, 206, 2007, 2008 Victor Kirhenshtein
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
** File: nxalarm.cpp
**
**/

#include "nxalarm.h"


//
// Static data
//

static TCHAR m_outFormat[MAX_DB_STRING] = _T("%I %S %H %m");


//
// List alarms
//

static DWORD ListAlarms(NXC_SESSION session)
{
	DWORD i, numAlarms, rcc;
	NXC_ALARM *alarmList;
	TCHAR *text;

	rcc = NXCLoadEventDB(session);
	if (rcc != RCC_SUCCESS)
		_tprintf(_T("WARNING: cannot load event database (%s)\n"), NXCGetErrorText(rcc));

	rcc = NXCLoadAllAlarms(session, FALSE, &numAlarms, &alarmList);
	if (rcc == RCC_SUCCESS)
	{
		for(i = 0; i < numAlarms; i++)
		{
			text = NXCFormatAlarmText(session, &alarmList[i], m_outFormat);
			_tprintf(_T("%s\n"), text);
			free(text);
		}
		_tprintf(_T("\n%d active alarms\n"), numAlarms);
	}
	else
	{
		_tprintf(_T("Cannot get alarm list: %s\n"), NXCGetErrorText(rcc));
	}
	return rcc;
}


//
// Callback function for debug printing
//

static void DebugCallback(TCHAR *pMsg)
{
   _tprintf(_T("*debug* %s\n"), pMsg);
}


//
// main
//

int main(int argc, char *argv[])
{
	TCHAR *eptr, login[MAX_DB_STRING] = _T("guest"),
	      password[MAX_DB_STRING] = _T("");
	BOOL isDebug = FALSE, isEncrypt = FALSE;
	DWORD rcc, alarmId, timeout = 3;
   NXC_SESSION session;
	int ch;

   // Parse command line
   opterr = 1;
   while((ch = getopt(argc, argv, "Deho:P:u:vw:")) != -1)
   {
      switch(ch)
      {
         case 'h':   // Display help and exit
            printf("Usage: nxalarm [<options>] <server> <command> [<alarm_id>]\n"
				       "Possible commands are:\n"
						 "   ack <id>       : Acknowlege alarm\n"
						 "   close <id>     : Notify system that incident associated with alarm is closed\n"
						 "   list           : List active alarms\n"
						 "   terminate <id> : Terminate alarm\n"
                   "Valid options are:\n"
                   "   -D             : Turn on debug mode.\n"
                   "   -e             : Encrypt session.\n"
                   "   -h             : Display help and exit.\n"
						 "   -o <format>    : Output format for list (see below).\n"
                   "   -P <password>  : Specify user's password. Default is empty password.\n"
                   "   -u <user>      : Login to server as <user>. Default is \"guest\".\n"
                   "   -v             : Display version and exit.\n"
                   "   -w <seconds>   : Specify command timeout (default is 3 seconds).\n"
						 "Output format string syntax:\n"
						 "   %%a Primary IP of source object\n"
						 "   %%e Event code\n"
						 "   %%E Event name\n"
						 "   %%h Helpdesk state as number\n"
						 "   %%H Helpdesk state as text\n"
						 "   %%i Source object identifier\n"
						 "   %%I Alarm identifier\n"
						 "   %%m Message text\n"
						 "   %%n Source object name\n"
						 "   %%s Severity as number\n"
						 "   %%S Severity as text\n"
						 "   %%%% Percent sign\n"
                   "\n");
            return 1;
         case 'D':
            isDebug = TRUE;
            break;
         case 'e':
            isEncrypt = TRUE;
            break;
         case 'u':
            nx_strncpy(login, optarg, MAX_DB_STRING);
            break;
         case 'P':
            nx_strncpy(password, optarg, MAX_DB_STRING);
            break;
         case 'o':
            nx_strncpy(m_outFormat, optarg, MAX_DB_STRING);
            break;
         case 'w':
            timeout = _tcstoul(optarg, NULL, 0);
            if ((timeout < 1) || (timeout > 120))
            {
               _tprintf(_T("Invalid timeout %s\n"), optarg);
               return 1;
            }
            break;
         case 'v':
            printf("NetXMS Alarm Control  Version " NETXMS_VERSION_STRING "\n");
            return 1;
         case '?':
            return 1;
         default:
            break;
		}
	}

   if (argc - optind < 2)
   {
      _tprintf(_T("Required arguments missing. Use nxalarm -h for help.\n"));
		return 1;
	}

	// All commands except "list" requirs alarm id
   if (_tcsicmp(argv[optind + 1], _T("list")) && (argc - optind < 3))
   {
      _tprintf(_T("Required arguments missing. Use nxalarm -h for help.\n"));
		return 1;
	}

#ifdef _WIN32
   WSADATA wsaData;

   if (WSAStartup(2, &wsaData) != 0)
   {
      _tprintf(_T("Unable to initialize Windows sockets\n"));
      return 4;
   }
#endif

   if (!NXCInitialize())
   {
      _tprintf(_T("Failed to initialize NetXMS client library\n"));
		return 2;
   }

   if (isDebug)
      NXCSetDebugCallback(DebugCallback);

	rcc = NXCConnect(isEncrypt ? NXCF_ENCRYPT : 0, argv[optind], login,
		              password, 0, NULL, NULL, &session,
                    _T("nxalarm/") NETXMS_VERSION_STRING, NULL);
	if (rcc != RCC_SUCCESS)
	{
		_tprintf(_T("Unable to connect to server: %s\n"), NXCGetErrorText(rcc));
		return 2;
	}

	NXCSetCommandTimeout(session, timeout * 1000);

	// Execute command
	if (!_tcsicmp(argv[optind + 1], _T("list")))
	{
		rcc = ListAlarms(session);
	}
	else
	{
		alarmId = _tcstoul(argv[optind + 2], &eptr, 0);
		if ((*eptr != 0) || (alarmId == 0))
		{
			_tprintf(_T("Invalid alarm ID \"%s\"\n"), argv[optind + 2]);
			NXCDisconnect(session);
			return 1;
		}
		if (!_tcsicmp(argv[optind + 1], _T("ack")))
		{
			rcc = NXCAcknowledgeAlarm(session, alarmId);
			if (rcc != RCC_SUCCESS)
				_tprintf(_T("Cannot acknowledge alarm: %s\n"), NXCGetErrorText(rcc));
		}
		else if (!_tcsicmp(argv[optind + 1], _T("close")))
		{
			rcc = NXCCloseAlarm(session, alarmId);
			if (rcc != RCC_SUCCESS)
				_tprintf(_T("Cannot close alarm: %s\n"), NXCGetErrorText(rcc));
		}
		else if (!_tcsicmp(argv[optind + 1], _T("terminate")))
		{
			rcc = NXCTerminateAlarm(session, alarmId);
			if (rcc != RCC_SUCCESS)
				_tprintf(_T("Cannot terminate alarm: %s\n"), NXCGetErrorText(rcc));
		}
		else
		{
			_tprintf(_T("Invalid command \"%s\"\n"), argv[optind + 1]);
			NXCDisconnect(session);
			return 1;
		}
	}

   NXCDisconnect(session);
	return (rcc == RCC_SUCCESS) ? 0 : 5;
}
