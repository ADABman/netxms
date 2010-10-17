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
package org.netxms.ui.eclipse.datacollection.propertypages;

import java.util.Arrays;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.viewers.TableViewer;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.ui.dialogs.PropertyPage;
import org.eclipse.ui.progress.UIJob;
import org.netxms.client.datacollection.DataCollectionItem;
import org.netxms.ui.eclipse.datacollection.Activator;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.nxsl.widgets.ScriptEditor;
import org.netxms.ui.eclipse.tools.WidgetFactory;
import org.netxms.ui.eclipse.tools.WidgetHelper;

/**
 * @author Victor
 *
 */
public class Transformation extends PropertyPage
{
	private static final String[] DCI_FUNCTIONS = { "FindDCIByName", "FindDCIByDescription", "GetDCIObject", "GetDCIValue" };
	private static final String[] DCI_VARIABLES = { "$node" };
	
	private DataCollectionItem dci;
	private Combo deltaCalculation;
	private ScriptEditor transformationScript;
	private Button testScriptButton;
	
	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#createContents(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createContents(Composite parent)
	{
		dci = (DataCollectionItem)getElement().getAdapter(DataCollectionItem.class);
		Composite dialogArea = new Composite(parent, SWT.NONE);
		
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = WidgetHelper.OUTER_SPACING;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
      dialogArea.setLayout(layout);

      deltaCalculation = WidgetHelper.createLabeledCombo(dialogArea, SWT.BORDER | SWT.READ_ONLY, "Step 1 - delta calculation",
                                                         WidgetHelper.DEFAULT_LAYOUT_DATA);
      deltaCalculation.add("None (keep original value)");
      deltaCalculation.add("Simple delta");
      deltaCalculation.add("Average delta per second");
      deltaCalculation.add("Average delta per minute");
      deltaCalculation.select(dci.getDeltaCalculation());
     
      GridData gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.verticalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      gd.grabExcessVerticalSpace = true;
      gd.widthHint = 0;
      gd.heightHint = 0;
      final WidgetFactory factory = new WidgetFactory() {
			@Override
			public Control createControl(Composite parent, int style)
			{
				return new ScriptEditor(parent, style);
			}
      };
      transformationScript = (ScriptEditor)WidgetHelper.createLabeledControl(dialogArea, SWT.BORDER | SWT.MULTI | SWT.H_SCROLL | SWT.V_SCROLL,
                                                                             factory, "Step 2 - transformation script", gd);
      transformationScript.addFunctions(Arrays.asList(DCI_FUNCTIONS));
      transformationScript.addVariables(Arrays.asList(DCI_VARIABLES));
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.verticalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      gd.grabExcessVerticalSpace = true;
      transformationScript.setLayoutData(gd);
      transformationScript.setText(dci.getTransformationScript());
      
      testScriptButton = new Button(transformationScript.getParent(), SWT.PUSH);
      testScriptButton.setText("&Test...");   
      gd = new GridData();
      gd.horizontalAlignment = SWT.RIGHT;
      gd.widthHint = WidgetHelper.BUTTON_WIDTH_HINT;
      testScriptButton.setLayoutData(gd);
		
		return dialogArea;
	}
	
	/**
	 * Apply changes
	 * 
	 * @param isApply true if update operation caused by "Apply" button
	 */
	protected void applyChanges(final boolean isApply)
	{
		if (isApply)
			setValid(false);
		
		dci.setDeltaCalculation(deltaCalculation.getSelectionIndex());
		dci.setTransformationScript(transformationScript.getText());
		
		new ConsoleJob("Update transformation settings for DCI " + dci.getId(), null, Activator.PLUGIN_ID, null) {
			@Override
			protected String getErrorMessage()
			{
				return "Cannot update transformation settings";
			}

			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				dci.getOwner().modifyItem(dci);
				new UIJob("Update data collection item list") {
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						((TableViewer)dci.getOwner().getUserData()).update(dci, null);
						return Status.OK_STATUS;
					}
				}.schedule();
			}

			/* (non-Javadoc)
			 * @see org.netxms.ui.eclipse.jobs.ConsoleJob#jobFinalize()
			 */
			@Override
			protected void jobFinalize()
			{
				if (isApply)
				{
					new UIJob("Update \"Transformation\" property page") {
						@Override
						public IStatus runInUIThread(IProgressMonitor monitor)
						{
							Transformation.this.setValid(true);
							return Status.OK_STATUS;
						}
					}.schedule();
				}
			}
		}.start();
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

	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#performDefaults()
	 */
	@Override
	protected void performDefaults()
	{
		super.performDefaults();
		deltaCalculation.select(DataCollectionItem.DELTA_NONE);
		transformationScript.setText("");
	}
}
