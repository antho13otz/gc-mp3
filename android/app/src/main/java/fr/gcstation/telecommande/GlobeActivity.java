package fr.gcstation.telecommande;

// Globe des radios (bien plus pousse que celui de la GameCube) : la Terre en 3D (OpenGL), l'annuaire mondial
// radio-browser (des milliers de radios placees sur la carte), jour / nuit en direct.
//  - glisser (avec inertie), pincer pour zoomer, double-toucher pour s'approcher, toucher une radio pour la choisir ;
//  - filtres par genre, "Lisibles par la GameCube", favoris ; recherche (nom, pays, ville, style) avec vol anime ;
//  - Au hasard, Radio de la console (elle est reperee sur le globe), rotation lente ;
//  - ecouter sur la GameCube (/api/playurl), sur le telephone, ou ajouter a "mes radios" de la console.

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.drawable.GradientDrawable;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.GestureDetector;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;

public class GlobeActivity extends Activity {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private GLSurfaceView gl;
    private GlobeRenderer r;
    private LabelView label;
    private TextView count, sheetTitle, selName, selInfo, phoneName, rotBtn;
    private LinearLayout sheet, phoneBar, chipRow;
    private EditText search;
    private ListView list;
    private BaseAdapter adapter;
    private final List<Integer> rows = new ArrayList<>();
    private List<RadioDir.Station> data = new ArrayList<>();
    private final Map<String, Integer> byName = new HashMap<>();
    private int genre = -1, sel = -1;
    private boolean gcOnly, favOnly, loading;
    private Set<String> favs;
    private MediaPlayer mp;
    private String phoneUuid;
    private final List<TextView> chips = new ArrayList<>();
    private boolean resumed;

    private int dp(float v) { return Neon.dp(this, v); }
    private SharedPreferences prefs() { return getSharedPreferences("gcs", MODE_PRIVATE); }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().setStatusBarColor(0xFF040108);
        getWindow().setNavigationBarColor(0xFF040108);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);
        favs = new HashSet<>(prefs().getStringSet("globe_favs", new HashSet<String>()));

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xFF040108);
        gl = new GLSurfaceView(this);
        gl.setEGLContextClientVersion(2);
        gl.setEGLConfigChooser(GlobeActivity::chooseConfig);   // (anticrenelage 4x si le telephone sait le faire : bords nets)
        gl.setPreserveEGLContextOnPause(true);
        r = new GlobeRenderer(this);
        gl.setRenderer(r);
        gl.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        root.addView(gl, new FrameLayout.LayoutParams(-1, -1));
        label = new LabelView(this);
        root.addView(label, new FrameLayout.LayoutParams(-1, -1));
        r.frameListener = () -> label.postInvalidateOnAnimation();
        gestures();

        // ---- en haut : titre, recherche, filtres ----
        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.VERTICAL);
        top.setPadding(dp(12), dp(10), dp(12), 0);
        GradientDrawable shade = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0xE6040108, 0x99040108, 0x00040108 });
        top.setBackground(shade);
        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        TextView back = Neon.btn(this, "←", 16, false, v -> finish());
        back.setPadding(0, 0, 0, 0);
        bar.addView(back, new LinearLayout.LayoutParams(dp(40), dp(40)));
        LinearLayout tt = new LinearLayout(this);
        tt.setOrientation(LinearLayout.VERTICAL);
        tt.setPadding(dp(12), 0, 0, 0);
        tt.addView(Neon.glow(this, "GLOBE DES RADIOS", 17, Neon.CYAN));
        count = Neon.text(this, "Chargement de l'annuaire mondial…", 12, Neon.DIM, false);
        tt.addView(count);
        bar.addView(tt, new LinearLayout.LayoutParams(0, -2, 1));
        top.addView(bar);
        search = new EditText(this);
        search.setHint("Radio, pays, ville, style…");
        search.setHintTextColor(Neon.FAINT);
        search.setTextColor(Neon.TEXT);
        search.setSingleLine(true);
        search.setBackground(Neon.glass(this, 18));
        search.setPadding(dp(16), dp(10), dp(16), dp(10));
        search.setTextSize(15);
        search.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int a, int c, int d) { }
            public void onTextChanged(CharSequence s, int a, int c, int d) { ui.removeCallbacks(doSearch); ui.postDelayed(doSearch, 280); }
            public void afterTextChanged(Editable e) { }
        });
        LinearLayout.LayoutParams sl = new LinearLayout.LayoutParams(-1, -2);
        sl.topMargin = dp(8);
        top.addView(search, sl);
        HorizontalScrollView hs = new HorizontalScrollView(this);
        hs.setHorizontalScrollBarEnabled(false);
        chipRow = new LinearLayout(this);
        chipRow.setPadding(0, dp(8), 0, dp(10));
        addChip("Toutes", -1, Neon.TEXT);
        for (int g = 0; g < RadioDir.GENRE.length; g++) addChip(RadioDir.GENRE[g], g, RadioDir.GENRE_COL[g]);
        addChip("▶ GameCube", -2, Neon.CYAN);
        addChip("★ Favoris", -3, 0xFFFFD25C);
        hs.addView(chipRow);
        top.addView(hs);
        root.addView(top, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP));
        paintChips();

        // ---- a droite : hasard, radio de la console, rotation, zoom ----
        LinearLayout fab = new LinearLayout(this);
        fab.setOrientation(LinearLayout.VERTICAL);
        fab.addView(fabBtn("🎲", v -> random()));
        fab.addView(fabBtn("◎", v -> toConsole()));
        rotBtn = fabBtn("⟳", v -> { r.autoTurn = !r.autoTurn; paintRot(); });
        fab.addView(rotBtn);
        fab.addView(fabBtn("＋", v -> r.flyTo(r.lat, r.lon, Math.max(1.3f, r.dist * 0.6f))));
        fab.addView(fabBtn("－", v -> r.flyTo(r.lat, r.lon, Math.min(6f, r.dist * 1.6f))));
        FrameLayout.LayoutParams fl = new FrameLayout.LayoutParams(-2, -2, Gravity.END | Gravity.CENTER_VERTICAL);
        fl.rightMargin = dp(10);
        root.addView(fab, fl);
        paintRot();

        // ---- en bas : la radio choisie, ses voisines ; lecture sur le telephone ----
        LinearLayout bottom = new LinearLayout(this);
        bottom.setOrientation(LinearLayout.VERTICAL);
        phoneBar = new LinearLayout(this);
        phoneBar.setGravity(Gravity.CENTER_VERTICAL);
        phoneBar.setBackground(Neon.glass(this, 16));
        phoneBar.setPadding(dp(14), dp(6), dp(6), dp(6));
        phoneName = Neon.text(this, "", 13, Neon.TEXT, true);
        phoneName.setSingleLine(true);
        phoneName.setEllipsize(TextUtils.TruncateAt.END);
        phoneBar.addView(phoneName, new LinearLayout.LayoutParams(0, -2, 1));
        TextView stop = Neon.btn(this, "■", 14, false, v -> stopPhone());
        stop.setPadding(0, 0, 0, 0);
        phoneBar.addView(stop, new LinearLayout.LayoutParams(dp(38), dp(38)));
        phoneBar.setVisibility(View.GONE);
        LinearLayout.LayoutParams pbl = new LinearLayout.LayoutParams(-1, -2);
        pbl.setMargins(dp(12), 0, dp(12), dp(6));
        bottom.addView(phoneBar, pbl);

        sheet = new LinearLayout(this);
        sheet.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable sb = new GradientDrawable();
        sb.setColor(0xE60C0418);
        sb.setCornerRadius(dp(22));
        sb.setStroke(dp(1), Neon.alpha(Neon.PINK, 0x66));
        sheet.setBackground(sb);
        sheet.setPadding(dp(16), dp(12), dp(16), dp(10));
        LinearLayout sh = new LinearLayout(this);
        sh.setGravity(Gravity.CENTER_VERTICAL);
        sheetTitle = Neon.text(this, "", 13, Neon.PINK, true);
        sheetTitle.setSingleLine(true);
        sheetTitle.setEllipsize(TextUtils.TruncateAt.END);
        sh.addView(sheetTitle, new LinearLayout.LayoutParams(0, -2, 1));
        TextView close = Neon.btn(this, "✕", 12, false, v -> hideSheet());
        close.setPadding(0, 0, 0, 0);
        sh.addView(close, new LinearLayout.LayoutParams(dp(32), dp(32)));
        sheet.addView(sh);
        selName = Neon.glow(this, "", 19, Neon.TEXT);
        selName.setSingleLine(true);
        selName.setEllipsize(TextUtils.TruncateAt.END);
        sheet.addView(selName);
        selInfo = Neon.text(this, "", 12, Neon.DIM, false);
        selInfo.setSingleLine(true);
        selInfo.setEllipsize(TextUtils.TruncateAt.END);
        sheet.addView(selInfo);
        LinearLayout act = new LinearLayout(this);
        act.setPadding(0, dp(10), 0, dp(6));
        act.addView(Neon.btn(this, "▶ GameCube", 13, true, v -> playConsole()), new LinearLayout.LayoutParams(0, dp(44), 1.3f));
        TextView ph = Neon.btn(this, "📱 Téléphone", 13, false, v -> playPhone());
        LinearLayout.LayoutParams pl = new LinearLayout.LayoutParams(0, dp(44), 1.2f);
        pl.leftMargin = dp(8);
        act.addView(ph, pl);
        TextView more = Neon.btn(this, "★", 15, false, v -> moreActions());
        LinearLayout.LayoutParams ml = new LinearLayout.LayoutParams(dp(48), dp(44));
        ml.leftMargin = dp(8);
        act.addView(more, ml);
        sheet.addView(act);
        list = new ListView(this);
        list.setDivider(null);
        list.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        adapter = new BaseAdapter() {
            public int getCount() { return rows.size(); }
            public Object getItem(int p) { return rows.get(p); }
            public long getItemId(int p) { return rows.get(p); }
            public View getView(int p, View cv, ViewGroup parent) { return row(rows.get(p), cv); }
        };
        list.setAdapter(adapter);
        list.setOnItemClickListener((parent, view, pos, id) -> {
            view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            select((int) id, true, false);
        });
        sheet.addView(list, new LinearLayout.LayoutParams(-1, dp(190)));
        sheet.setVisibility(View.GONE);
        LinearLayout.LayoutParams shl = new LinearLayout.LayoutParams(-1, -2);
        shl.setMargins(dp(10), 0, dp(10), dp(10));
        bottom.addView(sheet, shl);
        root.addView(bottom, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));

        setContentView(root);
        load();
    }

    /** anticrenelage 4x si possible, sinon une configuration simple (couleurs 8 bits, profondeur 16) */
    private static EGLConfig chooseConfig(EGL10 egl, javax.microedition.khronos.egl.EGLDisplay d) {
        int es2 = 4;                                            // (EGL_OPENGL_ES2_BIT)
        int[][] tries = {
            { EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_DEPTH_SIZE, 16, EGL10.EGL_RENDERABLE_TYPE, es2, EGL10.EGL_SAMPLE_BUFFERS, 1, EGL10.EGL_SAMPLES, 4, EGL10.EGL_NONE },
            { EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_DEPTH_SIZE, 16, EGL10.EGL_RENDERABLE_TYPE, es2, EGL10.EGL_NONE },
            { EGL10.EGL_DEPTH_SIZE, 16, EGL10.EGL_RENDERABLE_TYPE, es2, EGL10.EGL_NONE },
        };
        EGLConfig[] cf = new EGLConfig[1];
        int[] n = new int[1];
        for (int[] a : tries) if (egl.eglChooseConfig(d, a, cf, 1, n) && n[0] > 0 && cf[0] != null) return cf[0];
        return null;
    }

    // ---------------- donnees ----------------
    private void load() {
        loading = true;
        final boolean fresh = RadioDir.cacheFresh(this);
        Api.pool(() -> {
            final List<RadioDir.Station> cached = RadioDir.loadCache(this);
            ui.post(() -> { if (!cached.isEmpty()) setData(cached, false); });
            if (fresh && !cached.isEmpty()) { ui.post(() -> { loading = false; updateCount(); }); return; }
            List<RadioDir.Station> all = RadioDir.fetch((got, page) -> {
                final List<RadioDir.Station> snap = new ArrayList<>(got);
                if (cached.isEmpty() || page == RadioDir.PAGES - 1) ui.post(() -> { if (!isFinishing()) setData(snap, false); });
                return !isFinishing();
            });
            if (all != null && !all.isEmpty()) {
                RadioDir.saveCache(this, all);
                final List<RadioDir.Station> fin = all;
                ui.post(() -> { loading = false; setData(fin, true); });
            } else ui.post(() -> {
                loading = false;
                if (data.isEmpty()) Neon.set(count, "Annuaire injoignable (Internet sur le téléphone ?)");
                else updateCount();
            });
        });
    }

    private void setData(List<RadioDir.Station> st, boolean keepSel) {
        String selUuid = sel >= 0 && sel < data.size() ? data.get(sel).uuid : null;
        data = st;
        byName.clear();
        for (int i = 0; i < st.size(); i++) {
            String k = st.get(i).name.trim().toLowerCase(Locale.ROOT);
            if (!byName.containsKey(k)) byName.put(k, i);
        }
        sel = -1;
        if (keepSel && selUuid != null) for (int i = 0; i < st.size(); i++) if (selUuid.equals(st.get(i).uuid)) { sel = i; break; }
        r.stations = st;
        r.selected = sel;
        r.playing = -1;
        applyFilter();
        if (sel < 0 && sheet.getVisibility() == View.VISIBLE && search.getText().length() == 0) hideSheet();
    }

    private void applyFilter() {
        List<RadioDir.Station> st = data;
        boolean[] vis = new boolean[st.size()];
        for (int i = 0; i < st.size(); i++) {
            RadioDir.Station s = st.get(i);
            vis[i] = (genre < 0 || s.genre == genre) && (!gcOnly || s.gcOk()) && (!favOnly || favs.contains(s.uuid));
        }
        r.visible = (genre < 0 && !gcOnly && !favOnly) ? null : vis;
        r.version++;
        updateCount();
    }

    private boolean shown(int i) { boolean[] v = r.visible; return v == null || (i < v.length && v[i]); }

    private void updateCount() {
        int n = 0;
        Set<String> cc = new HashSet<>();
        for (int i = 0; i < data.size(); i++) if (shown(i)) { n++; cc.add(data.get(i).cc); }
        String t = String.format(Locale.FRANCE, "%,d radios · %d pays", n, cc.size()).replace(' ', ' ').replace(' ', ' ');
        if (loading) t += " · mise à jour…";
        Neon.set(count, data.isEmpty() ? "Chargement de l'annuaire mondial…" : t);
    }

    // ---------------- filtres ----------------
    private void addChip(String name, int g, int col) {
        TextView c = Neon.text(this, name, 13, Neon.TEXT, true);
        c.setPadding(dp(14), dp(7), dp(14), dp(7));
        c.setTag(new int[] { g, col });
        c.setOnClickListener(v -> {
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            if (g >= -1) genre = g;
            else if (g == -2) gcOnly = !gcOnly;
            else favOnly = !favOnly;
            paintChips();
            applyFilter();
        });
        LinearLayout.LayoutParams l = new LinearLayout.LayoutParams(-2, -2);
        l.rightMargin = dp(6);
        chipRow.addView(c, l);
        chips.add(c);
    }

    private void paintChips() {
        for (TextView c : chips) {
            int[] t = (int[]) c.getTag();
            boolean on = t[0] >= -1 ? genre == t[0] : t[0] == -2 ? gcOnly : favOnly;
            GradientDrawable d = new GradientDrawable();
            d.setCornerRadius(dp(16));
            d.setColor(on ? Neon.alpha(t[1], 0x55) : 0x22FFFFFF);
            d.setStroke(dp(on ? 1.5f : 1f), on ? t[1] : Neon.alpha(t[1], 0x66));
            c.setBackground(d);
            c.setTextColor(on ? Color.WHITE : Neon.DIM);
            c.setShadowLayer(on ? dp(8) : 0, 0, 0, t[1]);
        }
    }

    private TextView fabBtn(String s, View.OnClickListener l) {
        TextView b = Neon.btn(this, s, 17, false, l);
        b.setPadding(0, 0, 0, 0);
        b.setBackground(Neon.glass(this, 24));
        b.setLayoutParams(new LinearLayout.LayoutParams(dp(46), dp(46)));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(46), dp(46));
        lp.bottomMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    private void paintRot() { rotBtn.setTextColor(r.autoTurn ? Neon.CYAN : Neon.FAINT); rotBtn.setShadowLayer(r.autoTurn ? dp(8) : 0, 0, 0, Neon.CYAN); }

    // ---------------- gestes ----------------
    private void gestures() {
        final ScaleGestureDetector scale = new ScaleGestureDetector(this, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
            @Override public boolean onScale(ScaleGestureDetector d) {
                r.dist = Math.max(1.22f, Math.min(6f, r.dist / d.getScaleFactor()));
                return true;
            }
        });
        final GestureDetector g = new GestureDetector(this, new GestureDetector.SimpleOnGestureListener() {
            @Override public boolean onDown(MotionEvent e) { r.vLat = r.vLon = 0; return true; }
            @Override public boolean onScroll(MotionEvent e1, MotionEvent e2, float dx, float dy) {
                if (scale.isInProgress()) return true;
                float k = degPerPx();
                r.lon += dx * k;
                r.lat -= dy * k;
                return true;
            }
            @Override public boolean onFling(MotionEvent e1, MotionEvent e2, float vx, float vy) {
                if (scale.isInProgress()) return true;
                float k = degPerPx() * 0.85f;
                r.vLon = -vx * k;
                r.vLat = vy * k;
                return true;
            }
            @Override public boolean onSingleTapConfirmed(MotionEvent e) {
                int i = r.pick(e.getX(), e.getY(), dp(30));
                if (i >= 0) { gl.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); select(i, false, true); }
                return true;
            }
            @Override public boolean onDoubleTap(MotionEvent e) {
                r.flyTo(r.lat, r.lon, Math.max(1.3f, r.dist * 0.55f));
                return true;
            }
        });
        gl.setOnTouchListener((v, e) -> {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN) { r.touching = true; r.lastTouchMs = System.currentTimeMillis(); hideKeyboard(); }
            scale.onTouchEvent(e);
            g.onTouchEvent(e);
            if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) { r.touching = false; r.lastTouchMs = System.currentTimeMillis(); }
            return true;
        });
    }

    /** degres par point d'ecran : la Terre suit le doigt, quel que soit le zoom */
    private float degPerPx() {
        float px = Math.max(1, Math.min(r.width, r.height));
        return (float) (Math.toDegrees(2 * Math.atan((r.dist - 1) * Math.tan(Math.toRadians(17)))) * 2.0 / px);
    }

    private void hideKeyboard() {
        InputMethodManager im = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (im != null) im.hideSoftInputFromWindow(search.getWindowToken(), 0);
        search.clearFocus();
    }

    // ---------------- choix d'une radio ----------------
    private void select(int i, boolean fly, boolean neighbours) {
        if (i < 0 || i >= data.size()) return;
        sel = i;
        r.selected = i;
        RadioDir.Station s = data.get(i);
        if (fly) r.flyTo(s.lat, s.lon, Math.min(r.dist, 1.9f));
        Neon.set(selName, s.name);
        StringBuilder in = new StringBuilder();
        String place = !s.state.isEmpty() ? s.state + ", " + s.country : s.country;
        in.append(RadioDir.flag(s.cc)).append(' ').append(place);
        if (!s.codec.isEmpty()) in.append(" · ").append(s.codec).append(s.bitrate > 0 ? " " + s.bitrate + " kb/s" : "");
        if (!s.gcOk()) in.append(" · pas lisible par la GameCube");
        Neon.set(selInfo, in.toString());
        if (neighbours) {
            Neon.set(sheetTitle, "Autour de " + (!s.state.isEmpty() ? s.state : s.country));
            rows.clear();
            List<int[]> near = new ArrayList<>();
            double cl = Math.cos(Math.toRadians(s.lat));
            for (int k = 0; k < data.size(); k++) {
                if (!shown(k)) continue;
                RadioDir.Station o = data.get(k);
                double dla = o.lat - s.lat, dlo = (o.lon - s.lon) * cl;
                if (dla * dla + dlo * dlo < 0.35 * 0.35) near.add(new int[] { k, o.clicks });   // (~40 km)
            }
            Collections.sort(near, (x, y) -> Integer.compare(y[1], x[1]));
            for (int k = 0; k < near.size() && k < 60; k++) rows.add(near.get(k)[0]);
            adapter.notifyDataSetChanged();
            list.setSelection(0);
        } else adapter.notifyDataSetChanged();
        showSheet();
    }

    private void showSheet() {
        if (sheet.getVisibility() == View.VISIBLE) return;
        sheet.setVisibility(View.VISIBLE);
        sheet.setAlpha(0f);
        sheet.setTranslationY(dp(40));
        sheet.animate().alpha(1f).translationY(0).setDuration(220).start();
    }

    private void hideSheet() {
        sheet.setVisibility(View.GONE);
        sel = -1;
        r.selected = -1;
    }

    private View row(int idx, View cv) {
        LinearLayout rr;
        TextView dot, n, s;
        if (cv instanceof LinearLayout && cv.getTag() != null) {
            rr = (LinearLayout) cv;
            TextView[] t = (TextView[]) cv.getTag();
            dot = t[0]; n = t[1]; s = t[2];
        } else {
            rr = new LinearLayout(this);
            rr.setGravity(Gravity.CENTER_VERTICAL);
            rr.setPadding(dp(6), dp(8), dp(6), dp(8));
            dot = Neon.text(this, "●", 13, Neon.CYAN, true);
            rr.addView(dot, new LinearLayout.LayoutParams(dp(22), -2));
            LinearLayout col = new LinearLayout(this);
            col.setOrientation(LinearLayout.VERTICAL);
            n = Neon.text(this, "", 14, Neon.TEXT, true);
            n.setSingleLine(true);
            n.setEllipsize(TextUtils.TruncateAt.END);
            s = Neon.text(this, "", 11, Neon.DIM, false);
            s.setSingleLine(true);
            s.setEllipsize(TextUtils.TruncateAt.END);
            col.addView(n);
            col.addView(s);
            rr.addView(col, new LinearLayout.LayoutParams(0, -2, 1));
            rr.setTag(new TextView[] { dot, n, s });
        }
        RadioDir.Station st = data.get(idx);
        int c = RadioDir.GENRE_COL[st.genre];
        dot.setTextColor(c);
        dot.setShadowLayer(dp(6), 0, 0, c);
        n.setText((favs.contains(st.uuid) ? "★ " : "") + st.name);
        n.setTextColor(idx == sel ? Neon.CYAN : Neon.TEXT);
        String tags = st.tags.length() > 40 ? st.tags.substring(0, 40) + "…" : st.tags;
        s.setText(RadioDir.flag(st.cc) + " " + (st.state.isEmpty() ? st.country : st.state) + (tags.isEmpty() ? "" : " · " + tags.replace(",", ", ")));
        return rr;
    }

    // ---------------- recherche, hasard, radio de la console ----------------
    private final Runnable doSearch = () -> {
        String q = RadioDir.fold(search.getText().toString().trim());
        if (q.isEmpty()) { if (sel < 0) hideSheet(); return; }
        String[] words = q.split("\\s+");
        List<int[]> found = new ArrayList<>();
        for (int i = 0; i < data.size(); i++) {
            if (!shown(i)) continue;
            String k = data.get(i).key;
            boolean ok = true;
            for (String w : words) if (!k.contains(w)) { ok = false; break; }
            if (ok) found.add(new int[] { i, data.get(i).clicks });
        }
        Collections.sort(found, (x, y) -> Integer.compare(y[1], x[1]));
        rows.clear();
        for (int k = 0; k < found.size() && k < 80; k++) rows.add(found.get(k)[0]);
        adapter.notifyDataSetChanged();
        Neon.set(sheetTitle, found.isEmpty() ? "Aucune radio trouvée" : found.size() + " résultat" + (found.size() > 1 ? "s" : "") + " · touche pour y aller");
        if (!found.isEmpty()) select(found.get(0)[0], true, false);
        else { Neon.set(selName, "—"); Neon.set(selInfo, ""); showSheet(); }
    };

    private void random() {
        List<Integer> c = new ArrayList<>();
        for (int i = 0; i < data.size(); i++) if (shown(i)) c.add(i);
        if (c.isEmpty()) return;
        // (un peu plus de chances pour les radios ecoutees, mais tout le monde peut sortir)
        int i = c.get((int) (Math.pow(Math.random(), 1.6) * c.size()));
        select(i, true, true);
    }

    private void toConsole() {
        int p = r.playing;
        if (p >= 0 && p < data.size()) select(p, true, true);
        else Toast.makeText(this, "La console ne joue pas une radio du globe", Toast.LENGTH_SHORT).show();
    }

    // ---------------- ecouter ----------------
    private RadioDir.Station cur() { return sel >= 0 && sel < data.size() ? data.get(sel) : null; }

    private void playConsole() {
        final RadioDir.Station s = cur();
        if (s == null) return;
        if (!s.gcOk()) { Toast.makeText(this, "Format pas lisible par la GameCube (" + s.codec + ") : écoute-la sur le téléphone", Toast.LENGTH_LONG).show(); return; }
        if (Api.host == null) { Toast.makeText(this, "Console pas connectée", Toast.LENGTH_SHORT).show(); return; }
        final int idx = sel;
        Api.get("/api/playurl?u=" + Api.enc(s.url) + "&n=" + Api.enc(s.name), res -> {
            if (res != null && res.contains("\"ok\":1")) { r.playing = idx; Toast.makeText(this, "Sur la GameCube : " + s.name, Toast.LENGTH_SHORT).show(); }
            else Toast.makeText(this, res == null ? "Console injoignable" : "La console doit être mise à jour (v253)", Toast.LENGTH_LONG).show();
        });
        Api.pool(() -> RadioDir.click(s.uuid));
    }

    private void playPhone() {
        final RadioDir.Station s = cur();
        if (s == null) return;
        stopPhone();
        mp = new MediaPlayer();
        try {
            mp.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build());
            mp.setDataSource(s.url);
            final MediaPlayer m = mp;
            m.setOnPreparedListener(x -> { if (m == mp) { m.start(); Neon.set(phoneName, "📱 " + s.name); } });
            m.setOnErrorListener((x, w, e) -> { if (m == mp) { Toast.makeText(this, "Radio illisible sur le téléphone", Toast.LENGTH_SHORT).show(); stopPhone(); } return true; });
            m.prepareAsync();
            phoneUuid = s.uuid;
            Neon.set(phoneName, "📱 Connexion… " + s.name);
            phoneBar.setVisibility(View.VISIBLE);
            Api.pool(() -> RadioDir.click(s.uuid));
        } catch (Exception e) {
            Toast.makeText(this, "Radio illisible sur le téléphone", Toast.LENGTH_SHORT).show();
            stopPhone();
        }
    }

    private void stopPhone() {
        if (mp != null) { try { mp.release(); } catch (Exception e) { } mp = null; }
        phoneUuid = null;
        phoneBar.setVisibility(View.GONE);
    }

    private void moreActions() {
        final RadioDir.Station s = cur();
        if (s == null) return;
        final boolean fav = favs.contains(s.uuid);
        new NeonDialog(this).title(s.name).message(s.url)
            .items(new String[] { fav ? "☆  Retirer des favoris du globe" : "★  Favori du globe", "＋  Ajouter à mes radios (console)", "↗  Ouvrir le site de l'annuaire" }, w -> {
                if (w == 0) {
                    if (fav) favs.remove(s.uuid); else favs.add(s.uuid);
                    prefs().edit().putStringSet("globe_favs", new HashSet<>(favs)).apply();
                    adapter.notifyDataSetChanged();
                    if (favOnly) applyFilter();
                    Toast.makeText(this, fav ? "Retirée des favoris" : "Ajoutée aux favoris du globe", Toast.LENGTH_SHORT).show();
                } else if (w == 1) {
                    Api.get("/api/radioadd?u=" + Api.enc(s.url) + "&n=" + Api.enc(s.name), res -> Toast.makeText(this,
                        res != null && res.contains("\"ok\":1") ? "Ajoutée à tes radios de la console" : res == null ? "Console injoignable" : "La console doit être mise à jour (v253)", Toast.LENGTH_SHORT).show());
                } else {
                    try { startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse("https://www.radio-browser.info/history/" + s.uuid))); } catch (Exception e) { }
                }
            }).cancel("Fermer").show();
    }

    // ---------------- la radio de la console (reperee sur le globe) ----------------
    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!resumed) return;
            if (Api.host != null) Api.get("/api/state", res -> {
                if (res != null) try {
                    JSONObject j = new JSONObject(res);
                    String t = j.optString("title", "").trim().toLowerCase(Locale.ROOT);
                    Integer k = byName.get(t);
                    int p = r.playing;
                    if (k != null) r.playing = k;
                    else if (p >= 0 && p < data.size() && !data.get(p).name.trim().toLowerCase(Locale.ROOT).equals(t)) r.playing = -1;
                } catch (Exception e) { }
            });
            ui.postDelayed(this, 2500);
        }
    };

    @Override protected void onResume() { super.onResume(); resumed = true; gl.onResume(); ui.post(poll); }
    @Override protected void onPause() { super.onPause(); resumed = false; gl.onPause(); ui.removeCallbacks(poll); }
    @Override protected void onDestroy() { super.onDestroy(); stopPhone(); r.frameListener = null; }

    @Override public void onBackPressed() {
        if (sheet.getVisibility() == View.VISIBLE) { hideSheet(); return; }
        super.onBackPressed();
    }

    // ---------------- etiquette de la radio choisie (suit le point a l'ecran) ----------------
    private class LabelView extends View {
        private final Paint bg = new Paint(Paint.ANTI_ALIAS_FLAG), tx = new Paint(Paint.ANTI_ALIAS_FLAG), ln = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rc = new RectF();
        LabelView(Activity a) {
            super(a);
            bg.setColor(0xCC0C0418);
            tx.setColor(Neon.TEXT);
            tx.setTextSize(dp(13));
            tx.setFakeBoldText(true);
            ln.setColor(Neon.alpha(Neon.CYAN, 0xCC));
            ln.setStrokeWidth(dp(1.5f));
            setWillNotDraw(false);
        }
        @Override protected void onDraw(Canvas c) {
            float x = r.selX, y = r.selY;
            int i = sel;
            if (x < 0 || i < 0 || i >= data.size()) return;
            String s = data.get(i).name;
            if (s.length() > 28) s = s.substring(0, 27) + "…";
            float w = tx.measureText(s) + dp(20), h = dp(28), lx = x - w / 2, ly = y - dp(24) - h;
            if (lx < dp(8)) lx = dp(8);
            if (lx + w > getWidth() - dp(8)) lx = getWidth() - dp(8) - w;
            c.drawLine(x, y - dp(8), x, ly + h, ln);
            rc.set(lx, ly, lx + w, ly + h);
            bg.setShadowLayer(dp(8), 0, 0, Neon.alpha(Neon.PINK, 0x88));
            c.drawRoundRect(rc, h / 2, h / 2, bg);
            c.drawText(s, lx + dp(10), ly + h / 2 + tx.getTextSize() * 0.36f, tx);
        }
    }
}
