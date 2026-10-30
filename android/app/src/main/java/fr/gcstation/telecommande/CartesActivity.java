package fr.gcstation.telecommande;

// Jeu de cartes entre potes : le 8 américain (console build 762, appli build 46).
// Chacun ouvre l'appli (Plus > Jeu de cartes) : son téléphone montre SA main, la télé montre la table.
// Tout se décide dans la console (/api/cartes) : l'appli demande l'état toutes les 0,6 s et envoie ses coups.
// Cartes : c = couleur * 13 + valeur (couleurs : 0 pique, 1 cœur, 2 carreau, 3 trèfle ; valeurs : 0 as ... 12 roi).

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

public class CartesActivity extends Activity {
    static void open(Context c) { c.startActivity(new Intent(c, CartesActivity.class)); }

    static final String[] RANG = { "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "V", "D", "R" };
    static final String[] SUIT = { "♠", "♥", "♦", "♣" };
    static final String[] SUIT_NOM = { "Pique", "Cœur", "Carreau", "Trèfle" };
    static boolean red(int c) { int s = c / 13; return s == 1 || s == 2; }

    private SharedPreferences sp;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private boolean alive, busy;
    private long tok;
    private JSONObject st;
    private long lastVer = -1;
    private boolean wasMyTurn;

    private LinearLayout players, hand, actions, body;
    private TextView title, msg, big, err;
    private FrameLayout table;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        sp = getSharedPreferences("gcs", MODE_PRIVATE);
        tok = sp.getLong("c8_tok", 0);
        getWindow().setStatusBarColor(Neon.BG_TOP);
        getWindow().setNavigationBarColor(Neon.BG_BOTTOM);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        FrameLayout root = new FrameLayout(this);
        root.addView(new AuroraView(this), new FrameLayout.LayoutParams(-1, -1));
        ScrollView sc = new ScrollView(this);
        sc.setFillViewport(true);
        body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        int p = dp(14);
        body.setPadding(p, dp(18), p, p);
        sc.addView(body);
        root.addView(sc, new FrameLayout.LayoutParams(-1, -1));

        title = Neon.glow(this, "8 américain", 24, Neon.PINK);
        body.addView(title);
        msg = Neon.text(this, "Connexion à la table…", 13, Neon.DIM, false);
        msg.setPadding(0, dp(4), 0, dp(10));
        body.addView(msg);

        HorizontalScrollView ps = new HorizontalScrollView(this);
        ps.setHorizontalScrollBarEnabled(false);
        players = new LinearLayout(this);
        ps.addView(players);
        body.addView(ps);

        table = new FrameLayout(this);
        LinearLayout.LayoutParams tl = new LinearLayout.LayoutParams(-1, dp(170));
        tl.topMargin = dp(12);
        body.addView(table, tl);

        big = Neon.text(this, "", 20, Neon.TEXT, true);
        big.setGravity(Gravity.CENTER);
        big.setPadding(0, dp(8), 0, dp(4));
        body.addView(big);
        err = Neon.text(this, "", 13, 0xFFFF7A90, false);
        err.setGravity(Gravity.CENTER);
        body.addView(err);

        actions = new LinearLayout(this);
        actions.setGravity(Gravity.CENTER);
        actions.setPadding(0, dp(8), 0, dp(8));
        body.addView(actions);

        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        hand = new LinearLayout(this);
        hand.setPadding(dp(4), dp(18), dp(4), dp(8));
        hs.addView(hand);
        body.addView(hs);

        View fill = new View(this);
        body.addView(fill, new LinearLayout.LayoutParams(1, 0, 1));
        LinearLayout bottom = new LinearLayout(this);
        bottom.setGravity(Gravity.CENTER);
        bottom.addView(small("📺 Sur la télé", v -> call("tele", "")));
        bottom.addView(small("Quitter la table", v -> NeonDialog.confirm(this, "Quitter la table ?", "Tes cartes retournent sous la pioche.", "Quitter", () -> {
            call("quitter", ""); tok = 0; sp.edit().putLong("c8_tok", 0).apply(); finish(); })));
        bottom.addView(small("Tout arrêter", v -> NeonDialog.confirm(this, "Arrêter la partie pour tout le monde ?", null, "Arrêter", () -> {
            call("fin", ""); tok = 0; sp.edit().putLong("c8_tok", 0).apply(); finish(); })));
        body.addView(bottom);
        setContentView(root);

        if (tok == 0) askName(); else poll();
    }

    @Override protected void onResume() { super.onResume(); alive = true; ui.removeCallbacks(tick); ui.postDelayed(tick, 600); }
    @Override protected void onPause() { super.onPause(); alive = false; ui.removeCallbacks(tick); }

    private final Runnable tick = new Runnable() { @Override public void run() { if (!alive) return; if (tok != 0) poll(); ui.postDelayed(this, 600); } };

    private void askName() {
        NeonDialog.ask(this, "Ton nom à la table", sp.getString("c8_nom", ""), v -> {
            if (v.isEmpty()) v = "Joueur";
            sp.edit().putString("c8_nom", v).apply();
            final String nom = v;
            Api.get("/api/cartes?op=join&nom=" + Api.enc(nom), r -> {
                JSONObject j = parse(r);
                if (j == null) { msg.setText("La console ne répond pas (es-tu sur le même Wi-Fi ?)"); return; }
                if (j.optInt("ok") == 0) { show(j); err.setText(j.optString("err")); return; }
                tok = j.optLong("tok");
                sp.edit().putLong("c8_tok", tok).apply();
                show(j);
            });
        });
    }

    private void poll() {
        if (busy) return;
        busy = true;
        Api.get("/api/cartes?op=etat&tok=" + tok, r -> {
            busy = false;
            JSONObject j = parse(r);
            if (j == null) { msg.setText("La console ne répond pas…"); return; }
            if (j.optInt("moi", -1) < 0 && tok != 0) {          // (plus a la table : partie finie, parti trop longtemps)
                tok = 0; sp.edit().putLong("c8_tok", 0).apply();
                show(j);
                big.setText("Tu n'es plus à la table");
                actions.removeAllViews();
                actions.addView(btn("Rejoindre la table", true, v -> askName()));
                return;
            }
            show(j);
        });
    }

    private void call(String op, String extra) {
        if (tok == 0 && !op.equals("fin")) return;
        Api.get("/api/cartes?op=" + op + "&tok=" + tok + extra, r -> {
            JSONObject j = parse(r);
            if (j == null) return;
            show(j);
            if (j.optInt("ok") == 0) {
                err.setText(j.optString("err"));
                getWindow().getDecorView().performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
            }
        });
    }

    private static JSONObject parse(String r) { try { return r == null ? null : new JSONObject(r); } catch (Exception e) { return null; } }

    // ------------------------------------------------------------------ affichage de l'etat
    private void show(JSONObject j) {
        st = j;
        int ph = j.optInt("ph"), moi = j.optInt("moi", -1), tour = j.optInt("tour");
        long ver = j.optLong("ver");
        if (ver != lastVer) { err.setText(""); lastVer = ver; }
        msg.setText(j.optString("msg"));
        JSONArray js = j.optJSONArray("j");
        int n = js == null ? 0 : js.length();
        title.setText(ph >= 2 ? "8 américain · manche " + j.optInt("manche") : "8 américain · salon");

        players.removeAllViews();
        for (int i = 0; i < n; i++) {
            JSONObject o = js.optJSONObject(i);
            boolean cur = ph == 2 && i == tour, win = ph == 3 && i == j.optInt("gag");
            LinearLayout chip = new LinearLayout(this);
            chip.setOrientation(LinearLayout.VERTICAL);
            chip.setPadding(dp(12), dp(8), dp(12), dp(8));
            chip.setBackground(cur ? Neon.neonFill(this, 16) : Neon.glass(this, 16));
            TextView nm = Neon.text(this, o.optString("n") + (i == moi ? " (toi)" : ""), 14, cur ? 0xFF0B0B12 : win ? 0xFF6BFFB0 : Neon.TEXT, true);
            chip.addView(nm);
            TextView inf = Neon.text(this, (ph >= 2 ? o.optInt("c") + " carte" + (o.optInt("c") > 1 ? "s" : "") + " · " : "") + o.optInt("s") + " pts", 12, cur ? 0xFF1A1030 : Neon.DIM, false);
            chip.addView(inf);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, -2);
            lp.rightMargin = dp(8);
            players.addView(chip, lp);
        }

        table.removeAllViews();
        actions.removeAllViews();
        hand.removeAllViews();
        boolean my = ph == 2 && moi == tour;
        if (my && !wasMyTurn) getWindow().getDecorView().performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
        wasMyTurn = my;

        if (ph <= 1) {
            big.setText(n < 2 ? "On attend au moins un autre joueur…" : n + " joueurs à la table");
            TextView h = Neon.text(this, "Tes potes ouvrent l'appli GC Station > Plus > Jeu de cartes. La table s'affiche sur la télé.", 14, Neon.DIM, false);
            h.setGravity(Gravity.CENTER);
            table.addView(h, new FrameLayout.LayoutParams(-1, -2, Gravity.CENTER));
            if (moi >= 0 && n >= 2) actions.addView(btn("Commencer la partie", true, v -> call("debut", "")));
            return;
        }

        // la table : la pioche, la carte du dessus, la couleur demandee, les +2
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER);
        CardView back = new CardView(this, -1, false);
        back.label = String.valueOf(j.optInt("np"));
        row.addView(back, new LinearLayout.LayoutParams(dp(96), dp(140)));
        View sp1 = new View(this);
        row.addView(sp1, new LinearLayout.LayoutParams(dp(22), 1));
        row.addView(new CardView(this, j.optInt("haut", -1), false), new LinearLayout.LayoutParams(dp(100), dp(146)));
        int coul = j.optInt("coul", -1), p2 = j.optInt("p2");
        LinearLayout side = new LinearLayout(this);
        side.setOrientation(LinearLayout.VERTICAL);
        side.setGravity(Gravity.CENTER);
        side.setPadding(dp(14), 0, 0, 0);
        int top = j.optInt("haut", -1);
        if (coul >= 0 && top >= 0 && (top % 13 == 7)) {
            TextView cs = Neon.text(this, SUIT[coul], 44, coul == 1 || coul == 2 ? 0xFFFF4D6D : 0xFFF4EEFF, true);
            cs.setGravity(Gravity.CENTER);
            side.addView(cs);
            side.addView(Neon.text(this, "demandé", 12, Neon.DIM, false));
        }
        if (p2 > 0) side.addView(Neon.text(this, "+" + p2, 30, 0xFFFF4D6D, true));
        row.addView(side);
        table.addView(row, new FrameLayout.LayoutParams(-1, -1, Gravity.CENTER));

        if (ph == 3) {
            int g = j.optInt("gag");
            String w = g >= 0 && g < n ? js.optJSONObject(g).optString("n") : "?";
            big.setText(g == moi ? "Tu gagnes la manche ! 🎉" : w + " gagne la manche");
            actions.addView(btn("Manche suivante", true, v -> call("nouvelle", "")));
        } else {
            String who = tour < n ? js.optJSONObject(tour).optString("n") : "?";
            big.setText(my ? "À toi de jouer !" : "À " + who + " de jouer");
            if (my) {
                boolean pi = j.optInt("pi") != 0;
                actions.addView(btn(p2 > 0 ? "Piocher " + p2 + " cartes" : pi ? "Piocher (déjà fait)" : "Piocher", !pi, v -> call("piocher", "")));
                if (pi) actions.addView(btn("Passer", true, v -> call("passer", "")));
            }
        }

        // ma main : les cartes jouables sont levees ; toucher = poser
        JSONArray m = j.optJSONArray("main"), ok = j.optJSONArray("jouables");
        java.util.HashSet<Integer> play = new java.util.HashSet<>();
        for (int i = 0; ok != null && i < ok.length(); i++) play.add(ok.optInt(i));
        java.util.ArrayList<Integer> cards = new java.util.ArrayList<>();
        for (int i = 0; m != null && i < m.length(); i++) cards.add(m.optInt(i));
        java.util.Collections.sort(cards, (a, bb) -> a / 13 != bb / 13 ? a / 13 - bb / 13 : a % 13 - bb % 13);   // (rangees par couleur)
        for (int c : cards) {
            boolean can = play.contains(c);
            CardView cv = new CardView(this, c, my && !can);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(78), dp(114));
            lp.rightMargin = -dp(14);
            lp.topMargin = can ? 0 : dp(14);
            cv.setElevation(dp(2));
            if (my && can) cv.setOnClickListener(v -> {
                v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                if (c % 13 == 7) pickSuit(c); else call("jouer", "&c=" + c);
            });
            hand.addView(cv, lp);
        }
    }

    private void pickSuit(int c) {
        new NeonDialog(this).title("Le 8 : quelle couleur ?")
            .items(new String[] { SUIT[0] + "  " + SUIT_NOM[0], SUIT[1] + "  " + SUIT_NOM[1], SUIT[2] + "  " + SUIT_NOM[2], SUIT[3] + "  " + SUIT_NOM[3] },
                   w -> call("jouer", "&c=" + c + "&s=" + w))
            .cancel("Annuler").show();
    }

    private TextView btn(String s, boolean primary, View.OnClickListener l) {
        TextView t = Neon.btn(this, s, 15, primary, l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, dp(48));
        lp.leftMargin = lp.rightMargin = dp(6);
        t.setLayoutParams(lp);
        t.setPadding(dp(18), 0, dp(18), 0);
        return t;
    }

    private TextView small(String s, View.OnClickListener l) {
        TextView t = Neon.btn(this, s, 12, false, l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-2, dp(40));
        lp.leftMargin = lp.rightMargin = dp(4);
        lp.topMargin = dp(10);
        t.setLayoutParams(lp);
        t.setPadding(dp(12), 0, dp(12), 0);
        return t;
    }

    private int dp(float v) { return Neon.dp(this, v); }

    /** une carte dessinee (c < 0 : le dos de la pioche) */
    static final class CardView extends View {
        final int c; final boolean dim;
        String label;
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG), t = new Paint(Paint.ANTI_ALIAS_FLAG);

        CardView(Context ctx, int c, boolean dim) {
            super(ctx);
            this.c = c; this.dim = dim;
            t.setTextAlign(Paint.Align.CENTER);
            t.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        }

        @Override
        protected void onDraw(Canvas g) {
            float w = getWidth(), h = getHeight(), r = w * 0.12f, m = w * 0.04f;
            RectF rf = new RectF(m, m, w - m, h - m);
            p.setStyle(Paint.Style.FILL);
            p.setShader(null);
            p.setColor(0x55000000);
            g.drawRoundRect(new RectF(m + 2, m + 4, w - m + 2, h - m + 4), r, r, p);
            if (c < 0) {
                p.setShader(new LinearGradient(0, 0, w, h, 0xFF8A4BFF, 0xFF3A1490, Shader.TileMode.CLAMP));
                g.drawRoundRect(rf, r, r, p);
                p.setShader(null);
                p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(w * 0.03f); p.setColor(0xCCE6DAFF);
                RectF in = new RectF(rf); in.inset(w * 0.08f, w * 0.08f);
                g.drawRoundRect(in, r * 0.7f, r * 0.7f, p);
                t.setColor(0xFFF0E8FF); t.setTextSize(w * 0.2f);
                g.drawText("GC", w / 2, h * 0.47f, t);
                if (label != null) { t.setTextSize(w * 0.16f); g.drawText(label, w / 2, h * 0.68f, t); }
                return;
            }
            p.setShader(new LinearGradient(0, 0, 0, h, 0xFFFFFFFF, 0xFFE6E3F2, Shader.TileMode.CLAMP));
            g.drawRoundRect(rf, r, r, p);
            p.setShader(null);
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(Math.max(1.5f, w * 0.015f)); p.setColor(0xFF9C90C8);
            g.drawRoundRect(rf, r, r, p);
            int col = red(c) ? 0xFFD8243C : 0xFF1A1826;
            String rk = RANG[c % 13], su = SUIT[c / 13];
            t.setColor(col);
            t.setTextAlign(Paint.Align.LEFT);
            t.setTextSize(w * 0.24f);
            g.drawText(rk, w * 0.12f, h * 0.22f, t);
            t.setTextSize(w * 0.2f);
            g.drawText(su, w * 0.12f, h * 0.38f, t);
            t.setTextAlign(Paint.Align.CENTER);
            t.setTextSize(w * 0.55f);
            g.drawText(su, w * 0.56f, h * 0.78f, t);
            if (dim) { p.setStyle(Paint.Style.FILL); p.setColor(0x88100820); g.drawRoundRect(rf, r, r, p); }
        }
    }
}
