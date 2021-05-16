/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2020 Raden Solutions
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
package org.netxms.nxmc.base.windows;

import org.eclipse.jface.window.Window;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.DisposeEvent;
import org.eclipse.swt.events.DisposeListener;
import org.eclipse.swt.graphics.Point;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;
import org.eclipse.swt.widgets.Layout;
import org.eclipse.swt.widgets.Shell;
import org.netxms.nxmc.PreferenceStore;
import org.netxms.nxmc.base.views.View;
import org.netxms.nxmc.base.views.ViewContainer;

/**
 * Window that holds pop out view
 */
public class PopOutViewWindow extends Window
{
   private View view;

   /**
    * @param parentShell
    */
   public PopOutViewWindow(View view)
   {
      super((Shell)null);
      this.view = view;
   }

   /**
    * @see org.eclipse.jface.window.Window#configureShell(org.eclipse.swt.widgets.Shell)
    */
   @Override
   protected void configureShell(Shell newShell)
   {
      super.configureShell(newShell);
      newShell.setText(view.getFullName());
      Point shellSize = PreferenceStore.getInstance().getAsPoint("PopupWindowSize." + view.getBaseId(), null);
      if (shellSize != null)
         newShell.setSize(shellSize);
   }

   /**
    * @see org.eclipse.jface.window.Window#getLayout()
    */
   @Override
   protected Layout getLayout()
   {
      return new FillLayout();
   }

   /**
    * @see org.eclipse.jface.window.Window#createContents(org.eclipse.swt.widgets.Composite)
    */
   @Override
   protected Control createContents(Composite parent)
   {
      ViewContainer viewContainer = new ViewContainer(this, null, parent, false, false) {
         @Override
         public Point computeSize(int wHint, int hHint, boolean changed)
         {
            Point size = super.computeSize(wHint, hHint, changed);
            if ((wHint == SWT.DEFAULT) && (size.x < 100))
               size.x = 800;
            if ((hHint == SWT.DEFAULT) && (size.y < 100))
               size.y = 600;
            return size;
         }
      };
      viewContainer.setContextAware(false); // Preserve view's context if any
      viewContainer.setView(view);
      parent.addDisposeListener(new DisposeListener() {
         @Override
         public void widgetDisposed(DisposeEvent e)
         {
            PreferenceStore.getInstance().set("PopupWindowSize." + view.getBaseId(), getShell().getSize());
         }
      });
      return parent;
   }
}
