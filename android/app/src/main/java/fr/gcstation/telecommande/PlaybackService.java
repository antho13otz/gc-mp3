package fr.gcstation.telecommande;

// Lecture de la console hors de l'appli : notification avec pochette, titre, precedent / lecture / suivant (aussi sur
// l'ecran verrouille), boutons de volume du telephone -> volume de la console (lecteur "a distance" pour Android),
// widget d'ecran d'accueil tenu a jour, et verification des nouveaux builds de GC Station (toutes les 6 heures).
// En direct (Live) : l'etat n'est relu que quand la console previent (et toutes les 15 s) ; pendant un jeu, la
// notification montre la partie (chronometre) et previent 5 min avant la limite de jeu du jour, puis a la limite.

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.MediaMetadata;
import android.media.VolumeProvider;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

import org.json.JSONObject;

public class PlaybackService extends Service {
    static final String CH_PLAY = "lecture", CH_INFO = "infos";
    static final String ACT_TOGGLE = "toggle", ACT_NEXT = "next", ACT_PREV = "prev", ACT_STOP = "fermer";
    static final int ID_PLAY = 1, ID_BUILD = 2, ID_SEND = 3, ID_COPY = 4, ID_LIMIT = 7, ID_GAMEEND = 8;
    static volatile JSONObject last;                         // (dernier etat : le widget s'en sert)
    static volatile Bitmap lastCover;

    private final Handler h = new Handler(Looper.getMainLooper());
    private MediaSession session;
    private VolumeProvider volume;
    private static volatile PlaybackService me;

    /** bouton de volume appuye dans l'appli : meme chemin que la barre d'Android (garde la barre a jour) */
    static boolean adjust(int dir) {
        PlaybackService s = me;
        if (s == null || s.volume == null) return false;
        s.h.post(() -> s.volume.onAdjustVolume(dir));
        return true;
    }
    private String coverKey = "", shownKey = "";
    private boolean playing, running;
    private long lastBuildCheck;
    private volatile long volHold;                           // (juste apres un appui : la barre ne recule pas)

    static void channels(Context c) {
        if (Build.VERSION.SDK_INT < 26) return;
        NotificationManager nm = c.getSystemService(NotificationManager.class);
        NotificationChannel a = new NotificationChannel(CH_PLAY, "Lecture sur la console", NotificationManager.IMPORTANCE_LOW);
        a.setShowBadge(false);
        nm.createNotificationChannel(a);
        nm.createNotificationChannel(new NotificationChannel(CH_INFO, "Nouveautés et envois", NotificationManager.IMPORTANCE_DEFAULT));
    }

    static void start(Context c) {
        Intent i = new Intent(c, PlaybackService.class);
        try { if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i); } catch (Exception e) { }
    }

    @Override public IBinder onBind(Intent i) { return null; }

    private final Live.Listener live = new Live.Listener() {
        @Override public void changed() { later(); }
        @Override public void game(Live.Game g) { gameAlarms(g); shownKey = ""; getSystemService(NotificationManager.class).notify(ID_PLAY, build(last)); }
    };

    /** limite de jeu du jour : un mot 5 min avant, puis a la limite (la console ne coupe pas le jeu) */
    private final Runnable limitSoon = () -> PlaybackService.notify(this, ID_LIMIT, "Plus que 5 minutes de jeu", "Limite de jeu du jour bientôt atteinte" + gameWho() + ".");
    private final Runnable limitNow = () -> PlaybackService.notify(this, ID_LIMIT, "Temps de jeu du jour écoulé", "C'est l'heure d'arrêter" + gameWho() + " : A + Z + START pour revenir à GC Station.");
    private static String gameWho() { Live.Game g = Live.game; return g == null ? "" : " (" + g.name + (g.profile.isEmpty() ? "" : ", profil " + g.profile) + ")"; }
    private void gameAlarms(Live.Game g) {
        h.removeCallbacks(limitSoon);
        h.removeCallbacks(limitNow);
        if (g == null) return;
        long left = g.leftSec();
        if (left == Long.MIN_VALUE) return;
        if (left > 300) h.postDelayed(limitSoon, (left - 300) * 1000);
        if (left > 0) h.postDelayed(limitNow, left * 1000);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        Live.add(this, live);
        gameAlarms(Live.game);
        channels(this);
        session = new MediaSession(this, "GC Station");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { Api.cmd("toggle", -1); later(); }
            @Override public void onPause() { Api.cmd("toggle", -1); later(); }
            @Override public void onSkipToNext() { Api.cmd("next", -1); later(); }
            @Override public void onSkipToPrevious() { Api.cmd("prev", -1); later(); }
            @Override public void onStop() { Api.cmd("stop", -1); later(); }
        });
        // boutons de volume -> la console. La console change le volume par paliers de 10 % (0 a 100 %) : la barre
        // d'Android a 11 crans (0 a 10), reglee sur le vrai volume de la console (mis a jour avec l'etat).
        volume = new VolumeProvider(VolumeProvider.VOLUME_CONTROL_ABSOLUTE, 10, 10) {
            @Override public void onAdjustVolume(int dir) {
                if (dir == 0) return;
                int v = Math.max(0, Math.min(10, getCurrentVolume() + (dir > 0 ? 1 : -1)));
                if (v == getCurrentVolume()) return;
                setCurrentVolume(v);                              // (la barre bouge tout de suite)
                volHold = System.currentTimeMillis();
                Api.vol(dir > 0);
            }
            @Override public void onSetVolumeTo(int v) {         // (barre glissee : autant de crans que necessaire)
                v = Math.max(0, Math.min(10, v));
                int d = v - getCurrentVolume();
                setCurrentVolume(v);
                volHold = System.currentTimeMillis();
                for (int k = 0; k < Math.abs(d); k++) Api.vol(d > 0);
            }
        };
        session.setPlaybackToRemote(volume);
        me = this;
        session.setSessionActivity(PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE));
        session.setActive(true);
    }

    @Override
    public int onStartCommand(Intent i, int flags, int id) {
        String a = i != null ? i.getAction() : null;
        if (ACT_STOP.equals(a)) { stopSelf(); return START_NOT_STICKY; }
        if (ACT_TOGGLE.equals(a)) Api.cmd("toggle", -1);
        else if (ACT_NEXT.equals(a)) Api.cmd("next", -1);
        else if (ACT_PREV.equals(a)) Api.cmd("prev", -1);
        if (Api.host == null) Api.host = getSharedPreferences("gcs", MODE_PRIVATE).getString("host", null);
        Notification n = build(last);
        if (Build.VERSION.SDK_INT >= 29) startForeground(ID_PLAY, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else startForeground(ID_PLAY, n);
        if (!running) { running = true; h.post(poll); }
        else if (a != null) later();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        me = null;
        Live.remove(live);
        h.removeCallbacksAndMessages(null);
        session.setActive(false);
        session.release();
        super.onDestroy();
    }

    private void later() { h.removeCallbacks(poll); h.postDelayed(poll, 350); }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!running) return;
            if (Api.host == null) { h.postDelayed(this, 3000); return; }
            Api.get("/api/state", r -> {
                if (!running) return;
                if (r != null) {
                    Live.Game g = Live.consoleBack();             // (la console repond : la partie est finie)
                    if (g != null) PlaybackService.notify(PlaybackService.this, ID_GAMEEND, "Partie terminée : " + g.name, "Temps de jeu : " + Live.dur(g.playedSec()) + ".");
                    try { update(new JSONObject(r)); } catch (Exception e) { }
                }
                h.removeCallbacks(poll);
                h.postDelayed(poll, r != null ? (Live.pushing() ? 15000 : 2000) : Live.game != null ? 10000 : 5000);
            });
            if (System.currentTimeMillis() - lastBuildCheck > 6 * 3600 * 1000L) { lastBuildCheck = System.currentTimeMillis(); BuildCheck.run(PlaybackService.this); AppUpdate.check(PlaybackService.this); }
        }
    };

    private void update(JSONObject s) {
        last = s;
        LastLaunch.check(this, s);                          // (notifications : jeu plante / fige, telechargement fini)
        int cv = Math.max(0, Math.min(10, (s.optInt("vol", 100) + 5) / 10));
        if (System.currentTimeMillis() - volHold > 1500 && volume.getCurrentVolume() != cv) volume.setCurrentVolume(cv);
        playing = s.optInt("playing", 0) != 0;
        String title = s.optString("title", ""), sub = s.optString("sub", ""), info = s.optString("info", "");
        String key = s.optInt("cov", 0) != 0 ? title + "|" + sub : "";
        if (!key.equals(coverKey)) {
            coverKey = key;
            if (key.isEmpty()) { lastCover = null; publish(s, title, sub, info); }
            else {
                final String k = key;
                Api.bytes("/cover.bmp?k=" + Api.enc(k), b -> {
                    if (!k.equals(coverKey)) return;
                    lastCover = b == null ? null : BitmapFactory.decodeByteArray(b, 0, b.length);
                    shownKey = "";
                    publish(last, title, sub, info);
                });
            }
        }
        publish(s, title, sub, info);
    }

    private void publish(JSONObject s, String title, String sub, String info) {
        String k = title + "|" + sub + "|" + info + "|" + playing + "|" + (lastCover != null) + "|" + s.optInt("dur", 0);
        int el = s.optInt("el", 0), dur = s.optInt("dur", 0);
        PlaybackState.Builder ps = new PlaybackState.Builder()
            .setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE | PlaybackState.ACTION_SKIP_TO_NEXT
                        | PlaybackState.ACTION_SKIP_TO_PREVIOUS | PlaybackState.ACTION_STOP)
            .setState(playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED, el * 1000L, playing ? 1f : 0f);
        session.setPlaybackState(ps.build());
        if (Live.game != null) k = "jeu";
        if (k.equals(shownKey)) return;                      // (notification et widget : seulement si quelque chose change)
        shownKey = k;
        MediaMetadata.Builder md = new MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE, title.isEmpty() ? "GC Station" : title)
            .putString(MediaMetadata.METADATA_KEY_ARTIST, sub)
            .putString(MediaMetadata.METADATA_KEY_ALBUM, info);
        if (dur > 0) md.putLong(MediaMetadata.METADATA_KEY_DURATION, dur * 1000L);
        if (lastCover != null) md.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, lastCover);
        session.setMetadata(md.build());
        getSystemService(NotificationManager.class).notify(ID_PLAY, build(s));
        GcWidget.refresh(this);
    }

    private PendingIntent act(String a, int code) {
        return PendingIntent.getService(this, code, new Intent(this, PlaybackService.class).setAction(a), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }

    @SuppressWarnings("deprecation")
    private Notification build(JSONObject s) {
        Live.Game g = Live.game;
        if (g != null && System.currentTimeMillis() - g.start < 12 * 3600 * 1000L) {   // (partie en cours : chronometre)
            Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CH_PLAY) : new Notification.Builder(this);
            long left = g.leftSec();
            String txt = (g.profile.isEmpty() ? "" : "Profil " + g.profile + " · ")
                       + (left == Long.MIN_VALUE ? "A + Z + START pour revenir à GC Station" : left > 0 ? "limite du jour à " + new java.text.SimpleDateFormat("HH:mm", java.util.Locale.FRANCE).format(new java.util.Date(g.start + g.leftMin * 60000L)) : "temps du jour écoulé");
            b.setSmallIcon(R.drawable.ic_stat).setContentTitle("En jeu : " + g.name).setContentText(txt).setColor(Neon.PINK)
             .setOngoing(true).setShowWhen(true).setWhen(g.start).setUsesChronometer(true).setVisibility(Notification.VISIBILITY_PUBLIC)
             .setContentIntent(PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE))
             .addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(this, R.drawable.ic_close), "Fermer", act(ACT_STOP, 4)).build());
            return b.build();
        }
        String title = s != null ? s.optString("title", "") : "", sub = s != null ? s.optString("sub", "") : "";
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CH_PLAY) : new Notification.Builder(this);
        b.setSmallIcon(R.drawable.ic_stat)
         .setContentTitle(title.isEmpty() ? "GC Station" : title)
         .setContentText(sub.isEmpty() ? (Api.host != null ? "Console " + Api.host : "Télécommande") : sub)
         .setLargeIcon(lastCover)
         .setColor(Neon.PINK)
         .setOngoing(playing)
         .setShowWhen(false)
         .setVisibility(Notification.VISIBILITY_PUBLIC)
         .setContentIntent(PendingIntent.getActivity(this, 0, new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE))
         .addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(this, R.drawable.ic_prev), "Précédent", act(ACT_PREV, 1)).build())
         .addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(this, playing ? R.drawable.ic_pause : R.drawable.ic_play), playing ? "Pause" : "Lecture", act(ACT_TOGGLE, 2)).build())
         .addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(this, R.drawable.ic_next), "Suivant", act(ACT_NEXT, 3)).build())
         .addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(this, R.drawable.ic_close), "Fermer", act(ACT_STOP, 4)).build())
         .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken()).setShowActionsInCompactView(0, 1, 2));
        return b.build();
    }

    // ---------- notifications d'information ----------
    static void notify(Context c, int id, String title, String text) {
        channels(c);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(c, CH_INFO) : new Notification.Builder(c);
        b.setSmallIcon(R.drawable.ic_stat).setContentTitle(title).setContentText(text).setColor(Neon.PINK).setAutoCancel(true)
         .setStyle(new Notification.BigTextStyle().bigText(text))
         .setContentIntent(PendingIntent.getActivity(c, id, new Intent(c, MainActivity.class), PendingIntent.FLAG_IMMUTABLE));
        try { c.getSystemService(NotificationManager.class).notify(id, b.build()); } catch (Exception e) { }
    }
}

/** nouveau build de GC Station publie sur GitHub (release "dernier") plus recent que celui de la console ? */
final class BuildCheck {
    static void run(final Context c) {
        new Thread(() -> {
            try {
                String rel = HttpGet.text("https://github.com/antho13otz/gc-mp3/releases/download/dernier/release.json", 8000);
                String mem = Api.get("/api/memoire", 3000);
                if (rel == null || mem == null) return;
                int pub = 0;
                java.util.regex.Matcher m = java.util.regex.Pattern.compile("build=(\\d+)").matcher(new JSONObject(rel).optString("body", ""));
                if (m.find()) pub = Integer.parseInt(m.group(1));
                int have = new JSONObject(mem).optInt("build", 0);
                SharedPreferences p = c.getSharedPreferences("gcs", Context.MODE_PRIVATE);
                if (pub > 0 && have > 0 && pub > have && p.getInt("build_vu", 0) < pub) {
                    p.edit().putInt("build_vu", pub).apply();
                    PlaybackService.notify(c, PlaybackService.ID_BUILD, "Nouveau GC Station : build " + pub,
                        "Ta console a le build " + have + ". Sur la console : START > Mise à jour.");
                }
            } catch (Exception e) { }
        }).start();
    }
}

/** lecture d'une page https (GitHub, redirections suivies) */
final class HttpGet {
    static String text(String url, int ms) {
        java.net.HttpURLConnection c = null;
        try {
            for (int hop = 0; hop < 5; hop++) {
                c = (java.net.HttpURLConnection) new java.net.URL(url).openConnection();
                c.setConnectTimeout(ms);
                c.setReadTimeout(ms);
                c.setInstanceFollowRedirects(true);
                int code = c.getResponseCode();
                if (code / 100 == 3 && c.getHeaderField("Location") != null) { url = c.getHeaderField("Location"); c.disconnect(); continue; }
                if (code != 200) return null;
                java.io.InputStream in = c.getInputStream();
                java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
                return o.toString("UTF-8");
            }
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
        return null;
    }
}
