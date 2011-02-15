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
package org.netxms.ui.eclipse.epp.views;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Platform;
import org.eclipse.core.runtime.Status;
import org.eclipse.jface.action.Action;
import org.eclipse.jface.action.IMenuManager;
import org.eclipse.jface.action.IToolBarManager;
import org.eclipse.jface.action.Separator;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.ScrolledComposite;
import org.eclipse.swt.events.ControlAdapter;
import org.eclipse.swt.events.ControlEvent;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.Font;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.IActionBars;
import org.eclipse.ui.ISaveablePart;
import org.eclipse.ui.part.ViewPart;
import org.eclipse.ui.progress.UIJob;
import org.netxms.api.client.SessionNotification;
import org.netxms.client.NXCListener;
import org.netxms.client.NXCNotification;
import org.netxms.client.NXCSession;
import org.netxms.client.ServerAction;
import org.netxms.client.events.EventProcessingPolicy;
import org.netxms.client.events.EventProcessingPolicyRule;
import org.netxms.client.events.EventTemplate;
import org.netxms.ui.eclipse.epp.Activator;
import org.netxms.ui.eclipse.epp.widgets.RuleEditor;
import org.netxms.ui.eclipse.epp.widgets.helpers.ImageFactory;
import org.netxms.ui.eclipse.jobs.ConsoleJob;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;
import org.netxms.ui.eclipse.shared.SharedIcons;

/**
 * Event processing policy editor
 *
 */
public class EventProcessingPolicyEditor extends ViewPart implements ISaveablePart
{
	public static final String ID = "org.netxms.ui.eclipse.epp.view.policy_editor";
	public static final String JOB_FAMILY = "PolicyEditorJob";

	private static final Color BACKGROUND_COLOR = new Color(Display.getDefault(), 255, 255, 255);
	
	private NXCSession session;
	private boolean policyLocked = false;
	private EventProcessingPolicy policy; 
	private NXCListener sessionListener;
	private Map<Long, ServerAction> actions = new HashMap<Long, ServerAction>();
	private ScrolledComposite scroller;
	private Composite dataArea;
	private List<RuleEditor> ruleEditors = new ArrayList<RuleEditor>();
	private boolean verticalLayout = false;
	private boolean modified = false;
	private Set<RuleEditor> selection = new HashSet<RuleEditor>();
	private int lastSelectedRule = -1;
	private List<EventProcessingPolicyRule> clipboard = new ArrayList<EventProcessingPolicyRule>(0);
	
	private Font headerFont;
	private Font normalFont;
	private Font boldFont;
	
	private Image imageAlarm;
	private Image imageExecute;
	private Image imageTerminate;
	private Image imageStop;
	private Image imageCollapse;
	private Image imageExpand;
	private Image imageEdit;
	
	private Action actionHorizontal;
	private Action actionVertical;
	private Action actionSave;
	private Action actionCollapseAll;
	private Action actionExpandAll;
	private Action actionInsertBefore;
	private Action actionInsertAfter;
	private Action actionCut;
	private Action actionCopy;
	private Action actionPaste;
	private Action actionDelete;
	
	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#createPartControl(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	public void createPartControl(Composite parent)
	{
		session = (NXCSession)ConsoleSharedData.getSession();

		// Initiate loading of required plugins if they was not loaded yet
		try
		{
			Platform.getAdapterManager().loadAdapter(new EventTemplate(0), "org.eclipse.ui.model.IWorkbenchAdapter");
			Platform.getAdapterManager().loadAdapter(new ServerAction(0), "org.eclipse.ui.model.IWorkbenchAdapter");
			Platform.getAdapterManager().loadAdapter(session.getTopLevelObjects()[0], "org.eclipse.ui.model.IWorkbenchAdapter");
		}
		catch(Exception e)
		{
		}
		
		imageStop = Activator.getImageDescriptor("icons/stop.png").createImage();
		imageAlarm = Activator.getImageDescriptor("icons/alarm.png").createImage();
		imageExecute = Activator.getImageDescriptor("icons/execute.png").createImage();
		imageTerminate = Activator.getImageDescriptor("icons/terminate.png").createImage();
		imageCollapse = SharedIcons.COLLAPSE.createImage();
		imageExpand = SharedIcons.EXPAND.createImage();
		imageEdit = SharedIcons.EDIT.createImage();
		
		scroller = new ScrolledComposite(parent, SWT.V_SCROLL);
		
		dataArea = new Composite(scroller, SWT.NONE);
		GridLayout layout = new GridLayout();
		layout.marginHeight = 0;
		layout.marginWidth = 0;
		layout.verticalSpacing = 0;
		dataArea.setLayout(layout);
		dataArea.setBackground(BACKGROUND_COLOR);
		
		scroller.setContent(dataArea);
		scroller.setExpandVertical(true);
		scroller.setExpandHorizontal(true);
		scroller.getVerticalBar().setIncrement(20);
		scroller.addControlListener(new ControlAdapter() {
			public void controlResized(ControlEvent e)
			{
				Rectangle r = scroller.getClientArea();
				scroller.setMinSize(dataArea.computeSize(r.width, SWT.DEFAULT));
			}
		});

		headerFont = new Font(parent.getDisplay(), "Verdana", 10, SWT.BOLD);
		normalFont = new Font(parent.getDisplay(), "Verdana", 8, SWT.NORMAL);
		boldFont = new Font(parent.getDisplay(), "Verdana", 8, SWT.BOLD);
		
		sessionListener = new NXCListener() {
			@Override
			public void notificationHandler(SessionNotification n)
			{
				processSessionNotification(n);
			}
		};
		session.addListener(sessionListener);

		createActions();
		contributeToActionBars();
		
		openEventProcessingPolicy();
	}

	/**
	 * Create actions
	 */
	private void createActions()
	{
		actionHorizontal = new Action("&Horizontal layout", Action.AS_RADIO_BUTTON) {
			@Override
			public void run()
			{
				verticalLayout = false;
				updateLayout();
			}
		};
		actionHorizontal.setChecked(!verticalLayout);
		actionHorizontal.setImageDescriptor(Activator.getImageDescriptor("icons/h_layout.gif"));
		
		actionVertical = new Action("&Vertical layout", Action.AS_RADIO_BUTTON) {
			@Override
			public void run()
			{
				verticalLayout = true;
				updateLayout();
			}
		};
		actionVertical.setChecked(verticalLayout);
		actionVertical.setImageDescriptor(Activator.getImageDescriptor("icons/v_layout.gif"));

		actionSave = new Action("&Save policy") {
			@Override
			public void run()
			{
				savePolicy();
			}
		};
		actionSave.setImageDescriptor(SharedIcons.SAVE);
		actionSave.setEnabled(false);

		actionCollapseAll = new Action("&Collapse all") {
			@Override
			public void run()
			{
				setAllRulesCollapsed(true);
			}
		};
		actionCollapseAll.setImageDescriptor(SharedIcons.COLLAPSE_ALL);

		actionExpandAll = new Action("&Expand all") {
			@Override
			public void run()
			{
				setAllRulesCollapsed(false);
			}
		};
		actionExpandAll.setImageDescriptor(SharedIcons.EXPAND_ALL);
		
		actionDelete = new Action("&Delete") {
			@Override
			public void run()
			{
				deleteSelectedRules();
			}
		};
		actionDelete.setImageDescriptor(SharedIcons.DELETE_OBJECT);
		actionDelete.setEnabled(false);

		actionInsertBefore = new Action("Insert &before") {
			@Override
			public void run()
			{
				insertRule(lastSelectedRule - 1);
			}
		};

		actionInsertAfter = new Action("Insert &after") {
			@Override
			public void run()
			{
				insertRule(lastSelectedRule);
			}
		};

		actionCut = new Action("Cu&t") {
			@Override
			public void run()
			{
				cutRules();
			}
		};
		actionCut.setImageDescriptor(SharedIcons.CUT);
		actionCut.setEnabled(false);

		actionCopy = new Action("&Copy") {
			@Override
			public void run()
			{
				copyRules();
			}
		};
		actionCopy.setImageDescriptor(SharedIcons.COPY);
		actionCopy.setEnabled(false);

		actionPaste = new Action("&Paste") {
			@Override
			public void run()
			{
				pasteRules();
			}
		};
		actionPaste.setImageDescriptor(SharedIcons.PASTE);
		actionPaste.setEnabled(false);
	}
	
	/**
	 * Contribute actions to action bar
	 */
	private void contributeToActionBars()
	{
		IActionBars bars = getViewSite().getActionBars();
		fillLocalPullDown(bars.getMenuManager());
		fillLocalToolBar(bars.getToolBarManager());
	}

	/**
	 * Fill local pull-down menu
	 * 
	 * @param manager
	 *           Menu manager for pull-down menu
	 */
	private void fillLocalPullDown(IMenuManager manager)
	{
		manager.add(actionSave);
		manager.add(new Separator());
		manager.add(actionExpandAll);
		manager.add(actionCollapseAll);
		manager.add(new Separator());
		manager.add(actionHorizontal);
		manager.add(actionVertical);
		manager.add(new Separator());
		manager.add(actionInsertBefore);
		manager.add(actionInsertAfter);
		manager.add(new Separator());
		manager.add(actionCut);
		manager.add(actionCopy);
		manager.add(actionPaste);
		manager.add(new Separator());
		manager.add(actionDelete);
	}

	/**
	 * Fill local tool bar
	 * 
	 * @param manager
	 *           Menu manager for local toolbar
	 */
	private void fillLocalToolBar(IToolBarManager manager)
	{
		manager.add(actionSave);
		manager.add(new Separator());
		manager.add(actionExpandAll);
		manager.add(actionCollapseAll);
		manager.add(new Separator());
		manager.add(actionHorizontal);
		manager.add(actionVertical);
		manager.add(new Separator());
		manager.add(actionCut);
		manager.add(actionCopy);
		manager.add(actionPaste);
		manager.add(actionDelete);
	}

	/**
	 * Open event processing policy
	 */
	private void openEventProcessingPolicy()
	{
		ConsoleJob job = new ConsoleJob("Open event processing policy", this, Activator.PLUGIN_ID, JOB_FAMILY) {
			@Override
			protected String getErrorMessage()
			{
				return "Cannot open event processing policy";
			}

			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				List<ServerAction> actions = session.getActions();
				synchronized(EventProcessingPolicyEditor.this.actions)
				{
					for(ServerAction a : actions)
					{
						EventProcessingPolicyEditor.this.actions.put(a.getId(), a);
					}
				}
				
				policy = session.openEventProcessingPolicy();
				policyLocked = true;
				new UIJob("Update rules presentation") {
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						initPolicyEditor();
						return Status.OK_STATUS;
					}
				}.schedule();
			}

			@Override
			protected void jobFailureHandler()
			{
				new UIJob("Close event processing policy editor")
				{
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						EventProcessingPolicyEditor.this.getViewSite().getPage().hideView(EventProcessingPolicyEditor.this);
						return Status.OK_STATUS;
					}
				}.schedule();
			}
		};
		job.start();
	}
	
	/**
	 * Init policy editor
	 */
	private void initPolicyEditor()
	{
		int ruleNumber = 1;
		for(EventProcessingPolicyRule rule : policy.getRules())
		{
			RuleEditor editor = new RuleEditor(dataArea, rule, ruleNumber++, this);
			ruleEditors.add(editor);
			GridData gd = new GridData();
			gd.horizontalAlignment = SWT.FILL;
			gd.grabExcessHorizontalSpace = true;
			editor.setLayoutData(gd);
		}
		dataArea.layout();

		Rectangle r = scroller.getClientArea();
		scroller.setMinSize(dataArea.computeSize(r.width, SWT.DEFAULT));
	}
	
	/**
	 * Update editor's layout
	 */
	private void updateLayout()
	{
		for(RuleEditor editor : ruleEditors)
			editor.setVerticalLayout(verticalLayout, false);
		updateEditorAreaLayout();
	}
	
	/**
	 * Process session notifications
	 * 
	 * @param n notification
	 */
	private void processSessionNotification(SessionNotification n)
	{
		switch(n.getCode())
		{
			case NXCNotification.ACTION_CREATED:
				synchronized(actions)
				{
					actions.put(n.getSubCode(), (ServerAction)n.getObject());
				}
				break;
			case NXCNotification.ACTION_MODIFIED:
				synchronized(actions)
				{
					actions.put(n.getSubCode(), (ServerAction)n.getObject());
				}
				break;
			case NXCNotification.ACTION_DELETED:
				synchronized(actions)
				{
					actions.remove(n.getSubCode());
				}
				break;
		}
	}
	
	/**
	 * Set all rules to collapsed or expanded state
	 * 
	 * @param collapsed true to collapse all, false to expand
	 */
	private void setAllRulesCollapsed(boolean collapsed)
	{
		for(RuleEditor editor : ruleEditors)
			editor.setCollapsed(collapsed, false);
		updateEditorAreaLayout();
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#setFocus()
	 */
	@Override
	public void setFocus()
	{
		dataArea.setFocus();
	}
	
	/**
	 * Save policy to server
	 */
	private void savePolicy()
	{
		actionSave.setEnabled(false);
		new ConsoleJob("Save event processing policy", this, Activator.PLUGIN_ID, JOB_FAMILY) {
			@Override
			protected void runInternal(IProgressMonitor monitor) throws Exception
			{
				session.saveEventProcessingPolicy(policy);
				new UIJob("Update policy editor UI") {
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						modified = false;
						firePropertyChange(PROP_DIRTY);
						return Status.OK_STATUS;
					}
				}.schedule();
			}

			@Override
			protected void jobFinalize()
			{
				new UIJob("Update policy editor UI") {
					@Override
					public IStatus runInUIThread(IProgressMonitor monitor)
					{
						actionSave.setEnabled(modified);
						return Status.OK_STATUS;
					}
				}.schedule();
			}

			@Override
			protected String getErrorMessage()
			{
				return "Cannot save event processing policy";
			}
		}.start();
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.part.WorkbenchPart#dispose()
	 */
	@Override
	public void dispose()
	{
		if (sessionListener != null)
			session.removeListener(sessionListener);
		
		if (policyLocked)
		{
			new ConsoleJob("Close event processing policy", null, Activator.PLUGIN_ID, JOB_FAMILY) {
				@Override
				protected void runInternal(IProgressMonitor monitor) throws Exception
				{
					session.closeEventProcessingPolicy();
				}

				@Override
				protected String getErrorMessage()
				{
					return "Cannot close event processing policy";
				}
			}.start();
		}
		
		headerFont.dispose();
		normalFont.dispose();
		boldFont.dispose();
		
		imageStop.dispose();
		imageAlarm.dispose();
		imageExecute.dispose();
		imageTerminate.dispose();
		imageCollapse.dispose();
		imageExpand.dispose();
		imageEdit.dispose();
		
		super.dispose();
		ImageFactory.clearCache();
	}

	/**
	 * Update entire editor area layout after change in rule editor windget's size 
	 */
	public void updateEditorAreaLayout()
	{
		dataArea.layout();
		Rectangle r = scroller.getClientArea();
		scroller.setMinSize(dataArea.computeSize(r.width, SWT.DEFAULT));
	}
	
	/**
	 * Find server action by ID
	 * 
	 * @param id action id
	 * @return server action object or null
	 */
	public ServerAction findActionById(Long id)
	{
		return actions.get(id);
	}
	
	/**
	 * Find server actions for list of Ids
	 * 
	 * @param idList list of action identifiers
	 * @return list of server actions
	 */
	public Map<Long, ServerAction> findServerActions(List<Long> idList)
	{
		Map<Long, ServerAction> resultSet = new HashMap<Long, ServerAction>();
		for(Long id : idList)
		{
			ServerAction action = actions.get(id);
			if (action != null)
				resultSet.put(id, action);
		}
		return resultSet;
	}
	
	/**
	 * Return complete actions list
	 * 
	 * @return actions list
	 */
	public Collection<ServerAction> getActions()
	{
		return actions.values();
	}

	/**
	 * @return the normalFont
	 */
	public Font getNormalFont()
	{
		return normalFont;
	}

	/**
	 * @return the boldFont
	 */
	public Font getBoldFont()
	{
		return boldFont;
	}

	/**
	 * @return the imageAlarm
	 */
	public Image getImageAlarm()
	{
		return imageAlarm;
	}

	/**
	 * @return the imageExecute
	 */
	public Image getImageExecute()
	{
		return imageExecute;
	}

	/**
	 * @return the imageTerminate
	 */
	public Image getImageTerminate()
	{
		return imageTerminate;
	}

	/**
	 * @return the imageStop
	 */
	public Image getImageStop()
	{
		return imageStop;
	}

	/**
	 * @return the imageCollapse
	 */
	public Image getImageCollapse()
	{
		return imageCollapse;
	}

	/**
	 * @return the imageExpand
	 */
	public Image getImageExpand()
	{
		return imageExpand;
	}

	/**
	 * @return the imageEdit
	 */
	public Image getImageEdit()
	{
		return imageEdit;
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
		actionSave.setEnabled(modified);
		firePropertyChange(PROP_DIRTY);
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.ISaveablePart#doSave(org.eclipse.core.runtime.IProgressMonitor)
	 */
	@Override
	public void doSave(IProgressMonitor monitor)
	{
		try
		{
			session.saveEventProcessingPolicy(policy);
		}
		catch(Exception e)
		{
			MessageDialog.openError(getViewSite().getShell(), "Error", "Cannot save event processing policy: " + e.getMessage());
		}	
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.ISaveablePart#doSaveAs()
	 */
	@Override
	public void doSaveAs()
	{
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.ISaveablePart#isDirty()
	 */
	@Override
	public boolean isDirty()
	{
		return modified;
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.ISaveablePart#isSaveAsAllowed()
	 */
	@Override
	public boolean isSaveAsAllowed()
	{
		return false;
	}

	/* (non-Javadoc)
	 * @see org.eclipse.ui.ISaveablePart#isSaveOnCloseNeeded()
	 */
	@Override
	public boolean isSaveOnCloseNeeded()
	{
		return modified;
	}
	
	/**
	 * Clear selection
	 */
	private void clearSelection()
	{
		for(RuleEditor e : selection)
			e.setSelected(false);
		selection.clear();
		lastSelectedRule = -1;
	}
	
	/**
	 * Set selection to given rule
	 * 
	 * @param e rule editor
	 */
	public void setSelection(RuleEditor e)
	{
		clearSelection();
		addToSelection(e, false);
	}
	
	/**
	 * Add rule to selection
	 * 
	 * @param e rule editor
	 */
	public void addToSelection(RuleEditor e, boolean allFromPrevSelection)
	{
		if (allFromPrevSelection && (lastSelectedRule != -1))
		{
			int direction = Integer.signum(e.getRuleNumber() - lastSelectedRule);
			for(int i = lastSelectedRule + direction; i != e.getRuleNumber(); i += direction)
			{
				RuleEditor r = ruleEditors.get(i - 1);
				selection.add(r);
				r.setSelected(true);
			}
		}
		selection.add(e);
		e.setSelected(true);
		lastSelectedRule = e.getRuleNumber();
		onSelectionChange();
	}
	
	/**
	 * Internal handler for selection change
	 */
	private void onSelectionChange()
	{
		actionDelete.setEnabled(selection.size() > 0);
		actionInsertBefore.setEnabled(selection.size() == 1);
		actionInsertAfter.setEnabled(selection.size() == 1);
		actionCut.setEnabled(selection.size() > 0);
		actionCopy.setEnabled(selection.size() > 0);
		actionPaste.setEnabled((selection.size() == 1) && !clipboard.isEmpty());
	}
	
	/**
	 * Delete selected rules
	 */
	private void deleteSelectedRules()
	{
		for(RuleEditor e : selection)
		{
			policy.deleteRule(e.getRuleNumber() - 1);
			ruleEditors.remove(e);
			e.dispose();
		}
		
		// Renumber rules
		for(int i = 0; i < ruleEditors.size(); i++)
			ruleEditors.get(i).setRuleNumber(i + 1);
		
		selection.clear();
		lastSelectedRule = -1;
		onSelectionChange();
		
		updateEditorAreaLayout();
		setModified(true);
	}

	/**
	 * Insert new rule at given position
	 * 
	 * @param position
	 */
	private void insertRule(int position)
	{
		EventProcessingPolicyRule rule = new EventProcessingPolicyRule();
		policy.insertRule(rule, position);
		
		RuleEditor editor = new RuleEditor(dataArea, rule, position + 1, this);
		ruleEditors.add(position, editor);
		GridData gd = new GridData();
		gd.horizontalAlignment = SWT.FILL;
		gd.grabExcessHorizontalSpace = true;
		editor.setLayoutData(gd);
		
		for(int i = position + 1; i < ruleEditors.size(); i++)
			ruleEditors.get(i).setRuleNumber(i + 1);
		
		if (position < ruleEditors.size() - 1)
			editor.moveAbove(ruleEditors.get(position + 1));
		updateEditorAreaLayout();
		
		setModified(true);
	}
	
	/**
	 * Cut selected rules to internal clipboard
	 */
	private void cutRules()
	{
		clipboard.clear();
		actionPaste.setEnabled(true);
		
		for(RuleEditor e : selection)
		{
			clipboard.add(e.getRule());
			policy.deleteRule(e.getRuleNumber() - 1);
			ruleEditors.remove(e);
			e.dispose();
		}
		
		// Renumber rules
		for(int i = 0; i < ruleEditors.size(); i++)
			ruleEditors.get(i).setRuleNumber(i + 1);
		
		selection.clear();
		lastSelectedRule = -1;
		onSelectionChange();
		
		updateEditorAreaLayout();
		setModified(true);
	}

	/**
	 * Copy selected rules to internal clipboard
	 */
	private void copyRules()
	{
		clipboard.clear();
		actionPaste.setEnabled(true);
		
		for(RuleEditor e : selection)
			clipboard.add(new EventProcessingPolicyRule(e.getRule()));
	}

	/**
	 * Paste rules from internal clipboard
	 */
	private void pasteRules()
	{
		int position = lastSelectedRule;
		for(EventProcessingPolicyRule rule : clipboard)
		{
			policy.insertRule(rule, position);

			RuleEditor editor = new RuleEditor(dataArea, rule, position + 1, this);
			ruleEditors.add(position, editor);
			GridData gd = new GridData();
			gd.horizontalAlignment = SWT.FILL;
			gd.grabExcessHorizontalSpace = true;
			editor.setLayoutData(gd);
			
			if (position < ruleEditors.size() - 1)
				editor.moveAbove(ruleEditors.get(position + 1));
			
			position++;
		}
		
		for(int i = position; i < ruleEditors.size(); i++)
			ruleEditors.get(i).setRuleNumber(i + 1);
		
		updateEditorAreaLayout();
		setModified(true);
	}

	/**
	 * Fill context menu for rule
	 * 
	 * @param manager menu manager
	 */
	public void fillRuleContextMenu(IMenuManager manager)
	{
		manager.add(actionInsertBefore);
		manager.add(actionInsertAfter);
		manager.add(new Separator());
		manager.add(actionCut);
		manager.add(actionCopy);
		manager.add(actionPaste);
		manager.add(new Separator());
		manager.add(actionDelete);
	}
}
