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
import java.util.List;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.GC;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Canvas;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.netxms.client.topology.Port;
import org.netxms.ui.eclipse.console.resources.StatusDisplayInfo;
import org.netxms.ui.eclipse.topology.widgets.helpers.PortInfo;

/**
 * Single slot view
 */
public class SlotView extends Canvas implements PaintListener
{
	private static final int HORIZONTAL_MARGIN = 20;
	private static final int VERTICAL_MARGIN = 10;
	private static final int HORIZONTAL_SPACING = 10;
	private static final int VERTICAL_SPACING = 10;
	private static final int PORT_WIDTH = 40;
	private static final int PORT_HEIGHT = 30;
	
	private static final Color BACKGROUND_COLOR = new Color(Display.getDefault(), 224, 224, 224);
	private static final Color HIGHLIGHT_COLOR = new Color(Display.getDefault(), 64, 156, 224);
	
	private List<PortInfo> ports = new ArrayList<PortInfo>();
	private int rowCount = 2;
	private String slotName;
	private Point nameSize;
	private boolean portStatusVisible = true;
	
	/**
	 * @param parent
	 * @param style
	 */
	public SlotView(Composite parent, int style, String slotName)
	{
		super(parent, style | SWT.BORDER);
		this.slotName = slotName;
		
		GC gc = new GC(getDisplay());
		nameSize = gc.textExtent(slotName);
		gc.dispose();
		
		addPaintListener(this);
	}
	
	/**
	 * @param p
	 */
	public void addPort(PortInfo p)
	{
		ports.add(p);
	}

	/* (non-Javadoc)
	 * @see org.eclipse.swt.events.PaintListener#paintControl(org.eclipse.swt.events.PaintEvent)
	 */
	@Override
	public void paintControl(PaintEvent e)
	{
		e.gc.drawText(slotName, HORIZONTAL_MARGIN, (getSize().y - nameSize.y) / 2);
		
		int x = HORIZONTAL_MARGIN + nameSize.x + HORIZONTAL_SPACING;
		int y = VERTICAL_MARGIN;
		int row = 0;
		for(PortInfo p : ports)
		{
			drawPort(p, x, y, e.gc);
			row++;
			if (row == rowCount)
			{
				row = 0;
				y = VERTICAL_MARGIN;
				x += HORIZONTAL_SPACING + 40;
			}
			else
			{
				y += VERTICAL_SPACING + 30;
			}
		}
	}
	
	/**
	 * Draw single port
	 * 
	 * @param p port information
	 * @param x X coordinate of top left corner
	 * @param y Y coordinate of top left corner
	 */
	private void drawPort(PortInfo p, int x, int y, GC gc)
	{
		final String label = Integer.toString(p.getPort());
		Rectangle rect = new Rectangle(x, y, PORT_WIDTH, PORT_HEIGHT);
		
		if (p.isHighlighted())
		{
			gc.setBackground(HIGHLIGHT_COLOR);
			gc.fillRectangle(rect);
		}
		else if (portStatusVisible)
		{
			gc.setBackground(StatusDisplayInfo.getStatusColor(p.getStatus()));
			gc.fillRectangle(rect);
		}
		else
		{
			gc.setBackground(BACKGROUND_COLOR);
			gc.fillRectangle(rect);
		}
		gc.drawRectangle(rect);
		
		Point ext = gc.textExtent(label);
		gc.drawText(label, x + (PORT_WIDTH - ext.x) / 2, y + (PORT_HEIGHT - ext.y) / 2);
	}

	/* (non-Javadoc)
	 * @see org.eclipse.swt.widgets.Composite#computeSize(int, int, boolean)
	 */
	@Override
	public Point computeSize(int wHint, int hHint, boolean changed)
	{
		return new Point(((ports.size() + rowCount - 1) / rowCount) * (40 + HORIZONTAL_SPACING) + HORIZONTAL_MARGIN * 2 + nameSize.x,
				rowCount * 30 + (rowCount - 1) * VERTICAL_SPACING + VERTICAL_MARGIN * 2);
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
	 * Clear port highlight
	 */
	void clearHighlight()
	{
		for(PortInfo pi : ports)
			pi.setHighlighted(false);
	}
	
	/**
	 * Add port highlight
	 * 
	 * @param p
	 */
	void addHighlight(Port p)
	{
		for(PortInfo pi : ports)
			if (pi.getPort() == p.getPort())
				pi.setHighlighted(true);
	}
}
