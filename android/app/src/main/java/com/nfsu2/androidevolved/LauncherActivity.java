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
import android.widget.ScrollView;
import android.util.DisplayMetrics;
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
    private Spinner resolution;
    private Spinner frameLimit;
    private static final int[] FRAME_LIMITS={0,30,60,120};
    private final int[][] sizes = new int[4][2];
    private TextView details;
    private Button start;
    private boolean allowed() {
        return Build.VERSION.SDK_INT >= 30 ? Environment.isExternalStorageManager()
            : checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
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
        layout.addView(text("Resolución de renderizado", 18));
        DisplayMetrics metrics = new DisplayMetrics(); getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        int width = Math.max(metrics.widthPixels, metrics.heightPixels), height = Math.min(metrics.widthPixels, metrics.heightPixels);
        ArrayList<String> sizeLabels = new ArrayList<>();
        String[] names = {"Nativa", "75%", "50%", "16:9"};
        for (int i = 0; i < 4; ++i) {
            float scale = i == 0 ? 1f : i == 1 ? 0.75f : 0.5f;
            sizes[i][0] = i == 3 ? 1280 : Math.round(width * scale / 2f) * 2;
            sizes[i][1] = i == 3 ? 720 : Math.round(height * scale / 2f) * 2;
            sizeLabels.add(names[i] + " — " + sizes[i][0] + " × " + sizes[i][1]);
        }
        resolution = new Spinner(this); resolution.setBackgroundColor(Color.rgb(230, 234, 239));
        ArrayAdapter<String> sizeAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, sizeLabels);
        sizeAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); resolution.setAdapter(sizeAdapter);
        resolution.setSelection(Math.max(0, Math.min(3, getSharedPreferences("launcher", MODE_PRIVATE).getInt("resolution", 0))));
        layout.addView(resolution);
        layout.addView(text("Límite de FPS",18));
        frameLimit=new Spinner(this);frameLimit.setBackgroundColor(Color.rgb(230,234,239));
        ArrayAdapter<String> frameAdapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,
                new String[]{"Sin límite","30 FPS","60 FPS","120 FPS"});
        frameAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        frameLimit.setAdapter(frameAdapter);
        int savedCap=getSharedPreferences("performance",MODE_PRIVATE).getInt("frameCap",0);
        for(int i=0;i<FRAME_LIMITS.length;i++)if(FRAME_LIMITS[i]==savedCap)frameLimit.setSelection(i);
        layout.addView(frameLimit);
        layout.addView(text("Una resolución inferior reduce la carga de la GPU.", 14));
        details = text("", 15); layout.addView(details);
        Button permission = new Button(this); permission.setText("Dar acceso a la carpeta nfsu2");
        permission.setOnClickListener(view -> {
            if (Build.VERSION.SDK_INT >= 30) startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
            else requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
        }); layout.addView(permission);
        start = new Button(this); start.setText("Iniciar juego");
        start.setOnClickListener(view -> {
            int index = selector.getSelectedItemPosition(); if (index < 0 || index >= available.size()) return;
            String[] language = available.get(index);
            int size = resolution.getSelectedItemPosition(); if (size < 0 || size >= sizes.length) return;
            getSharedPreferences("launcher", MODE_PRIVATE).edit().putString("language", language[1]).putInt("resolution", size).apply();
            getSharedPreferences("performance",MODE_PRIVATE).edit()
                    .putInt("frameCap",FRAME_LIMITS[frameLimit.getSelectedItemPosition()]).apply();
            android.app.ActivityManager manager = (android.app.ActivityManager)getSystemService(ACTIVITY_SERVICE);
            java.util.List<android.app.ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
            if (processes != null) for (android.app.ActivityManager.RunningAppProcessInfo process : processes) {
                if (process.uid == android.os.Process.myUid() && process.processName.equals(getPackageName() + ":game"))
                    android.os.Process.killProcess(process.pid);
            }
            startActivity(new Intent(this, GameActivity.class).putExtra("language", language[1])
                    .putExtra("renderWidth", sizes[size][0]).putExtra("renderHeight", sizes[size][1]));
        }); layout.addView(start);
        layout.addView(text("Port en desarrollo. La pantalla inicial ya se muestra; el menú y las carreras siguen en adaptación.", 14));
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.addView(layout); setContentView(scroll);
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
