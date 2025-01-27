/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2022 Victor Kirhenshtein
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
package org.netxms.nxmc.modules.dashboards.views;

import org.eclipse.swt.SWT;
import org.netxms.client.objects.AbstractObject;
import org.netxms.client.objects.Dashboard;
import org.netxms.nxmc.modules.dashboards.widgets.DashboardControl;
import org.netxms.nxmc.resources.ResourceManager;

/**
 * Context dashboard view
 */
public class ContextDashboardView extends AbstractDashboardView
{
   private Dashboard dashboard;

   /**
    * @param name
    * @param image
    * @param id
    * @param hasFilter
    */
   public ContextDashboardView(Dashboard dashboard)
   {
      super(dashboard.getObjectName(), ResourceManager.getImageDescriptor("icons/object-views/dashboard.png"), "ContextDashboard." + dashboard.getObjectId());
      this.dashboard = dashboard;
   }

   /**
    * @see org.netxms.nxmc.modules.objects.views.ObjectView#isValidForContext(java.lang.Object)
    */
   @Override
   public boolean isValidForContext(Object context)
   {
      return (context != null) && (context instanceof AbstractObject) && ((AbstractObject)context).hasDashboard(dashboard.getObjectId());
   }

   /**
    * @see org.netxms.nxmc.modules.objects.views.ObjectView#onObjectChange(org.netxms.client.objects.AbstractObject)
    */
   @Override
   protected void onObjectChange(AbstractObject object)
   {
      if (dbc != null)
         dbc.dispose();
      dbc = new DashboardControl(viewArea, SWT.NONE, dashboard, object, this, null, false);
      viewArea.layout(true, true);
   }

   /**
    * @see org.netxms.nxmc.base.views.View#getPriority()
    */
   @Override
   public int getPriority()
   {
      return (dashboard.getDisplayPriority() > 0) ? dashboard.getDisplayPriority() : super.getPriority();
   }
}
