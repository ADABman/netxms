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
package org.netxms.ui.eclipse.charts.objecttabs;

import java.util.ArrayList;
import java.util.List;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.core.runtime.jobs.Job;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.ScrolledComposite;
import org.eclipse.swt.events.ControlAdapter;
import org.eclipse.swt.events.ControlEvent;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.ui.progress.UIJob;
import org.netxms.client.NXCSession;
import org.netxms.client.datacollection.PerfTabDci;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.objects.Node;
import org.netxms.ui.eclipse.charts.PerfTabGraphSettings;
import org.netxms.ui.eclipse.charts.objecttabs.internal.PerfTabGraph;
import org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;

/**
 * Performance tab
 *
 */
public class PerformanceTab extends ObjectTab
{
	private List<PerfTabGraph> charts = new ArrayList<PerfTabGraph>();
	private ScrolledComposite scroller;
	private Composite chartArea;
	private Label labelLoading = null;
	
	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#createTabContent(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected void createTabContent(Composite parent)
	{
		scroller = new ScrolledComposite(parent, SWT.V_SCROLL);
		
		chartArea = new Composite(scroller, SWT.NONE);
		chartArea.setBackground(new Color(Display.getCurrent(), 255, 255, 255));
		
		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		layout.makeColumnsEqualWidth = true;
		layout.marginWidth = 15;
		layout.marginHeight = 15;
		layout.horizontalSpacing = 10;
		layout.verticalSpacing = 10;
		chartArea.setLayout(layout);

		scroller.setContent(chartArea);
		scroller.setExpandVertical(true);
		scroller.setExpandHorizontal(true);
		scroller.getVerticalBar().setIncrement(20);
		scroller.addControlListener(new ControlAdapter() {
			public void controlResized(ControlEvent e)
			{
				Rectangle r = scroller.getClientArea();
				scroller.setMinSize(chartArea.computeSize(r.width, SWT.DEFAULT));
			}
		});
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#objectChanged(org.netxms.client.objects.GenericObject)
	 */
	@Override
	public void objectChanged(final GenericObject object)
	{
		for(PerfTabGraph chart : charts)
			chart.dispose();
		charts.clear();
		
		if ((labelLoading == null) || labelLoading.isDisposed())
		{
			labelLoading = new Label(chartArea, SWT.NONE);
			labelLoading.setText("Loading performance data...");
		}
		
		Job job = new Job("Update performance tab") {
			@Override
			protected IStatus run(IProgressMonitor monitor)
			{
				final NXCSession session = (NXCSession)ConsoleSharedData.getSession();
				try
				{
					final PerfTabDci[] items = session.getPerfTabItems(object.getObjectId());
					new UIJob("Update performance tab") {
						@Override
						public IStatus runInUIThread(IProgressMonitor monitor)
						{
							if (PerformanceTab.this.getObject().getObjectId() == object.getObjectId())
							{
								update(items);
							}
							return Status.OK_STATUS;
						}
					}.schedule();
				}
				catch(Exception e)
				{
				}
				return Status.OK_STATUS;
			}
		};
		job.setSystem(true);
		job.schedule();
	}
	
	/**
	 * Update tab with received DCIs
	 * 
	 * @param items Performance tab items
	 */
	private void update(final PerfTabDci[] items)
	{
		labelLoading.dispose();
		
		for(int i = 0; i < items.length; i++)
		{
			try
			{
				PerfTabGraphSettings settings = PerfTabGraphSettings.createFromXml(items[i].getPerfTabSettings());

				PerfTabGraph chart = new PerfTabGraph(chartArea, getObject().getObjectId(), items[i], settings);
				charts.add(chart);
				
				final GridData gd = new GridData();
				gd.horizontalAlignment = SWT.FILL;
				gd.grabExcessHorizontalSpace = true;
				gd.heightHint = 250;
				chart.setLayoutData(gd);
			}
			catch(Exception e)
			{
			}
		}
		updateChartAreaLayout();
	}

	/**
	 * Update entire chart area layout after content change
	 */
	private void updateChartAreaLayout()
	{
		chartArea.layout();
		Rectangle r = scroller.getClientArea();
		scroller.setMinSize(chartArea.computeSize(r.width, SWT.DEFAULT));
	}
	
	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.objectview.objecttabs.ObjectTab#showForObject(org.netxms.client.objects.GenericObject)
	 */
	@Override
	public boolean showForObject(GenericObject object)
	{
		return object instanceof Node;
	}
}
