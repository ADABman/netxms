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
package org.netxms.ui.eclipse.dashboard.propertypages;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.jface.viewers.ArrayContentProvider;
import org.eclipse.jface.viewers.DoubleClickEvent;
import org.eclipse.jface.viewers.IDoubleClickListener;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.layout.RowLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Event;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.dialogs.PropertyPage;
import org.netxms.client.NXCObjectModificationData;
import org.netxms.client.NXCSession;
import org.netxms.client.dashboards.DashboardElement;
import org.netxms.client.objects.Dashboard;
import org.netxms.ui.eclipse.dashboard.Activator;
import org.netxms.ui.eclipse.dashboard.dialogs.AddDashboardElementDlg;
import org.netxms.ui.eclipse.dashboard.dialogs.EditDashboardElementDlg;
import org.netxms.ui.eclipse.dashboard.propertypages.helpers.DashboardElementsLabelProvider;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.tools.WidgetHelper;
import org.netxms.ui.eclipse.widgets.LabeledText;
import org.netxms.ui.eclipse.widgets.SortableTableViewer;

/**
 * "Dashboard elements" property page for dashboard objects
 *
 */
public class DashboardElements extends PropertyPage
{
	public static final int COLUMN_TYPE = 0;
	public static final int COLUMN_SPAN = 1;
	public static final int COLUMN_ALIGNMENT = 2;
	
	private Dashboard object;
	private LabeledText columnCount;
	private SortableTableViewer viewer;
	private Button addButton;
	private Button editButton;
	private Button deleteButton;
	private Button upButton;
	private Button downButton;
	private List<DashboardElement> elements;
	
	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#createContents(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createContents(Composite parent)
	{
		Composite dialogArea = new Composite(parent, SWT.NONE);
		
		object = (Dashboard)getElement().getAdapter(Dashboard.class);
		if (object == null)	// Paranoid check
			return dialogArea;

		GridLayout layout = new GridLayout();
		layout.verticalSpacing = WidgetHelper.DIALOG_SPACING;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		layout.numColumns = 2;
      dialogArea.setLayout(layout);
      
      columnCount = new LabeledText(dialogArea, SWT.NONE);
      columnCount.setLabel("Number of columns");
      columnCount.setText(Integer.toString(object.getNumColumns()));
      GridData gridData = new GridData();
      gridData.horizontalAlignment = GridData.FILL;
      gridData.grabExcessHorizontalSpace = true;
      gridData.horizontalSpan = 2;
      columnCount.setLayoutData(gridData);
      
      final String[] columnNames = { "Type", "Span", "Alignment" };
      final int[] columnWidths = { 150, 60, 150 };
      viewer = new SortableTableViewer(dialogArea, columnNames, columnWidths, 0, SWT.UP,
                                       SWT.BORDER | SWT.MULTI | SWT.FULL_SELECTION);
      viewer.setContentProvider(new ArrayContentProvider());
      viewer.setLabelProvider(new DashboardElementsLabelProvider());
      
      elements = copyElements(object.getElements());
      viewer.setInput(elements.toArray());
      
      gridData = new GridData();
      gridData.verticalAlignment = GridData.FILL;
      gridData.grabExcessVerticalSpace = true;
      gridData.horizontalAlignment = GridData.FILL;
      gridData.grabExcessHorizontalSpace = true;
      gridData.heightHint = 0;
      gridData.horizontalSpan = 2;
      viewer.getControl().setLayoutData(gridData);
      
      Composite leftButtons = new Composite(dialogArea, SWT.NONE);
      RowLayout buttonLayout = new RowLayout();
      buttonLayout.type = SWT.HORIZONTAL;
      buttonLayout.pack = false;
      buttonLayout.marginWidth = 0;
      buttonLayout.marginLeft = 0;
      leftButtons.setLayout(buttonLayout);
      gridData = new GridData();
      gridData.horizontalAlignment = SWT.LEFT;
      leftButtons.setLayoutData(gridData);

      upButton = new Button(leftButtons, SWT.PUSH);
      upButton.setText("&Up");
      upButton.setEnabled(false);
      upButton.addSelectionListener(new SelectionListener() {
		@Override
			public void widgetDefaultSelected(SelectionEvent e)
			{
				widgetSelected(e);
			}

			@Override
			public void widgetSelected(SelectionEvent e)
			{
				moveUp();
			}
      });
      
      downButton = new Button(leftButtons, SWT.PUSH);
      downButton.setText("&Down");
      downButton.setEnabled(false);
      downButton.addSelectionListener(new SelectionListener() {
   		@Override
   			public void widgetDefaultSelected(SelectionEvent e)
   			{
   				widgetSelected(e);
   			}

   			@Override
   			public void widgetSelected(SelectionEvent e)
   			{
   				moveDown();
   			}
         });

      Composite rightButtons = new Composite(dialogArea, SWT.NONE);
      buttonLayout = new RowLayout();
      buttonLayout.type = SWT.HORIZONTAL;
      buttonLayout.pack = false;
      buttonLayout.marginWidth = 0;
      buttonLayout.marginRight = 0;
      rightButtons.setLayout(buttonLayout);
      gridData = new GridData();
      gridData.horizontalAlignment = SWT.RIGHT;
      rightButtons.setLayoutData(gridData);

      addButton = new Button(rightButtons, SWT.PUSH);
      addButton.setText("&Add...");
      addButton.addSelectionListener(new SelectionListener() {
			@Override
			public void widgetSelected(SelectionEvent e)
			{
				addNewElement();
			}
			
			@Override
			public void widgetDefaultSelected(SelectionEvent e)
			{
				widgetSelected(e);
			}
		});

      editButton = new Button(rightButtons, SWT.PUSH);
      editButton.setText("&Modify...");
      editButton.addSelectionListener(new SelectionListener() {
			@Override
			public void widgetSelected(SelectionEvent e)
			{
				editElement();
			}
			
			@Override
			public void widgetDefaultSelected(SelectionEvent e)
			{
				widgetSelected(e);
			}
		});

      deleteButton = new Button(rightButtons, SWT.PUSH);
      deleteButton.setText("&Delete");
      deleteButton.addSelectionListener(new SelectionListener() {
			@Override
			public void widgetSelected(SelectionEvent e)
			{
				deleteElements();
			}
			
			@Override
			public void widgetDefaultSelected(SelectionEvent e)
			{
				widgetSelected(e);
			}
		});

      viewer.addSelectionChangedListener(new ISelectionChangedListener() {
			@Override
			public void selectionChanged(SelectionChangedEvent event)
			{
				IStructuredSelection selection = (IStructuredSelection)event.getSelection();
				int index = elements.indexOf(selection.getFirstElement());
				upButton.setEnabled((selection.size() == 1) && (index > 0));
				downButton.setEnabled((selection.size() == 1) && (index >= 0) && (index < elements.size() - 1));
				editButton.setEnabled(selection.size() == 1);
				deleteButton.setEnabled(selection.size() > 0);
			}
      });
      
      viewer.addDoubleClickListener(new IDoubleClickListener() {
			@Override
			public void doubleClick(DoubleClickEvent event)
			{
				editButton.notifyListeners(SWT.Selection, new Event());
			}
      });
      
		return dialogArea;
	}

	/**
	 * @param src
	 * @return
	 */
	private static List<DashboardElement> copyElements(List<DashboardElement> src)
	{
		List<DashboardElement> dst = new ArrayList<DashboardElement>(src.size());
		for(DashboardElement e : src)
			dst.add(new DashboardElement(e));
		return dst;
	}
	
	/**
	 * Apply changes
	 * 
	 * @param isApply true if update operation caused by "Apply" button
	 */
	private boolean applyChanges(final boolean isApply)
	{
		int numColumns;
		try
		{
			numColumns = Integer.parseInt(columnCount.getText());
			if ((numColumns < 1) || (numColumns > 8))
				throw new NumberFormatException();
		}
		catch(NumberFormatException e)
		{
			MessageDialog.openError(getShell(), "Error", "Please enter valid integer in range 1 .. 8 as number of columns");
			return false;
		}
		
		final NXCObjectModificationData md = new NXCObjectModificationData(object.getObjectId());
		md.setDashboardElements(elements);
		md.setColumnCount(numColumns);
		
		if (isApply)
			setValid(false);
		
		new ConsoleJob("Update dashboard configuration", null, Activator.PLUGIN_ID, null) {
			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				((NXCSession)ConsoleSharedData.getSession()).modifyObject(md);
			}

			@Override
			protected String getErrorMessage()
			{
				return "Cannot update dashboard configuration";
			}

			/* (non-Javadoc)
			 * @see org.netxms.ui.eclipse.jobs.ConsoleJob#jobFinalize()
			 */
			@Override
			protected void jobFinalize()
			{
				if (isApply)
				{
					PlatformUI.getWorkbench().getDisplay().asyncExec(new Runnable() {
						@Override
						public void run()
						{
							DashboardElements.this.setValid(true);
						}
					});
				}
			}	
		}.start();
		
		return true;
	}

	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#performApply()
	 */
	@Override
	protected void performApply()
	{
		applyChanges(true);
	}

	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#performOk()
	 */
	@Override
	public boolean performOk()
	{
		return applyChanges(false);
	}
	
	/**
	 * Add new dashboard element
	 */
	private void addNewElement()
	{
		AddDashboardElementDlg dlg = new AddDashboardElementDlg(getShell());
		if (dlg.open() == Window.OK)
		{
			DashboardElement element = new DashboardElement(dlg.getElementType(), "<element>\n</element>\n");
			elements.add(element);
			viewer.setInput(elements.toArray());
			viewer.setSelection(new StructuredSelection(element));
		}
	}
	
	private void editElement()
	{
		IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		if (selection.size() != 1)
			return;
		
		DashboardElement element = (DashboardElement)selection.getFirstElement();
		EditDashboardElementDlg dlg = new EditDashboardElementDlg(getShell(), element);
		if (dlg.open() == Window.OK)
		{
			viewer.update(element, null);
		}
	}
	
	private void deleteElements()
	{
		IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		for(Object o : selection.toList())
		{
			elements.remove(o);
		}
		viewer.setInput(elements.toArray());
	}

	/**
	 * Move currently selected element up
	 */
	private void moveUp()
	{
		final IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		if (selection.size() == 1)
		{
			DashboardElement element = (DashboardElement)selection.getFirstElement();
			
			int index = elements.indexOf(element);
			if (index > 0)
			{
				Collections.swap(elements, index - 1, index);
		      viewer.setInput(elements.toArray());
		      viewer.setSelection(new StructuredSelection(element));
			}
		}
	}
	
	/**
	 * Move currently selected element down
	 */
	private void moveDown()
	{
		final IStructuredSelection selection = (IStructuredSelection)viewer.getSelection();
		if (selection.size() == 1)
		{
			DashboardElement element = (DashboardElement)selection.getFirstElement();
			
			int index = elements.indexOf(element);
			if ((index < elements.size() - 1) && (index >= 0))
			{
				Collections.swap(elements, index + 1, index);
		      viewer.setInput(elements.toArray());
		      viewer.setSelection(new StructuredSelection(element));
			}
		}
	}
}
