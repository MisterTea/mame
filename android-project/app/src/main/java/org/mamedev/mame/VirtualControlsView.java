// license:BSD-3-Clause
package org.mamedev.mame;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.Build;
import android.util.TypedValue;
import android.view.MotionEvent;
import android.view.View;
import android.view.KeyEvent;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;

/**
 * Lightweight on-screen controls for Android touch devices.
 * Emits MAME's default keyboard bindings (not Android GAMEPAD button codes,
 * which SDL maps to UNKNOWN on the keyboard path).
 *
 * SNES / MAME defaults used here:
 *   D-pad  KEYCODE_DPAD_*
 *   Y      KEYCODE_CTRL_LEFT
 *   B      KEYCODE_ALT_LEFT
 *   A      KEYCODE_SPACE
 *   X      KEYCODE_SHIFT_LEFT
 *   Start  KEYCODE_1
 *   Select KEYCODE_5
 */
final class VirtualControlsView extends View {
	private static final int HOLD_MS = 100;
	private static final int MENU_POLL_MS = 120;
	private static final String MENU_ACTIVE_HINT = "MAMEHUB_MENU_ACTIVE";

	private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
	private final Handler handler = new Handler(Looper.getMainLooper());

	private final RectF dpadArea = new RectF();
	private final RectF faceArea = new RectF();
	private final RectF startBtn = new RectF();
	private final RectF selectBtn = new RectF();

	private int activeDpad = -1;
	private int activeFace = -1;
	private boolean menuMode = false;
	private final Runnable menuPoll = new Runnable() {
		@Override
		public void run() {
			boolean nextMenuMode = SDLActivity.nativeGetHintBoolean(MENU_ACTIVE_HINT, false);
			if (nextMenuMode != menuMode) {
				menuMode = nextMenuMode;
				releaseKey(activeFace);
				activeFace = -1;
				requestLayout();
				invalidate();
			}
			handler.postDelayed(this, MENU_POLL_MS);
		}
	};

	VirtualControlsView(Context context) {
		super(context);
		setClickable(true);
		setFocusable(false);

		fill.setStyle(Paint.Style.FILL);
		fill.setColor(0x66222222);
		stroke.setStyle(Paint.Style.STROKE);
		stroke.setStrokeWidth(dp(2));
		stroke.setColor(0xCCEEEEEE);
		label.setColor(0xFFFFFFFF);
		label.setTextAlign(Paint.Align.CENTER);
		label.setTextSize(dp(14));
		label.setFakeBoldText(true);
	}

	private float dp(float v) {
		return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
				getResources().getDisplayMetrics());
	}

	@Override
	protected void onSizeChanged(int w, int h, int oldw, int oldh) {
		super.onSizeChanged(w, h, oldw, oldh);
		float pad = dp(16);
		float dpadSize = Math.min(h, w) * 0.42f;
		float faceSize = dpadSize;
		float btnW = dp(96);
		float btnH = dp(48);
		float leftInset = sideInset(true);
		float rightInset = sideInset(false);
		float bottom = h - pad - bottomInset();

		dpadArea.set(pad + leftInset, bottom - dpadSize, pad + leftInset + dpadSize, bottom);
		faceArea.set(w - pad - rightInset - faceSize, bottom - faceSize, w - pad - rightInset, bottom);
		startBtn.set(w * 0.5f - btnW - dp(12), bottom - btnH, w * 0.5f - dp(12), bottom);
		selectBtn.set(w * 0.5f + dp(12), bottom - btnH, w * 0.5f + btnW + dp(12), bottom);
		if (menuMode) {
			float menuBtnW = dp(136);
			startBtn.set(w * 0.5f - (menuBtnW * 0.5f), bottom - btnH, w * 0.5f + (menuBtnW * 0.5f), bottom);
		}
	}

	@Override
	protected void onDraw(Canvas canvas) {
		updateMenuMode();
		drawRound(canvas, dpadArea);
		drawRound(canvas, startBtn);
		if (!menuMode) {
			drawRound(canvas, faceArea);
			drawRound(canvas, selectBtn);
		}

		float dpadCx = dpadArea.centerX();
		float dpadCy = dpadArea.centerY();
		float faceCx = faceArea.centerX();
		float faceCy = faceArea.centerY();
		float o = dpadArea.width() * 0.28f;

		canvas.drawText("↑", dpadCx, dpadCy - o + label.getTextSize() * 0.35f, label);
		canvas.drawText("↓", dpadCx, dpadCy + o + label.getTextSize() * 0.35f, label);
		canvas.drawText("←", dpadCx - o, dpadCy + label.getTextSize() * 0.35f, label);
		canvas.drawText("→", dpadCx + o, dpadCy + label.getTextSize() * 0.35f, label);

		if (!menuMode) {
			canvas.drawText("X", faceCx, faceCy - o + label.getTextSize() * 0.35f, label);
			canvas.drawText("B", faceCx, faceCy + o + label.getTextSize() * 0.35f, label);
			canvas.drawText("Y", faceCx - o, faceCy + label.getTextSize() * 0.35f, label);
			canvas.drawText("A", faceCx + o, faceCy + label.getTextSize() * 0.35f, label);
			canvas.drawText("START", startBtn.centerX(), startBtn.centerY() + label.getTextSize() * 0.35f, label);
			canvas.drawText("SELECT", selectBtn.centerX(), selectBtn.centerY() + label.getTextSize() * 0.35f, label);
		} else {
			canvas.drawText("SELECT", startBtn.centerX(), startBtn.centerY() + label.getTextSize() * 0.35f, label);
		}
	}

	private void drawRound(Canvas canvas, RectF r) {
		float rad = dp(12);
		canvas.drawRoundRect(r, rad, rad, fill);
		canvas.drawRoundRect(r, rad, rad, stroke);
	}

	@Override
	public boolean onTouchEvent(MotionEvent event) {
		updateMenuMode();
		final int action = event.getActionMasked();
		final float x = event.getX();
		final float y = event.getY();

		if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
			if (startBtn.contains(x, y)) {
				pulseKey(menuMode ? KeyEvent.KEYCODE_5 : KeyEvent.KEYCODE_1);
				return true;
			}
			if (!menuMode && selectBtn.contains(x, y)) {
				pulseKey(KeyEvent.KEYCODE_5);
				return true;
			}
			if (dpadArea.contains(x, y) || (!menuMode && faceArea.contains(x, y))) {
				// fall through to shared press logic below
			} else {
				// Let SDL receive taps outside the pad (menus / "press any key").
				return false;
			}
		}

		if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN
				|| action == MotionEvent.ACTION_MOVE) {
			if (dpadArea.contains(x, y)) {
				int key = quadrantKey(dpadArea, x, y, true);
				if (key != activeDpad) {
					releaseKey(activeDpad);
					pressKey(key);
					activeDpad = key;
				}
				return true;
			}
			if (!menuMode && faceArea.contains(x, y)) {
				int key = quadrantKey(faceArea, x, y, false);
				if (key != activeFace) {
					releaseKey(activeFace);
					pressKey(key);
					activeFace = key;
				}

				@Override
				protected void onAttachedToWindow() {
					super.onAttachedToWindow();
					handler.post(menuPoll);
				}

				@Override
				protected void onDetachedFromWindow() {
					handler.removeCallbacks(menuPoll);
					super.onDetachedFromWindow();
				}

				private void updateMenuMode() {
					boolean nextMenuMode = SDLActivity.nativeGetHintBoolean(MENU_ACTIVE_HINT, false);
					if (nextMenuMode != menuMode) {
						menuMode = nextMenuMode;
						releaseKey(activeFace);
						activeFace = -1;
						requestLayout();
						invalidate();
					}
				}

				private float bottomInset() {
					if (Build.VERSION.SDK_INT >= 30) {
						WindowInsets insets = getRootWindowInsets();
						if (insets != null) {
							return insets.getInsets(WindowInsets.Type.systemBars()).bottom;
						}
					} else if (Build.VERSION.SDK_INT >= 23) {
						WindowInsets insets = getRootWindowInsets();
						if (insets != null) {
							return insets.getStableInsetBottom();
						}
					}
					return 0f;
				}

				private float sideInset(boolean left) {
					if (Build.VERSION.SDK_INT >= 30) {
						WindowInsets insets = getRootWindowInsets();
						if (insets != null) {
							return left
									? insets.getInsets(WindowInsets.Type.systemBars()).left
									: insets.getInsets(WindowInsets.Type.systemBars()).right;
						}
					} else if (Build.VERSION.SDK_INT >= 23) {
						WindowInsets insets = getRootWindowInsets();
						if (insets != null) {
							return left ? insets.getStableInsetLeft() : insets.getStableInsetRight();
						}
					}
					return 0f;
				}
				return true;
			}
			if (action == MotionEvent.ACTION_MOVE && (activeDpad >= 0 || activeFace >= 0)) {
				releaseKey(activeDpad);
				releaseKey(activeFace);
				activeDpad = -1;
				activeFace = -1;
				return true;
			}
		}

		if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL
				|| action == MotionEvent.ACTION_POINTER_UP) {
			if (activeDpad >= 0 || activeFace >= 0) {
				releaseKey(activeDpad);
				releaseKey(activeFace);
				activeDpad = -1;
				activeFace = -1;
				return true;
			}
			return false;
		}
		return false;
	}

	/** Map touch in a square to a quadrant key. */
	private int quadrantKey(RectF area, float x, float y, boolean dpad) {
		float dx = x - area.centerX();
		float dy = y - area.centerY();
		if (Math.abs(dx) > Math.abs(dy)) {
			if (dpad) {
				return dx < 0 ? KeyEvent.KEYCODE_DPAD_LEFT : KeyEvent.KEYCODE_DPAD_RIGHT;
			}
			return dx < 0 ? KeyEvent.KEYCODE_CTRL_LEFT : KeyEvent.KEYCODE_SPACE; // Y / A
		}
		if (dpad) {
			return dy < 0 ? KeyEvent.KEYCODE_DPAD_UP : KeyEvent.KEYCODE_DPAD_DOWN;
		}
		return dy < 0 ? KeyEvent.KEYCODE_SHIFT_LEFT : KeyEvent.KEYCODE_ALT_LEFT; // X / B
	}

	static void pressKey(int keyCode) {
		if (keyCode < 0) {
			return;
		}
		SDLActivity.onNativeKeyDown(keyCode);
	}

	static void releaseKey(int keyCode) {
		if (keyCode < 0) {
			return;
		}
		SDLActivity.onNativeKeyUp(keyCode);
	}

	/** Fire a held key long enough for MAME to sample it pressed. */
	void pulseKey(int keyCode) {
		pressKey(keyCode);
		handler.postDelayed(() -> releaseKey(keyCode), HOLD_MS);
	}

	/** Hold {@code keyCode} for {@code holdMs} (used by e2e inject broadcast). */
	static void holdKey(int keyCode, int holdMs) {
		pressKey(keyCode);
		new Handler(Looper.getMainLooper()).postDelayed(
				() -> releaseKey(keyCode),
				Math.max(holdMs, HOLD_MS));
	}

	/** Wall-clock helper so callers can sleep without busy-waiting the UI thread. */
	static void holdKeyBlocking(int keyCode, int holdMs) {
		pressKey(keyCode);
		SystemClock.sleep(Math.max(holdMs, HOLD_MS));
		releaseKey(keyCode);
	}
}
