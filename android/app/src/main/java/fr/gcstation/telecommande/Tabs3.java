package fr.gcstation.telecommande;

// Ecrans Recherche (morceaux, radios et jeux d'un coup) et Copies (cartes memoire et reglages gardes sur le telephone).

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;

// ---------------------------------------------------------------- Recherche
class SearchTab extends Tab {
    private EditText field;
    private LinearLayout res;
    private TextView status;
    private List<String[]> stations;
    private List<JSONArray> games;
    private boolean asked;
    SearchTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        field = UiKit.search(a, "Morceau, artiste, radio, jeu…", this::run);
        v.addView(field, lp(-1, -2, 0));
        status = UiKit.hint(a, "Tape au moins 2 lettres");
        v.addView(status);
        ScrollView sc = new ScrollView(a);
        sc.setVerticalScrollBarEnabled(false);
        res = new LinearLayout(a);
        res.setOrientation(LinearLayout.VERTICAL);
        res.setPadding(0, 0, 0, dp(16));
        sc.addView(res);
        v.addView(sc, lp(-1, 0, 1));
        root = v;
        return v;
    }

    @Override void show() {
        if (!asked) { asked = true; load(); }
        field.requestFocus();
    }

    void load() {
        status.setText(Lang.t("Chargement des listes…"));
        if (QueueTab.tracks == null) Api.get("/api/tracks", r -> {
            if (r == null) return;
            List<String[]> l = new ArrayList<>();
            try { JSONArray j = new JSONArray(r); for (int k = 0; k < j.length(); k++) { JSONArray e = j.optJSONArray(k); l.add(new String[] { e != null ? e.optString(0) : "", e != null ? e.optString(1) : "" }); } } catch (Exception e) { }
            QueueTab.tracks = l;
            run();
        });
        Api.get("/api/stations", r -> {
            if (r == null) return;
            List<String[]> l = new ArrayList<>();
            try { JSONArray j = new JSONArray(r); for (int k = 0; k < j.length(); k++) l.add(new String[] { j.optString(k, ""), "" }); } catch (Exception e) { }
            stations = l;
            run();
        });
        Api.get("/api/games", r -> {
            if (r == null) return;
            List<JSONArray> l = new ArrayList<>();
            try { JSONArray j = new JSONArray(r); for (int k = 0; k < j.length(); k++) { JSONArray g = j.getJSONArray(k); g.put(k); if (!g.optString(1).isEmpty()) l.add(g); } } catch (Exception e) { }
            games = l;
            run();
        });
    }

    private void run() {
        String q = field.getText().toString().trim().toLowerCase(Locale.ROOT);
        res.removeAllViews();
        if (q.length() < 2) { status.setText(Lang.t(QueueTab.tracks == null ? "Chargement des listes…" : "Tape au moins 2 lettres")); return; }
        int n = 0;
        List<String[]> tr = QueueTab.tracks;
        List<Integer> t = new ArrayList<>();
        for (int k = 0; tr != null && k < tr.size() && t.size() < 40; k++) if (tr.get(k)[0].toLowerCase(Locale.ROOT).contains(q) || tr.get(k)[1].toLowerCase(Locale.ROOT).contains(q)) t.add(k);
        if (!t.isEmpty()) {
            head("MUSIQUE", Neon.PINK);
            for (final int k : t) {
                View r = row("♫", Neon.PINK, tr.get(k)[0], tr.get(k)[1]);
                r.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); Api.cmd("play", k); Lang.toast(a, "Lecture : " + tr.get(k)[0], Toast.LENGTH_SHORT).show(); });
                r.setOnLongClickListener(v -> {
                    new NeonDialog(a).title(tr.get(k)[0]).items(new String[] { "⤵  Lire juste après", "☰  Ajouter à la fin de la file" }, w ->
                        Api.get("/api/queueadd?i=" + k + "&d=" + (w == 0 ? 1 : 0), 10000, x -> Lang.toast(a, x != null ? "Ajouté à la file" : "Pas possible", Toast.LENGTH_SHORT).show())).cancel("Fermer").show();
                    return true;
                });
                n++;
            }
        }
        List<Integer> s = new ArrayList<>();
        for (int k = 0; stations != null && k < stations.size() && s.size() < 20; k++) if (stations.get(k)[0].toLowerCase(Locale.ROOT).contains(q)) s.add(k);
        if (!s.isEmpty()) {
            head("RADIO", Neon.CYAN);
            for (final int k : s) {
                View r = row("◉", Neon.CYAN, stations.get(k)[0], "");
                r.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); Api.cmd("radio", k); Lang.toast(a, "Radio : " + stations.get(k)[0], Toast.LENGTH_SHORT).show(); });
                n++;
            }
        }
        List<JSONArray> g = new ArrayList<>();
        for (int k = 0; games != null && k < games.size() && g.size() < 20; k++) { JSONArray x = games.get(k); if (x.optString(0).toLowerCase(Locale.ROOT).contains(q) || x.optString(1).toLowerCase(Locale.ROOT).contains(q)) g.add(x); }
        if (!g.isEmpty()) {
            head("JEUX", Neon.VIOLET);
            for (final JSONArray x : g) {
                View r = row("◈", Neon.VIOLET, (x.optInt(4) != 0 ? "★ " : "") + x.optString(0), x.optString(1));
                r.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); ((Host) a).openGame(x); });
                n++;
            }
        }
        status.setText(Lang.t(n == 0 ? "Rien trouvé" : n + " résultat" + (n > 1 ? "s" : "") + " · appui long sur un morceau : file d'attente"));
    }

    private void head(String s, int c) {
        TextView t = Neon.glow(a, s, 13, c);
        t.setPadding(dp(4), dp(14), 0, dp(4));
        res.addView(t);
    }

    private View row(String ic, int col, String name, String sub) {
        LinearLayout r = new LinearLayout(a);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setBackground(Neon.button(a, 16, false));
        r.setPadding(dp(12), dp(12), dp(12), dp(12));
        TextView i = Neon.glow(a, ic, 17, col);
        i.setGravity(Gravity.CENTER);
        r.addView(i, new LinearLayout.LayoutParams(dp(34), -2));
        LinearLayout c = new LinearLayout(a);
        c.setOrientation(LinearLayout.VERTICAL);
        TextView n = Neon.text(a, name, 15, Neon.TEXT, true);
        n.setSingleLine(true);
        n.setEllipsize(android.text.TextUtils.TruncateAt.END);
        c.addView(n);
        if (!sub.isEmpty()) { TextView s = Neon.text(a, sub, 12, Neon.DIM, false); s.setSingleLine(true); c.addView(s); }
        r.addView(c, new LinearLayout.LayoutParams(0, -2, 1));
        r.setClickable(true);
        LinearLayout.LayoutParams p = lp(-1, -2, 0);
        p.topMargin = dp(6);
        res.addView(r, p);
        return r;
    }
}

// ---------------------------------------------------------------- Copies (sur le telephone)
/** copie des cartes memoire virtuelles et de tous les reglages de la console, gardee dans le telephone
 *  (Android/data/fr.gcstation.telecommande/files/copies/<date>/) ; les 5 dernieres sont gardees */
final class Backup {
    static final int KEEP = 5;
    private static volatile boolean running;
    private Backup() { }

    static SharedPreferences prefs(Context c) { return c.getSharedPreferences("gcs", Context.MODE_PRIVATE); }
    static File dir(Context c) { File d = new File(c.getExternalFilesDir(null), "copies"); d.mkdirs(); return d; }
    // (build 82 : plus de copie toute seule par defaut, on la fait a la main ; nouvelle cle pour que l'ancien
    //  reglage "active par defaut" ne compte plus ; la reactiver dans Plus > Copies reste possible)
    static final String AUTO = "copie_auto_manuel";
    static boolean autoOn(Context c) { return prefs(c).getBoolean(AUTO, false); }

    static File[] list(Context c) {
        File[] l = dir(c).listFiles(File::isDirectory);
        if (l == null) return new File[0];
        Arrays.sort(l, (x, y) -> y.getName().compareTo(x.getName()));
        return l;
    }

    /** a la connexion : une copie si la derniere a plus de 20 heures */
    static void auto(Context c) {
        if (!autoOn(c) || System.currentTimeMillis() - prefs(c).getLong("copie_t", 0) < 20L * 3600 * 1000) return;
        run(c, true, null);
    }

    interface Done { void on(String msg); }

    static void run(final Context c, final boolean quiet, final Done done) {
        if (running) { if (done != null) done.on("Copie déjà en cours…"); return; }
        running = true;
        final Context ac = c.getApplicationContext();
        new Thread(() -> {
            String msg;
            File d = new File(dir(ac), new SimpleDateFormat("yyyy-MM-dd_HH'h'mm", Locale.FRANCE).format(new Date()));
            try {
                d.mkdirs();
                int cards = 0, fail = 0;
                String r = Api.get("/api/cards", 20000);
                JSONArray j = r != null ? new JSONArray(r) : null;
                for (int k = 0; j != null && k < j.length(); k++) {
                    JSONObject o = j.getJSONObject(k);
                    if (o.optInt("x") == 0) continue;
                    String f = o.optString("f");
                    if (f.isEmpty() || f.contains("/")) continue;
                    if (download("/api/cardfile/" + android.net.Uri.encode(f), new File(d, f))) cards++; else fail++;
                }
                boolean regs = download("/api/regpack", new File(d, "reglages.txt"));
                if (cards == 0 && !regs) { deleteDir(d); msg = "Copie impossible : la console ne répond pas"; }
                else {
                    prefs(ac).edit().putLong("copie_t", System.currentTimeMillis()).apply();
                    File[] all = list(ac);
                    for (int k = KEEP; k < all.length; k++) deleteDir(all[k]);
                    msg = "Copie faite : " + cards + " carte" + (cards > 1 ? "s" : "") + " mémoire" + (regs ? " et les réglages" : "") + (fail > 0 ? " (" + fail + " ratée" + (fail > 1 ? "s" : "") + ")" : "");
                    if (quiet) PlaybackService.notify(ac, PlaybackService.ID_COPY, "Copie de GC Station", msg);
                }
            } catch (Exception e) {
                deleteDir(d);
                msg = "Copie impossible";
            }
            running = false;
            final String m = msg;
            if (done != null) new android.os.Handler(android.os.Looper.getMainLooper()).post(() -> done.on(m));
        }).start();
    }

    /** fichier de la console -> fichier du telephone (ecrit a cote puis renomme : jamais a moitie) */
    static boolean download(String path, File out) {
        HttpURLConnection c = null;
        File part = new File(out.getPath() + ".part");
        try {
            c = (HttpURLConnection) new URL("http://" + Api.host + path).openConnection();
            c.setConnectTimeout(5000);
            c.setReadTimeout(30000);
            if (c.getResponseCode() != 200) return false;
            long n = 0;
            try (InputStream in = c.getInputStream(); FileOutputStream o = new FileOutputStream(part)) {
                byte[] b = new byte[32768];
                int k;
                while ((k = in.read(b)) > 0) { o.write(b, 0, k); n += k; }
            }
            if (n == 0) { part.delete(); return false; }
            out.delete();
            return part.renameTo(out);
        } catch (Exception e) {
            part.delete();
            return false;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    static void deleteDir(File d) {
        File[] l = d.listFiles();
        if (l != null) for (File f : l) { if (f.isDirectory()) deleteDir(f); else f.delete(); }
        d.delete();
    }

    /** reglages remis sur la console (envoyes, puis la console les relit) */
    static String restoreRegs(File f) {
        try (InputStream in = new FileInputStream(f)) {
            if (Api.put("regtel", "reglages.txt", in, f.length()) == null) return "Envoi impossible";
        } catch (Exception e) { return "Envoi impossible"; }
        String r = Api.get("/api/regrestore", 15000);
        try { return r != null ? new JSONObject(r).optString("msg", "Fait") : "Pas de réponse"; } catch (Exception e) { return "Fait"; }
    }

    /** une carte remise sur la console (l'ancienne est gardee en .bak par la console) */
    static String restoreCard(File f) {
        String name;
        try (InputStream in = new FileInputStream(f)) { name = Api.put("gci", f.getName(), in, f.length()); } catch (Exception e) { name = null; }
        if (name == null) return "Envoi impossible";
        String r = Api.get("/api/cardput?c=" + Api.enc(f.getName()) + "&n=" + Api.enc(name.trim()), 15000);
        try { return r != null ? new JSONObject(r).optString("m", "Fait") : "Pas de réponse"; } catch (Exception e) { return "Fait"; }
    }
}

class CopiesTab extends Tab {
    private LinearLayout box;
    private TextView status;
    private View sw;
    CopiesTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        LinearLayout c = UiKit.card(a);
        LinearLayout ar = new LinearLayout(a);
        ar.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout tx = new LinearLayout(a);
        tx.setOrientation(LinearLayout.VERTICAL);
        tx.addView(Neon.text(a, "Copie automatique", 15, Neon.TEXT, true));
        tx.addView(Neon.text(a, "Désactivée : tu fais tes copies à la main avec le bouton ci-dessous. Activée : une fois par jour, à la connexion (cartes mémoire virtuelles et réglages). Les " + Backup.KEEP + " dernières sont gardées.", 12, Neon.DIM, false));
        ar.addView(tx, lp(0, -2, 1));
        sw = NeonDialog.toggle(a, Backup.autoOn(a));
        ar.addView(sw, lp(dp(48), dp(28), 0));
        ar.setOnClickListener(x -> {
            boolean on = !Backup.autoOn(a);
            Backup.prefs(a).edit().putBoolean(Backup.AUTO, on).apply();
            NeonDialog.paintToggle(a, sw, on);
        });
        c.addView(ar);
        LinearLayout.LayoutParams bp = lp(-1, -2, 0);
        bp.topMargin = dp(12);
        c.addView(Neon.btn(a, "Faire une copie maintenant", 14, true, x -> {
            status.setText(Lang.t("Copie en cours (les cartes peuvent prendre un moment)…"));
            Backup.run(a, false, m -> { status.setText(Lang.t(m)); load(); });
        }), bp);
        v.addView(c, lp(-1, -2, 0));
        status = UiKit.hint(a, "");
        v.addView(status);
        ScrollView sc = new ScrollView(a);
        box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, 0, 0, dp(16));
        sc.addView(box);
        v.addView(sc, lp(-1, 0, 1));
        root = v;
        return v;
    }

    @Override void show() { load(); }

    void load() {
        box.removeAllViews();
        File[] l = Backup.list(a);
        long t = Backup.prefs(a).getLong("copie_t", 0);
        if (status.getText().length() == 0 || !status.getText().toString().startsWith("Copie"))
            status.setText(Lang.t(l.length == 0 ? "Aucune copie sur ce téléphone pour l'instant" : "Dernière copie : " + (t > 0 ? new SimpleDateFormat("d MMM 'à' HH'h'mm", Locale.FRANCE).format(new Date(t)) : l[0].getName())));
        for (final File d : l) {
            LinearLayout c = UiKit.card(a);
            c.addView(Neon.glow(a, d.getName().replace('_', ' ').replace("h", " h "), 15, Neon.PINK));
            File[] fs = d.listFiles(f -> f.isFile() && !f.getName().endsWith(".part"));
            if (fs == null) fs = new File[0];
            final List<File> cards = new ArrayList<>();
            File regs = null;
            long size = 0;
            for (File f : fs) { size += f.length(); if (f.getName().equals("reglages.txt")) regs = f; else if (f.getName().toLowerCase(Locale.ROOT).endsWith(".raw")) cards.add(f); }
            c.addView(UiKit.hint(a, cards.size() + " carte" + (cards.size() > 1 ? "s" : "") + (regs != null ? " + réglages" : "") + " · " + (size / 1024 / 1024 > 0 ? size / 1024 / 1024 + " Mo" : size / 1024 + " Ko")));
            LinearLayout row = new LinearLayout(a);
            row.setPadding(0, dp(6), 0, 0);
            final File fr = regs;
            if (fr != null) row.addView(Neon.btn(a, "Réglages", 12, false, x -> NeonDialog.confirm(a, "Remettre ces réglages ?", "Les réglages de la console sont remplacés par ceux de cette copie.", "Remettre", () -> restore(() -> Backup.restoreRegs(fr)))), lp(0, -2, 1));
            if (!cards.isEmpty()) {
                if (row.getChildCount() > 0) row.addView(new View(a), lp(dp(8), 1, 0));
                row.addView(Neon.btn(a, "Une carte…", 12, false, x -> {
                    String[] names = new String[cards.size()];
                    for (int k = 0; k < names.length; k++) names[k] = cards.get(k).getName() + "  (" + cards.get(k).length() / 1024 / 1024 + " Mo)";
                    new NeonDialog(a).title("Remettre quelle carte ?").message("L'ancienne est gardée en .bak sur la console.")
                        .items(names, w -> restore(() -> Backup.restoreCard(cards.get(w)))).cancel("Annuler").show();
                }), lp(0, -2, 1));
            }
            row.addView(new View(a), lp(dp(8), 1, 0));
            row.addView(Neon.btn(a, "Supprimer", 12, false, x -> NeonDialog.confirm(a, "Supprimer cette copie ?", null, "Supprimer", () -> { Backup.deleteDir(d); load(); })), lp(0, -2, 1));
            c.addView(row);
            LinearLayout.LayoutParams p = lp(-1, -2, 0);
            p.topMargin = dp(10);
            box.addView(c, p);
        }
        TextView w = UiKit.hint(a, "Dossier : Android/data/fr.gcstation.telecommande/files/copies (supprimé si tu désinstalles l'appli).");
        w.setPadding(dp(4), dp(14), 0, 0);
        box.addView(w);
    }

    interface Job { String run(); }
    private void restore(final Job j) {
        status.setText(Lang.t("Envoi vers la console…"));
        new Thread(() -> { final String m = j.run(); a.runOnUiThread(() -> { status.setText(Lang.t(m)); Lang.toast(a, m, Toast.LENGTH_LONG).show(); }); }).start();
    }
}
