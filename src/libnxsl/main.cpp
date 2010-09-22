/* 
** NetXMS - Network Management System
** NetXMS Scripting Language Interpreter
** Copyright (C) 2003-2010 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** File: main.cpp
**
**/

#include "libnxsl.h"

#ifdef _WIN32
#define read	_read
#define close	_close
#endif


//
// For unknown reasons, min() becames undefined on Linux, despite the fact
// that it is defined in nms_common.h
//

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif


//
// Interface to compiler
//

NXSL_Program LIBNXSL_EXPORTABLE *NXSLCompile(const TCHAR *pszSource,
                                             TCHAR *pszError, int nBufSize)
{
   NXSL_Compiler compiler;
   NXSL_Program *pResult;

   pResult = compiler.compile(pszSource);
   if (pResult == NULL)
   {
      if (pszError != NULL)
         nx_strncpy(pszError, compiler.getErrorText(), nBufSize);
   }
   return pResult;
}


//
// Load file into memory
//

TCHAR LIBNXSL_EXPORTABLE *NXSLLoadFile(const TCHAR *pszFileName, DWORD *pdwFileSize)
{
   int fd, iBufPos, iNumBytes, iBytesRead;
   TCHAR *pBuffer = NULL;
   struct stat fs;

   fd = _topen(pszFileName, O_RDONLY | O_BINARY);
   if (fd != -1)
   {
      if (fstat(fd, &fs) != -1)
      {
         pBuffer = (TCHAR *)malloc(fs.st_size + 1);
         if (pBuffer != NULL)
         {
            *pdwFileSize = fs.st_size;
            for(iBufPos = 0; iBufPos < fs.st_size; iBufPos += iBytesRead)
            {
               iNumBytes = min(16384, fs.st_size - iBufPos);
               if ((iBytesRead = read(fd, &pBuffer[iBufPos], iNumBytes)) < 0)
               {
                  free(pBuffer);
                  pBuffer = NULL;
                  break;
               }
            }
				if (pBuffer != NULL)
				{
					for(iBufPos = 0; iBufPos < fs.st_size; iBufPos++)
						if (pBuffer[iBufPos] == 0)
							pBuffer[iBufPos] = ' ';
					pBuffer[fs.st_size] = 0;
				}
         }
      }
      close(fd);
   }
   return pBuffer;
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

#endif   /* _WIN32 */
