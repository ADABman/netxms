/* 
** NetXMS - Network Management System
** NetXMS Foundation Library
** Copyright (C) 2003-2011 Victor Kirhenshtein
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
** File: stringlist.cpp
**
**/

#include "libnetxms.h"

#define ALLOCATION_STEP		16


//
// Constructor
//

StringList::StringList()
{
	m_count = 0;
	m_allocated = ALLOCATION_STEP;
	m_values = (TCHAR **)malloc(sizeof(TCHAR *) * m_allocated);
	memset(m_values, 0, sizeof(TCHAR *) * m_allocated);
}


//
// Destructor
//

StringList::~StringList()
{
	for(int i = 0; i < m_count; i++)
		safe_free(m_values[i]);
	safe_free(m_values);
}


//
// Clear list
//

void StringList::clear()
{
	for(int i = 0; i < m_count; i++)
		safe_free(m_values[i]);
	m_count = 0;
	memset(m_values, 0, sizeof(TCHAR *) * m_allocated);
}


//
// Add preallocated string to list
//

void StringList::addPreallocated(TCHAR *value)
{
	if (m_allocated == m_count)
	{
		m_allocated += ALLOCATION_STEP;
		m_values = (TCHAR **)realloc(m_values, sizeof(TCHAR *) * m_allocated);
	}
	m_values[m_count++] = value;
}


//
// Add string to list
//

void StringList::add(const TCHAR *value)
{
	addPreallocated(_tcsdup(value));
}


//
// Add signed 32-bit integer as string
//

void StringList::add(LONG value)
{
	TCHAR buffer[32];

	_sntprintf(buffer, 32, _T("%d"), (int)value);
	add(buffer);
}


//
// Add unsigned 32-bit integer as string
//

void StringList::add(DWORD value)
{
	TCHAR buffer[32];

	_sntprintf(buffer, 32, _T("%u"), (unsigned int)value);
	add(buffer);
}


//
// Add signed 64-bit integer as string
//

void StringList::add(INT64 value)
{
	TCHAR buffer[32];

	_sntprintf(buffer, 32, INT64_FMT, value);
	add(buffer);
}


//
// Add unsigned 64-bit integer as string
//

void StringList::add(QWORD value)
{
	TCHAR buffer[32];

	_sntprintf(buffer, 32, UINT64_FMT, value);
	add(buffer);
}


//
// Add floating point number as string
//

void StringList::add(double value)
{
	TCHAR buffer[64];

	_sntprintf(buffer, 64, _T("%f"), value);
	add(buffer);
}


//
// Replace string at given position
//

void StringList::replace(int index, const TCHAR *value)
{
	if ((index < 0) || (index >= m_count))
		return;

	safe_free(m_values[index]);
	m_values[index] = _tcsdup(value);
}


//
// Get index of given value. Returns zero-based index ot -1 
// if given value not found in the list. If list contains duplicate values,
// index of first occurence will be returned.
//

int StringList::getIndex(const TCHAR *value)
{
	for(int i = 0; i < m_count; i++)
		if ((m_values[i] != NULL) && !_tcscmp(m_values[i], value))
			return i;
	return -1;
}

int StringList::getIndexIgnoreCase(const TCHAR *value)
{
	for(int i = 0; i < m_count; i++)
		if ((m_values[i] != NULL) && !_tcsicmp(m_values[i], value))
			return i;
	return -1;
}
