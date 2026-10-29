package fr.gcstation.telecommande;

// La console en direct (GC Station v266 et plus, port UDP 51236) :
//  - decouverte : "GCSQ1" envoye a tout le reseau, la console repond "GCSA1\tnom\tport\tbuild" (moins d'une seconde,
//    au lieu d'essayer les 254 adresses) ;
//  - abonnement : "GCSS1" toutes les 10 s ; la console repond "GCSK1" puis previent "GCSN1" a chaque changement
//    (morceau, pause, volume, paroles, position) : l'etat est relu tout de suite, plus besoin de le demander chaque seconde ;
//  - partie en cours : "GCSG1\tid\tnom\tminutes restantes (-1 : pas de limite)\tprofil" juste avant un jeu (la console
//    ne repond plus pendant le jeu), "GCSE1" si le lancement a echoue. La partie est gardee (prefs) jusqu'au retour
//    de la console.

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;

final class Live {
    static final int PORT = 51236;

    interface Listener {
        void changed();                                      // (etat a relire)
        void game(Game g);                                   // (un jeu se lance ; null : partie finie ou lancement rate)
    }

    static final class Game {
        String id = "", name = "", profile = "";
        int leftMin = -1;                                    // (minutes de jeu restantes aujourd'hui au lancement, -1 : pas de limite)
        long start;                                          // (ms, horloge du telephone)
        long playedSec() { return Math.max(0, (System.currentTimeMillis() - start) / 1000); }
        /** secondes restantes avant la limite du jour, ou Long.MIN_VALUE sans limite */
        long leftSec() { return leftMin < 0 ? Long.MIN_VALUE : leftMin * 60L - playedSec(); }
    }

    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static final List<Listener> listeners = new CopyOnWriteArrayList<>();
    private static DatagramSocket sock;
    private static Thread th;
    private static volatile long lastAck;
    static volatile Game game;
    private static SharedPreferences prefs;

    private Live() { }

    /** la console previent-elle des changements (abonnement confirme recemment) ? */
    static boolean pushing() { return System.currentTimeMillis() - lastAck < 25000; }

    static synchronized void add(Context c, Listener l) {
        if (prefs == null) { prefs = c.getApplicationContext().getSharedPreferences("gcs", Context.MODE_PRIVATE); loadGame(); }
        if (!listeners.contains(l)) listeners.add(l);
        if (th == null) {
            th = new Thread(Live::loop, "gcs-direct");
            th.setDaemon(true);
            th.start();
        }
    }

    static synchronized void remove(Listener l) {
        listeners.remove(l);
        if (listeners.isEmpty() && th != null) { th.interrupt(); th = null; if (sock != null) sock.close(); sock = null; lastAck = 0; }
    }

    /** relire l'abonnement tout de suite (nouvelle adresse de la console) */
    static void resubscribe() { lastAck = 0; nextSub = 0; }

    private static volatile long nextSub;

    private static void loop() {
        DatagramSocket s = null;
        try {
            s = new DatagramSocket();
            s.setSoTimeout(1000);
            synchronized (Live.class) { sock = s; }
            byte[] buf = new byte[512];
            String subHost = null;
            while (!Thread.currentThread().isInterrupted()) {
                String h = Api.host;
                long now = System.currentTimeMillis();
                if (h != null && (now >= nextSub || !h.equals(subHost))) {
                    subHost = h;
                    nextSub = now + 10000;
                    try {
                        byte[] m = "GCSS1".getBytes(StandardCharsets.US_ASCII);
                        s.send(new DatagramPacket(m, m.length, InetAddress.getByName(hostOnly(h)), PORT));
                    } catch (Exception e) { }
                }
                DatagramPacket p = new DatagramPacket(buf, buf.length);
                try { s.receive(p); } catch (java.net.SocketTimeoutException e) { continue; }
                if (h == null || !p.getAddress().getHostAddress().equals(hostOnly(h))) continue;
                String t = new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8).trim();
                String[] f = t.split("\t", -1);
                switch (f[0]) {
                    case "GCSK1": lastAck = System.currentTimeMillis(); break;
                    case "GCSN1": lastAck = System.currentTimeMillis(); ui.post(() -> { for (Listener l : listeners) l.changed(); }); break;
                    case "GCSG1": {
                        if (game != null && f.length > 1 && f[1].equals(game.id) && System.currentTimeMillis() - game.start < 10000) break;   // (envoye deux fois)
                        Game g = new Game();
                        g.id = f.length > 1 ? f[1] : "";
                        g.name = f.length > 2 ? f[2] : g.id;
                        try { g.leftMin = f.length > 3 ? Integer.parseInt(f[3]) : -1; } catch (Exception e) { g.leftMin = -1; }
                        g.profile = f.length > 4 ? f[4] : "";
                        g.start = System.currentTimeMillis();
                        setGame(g);
                        break;
                    }
                    case "GCSE1": if (game != null) setGame(null); break;
                    default: break;
                }
            }
        } catch (Exception e) {
        } finally {
            if (s != null) s.close();
        }
    }

    private static String hostOnly(String h) { int i = h.indexOf(':'); return i >= 0 ? h.substring(0, i) : h; }

    private static void setGame(final Game g) {
        game = g;
        if (prefs != null) {
            if (g == null) prefs.edit().remove("jeu_id").apply();
            else prefs.edit().putString("jeu_id", g.id).putString("jeu_nom", g.name).putString("jeu_prof", g.profile)
                          .putInt("jeu_lim", g.leftMin).putLong("jeu_t", g.start).apply();
        }
        ui.post(() -> { for (Listener l : listeners) l.game(g); });
    }

    private static void loadGame() {
        String id = prefs.getString("jeu_id", null);
        if (id == null) return;
        Game g = new Game();
        g.id = id;
        g.name = prefs.getString("jeu_nom", id);
        g.profile = prefs.getString("jeu_prof", "");
        g.leftMin = prefs.getInt("jeu_lim", -1);
        g.start = prefs.getLong("jeu_t", 0);
        if (System.currentTimeMillis() - g.start > 12 * 3600 * 1000L) { prefs.edit().remove("jeu_id").apply(); return; }   // (trop vieux)
        game = g;
    }

    /** la console a repondu : si une partie etait en cours (et pas juste lancee), elle est finie. Rend la partie finie, ou null */
    static Game consoleBack() {
        Game g = game;
        if (g == null || System.currentTimeMillis() - g.start < 45000) return null;   // (la console repond encore un peu avant le jeu)
        setGame(null);
        return g;
    }

    static String dur(long sec) {
        if (sec < 60) return sec + " s";
        if (sec < 3600) return (sec / 60) + " min";
        return (sec / 3600) + " h " + String.format(java.util.Locale.FRANCE, "%02d", sec % 3600 / 60);
    }

    // ---------- decouverte ----------
    /** consoles qui repondent en ms millisecondes : { adresse, nom } */
    static List<String[]> discover(int ms) {
        List<String[]> r = new ArrayList<>();
        DatagramSocket s = null;
        try {
            s = new DatagramSocket();
            s.setBroadcast(true);
            s.setSoTimeout(200);
            byte[] q = "GCSQ1".getBytes(StandardCharsets.US_ASCII);
            List<InetAddress> to = new ArrayList<>();
            to.add(InetAddress.getByName("255.255.255.255"));
            try {
                for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                    if (!ni.isUp() || ni.isLoopback()) continue;
                    for (InterfaceAddress ia : ni.getInterfaceAddresses()) if (ia.getBroadcast() != null) to.add(ia.getBroadcast());
                }
            } catch (Exception e) { }
            long end = System.currentTimeMillis() + ms, again = 0;
            byte[] buf = new byte[512];
            while (System.currentTimeMillis() < end) {
                if (System.currentTimeMillis() >= again) {         // (UDP : redemande toutes les 400 ms)
                    again = System.currentTimeMillis() + 400;
                    for (InetAddress a : to) try { s.send(new DatagramPacket(q, q.length, a, PORT)); } catch (Exception e) { }
                }
                DatagramPacket p = new DatagramPacket(buf, buf.length);
                try { s.receive(p); } catch (java.net.SocketTimeoutException e) { continue; }
                String t = new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8).trim();
                String[] f = t.split("\t", -1);
                if (!f[0].equals("GCSA1")) continue;
                String ip = p.getAddress().getHostAddress();
                int port = 80;
                try { port = Integer.parseInt(f.length > 2 ? f[2] : "80"); } catch (Exception e) { }
                String addr = port == 80 ? ip : ip + ":" + port;
                boolean have = false;
                for (String[] x : r) if (x[0].equals(addr)) have = true;
                if (!have) r.add(new String[] { addr, f.length > 1 && !f[1].isEmpty() ? f[1] : ip });
                if (r.size() == 1) end = Math.min(end, System.currentTimeMillis() + 300);   // (une autre console ? un instant)
            }
        } catch (Exception e) {
        } finally {
            if (s != null) s.close();
        }
        return r;
    }
}
