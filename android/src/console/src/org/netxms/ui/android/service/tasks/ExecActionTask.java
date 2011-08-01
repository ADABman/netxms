/**
 * 
 */
package org.netxms.ui.android.service.tasks;

import org.netxms.client.NXCSession;
import org.netxms.ui.android.service.ClientConnectorService;
import android.os.AsyncTask;

/**
 * Async task for executing agent's action
 */
public class ExecActionTask extends AsyncTask<Object, Void, Exception>
{
	private ClientConnectorService service;
	
	/* (non-Javadoc)
	 * @see android.os.AsyncTask#doInBackground(Params[])
	 */
	@Override
	protected Exception doInBackground(Object... params)
	{
		service = (ClientConnectorService)params[3];
		try
		{
			((NXCSession)params[0]).executeAction((Long)params[1], (String)params[2]);
		}
		catch(Exception e)
		{
			return e;
		}
		return null;
	}

	/* (non-Javadoc)
	 * @see android.os.AsyncTask#onPostExecute(java.lang.Object)
	 */
	@Override
	protected void onPostExecute(Exception result)
	{
		if (result == null)
		{
			service.showToast("Action executed successfully");
		}
		else
		{
			service.showToast("Cannot execute action: " + result.getLocalizedMessage());
		}
	}
}
