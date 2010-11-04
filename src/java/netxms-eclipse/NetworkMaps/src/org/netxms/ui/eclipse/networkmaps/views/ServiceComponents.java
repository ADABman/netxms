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

import java.util.Comparator;
import java.util.Iterator;

import org.eclipse.swt.widgets.Composite;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.PartInitException;
import org.eclipse.zest.layouts.LayoutAlgorithm;
import org.eclipse.zest.layouts.LayoutStyles;
import org.eclipse.zest.layouts.algorithms.CompositeLayoutAlgorithm;
import org.eclipse.zest.layouts.algorithms.TreeLayoutAlgorithm;
import org.netxms.client.maps.NetworkMapObjectData;
import org.netxms.client.maps.NetworkMapObjectLink;
import org.netxms.client.maps.NetworkMapPage;
import org.netxms.client.objects.Cluster;
import org.netxms.client.objects.Condition;
import org.netxms.client.objects.Container;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.objects.Node;
import org.netxms.ui.eclipse.networkmaps.algorithms.SparseTree;

/**
 * Service dependency for service object
 *
 */
public class ServiceComponents extends NetworkMap
{
	public static final String ID = "org.netxms.ui.eclipse.networkmaps.views.ServiceComponents";

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.ViewPart#init(org.eclipse.ui.IViewSite)
	 */
	@Override
	public void init(IViewSite site) throws PartInitException
	{
		super.init(site);
		setPartName("Service Components - " + ((rootObject != null) ? rootObject.getObjectName() : "<error>"));
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.networkmaps.views.NetworkMap#buildMapPage()
	 */
	@Override
	protected void buildMapPage()
	{
		mapPage = new NetworkMapPage();
		mapPage.addObject(new NetworkMapObjectData(rootObject.getObjectId()));
		addServiceComponents(rootObject);
	}

	/**
	 * Add parent services for given object
	 * 
	 * @param object
	 */
	private void addServiceComponents(GenericObject object)
	{
		Iterator<Long> it = object.getChilds();
		while(it.hasNext())
		{
			long id = it.next();
			GenericObject child = session.findObjectById(id);
			if ((child != null) && 
					((child instanceof Container) || 
					 (child instanceof Cluster) || 
					 (child instanceof Node) ||
					 (child instanceof Condition)))
			{
				mapPage.addObject(new NetworkMapObjectData(id));
				mapPage.addLink(new NetworkMapObjectLink(NetworkMapObjectLink.NORMAL, object.getObjectId(), id));
				addServiceComponents(child);
			}
		}
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.networkmaps.views.NetworkMap#createPartControl(org.eclipse.swt.widgets.Composite)
	 */
	@SuppressWarnings("rawtypes")
	@Override
	public void createPartControl(Composite parent)
	{
		super.createPartControl(parent);

		TreeLayoutAlgorithm mainLayoutAlgorithm = new TreeLayoutAlgorithm(LayoutStyles.NO_LAYOUT_NODE_RESIZING);
		mainLayoutAlgorithm.setComparator(new Comparator() {
			@Override
			public int compare(Object arg0, Object arg1)
			{
				return arg0.toString().compareToIgnoreCase(arg1.toString());
			}
		});
		viewer.setLayoutAlgorithm(new CompositeLayoutAlgorithm(LayoutStyles.NO_LAYOUT_NODE_RESIZING, 
				new LayoutAlgorithm[] { mainLayoutAlgorithm,
				                        new SparseTree(LayoutStyles.NO_LAYOUT_NODE_RESIZING) }));
	}
}
