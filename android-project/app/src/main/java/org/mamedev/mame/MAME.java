// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic, Miso Kim
package org.mamedev.mame;

import java.io.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.os.*;
import android.util.Log;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import org.libsdl.app.SDLActivity;
import com.discord.socialsdk.DiscordSocialSdkInit;

/**
 * SDL Activity
 */
public class MAME extends SDLActivity {
	private static final String TAG = "MAME";
	private static final String ASSET_MARKER = ".mame_assets_v1";
	/** adb: am broadcast -a org.mamedev.mame.INJECT_KEY -p org.mamedev.mame --ei code 8 --ei hold_ms 120 */
	public static final String ACTION_INJECT_KEY = "org.mamedev.mame.INJECT_KEY";
	private static final AtomicBoolean sAssetsReady = new AtomicBoolean(false);

	private VirtualControlsView mVirtualControls;
	private BroadcastReceiver mInjectReceiver;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		DiscordSocialSdkInit.setEngineActivity(this);

		// Never unlock to portrait — landscape games get stretched when SDL
		// flips to SCREEN_ORIENTATION_FULL_USER on resizable windows.
		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

		// Copy large asset trees off the UI thread so onCreate cannot ANR.
		// SDL starts after the surface is ready; wait briefly so first launch
		// usually has bgfx/language in place before native code runs.
		final CountDownLatch assetsDone = new CountDownLatch(1);
		new Thread(() -> {
			try {
				ensureAssetsCopied();
			} finally {
				sAssetsReady.set(true);
				assetsDone.countDown();
			}
		}, "mame-asset-copy").start();

		try {
			assetsDone.await(2, TimeUnit.SECONDS);
		} catch (InterruptedException e) {
			Thread.currentThread().interrupt();
		}

		super.onCreate(savedInstanceState);
		ensureKeepAspectIni();
		ensureSnesFourByThreeCfg();
		attachVirtualControls();
		registerInjectReceiver();
	}

	@Override
	protected void onDestroy() {
		if (mInjectReceiver != null) {
			try {
				unregisterReceiver(mInjectReceiver);
			} catch (IllegalArgumentException ignored) {
			}
			mInjectReceiver = null;
		}
		super.onDestroy();
	}

	/**
	 * Keep the activity locked to landscape even when SDL asks for FULL_USER
	 * (empty orientation hint + resizable window).
	 */
	@Override
	public void setOrientationBis(int w, int h, boolean resizable, String hint) {
		Log.v(TAG, "Ignoring SDL orientation request w=" + w + " h=" + h
				+ " resizable=" + resizable + " hint=" + hint
				+ "; keeping SENSOR_LANDSCAPE");
		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
	}

	@Override
	protected String[] getArguments() {
		Intent intent = getIntent();
		String[] arguments = new String[0];
		if (intent != null) {
			String[] fromArray = intent.getStringArrayExtra("args");
			if (fromArray != null && fromArray.length > 0) {
				arguments = fromArray;
			} else {
				String joined = intent.getStringExtra("args");
				if (joined != null && !joined.trim().isEmpty()) {
					arguments = splitArgs(joined.trim());
				}
			}
		}
		arguments = ensureArg(arguments, "-keepaspect");
		arguments = ensureViewFourByThree(arguments);
		Log.i(TAG, "Native args: " + Arrays.toString(arguments));
		return arguments;
	}

	@Override
	protected String[] getLibraries() {
		return new String[] { "SDL2", "mainhub" };
	}

	private void attachVirtualControls() {
		ViewGroup layout = mLayout;
		if (layout == null) {
			Log.w(TAG, "No SDL layout; skipping virtual controls");
			return;
		}
		mVirtualControls = new VirtualControlsView(this);
		FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
				ViewGroup.LayoutParams.MATCH_PARENT,
				ViewGroup.LayoutParams.MATCH_PARENT);
		layout.addView(mVirtualControls, lp);
		Log.i(TAG, "Virtual on-screen controls attached");
	}

	private void registerInjectReceiver() {
		mInjectReceiver = new BroadcastReceiver() {
			@Override
			public void onReceive(Context context, Intent intent) {
				if (intent == null || !ACTION_INJECT_KEY.equals(intent.getAction())) {
					return;
				}
				int code = intent.getIntExtra("code", -1);
				int holdMs = intent.getIntExtra("hold_ms", 100);
				if (code < 0) {
					return;
				}
				Log.i(TAG, "Inject key code=" + code + " hold_ms=" + holdMs);
				VirtualControlsView.holdKey(code, holdMs);
			}
		};
		IntentFilter filter = new IntentFilter(ACTION_INJECT_KEY);
		if (Build.VERSION.SDK_INT >= 33) {
			registerReceiver(mInjectReceiver, filter, Context.RECEIVER_EXPORTED);
		} else {
			registerReceiver(mInjectReceiver, filter);
		}
	}

	/** Persist keepaspect so portrait/resume paths still letterbox. */
	private void ensureKeepAspectIni() {
		File root = getExternalFilesDir(null);
		if (root == null) {
			return;
		}
		File ini = new File(root, "mame.ini");
		if (ini.exists()) {
			return;
		}
		try (FileWriter fw = new FileWriter(ini)) {
			fw.write("# Generated by MAMEHub Android\n");
			fw.write("keepaspect           1\n");
			fw.write("unevenstretch        1\n");
		} catch (IOException e) {
			Log.w(TAG, "Could not write mame.ini", e);
		}
	}

	/**
	 * SNES default "pixel aspect" views look horizontally stretched on square-pixel
	 * phone displays. Prefer the CRT-style 4:3 view used by desktop e2e.
	 */
	private void ensureSnesFourByThreeCfg() {
		File root = getExternalFilesDir(null);
		if (root == null) {
			return;
		}
		File cfgDir = new File(root, "cfg");
		if (!cfgDir.exists() && !cfgDir.mkdirs()) {
			return;
		}
		File cfg = new File(cfgDir, "snes.cfg");
		try (FileWriter fw = new FileWriter(cfg)) {
			fw.write("<?xml version=\"1.0\"?>\n");
			fw.write("<mameconfig version=\"10\">\n");
			fw.write("    <system name=\"snes\">\n");
			fw.write("        <video>\n");
			fw.write("            <target index=\"0\" view=\"Screen 0 Standard (4:3)\" />\n");
			fw.write("        </video>\n");
			fw.write("    </system>\n");
			fw.write("</mameconfig>\n");
		} catch (IOException e) {
			Log.w(TAG, "Could not write snes.cfg", e);
		}
	}

	private static String[] ensureArg(String[] args, String flag) {
		for (String a : args) {
			if (flag.equals(a)) {
				return args;
			}
			if ("-nokeepaspect".equals(a)) {
				return args;
			}
		}
		String[] out = new String[args.length + 1];
		out[0] = flag;
		System.arraycopy(args, 0, out, 1, args.length);
		return out;
	}

	private static String[] ensureViewFourByThree(String[] args) {
		for (int i = 0; i < args.length; i++) {
			if ("-view".equals(args[i]) || "-view0".equals(args[i])) {
				return args; // caller already chose a view
			}
		}
		String[] out = new String[args.length + 2];
		out[0] = "-view";
		out[1] = "Screen 0 Standard (4:3)";
		System.arraycopy(args, 0, out, 2, args.length);
		return out;
	}

	private static String[] splitArgs(String cmdline) {
		ArrayList<String> out = new ArrayList<>();
		StringBuilder cur = new StringBuilder();
		boolean inQuote = false;
		for (int i = 0; i < cmdline.length(); i++) {
			char c = cmdline.charAt(i);
			if (c == '"') {
				inQuote = !inQuote;
			} else if (Character.isWhitespace(c) && !inQuote) {
				if (cur.length() > 0) {
					out.add(cur.toString());
					cur.setLength(0);
				}
			} else {
				cur.append(c);
			}
		}
		if (cur.length() > 0) {
			out.add(cur.toString());
		}
		return out.toArray(new String[0]);
	}

	private void ensureAssetsCopied() {
		File root = getExternalFilesDir(null);
		if (root == null) {
			Log.w(TAG, "No external files dir; skipping asset copy");
			return;
		}
		File marker = new File(root, ASSET_MARKER);
		if (marker.exists()) {
			Log.i(TAG, "Assets already copied (" + marker.getName() + ")");
			return;
		}
		Log.i(TAG, "Copying bgfx/language assets...");
		copyAssetAll("bgfx");
		copyAssetAll("language");
		try {
			if (!marker.createNewFile()) {
				Log.w(TAG, "Could not create asset marker");
			}
		} catch (IOException e) {
			Log.w(TAG, "Asset marker write failed", e);
		}
	}

	public void copyAssetAll(String srcPath) {
		AssetManager assetMgr = this.getAssets();
		try {
			String destPath = getExternalFilesDir(null) + File.separator + srcPath;
			String[] assets = assetMgr.list(srcPath);
			if (assets == null) {
				return;
			}
			if (assets.length == 0) {
				copyFile(srcPath, destPath);
			} else {
				File dir = new File(destPath);
				if (!dir.exists()) {
					dir.mkdir();
				}
				for (String element : assets) {
					copyAssetAll(srcPath + File.separator + element);
				}
			}
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	public void copyFile(String srcFile, String destFile) {
		AssetManager assetMgr = this.getAssets();
		InputStream is = null;
		OutputStream os = null;
		try {
			if (new File(destFile).exists()) {
				return;
			}
			is = assetMgr.open(srcFile);
			os = new FileOutputStream(destFile);
			byte[] buffer = new byte[8192];
			int read;
			while ((read = is.read(buffer)) != -1) {
				os.write(buffer, 0, read);
			}
			os.flush();
			Log.v(TAG, "copy from Asset:" + destFile);
		} catch (IOException e) {
			e.printStackTrace();
		} finally {
			try {
				if (is != null) is.close();
			} catch (IOException ignored) {}
			try {
				if (os != null) os.close();
			} catch (IOException ignored) {}
		}
	}
}
