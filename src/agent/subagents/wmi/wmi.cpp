/*
** WMI NetXMS subagent
** Copyright (C) 2008 Victor Kirhenshtein
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
** File: wmi.cpp
**
**/

#include "wmi.h"


//
// Externals
//

LONG H_ACPIThermalZones(TCHAR *pszParam, TCHAR *pArg, NETXMS_VALUES_LIST *pValue);
LONG H_ACPITZCurrTemp(char *cmd, char *arg, char *value);


//
// Convert variant to string value
//

char *VariantToString(VARIANT *pValue)
{
   char *buf = NULL;

   switch (pValue->vt) 
   {
		case VT_NULL: 
         buf = strdup("<null>");
         break;
	   case VT_BOOL: 
			buf = strdup(pValue->boolVal ? "TRUE" : "FALSE");
         break;
	   case VT_UI1:
			buf = (char *)malloc(32);
			sprintf(buf, "%d", pValue->bVal);
			break;
		case VT_I2:
			buf = (char *)malloc(32);
			sprintf(buf, "%d", pValue->uiVal);
			break;
		case VT_I4:
			buf = (char *)malloc(32);
			sprintf(buf, "%d", pValue->lVal);
			break;
		case VT_R4:
			buf = (char *)malloc(32);
			sprintf(buf, "%f", pValue->fltVal);
			break;
		case VT_R8:
			buf = (char *)malloc(32);
			sprintf(buf, "%f", pValue->dblVal);
			break;
		case VT_BSTR:
			buf = MBStringFromWideString(pValue->bstrVal);
			break;
	   default:
			break;
	}

   return buf;
}


// Convert variant to integer value
//

LONG VariantToInt(VARIANT *pValue)
{
   LONG val;

   switch (pValue->vt) 
   {
	   case VT_BOOL: 
			val = pValue->boolVal ? 1 : 0;
         break;
	   case VT_UI1:
			val = pValue->bVal;
			break;
		case VT_I2:
			val = pValue->uiVal;
			break;
		case VT_I4:
			val = pValue->lVal;
			break;
		case VT_R4:
			val = (LONG)pValue->fltVal;
			break;
		case VT_R8:
			val = (LONG)pValue->dblVal;
			break;
		case VT_BSTR:
			val = wcstol(pValue->bstrVal, NULL, 0);
			break;
	   default:
			val = 0;
			break;
	}

   return val;
}


//
// Perform generic WMI query
// Returns pointer to IEnumWbemClassObject ready for enumeration or NULL
//

IEnumWbemClassObject *DoWMIQuery(WCHAR *ns, WCHAR *query, WMI_QUERY_CONTEXT *ctx)
{
	IWbemLocator *pWbemLocator = NULL;
	IWbemServices *pWbemServices = NULL;
	IEnumWbemClassObject *pEnumObject = NULL;

	memset(ctx, 0, sizeof(WMI_QUERY_CONTEXT));

	if (CoInitializeEx(0, COINIT_MULTITHREADED) != S_OK)
		return NULL;

	if (CoInitializeSecurity(NULL, -1, NULL, NULL,
	                         RPC_C_AUTHN_LEVEL_PKT,
	                         RPC_C_IMP_LEVEL_IMPERSONATE,
	                         NULL, EOAC_NONE,	0) != S_OK)
	{
		CoUninitialize();
		return NULL;
	}

	if (CoCreateInstance(CLSID_WbemAdministrativeLocator,
	                     NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
	                     IID_IUnknown, (void **)&pWbemLocator) == S_OK)
	{
		ctx->m_locator = pWbemLocator;
		if (pWbemLocator->ConnectServer(ns, NULL, NULL,
			                             NULL, 0, NULL, NULL, &pWbemServices) == S_OK)
		{
			ctx->m_services = pWbemServices;
			if (pWbemServices->ExecQuery(L"WQL", query, WBEM_FLAG_RETURN_IMMEDIATELY,
					                       NULL, &pEnumObject) == S_OK)
			{
				pEnumObject->Reset();
			}
		}
	}
	if (pEnumObject == NULL)
		CloseWMIQuery(ctx);
	return pEnumObject;
}


//
// Cleanup after WMI query
//

void CloseWMIQuery(WMI_QUERY_CONTEXT *ctx)
{
	if (ctx->m_services != NULL)
		ctx->m_services->Release();
	if (ctx->m_locator != NULL)
		ctx->m_locator->Release();
	memset(ctx, 0, sizeof(WMI_QUERY_CONTEXT));
}


//
// Handler for generic WMI query
//

static LONG H_WMIQuery(char *cmd, char *arg, char *value)
{
	WMI_QUERY_CONTEXT ctx;
	IEnumWbemClassObject *pEnumObject = NULL;
	IWbemClassObject *pClassObject = NULL;
	char ns[256], query[256], prop[256];
	WCHAR *pwszNamespace, *pwszQuery;
	ULONG uRet;
	LONG rc = SYSINFO_RC_ERROR;

	if (!NxGetParameterArg(cmd, 1, ns, 256) ||
	    !NxGetParameterArg(cmd, 2, query, 256))
		return SYSINFO_RC_UNSUPPORTED;

	pwszNamespace = WideStringFromMBString(ns);
	pwszQuery = WideStringFromMBString(query);
	pEnumObject = DoWMIQuery(pwszNamespace, pwszQuery, &ctx);
	if (pEnumObject != NULL)
	{
		if (pEnumObject->Next(WBEM_INFINITE, 1, &pClassObject, &uRet) == S_OK)
		{
			if (NxGetParameterArg(cmd, 3, prop, 256))
			{
				VARIANT v;
				WCHAR *pwstrProperty;

				pwstrProperty = WideStringFromMBString(prop);
				if (pClassObject->Get(pwstrProperty, 0, &v, 0, 0) == S_OK)
				{
					char *str;

					str = VariantToString(&v);
					if (str != NULL)
					{
						ret_string(value, str);
						free(str);
					}
					else
					{
						ret_string(value, "<WMI result conversion error>");
					}
					VariantClear(&v);

					rc = SYSINFO_RC_SUCCESS;
				}
				free(pwstrProperty);
			}
			else
			{
				rc = SYSINFO_RC_UNSUPPORTED;
			}
			pClassObject->Release();
		}
		pEnumObject->Release();
		CloseWMIQuery(&ctx);
	}
	free(pwszNamespace);
	free(pwszQuery);
   return rc;
}


//
// Handler for WMI.NameSpaces enum
//

static LONG H_WMINameSpaces(TCHAR *pszParam, TCHAR *pArg, NETXMS_VALUES_LIST *pValue)
{
	WMI_QUERY_CONTEXT ctx;
	IEnumWbemClassObject *pEnumObject = NULL;
	IWbemClassObject *pClassObject = NULL;
	ULONG uRet;
	LONG rc = SYSINFO_RC_ERROR;

	pEnumObject = DoWMIQuery(L"root", L"SELECT Name FROM __NAMESPACE", &ctx);
	if (pEnumObject != NULL)
	{
		while(pEnumObject->Next(WBEM_INFINITE, 1, &pClassObject, &uRet) == S_OK)
		{
			VARIANT v;

			if (pClassObject->Get(L"Name", 0, &v, 0, 0) == S_OK)
			{
				char *str;

				str = VariantToString(&v);
				VariantClear(&v);
				NxAddResultString(pValue, str);
				free(str);
			}
			pClassObject->Release();
		}
		pEnumObject->Release();
		CloseWMIQuery(&ctx);
		rc = SYSINFO_RC_SUCCESS;
	}
   return rc;
}


//
// Handler for WMI.Classes enum
//

static LONG H_WMIClasses(TCHAR *pszParam, TCHAR *pArg, NETXMS_VALUES_LIST *pValue)
{
	WMI_QUERY_CONTEXT ctx;
	IEnumWbemClassObject *pEnumObject = NULL;
	IWbemClassObject *pClassObject = NULL;
	TCHAR ns[256];
	WCHAR *pwszNamespace;
	ULONG uRet;
	LONG rc = SYSINFO_RC_ERROR;

	if (!NxGetParameterArg(pszParam, 1, ns, 256))
		return SYSINFO_RC_UNSUPPORTED;
	pwszNamespace = WideStringFromMBString(ns);

	pEnumObject = DoWMIQuery(pwszNamespace, L"SELECT * FROM __CLASSES", &ctx);
	if (pEnumObject != NULL)
	{
		while(pEnumObject->Next(WBEM_INFINITE, 1, &pClassObject, &uRet) == S_OK)
		{
			VARIANT v;

printf("class found !!!\n");
			if (pClassObject->Get(L"NAME", 0, &v, 0, 0) == S_OK)
			{
				char *str;

				str = VariantToString(&v);
				VariantClear(&v);
				NxAddResultString(pValue, str);
				free(str);
			}
			pClassObject->Release();
		}
		pEnumObject->Release();
		CloseWMIQuery(&ctx);
		rc = SYSINFO_RC_SUCCESS;
	}

	free(pwszNamespace);
   return rc;
}


//
// Initialize subagent
//

static BOOL SubAgentInit(TCHAR *pszConfigFile)
{
	return TRUE;
}


//
// Handler for subagent unload
//

static void SubAgentShutdown(void)
{
}


//
// Subagent information
//

static NETXMS_SUBAGENT_PARAM m_parameters[] =
{
   { _T("ACPI.ThermalZone.CurrentTemp(*)"), H_ACPITZCurrTemp, NULL, DCI_DT_INT, _T("Current temperature in ACPI thermal zone {instance}") },
   { _T("WMI.Query(*)"), H_WMIQuery, NULL, DCI_DT_STRING, _T("Generic WMI query") }
};
static NETXMS_SUBAGENT_ENUM m_enums[] =
{
   { _T("ACPI.ThermalZones"), H_ACPIThermalZones, NULL },
   { _T("WMI.Classes(*)"), H_WMIClasses, NULL },
   { _T("WMI.NameSpaces"), H_WMINameSpaces, NULL }
};

static NETXMS_SUBAGENT_INFO m_info =
{
   NETXMS_SUBAGENT_INFO_MAGIC,
	_T("WMI"), _T(NETXMS_VERSION_STRING) _T(DEBUG_SUFFIX),
   SubAgentInit, SubAgentShutdown, NULL,      // handlers
   sizeof(m_parameters) / sizeof(NETXMS_SUBAGENT_PARAM),
	m_parameters,
	sizeof(m_enums) / sizeof(NETXMS_SUBAGENT_ENUM),
	m_enums,
   0, NULL              // actions
};


//
// Entry point for NetXMS agent
//

extern "C" BOOL __declspec(dllexport) __cdecl 
   NxSubAgentRegister(NETXMS_SUBAGENT_INFO **ppInfo, TCHAR *pszConfigFile)
{
   *ppInfo = &m_info;
   return TRUE;
}


//
// DLL entry point
//

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
   if (dwReason == DLL_PROCESS_ATTACH)
      DisableThreadLibraryCalls(hInstance);
   return TRUE;
}
