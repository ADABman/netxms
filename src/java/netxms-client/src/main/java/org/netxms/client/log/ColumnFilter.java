/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2010 Victor Kirhenshtein
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.client.log;

import java.util.HashSet;
import java.util.Set;

import org.netxms.base.NXCPMessage;

/**
 * @author Victor Kirhenshtein
 *
 */
public class ColumnFilter
{
	public static final int EQUALS = 0;
	public static final int RANGE = 1;
	public static final int SET = 2;
	public static final int LIKE = 3;
	
	public static final int AND = 0;
	public static final int OR = 1;
	
	private int type;
	private long rangeFrom;
	private long rangeTo;
	private long equalsTo;
	private String like;
	private HashSet<ColumnFilter> set;
	private int operation;	// Set operation: AND or OR
	
	/**
	 * Create filter of type EQUALS
	 * 
	 * @param value
	 */
	public ColumnFilter(long value)
	{
		type = EQUALS;
		equalsTo = value;
	}

	/**
	 * Create filter of type RANGE
	 * 
	 * @param rangeFrom
	 * @param rangeTo
	 */
	public ColumnFilter(long rangeFrom, long rangeTo)
	{
		type = RANGE;
		this.rangeFrom = rangeFrom;
		this.rangeTo = rangeTo;
	}

	/**
	 * Create filter of type LIKE
	 * 
	 * @param value
	 */
	public ColumnFilter(String value)
	{
		type = LIKE;
		like = value;
	}

	/**
	 * Create filter of type SET
	 */
	public ColumnFilter()
	{
		type = SET;
		set = new HashSet<ColumnFilter>();
		operation = AND;
	}
	
	/**
	 * Add new element to SET type filter
	 *  
	 * @param filter
	 */
	public void addSubFilter(ColumnFilter filter)
	{
		if (type == SET)
			set.add(filter);
	}
	
	/**
	 * Fill NXCP message with filters' data
	 * 
	 * @param msg NXCP message
	 * @param baseId Base variable ID
	 * @return Number of variables used
	 */
	int fillMessage(final NXCPMessage msg, final long baseId)
	{
		int varCount = 1;
		msg.setVariableInt16(baseId, type);
		switch(type)
		{
			case EQUALS:
				msg.setVariableInt64(baseId + 1, equalsTo);
				varCount++;
				break;
			case RANGE:
				msg.setVariableInt64(baseId + 1, rangeFrom);
				msg.setVariableInt64(baseId + 2, rangeTo);
				varCount += 2;
				break;
			case LIKE:
				msg.setVariable(baseId + 1, like);
				varCount++;
				break;
			case SET:
				msg.setVariableInt16(baseId + 1, operation);
				msg.setVariableInt16(baseId + 2, set.size());
				varCount += 2;
				long varId = baseId + 3;
				for(final ColumnFilter f : set)
				{
					int count = f.fillMessage(msg, varId);
					varId += count;
					varCount += count;
				}
				break;
		}
		return varCount;
	}

	/**
	 * @return the rangeFrom
	 */
	public long getRangeFrom()
	{
		return rangeFrom;
	}

	/**
	 * @param rangeFrom the rangeFrom to set
	 */
	public void setRangeFrom(long rangeFrom)
	{
		this.rangeFrom = rangeFrom;
	}

	/**
	 * @return the rangeTo
	 */
	public long getRangeTo()
	{
		return rangeTo;
	}

	/**
	 * @param rangeTo the rangeTo to set
	 */
	public void setRangeTo(long rangeTo)
	{
		this.rangeTo = rangeTo;
	}

	/**
	 * @return the equalsTo
	 */
	public long getEqualsTo()
	{
		return equalsTo;
	}

	/**
	 * @param equalsTo the equalsTo to set
	 */
	public void setEqualsTo(long equalsTo)
	{
		this.equalsTo = equalsTo;
	}

	/**
	 * @return the like
	 */
	public String getLike()
	{
		return like;
	}

	/**
	 * @param like the like to set
	 */
	public void setLike(String like)
	{
		this.like = like;
	}

	/**
	 * @return the operation
	 */
	public int getOperation()
	{
		return operation;
	}

	/**
	 * @param operation the operation to set
	 */
	public void setOperation(int operation)
	{
		this.operation = operation;
	}

	/**
	 * @return the type
	 */
	public int getType()
	{
		return type;
	}

	/**
	 * Get sub-filters.
	 * 
	 * @return Set of sub-filters
	 */
	public Set<ColumnFilter> getSubFilters()
	{
		return set;
	}
}
