package fr.gcstation.telecommande;

// Le telephone devient une vraie manette GameCube dans un jeu lance avec Pulse.
// La console (reglage "Manettes du telephone") donne des ports aux telephones ; Pulse lit le Broadband Adapter a chaque
// image et donne au jeu ce que l'appli envoie : UDP vers la console, port 51237, 60 fois par seconde :
//   "GCPD" {version 1, port 0-3, 0, 0} {numero} {INBUFH : touches, stick} {INBUFL : stick C, gachettes}
// INBUFH : START 0x10000000, Y 0x08000000, X 0x04000000, B 0x02000000, A 0x01000000, L 0x00400000, R 0x00200000,
//          Z 0x00100000, haut 0x00080000, bas 0x00040000, droite 0x00020000, gauche 0x00010000, stick X << 8, stick Y.
// INBUFL : stick C X << 24, stick C Y << 16, gachette L << 8, gachette R.
//
// Commandes (appli build 41) :
//  - stick flottant : il se place la ou le pouce se pose (moitie gauche de l'ecran) ;
//  - diriger en inclinant le telephone (comme un volant) : stick, gauche / droite ;
//  - gachettes progressives : glisser le doigt vers le bas sur L / R enfonce la gachette, au fond : le clic ;
//  - manette Bluetooth ou USB branchee au telephone : ses touches et ses sticks vont au jeu (touches modifiables) ;
//  - disposition modifiable : deplacer, agrandir, changer la fonction ou masquer chaque touche (gardee dans l'appli).

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.RectF;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;

public class ManetteActivity extends Activity implements SensorEventListener {
    static final int PORT = 51237;
    static final String EXTRA_MASK = "ports";

    static final String EXTRA_SETUP = "reglages";

    static void open(Context c, int mask) {
        c.startActivity(new Intent(c, ManetteActivity.class).putExtra(EXTRA_MASK, mask));
    }

    /** reglages et disposition, sans jeu : rien n'est envoye a la console (⚙ de l'appli > Manette : réglages et disposition) */
    static void openSetup(Context c) {
        c.startActivity(new Intent(c, ManetteActivity.class).putExtra(EXTRA_MASK, 0xF).putExtra(EXTRA_SETUP, true));
    }

    // fonctions de la manette GameCube (index = controle par defaut) : 2 A, 3 B, 4 X, 5 Y, 6 START, 7 Z, 8 L, 9 R,
    // 10 haut, 11 bas, 12 gauche, 13 droite ; -1 : aucune (touche masquee)
    static final int[] BIT = { 0, 0, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000, 0x00100000, 0x00400000, 0x00200000,
                               0x00080000, 0x00040000, 0x00010000, 0x00020000, 0, 0, 0 };
    static final String[] LBL = { "", "C", "A", "B", "X", "Y", "START", "Z", "L", "R", "▲", "▼", "◀", "▶", "", "✕", "⚙" };
    static final String[] NAME = { "Stick", "Stick C", "A", "B", "X", "Y", "START", "Z", "L", "R", "Haut", "Bas", "Gauche", "Droite" };
    static final int[] COL = { 0xFFBFC3D0, 0xFFFFD43B, 0xFF2ED47A, 0xFFFF4D6D, 0xFFE6E8EF, 0xFFE6E8EF, 0xFFE6E8EF, 0xFF8E7CFF, 0xFFBFC3D0, 0xFFBFC3D0,
                               0xFFBFC3D0, 0xFFBFC3D0, 0xFFBFC3D0, 0xFFBFC3D0, Neon.CYAN, Neon.PINK, Neon.CYAN };
    static final int N = 17;                                  // 0-13 manette, 14 port, 15 fermer, 16 reglages

    private Pad pad;
    private volatile boolean running;
    private Thread sender;
    private int mask, port;
    private boolean setup;                                    // (reglages sans jeu : pas d'envoi)
    private SharedPreferences sp;
    private FrameLayout root;
    private FrameLayout panel;                                 // (voile sombre plein ecran + la carte des reglages)
    private LinearLayout panelBody, editBar;
    private TextView editFn, editInfo;
    private SensorManager sm;

    // reglages (gardes dans l'appli)
    boolean optFloat, optTrig, optVib = true;
    int optTilt;                                              // 0 non, 1 doux (35 deg), 2 normal (25 deg), 3 fort (15 deg)
    int optAlpha = 100;                                       // opacite des touches (%)
    final int[] btMap = new int[KeyEvent.getMaxKeyCode() + 1];   // touche Android -> fonction (2-13), 0 : rien
    int btCapture = -1;                                       // (fonction qui attend une touche de la manette)

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        mask = getIntent().getIntExtra(EXTRA_MASK, 0x2);
        setup = getIntent().getBooleanExtra(EXTRA_SETUP, false);
        if (mask == 0) mask = 0x2;
        sp = getSharedPreferences("gcs", MODE_PRIVATE);
        port = sp.getInt("manette_port", -1);
        if (port < 0 || (mask >> port & 1) == 0) port = Integer.numberOfTrailingZeros(mask);
        optFloat = sp.getBoolean("pad_float", false);
        optTrig = sp.getBoolean("pad_trig", false);
        optVib = sp.getBoolean("pad_vib", true);
        optRumble = sp.getBoolean("pad_rumble", true);
        optTilt = sp.getInt("pad_tilt", 0);
        optAlpha = sp.getInt("pad_alpha", 100);
        loadBt();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setStatusBarColor(Neon.BG_BOTTOM);
        getWindow().setNavigationBarColor(Neon.BG_BOTTOM);
        root = new FrameLayout(this);
        pad = new Pad(this);
        root.addView(pad, new FrameLayout.LayoutParams(-1, -1));
        buildEditBar();
        buildPanel();
        setContentView(root);
        if (setup) root.post(this::showPanel);
        sm = (SensorManager) getSystemService(SENSOR_SERVICE);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController ic = getWindow().getInsetsController();
            if (ic != null) { ic.hide(WindowInsets.Type.systemBars()); ic.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); }
        }
    }

    @Override protected void onResume() { super.onResume(); if (!setup) startSender(); tiltSensor(); }
    @Override protected void onPause() { super.onPause(); running = false; if (sm != null) sm.unregisterListener(this); rumbleSet(false); }

    @Override
    public void onBackPressed() {
        if (btCapture >= 0) { btCapture = -1; fillPanel(); return; }
        if (panel.getVisibility() == View.VISIBLE) { panel.setVisibility(View.GONE); return; }
        if (pad.edit) { pad.endEdit(); return; }
        super.onBackPressed();
    }

    // ------------------------------------------------------------------ envoi a la console
    private void startSender() {
        if (running) return;
        running = true;
        sender = new Thread(() -> {
            DatagramSocket s = null;
            try {
                s = new DatagramSocket();
                String h = Api.host;
                if (h == null) h = sp.getString("host", null);
                if (h == null) return;
                InetAddress to = InetAddress.getByName(h.contains(":") ? h.substring(0, h.indexOf(':')) : h);
                byte[] buf = new byte[20];
                int seq = (int) (System.nanoTime() & 0x7FFFFFFF);
                long next = System.nanoTime();
                final DatagramSocket rs = s;
                Thread rx = new Thread(() -> rumbleRx(rs), "gcs-vibration");   // (vibration du jeu : la console repond a cette adresse)
                rx.start();
                while (running) {
                    rumbleTick();
                    int[] st = pad.state();
                    buf[0] = 'G'; buf[1] = 'C'; buf[2] = 'P'; buf[3] = 'D';
                    buf[4] = 1; buf[5] = (byte) port; buf[6] = 0; buf[7] = 0;
                    put(buf, 8, ++seq); put(buf, 12, st[0]); put(buf, 16, st[1]);
                    try { s.send(new DatagramPacket(buf, 20, to, PORT)); } catch (Exception e) { }
                    next += 16_666_667L;
                    long w = next - System.nanoTime();
                    if (w > 0) Thread.sleep(w / 1_000_000L, (int) (w % 1_000_000L)); else next = System.nanoTime();
                }
                rumbleSet(false);
                for (int k = 0; k < 3; k++) {                    // (au repos en partant)
                    put(buf, 8, ++seq); put(buf, 12, 0x00808080); put(buf, 16, 0x80800000);
                    try { s.send(new DatagramPacket(buf, 20, to, PORT)); } catch (Exception e) { }
                }
            } catch (Exception e) {
            } finally {
                if (s != null) s.close();
            }
        }, "gcs-manette");
        sender.start();
    }

    // ------------------------------------------------------------------ vibration du jeu (rumble)
    // Pulse voit le jeu allumer le moteur de la manette du port du telephone et renvoie "GCRB" {port, 1 / 0, 0, 0} a
    // l'adresse d'ou viennent les touches (toutes les 8 images tant que ca dure) ; sans nouvelles depuis 350 ms : arret.
    private volatile long rumbleAt;
    private boolean rumbling;
    boolean optRumble = true;

    private void rumbleRx(DatagramSocket s) {
        byte[] b = new byte[64];
        DatagramPacket pk = new DatagramPacket(b, b.length);
        try { s.setSoTimeout(250); } catch (Exception e) { }
        while (running && !s.isClosed()) {
            try {
                s.receive(pk);
                if (pk.getLength() >= 8 && b[0] == 'G' && b[1] == 'C' && b[2] == 'R' && b[3] == 'B' && (b[4] & 3) == port)
                    rumbleAt = b[5] != 0 ? System.nanoTime() : 0;
            } catch (java.net.SocketTimeoutException e) {
            } catch (Exception e) { if (!running) break; }
        }
    }

    private void rumbleTick() {
        boolean want = optRumble && rumbleAt != 0 && System.nanoTime() - rumbleAt < 350_000_000L;
        if (want != rumbling) rumbleSet(want);
    }

    @SuppressWarnings("deprecation")
    private void rumbleSet(boolean on) {
        rumbling = on;
        android.os.Vibrator v = (android.os.Vibrator) getSystemService(VIBRATOR_SERVICE);
        if (v == null || !v.hasVibrator()) return;
        try {
            if (!on) { v.cancel(); return; }
            if (android.os.Build.VERSION.SDK_INT >= 26) v.vibrate(android.os.VibrationEffect.createWaveform(new long[] { 0, 1000 }, 0));
            else v.vibrate(new long[] { 0, 1000 }, 0);
        } catch (Exception e) { }
    }

    private static void put(byte[] b, int o, int v) { b[o] = (byte) (v >>> 24); b[o + 1] = (byte) (v >>> 16); b[o + 2] = (byte) (v >>> 8); b[o + 3] = (byte) v; }

    void nextPort() {
        for (int k = 1; k <= 4; k++) { int p = (port + k) % 4; if ((mask >> p & 1) != 0) { port = p; break; } }
        sp.edit().putInt("manette_port", port).apply();
        pad.invalidate();
    }

    // ------------------------------------------------------------------ inclinaison (volant)
    private void tiltSensor() {
        if (sm == null) return;
        sm.unregisterListener(this);
        pad.tiltX = 0;
        if (optTilt == 0) return;
        Sensor g = sm.getDefaultSensor(Sensor.TYPE_GRAVITY);
        if (g == null) g = sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        if (g != null) sm.registerListener(this, g, SensorManager.SENSOR_DELAY_GAME);
    }

    private float lx, ly;

    @Override
    public void onSensorChanged(SensorEvent e) {
        float ax = e.values[0], ay = e.values[1];
        if (e.sensor.getType() == Sensor.TYPE_ACCELEROMETER) { lx += (ax - lx) * 0.2f; ly += (ay - ly) * 0.2f; ax = lx; ay = ly; }
        int rot = getWindowManager().getDefaultDisplay().getRotation();
        float right, up;                                      // (vers le haut, dans le repere de l'ecran)
        switch (rot) {
            case Surface.ROTATION_90: right = -ay; up = ax; break;
            case Surface.ROTATION_270: right = ay; up = -ax; break;
            case Surface.ROTATION_180: right = -ax; up = -ay; break;
            default: right = ax; up = ay;
        }
        float ang = (float) Math.toDegrees(Math.atan2(-right, up));   // tourner a droite : positif
        float max = optTilt == 1 ? 35f : optTilt == 3 ? 15f : 25f, dead = 3f;
        float v = Math.abs(ang) < dead ? 0 : (ang - Math.signum(ang) * dead) / (max - dead);
        pad.tiltX = Math.max(-1f, Math.min(1f, v));
    }

    @Override public void onAccuracyChanged(Sensor s, int a) { }

    // ------------------------------------------------------------------ manette Bluetooth / USB
    private void loadBt() {
        String m = sp.getString("pad_bt", null);
        if (m == null) {
            btMap[KeyEvent.KEYCODE_BUTTON_A] = 2; btMap[KeyEvent.KEYCODE_BUTTON_B] = 3;
            btMap[KeyEvent.KEYCODE_BUTTON_X] = 4; btMap[KeyEvent.KEYCODE_BUTTON_Y] = 5;
            btMap[KeyEvent.KEYCODE_BUTTON_START] = 6; btMap[KeyEvent.KEYCODE_BUTTON_R1] = 7;
            btMap[KeyEvent.KEYCODE_BUTTON_L1] = 7; btMap[KeyEvent.KEYCODE_BUTTON_SELECT] = 6;
            btMap[KeyEvent.KEYCODE_BUTTON_L2] = 8; btMap[KeyEvent.KEYCODE_BUTTON_R2] = 9;
            btMap[KeyEvent.KEYCODE_DPAD_UP] = 10; btMap[KeyEvent.KEYCODE_DPAD_DOWN] = 11;
            btMap[KeyEvent.KEYCODE_DPAD_LEFT] = 12; btMap[KeyEvent.KEYCODE_DPAD_RIGHT] = 13;
            return;
        }
        for (String kv : m.split(",")) {
            int c = kv.indexOf(':');
            if (c <= 0) continue;
            try { int k = Integer.parseInt(kv.substring(0, c)), f = Integer.parseInt(kv.substring(c + 1)); if (k >= 0 && k < btMap.length) btMap[k] = f; } catch (Exception e) { }
        }
    }

    private void saveBt() {
        StringBuilder s = new StringBuilder();
        for (int k = 0; k < btMap.length; k++) if (btMap[k] != 0) s.append(k).append(':').append(btMap[k]).append(',');
        sp.edit().putString("pad_bt", s.toString()).apply();
    }

    private static boolean fromPad(KeyEvent e) {
        int s = e.getSource();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
            || KeyEvent.isGamepadButton(e.getKeyCode());
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        int k = e.getKeyCode();
        if (!fromPad(e) || k == KeyEvent.KEYCODE_BACK && btCapture < 0 && btMap[k] == 0) return super.dispatchKeyEvent(e);
        if (btCapture >= 0) {
            if (e.getAction() == KeyEvent.ACTION_DOWN && k < btMap.length) {
                for (int i = 0; i < btMap.length; i++) if (btMap[i] == btCapture) btMap[i] = 0;
                btMap[k] = btCapture; btCapture = -1; saveBt(); fillPanel();
            }
            return true;
        }
        if (k >= btMap.length || btMap[k] == 0) return super.dispatchKeyEvent(e);
        if (e.getRepeatCount() == 0) pad.btKey(btMap[k], e.getAction() == KeyEvent.ACTION_DOWN);
        return true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK || e.getAction() != MotionEvent.ACTION_MOVE)
            return super.dispatchGenericMotionEvent(e);
        InputDevice d = e.getDevice();
        float x = axis(e, d, MotionEvent.AXIS_X), y = axis(e, d, MotionEvent.AXIS_Y);
        float cx = axis(e, d, MotionEvent.AXIS_Z), cy = axis(e, d, MotionEvent.AXIS_RZ);
        if (cx == 0 && cy == 0) { cx = axis(e, d, MotionEvent.AXIS_RX); cy = axis(e, d, MotionEvent.AXIS_RY); }
        float tl = Math.max(axis(e, d, MotionEvent.AXIS_LTRIGGER), axis(e, d, MotionEvent.AXIS_BRAKE));
        float tr = Math.max(axis(e, d, MotionEvent.AXIS_RTRIGGER), axis(e, d, MotionEvent.AXIS_GAS));
        float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X), hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        pad.btAxes(x, y, cx, cy, tl, tr, hx, hy);
        return true;
    }

    private static float axis(MotionEvent e, InputDevice d, int a) {
        float v = e.getAxisValue(a);
        InputDevice.MotionRange r = d != null ? d.getMotionRange(a, e.getSource()) : null;
        float flat = r != null ? Math.max(r.getFlat(), 0.08f) : 0.1f;
        return Math.abs(v) <= flat ? 0 : v;
    }

    // ------------------------------------------------------------------ panneau des reglages
    private TextView row(String s, View.OnClickListener l) {
        TextView t = Neon.btn(this, s, 14, false, l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = Neon.dp(this, 6);
        t.setLayoutParams(lp);
        return t;
    }

    private void buildPanel() {
        panel = new FrameLayout(this);
        panel.setBackgroundColor(0xE6000000);                 // (voile : la manette ne se voit plus a travers)
        panel.setClickable(true);
        panel.setOnClickListener(v -> { if (btCapture < 0) closePanel(); });
        ScrollView sv = new ScrollView(this);
        sv.setBackground(NeonDialog.frame(this, 18));          // (opaque)
        panelBody = new LinearLayout(this);
        panelBody.setOrientation(LinearLayout.VERTICAL);
        int p = Neon.dp(this, 14);
        panelBody.setPadding(p, p, p, p);
        sv.addView(panelBody);
        sv.setClickable(true);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(Neon.dp(this, 440), -1, Gravity.CENTER_HORIZONTAL);
        lp.topMargin = lp.bottomMargin = Neon.dp(this, 10);
        panel.addView(sv, lp);
        panel.setVisibility(View.GONE);
        root.addView(panel, new FrameLayout.LayoutParams(-1, -1));
    }

    private void closePanel() {
        btCapture = -1;
        panel.setVisibility(View.GONE);
    }

    void showPanel() { fillPanel(); panel.setVisibility(View.VISIBLE); }

    private static final String[] TILT = { "non", "doux", "normal", "fort" };

    private void fillPanel() {
        LinearLayout b = panelBody;
        b.removeAllViews();
        b.addView(Neon.glow(this, "Réglages de la manette", 18, Neon.TEXT));
        b.addView(row("Stick flottant (il se place sous le pouce) : " + (optFloat ? "oui" : "non"), v -> {
            optFloat = !optFloat; sp.edit().putBoolean("pad_float", optFloat).apply(); fillPanel(); }));
        b.addView(row("Diriger en inclinant le téléphone : " + TILT[optTilt], v -> {
            optTilt = (optTilt + 1) % 4; sp.edit().putInt("pad_tilt", optTilt).apply(); tiltSensor(); fillPanel(); }));
        b.addView(row("Gâchettes progressives (glisser vers le bas) : " + (optTrig ? "oui" : "non"), v -> {
            optTrig = !optTrig; sp.edit().putBoolean("pad_trig", optTrig).apply(); fillPanel(); }));
        b.addView(row("Vibration du jeu (le téléphone vibre comme la manette) : " + (optRumble ? "oui" : "non"), v -> {
            optRumble = !optRumble; sp.edit().putBoolean("pad_rumble", optRumble).apply(); fillPanel(); }));
        b.addView(row("Vibration des touches : " + (optVib ? "oui" : "non"), v -> {
            optVib = !optVib; sp.edit().putBoolean("pad_vib", optVib).apply(); fillPanel(); }));
        b.addView(row("Opacité des touches : " + optAlpha + " %", v -> {
            optAlpha = optAlpha <= 40 ? 100 : optAlpha - 20; sp.edit().putInt("pad_alpha", optAlpha).apply(); pad.invalidate(); fillPanel(); }));
        b.addView(row("✎  Modifier la disposition des touches", v -> { panel.setVisibility(View.GONE); pad.startEdit(); }));
        b.addView(row("Remettre la disposition d'origine", v -> { pad.resetLayout(); fillPanel(); }));
        TextView h = Neon.text(this, "Manette Bluetooth ou USB branchée au téléphone : sticks, gâchettes et croix vont au jeu. Touches :", 13, Neon.DIM, false);
        h.setPadding(0, Neon.dp(this, 14), 0, 0);
        b.addView(h);
        for (int f = 2; f <= 13; f++) {
            final int fn = f;
            StringBuilder keys = new StringBuilder();
            for (int k = 0; k < btMap.length; k++) if (btMap[k] == f) { if (keys.length() > 0) keys.append(", "); keys.append(keyName(k)); }
            String s = btCapture == f ? NAME[f] + " : appuie sur un bouton de la manette…" : NAME[f] + " : " + (keys.length() > 0 ? keys : "aucun") + "   (changer)";
            b.addView(row(s, v -> { btCapture = btCapture == fn ? -1 : fn; fillPanel(); }));
        }
        b.addView(row("Touches de la manette d'origine", v -> {
            java.util.Arrays.fill(btMap, 0); sp.edit().remove("pad_bt").apply(); loadBt(); fillPanel(); }));
        b.addView(row(setup ? "Fermer (la manette s'affiche pour essayer)" : "Fermer", v -> closePanel()));
    }

    private static String keyName(int k) {
        String s = KeyEvent.keyCodeToString(k);
        if (s.startsWith("KEYCODE_BUTTON_")) s = s.substring(15);
        else if (s.startsWith("KEYCODE_DPAD_")) s = "croix " + s.substring(13).toLowerCase();
        else if (s.startsWith("KEYCODE_")) s = s.substring(8);
        return s;
    }

    // ------------------------------------------------------------------ barre du mode disposition
    private void buildEditBar() {
        editBar = new LinearLayout(this);
        editBar.setOrientation(LinearLayout.VERTICAL);
        editBar.setGravity(Gravity.CENTER_HORIZONTAL);
        editBar.setBackground(NeonDialog.frame(this, 16));
        int p = Neon.dp(this, 8);
        editBar.setPadding(p, p, p, p);
        editInfo = Neon.text(this, "", 12, Neon.DIM, false);
        editInfo.setGravity(Gravity.CENTER);
        editBar.addView(editInfo);
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.addView(small("− Taille", v -> pad.resize(1 / 1.12f)));
        r.addView(small("Taille +", v -> pad.resize(1.12f)));
        editFn = small("Fonction", v -> pad.cycleFn());
        r.addView(editFn);
        r.addView(small("Origine", v -> pad.resetOne()));
        r.addView(small("Terminé", v -> pad.endEdit()));
        editBar.addView(r);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(-2, -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        lp.topMargin = Neon.dp(this, 6);
        editBar.setVisibility(View.GONE);
        root.addView(editBar, lp);
    }

    private TextView small(String s, View.OnClickListener l) {
        TextView t = Neon.btn(this, s, 13, false, l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
        lp.leftMargin = lp.rightMargin = Neon.dp(this, 3);
        lp.topMargin = Neon.dp(this, 6);
        t.setLayoutParams(lp);
        return t;
    }

    // ------------------------------------------------------------------ la manette dessinee
    final class Pad extends View {
        // controles : 0 stick, 1 stick C, 2-13 touches, 14 port, 15 fermer, 16 reglages
        private final float[][] at = new float[N][4];         // cx, cy, rayon (ou demi-largeur), demi-hauteur (rectangles)
        private final float[][] base = new float[N][4];       // (position d'origine : pour les reglages "origine")
        private final float[] lx = new float[14], ly = new float[14], ls = new float[14];   // disposition gardee : x, y (0-1), taille ; -1 : origine
        private final int[] fn = new int[14];                 // fonction de chaque touche (2-13), -1 : masquee
        private final int[] owner = new int[N];               // (pointeur qui tient le controle, -1)
        private final float[] trig = new float[14];           // (gachettes progressives : 0-1 sous chaque touche)
        private float sx, sy, cx, cy;                          // sticks : -1..1
        private float fx_, fy; private boolean floating;        // (stick flottant : ou il s'est pose)
        volatile float tiltX;
        // manette Bluetooth / USB
        private final boolean[] btOn = new boolean[14];
        private float bsx, bsy, bcx, bcy, btl, btr, bhx, bhy;
        boolean edit; private int sel = -1; private float dragX, dragY;
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG), t = new Paint(Paint.ANTI_ALIAS_FLAG), edge = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint dash = new Paint(Paint.ANTI_ALIAS_FLAG);

        Pad(Context c) {
            super(c);
            setBackgroundColor(Neon.BG_BOTTOM);
            t.setTextAlign(Paint.Align.CENTER);
            t.setFakeBoldText(true);
            for (int i = 0; i < N; i++) owner[i] = -1;
            for (int i = 0; i < 14; i++) { fn[i] = i; lx[i] = ly[i] = -1; ls[i] = 1; }
            String s = sp.getString("pad_lay", null);
            if (s != null) {
                String[] e = s.split("\\|");
                for (int i = 0; i < 14 && i < e.length; i++) {
                    String[] v = e[i].split(";");
                    if (v.length < 4) continue;
                    try { lx[i] = Float.parseFloat(v[0]); ly[i] = Float.parseFloat(v[1]); ls[i] = Float.parseFloat(v[2]); fn[i] = Integer.parseInt(v[3]); } catch (Exception x) { }
                    if (i >= 2 && (fn[i] < -1 || fn[i] > 13 || fn[i] == 0 || fn[i] == 1)) fn[i] = i;
                    if (i < 2) fn[i] = i;
                    if (ls[i] < 0.4f || ls[i] > 2.5f) ls[i] = 1;
                }
            }
            dash.setStyle(Paint.Style.STROKE);
            dash.setPathEffect(new DashPathEffect(new float[] { 12, 10 }, 0));
        }

        private void saveLayout() {
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < 14; i++) s.append(lx[i]).append(';').append(ly[i]).append(';').append(ls[i]).append(';').append(fn[i]).append('|');
            sp.edit().putString("pad_lay", s.toString()).apply();
        }

        /** {INBUFH, INBUFL} de l'etat actuel : ecran, inclinaison et manette Bluetooth ensemble */
        synchronized int[] state() {
            int h = 0;
            float tl = 0, tr = 0;
            for (int i = 2; i < 14; i++) {
                int f = fn[i];
                if (f < 2) continue;
                boolean on = owner[i] >= 0;
                if (!on) continue;
                if (f == 8 || f == 9) {
                    float v = optTrig ? trig[i] : 1f;
                    if (f == 8) tl = Math.max(tl, v); else tr = Math.max(tr, v);
                    if (v >= 0.92f) h |= BIT[f];
                } else h |= BIT[f];
            }
            for (int f = 2; f < 14; f++) if (btOn[f]) { h |= BIT[f]; if (f == 8) tl = 1; if (f == 9) tr = 1; }
            if (bhy < -0.5f) h |= BIT[10]; if (bhy > 0.5f) h |= BIT[11]; if (bhx < -0.5f) h |= BIT[12]; if (bhx > 0.5f) h |= BIT[13];
            tl = Math.max(tl, btl); tr = Math.max(tr, btr);
            if (btl >= 0.92f) h |= BIT[8];
            if (btr >= 0.92f) h |= BIT[9];
            float x = sx, y = sy;
            if (owner[0] < 0) {
                if (bsx != 0 || bsy != 0) { x = bsx; y = bsy; }
                else if (tiltX != 0) x = tiltX;
            }
            float ccx = owner[1] < 0 && (bcx != 0 || bcy != 0) ? bcx : cx, ccy = owner[1] < 0 && (bcx != 0 || bcy != 0) ? bcy : cy;
            int ix = 0x80 + Math.round(clamp(x) * 100), iy = 0x80 - Math.round(clamp(y) * 100);
            int icx = 0x80 + Math.round(clamp(ccx) * 100), icy = 0x80 - Math.round(clamp(ccy) * 100);
            int itl = Math.round(clamp(tl) * 0xE0), itr = Math.round(clamp(tr) * 0xE0);
            if (itl < 0) itl = 0; if (itr < 0) itr = 0;
            return new int[] { h | ix << 8 | iy, icx << 24 | icy << 16 | itl << 8 | itr };
        }
        private float clamp(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }

        synchronized void btKey(int f, boolean down) {
            if (f >= 2 && f < 14) btOn[f] = down;
        }
        synchronized void btAxes(float x, float y, float cx_, float cy_, float tl, float tr, float hx, float hy) {
            bsx = x; bsy = y; bcx = cx_; bcy = cy_; btl = tl; btr = tr; bhx = hx; bhy = hy;
            postInvalidate();
        }

        @Override
        protected void onSizeChanged(int w, int h, int ow, int oh) {
            float H = Math.min(h, w * 0.5f);
            set(0, 0.17f * w, 0.47f * h, 0.19f * H, 0);         // stick
            set(1, 0.66f * w, 0.78f * h, 0.12f * H, 0);         // stick C
            set(2, 0.82f * w, 0.50f * h, 0.12f * H, 0);         // A
            set(3, 0.69f * w, 0.60f * h, 0.07f * H, 0);         // B
            set(4, 0.94f * w, 0.40f * h, 0.07f * H, 0);         // X
            set(5, 0.79f * w, 0.27f * h, 0.07f * H, 0);         // Y
            set(6, 0.50f * w, 0.56f * h, 0.055f * H, 0);        // START
            set(7, 0.92f * w, 0.20f * h, 0.06f * w, 0.045f * h); // Z
            set(8, 0.15f * w, 0.08f * h, 0.13f * w, 0.06f * h);  // L
            set(9, 0.85f * w, 0.08f * h, 0.13f * w, 0.06f * h);  // R
            float dx = 0.34f * w, dy = 0.80f * h, d = 0.075f * H;
            set(10, dx, dy - d, d * 0.6f, d * 0.6f); set(11, dx, dy + d, d * 0.6f, d * 0.6f);
            set(12, dx - d, dy, d * 0.6f, d * 0.6f); set(13, dx + d, dy, d * 0.6f, d * 0.6f);
            set(14, 0.50f * w, 0.10f * h, 0.11f * w, 0.06f * h);  // port (appui : port suivant)
            set(15, 0.57f * w, 0.88f * h, 0.05f * H, 0);         // fermer
            set(16, 0.43f * w, 0.88f * h, 0.05f * H, 0);         // reglages
            for (int i = 0; i < N; i++) System.arraycopy(at[i], 0, base[i], 0, 4);
            apply();
        }
        private void set(int i, float x, float y, float r, float hh) { at[i][0] = x; at[i][1] = y; at[i][2] = r; at[i][3] = hh; }

        /** la disposition gardee par-dessus l'origine */
        private void apply() {
            int w = getWidth(), h = getHeight();
            if (w == 0) return;
            for (int i = 0; i < 14; i++) {
                at[i][0] = lx[i] >= 0 ? lx[i] * w : base[i][0];
                at[i][1] = ly[i] >= 0 ? ly[i] * h : base[i][1];
                at[i][2] = base[i][2] * ls[i];
                at[i][3] = base[i][3] * ls[i];
            }
        }

        private int hit(float x, float y) {
            int best = -1; float bd = Float.MAX_VALUE;
            for (int i = 0; i < N; i++) {
                if (i >= 2 && i < 14 && fn[i] < 0 && !edit) continue;
                if (edit && i >= 14) continue;
                float[] a = at[i];
                float d;
                if (a[3] > 0) { float ex = Math.max(0, Math.abs(x - a[0]) - a[2]), ey = Math.max(0, Math.abs(y - a[1]) - a[3]); d = (float) Math.hypot(ex, ey); }
                else d = Math.max(0, (float) Math.hypot(x - a[0], y - a[1]) - a[2]);
                float tol = (i <= 1 ? 1.3f : 0.45f) * (a[3] > 0 ? a[3] : a[2]);   // (un peu de marge autour, plus pour les sticks)
                if (d <= tol && d < bd) { bd = d; best = i; }
            }
            return best;
        }

        private void buzz() { if (optVib) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); }

        @Override
        public synchronized boolean onTouchEvent(MotionEvent e) {
            if (edit) return editTouch(e);
            int act = e.getActionMasked(), idx = e.getActionIndex();
            switch (act) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: {
                    int id = e.getPointerId(idx);
                    float x = e.getX(idx), y = e.getY(idx);
                    int c = hit(x, y);
                    if (c < 0 && optFloat && owner[0] < 0 && x < getWidth() * 0.45f && y > getHeight() * 0.2f) {   // stick flottant
                        c = 0; floating = true; fx_ = x; fy = y;
                    }
                    if (c < 0 || owner[c] >= 0) break;
                    if (c == 14) { nextPort(); break; }
                    if (c == 15) { finish(); break; }
                    if (c == 16) { showPanel(); break; }
                    owner[c] = id;
                    if (c == 0 && optFloat && !floating) { floating = true; fx_ = x; fy = y; }
                    if (c <= 1) stick(c, x, y);
                    else { trig[c] = trigAt(c, y); buzz(); }
                    break;
                }
                case MotionEvent.ACTION_MOVE:
                    for (int k = 0; k < e.getPointerCount(); k++) {
                        int id = e.getPointerId(k);
                        for (int c = 0; c < 14; c++) if (owner[c] == id) {
                            if (c <= 1) stick(c, e.getX(k), e.getY(k));
                            else if (optTrig && (fn[c] == 8 || fn[c] == 9)) {
                                float v = trigAt(c, e.getY(k));
                                if (v >= 0.92f && trig[c] < 0.92f) buzz();    // (le clic au fond)
                                trig[c] = v;
                            }
                        }
                    }
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP:
                case MotionEvent.ACTION_CANCEL: {
                    for (int c = 0; c < N; c++) {
                        if (owner[c] < 0) continue;
                        if (act == MotionEvent.ACTION_CANCEL || act == MotionEvent.ACTION_UP || owner[c] == e.getPointerId(idx)) {
                            owner[c] = -1;
                            if (c == 0) { sx = sy = 0; floating = false; } else if (c == 1) { cx = cy = 0; }
                            else if (c < 14) trig[c] = 0;
                        }
                    }
                    break;
                }
            }
            invalidate();
            return true;
        }

        /** gachette progressive : enfoncement selon la hauteur du doigt sur la touche (haut : leger, bas : au fond) */
        private float trigAt(int c, float y) {
            float[] a = at[c];
            float hh = a[3] > 0 ? a[3] : a[2];
            float v = 0.25f + (y - (a[1] - hh)) / (2 * hh) * 0.9f;
            return v < 0.15f ? 0.15f : v > 1 ? 1 : v;
        }

        private void stick(int c, float x, float y) {
            float[] a = at[c];
            float ox = c == 0 && floating ? fx_ : a[0], oy = c == 0 && floating ? fy : a[1];
            float dx = (x - ox) / a[2], dy = (y - oy) / a[2], m = (float) Math.hypot(dx, dy);
            if (m > 1) { dx /= m; dy /= m; }
            if (c == 0) { sx = dx; sy = dy; } else { cx = dx; cy = dy; }
        }

        // -------- mode disposition : toucher une touche la choisit, glisser la deplace
        void startEdit() {
            edit = true; sel = -1;
            for (int i = 0; i < N; i++) owner[i] = -1;
            sx = sy = cx = cy = 0; floating = false;
            editBar.setVisibility(View.VISIBLE);
            editInfo();
            invalidate();
        }
        void endEdit() {
            edit = false; sel = -1;
            editBar.setVisibility(View.GONE);
            saveLayout();
            invalidate();
        }
        private void editInfo() {
            if (sel < 0) { editInfo.setText("Touche une touche pour la choisir, glisse-la pour la déplacer"); editFn.setText("Fonction"); return; }
            editInfo.setText("Choisie : " + NAME[sel] + (sel >= 10 ? " (la croix bouge en entier)" : ""));
            editFn.setText(sel < 2 ? "Fonction : " + NAME[sel] : "Fonction : " + (fn[sel] < 0 ? "masquée" : NAME[fn[sel]]));
        }
        private boolean dpad(int i) { return i >= 10 && i <= 13; }

        private boolean editTouch(MotionEvent e) {
            int act = e.getActionMasked();
            float x = e.getX(), y = e.getY();
            if (act == MotionEvent.ACTION_DOWN) {
                int c = hit(x, y);
                sel = c; dragX = x; dragY = y;
                editInfo();
            } else if (act == MotionEvent.ACTION_MOVE && sel >= 0 && e.getPointerCount() == 1) {
                float ddx = x - dragX, ddy = y - dragY;
                dragX = x; dragY = y;
                int w = getWidth(), h = getHeight();
                for (int i = 0; i < 14; i++) {
                    if (i != sel && !(dpad(sel) && dpad(i))) continue;
                    float nx = Math.max(0, Math.min(w, at[i][0] + ddx)), ny = Math.max(0, Math.min(h, at[i][1] + ddy));
                    lx[i] = nx / w; ly[i] = ny / h;
                }
                apply();
            } else if (act == MotionEvent.ACTION_UP) {
                saveLayout();
            }
            invalidate();
            return true;
        }

        synchronized void resize(float k) {
            if (sel < 0) return;
            int w = getWidth(), h = getHeight();
            if (dpad(sel)) {                                 // la croix : les 4 touches et leur ecart
                float gx = 0, gy = 0;
                for (int i = 10; i <= 13; i++) { gx += at[i][0] / 4; gy += at[i][1] / 4; }
                float ns = Math.max(0.4f, Math.min(2.5f, ls[10] * k)), kk = ns / ls[10];
                for (int i = 10; i <= 13; i++) {
                    ls[i] = ns;
                    lx[i] = (gx + (at[i][0] - gx) * kk) / w; ly[i] = (gy + (at[i][1] - gy) * kk) / h;
                }
            } else ls[sel] = Math.max(0.4f, Math.min(2.5f, ls[sel] * k));
            apply(); saveLayout(); invalidate();
        }
        synchronized void cycleFn() {
            if (sel < 2 || sel > 13) return;
            int f = fn[sel];
            f = f < 0 ? 2 : f == 13 ? -1 : f + 1;            // A B X Y START Z L R haut bas gauche droite, masquee
            fn[sel] = f;
            editInfo(); saveLayout(); invalidate();
        }
        synchronized void resetOne() {
            if (sel < 0) return;
            for (int i = 0; i < 14; i++) if (i == sel || (dpad(sel) && dpad(i))) { lx[i] = ly[i] = -1; ls[i] = 1; fn[i] = i; }
            apply(); editInfo(); saveLayout(); invalidate();
        }
        synchronized void resetLayout() {
            for (int i = 0; i < 14; i++) { lx[i] = ly[i] = -1; ls[i] = 1; fn[i] = i; }
            apply(); saveLayout(); invalidate();
        }

        private int dark(int c) { return 0xFF000000 | ((c >> 16 & 255) * 9 / 20) << 16 | ((c >> 8 & 255) * 9 / 20) << 8 | (c & 255) * 9 / 20; }
        /** c melangee avec m (0-1 : part de m), alpha a */
        private int mix(int c, int m, float k, int a) {
            int r = Math.round((c >> 16 & 255) * (1 - k) + (m >> 16 & 255) * k), gg = Math.round((c >> 8 & 255) * (1 - k) + (m >> 8 & 255) * k), bb = Math.round((c & 255) * (1 - k) + (m & 255) * k);
            return a << 24 | r << 16 | gg << 8 | bb;
        }
        private final Paint fx = new Paint(Paint.ANTI_ALIAS_FLAG);
        private android.graphics.Shader bgShader; private int bgW, bgH;

        /** fond : degrade sombre, une lueur violette au centre */
        private void drawBg(Canvas g) {
            int w = getWidth(), h = getHeight();
            if (bgShader == null || bgW != w || bgH != h) {
                bgW = w; bgH = h;
                bgShader = new android.graphics.RadialGradient(w / 2f, h * 0.55f, Math.max(w, h) * 0.65f,
                    new int[] { 0xFF1A1036, 0xFF0C0820, 0xFF05030E }, new float[] { 0f, 0.55f, 1f }, android.graphics.Shader.TileMode.CLAMP);
            }
            fx.setShader(bgShader); fx.setStyle(Paint.Style.FILL);
            g.drawRect(0, 0, w, h, fx);
            fx.setShader(null);
        }

        /** lueur douce autour d'une touche appuyee */
        private void glow(Canvas g, float x, float y, float r, int col, int alpha) {
            fx.setShader(new android.graphics.RadialGradient(x, y, r, new int[] { mix(col, col, 0, alpha * 110 / 255), mix(col, col, 0, 0) }, new float[] { 0.45f, 1f }, android.graphics.Shader.TileMode.CLAMP));
            g.drawCircle(x, y, r, fx);
            fx.setShader(null);
        }

        /** touche ronde en verre : reflet en haut, anneau de couleur ; appuyee : pleine, plus petite, lumineuse */
        private void roundBtn(Canvas g, float x, float y, float r, int col, boolean on, int alpha) {
            if (on) { glow(g, x, y, r * 1.55f, col, alpha); r *= 0.93f; }
            fx.setStyle(Paint.Style.FILL);
            fx.setShader(new android.graphics.LinearGradient(x, y - r, x, y + r,
                on ? new int[] { mix(col, 0xFFFFFFFF, 0.35f, alpha), mix(col, 0xFF000000, 0.15f, alpha) }
                   : new int[] { mix(col, 0xFF140C26, 0.70f, alpha), mix(col, 0xFF07040F, 0.88f, alpha) },
                null, android.graphics.Shader.TileMode.CLAMP));
            g.drawCircle(x, y, r, fx);
            fx.setShader(new android.graphics.LinearGradient(x, y - r, x, y, mix(0xFFFFFFFF, 0, 0, on ? alpha * 70 / 255 : alpha * 40 / 255), 0x00FFFFFF, android.graphics.Shader.TileMode.CLAMP));
            g.drawOval(x - r * 0.78f, y - r * 0.92f, x + r * 0.78f, y + r * 0.05f, fx);   // (reflet)
            fx.setShader(null);
            fx.setStyle(Paint.Style.STROKE); fx.setStrokeWidth(Math.max(2f, r * 0.07f));
            fx.setColor(mix(col, 0xFFFFFFFF, on ? 0.5f : 0.1f, alpha * (on ? 255 : 200) / 255));
            g.drawCircle(x, y, r - fx.getStrokeWidth() / 2, fx);
            fx.setStyle(Paint.Style.FILL);
        }

        /** touche rectangulaire arrondie, meme style */
        private void rectBtn(Canvas g, RectF rf, float rad, int col, boolean on, int alpha) {
            if (on) {
                RectF gr = new RectF(rf); gr.inset(-rf.height() * 0.25f, -rf.height() * 0.25f);
                fx.setColor(mix(col, col, 0, alpha * 70 / 255));
                fx.setMaskFilter(new android.graphics.BlurMaskFilter(rf.height() * 0.3f, android.graphics.BlurMaskFilter.Blur.NORMAL));
                g.drawRoundRect(gr, rad * 1.4f, rad * 1.4f, fx);
                fx.setMaskFilter(null);
                rf = new RectF(rf); rf.inset(rf.width() * 0.03f, rf.height() * 0.06f);
            }
            fx.setStyle(Paint.Style.FILL);
            fx.setShader(new android.graphics.LinearGradient(0, rf.top, 0, rf.bottom,
                on ? new int[] { mix(col, 0xFFFFFFFF, 0.35f, alpha), mix(col, 0xFF000000, 0.15f, alpha) }
                   : new int[] { mix(col, 0xFF140C26, 0.70f, alpha), mix(col, 0xFF07040F, 0.88f, alpha) },
                null, android.graphics.Shader.TileMode.CLAMP));
            g.drawRoundRect(rf, rad, rad, fx);
            fx.setShader(new android.graphics.LinearGradient(0, rf.top, 0, rf.centerY(), mix(0xFFFFFFFF, 0, 0, on ? alpha * 70 / 255 : alpha * 36 / 255), 0x00FFFFFF, android.graphics.Shader.TileMode.CLAMP));
            g.drawRoundRect(new RectF(rf.left + rad * 0.3f, rf.top + 2, rf.right - rad * 0.3f, rf.centerY()), rad * 0.8f, rad * 0.8f, fx);
            fx.setShader(null);
            fx.setStyle(Paint.Style.STROKE); fx.setStrokeWidth(Math.max(2f, rf.height() * 0.05f));
            fx.setColor(mix(col, 0xFFFFFFFF, on ? 0.5f : 0.1f, alpha * (on ? 255 : 200) / 255));
            float h2 = fx.getStrokeWidth() / 2;
            g.drawRoundRect(new RectF(rf.left + h2, rf.top + h2, rf.right - h2, rf.bottom - h2), rad, rad, fx);
            fx.setStyle(Paint.Style.FILL);
        }

        /** stick : socle creuse avec le cran octogonal de la GameCube, tete bombee */
        private void stickDraw(Canvas g, float ox, float oy, float r, float kx, float ky, int col, boolean on, int alpha, boolean c) {
            fx.setStyle(Paint.Style.FILL);
            fx.setShader(new android.graphics.RadialGradient(ox, oy, r, new int[] { mix(0xFF05030C, 0, 0, alpha), mix(0xFF1B1430, 0, 0, alpha) }, new float[] { 0.55f, 1f }, android.graphics.Shader.TileMode.CLAMP));
            g.drawCircle(ox, oy, r, fx);
            fx.setShader(null);
            android.graphics.Path oct = new android.graphics.Path();
            for (int k = 0; k < 8; k++) {
                double an = Math.PI / 8 + k * Math.PI / 4;
                float px = ox + (float) Math.cos(an) * r * 0.82f, py = oy + (float) Math.sin(an) * r * 0.82f;
                if (k == 0) oct.moveTo(px, py); else oct.lineTo(px, py);
            }
            oct.close();
            fx.setStyle(Paint.Style.STROKE); fx.setStrokeWidth(Math.max(1.5f, r * 0.025f));
            fx.setColor(mix(col, 0xFF000000, 0.3f, alpha * 110 / 255));
            g.drawPath(oct, fx);
            fx.setStrokeWidth(Math.max(2f, r * 0.04f));
            fx.setColor(mix(col, 0xFFFFFFFF, on ? 0.4f : 0f, alpha * (on ? 230 : 150) / 255));
            g.drawCircle(ox, oy, r - fx.getStrokeWidth() / 2, fx);
            float hx = ox + kx * r * 0.62f, hy = oy + ky * r * 0.62f, hr = r * (c ? 0.36f : 0.42f);
            if (on) glow(g, hx, hy, hr * 1.7f, col, alpha);
            fx.setStyle(Paint.Style.FILL);
            fx.setColor(mix(0xFF000000, 0, 0, alpha * 90 / 255));
            g.drawCircle(hx, hy + hr * 0.12f, hr * 1.04f, fx);    // (ombre)
            fx.setShader(new android.graphics.RadialGradient(hx - hr * 0.3f, hy - hr * 0.35f, hr * 1.4f,
                new int[] { mix(col, 0xFFFFFFFF, 0.55f, alpha), mix(col, 0xFF000000, 0.05f, alpha), mix(col, 0xFF000000, 0.45f, alpha) }, new float[] { 0f, 0.55f, 1f }, android.graphics.Shader.TileMode.CLAMP));
            g.drawCircle(hx, hy, hr, fx);
            fx.setShader(null);
            if (!c) {                                         // (tete du stick : stries comme la vraie)
                fx.setStyle(Paint.Style.STROKE); fx.setStrokeWidth(Math.max(1f, hr * 0.05f));
                fx.setColor(mix(0xFF000000, 0, 0, alpha * 50 / 255));
                g.drawCircle(hx, hy, hr * 0.62f, fx);
                g.drawCircle(hx, hy, hr * 0.34f, fx);
                fx.setStyle(Paint.Style.FILL);
            } else {
                t.setColor(mix(0xFF000000, 0, 0, alpha * 150 / 255)); t.setTextSize(hr * 0.9f);
                g.drawText("C", hx, hy + hr * 0.32f, t);
            }
        }

        @Override
        protected synchronized void onDraw(Canvas g) {
            drawBg(g);
            int al = Math.round(optAlpha * 2.55f);
            t.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.BOLD));
            for (int i = 0; i < N; i++) {
                float[] a = at[i];
                if (edit && i >= 14) continue;
                int f = i < 14 ? fn[i] : i;
                boolean hidden = i >= 2 && i < 14 && f < 0;
                if (hidden && !edit) continue;
                boolean on = owner[i] >= 0 || (i >= 2 && i < 14 && f >= 2 && btOn[f]);
                int col = hidden ? 0xFF555566 : COL[f < 0 ? i : f];
                if (f == 4 || f == 5 || f == 6) col = 0xFFB9B4D6;            // (X, Y, START : gris lavande)
                if (f >= 8 && f <= 13) col = 0xFF9AA3C7;                       // (L, R, croix)
                int alpha = i >= 14 ? 255 : al;
                if (hidden) alpha = 110;
                if (i <= 1) {
                    float ox = i == 0 && floating ? fx_ : a[0], oy = i == 0 && floating ? fy : a[1];
                    float kx = i == 0 ? sx : cx, ky = i == 0 ? sy : cy;
                    if (i == 0 && owner[0] < 0) { if (bsx != 0 || bsy != 0) { kx = bsx; ky = bsy; } else kx = tiltX; }
                    if (i == 1 && owner[1] < 0 && (bcx != 0 || bcy != 0)) { kx = bcx; ky = bcy; }
                    stickDraw(g, ox, oy, a[2], kx, ky, i == 0 ? 0xFFC9CEE8 : 0xFFFFD43B, on, alpha, i == 1);
                } else if (a[3] > 0) {
                    float r = Math.min(a[2], a[3]) * 0.9f;
                    RectF rf = new RectF(a[0] - a[2], a[1] - a[3], a[0] + a[2], a[1] + a[3]);
                    rectBtn(g, rf, r, col, on, alpha);
                    if (optTrig && on && i < 14 && (f == 8 || f == 9) && owner[i] >= 0) {   // (enfoncement de la gachette)
                        fx.setColor(mix(0xFFFFFFFF, 0, 0, 170));
                        float bh = rf.height() * 0.12f;
                        g.drawRoundRect(new RectF(rf.left + r * 0.5f, rf.bottom - bh * 2, rf.left + r * 0.5f + (rf.width() - r) * trig[i], rf.bottom - bh), bh / 2, bh / 2, fx);
                    }
                } else roundBtn(g, a[0], a[1], a[2], col, on, alpha);
                if (edit && i == sel) {
                    dash.setStrokeWidth(getHeight() * 0.006f); dash.setColor(Neon.CYAN);
                    float rr = (a[3] > 0 ? Math.max(a[2], a[3]) : a[2]) * 1.15f;
                    g.drawCircle(a[0], a[1], rr, dash);
                }
                String l = i == 14 ? "PORT " + (port + 1) + "  ↻" : hidden ? "—" : LBL[f];
                if (l.isEmpty() || (i <= 1)) continue;
                int lc = on ? 0xFF0B0B14 : (f == 2 || f == 3 || f == 7) ? mix(col, 0xFFFFFFFF, 0.25f, 255) : 0xFFEDEBFA;
                if (i >= 14) lc = 0xFFFFFFFF;
                t.setColor(lc);
                t.setAlpha(alpha);
                float sz = (a[3] > 0 ? a[3] : a[2]) * (f == 6 ? 0.5f : i == 14 ? 0.75f : f >= 10 && f <= 13 ? 0.75f : 0.85f);
                if (on && a[3] <= 0) sz *= 0.93f;
                t.setTextSize(sz);
                g.drawText(l, a[0], a[1] + sz * 0.36f, t);
            }
            t.setAlpha(255);
            t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            if (!edit) {
                t.setTextSize(base[14][3] * 0.5f); t.setColor(mix(Neon.DIM, 0, 0, 170));
                g.drawText(setup ? "Essai de la manette (pas en jeu : rien n'est envoyé) · ⚙ réglages" : "Manette en jeu · A + Z + START sur la vraie manette pour revenir", getWidth() / 2f, base[14][1] + base[14][3] * 2.2f, t);
            }
        }
    }
}
