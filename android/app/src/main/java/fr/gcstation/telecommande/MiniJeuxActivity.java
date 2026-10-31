package fr.gcstation.telecommande;

// Mini-jeux de GC Station depuis le téléphone (appli build 49, console build 765) : choisis un jeu, il démarre sur la
// télé et le téléphone devient la manette du joueur choisi (ManetteActivity, mode GC Station : UDP 51238).
// Chacun de tes potes fait pareil avec son téléphone et prend un autre joueur : jusqu'à 4, avec ou sans vraies manettes.

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.widget.GridLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

public class MiniJeuxActivity extends Activity {
    static void open(Context c) { c.startActivity(new Intent(c, MiniJeuxActivity.class)); }

    private static final int[] PCOL = { 0xFF78B4FF, 0xFFFF788C, 0xFF78E68C, 0xFFFFD25A };
    private SharedPreferences sp;
    private LinearLayout players;
    private GridLayout grid;
    private TextView sub;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        Lang.init(this);
        sp = getSharedPreferences("gcs", MODE_PRIVATE);
        getWindow().setStatusBarColor(0xFF120A26);
        getWindow().setNavigationBarColor(0xFF0A0616);
        ScrollView sc = new ScrollView(this);
        sc.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0xFF1A0E36, 0xFF0A0616 }));
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(16), dp(20), dp(16), dp(20));
        sc.addView(col);
        col.addView(Neon.glow(this, "Mini-jeux", 26, Neon.PINK));
        sub = Neon.text(this, "Choisis un jeu : il démarre sur la télé, et ton téléphone devient ta manette. Tes potes font pareil avec le leur.", 13, Neon.DIM, false);
        sub.setPadding(0, dp(4), 0, dp(14));
        col.addView(sub);

        col.addView(Neon.text(this, "TU ES LE", 11, Neon.FAINT, true));
        players = new LinearLayout(this);
        players.setPadding(0, dp(6), 0, dp(14));
        col.addView(players);
        drawPlayers();

        grid = new GridLayout(this);
        grid.setColumnCount(2);
        col.addView(grid);

        TextView pad = Neon.btn(this, "🎮  Juste la manette (menus et jeux)", 15, true, v -> ManetteActivity.openGcs(this));
        LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(-1, dp(52));
        pl.topMargin = dp(16);
        col.addView(pad, pl);
        setContentView(sc);
        load();
    }

    private void drawPlayers() {
        players.removeAllViews();
        int cur = Math.max(0, sp.getInt("manette_joueur", 0)) & 3;
        for (int i = 0; i < 4; i++) {
            final int p = i;
            TextView t = Neon.text(this, "Joueur " + (i + 1), 14, i == cur ? 0xFF120820 : Neon.TEXT, true);
            t.setGravity(Gravity.CENTER);
            GradientDrawable g = new GradientDrawable();
            g.setCornerRadius(dp(16));
            if (i == cur) g.setColor(PCOL[i]); else { g.setColor(0xFF1E1236); g.setStroke(dp(2), PCOL[i]); }
            t.setBackground(g);
            t.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); sp.edit().putInt("manette_joueur", p).apply(); drawPlayers(); });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(40), 1);
            lp.rightMargin = i < 3 ? dp(6) : 0;
            players.addView(t, lp);
        }
    }

    private void load() {
        Api.get("/api/minijeux", r -> {
            JSONArray j = null;
            try { j = r == null ? null : new JSONObject(r).optJSONArray("j"); } catch (Exception e) { }
            grid.removeAllViews();
            if (j == null) { sub.setText(Lang.t("La console ne répond pas, ou elle n'a pas encore le build 765.")); return; }
            for (int i = 0; i < j.length(); i++) {
                JSONObject o = j.optJSONObject(i);
                card(i, o.optString("n"), o.optString("d"));
            }
        });
    }

    private static String icon(String n) {
        String s = n.toLowerCase(java.util.Locale.FRANCE);
        if (s.contains("karaok")) return "🎤"; if (s.contains("rythme")) return "🥁"; if (s.contains("quiz")) return "❓";
        if (s.contains("snake")) return "🐍"; if (s.contains("brique")) return "🧱"; if (s.contains("pong")) return "🏓";
        if (s.contains("volante")) return "🎵"; if (s.contains("simon")) return "🔴"; if (s.contains("gaine")) return "⚡";
        if (s.contains("course")) return "🏃"; if (s.contains("bulle")) return "🫧"; if (s.contains("moto")) return "🏍";
        if (s.contains("hockey")) return "🏒"; if (s.contains("puissance")) return "🔵"; if (s.contains("sumo")) return "🤼";
        return "🎮";
    }

    private void card(int i, String name, String desc) {
        boolean duel = desc.toLowerCase(java.util.Locale.FRANCE).contains("duel") || desc.contains("à 4") || desc.contains("2 joueurs");
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setPadding(dp(14), dp(14), dp(14), dp(14));
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TL_BR, duel ? new int[] { 0xFF3A1A6A, 0xFF1C0E3C } : new int[] { 0xFF241848, 0xFF140C2C });
        g.setCornerRadius(dp(20));
        g.setStroke(dp(1), duel ? 0xFF8A5CFF : 0xFF3A2A66);
        c.setBackground(g);
        c.setClickable(true);
        LinearLayout top = new LinearLayout(this);
        top.setGravity(Gravity.CENTER_VERTICAL);
        TextView ic = new TextView(this);
        ic.setText(Lang.t(icon(name)));
        ic.setTextSize(30);
        top.addView(ic, new LinearLayout.LayoutParams(0, -2, 1));
        if (duel) {
            TextView badge = Neon.text(this, "À PLUSIEURS", 10, 0xFF120820, true);
            GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] { Neon.PINK, Neon.CYAN });
            bg.setCornerRadius(dp(10));
            badge.setBackground(bg);
            badge.setPadding(dp(8), dp(3), dp(8), dp(3));
            top.addView(badge);
        }
        c.addView(top);
        TextView n = Neon.text(this, name, 16, Neon.TEXT, true);
        n.setPadding(0, dp(8), 0, dp(2));
        c.addView(n);
        c.addView(Neon.text(this, desc, 12, Neon.DIM, false));
        c.setOnClickListener(v -> {
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            v.animate().scaleX(0.95f).scaleY(0.95f).setDuration(80).withEndAction(() -> v.animate().scaleX(1).scaleY(1).setDuration(120).start()).start();
            Api.get("/api/minijeux?g=" + i, r -> ManetteActivity.openGcs(this));
        });
        GridLayout.LayoutParams lp = new GridLayout.LayoutParams(GridLayout.spec(i / 2), GridLayout.spec(i % 2, 1f));
        lp.width = 0;
        lp.setMargins(dp(5), dp(5), dp(5), dp(5));
        grid.addView(c, lp);
    }

    private int dp(float v) { return Neon.dp(this, v); }
}
