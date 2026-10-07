package com.nfsu2.androidevolved;

import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.os.Environment;
import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.provider.Settings;
import android.graphics.Color;
import android.view.Gravity;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.File;
import java.util.ArrayList;

public final class LauncherActivity extends Activity {
    private final String[][] languages = {
        {"Español", "Spanish", "Spanish"}, {"English", "English UK", "English"},
        {"Français", "French", "French"}, {"Deutsch", "German", "German"},
        {"Italiano", "Italian", "Italian"}, {"Nederlands", "Dutch", "Dutch"},
        {"Svenska", "Swedish", "Swedish"}, {"Dansk", "Danish", "Danish"},
        {"日本語", "Japanese", "Japanese"}, {"한국어", "Korean", "Korean"},
        {"繁體中文", "Chinese (Traditional)", "Chinese"}, {"ไทย", "Thai", "Thailand"}
    };
    private final ArrayList<String[]> available = new ArrayList<>();
    private Spinner selector;
    private TextView details;
    private Button start;
    private boolean allowed() {
        return Build.VERSION.SDK_INT >= 30 ? Environment.isExternalStorageManager()
            : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }
    private TextView text(String value, int size) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size);
        view.setTextColor(Color.WHITE); view.setPadding(0, 8, 0, 8); return view;
    }
    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        LinearLayout layout = new LinearLayout(this); layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER_VERTICAL); layout.setPadding(64, 24, 64, 24);
        layout.setBackgroundColor(Color.rgb(12, 28, 40));
        layout.addView(text("NFSU2 Android Evolved", 28));
        layout.addView(text("Idioma del juego", 18));
        selector = new Spinner(this); selector.setBackgroundColor(Color.rgb(230, 234, 239)); layout.addView(selector);
        details = text("", 15); layout.addView(details);
        Button permission = new Button(this); permission.setText("Dar acceso a la carpeta nfsu2");
        permission.setOnClickListener(view -> {
            if (Build.VERSION.SDK_INT >= 30) startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
            else requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 1);
        }); layout.addView(permission);
        start = new Button(this); start.setText("Iniciar juego");
        start.setOnClickListener(view -> {
            int index = selector.getSelectedItemPosition(); if (index < 0 || index >= available.size()) return;
            String[] language = available.get(index);
            getSharedPreferences("launcher", MODE_PRIVATE).edit().putString("language", language[1]).apply();
            android.app.ActivityManager manager = (android.app.ActivityManager)getSystemService(ACTIVITY_SERVICE);
            java.util.List<android.app.ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
            if (processes != null) for (android.app.ActivityManager.RunningAppProcessInfo process : processes) {
                if (process.uid == android.os.Process.myUid() && process.processName.equals(getPackageName() + ":game"))
                    android.os.Process.killProcess(process.pid);
            }
            startActivity(new Intent(this, GameActivity.class).putExtra("language", language[1]));
        }); layout.addView(start);
        layout.addView(text("Port en desarrollo. La pantalla inicial ya se muestra; el menú y las carreras siguen en adaptación.", 14));
        setContentView(layout);
    }
    @Override protected void onResume() {
        super.onResume(); refresh();
    }
    private void refresh() {
        boolean access = allowed(); File root = new File(Environment.getExternalStorageDirectory(), "nfsu2");
        String saved = getSharedPreferences("launcher", MODE_PRIVATE).getString("language", "Spanish");
        available.clear(); ArrayList<String> labels = new ArrayList<>();
        int selected = 0;
        for (String[] language : languages) {
            if (access && new File(root, "LANGUAGES/" + language[2] + ".bin").isFile()) {
                if (language[1].equals(saved)) selected = available.size();
                available.add(language); labels.add(language[0]);
            }
        }
        if (labels.isEmpty()) labels.add("Español — pendiente de acceso a los datos");
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        selector.setAdapter(adapter); selector.setSelection(selected);
        boolean ready = access && new File(root, "SPEED2.EXE").isFile() && !available.isEmpty();
        start.setEnabled(ready);
        details.setText(ready ? "Datos encontrados en memoria interna/nfsu2.\nSolo se muestran los idiomas presentes en tu copia."
                : "Copia los datos de tu juego en memoria interna/nfsu2 y permite el acceso a archivos.");
    }
    @Override public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results); refresh();
    }
}
