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
package org.netxms.ui.eclipse.epp.widgets;

import java.util.Map.Entry;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IMenuListener;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.MenuManager;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.CLabel;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.MouseListener;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.ui.internal.dialogs.PropertyDialog;
import org.eclipse.ui.model.WorkbenchLabelProvider;
import org.netxms.client.NXCSession;
import org.netxms.client.ServerAction;
import org.netxms.client.constants.Severity;
import org.netxms.client.events.EventProcessingPolicyRule;
import org.netxms.client.events.EventTemplate;
import org.netxms.client.objects.GenericObject;
import org.netxms.ui.eclipse.console.resources.StatusDisplayInfo;
import org.netxms.ui.eclipse.epp.views.EventProcessingPolicyEditor;
import org.netxms.ui.eclipse.nxsl.widgets.ScriptEditor;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.widgets.DashboardElement;
import org.netxms.ui.eclipse.widgets.helpers.DashboardElementButton;

/**
 * Rule editor widget
 *
 */
@SuppressWarnings("restriction")
public class RuleEditor extends Composite
{
	private static final Color BACKGROUND_COLOR = new Color(Display.getDefault(), 255, 255, 255);
	private static final Color TITLE_BACKGROUND_COLOR = new Color(Display.getDefault(), 225, 233, 241);
	private static final Color SELECTED_TITLE_BACKGROUND_COLOR = new Color(Display.getDefault(), 245, 249, 104);
	private static final Color RULE_BORDER_COLOR = new Color(Display.getDefault(), 153, 180, 209);
	private static final Color CONDITION_BORDER_COLOR = new Color(Display.getDefault(), 198,214,172);
	private static final Color ACTION_BORDER_COLOR = new Color(Display.getDefault(), 186,176,201);
	private static final Color TITLE_COLOR = new Color(Display.getDefault(), 0, 0, 0);
	private static final int INDENT = 20;
	
	private EventProcessingPolicyRule rule;
	private int ruleNumber;
	private EventProcessingPolicyEditor editor;
	private NXCSession session;
	private WorkbenchLabelProvider labelProvider;
	private boolean collapsed = true;
	private boolean verticalLayout = false;
	private Composite leftPanel;
	private Label ruleNumberLabel;
	private Label headerLabel;
	private Composite mainArea;
	private DashboardElement condition;
	private DashboardElement action;
	private Label expandButton;
	private boolean modified = false;
	private boolean selected = false;
	
	/**
	 * @param parent
	 * @param rule
	 */
	public RuleEditor(Composite parent, EventProcessingPolicyRule rule, int ruleNumber, EventProcessingPolicyEditor editor)
	{
		super(parent, SWT.NONE);
		this.rule = rule;
		this.ruleNumber = ruleNumber;
		this.editor = editor;
		
		session = (NXCSession)ConsoleSharedData.getSession();
		labelProvider = new WorkbenchLabelProvider();
		
		setBackground(RULE_BORDER_COLOR);

		GridLayout layout = new GridLayout();
		layout.numColumns = 2;
		layout.marginHeight = 1;
		layout.marginWidth = 0;
		layout.horizontalSpacing = 1;
		layout.verticalSpacing = 1;
		setLayout(layout);
		
		createLeftPanel();
		createHeader();
		createMainArea();
		
		condition = new DashboardElement(mainArea, "Filter") {
			@Override
			protected Control createClientArea(Composite parent)
			{
				setBorderColor(CONDITION_BORDER_COLOR);
				setTitleColor(TITLE_COLOR);
				return createConditionControl(parent, RuleEditor.this.rule);
			}
		};
		configureLayout(condition);
		final Action editRuleCondition = new Action() {
			@Override
			public void run()
			{
				editRule("org.netxms.ui.eclipse.epp.propertypages.RuleCondition#0");
			}
		};
		condition.addButton(new DashboardElementButton("Edit condition", editor.getImageEdit(), editRuleCondition));
		condition.setDoubleClickAction(editRuleCondition);

		action = new DashboardElement(mainArea, "Action") {
			@Override
			protected Control createClientArea(Composite parent)
			{
				setBorderColor(ACTION_BORDER_COLOR);
				setTitleColor(TITLE_COLOR);
				return createActionControl(parent, RuleEditor.this.rule);
			}
		};
		configureLayout(action);
		final Action editRuleAction = new Action() {
			@Override
			public void run()
			{
				editRule("org.netxms.ui.eclipse.epp.propertypages.RuleAction#1");
			}
		};
		action.addButton(new DashboardElementButton("Edit actions", editor.getImageEdit(), editRuleAction));
		action.setDoubleClickAction(editRuleAction);
	}
	
	/**
	 * Create main area which contains condition and action elements
	 */
	private void createMainArea()
	{
		mainArea = new Composite(this, SWT.NONE);
		mainArea.setBackground(BACKGROUND_COLOR);
		
		GridLayout layout = new GridLayout();
		layout.numColumns = verticalLayout ? 1 : 2;
		layout.marginHeight = 4;
		layout.marginWidth = 4;
		layout.makeColumnsEqualWidth = true;
		mainArea.setLayout(layout);
		
		GridData gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		gd.exclude = collapsed;
		mainArea.setLayoutData(gd);
	}
	
	/**
	 * Create panel with rule number and collapse/expand button on the left
	 */
	private void createLeftPanel()
	{
		final MouseListener mouseListener = new MouseListener() {
			@Override
			public void mouseDoubleClick(MouseEvent e)
			{
			}

			@Override
			public void mouseDown(MouseEvent e)
			{
				switch(e.button)
				{
					case 1:
						processRuleMouseEvent(e);
						break;
					default:
						if (!selected)
							editor.setSelection(RuleEditor.this);
						break;
				}
			}

			@Override
			public void mouseUp(MouseEvent e)
			{
			}
		};
		
		leftPanel = new Composite(this, SWT.NONE);
		leftPanel.setBackground(TITLE_BACKGROUND_COLOR);
		leftPanel.addMouseListener(mouseListener);
		
		GridLayout layout = new GridLayout();
		layout.marginHeight = 0;
		layout.marginWidth = 0;
		leftPanel.setLayout(layout);

		GridData gd = new GridData();
		gd.verticalSpan = collapsed ? 1 : 2;
		gd.horizontalAlignment = SWT.FILL;
		gd.verticalAlignment = SWT.FILL;
		gd.grabExcessVerticalSpace = true;
		leftPanel.setLayoutData(gd);		
		
		ruleNumberLabel = new Label(leftPanel, SWT.NONE);
		ruleNumberLabel.setText(Integer.toString(ruleNumber));
		ruleNumberLabel.setFont(editor.getBoldFont());
		ruleNumberLabel.setBackground(TITLE_BACKGROUND_COLOR);
		ruleNumberLabel.setForeground(TITLE_COLOR);
		ruleNumberLabel.setAlignment(SWT.CENTER);
		ruleNumberLabel.addMouseListener(mouseListener);
		
		gd = new GridData();
		gd.horizontalAlignment = SWT.CENTER;
		gd.verticalAlignment = SWT.CENTER;
		gd.grabExcessVerticalSpace = true;
		gd.widthHint = 30;
		ruleNumberLabel.setLayoutData(gd);
		
		createPopupMenu(new Control[] { leftPanel, ruleNumberLabel });
	}

	/**
	 * Create popup menu for given controls
	 * 
	 * @param controls
	 */
	private void createPopupMenu(final Control[] controls)
	{
		MenuManager menuMgr = new MenuManager();
		menuMgr.setRemoveAllWhenShown(true);
		menuMgr.addMenuListener(new IMenuListener() {
			public void menuAboutToShow(IMenuManager mgr)
			{
				editor.fillRuleContextMenu(mgr);
			}
		});

		for(Control c : controls)
		{
			Menu menu = menuMgr.createContextMenu(c);
			c.setMenu(menu);
		}
	}
		
	/**
	 * Create rule header
	 */
	private void createHeader()
	{
		Composite header = new Composite(this, SWT.NONE);
		header.setBackground(TITLE_BACKGROUND_COLOR);
		
		GridLayout layout = new GridLayout();
		layout.numColumns = 3;
		header.setLayout(layout);
		
		GridData gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		header.setLayoutData(gd);
		
		headerLabel = new Label(header, SWT.NONE);
		headerLabel.setText(rule.getComments());
		headerLabel.setBackground(TITLE_BACKGROUND_COLOR);
		headerLabel.setForeground(TITLE_COLOR);
		headerLabel.setFont(editor.getNormalFont());
		gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		headerLabel.setLayoutData(gd);
		headerLabel.addMouseListener(new MouseListener() {
			@Override
			public void mouseDoubleClick(MouseEvent e)
			{
				if (e.button == 1)
					setCollapsed(!isCollapsed(), true);
			}

			@Override
			public void mouseDown(MouseEvent e)
			{
			}

			@Override
			public void mouseUp(MouseEvent e)
			{
			}
		});
		
		Label editButton = new Label(header, SWT.NONE);
		editButton.setBackground(TITLE_BACKGROUND_COLOR);
		editButton.setCursor(getDisplay().getSystemCursor(SWT.CURSOR_HAND));
		editButton.setImage(editor.getImageEdit());
		editButton.setToolTipText("Edit rule");
		editButton.addMouseListener(new MouseListener() {
			private boolean doAction = false;
			
			@Override
			public void mouseDoubleClick(MouseEvent e)
			{
				if (e.button == 1)
					doAction = false;
			}

			@Override
			public void mouseDown(MouseEvent e)
			{
				if (e.button == 1)
					doAction = true;
			}

			@Override
			public void mouseUp(MouseEvent e)
			{
				if ((e.button == 1) && doAction)
					editRule("org.netxms.ui.eclipse.epp.propertypages.RuleComments#2");
			}
		});
		
		expandButton = new Label(header, SWT.NONE);
		expandButton.setBackground(TITLE_BACKGROUND_COLOR);
		expandButton.setCursor(getDisplay().getSystemCursor(SWT.CURSOR_HAND));
		expandButton.setImage(collapsed ? editor.getImageExpand() : editor.getImageCollapse());
		expandButton.setToolTipText(collapsed ? "Expand rule" : "Collapse rule");
		expandButton.addMouseListener(new MouseListener() {
			private boolean doAction = false;
			
			@Override
			public void mouseDoubleClick(MouseEvent e)
			{
				if (e.button == 1)
					doAction = false;
			}

			@Override
			public void mouseDown(MouseEvent e)
			{
				if (e.button == 1)
					doAction = true;
			}

			@Override
			public void mouseUp(MouseEvent e)
			{
				if ((e.button == 1) && doAction)
					setCollapsed(!isCollapsed(), true);
			}
		});
	}
	
	/**
	 * Configure layout for child element
	 * 
	 * @param ctrl child control
	 */
	private void configureLayout(Control ctrl)
	{
		GridData gd = new GridData();
		gd.grabExcessHorizontalSpace = true;
		gd.horizontalAlignment = SWT.FILL;
		gd.verticalAlignment = SWT.FILL;
		ctrl.setLayoutData(gd);
	}
	
	/**
	 * Create mouse listener for elements
	 * 
	 * @param pageId property page ID to be opened on double click
	 * @return
	 */
	private MouseListener createMouseListener(final String pageId)
	{
		return new MouseListener() {
			@Override
			public void mouseUp(MouseEvent e)
			{
			}
			
			@Override
			public void mouseDown(MouseEvent e)
			{
			}
			
			@Override
			public void mouseDoubleClick(MouseEvent e)
			{
				editRule(pageId);
			}
		};
	}
	
	/**
	 * Create condition summary control
	 * 
	 * @param parent
	 * @param rule
	 * @return
	 */
	private Control createConditionControl(Composite parent, final EventProcessingPolicyRule rule)
	{
		Composite clientArea = new Composite(parent, SWT.NONE);
		
		clientArea.setBackground(BACKGROUND_COLOR);
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = 0;
		clientArea.setLayout(layout);
		
		boolean needAnd = false;
		createLabel(clientArea, 0, true, "IF", null);
		
		/* source */
		if (rule.getSources().size() > 0)
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleSourceObjects#0");
			addConditionGroupLabel(clientArea, "source object is one of the following:", needAnd, listener);
			
			for(Long id : rule.getSources())
			{
				CLabel clabel = createCLabel(clientArea, 2, false);
				clabel.addMouseListener(listener);
				
				GenericObject object = session.findObjectById(id);
				if (object != null)
				{
					clabel.setText(object.getObjectName());
					clabel.setImage(labelProvider.getImage(object));
				}
				else
				{
					clabel.setText("[" + id.toString() + "]");
				}
			}
			needAnd = true;
		}
		
		/* events */
		if (rule.getEvents().size() > 0)
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleEvents#10");
			addConditionGroupLabel(clientArea, "event code is one of the following:", needAnd, listener);
			
			for(Long code : rule.getEvents())
			{
				CLabel clabel = createCLabel(clientArea, 2, false);
				clabel.addMouseListener(listener);
				
				EventTemplate event = session.findEventTemplateByCode(code);
				if (event != null)
				{
					clabel.setText(event.getName());
					clabel.setImage(StatusDisplayInfo.getStatusImage(event.getSeverity()));
				}
				else
				{
					clabel.setText("<" + code.toString() + ">");
					clabel.setImage(StatusDisplayInfo.getStatusImage(Severity.UNKNOWN));
				}
			}
			needAnd = true;
		}
		
		/* severity */
		if ((rule.getFlags() & EventProcessingPolicyRule.SEVERITY_ANY) != EventProcessingPolicyRule.SEVERITY_ANY)
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleSeverityFilter#20");
			addConditionGroupLabel(clientArea, "event severity is one of the following:", needAnd, listener);
			
			if ((rule.getFlags() & EventProcessingPolicyRule.SEVERITY_NORMAL) != 0)
				addSeverityLabel(clientArea, Severity.NORMAL, listener);

			if ((rule.getFlags() & EventProcessingPolicyRule.SEVERITY_WARNING) != 0)
				addSeverityLabel(clientArea, Severity.WARNING, listener);

			if ((rule.getFlags() & EventProcessingPolicyRule.SEVERITY_MINOR) != 0)
				addSeverityLabel(clientArea, Severity.MINOR, listener);

			if ((rule.getFlags() & EventProcessingPolicyRule.SEVERITY_MAJOR) != 0)
				addSeverityLabel(clientArea, Severity.MAJOR, listener);

			if ((rule.getFlags() & EventProcessingPolicyRule.SEVERITY_CRITICAL) != 0)
				addSeverityLabel(clientArea, Severity.CRITICAL, listener);
			
			needAnd = true;
		}
		
		/* script */
		if ((rule.getScript() != null) && !rule.getScript().isEmpty())
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleFilterScript#30");
			addConditionGroupLabel(clientArea, "the following script returns true:", needAnd, listener);
			
			ScriptEditor scriptEditor = new ScriptEditor(clientArea, SWT.BORDER);
			GridData gd = new GridData();
			gd.horizontalIndent = INDENT * 2;
			gd.horizontalAlignment = SWT.FILL;
			gd.grabExcessHorizontalSpace = true;
			scriptEditor.setLayoutData(gd);
			scriptEditor.setText(rule.getScript());
			scriptEditor.getTextWidget().setEditable(false);
			scriptEditor.getTextWidget().addMouseListener(listener);
		}
		
		return clientArea;
	}
	
	/**
	 * Create label with given text and indent
	 * 
	 * @param parent
	 * @param indent
	 * @param bold
	 * @param text
	 * @return
	 */
	private Label createLabel(Composite parent, int indent, boolean bold, String text, MouseListener mouseListener)
	{
		Label label = new Label(parent, SWT.NONE);
		label.setBackground(BACKGROUND_COLOR);
		label.setFont(bold ? editor.getBoldFont() : editor.getNormalFont());
		label.setText(text);

		GridData gd = new GridData();
		gd.horizontalIndent = INDENT * indent;
		label.setLayoutData(gd);
		
		if (mouseListener != null)
			label.addMouseListener(mouseListener);
		
		return label;
	}

	/**
	 * Create CLable with given indent
	 * 
	 * @param parent
	 * @param indent
	 * @param bold
	 * @return
	 */
	private CLabel createCLabel(Composite parent, int indent, boolean bold)
	{
		CLabel label = new CLabel(parent, SWT.NONE);
		label.setBackground(BACKGROUND_COLOR);
		label.setFont(bold ? editor.getBoldFont() : editor.getNormalFont());

		GridData gd = new GridData();
		gd.horizontalIndent = INDENT * indent;
		label.setLayoutData(gd);
		
		return label;
	}
	
	/**
	 * Add condition group opening text
	 * 
	 * @param parent parent composite
	 * @param title group's title
	 * @param needAnd true if AND clause have to be added
	 */
	private void addConditionGroupLabel(Composite parent, String title, boolean needAnd, MouseListener mouseListener)
	{
		if (needAnd)
			createLabel(parent, 0, true, "AND", null);
		createLabel(parent, 1, false, title, mouseListener);
	}
	
	/**
	 * Add severity label
	 * 
	 * @param parent parent composite
	 * @param severity severity code
	 */
	private void addSeverityLabel(Composite parent, int severity, MouseListener mouseListener)
	{
		CLabel clabel = createCLabel(parent, 2, false);
		clabel.setText(StatusDisplayInfo.getStatusText(severity));
		clabel.setImage(StatusDisplayInfo.getStatusImage(severity));
		clabel.addMouseListener(mouseListener);
	}

	/**
	 * Create action summary control
	 * 
	 * @param parent
	 * @param rule
	 * @return
	 */
	private Control createActionControl(Composite parent, final EventProcessingPolicyRule rule)
	{
		Composite clientArea = new Composite(parent, SWT.NONE);
		
		clientArea.setBackground(BACKGROUND_COLOR);
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = 0;
		clientArea.setLayout(layout);
		
		/* alarm */
		if ((rule.getFlags() & EventProcessingPolicyRule.GENERATE_ALARM) != 0)
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleAlarm#10");
			if (rule.getAlarmSeverity() < Severity.UNMANAGED)
			{
				addActionGroupLabel(clientArea, "Generate alarm", editor.getImageAlarm(), listener);
				
				CLabel clabel = createCLabel(clientArea, 1, false);
				clabel.setImage(StatusDisplayInfo.getStatusImage(rule.getAlarmSeverity()));
				clabel.setText(rule.getAlarmMessage());
				clabel.addMouseListener(listener);
				
				if ((rule.getAlarmKey() != null) && !rule.getAlarmKey().isEmpty())
				{
					createLabel(clientArea, 1, false, "with key \"" + rule.getAlarmKey() + "\"", null);
				}
			}
			else
			{
				addActionGroupLabel(clientArea, "Terminate alarms", editor.getImageTerminate(), listener);
				createLabel(clientArea, 1, false, "with key \"" + rule.getAlarmKey() + "\"", listener);
				if ((rule.getFlags() & EventProcessingPolicyRule.TERMINATE_BY_REGEXP) != 0)
					createLabel(clientArea, 1, false, "(use regular expression for alarm termination)", listener);
			}
		}
		
		/* situation */
		if (rule.getSituationId() != 0)
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleSituation#20");
			addActionGroupLabel(clientArea, "Update situation object", editor.getImageSituation(), listener);
			createLabel(clientArea, 1, false, "instance \"" + rule.getSituationInstance() + "\"", listener);
			createLabel(clientArea, 1, false, "attributes:", listener);
			for(Entry<String, String> e : rule.getSituationAttributes().entrySet())
			{
				createLabel(clientArea, 2, false, e.getKey() + " = \"" + e.getValue() + "\"", listener);
			}
		}
		
		/* actions */
		if (rule.getActions().size() > 0)
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleServerActions#30");
			addActionGroupLabel(clientArea, "Execute the following predefined actions:", editor.getImageExecute(), listener);
			for(Long id : rule.getActions())
			{
				CLabel clabel = createCLabel(clientArea, 1, false);
				clabel.addMouseListener(listener);
				ServerAction action = editor.findActionById(id);
				if (action != null)
				{
					clabel.setText(action.getName());
					clabel.setImage(labelProvider.getImage(action));
				}
				else
				{
					clabel.setText("<" + id.toString() + ">");
				}
			}
		}
		
		/* flags */
		if ((rule.getFlags() & EventProcessingPolicyRule.STOP_PROCESSING) != 0)
		{
			final MouseListener listener = createMouseListener("org.netxms.ui.eclipse.epp.propertypages.RuleAction#1");
			addActionGroupLabel(clientArea, "Stop event processing", editor.getImageStop(), listener);
		}
		
		return clientArea;
	}

	/**
	 * Add condition group opening text
	 * 
	 * @param parent parent composite
	 * @param title group's title
	 * @param needAnd true if AND clause have to be added
	 */
	private void addActionGroupLabel(Composite parent, String title, Image image, MouseListener mouseListener)
	{
		CLabel label = createCLabel(parent, 0, true);
		label.setImage(image);
		label.setText(title);
		label.addMouseListener(mouseListener);
	}

	/* (non-Javadoc)
	 * @see org.eclipse.swt.widgets.Widget#dispose()
	 */
	@Override
	public void dispose()
	{
		labelProvider.dispose();
		super.dispose();
	}

	/**
	 * @return the ruleNumber
	 */
	public int getRuleNumber()
	{
		return ruleNumber;
	}

	/**
	 * @param ruleNumber the ruleNumber to set
	 */
	public void setRuleNumber(int ruleNumber)
	{
		this.ruleNumber = ruleNumber;
		ruleNumberLabel.setText(Integer.toString(ruleNumber));
		leftPanel.layout();
	}

	/**
	 * @param verticalLayout the verticalLayout to set
	 */
	public void setVerticalLayout(boolean verticalLayout, boolean doLayout)
	{
		this.verticalLayout = verticalLayout;
		GridLayout layout = (GridLayout)mainArea.getLayout();
		layout.numColumns = verticalLayout ? 1 : 2;
		if (doLayout)
			layout();
	}
	
	/**
	 * Check if widget is in collapsed state.
	 * 
	 * @return true if widget is in collapsed state
	 */
	public boolean isCollapsed()
	{
		return collapsed;
	}

	/**
	 * Set widget's to collapsed or expanded state
	 *  
	 * @param collapsed true to collapse widget, false to expand
	 * @param doLayout if set to true, view layout will be updated
	 */
	public void setCollapsed(boolean collapsed, boolean doLayout)
	{
		this.collapsed = collapsed;
		expandButton.setImage(collapsed ? editor.getImageExpand() : editor.getImageCollapse());
		expandButton.setToolTipText(collapsed ? "Expand rule" : "Collapse rule");
		mainArea.setVisible(!collapsed);
		((GridData)mainArea.getLayoutData()).exclude = collapsed;
		((GridData)leftPanel.getLayoutData()).verticalSpan = collapsed ? 1 : 2;
		if (doLayout)
			editor.updateEditorAreaLayout();
	}
	
	/**
	 * Edit rule's condition
	 */
	private void editRule(String pageId)
	{
		PropertyDialog dlg = PropertyDialog.createDialogOn(editor.getSite().getShell(), pageId, this);
		if (dlg != null)
		{
			modified = false;
			dlg.open();
			if (modified)
			{
				headerLabel.setText(rule.getComments());
				condition.replaceClientArea();
				action.replaceClientArea();
				editor.updateEditorAreaLayout();
				editor.setModified(true);
			}
		}
	}

	/**
	 * @return the rule
	 */
	public EventProcessingPolicyRule getRule()
	{
		return rule;
	}

	/**
	 * @return the modified
	 */
	public boolean isModified()
	{
		return modified;
	}

	/**
	 * @param modified the modified to set
	 */
	public void setModified(boolean modified)
	{
		this.modified = modified;
	}

	/**
	 * @return the editor
	 */
	public EventProcessingPolicyEditor getEditorView()
	{
		return editor;
	}
	
	/**
	 * Process mouse click on rule number
	 * 
	 * @param e mouse event
	 */
	private void processRuleMouseEvent(MouseEvent e)
	{
		boolean ctrlPressed = (e.stateMask & SWT.CTRL) != 0;
		boolean shiftPressed = (e.stateMask & SWT.SHIFT) != 0;
		
		if (ctrlPressed)
		{
			editor.addToSelection(this, false);
		}
		else if (shiftPressed)
		{
			editor.addToSelection(this, true);
		}
		else
		{
			editor.setSelection(this);
		}
	}

	/**
	 * @return the selected
	 */
	public boolean isSelected()
	{
		return selected;
	}

	/**
	 * Set selection status
	 * 
	 * @param selected the selected to set
	 */
	public void setSelected(boolean selected)
	{
		this.selected = selected;
		leftPanel.setBackground(selected ? SELECTED_TITLE_BACKGROUND_COLOR : TITLE_BACKGROUND_COLOR);
		for(Control c : leftPanel.getChildren())
			c.setBackground(selected ? SELECTED_TITLE_BACKGROUND_COLOR : TITLE_BACKGROUND_COLOR);
		leftPanel.redraw();
	}
}
