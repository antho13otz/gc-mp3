package fr.gcstation.telecommande;

// Dernier jeu lance avec Pulse (build 727 et plus : /api/pulse, /api/pulse.txt, "pj" / "dl" dans /api/state) :
//  - la carte "Dernier lancement" en haut de l'onglet Jeux (resultat, ou, combien de temps ; journal ; partager) ;
//  - le dernier resultat de chaque jeu, garde sur le telephone (fiche du jeu) ;
//  - les notifications : jeu plante / fige, telechargement fini (verifiees a chaque etat de la console, meme
//    appli fermee tant que le service de lecture tourne).

import android.app.Activity;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONObject;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

final class LastLaunch {
    static final String[] VERDICT = {
        "Quitté avec A+Z+START",
        "Le jeu a planté",
        "Le jeu s'est figé (repéré par Pulse)",
        "Aucune nouvelle du jeu (bloqué très tôt)",
        "Pas de retour (console éteinte ou redémarrée ?)" };
    static final int[] COLOR = { Neon.CYAN, 0xFFFF4D6D, 0xFFFFA040, 0xFFFFA040, Neon.DIM };
    static final int ID_GAME = 5, ID_DL = 6, ID_ACH = 9;

    static String verdict(int r) { return r >= 0 && r < VERDICT.length ? VERDICT[r] : "?"; }
    static int color(int r) { return r >= 0 && r < COLOR.length ? COLOR[r] : Neon.DIM; }
    private static SharedPreferences prefs(Context c) { return c.getSharedPreferences("gcs", Context.MODE_PRIVATE); }

    static String ago(long when) {
        long s = System.currentTimeMillis() / 1000 - when;
        if (when <= 0) return "";
        if (s < 90) return "à l'instant";
        if (s < 3600) return "il y a " + (s / 60) + " min";
        if (s < 86400) return "il y a " + (s / 3600) + " h";
        return "le " + new java.text.SimpleDateFormat("d MMM", java.util.Locale.FRANCE).format(new java.util.Date(when * 1000));
    }

    /** dernier resultat connu d'un jeu (garde par le telephone) : "r|quand|sec", ou null */
    static int[] forGame(Context c, String id) {
        String v = prefs(c).getString("pulse_" + id, null);
        if (v == null) return null;
        try { String[] p = v.split("\\|"); return new int[] { Integer.parseInt(p[0]), Integer.parseInt(p[1]), p.length > 2 ? Integer.parseInt(p[2]) : 0 }; } catch (Exception e) { return null; }
    }
    private static void remember(Context c, JSONObject j) {
        String id = j.optString("id");
        if (id.length() == 6) prefs(c).edit().putString("pulse_" + id, j.optInt("r") + "|" + j.optLong("quand") + "|" + j.optInt("sec")).apply();
    }

    // ---------- notifications (appele a chaque etat de la console, hors du fil de l'interface) ----------
    static void check(final Context c, JSONObject s) {
        if (!s.has("pj")) return;                            // (console plus ancienne)
        SharedPreferences p = prefs(c);
        long pj = s.optLong("pj");
        long seen = p.getLong("pj_vu", -1);
        if (pj > 0 && pj != seen) {
            p.edit().putLong("pj_vu", pj).apply();
            final boolean tell = seen >= 0;                   // (1re fois : on retient sans prevenir)
            Api.pool(() -> {
                String r = Api.get("/api/pulse", 4000);
                if (r == null) return;
                try {
                    JSONObject j = new JSONObject(r);
                    remember(c, j);
                    int v = j.optInt("r");
                    if (tell && v >= 1 && v <= 3)
                        PlaybackService.notify(c, ID_GAME, j.optString("nom", j.optString("id")) + " : " + verdict(v).toLowerCase(java.util.Locale.FRANCE).replace("le jeu ", ""),
                                "Ouvre l'onglet Jeux de l'appli : « Dernier lancement » > Partager le journal.");
                } catch (Exception e) { }
            });
        }
        long ac = s.optLong("ach", 0), aseen = p.getLong("ach_vu", -1);   // (GC Station v268 : succes obtenus dans un jeu)
        if (ac > 0 && ac != aseen) {
            p.edit().putLong("ach_vu", ac).apply();
            if (aseen >= 0 || System.currentTimeMillis() / 1000 - ac < 600)
                PlaybackService.notify(c, ID_ACH, "\uD83C\uDFC6 " + s.optString("acht", "Succès obtenu"), "Liste : onglet Jeux > Dernier lancement > Succès.");
        }
        long dl = s.optLong("dl", -1), dseen = p.getLong("dl_vu", -1);
        if (dl >= 0 && dl != dseen) {
            p.edit().putLong("dl_vu", dl).apply();
            if (dseen >= 0 && dl > dseen && !s.optString("dln").isEmpty())   // (compteur remis a 0 : la console a redemarre)
                PlaybackService.notify(c, ID_DL, "Téléchargement fini", s.optString("dln") + " est sur la carte SD de la console.");
        }
    }

    // ---------- la carte de l'onglet Jeux ----------
    interface OpenGame { void open(String id); }
    final LinearLayout view;
    private final Activity a;
    private final TextView title, line, sub;
    private final OpenGame open;
    long pj = -1;
    private String gameId = "";

    LastLaunch(Activity a, OpenGame open) {
        this.a = a;
        this.open = open;
        view = UiKit.card(a);
        view.addView(Neon.glow(a, "DERNIER LANCEMENT", 12, Neon.CYAN));
        title = Neon.text(a, "", 15, Neon.TEXT, true);
        title.setPadding(0, Neon.dp(a, 4), 0, 0);
        view.addView(title);
        line = Neon.text(a, "", 13, Neon.CYAN, true);
        view.addView(line);
        sub = Neon.text(a, "", 12, Neon.DIM, false);
        view.addView(sub);
        LinearLayout row = new LinearLayout(a);
        row.setPadding(0, Neon.dp(a, 8), 0, 0);
        row.addView(Neon.btn(a, "Journal", 12, false, v -> showJournal(a)), new LinearLayout.LayoutParams(0, -2, 1));
        View g = new View(a); row.addView(g, new LinearLayout.LayoutParams(Neon.dp(a, 6), 1));
        row.addView(Neon.btn(a, "Partager le journal", 12, true, v -> share(a, gameId)), new LinearLayout.LayoutParams(0, -2, 1.4f));
        View g2 = new View(a); row.addView(g2, new LinearLayout.LayoutParams(Neon.dp(a, 6), 1));
        row.addView(Neon.btn(a, "Succès", 12, false, v -> showAch(a)), new LinearLayout.LayoutParams(0, -2, 1));
        view.addView(row);
        title.setOnClickListener(v -> { if (!gameId.isEmpty() && open != null) open.open(gameId); });
        view.setVisibility(View.GONE);
    }

    void refresh() {
        Api.get("/api/pulse", r -> {
            JSONObject j = null;
            try { if (r != null) j = new JSONObject(r); } catch (Exception e) { }
            if (j == null || j.optString("id").isEmpty()) { view.setVisibility(View.GONE); return; }
            pj = j.optLong("quand");
            gameId = j.optString("id");
            int v = j.optInt("r");
            final JSONObject jj = j;
            Api.pool(() -> remember(a, jj));
            title.setText(j.optString("nom", gameId).trim() + "  ·  " + gameId);
            line.setText(verdict(v));
            line.setTextColor(color(v));
            String pc = j.optString("pc");
            int sec = j.optInt("sec");
            String d = ago(pj);
            if (sec > 0) d += " · joué " + (sec >= 3600 ? (sec / 3600) + " h " + (sec % 3600 / 60) + " min" : sec >= 60 ? (sec / 60) + " min" : sec + " s");
            if ((v == 1 || v == 2 || v == 0) && !pc.isEmpty() && !pc.equals("00000000")) d += " · endroit : " + pc;
            sub.setText(d);
            view.setVisibility(View.VISIBLE);
        });
    }

    /** succes des jeux (GC Station v268 : verifies par Pulse pendant le jeu) */
    static void showAch(final Activity a) {
        Api.get("/api/succes", r -> {
            org.json.JSONArray j = null;
            try { if (r != null) j = new org.json.JSONArray(r); } catch (Exception e) { }
            if (j == null) { Toast.makeText(a, "Pas de réponse de la console (GC Station v268 et plus)", Toast.LENGTH_SHORT).show(); return; }
            LinearLayout box = new LinearLayout(a);
            box.setOrientation(LinearLayout.VERTICAL);
            String last = "";
            int got = 0;
            for (int i = 0; i < j.length(); i++) {
                org.json.JSONArray x = j.optJSONArray(i);
                if (x == null) continue;
                String id = x.optString(0);
                if (!id.equals(last)) {
                    last = id;
                    TextView h = Neon.glow(a, id, 13, Neon.CYAN);
                    h.setPadding(0, Neon.dp(a, i == 0 ? 0 : 10), 0, Neon.dp(a, 2));
                    box.addView(h);
                }
                boolean ok = x.optInt(3) != 0;
                if (ok) got++;
                TextView t = Neon.text(a, (ok ? "\uD83C\uDFC6  " : "\u25CB  ") + x.optString(1) + "\n      " + x.optString(2), 13, ok ? Neon.TEXT : Neon.DIM, ok);
                t.setPadding(0, Neon.dp(a, 3), 0, Neon.dp(a, 3));
                box.addView(t);
            }
            if (j.length() == 0) box.addView(Neon.text(a, "Aucun succès : ajoute un fichier SD:/gc-mp3/succes/<ID du jeu>.txt (quelques jeux en ont déjà un).", 13, Neon.DIM, false));
            new NeonDialog(a).title("Succès · " + got + " / " + j.length()).view(box).cancel("Fermer").show();   // (la boite defile deja)
        });
    }

    static void showJournal(final Activity a) {
        Api.get("/api/pulse.txt", r -> {
            if (r == null) { Toast.makeText(a, "Pas de journal sur la console", Toast.LENGTH_SHORT).show(); return; }
            TextView t = Neon.text(a, "", 10, Neon.TEXT, false);
            t.setTypeface(Typeface.MONOSPACE);
            t.setTextIsSelectable(true);
            t.setText(r);
            new NeonDialog(a).title("Journal du dernier lancement").view(t).cancel("Fermer").button("Partager", true, () -> share(a, "")).show();
        });
    }

    /** le journal en fichier .txt (Telechargements / GC Station), envoye avec le partage d'Android */
    static void share(final Activity a, final String id) {
        Toast.makeText(a, "Lecture du journal…", Toast.LENGTH_SHORT).show();
        Api.get("/api/pulse.txt", r -> {
            if (r == null) { Toast.makeText(a, "Pas de journal sur la console", Toast.LENGTH_SHORT).show(); return; }
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("text/plain");
            i.putExtra(Intent.EXTRA_SUBJECT, "Journal GC Station" + (id.isEmpty() ? "" : " · " + id));
            Uri u = null;
            if (Build.VERSION.SDK_INT >= 29) try {
                ContentValues cv = new ContentValues();
                cv.put(MediaStore.Downloads.DISPLAY_NAME, "journal-" + (id.isEmpty() ? "jeu" : id) + "-" + (System.currentTimeMillis() / 1000) + ".txt");
                cv.put(MediaStore.Downloads.MIME_TYPE, "text/plain");
                cv.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/GC Station");
                u = a.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
                if (u != null) try (OutputStream o = a.getContentResolver().openOutputStream(u)) { o.write(r.getBytes(StandardCharsets.UTF_8)); }
            } catch (Exception e) { u = null; }
            if (u != null) { i.putExtra(Intent.EXTRA_STREAM, u); i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            else i.putExtra(Intent.EXTRA_TEXT, r);
            try { a.startActivity(Intent.createChooser(i, "Partager le journal")); } catch (Exception e) { }
        });
    }
}
