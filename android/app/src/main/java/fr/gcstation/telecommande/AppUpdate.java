package fr.gcstation.telecommande;

// Mise a jour de l'appli depuis l'appli (⚙ > Mettre a jour l'appli).
// L'APK n'est pas public : il est joint aux compilations GitHub (android.yml, artefact "gc-station-telecommande").
// Pour le telecharger, GitHub demande un jeton : un "fine-grained token" en lecture seule (Actions : Read-only) sur le
// depot antho13otz/gc-mp3, colle une fois dans l'appli et garde sur le telephone (preferences privees de l'appli).
//  1. derniere compilation reussie de l'appli sur main (API GitHub) : son numero = versionCode de l'APK ;
//  2. plus recente que l'appli installee : l'artefact (zip) est telecharge, l'APK qu'il contient est donne a
//     l'installateur d'Android (PackageInstaller), qui demande la confirmation ; meme signature : par-dessus.
// Build 452 : GitLab (le compte GitHub est suspendu depuis le 6 novembre 2026). Un jeton GitLab ("glpat-...", projet
// GC-Station-group/GC-Station, role Developer (les fichiers sont reserves aux developpeurs du projet), droit read_api) : le dernier job "appli" reussi sur main, son numero de build
// (ci/numeros.env, APPLI_BUILD, a ce commit) et son APK (artefact, visible des membres seulement). Un jeton GitHub : comme avant.

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class AppUpdate {
    static final String REPO = "antho13otz/gc-mp3";
    static final String ARTIFACT = BuildConfig.MAJ_ARTEFACT;   // (chaque appli : sa compilation et son artefact, build.gradle)
    static final String ACT_DONE = "fr.gcstation.MAJ_APPLI";
    private static final Handler ui = new Handler(Looper.getMainLooper());

    private AppUpdate() { }

    private static SharedPreferences prefs(Context c) { return c.getSharedPreferences("gcs", Context.MODE_PRIVATE); }
    static String token(Context c) { return !BuildConfig.PRIVE ? PUBLIC : prefs(c).getString("gh_jeton", ""); }   // (build 457 : version publique : sans jeton)

    static int installed(Context c) {
        try {
            android.content.pm.PackageInfo pi = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return Build.VERSION.SDK_INT >= 28 ? (int) pi.getLongVersionCode() : pi.versionCode;
        } catch (Exception e) { return 0; }
    }

    // ---------- GitLab (build 452) ----------
    static final String GL = "https://gitlab.com/api/v4/projects/88617655";
    static boolean gitlab(String tok) { return tok.startsWith("glpat-"); }
    // ---------- build 457 : version publique : paquet generique public du projet (appli-publique/dernier), sans jeton ----------
    static final String PUBLIC = "public";
    static boolean pub(String tok) { return PUBLIC.equals(tok); }
    private static HttpURLConnection pubGet(String nom) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(GL + "/packages/generic/" + BuildConfig.MAJ_PAQUET + "/dernier/" + nom).openConnection();
        c.setConnectTimeout(8000); c.setReadTimeout(30000);
        c.setRequestProperty("User-Agent", "gc-remote-lite");
        int code = c.getResponseCode();
        if (code == 404) throw new Exception("Pas encore de version publiée.");
        if (code != 200) throw new Exception("GitLab : erreur " + code);
        return c;
    }
    /** {numero, 0, numero} de la derniere version publique (version.json : {"build": N, "apk": "nom"}) */
    static long[] latestPublic() throws Exception {
        HttpURLConnection c = pubGet("version.json");
        InputStream in = c.getInputStream(); ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[4096]; int n; while ((n = in.read(b)) > 0) o.write(b, 0, n); c.disconnect();
        JSONObject j = new JSONObject(o.toString("UTF-8"));
        long v = j.getLong("build"); pubApk = j.optString("apk", "gc-remote-lite-" + v + ".apk");
        return new long[] { v, 0, v };
    }
    private static volatile String pubApk = "";
    private static InputStream downloadPublic() throws Exception { return pubGet(pubApk).getInputStream(); }
    static long[] derniere(String tok) throws Exception { return pub(tok) ? latestPublic() : gitlab(tok) ? latestGitLab(tok) : latest(tok); }
    private static HttpURLConnection gl(String path, String tok) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(GL + path).openConnection();
        c.setConnectTimeout(8000);
        c.setReadTimeout(30000);
        c.setRequestProperty("User-Agent", "gc-station-telecommande");
        c.setRequestProperty("PRIVATE-TOKEN", tok);
        int code = c.getResponseCode();
        if (code == 401) throw new Exception("Jeton refusé par GitLab (expiré ou mal copié ?)");
        if (code == 403 || code == 404) throw new Exception("GitLab refuse (" + code + ") : le jeton a-t-il le rôle Developer et le droit read_api ?");
        if (code != 200) throw new Exception("GitLab : erreur " + code);
        return c;
    }
    private static String glText(String path, String tok) throws Exception {
        HttpURLConnection c = gl(path, tok);
        InputStream in = c.getInputStream();
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192]; int n;
        while ((n = in.read(b)) > 0) o.write(b, 0, n);
        c.disconnect();
        return o.toString("UTF-8");
    }
    /** derniere appli compilee sur GitLab : {numero, id du job} ; null si aucune */
    static long[] latestGitLab(String tok) throws Exception {
        JSONArray jobs = new JSONArray(glText("/jobs?scope%5B%5D=success&per_page=50", tok));
        for (int i = 0; i < jobs.length(); i++) {
            JSONObject j = jobs.getJSONObject(i);
            if (!"appli".equals(j.optString("name")) || !"main".equals(j.optString("ref"))) continue;
            JSONArray arts = j.optJSONArray("artifacts");
            boolean ok = false;
            if (arts != null) for (int k = 0; k < arts.length(); k++) if ("archive".equals(arts.getJSONObject(k).optString("file_type"))) ok = true;
            if (!ok) continue;                                   // (artefact efface : 90 jours)
            String sha = j.getJSONObject("commit").getString("id");
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("(?m)^APPLI_BUILD=(\\d+)").matcher(glText("/repository/files/ci%2Fnumeros.env/raw?ref=" + sha, tok));
            if (!m.find()) continue;
            return new long[] { Long.parseLong(BuildConfig.MAJ_PREFIXE + m.group(1)), j.getLong("id"), Long.parseLong(m.group(1)) };
        }
        return null;
    }
    private static InputStream downloadGitLab(long job, long n, String tok) throws Exception {
        return gl("/jobs/" + job + "/artifacts/gc-station-telecommande-" + n + ".apk", tok).getInputStream();
    }

    // ---------- GitHub ----------
    private static String api(String path, String tok) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL("https://api.github.com/repos/" + REPO + path).openConnection();
        c.setConnectTimeout(8000);
        c.setReadTimeout(15000);
        c.setRequestProperty("Accept", "application/vnd.github+json");
        c.setRequestProperty("User-Agent", "gc-station-telecommande");
        if (!tok.isEmpty()) c.setRequestProperty("Authorization", "Bearer " + tok);
        int code = c.getResponseCode();
        if (code == 401) throw new Exception("Jeton refusé par GitHub (expiré ou mal copié ?)");
        if (code == 403 || code == 404) throw new Exception("GitHub refuse (" + code + ") : le jeton a-t-il « Actions : Read-only » sur " + REPO + " ?");
        if (code != 200) throw new Exception("GitHub : erreur " + code);
        InputStream in = c.getInputStream();
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192]; int n;
        while ((n = in.read(b)) > 0) o.write(b, 0, n);
        c.disconnect();
        return o.toString("UTF-8");
    }

    /** derniere version publiee : {numero, id de l'artefact} ; null si aucune */
    static long[] latest(String tok) throws Exception {
        JSONArray runs = new JSONObject(api("/actions/workflows/" + BuildConfig.MAJ_WORKFLOW + "/runs?branch=" + BuildConfig.MAJ_BRANCHE + "&status=success&event=push&per_page=10", tok)).getJSONArray("workflow_runs");
        // (le plus grand numero de compilation, pas le premier de la liste : GitHub ne les range pas toujours dans l'ordre)
        java.util.List<JSONObject> l = new java.util.ArrayList<>();
        for (int i = 0; i < runs.length(); i++) l.add(runs.getJSONObject(i));
        java.util.Collections.sort(l, (x, y) -> Long.compare(y.optLong("run_number"), x.optLong("run_number")));
        for (JSONObject r : l) {
            JSONArray arts = new JSONObject(api("/actions/runs/" + r.getLong("id") + "/artifacts", tok)).getJSONArray("artifacts");
            for (int k = 0; k < arts.length(); k++) {
                JSONObject a = arts.getJSONObject(k);
                if (ARTIFACT.equals(a.optString("name")) && !a.optBoolean("expired")) return new long[] { Long.parseLong(BuildConfig.MAJ_PREFIXE + r.getLong("run_number")), a.getLong("id") };   // (versionCode : prefixe + numero de la compilation, comme le .yml)
            }
        }
        return null;
    }

    /** l'artefact (zip) : GitHub renvoie vers un stockage a part, qui ne veut PAS du jeton */
    private static InputStream download(long artifact, String tok) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL("https://api.github.com/repos/" + REPO + "/actions/artifacts/" + artifact + "/zip").openConnection();
        c.setInstanceFollowRedirects(false);
        c.setConnectTimeout(8000);
        c.setRequestProperty("User-Agent", "gc-station-telecommande");
        c.setRequestProperty("Authorization", "Bearer " + tok);
        int code = c.getResponseCode();
        String loc = c.getHeaderField("Location");
        c.disconnect();
        if (code / 100 != 3 || loc == null) throw new Exception(code == 401 || code == 403 ? "Jeton refusé pour le téléchargement (Actions : Read-only ?)" : "Téléchargement refusé (" + code + ")");
        HttpURLConnection d = (HttpURLConnection) new URL(loc).openConnection();
        d.setConnectTimeout(8000);
        d.setReadTimeout(30000);
        if (d.getResponseCode() != 200) throw new Exception("Téléchargement : erreur " + d.getResponseCode());
        return d.getInputStream();
    }

    // ---------- installation ----------
    private static void install(Context c, InputStream zip) throws Exception { install(c, zip, true); }
    private static void install(Context c, InputStream zip, boolean isZip) throws Exception {
        PackageInstaller pi = c.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams sp = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        sp.setAppPackageName(c.getPackageName());
        int id = pi.createSession(sp);
        PackageInstaller.Session s = pi.openSession(id);
        boolean ok = false;
        if (!isZip) {                                            // (build 452 : GitLab donne l'APK lui-meme)
            try (OutputStream o = s.openWrite("gc-remote.apk", 0, -1)) {
                byte[] b = new byte[65536]; int n; long tot = 0;
                while ((n = zip.read(b)) > 0) { o.write(b, 0, n); tot += n; }
                s.fsync(o);
                ok = tot > 100000;
            }
        } else
        try (ZipInputStream z = new ZipInputStream(zip)) {
            ZipEntry e;
            while ((e = z.getNextEntry()) != null) {
                if (!e.getName().endsWith(".apk")) continue;
                try (OutputStream o = s.openWrite("gc-remote.apk", 0, -1)) {
                    byte[] b = new byte[65536]; int n;
                    while ((n = z.read(b)) > 0) o.write(b, 0, n);
                    s.fsync(o);
                }
                ok = true;
                break;
            }
        }
        if (!ok) { s.abandon(); throw new Exception("Pas d'APK dans le fichier téléchargé"); }
        Intent done = new Intent(c, Done.class).setAction(ACT_DONE);
        int fl = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
        s.commit(PendingIntent.getBroadcast(c, 7, done, fl).getIntentSender());
        s.close();
    }

    /** resultat de l'installateur : il faut d'abord la confirmation de la personne */
    public static class Done extends BroadcastReceiver {
        @Override public void onReceive(Context c, Intent i) {
            int st = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            if (st == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                Intent confirm = i.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm != null) { confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); try { c.startActivity(confirm); } catch (Exception e) { } }
            } else if (st != PackageInstaller.STATUS_SUCCESS) {
                String m = i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
                Lang.toast(c, "Mise à jour pas installée" + (m != null ? " : " + m : ""), Toast.LENGTH_LONG).show();
            }
        }
    }

    // ---------- ecran ⚙ > Mettre a jour l'appli ----------
    static void open(final Activity a) {
        if (token(a).isEmpty()) { askToken(a); return; }
        final TextView msg = Neon.text(a, "Recherche d'une nouvelle version…", 14, Neon.TEXT, false);
        final NeonDialog dlg = new NeonDialog(a).title("Mettre à jour l'appli").view(msg);
        if (BuildConfig.PRIVE) dlg.button("Changer le jeton", false, () -> askToken(a));
        dlg.cancel("Fermer");
        dlg.show();
        final String tok = token(a);
        final int have = installed(a);
        new Thread(() -> {
            try {
                final boolean g = gitlab(tok);
                long[] l = derniere(tok);
                if (l == null) { ui.post(() -> msg.setText(Lang.t("Aucune version de l'appli disponible sur " + (g || pub(tok) ? "GitLab" : "GitHub") + " (les fichiers sont gardés 90 jours)."))); return; }
                if (l[0] <= have) { ui.post(() -> msg.setText(Lang.t("L'appli est à jour (version 1." + have + ")."))); return; }
                ui.post(() -> {
                    msg.setText(Lang.t("Nouvelle version : 1." + l[0] + " (installée : 1." + have + ")."));
                    new NeonDialog(a).title("Version 1." + l[0])
                        .message("Télécharger et installer ? Android demande ensuite de confirmer. Tes réglages sont gardés.")
                        .button("Installer", true, () -> get(a, tok, l)).cancel("Plus tard").show();
                });
            } catch (Exception e) {
                ui.post(() -> msg.setText(Lang.t(e.getMessage() != null ? e.getMessage() : "Pas de réponse")));
            }
        }).start();
    }

    private static void get(final Activity a, final String tok, final long[] l) {
        if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
            Lang.toast(a, "Autorise GC Remote à installer des applis, puis reviens et réessaie.", Toast.LENGTH_LONG).show();
            try { a.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + a.getPackageName()))); } catch (Exception e) { }
            return;
        }
        Lang.toast(a, "Téléchargement de la nouvelle version…", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            boolean g = gitlab(tok);
            try (InputStream in = pub(tok) ? downloadPublic() : g ? downloadGitLab(l[1], l[2], tok) : download(l[1], tok)) {
                install(a.getApplicationContext(), in, !g && !pub(tok));
            } catch (Exception e) {
                ui.post(() -> Lang.toast(a, e.getMessage() != null ? e.getMessage() : "Échec du téléchargement", Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    static void askToken(final Activity a) {
        LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.addView(Neon.text(a, "L'appli n'est pas publique : GitLab demande un jeton pour la télécharger. À faire une fois :\n"
            + "1. « Créer le jeton » ouvre le projet GitLab (connecte-toi).\n"
            + "2. Add new token : nom GC Remote ; rôle Developer ; coche seulement read_api.\n"
            + "3. Create project access token, puis copie le jeton (glpat-…).\n"
            + "4. Colle-le ici. Il reste sur ce téléphone.\n(Un jeton GitHub marche encore, si le dépôt GitHub revient.)", 13, Neon.DIM, false));
        final EditText f = new EditText(a);
        f.setHint("glpat-…");
        f.setHintTextColor(Neon.FAINT);
        f.setTextColor(Neon.TEXT);
        f.setSingleLine(true);
        f.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        f.setBackground(Neon.glass(a, 14));
        f.setPadding(Neon.dp(a, 12), Neon.dp(a, 10), Neon.dp(a, 12), Neon.dp(a, 10));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = Neon.dp(a, 12);
        box.addView(f, lp);
        new NeonDialog(a).title("Jeton GitLab").view(box)
            .button("Créer le jeton", false, false, () -> {
                try { a.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://gitlab.com/gc-station-group/gc-station/-/settings/access_tokens"))); } catch (Exception e) { }
            })
            .button("Enregistrer", true, () -> {
                String t = f.getText().toString().trim();
                prefs(a).edit().putString("gh_jeton", t).apply();
                if (!t.isEmpty()) open(a);
            }).cancel("Annuler").show();
    }

    /** nouvelle version de l'appli : une notification (a l'ouverture de l'appli, et toutes les 6 h ; au plus toutes les 15 min) */
    private static volatile long lastCheck;
    static void check(final Context c) {
        final String tok = token(c);
        if (tok.isEmpty() || System.currentTimeMillis() - lastCheck < 15 * 60 * 1000L) return;
        lastCheck = System.currentTimeMillis();
        new Thread(() -> {
            try {
                long[] l = derniere(tok);
                if (l == null || l[0] <= installed(c) || prefs(c).getLong("appli_vue", 0) >= l[0]) return;
                prefs(c).edit().putLong("appli_vue", l[0]).apply();
                PlaybackService.notify(c, 10, "Nouvelle version de l'appli : 1." + l[0], "Dans l'appli : ⚙ > Mettre à jour l'appli.");
            } catch (Exception e) { }
        }).start();
    }
}
