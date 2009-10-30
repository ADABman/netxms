/* $Id$ */
/* 
** NetXMS - Network Management System
** Copyright (C) 2003, 2004, 2005, 2006 Victor Kirhenshtein
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
** File: net.cpp
**
**/

#include <nms_common.h>
#include <nms_agent.h>

#include <sys/ioctl.h>
#include "net.h"

SOCKET NetConnectTCP(const char *szHost, DWORD dwAddr, unsigned short nPort)
{
	SOCKET nSocket;

	nSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (nSocket != INVALID_SOCKET)
	{
		int flags = fcntl(nSocket, F_GETFL, NULL);
		fcntl(nSocket, F_SETFL, flags | O_NONBLOCK);

		struct sockaddr_in sa;
		sa.sin_family = AF_INET;
		sa.sin_port = htons(nPort);
		if (szHost != NULL)
		{
			sa.sin_addr.s_addr = inet_addr(szHost);
		}
		else
		{
			sa.sin_addr.s_addr = htonl(dwAddr);
		}

		if (connect(nSocket, (struct sockaddr*)&sa, sizeof(sa)) < 0)
		{
			if (errno == EINPROGRESS)
			{
				if (!NetCanWrite(nSocket, m_dwTimeout))
				{
					closesocket(nSocket);
					nSocket = INVALID_SOCKET;
				}
			}
			else
			{
				closesocket(nSocket);
				nSocket = INVALID_SOCKET;
			}
		}
	}

	return nSocket;
}

bool NetCanRead(SOCKET nSocket, int nTimeout /* ms */)
{
	bool ret = false;
	struct timeval timeout;
	fd_set readFdSet;

	FD_ZERO(&readFdSet);
	FD_SET(nSocket, &readFdSet);
	timeout.tv_sec = nTimeout / 1000;
	timeout.tv_usec = (nTimeout % 1000) * 1000;

	if (select(SELECT_NFDS(nSocket + 1), &readFdSet, NULL, NULL, &timeout) > 0)
	{
		ret = true;
	}

	return ret;
}

bool NetCanWrite(SOCKET nSocket, int nTimeout /* ms */)
{
	bool ret = false;
	struct timeval timeout;
	fd_set writeFdSet;

	FD_ZERO(&writeFdSet);
	FD_SET(nSocket, &writeFdSet);
	timeout.tv_sec = nTimeout / 1000;
	timeout.tv_usec = (nTimeout % 1000) * 1000;

	if (select(SELECT_NFDS(nSocket + 1), NULL, &writeFdSet, NULL, &timeout) > 0)
	{
		ret = true;
	}

	return ret;
}

int NetRead(SOCKET nSocket, char *pBuff, int nSize)
{
	return recv(nSocket, pBuff, nSize, 0);
}

int NetWrite(SOCKET nSocket, const char *pBuff, int nSize)
{
	return send(nSocket, pBuff, nSize, 0);
}

void NetClose(SOCKET nSocket)
{
	shutdown(nSocket, SHUT_RDWR);
	{
		closesocket(nSocket);
	}
}
