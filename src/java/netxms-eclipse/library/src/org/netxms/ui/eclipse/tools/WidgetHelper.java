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
package org.netxms.ui.eclipse.tools;

import org.eclipse.jface.dialogs.IDialogSettings;
import org.eclipse.jface.preference.ColorSelector;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.StyledText;
import org.eclipse.swt.dnd.Clipboard;
import org.eclipse.swt.dnd.TextTransfer;
import org.eclipse.swt.dnd.Transfer;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Table;
import org.eclipse.swt.widgets.TableColumn;
import org.eclipse.swt.widgets.Text;
import org.netxms.ui.eclipse.widgets.SortableTableViewer;

/**
 * Utility class for simplified creation of widgets
 */
public class WidgetHelper
{
	public static final int INNER_SPACING = 2;
	public static final int OUTER_SPACING = 4;
	public static final int DIALOG_WIDTH_MARGIN = 10;
	public static final int DIALOG_HEIGHT_MARGIN = 10;
	public static final int DIALOG_SPACING = 4;
	public static final int BUTTON_WIDTH_HINT = 90;
	public static final int WIDE_BUTTON_WIDTH_HINT = 120;
	public static final String DEFAULT_LAYOUT_DATA = "WidgetHelper::default_layout_data";
		
	/**
    * Create pair of label and input field, with label above
	 * 
	 * @param parent Parent composite
	 * @param flags Flags for Text creation
	 * @param widthHint Width hint for text control
	 * @param labelText Label's text
	 * @param initialText Initial text for input field (may be null)
	 * @param layoutData Layout data for label/input pair. If null, default GridData will be assigned.
	 * @return Created Text object
	 */
	public static Text createLabeledText(final Composite parent, int flags, int widthHint, final String labelText,
	                                     final String initialText, Object layoutData)
	{
		Composite group = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = INNER_SPACING;
		layout.horizontalSpacing = 0;
		layout.marginTop = 0;
		layout.marginBottom = 0;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		group.setLayout(layout);

		if (layoutData != DEFAULT_LAYOUT_DATA)
		{
			group.setLayoutData(layoutData);
		}
		else
		{
			GridData gridData = new GridData();
			gridData.horizontalAlignment = GridData.FILL;
			gridData.grabExcessHorizontalSpace = true;
			group.setLayoutData(gridData);
		}
		
		Label label = new Label(group, SWT.NONE);
		label.setText(labelText);

		Text text = new Text(group, flags);
		if (initialText != null)
			text.setText(initialText);
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.widthHint = widthHint;
		text.setLayoutData(gridData);		
		
		return text;
	}

	/**
    * Create pair of label and StyledText widget, with label above
	 * 
	 * @param parent Parent composite
	 * @param flags Flags for Text creation
	 * @param labelText Label's text
	 * @param initialText Initial text for input field (may be null)
	 * @param layoutData Layout data for label/input pair. If null, default GridData will be assigned.
	 * @return Created Text object
	 */
	public static StyledText createLabeledStyledText(final Composite parent, int flags, final String labelText,
	                                                 final String initialText, Object layoutData)
	{
		Composite group = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = INNER_SPACING;
		layout.horizontalSpacing = 0;
		layout.marginTop = 0;
		layout.marginBottom = 0;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		group.setLayout(layout);

		if (layoutData != DEFAULT_LAYOUT_DATA)
		{
			group.setLayoutData(layoutData);
		}
		else
		{
			GridData gridData = new GridData();
			gridData.horizontalAlignment = GridData.FILL;
			gridData.grabExcessHorizontalSpace = true;
			group.setLayoutData(gridData);
		}
		
		Label label = new Label(group, SWT.NONE);
		label.setText(labelText);

		StyledText text = new StyledText(group, flags);
		if (initialText != null)
			text.setText(initialText);
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		text.setLayoutData(gridData);		
		
		return text;
	}

	/**
    * Create pair of label and combo box, with label above
	 * 
	 * @param parent Parent composite
	 * @param flags Flags for Text creation
	 * @param labelText Label's text
	 * @param layoutData Layout data for label/input pair. If null, default GridData will be assigned.
	 * @return Created Text object
	 */
	public static Combo createLabeledCombo(final Composite parent, int flags, final String labelText,
	                                       Object layoutData)
	{
		Composite group = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = INNER_SPACING;
		layout.horizontalSpacing = 0;
		layout.marginTop = 0;
		layout.marginBottom = 0;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		group.setLayout(layout);

		if (layoutData != DEFAULT_LAYOUT_DATA)
		{
			group.setLayoutData(layoutData);
		}
		else
		{
			GridData gridData = new GridData();
			gridData.horizontalAlignment = GridData.FILL;
			gridData.grabExcessHorizontalSpace = true;
			group.setLayoutData(gridData);
		}
		
		Label label = new Label(group, SWT.NONE);
		label.setText(labelText);

		Combo combo = new Combo(group, flags);
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		combo.setLayoutData(gridData);		
		
		return combo;
	}
	
	/**
    * Create pair of label and color selector, with label above
	 * 
	 * @param parent Parent composite
	 * @param labelText Label's text
	 * @param layoutData Layout data for label/input pair. If null, default GridData will be assigned.
	 * @return Created Text object
	 */
	public static ColorSelector createLabeledColorSelector(final Composite parent, final String labelText, Object layoutData)
	{
		Composite group = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = INNER_SPACING;
		layout.horizontalSpacing = 0;
		layout.marginTop = 0;
		layout.marginBottom = 0;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		group.setLayout(layout);

		if (layoutData != DEFAULT_LAYOUT_DATA)
		{
			group.setLayoutData(layoutData);
		}
		else
		{
			GridData gridData = new GridData();
			gridData.horizontalAlignment = GridData.FILL;
			group.setLayoutData(gridData);
		}
		
		Label label = new Label(group, SWT.NONE);
		label.setText(labelText);

		ColorSelector cs = new ColorSelector(group);
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		cs.getButton().setLayoutData(gridData);		
		
		return cs;
	}
	
	/**
	 * Create labeled control using factory.
	 * 
	 * @param parent parent composite
	 * @param flags flags for control being created
	 * @param factory control factory
	 * @param labelText Label's text
	 * @param layoutData Layout data for label/input pair. If null, default GridData will be assigned.
	 * @return created control
	 */
	public static Control createLabeledControl(Composite parent, int flags, WidgetFactory factory, String labelText, Object layoutData)
	{
		Composite group = new Composite(parent, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = INNER_SPACING;
		layout.horizontalSpacing = 0;
		layout.marginTop = 0;
		layout.marginBottom = 0;
		layout.marginWidth = 0;
		layout.marginHeight = 0;
		group.setLayout(layout);

		if (layoutData != DEFAULT_LAYOUT_DATA)
		{
			group.setLayoutData(layoutData);
		}
		else
		{
			GridData gridData = new GridData();
			gridData.horizontalAlignment = GridData.FILL;
			gridData.grabExcessHorizontalSpace = true;
			group.setLayoutData(gridData);
		}
		
		Label label = new Label(group, SWT.NONE);
		label.setText(labelText);

		final Control widget = factory.createControl(group, flags);
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		widget.setLayoutData(gridData);		

		return widget;
	}
	
	/**
	 * Save settings of table viewer columns
	 * 
	 * @param table Table control
	 * @param settings Dialog settings object
	 * @param prefix Prefix for properties
	 */
	public static void saveColumnSettings(Table table, IDialogSettings settings, String prefix)
	{
		TableColumn[] columns = table.getColumns();
		for(int i = 0; i < columns.length; i++)
		{
			settings.put(prefix + "." + i + ".width", columns[i].getWidth()); //$NON-NLS-1$
		}
	}
	
	/**
	 * Restore settings of table viewer columns previously saved by call to WidgetHelper.saveColumnSettings
	 * 
	 * @param table Table control
	 * @param settings Dialog settings object
	 * @param prefix Prefix for properties
	 */
	public static void restoreColumnSettings(Table table, IDialogSettings settings, String prefix)
	{
		TableColumn[] columns = table.getColumns();
		for(int i = 0; i < columns.length; i++)
		{
			try
			{
				int w = settings.getInt(prefix + "." + i + ".width"); //$NON-NLS-1$
				columns[i].setWidth(w);
			}
			catch(NumberFormatException e)
			{
			}
		}
	}
	
	/**
	 * Save settings for sortable table viewer
	 * @param viewer Viewer
	 * @param settings Dialog settings object
	 * @param prefix Prefix for properties
	 */
	public static void saveTableViewerSettings(SortableTableViewer viewer, IDialogSettings settings, String prefix)
	{
		final Table table = viewer.getTable();
		saveColumnSettings(table, settings, prefix);
		TableColumn column = table.getSortColumn();
		settings.put(prefix + ".sortColumn", (column != null) ? (Integer)column.getData("ID") : -1);
		settings.put(prefix + ".sortDirection", table.getSortDirection());
	}
	
	/**
	 * Restore settings for sortable table viewer
	 * @param viewer Viewer
	 * @param settings Dialog settings object
	 * @param prefix Prefix for properties
	 */
	public static void restoreTableViewerSettings(SortableTableViewer viewer, IDialogSettings settings, String prefix)
	{
		final Table table = viewer.getTable();
		restoreColumnSettings(table, settings, prefix);
		try
		{
			table.setSortDirection(settings.getInt(prefix + ".sortDirection"));
			int column = settings.getInt(prefix + ".sortColumn");
			if (column >= 0)
			{
				table.setSortColumn(viewer.getColumnById(column));
			}
		}
		catch(NumberFormatException e)
		{
		}
	}
	
	/**
	 * Copy given text to clipboard
	 * 
	 * @param text 
	 */
	public static void copyToClipboard(final String text)
	{
		final Clipboard cb = new Clipboard(Display.getCurrent());
      Transfer transfer = TextTransfer.getInstance();
      cb.setContents(new Object[] { (text != null) ? text : "" }, new Transfer[] { transfer });
      cb.dispose();
   }
}
