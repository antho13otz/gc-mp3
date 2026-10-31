package fr.gcstation.telecommande;

// Tuile "GC Station" des reglages rapides (volet du haut d'Android) : toucher = lecture / pause sur la console ;
// le titre du morceau s'affiche sous la tuile. Appui long : ouvre l'appli.

import android.content.Context;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

import org.json.JSONObject;

public class GcTile extends TileService {
    @Override public void onStartListening() { refresh(); fetch(); }

    @Override public void onClick() {
        host();
        if (Api.host == null) { refresh(); return; }
        Api.pool(() -> {
            Api.get("/api/cmd?c=toggle", 2500);
            try { Thread.sleep(300); } catch (InterruptedException e) { }
            fetch();
        });
    }

    private void host() { if (Api.host == null) Api.host = getSharedPreferences("gcs", Context.MODE_PRIVATE).getString("host", null); }

    private void fetch() {
        host();
        if (Api.host == null) return;
        Api.pool(() -> {
            String r = Api.get("/api/state", 2000);
            try { if (r != null) PlaybackService.last = new JSONObject(r); } catch (Exception e) { }
            new android.os.Handler(android.os.Looper.getMainLooper()).post(this::refresh);
        });
    }

    private void refresh() {
        Tile t = getQsTile();
        if (t == null) return;
        JSONObject s = PlaybackService.last;
        boolean playing = s != null && s.optInt("playing", 0) != 0;
        t.setState(Api.host == null ? Tile.STATE_UNAVAILABLE : playing ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        t.setIcon(Icon.createWithResource(this, playing ? R.drawable.ic_pause : R.drawable.ic_tile));
        t.setLabel("GC Station");
        if (Build.VERSION.SDK_INT >= 29) {
            String title = s != null ? s.optString("title", "") : "";
            t.setSubtitle(Lang.t(title.isEmpty() ? (Api.host == null ? "Ouvre l'appli une fois" : "Rien en lecture") : title));
        }
        t.updateTile();
    }
}
