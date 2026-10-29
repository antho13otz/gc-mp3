package fr.gcstation.telecommande;

// GC Station : telecommande native (theme Neon retro). L'appli parle directement a la console (Api) :
// - trouve la console sur le Wi-Fi (adresses x.y.z.1 a 254, /api/state) et retient son adresse ;
// - onglets Lecture, Musique, Radio, Manette, Fichiers ; mini-lecteur en haut ; etat lu toutes les secondes ;
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

public class MainActivity extends Activity {
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

    void select(int k) {
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
            Api.get("/api/state", r -> {
                if (r != null) {
                    fails = 0;
                    try { onState(new JSONObject(r)); } catch (Exception e) { }
                    dot.setTextColor(Neon.CYAN);
                    Neon.set(status, "Connecté · " + Api.host);
                } else if (++fails >= 3) {
                    dot.setTextColor(0xFFFF4D6D);
                    Neon.set(status, "Console injoignable…");
                }
                if (fails >= 8) { connected = false; showPanel("La console ne répond plus (" + Api.host + "). Elle est allumée, sur le même Wi-Fi ?", false); return; }
                ui.postDelayed(poll, 1000);
            });
        }
    };

    private void onState(JSONObject s) {
        String t = s.optString("title", "");
        Neon.set(miniTitle, t.isEmpty() ? "Rien en lecture" : t);
        Neon.set(miniSub, s.optString("sub", ""));
        Neon.set(miniPlay, s.optInt("playing", 0) != 0 ? "❚❚" : "▶");
        for (Tab tb : tabs) tb.state(s);
    }

    @Override protected void onResume() { super.onResume(); visible = true; aurora.setRunning(true); ui.removeCallbacks(poll); if (connected) ui.post(poll); }
    @Override protected void onPause() { super.onPause(); visible = false; aurora.setRunning(false); }

    // ---------- connexion ----------
    private void start() {
        if (Api.host == null) { search(); return; }
        showPanel("Connexion à la console (" + Api.host + ")…", true);
        final String hh = Api.host;
        new Thread(() -> {
            boolean ok = Api.isConsole(hh, 1500);
            ui.post(() -> { if (ok) connect(hh); else search(); });
        }).start();
    }

    private void connect(String ip) {
        Api.host = ip;
        prefs().edit().putString("host", ip).apply();
        connected = true;
        fails = 0;
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
        String[] items = { "Actualiser les listes", "Chercher la console à nouveau", "Saisir l'adresse de la console" };
        new NeonDialog(this)
            .title("GC Station" + (Api.host != null ? " · " + Api.host : ""))
            .items(items, w -> {
                if (w == 0) { ((ListTab) tabs[1]).load(); ((ListTab) tabs[2]).load(); ((FilesTab) tabs[T_FILES]).refresh(); ((PhotosTab) tabs[T_PHOTOS]).load(); QueueTab.tracks = null; }
                else if (w == 1) { connected = false; search(); }
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
    void openGame(org.json.JSONArray x) { select(T_GAMES); ((GamesTab) tabs[T_GAMES]).sheet(x); }

    /** manette en plein ecran (paysage) : sans en-tete, mini-lecteur ni barre d'onglets */
    void fullPad(boolean on) {
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

    private void send(final List<Uri> uris) {
        if (uris.isEmpty()) { Toast.makeText(this, "Rien à envoyer (seulement des fichiers)", Toast.LENGTH_LONG).show(); return; }
        Toast.makeText(this, "Envoi vers GC Station : " + uris.size() + " fichier" + (uris.size() > 1 ? "s" : "") + "…", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            int ok = 0;
            StringBuilder where = new StringBuilder();
            for (Uri u : uris) { String r = upload(u); if (r != null) { ok++; if (r.startsWith("SD:")) where.append(" ").append(r); } }
            final int n = ok;
            final String w = where.toString();
            ui.post(() -> {
                Toast.makeText(this, (n == uris.size() ? "Envoyé sur la console (" + n + ")" : "Envoyés : " + n + " sur " + uris.size()) + (w.isEmpty() ? "" : " →" + w), Toast.LENGTH_LONG).show();
                ((FilesTab) tabs[T_FILES]).refresh();
                PlaybackService.notify(this, PlaybackService.ID_SEND, "Envoi termin\u00e9", (n == uris.size() ? n + " fichier(s) sur la console" : n + " sur " + uris.size() + " envoy\u00e9(s)") + (w.isEmpty() ? "" : " :" + w));
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
    void switchTo(String ip) {
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

    private String upload(Uri u) { return uploadTo(u, null); }

    private String uploadTo(Uri u, String forceDir) {
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
                return Api.put(dir, name, in, size);
            }
        } catch (Exception e) {
            return null;
        } finally {
            if (tmp != null) tmp.delete();
        }
    }
}
