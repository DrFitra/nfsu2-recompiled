package com.nfsu2.androidevolved;

import android.content.Context;
import android.content.SharedPreferences;
import android.app.AlertDialog;
import android.widget.EditText;
import android.widget.Toast;
import org.json.JSONObject;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.HashMap;

/** One surface tracks independent fingers so steering and pedals can be held together. */
final class RacingControlsView extends View {
    interface Keys { void set(int scan, boolean down); }
    private final Keys keys;
    private final Runnable textInput;
    private final Runnable configureTilt;
    private String style;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<Control> controls = new ArrayList<>();
    private final HashMap<Integer, Control> fingers = new HashMap<>();
    private boolean racing;
    private boolean lastGameMode;
    private boolean editing;
    private Control selected;
    private int dragPointer = -1;
    private float dragX, dragY;
    private final SharedPreferences layouts;
    private float scale;
    private static final int CYAN = Color.rgb(110, 235, 218);
    private static final class Control {
        final RectF area;
        final String glyph, label;
        final int scan;
        final RectF original;
        String id;
        int held;
        float steering;
        Control(float x, float y, float w, float h, String glyph, String label, int scan) {
            area = new RectF(x, y, x+w, y+h); original=new RectF(area); this.glyph=glyph; this.label=label; this.scan=scan;
        }
    }
    RacingControlsView(Context context, Keys keys, Runnable textInput, Runnable configureTilt) {
        super(context); this.keys=keys; this.textInput=textInput;
        layouts=context.getSharedPreferences("touch_layouts_v1",Context.MODE_PRIVATE);
        style=layouts.getString("style","classic");this.configureTilt=configureTilt;
        setContentDescription("Controles del juego: cambia entre menú y conducción");
    }
    private void add(float x, float y, float w, float h, String glyph, String label, int scan) {
        Control c=new Control(x,y,w,h,glyph,label,scan);
        c.id=(racing?"race."+(style.equals("classic")?"":style+"."):"menu.")+scan;
        float factor=layouts.getFloat(c.id+".size",1);
        float cx=layouts.getFloat(c.id+".x",c.area.centerX()/(getWidth()/scale))*(getWidth()/scale);
        float cy=layouts.getFloat(c.id+".y",c.area.centerY()/(getHeight()/scale))*(getHeight()/scale);
        c.area.set(cx-w*factor/2,cy-h*factor/2,cx+w*factor/2,cy+h*factor/2);
        clamp(c);controls.add(c);
    }
    private void clamp(Control c) {
        float w=getWidth()/scale,h=getHeight()/scale;
        float dx=c.area.left<4?4-c.area.left:c.area.right>w-4?w-4-c.area.right:0;
        float dy=c.area.top<4?4-c.area.top:c.area.bottom>h-4?h-4-c.area.bottom:0;
        c.area.offset(dx,dy);
    }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) { layoutControls(); }
    void setGameMode(boolean driving) {
        if(editing||lastGameMode==driving)return;
        lastGameMode=driving;racing=driving;layoutControls();
    }
    boolean isDriving() {return racing&&!editing;}
    private void layoutControls() {
        releaseAll(); controls.clear();selected=null;dragPointer=-1;
        scale=Math.min(getResources().getDisplayMetrics().density, getWidth()/820f);
        float w=getWidth()/scale, h=getHeight()/scale, bottom=h-22;
        add(22,18,54,40,"Ⅱ","PAUSA",1);
        add(w-152,18,130,40,racing?"MENÚ":"CONDUCIR","",-1);
        if(racing) {
            if(style.equals("classic")) {
                add(22,bottom-78,78,78,"‹","IZQUIERDA",0xcb);
                add(112,bottom-78,78,78,"›","DERECHA",0xcd);
                add(w-106,bottom-120,84,120,"↑","ACELERAR",0xc8);
                add(w-200,bottom-88,78,88,"↓","FRENO",0xd0);
                add(w-280,bottom-62,62,62,"N₂O","NITRO",0x38);
                add(216,bottom-62,62,62,"◎","F. MANO",0x39);
            } else {
                boolean xbox=style.equals("xbox");
                add(22,bottom-148,148,148,"","DIRECCIÓN",-3);
                add(w-104,bottom-120,82,120,xbox?"RT":"R2","ACELERAR",0xc8);
                add(w-200,bottom-88,78,88,xbox?"LT":"L2","FRENO",0xd0);
                add(w-235,bottom-162,58,58,xbox?"A":"×","F. MANO",0x39);
                add(w-167,bottom-230,58,58,xbox?"B":"○","NITRO",0x38);
                add(w-303,bottom-230,58,58,xbox?"X":"□","CÁMARA",0x2e);
                add(w-235,bottom-298,58,58,xbox?"Y":"△","MIRAR",0x30);
            }
            float gearY=bottom-(style.equals("classic")?138:220);
            add(22,gearY,60,48,"+","MARCHA",0x2a);
            add(90,gearY,60,48,"−","MARCHA",0x1d);
            if(style.equals("classic"))add(158,bottom-138,60,48,"C","CÁMARA",0x2e);
        } else {
            add(22,bottom-62,62,62,"‹","",0xcb);
            add(96,bottom-62,62,62,"›","",0xcd);
            add(w-240,bottom-62,62,62,"↑","",0xc8);
            add(w-166,bottom-62,62,62,"↓","",0xd0);
            add(w-92,bottom-62,70,62,"✓","ACEPTAR",0x1c);
            add(w/2-44,bottom-42,88,42,"TEXTO","",-2);
        }
        invalidate();
    }
    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas); canvas.save(); canvas.scale(scale,scale);
        for(Control c:controls) {
            boolean face=racing&&!style.equals("classic")&&(c.scan==0x39||c.scan==0x38||c.scan==0x2e||c.scan==0x30);
            float radius=Math.min(c.area.width(),c.area.height())/2;
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(c.held>0?0xe02c796f:0xb014202c);
            if(face||c.scan==-3)canvas.drawCircle(c.area.centerX(),c.area.centerY(),radius,paint);
            else canvas.drawRoundRect(c.area,18,18,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(c.held>0?2.5f:1.2f);
            paint.setColor(editing&&c==selected?0xffffcf69:c.held>0?CYAN:0x85798f9e);
            if(face) {
                paint.setStrokeWidth(3);
                paint.setColor(editing&&c==selected?0xffffcf69:faceColor(c.scan));
            }
            if(face||c.scan==-3)canvas.drawCircle(c.area.centerX(),c.area.centerY(),radius-2,paint);
            else canvas.drawRoundRect(c.area,18,18,paint);
            paint.setStyle(Paint.Style.FILL);paint.setTextAlign(Paint.Align.CENTER);
            paint.setColor(c.scan==0xc8||c.held>0?CYAN:Color.WHITE);
            if(racing&&!style.equals("classic")) {
                if(face)paint.setColor(faceColor(c.scan));
            }
            paint.setTypeface(android.graphics.Typeface.create("sans-serif-medium",0));
            paint.setTextSize(c.glyph.length()>3?13: c.glyph.equals("N₂O")?19:30);
            float mid=c.area.centerY()-(c.label.isEmpty()?0:6);
            canvas.drawText(c.glyph,c.area.centerX(),mid-(paint.ascent()+paint.descent())/2,paint);
            if(!c.label.isEmpty()) {
                paint.setTextSize(8);paint.setColor(0xffc1ced7);
                canvas.drawText(c.label,c.area.centerX(),c.area.bottom-11,paint);
            }
            if(c.scan==-3) {
                paint.setColor(c.held>0?CYAN:0xff526b7b);
                canvas.drawCircle(c.area.centerX()+c.steering*radius*.55f,c.area.centerY(),radius*.32f,paint);
            }
        }
        float w=getWidth()/scale;
        if(editing) {
            drawEditorButton(canvas,new RectF(w/2-250,18,w/2-150,58),"RESTABLECER");
            drawEditorButton(canvas,new RectF(w/2-142,18,w/2-102,58),"−");
            drawEditorButton(canvas,new RectF(w/2-94,18,w/2-54,58),"+");
            drawEditorButton(canvas,new RectF(w/2-46,18,w/2+58,58),"GUARDAR");
            drawEditorButton(canvas,new RectF(w/2+66,18,w/2+170,58),"CANCELAR");
            paint.setColor(Color.WHITE);paint.setTextSize(12);
            canvas.drawText("Arrastra un botón · Selecciónalo y usa − / + para cambiar su tamaño",w/2,82,paint);
            drawEditorButton(canvas,new RectF(w/2-65,96,w/2+65,132),"OPCIONES");
        } else drawEditorButton(canvas,new RectF(w/2-48,18,w/2+48,58),"AJUSTAR");
        canvas.restore();
    }
    private int faceColor(int scan) {
        if(scan==0x39)return style.equals("xbox")?0xff74df8e:0xff80b8ff;
        if(scan==0x38)return 0xffff7f86;
        if(scan==0x2e)return style.equals("xbox")?0xff80b8ff:0xffdd9cf4;
        return style.equals("xbox")?0xffffd778:0xff74dfb3;
    }
    private void drawEditorButton(Canvas canvas,RectF area,String label) {
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xe014202c);canvas.drawRoundRect(area,12,12,paint);
        paint.setColor(CYAN);paint.setTextSize(label.length()>2?11:24);paint.setTextAlign(Paint.Align.CENTER);
        canvas.drawText(label,area.centerX(),area.centerY()-(paint.ascent()+paint.descent())/2,paint);
    }
    private void resizeSelected(float change) {
        if(selected==null)return;
        float factor=Math.max(.55f,Math.min(2.5f,selected.area.width()/selected.original.width()+change));
        float cx=selected.area.centerX(),cy=selected.area.centerY();
        float w=selected.original.width()*factor,h=selected.original.height()*factor;
        selected.area.set(cx-w/2,cy-h/2,cx+w/2,cy+h/2);clamp(selected);
    }
    private void saveLayout() {
        SharedPreferences.Editor save=layouts.edit();
        for(Control c:controls)save.putFloat(c.id+".x",c.area.centerX()/(getWidth()/scale))
                .putFloat(c.id+".y",c.area.centerY()/(getHeight()/scale))
                .putFloat(c.id+".size",c.area.width()/c.original.width());
        save.apply();
    }
    private boolean editTouch(MotionEvent e) {
        int action=e.getActionMasked(),index=e.getActionIndex();
        float x=e.getX(index)/scale,y=e.getY(index)/scale,w=getWidth()/scale;
        if(action==MotionEvent.ACTION_DOWN) {
            if(x>=w/2-65&&x<=w/2+65&&y>=96&&y<=132) {showOptions();return true;}
            if(y>=18&&y<=58) {
                boolean handled=true;
                if(x>=w/2-250&&x<=w/2-150) {for(Control c:controls)c.area.set(c.original);selected=null;}
                else if(x>=w/2-142&&x<=w/2-102)resizeSelected(-.1f);
                else if(x>=w/2-94&&x<=w/2-54)resizeSelected(.1f);
                else if(x>=w/2-46&&x<=w/2+58) {saveLayout();editing=false;layoutControls();}
                else if(x>=w/2+66&&x<=w/2+170) {editing=false;layoutControls();}
                else handled=false;
                if(handled) {invalidate();return true;}
            }
            selected=hit(e.getX(index),e.getY(index));
            dragPointer=e.getPointerId(index);dragX=x;dragY=y;
        } else if(action==MotionEvent.ACTION_MOVE&&selected!=null&&dragPointer>=0) {
            int i=e.findPointerIndex(dragPointer);
            if(i>=0) {x=e.getX(i)/scale;y=e.getY(i)/scale;selected.area.offset(x-dragX,y-dragY);clamp(selected);dragX=x;dragY=y;}
        } else if(action==MotionEvent.ACTION_CANCEL||action==MotionEvent.ACTION_UP||
                (action==MotionEvent.ACTION_POINTER_UP&&e.getPointerId(index)==dragPointer))dragPointer=-1;
        invalidate();return true;
    }
    private void showOptions() {
        new AlertDialog.Builder(getContext()).setTitle("Opciones de controles")
                .setItems(new String[]{"Estilo: clásico / Xbox / PlayStation","Conducción por inclinación","Guardar layout con nombre","Cargar layout personalizado"},(dialog,which)->{
                    if(which==0)chooseStyle();else if(which==1)configureTilt.run();
                    else if(which==2)saveNamedLayout();else loadNamedLayout();
                }).setNegativeButton("Cerrar",null).show();
    }
    private void chooseStyle() {
        String[] ids={"classic","xbox","playstation"};
        new AlertDialog.Builder(getContext()).setTitle("Estilo de conducción")
                .setSingleChoiceItems(new String[]{"Clásico · pedales","Xbox · A / B / X / Y","PlayStation 3 · × / ○ / □ / △"},style.equals("classic")?0:style.equals("xbox")?1:2,(dialog,which)->{
                    style=ids[which];layouts.edit().putString("style",style).apply();
                    layoutControls();dialog.dismiss();
                }).setNegativeButton("Cancelar",null).show();
    }
    private void saveNamedLayout() {
        EditText name=new EditText(getContext());name.setSingleLine();name.setHint("Nombre del layout");
        name.setFilters(new android.text.InputFilter[]{new android.text.InputFilter.LengthFilter(24)});
        AlertDialog dialog=new AlertDialog.Builder(getContext()).setTitle("Guardar diseño de "+(racing?"conducción":"menú"))
                .setView(name).setPositiveButton("Guardar",null).setNegativeButton("Cancelar",null).create();
        dialog.setOnShowListener(ignored->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{
            String value=name.getText().toString().trim();if(value.isEmpty()) {name.setError("Escribe un nombre");return;}
            try {
                JSONObject data=new JSONObject();data.put("style",style);
                for(Control c:controls) {JSONObject item=new JSONObject();
                    item.put("x",c.area.centerX()/(getWidth()/scale));item.put("y",c.area.centerY()/(getHeight()/scale));
                    item.put("size",c.area.width()/c.original.width());data.put(c.id,item);}
                layouts.edit().putString("preset."+(racing?"race.":"menu.")+value,data.toString()).apply();
                saveLayout();Toast.makeText(getContext(),"Layout guardado: "+value,Toast.LENGTH_SHORT).show();dialog.dismiss();
            }catch(org.json.JSONException exception) {name.setError("No se pudo guardar el layout");}
        }));dialog.show();
    }
    private void loadNamedLayout() {
        String prefix="preset."+(racing?"race.":"menu.");ArrayList<String> names=new ArrayList<>();
        for(String key:layouts.getAll().keySet())if(key.startsWith(prefix))names.add(key.substring(prefix.length()));
        java.util.Collections.sort(names);
        if(names.isEmpty()) {Toast.makeText(getContext(),"Todavía no hay layouts guardados para este modo",Toast.LENGTH_SHORT).show();return;}
        new AlertDialog.Builder(getContext()).setTitle("Cargar layout").setItems(names.toArray(new String[0]),(dialog,which)->{
            try {
                JSONObject data=new JSONObject(layouts.getString(prefix+names.get(which),"{}"));
                style=data.getString("style");SharedPreferences.Editor save=layouts.edit().putString("style",style);
                java.util.Iterator<String> ids=data.keys();while(ids.hasNext()) {String id=ids.next();if(id.equals("style"))continue;
                    JSONObject item=data.getJSONObject(id);save.putFloat(id+".x",(float)item.getDouble("x"))
                            .putFloat(id+".y",(float)item.getDouble("y")).putFloat(id+".size",(float)item.getDouble("size"));}
                save.apply();layoutControls();
            }catch(org.json.JSONException exception) {Toast.makeText(getContext(),"No se pudo cargar el layout",Toast.LENGTH_SHORT).show();}
        }).setNegativeButton("Cancelar",null).show();
    }
    private Control hit(float x,float y) {
        for(Control c:controls) if(c.area.contains(x/scale,y/scale)) return c;
        return null;
    }
    private void hold(Control c) { if(c!=null && ++c.held==1 && c.scan>=0) keys.set(c.scan,true); }
    private void release(Control c) { if(c!=null && --c.held==0) {
        if(c.scan>=0)keys.set(c.scan,false);
        else if(c.scan==-3) {c.steering=0;keys.set(0xcb,false);keys.set(0xcd,false);}
    } }
    private void steer(Control c,float x) {
        c.steering=Math.max(-1,Math.min(1,(x/scale-c.area.centerX())/(c.area.width()/2)));
        keys.set(0xcb,c.steering<-.18f);keys.set(0xcd,c.steering>.18f);
    }
    void releaseAll() { for(Control c:fingers.values()) release(c); fingers.clear(); invalidate(); }
    @Override public boolean onTouchEvent(MotionEvent e) {
        if(editing)return editTouch(e);
        int action=e.getActionMasked(),index=e.getActionIndex(),id=e.getPointerId(index);
        if(action==MotionEvent.ACTION_DOWN) {
            float x=e.getX(index)/scale,y=e.getY(index)/scale,w=getWidth()/scale;
            if(x>=w/2-48&&x<=w/2+48&&y>=18&&y<=58) {
                releaseAll();editing=true;selected=null;invalidate();return true;
            }
        }
        if(action==MotionEvent.ACTION_CANCEL) { releaseAll(); return true; }
        if(action==MotionEvent.ACTION_DOWN||action==MotionEvent.ACTION_POINTER_DOWN) {
            Control c=hit(e.getX(index),e.getY(index));if(c!=null&&c.scan==-3&&c.held>0)c=null;
            fingers.put(id,c);hold(c);if(c!=null&&c.scan==-3)steer(c,e.getX(index));
        } else if(action==MotionEvent.ACTION_MOVE) {
            for(int i=0;i<e.getPointerCount();++i) {
                int pointer=e.getPointerId(i); Control old=fingers.get(pointer),next=hit(e.getX(i),e.getY(i));
                if(old!=null&&old.scan==-3) {steer(old,e.getX(i));continue;}
                if(next!=null&&next.scan==-3&&next.held>0)next=null;
                if(old!=next) {release(old);fingers.put(pointer,next);hold(next);if(next!=null&&next.scan==-3)steer(next,e.getX(i));}
            }
        } else if(action==MotionEvent.ACTION_UP||action==MotionEvent.ACTION_POINTER_UP) {
            Control c=fingers.remove(id); release(c);
            if(c!=null && c==hit(e.getX(index),e.getY(index))) {
                if(c.scan==-1) {racing=!racing;layoutControls();}
                else if(c.scan==-2) {releaseAll();textInput.run();}
                performClick();
            }
        }
        invalidate();return true;
    }
    @Override public boolean performClick() { super.performClick();return true; }
}
