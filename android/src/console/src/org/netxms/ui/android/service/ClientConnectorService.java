/**
 * 
 */
package org.netxms.ui.android.service;

import java.io.IOException;
import java.util.List;
import java.util.Map;
import org.netxms.api.client.SessionListener;
import org.netxms.api.client.SessionNotification;
import org.netxms.base.Logger;
import org.netxms.client.NXCException;
import org.netxms.client.NXCNotification;
import org.netxms.client.NXCSession;
import org.netxms.client.datacollection.DciValue;
import org.netxms.client.events.Alarm;
import org.netxms.client.objects.GenericObject;
import org.netxms.client.objecttools.ObjectTool;
import org.netxms.ui.android.R;
import org.netxms.ui.android.main.activities.AlarmBrowser;
import org.netxms.ui.android.main.activities.HomeScreen;
import org.netxms.ui.android.main.activities.NodeBrowser;
import org.netxms.ui.android.service.helpers.AndroidLoggingFacility;
import org.netxms.ui.android.service.tasks.ConnectTask;
import org.netxms.ui.android.service.tasks.ExecActionTask;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.os.Binder;
import android.os.IBinder;
import android.preference.PreferenceManager;
import android.widget.Toast;

/**
 * Background communication service for NetXMS client.
 * 
 */
public class ClientConnectorService extends Service implements SessionListener
{
	public static final String ACTION_RECONNECT = "org.netxms.ui.android.ACTION_RECONNECT";

	private static final int NOTIFY_CONN_STATUS = 1;
	private static final int NOTIFY_ALARM = 1;

	private String mutex = "MUTEX";
	private Binder binder = new ClientConnectorBinder();
	private NotificationManager notificationManager;
	private NXCSession session = null;
	private boolean connectionInProgress = false;
	private String connectionStatus = "disconnected";
	private Map<Long, Alarm> alarms = null;
	private HomeScreen homeScreen = null;
	private AlarmBrowser alarmBrowser = null;
	private NodeBrowser nodeBrowser = null;

	private List<ObjectTool> objectTools = null;

	/**
	 * Class for clients to access. Because we know this service always runs in
	 * the same process as its clients, we don't need to deal with IPC.
	 */
	public class ClientConnectorBinder extends Binder
	{
		public ClientConnectorService getService()
		{
			return ClientConnectorService.this;
		}
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onCreate()
	 */
	@Override
	public void onCreate()
	{
		super.onCreate();

		Logger.setLoggingFacility(new AndroidLoggingFacility());

		notificationManager = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
		showNotification(1, "NetXMS service started");

		BroadcastReceiver receiver = new BroadcastReceiver()
		{
			@Override
			public void onReceive(Context context, Intent intent)
			{
				Intent i = new Intent(context, ClientConnectorService.class);
				i.setAction(ACTION_RECONNECT);
				context.startService(i);
			}
		};
		registerReceiver(receiver, new IntentFilter(Intent.ACTION_TIME_TICK));

		reconnect();
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onStartCommand(android.content.Intent, int, int)
	 */
	@Override
	public int onStartCommand(Intent intent, int flags, int startId)
	{
		if ((intent.getAction() != null) && intent.getAction().equals(ACTION_RECONNECT))
			reconnect();
		return super.onStartCommand(intent, flags, startId);
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onBind(android.content.Intent)
	 */
	@Override
	public IBinder onBind(Intent intent)
	{
		return binder;
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see android.app.Service#onDestroy()
	 */
	@Override
	public void onDestroy()
	{
		super.onDestroy();
	}

	/**
	 * Show notification
	 * 
	 * @param text
	 */
	public void showNotification(int id, String text)
	{
		Notification n = new Notification(android.R.drawable.stat_notify_sdcard, text, System.currentTimeMillis());
		n.defaults = Notification.DEFAULT_SOUND | Notification.DEFAULT_LIGHTS;

		Intent notifyIntent = new Intent(getApplicationContext(), HomeScreen.class);
		PendingIntent intent = PendingIntent.getActivity(getApplicationContext(), 0, notifyIntent, Intent.FLAG_ACTIVITY_NEW_TASK);
		n.setLatestEventInfo(getApplicationContext(), getString(R.string.notification_title), text, intent);

		notificationManager.notify(id, n);
	}

	/**
	 * Hide notification
	 * 
	 * @param id
	 *           notification id
	 */
	private void hideNotification(int id)
	{
		notificationManager.cancel(id);
	}

	/**
	 * Clear all notifications
	 */
	public void clearNotifications()
	{
		notificationManager.cancelAll();
	}

	/**
	 * Reconnect to server if needed
	 */
	private void reconnect()
	{
		synchronized(mutex)
		{
			if ((session == null) && !connectionInProgress)
			{
				connectionInProgress = true;
				setConnectionStatus("connecting...");
				SharedPreferences sp = PreferenceManager.getDefaultSharedPreferences(this);
				new ConnectTask(this).execute(sp.getString("connection.server", ""), sp.getString("connection.login", ""),
						sp.getString("connection.password", ""));
			}
		}
	}

	/**
	 * Called by connect task after successful connection
	 * 
	 * @param session
	 *           new session object
	 */
	public void onConnect(NXCSession session, Map<Long, Alarm> alarms)
	{
		synchronized(mutex)
		{
			connectionInProgress = false;
			this.session = session;
			this.alarms = alarms;
			session.addListener(this);
		}
	}

	/**
	 * Called by connect task or session notification listener after unsuccessful
	 * connection or disconnect
	 */
	public void onDisconnect()
	{
		synchronized(mutex)
		{
			if (session != null)
			{
				session.removeListener(this);
				hideNotification(NOTIFY_ALARM);
				showNotification(NOTIFY_CONN_STATUS, getString(R.string.notify_disconnected));
			}
			connectionInProgress = false;
			session = null;
			alarms = null;
		}
	}

	/**
	 * Process alarm change
	 * 
	 * @param alarm
	 */
	private void processAlarmChange(Alarm alarm)
	{
		synchronized(mutex)
		{
			if (alarms != null)
				alarms.put(alarm.getId(), alarm);
		}
		
		syncObjectIfMissing(alarm.getSourceObjectId());

		if (alarmBrowser != null)
		{
			alarmBrowser.runOnUiThread(new Runnable()
			{
				public void run()
				{
					alarmBrowser.refreshList();
				}
			});
		}

		GenericObject object = session.findObjectById(alarm.getSourceObjectId());
		showNotification(NOTIFY_ALARM, ((object != null) ? object.getObjectName() : "<unknown>") + ": " + alarm.getMessage());
	}

	/**
	 * Process alarm change
	 * 
	 * @param alarm
	 */
	private void processAlarmDelete(long id)
	{
		synchronized(mutex)
		{
			if (alarms != null)
				alarms.remove(id);
		}

		if (alarmBrowser != null)
		{
			alarmBrowser.runOnUiThread(new Runnable()
			{
				public void run()
				{
					alarmBrowser.refreshList();
				}
			});
		}
	}
	
	/**
	 * Sync object with given ID if it is missing
	 * @param objectId
	 */
	private void syncObjectIfMissing(final long objectId)
	{
		if (session.findObjectById(objectId) == null)
		{
			new Thread("Background object sync") {
				@Override
				public void run()
				{
					try
					{
						session.syncObjectSet(new long[] { objectId }, false, NXCSession.OBJECT_SYNC_NOTIFY);
					}
					catch(Exception e)
					{
					}
				}
			}.start();
		}
	}

	/**
	 * @param object
	 */
	private void processObjectUpdate(GenericObject object)
	{
		synchronized(mutex)
		{
			if (this.alarmBrowser != null)
			{
				alarmBrowser.runOnUiThread(new Runnable()
				{
					public void run()
					{
						alarmBrowser.refreshList();
					}
				});
			}
			if (this.nodeBrowser != null)
			{
				nodeBrowser.runOnUiThread(new Runnable()
				{
					public void run()
					{
						nodeBrowser.refreshList();
					}
				});
			}
		}
	}

	/*
	 * (non-Javadoc)
	 * 
	 * @see
	 * org.netxms.api.client.SessionListener#notificationHandler(org.netxms.api
	 * .client.SessionNotification)
	 */
	@Override
	public void notificationHandler(SessionNotification n)
	{
		switch(n.getCode())
		{
			case SessionNotification.CONNECTION_BROKEN:
			case SessionNotification.SERVER_SHUTDOWN:
				onDisconnect();
			case NXCNotification.NEW_ALARM:
			case NXCNotification.ALARM_CHANGED:
				processAlarmChange((Alarm)n.getObject());
				break;
			case NXCNotification.ALARM_DELETED:
			case NXCNotification.ALARM_TERMINATED:
				processAlarmDelete(((Alarm)n.getObject()).getId());
				break;
			case NXCNotification.OBJECT_CHANGED:
			case NXCNotification.OBJECT_SYNC_COMPLETED:
				processObjectUpdate((GenericObject)n.getObject());
				break;
			default:
				break;
		}
	}

	/**
	 * Get list of active alarms
	 * 
	 * @return list of active alarms
	 */
	public Alarm[] getAlarms()
	{
		Alarm[] a;
		synchronized(mutex)
		{
			if (alarms != null)
				a = alarms.values().toArray(new Alarm[alarms.size()]);
			else
				a = new Alarm[0];
		}
		return a;
	}

	/**
	 * @param id
	 * @return
	 */
	public GenericObject findObjectById(long id)
	{
		// we can't search without active session
		if (session == null)
			return null;

		GenericObject obj = session.findObjectById(id);
		// if we don't have object - probably we never synced it (initial alarm
		// viewer run?)
		// try to retreive it
		if (obj == null)
		{
			long[] list = { id };
			try
			{
				session.syncObjectSet(list, false, NXCSession.OBJECT_SYNC_NOTIFY);
			}
			catch(NXCException e)
			{
			}
			catch(IOException e)
			{
			}
		}
		return obj;
	}

	/**
	 * @param id
	 */
	public void teminateAlarm(long id)
	{
		try
		{
			session.terminateAlarm(id);
		}
		catch(NXCException e)
		{
		}
		catch(IOException e)
		{
		}
	}

	/**
	 * @param id
	 * @param state
	 */
	public void setObjectMgmtState(long id, boolean state)
	{
		try
		{
			session.setObjectManaged(id, state);
		}
		catch(NXCException e)
		{
		}
		catch(IOException e)
		{
		}
	}

	/**
	 * @param objectId
	 * @return
	 */
	public DciValue[] getLastValues(long objectId)
	{
		try
		{
			return session.getLastValues(objectId);
		}
		catch(Exception e)
		{
			return new DciValue[0];
		}
	}
	
	/**
	 * 
	 */
	public void loadTools()
	{
		 try
		{
			this.objectTools = session.getObjectTools();
		}
		catch(Exception e)
		{
			this.objectTools = null;
		}
	}
	
	/**
	 * Execute agent action. Communication with server will be done in separate worker thread.
	 * 
	 * @param objectId
	 * @param action
	 */
	public void executeAction(long objectId, String action)
	{
		new ExecActionTask().execute(new Object[] { session, objectId, action, this });
	}
	
	/**
	 * @return
	 */
	public List<ObjectTool> getTools()
	{
		return this.objectTools;
	}

	/**
	 * @param homeScreen
	 */
	public void registerHomeScreen(HomeScreen homeScreen)
	{
		this.homeScreen = homeScreen;
	}

	/**
	 * @param browser
	 */
	public void registerAlarmBrowser(AlarmBrowser browser)
	{
		this.alarmBrowser = browser;
	}

	/**
	 * @param browser
	 */
	public void registerNodeBrowser(NodeBrowser browser)
	{
		this.nodeBrowser = browser;
	}

	/**
	 * @return the connectionStatus
	 */
	public String getConnectionStatus()
	{
		return connectionStatus;
	}

	/**
	 * @param connectionStatus
	 *           the connectionStatus to set
	 */
	public void setConnectionStatus(final String connectionStatus)
	{
		this.connectionStatus = connectionStatus;
		if (homeScreen != null)
		{
			homeScreen.runOnUiThread(new Runnable() {
				@Override
				public void run()
				{
					homeScreen.setStatusText(connectionStatus);
				}
			});
		}
	}

	/**
	 * @return the session
	 */
	public NXCSession getSession()
	{
		return session;
	}
	
	/**
	 * Show toast with given text
	 * 
	 * @param text message text
	 */
	public void showToast(final String text)
	{
		Toast.makeText(getApplicationContext(), text, Toast.LENGTH_SHORT).show();
	}
}
