/*
 ** NetXMS subagent for SunOS/Solaris
 ** Copyright (C) 2004-2010 Victor Kirhenshtein
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
 ** File: sunos_subagent.h
 **
 **/

#ifndef _sunos_subagent_h_
#define _sunos_subagent_h_

#include <nms_common.h>
#include <nms_agent.h>
#include <nms_threads.h>
#include <fcntl.h>
#include <kstat.h>


//
// File system info types
//

#define DISK_FREE	0
#define DISK_USED	1
#define DISK_TOTAL	2
#define DISK_AVAIL      3
#define DISK_USED_PERC  4
#define DISK_AVAIL_PERC 5
#define DISK_FREE_PERC  6


//
// Request types for H_MemoryInfo
//

enum
{
	MEMINFO_PHYSICAL_FREE,
	MEMINFO_PHYSICAL_FREEPCT,
	MEMINFO_PHYSICAL_TOTAL,
	MEMINFO_PHYSICAL_USED,
	MEMINFO_PHYSICAL_USEDPCT,
	MEMINFO_SWAP_FREE,
	MEMINFO_SWAP_FREEPCT,
	MEMINFO_SWAP_TOTAL,
	MEMINFO_SWAP_USED,
	MEMINFO_SWAP_USEDPCT,
	MEMINFO_VIRTUAL_FREE,
	MEMINFO_VIRTUAL_FREEPCT,
	MEMINFO_VIRTUAL_TOTAL,
	MEMINFO_VIRTUAL_USED,
	MEMINFO_VIRTUAL_USEDPCT
};


//
// Types for Process.XXX() parameters
//

enum
{
	PROCINFO_IO_READ_B,
	PROCINFO_IO_READ_OP,
	PROCINFO_IO_WRITE_B,
	PROCINFO_IO_WRITE_OP,
	PROCINFO_KTIME,
	PROCINFO_PF,
	PROCINFO_UTIME,
	PROCINFO_VMSIZE,
	PROCINFO_WKSET,
	PROCINFO_SYSCALLS,
	PROCINFO_THREADS,
	PROCINFO_CPUTIME
};


//
// I/O stats request types
//

enum
{	
	IOSTAT_NUM_READS,
	IOSTAT_NUM_WRITES,
	IOSTAT_NUM_RBYTES,
	IOSTAT_NUM_WBYTES,
	IOSTAT_IO_TIME,
	IOSTAT_QUEUE
};


//
// Process list entry structure
//

typedef struct t_ProcEnt
{
	unsigned int nPid;
	char szProcName[128];
} PROC_ENT;


//
// Functions
//

int mac_addr_dlpi(char *pszIfName, u_char *pMacAddr);
LONG ReadKStatValue(char *pszModule, LONG nInstance, char *pszName,
		char *pszStat, char *pValue, kstat_named_t *pRawValue);

THREAD_RESULT THREAD_CALL CPUStatCollector(void *arg);
THREAD_RESULT THREAD_CALL IOStatCollector(void *arg);


//
// Global variables
//

extern BOOL g_bShutdown;


#endif
