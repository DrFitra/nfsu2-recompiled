package com.nfsu2.androidevolved;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.content.Intent;
import android.net.Uri;
import android.Manifest;
import android.content.pm.PackageManager;
import android.widget.Toast;
import android.widget.TextView;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.PopupWindow;
import android.graphics.Color;
import android.widget.LinearLayout;
import android.widget.Button;
import android.view.MotionEvent;

/** Storage permission gateway for the fixed Internal storage/nfsu2 directory. */
public final class GameActivity extends NativeActivity {
    static { System.loadLibrary("nfsu2_android"); }
    private boolean requested;
    private TextView status;
    private PopupWindow statusWindow;
    private PopupWindow leftControls, rightControls;
    private static native void nativeKey(int scan, boolean down);
    private static native void nativeLanguage(String language);
    private PopupWindow controls(String[] labels, int[] scans) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < labels.length; ++i) {
            final int scan = scans[i];
            Button button = new Button(this);
            button.setText(labels[i]);
            button.setTextSize(14); button.setSingleLine(true);
            button.setMinWidth(0); button.setMinimumWidth(0); button.setPadding(2, 0, 2, 0);
            button.setTextColor(Color.WHITE);
            button.setBackgroundColor(0xb0334455);
            button.setOnTouchListener((view, event) -> {
                int action = event.getActionMasked();
                if (action == MotionEvent.ACTION_DOWN) { nativeKey(scan, true); view.setAlpha(0.55f); }
                else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                    nativeKey(scan, false); view.setAlpha(1f);
                    if (action == MotionEvent.ACTION_UP) view.performClick();
                }
                return true;
            });
            row.addView(button, new LinearLayout.LayoutParams(dp(64), dp(60)));
        }
        return new PopupWindow(row, ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, false);
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        String language = getIntent().getStringExtra("language");
        if (language == null) language = getSharedPreferences("launcher", MODE_PRIVATE).getString("language", "Spanish");
        nativeLanguage(language);
        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setBackgroundColor(0xb0000000);
        status.setTextSize(18);
        status.setPadding(24, 16, 24, 16);
        status.setText("NFSU2 Android Evolved\nComprobando memoria interna/nfsu2…\nPort en desarrollo; todavía no es jugable.");
        // NativeActivity gives its main window surface to Vulkan; use a separate
        // non-interactive popup surface for the diagnostic text.
        statusWindow = new PopupWindow(status, ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, false);
        statusWindow.setTouchable(false);
        leftControls = controls(new String[]{"←", "→", "Esc", "OK"}, new int[]{0xcb, 0xcd, 1, 0x1c});
        rightControls = controls(new String[]{"↑", "↓", "F. mano", "Nitro"}, new int[]{0xc8, 0xd0, 0x39, 0x38});
    }
    public void setStatus(String message) {
        runOnUiThread(() -> status.setText("NFSU2 Android Evolved\n" + message +
                "\nPort en desarrollo; todavía no es jugable."));
    }
    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused && statusWindow != null && !statusWindow.isShowing()) {
            statusWindow.showAtLocation(getWindow().getDecorView(), Gravity.TOP | Gravity.START, 0, 0);
        }
        if (focused && leftControls != null && !leftControls.isShowing()) {
            leftControls.showAtLocation(getWindow().getDecorView(), Gravity.BOTTOM | Gravity.START, dp(12), dp(12));
            rightControls.showAtLocation(getWindow().getDecorView(), Gravity.BOTTOM | Gravity.END, dp(12), dp(12));
        }
    }
    @Override protected void onPause() {
        if (statusWindow != null) statusWindow.dismiss();
        if (leftControls != null) leftControls.dismiss();
        if (rightControls != null) rightControls.dismiss();
        super.onPause();
    }
    @Override protected void onResume() {
        super.onResume();
        boolean allowed = Build.VERSION.SDK_INT >= 30
                ? Environment.isExternalStorageManager()
                : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        if (!allowed && !requested) {
            requested = true;
            Toast.makeText(this, "Permite acceso a archivos para leer la carpeta nfsu2 de la memoria interna", Toast.LENGTH_LONG).show();
            if (Build.VERSION.SDK_INT >= 30) {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } else {
                requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 1);
            }
        }
    }
    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        // NativeActivity may stay resumed when a runtime permission dialog closes.
        if (code == 1 && results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) recreate();
    }
    @Override protected void onDestroy() {
        super.onDestroy();
        // Each launch uses a fresh guest machine in the dedicated :game process.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
