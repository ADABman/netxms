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
package org.netxms.ui.eclipse.osm.tools;

import java.io.File;
import java.net.MalformedURLException;
import java.net.URISyntaxException;
import java.net.URL;

import org.eclipse.core.runtime.Platform;
import org.eclipse.jface.resource.ImageDescriptor;
import org.eclipse.osgi.service.datalocation.Location;
import org.eclipse.swt.SWT;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.graphics.ImageData;
import org.eclipse.swt.graphics.ImageLoader;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.graphics.Rectangle;
import org.eclipse.swt.widgets.Display;
import org.netxms.client.GeoLocation;
import org.netxms.ui.eclipse.osm.Activator;
import org.netxms.ui.eclipse.osm.GeoLocationCache;

/**
 * Map Loader - loads geographic map from tile server. Uses cached tiles when possible.
 */
public class MapLoader
{
	private static Image missingTile = null; 
	private static Image borderTile = null; 
	
	/**
	 * @param location
	 * @param zoom
	 * @return
	 */
	private static Point tileFromLocation(double lat, double lon, int zoom)
	{
		int x = (int)Math.floor((lon + 180) / 360 * (1 << zoom));
		int y = (int)Math.floor((1 - Math.log(Math.tan(Math.toRadians(lat)) + 1 / Math.cos(Math.toRadians(lat))) / Math.PI) / 2 * (1 << zoom));
		return new Point(x, y);
	}
	
	/**
	 * @param x
	 * @param z
	 * @return
	 */
	private static double longitudeFromTile(int x, int z)
	{
		return x / Math.pow(2.0, z) * 360.0 - 180;
	}
	 
	/**
	 * @param y
	 * @param z
	 * @return
	 */
	private static double latitudeFromTile(int y, int z) 
	{
		double n = Math.PI - (2.0 * Math.PI * y) / Math.pow(2.0, z);
		return Math.toDegrees(Math.atan(Math.sinh(n)));
	}
	
	/**
	 * get image for missing tile
	 * 
	 * @return
	 */
	private static Image getMissingTileImage()
	{
		if (missingTile == null)
			missingTile = Activator.getImageDescriptor("icons/missing_tile.png").createImage();
		return missingTile;
	}
	
	/**
	 * get image for border tile (tile out of map boundaries)
	 * 
	 * @return
	 */
	private static Image getBorderTileImage()
	{
		if (borderTile == null)
			borderTile = Activator.getImageDescriptor("icons/border_tile.png").createImage();
		return borderTile;
	}
	
	/**
	 * @param zoom
	 * @param x
	 * @param y
	 * @return
	 */
	private static Image loadTile(int zoom, int x, int y)
	{
		// check x and y for validity
		int maxTileNum = (1 << zoom) - 1;
		if ((x < 0) || (y < 0) || (x > maxTileNum) || (y > maxTileNum))
			return getBorderTileImage();
		
		URL url = null;
		try
		{
			url = new URL("http://tile.openstreetmap.org/" + zoom + "/" + x + "/" + y + ".png");
		}
		catch(MalformedURLException e)
		{
			// TODO Auto-generated catch block
			e.printStackTrace();
			return null;
		}
		
		final ImageDescriptor id = ImageDescriptor.createFromURL(url);
		final Image image = id.createImage(false);
		if (image != null)
		{
			// save to cache
			File imageFile = buildCacheFileName(zoom, x, y);
			imageFile.getParentFile().mkdirs();
			ImageLoader imageLoader = new ImageLoader();
			imageLoader.data = new ImageData[] { image.getImageData() };
			imageLoader.save(imageFile.getAbsolutePath(), SWT.IMAGE_PNG);
		}
		
		return image;
	}
	
	/**
	 * @param zoom
	 * @param x
	 * @param y
	 * @return
	 */
	private static File buildCacheFileName(int zoom, int x, int y)
	{
		Location loc = Platform.getInstanceLocation();
		File targetDir;
		try
		{
			targetDir = new File(loc.getURL().toURI());
		}
		catch(URISyntaxException e)
		{
			targetDir = new File(loc.getURL().getPath());
		}
		
		StringBuilder sb = new StringBuilder("OSM");
		sb.append(File.separatorChar);
		sb.append(zoom);
		sb.append(File.separatorChar);
		sb.append(x);
		sb.append(File.separatorChar);
		sb.append(y);
		sb.append(".png");

		return new File(targetDir, sb.toString());
	}
	
	/**
	 * @param zoom
	 * @param x
	 * @param y
	 * @return
	 */
	private static Image loadTileFromCache(int zoom, int x, int y)
	{
		try
		{
			File imageFile = buildCacheFileName(zoom, x, y);
			if (imageFile.canRead())
			{
				return new Image(Display.getDefault(), imageFile.getAbsolutePath());
			}
			return null;
		}
		catch(Exception e)
		{
			e.printStackTrace(); /* FIXME: for debug purposes only */
			return null;
		}
	}
	
	/**
	 * @param zoom
	 * @param x
	 * @param y
	 * @return
	 */
	public static Image getTile(int zoom, int x, int y)
	{
		Image tile = loadTileFromCache(zoom, x, y);
		if (tile == null)
		{
			tile = loadTile(zoom, x, y);
		}
		return (tile != null) ? tile : getMissingTileImage();
	}
	
	/**
	 * @param mapSize
	 * @param centerPoint
	 * @param zoom
	 * @return
	 */
	public static Rectangle calculateBoundingBox(Point mapSize, GeoLocation centerPoint, int zoom)
	{
		Area coverage = GeoLocationCache.calculateCoverage(mapSize, centerPoint, zoom);
		Point topLeft = tileFromLocation(coverage.getxLow(), coverage.getyLow(), zoom);
		Point bottomRight = tileFromLocation(coverage.getxHigh(), coverage.getyHigh(), zoom);
		return new Rectangle(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y);
	}
	
	/**
	 * @param mapSize
	 * @param centerPoint
	 * @param zoom
	 * @return
	 */
	public static TileSet getAllTiles(Point mapSize, GeoLocation centerPoint, int zoom)
	{
		if ((mapSize.x < 32) || (mapSize.y < 32))
			return null;
		
		Area coverage = GeoLocationCache.calculateCoverage(mapSize, centerPoint, zoom);
		Point bottomLeft = tileFromLocation(coverage.getxLow(), coverage.getyLow(), zoom);
		Point topRight = tileFromLocation(coverage.getxHigh(), coverage.getyHigh(), zoom);
		
		Image[][] tiles = new Image[bottomLeft.y - topRight.y + 1][topRight.x - bottomLeft.x + 1];
		
		int x = bottomLeft.x;
		int y = topRight.y;
		int l = (bottomLeft.y - topRight.y + 1) * (topRight.x - bottomLeft.x + 1);
		for(int i = 0; i < l; i++)
		{
			tiles[y - topRight.y][x - bottomLeft.x] = getTile(zoom, x, y);
			x++;
			if (x > topRight.x)
			{
				x = bottomLeft.x;
				y++;
			}
		}
		
		double lat = latitudeFromTile(topRight.y, zoom);
		double lon = longitudeFromTile(bottomLeft.x, zoom);
		Point realTopLeft = GeoLocationCache.coordinateToDisplay(new GeoLocation(lat, lon), zoom);
		Point reqTopLeft = GeoLocationCache.coordinateToDisplay(new GeoLocation(coverage.getxHigh(), coverage.getyLow()), zoom);
		
		return new TileSet(tiles, realTopLeft.x - reqTopLeft.x, realTopLeft.y - reqTopLeft.y);
	}
	
	/**
	 * Returns true if given image is internally generated (not downloaded)
	 * 
	 * @param image
	 * @return
	 */
	static boolean isInternalImage(Image image)
	{
		return (image == missingTile) || (image == borderTile);
	}
}
