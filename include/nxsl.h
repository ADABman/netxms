/* $Id$ */
/* 
** NetXMS - Network Management System
** Copyright (C) 2005, 2006 Victor Kirhenshtein
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
** File: nxsl.h
**
**/

#ifndef _nxsl_h_
#define _nxsl_h_

#ifdef _WIN32
#ifdef LIBNXSL_EXPORTS
#define LIBNXSL_EXPORTABLE __declspec(dllexport)
#else
#define LIBNXSL_EXPORTABLE __declspec(dllimport)
#endif
#else    /* _WIN32 */
#define LIBNXSL_EXPORTABLE
#endif


//
// Various defines
//

#define MAX_FUNCTION_NAME  64


//
// Script execution errors
//

#define NXSL_ERR_DATA_STACK_UNDERFLOW     1
#define NXSL_ERR_CONTROL_STACK_UNDERFLOW  2
#define NXSL_ERR_BAD_CONDITION            3
#define NXSL_ERR_NOT_NUMBER               4
#define NXSL_ERR_NULL_VALUE               5
#define NXSL_ERR_INTERNAL                 6
#define NXSL_ERR_NO_MAIN                  7
#define NXSL_ERR_CONTROL_STACK_OVERFLOW   8
#define NXSL_ERR_DIVIDE_BY_ZERO           9
#define NXSL_ERR_REAL_VALUE               10
#define NXSL_ERR_NO_FUNCTION              11
#define NXSL_ERR_INVALID_ARGUMENT_COUNT   12
#define NXSL_ERR_TYPE_CAST                13
#define NXSL_ERR_NOT_OBJECT               14
#define NXSL_ERR_NO_SUCH_ATTRIBUTE        15
#define NXSL_ERR_MODULE_NOT_FOUND         16
#define NXSL_ERR_NOT_STRING               17
#define NXSL_ERR_REGEXP_ERROR             18
#define NXSL_ERR_NOT_INTEGER					19
#define NXSL_ERR_INVALID_OBJECT_OPERATION 20
#define NXSL_ERR_BAD_CLASS                21
#define NXSL_ERR_VARIABLE_ALREADY_EXIST   22
#define NXSL_ERR_INDEX_NOT_INTEGER        23
#define NXSL_ERR_NOT_ARRAY                24


//
// Special return codes for external functions
//

#define NXSL_STOP_SCRIPT_EXECUTION        -1


//
// Exportable classes
//

#ifdef __cplusplus
#include <nxsl_classes.h>
#else
struct NXSL_Program;
#endif


//
// Functions
//

#ifdef __cplusplus
extern "C" {
#endif

NXSL_Program LIBNXSL_EXPORTABLE *NXSLCompile(const TCHAR *pszSource,
                                             TCHAR *pszError, int nBufSize);
TCHAR LIBNXSL_EXPORTABLE *NXSLLoadFile(const TCHAR *pszFileName, DWORD *pdwFileSize);

#ifdef __cplusplus
}
#endif

#endif
