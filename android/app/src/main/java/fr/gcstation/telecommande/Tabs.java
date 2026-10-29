package fr.gcstation.telecommande;

// Les ecrans de l'appli : Lecture, Musique, Radio, Manette, Fichiers. Chacun appelle directement les commandes de
// la console (Api) et se met a jour avec l'etat recu toutes les secondes (/api/state).

import android.app.Activity;
import android.app.AlertDialog;
import android.app.DownloadManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

abstract class Tab {
    final Activity a;
    View root;
    Tab(Activity a) { this.a = a; }
    abstract View build();
    void show() { }
    void state(JSONObject s) { }
    int dp(float v) { return Neon.dp(a, v); }
    static String clock(int s) { if (s < 0) s = 0; return (s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60); }
    LinearLayout.LayoutParams lp(int w, int h, float weight) { return new LinearLayout.LayoutParams(w, h, weight); }
}

// ---------------------------------------------------------------- Lecture
class PlayerTab extends Tab {
    private ImageView cover;
    private TextView title, sub, info, lyr, el, dur, vol, play;
    private View fill;
    private FrameLayout bar;
    private String coverKey = "";
    PlayerTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = new LinearLayout(a);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setGravity(Gravity.CENTER_HORIZONTAL);
        v.setPadding(dp(24), dp(8), dp(24), dp(16));
        FrameLayout frame = new FrameLayout(a);                // pochette dans un cadre neon
        GradientDrawable ring = Neon.neonFill(a, 26);
        frame.setBackground(ring);
        frame.setPadding(dp(3), dp(3), dp(3), dp(3));
        frame.setElevation(dp(12));
        cover = new ImageView(a);
        cover.setScaleType(ImageView.ScaleType.CENTER_CROP);
        GradientDrawable cbg = new GradientDrawable();
        cbg.setColor(0xFF140A26);
        cbg.setCornerRadius(dp(23));
        cover.setBackground(cbg);
        cover.setClipToOutline(true);
        cover.setImageResource(R.mipmap.ic_launcher);
        frame.addView(cover, new FrameLayout.LayoutParams(dp(230), dp(230)));
        LinearLayout.LayoutParams flp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        flp.topMargin = dp(10);
        v.addView(frame, flp);
        title = Neon.glow(a, "…", 22, Neon.TEXT);
        title.setGravity(Gravity.CENTER);
        title.setMaxLines(2);
        title.setEllipsize(TextUtils.TruncateAt.END);
        title.setPadding(0, dp(22), 0, dp(4));
        v.addView(title, lp(-1, -2, 0));
        sub = Neon.text(a, "", 15, Neon.PINK, true);
        sub.setGravity(Gravity.CENTER);
        sub.setSingleLine(true);
        sub.setEllipsize(TextUtils.TruncateAt.END);
        v.addView(sub, lp(-1, -2, 0));
        info = Neon.text(a, "", 13, Neon.DIM, false);
        info.setGravity(Gravity.CENTER);
        info.setSingleLine(true);
        v.addView(info, lp(-1, -2, 0));
        lyr = Neon.text(a, "", 14, Neon.CYAN, false);
        lyr.setGravity(Gravity.CENTER);
        lyr.setMaxLines(2);
        lyr.setPadding(0, dp(8), 0, 0);
        v.addView(lyr, lp(-1, -2, 0));
        bar = new FrameLayout(a);                               // progression
        GradientDrawable track = new GradientDrawable();
        track.setColor(0x26FFFFFF);
        track.setCornerRadius(dp(3));
        bar.setBackground(track);
        fill = new View(a);
        fill.setBackground(Neon.neonFill(a, 3));
        fill.setScaleX(0f);
        bar.addView(fill, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout.LayoutParams blp = lp(-1, dp(6), 0);
        blp.topMargin = dp(18);
        v.addView(bar, blp);
        LinearLayout times = new LinearLayout(a);
        el = Neon.text(a, "0:00", 12, Neon.DIM, false);
        dur = Neon.text(a, "", 12, Neon.DIM, false);
        dur.setGravity(Gravity.END);
        times.addView(el, lp(0, -2, 1));
        times.addView(dur, lp(0, -2, 1));
        v.addView(times, lp(-1, -2, 0));
        LinearLayout ctl = new LinearLayout(a);                 // commandes
        ctl.setGravity(Gravity.CENTER);
        ctl.setPadding(0, dp(14), 0, dp(10));
        ctl.addView(round("◀◀", 54, false, x -> Api.cmd("prev", -1)));
        play = round("▶", 76, true, x -> Api.cmd("toggle", -1));
        ctl.addView(play);
        ctl.addView(round("▶▶", 54, false, x -> Api.cmd("next", -1)));
        ctl.addView(round("■", 44, false, x -> Api.cmd("stop", -1)));
        v.addView(ctl, lp(-1, -2, 0));
        LinearLayout vr = new LinearLayout(a);                  // volume
        vr.setGravity(Gravity.CENTER);
        vr.addView(round("−", 46, false, x -> Api.cmd("voldown", -1)));
        vol = Neon.glow(a, "", 16, Neon.CYAN);
        vol.setGravity(Gravity.CENTER);
        vr.addView(vol, new LinearLayout.LayoutParams(dp(110), -2));
        vr.addView(round("+", 46, false, x -> Api.cmd("volup", -1)));
        v.addView(vr, lp(-1, -2, 0));
        root = v;
        return v;
    }

    private TextView round(String s, int size, boolean primary, View.OnClickListener l) {
        TextView b = Neon.btn(a, s, size > 60 ? 26 : 15, primary, l);
        b.setPadding(0, 0, 0, 0);
        GradientDrawable g = primary ? Neon.neonFill(a, size) : new GradientDrawable();
        g.setShape(GradientDrawable.OVAL);
        if (!primary) { g.setColor(Neon.GLASS); g.setStroke(dp(1.2f), Neon.alpha(Neon.PINK, 0x99)); }
        b.setBackground(new android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(Neon.alpha(Neon.CYAN, 0x66)), g, null));
        if (primary) { b.setElevation(dp(10)); b.setShadowLayer(dp(8), 0, 0, Color.WHITE); }
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(dp(size), dp(size));
        p.setMargins(dp(8), 0, dp(8), 0);
        b.setLayoutParams(p);
        return b;
    }

    @Override void state(JSONObject s) {
        String t = s.optString("title", "");
        Neon.set(title, t.isEmpty() ? "Rien en lecture" : t);
        Neon.set(sub, s.optString("sub", ""));
        Neon.set(info, s.optString("info", ""));
        Neon.set(lyr, s.optString("lyr", ""));
        int e = s.optInt("el", 0), d = s.optInt("dur", 0);
        Neon.set(el, clock(e));
        Neon.set(dur, d > 0 ? clock(d) : "");
        float f = d > 0 ? Math.min(1f, e / (float) d) : 0f;    // (barre etiree : pas de nouvelle mise en page)
        fill.setPivotX(0);
        fill.animate().scaleX(f).setDuration(f < fill.getScaleX() ? 0 : 950).setInterpolator(new android.view.animation.LinearInterpolator()).start();
        Neon.set(vol, "Volume " + s.optInt("vol", 0) + " %");
        Neon.set(play, s.optInt("playing", 0) != 0 ? "❚❚" : "▶");
        String key = s.optInt("cov", 0) != 0 ? t + "|" + s.optString("sub", "") : "";
        if (!key.equals(coverKey)) {
            coverKey = key;
            if (key.isEmpty()) cover.setImageResource(R.mipmap.ic_launcher);
            else Api.bytes("/cover.bmp?k=" + Api.enc(key), b -> {
                Bitmap bm = b == null ? null : BitmapFactory.decodeByteArray(b, 0, b.length);
                if (bm != null && key.equals(coverKey)) cover.setImageBitmap(bm);
            });
        }
    }
}

// ---------------------------------------------------------------- Musique / Radio
class ListTab extends Tab {
    private final boolean radio;
    private final List<String[]> all = new ArrayList<>();
    private final List<Integer> shown = new ArrayList<>();
    private TextView status;
    private EditText search;
    private BaseAdapter adapter;
    private boolean loaded;
    ListTab(Activity a, boolean radio) { super(a); this.radio = radio; }

    @Override View build() {
        LinearLayout v = new LinearLayout(a);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(dp(16), dp(4), dp(16), 0);
        search = new EditText(a);
        search.setHint(radio ? "Chercher une radio" : "Chercher un morceau, un artiste");
        search.setHintTextColor(Neon.FAINT);
        search.setTextColor(Neon.TEXT);
        search.setSingleLine(true);
        search.setBackground(Neon.glass(a, 18));
        search.setPadding(dp(16), dp(11), dp(16), dp(11));
        search.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int st, int c, int af) { }
            public void onTextChanged(CharSequence s, int st, int b, int c) { filter(); }
            public void afterTextChanged(Editable e) { }
        });
        v.addView(search, lp(-1, -2, 0));
        status = Neon.text(a, "Chargement…", 12, Neon.DIM, false);
        status.setPadding(dp(6), dp(8), 0, dp(4));
        v.addView(status, lp(-1, -2, 0));
        ListView lv = new ListView(a);
        lv.setDivider(null);
        lv.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        lv.setClipToPadding(false);
        lv.setFastScrollEnabled(true);
        lv.setPadding(0, 0, 0, dp(12));
        adapter = new BaseAdapter() {
            public int getCount() { return shown.size(); }
            public Object getItem(int p) { return all.get(shown.get(p)); }
            public long getItemId(int p) { return shown.get(p); }
            public View getView(int p, View cv, ViewGroup parent) { return row(all.get(shown.get(p)), shown.get(p), cv); }
        };
        lv.setAdapter(adapter);
        lv.setOnItemClickListener((parent, view, pos, id) -> {
            view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            Api.cmd(radio ? "radio" : "play", (int) id);
            Toast.makeText(a, (radio ? "Radio : " : "Lecture : ") + all.get((int) id)[0], Toast.LENGTH_SHORT).show();
        });
        v.addView(lv, lp(-1, 0, 1));
        root = v;
        return v;
    }

    private View row(String[] it, int idx, View cv) {
        LinearLayout r;
        TextView n, s, ic;
        if (cv instanceof LinearLayout && cv.getTag() != null) {
            r = (LinearLayout) cv;
            TextView[] t = (TextView[]) cv.getTag();
            ic = t[0]; n = t[1]; s = t[2];
        } else {
            r = new LinearLayout(a);
            r.setGravity(Gravity.CENTER_VERTICAL);
            r.setPadding(dp(12), dp(10), dp(12), dp(10));
            r.setBackground(new android.graphics.drawable.InsetDrawable(Neon.button(a, 16, false), 0, dp(4), 0, dp(4)));
            r.setPadding(dp(12), dp(14), dp(12), dp(14));
            ic = Neon.glow(a, radio ? "◉" : "♫", 18, radio ? Neon.CYAN : Neon.PINK);
            ic.setGravity(Gravity.CENTER);
            r.addView(ic, new LinearLayout.LayoutParams(dp(36), -2));
            LinearLayout col = new LinearLayout(a);
            col.setOrientation(LinearLayout.VERTICAL);
            n = Neon.text(a, "", 15, Neon.TEXT, true);
            n.setSingleLine(true);
            n.setEllipsize(TextUtils.TruncateAt.END);
            s = Neon.text(a, "", 12, Neon.DIM, false);
            s.setSingleLine(true);
            s.setEllipsize(TextUtils.TruncateAt.END);
            col.addView(n);
            col.addView(s);
            r.addView(col, new LinearLayout.LayoutParams(0, -2, 1));
            r.setTag(new TextView[] { ic, n, s });
        }
        n.setText(it[0]);
        s.setText(it[1]);
        s.setVisibility(it[1].isEmpty() ? View.GONE : View.VISIBLE);
        return r;
    }

    @Override void show() { if (!loaded) load(); }

    void load() {
        status.setText("Chargement…");
        Api.get(radio ? "/api/stations" : "/api/tracks", r -> {
            if (r == null) { status.setText("Pas de réponse de la console"); return; }
            all.clear();
            try {
                JSONArray j = new JSONArray(r);
                for (int i = 0; i < j.length(); i++) {
                    if (radio) all.add(new String[] { j.optString(i, ""), "" });
                    else { JSONArray e = j.optJSONArray(i); all.add(new String[] { e != null ? e.optString(0, "") : "", e != null ? e.optString(1, "") : "" }); }
                }
            } catch (Exception e) { }
            loaded = true;
            filter();
        });
    }

    private void filter() {
        String q = search.getText().toString().trim().toLowerCase();
        shown.clear();
        for (int i = 0; i < all.size(); i++) {
            String[] it = all.get(i);
            if (q.isEmpty() || it[0].toLowerCase().contains(q) || it[1].toLowerCase().contains(q)) shown.add(i);
        }
        if (loaded) Neon.set(status, shown.size() + (radio ? " radio" : " morceau") + (shown.size() > 1 ? "s" : "") + (q.isEmpty() ? "" : " trouvé" + (shown.size() > 1 ? "s" : "")));
        adapter.notifyDataSetChanged();
    }
}

// ---------------------------------------------------------------- Manette (+ pave tactile et clavier)
class PadTab extends Tab {
    private LinearLayout kbCard;
    private TextView kbHint;
    private EditText kbField;
    private boolean kbTyping;
    private final Handler h = new Handler(Looper.getMainLooper());
    private Runnable kbSend;
    PadTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = new LinearLayout(a);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(dp(16), dp(4), dp(16), dp(12));
        kbCard = new LinearLayout(a);                          // clavier : quand un clavier est ouvert sur la tele
        kbCard.setOrientation(LinearLayout.VERTICAL);
        kbCard.setBackground(Neon.glass(a, 18));
        kbCard.setPadding(dp(14), dp(10), dp(14), dp(12));
        kbHint = Neon.text(a, "Clavier ouvert sur la télé", 13, Neon.CYAN, true);
        kbCard.addView(kbHint);
        LinearLayout kr = new LinearLayout(a);
        kr.setGravity(Gravity.CENTER_VERTICAL);
        kbField = new EditText(a);
        kbField.setTextColor(Neon.TEXT);
        kbField.setSingleLine(true);
        kbField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        kbField.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int st, int c, int af) { }
            public void onTextChanged(CharSequence s, int st, int b, int c) { if (kbField.hasFocus()) sendText(false); }
            public void afterTextChanged(Editable e) { }
        });
        kr.addView(kbField, lp(0, -2, 1));
        kr.addView(Neon.btn(a, "OK", 14, true, x -> sendText(true)), lp(-2, -2, 0));
        kbCard.addView(kr);
        kbCard.setVisibility(View.GONE);
        LinearLayout.LayoutParams kp = lp(-1, -2, 0);
        kp.bottomMargin = dp(10);
        v.addView(kbCard, kp);

        LinearLayout sh = new LinearLayout(a);                 // gachettes
        sh.addView(key("L", "l", false), lp(0, dp(44), 1));
        sh.addView(space(), lp(0, 1, 0.3f));
        sh.addView(key("Z", "z", false), lp(0, dp(44), 1));
        sh.addView(space(), lp(0, 1, 0.3f));
        sh.addView(key("R", "r", false), lp(0, dp(44), 1));
        v.addView(sh, lp(-1, -2, 0));

        LinearLayout mid = new LinearLayout(a);                // croix + boutons
        mid.setGravity(Gravity.CENTER_VERTICAL);
        mid.setPadding(0, dp(14), 0, dp(10));
        GridLayout dpad = new GridLayout(a);
        dpad.setColumnCount(3);
        String[][] d = { { null, null }, { "▲", "up" }, { null, null }, { "◀", "left" }, { "●", null }, { "▶", "right" }, { null, null }, { "▼", "down" }, { null, null } };
        for (String[] k : d) {
            View b = k[0] == null ? space() : k[1] == null ? centerDot() : key(k[0], k[1], false);
            GridLayout.LayoutParams g = new GridLayout.LayoutParams();
            g.width = dp(50); g.height = dp(50);
            g.setMargins(dp(2), dp(2), dp(2), dp(2));
            dpad.addView(b, g);
        }
        mid.addView(dpad);
        mid.addView(space(), lp(0, 1, 1));
        FrameLayout abxy = new FrameLayout(a);                 // A (gros, vert), B (rouge), X, Y
        abxy.addView(face("A", "a", 0xFF2FE08A, 68), at(dp(40), dp(44), dp(68)));
        abxy.addView(face("B", "b", 0xFFFF4D6D, 44), at(dp(0), dp(92), dp(44)));
        abxy.addView(face("X", "x", Neon.CYAN, 44), at(dp(112), dp(30), dp(44)));
        abxy.addView(face("Y", "y", Neon.VIOLET, 44), at(dp(44), dp(0), dp(44)));
        mid.addView(abxy, new LinearLayout.LayoutParams(dp(160), dp(140)));
        v.addView(mid, lp(-1, -2, 0));

        LinearLayout st = new LinearLayout(a);
        st.setGravity(Gravity.CENTER);
        TextView start = key("START", "start", false);
        st.addView(start, new LinearLayout.LayoutParams(dp(120), dp(40)));
        v.addView(st, lp(-1, -2, 0));

        TextView tl = Neon.text(a, "Pavé tactile : pointeur de Cube Browser (toucher = clic, deux doigts = défiler)", 12, Neon.DIM, false);
        tl.setPadding(dp(4), dp(14), 0, dp(6));
        v.addView(tl, lp(-1, -2, 0));
        v.addView(touchpad(), lp(-1, 0, 1));
        root = v;
        return v;
    }

    private View space() { return new View(a); }
    private View centerDot() { TextView t = Neon.text(a, "", 10, Neon.FAINT, false); t.setBackground(Neon.glass(a, 10)); return t; }
    private FrameLayout.LayoutParams at(int x, int y, int s) { FrameLayout.LayoutParams p = new FrameLayout.LayoutParams(s, s); p.leftMargin = x; p.topMargin = y; return p; }

    private TextView key(String label, String k, boolean primary) {
        TextView b = Neon.btn(a, label, label.length() > 2 ? 13 : 17, primary, x -> Api.key(k));
        b.setPadding(0, 0, 0, 0);
        return b;
    }

    private TextView face(String label, String k, int color, int size) {
        TextView b = Neon.btn(a, label, size > 50 ? 26 : 18, false, x -> Api.key(k));
        b.setPadding(0, 0, 0, 0);
        b.setTextColor(Color.WHITE);
        b.setShadowLayer(dp(8), 0, 0, color);
        GradientDrawable g = new GradientDrawable();
        g.setShape(GradientDrawable.OVAL);
        g.setColor(Neon.alpha(color, 0x55));
        g.setStroke(dp(2), color);
        b.setBackground(new android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(Neon.alpha(Color.WHITE, 0x66)), g, null));
        b.setElevation(dp(6));
        return b;
    }

    private View touchpad() {
        FrameLayout p = new FrameLayout(a);
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TL_BR, new int[] { Neon.alpha(Neon.PINK, 0x22), Neon.alpha(Neon.CYAN, 0x18) });
        g.setCornerRadius(dp(22));
        g.setStroke(dp(1.5f), Neon.alpha(Neon.CYAN, 0x88));
        p.setBackground(g);
        TextView hint = Neon.glow(a, "☝", 30, Neon.alpha(Neon.CYAN, 0x99));
        hint.setGravity(Gravity.CENTER);
        p.addView(hint, new FrameLayout.LayoutParams(-1, -1));
        final float[] last = new float[3];
        final long[] down = new long[1];
        final float[] moved = new float[1];
        final int[] acc = new int[3];
        p.setOnTouchListener((view, e) -> {
            int n = e.getPointerCount();
            float x = e.getX(), y = e.getY();
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    last[0] = x; last[1] = y; last[2] = 1; down[0] = System.currentTimeMillis(); moved[0] = 0; return true;
                case MotionEvent.ACTION_POINTER_DOWN:
                    last[1] = e.getY(0); last[2] = 2; return true;
                case MotionEvent.ACTION_MOVE: {
                    float dx = x - last[0], dy = (n > 1 ? e.getY(0) : y) - last[1];
                    moved[0] += Math.abs(dx) + Math.abs(dy);
                    last[0] = x; last[1] = n > 1 ? e.getY(0) : y;
                    float k = 1.6f / a.getResources().getDisplayMetrics().density * 2;
                    if (n > 1) acc[2] += (int) (dy * k); else { acc[0] += (int) (dx * k); acc[1] += (int) (dy * k); }
                    if (Math.abs(acc[0]) + Math.abs(acc[1]) + Math.abs(acc[2]) >= 3) { Api.mouse(acc[0], acc[1], -acc[2] * 3, 0); acc[0] = acc[1] = acc[2] = 0; }
                    return true;
                }
                case MotionEvent.ACTION_UP:
                    if (System.currentTimeMillis() - down[0] < 250 && moved[0] < 16 && last[2] < 2) { view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); Api.mouse(0, 0, 0, 1); }
                    return true;
            }
            return true;
        });
        return p;
    }

    private void sendText(final boolean go) {
        if (kbSend != null) h.removeCallbacks(kbSend);
        final String t = kbField.getText().toString();
        kbSend = () -> Api.get("/api/type?go=" + (go ? 1 : 0) + "&t=" + Api.enc(t), (Api.Done<String>) null);
        kbTyping = !go;
        if (go) { kbSend.run(); kbField.clearFocus(); hideIme(); } else h.postDelayed(kbSend, 250);
    }

    private void hideIme() {
        android.view.inputmethod.InputMethodManager im = (android.view.inputmethod.InputMethodManager) a.getSystemService(Context.INPUT_METHOD_SERVICE);
        if (im != null) im.hideSoftInputFromWindow(kbField.getWindowToken(), 0);
    }

    @Override void state(JSONObject s) {
        boolean kb = s.optInt("kb", 0) != 0;
        int vis = kb ? View.VISIBLE : View.GONE;
        if (kbCard.getVisibility() != vis) kbCard.setVisibility(vis);
        if (kb) {
            String hn = s.optString("kbh", "");
            Neon.set(kbHint, hn.isEmpty() ? "Clavier ouvert sur la télé" : hn);
            if (!kbField.hasFocus() && !kbTyping) kbField.setText(s.optString("kbt", ""));
        } else kbTyping = false;
    }
}

// ---------------------------------------------------------------- Fichiers (carte SD de la console)
class FilesTab extends Tab {
    static final int REQ_SEND = 7;
    private TextView path, msg, paste;
    private String h = "", uh = null;
    private final List<Object[]> entries = new ArrayList<>();   // { nom, dossier?, taille }
    private BaseAdapter adapter;
    private String[] cut;                                       // { h, i, nom }
    private boolean loaded;
    FilesTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = new LinearLayout(a);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(dp(16), dp(4), dp(16), 0);
        path = Neon.glow(a, "sd:", 15, Neon.CYAN);
        path.setSingleLine(true);
        path.setEllipsize(TextUtils.TruncateAt.START);
        v.addView(path, lp(-1, -2, 0));
        LinearLayout tb = new LinearLayout(a);
        tb.setPadding(0, dp(8), 0, dp(4));
        tb.addView(Neon.btn(a, "↑", 16, false, x -> { if (uh != null) open(uh, -1); }), lp(0, -2, 0.6f));
        tb.addView(gap());
        tb.addView(Neon.btn(a, "+ Dossier", 13, false, x -> mkdir()), lp(0, -2, 1.2f));
        tb.addView(gap());
        paste = Neon.btn(a, "Coller", 13, true, x -> doPaste());
        paste.setVisibility(View.GONE);
        tb.addView(paste, lp(0, -2, 1.1f));
        tb.addView(gap());
        tb.addView(Neon.btn(a, "Envoyer", 13, false, x -> pick()), lp(0, -2, 1.1f));
        v.addView(tb, lp(-1, -2, 0));
        msg = Neon.text(a, "Touche un fichier pour le télécharger, appui long pour le reste", 12, Neon.DIM, false);
        msg.setPadding(dp(4), dp(4), 0, dp(6));
        v.addView(msg, lp(-1, -2, 0));
        ListView lv = new ListView(a);
        lv.setDivider(null);
        lv.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        adapter = new BaseAdapter() {
            public int getCount() { return entries.size(); }
            public Object getItem(int p) { return entries.get(p); }
            public long getItemId(int p) { return p; }
            public View getView(int p, View cv, ViewGroup parent) { return row(entries.get(p), cv); }
        };
        lv.setAdapter(adapter);
        lv.setOnItemClickListener((parent, view, pos, id) -> {
            Object[] e = entries.get(pos);
            if ((Boolean) e[1]) open(h, pos); else download(pos, (String) e[0]);
        });
        lv.setOnItemLongClickListener((parent, view, pos, id) -> { view.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS); actions(pos); return true; });
        v.addView(lv, lp(-1, 0, 1));
        root = v;
        return v;
    }

    private View gap() { return new View(a) { { setLayoutParams(new LinearLayout.LayoutParams(Neon.dp(a, 6), 1)); } }; }

    private View row(Object[] e, View cv) {
        LinearLayout r;
        TextView ic, n, s;
        if (cv instanceof LinearLayout && cv.getTag() != null) {
            r = (LinearLayout) cv;
            TextView[] t = (TextView[]) cv.getTag();
            ic = t[0]; n = t[1]; s = t[2];
        } else {
            r = new LinearLayout(a);
            r.setGravity(Gravity.CENTER_VERTICAL);
            r.setPadding(dp(12), dp(12), dp(12), dp(12));
            r.setBackground(new android.graphics.drawable.InsetDrawable(Neon.button(a, 14, false), 0, dp(3), 0, dp(3)));
            r.setPadding(dp(12), dp(15), dp(12), dp(15));
            ic = Neon.glow(a, "", 17, Neon.PINK);
            ic.setGravity(Gravity.CENTER);
            r.addView(ic, new LinearLayout.LayoutParams(dp(34), -2));
            n = Neon.text(a, "", 15, Neon.TEXT, true);
            n.setSingleLine(true);
            n.setEllipsize(TextUtils.TruncateAt.MIDDLE);
            r.addView(n, new LinearLayout.LayoutParams(0, -2, 1));
            s = Neon.text(a, "", 12, Neon.DIM, false);
            r.addView(s);
            r.setTag(new TextView[] { ic, n, s });
        }
        boolean dir = (Boolean) e[1];
        ic.setText(dir ? "▣" : "▫");
        ic.setTextColor(dir ? Neon.CYAN : Neon.PINK);
        n.setText((String) e[0]);
        long sz = (Long) e[2];
        s.setText(dir ? "" : sz >= 1048576 ? String.format(java.util.Locale.FRANCE, "%.1f Mo", sz / 1048576.0) : Math.max(1, (sz + 1023) / 1024) + " Ko");
        return r;
    }

    @Override void show() { if (!loaded) open("", -1); }

    void open(String dirH, int i) {
        msg.setText("Lecture du dossier…");
        Api.get("/api/files?h=" + dirH + (i >= 0 ? "&i=" + i : ""), r -> {
            if (r == null) { msg.setText("Pas de réponse de la console"); return; }
            try {
                JSONObject j = new JSONObject(r);
                h = j.optString("h", "");
                uh = j.isNull("uh") ? null : j.optString("uh", null);
                path.setText(j.optString("d", "sd:"));
                entries.clear();
                JSONArray e = j.optJSONArray("e");
                for (int k = 0; e != null && k < e.length(); k++) {
                    JSONArray x = e.getJSONArray(k);
                    entries.add(new Object[] { x.optString(0, ""), x.optInt(1, 0) != 0, x.optLong(2, 0) });
                }
                loaded = true;
                msg.setText(entries.isEmpty() ? "(dossier vide)" : entries.size() + " élément" + (entries.size() > 1 ? "s" : "") + " · appui long : renommer, couper, supprimer");
                adapter.notifyDataSetChanged();
            } catch (Exception ex) { msg.setText("Réponse illisible"); }
        });
    }

    private void fsop(String q) {
        msg.setText("…");
        Api.get("/api/fsop?" + q, r -> {
            String m = "Pas de réponse de la console";
            try { if (r != null) m = new JSONObject(r).optString("m", m); } catch (Exception e) { }
            msg.setText(m);
            open(h, -1);
        });
    }

    private void actions(final int pos) {
        final String name = (String) entries.get(pos)[0];
        final boolean dir = (Boolean) entries.get(pos)[1];
        final String q = "&h=" + h + "&i=" + pos + "&x=" + Api.enc(name);
        new AlertDialog.Builder(a)
            .setTitle(name)
            .setItems(new String[] { "Renommer", "Couper (pour déplacer)", "Supprimer" }, (dlg, w) -> {
                if (w == 0) ask("Nouveau nom", name, v -> { if (!v.isEmpty() && !v.equals(name)) fsop("op=ren" + q + "&n=" + Api.enc(v)); });
                else if (w == 1) { cut = new String[] { h, String.valueOf(pos), name }; paste.setText("Coller : " + name); paste.setVisibility(View.VISIBLE); msg.setText("Ouvre le dossier de destination, puis Coller"); }
                else new AlertDialog.Builder(a).setTitle("Supprimer " + name + (dir ? " et tout son contenu" : "") + " ?")
                        .setPositiveButton("Supprimer", (d2, w2) -> fsop("op=del" + q)).setNegativeButton("Annuler", null).show();
            }).show();
    }

    private void doPaste() {
        if (cut == null) return;
        String q = "op=mv&h=" + cut[0] + "&i=" + cut[1] + "&x=" + Api.enc(cut[2]) + "&t=" + h;
        cut = null;
        paste.setVisibility(View.GONE);
        fsop(q);
    }

    private void mkdir() { ask("Nom du nouveau dossier", "", v -> { if (!v.isEmpty()) fsop("op=mkdir&h=" + h + "&n=" + Api.enc(v)); }); }

    interface Text { void on(String v); }
    private void ask(String title, String init, final Text t) {
        final EditText f = new EditText(a);
        f.setText(init);
        f.setSingleLine(true);
        f.setSelection(f.getText().length());
        FrameLayout box = new FrameLayout(a);
        box.setPadding(dp(20), dp(8), dp(20), 0);
        box.addView(f);
        new AlertDialog.Builder(a).setTitle(title).setView(box)
            .setPositiveButton("OK", (d, w) -> t.on(f.getText().toString().trim())).setNegativeButton("Annuler", null).show();
    }

    private void download(int pos, String name) {
        try {
            DownloadManager.Request r = new DownloadManager.Request(Uri.parse("http://" + Api.host + "/api/get?h=" + h + "&i=" + pos));
            r.setTitle(name);
            r.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            r.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, name);
            ((DownloadManager) a.getSystemService(Context.DOWNLOAD_SERVICE)).enqueue(r);
            Toast.makeText(a, "Téléchargement : " + name, Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            Toast.makeText(a, "Téléchargement impossible", Toast.LENGTH_LONG).show();
        }
    }

    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try { a.startActivityForResult(i, REQ_SEND); } catch (Exception e) { Toast.makeText(a, "Pas de sélecteur de fichiers", Toast.LENGTH_LONG).show(); }
    }

    void refresh() { if (loaded) open(h, -1); }
}
