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
package org.netxms.ui.eclipse.objectmanager.propertypages;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.core.runtime.jobs.Job;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.events.SelectionListener;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Label;
import org.eclipse.ui.dialogs.PropertyPage;
import org.eclipse.ui.progress.UIJob;
import org.netxms.client.NXCException;
import org.netxms.client.NXCObjectModificationData;
import org.netxms.client.NXCSession;
import org.netxms.client.objects.Template;
import org.netxms.ui.eclipse.nxsl.widgets.ScriptEditor;
import org.netxms.ui.eclipse.objectmanager.Activator;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.tools.WidgetHelper;

/**
 * "Auto apply" property page for template object
 *
 */
public class AutoApply extends PropertyPage
{
	private Template object;
	private Button checkboxEnable;
	private ScriptEditor filterSource;
	private boolean initialApplyFlag;
	private String initialApplyFilter;

	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#createContents(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createContents(Composite parent)
	{
		Composite dialogArea = new Composite(parent, SWT.NONE);
		
		object = (Template)getElement().getAdapter(Template.class);
		if (object == null)	// Paranoid check
			return dialogArea;
		
		initialApplyFlag = object.isAutoApplyEnabled();
		initialApplyFilter = new String(object.getAutoApplyFilter());
		
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = WidgetHelper.OUTER_SPACING;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
      dialogArea.setLayout(layout);

      // Enable/disable check box
      checkboxEnable = new Button(dialogArea, SWT.CHECK);
      checkboxEnable.setText("Apply this template automatically to nodes selected by filter");
      checkboxEnable.setSelection(object.isAutoApplyEnabled());
      checkboxEnable.addSelectionListener(new SelectionListener() {
			@Override
			public void widgetDefaultSelected(SelectionEvent e)
			{
				widgetSelected(e);
			}

			@Override
			public void widgetSelected(SelectionEvent e)
			{
				if (checkboxEnable.getSelection())
				{
					filterSource.setEnabled(true);
					filterSource.setFocus();
				}
				else
				{
					filterSource.setEnabled(false);
				}
			}
      });
      
      // Filtering script
      Label label = new Label(dialogArea, SWT.NONE);
      label.setText("Filtering script");

      GridData gd = new GridData();
      gd.verticalIndent = WidgetHelper.DIALOG_SPACING;
		label.setLayoutData(gd);
      
      filterSource = new ScriptEditor(dialogArea, SWT.BORDER | SWT.MULTI | SWT.H_SCROLL | SWT.V_SCROLL);
		filterSource.setText(object.getAutoApplyFilter());
		filterSource.setEnabled(object.isAutoApplyEnabled());
		
		gd = new GridData();
		gd.grabExcessHorizontalSpace = true;
		gd.grabExcessVerticalSpace = true;
		gd.horizontalAlignment = SWT.FILL;
		gd.verticalAlignment = SWT.FILL;
		gd.widthHint = 0;
      gd.heightHint = 0;
		filterSource.setLayoutData(gd);
		
		return dialogArea;
	}
	
	/**
	 * Apply changes
	 * 
	 * @param isApply true if update operation caused by "Apply" button
	 */
	protected void applyChanges(final boolean isApply)
	{
		final boolean isAutoApplyEnabled = checkboxEnable.getSelection();
		if ((!isAutoApplyEnabled && !initialApplyFlag) ||
		    (isAutoApplyEnabled && initialApplyFlag && initialApplyFilter.equals(filterSource.getText())))
			return;		// Nothing to apply
		
		if (isApply)
			setValid(false);
		
		final String newApplyFilter = new String(filterSource.getText());
		new Job("Update auto-apply filter") {
			@Override
			protected IStatus run(IProgressMonitor monitor)
			{
				IStatus status;
				
				try
				{
					if (object != null)
					{
						NXCObjectModificationData md = new NXCObjectModificationData(object.getObjectId());
						md.setAutoApplyEnabled(isAutoApplyEnabled);
						md.setAutoApplyFilter(newApplyFilter);
						((NXCSession)ConsoleSharedData.getSession()).modifyObject(md);
					}
					initialApplyFlag = isAutoApplyEnabled;
					initialApplyFilter = newApplyFilter;
					status = Status.OK_STATUS;
				}
				catch(Exception e)
				{
					status = new Status(Status.ERROR, Activator.PLUGIN_ID, 
					                    (e instanceof NXCException) ? ((NXCException)e).getErrorCode() : 0,
					                    "Cannot change template automatic apply options: " + e.getMessage(), null);
				}

				if (isApply)
				{
					new UIJob("Update \"Automatic Apply Rules\" property page") {
						@Override
						public IStatus runInUIThread(IProgressMonitor monitor)
						{
							AutoApply.this.setValid(true);
							return Status.OK_STATUS;
						}
					}.schedule();
				}

				return status;
			}
		}.schedule();
	}
	
	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#performOk()
	 */
	@Override
	public boolean performOk()
	{
		applyChanges(false);
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
}
