/**
 * 
 */
package org.netxms.ui.eclipse.usermanager.propertypages;

import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Status;
import org.eclipse.core.runtime.jobs.Job;
import org.eclipse.swt.SWT;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Button;
import org.eclipse.swt.widgets.Combo;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Group;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.Text;
import org.eclipse.ui.dialogs.PropertyPage;
import org.eclipse.ui.progress.UIJob;
import org.netxms.client.NXCException;
import org.netxms.client.NXCSession;
import org.netxms.client.NXCUser;
import org.netxms.client.NXCUserDBObject;
import org.netxms.ui.eclipse.shared.NXMCSharedData;
import org.netxms.ui.eclipse.tools.WidgetHelper;
import org.netxms.ui.eclipse.usermanager.Activator;

/**
 * @author Victor
 *
 */
public class Authentication extends PropertyPage
{
	private NXCSession session;
	private NXCUser object;
	private Button checkDisabled;
	private Button checkChangePassword;
	private Button checkFixedPassword;
	private Combo comboAuthMethod;
	private Combo comboMappingMethod;
	private Text textMappingData;
	
	/**
	 * Default constructor
	 */
	public Authentication()
	{
		super();
		session = NXMCSharedData.getInstance().getSession();
	}

	/* (non-Javadoc)
	 * @see org.eclipse.jface.preference.PreferencePage#createContents(org.eclipse.swt.widgets.Composite)
	 */
	@Override
	protected Control createContents(Composite parent)
	{
		Composite dialogArea = new Composite(parent, SWT.NONE);
		object = (NXCUser)getElement().getAdapter(NXCUserDBObject.class);
		
		GridLayout layout = new GridLayout();
		layout.verticalSpacing = WidgetHelper.OUTER_SPACING;
      dialogArea.setLayout(layout);

      Group groupFlags = new Group(dialogArea, SWT.NONE);
      groupFlags.setText("Account Options");
      GridLayout groupFlagsLayout = new GridLayout();
      groupFlags.setLayout(groupFlagsLayout);
		GridData gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		groupFlags.setLayoutData(gridData);
		
      checkDisabled = new Button(groupFlags, SWT.CHECK);
      checkDisabled.setText("Account &disabled");
      checkDisabled.setSelection(object.isDisabled());
		
      checkChangePassword = new Button(groupFlags, SWT.CHECK);
      checkChangePassword.setText("User must &change password at next logon");
      checkChangePassword.setSelection(object.isPasswordChangeNeeded());
		
      checkFixedPassword = new Button(groupFlags, SWT.CHECK);
      checkFixedPassword.setText("User cannot change &password");
      checkFixedPassword.setSelection(object.isPasswordChangeForbidden());
		
      Group groupMethod = new Group(dialogArea, SWT.NONE);
      groupMethod.setText("Authentication Method");
      GridLayout groupMethodLayout = new GridLayout();
      groupMethodLayout.numColumns = 2;
      groupMethod.setLayout(groupMethodLayout);
		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		groupMethod.setLayoutData(gridData);
		
		Label label = new Label(groupMethod, SWT.NONE);
		label.setText("Authentication method:");
		comboAuthMethod = new Combo(groupMethod, SWT.DROP_DOWN | SWT.READ_ONLY);
		comboAuthMethod.add("NetXMS password");
		comboAuthMethod.add("RADIUS");
		comboAuthMethod.add("Certificate");
		comboAuthMethod.select(object.getAuthMethod());
		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		comboAuthMethod.setLayoutData(gridData);
		
		label = new Label(groupMethod, SWT.NONE);
		label.setText("Certificate mapping method:");
		comboMappingMethod = new Combo(groupMethod, SWT.DROP_DOWN | SWT.READ_ONLY);
		comboMappingMethod.add("Subject");
		comboMappingMethod.add("Public key");
		comboMappingMethod.select(object.getCertMappingMethod());
		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		comboMappingMethod.setLayoutData(gridData);

		gridData = new GridData();
		gridData.horizontalAlignment = GridData.FILL;
		gridData.grabExcessHorizontalSpace = true;
		gridData.horizontalSpan = 2;
      textMappingData = WidgetHelper.createLabeledText(groupMethod, SWT.SINGLE | SWT.BORDER, "Certificate mapping data",
                                                       object.getCertMappingData(), gridData);
		
		return dialogArea;
	}
	
	/**
	 * Apply changes
	 * 
	 * @param isApply true if update operation caused by "Apply" button
	 */
	protected void applyChanges(final boolean isApply)
	{
		// Account flags
		int flags = 0;
		if (checkDisabled.getSelection())
			flags |= NXCUserDBObject.DISABLED;
		if (checkChangePassword.getSelection())
			flags |= NXCUserDBObject.CHANGE_PASSWORD;
		if (checkFixedPassword.getSelection())
			flags |= NXCUserDBObject.CANNOT_CHANGE_PASSWORD;
		object.setFlags(flags);
		
		// Authentication
		object.setAuthMethod(comboAuthMethod.getSelectionIndex());
		object.setCertMappingMethod(comboMappingMethod.getSelectionIndex());
		object.setCertMappingData(textMappingData.getText());
		
		if (isApply)
			setValid(false);
		
		new Job("Update user database object") {
			@Override
			protected IStatus run(IProgressMonitor monitor)
			{
				IStatus status;
				
				try
				{
					session.modifyUserDBObject(object, NXCSession.USER_MODIFY_FLAGS | NXCSession.USER_MODIFY_AUTH_METHOD | NXCSession.USER_MODIFY_CERT_MAPPING);
					status = Status.OK_STATUS;
				}
				catch(Exception e)
				{
					status = new Status(Status.ERROR, Activator.PLUGIN_ID, 
					                    (e instanceof NXCException) ? ((NXCException)e).getErrorCode() : 0,
					                    "Cannot update user account: " + e.getMessage(), null);
				}

				if (isApply)
				{
					new UIJob("Update \"Authentication\" property page") {
						@Override
						public IStatus runInUIThread(IProgressMonitor monitor)
						{
							Authentication.this.setValid(true);
							return Status.OK_STATUS;
						}
					}.schedule();
				}

				return status;
			}
		}.schedule();
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
}
