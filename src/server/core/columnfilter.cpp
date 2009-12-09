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
** File: columnfilter.cpp
**
**/

#include "nxcore.h"


//
// Constructor
//

ColumnFilter::ColumnFilter(CSCPMessage *msg, const TCHAR *column, DWORD baseId)
{
	DWORD varId;

	m_column = _tcsdup(column);
	m_type = (int)msg->GetVariableShort(baseId);
	switch(m_type)
	{
		case FILTER_EQUALS:
			m_value.equalsTo = msg->GetVariableInt64(baseId + 1);
			m_varCount = 2;
			break;
		case FILTER_RANGE:
			m_value.range.start = msg->GetVariableInt64(baseId + 1);
			m_value.range.end = msg->GetVariableInt64(baseId + 2);
			m_varCount = 3;
			break;
		case FILTER_LIKE:
			m_value.like = msg->GetVariableStr(baseId + 1);
			m_varCount = 2;
			break;
		case FILTER_SET:
			m_value.set.operation = msg->GetVariableShort(baseId + 1);
			m_value.set.count = msg->GetVariableShort(baseId + 2);
			m_varCount = 3;

			m_value.set.filters = (ColumnFilter **)malloc(sizeof(ColumnFilter *) * m_value.set.count);
			varId = baseId + 3;
			for(int i = 0; i < m_value.set.count; i++)
			{
				ColumnFilter *filter = new ColumnFilter(msg, column, varId);
				m_value.set.filters[i] = filter;
				varId += filter->getVariableCount();
				m_varCount += filter->getVariableCount();
			}
			break;
		default:
			break;
	}
}


//
// Destructor
//

ColumnFilter::~ColumnFilter()
{
	safe_free(m_column);
	switch(m_type)
	{
		case FILTER_LIKE:
			safe_free(m_value.like);
			break;
		case FILTER_SET:
			for(int i = 0; i < m_value.set.count; i++)
				delete m_value.set.filters[i];
			safe_free(m_value.set.filters);
			break;
	}
}


//
// Generate SQL for column filter
//

String ColumnFilter::generateSql()
{
	String sql;

	switch(m_type)
	{
		case FILTER_EQUALS:
			sql.addFormattedString(_T("%s = ") INT64_FMT, m_column, m_value.equalsTo);
			break;
		case FILTER_RANGE:
			sql.addFormattedString(_T("%s BETWEEN ") INT64_FMT _T(" AND ") INT64_FMT, m_column, m_value.range.start, m_value.range.end);
			break;
		case FILTER_LIKE:
			sql.addFormattedString(_T("%s LIKE %s"), m_column, (const TCHAR *)DBPrepareString(g_hCoreDB, m_value.like));
			break;
		case FILTER_SET:
			bool first = true;
			for(int i = 0; i < m_value.set.count; i++)
			{
				String subExpr = m_value.set.filters[i]->generateSql();
				if (!subExpr.isEmpty())
				{
					if (first)
					{
						first = false;
					}
					else
					{
						sql += (m_value.set.operation == SET_OPERATION_AND) ? _T(" AND ") : _T(" OR ");
					}
					sql += _T("(");
					sql += subExpr;
					sql += _T(")");
				}
			}
			break;
	}

	return sql;
}
