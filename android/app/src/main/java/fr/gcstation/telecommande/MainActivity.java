package fr.gcstation.telecommande;

// GC Station : telecommande native (theme Neon retro). L'appli parle directement a la console (Api) :
// - trouve la console sur le Wi-Fi (adresses x.y.z.1 a 254, /api/state) et retient son adresse ;
// - onglets Lecture, Musique, Radio, Manette, Fichiers ; mini-lecteur en haut ; etat lu toutes les secondes, ou
//   seulement quand la console previent d'un changement (Live : GC Station v266 et plus) ;
// - console injoignable : bandeau, nouvel essai tout seul (et la console cherchee si son adresse a change) ;
// - partie en cours (jeu lance sur la console) : jeu, jaquette, temps de jeu, temps restant du jour ;
// - "Partager -> GC Station" : fichiers envoyes sur la carte SD (musique, photos, videos, .dol, .gci...).

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public class MainActivity extends Activity implements Host {
    private static final String[] TAB_ICON = { "▶", "♫", "◉", "✚", "◈" };
    private static final String[] TAB_NAME = { "Lecture", "Musique", "Radio", "Manette", "Plus" };
    // ecrans : 0-3 onglets, 4 Plus (grille), 5 et suivants : ouverts depuis Plus (avec une fleche retour)
    static final int T_FILES = 5, T_GAMES = 6, T_PHOTOS = 7, T_QUEUE = 8, T_SETTINGS = 9, T_CARDS = 10, T_SALON = 11, T_TOOLS = 12, T_SEARCH = 13, T_COPIES = 14, NT = 15;
    static final String[] SUB_NAME = { "", "", "", "", "", "Fichiers", "Jeux", "Photos", "File d'attente", "R\u00e9glages", "Cartes m\u00e9moire", "Salon", "Outils", "Recherche", "Copies" };
    static final String ACT_GAME = "fr.gcstation.JEU", ACT_TAB = "fr.gcstation.ONGLET_";   // (raccourcis : un jeu, un ecran)
    private Intent pendingGame;
    private LinearLayout header;
    private boolean full;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private AuroraView aurora;
    private FrameLayout content;
    private LinearLayout nav, mini, panel;
    private TextView status, dot, miniTitle, miniSub, miniPlay, panelMsg;
    private ProgressBar panelSpin;
    private EditText panelIp;
    private final Tab[] tabs = new Tab[NT];
    private final View[] tabViews = new View[NT];
    private LinearLayout subBar;
    private TextView subTitle;
    private final TextView[] navIcon = new TextView[5], navLabel = new TextView[5];
    private int cur = -1;
    private boolean visible, searching, connected;
    private Intent pendingShare;
    private int fails;
    private JSONObject lastState;
    private long lastStateAt;
    private LinearLayout offline, gameView, sendBar;
    private TextView sendText;
    private ProgressBar sendBarP;
    private TextView offlineMsg, gameTitle, gameSub, gameClock, gameLeft, gamePad;
    private android.widget.ImageView gameCover;
    private boolean gameHidden, wasOffline, rediscovering;
    private final Live.Listener live = new Live.Listener() {
        @Override public void changed() { if (connected && visible) { ui.removeCallbacks(poll); ui.post(poll); } }
        @Override public void game(Live.Game g) { gameHidden = false; showGame(); }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        Window w = getWindow();
        w.setStatusBarColor(Neon.BG_TOP);
        w.setNavigationBarColor(Neon.BG_BOTTOM);
        w.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        highRefresh(w);

        FrameLayout root = new FrameLayout(this);
        aurora = new AuroraView(this);
        root.addView(aurora, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        root.addView(col, new FrameLayout.LayoutParams(-1, -1));

        LinearLayout top = new LinearLayout(this);              // en-tete : GC STATION + etat + reglages
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(20), dp(14), dp(12), dp(6));
        LinearLayout brand = new LinearLayout(this);
        brand.setOrientation(LinearLayout.VERTICAL);
        TextView logo = Neon.glow(this, "GC STATION", 22, Neon.PINK);
        brand.addView(logo);
        LinearLayout st = new LinearLayout(this);
        st.setGravity(Gravity.CENTER_VERTICAL);
        dot = Neon.text(this, "● ", 11, Neon.FAINT, true);
        status = Neon.text(this, "Télécommande", 12, Neon.DIM, false);
        st.addView(dot);
        st.addView(status);
        brand.addView(st);
        top.addView(brand, new LinearLayout.LayoutParams(0, -2, 1));
        TextView gear = Neon.btn(this, "⚙", 18, false, v -> menu());
        gear.setPadding(0, 0, 0, 0);
        top.addView(gear, new LinearLayout.LayoutParams(dp(44), dp(44)));
        col.addView(top);
        header = top;

        mini = new LinearLayout(this);                          // mini-lecteur (sauf sur Lecture)
        mini.setGravity(Gravity.CENTER_VERTICAL);
        mini.setBackground(Neon.glass(this, 18));
        mini.setPadding(dp(14), dp(8), dp(8), dp(8));
        LinearLayout mt = new LinearLayout(this);
        mt.setOrientation(LinearLayout.VERTICAL);
        miniTitle = Neon.text(this, "", 14, Neon.TEXT, true);
        miniTitle.setSingleLine(true);
        miniTitle.setEllipsize(TextUtils.TruncateAt.END);
        miniSub = Neon.text(this, "", 12, Neon.PINK, false);
        miniSub.setSingleLine(true);
        miniSub.setEllipsize(TextUtils.TruncateAt.END);
        mt.addView(miniTitle);
        mt.addView(miniSub);
        mini.addView(mt, new LinearLayout.LayoutParams(0, -2, 1));
        miniPlay = Neon.btn(this, "▶", 16, true, v -> Api.cmd("toggle", -1));
        miniPlay.setPadding(0, 0, 0, 0);
        mini.addView(miniPlay, new LinearLayout.LayoutParams(dp(42), dp(42)));
        TextView mn = Neon.btn(this, "▶▶", 12, false, v -> Api.cmd("next", -1));
        mn.setPadding(0, 0, 0, 0);
        LinearLayout.LayoutParams mnl = new LinearLayout.LayoutParams(dp(42), dp(42));
        mnl.leftMargin = dp(6);
        mini.addView(mn, mnl);
        mini.setOnClickListener(v -> select(0));
        LinearLayout.LayoutParams ml = new LinearLayout.LayoutParams(-1, -2);
        ml.setMargins(dp(14), dp(2), dp(14), dp(6));
        col.addView(mini, ml);

        offline = new LinearLayout(this);                       // console injoignable (l'appli reessaie toute seule)
        offline.setGravity(Gravity.CENTER_VERTICAL);
        offline.setBackground(Neon.glass(this, 16));
        offline.setPadding(dp(14), dp(8), dp(8), dp(8));
        offlineMsg = Neon.text(this, "", 12, Neon.TEXT, false);
        offline.addView(offlineMsg, new LinearLayout.LayoutParams(0, -2, 1));
        TextView ob = Neon.btn(this, "Chercher", 12, false, v -> { connected = false; search(); });
        offline.addView(ob, new LinearLayout.LayoutParams(-2, dp(38)));
        TextView op = Neon.btn(this, "\uD83C\uDFAE", 14, true, v -> padNow());   // (manette en jeu)
        LinearLayout.LayoutParams opl = new LinearLayout.LayoutParams(dp(44), dp(38));
        opl.leftMargin = dp(6);
        offline.addView(op, opl);
        offline.setVisibility(View.GONE);
        LinearLayout.LayoutParams ol = new LinearLayout.LayoutParams(-1, -2);
        ol.setMargins(dp(14), 0, dp(14), dp(6));
        col.addView(offline, ol);

        sendBar = new LinearLayout(this);                       // envoi de fichiers en cours : nom, %, debit
        sendBar.setOrientation(LinearLayout.VERTICAL);
        sendBar.setBackground(Neon.glass(this, 16));
        sendBar.setPadding(dp(14), dp(8), dp(14), dp(10));
        sendText = Neon.text(this, "", 12, Neon.TEXT, true);
        sendText.setSingleLine(true);
        sendText.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        sendBar.addView(sendText);
        sendBarP = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        sendBarP.setMax(100);
        sendBarP.setProgressTintList(android.content.res.ColorStateList.valueOf(Neon.PINK));
        LinearLayout.LayoutParams spl = new LinearLayout.LayoutParams(-1, dp(8));
        spl.topMargin = dp(6);
        sendBar.addView(sendBarP, spl);
        sendBar.setVisibility(View.GONE);
        LinearLayout.LayoutParams sbl = new LinearLayout.LayoutParams(-1, -2);
        sbl.setMargins(dp(14), 0, dp(14), dp(6));
        col.addView(sendBar, sbl);

        subBar = new LinearLayout(this);                        // ecran ouvert depuis Plus : fleche retour + titre
        subBar.setGravity(Gravity.CENTER_VERTICAL);
        subBar.setPadding(dp(14), 0, dp(14), dp(6));
        TextView back = Neon.btn(this, "\u2190", 16, false, v -> select(4));
        back.setPadding(0, 0, 0, 0);
        subBar.addView(back, new LinearLayout.LayoutParams(dp(40), dp(40)));
        subTitle = Neon.glow(this, "", 19, Neon.CYAN);
        subTitle.setPadding(dp(12), 0, 0, 0);
        subBar.addView(subTitle);
        subBar.setVisibility(View.GONE);
        col.addView(subBar);

        content = new FrameLayout(this);
        col.addView(content, new LinearLayout.LayoutParams(-1, 0, 1));

        nav = new LinearLayout(this);                           // barre d'onglets
        nav.setPadding(dp(6), dp(6), dp(6), dp(8));
        GradientDrawable nb = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0x22FFFFFF, 0x10FFFFFF });
        nb.setCornerRadius(dp(24));
        nb.setStroke(dp(1), Neon.alpha(Neon.PINK, 0x55));
        nav.setBackground(nb);
        for (int i = 0; i < 5; i++) {
            final int k = i;
            LinearLayout it = new LinearLayout(this);
            it.setOrientation(LinearLayout.VERTICAL);
            it.setGravity(Gravity.CENTER);
            it.setPadding(0, dp(6), 0, dp(4));
            navIcon[i] = Neon.text(this, TAB_ICON[i], 19, Neon.FAINT, true);
            navIcon[i].setGravity(Gravity.CENTER);
            navLabel[i] = Neon.text(this, TAB_NAME[i], 11, Neon.FAINT, true);
            navLabel[i].setGravity(Gravity.CENTER);
            it.addView(navIcon[i]);
            it.addView(navLabel[i]);
            it.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); select(k); });
            nav.addView(it, new LinearLayout.LayoutParams(0, -2, 1));
        }
        LinearLayout.LayoutParams nl = new LinearLayout.LayoutParams(-1, -2);
        nl.setMargins(dp(12), dp(4), dp(12), dp(12));
        col.addView(nav, nl);

        tabs[0] = new PlayerTab(this);
        tabs[1] = new ListTab(this, false);
        tabs[2] = new ListTab(this, true);
        tabs[3] = new PadTab(this);
        tabs[4] = new PlusTab(this);
        tabs[T_FILES] = new FilesTab(this);
        tabs[T_GAMES] = new GamesTab(this);
        tabs[T_PHOTOS] = new PhotosTab(this);
        tabs[T_QUEUE] = new QueueTab(this);
        tabs[T_SETTINGS] = new SettingsTab(this);
        tabs[T_CARDS] = new CardsTab(this);
        tabs[T_SALON] = new SalonTab(this);
        tabs[T_TOOLS] = new ToolsTab(this);
        tabs[T_SEARCH] = new SearchTab(this);
        tabs[T_COPIES] = new CopiesTab(this);
        for (int i = 0; i < NT; i++) {
            tabViews[i] = tabs[i].build();
            tabViews[i].setVisibility(View.GONE);
            content.addView(tabViews[i], new FrameLayout.LayoutParams(-1, -1));
        }

        gameView = buildGame();                                 // partie en cours (par-dessus les onglets)
        content.addView(gameView, new FrameLayout.LayoutParams(-1, -1));

        panel = buildPanel();                                   // connexion (par-dessus tout)
        root.addView(panel, new FrameLayout.LayoutParams(-1, -1));

        setContentView(root);
        select(0);
        PlaybackService.channels(this);
        if (android.os.Build.VERSION.SDK_INT >= 33 && checkSelfPermission("android.permission.POST_NOTIFICATIONS") != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, 1);
        Api.host = prefs().getString("host", null);
        if (isShare(getIntent())) pendingShare = getIntent();
        else shortcut(getIntent());
        start();
    }

    /** le mode d'affichage le plus rapide de l'ecran (120 Hz sur les telephones qui l'ont), meme definition */
    @SuppressWarnings("deprecation")
    private void highRefresh(Window w) {
        try {
            android.view.Display d = android.os.Build.VERSION.SDK_INT >= 30 ? getDisplay() : getWindowManager().getDefaultDisplay();
            if (d == null) return;
            android.view.Display.Mode cur = d.getMode(), best = cur;
            for (android.view.Display.Mode m : d.getSupportedModes())
                if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight() && m.getRefreshRate() > best.getRefreshRate()) best = m;
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            lp.preferredRefreshRate = best.getRefreshRate();
            w.setAttributes(lp);
        } catch (Exception e) { }
    }

    private SharedPreferences prefs() { return getSharedPreferences("gcs", MODE_PRIVATE); }
    private int dp(float v) { return Neon.dp(this, v); }

    public void select(int k) {
        if (k == cur) return;
        if (full && k != 3) fullPad(false);
        for (int i = 0; i < NT; i++) tabViews[i].setVisibility(i == k ? View.VISIBLE : View.GONE);
        int nk = k >= 4 ? 4 : k;
        for (int i = 0; i < 5; i++) {
            boolean on = i == nk;
            navIcon[i].setTextColor(on ? Neon.CYAN : Neon.FAINT);
            navIcon[i].setShadowLayer(on ? dp(10) : 0, 0, 0, Neon.CYAN);
            navLabel[i].setTextColor(on ? Neon.TEXT : Neon.FAINT);
        }
        subBar.setVisibility(k >= T_FILES ? View.VISIBLE : View.GONE);
        if (k >= T_FILES) subTitle.setText(SUB_NAME[k]);
        if (k >= 0) { tabViews[k].setAlpha(0f); tabViews[k].setTranslationY(dp(14)); tabViews[k].animate().alpha(1f).translationY(0).setDuration(220).start(); }
        cur = k;
        mini.setVisibility(k == 0 ? View.GONE : View.VISIBLE);
        if (connected) tabs[k].show();
    }

    // ---------- etat de la console (toutes les secondes, appli visible) ----------
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!visible || !connected) return;
            ui.removeCallbacks(poll);
            Api.get("/api/state", r -> {
                if (!visible || !connected) return;
                ui.removeCallbacks(poll);
                if (r != null) {
                    fails = 0;
                    online();
                    try { JSONObject j = new JSONObject(r); lastState = j; lastStateAt = android.os.SystemClock.uptimeMillis(); onState(j); } catch (Exception e) { }
                    dot.setTextColor(Neon.CYAN);
                    Neon.set(status, "Connecté · " + Api.host + (Live.pushing() ? " · en direct" : ""));
                } else offlineStep(++fails);
                // en direct : la console previent des changements, l'etat n'est relu que toutes les 5 s (au cas ou)
                ui.postDelayed(poll, r == null ? 3000 : Live.pushing() ? 5000 : 1000);
            });
        }
    };

    /** entre deux lectures de l'etat (en direct) : la position avance ici, et la partie en cours se met a jour */
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            if (!visible) return;
            ui.postDelayed(this, 1000);
            if (Live.game != null) updateGame();
            JSONObject s = lastState;
            if (s == null || !connected || fails > 0 || !Live.pushing()) return;
            long dt = (android.os.SystemClock.uptimeMillis() - lastStateAt) / 1000;
            if (dt < 1) return;
            try {
                JSONObject c = new JSONObject(s.toString());
                if (c.optInt("playing", 0) != 0) {
                    int dur = c.optInt("dur", 0);
                    long el = c.optInt("el", 0) + dt;
                    c.put("el", dur > 0 ? Math.min(dur, el) : el);
                }
                onState(c);
            } catch (Exception e) { }
        }
    };

    // ---------- console injoignable : bandeau, nouvel essai, adresse changee ----------
    private void offlineStep(int n) {
        if (Live.game != null) {                                // (jeu en cours : la console ne peut pas repondre, c'est normal)
            dot.setTextColor(Neon.PINK);
            Neon.set(status, "En jeu · " + Live.game.name);
            offline.setVisibility(View.GONE);
            showGame();
            return;
        }
        if (n < 3) return;
        wasOffline = true;
        dot.setTextColor(0xFFFF4D6D);
        Neon.set(status, "Console injoignable · nouvel essai…");
        Neon.set(offlineMsg, "La console ne répond pas (" + Api.host + "). Éteinte, en jeu ou hors du Wi-Fi ? L'appli se reconnecte toute seule.");
        offline.setVisibility(View.VISIBLE);
        if (n % 6 == 3 && !rediscovering) {                      // (environ toutes les 18 s : la console a peut-etre change d'adresse)
            rediscovering = true;
            final String old = Api.host;
            new Thread(() -> {
                java.util.List<String[]> f = Live.discover(1500);
                ui.post(() -> {
                    rediscovering = false;
                    if (f.isEmpty() || old == null || !old.equals(Api.host)) return;
                    String ip = f.get(0)[0];
                    for (String[] x : f) if (x[0].equals(old)) ip = old;
                    if (!ip.equals(old)) {
                        Api.host = ip;
                        prefs().edit().putString("host", ip).apply();
                        Live.resubscribe();
                        Toast.makeText(this, "Console retrouvée : " + ip, Toast.LENGTH_SHORT).show();
                        ui.removeCallbacks(poll); ui.post(poll);
                    }
                });
            }).start();
        }
    }

    private void online() {
        Live.Game g = Live.consoleBack();
        if (g != null) Toast.makeText(this, "Partie terminée : " + g.name + " · " + Live.dur(g.playedSec()), Toast.LENGTH_LONG).show();
        else if (wasOffline) Toast.makeText(this, "Console de retour", Toast.LENGTH_SHORT).show();
        wasOffline = false;
        offline.setVisibility(View.GONE);
        if (Live.game == null || System.currentTimeMillis() - Live.game.start > 45000) showGame();
    }

    // ---------- partie en cours ----------
    private LinearLayout buildGame() {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);
        p.setGravity(Gravity.CENTER);
        p.setPadding(dp(18), dp(10), dp(18), dp(18));
        p.setClickable(true);
        p.setBackgroundColor(Neon.BG_BOTTOM);                   // (opaque : l'onglet en dessous ne se voit pas a travers)
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER_HORIZONTAL);
        android.graphics.drawable.GradientDrawable cb = new android.graphics.drawable.GradientDrawable(android.graphics.drawable.GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0xFF1C1030, 0xFF0E0818 });
        cb.setCornerRadius(dp(26)); cb.setStroke(dp(1), Neon.alpha(Neon.PINK, 0x88));
        card.setBackground(cb);
        card.setPadding(dp(20), dp(20), dp(20), dp(18));
        card.addView(Neon.glow(this, "PARTIE EN COURS", 13, Neon.CYAN));
        gameCover = new android.widget.ImageView(this);
        gameCover.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams cl = new LinearLayout.LayoutParams(dp(150), dp(150));
        cl.setMargins(0, dp(14), 0, dp(10));
        card.addView(gameCover, cl);
        gameTitle = Neon.text(this, "", 19, Neon.TEXT, true);
        gameTitle.setGravity(Gravity.CENTER);
        card.addView(gameTitle);
        gameSub = Neon.text(this, "", 12, Neon.DIM, false);
        gameSub.setGravity(Gravity.CENTER);
        card.addView(gameSub);
        gameClock = Neon.glow(this, "0:00", 38, Neon.PINK);
        gameClock.setGravity(Gravity.CENTER);
        gameClock.setPadding(0, dp(12), 0, 0);
        card.addView(gameClock);
        TextView cap = Neon.text(this, "temps de jeu", 11, Neon.FAINT, false);
        cap.setGravity(Gravity.CENTER);
        card.addView(cap);
        gameLeft = Neon.text(this, "", 14, Neon.CYAN, true);
        gameLeft.setGravity(Gravity.CENTER);
        gameLeft.setPadding(0, dp(10), 0, 0);
        card.addView(gameLeft);
        TextView hint = Neon.text(this, "Pour revenir à GC Station : A + Z + START sur la manette. L'appli se reconnecte toute seule au retour.", 12, Neon.DIM, false);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(0, dp(14), 0, dp(12));
        card.addView(hint);
        gamePad = Neon.btn(this, "\uD83C\uDFAE  Manette dans le jeu", 14, true, v -> { Live.Game g = Live.game; if (g != null) ManetteActivity.open(this, g.padMask); });
        LinearLayout.LayoutParams gpl = new LinearLayout.LayoutParams(-1, -2);
        gpl.bottomMargin = dp(8);
        card.addView(gamePad, gpl);
        card.addView(Neon.btn(this, "Masquer", 13, false, v -> { gameHidden = true; showGame(); }), new LinearLayout.LayoutParams(-1, -2));
        p.addView(card, new LinearLayout.LayoutParams(-1, -2));
        p.setVisibility(View.GONE);
        return p;
    }

    private String coverFor = "";
    private void showGame() {
        Live.Game g = Live.game;
        boolean on = g != null && !gameHidden;
        if (on && gameView.getVisibility() != View.VISIBLE) { gameView.setAlpha(0f); gameView.animate().alpha(1f).setDuration(250).start(); }
        gameView.setVisibility(on ? View.VISIBLE : View.GONE);
        if (g == null) { coverFor = ""; return; }
        if (!g.id.equals(coverFor)) {
            coverFor = g.id;
            java.io.File f = new java.io.File(getCacheDir(), "jaquettes/" + g.id + ".png");   // (gardee par l'onglet Jeux)
            android.graphics.Bitmap b = f.exists() ? android.graphics.BitmapFactory.decodeFile(f.getPath()) : null;
            if (b != null) gameCover.setImageBitmap(b); else gameCover.setImageResource(R.mipmap.ic_launcher);
        }
        Neon.set(gameTitle, g.name.isEmpty() ? g.id : g.name);
        gamePad.setVisibility(g.padMask != 0 ? View.VISIBLE : View.GONE);   // (reglage "Manettes du telephone" de la console)
        Neon.set(gameSub, g.id + (g.profile.isEmpty() ? "" : " · profil " + g.profile) + " · lancé à "
                 + new java.text.SimpleDateFormat("HH:mm", java.util.Locale.FRANCE).format(new java.util.Date(g.start)));
        updateGame();
    }

    private void updateGame() {
        Live.Game g = Live.game;
        if (g == null || gameView.getVisibility() != View.VISIBLE) return;
        long t = g.playedSec();
        Neon.set(gameClock, (t / 3600) + ":" + String.format(java.util.Locale.FRANCE, "%02d:%02d", t % 3600 / 60, t % 60));
        long left = g.leftSec();
        if (left == Long.MIN_VALUE) { gameLeft.setVisibility(View.GONE); return; }
        gameLeft.setVisibility(View.VISIBLE);
        if (left > 0) { Neon.set(gameLeft, "Il reste " + Live.dur(left) + " de jeu aujourd'hui"); gameLeft.setTextColor(left <= 300 ? 0xFFFFA040 : Neon.CYAN); }
        else { Neon.set(gameLeft, "Temps de jeu du jour écoulé"); gameLeft.setTextColor(0xFFFF4D6D); }
    }

    private void onState(JSONObject s) {
        String t = s.optString("title", "");
        Neon.set(miniTitle, t.isEmpty() ? "Rien en lecture" : t);
        Neon.set(miniSub, s.optString("sub", ""));
        Neon.set(miniPlay, s.optInt("playing", 0) != 0 ? "❚❚" : "▶");
        for (Tab tb : tabs) tb.state(s);
    }

    @Override protected void onResume() {
        super.onResume(); visible = true; aurora.setRunning(true);
        Live.add(this, live);
        showGame();
        ui.removeCallbacks(poll); if (connected) ui.post(poll);
        ui.removeCallbacks(tick); ui.postDelayed(tick, 1000);
    }
    @Override protected void onPause() { super.onPause(); visible = false; aurora.setRunning(false); Live.remove(live); ui.removeCallbacks(tick); }

    // ---------- connexion ----------
    private void start() {
        if (Api.host == null) { search(); return; }
        showPanel("Connexion à la console (" + Api.host + ")…", true);
        final String hh = Api.host;
        new Thread(() -> {
            boolean ok = Api.isConsole(hh, 1500);
            ui.post(() -> { if (ok || Live.game != null) connect(hh); else search(); });   // (jeu en cours : la console ne repond pas)
        }).start();
    }

    private void connect(String ip) {
        Api.host = ip;
        prefs().edit().putString("host", ip).apply();
        connected = true;
        fails = 0;
        Live.resubscribe();
        panel.animate().alpha(0f).setDuration(250).withEndAction(() -> panel.setVisibility(View.GONE)).start();
        dot.setTextColor(Neon.CYAN);
        status.setText("Connecté · " + ip);
        tabs[cur].show();
        PlaybackService.start(this);                            // (notification, ecran verrouille, widget, volume)
        BuildCheck.run(this);
        ui.removeCallbacks(poll);
        ui.post(poll);
        if (pendingShare != null) { Intent i = pendingShare; pendingShare = null; share(i); }
        if (pendingGame != null) { Intent i = pendingGame; pendingGame = null; launchGame(i); }
        Backup.auto(this);                                      // (copie des cartes et reglages, une fois par jour)
        AppUpdate.check(this);                                  // (nouvelle version de l'appli ? notification)
    }

    private static List<String> localPrefixes() {
        List<String> r = new ArrayList<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InetAddress a : Collections.list(ni.getInetAddresses())) {
                    if (!(a instanceof Inet4Address) || !a.isSiteLocalAddress()) continue;
                    String ip = a.getHostAddress();
                    String p = ip.substring(0, ip.lastIndexOf('.') + 1);
                    if (ni.getName().startsWith("wlan")) { r.remove(p); r.add(0, p); } else if (!r.contains(p)) r.add(p);
                }
            }
        } catch (Exception e) { }
        return r;
    }

    private void search() {
        if (searching) return;
        searching = true;
        showPanel("Recherche de la console sur le Wi-Fi…", true);
        new Thread(() -> {
            final List<String[]> quick = Live.discover(1500);   // (la console repond elle-meme : GC Station v266 et plus)
            if (!quick.isEmpty()) {
                searching = false;
                ui.post(() -> found(quick));
                return;
            }
            final List<String> pre = localPrefixes();
            final AtomicReference<String> found = new AtomicReference<>();
            final AtomicInteger done = new AtomicInteger();
            for (String p : pre) {
                if (found.get() != null) break;
                ExecutorService ex = Executors.newFixedThreadPool(48);
                for (int i = 1; i <= 254; i++) {
                    final String ip = p + i;
                    ex.execute(() -> {
                        if (found.get() == null && Api.isConsole(ip, 600)) found.compareAndSet(null, ip);
                        int d = done.incrementAndGet();
                        if (d % 32 == 0) ui.post(() -> panelMsg.setText("Recherche de la console sur le Wi-Fi… " + Math.min(100, d * 100 / (254 * Math.max(1, pre.size()))) + " %"));
                    });
                }
                ex.shutdown();
                try { ex.awaitTermination(30, TimeUnit.SECONDS); } catch (InterruptedException e) { }
            }
            searching = false;
            final String ip = found.get();
            ui.post(() -> {
                if (ip != null) connect(ip);
                else showPanel(pre.isEmpty() ? "Pas de Wi-Fi : connecte le téléphone au même réseau que la console."
                                             : "Console introuvable. Allume-la et active la télécommande (onglet TÉLÉPHONE), ou tape son adresse.", false);
            });
        }).start();
    }

    /** la manette en jeu, meme si l'appli n'a pas vu le lancement : adresse tapee ou gardee, tous les ports */
    private void padNow() {
        String ip = panelIp.getText().toString().trim().replaceFirst("^https?://", "").replaceAll("/.*$", "");
        if (!ip.isEmpty()) { Api.host = ip; prefs().edit().putString("host", ip).apply(); }
        if (Api.host == null) { Toast.makeText(this, "Tape d'abord l'adresse de la console", Toast.LENGTH_SHORT).show(); return; }
        Live.Game g = Live.game;
        ManetteActivity.open(this, g != null && g.padMask != 0 ? g.padMask : 0xF);
    }

    /** consoles trouvees en direct : une seule -> connexion ; plusieurs -> laquelle ? */
    private void found(List<String[]> f) {
        if (f.size() == 1) { connect(f.get(0)[0]); return; }
        showPanel(f.size() + " consoles GC Station sur ce Wi-Fi.", false);
        String[] items = new String[f.size()];
        for (int i = 0; i < f.size(); i++) items[i] = f.get(i)[1] + " · " + f.get(i)[0];
        new NeonDialog(this).title("Quelle console ?").items(items, w -> connect(f.get(w)[0]))
            .cancel("Fermer").show();
    }

    private LinearLayout buildPanel() {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);
        p.setGravity(Gravity.CENTER);
        p.setPadding(dp(26), dp(26), dp(26), dp(26));
        p.setBackgroundColor(Neon.alpha(Neon.BG_BOTTOM, 0xCC));
        p.setClickable(true);
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER_HORIZONTAL);
        card.setBackground(Neon.glass(this, 26));
        card.setPadding(dp(22), dp(26), dp(22), dp(22));
        TextView t = Neon.glow(this, "GC STATION", 30, Neon.PINK);
        t.setGravity(Gravity.CENTER);
        card.addView(t);
        TextView sub = Neon.glow(this, "Télécommande", 15, Neon.CYAN);
        sub.setGravity(Gravity.CENTER);
        sub.setPadding(0, dp(2), 0, dp(20));
        card.addView(sub);
        panelSpin = new ProgressBar(this);
        panelSpin.setIndeterminateTintList(android.content.res.ColorStateList.valueOf(Neon.PINK));
        card.addView(panelSpin);
        panelMsg = Neon.text(this, "", 14, Neon.TEXT, false);
        panelMsg.setGravity(Gravity.CENTER);
        panelMsg.setPadding(0, dp(14), 0, dp(18));
        card.addView(panelMsg);
        panelIp = new EditText(this);
        panelIp.setHint("Adresse de la console (ex. 192.168.1.20)");
        panelIp.setHintTextColor(Neon.FAINT);
        panelIp.setTextColor(Neon.TEXT);
        panelIp.setSingleLine(true);
        panelIp.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        panelIp.setBackground(Neon.glass(this, 16));
        panelIp.setPadding(dp(14), dp(12), dp(14), dp(12));
        card.addView(panelIp, new LinearLayout.LayoutParams(-1, -2));
        LinearLayout row = new LinearLayout(this);
        row.setPadding(0, dp(12), 0, 0);
        row.addView(Neon.btn(this, "Se connecter", 14, true, v -> {
            String ip = panelIp.getText().toString().trim().replaceFirst("^https?://", "").replaceAll("/.*$", "");
            if (ip.isEmpty()) return;
            Api.host = ip;
            start();
        }), new LinearLayout.LayoutParams(0, -2, 1));
        View g = new View(this);
        row.addView(g, new LinearLayout.LayoutParams(dp(10), 1));
        row.addView(Neon.btn(this, "Chercher", 14, false, v -> search()), new LinearLayout.LayoutParams(0, -2, 1));
        card.addView(row, new LinearLayout.LayoutParams(-1, -2));
        // la console est en jeu : elle ne repond pas, mais Pulse attend les touches de la manette
        TextView pad = Neon.btn(this, "\uD83C\uDFAE  Manette dans le jeu", 14, false, v -> padNow());
        LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(-1, -2);
        pl.topMargin = dp(10);
        card.addView(pad, pl);
        p.addView(card, new LinearLayout.LayoutParams(-1, -2));
        return p;
    }

    private void showPanel(String msg, boolean busy) {
        panel.animate().cancel();
        panel.setAlpha(1f);
        panel.setVisibility(View.VISIBLE);
        panelMsg.setText(msg);
        panelSpin.setVisibility(busy ? View.VISIBLE : View.INVISIBLE);
        if (Api.host != null && panelIp.getText().length() == 0) panelIp.setText(Api.host);
        dot.setTextColor(Neon.FAINT);
        status.setText(busy ? "Connexion…" : "Pas connecté");
    }

    private void menu() {
        String[] items = { "Actualiser les listes", "Chercher la console à nouveau", "Saisir l'adresse de la console", "Mettre à jour l'appli (1." + AppUpdate.installed(this) + ")", "\uD83C\uDFAE  Manette : réglages et disposition" };
        new NeonDialog(this)
            .title("GC Station" + (Api.host != null ? " · " + Api.host : ""))
            .items(items, w -> {
                if (w == 0) { ((ListTab) tabs[1]).load(); ((ListTab) tabs[2]).load(); ((FilesTab) tabs[T_FILES]).refresh(); ((PhotosTab) tabs[T_PHOTOS]).load(); QueueTab.tracks = null; }
                else if (w == 1) { connected = false; search(); }
                else if (w == 3) AppUpdate.open(this);
                else if (w == 4) ManetteActivity.openSetup(this);
                else { connected = false; showPanel("Tape l'adresse affichée dans l'onglet TÉLÉPHONE de la console.", false); }
            }).cancel("Fermer").show();
    }

    @Override
    public void onBackPressed() {
        if (full) { fullPad(false); return; }
        if (panel.getVisibility() != View.VISIBLE && cur >= T_FILES) select(4);
        else if (panel.getVisibility() != View.VISIBLE && cur != 0) select(0);
        else super.onBackPressed();
    }

    // ---------- envoi de fichiers : Partager -> GC Station, et Fichiers > Envoyer ----------
    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        if (isShare(i)) { if (connected) share(i); else pendingShare = i; }
        else shortcut(i);
    }

    /** raccourcis : un ecran (appui long sur l'icone) ou un jeu a lancer (icone d'un jeu sur l'ecran d'accueil) */
    private void shortcut(Intent i) {
        String act = i != null ? i.getAction() : null;
        if (act == null) return;
        if (act.startsWith(ACT_TAB)) {
            try { int k = Integer.parseInt(act.substring(ACT_TAB.length())); if (k >= 0 && k < NT) select(k); } catch (Exception e) { }
        } else if (ACT_GAME.equals(act)) {
            if (connected) launchGame(i); else pendingGame = i;
        }
    }
    private void launchGame(Intent i) {
        select(T_GAMES);
        GamesTab.launch(this, i.getStringExtra("id"), 0, 1, i.getStringExtra("nom"));
    }
    public void openGame(org.json.JSONArray x) { select(T_GAMES); ((GamesTab) tabs[T_GAMES]).sheet(x); }

    /** manette en plein ecran (paysage) : sans en-tete, mini-lecteur ni barre d'onglets */
    public void fullPad(boolean on) {
        full = on;
        if (on) select(3);
        setRequestedOrientation(on ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
        int vis = on ? View.GONE : View.VISIBLE;
        header.setVisibility(vis);
        nav.setVisibility(vis);
        mini.setVisibility(on || cur == 0 ? View.GONE : View.VISIBLE);
        ((PadTab) tabs[3]).setLand(on);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            android.view.WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                if (on) { c.hide(android.view.WindowInsets.Type.systemBars()); c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); }
                else c.show(android.view.WindowInsets.Type.systemBars());
            }
        }
        getWindow().getDecorView().performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
    }

    private static boolean isShare(Intent i) {
        return i != null && (Intent.ACTION_SEND.equals(i.getAction()) || Intent.ACTION_SEND_MULTIPLE.equals(i.getAction()));
    }

    private void share(Intent i) {
        List<Uri> uris = new ArrayList<>();
        if (Intent.ACTION_SEND.equals(i.getAction())) { Uri u = i.getParcelableExtra(Intent.EXTRA_STREAM); if (u != null) uris.add(u); }
        else { ArrayList<Uri> l = i.getParcelableArrayListExtra(Intent.EXTRA_STREAM); if (l != null) uris.addAll(l); }
        String txt = i.getStringExtra(Intent.EXTRA_TEXT);
        if (uris.isEmpty() && txt != null) {                   // un lien partage : ouvert dans Cube Browser sur la tele
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("https?://\\S+").matcher(txt);
            if (m.find()) {
                final String url = m.group();
                Api.get("/api/open?u=" + Api.enc(url), r -> Toast.makeText(this, r != null ? "Ouvert sur la t\u00e9l\u00e9 (Cube Browser)" : "Pas de r\u00e9ponse de la console", Toast.LENGTH_LONG).show());
                return;
            }
        }
        send(uris);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null) return;
        if (req == PadTab.REQ_VOICE) {
            java.util.ArrayList<String> r = data.getStringArrayListExtra(android.speech.RecognizerIntent.EXTRA_RESULTS);
            if (r != null && !r.isEmpty()) ((PadTab) tabs[3]).voice(r.get(0));
            return;
        }
        if ((req == CardsTab.REQ_GCI || req == CardsTab.REQ_RAW) && data.getData() != null && CardsTab.pendingCard != null) { cardFile(data.getData(), CardsTab.pendingCard, req == CardsTab.REQ_RAW); return; }
        if (req != FilesTab.REQ_SEND) return;
        List<Uri> uris = new ArrayList<>();
        ClipData cd = data.getClipData();
        if (cd != null) for (int k = 0; k < cd.getItemCount(); k++) uris.add(cd.getItemAt(k).getUri());
        else if (data.getData() != null) uris.add(data.getData());
        send(uris);
    }

    // ---------- progression des envois (barre dans l'appli + notification), envoi qui tient ecran eteint, nouvel essai ----------
    private volatile long upD, upT, upMs, upAt;              // (GET /api/up : progression vue par la console)
    private volatile boolean upAsk;
    private boolean upFresh() { return System.currentTimeMillis() - upAt < 3000 && upT > 0; }
    private volatile String sendName = "";
    private volatile long sendDone, sendSize, sendT0;
    private volatile int sendIdx, sendCount;
    private final Runnable sendTick = new Runnable() {
        @Override public void run() {
            if (sendCount == 0) return;
            long sz = sendSize, d = sendDone;
            long el = System.currentTimeMillis() - sendT0;
            // (ce que la console a vraiment recu : le telephone, lui, compte ce qui part dans sa memoire tampon, en avance)
            if (upFresh()) { sz = upT; d = upD; el = upMs; }
            int pct = sz > 0 ? (int) Math.min(100, d * 100 / sz) : 0;
            if (!upAsk) { upAsk = true; Api.get("/api/up", r -> { upAsk = false; try { JSONObject j = new JSONObject(r); if (j.optLong("t") == sendSize) { upD = j.optLong("d"); upT = j.optLong("t"); upMs = j.optLong("ms"); upAt = System.currentTimeMillis(); } } catch (Exception e) { } }); }
            String sp = el > 800 && d > 0 ? String.format(java.util.Locale.FRANCE, " · %.1f Mo/s", d / 1048576.0 / (el / 1000.0)) : "";
            String t = (sendCount > 1 ? "Envoi " + (sendIdx + 1) + "/" + sendCount + " : " : "Envoi : ") + sendName;
            Neon.set(sendText, t + "  " + pct + " %" + sp);
            sendBarP.setProgress(pct);
            sendNotify(t, pct + " %" + sp, pct, false);
            ui.postDelayed(this, 400);
        }
    };
    private void sendNotify(String title, String text, int pct, boolean end) {
        try {
            PlaybackService.channels(this);
            android.app.Notification.Builder b = android.os.Build.VERSION.SDK_INT >= 26 ? new android.app.Notification.Builder(this, PlaybackService.CH_INFO) : new android.app.Notification.Builder(this);
            b.setSmallIcon(R.drawable.ic_stat).setContentTitle(title).setContentText(text).setColor(Neon.PINK).setOnlyAlertOnce(true).setShowWhen(false)
             .setContentIntent(android.app.PendingIntent.getActivity(this, PlaybackService.ID_SEND, Host.home(this), android.app.PendingIntent.FLAG_IMMUTABLE));
            if (!end) b.setOngoing(true).setProgress(100, pct, false); else b.setAutoCancel(true);
            getSystemService(android.app.NotificationManager.class).notify(PlaybackService.ID_SEND, b.build());
        } catch (Exception e) { }
    }

    private void send(final List<Uri> uris) {
        if (uris.isEmpty()) { Toast.makeText(this, "Rien à envoyer (seulement des fichiers)", Toast.LENGTH_LONG).show(); return; }
        if (sendCount > 0) { Toast.makeText(this, "Un envoi est déjà en cours", Toast.LENGTH_SHORT).show(); return; }
        sendCount = uris.size(); sendIdx = 0; sendDone = 0; sendSize = 0; sendName = ""; sendT0 = System.currentTimeMillis();
        sendBar.setVisibility(View.VISIBLE);
        sendBarP.setProgress(0);
        ui.removeCallbacks(sendTick); ui.post(sendTick);
        new Thread(() -> {
            // (ecran eteint ou appli en fond : le telephone garde le Wi-Fi et le processeur pour finir l'envoi)
            android.os.PowerManager.WakeLock wl = null;
            android.net.wifi.WifiManager.WifiLock wf = null;
            try {
                wl = ((android.os.PowerManager) getSystemService(POWER_SERVICE)).newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "gcs:envoi");
                wl.acquire(3 * 3600 * 1000L);
                wf = ((android.net.wifi.WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE)).createWifiLock(android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF, "gcs:envoi");
                wf.acquire();
            } catch (Exception e) { }
            int ok = 0;
            StringBuilder where = new StringBuilder();
            for (int i = 0; i < uris.size(); i++) {
                sendIdx = i;
                String r = null;
                for (int t = 0; t < 3 && r == null; t++) {            // (3 essais : coupure du Wi-Fi, console occupee par un autre envoi)
                    if (t > 0) try { Thread.sleep(Api.lastCode == 503 ? 4000 : 2000); } catch (InterruptedException e) { }
                    sendDone = 0; sendT0 = System.currentTimeMillis();
                    r = upload(uris.get(i));
                }
                if (r != null) { ok++; if (r.startsWith("SD:")) where.append(" ").append(r); }
            }
            try { if (wl != null && wl.isHeld()) wl.release(); if (wf != null && wf.isHeld()) wf.release(); } catch (Exception e) { }
            final int n = ok;
            final String w = where.toString();
            ui.post(() -> {
                sendCount = 0;
                ui.removeCallbacks(sendTick);
                Neon.set(sendText, n == uris.size() ? "Envoyé : " + n + " fichier" + (n > 1 ? "s" : "") + " ✓" : "Envoyés : " + n + " sur " + uris.size() + " (les autres ont échoué après 3 essais)");
                if (n == uris.size()) sendBarP.setProgress(100);
                ui.postDelayed(() -> { if (sendCount == 0) sendBar.setVisibility(View.GONE); }, 5000);
                Toast.makeText(this, (n == uris.size() ? "Envoyé sur la console (" + n + ")" : "Envoyés : " + n + " sur " + uris.size()) + (w.isEmpty() ? "" : " →" + w), Toast.LENGTH_LONG).show();
                ((FilesTab) tabs[T_FILES]).refresh();
                sendNotify(n == uris.size() ? "Envoi termin\u00e9" : "Envoi incomplet", (n == uris.size() ? n + " fichier(s) sur la console" : n + " sur " + uris.size() + " envoy\u00e9(s)") + (w.isEmpty() ? "" : " :" + w), 100, true);
                ((ListTab) tabs[1]).load();
            });
        }).start();
    }

    /** une sauvegarde (.gci) ajoutee a une carte, ou la carte remplacee (.raw) : envoi, puis la console l'integre */
    private void cardFile(final Uri u, final String card, final boolean raw) {
        Toast.makeText(this, "Envoi vers la carte " + card + "\u2026", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String name = uploadTo(u, "gci");
            String r = name == null ? null : Api.get((raw ? "/api/cardput" : "/api/gciimp") + "?c=" + Api.enc(card) + "&n=" + Api.enc(name), 8000);
            String m = "Envoi impossible";
            try { if (r != null) m = new JSONObject(r).optString("m", "Fait"); } catch (Exception e) { }
            final String fm = m;
            ui.post(() -> { Toast.makeText(this, fm, Toast.LENGTH_LONG).show(); ((CardsTab) tabs[T_CARDS]).load(); });
        }).start();
    }

    /** piloter une autre GC Station (Salon) */
    public void switchTo(String ip) {
        Toast.makeText(this, "Console " + ip, Toast.LENGTH_SHORT).show();
        QueueTab.tracks = null;
        connected = false;
        Api.host = ip;
        start();
        select(0);
    }

    // boutons de volume du telephone -> volume de la console (appli ouverte ; sinon : la notification de lecture)
    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent e) {
        int k = e.getKeyCode();
        if (connected && (k == android.view.KeyEvent.KEYCODE_VOLUME_UP || k == android.view.KeyEvent.KEYCODE_VOLUME_DOWN)) {
            if (e.getAction() == android.view.KeyEvent.ACTION_DOWN) {
                int dir = k == android.view.KeyEvent.KEYCODE_VOLUME_UP ? 1 : -1;
                if (!PlaybackService.adjust(dir)) Api.cmd(dir > 0 ? "volup" : "voldown", -1);
                ui.removeCallbacks(poll); ui.postDelayed(poll, 250);
            }
            return true;
        }
        return super.dispatchKeyEvent(e);
    }

    private String upload(Uri u) { return uploadTo(u, null, true); }
    private String uploadTo(Uri u, String forceDir) { return uploadTo(u, forceDir, false); }

    private String uploadTo(Uri u, String forceDir, boolean prog) {
        File tmp = null;
        try {
            String name = "fichier", mime = getContentResolver().getType(u);
            long size = -1;
            try (Cursor c = getContentResolver().query(u, null, null, null, null)) {
                if (c != null && c.moveToFirst()) {
                    int ni = c.getColumnIndex(OpenableColumns.DISPLAY_NAME), si = c.getColumnIndex(OpenableColumns.SIZE);
                    if (ni >= 0 && c.getString(ni) != null) name = c.getString(ni);
                    if (si >= 0 && !c.isNull(si)) size = c.getLong(si);
                }
            }
            if (size <= 0) {                                       // (taille inconnue : copie d'abord, la console veut la longueur)
                tmp = File.createTempFile("envoi", null, getCacheDir());
                try (InputStream in = getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(tmp)) {
                    byte[] buf = new byte[32768]; int n;
                    while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
                }
                size = tmp.length();
            }
            String low = name.toLowerCase();
            String dir = forceDir != null ? forceDir : mime != null && mime.startsWith("audio/") ? "mp3"
                       : mime != null && mime.startsWith("image/") ? "photos"
                       : mime != null && mime.startsWith("video/") ? "videos"
                       : low.endsWith(".gci") ? "gci" : "downloads";   // (.dol : la console le range dans SD:/apps)
            try (InputStream in = tmp != null ? new FileInputStream(tmp) : getContentResolver().openInputStream(u)) {
                if (prog) { sendName = name; sendSize = size; }
                Api.Sent sp = prog ? d -> sendDone = d : null;
                // (v272 : par morceaux si la console sait le faire, sinon d'un coup comme avant)
                return Api.canChunk() ? Api.putChunks(dir, name, in, size, sp) : Api.put(dir, name, in, size, sp);
            }
        } catch (Exception e) {
            return null;
        } finally {
            if (tmp != null) tmp.delete();
        }
    }
}
