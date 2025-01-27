/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2022 Victor Kirhenshtein
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
package org.netxms.ui.eclipse.datacollection.actions;

import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.jface.action.IAction;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.window.Window;
import org.eclipse.swt.widgets.Shell;
import org.eclipse.ui.IObjectActionDelegate;
import org.eclipse.ui.IWorkbenchPart;
import org.eclipse.ui.part.ViewPart;
import org.netxms.client.NXCSession;
import org.netxms.client.objects.AbstractObject;
import org.netxms.client.objects.Template;
import org.netxms.ui.eclipse.datacollection.Activator;
import org.netxms.ui.eclipse.datacollection.Messages;
import org.netxms.ui.eclipse.datacollection.dialogs.DciRemoveConfirmationDialog;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.objectbrowser.dialogs.RelatedObjectSelectionDialog;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;

/**
 * Remove template from data collection target
 */
public class RemoveTemplate implements IObjectActionDelegate
{
	private Shell shell;
	private ViewPart viewPart;
	private long parentId;

   /**
    * @see org.eclipse.ui.IObjectActionDelegate#setActivePart(org.eclipse.jface.action.IAction, org.eclipse.ui.IWorkbenchPart)
    */
	public void setActivePart(IAction action, IWorkbenchPart targetPart)
	{
		shell = targetPart.getSite().getShell();
		viewPart = (targetPart instanceof ViewPart) ? (ViewPart)targetPart : null;
	}

   /**
    * @see org.eclipse.ui.IActionDelegate#run(org.eclipse.jface.action.IAction)
    */
	public void run(IAction action)
	{
      final RelatedObjectSelectionDialog objectSelectionDialog = new RelatedObjectSelectionDialog(shell, parentId, RelatedObjectSelectionDialog.RelationType.DIRECT_SUBORDINATES, null);
		if (objectSelectionDialog.open() == Window.OK)
		{
			final DciRemoveConfirmationDialog confirmationDialog = new DciRemoveConfirmationDialog(shell);
			if (confirmationDialog.open() == Window.OK)
			{
            final NXCSession session = ConsoleSharedData.getSession();
				new ConsoleJob(Messages.get().RemoveTemplate_JobTitle, viewPart, Activator.PLUGIN_ID, null) {
					@Override
					protected String getErrorMessage()
					{
						return Messages.get().RemoveTemplate_JobError;
					}
	
					@Override
					protected void runInternal(IProgressMonitor monitor) throws Exception
					{
						List<AbstractObject> objects = objectSelectionDialog.getSelectedObjects();
						for(int i = 0; i < objects.size(); i++)
							session.removeTemplate(parentId, objects.get(i).getObjectId(), confirmationDialog.getRemoveFlag());
					}
				}.start();
			}
		}
	}

   /**
    * @see org.eclipse.ui.IActionDelegate#selectionChanged(org.eclipse.jface.action.IAction, org.eclipse.jface.viewers.ISelection)
    */
	public void selectionChanged(IAction action, ISelection selection)
	{
      if ((selection instanceof IStructuredSelection) && (((IStructuredSelection)selection).size() == 1))
		{
			Object object = ((IStructuredSelection)selection).getFirstElement();
			if (object instanceof Template)
			{
				action.setEnabled(true);
				parentId = ((AbstractObject)object).getObjectId();
			}
			else
			{
				action.setEnabled(false);
				parentId = 0;
			}
		}
		else
		{
			action.setEnabled(false);
			parentId = 0;
		}
	}
}
