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

    interface Sent { void on(long done); }                    // (progression d'un envoi, sur le fil de l'envoi)
    static volatile int lastCode;                              // (code HTTP du dernier envoi : 503 = un autre envoi en cours)

    /** envoi d'un flux vers la carte SD de la console (PUT /upload) ; rend la reponse ou null */
    static String put(String dir, String name, InputStream in, long size) { return put(dir, name, in, size, null); }
    static String put(String dir, String name, InputStream in, long size, Sent prog) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("http://" + host + "/upload?dir=" + dir + "&name=" + enc(name)).openConnection();
            c.setRequestMethod("PUT");
            c.setDoOutput(true);
            c.setConnectTimeout(5000);
            c.setReadTimeout(60000);
            c.setFixedLengthStreamingMode(size);
            lastCode = 0;
            OutputStream o = c.getOutputStream();
            byte[] buf = new byte[32768];
            int n;
            long done = 0;
            while ((n = in.read(buf)) > 0) { o.write(buf, 0, n); done += n; if (prog != null) prog.on(done); }
            o.close();
            lastCode = c.getResponseCode();
            if (lastCode != 200) return null;
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

    // ---------- v272 : envoi par morceaux de 256 Ko (console build 757 et plus) ----------
    // Chaque morceau est un PUT court ; un morceau qui echoue est renvoye seul, apres avoir demande a la console ce
    // qu'elle a deja (GET /api/upstat?id=). Avant : tout le fichier d'un coup, et a la moindre coupure, tout etait perdu.
    static final int CHUNK = 256 * 1024;
    /** la console sait-elle recevoir par morceaux ? */
    static boolean canChunk() { return get("/api/upstat?id=0", 3000) != null; }

    private static int readFully(InputStream in, byte[] b, int n) throws java.io.IOException {
        int got = 0;
        while (got < n) { int r = in.read(b, got, n - got); if (r <= 0) break; got += r; }
        return got;
    }

    private static String putOne(String dir, String name, String id, long off, long total, byte[] b, int n, Sent prog) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("http://" + host + "/upload?dir=" + dir + "&name=" + enc(name) + "&id=" + id + "&off=" + off + "&total=" + total).openConnection();
            c.setRequestMethod("PUT");
            c.setDoOutput(true);
            c.setConnectTimeout(5000);
            c.setReadTimeout(20000);
            c.setFixedLengthStreamingMode(n);
            OutputStream o = c.getOutputStream();
            for (int w = 0; w < n; w += 16384) { int k = Math.min(16384, n - w); o.write(b, w, k); if (prog != null) prog.on(off + w + k); }
            o.close();
            lastCode = c.getResponseCode();
            if (lastCode != 200) return null;
            InputStream r = c.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[1024]; int k;
            while ((k = r.read(buf)) > 0) bo.write(buf, 0, k);
            return bo.toString("UTF-8");
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    /** envoi par morceaux ; rend la reponse du dernier morceau (comme put), ou null */
    static String putChunks(String dir, String name, InputStream in, long size, Sent prog) {
        try {
            String id = Long.toHexString(System.nanoTime() ^ (long) (Math.random() * Long.MAX_VALUE));
            byte[] b = new byte[CHUNK];
            long off = 0;
            String last = null;
            while (off < size) {
                int n = readFully(in, b, (int) Math.min(CHUNK, size - off));
                if (n <= 0) return null;
                String r = null;
                for (int t = 0; t < 6 && r == null; t++) {
                    if (t > 0) {                                      // (que s'est-il passe ? la console le dit)
                        try { Thread.sleep(800L * t); } catch (InterruptedException e) { return null; }
                        String st = get("/api/upstat?id=" + id, 4000);
                        long have = -2;
                        try { if (st != null) have = Long.parseLong(st.trim()); } catch (Exception e) { }
                        if (have == off + n && off + n < size) { r = "suite"; break; }   // (arrive, mais la reponse s'est perdue)
                        if (have >= 0 && have != off) return null;    // (la console a autre chose : on recommence tout le fichier)
                    }
                    r = putOne(dir, name, id, off, size, b, n, prog);
                }
                if (r == null) return null;
                off += n;
                last = r;
            }
            return last;
        } catch (Exception e) {
            return null;
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
