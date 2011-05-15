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
package org.netxms.ui.eclipse.charts.objecttabs.internal;

import java.util.Arrays;
import java.util.Date;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.PlatformUI;
import org.netxms.client.NXCSession;
import org.netxms.client.datacollection.DciData;
import org.netxms.client.datacollection.GraphItem;
import org.netxms.client.datacollection.GraphItemStyle;
import org.netxms.client.datacollection.PerfTabDci;
import org.netxms.ui.eclipse.charts.Activator;
import org.netxms.ui.eclipse.charts.PerfTabGraphSettings;
import org.netxms.ui.eclipse.charts.widgets.LineChart;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.widgets.BorderedComposite;

/**
 * Performance tab graph
 *
 */
public class PerfTabGraph extends BorderedComposite
{
	private long nodeId;
	private PerfTabDci dci;
	private LineChart chart;
	private Runnable refreshTimer;
	private boolean updateInProgress = false;
	private NXCSession session;
	
	/**
	 * @param parent
	 * @param style
	 */
	public PerfTabGraph(Composite parent, long nodeId, PerfTabDci dci, PerfTabGraphSettings settings)
	{
		super(parent, SWT.NONE);
		this.nodeId = nodeId;
		this.dci = dci;
		session = (NXCSession)ConsoleSharedData.getSession();
		
		setLayout(new FillLayout());
		
		chart = new LineChart(this, SWT.NONE);
		chart.setZoomEnabled(false);
		chart.setTitleVisible(true);
		chart.setChartTitle(settings.getTitle().isEmpty() ? dci.getDescription() : settings.getTitle());
		chart.setLegendVisible(false);
		
		GraphItemStyle style = new GraphItemStyle(settings.getType(), settings.getColorAsInt(), 2, 0);
		chart.setItemStyles(Arrays.asList(new GraphItemStyle[] { style }));
		
		chart.addParameter(new GraphItem(nodeId, dci.getId(), 0, 0, "", dci.getDescription()));

		final Display display = getDisplay();
		refreshTimer = new Runnable() {
			@Override
			public void run()
			{
				if (PerfTabGraph.this.isDisposed())
					return;
				
				refreshData();
				display.timerExec(30000, this);
			}
		};
		display.timerExec(30000, refreshTimer);
		refreshData();
	}

	/**
	 * Refresh graph's data
	 */
	private void refreshData()
	{
		if (updateInProgress)
			return;
		
		updateInProgress = true;
		
		ConsoleJob job = new ConsoleJob("Get DCI values for history graph", null, Activator.PLUGIN_ID, Activator.PLUGIN_ID)
		{
			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				final Date from = new Date(System.currentTimeMillis() - 3600000);
				final Date to = new Date(System.currentTimeMillis());
				final DciData data = session.getCollectedData(nodeId, dci.getId(), from, to, 0);
				//final Threshold[] thresholds = session.getThresholds(nodeId, dci.getId());

				PlatformUI.getWorkbench().getDisplay().asyncExec(new Runnable() {
					@Override
					public void run()
					{
						if (!chart.isDisposed())
						{
							chart.setTimeRange(from, to);
							chart.updateParameter(0, data, true);
						}
						updateInProgress = false;
					}
				});
			}

			@Override
			protected String getErrorMessage()
			{
				return "Cannot get DCI values for history graph";
			}

			@Override
			protected void jobFailureHandler()
			{
				updateInProgress = false;
				super.jobFailureHandler();
			}
		};
		job.setUser(false);
		job.start();
	}
}
