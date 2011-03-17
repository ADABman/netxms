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
package org.netxms.ui.eclipse.datacollection.views;

import java.util.Iterator;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.core.runtime.jobs.Job;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.GroupMarker;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.ArrayContentProvider;
import org.eclipse.jface.viewers.DoubleClickEvent;
import org.eclipse.jface.viewers.IDoubleClickListener;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.ui.IActionBars;
import org.eclipse.ui.IViewSite;
import org.eclipse.ui.IWorkbenchActionConstants;
import org.eclipse.ui.PartInitException;
import org.eclipse.ui.dialogs.PropertyDialogAction;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.ui.progress.IWorkbenchSiteProgressService;
import org.eclipse.ui.progress.UIJob;
import org.netxms.client.NXCException;
import org.netxms.client.NXCSession;
import org.netxms.client.datacollection.DataCollectionConfiguration;
import org.netxms.client.datacollection.DataCollectionItem;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.objects.Node;
import org.netxms.client.objects.Template;
import org.netxms.ui.eclipse.actions.RefreshAction;
import org.netxms.ui.eclipse.datacollection.Activator;
import org.netxms.ui.eclipse.datacollection.DciComparator;
import org.netxms.ui.eclipse.datacollection.DciLabelProvider;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.tools.WidgetHelper;
import org.netxms.ui.eclipse.widgets.SortableTableViewer;

/**
 * Data collection configuration view
 * 
 */
public class DataCollectionEditor extends ViewPart
{
	public static final String ID = "org.netxms.ui.eclipse.datacollection.view.data_collection_editor";
	public static final String JOB_FAMILY = "DataCollectionEditorJob";

	// Columns
	public static final int COLUMN_ID = 0;
	public static final int COLUMN_ORIGIN = 1;
	public static final int COLUMN_DESCRIPTION = 2;
	public static final int COLUMN_PARAMETER = 3;
	public static final int COLUMN_DATATYPE = 4;
	public static final int COLUMN_INTERVAL = 5;
	public static final int COLUMN_RETENTION = 6;
	public static final int COLUMN_STATUS = 7;
	public static final int COLUMN_TEMPLATE = 8;

	private SortableTableViewer viewer;
	private NXCSession session;
	private GenericObject object;
	private DataCollectionConfiguration dciConfig = null;
	private Action actionCreate;
	private Action actionEdit;
	private Action actionDelete;
	private Action actionCopy;
	private Action actionMove;
	private Action actionConvert;
	private Action actionDuplicate;
	private Action actionActivate;
	private Action actionDisable;
	private RefreshAction actionRefresh;

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.ViewPart#init(org.eclipse.ui.IViewSite)
	 */
	@Override
	public void init(IViewSite site) throws PartInitException
	{
		super.init(site);
		
		session = (NXCSession)ConsoleSharedData.getSession();
		GenericObject obj = session.findObjectById(Long.parseLong(site.getSecondaryId()));
		object = ((obj != null) && ((obj instanceof Node) || (obj instanceof Template))) ? obj : null;
		setPartName("Data Collection Configuration - " + ((object != null) ? object.getObjectName() : "<error>"));
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#createPartControl(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	public void createPartControl(Composite parent)
	{
		final String[] names = { "ID", "Origin", "Description", "Parameter", "Data Type", "Polling Interval", "Retention Time", "Status", "Template" };
		final int[] widths = { 60, 100, 250, 200, 90, 90, 90, 100, 150 };
		viewer = new SortableTableViewer(parent, names, widths, 0, SWT.UP, SortableTableViewer.DEFAULT_STYLE);
		viewer.setContentProvider(new ArrayContentProvider());
		viewer.setLabelProvider(new DciLabelProvider());
		viewer.setComparator(new DciComparator((DciLabelProvider)viewer.getLabelProvider()));
		WidgetHelper.restoreTableViewerSettings(viewer, Activator.getDefault().getDialogSettings(), "DataCollectionEditor");
		
		viewer.addSelectionChangedListener(new ISelectionChangedListener()
		{
			@SuppressWarnings("unchecked")
			@Override
			public void selectionChanged(SelectionChangedEvent event)
			{
				IStructuredSelection selection = (IStructuredSelection)event.getSelection();
				if (selection != null)
				{
					actionEdit.setEnabled(selection.size() == 1);
					actionDelete.setEnabled(selection.size() > 0);
					actionCopy.setEnabled(selection.size() > 0);
					actionMove.setEnabled(selection.size() > 0);
					actionConvert.setEnabled(selection.size() > 0);
					actionDuplicate.setEnabled(selection.size() > 0);
					
					Iterator<DataCollectionItem> it = selection.iterator();
					boolean canActivate = false;
					boolean canDisable = false;
					while(it.hasNext() && (!canActivate || !canDisable))
					{
						DataCollectionItem dci = it.next();
						if (dci.getStatus() != DataCollectionItem.ACTIVE)
							canActivate = true;
						if (dci.getStatus() != DataCollectionItem.DISABLED)
							canDisable = true;
					}
					actionActivate.setEnabled(canActivate);
					actionDisable.setEnabled(canDisable);
				}
			}
		});
		viewer.addDoubleClickListener(new IDoubleClickListener() {
			@Override
			public void doubleClick(DoubleClickEvent event)
			{
				actionEdit.run();
			}
		});
		viewer.getTable().addDisposeListener(new DisposeListener() {
			@Override
			public void widgetDisposed(DisposeEvent e)
			{
				WidgetHelper.saveTableViewerSettings(viewer, Activator.getDefault().getDialogSettings(), "DataCollectionEditor");
			}
		});

		makeActions();
		contributeToActionBars();
		createPopupMenu();

		// Request server to open data collection configuration
		Job job = new Job("Open data collection configuration for " + object.getObjectName())
		{
			@Override
			protected IStatus run(IProgressMonitor monitor)
			{
				IStatus status;

				try
				{
					dciConfig = session.openDataCollectionConfiguration(object.getObjectId());
					dciConfig.setUserData(viewer);
					new UIJob("Update data collection configurator for " + object.getObjectName())
					{
						@Override
						public IStatus runInUIThread(IProgressMonitor monitor)
						{
							viewer.setInput(dciConfig.getItems());
							return Status.OK_STATUS;
						}
					}.schedule();
					status = Status.OK_STATUS;
				}
				catch(Exception e)
				{
					status = new Status(Status.ERROR, Activator.PLUGIN_ID, (e instanceof NXCException) ? ((NXCException) e)
							.getErrorCode() : 0, "Cannot open data collection configuration for " + object.getObjectName() + ": " + e.getMessage(), null);
					new UIJob("Close data collection configurator for " + object.getObjectName())
					{
						@Override
						public IStatus runInUIThread(IProgressMonitor monitor)
						{
							DataCollectionEditor.this.getViewSite().getPage().hideView(DataCollectionEditor.this);
							return Status.OK_STATUS;
						}
					}.schedule();
				}
				return status;
			}
		};
		scheduleJob(job);
	}
	
	/**
	 * Schedule job related to view
	 * 
	 * @param job Job to schedule
	 */
	private void scheduleJob(final Job job)
	{
		IWorkbenchSiteProgressService siteService = (IWorkbenchSiteProgressService) getSite().getAdapter(IWorkbenchSiteProgressService.class);
		siteService.schedule(job, 0, true);
	}

	/**
	 * Contribute actions to action bar
	 */
	private void contributeToActionBars()
	{
		IActionBars bars = getViewSite().getActionBars();
		fillLocalPullDown(bars.getMenuManager());
		fillLocalToolBar(bars.getToolBarManager());
	}

	/**
	 * Fill local pull-down menu
	 * 
	 * @param manager
	 *           Menu manager for pull-down menu
	 */
	private void fillLocalPullDown(IMenuManager manager)
	{
		manager.add(actionCreate);
		manager.add(actionEdit);
		manager.add(actionDelete);
		manager.add(actionCopy);
		manager.add(actionMove);
		manager.add(actionConvert);
		manager.add(actionDuplicate);
		manager.add(new Separator());
		manager.add(actionActivate);
		manager.add(actionDisable);
		manager.add(new Separator());
		manager.add(new GroupMarker(IWorkbenchActionConstants.MB_ADDITIONS));
		manager.add(new Separator());
		manager.add(actionRefresh);
	}

	/**
	 * Fill local tool bar
	 * 
	 * @param manager
	 *           Menu manager for local toolbar
	 */
	private void fillLocalToolBar(IToolBarManager manager)
	{
		manager.add(actionCreate);
		manager.add(actionRefresh);
	}

	/**
	 * Fill context menu
	 * 
	 * @param mgr Menu manager
	 */
	protected void fillContextMenu(final IMenuManager manager)
	{
		manager.add(actionCreate);
		manager.add(actionEdit);
		manager.add(actionDelete);
		manager.add(actionCopy);
		manager.add(actionMove);
		manager.add(actionConvert);
		manager.add(actionDuplicate);
		manager.add(new Separator());
		manager.add(actionActivate);
		manager.add(actionDisable);
		manager.add(new Separator());
		manager.add(new GroupMarker(IWorkbenchActionConstants.MB_ADDITIONS));
	}

	/**
	 * Create actions
	 */
	private void makeActions()
	{
		actionRefresh = new RefreshAction()
		{
			@Override
			public void run()
			{
				viewer.setInput(dciConfig.getItems());
			}
		};

		actionCreate = new Action()
		{
			@Override
			public void run()
			{
				createItem();
			}
		};
		actionCreate.setText("&New...");
		actionCreate.setImageDescriptor(Activator.getImageDescriptor("icons/new.png"));

		actionEdit = new PropertyDialogAction(getSite(), viewer);
		actionEdit.setText("&Edit...");
		actionEdit.setImageDescriptor(Activator.getImageDescriptor("icons/edit.png"));
		actionEdit.setEnabled(false);

		actionDelete = new Action()
		{
			/* (non-Javadoc)
			 * @see org.eclipse.jface.action.Action#run()
			 */
			@Override
			public void run()
			{
				deleteItems();
			}
		};
		actionDelete.setText("&Delete");
		actionDelete.setImageDescriptor(Activator.getImageDescriptor("icons/delete.png"));
		actionDelete.setEnabled(false);

		actionCopy = new Action()
		{
			@Override
			public void run()
			{
			}
		};
		actionCopy.setText("&Copy to other node(s)...");
		actionCopy.setEnabled(false);

		actionMove = new Action()
		{
			@Override
			public void run()
			{
			}
		};
		actionMove.setText("&Move to other node(s)...");
		actionMove.setEnabled(false);

		actionConvert = new Action()
		{
			@Override
			public void run()
			{
			}
		};
		actionConvert.setText("Convert to &template item...");
		actionConvert.setEnabled(false);

		actionDuplicate = new Action()
		{
			@Override
			public void run()
			{
				duplicateItems();
			}
		};
		actionDuplicate.setText("D&uplicate");
		actionDuplicate.setEnabled(false);

		actionActivate = new Action()
		{
			@Override
			public void run()
			{
				setItemStatus(DataCollectionItem.ACTIVE);
			}
		};
		actionActivate.setText("&Activate");
		actionActivate.setImageDescriptor(Activator.getImageDescriptor("icons/active.gif"));
		actionActivate.setEnabled(false);

		actionDisable = new Action()
		{
			@Override
			public void run()
			{
				setItemStatus(DataCollectionItem.DISABLED);
			}
		};
		actionDisable.setText("D&isable");
		actionDisable.setImageDescriptor(Activator.getImageDescriptor("icons/disabled.gif"));
		actionDisable.setEnabled(false);
	}

	/**
	 * Create pop-up menu for variable list
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
		Menu menu = menuMgr.createContextMenu(viewer.getControl());
		viewer.getControl().setMenu(menu);

		// Register menu for extension.
		getSite().registerContextMenu(menuMgr, viewer);
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#setFocus()
	 */
	@Override
	public void setFocus()
	{
		viewer.getControl().setFocus();
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#dispose()
	 */
	@Override
	public void dispose()
	{
		if (dciConfig != null)
		{
			new Job("Unlock data collection configuration for " + object.getObjectName())
			{
				@Override
				protected IStatus run(IProgressMonitor monitor)
				{
					IStatus status;

					try
					{
						dciConfig.close();
						dciConfig = null;
						status = Status.OK_STATUS;
					}
					catch(Exception e)
					{
						status = new Status(Status.ERROR, Activator.PLUGIN_ID,
								(e instanceof NXCException) ? ((NXCException) e).getErrorCode() : 0,
								"Cannot unlock data collection configuration for " + object.getObjectName() + ": " + e.getMessage(), null);
					}
					return status;
				}
			}.schedule();
		}
		super.dispose();
	}

	/**
	 * Change status for selected items
	 * 
	 * @param newStatus New status
	 */
	private void setItemStatus(final int newStatus)
	{
		final IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		if (selection.size() <= 0)
			return;
		
		Job job = new Job("Change status of data collection items for " + object.getObjectName())
		{
			@SuppressWarnings("unchecked")
			@Override
			protected IStatus run(IProgressMonitor monitor)
			{
				final long[] itemList = new long[selection.size()];
				Iterator<DataCollectionItem> it = selection.iterator();
				for(int pos = 0; it.hasNext(); pos++)
				{
					final DataCollectionItem dci = it.next();
					itemList[pos] = dci.getId();
				}
				IStatus status;
				try
				{
					dciConfig.setItemStatus(itemList, newStatus);
					new UIJob("Update DCI list for " + object.getObjectName())
					{
						@Override
						public IStatus runInUIThread(IProgressMonitor monitor)
						{
							Iterator<DataCollectionItem> it = selection.iterator();
							while(it.hasNext())
							{
								final DataCollectionItem dci = it.next();
								dci.setStatus(newStatus);
								viewer.update(dci, null);
							}
							return Status.OK_STATUS;
						}
					}.schedule();
					status = Status.OK_STATUS;
				}
				catch(Exception e)
				{
					status = new Status(Status.ERROR, Activator.PLUGIN_ID, 
		                    (e instanceof NXCException) ? ((NXCException)e).getErrorCode() : 0,
		                    "Cannot change status of data collection items for " + object.getObjectName() + ": " + e.getMessage(), null);
				}
				return status;
			}
		};
		scheduleJob(job);
	}

	/**
	 * Delete currently selected DCIs
	 */
	private void deleteItems()
	{
		final IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		if (selection.size() <= 0)
			return;
		
		if (!MessageDialog.openConfirm(getSite().getShell(), "Delete Data Collection Items",
		                               "Do you really want to delete selected data collection items?"))
			return;
		
		Job job = new Job("Delete data collection items for " + object.getObjectName()) {
			@SuppressWarnings("unchecked")
			@Override
			protected IStatus run(IProgressMonitor monitor)
			{
				IStatus status;
				try
				{
					Iterator<DataCollectionItem> it = selection.iterator();
					while(it.hasNext())
					{
						final DataCollectionItem dci = it.next();
						dciConfig.deleteItem(dci.getId());
					}
					status = Status.OK_STATUS;
				}
				catch(Exception e)
				{
					status = new Status(Status.ERROR, Activator.PLUGIN_ID, 
		                    (e instanceof NXCException) ? ((NXCException)e).getErrorCode() : 0,
		                    "Cannot delete data collection items for " + object.getObjectName() + ": " + e.getMessage(), null);
				}
				
				new UIJob("Update DCI list for " + object.getObjectName())
				{
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						viewer.setInput(dciConfig.getItems());
						return Status.OK_STATUS;
					}
				}.schedule();
				return status;
			}
		};
		scheduleJob(job);
	}
	
	/**
	 * Create new data collection item
	 */
	private void createItem()
	{
		new ConsoleJob("Create new data collection item for " + object.getObjectName(), this, Activator.PLUGIN_ID, null) {
			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				final long id = dciConfig.createItem();
				new UIJob("Open created DCI") {
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						viewer.setInput(dciConfig.getItems());
						DataCollectionItem dci = dciConfig.findItem(id);
						viewer.setSelection(new StructuredSelection(dci), true);
						actionEdit.run();
						return Status.OK_STATUS;
					}
				}.schedule();
			}

			@Override
			protected String getErrorMessage()
			{
				return "Cannot create new data collection item for " + object.getObjectName();
			}
		}.start();
	}
	
	/**
	 * Duplicate selected item(s)
	 */
	@SuppressWarnings("unchecked")
	private void duplicateItems()
	{
		IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		Iterator<DataCollectionItem> it = selection.iterator();
		final long[] dciList = new long[selection.size()];
		for(int i = 0; (i < dciList.length) && it.hasNext(); i++)
			dciList[i] = it.next().getId();
		
		new ConsoleJob("Duplicate data collection items for " + object.getObjectName(), this, Activator.PLUGIN_ID, null) {
			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				dciConfig.copyItems(dciConfig.getNodeId(), dciList);
				new UIJob("Update DCI list")
				{
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						viewer.setInput(dciConfig.getItems());
						return Status.OK_STATUS;
					}
				}.schedule();
			}

			@Override
			protected String getErrorMessage()
			{
				return "Cannot duplicate data collection item for " + object.getObjectName();
			}
		}.start();
	}
}
