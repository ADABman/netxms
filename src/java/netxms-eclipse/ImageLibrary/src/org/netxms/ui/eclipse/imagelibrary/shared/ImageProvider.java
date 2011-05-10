package org.netxms.ui.eclipse.imagelibrary.shared;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.swt.SWTException;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.PlatformUI;
import org.eclipse.ui.plugin.AbstractUIPlugin;
import org.netxms.api.client.images.LibraryImage;
import org.netxms.client.NXCException;
import org.netxms.client.NXCSession;
import org.netxms.ui.eclipse.imagelibrary.Activator;
import org.netxms.ui.eclipse.shared.ConsoleSharedData;

public class ImageProvider
{
	private static ImageProvider instance = new ImageProvider();

	private static final Map<UUID, Image> cache = Collections.synchronizedMap(new HashMap<UUID, Image>());
	private static final Map<UUID, LibraryImage> libraryIndex = Collections.synchronizedMap(new HashMap<UUID, LibraryImage>());

	public static ImageProvider getInstance()
	{
		return instance;
	}

	private final Image missingImage;
	private final Set<ImageUpdateListener> updateListeners;

	private List<LibraryImage> imageLibrary;

	private ImageProvider()
	{
		final ImageDescriptor imageDescriptor = AbstractUIPlugin.imageDescriptorFromPlugin(Activator.PLUGIN_ID, "icons/missing.png");
		missingImage = imageDescriptor.createImage();
		updateListeners = new HashSet<ImageUpdateListener>();
	}

	public void addUpdateListener(final ImageUpdateListener listener)
	{
		updateListeners.add(listener);
	}

	public void removeUpdateListener(final ImageUpdateListener listener)
	{
		updateListeners.remove(listener);
	}

	public void syncMetaData() throws NXCException, IOException
	{
		final Display display = PlatformUI.getWorkbench().getDisplay();

		final NXCSession session = (NXCSession)ConsoleSharedData.getSession();
		imageLibrary = session.getImageLibrary();
		for(final LibraryImage libraryImage : imageLibrary)
		{
			libraryIndex.put(libraryImage.getGuid(), libraryImage);
			if (!libraryImage.isComplete())
			{
				try
				{
					final LibraryImage completeLibraryImage = session.getImage(libraryImage.getGuid());
					final ByteArrayInputStream stream = new ByteArrayInputStream(completeLibraryImage.getBinaryData());
					try
					{
						final Image image = new Image(display, stream);
						cache.put(completeLibraryImage.getGuid(), image);
					}
					catch(SWTException e)
					{
						// TODO: log?
						cache.put(completeLibraryImage.getGuid(), missingImage);
					}

					for(final ImageUpdateListener listener : updateListeners)
					{
						listener.imageUpdated(completeLibraryImage.getGuid());
					}
				}
				catch(Exception e)
				{
					// TODO: log
				}
			}
		}
	}

	public Image getImage(final UUID guid)
	{
		final Image image;
		if (cache.containsKey(guid))
		{
			image = cache.get(guid);
		}
		else
		{
			image = missingImage;
		}
		return image;
	}

	/**
	 * Get image library object
	 * 
	 * @param guid
	 *           image GUID
	 * @return image library element or null if there are no image with given
	 *         GUID
	 */
	public LibraryImage getLibraryImageObject(final UUID guid)
	{
		return libraryIndex.get(guid);
	}

	public List<LibraryImage> getImageLibrary()
	{
		return imageLibrary;
	}
}
