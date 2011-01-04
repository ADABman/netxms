/* 
** nxsnmpwalk - command line tool used to retrieve parameters from SNMP agent
** Copyright (C) 2004-2009 Victor Kirhenshtein
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
** File: nxsnmpwalk.cpp
**
**/

#include <nms_common.h>
#include <nms_agent.h>
#include <nms_util.h>
#include <nxsnmp.h>


//
// Static data
//

static char m_community[256] = "public";
static char m_user[256] = "";
static char m_authPassword[256] = "";
static char m_encryptionPassword[256] = "";
static int m_authMethod = SNMP_AUTH_NONE;
static int m_encryptionMethod = SNMP_ENCRYPT_NONE;
static WORD m_port = 161;
static DWORD m_snmpVersion = SNMP_VERSION_2C;
static DWORD m_timeout = 3000;


//
// Get data
//

int GetData(char *pszHost, char *pszRootOid)
{
   SNMP_UDPTransport *pTransport;
   SNMP_PDU *pRqPDU, *pRespPDU;
   DWORD dwResult, dwRootLen, dwNameLen;
   DWORD pdwRootName[MAX_OID_LEN], pdwName[MAX_OID_LEN];
   TCHAR szBuffer[1024], typeName[256];
   int i, iExit = 0;
   BOOL bRunning = TRUE;

   // Initialize WinSock
#ifdef _WIN32
   WSADATA wsaData;
   WSAStartup(2, &wsaData);
#endif

   // Create SNMP transport
   pTransport = new SNMP_UDPTransport;
   dwResult = pTransport->createUDPTransport(pszHost, 0, m_port);
   if (dwResult != SNMP_ERR_SUCCESS)
   {
      printf("Unable to create UDP transport: %s\n", SNMPGetErrorText(dwResult));
      iExit = 2;
   }
   else
   {
		if (m_snmpVersion == SNMP_VERSION_3)
			pTransport->setSecurityContext(new SNMP_SecurityContext(m_user, m_authPassword, m_encryptionPassword, m_authMethod, m_encryptionMethod));
		else
			pTransport->setSecurityContext(new SNMP_SecurityContext(m_community));

      // Get root
      dwRootLen = SNMPParseOID(pszRootOid, pdwRootName, MAX_OID_LEN);
      if (dwRootLen == 0)
      {
         dwResult = SNMP_ERR_BAD_OID;
      }
      else
      {
         memcpy(pdwName, pdwRootName, dwRootLen * sizeof(DWORD));
         dwNameLen = dwRootLen;

         // Walk the MIB
         while(bRunning)
         {
				pRqPDU = new SNMP_PDU(SNMP_GET_NEXT_REQUEST, getpid(), m_snmpVersion);
            pRqPDU->bindVariable(new SNMP_Variable(pdwName, dwNameLen));
            dwResult = pTransport->doRequest(pRqPDU, &pRespPDU, m_timeout, 3);

            // Analyze response
            if (dwResult == SNMP_ERR_SUCCESS)
            {
               if ((pRespPDU->getNumVariables() > 0) &&
                   (pRespPDU->getErrorCode() == 0))
               {
                  SNMP_Variable *pVar = pRespPDU->getVariable(0);

                  if ((pVar->GetType() != ASN_NO_SUCH_OBJECT) &&
                      (pVar->GetType() != ASN_NO_SUCH_INSTANCE))
                  {
                     // Should we stop walking?
                     if ((pVar->GetName()->Length() < dwRootLen) ||
                         (memcmp(pdwRootName, pVar->GetName()->GetValue(), dwRootLen * sizeof(DWORD))) ||
                         ((pVar->GetName()->Length() == dwNameLen) &&
                          (!memcmp(pVar->GetName()->GetValue(), pdwName, pVar->GetName()->Length() * sizeof(DWORD)))))
                     {
                        bRunning = FALSE;
                        delete pRespPDU;
                        delete pRqPDU;
                        break;
                     }
                     memcpy(pdwName, pVar->GetName()->GetValue(), 
                            pVar->GetName()->Length() * sizeof(DWORD));
                     dwNameLen = pVar->GetName()->Length();

                     // Print OID and value
                     pVar->GetValueAsString(szBuffer, 1024);
                     for(i = 0; szBuffer[i] != 0; i++)
                        if (szBuffer[i] < ' ')
                           szBuffer[i] = '.';

							bool convert = true;
							pVar->getValueAsPrintableString(szBuffer, 1024, &convert);
							_tprintf(_T("%s [%s]: %s\n"), pVar->GetName()->GetValueAsText(),
										convert ? _T("Hex-STRING") : SNMPDataTypeName(pVar->GetType(), typeName, 256),
										szBuffer);
                  }
                  else
                  {
                     dwResult = SNMP_ERR_NO_OBJECT;
                     bRunning = FALSE;
                  }
               }
               else
               {
                  dwResult = SNMP_ERR_AGENT;
                  bRunning = FALSE;
               }
               delete pRespPDU;
            }
            else
            {
               bRunning = FALSE;
            }
            delete pRqPDU;
         }
      }

      if (dwResult != SNMP_ERR_SUCCESS)
      {
         printf("SNMP Error: %s\n", SNMPGetErrorText(dwResult));
         iExit = 3;
      }
   }

   delete pTransport;
   return iExit;
}


//
// Startup
//

int main(int argc, char *argv[])
{
   int ch, iExit = 1;
   DWORD dwValue;
   char *eptr;
   BOOL bStart = TRUE;

   // Parse command line
   opterr = 1;
	while((ch = getopt(argc, argv, "a:A:c:e:E:hp:u:v:w:")) != -1)
   {
      switch(ch)
      {
         case 'h':   // Display help and exit
            printf("Usage: nxsnmpwalk [<options>] <host> <start_oid>\n"
                   "Valid options are:\n"
						 "   -a <method>  : Authentication method for SNMP v3 USM. Valid methods are MD5 and SHA1\n"
                   "   -A <passwd>  : User's authentication password for SNMP v3 USM\n"
                   "   -c <string>  : Community string. Default is \"public\"\n"
						 "   -e <method>  : Encryption method for SNMP v3 USM. Valid methods are DES and AES\n"
                   "   -E <passwd>  : User's encryption password for SNMP v3 USM\n"
                   "   -h           : Display help and exit\n"
                   "   -p <port>    : Agent's port number. Default is 161\n"
                   "   -u <user>    : User name for SNMP v3 USM\n"
                   "   -v <version> : SNMP version to use (valid values is 1, 2c, and 3)\n"
                   "   -w <seconds> : Request timeout (default is 3 seconds)\n"
                   "\n");
            iExit = 0;
            bStart = FALSE;
            break;
         case 'c':   // Community
            nx_strncpy(m_community, optarg, 256);
            break;
         case 'u':   // User
            nx_strncpy(m_user, optarg, 256);
            break;
			case 'a':   // authentication method
				if (!stricmp(optarg, "md5"))
				{
					m_authMethod = SNMP_AUTH_MD5;
				}
				else if (!stricmp(optarg, "sha1"))
				{
					m_authMethod = SNMP_AUTH_SHA1;
				}
				else if (!stricmp(optarg, "none"))
				{
					m_authMethod = SNMP_AUTH_NONE;
				}
				else
				{
               printf("Invalid authentication method %s\n", optarg);
					bStart = FALSE;
				}
				break;
         case 'A':   // authentication password
            nx_strncpy(m_authPassword, optarg, 256);
				if (_tcslen(m_authPassword) < 8)
				{
               printf("Authentication password should be at least 8 characters long\n");
					bStart = FALSE;
				}
            break;
			case 'e':   // encryption method
				if (!stricmp(optarg, "des"))
				{
					m_encryptionMethod = SNMP_ENCRYPT_DES;
				}
				else if (!stricmp(optarg, "aes"))
				{
					m_encryptionMethod = SNMP_ENCRYPT_AES;
				}
				else if (!stricmp(optarg, "none"))
				{
					m_encryptionMethod = SNMP_ENCRYPT_NONE;
				}
				else
				{
               printf("Invalid encryption method %s\n", optarg);
					bStart = FALSE;
				}
				break;
         case 'E':   // encription password
            nx_strncpy(m_encryptionPassword, optarg, 256);
				if (_tcslen(m_encryptionPassword) < 8)
				{
               printf("Encryption password should be at least 8 characters long\n");
					bStart = FALSE;
				}
            break;
         case 'p':   // Port number
            dwValue = strtoul(optarg, &eptr, 0);
            if ((*eptr != 0) || (dwValue > 65535) || (dwValue == 0))
            {
               printf("Invalid port number %s\n", optarg);
               bStart = FALSE;
            }
            else
            {
               m_port = (WORD)dwValue;
            }
            break;
         case 'v':   // Version
            if (!strcmp(optarg, "1"))
            {
               m_snmpVersion = SNMP_VERSION_1;
            }
            else if (!stricmp(optarg, "2c"))
            {
               m_snmpVersion = SNMP_VERSION_2C;
            }
            else if (!stricmp(optarg, "3"))
            {
               m_snmpVersion = SNMP_VERSION_3;
            }
            else
            {
               printf("Invalid SNMP version %s\n", optarg);
               bStart = FALSE;
            }
            break;
         case 'w':   // Timeout
            dwValue = strtoul(optarg, &eptr, 0);
            if ((*eptr != 0) || (dwValue > 60) || (dwValue == 0))
            {
               printf("Invalid timeout value %s\n", optarg);
               bStart = FALSE;
            }
            else
            {
               m_timeout = dwValue;
            }
            break;
         case '?':
            bStart = FALSE;
            break;
         default:
            break;
      }
   }

   if (bStart)
   {
      if (argc - optind < 2)
      {
         printf("Required argument(s) missing.\nUse nxsnmpwalk -h to get complete command line syntax.\n");
      }
      else
      {
         iExit = GetData(argv[optind], argv[optind + 1]);
      }
   }

   return iExit;
}
