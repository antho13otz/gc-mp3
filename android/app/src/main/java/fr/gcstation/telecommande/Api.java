package fr.gcstation.telecommande;

// Les commandes reseau de GC Station (serveur du telephone de la console, port 80) :
//   /api/state (etat), /api/tracks, /api/stations, /api/cmd?c=toggle|next|prev|stop|volup|voldown|play|radio&i=,
//   /api/key?k=a|b|x|y|z|l|r|start|up|down|left|right, /api/mouse?dx=&dy=&w=&c=, /api/type?t=&go=,
//   /api/files?h=&i=, /api/get?h=&i=, /api/fsop?op=mkdir|del|ren|mv&h=&i=&x=&n=&t=, /upload?dir=&name= (PUT),
//   /cover.bmp (pochette en cours).

import android.os.Handler;
import android.os.Looper;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class Api {
    interface Done<T> { void on(T result); }                 // (appele sur le fil de l'interface ; null = echec)

    static volatile String host;
    private static final ExecutorService pool = Executors.newFixedThreadPool(4);
    private static final ExecutorService keys = Executors.newSingleThreadExecutor();   // (touches dans l'ordre)
    private static final Handler ui = new Handler(Looper.getMainLooper());

    private Api() { }

    static String enc(String s) {
        try { return URLEncoder.encode(s, "UTF-8"); } catch (Exception e) { return s; }
    }

    static byte[] getBytes(String path, int timeoutMs) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("http://" + host + path).openConnection();
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs * 3);
            if (c.getResponseCode() != 200) return null;
            InputStream in = c.getInputStream();
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            return o.toByteArray();
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    static String get(String path, int timeoutMs) {
        byte[] b = getBytes(path, timeoutMs);
        try { return b == null ? null : new String(b, "UTF-8"); } catch (Exception e) { return null; }
    }

    static void get(final String path, final Done<String> done) {
        pool.execute(() -> { final String r = get(path, 2500); if (done != null) ui.post(() -> done.on(r)); });
    }

    static void bytes(final String path, final Done<byte[]> done) {
        pool.execute(() -> { final byte[] r = getBytes(path, 3000); ui.post(() -> done.on(r)); });
    }

    static void pool(Runnable r) { pool.execute(r); }

    static void cmd(String c, int i) { get("/api/cmd?c=" + c + (i >= 0 ? "&i=" + i : ""), (Done<String>) null); }
    /** un cran de volume (+/- 10 % sur la console) ; envoyes dans l'ordre, l'un apres l'autre */
    static void vol(final boolean up) { keys.execute(() -> get("/api/cmd?c=" + (up ? "volup" : "voldown"), 1500)); }
    static void key(final String k) { keys.execute(() -> get("/api/key?k=" + k, 1500)); }
    static void mouse(final int dx, final int dy, final int w, final int click) {
        keys.execute(() -> get("/api/mouse?dx=" + dx + "&dy=" + dy + "&w=" + w + "&c=" + click, 1500));
    }

    /** envoi d'un flux vers la carte SD de la console (PUT /upload) ; rend la reponse ou null */
    static String put(String dir, String name, InputStream in, long size) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("http://" + host + "/upload?dir=" + dir + "&name=" + enc(name)).openConnection();
            c.setRequestMethod("PUT");
            c.setDoOutput(true);
            c.setConnectTimeout(5000);
            c.setReadTimeout(60000);
            c.setFixedLengthStreamingMode(size);
            OutputStream o = c.getOutputStream();
            byte[] buf = new byte[32768];
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            o.close();
            if (c.getResponseCode() != 200) return null;
            InputStream r = c.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            while ((n = r.read(buf)) > 0) bo.write(buf, 0, n);
            return bo.toString("UTF-8");
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    /** la console repond-elle a cette adresse ? */
    static boolean isConsole(String ip, int ms) {
        String old = host;
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
            return s.startsWith("{\"title\":") && s.contains("\"vol\"");
        } catch (Exception e) {
            return false;
        } finally {
            if (c != null) c.disconnect();
            host = old;
        }
    }
}
