/* $Id$ */

#include <nms_common.h>
#include <nms_agent.h>

#include "main.h"
#include "net.h"

LONG H_CheckSSH(const char *pszParam, const char *pArg, char *pValue)
{
	LONG nRet = SYSINFO_RC_SUCCESS;

	char szHost[256];
	char szPort[256];
	char szTimeout[64];
	unsigned short nPort;

   AgentGetParameterArg(pszParam, 1, szHost, sizeof(szHost));
   AgentGetParameterArg(pszParam, 2, szPort, sizeof(szPort));
   AgentGetParameterArg(pszParam, 3, szTimeout, sizeof(szTimeout));

	if (szHost[0] == 0)
	{
		return SYSINFO_RC_ERROR;
	}

	nPort = (unsigned short)atoi(szPort);
	if (nPort == 0)
	{
		nPort = 22;
	}

	DWORD dwTimeout = strtoul(szTimeout, NULL, 0);
	ret_int(pValue, CheckSSH(szHost, 0, nPort, NULL, NULL, dwTimeout));
	return nRet;
}

int CheckSSH(char *szAddr, DWORD dwAddr, short nPort, char *szUser, char *szPass, DWORD dwTimeout)
{
	int nRet = 0;
	SOCKET nSd;

	nSd = NetConnectTCP(szAddr, dwAddr, nPort, dwTimeout);
	if (nSd != INVALID_SOCKET)
	{
		char szBuff[512];
		char szTmp[128];

		nRet = PC_ERR_HANDSHAKE;

		if (NetRead(nSd, szBuff, sizeof(szBuff)) >= 8)
		{
			int nMajor, nMinor;

			if (sscanf(szBuff, "SSH-%d.%d-", &nMajor, &nMinor) == 2)
			{
				snprintf(szTmp, sizeof(szTmp), "SSH-%d.%d-NetXMS\n",
						nMajor, nMinor);
				if (NetWrite(nSd, szTmp, (int)strlen(szTmp)) > 0)
				{
					nRet = PC_ERR_NONE;
				}
			}
		}

		NetClose(nSd);
	}
	else
	{
		nRet = PC_ERR_CONNECT;
	}

	return nRet;
}
