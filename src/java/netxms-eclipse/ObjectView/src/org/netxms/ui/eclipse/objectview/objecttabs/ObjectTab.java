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
package org.netxms.ui.eclipse.objectview.objecttabs;

import org.eclipse.core.runtime.IConfigurationElement;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CTabFolder;
import org.eclipse.swt.custom.CTabItem;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.ui.plugin.AbstractUIPlugin;
import org.netxms.client.objects.GenericObject;

/**
 * Abstract object tab class
 *
 */
public abstract class ObjectTab
{
	private ViewPart viewPart;
	private CTabFolder tabFolder;
	private CTabItem tabItem;
	private Composite clientArea;
	private GenericObject object;
	private String name;
	private int order;
	private ImageDescriptor icon;

	/**
	 * Read configuration before widget creation
	 * 
	 * @param ce Eclipse registry configuration element
	 */
	public void configure(IConfigurationElement ce, ViewPart viewPart)
	{
		this.viewPart = viewPart;
		
		name = ce.getAttribute("name");
		if (name == null)
			name = "<noname>";
		
		try
		{
			order = Integer.parseInt(ce.getAttribute("order"), 0);
		}
		catch(NumberFormatException e)
		{
			order = Integer.MAX_VALUE;
		}
		
		String path = ce.getAttribute("icon");
		if (path != null)
		{
			icon = AbstractUIPlugin.imageDescriptorFromPlugin(ce.getContributor().getName(), path);
		}
	}
	
	/**
	 * Create object tab. Intended to be called only by TabbedObjectView.
	 * 
	 * @param tabFolder Parent tab folder
	 * @param ce Configuration element
	 */
	public void create(CTabFolder tabFolder)
	{
		this.tabFolder = tabFolder;
		clientArea = new Composite(tabFolder, SWT.NONE);
		createTabContent(clientArea);
		clientArea.setVisible(false);
	}
	
	/**
	 * Create tab's content.
	 * 
	 * @param parent Parent composite
	 */
	protected abstract void createTabContent(Composite parent);
	
	/**
	 * Test if tab should be shown for given NetXMS object. Default implementation always returns true.
	 * 
	 * @param object Object to test
	 * @return Should return true if tab must be shown for given object
	 */
	public boolean showForObject(final GenericObject object)
	{
		return true;
	}
	
	/**
	 * Called by parent view to inform tab that currently selected object was changed.
	 * 
	 * @param object New object to display
	 */
	public abstract void objectChanged(GenericObject object);
	
	/**
	 * Change current object. Intended to be called only by parent view.
	 * 
	 * @param object New object to display
	 */
	public void changeObject(GenericObject object)
	{
		this.object = object;
		objectChanged(object);
	}
	
	/**
	 * Show tab
	 */
	public void show()
	{
		if (tabItem == null)
		{
			tabItem = new CTabItem(tabFolder, SWT.NONE);
			tabItem.setText(name);
			if (icon != null)
				tabItem.setImage(icon.createImage());
			tabItem.setControl(clientArea);
			clientArea.setVisible(true);
		}
	}
	
	/**
	 * Hide tab
	 */
	public void hide()
	{
		if (tabItem != null)
		{
			tabItem.setControl(null);
			tabItem.dispose();
			tabItem = null;
			clientArea.setVisible(false);
		}
	}

	/**
	 * Get currently selected object.
	 * 
	 * @return Currently selected object
	 */
	public GenericObject getObject()
	{
		return object;
	}

	/**
	 * @return the order
	 */
	public int getOrder()
	{
		return order;
	}

	/**
	 * @return the viewPart
	 */
	public ViewPart getViewPart()
	{
		return viewPart;
	}
	
	/**
	 * Disposes the tab.
	 * 
	 * This is the last method called on the ObjectTab. There is no guarantee that createTabContent() has been called,
	 * so the tab controls may never have been created.
	 * 
	 * Within this method a part may release any resources, fonts, images, etc.  held by this part. It is also very
	 * important to deregister all listeners from the workbench.
	 * 
	 * Clients should not call this method (the workbench calls this method at appropriate times). 
	 */
	public void dispose()
	{
	}
}
