package fr.gcstation.telecommande;

// Widget d'ecran d'accueil : pochette, titre, artiste, precedent / lecture / suivant. Tenu a jour par PlaybackService
// (etat lu toutes les 2 secondes). Un bouton touche demarre aussi le service s'il ne tournait plus.

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.widget.RemoteViews;

import org.json.JSONObject;

public class GcWidget extends AppWidgetProvider {
    @Override
    public void onUpdate(Context c, AppWidgetManager m, int[] ids) {
        refresh(c);
        PlaybackService.start(c);                            // (pour que le widget se mette a jour tout seul)
    }

    private static PendingIntent act(Context c, String a, int code) {
        Intent i = new Intent(c, PlaybackService.class).setAction(a);
        int f = PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT;
        return Build.VERSION.SDK_INT >= 26 ? PendingIntent.getForegroundService(c, 100 + code, i, f) : PendingIntent.getService(c, 100 + code, i, f);
    }

    static void refresh(Context c) {
        AppWidgetManager m = AppWidgetManager.getInstance(c);
        int[] ids = m.getAppWidgetIds(new ComponentName(c, GcWidget.class));
        if (ids == null || ids.length == 0) return;
        RemoteViews v = new RemoteViews(c.getPackageName(), R.layout.widget);
        JSONObject s = PlaybackService.last;
        if (s != null) {
            String t = s.optString("title", "");
            v.setTextViewText(R.id.w_title, t.isEmpty() ? "Rien en lecture" : t);
            v.setTextViewText(R.id.w_sub, s.optString("sub", ""));
            v.setImageViewResource(R.id.w_play, s.optInt("playing", 0) != 0 ? R.drawable.ic_pause : R.drawable.ic_play);
        }
        // build 185 : le jeu en cours (Pulse), avec le temps de la partie
        Live.Game g = Live.game;
        if (g != null && !g.name.isEmpty()) {
            long mn = g.playedSec() / 60;
            v.setTextViewText(R.id.w_game, "\uD83C\uDFAE " + g.name + " \u00B7 " + (mn >= 60 ? (mn / 60) + " h " + String.format(java.util.Locale.FRANCE, "%02d", mn % 60) : mn + " min"));
            v.setViewVisibility(R.id.w_game, android.view.View.VISIBLE);
        } else v.setViewVisibility(R.id.w_game, android.view.View.GONE);
        if (PlaybackService.lastCover != null) v.setImageViewBitmap(R.id.w_cover, PlaybackService.lastCover);
        else v.setImageViewResource(R.id.w_cover, R.mipmap.ic_launcher);
        v.setOnClickPendingIntent(R.id.w_prev, act(c, PlaybackService.ACT_PREV, 1));
        v.setOnClickPendingIntent(R.id.w_play, act(c, PlaybackService.ACT_TOGGLE, 2));
        v.setOnClickPendingIntent(R.id.w_next, act(c, PlaybackService.ACT_NEXT, 3));
        PendingIntent open = PendingIntent.getActivity(c, 0, Host.home(c), PendingIntent.FLAG_IMMUTABLE);
        v.setOnClickPendingIntent(R.id.w_cover, open);
        v.setOnClickPendingIntent(R.id.w_title, open);
        v.setOnClickPendingIntent(R.id.w_sub, open);
        v.setOnClickPendingIntent(R.id.w_game, open);
        m.updateAppWidget(ids, v);
    }
}
