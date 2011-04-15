/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2011 Victor Kirhenshtein
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
package org.netxms.ui.eclipse.topology.widgets;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.widgets.Composite;
import org.netxms.client.NXCSession;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.objects.Interface;
import org.netxms.client.objects.Node;
import org.netxms.client.topology.Port;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.topology.widgets.helpers.PortInfo;

/**
 * View of switch/router ports
 *
 */
public class DeviceView extends Composite
{
	private long nodeId;
	private NXCSession session;
	private Map<Long, PortInfo> ports = new HashMap<Long, PortInfo>();
	private Map<Integer, SlotView> slots = new HashMap<Integer, SlotView>();
	private boolean portStatusVisible = true;
	
	/**
	 * @param parent
	 * @param style
	 */
	public DeviceView(Composite parent, int style)
	{
		super(parent, style);
		
		session = (NXCSession)ConsoleSharedData.getSession();
		
		RowLayout layout = new RowLayout();
		layout.type = SWT.VERTICAL;
		layout.fill = true;
		layout.wrap = false;
		setLayout(layout);
		
		setBackground(new Color(getDisplay(), 255, 255, 255));
	}
	
	/**
	 * Refresh widget
	 */
	public void refresh()
	{
		GenericObject object = session.findObjectById(nodeId);
		if ((object == null) || !(object instanceof Node))
			return;
		
		for(SlotView s : slots.values())
			s.dispose();
		slots.clear();
		ports.clear();
		
		List<Interface> interfaces = new ArrayList<Interface>();
		for(GenericObject o: object.getAllChilds(GenericObject.OBJECT_INTERFACE))
		{
			if ((((Interface)o).getFlags() & Interface.IF_PHYSICAL_PORT) != 0)
				interfaces.add((Interface)o);
		}
		Collections.sort(interfaces, new Comparator<Interface>() {
			@Override
			public int compare(Interface arg0, Interface arg1)
			{
				if (arg0.getSlot() == arg1.getSlot())
					return arg0.getPort() - arg1.getPort();
				return arg0.getSlot() - arg1.getSlot();
			}
		});
		
		for(Interface iface : interfaces)
		{
			int slot = iface.getSlot();
			SlotView sv = slots.get(slot);
			if (sv == null)
			{
				sv = new SlotView(this, SWT.NONE, slot);
				sv.setPortStatusVisible(portStatusVisible);
				slots.put(slot, sv);
			}
			
			PortInfo p = new PortInfo(iface);
			ports.put(iface.getObjectId(), p);
			sv.addPort(p);
		}
		
		layout();
	}

	/**
	 * @return the nodeId
	 */
	public long getNodeId()
	{
		return nodeId;
	}

	/**
	 * @param nodeId the nodeId to set
	 */
	public void setNodeId(long nodeId)
	{
		this.nodeId = nodeId;
		refresh();
	}

	/**
	 * @return the portStatusVisible
	 */
	public boolean isPortStatusVisible()
	{
		return portStatusVisible;
	}

	/**
	 * @param portStatusVisible the portStatusVisible to set
	 */
	public void setPortStatusVisible(boolean portStatusVisible)
	{
		this.portStatusVisible = portStatusVisible;
	}
	
	/**
	 * Set port highlight
	 * 
	 * @param ports
	 */
	public void setHighlight(Port[] ports)
	{
		clearHighlight(false);
		
		for(Port p : ports)
		{
			SlotView sv = slots.get(p.getSlot());
			if (sv != null)
			{
				sv.addHighlight(p);
			}
		}
		
		for(SlotView sv : slots.values())
			sv.redraw();
	}
	
	/**
	 * Clear port highlight.
	 * 
	 * @param doRedraw if true, control will be redrawn
	 */
	public void clearHighlight(boolean doRedraw)
	{
		for(SlotView sv : slots.values())
		{
			sv.clearHighlight();
			if (doRedraw)
				sv.redraw();
		}
	}
}
