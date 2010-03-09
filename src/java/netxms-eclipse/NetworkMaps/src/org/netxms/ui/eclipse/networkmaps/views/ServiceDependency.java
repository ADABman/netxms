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
package org.netxms.ui.eclipse.networkmaps.views;

import java.util.Iterator;

import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.zest.layouts.LayoutStyles;
import org.eclipse.zest.layouts.algorithms.TreeLayoutAlgorithm;
import org.netxms.client.maps.NetworkMapObjectData;
import org.netxms.client.maps.NetworkMapObjectLink;
import org.netxms.client.maps.NetworkMapPage;
import org.netxms.client.objects.Cluster;
import org.netxms.client.objects.Container;
import org.netxms.client.objects.GenericObject;

/**
 * Service dependency for service, cluster, condition, or node object
 *
 */
public class ServiceDependency extends NetworkMap
{
	public static final String ID = "org.netxms.ui.eclipse.networkmaps.view.service_dependency";

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.ViewPart#init(org.eclipse.ui.IViewSite)
	 */
	@Override
	public void init(IViewSite site) throws PartInitException
	{
		super.init(site);
		setPartName("Service Dependency - " + ((rootObject != null) ? rootObject.getObjectName() : "<error>"));
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.networkmaps.views.NetworkMap#buildMapPage()
	 */
	@Override
	protected void buildMapPage()
	{
		mapPage = new NetworkMapPage();

		mapPage.addObject(new NetworkMapObjectData(rootObject.getObjectId()));
		addParentServices(rootObject);
	}

	/**
	 * Add parent services for given object
	 * 
	 * @param object
	 */
	private void addParentServices(GenericObject object)
	{
		Iterator<Long> it = object.getParents();
		while(it.hasNext())
		{
			long id = it.next();
			GenericObject parent = session.findObjectById(id);
			if ((parent != null) && ((parent instanceof Container) || (parent instanceof Cluster)))
			{
				mapPage.addObject(new NetworkMapObjectData(id));
				mapPage.addLink(new NetworkMapObjectLink(NetworkMapObjectLink.NORMAL, object.getObjectId(), id));
				addParentServices(parent);
			}
		}
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.networkmaps.views.NetworkMap#createPartControl(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	public void createPartControl(Composite parent)
	{
		super.createPartControl(parent);
		viewer.setLayoutAlgorithm(new TreeLayoutAlgorithm(LayoutStyles.NO_LAYOUT_NODE_RESIZING));
	}
}
