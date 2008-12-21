/*
** NetXMS UPS management subagent
** Copyright (C) 2006 Victor Kirhenshtein
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
** File: sms.h
**
**/

#ifndef _sms_h_
#define _sms_h_

#include <nms_common.h>
#include <nms_util.h>
#include <nms_agent.h>


//
// Functions
//

BOOL InitSender(TCHAR *pszInitArgs);
void ShutdownSender(void);
BOOL SendSMS(TCHAR *pszPhoneNumber, TCHAR *pszText);


//
// Global variables
//

extern TCHAR g_szDeviceModel[];


#endif
