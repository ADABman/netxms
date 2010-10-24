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
package org.netxms.ui.eclipse.charts.widgets;

import org.eclipse.birt.chart.api.ChartEngine;
import org.eclipse.birt.chart.device.IDeviceRenderer;
import org.eclipse.birt.chart.exception.ChartException;
import org.eclipse.birt.chart.factory.GeneratedChartState;
import org.eclipse.birt.chart.factory.IGenerator;
import org.eclipse.birt.chart.model.Chart;
import org.eclipse.birt.chart.model.attribute.Bounds;
import org.eclipse.birt.chart.model.attribute.ColorDefinition;
import org.eclipse.birt.chart.model.attribute.Palette;
import org.eclipse.birt.chart.model.attribute.Position;
import org.eclipse.birt.chart.model.attribute.impl.BoundsImpl;
import org.eclipse.birt.chart.model.attribute.impl.ColorDefinitionImpl;
import org.eclipse.birt.chart.model.attribute.impl.PaletteImpl;
import org.eclipse.birt.core.framework.PlatformConfig;
import org.eclipse.jface.preference.PreferenceConverter;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.ControlEvent;
import org.eclipse.swt.events.ControlListener;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.GC;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.graphics.RGB;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Composite;
import org.netxms.ui.eclipse.charts.api.ChartColor;

/**
 * Abstract base class for all BIRT-based charts
 *
 */
public abstract class GenericBirtChart extends GenericChart implements PaintListener, ControlListener
{
	private Chart chart = null;
	private boolean fullRepaint = true;
	private IDeviceRenderer deviceRenderer = null;
	private GC gcImage = null;
	private GeneratedChartState generatedChartState = null;
	private IGenerator generator = null;
	private Image imgChart = null;
	
	/**
	 * Create chart widget
	 * 
	 * @param parent parent widget
	 * @param style widget style
	 */
	public GenericBirtChart(Composite parent, int style)
	{
		super(parent, style | SWT.NO_BACKGROUND);
		
		try
		{
			PlatformConfig config = new PlatformConfig();
			config.setBIRTHome("");
			final ChartEngine chartEngine = ChartEngine.instance(config);
			deviceRenderer = chartEngine.getRenderer("dv.SWT");
			generator = chartEngine.getGenerator();
		}
		catch(ChartException e)
		{
			/* TODO: implement logging or error reporting */
			e.printStackTrace();
		}
		
		addPaintListener(this);
		addControlListener(this);
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.api.DataChart#initializationComplete()
	 */
	@Override
	public void initializationComplete()
	{
		chart = createChart();
		fullRepaint = true;
		redraw();
	}
	
	/**
	 * Force chart recreation (for example, after chart type change).
	 */
	protected void recreateChart()
	{
		chart = createChart();
		fullRepaint = true;
		redraw();
	}

	/**
	 * Create underlying chart object. Must be implemented by subclasses.
	 * 
	 * @return BIRT chart object
	 */
	protected abstract Chart createChart();

	/**
	 * Get underlying BIRT chart object.
	 * 
	 * @return underlying BIRT chart object
	 */
	public Chart getChart()
	{
		return chart;
	}

	/* (non-Javadoc)
	 * @see org.eclipse.swt.events.ControlListener#controlMoved(org.eclipse.swt.events.ControlEvent)
	 */
	@Override
	public void controlMoved(ControlEvent arg0)
	{
		fullRepaint = true;
	}

	/* (non-Javadoc)
	 * @see org.eclipse.swt.events.ControlListener#controlResized(org.eclipse.swt.events.ControlEvent)
	 */
	@Override
	public void controlResized(ControlEvent arg0)
	{
		fullRepaint = true;
	}

	/* (non-Javadoc)
	 * @see org.eclipse.swt.widgets.Widget#dispose()
	 */
	@Override
	public void dispose()
	{
		if ((imgChart != null) && (!imgChart.isDisposed()))
		{
			imgChart.dispose();
		}
		if ((gcImage != null) && (!gcImage.isDisposed()))
		{
			gcImage.dispose();
		}
		super.dispose();
	}

	/* (non-Javadoc)
	 * @see org.eclipse.swt.events.PaintListener#paintControl(org.eclipse.swt.events.PaintEvent)
	 */
	@Override
	public final void paintControl(PaintEvent event)
	{
		if (chart == null)
			return;
		
		Rectangle clientArea = this.getClientArea();
		if (fullRepaint)
		{
			//if resized
			if ((imgChart != null) && (!imgChart.isDisposed()))
				imgChart.dispose();
			if ((gcImage != null) && (!gcImage.isDisposed()))
				gcImage.dispose();

			imgChart = new Image(getDisplay(), clientArea);
			gcImage = new GC(imgChart);
			deviceRenderer.setProperty(IDeviceRenderer.GRAPHICS_CONTEXT, gcImage);
			final Bounds bounds = BoundsImpl.create(0, 0, clientArea.width, clientArea.height);
			bounds.scale(72d / deviceRenderer.getDisplayServer().getDpiResolution());

			try
			{
				generatedChartState = generator.build(deviceRenderer.getDisplayServer(), chart, bounds, null, null, null);
				generator.render(deviceRenderer, generatedChartState);
			}
			catch(ChartException e)
			{
				/* TODO: add logging and/or user notification */
				e.printStackTrace();
			}
			
			fullRepaint = false;
		}

		event.gc.drawImage(imgChart, clientArea.x, clientArea.y);
	}
	
	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.api.DataChart#refresh()
	 */
	@Override
	public void refresh()
	{
		fullRepaint = true;
		redraw();
	}	

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.api.DataChart#setTitle(java.lang.String)
	 */
	@Override
	public void setTitle(String title)
	{
		this.title = title;
		if (chart != null)
		{
			chart.getTitle().getLabel().getCaption().setValue(title);
			refresh();
		}
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.api.DataChart#setLegendVisible(boolean)
	 */
	@Override
	public void setLegendVisible(boolean visible)
	{
		legendVisible = visible;
		if (chart != null)
		{
			chart.getLegend().setVisible(visible);
			refresh();
		}
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.api.DataChart#setTitleVisible(boolean)
	 */
	@Override
	public void setTitleVisible(boolean visible)
	{
		titleVisible = visible;
		if (chart != null)
			chart.getTitle().setVisible(visible);
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.api.DataChart#set3DModeEnabled(boolean)
	 */
	@Override
	public void set3DModeEnabled(boolean enabled)
	{
		displayIn3D = enabled;
		if (chart != null)
			recreateChart();
	}
	
	/**
	 * Get palette usable by BIRT
	 * @return BIRT palette object
	 */
	protected Palette getBirtPalette()
	{
		Palette birtPalette = PaletteImpl.create(0, true);
		for(ChartColor c : palette)
			birtPalette.getEntries().add(ColorDefinitionImpl.create(c.red, c.green, c.blue, c.alpha));
		return birtPalette;
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.api.DataChart#setLogScaleEnabled(boolean)
	 */
	@Override
	public void setLogScaleEnabled(boolean enabled)
	{
		if (useLogScale != enabled)
		{
			useLogScale = enabled;
			if (chart != null)
				recreateChart();
		}
	}

	/**
	 * Create BIRT color definition object from preference string
	 *  
	 * @param name Preference name
	 * @return color definition object
	 */
	protected ColorDefinition getColorFromPreferences(final String name)
	{
		RGB rgb = PreferenceConverter.getColor(preferenceStore, name);
		return ColorDefinitionImpl.create(rgb.red, rgb.green, rgb.blue);
	}
	
	/**
	 * Get Position object from int constant
	 * @param value
	 * @return
	 */
	protected Position positionFromInt(int value)
	{
		switch(value)
		{
			case POSITION_LEFT:
				return Position.LEFT_LITERAL;
			case POSITION_RIGHT:
				return Position.RIGHT_LITERAL;
			case POSITION_TOP:
				return Position.ABOVE_LITERAL;
			case POSITION_BOTTOM:
				return Position.BELOW_LITERAL;
		}
		return Position.RIGHT_LITERAL;
	}

	/* (non-Javadoc)
	 * @see org.netxms.ui.eclipse.charts.widgets.GenericChart#setLegendPosition(int)
	 */
	@Override
	public void setLegendPosition(int position)
	{
		super.setLegendPosition(position);
		if (chart != null)
			recreateChart();
	}
}
