package fr.gcstation.telecommande;

// Mise a jour de l'appli depuis l'appli (⚙ > Mettre a jour l'appli).
// L'APK n'est pas public : il est joint aux compilations GitHub (android.yml, artefact "gc-station-telecommande").
// Pour le telecharger, GitHub demande un jeton : un "fine-grained token" en lecture seule (Actions : Read-only) sur le
// depot antho13otz/gc-mp3, colle une fois dans l'appli et garde sur le telephone (preferences privees de l'appli).
//  1. derniere compilation reussie de l'appli sur main (API GitHub) : son numero = versionCode de l'APK ;
//  2. plus recente que l'appli installee : l'artefact (zip) est telecharge, l'APK qu'il contient est donne a
//     l'installateur d'Android (PackageInstaller), qui demande la confirmation ; meme signature : par-dessus.

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
    static String token(Context c) { return prefs(c).getString("gh_jeton", ""); }

    static int installed(Context c) {
        try {
            android.content.pm.PackageInfo pi = c.getPackageManager().getPackageInfo(c.getPackageName(), 0);
            return Build.VERSION.SDK_INT >= 28 ? (int) pi.getLongVersionCode() : pi.versionCode;
        } catch (Exception e) { return 0; }
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
        JSONArray runs = new JSONObject(api("/actions/workflows/" + BuildConfig.MAJ_WORKFLOW + "/runs?branch=" + BuildConfig.MAJ_BRANCHE + "&status=success&event=push&per_page=5", tok)).getJSONArray("workflow_runs");
        for (int i = 0; i < runs.length(); i++) {
            JSONObject r = runs.getJSONObject(i);
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
    private static void install(Context c, InputStream zip) throws Exception {
        PackageInstaller pi = c.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams sp = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        sp.setAppPackageName(c.getPackageName());
        int id = pi.createSession(sp);
        PackageInstaller.Session s = pi.openSession(id);
        boolean ok = false;
        try (ZipInputStream z = new ZipInputStream(zip)) {
            ZipEntry e;
            while ((e = z.getNextEntry()) != null) {
                if (!e.getName().endsWith(".apk")) continue;
                try (OutputStream o = s.openWrite("gc-station.apk", 0, -1)) {
                    byte[] b = new byte[65536]; int n;
                    while ((n = z.read(b)) > 0) o.write(b, 0, n);
                    s.fsync(o);
                }
                ok = true;
                break;
            }
        }
        if (!ok) { s.abandon(); throw new Exception("Pas d'APK dans le fichier de GitHub"); }
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
                Toast.makeText(c, "Mise à jour pas installée" + (m != null ? " : " + m : ""), Toast.LENGTH_LONG).show();
            }
        }
    }

    // ---------- ecran ⚙ > Mettre a jour l'appli ----------
    static void open(final Activity a) {
        if (token(a).isEmpty()) { askToken(a); return; }
        final TextView msg = Neon.text(a, "Recherche d'une nouvelle version…", 14, Neon.TEXT, false);
        final NeonDialog dlg = new NeonDialog(a).title("Mettre à jour l'appli").view(msg)
            .button("Changer le jeton", false, () -> askToken(a)).cancel("Fermer");
        dlg.show();
        final String tok = token(a);
        final int have = installed(a);
        new Thread(() -> {
            try {
                long[] l = latest(tok);
                if (l == null) { ui.post(() -> msg.setText("Aucune version de l'appli disponible sur GitHub (les fichiers sont gardés 90 jours).")); return; }
                if (l[0] <= have) { ui.post(() -> msg.setText("L'appli est à jour (version 1." + have + ").")); return; }
                ui.post(() -> {
                    msg.setText("Nouvelle version : 1." + l[0] + " (installée : 1." + have + ").");
                    new NeonDialog(a).title("Version 1." + l[0])
                        .message("Télécharger et installer ? Android demande ensuite de confirmer. Tes réglages sont gardés.")
                        .button("Installer", true, () -> get(a, tok, l[1])).cancel("Plus tard").show();
                });
            } catch (Exception e) {
                ui.post(() -> msg.setText(e.getMessage() != null ? e.getMessage() : "Pas de réponse de GitHub"));
            }
        }).start();
    }

    private static void get(final Activity a, final String tok, final long artifact) {
        if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
            Toast.makeText(a, "Autorise GC Station à installer des applis, puis reviens et réessaie.", Toast.LENGTH_LONG).show();
            try { a.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + a.getPackageName()))); } catch (Exception e) { }
            return;
        }
        Toast.makeText(a, "Téléchargement de la nouvelle version…", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try (InputStream in = download(artifact, tok)) {
                install(a.getApplicationContext(), in);
            } catch (Exception e) {
                ui.post(() -> Toast.makeText(a, e.getMessage() != null ? e.getMessage() : "Échec du téléchargement", Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    static void askToken(final Activity a) {
        LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.addView(Neon.text(a, "L'appli n'est pas publique : GitHub demande un jeton pour la télécharger. À faire une fois :\n"
            + "1. « Créer le jeton » ouvre GitHub (connecte-toi).\n"
            + "2. Nom : GC Station ; Repository access : Only select repositories > " + REPO + ".\n"
            + "3. Permissions > Repository > Actions : Read-only. Generate token, puis copie-le.\n"
            + "4. Colle-le ici. Il reste sur ce téléphone.", 13, Neon.DIM, false));
        final EditText f = new EditText(a);
        f.setHint("github_pat_…");
        f.setHintTextColor(Neon.FAINT);
        f.setTextColor(Neon.TEXT);
        f.setSingleLine(true);
        f.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        f.setBackground(Neon.glass(a, 14));
        f.setPadding(Neon.dp(a, 12), Neon.dp(a, 10), Neon.dp(a, 12), Neon.dp(a, 10));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
        lp.topMargin = Neon.dp(a, 12);
        box.addView(f, lp);
        new NeonDialog(a).title("Jeton GitHub").view(box)
            .button("Créer le jeton", false, false, () -> {
                try { a.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/settings/personal-access-tokens/new"))); } catch (Exception e) { }
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
                long[] l = latest(tok);
                if (l == null || l[0] <= installed(c) || prefs(c).getLong("appli_vue", 0) >= l[0]) return;
                prefs(c).edit().putLong("appli_vue", l[0]).apply();
                PlaybackService.notify(c, 10, "Nouvelle version de l'appli : 1." + l[0], "Dans l'appli : ⚙ > Mettre à jour l'appli.");
            } catch (Exception e) { }
        }).start();
    }
}
