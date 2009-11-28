/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2009 Victor Kirhenshtein
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
package org.netxms.ui.eclipse.datacollection.dialogs;

import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.widgets.Shell;
import org.netxms.client.AgentParameter;
import org.netxms.client.NXCSession;
import org.netxms.ui.eclipse.datacollection.Activator;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.shared.NXMCSharedData;

/**
 * @author victor
 *
 */
public class SelectAgentParamDlg extends AbstractSelectParamDlg
{
	private List<AgentParameter> parameters;
	
	public SelectAgentParamDlg(Shell parentShell, long nodeId)
	{
		super(parentShell, nodeId);
	}

	/**
	 * Fill parameter list
	 */
	void fillParameterList()
	{
		final NXCSession session = NXMCSharedData.getInstance().getSession();
		
		ConsoleJob job = new ConsoleJob("Get list of supported parameters for " + object.getObjectName(),
				                          null, Activator.PLUGIN_ID, null) {
			@Override
			protected String getErrorMessage()
			{
				return "Unable to retrieve list of supported parameters";
			}

			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				parameters = session.getSupportedParameters(object.getObjectId());
			}
		};
		job.start();
		
		boolean jobCompleted = false;
		do
		{
			try
			{
				job.join();
				jobCompleted = true;
			}
			catch(InterruptedException e)
			{
			}
		} while(!jobCompleted);
				
		viewer.setInput(parameters.toArray());
	}
}
