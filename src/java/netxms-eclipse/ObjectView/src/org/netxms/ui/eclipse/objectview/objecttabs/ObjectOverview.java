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
package org.netxms.ui.eclipse.objectview.objecttabs;

import java.util.HashSet;
import java.util.Set;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Layout;
import org.netxms.client.objects.GenericObject;
import org.netxms.ui.eclipse.objectview.objecttabs.elements.Capabilities;
import org.netxms.ui.eclipse.objectview.objecttabs.elements.Commands;
import org.netxms.ui.eclipse.objectview.objecttabs.elements.Comments;
import org.netxms.ui.eclipse.objectview.objecttabs.elements.Connection;
import org.netxms.ui.eclipse.objectview.objecttabs.elements.GeneralInfo;
import org.netxms.ui.eclipse.objectview.objecttabs.elements.OverviewPageElement;

/**
 * Object overview tab
 *
 */
public class ObjectOverview extends ObjectTab
{
	private static final Color BACKGROUND_COLOR = new Color(Display.getDefault(), 255, 255, 255);
	
	private Set<OverviewPageElement> elements = new HashSet<OverviewPageElement>();
	private Composite viewArea;
	private Composite leftColumn;
	private Composite rightColumn;
	
	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#createTabContent(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected void createTabContent(Composite parent)
	{
		viewArea = new Composite(parent, SWT.NONE);
		viewArea.setBackground(BACKGROUND_COLOR);
		
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		viewArea.setLayout(layout);
		
		leftColumn = new Composite(viewArea, SWT.NONE);
		leftColumn.setLayout(createColumnLayout());
		leftColumn.setBackground(BACKGROUND_COLOR);
		GridData gd = new GridData();
		gd.verticalAlignment = SWT.TOP;
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		leftColumn.setLayoutData(gd);
		
		rightColumn = new Composite(viewArea, SWT.NONE);
		rightColumn.setLayout(createColumnLayout());
		rightColumn.setBackground(BACKGROUND_COLOR);
		gd = new GridData();
		gd.verticalAlignment = SWT.TOP;
		gd.horizontalAlignment = SWT.LEFT;
		gd.grabExcessHorizontalSpace = true;
		gd.minimumWidth = SWT.DEFAULT;
		rightColumn.setLayoutData(gd);

		addElement(new GeneralInfo(leftColumn, getObject()));
		addElement(new Commands(leftColumn, getObject()));
		addElement(new Comments(leftColumn, getObject()));
		addElement(new Capabilities(rightColumn, getObject()));
		addElement(new Connection(rightColumn, getObject()));
	}
	
	/**
	 * Create layout for column
	 * 
	 * @return
	 */
	private Layout createColumnLayout()
	{
		GridLayout layout = new GridLayout();
		layout.marginHeight = 0;
		layout.marginWidth = 0;
		layout.verticalSpacing = 5;
		return layout;
	}
	
	/**
	 * Add page element
	 * 
	 * @param element
	 * @param span
	 * @param grabExcessSpace
	 */
	private void addElement(OverviewPageElement element)
	{
		GridData gd = new GridData();
		gd.exclude = false;
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		element.setLayoutData(gd);
		elements.add(element);
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#objectChanged(org.netxms.client.objects.GenericObject)
	 */
	@Override
	public void objectChanged(GenericObject object)
	{
		viewArea.setRedraw(false);
		for(OverviewPageElement element : elements)
		{
			element.setVisible(element.isApplicableForObject(object));
			((GridData)element.getLayoutData()).exclude = !element.isVisible();
			element.setObject(object);
		}
		viewArea.layout(true, true);
		viewArea.setRedraw(true);
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#dispose()
	 */
	@Override
	public void dispose()
	{
		super.dispose();
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#selected()
	 */
	@Override
	public void selected()
	{
		super.selected();
		// I don't know why, but content lay out incorrectly if object selection
		// changes while this tab is not active.
		// As workaround, we force reconstruction of the content on each tab activation
		objectChanged(getObject());
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#refresh()
	 */
	@Override
	public void refresh()
	{
		objectChanged(getObject());
	}
}
