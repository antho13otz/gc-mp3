package fr.gcstation.telecommande;

// Telecommande GC Station : la page du telephone de la console (http://<console>/) en plein ecran.
// - trouve la console sur le Wi-Fi (adresses x.y.z.1 a 254, page /api/state), retient son adresse ;
// - envoi de fichiers depuis la page (bouton Envoyer, Photos) et telechargements (Fichiers) ;
// - "Partager -> GC Station" : le fichier part sur la carte SD de la console (musique, photos, videos...).

import android.app.Activity;
import android.app.AlertDialog;
import android.app.DownloadManager;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.URLUtil;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.URL;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public class MainActivity extends Activity {
    private static final int REQ_FILE = 1;
    private static final int BG = Color.rgb(16, 14, 30);

    private WebView web;
    private LinearLayout panel;
    private TextView status;
    private ProgressBar spinner;
    private EditText ipField;
    private String host;                       // adresse de la console (ex. 192.168.1.20)
    private ValueCallback<Uri[]> fileCb;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean searching;
    private Intent pendingShare;               // fichiers partages en attendant de trouver la console

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().setStatusBarColor(BG);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(BG);

        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        web.setBackgroundColor(BG);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
                Uri u = r.getUrl();
                if (host != null && host.equals(u.getHost())) return false;   // pages de la console : ici
                try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (Exception e) { }
                return true;                                                  // le reste : le navigateur
            }
            @Override
            public void onReceivedError(WebView v, WebResourceRequest r, WebResourceError e) {
                if (r.isForMainFrame()) showPanel("Console injoignable (" + host + "). Elle est allumée, sur le même Wi-Fi, avec la télécommande activée ?", false);
            }
        });
        web.setWebChromeClient(new WebChromeClient() {   // (alert / confirm / prompt : boites d'Android par defaut)
            @Override
            public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
                if (fileCb != null) fileCb.onReceiveValue(null);
                fileCb = cb;
                Intent i = p.createIntent();
                if (p.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try { startActivityForResult(i, REQ_FILE); } catch (Exception e) { fileCb = null; return false; }
                return true;
            }
        });
        web.setDownloadListener((url, ua, cd, mime, len) -> download(url, cd, mime));
        root.addView(web, new FrameLayout.LayoutParams(-1, -1));

        panel = buildPanel();
        root.addView(panel, new FrameLayout.LayoutParams(-1, -1));

        TextView gear = new TextView(this);                // reglages : chercher a nouveau, adresse
        gear.setText("⚙");
        gear.setTextColor(Color.WHITE);
        gear.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        gear.setGravity(Gravity.CENTER);
        GradientDrawable gd = new GradientDrawable();
        gd.setShape(GradientDrawable.OVAL);
        gd.setColor(Color.argb(110, 90, 60, 200));
        gear.setBackground(gd);
        int sz = dp(40);
        FrameLayout.LayoutParams glp = new FrameLayout.LayoutParams(sz, sz, Gravity.BOTTOM | Gravity.END);
        glp.setMargins(0, 0, dp(10), dp(10));
        gear.setOnClickListener(v -> menu());
        root.addView(gear, glp);

        setContentView(root);
        host = prefs().getString("host", null);
        if (isShare(getIntent())) pendingShare = getIntent();
        start();
    }

    @Override
    protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        if (isShare(i)) { if (host != null && panel.getVisibility() != View.VISIBLE) shareFiles(i); else pendingShare = i; }
    }

    private SharedPreferences prefs() { return getSharedPreferences("gcs", MODE_PRIVATE); }
    private int dp(int v) { return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()); }

    // ---------- demarrage : l'adresse retenue, sinon la recherche ----------
    private void start() {
        if (host == null) { search(); return; }
        showPanel("Connexion à la console (" + host + ")…", true);
        new Thread(() -> {
            boolean ok = isConsole(host, 1500);
            ui.post(() -> { if (ok) open(); else search(); });
        }).start();
    }

    private void open() {
        panel.setVisibility(View.GONE);
        web.setVisibility(View.VISIBLE);
        web.loadUrl("http://" + host + "/");
        if (pendingShare != null) { Intent i = pendingShare; pendingShare = null; shareFiles(i); }
    }

    private static boolean isConsole(String ip, int ms) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("http://" + ip + "/api/state").openConnection();
            c.setConnectTimeout(ms);
            c.setReadTimeout(ms * 2);
            if (c.getResponseCode() != 200) return false;
            InputStream in = c.getInputStream();
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] buf = new byte[2048];
            int n;
            while (o.size() < 16384 && (n = in.read(buf)) > 0) o.write(buf, 0, n);
            String s = o.toString("UTF-8");
            return s.startsWith("{\"title\":") && s.contains("\"vol\"");   // (la reponse de GC Station)
        } catch (Exception e) {
            return false;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    // ---------- recherche sur le Wi-Fi ----------
    private static List<String> localPrefixes() {
        List<String> r = new ArrayList<>();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) continue;
                for (InetAddress a : Collections.list(ni.getInetAddresses())) {
                    if (!(a instanceof Inet4Address) || !a.isSiteLocalAddress()) continue;
                    String ip = a.getHostAddress();
                    String p = ip.substring(0, ip.lastIndexOf('.') + 1);
                    if (ni.getName().startsWith("wlan")) r.add(0, p); else if (!r.contains(p)) r.add(p);
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
            List<String> pre = localPrefixes();
            AtomicReference<String> found = new AtomicReference<>();
            AtomicInteger done = new AtomicInteger();
            for (String p : pre) {
                if (found.get() != null) break;
                ExecutorService ex = Executors.newFixedThreadPool(48);
                for (int i = 1; i <= 254; i++) {
                    final String ip = p + i;
                    ex.execute(() -> {
                        if (found.get() == null && isConsole(ip, 600)) found.compareAndSet(null, ip);
                        int d = done.incrementAndGet();
                        if (d % 32 == 0) ui.post(() -> status.setText("Recherche de la console sur le Wi-Fi… " + Math.min(100, d * 100 / (254 * Math.max(1, pre.size()))) + " %"));
                    });
                }
                ex.shutdown();
                try { ex.awaitTermination(30, TimeUnit.SECONDS); } catch (InterruptedException e) { }
            }
            searching = false;
            String ip = found.get();
            ui.post(() -> {
                if (ip != null) { host = ip; prefs().edit().putString("host", ip).apply(); open(); }
                else showPanel(pre.isEmpty() ? "Pas de Wi-Fi : connecte le téléphone au même réseau que la console."
                                             : "Console introuvable. Allume-la, active la télécommande (onglet TÉLÉPHONE), ou tape son adresse (affichée dans cet onglet).", false);
            });
        }).start();
    }

    // ---------- ecran de connexion ----------
    private LinearLayout buildPanel() {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);
        p.setGravity(Gravity.CENTER);
        p.setPadding(dp(28), dp(28), dp(28), dp(28));
        p.setBackgroundColor(BG);
        TextView t = new TextView(this);
        t.setText("GC Station");
        t.setTextColor(Color.WHITE);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 28);
        t.setGravity(Gravity.CENTER);
        p.addView(t);
        TextView sub = new TextView(this);
        sub.setText("Télécommande");
        sub.setTextColor(Color.rgb(190, 160, 255));
        sub.setGravity(Gravity.CENTER);
        sub.setPadding(0, 0, 0, dp(24));
        p.addView(sub);
        spinner = new ProgressBar(this);
        p.addView(spinner);
        status = new TextView(this);
        status.setTextColor(Color.rgb(220, 215, 240));
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(16), 0, dp(20));
        p.addView(status);
        ipField = new EditText(this);
        ipField.setHint("Adresse de la console (ex. 192.168.1.20)");
        ipField.setTextColor(Color.WHITE);
        ipField.setHintTextColor(Color.rgb(140, 130, 170));
        ipField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        ipField.setSingleLine(true);
        p.addView(ipField, new LinearLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT));
        Button go = new Button(this);
        go.setText("Se connecter");
        go.setOnClickListener(v -> {
            String ip = ipField.getText().toString().trim().replaceFirst("^https?://", "").replaceAll("/.*$", "");
            if (ip.isEmpty()) return;
            host = ip;
            prefs().edit().putString("host", ip).apply();
            start();
        });
        p.addView(go, new LinearLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT));
        Button again = new Button(this);
        again.setText("Chercher à nouveau");
        again.setOnClickListener(v -> search());
        p.addView(again, new LinearLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT));
        return p;
    }

    private void showPanel(String msg, boolean busy) {
        web.setVisibility(View.GONE);
        panel.setVisibility(View.VISIBLE);
        status.setText(msg);
        spinner.setVisibility(busy ? View.VISIBLE : View.INVISIBLE);
        if (host != null && ipField.getText().length() == 0) ipField.setText(host);
    }

    private void menu() {
        String[] items = { "Recharger la page", "Chercher la console à nouveau", "Saisir l'adresse de la console" };
        new AlertDialog.Builder(this)
            .setTitle("GC Station" + (host != null ? " (" + host + ")" : ""))
            .setItems(items, (d, w) -> {
                if (w == 0) { if (host != null) start(); else search(); }
                else if (w == 1) search();
                else showPanel("Tape l'adresse affichée dans l'onglet TÉLÉPHONE de la console.", false);
            })
            .show();
    }

    @Override
    public void onBackPressed() {
        if (web.getVisibility() == View.VISIBLE && web.canGoBack()) web.goBack();
        else super.onBackPressed();
    }

    // ---------- envoi depuis la page (bouton "Choisir un fichier") ----------
    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_FILE || fileCb == null) return;
        Uri[] r = null;
        if (res == RESULT_OK && data != null) {
            ClipData cd = data.getClipData();
            if (cd != null && cd.getItemCount() > 0) {
                r = new Uri[cd.getItemCount()];
                for (int i = 0; i < r.length; i++) r[i] = cd.getItemAt(i).getUri();
            } else if (data.getData() != null) r = new Uri[] { data.getData() };
        }
        fileCb.onReceiveValue(r);
        fileCb = null;
    }

    // ---------- telechargements (Fichiers > toucher un fichier) ----------
    private void download(String url, String cd, String mime) {
        try {
            String name = URLUtil.guessFileName(url, cd, mime);
            DownloadManager.Request r = new DownloadManager.Request(Uri.parse(url));
            if (mime != null) r.setMimeType(mime);
            r.setTitle(name);
            r.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            r.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, name);
            ((DownloadManager) getSystemService(Context.DOWNLOAD_SERVICE)).enqueue(r);
            Toast.makeText(this, "Téléchargement : " + name, Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            Toast.makeText(this, "Téléchargement impossible", Toast.LENGTH_LONG).show();
        }
    }

    // ---------- "Partager -> GC Station" ----------
    private static boolean isShare(Intent i) {
        return i != null && (Intent.ACTION_SEND.equals(i.getAction()) || Intent.ACTION_SEND_MULTIPLE.equals(i.getAction()));
    }

    private void shareFiles(Intent i) {
        final List<Uri> uris = new ArrayList<>();
        if (Intent.ACTION_SEND.equals(i.getAction())) {
            Uri u = i.getParcelableExtra(Intent.EXTRA_STREAM);
            if (u != null) uris.add(u);
        } else {
            ArrayList<Uri> l = i.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (l != null) uris.addAll(l);
        }
        if (uris.isEmpty()) { Toast.makeText(this, "Rien à envoyer (seulement des fichiers)", Toast.LENGTH_LONG).show(); return; }
        final String h = host;
        Toast.makeText(this, "Envoi vers GC Station : " + uris.size() + " fichier(s)…", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            int ok = 0;
            for (Uri u : uris) if (upload(h, u)) ok++;
            final int n = ok;
            ui.post(() -> {
                Toast.makeText(this, n == uris.size() ? "Envoyé sur la console (" + n + ")" : "Envoyés : " + n + " sur " + uris.size(), Toast.LENGTH_LONG).show();
                if (web.getVisibility() == View.VISIBLE) web.reload();
            });
        }).start();
    }

    private boolean upload(String h, Uri u) {
        File tmp = null;
        HttpURLConnection c = null;
        try {
            String name = "fichier", mime = getContentResolver().getType(u);
            long size = -1;
            try (Cursor cur = getContentResolver().query(u, null, null, null, null)) {
                if (cur != null && cur.moveToFirst()) {
                    int ni = cur.getColumnIndex(OpenableColumns.DISPLAY_NAME), si = cur.getColumnIndex(OpenableColumns.SIZE);
                    if (ni >= 0 && cur.getString(ni) != null) name = cur.getString(ni);
                    if (si >= 0 && !cur.isNull(si)) size = cur.getLong(si);
                }
            }
            if (size <= 0) {                                       // (taille inconnue : copie d'abord, la console veut la longueur)
                tmp = File.createTempFile("envoi", null, getCacheDir());
                try (InputStream in = getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(tmp)) { copy(in, o); }
                size = tmp.length();
            }
            String low = name.toLowerCase();
            String dir = mime != null && mime.startsWith("audio/") ? "mp3"
                       : mime != null && mime.startsWith("image/") ? "photos"
                       : mime != null && mime.startsWith("video/") ? "videos"
                       : low.endsWith(".gci") ? "gci" : "downloads";   // (.dol : la console le range dans SD:/apps)
            c = (HttpURLConnection) new URL("http://" + h + "/upload?dir=" + dir + "&name=" + URLEncoder.encode(name, "UTF-8")).openConnection();
            c.setRequestMethod("PUT");
            c.setDoOutput(true);
            c.setConnectTimeout(5000);
            c.setReadTimeout(60000);
            c.setFixedLengthStreamingMode(size);
            try (InputStream in = tmp != null ? new FileInputStream(tmp) : getContentResolver().openInputStream(u); OutputStream o = c.getOutputStream()) { copy(in, o); }
            return c.getResponseCode() == 200;
        } catch (Exception e) {
            return false;
        } finally {
            if (c != null) c.disconnect();
            if (tmp != null) tmp.delete();
        }
    }

    private static void copy(InputStream in, OutputStream o) throws java.io.IOException {
        byte[] buf = new byte[32768];
        int n;
        while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
    }
}
