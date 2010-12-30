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
package org.netxms.client;

import org.netxms.base.NXCPCodes;
import org.netxms.base.NXCPMessage;

/**
 * Connection point information
 *
 */
public class ConnectionPoint
{
	private long localNodeId;
	private long localInterfaceId;
	private MacAddress localMacAddress;
	private long nodeId;
	private long interfaceId;
	private int interfaceIndex;
	private Object data;
	
	/**
	 * Create connection point information from NXCP message
	 * 
	 * @param msg NXCP message
	 */
	protected ConnectionPoint(NXCPMessage msg)
	{
		nodeId = msg.getVariableAsInt64(NXCPCodes.VID_OBJECT_ID);
		interfaceId = msg.getVariableAsInt64(NXCPCodes.VID_INTERFACE_ID);
		interfaceIndex = msg.getVariableAsInteger(NXCPCodes.VID_IF_INDEX);
		localNodeId = msg.getVariableAsInt64(NXCPCodes.VID_LOCAL_NODE_ID);
		localInterfaceId = msg.getVariableAsInt64(NXCPCodes.VID_LOCAL_INTERFACE_ID);
		localMacAddress = new MacAddress(msg.getVariableAsBinary(NXCPCodes.VID_MAC_ADDR));
	}

	/**
	 * @return the nodeId
	 */
	public long getNodeId()
	{
		return nodeId;
	}

	/**
	 * @return the interfaceId
	 */
	public long getInterfaceId()
	{
		return interfaceId;
	}

	/**
	 * @return the interfaceIndex
	 */
	public int getInterfaceIndex()
	{
		return interfaceIndex;
	}

	/**
	 * @return the localNodeId
	 */
	public long getLocalNodeId()
	{
		return localNodeId;
	}

	/**
	 * @return the localInterfaceId
	 */
	public long getLocalInterfaceId()
	{
		return localInterfaceId;
	}

	/**
	 * @return the localMacAddress
	 */
	public MacAddress getLocalMacAddress()
	{
		return localMacAddress;
	}

	/**
	 * Get user data.
	 * 
	 * @return user data
	 */
	public Object getData()
	{
		return data;
	}

	/**
	 * Set user data.
	 * 
	 * @param data user data
	 */
	public void setData(Object data)
	{
		this.data = data;
	}
}
