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
package org.netxms.ui.eclipse.topology.views;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.GroupMarker;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.ArrayContentProvider;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.ui.IActionBars;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.IWorkbenchActionConstants;
import org.eclipse.ui.IWorkbenchPage;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.part.ViewPart;
import org.netxms.client.NXCSession;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.topology.VlanInfo;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.topology.views.helpers.VlanLabelProvider;
import org.netxms.ui.eclipse.topology.widgets.DeviceView;
import org.netxms.ui.eclipse.widgets.SortableTableViewer;

/**
 * Display VLAN configuration on a node
 */
public class VlanView extends ViewPart implements ISelectionChangedListener
{
	public static final String ID = "org.netxms.ui.eclipse.topology.views.VlanView";
	
	public static final int COLUMN_VLAN_ID = 0;
	public static final int COLUMN_NAME = 1;
	public static final int COLUMN_PORTS = 2;
	
	private long nodeId;
	private List<VlanInfo> vlans = new ArrayList<VlanInfo>(0);
	private NXCSession session;
	private SortableTableViewer vlanList;
	private DeviceView deviceView;
	
	private Action actionShowVlanMap;
	
	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.ViewPart#init(org.eclipse.ui.IViewSite)
	 */
	@Override
	public void init(IViewSite site) throws PartInitException
	{
		super.init(site);
		nodeId = Long.parseLong(site.getSecondaryId());
		session = (NXCSession)ConsoleSharedData.getSession();
		GenericObject object = session.findObjectById(nodeId);
		setPartName("VLAN View - " + ((object != null) ? object.getObjectName() : "<" + site.getSecondaryId() + ">"));
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#createPartControl(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	public void createPartControl(Composite parent)
	{
		GridLayout layout = new GridLayout();
		layout.marginHeight = 0;
		layout.marginWidth = 0;
		parent.setLayout(layout);
		
		final String[] names = { "ID", "Name", "Ports" };
		final int[] widths = { 80, 180, 400 };
		vlanList = new SortableTableViewer(parent, names, widths, 0, SWT.DOWN, SWT.FULL_SELECTION | SWT.MULTI);
		vlanList.setContentProvider(new ArrayContentProvider());
		vlanList.setLabelProvider(new VlanLabelProvider());
		GridData gd = new GridData();
		gd.grabExcessHorizontalSpace = true;
		gd.horizontalAlignment = SWT.FILL;
		gd.verticalAlignment = SWT.FILL;
		gd.grabExcessVerticalSpace = true;
		vlanList.getTable().setLayoutData(gd);
		vlanList.setInput(vlans.toArray());
		vlanList.addSelectionChangedListener(this);
		
		deviceView = new DeviceView(parent, SWT.NONE);
		gd = new GridData();
		gd.grabExcessHorizontalSpace = true;
		gd.horizontalAlignment = SWT.FILL;
		deviceView.setLayoutData(gd);
		deviceView.setPortStatusVisible(false);
		deviceView.setNodeId(nodeId);

		createActions();
		contributeToActionBars();
		createPopupMenu();
	}

	/**
	 * Create actions
	 */
	private void createActions()
	{
		actionShowVlanMap = new Action("Show VLAN map") {
			@Override
			public void run()
			{
				showVlanMap();
			}
		};
	}

	/**
	 * Contribute actions to action bars
	 */
	private void contributeToActionBars()
	{
		IActionBars bars = getViewSite().getActionBars();
		fillLocalPullDown(bars.getMenuManager());
		fillLocalToolBar(bars.getToolBarManager());
	}

	/**
	 * Fill local pulldown menu
	 * @param manager menu manager
	 */
	private void fillLocalPullDown(IMenuManager manager)
	{
		manager.add(actionShowVlanMap);
	}

	/**
	 * Fill local toolbar
	 * @param manager menu manager
	 */
	private void fillLocalToolBar(IToolBarManager manager)
	{
	}

	/**
	 * Create pop-up menu
	 */
	private void createPopupMenu()
	{
		// Create menu manager.
		MenuManager menuMgr = new MenuManager();
		menuMgr.setRemoveAllWhenShown(true);
		menuMgr.addMenuListener(new IMenuListener()
		{
			public void menuAboutToShow(IMenuManager mgr)
			{
				fillContextMenu(mgr);
			}
		});

		// Create menu.
		Menu menu = menuMgr.createContextMenu(vlanList.getControl());
		vlanList.getControl().setMenu(menu);

		// Register menu for extension.
		getSite().registerContextMenu(menuMgr, vlanList);
	}

	/**
	 * Fill context menu
	 * @param mgr Menu manager
	 */
	protected void fillContextMenu(IMenuManager manager)
	{
		manager.add(actionShowVlanMap);
		manager.add(new Separator());
		manager.add(new GroupMarker(IWorkbenchActionConstants.MB_ADDITIONS));
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#setFocus()
	 */
	@Override
	public void setFocus()
	{
		vlanList.getControl().setFocus();
	}

	/**
	 * @param vlans the vlans to set
	 */
	public void setVlans(List<VlanInfo> vlans)
	{
		this.vlans = vlans;
		vlanList.setInput(vlans.toArray());
	}

	/* (non-Javadoc)
	 * @see org.eclipse.jface.viewers.ISelectionChangedListener#selectionChanged(org.eclipse.jface.viewers.SelectionChangedEvent)
	 */
	@Override
	public void selectionChanged(SelectionChangedEvent event)
	{
		IStructuredSelection selection = (IStructuredSelection)vlanList.getSelection();
		VlanInfo vlan = (VlanInfo)selection.getFirstElement();
		if (vlan != null)
		{
			deviceView.setHighlight(vlan.getPorts());
		}
		else
		{
			deviceView.clearHighlight(true);
		}
	}
	
	/**
	 * Show map for currently selected VLAN
	 */
	private void showVlanMap()
	{
		IStructuredSelection selection = (IStructuredSelection)vlanList.getSelection();
		IWorkbenchPage page = getSite().getWorkbenchWindow().getActivePage();
		for(final Object o : selection.toList())
		{
			final VlanInfo vlan = (VlanInfo)o;
			try
			{
				page.showView("org.netxms.ui.eclipse.networkmaps.views.VlanMap", Long.toString(nodeId) + "&" + Integer.toString(vlan.getVlanId()), IWorkbenchPage.VIEW_ACTIVATE);
			}
			catch(PartInitException e)
			{
				MessageDialog.openError(getSite().getShell(), "Error", "Cannot open VLAN map view for VLAN " + vlan.getVlanId() + ": " + e.getLocalizedMessage());
			}
		}
	}
}
