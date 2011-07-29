/**
 * 
 */
package org.netxms.ui.android.main.views;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Paint.Style;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * View representing single element in activity list
 *
 */
public class ActivityListElement extends LinearLayout
{
	/**
	 * @param context
	 */
	public ActivityListElement(Context context, int imageId, int textId)
	{
		super(context);
		setOrientation(VERTICAL);
		setGravity(Gravity.CENTER_HORIZONTAL);
		setPadding(8, 8, 8, 8);
		setWillNotDraw(false);
		
		ImageView imageView = new ImageView(context);
		imageView.setImageResource(imageId);
		imageView.setPadding(8, 8, 8, 8);
		addView(imageView, new LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT));
		
		TextView text = new TextView(context);
		text.setText(textId);
		text.setTextColor(0xFF404040);
		text.setGravity(Gravity.CENTER_HORIZONTAL);
		addView(text, new LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT));
	}

	/* (non-Javadoc)
	 * @see android.view.View#onDraw(android.graphics.Canvas)
	 */
	@Override
	protected void onDraw(Canvas canvas)
	{
		Rect rect = new Rect(1, 1, getWidth() - 1, getHeight() - 1);
		RectF rectF = new RectF(rect);
		DisplayMetrics metrics = new DisplayMetrics();
		Activity activity = (Activity)getContext();
		activity.getWindowManager().getDefaultDisplay().getMetrics(metrics);
		float density = metrics.density;
		int arcSize = Math.round(density * 8);

		Paint paint = new Paint();
		paint.setColor(0xFFFFFFFF);
		paint.setStyle(Style.FILL);
		paint.setAntiAlias(true);
		canvas.drawRoundRect(rectF, arcSize, arcSize, paint);

		paint.setColor(0xFF000000);
		paint.setStyle(Style.STROKE);
		paint.setStrokeWidth(4);
		canvas.drawRoundRect(rectF, arcSize, arcSize, paint);
	}
}
