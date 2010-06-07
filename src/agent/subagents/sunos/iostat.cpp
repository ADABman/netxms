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
 ** File: io.cpp
 **
 **/

#include "sunos_subagent.h"


//
// Constants
//

#define MAX_DEVICES		255
#define HISTORY_SIZE		60


//
// Device I/O stats structure
//

struct IO_STATS
{
	// device name
	char dev[KSTAT_STRLEN];

	// current values
	QWORD currBytesRead;
	QWORD currBytesWritten;
	DWORD currReadOps;
	DWORD currWriteOps;
	DWORD currQueue;
	
	// history - totals for queue, deltas for others
	QWORD histBytesRead[HISTORY_SIZE];
	QWORD histBytesWritten[HISTORY_SIZE];
	DWORD histReadOps[HISTORY_SIZE];
	DWORD histWriteOps[HISTORY_SIZE];
	DWORD histQueue[HISTORY_SIZE];
};


//
// Static data
//

static IO_STATS s_data[MAX_DEVICES + 1];
static int s_currSlot = 0;	// current history slot


//
// Process stats for single device
//

static void ProcessDeviceStats(const char *dev, kstat_io_t *kio)
{
	int i;

	// find device
	for(i = 1; i <= MAX_DEVICES; i++)
		if (!strcmp(dev, s_data[i].dev) || (s_data[i].dev[0] == 0))
			break;
	if (i > MAX_DEVICES)
		return;		// No more free slots

	if (s_data[i].dev[0] == 0)
	{
		// new device
		strcpy(s_data[i].dev, dev);
		AgentWriteDebugLog(5, "SunOS: device %s added to I/O stat collection", dev);
	}
	else
	{
		// existing device - update history
		s_data[i].histBytesRead[s_currSlot] = kio->nread - s_data[i].currBytesRead;
		s_data[i].histBytesWritten[s_currSlot] = kio->nwritten - s_data[i].currBytesWritten;
		s_data[i].histReadOps[s_currSlot] = kio->reads - s_data[i].currReadOps;
		s_data[i].histWriteOps[s_currSlot] = kio->writes - s_data[i].currWriteOps;
		s_data[i].histQueue[s_currSlot] = kio->wcnt + kio->rcnt;
	}

	// update current values
	s_data[i].currBytesRead = kio->nread;
	s_data[i].currBytesWritten = kio->nwritten;
	s_data[i].currReadOps = kio->reads;
	s_data[i].currWriteOps = kio->writes;
	s_data[i].currQueue = kio->wcnt + kio->rcnt;
}


//
// Calculate total values for all devices
//

static void CalculateTotals()
{
	QWORD br = 0, bw = 0;
	DWORD r = 0, w = 0, q = 0;

	for(int i = 1; (i <= MAX_DEVICES) && (s_data[i].dev[0] != 0); i++)
	{
		br += s_data[i].histBytesRead[s_currSlot];
		bw += s_data[i].histBytesWritten[s_currSlot];
		r += s_data[i].histReadOps[s_currSlot];
		w += s_data[i].histWriteOps[s_currSlot];
		q += s_data[i].histQueue[s_currSlot];
	}

	s_data[0].histBytesRead[s_currSlot] = br;
	s_data[0].histBytesWritten[s_currSlot] = bw;
	s_data[0].histReadOps[s_currSlot] = r;
	s_data[0].histWriteOps[s_currSlot] = w;
	s_data[0].histQueue[s_currSlot] = q;
}


//
// I/O stat collector
//

THREAD_RESULT THREAD_CALL IOStatCollector(void *arg)
{
	kstat_ctl_t *kc;
	kstat_t *kp;
	kstat_io_t kio;

	kc = kstat_open();
	if (kc == NULL)
	{
		AgentWriteLog(EVENTLOG_ERROR_TYPE, "SunOS::IOStatCollector: call to kstat_open failed (%s), I/O statistic will not be collected", strerror(errno));
		return THREAD_OK;
	}

	memset(s_data, 0, sizeof(IO_STATS) * (MAX_DEVICES + 1));
	AgentWriteDebugLog(1, "SunOS: I/O stat collector thread started");

	while(!g_bShutdown)
	{
		kstat_chain_update(kc);
		for(kp = kc->kc_chain; kp != NULL; kp = kp->ks_next)
		{
			if (kp->ks_type == KSTAT_TYPE_IO)
			{
				kstat_read(kc, kp, &kio);
				ProcessDeviceStats(kp->ks_name, &kio);
			}
		}
		CalculateTotals();
		s_currSlot++;
		if (s_currSlot == HISTORY_SIZE)
			s_currSlot = 0;
		ThreadSleepMs(1000);
	}

	AgentWriteDebugLog(1, "SunOS: I/O stat collector thread stopped");
	kstat_close(kc);
	return THREAD_OK;
}


//
// Calculate average value for 32bit series
//

static double CalculateAverage32(DWORD *series)
{
	double sum = 0;
	for(int i = 0; i < HISTORY_SIZE; i++)
		sum += series[i];
	return sum / (double)HISTORY_SIZE;
}


//
// Calculate average value for 64bit series
//

static QWORD CalculateAverage64(QWORD *series)
{
	QWORD sum = 0;
	for(int i = 0; i < HISTORY_SIZE; i++)
		sum += series[i];
	return sum / HISTORY_SIZE;
}


//
// Get total I/O stat value
//

LONG H_IOStatsTotal(const char *cmd, const char *arg, char *value)
{
	LONG rc = SYSINFO_RC_SUCCESS;

	switch(CAST_FROM_POINTER(arg, int))
	{
		case IOSTAT_NUM_READS:
			ret_double(value, CalculateAverage32(s_data[0].histReadOps));
			break;
		case IOSTAT_NUM_WRITES:
			ret_double(value, CalculateAverage32(s_data[0].histWriteOps));
			break;
		case IOSTAT_QUEUE:
			ret_double(value, CalculateAverage32(s_data[0].histQueue));
			break;
		case IOSTAT_NUM_RBYTES:
			ret_uint64(value, CalculateAverage64(s_data[0].histBytesRead));
			break;
		case IOSTAT_NUM_WBYTES:
			ret_uint64(value, CalculateAverage64(s_data[0].histBytesWritten));
			break;
		default:
			rc = SYSINFO_RC_UNSUPPORTED;
			break;
	}
	return rc;
}


//
// Get I/O stat for specific device
//

LONG H_IOStats(const char *cmd, const char *arg, char *value)
{
	LONG rc = SYSINFO_RC_SUCCESS;

	return rc;
}

