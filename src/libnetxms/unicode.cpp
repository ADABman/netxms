/*
** NetXMS - Network Management System
** Copyright (C) 2003-2010 Victor Kirhenshtein
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published
** by the Free Software Foundation; either version 3 of the License, or
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
** File: unicode.cpp
**
**/

#include "libnetxms.h"


//
// Static data
//

static char m_cpDefault[MAX_CODEPAGE_LEN] = ICONV_DEFAULT_CODEPAGE_A;


#ifndef _WIN32

#if HAVE_ICONV_H
#include <iconv.h>
#endif


//
// UNICODE character set
//

#ifndef __DISABLE_ICONV

// configure first test for libiconv, then for iconv
// if libiconv was found, HAVE_ICONV will not be set correctly
#if HAVE_LIBICONV
#undef HAVE_ICONV
#define HAVE_ICONV 1
#endif

#if HAVE_ICONV_UCS_2_INTERNAL
#define UCS2_CODEPAGE_NAME	"UCS-2-INTERNAL"
#elif HAVE_ICONV_UCS_2BE && WORDS_BIGENDIAN
#define UCS2_CODEPAGE_NAME	"UCS-2BE"
#elif HAVE_ICONV_UCS_2LE && !WORDS_BIGENDIAN
#define UCS2_CODEPAGE_NAME	"UCS-2LE"
#elif HAVE_ICONV_UCS_2
#define UCS2_CODEPAGE_NAME	"UCS-2"
#elif HAVE_ICONV_UCS2
#define UCS2_CODEPAGE_NAME	"UCS2"
#elif HAVE_ICONV_UTF_16
#define UCS2_CODEPAGE_NAME	"UTF-16"
#else
#ifdef UNICODE
#error Cannot determine valid UCS-2 codepage name
#else
#warning Cannot determine valid UCS-2 codepage name
#undef HAVE_ICONV
#endif
#endif

#if HAVE_ICONV_UCS_4_INTERNAL
#define UCS4_CODEPAGE_NAME	"UCS-4-INTERNAL"
#elif HAVE_ICONV_UCS_4BE && WORDS_BIGENDIAN
#define UCS4_CODEPAGE_NAME	"UCS-4BE"
#elif HAVE_ICONV_UCS_4LE && !WORDS_BIGENDIAN
#define UCS4_CODEPAGE_NAME	"UCS-4LE"
#elif HAVE_ICONV_UCS_4
#define UCS4_CODEPAGE_NAME	"UCS-4"
#elif HAVE_ICONV_UCS4
#define UCS4_CODEPAGE_NAME	"UCS4"
#elif HAVE_ICONV_UTF_32
#define UCS4_CODEPAGE_NAME	"UTF-32"
#else
#if defined(UNICODE) && defined(UNICODE_UCS4)
#error Cannot determine valid UCS-4 codepage name
#else
#warning Cannot determine valid UCS-4 codepage name
#undef HAVE_ICONV
#endif
#endif

#ifdef UNICODE_UCS4
#define UNICODE_CODEPAGE_NAME	UCS4_CODEPAGE_NAME
#else	/* assume UCS-2 */
#define UNICODE_CODEPAGE_NAME	UCS2_CODEPAGE_NAME
#endif

#endif   /* __DISABLE_ICONV */


//
// Set application's default codepage
//

BOOL LIBNETXMS_EXPORTABLE SetDefaultCodepage(const char *cp)
{
	BOOL rc;
#if HAVE_ICONV && !defined(__DISABLE_ICONV)
	iconv_t cd;

	cd = iconv_open(cp, "UTF-8");
	if (cd != (iconv_t)(-1))
	{
		iconv_close(cd);
#endif		
		strncpy(m_cpDefault, cp, MAX_CODEPAGE_LEN);
		m_cpDefault[MAX_CODEPAGE_LEN - 1] = 0;
		rc = TRUE;
#if HAVE_ICONV && !defined(__DISABLE_ICONV)
	}
	else
	{
		rc = FALSE;
	}
#endif
	return rc;
}


//
// Calculate length of wide character string
//

#if !UNICODE_UCS2 || !HAVE_WCSLEN

int LIBNETXMS_EXPORTABLE ucs2_strlen(const UCS2CHAR *pStr)
{
   int iLen = 0;
   const UCS2CHAR *pCurr = pStr;

   while(*pCurr++)
      iLen++;
   return iLen;
}

#endif


//
// Duplicate wide character string
//

#if !UNICODE_UCS2 || !HAVE_WCSDUP

UCS2CHAR LIBNETXMS_EXPORTABLE *ucs2_strdup(const UCS2CHAR *pStr)
{
	return (UCS2CHAR *)nx_memdup(pStr, (ucs2_strlen(pStr) + 1) * sizeof(UCS2CHAR));
}

#endif


//
// Copy wide character string with length limitation
//

#if !UNICODE_UCS2 || !HAVE_WCSNCPY

UCS2CHAR LIBNETXMS_EXPORTABLE *ucs2_strncpy(UCS2CHAR *pDst, const UCS2CHAR *pSrc, int nDstLen)
{
	int nLen;

	nLen = ucs2_strlen(pSrc) + 1;
	if (nLen > nDstLen)
		nLen = nDstLen;
	memcpy(pDst, pSrc, nLen * sizeof(UCS2CHAR));
	return pDst;
}

#endif


//
// Convert UNICODE string to single-byte string
//

int LIBNETXMS_EXPORTABLE WideCharToMultiByte(int iCodePage, DWORD dwFlags,
                                             const WCHAR *pWideCharStr, int cchWideChar,
															char *pByteStr, int cchByteChar, 
                                             char *pDefaultChar, BOOL *pbUsedDefChar)
{
#if HAVE_ICONV && !defined(__DISABLE_ICONV)
	iconv_t cd;
	int nRet;
	const char *inbuf;
	char *outbuf;
	size_t inbytes, outbytes;
	char cp[MAX_CODEPAGE_LEN + 16];

	// Calculate required length. Because iconv cannot calculate
	// resulting multibyte string length, assume the worst case - 3 bytes
	// per character for UTF-8 and 2 bytes per character for other encodings
	if (cchByteChar == 0)
	{
		return wcslen(pWideCharStr) * (iCodePage == CP_UTF8 ? 3 : 2) + 1;
	}

	strcpy(cp, m_cpDefault);
#if HAVE_ICONV_IGNORE
	strcat(cp, "//IGNORE");
#endif
	cd = iconv_open(iCodePage == CP_UTF8 ? "UTF-8" : cp, UNICODE_CODEPAGE_NAME);
	if (cd != (iconv_t)(-1))
	{
		inbuf = (const char *)pWideCharStr;
		inbytes = ((cchWideChar == -1) ? wcslen(pWideCharStr) + 1 : cchWideChar) * sizeof(WCHAR);
		outbuf = pByteStr;
		outbytes = cchByteChar;
		nRet = iconv(cd, (ICONV_CONST char **)&inbuf, &inbytes, &outbuf, &outbytes);
		iconv_close(cd);
		if (nRet == -1)
		{
			if (errno == EILSEQ)
			{
				nRet = cchByteChar - outbytes;
			}
			else
			{
				nRet = 0;
			}
		}
		if ((cchWideChar == -1) && (outbytes > 0))
		{
			*outbuf = 0;
		}
	}
	else
	{
		goto fallback;
	}
	return nRet;

fallback:

#else

   if (cchByteChar == 0)
   {
      return wcslen(pWideCharStr) + 1;
   }

#endif	/* HAVE_ICONV */

   const WCHAR *pSrc;
   char *pDest;
   int iPos, iSize;

   iSize = (cchWideChar == -1) ? wcslen(pWideCharStr) : cchWideChar;
   if (iSize >= cchByteChar)
      iSize = cchByteChar - 1;
   for(pSrc = pWideCharStr, iPos = 0, pDest = pByteStr; iPos < iSize; iPos++, pSrc++, pDest++)
      *pDest = (*pSrc < 256) ? (char)(*pSrc) : '?';
   *pDest = 0;
   return iSize;

}


//
// Convert single-byte to UNICODE string
//

int LIBNETXMS_EXPORTABLE MultiByteToWideChar(int iCodePage, DWORD dwFlags, const char *pByteStr,
                                             int cchByteChar, WCHAR *pWideCharStr, int cchWideChar)
{
#if HAVE_ICONV && !defined(__DISABLE_ICONV)
	iconv_t cd;
	int nRet;
	const char *inbuf;
	char *outbuf;
	size_t inbytes, outbytes;

	if (cchWideChar == 0)
	{
		return strlen(pByteStr) + 1;
	}

	cd = iconv_open(UNICODE_CODEPAGE_NAME, iCodePage == CP_UTF8 ? "UTF-8" : m_cpDefault);
	if (cd != (iconv_t)(-1))
	{
		inbuf = pByteStr;
		inbytes = (cchByteChar == -1) ? strlen(pByteStr) + 1 : cchByteChar;
		outbuf = (char *)pWideCharStr;
		outbytes = cchWideChar * sizeof(WCHAR);
		nRet = iconv(cd, (ICONV_CONST char **)&inbuf, &inbytes, &outbuf, &outbytes);
		iconv_close(cd);
		if (nRet == -1)
		{
			if (errno == EILSEQ)
			{
				nRet = (cchWideChar * sizeof(WCHAR) - outbytes) / sizeof(WCHAR);
			}
			else
			{
				nRet = 0;
			}
		}
		if (((char *)outbuf - (char *)pWideCharStr > sizeof(WCHAR)) && (*pWideCharStr == 0xFEFF))
		{
			// Remove UNICODE byte order indicator if presented
			memmove(pWideCharStr, &pWideCharStr[1], (char *)outbuf - (char *)pWideCharStr - sizeof(WCHAR));
			outbuf -= sizeof(WCHAR);
		}
		if ((cchByteChar == -1) && (outbytes >= sizeof(WCHAR)))
		{
			*((WCHAR *)outbuf) = 0;
		}
	}
	else
	{
		goto fallback;
	}
	return nRet;

fallback:

#else

	if (cchWideChar == 0)
	{
		return strlen(pByteStr) + 1;
	}

#endif

	const char *pSrc;
	WCHAR *pDest;
	int iPos, iSize;

	iSize = (cchByteChar == -1) ? strlen(pByteStr) : cchByteChar;
	if (iSize >= cchWideChar)
		iSize = cchWideChar - 1;
	for(pSrc = pByteStr, iPos = 0, pDest = pWideCharStr; iPos < iSize; iPos++, pSrc++, pDest++)
		*pDest = (WCHAR)(*pSrc);
	*pDest = 0;

	return iSize;
}

#endif   /* not _WIN32 */


//
// UNICODE version of inet_addr()
//

DWORD LIBNETXMS_EXPORTABLE inet_addr_w(const WCHAR *pszAddr)
{
   char szBuffer[256];

   WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR,
                       pszAddr, -1, szBuffer, 256, NULL, NULL);
   return inet_addr(szBuffer);
}


//
// Convert multibyte string to wide string using current codepage and
// allocating wide string dynamically
//

WCHAR LIBNETXMS_EXPORTABLE *WideStringFromMBString(const char *pszString)
{
   WCHAR *pwszOut;
   int nLen;

   nLen = (int)strlen(pszString) + 1;
   pwszOut = (WCHAR *)malloc(nLen * sizeof(WCHAR));
   MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, pszString, -1, pwszOut, nLen);
   return pwszOut;
}


//
// Convert UTF8 string to wide string allocating wide string dynamically
//

WCHAR LIBNETXMS_EXPORTABLE *WideStringFromUTF8String(const char *pszString)
{
   WCHAR *pwszOut;
   int nLen;

   nLen = (int)strlen(pszString) + 1;
   pwszOut = (WCHAR *)malloc(nLen * sizeof(WCHAR));
   MultiByteToWideChar(CP_UTF8, 0, pszString, -1, pwszOut, nLen);
   return pwszOut;
}


//
// Convert wide string to multibyte string using current codepage and
// allocating multibyte string dynamically
//

char LIBNETXMS_EXPORTABLE *MBStringFromWideString(const WCHAR *pwszString)
{
   char *pszOut;
   int nLen;

   nLen = (int)wcslen(pwszString) + 1;
   pszOut = (char *)malloc(nLen);
   WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR,
                       pwszString, -1, pszOut, nLen, NULL, NULL);
   return pszOut;
}


//
// Convert wide string to UTF8 string allocating UTF8 string dynamically
//

char LIBNETXMS_EXPORTABLE *UTF8StringFromWideString(const WCHAR *pwszString)
{
   char *pszOut;
   int nLen;

   nLen = WideCharToMultiByte(CP_UTF8, 0, pwszString, -1, NULL, 0, NULL, NULL);
   pszOut = (char *)malloc(nLen);
   WideCharToMultiByte(CP_UTF8, 0, pwszString, -1, pszOut, nLen, NULL, NULL);
   return pszOut;
}


//
// Get OpenSSL error string as UNICODE string
// Buffer must be at least 256 character long
//

#ifdef _WITH_ENCRYPTION

WCHAR LIBNETXMS_EXPORTABLE *ERR_error_string_W(int nError, WCHAR *pwszBuffer)
{
	char text[256];

	memset(text, 0, sizeof(text));
	ERR_error_string(nError, text);
   MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, text, -1, pwszBuffer, 256);
	return pwszBuffer;
}

#endif


#ifdef UNICODE_UCS4


//
// Convert UCS-2 to UCS-4 - internal dumb method
//

static size_t __internal_ucs2_to_ucs4(const UCS2CHAR *src, int srcLen, WCHAR *dst, int dstLen)
{
	int i, len;

	len = (int)((srcLen == -1) ? ucs2_strlen(src) : srcLen);
	if (len > (int)dstLen - 1)
		len = (int)dstLen - 1;
	for(i = 0; i < len; i++)
		dst[i] = (WCHAR)src[i];
	dst[i] = 0;
	return len;
}


//
// Convert UCS-4 to UCS-2 - internal dumb method
//

static size_t __internal_ucs4_to_ucs2(const WCHAR *src, int srcLen, UCS2CHAR *dst, int dstLen)
{
	int i, len;

	len = (int)((srcLen == -1) ? wcslen(src) : srcLen);
	if (len > (int)dstLen - 1)
		len = (int)dstLen - 1;
	for(i = 0; i < len; i++)
		dst[i] = (UCS2CHAR)src[i];
	dst[i] = 0;
	return len;
}


#ifndef __DISABLE_ICONV

//
// Convert UCS-2 to UCS-4
//

size_t LIBNETXMS_EXPORTABLE ucs2_to_ucs4(const UCS2CHAR *src, int srcLen, WCHAR *dst, int dstLen)
{
	iconv_t cd;
	const char *inbuf;
	char *outbuf;
	size_t count, inbytes, outbytes;
	
	cd = iconv_open(UCS4_CODEPAGE_NAME, UCS2_CODEPAGE_NAME);
	if (cd != (iconv_t)(-1))
	{
		inbuf = (const char *)src;
		inbytes = ((srcLen == -1) ? ucs2_strlen(src) + 1 : (size_t)srcLen) * sizeof(UCS2CHAR);
		outbuf = (char *)dst;
		outbytes = (size_t)dstLen * sizeof(WCHAR);
		count = iconv(cd, (ICONV_CONST char **)&inbuf, &inbytes, &outbuf, &outbytes);
		iconv_close(cd);
		if (count == (size_t)-1)
		{
			if (errno == EILSEQ)
			{
				count = (dstLen * sizeof(WCHAR) - outbytes) / sizeof(WCHAR);
			}
			else
			{
				count = 0;
			}
		}
		else
		{
			count = (dstLen * sizeof(WCHAR) - outbytes) / sizeof(WCHAR);
		}
		if ((srcLen == -1) && (outbytes >= sizeof(WCHAR)))
		{
			*((WCHAR *)outbuf) = 0;
		}
	}
	else
	{
		count = __internal_ucs2_to_ucs4(src, srcLen, dst, dstLen);
	}
	return count;
}


//
// Convert UCS-4 to UCS-2
//

size_t LIBNETXMS_EXPORTABLE ucs4_to_ucs2(const WCHAR *src, int srcLen, UCS2CHAR *dst, int dstLen)
{
	iconv_t cd;
	const char *inbuf;
	char *outbuf;
	size_t count, inbytes, outbytes;
	
	cd = iconv_open(UCS2_CODEPAGE_NAME, UCS4_CODEPAGE_NAME);
	if (cd != (iconv_t)(-1))
	{
		inbuf = (const char *)src;
		inbytes = ((srcLen == -1) ? wcslen(src) + 1 : (size_t)srcLen) * sizeof(WCHAR);
		outbuf = (char *)dst;
		outbytes = (size_t)dstLen * sizeof(UCS2CHAR);
		count = iconv(cd, (ICONV_CONST char **)&inbuf, &inbytes, &outbuf, &outbytes);
		iconv_close(cd);
		if (count == (size_t)-1)
		{
			if (errno == EILSEQ)
			{
				count = (dstLen * sizeof(UCS2CHAR) - outbytes) / sizeof(UCS2CHAR);
			}
			else
			{
				count = 0;
			}
		}
		else
		{
			count = (dstLen * sizeof(UCS2CHAR) - outbytes) / sizeof(UCS2CHAR);
		}
		if (((char *)outbuf - (char *)dst > sizeof(UCS2CHAR)) && (*dst == 0xFEFF))
		{
			// Remove UNICODE byte order indicator if presented
			memmove(dst, &dst[1], (char *)outbuf - (char *)dst - sizeof(UCS2CHAR));
			outbuf -= sizeof(UCS2CHAR);
		}
		if ((srcLen == -1) && (outbytes >= sizeof(UCS2CHAR)))
		{
			*((UCS2CHAR *)outbuf) = 0;
		}
	}
	else
	{
		count = __internal_ucs4_to_ucs2(src, srcLen, dst, dstLen);
	}
	return count;
}

#else   /* __DISABLE_ICONV */


//
// Convert UCS-2 to UCS-4
//

size_t LIBNETXMS_EXPORTABLE ucs2_to_ucs4(const UCS2CHAR *src, int srcLen, WCHAR *dst, int dstLen)
{
	return __internal_ucs2_to_ucs4(src, srcLen, dst, dstLen);
}


//
// Convert UCS-4 to UCS-2
//

size_t LIBNETXMS_EXPORTABLE ucs4_to_ucs2(const WCHAR *src, int srcLen, UCS2CHAR *dst, int dstLen)
{
	return __internal_ucs4_to_ucs2(src, srcLen, dst, dstLen);
}

#endif 	/* __DISABLE_ICONV */

//
// Convert UCS-4 string to UCS-2 string allocating UCS-2 string dynamically
//

UCS2CHAR LIBNETXMS_EXPORTABLE *UCS2StringFromUCS4String(const WCHAR *pwszString)
{
   UCS2CHAR *pszOut;
   int nLen;

   nLen = (int)wcslen(pwszString) + 1;
   pszOut = (UCS2CHAR *)malloc(nLen * sizeof(UCS2CHAR));
   ucs4_to_ucs2(pwszString, -1, pszOut, nLen);
   return pszOut;
}


//
// Convert UCS-2 string to UCS-4 string allocating UCS-4 string dynamically
//

WCHAR LIBNETXMS_EXPORTABLE *UCS4StringFromUCS2String(const UCS2CHAR *pszString)
{
   WCHAR *pwszOut;
   int nLen;

   nLen = (int)ucs2_strlen(pszString) + 1;
   pwszOut = (WCHAR *)malloc(nLen * sizeof(WCHAR));
   ucs2_to_ucs4(pszString, -1, pwszOut, nLen);
   return pwszOut;
}


//
// Convert UCS-2 to UTF-8
//

size_t LIBNETXMS_EXPORTABLE ucs2_to_utf8(const UCS2CHAR *src, int srcLen, char *dst, int dstLen)
{
#if HAVE_ICONV && !defined(__DISABLE_ICONV)
	iconv_t cd;
	const char *inbuf;
	char *outbuf;
	size_t count, inbytes, outbytes;
	
	cd = iconv_open("UTF-8", UCS2_CODEPAGE_NAME);
	if (cd != (iconv_t)(-1))
	{
		inbuf = (const char *)src;
		inbytes = ((srcLen == -1) ? ucs2_strlen(src) + 1 : (size_t)srcLen) * sizeof(UCS2CHAR);
		outbuf = (char *)dst;
		outbytes = (size_t)dstLen;
		count = iconv(cd, (ICONV_CONST char **)&inbuf, &inbytes, &outbuf, &outbytes);
		iconv_close(cd);
		if (count == (size_t)-1)
		{
			if (errno == EILSEQ)
			{
				count = (dstLen * sizeof(char) - outbytes) / sizeof(char);
			}
			else
			{
				count = 0;
			}
		}
		else
		{
			count = dstLen - outbytes;
		}
		if ((srcLen == -1) && (outbytes >= sizeof(char)))
		{
			*((char *)outbuf) = 0;
		}
	}
	else
	{
		*dst = 0;
		count = 0;
	}
	return count;
	
#else

   const UCS2CHAR *psrc;
   char *pdst;
   int pos, size;

   size = (srcLen == -1) ? ucs2_strlen(src) : srcLen;
   if (size >= dstLen)
      size = dstLen - 1;
   for(psrc = src, pos = 0, pdst = dst; pos < size; pos++, psrc++, pdst++)
      *pdst = (*psrc < 256) ? (char)(*psrc) : '?';
   *pdst = 0;
   return size;
#endif
}

#endif	/* UNICODE_UCS4 */


#if !defined(_WIN32) && !defined(UNICODE)

//
// Convert UCS-2 to multibyte
//

size_t LIBNETXMS_EXPORTABLE ucs2_to_mb(const UCS2CHAR *src, int srcLen, char *dst, int dstLen)
{
#if HAVE_ICONV && !defined(__DISABLE_ICONV)
	iconv_t cd;
	const char *inbuf;
	char *outbuf;
	size_t count, inbytes, outbytes;
	
	cd = iconv_open(m_cpDefault, UCS2_CODEPAGE_NAME);
	if (cd != (iconv_t)(-1))
	{
		inbuf = (const char *)src;
		inbytes = ((srcLen == -1) ? ucs2_strlen(src) + 1 : (size_t)srcLen) * sizeof(UCS2CHAR);
		outbuf = (char *)dst;
		outbytes = (size_t)dstLen;
		count = iconv(cd, (ICONV_CONST char **)&inbuf, &inbytes, &outbuf, &outbytes);
		iconv_close(cd);
		if (count == (size_t)-1)
		{
			if (errno == EILSEQ)
			{
				count = (dstLen * sizeof(char) - outbytes) / sizeof(char);
			}
			else
			{
				count = 0;
			}
		}
		if ((srcLen == -1) && (outbytes >= sizeof(char)))
		{
			*((char *)outbuf) = 0;
		}
	}
	else
	{
		*dst = 0;
		count = 0;
	}
	return count;
	
#else

   const UCS2CHAR *psrc;
   char *pdst;
   int pos, size;

   size = (srcLen == -1) ? (int)ucs2_strlen(src) : srcLen;
   if (size >= dstLen)
      size = dstLen - 1;
   for(psrc = src, pos = 0, pdst = dst; pos < size; pos++, psrc++, pdst++)
      *pdst = (*psrc < 256) ? (char)(*psrc) : '?';
   *pdst = 0;
   return size;
#endif
}


//
// Convert multibyte to UCS-2
//

size_t LIBNETXMS_EXPORTABLE mb_to_ucs2(const char *src, int srcLen, UCS2CHAR *dst, int dstLen)
{
#if HAVE_ICONV && !defined(__DISABLE_ICONV)
	iconv_t cd;
	const char *inbuf;
	char *outbuf;
	size_t count, inbytes, outbytes;
	
	cd = iconv_open(UCS2_CODEPAGE_NAME, m_cpDefault);
	if (cd != (iconv_t)(-1))
	{
		inbuf = (const char *)src;
		inbytes = (srcLen == -1) ? strlen(src) + 1 : (size_t)srcLen;
		outbuf = (char *)dst;
		outbytes = (size_t)dstLen * sizeof(UCS2CHAR);
		count = iconv(cd, (ICONV_CONST char **)&inbuf, &inbytes, &outbuf, &outbytes);
		iconv_close(cd);
		if (count == (size_t)-1)
		{
			if (errno == EILSEQ)
			{
				count = (dstLen * sizeof(UCS2CHAR) - outbytes) / sizeof(UCS2CHAR);
			}
			else
			{
				count = 0;
			}
		}
		if (((char *)outbuf - (char *)dst > sizeof(UCS2CHAR)) && (*dst == 0xFEFF))
		{
			// Remove UNICODE byte order indicator if presented
			memmove(dst, &dst[1], (char *)outbuf - (char *)dst - sizeof(UCS2CHAR));
			outbuf -= sizeof(UCS2CHAR);
		}
		if ((srcLen == -1) && (outbytes >= sizeof(UCS2CHAR)))
		{
			*((UCS2CHAR *)outbuf) = 0;
		}
	}
	else
	{
		*dst = 0;
		count = 0;
	}
	return count;
	
#else

	const char *psrc;
	UCS2CHAR *pdst;
	int pos, size;

	size = (srcLen == -1) ? (int)strlen(src) : srcLen;
	if (size >= dstLen)
		size = dstLen - 1;
	for(psrc = src, pos = 0, pdst = dst; pos < size; pos++, psrc++, pdst++)
		*pdst = (UCS2CHAR)(*psrc);
	*pdst = 0;

	return size;
#endif
}


//
// Convert multibyte string to UCS-2 string allocating UCS-2 string dynamically
//

#if !UNICODE_UCS2
UCS2CHAR LIBNETXMS_EXPORTABLE *UCS2StringFromMBString(const char *pszString)
{
	UCS2CHAR *pszOut;
	int nLen;

	nLen = (int)strlen(pszString) + 1;
	pszOut = (UCS2CHAR *)malloc(nLen * sizeof(UCS2CHAR));
	mb_to_ucs2(pszString, -1, pszOut, nLen);
	return pszOut;
}
#endif


//
// Convert UCS-2 string to multibyte string allocating multibyte string dynamically
//

#if !UNICODE_UCS2
char LIBNETXMS_EXPORTABLE *MBStringFromUCS2String(const UCS2CHAR *pszString)
{
	char *pszOut;
	int nLen;

	nLen = (int)ucs2_strlen(pszString) + 1;
	pszOut = (char *)malloc(nLen);
	ucs2_to_mb(pszString, -1, pszOut, nLen);
	return pszOut;
}
#endif

#endif	/* !defined(_WIN32) && !defined(UNICODE) */


//
// UNIX UNICODE specific wrapper functions
//

#if !defined(_WIN32) && defined(UNICODE)


//
// Wide character version of some libc functions
//

#if !HAVE_WFOPEN

FILE LIBNETXMS_EXPORTABLE *wfopen(const WCHAR *_name, const WCHAR *_type)
{
	char *name, *type;
	FILE *f;
	
	name = MBStringFromWideString(_name);
	type = MBStringFromWideString(_type);
	f = fopen(name, type);
	free(name);
	free(type);
	return f;
}

#endif

#if !HAVE_WOPEN

int LIBNETXMS_EXPORTABLE wopen(const WCHAR *_name, int flags, ...)
{
	char *name;
	int rc;
	
	name = MBStringFromWideString(_name);
	if (flags & O_CREAT)
	{
		va_list args;
		
		va_start(args, flags);
		rc = open(name, flags, va_arg(args, mode_t));
		va_end(args); 
	}
	else
	{
		rc = open(name, flags);
	}
	free(name);
	return rc;
}

#endif

#if !HAVE_WSTAT

int wstat(const WCHAR *_path, struct stat *_sbuf)
{
	char path[MAX_PATH];
	
	WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR,
                       _path, -1, path, MAX_PATH, NULL, NULL);
	return stat(path, _sbuf);
}

#endif

#if !HAVE_WUNLINK

int wunlink(const WCHAR *_path)
{
	char path[MAX_PATH];
	
	WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR,
	                    _path, -1, path, MAX_PATH, NULL, NULL);
	return unlink(path);
}

#endif

#if !HAVE_WRENAME

int wrename(const WCHAR *_oldpath, const WCHAR *_newpath)
{
	char oldpath[MAX_PATH], newpath[MAX_PATH];
	
	WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR,
	                    _oldpath, -1, oldpath, MAX_PATH, NULL, NULL);
	WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR,
	                    _newpath, -1, newpath, MAX_PATH, NULL, NULL);
	return rename(oldpath, newpath);
}

#endif

#if !HAVE_WGETENV

WCHAR *wgetenv(const WCHAR *_string)
{
	char name[256], *p;
	static WCHAR value[8192];
	
	WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR, _string, -1, name, 256, NULL, NULL);
	p = getenv(name);
	if (p == NULL)
		return NULL;

	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, p, -1, value, 8192);
	return value;
}

#endif

#if !HAVE_WCSERROR && HAVE_STRERROR

WCHAR *wcserror(int errnum)
{
	static WCHAR value[8192];

	MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, strerror(errnum), -1, value, 8192);
	return value;
}

#endif

#if !HAVE_WCSERROR_R && HAVE_STRERROR_R

#if HAVE_POSIX_STRERROR_R
int wcserror_r(int errnum, WCHAR *strerrbuf, size_t buflen)
#else
WCHAR *wcserror_r(int errnum, WCHAR *strerrbuf, size_t buflen)
#endif
{
	char *mbbuf;
#if HAVE_POSIX_STRERROR_R
	int err;
#endif

	mbbuf = (char *)malloc(buflen);
	if (mbbuf != NULL)
	{
#if HAVE_POSIX_STRERROR_R
		err = strerror_r(errnum, mbbuf, buflen);
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, mbbuf, -1, strerrbuf, buflen);
#else
		MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, strerror_r(errnum, mbbuf, buflen), -1, strerrbuf, buflen);
#endif
		free(mbbuf);
	}
	else
	{
		*strerrbuf = 0;
	}

#if HAVE_POSIX_STRERROR_R
	return err;
#else
	return strerrbuf;
#endif
}

#endif


//
// Wrappers for wprintf family
//
// All these wrappers replaces %s with %S and %c with %C
// because in POSIX version of wprintf functions %s and %c
// means "multibyte string/char" instead of expected "UNICODE string/char"
//

int LIBNETXMS_EXPORTABLE nx_wprintf(const WCHAR *format, ...)
{
	va_list args;
	int rc;

	va_start(args, format);
	rc = nx_vwprintf(format, args);
	va_end(args);
	return rc;
}

int LIBNETXMS_EXPORTABLE nx_fwprintf(FILE *fp, const WCHAR *format, ...)
{
	va_list args;
	int rc;

	va_start(args, format);
	rc = nx_vfwprintf(fp, format, args);
	va_end(args);
	return rc;
}

int LIBNETXMS_EXPORTABLE nx_swprintf(WCHAR *buffer, size_t size, const WCHAR *format, ...)
{
	va_list args;
	int rc;

	va_start(args, format);
	rc = nx_vswprintf(buffer, size, format, args);
	va_end(args);
	return rc;
}

static WCHAR *ReplaceFormatSpecs(const WCHAR *oldFormat)
{
	WCHAR *fmt, *p;
	int state = 0;
	bool hmod;

	fmt = wcsdup(oldFormat);
	for(p = fmt; *p != 0; p++)
	{
		switch(state)
		{
			case 0:	// Normal text
				if (*p == L'%')
				{
					state = 1;
					hmod = false;
				}
				break;
			case 1:	// Format start
				switch(*p)
				{
					case L's':
						if (hmod)
						{
							memmove(p - 1, p, wcslen(p - 1) * sizeof(TCHAR));
						}
						else
						{
							*p = L'S';
						}
						state = 0;
						break;
					case L'S':
						*p = L's';
						state = 0;
						break;
					case L'c':
						if (hmod)
						{
							memmove(p - 1, p, wcslen(p - 1) * sizeof(TCHAR));
						}
						else
						{
							*p = L'C';
						}
						state = 0;
						break;
					case L'C':
						*p = L'c';
						state = 0;
						break;
					case L'.':	// All this characters could be part of format specifier	
					case L'*':	// and has no interest for us
					case L'+':
					case L'-':
					case L' ':
					case L'#':
					case L'0':
					case L'1':
					case L'2':
					case L'3':
					case L'4':
					case L'5':
					case L'6':
					case L'7':
					case L'8':
					case L'9':
					case L'l':
					case L'L':
					case L'F':
					case L'N':
					case L'w':
						break;
					case L'h':	// check for %hs
						hmod = true;
						break;
					default:		// All other cahacters means end of format
						state = 0;
						break;

				}
				break;
		}
	}
	return fmt;
}

int LIBNETXMS_EXPORTABLE nx_vwprintf(const WCHAR *format, va_list args)
{
	WCHAR *fmt;
	int rc;

	fmt = ReplaceFormatSpecs(format);
	rc = vwprintf(fmt, args);
	free(fmt);
	return rc;
}

int LIBNETXMS_EXPORTABLE nx_vfwprintf(FILE *fp, const WCHAR *format, va_list args)
{
	WCHAR *fmt;
	int rc;

	fmt = ReplaceFormatSpecs(format);
	rc = vfwprintf(fp, fmt, args);
	free(fmt);
	return rc;
}

int LIBNETXMS_EXPORTABLE nx_vswprintf(WCHAR *buffer, size_t size, const WCHAR *format, va_list args)
{
	WCHAR *fmt;
	int rc;

	fmt = ReplaceFormatSpecs(format);
	rc = vswprintf(buffer, size, fmt, args);
	free(fmt);
	return rc;
}

#endif
