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
package org.netxms.ui.eclipse.objectview.objecttabs.elements;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.objects.Interface;
import org.netxms.client.objects.Node;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.objectview.Activator;
import org.netxms.ui.eclipse.widgets.CommandBox;

/**
 * "Commands" element
 *
 */
public class Commands extends OverviewPageElement
{
	private CommandBox commandBox;
	private Action actionShutdown;
	
	/**
	 * @param parent
	 * @param object
	 */
	public Commands(Composite parent, GenericObject object)
	{
		super(parent, object);
		createActions();
	}
	
	/**
	 * Create actions
	 */
	private void createActions()
	{
		actionShutdown = new Action("Shutdown system") {
			@Override
			public void run()
			{
				final GenericObject object = getObject();
				if (MessageDialog.openQuestion(getShell(), "Confirmation", "Node " + object.getObjectName() + " will be shut down. Are you sure?"))
				{
					new ConsoleJob("Initiate node shutdown", null, Activator.PLUGIN_ID, null) {
						@Override
						protected void runInternal(IProgressMonitor monitor) throws Exception
						{
						}
	
						@Override
						protected String getErrorMessage()
						{
							return "Cannot initiate node shutdown";
						}
					}.start();
				}
			}
		};
		actionShutdown.setImageDescriptor(Activator.getImageDescriptor("icons/shutdown.png"));
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.elements.OverviewPageElement#getTitle()
	 */
	@Override
	protected String getTitle()
	{
		return "Commands";
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.elements.OverviewPageElement#onObjectChange()
	 */
	@Override
	void onObjectChange()
	{
		commandBox.deleteAll(false);
		if (getObject() instanceof Node)
		{
			commandBox.add(actionShutdown, false);
		}
		else if (getObject() instanceof Interface)
		{
		}
		commandBox.rebuild();
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.widgets.DashboardElement#createClientArea(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createClientArea(Composite parent)
	{
		commandBox = new CommandBox(parent, SWT.NONE);
		return commandBox;
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.elements.OverviewPageElement#isApplicableForObject(org.netxms.client.objects.GenericObject)
	 */
	@Override
	public boolean isApplicableForObject(GenericObject object)
	{
		return (object instanceof Node) || (object instanceof Interface);
	}
}
