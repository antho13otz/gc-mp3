package fr.gcstation.telecommande;

// Jeu de cartes entre potes : le 8 américain (console build 762, appli build 47).
// Chacun ouvre l'appli (Plus > Jeu de cartes) : son téléphone montre SA main, la télé montre la table.
// Tout se décide dans la console (/api/cartes) : l'appli demande l'état toutes les 0,35 s et envoie ses coups.
// Cartes : c = couleur * 13 + valeur (couleurs : 0 pique, 1 cœur, 2 carreau, 3 trèfle ; valeurs : 0 as ... 12 roi).
//
// Appli build 47 : tout est dessiné par deux vues (la table, la main en éventail) qui s'animent à chaque image
// (cartes qui glissent, carte posée qui s'envole, carte piochée qui arrive) ; l'écran n'est refait que si l'état
// change (avant : tout était reconstruit toutes les 0,6 s : saccades) ; cartes et panneaux pleins, sans transparence.

import android.animation.ValueAnimator;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.animation.OvershootInterpolator;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;

public class CartesActivity extends Activity {
    static void open(Context c) { c.startActivity(new Intent(c, CartesActivity.class)); }

    static final String[] RANG = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "V", "D", "R" };
    static final String[] SUIT = { "♠", "♥", "♦", "♣" };
    static final String[] SUIT_NOM = { "Pique", "Cœur", "Carreau", "Trèfle" };
    static boolean red(int c) { int s = c / 13; return s == 1 || s == 2; }
    static final int PANEL = 0xFF1A1030, PANEL2 = 0xFF120A24, EDGE = 0xFF3A2A66;

    private SharedPreferences sp;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private boolean alive, busy;
    private long tok;
    private String lastRaw = "", lastPlayers = "", lastActions = "";
    private long lastVer = -1;
    private boolean wasMyTurn;

    private LinearLayout players, actions;
    private TextView title, msg, big, err;
    private TableView table;
    private HandView hand;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        Lang.init(this);
        sp = getSharedPreferences("gcs", MODE_PRIVATE);
        tok = sp.getLong("c8_tok", 0);
        getWindow().setStatusBarColor(0xFF0A0616);
        getWindow().setNavigationBarColor(0xFF0A0616);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0xFF140A2A, 0xFF0A0616 }));
        int p = dp(12);
        root.setPadding(p, dp(14), p, dp(10));

        LinearLayout head = new LinearLayout(this);                       // titre + dernier coup, sur un panneau plein
        head.setOrientation(LinearLayout.VERTICAL);
        head.setBackground(panel(18));
        head.setPadding(dp(16), dp(12), dp(16), dp(12));
        title = Neon.glow(this, "8 américain", 22, Neon.PINK);
        head.addView(title);
        msg = Neon.text(this, "Connexion à la table…", 13, Neon.DIM, false);
        msg.setPadding(0, dp(4), 0, 0);
        head.addView(msg);
        root.addView(head);

        HorizontalScrollView ps = new HorizontalScrollView(this);
        ps.setHorizontalScrollBarEnabled(false);
        players = new LinearLayout(this);
        players.setPadding(0, dp(10), 0, dp(4));
        ps.addView(players);
        root.addView(ps);

        table = new TableView(this);
        root.addView(table, new LinearLayout.LayoutParams(-1, 0, 1.15f));

        big = Neon.text(this, "", 20, Neon.TEXT, true);
        big.setGravity(Gravity.CENTER);
        big.setPadding(0, dp(6), 0, 0);
        root.addView(big);
        err = Neon.text(this, "", 13, 0xFFFF7A90, false);
        err.setGravity(Gravity.CENTER);
        root.addView(err);

        actions = new LinearLayout(this);
        actions.setGravity(Gravity.CENTER);
        actions.setPadding(0, dp(6), 0, dp(2));
        actions.setMinimumHeight(dp(56));
        root.addView(actions);

        hand = new HandView(this);
        root.addView(hand, new LinearLayout.LayoutParams(-1, 0, 1f));

        LinearLayout bottom = new LinearLayout(this);
        bottom.setGravity(Gravity.CENTER);
        bottom.addView(small("📺 Sur la télé", v -> call("tele", "")));
        bottom.addView(small("Quitter la table", v -> NeonDialog.confirm(this, "Quitter la table ?", "Tes cartes retournent sous la pioche.", "Quitter", () -> {
            call("quitter", ""); tok = 0; sp.edit().putLong("c8_tok", 0).apply(); finish(); })));
        bottom.addView(small("Tout arrêter", v -> NeonDialog.confirm(this, "Arrêter la partie pour tout le monde ?", null, "Arrêter", () -> {
            call("fin", ""); tok = 0; sp.edit().putLong("c8_tok", 0).apply(); finish(); })));
        root.addView(bottom);
        setContentView(root);

        if (tok == 0) askName(); else poll();
    }

    private GradientDrawable panel(float r) {
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { PANEL, PANEL2 });
        g.setCornerRadius(dp(r));
        g.setStroke(dp(1), EDGE);
        return g;
    }

    @Override protected void onResume() { super.onResume(); alive = true; ui.removeCallbacks(tick); ui.postDelayed(tick, 350); }
    @Override protected void onPause() { super.onPause(); alive = false; ui.removeCallbacks(tick); }

    private final Runnable tick = new Runnable() { @Override public void run() { if (!alive) return; if (tok != 0) poll(); ui.postDelayed(this, 350); } };

    private void askName() {
        NeonDialog.ask(this, "Ton nom à la table", sp.getString("c8_nom", ""), v -> {
            if (v.isEmpty()) v = "Joueur";
            sp.edit().putString("c8_nom", v).apply();
            final String nom = v;
            Api.get("/api/cartes?op=join&nom=" + Api.enc(nom), r -> {
                JSONObject j = parse(r);
                if (j == null) { msg.setText(Lang.t("La console ne répond pas (es-tu sur le même Wi-Fi ?)")); return; }
                if (j.optInt("ok") == 0) { show(j, r); err.setText(Lang.t(j.optString("err"))); return; }
                tok = j.optLong("tok");
                sp.edit().putLong("c8_tok", tok).apply();
                show(j, r);
            });
        });
    }

    private void poll() {
        if (busy) return;
        busy = true;
        Api.get("/api/cartes?op=etat&tok=" + tok, r -> {
            busy = false;
            if (r == null) { msg.setText(Lang.t("La console ne répond pas…")); return; }
            if (r.equals(lastRaw)) return;                                 // (rien n'a change : rien a refaire)
            JSONObject j = parse(r);
            if (j == null) return;
            if (j.optInt("moi", -1) < 0 && tok != 0) {                     // (plus a la table : partie finie, parti trop longtemps)
                tok = 0; sp.edit().putLong("c8_tok", 0).apply();
                show(j, r);
                big.setText(Lang.t("Tu n'es plus à la table"));
                lastActions = "";
                actions.removeAllViews();
                actions.addView(btn("Rejoindre la table", true, v -> askName()));
                return;
            }
            show(j, r);
        });
    }

    private void call(String op, String extra) {
        if (tok == 0 && !op.equals("fin")) return;
        Api.get("/api/cartes?op=" + op + "&tok=" + tok + extra, r -> {
            JSONObject j = parse(r);
            if (j == null) return;
            show(j, r);
            if (j.optInt("ok") == 0) {
                err.setText(Lang.t(j.optString("err")));
                hand.shake();
                getWindow().getDecorView().performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            }
        });
    }

    private static JSONObject parse(String r) { try { return r == null ? null : new JSONObject(r); } catch (Exception e) { return null; } }

    // ------------------------------------------------------------------ l'etat recu : seulement ce qui change
    private void show(JSONObject j, String raw) {
        lastRaw = raw == null ? "" : raw;
        int ph = j.optInt("ph"), moi = j.optInt("moi", -1), tour = j.optInt("tour");
        long ver = j.optLong("ver");
        if (ver != lastVer) { if (j.optInt("ok", 1) != 0) err.setText(""); lastVer = ver; }
        Neon.set(msg, j.optString("msg"));
        JSONArray js = j.optJSONArray("j");
        int n = js == null ? 0 : js.length();
        Neon.set(title, ph >= 2 ? "8 américain · manche " + j.optInt("manche") : "8 américain · salon");
        boolean my = ph == 2 && moi == tour;

        // joueurs (refaits seulement s'ils changent)
        StringBuilder sig = new StringBuilder().append(ph).append('|').append(tour).append('|').append(j.optInt("gag")).append('|').append(moi);
        for (int i = 0; i < n; i++) { JSONObject o = js.optJSONObject(i); sig.append('|').append(o.optString("n")).append(',').append(o.optInt("c")).append(',').append(o.optInt("s")); }
        if (!sig.toString().equals(lastPlayers)) {
            lastPlayers = sig.toString();
            players.removeAllViews();
            for (int i = 0; i < n; i++) {
                JSONObject o = js.optJSONObject(i);
                boolean cur = ph == 2 && i == tour, win = ph == 3 && i == j.optInt("gag");
                LinearLayout chip = new LinearLayout(this);
                chip.setOrientation(LinearLayout.VERTICAL);
                chip.setPadding(dp(12), dp(7), dp(12), dp(7));
                GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                    cur ? new int[] { 0xFFFF52D9, 0xFF9A3CFF } : win ? new int[] { 0xFF2FD68A, 0xFF138A5A } : new int[] { PANEL, PANEL2 });
                bg.setCornerRadius(dp(14));
                bg.setStroke(dp(1), cur ? 0xFFFFC2F2 : EDGE);
                chip.setBackground(bg);
                chip.addView(Neon.text(this, o.optString("n") + (i == moi ? " (toi)" : ""), 14, cur || win ? 0xFF120820 : Neon.TEXT, true));
                int c = o.optInt("c");
                chip.addView(Neon.text(this, (ph >= 2 ? c + " carte" + (c > 1 ? "s" : "") + " · " : "") + o.optInt("s") + " pts", 12, cur || win ? 0xFF2A1440 : Neon.DIM, false));
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
                lp.rightMargin = dp(8);
                players.addView(chip, lp);
            }
        }

        // la table
        int coul = j.optInt("coul", -1), top = j.optInt("haut", -1);
        table.set(ph, ph >= 2 ? top : -1, j.optInt("np"), top >= 0 && top % 13 == 7 ? coul : -1, j.optInt("p2"), j.optInt("sens", 1), my);

        // le bandeau et les boutons
        String banner, acts;
        if (ph <= 1) {
            banner = n < 2 ? "On attend au moins un autre joueur…" : n + " joueurs à la table";
            acts = moi >= 0 && n >= 2 ? "debut" : "";
        } else if (ph == 3) {
            int g = j.optInt("gag");
            String w = g >= 0 && g < n ? js.optJSONObject(g).optString("n") : "?";
            banner = g == moi ? "Tu gagnes la manche ! 🎉" : w + " gagne la manche";
            acts = "nouvelle";
        } else {
            String who = tour < n ? js.optJSONObject(tour).optString("n") : "?";
            banner = my ? "À toi de jouer !" : "À " + who + " de jouer";
            acts = my ? (j.optInt("p2") > 0 ? "p2:" + j.optInt("p2") : j.optInt("pi") != 0 ? "passer" : "piocher") : "";
        }
        Neon.set(big, banner);
        if (my && !wasMyTurn) {
            getWindow().getDecorView().performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            big.setScaleX(1.25f); big.setScaleY(1.25f);
            big.animate().scaleX(1f).scaleY(1f).setDuration(380).setInterpolator(new OvershootInterpolator(2.5f)).start();
        }
        wasMyTurn = my;
        if (!acts.equals(lastActions)) {
            lastActions = acts;
            actions.removeAllViews();
            if (acts.equals("debut")) actions.addView(btn("Commencer la partie", true, v -> call("debut", "")));
            else if (acts.equals("nouvelle")) actions.addView(btn("Manche suivante", true, v -> call("nouvelle", "")));
            else if (acts.startsWith("p2:")) actions.addView(btn("Piocher " + acts.substring(3) + " cartes", true, v -> call("piocher", "")));
            else if (acts.equals("piocher")) actions.addView(btn("Piocher", false, v -> call("piocher", "")));
            else if (acts.equals("passer")) actions.addView(btn("Passer", true, v -> call("passer", "")));
        }

        // ma main
        JSONArray m = j.optJSONArray("main"), ok = j.optJSONArray("jouables");
        HashSet<Integer> play = new HashSet<>();
        for (int i = 0; ok != null && i < ok.length(); i++) play.add(ok.optInt(i));
        ArrayList<Integer> cards = new ArrayList<>();
        for (int i = 0; m != null && i < m.length(); i++) cards.add(m.optInt(i));
        java.util.Collections.sort(cards, (a, bb) -> a / 13 != bb / 13 ? a / 13 - bb / 13 : a % 13 - bb % 13);   // (rangees par couleur)
        hand.set(cards, play, my);
    }

    void tapCard(int c) {
        if (c % 13 == 7) pickSuit(c); else call("jouer", "&c=" + c);
    }

    void tapPile() { if (lastActions.equals("piocher") || lastActions.startsWith("p2:")) call("piocher", ""); }

    /** le 8 : quatre grosses touches de couleur */
    private void pickSuit(int c) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        final NeonDialog d = new NeonDialog(this).title("Le 8 : quelle couleur ?");
        for (int s = 0; s < 4; s++) {
            final int su = s;
            TextView t = new TextView(this);
            t.setText(Lang.t(SUIT[s]));
            t.setTextSize(34);
            t.setGravity(Gravity.CENTER);
            t.setTextColor(s == 1 || s == 2 ? 0xFFD8243C : 0xFF1A1826);
            GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0xFFFFFFFF, 0xFFE4E0F0 });
            g.setCornerRadius(dp(14));
            g.setStroke(dp(2), 0xFF9C90C8);
            t.setBackground(g);
            t.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); d.dismiss(); call("jouer", "&c=" + c + "&s=" + su); });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(72), 1);
            lp.leftMargin = lp.rightMargin = dp(5);
            row.addView(t, lp);
        }
        d.view(row).cancel("Annuler").show();
    }

    private TextView btn(String s, boolean primary, View.OnClickListener l) {
        TextView t = Neon.btn(this, s, 15, primary, l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, dp(48));
        lp.leftMargin = lp.rightMargin = dp(6);
        t.setLayoutParams(lp);
        t.setPadding(dp(22), 0, dp(22), 0);
        return t;
    }

    private TextView small(String s, View.OnClickListener l) {
        TextView t = Neon.btn(this, s, 12, false, l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, dp(38));
        lp.leftMargin = lp.rightMargin = dp(4);
        lp.topMargin = dp(6);
        t.setLayoutParams(lp);
        t.setPadding(dp(12), 0, dp(12), 0);
        return t;
    }

    private int dp(float v) { return Neon.dp(this, v); }

    // ------------------------------------------------------------------ dessin d'une carte (plein, sans transparence)
    static final Paint CP = new Paint(Paint.ANTI_ALIAS_FLAG), CT = new Paint(Paint.ANTI_ALIAS_FLAG);
    static {
        CT.setTextAlign(Paint.Align.CENTER);
        CT.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
    }
    /** carte dans rf ; c < 0 : dos ; off : pas jouable (grisee, toujours pleine) ; lift : 0-1 (lueur) */
    static void drawCard(Canvas g, RectF rf, int c, boolean off, float lift) {
        float w = rf.width(), h = rf.height(), r = w * 0.12f;
        CP.setShader(null); CP.setStyle(Paint.Style.FILL);
        CP.setColor(0xFF05020C);                                          // (ombre pleine, juste dessous)
        g.drawRoundRect(new RectF(rf.left + w * 0.03f, rf.top + w * 0.05f, rf.right + w * 0.03f, rf.bottom + w * 0.05f), r, r, CP);
        if (lift > 0.01f) {
            CP.setColor(0xFFFF6BE0);
            RectF gl = new RectF(rf); gl.inset(-w * 0.04f * lift, -w * 0.04f * lift);
            g.drawRoundRect(gl, r * 1.2f, r * 1.2f, CP);
        }
        if (c < 0) {
            CP.setShader(new LinearGradient(rf.left, rf.top, rf.right, rf.bottom, 0xFF8A4BFF, 0xFF3A1490, Shader.TileMode.CLAMP));
            g.drawRoundRect(rf, r, r, CP);
            CP.setShader(null);
            CP.setStyle(Paint.Style.STROKE); CP.setStrokeWidth(w * 0.03f); CP.setColor(0xFFE6DAFF);
            RectF in = new RectF(rf); in.inset(w * 0.08f, w * 0.08f);
            g.drawRoundRect(in, r * 0.7f, r * 0.7f, CP);
            CT.setTextAlign(Paint.Align.CENTER); CT.setColor(0xFFF0E8FF); CT.setTextSize(w * 0.22f);
            g.drawText("GC", rf.centerX(), rf.centerY() + w * 0.08f, CT);
            return;
        }
        CP.setShader(new LinearGradient(0, rf.top, 0, rf.bottom, off ? 0xFFC9C5D6 : 0xFFFFFFFF, off ? 0xFFADA8BE : 0xFFE9E6F4, Shader.TileMode.CLAMP));
        g.drawRoundRect(rf, r, r, CP);
        CP.setShader(null);
        CP.setStyle(Paint.Style.STROKE); CP.setStrokeWidth(Math.max(1.5f, w * 0.018f)); CP.setColor(off ? 0xFF7E7896 : 0xFF9C90C8);
        g.drawRoundRect(rf, r, r, CP);
        int col = red(c) ? (off ? 0xFF9A4A58 : 0xFFD8243C) : (off ? 0xFF4A4658 : 0xFF1A1826);
        String rk = RANG[c % 13], su = SUIT[c / 13];
        CT.setColor(col);
        CT.setTextAlign(Paint.Align.CENTER);
        CT.setTextSize(w * 0.25f);
        g.drawText(rk, rf.left + w * 0.19f, rf.top + h * 0.2f, CT);
        CT.setTextSize(w * 0.21f);
        g.drawText(su, rf.left + w * 0.19f, rf.top + h * 0.36f, CT);
        CT.setTextSize(w * 0.58f);
        g.drawText(su, rf.left + w * 0.57f, rf.top + h * 0.8f, CT);
    }

    // ------------------------------------------------------------------ la table (tapis, pioche, tas)
    final class TableView extends View {
        private int ph, top = -1, np, coul = -1, p2, sens = 1;
        private boolean my;
        private final ArrayList<Integer> pile = new ArrayList<>();      // (les dernieres cartes du tas, la derniere en haut)
        private float drop = 1f;                                       // (carte qui arrive sur le tas : 0 -> 1)
        private float pulse;
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG), t = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF pioche = new RectF();
        private ValueAnimator anim;

        TableView(Context c) {
            super(c);
            t.setTextAlign(Paint.Align.CENTER);
            t.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
            ValueAnimator pa = ValueAnimator.ofFloat(0, 1);                 // (lueur de la pioche quand on doit piocher)
            pa.setDuration(1400); pa.setRepeatCount(ValueAnimator.INFINITE);
            pa.addUpdateListener(a -> { pulse = (float) a.getAnimatedValue(); if (this.my) invalidate(); });
            pa.start();
        }

        void set(int ph, int top, int np, int coul, int p2, int sens, boolean my) {
            if (top != this.top) {
                if (top < 0) pile.clear();
                else {
                    pile.add(top);
                    while (pile.size() > 4) pile.remove(0);
                    if (anim != null) anim.cancel();
                    anim = ValueAnimator.ofFloat(0, 1);
                    anim.setDuration(320);
                    anim.setInterpolator(new OvershootInterpolator(1.4f));
                    anim.addUpdateListener(a -> { drop = (float) a.getAnimatedValue(); invalidate(); });
                    drop = 0; anim.start();
                }
            }
            this.ph = ph; this.top = top; this.np = np; this.coul = coul; this.p2 = p2; this.sens = sens; this.my = my;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas g) {
            float w = getWidth(), h = getHeight(), m = dp(2);
            RectF felt = new RectF(m, dp(8), w - m, h - dp(4));
            p.setShader(new RadialGradient(w / 2, h / 2, Math.max(w, h) * 0.7f, new int[] { 0xFF1E5A4C, 0xFF0F3A31, 0xFF0A251F }, null, Shader.TileMode.CLAMP));
            p.setStyle(Paint.Style.FILL);
            g.drawRoundRect(felt, dp(26), dp(26), p);                       // (tapis de jeu, plein)
            p.setShader(null);
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(dp(3)); p.setColor(0xFF2E7A66);
            g.drawRoundRect(felt, dp(26), dp(26), p);
            p.setStyle(Paint.Style.FILL);
            if (ph < 2) {
                t.setColor(0xFFCFEDE4); t.setTextSize(dp(15));
                g.drawText(Lang.t("Tes potes ouvrent l'appli"), w / 2, h / 2 - dp(8), t);
                g.drawText(Lang.t("Plus > Jeu de cartes"), w / 2, h / 2 + dp(16), t);
                return;
            }
            float ch = Math.min(h * 0.72f, w * 0.42f), cw = ch * 0.69f, cy = h / 2 + dp(2);
            // pioche (a gauche) : touche-la pour piocher
            float px = w * 0.22f;
            for (int i = Math.min(3, Math.max(np, 1)) - 1; i >= 0; i--) {
                RectF r = new RectF(px - cw / 2 - i * dp(3), cy - ch / 2 - i * dp(3), px + cw / 2 - i * dp(3), cy + ch / 2 - i * dp(3));
                drawCard(g, r, -1, false, i == 0 && my ? 0.5f + 0.5f * (float) Math.sin(pulse * Math.PI * 2) : 0);
            }
            pioche.set(px - cw / 2 - dp(8), cy - ch / 2 - dp(8), px + cw / 2 + dp(4), cy + ch / 2 + dp(4));
            t.setColor(0xFFCFEDE4); t.setTextSize(dp(12));
            g.drawText(Lang.t(np + " dans la pioche"), px, cy + ch / 2 + dp(15), t);
            // tas (au milieu) : les dernieres cartes un peu tournees, la nouvelle arrive
            float tx = w * 0.53f;
            for (int i = 0; i < pile.size(); i++) {
                int c = pile.get(i);
                boolean last = i == pile.size() - 1;
                float rot = ((c * 37) % 21) - 10;
                float k = last ? 1.35f - 0.35f * drop : 1f, dy = last ? -(1 - drop) * dp(40) : 0;
                g.save();
                g.translate(tx, cy + dy);
                g.rotate(last ? rot * drop : rot);
                g.scale(k, k);
                drawCard(g, new RectF(-cw / 2, -ch / 2, cw / 2, ch / 2), c, false, 0);
                g.restore();
            }
            // a droite : couleur demandee par un 8, +2, sens du jeu
            float rx = w * 0.84f;
            float yy = cy - ch * 0.28f;
            if (coul >= 0) {
                p.setColor(0xFFFFFFFF);
                g.drawCircle(rx, yy, dp(26), p);
                p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(dp(3)); p.setColor(0xFFFFC94D); g.drawCircle(rx, yy, dp(26), p); p.setStyle(Paint.Style.FILL);
                t.setColor(coul == 1 || coul == 2 ? 0xFFD8243C : 0xFF1A1826); t.setTextSize(dp(30));
                g.drawText(Lang.t(SUIT[coul]), rx, yy + dp(10), t);
                t.setColor(0xFFFFE2A0); t.setTextSize(dp(11));
                g.drawText(Lang.t("demandé"), rx, yy + dp(42), t);
            }
            if (p2 > 0) {
                float y2 = coul >= 0 ? cy + ch * 0.3f : yy;
                p.setColor(0xFFE8304E); g.drawRoundRect(new RectF(rx - dp(26), y2 - dp(18), rx + dp(26), y2 + dp(18)), dp(18), dp(18), p);
                t.setColor(0xFFFFFFFF); t.setTextSize(dp(20));
                g.drawText("+" + p2, rx, y2 + dp(7), t);
            }
            t.setColor(0xFF8FD8C2); t.setTextSize(dp(22));
            g.drawText(sens > 0 ? "↻" : "↺", rx, cy + ch / 2, t);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if (e.getAction() == MotionEvent.ACTION_UP && pioche.contains(e.getX(), e.getY())) { performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); tapPile(); }
            return true;
        }
    }

    // ------------------------------------------------------------------ la main en eventail
    final class HandView extends View {
        private final class C { int c; float x, y, rot, tx, ty, trot, a = 1f; boolean can, gone; }
        private final ArrayList<C> cards = new ArrayList<>();
        private final HashMap<Integer, C> byCard = new HashMap<>();
        private boolean my;
        private int press = -1;
        private float shake;

        HandView(Context c) { super(c); }

        void set(ArrayList<Integer> list, HashSet<Integer> play, boolean my) {
            this.my = my;
            HashSet<Integer> now = new HashSet<>(list);
            for (C k : cards) if (!now.contains(k.c) && !k.gone) {           // (posee : elle s'envole vers la table)
                k.gone = true; k.ty = -getHeight() * 0.9f; k.tx = getWidth() / 2f; k.trot = 0;
            }
            ArrayList<C> order = new ArrayList<>();
            for (int c : list) {
                C k = byCard.get(c);
                if (k == null || k.gone) {                                  // (piochee : elle arrive d'en haut a gauche)
                    k = new C(); k.c = c; k.x = getWidth() * 0.15f; k.y = -getHeight() * 0.6f; k.rot = -25;
                    byCard.put(c, k);
                }
                k.can = my && play.contains(c);
                order.add(k);
            }
            ArrayList<C> flying = new ArrayList<>();
            for (C k : cards) if (k.gone) flying.add(k);
            cards.clear(); cards.addAll(order); cards.addAll(flying);
            layoutCards();
            postInvalidateOnAnimation();
        }

        void shake() { shake = 1f; postInvalidateOnAnimation(); }

        private float cw() { return Math.min(getHeight() * 0.62f, getWidth() * 0.24f); }

        private void layoutCards() {
            int n = 0;
            for (C k : cards) if (!k.gone) n++;
            float w = getWidth(), h = getHeight(), cw = cw(), ch = cw * 1.45f;
            float span = Math.min(w - cw - dp(16), n * cw * 0.62f);
            float step = n > 1 ? span / (n - 1) : 0, x0 = w / 2 - span / 2;
            float maxRot = Math.min(16f, n * 2.2f);
            int i = 0;
            for (C k : cards) {
                if (k.gone) continue;
                float f = n > 1 ? (float) i / (n - 1) * 2 - 1 : 0;           // (-1 .. 1)
                k.tx = x0 + i * step;
                k.ty = h - ch / 2 - dp(10) + f * f * dp(14) - (k.can ? dp(18) : 0) - (i == press ? dp(16) : 0);
                k.trot = f * maxRot;
                i++;
            }
        }

        @Override protected void onSizeChanged(int w, int h, int ow, int oh) { layoutCards(); }

        @Override
        protected void onDraw(Canvas g) {
            if (cards.isEmpty()) return;
            float cw = cw(), ch = cw * 1.45f;
            boolean moving = false;
            float sx = shake > 0 ? (float) Math.sin(shake * 30) * dp(8) * shake : 0;
            if (shake > 0) { shake = Math.max(0, shake - 0.06f); moving = true; }
            ArrayList<C> done = new ArrayList<>();
            for (C k : cards) {
                k.x += (k.tx - k.x) * 0.24f; k.y += (k.ty - k.y) * 0.24f; k.rot += (k.trot - k.rot) * 0.24f;
                if (k.gone) { k.a -= 0.08f; if (k.a <= 0) { done.add(k); continue; } }
                if (Math.abs(k.tx - k.x) > 0.5f || Math.abs(k.ty - k.y) > 0.5f || Math.abs(k.trot - k.rot) > 0.2f || k.gone) moving = true;
                g.save();
                g.translate(k.x + (k.gone ? 0 : sx), k.y);
                g.rotate(k.rot);
                if (k.gone) { float s = 0.7f + 0.3f * k.a; g.scale(s, s); }
                drawCard(g, new RectF(-cw / 2, -ch / 2, cw / 2, ch / 2), k.c, my && !k.can && !k.gone, k.can ? 0.6f : 0);
                g.restore();
            }
            for (C k : done) { cards.remove(k); if (byCard.get(k.c) == k) byCard.remove(k.c); }
            if (moving) postInvalidateOnAnimation();
        }

        private int hit(float x, float y) {
            float cw = cw(), ch = cw * 1.45f;
            int idx = -1, i = 0;
            for (C k : cards) {                                             // (la plus haute : la derniere dessinee)
                if (!k.gone) {
                    double a = Math.toRadians(-k.rot);
                    float dx = x - k.x, dy = y - k.y;
                    float rx = (float) (dx * Math.cos(a) - dy * Math.sin(a)), ry = (float) (dx * Math.sin(a) + dy * Math.cos(a));
                    if (Math.abs(rx) <= cw / 2 && Math.abs(ry) <= ch / 2) idx = i;
                    i++;
                }
            }
            return idx;
        }

        private C live(int i) { int j = 0; for (C k : cards) if (!k.gone) { if (j == i) return k; j++; } return null; }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_MOVE) {
                int h = hit(e.getX(), e.getY());
                if (h != press) { press = h; if (h >= 0) performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK); layoutCards(); postInvalidateOnAnimation(); }
            } else if (a == MotionEvent.ACTION_UP) {
                C k = press >= 0 ? live(press) : null;
                press = -1; layoutCards(); postInvalidateOnAnimation();
                if (k != null) {
                    if (k.can) { performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); tapCard(k.c); }
                    else if (my) shake();
                }
            } else if (a == MotionEvent.ACTION_CANCEL) { press = -1; layoutCards(); postInvalidateOnAnimation(); }
            return true;
        }
    }
}
