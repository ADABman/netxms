/* 
** NetXMS - Network Management System
** Copyright (C) 2003-2009 Victor Kirhenshtein
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
** File: geolocation.cpp
**
**/

#include "libnetxms.h"
#include <geolocation.h>
#include <math.h>

static const double ROUND_OFF = 0.00000001;


//
// Default constructor - create location of type UNSET
//

GeoLocation::GeoLocation()
{
	m_type = GL_UNSET;
	m_lat = 0;
	m_lon = 0;
	posToString(true, 0);
	posToString(false, 0);
	m_isValid = true;
}


//
// Constructor - create location from double lat/lon values
//

GeoLocation::GeoLocation(int type, double lat, double lon)
{
	m_type = type;
	m_lat = lat;
	m_lon = lon;
	posToString(true, lat);
	posToString(false, lon);
	m_isValid = true;
}


//
// Constructor - create location from string lat/lon values
//

GeoLocation::GeoLocation(int type, const TCHAR *lat, const TCHAR *lon)
{
	m_type = type;
	m_isValid = parseLatitude(lat) && parseLongitude(lon);
	posToString(true, m_lat);
	posToString(false, m_lon);
}


//
// Copy constructor
//

GeoLocation::GeoLocation(GeoLocation &src)
{
	m_type = src.m_type;
	m_lat = src.m_lat;
	m_lon = src.m_lon;
	nx_strncpy(m_latStr, src.m_latStr, 20);
	nx_strncpy(m_lonStr, src.m_lonStr, 20);
	m_isValid = src.m_isValid;
}


//
// Destructor
//

GeoLocation::~GeoLocation()
{
}


//
// Assignment operator
//

GeoLocation& GeoLocation::operator =(const GeoLocation &src)
{
	m_type = src.m_type;
	m_lat = src.m_lat;
	m_lon = src.m_lon;
	nx_strncpy(m_latStr, src.m_latStr, 20);
	nx_strncpy(m_lonStr, src.m_lonStr, 20);
	m_isValid = src.m_isValid;
	return *this;
}


//
// Getters for degree, minutes, and seconds from double value
//

int GeoLocation::getIntegerDegree(double pos)
{
	return (int)(fabs(pos) + ROUND_OFF);
}

int GeoLocation::getIntegerMinutes(double pos)
{
	double d = fabs(pos) + ROUND_OFF;
	return (int)((d - (double)((int)d)) * 60.0);
}

double GeoLocation::getDecimalSeconds(double pos)
{
	double d = fabs(pos) * 60.0 + ROUND_OFF;
	return (d - (double)((int)d)) * 60.0;
}


//
// Convert position to string
//

void GeoLocation::posToString(bool isLat, double pos)
{
	TCHAR *buffer = isLat ? m_latStr : m_lonStr;

	// Chack range
	if ((pos < -180.0) || (pos > 180.0))
	{
		_tcscpy(buffer, _T("<invalid>"));
		return;
	}

	// Encode hemisphere
	if (isLat)
	{
		*buffer = (pos < 0) ? _T('S') : _T('N');
	}
	else
	{
		*buffer = (pos < 0) ? _T('W') : _T('E');
	}
	buffer++;
	*buffer++ = _T(' ');

	_sntprintf(buffer, 18, _T("%02d� %02d' %02.3f\""), getIntegerDegree(pos), getIntegerMinutes(pos), getDecimalSeconds(pos));
}


//
// Parse latitude/longitude string
//

double GeoLocation::parse(const TCHAR *str, bool isLat, bool *isValid)
{
	*isValid = false;

	// Prepare input string
	TCHAR *in = _tcsdup(str);
	StrStrip(in);

	// Check if string given is just double value
	TCHAR *eptr;
	double value = _tcstod(in, &eptr);
	if (*eptr == 0)
	{
		*isValid = true;
	}
	else   // If not a valid double, check if it's in DMS form
	{
		// Check for invalid characters
		if (_tcsspn(in, isLat ? _T("0123456789.�'\" NS") : _T("0123456789.�'\" EW")) == _tcslen(in))
		{
			TCHAR *curr = in;

			int sign = 0;
			if ((*curr == _T('N')) || (*curr == _T('E')))
			{
				sign = 1;
				curr++;
			}
			else if ((*curr == _T('S')) || (*curr == _T('W')))
			{
				sign = -1;
				curr++;
			}

			while(*curr == _T(' '))
				curr++;

			double deg = 0.0, min = 0.0, sec = 0.0;

			deg = _tcstod(curr, &eptr);
			if (*eptr == 0)	// End of string
				goto finish_parsing;
			if (*eptr != _T('�'))
				goto cleanup;	// Unexpected character
			curr = eptr + 1;
			while(*curr == _T(' '))
				curr++;

			min = _tcstod(curr, &eptr);
			if (*eptr == 0)	// End of string
				goto finish_parsing;
			if (*eptr != _T('\''))
				goto cleanup;	// Unexpected character
			curr = eptr + 1;
			while(*curr == _T(' '))
				curr++;

			sec = _tcstod(curr, &eptr);
			if (*eptr == 0)	// End of string
				goto finish_parsing;
			if (*eptr != _T('"'))
				goto cleanup;	// Unexpected character
			curr = eptr + 1;
			while(*curr == _T(' '))
				curr++;

			if ((*curr == _T('N')) || (*curr == _T('E')))
			{
				sign = 1;
				curr++;
			}
			else if ((*curr == _T('S')) || (*curr == _T('W')))
			{
				sign = -1;
				curr++;
			}

			if (sign == 0)
				goto cleanup;	// Hemisphere was not specified

finish_parsing:
			value = sign * (deg + min / 60.0 + sec / 3600.0);
			*isValid = true;
		}
	}

cleanup:
	free(in);
	return value;
}


//
// Parse latitude
//

bool GeoLocation::parseLatitude(const TCHAR *lat)
{
	bool isValid;

	m_lat = parse(lat, true, &isValid);
	if (!isValid)
		m_lat = 0.0;
	return isValid;
}


//
// Parse longitude
//

bool GeoLocation::parseLongitude(const TCHAR *lon)
{
	bool isValid;

	m_lon = parse(lon, false, &isValid);
	if (!isValid)
		m_lon = 0.0;
	return isValid;
}
