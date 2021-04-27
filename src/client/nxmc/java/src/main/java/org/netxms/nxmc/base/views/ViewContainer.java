/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2021 Raden Solutions
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
package org.netxms.nxmc.base.views;

import org.eclipse.jface.window.Window;
import org.eclipse.swt.SWT;
import org.eclipse.swt.events.SelectionAdapter;
import org.eclipse.swt.events.SelectionEvent;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.layout.GridData;
import org.eclipse.swt.layout.GridLayout;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Label;
import org.eclipse.swt.widgets.ToolBar;
import org.eclipse.swt.widgets.ToolItem;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.base.windows.PopOutViewWindow;
import org.netxms.nxmc.resources.SharedIcons;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * View container - holds single view
 */
public class ViewContainer extends Composite
{
   private static final Logger logger = LoggerFactory.getLogger(ViewContainer.class);

   private Window window;
   private Perspective perspective;
   private View view;
   private Object context;
   private ToolBar viewControlBar;
   private Composite viewArea;

   /**
    * Create new view stack.
    *
    * @param window owning window
    * @param perspective owning perspective
    * @param parent parent composite
    * @param enableViewExtraction enable/disable view extraction into separate window
    * @param enableViewPinning nable/disable view extraction into "Pinboard" perspective
    */
   public ViewContainer(Window window, Perspective perspective, Composite parent, boolean enableViewExtraction, boolean enableViewPinning)
   {
      super(parent, SWT.BORDER);
      this.window = window;
      this.perspective = perspective;

      GridLayout layout = new GridLayout();
      layout.verticalSpacing = 0;
      layout.marginHeight = 0;
      layout.marginWidth = 0;
      layout.numColumns = 1;
      setLayout(layout);

      viewControlBar = new ToolBar(this, SWT.FLAT | SWT.WRAP | SWT.RIGHT);
      GridData gd = new GridData();
      gd.horizontalAlignment = SWT.RIGHT;
      gd.grabExcessHorizontalSpace = true;
      viewControlBar.setLayoutData(gd);

      ToolItem refreshView = new ToolItem(viewControlBar, SWT.PUSH);
      refreshView.setImage(SharedIcons.IMG_REFRESH);
      refreshView.addSelectionListener(new SelectionAdapter() {
         @Override
         public void widgetSelected(SelectionEvent e)
         {
            if (view != null)
            {
               view.refresh();
            }
         }
      });

      if (enableViewPinning)
      {
         ToolItem pinView = new ToolItem(viewControlBar, SWT.PUSH);
         pinView.setImage(SharedIcons.IMG_PIN);
         pinView.addSelectionListener(new SelectionAdapter() {
            @Override
            public void widgetSelected(SelectionEvent e)
            {
               if (view != null)
               {
                  View clone = view.cloneView();
                  if (clone != null)
                  {
                     Registry.getMainWindow().pinView(clone);
                  }
               }
            }
         });
      }
      if (enableViewExtraction)
      {
         ToolItem popOutView = new ToolItem(viewControlBar, SWT.PUSH);
         popOutView.setImage(SharedIcons.IMG_POP_OUT);
         popOutView.addSelectionListener(new SelectionAdapter() {
            @Override
            public void widgetSelected(SelectionEvent e)
            {
               if (view != null)
               {
                  View clone = view.cloneView();
                  if (clone != null)
                  {
                     PopOutViewWindow window = new PopOutViewWindow(clone);
                     window.open();
                  }
               }
            }
         });
      }

      Label separator = new Label(this, SWT.SEPARATOR | SWT.HORIZONTAL);
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      separator.setLayoutData(gd);

      viewArea = new Composite(this, SWT.NONE);
      viewArea.setLayout(new FillLayout());
      gd = new GridData();
      gd.horizontalAlignment = SWT.FILL;
      gd.verticalAlignment = SWT.FILL;
      gd.grabExcessHorizontalSpace = true;
      gd.grabExcessVerticalSpace = true;
      viewArea.setLayoutData(gd);
   }

   /**
    * Set view to show in this container. Will dispose existing view if any.
    *
    * @param view view to show
    */
   public void setView(View view)
   {
      if (this.view != null)
      {
         logger.debug("Existing view " + this.view.getId() + " replaced by view " + view.getId());
         this.view.dispose();
      }
      this.view = view;
      if (view != null)
      {
         view.create(window, perspective, viewArea);
         viewArea.layout(true, true);
         if (view instanceof ViewWithContext)
            ((ViewWithContext)view).setContext(context);
      }
   }

   /**
    * Get current view.
    *
    * @return current view or null
    */
   public View getView()
   {
      return view;
   }

   /**
    * Set context for containing view
    *
    * @param context
    */
   public void setContext(Object context)
   {
      this.context = context;
      if ((view != null) && (view instanceof ViewWithContext))
         ((ViewWithContext)view).setContext(context);
   }
}