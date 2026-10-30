package fr.gcstation.telecommande;

// Les ecrans de "Plus" : Jeux (jaquettes), Photos, File d'attente, Reglages, Cartes memoire, Salon, Outils.

import android.app.Activity;
import android.app.DownloadManager;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Shader;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.util.LruCache;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

/** petites fabriques partagees par les ecrans */
final class UiKit {
    static LinearLayout column(Activity a) {
        LinearLayout v = new LinearLayout(a);
        v.setOrientation(LinearLayout.VERTICAL);
        v.setPadding(Neon.dp(a, 16), Neon.dp(a, 4), Neon.dp(a, 16), 0);
        return v;
    }
    static TextView hint(Activity a, String s) {
        TextView t = Neon.text(a, s, 12, Neon.DIM, false);
        t.setPadding(Neon.dp(a, 4), Neon.dp(a, 6), 0, Neon.dp(a, 6));
        return t;
    }
    static EditText search(Activity a, String hint, Runnable onChange) {
        EditText e = new EditText(a);
        e.setHint(hint);
        e.setHintTextColor(Neon.FAINT);
        e.setTextColor(Neon.TEXT);
        e.setSingleLine(true);
        e.setBackground(Neon.glass(a, 18));
        e.setPadding(Neon.dp(a, 16), Neon.dp(a, 11), Neon.dp(a, 16), Neon.dp(a, 11));
        e.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int st, int c, int af) { }
            public void onTextChanged(CharSequence s, int st, int b, int c) { onChange.run(); }
            public void afterTextChanged(Editable x) { }
        });
        return e;
    }
    /** champ de saisie neon (multiline : plusieurs lignes, ex. des codes) */
    static EditText field(Activity a, String hint, boolean multiline) {
        EditText e = new EditText(a);
        e.setHint(hint);
        e.setHintTextColor(Neon.FAINT);
        e.setTextColor(Neon.TEXT);
        e.setBackground(Neon.glass(a, 14));
        e.setPadding(Neon.dp(a, 14), Neon.dp(a, 11), Neon.dp(a, 14), Neon.dp(a, 11));
        if (multiline) {
            e.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE | android.text.InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            e.setMinLines(4);
            e.setGravity(Gravity.TOP | Gravity.START);
        } else e.setSingleLine(true);
        return e;
    }
    static ListView list(Activity a) {
        ListView lv = new ListView(a);
        lv.setDivider(null);
        lv.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        lv.setClipToPadding(false);
        lv.setPadding(0, 0, 0, Neon.dp(a, 12));
        return lv;
    }
    static LinearLayout card(Activity a) {
        LinearLayout c = new LinearLayout(a);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(Neon.glass(a, 18));
        c.setPadding(Neon.dp(a, 14), Neon.dp(a, 12), Neon.dp(a, 14), Neon.dp(a, 12));
        return c;
    }
    static LinearLayout.LayoutParams gapBelow(Activity a, int dp) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2);
        p.bottomMargin = Neon.dp(a, dp);
        return p;
    }
    static void download(Activity a, String path, String name) {
        try {
            DownloadManager.Request r = new DownloadManager.Request(Uri.parse("http://" + Api.host + path));
            r.setTitle(name);
            r.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            r.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, name);
            ((DownloadManager) a.getSystemService(Context.DOWNLOAD_SERVICE)).enqueue(r);
            Toast.makeText(a, "Téléchargement : " + name, Toast.LENGTH_SHORT).show();
        } catch (Exception e) { Toast.makeText(a, "Téléchargement impossible", Toast.LENGTH_LONG).show(); }
    }
    static int cols(Activity a, int minDp) { return Math.max(2, (int) (a.getResources().getDisplayMetrics().widthPixels / a.getResources().getDisplayMetrics().density / minDp)); }
}

// ---------------------------------------------------------------- Jeux
class GamesTab extends Tab {
    private final List<JSONArray> all = new ArrayList<>(), shown = new ArrayList<>();
    private final LruCache<Integer, Bitmap> covers = new LruCache<Integer, Bitmap>(16 * 1024 * 1024) {
        @Override protected int sizeOf(Integer k, Bitmap b) { return b.getByteCount(); }
    };
    private final java.util.Set<Integer> asked = java.util.Collections.synchronizedSet(new java.util.HashSet<>());
    private static final java.util.concurrent.ExecutorService coverExec = java.util.concurrent.Executors.newSingleThreadExecutor();
    private TextView status;
    private EditText search;
    private BaseAdapter adapter;
    private boolean loaded;
    private LastLaunch last;                                  // (carte "Dernier lancement")
    GamesTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        search = UiKit.search(a, "Chercher un jeu", this::filter);
        v.addView(search, lp(-1, -2, 0));
        last = new LastLaunch(a, this::openId);
        LinearLayout.LayoutParams ll = lp(-1, -2, 0);
        ll.topMargin = dp(8);
        v.addView(last.view, ll);
        status = UiKit.hint(a, "Chargement…");
        v.addView(status);
        GridView g = new GridView(a);
        g.setNumColumns(UiKit.cols(a, 120));
        g.setVerticalSpacing(dp(10));
        g.setHorizontalSpacing(dp(10));
        g.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        g.setClipToPadding(false);
        g.setPadding(0, 0, 0, dp(12));
        adapter = new BaseAdapter() {
            public int getCount() { return shown.size(); }
            public Object getItem(int p) { return shown.get(p); }
            public long getItemId(int p) { return shown.get(p).optInt(6); }
            public View getView(int p, View cv, ViewGroup parent) { return cell(shown.get(p), cv); }
        };
        g.setAdapter(adapter);
        g.setOnItemClickListener((parent, view, pos, id) -> {
            view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            final JSONArray x = shown.get(pos);
            sheet(x);
        });
        v.addView(g, lp(-1, 0, 1));
        root = v;
        return v;
    }

    // fiche du jeu : jaquette, ID, temps de jeu, favori, cheats (activer, ajouter, modifier), reglages du jeu,
    // raccourci sur l'ecran d'accueil, puis Pulse ou le lanceur de secours
    private String mask = "";                                 // (choix des cheats : '0'/'1' par cheat)
    private boolean maskDirty;
    void sheet(final JSONArray x) {
        final int i = x.optInt(6);
        final String id = x.optString(1), name = x.optString(0);
        LinearLayout top = new LinearLayout(a);
        top.setGravity(Gravity.CENTER_VERTICAL);
        ImageView im = new ImageView(a);
        im.setScaleType(ImageView.ScaleType.CENTER_CROP);
        GradientDrawable ib = new GradientDrawable();
        ib.setCornerRadius(dp(10)); ib.setColor(0x22FFFFFF);
        im.setBackground(ib);
        im.setClipToOutline(true);
        final Bitmap b = covers.get(i);
        if (b != null) im.setImageBitmap(b);
        top.addView(im, lp(dp(78), dp(108), 0));
        LinearLayout info = new LinearLayout(a);
        info.setOrientation(LinearLayout.VERTICAL);
        info.setPadding(dp(14), 0, 0, 0);
        info.addView(Neon.text(a, id, 13, Neon.CYAN, true));
        int min = x.optInt(3);
        info.addView(Neon.text(a, min > 0 ? "Joué " + (min >= 60 ? (min / 60) + " h " + (min % 60) + " min" : min + " min") : "Jamais joué avec Pulse", 13, Neon.DIM, false));
        {   // statistiques : lancements, dernier lancement, dernier resultat avec Pulse (garde par le telephone)
            int runs = x.optInt(2);
            long lastT = x.optLong(5);
            if (runs > 0) info.addView(Neon.text(a, "Lancé " + runs + " fois" + (lastT > 0 ? " · dernière fois " + LastLaunch.ago(lastT) : ""), 12, Neon.DIM, false));
            int[] pr = LastLaunch.forGame(a, id);
            if (pr != null) info.addView(Neon.text(a, "Dernier essai Pulse : " + LastLaunch.verdict(pr[0]) + " (" + LastLaunch.ago(pr[1]) + ")", 12, LastLaunch.color(pr[0]), false));
        }
        LinearLayout ir = new LinearLayout(a);
        ir.setPadding(0, dp(8), 0, 0);
        final TextView fav = Neon.btn(a, x.optInt(4) != 0 ? "★ Favori" : "☆ Favori", 12, x.optInt(4) != 0, null);
        fav.setOnClickListener(v -> {
            v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            Api.get("/api/gamefav?id=" + id + "&i=" + i, r -> {
                JSONObject j = null;
                try { if (r != null) j = new JSONObject(r); } catch (Exception e) { }
                if (j == null) { Toast.makeText(a, "Pas possible (console à mettre à jour ?)", Toast.LENGTH_SHORT).show(); return; }
                boolean on = j.optInt("fav") != 0;
                try { x.put(4, on ? 1 : 0); } catch (Exception e) { }
                fav.setText(on ? "★ Favori" : "☆ Favori");
                fav.setBackground(Neon.button(a, 16, on));
                adapter.notifyDataSetChanged();
            });
        });
        ir.addView(fav, lp(-2, -2, 0));
        TextView pin = Neon.btn(a, "⌂ Raccourci", 12, false, v -> pinShortcut(id, name, covers.get(i)));
        LinearLayout.LayoutParams pl = lp(-2, -2, 0);
        pl.leftMargin = dp(8);
        ir.addView(pin, pl);
        info.addView(ir);
        top.addView(info, lp(0, -2, 1));
        LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.addView(top);
        // cheats
        LinearLayout hr = new LinearLayout(a);
        hr.setGravity(Gravity.CENTER_VERTICAL);
        hr.setPadding(0, dp(18), 0, dp(4));
        TextView chTitle = Neon.glow(a, "CHEATS", 13, Neon.CYAN);
        hr.addView(chTitle, lp(0, -2, 1));
        final TextView ch = Neon.text(a, "Chargement des cheats…", 13, Neon.FAINT, false);
        final LinearLayout list = new LinearLayout(a);
        list.setOrientation(LinearLayout.VERTICAL);
        cheatList = list;
        foldBtn = Neon.btn(a, "▾", 14, false, v -> setCheatsOpen(!cheatsOpen()));   // (replier / deplier la liste)
        foldBtn.setVisibility(View.GONE);
        LinearLayout.LayoutParams fl = lp(-2, -2, 0);
        fl.rightMargin = dp(8);
        hr.addView(foldBtn, fl);
        chTitle.setOnClickListener(v -> { if (foldBtn.getVisibility() == View.VISIBLE) setCheatsOpen(!cheatsOpen()); });
        hr.addView(Neon.btn(a, "+ Ajouter", 12, false, v -> editCheat(id, -1, "", "", ch, list)), lp(-2, -2, 0));
        box.addView(hr);
        box.addView(ch);
        box.addView(list);
        // pastille (jouable, marche mal, ecran noir, plantage) : posee a la main
        TextView ph = Neon.glow(a, "PASTILLE", 13, Neon.CYAN);
        ph.setPadding(0, dp(18), 0, dp(6));
        box.addView(ph);
        final LinearLayout dots = new LinearLayout(a);
        dots.setGravity(Gravity.CENTER_VERTICAL);
        final TextView dname = Neon.text(a, "", 13, Neon.DIM, true);
        final Runnable[] paint = new Runnable[1];
        for (int k = 0; k < CP_COL.length; k++) {
            final int v = k;
            View d = new View(a);
            d.setClickable(true);
            d.setOnClickListener(vv -> {
                vv.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                Api.get("/api/compatset?id=" + id + "&d=" + v, r -> {
                    if (r == null) { Toast.makeText(a, "Pas possible (console à mettre à jour ?)", Toast.LENGTH_SHORT).show(); return; }
                    readCompat(r);
                    paint[0].run();
                    adapter.notifyDataSetChanged();
                });
            });
            LinearLayout.LayoutParams dp0 = lp(dp(34), dp(34), 0);
            dp0.rightMargin = dp(10);
            dots.addView(d, dp0);
        }
        dots.addView(dname, lp(0, -2, 1));
        paint[0] = () -> {
            Integer cur = compat.get(id);
            int cv = cur == null ? 0 : cur;
            for (int k = 0; k < CP_COL.length; k++) {
                GradientDrawable g = new GradientDrawable();
                g.setShape(GradientDrawable.OVAL);
                g.setColor(k == 0 ? 0x22FFFFFF : CP_COL[k]);
                g.setStroke(dp(k == cv ? 3 : 1), k == cv ? 0xFFFFFFFF : 0x55FFFFFF);
                dots.getChildAt(k).setBackground(g);
                dots.getChildAt(k).setScaleX(k == cv ? 1.15f : 1f); dots.getChildAt(k).setScaleY(k == cv ? 1.15f : 1f);
            }
            dname.setText(CP_NAME[cv < CP_NAME.length ? cv : 0]);
            dname.setTextColor(cv == 0 ? Neon.FAINT : CP_COL[cv]);
        };
        paint[0].run();
        box.addView(dots);
        // reglages du jeu (comme Y sur la console)
        LinearLayout gr = new LinearLayout(a);
        gr.setGravity(Gravity.CENTER_VERTICAL);
        gr.setPadding(0, dp(20), 0, dp(4));
        gr.addView(Neon.glow(a, "RÉGLAGES DU JEU", 13, Neon.CYAN), lp(0, -2, 1));
        final LinearLayout cfg = new LinearLayout(a);
        cfg.setOrientation(LinearLayout.VERTICAL);
        final TextView open = Neon.btn(a, "Afficher", 12, false, null);
        open.setOnClickListener(v -> {
            if (cfg.getChildCount() > 0) { cfg.removeAllViews(); open.setText("Afficher"); return; }
            open.setText("Masquer");
            loadCfg(id, i, -1, 0, cfg);
        });
        gr.addView(open, lp(-2, -2, 0));
        box.addView(gr);
        box.addView(cfg);
        // sauvegardes de ce jeu sur ses cartes memoire virtuelles (telecharger, effacer)
        LinearLayout sr = new LinearLayout(a);
        sr.setGravity(Gravity.CENTER_VERTICAL);
        sr.setPadding(0, dp(20), 0, dp(4));
        sr.addView(Neon.glow(a, "SAUVEGARDES (CARTE VIRTUELLE)", 13, Neon.CYAN), lp(0, -2, 1));
        final LinearLayout saves = new LinearLayout(a);
        saves.setOrientation(LinearLayout.VERTICAL);
        final TextView sopen = Neon.btn(a, "Afficher", 12, false, null);
        sopen.setOnClickListener(v -> {
            if (saves.getChildCount() > 0) { saves.removeAllViews(); sopen.setText("Afficher"); return; }
            sopen.setText("Masquer");
            CardsTab.gameSaves(a, id, saves);
        });
        sr.addView(sopen, lp(-2, -2, 0));
        box.addView(sr);
        box.addView(saves);
        mask = ""; maskDirty = false; saveId = id;
        Api.get("/api/cheats?id=" + id, r -> fillCheats(r, id, ch, list));
        new NeonDialog(a).title(name).view(box)
            .cancel("Annuler")
            .button("Lanceur de secours", false, () -> launchWith(id, i, 0, name))
            .button("Pulse + patchs de secours…", false, () -> fxDialog(id, i, name))
            .button("Lancer avec Pulse", true, () -> launchWith(id, i, 1, name))
            .onDismiss(() -> flushMask(null))
            .show();
    }

    // ---------- liste des cheats repliable (choix garde d'un jeu a l'autre) ----------
    private LinearLayout cheatList;
    private TextView foldBtn;
    private boolean cheatsOpen() { return a.getSharedPreferences("gcs", Context.MODE_PRIVATE).getBoolean("cheats_ouverts", false); }
    private void setCheatsOpen(boolean on) {
        a.getSharedPreferences("gcs", Context.MODE_PRIVATE).edit().putBoolean("cheats_ouverts", on).apply();
        applyFold();
    }
    private void applyFold() {
        if (cheatList == null || foldBtn == null) return;
        int n = cheatList.getChildCount();
        boolean open = cheatsOpen() || n <= 2;                // (1 ou 2 cheats : toujours affiches)
        foldBtn.setVisibility(n > 2 ? View.VISIBLE : View.GONE);
        foldBtn.setText(open ? "▴ Replier" : "▾ Afficher (" + n + ")");
        foldBtn.setTextSize(12);
        cheatList.setVisibility(open ? View.VISIBLE : View.GONE);
    }

    private void fillCheats(String r, final String id, final TextView ch, final LinearLayout list) {
        JSONObject j = null;
        try { if (r != null) j = new JSONObject(r); } catch (Exception e) { }
        JSONArray c = j != null ? j.optJSONArray("c") : null;
        list.removeAllViews();
        if (j == null) { ch.setText("Cheats indisponibles (console à mettre à jour ?)"); return; }
        if (!j.optString("err").isEmpty()) Toast.makeText(a, j.optString("err"), Toast.LENGTH_LONG).show();
        if (c == null || c.length() == 0) {
            mask = "";
            ch.setText(j.optInt("x") != 0 ? "Aucun cheat utilisable dans SD:/gc-mp3/cheats/" + id + ".txt" : "Pas encore de cheats pour ce jeu : + Ajouter (ou SD:/gc-mp3/cheats/" + id + ".txt)");
            applyFold();
            return;
        }
        StringBuilder m = new StringBuilder();
        for (int k = 0; k < c.length(); k++) m.append(c.optJSONArray(k).optInt(1) != 0 ? '1' : '0');
        mask = m.toString();
        maskDirty = false;
        final Runnable count = () -> {
            int on = 0;
            for (int k = 0; k < mask.length(); k++) if (mask.charAt(k) == '1') on++;
            ch.setText(on == 0 ? "Aucun cheat activé · appui long : modifier" : on + " cheat" + (on > 1 ? "s activés" : " activé") + " · Pulse les applique (sinon : lanceur de secours)");
            ch.setTextColor(on == 0 ? Neon.FAINT : Neon.DIM);
        };
        count.run();
        for (int k = 0; k < c.length(); k++) {
            final int idx = k;
            final JSONArray e = c.optJSONArray(k);
            LinearLayout row = new LinearLayout(a);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setBackground(Neon.button(a, 14, false));
            row.setPadding(dp(14), dp(10), dp(12), dp(10));
            LinearLayout tx = new LinearLayout(a);
            tx.setOrientation(LinearLayout.VERTICAL);
            TextView n = Neon.text(a, e.optString(0), 15, Neon.TEXT, true);
            tx.addView(n);
            int nc = e.optInt(2);
            tx.addView(Neon.text(a, nc + " ligne" + (nc > 1 ? "s" : "") + " de code · appui long : modifier", 12, Neon.FAINT, false));
            row.addView(tx, lp(0, -2, 1));
            final View sw = NeonDialog.toggle(a, mask.charAt(k) == '1');
            row.addView(sw, lp(dp(48), dp(28), 0));
            row.setClickable(true);
            row.setOnClickListener(v -> {
                v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                if (idx >= mask.length()) return;
                char[] cs = mask.toCharArray();
                cs[idx] = cs[idx] == '1' ? '0' : '1';
                mask = new String(cs);
                NeonDialog.paintToggle(a, sw, cs[idx] == '1');
                count.run();
                maskDirty = true;
                saveSoon(id);
            });
            row.setOnLongClickListener(v -> {
                v.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
                new NeonDialog(a).title(e.optString(0))
                    .items(new String[] { "Renommer", "Modifier les codes", "Supprimer" }, new int[] { 2 }, w -> {
                        if (w == 0) NeonDialog.ask(a, "Nouveau nom", e.optString(0), nm -> { if (!nm.isEmpty()) cheatOp(id, "op=r&n=" + idx + "&name=" + Api.enc(nm), ch, list); });
                        else if (w == 1) editCheat(id, idx, e.optString(0), e.optString(3), ch, list);
                        else NeonDialog.confirm(a, "Supprimer « " + e.optString(0) + " » ?", "Le cheat est retiré du fichier " + id + ".txt.", "Supprimer", () -> cheatOp(id, "op=d&n=" + idx, ch, list));
                    }).cancel("Fermer").show();
                return true;
            });
            LinearLayout.LayoutParams p = lp(-1, -2, 0);
            p.topMargin = dp(8);
            list.addView(row, p);
        }
        applyFold();
    }

    /** ajout (n = -1) ou nouveaux codes d'un cheat : nom + codes, une ligne XXXXXXXX YYYYYYYY par code */
    private void editCheat(final String id, final int n, String name, String codes, final TextView ch, final LinearLayout list) {
        LinearLayout f = new LinearLayout(a);
        f.setOrientation(LinearLayout.VERTICAL);
        final EditText nm = UiKit.field(a, "Nom du cheat (ex. Vies infinies)", false);
        nm.setText(name);
        if (n >= 0) nm.setEnabled(false);
        f.addView(nm, lp(-1, -2, 0));
        final EditText cd = UiKit.field(a, "04001234 00000063\n04005678 00000001", true);
        cd.setTypeface(android.graphics.Typeface.MONOSPACE);
        StringBuilder sb = new StringBuilder();
        for (String c : codes.split(",")) if (c.length() == 16) sb.append(sb.length() > 0 ? "\n" : "").append(c, 0, 8).append(' ').append(c.substring(8));
        cd.setText(sb);
        LinearLayout.LayoutParams cl = lp(-1, -2, 0);
        cl.topMargin = dp(10);
        f.addView(cd, cl);
        TextView hint = Neon.text(a, "Codes Action Replay déchiffrés / Gecko : 8 + 8 chiffres hexadécimaux par ligne.", 12, Neon.FAINT, false);
        hint.setPadding(dp(2), dp(8), 0, 0);
        f.addView(hint);
        new NeonDialog(a).title(n < 0 ? "Nouveau cheat" : "Codes : " + name).view(f).cancel("Annuler")
            .button("Enregistrer", true, () -> {
                String hex = cd.getText().toString().replaceAll("[^0-9A-Fa-f]", "");
                if (hex.isEmpty() || hex.length() % 16 != 0) { Toast.makeText(a, "Chaque ligne doit avoir 16 chiffres (XXXXXXXX YYYYYYYY)", Toast.LENGTH_LONG).show(); return; }
                StringBuilder p = new StringBuilder();
                for (int k = 0; k < hex.length(); k += 16) p.append(k > 0 ? "," : "").append(hex, k, k + 16);
                String nn = nm.getText().toString().trim();
                if (n < 0 && nn.isEmpty()) { Toast.makeText(a, "Donne un nom au cheat", Toast.LENGTH_SHORT).show(); return; }
                cheatOp(id, n < 0 ? "op=a&name=" + Api.enc(nn) + "&codes=" + p : "op=c&n=" + n + "&codes=" + p, ch, list);
            }).show();
    }

    private void cheatOp(final String id, final String q, final TextView ch, final LinearLayout list) {
        flushMask(() -> Api.get("/api/cheatedit?id=" + id + "&" + q, r -> {
            if (r == null) { Toast.makeText(a, "Pas de réponse (console à mettre à jour ?)", Toast.LENGTH_LONG).show(); return; }
            fillCheats(r, id, ch, list);
        }));
    }

    /** reglages du jeu : lus (row = -1) ou une ligne changee d'un cran (d = 1 / -1), row 13 = tout comme la console */
    private void loadCfg(final String id, final int i, int row, int d, final LinearLayout cfg) {
        Api.get("/api/gamecfg?id=" + id + "&i=" + i + "&row=" + row + "&d=" + d, r -> {
            JSONObject j = null;
            try { if (r != null) j = new JSONObject(r); } catch (Exception e) { }
            cfg.removeAllViews();
            if (j == null) { cfg.addView(Neon.text(a, "Indisponible (console à mettre à jour ?)", 13, Neon.FAINT, false)); return; }
            if (j.optInt("busy") != 0) { cfg.addView(Neon.text(a, "Les réglages de ce jeu sont ouverts sur la télé : ferme-les d'abord.", 13, Neon.PINK, false)); return; }
            JSONArray rows = j.optJSONArray("r");
            for (int k = 0; rows != null && k < rows.length(); k++) {
                final int rk = k;
                JSONArray e = rows.optJSONArray(k);
                LinearLayout rw = new LinearLayout(a);
                rw.setGravity(Gravity.CENTER_VERTICAL);
                rw.setBackground(Neon.glass(a, 14));
                rw.setPadding(dp(12), dp(8), dp(6), dp(8));
                LinearLayout tx = new LinearLayout(a);
                tx.setOrientation(LinearLayout.VERTICAL);
                tx.addView(Neon.text(a, e.optString(0), 14, Neon.TEXT, true));
                TextView val = Neon.text(a, e.optString(1), 13, e.optInt(2) != 0 ? 0xFF7DF0A6 : Neon.DIM, true);
                tx.addView(val);
                rw.addView(tx, lp(0, -2, 1));
                final String help = e.optString(3);
                rw.setOnClickListener(v -> Toast.makeText(a, help, Toast.LENGTH_LONG).show());
                TextView l = Neon.btn(a, "‹", 18, false, v -> loadCfg(id, i, rk, -1, cfg));
                l.setPadding(0, 0, 0, 0);
                TextView rr = Neon.btn(a, "›", 18, false, v -> loadCfg(id, i, rk, 1, cfg));
                rr.setPadding(0, 0, 0, 0);
                rw.addView(l, lp(dp(40), dp(40), 0));
                LinearLayout.LayoutParams rl = lp(dp(40), dp(40), 0);
                rl.leftMargin = dp(6);
                rw.addView(rr, rl);
                LinearLayout.LayoutParams p = lp(-1, -2, 0);
                p.topMargin = dp(6);
                cfg.addView(rw, p);
            }
            LinearLayout.LayoutParams p = lp(-1, -2, 0);
            p.topMargin = dp(10);
            cfg.addView(Neon.btn(a, "Tout comme la console", 13, false, v -> loadCfg(id, i, rows != null ? rows.length() : 13, 0, cfg)), p);
        });
    }

    /** icone du jeu sur l'ecran d'accueil : la toucher lance le jeu avec Pulse */
    private void pinShortcut(String id, String name, Bitmap cover) {
        android.content.pm.ShortcutManager sm = a.getSystemService(android.content.pm.ShortcutManager.class);
        if (sm == null || !sm.isRequestPinShortcutSupported()) { Toast.makeText(a, "Ton écran d'accueil n'accepte pas les raccourcis", Toast.LENGTH_LONG).show(); return; }
        Intent it = new Intent(a, MainActivity.class).setAction(MainActivity.ACT_GAME).putExtra("id", id).putExtra("nom", name)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        android.graphics.drawable.Icon icon;
        if (cover != null) {                                  // (icone adaptative : jaquette au centre sur fond neon)
            Bitmap ic = Bitmap.createBitmap(432, 432, Bitmap.Config.ARGB_8888);
            Canvas cv = new Canvas(ic);
            Paint bg = new Paint();
            bg.setShader(new LinearGradient(0, 0, 432, 432, new int[] { Neon.BG_TOP, 0xFF2A0A4A, Neon.BG_BOTTOM }, null, Shader.TileMode.CLAMP));
            cv.drawRect(0, 0, 432, 432, bg);
            float h = 260, w = h * cover.getWidth() / cover.getHeight();
            Paint p = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
            cv.drawBitmap(cover, null, new android.graphics.RectF(216 - w / 2, 216 - h / 2, 216 + w / 2, 216 + h / 2), p);
            icon = android.graphics.drawable.Icon.createWithAdaptiveBitmap(ic);
        } else icon = android.graphics.drawable.Icon.createWithResource(a, R.mipmap.ic_launcher);
        android.content.pm.ShortcutInfo si = new android.content.pm.ShortcutInfo.Builder(a, "jeu-" + id)
            .setShortLabel(name.length() > 24 ? name.substring(0, 23) + "…" : name).setLongLabel(name)
            .setIcon(icon).setIntent(it).build();
        try { sm.requestPinShortcut(si, null); } catch (Exception e) { Toast.makeText(a, "Raccourci impossible", Toast.LENGTH_SHORT).show(); }
    }

    private final Runnable saveRun = new Runnable() { public void run() { flushMask(null); } };
    private String saveId;
    private void saveSoon(String id) { saveId = id; root.removeCallbacks(saveRun); root.postDelayed(saveRun, 600); }
    /** envoie le choix des cheats a la console (s'il a change), puis then */
    private void flushMask(final Runnable then) {
        root.removeCallbacks(saveRun);
        if (!maskDirty || saveId == null) { if (then != null) then.run(); return; }
        maskDirty = false;
        Api.get("/api/cheatset?id=" + saveId + "&s=" + mask, r -> {
            if (r == null) Toast.makeText(a, "Cheats pas enregistrés", Toast.LENGTH_SHORT).show();
            if (then != null) then.run();
        });
    }
    private void launchWith(String id, int i, int m, String name) { flushMask(() -> launch(id, i, m, name)); }

    /** patchs de secours de Pulse, pour ce lancement seulement (les essais de la reparation automatique, au choix) */
    private static final String[][] FX = {
        { "T", "Pulse universel", "Attrape les accès au lecteur pendant le jeu (jeux qui se décompressent, plusieurs programmes : PSO…)" },
        { "S", "Lecteur lent (comme un vrai)", "Pour les jeux qui bloquent quand le disque répond trop vite" },
        { "F", "Lecteur rapide (normal)", "Vitesse habituelle de Pulse : pour un jeu réglé sur « lecteur lent » qui n'en a pas besoin" },
        { "C", "Sans carte mémoire virtuelle", "Le jeu utilise tes vraies cartes" },
        { "H", "Sans cheats", "Le jeu démarre sans ses cheats (ils restent choisis)" },
        { "W", "Sans 16:9 forcé", "Image d'origine du jeu" },
        { "V", "Vidéo d'origine", "Sans 480p / 60 Hz / sans flou / image large" } };
    private void fxDialog(final String id, final int i, final String name) {
        final boolean[] on = new boolean[FX.length];
        LinearLayout box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.addView(UiKit.hint(a, "Pour ce lancement seulement : les réglages du jeu ne changent pas. Choisis un ou plusieurs patchs."));
        for (int k = 0; k < FX.length; k++) {
            final int fk = k;
            LinearLayout row = new LinearLayout(a);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setBackground(Neon.button(a, 14, false));
            row.setPadding(dp(14), dp(10), dp(12), dp(10));
            LinearLayout tx = new LinearLayout(a);
            tx.setOrientation(LinearLayout.VERTICAL);
            tx.addView(Neon.text(a, FX[k][1], 15, Neon.TEXT, true));
            tx.addView(Neon.text(a, FX[k][2], 12, Neon.FAINT, false));
            row.addView(tx, lp(0, -2, 1));
            final View sw = NeonDialog.toggle(a, false);
            row.addView(sw, lp(dp(48), dp(28), 0));
            row.setClickable(true);
            row.setOnClickListener(v -> {
                v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                on[fk] = !on[fk];
                if (on[fk] && (fk == 1 || fk == 2)) { int other = fk == 1 ? 2 : 1; if (on[other]) { on[other] = false; NeonDialog.paintToggle(a, ((LinearLayout) ((LinearLayout) v.getParent()).getChildAt(other + 1)).getChildAt(1), false); } }
                NeonDialog.paintToggle(a, sw, on[fk]);
            });
            LinearLayout.LayoutParams p = lp(-1, -2, 0);
            p.topMargin = dp(8);
            box.addView(row, p);
        }
        new NeonDialog(a).title("Patchs de secours").view(box).cancel("Annuler")
            .button("Lancer avec Pulse", true, () -> {
                StringBuilder fx = new StringBuilder();
                for (int k = 0; k < FX.length; k++) if (on[k]) fx.append(FX[k][0]);
                if (fx.length() == 0) { Toast.makeText(a, "Aucun patch choisi : lancement normal avec Pulse", Toast.LENGTH_SHORT).show(); }
                final String f = fx.toString();
                flushMask(() -> Api.get("/api/launch?i=" + Math.max(0, i) + "&m=1&id=" + id + (f.isEmpty() ? "" : "&fx=" + f),
                    r -> Toast.makeText(a, r != null ? "Lancement avec Pulse" + (f.isEmpty() ? "" : " + " + f.length() + " patch" + (f.length() > 1 ? "s" : "")) + " : " + name : "Refusé", Toast.LENGTH_SHORT).show()));
            }).show();
    }

    static void launch(Activity a, String id, int i, int m, String name) {
        Api.get("/api/launch?i=" + Math.max(0, i) + "&m=" + m + "&id=" + id, r -> Toast.makeText(a, r != null ? "Lancement sur la télé : " + name : "Refusé", Toast.LENGTH_SHORT).show());
    }
    private void launch(String id, int i, int m, String name) { launch(a, id, i, m, name); }

    private View cell(JSONArray x, View cv) {
        LinearLayout c;
        ImageView im;
        TextView n, s;
        View dot;
        if (cv instanceof LinearLayout && cv.getTag() != null && ((Object[]) cv.getTag()).length > 3) { c = (LinearLayout) cv; Object[] t = (Object[]) cv.getTag(); im = (ImageView) t[0]; n = (TextView) t[1]; s = (TextView) t[2]; dot = (View) t[3]; }
        else {
            c = new LinearLayout(a);
            c.setOrientation(LinearLayout.VERTICAL);
            c.setBackground(Neon.button(a, 16, false));
            c.setPadding(dp(6), dp(6), dp(6), dp(8));
            im = new ImageView(a);
            im.setScaleType(ImageView.ScaleType.CENTER_CROP);
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(0xFF1A0E30);
            bg.setCornerRadius(dp(10));
            im.setBackground(bg);
            im.setClipToOutline(true);
            FrameLayout fr = new FrameLayout(a);                // jaquette + pastille (jouable, marche mal...) en haut a droite
            fr.addView(im, new FrameLayout.LayoutParams(-1, -1));
            dot = new View(a);
            FrameLayout.LayoutParams dl = new FrameLayout.LayoutParams(dp(18), dp(18), Gravity.TOP | Gravity.END);
            dl.setMargins(0, dp(6), dp(6), 0);
            dot.setElevation(dp(4));
            fr.addView(dot, dl);
            c.addView(fr, new LinearLayout.LayoutParams(-1, dp(140)));
            n = Neon.text(a, "", 13, Neon.TEXT, true);
            n.setMaxLines(2);
            n.setEllipsize(TextUtils.TruncateAt.END);
            n.setPadding(dp(2), dp(6), dp(2), 0);
            c.addView(n);
            s = Neon.text(a, "", 11, Neon.DIM, false);
            s.setPadding(dp(2), 0, dp(2), 0);
            c.addView(s);
            c.setTag(new Object[] { im, n, s, dot });
        }
        int i = x.optInt(6);
        n.setText((x.optInt(4) != 0 ? "★ " : "") + x.optString(0));
        Integer cv0 = compat.get(x.optString(1));
        paintDot(dot, cv0 == null ? 0 : cv0);
        String id = x.optString(1);
        s.setText(id + (x.optInt(3) > 0 ? " · " + x.optInt(3) + " min" : ""));
        Bitmap b = covers.get(i);
        im.setImageBitmap(b);
        im.setTag(i);
        if (b == null && !id.isEmpty() && asked.add(i)) {
            final ImageView target = im;
            final java.io.File disk = new java.io.File(a.getCacheDir(), "jaquettes/" + id + ".png");
            coverExec.execute(() -> {                         // (une a la fois : la console lit sa carte SD pour chacune)
                Bitmap bm = disk.exists() ? BitmapFactory.decodeFile(disk.getPath()) : null;
                if (bm == null) {
                    if (!Integer.valueOf(i).equals(target.getTag()) || root == null || !root.isShown()) { asked.remove(i); return; }   // (plus a l'ecran)
                    bm = Cmpr.fetch(i);
                    if (bm != null) {
                        try { disk.getParentFile().mkdirs(); java.io.FileOutputStream o = new java.io.FileOutputStream(disk); bm.compress(Bitmap.CompressFormat.PNG, 100, o); o.close(); } catch (Exception e) { }
                        try { Thread.sleep(40); } catch (InterruptedException e) { }
                    }
                }
                final Bitmap fb = bm;
                a.runOnUiThread(() -> { if (fb != null) { covers.put(i, fb); if (Integer.valueOf(i).equals(target.getTag())) target.setImageBitmap(fb); } });
            });
        }
        return c;
    }

    @Override void show() { if (!loaded) load(); last.refresh(); }
    @Override void state(JSONObject s) {                       // (nouveau lancement termine : carte et liste a jour)
        if (last != null && s.has("pj") && s.optLong("pj") != last.pj && last.pj >= 0 && root != null && root.isShown()) { last.pj = s.optLong("pj"); last.refresh(); load(); }
    }
    private void openId(String id) { for (JSONArray g : all) if (id.equals(g.optString(1))) { sheet(g); return; } }

    private void load() {
        Neon.set(status, "Chargement des jeux (la console fait la liste)…");
        Api.get("/api/games", r -> {
            if (r == null) { Neon.set(status, "Pas de réponse de la console"); return; }
            all.clear();
            try { JSONArray j = new JSONArray(r); for (int k = 0; k < j.length(); k++) { JSONArray g = j.getJSONArray(k); g.put(k); if (!g.optString(1).isEmpty()) all.add(g); } } catch (Exception e) { }
            loaded = true;
            filter();
            loadCompat();
        });
    }

    // ---------- pastilles (compatibilite) : posees aussi a la main ----------
    static final int[] CP_COL = { 0x66FFFFFF, 0xFF46DC78, 0xFFFFBE3C, 0xFFFF783C, 0xFFFF4650 };
    static final String[] CP_NAME = { "Aucune", "Jouable", "Marche mal", "Écran noir", "Plantage" };
    private final java.util.Map<String, Integer> compat = new java.util.HashMap<>();
    private void loadCompat() {
        Api.get("/api/compat", r -> { if (r != null) { readCompat(r); adapter.notifyDataSetChanged(); } });
    }
    private void readCompat(String r) {
        try {
            JSONObject j = new JSONObject(r);
            compat.clear();
            for (java.util.Iterator<String> it = j.keys(); it.hasNext(); ) { String k = it.next(); compat.put(k, j.optInt(k)); }
        } catch (Exception e) { }
    }
    private void paintDot(View dot, int v) {
        if (v <= 0 || v >= CP_COL.length) { dot.setVisibility(View.GONE); return; }
        dot.setVisibility(View.VISIBLE);
        GradientDrawable g = new GradientDrawable();
        g.setShape(GradientDrawable.OVAL);
        g.setColor(CP_COL[v]);
        g.setStroke(dp(2), 0xFF0C0418);
        dot.setBackground(g);
    }

    private void filter() {
        String q = search.getText().toString().trim().toLowerCase();
        shown.clear();
        for (JSONArray g : all) if (q.isEmpty() || g.optString(0).toLowerCase().contains(q) || g.optString(1).toLowerCase().contains(q)) shown.add(g);
        if (loaded) Neon.set(status, all.isEmpty() ? "Aucun jeu (onglet JEUX de la GameCube)" : shown.size() + " jeu" + (shown.size() > 1 ? "x" : "") + " · toucher pour lancer");
        adapter.notifyDataSetChanged();
    }
}

/** jaquettes : la console les envoie en CMPR (format de texture GameCube, blocs de 8x8 en 4 sous-blocs DXT1) */
final class Cmpr {
    static Bitmap fetch(int i) {
        java.net.HttpURLConnection c = null;
        try {
            c = (java.net.HttpURLConnection) new java.net.URL("http://" + Api.host + "/api/cover?i=" + i).openConnection();
            c.setConnectTimeout(3000);
            c.setReadTimeout(8000);
            if (c.getResponseCode() != 200) return null;
            String[] sz = (c.getHeaderField("X-Size") != null ? c.getHeaderField("X-Size") : "256x256x192x256").split("x");
            int w = Integer.parseInt(sz[0]), h = Integer.parseInt(sz[1]);
            int dw = sz.length > 2 ? Integer.parseInt(sz[2]) : w, dh = sz.length > 3 ? Integer.parseInt(sz[3]) : h;
            java.io.InputStream in = c.getInputStream();
            java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            int[] px = decode(o.toByteArray(), w, h);
            if (px == null) return null;
            Bitmap b = Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
            return dw != w || dh != h ? Bitmap.createBitmap(b, 0, 0, Math.min(dw, w), Math.min(dh, h)) : b;
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }
    private static int rgb(int v) { return 0xFF000000 | (((v >> 11) & 31) * 255 / 31) << 16 | (((v >> 5) & 63) * 255 / 63) << 8 | ((v & 31) * 255 / 31); }
    private static int mix(int a, int b, int x, int y, int z) {
        int r = (((a >> 16) & 255) * x + ((b >> 16) & 255) * y) / z, g = (((a >> 8) & 255) * x + ((b >> 8) & 255) * y) / z, bl = ((a & 255) * x + (b & 255) * y) / z;
        return 0xFF000000 | r << 16 | g << 8 | bl;
    }
    static int[] decode(byte[] d, int w, int h) {
        if (d.length < w * h / 2) return null;
        int[] p = new int[w * h];
        int o = 0;
        int[] cs = new int[4];
        for (int ty = 0; ty < h; ty += 8)
            for (int tx = 0; tx < w; tx += 8)
                for (int s = 0; s < 4; s++) {
                    int bx = tx + (s & 1) * 4, by = ty + (s >> 1) * 4;
                    int c0 = (d[o] & 255) << 8 | (d[o + 1] & 255), c1 = (d[o + 2] & 255) << 8 | (d[o + 3] & 255);
                    long bt = ((long) (d[o + 4] & 255) << 24) | ((d[o + 5] & 255) << 16) | ((d[o + 6] & 255) << 8) | (d[o + 7] & 255);
                    o += 8;
                    cs[0] = rgb(c0); cs[1] = rgb(c1);
                    if (c0 > c1) { cs[2] = mix(cs[0], cs[1], 2, 1, 3); cs[3] = mix(cs[0], cs[1], 1, 2, 3); }
                    else { cs[2] = mix(cs[0], cs[1], 1, 1, 2); cs[3] = 0; }
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++) {
                            int px = bx + x, py = by + y;
                            if (px < w && py < h) p[py * w + px] = cs[(int) ((bt >>> (30 - 2 * (y * 4 + x))) & 3)];
                        }
                }
        return p;
    }
}

// ---------------------------------------------------------------- Photos
class PhotosTab extends Tab {
    private final List<JSONArray> items = new ArrayList<>();
    private final LruCache<Integer, Bitmap> thumbs = new LruCache<Integer, Bitmap>(24 * 1024 * 1024) {
        @Override protected int sizeOf(Integer k, Bitmap b) { return b.getByteCount(); }
    };
    private final java.util.Set<Integer> asked = new java.util.HashSet<>();
    private TextView status;
    private BaseAdapter adapter;
    private boolean loaded;
    PhotosTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        status = UiKit.hint(a, "Chargement…");
        v.addView(status);
        GridView g = new GridView(a);
        g.setNumColumns(UiKit.cols(a, 110));
        g.setVerticalSpacing(dp(8));
        g.setHorizontalSpacing(dp(8));
        g.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        adapter = new BaseAdapter() {
            public int getCount() { return items.size(); }
            public Object getItem(int p) { return items.get(p); }
            public long getItemId(int p) { return items.get(p).optInt(0); }
            public View getView(int p, View cv, ViewGroup parent) { return cell(items.get(p), cv); }
        };
        g.setAdapter(adapter);
        g.setOnItemClickListener((parent, view, pos, id) -> {
            view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            JSONArray x = items.get(pos);
            Api.get("/api/show?i=" + x.optInt(0), r -> Neon.set(status, r != null ? "Sur la télé : " + x.optString(1) : "Pas de réponse de la console"));
        });
        v.addView(g, lp(-1, 0, 1));
        root = v;
        return v;
    }

    private View cell(JSONArray x, View cv) {
        FrameLayout f;
        ImageView im;
        TextView play;
        if (cv instanceof FrameLayout && cv.getTag() != null) { f = (FrameLayout) cv; Object[] t = (Object[]) cv.getTag(); im = (ImageView) t[0]; play = (TextView) t[1]; }
        else {
            f = new FrameLayout(a);
            int s = (int) (a.getResources().getDisplayMetrics().widthPixels / (float) UiKit.cols(a, 110)) - dp(12);
            f.setLayoutParams(new android.widget.AbsListView.LayoutParams(-1, s));
            im = new ImageView(a);
            im.setScaleType(ImageView.ScaleType.CENTER_CROP);
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(0xFF1A0E30);
            bg.setCornerRadius(dp(14));
            bg.setStroke(dp(1), Neon.alpha(Neon.PINK, 0x55));
            im.setBackground(bg);
            im.setClipToOutline(true);
            f.addView(im, new FrameLayout.LayoutParams(-1, -1));
            play = Neon.glow(a, "▶", 26, Neon.CYAN);
            play.setGravity(Gravity.CENTER);
            f.addView(play, new FrameLayout.LayoutParams(-1, -1));
            f.setTag(new Object[] { im, play });
        }
        final int i = x.optInt(0);
        boolean video = x.optInt(2) != 0;
        play.setVisibility(video ? View.VISIBLE : View.GONE);
        Bitmap b = thumbs.get(i);
        im.setImageBitmap(b);
        im.setTag(i);
        if (!video && b == null && asked.add(i)) {
            final ImageView target = im;
            Api.pool(() -> {
                byte[] d = Api.getBytes("/api/photo?i=" + i, 4000);
                Bitmap bm = null;
                if (d != null) {
                    BitmapFactory.Options o = new BitmapFactory.Options();
                    o.inJustDecodeBounds = true;
                    BitmapFactory.decodeByteArray(d, 0, d.length, o);
                    int ss = 1;
                    while (Math.max(o.outWidth, o.outHeight) / (ss * 2) >= 320) ss *= 2;
                    o = new BitmapFactory.Options();
                    o.inSampleSize = ss;
                    bm = BitmapFactory.decodeByteArray(d, 0, d.length, o);
                }
                final Bitmap fb = bm;
                a.runOnUiThread(() -> { if (fb != null) { thumbs.put(i, fb); if (Integer.valueOf(i).equals(target.getTag())) target.setImageBitmap(fb); } });
            });
        }
        return f;
    }

    @Override void show() { if (!loaded) load(); }

    void load() {
        Api.get("/api/photos", r -> {
            if (r == null) { Neon.set(status, "Pas de réponse de la console"); return; }
            items.clear();
            try { JSONArray j = new JSONArray(r); for (int k = 0; k < j.length(); k++) items.add(j.getJSONArray(k)); } catch (Exception e) { }
            loaded = true;
            Neon.set(status, items.isEmpty() ? "Aucune photo (SD:/photos)" : items.size() + " photo" + (items.size() > 1 ? "s" : "") + " · toucher pour l'afficher sur la télé");
            adapter.notifyDataSetChanged();
        });
    }
}

// ---------------------------------------------------------------- File d'attente
class QueueTab extends Tab {
    static List<String[]> tracks;                            // (liste des morceaux, partagee)
    private final List<Integer> q = new ArrayList<>();
    private String qKey = "";
    private TextView status;
    private BaseAdapter adapter;
    private long lastLoad;
    QueueTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        status = UiKit.hint(a, "À suivre sur la console");
        v.addView(status);
        ListView lv = UiKit.list(a);
        adapter = new BaseAdapter() {
            public int getCount() { return q.size(); }
            public Object getItem(int p) { return q.get(p); }
            public long getItemId(int p) { return q.get(p); }
            public View getView(int p, View cv, ViewGroup parent) {
                LinearLayout r;
                TextView num, n, s;
                if (cv instanceof LinearLayout && cv.getTag() != null) { r = (LinearLayout) cv; TextView[] t = (TextView[]) cv.getTag(); num = t[0]; n = t[1]; s = t[2]; }
                else {
                    r = new LinearLayout(a);
                    r.setGravity(Gravity.CENTER_VERTICAL);
                    r.setBackground(new android.graphics.drawable.InsetDrawable(Neon.button(a, 16, false), 0, dp(4), 0, dp(4)));
                    r.setPadding(dp(12), dp(14), dp(12), dp(14));
                    num = Neon.glow(a, "", 15, Neon.CYAN);
                    num.setGravity(Gravity.CENTER);
                    r.addView(num, new LinearLayout.LayoutParams(dp(36), -2));
                    LinearLayout col = new LinearLayout(a);
                    col.setOrientation(LinearLayout.VERTICAL);
                    n = Neon.text(a, "", 15, Neon.TEXT, true); n.setSingleLine(true); n.setEllipsize(TextUtils.TruncateAt.END);
                    s = Neon.text(a, "", 12, Neon.DIM, false); s.setSingleLine(true);
                    col.addView(n); col.addView(s);
                    r.addView(col, new LinearLayout.LayoutParams(0, -2, 1));
                    r.setTag(new TextView[] { num, n, s });
                }
                int k = q.get(p);
                String[] t = tracks != null && k >= 0 && k < tracks.size() ? tracks.get(k) : new String[] { "Morceau " + k, "" };
                num.setText(String.valueOf(p + 1));
                n.setText(t[0]);
                s.setText(t[1]);
                s.setVisibility(t[1].isEmpty() ? View.GONE : View.VISIBLE);
                return r;
            }
        };
        lv.setAdapter(adapter);
        lv.setOnItemClickListener((parent, view, pos, id) -> { view.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); Api.cmd("play", (int) id); });
        v.addView(lv, lp(-1, 0, 1));
        root = v;
        return v;
    }

    @Override void show() { lastLoad = 0; refresh(); }
    @Override void state(JSONObject s) { if (root != null && root.isShown() && System.currentTimeMillis() - lastLoad > 3500) refresh(); }

    private void refresh() {
        lastLoad = System.currentTimeMillis();
        if (tracks == null) {
            Api.get("/api/tracks", r -> {
                if (r == null) return;
                List<String[]> l = new ArrayList<>();
                try { JSONArray j = new JSONArray(r); for (int k = 0; k < j.length(); k++) { JSONArray e = j.optJSONArray(k); l.add(new String[] { e != null ? e.optString(0) : "", e != null ? e.optString(1) : "" }); } } catch (Exception e) { }
                tracks = l;
                adapter.notifyDataSetChanged();
            });
        }
        Api.get("/api/queue", r -> {
            if (r == null || r.equals(qKey)) return;
            qKey = r;
            q.clear();
            try { JSONArray j = new JSONArray(r); for (int k = 0; k < j.length(); k++) q.add(j.getInt(k)); } catch (Exception e) { }
            Neon.set(status, q.isEmpty() ? "Fin de la file (rien à suivre)" : q.size() + " morceau" + (q.size() > 1 ? "x" : "") + " à suivre · toucher pour le jouer");
            adapter.notifyDataSetChanged();
        });
    }
}

// ---------------------------------------------------------------- Reglages
class SettingsTab extends Tab {
    private LinearLayout box;
    private TextView status;
    private ScrollView scroll;
    SettingsTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        status = UiKit.hint(a, "Les réglages changent tout de suite sur la télé");
        v.addView(status);
        scroll = new ScrollView(a);
        box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, 0, 0, dp(16));
        scroll.addView(box);
        v.addView(scroll, lp(-1, 0, 1));
        root = v;
        return v;
    }

    @Override void show() { load(); }

    private void load() {
        Api.get("/api/settings", r -> {
            if (r == null) { Neon.set(status, "Pas de réponse de la console"); return; }
            try { fill(new JSONObject(r)); } catch (Exception e) { Neon.set(status, "Réponse illisible"); }
        });
    }

    private void fill(JSONObject j) {
        int y = scroll.getScrollY();
        box.removeAllViews();
        JSONArray m = j.optJSONArray("m");
        for (int k = 0; m != null && k < m.length(); k++) {
            JSONArray it = m.optJSONArray(k);
            if (it == null) continue;
            final int i = it.optInt(0);
            LinearLayout row = new LinearLayout(a);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setBackground(new android.graphics.drawable.InsetDrawable(Neon.glass(a, 14), 0, dp(3), 0, dp(3)));
            row.setPadding(dp(10), dp(10), dp(10), dp(10));
            LinearLayout col = new LinearLayout(a);
            col.setOrientation(LinearLayout.VERTICAL);
            TextView l = Neon.text(a, it.optString(1), 14, Neon.TEXT, true);
            TextView val = Neon.text(a, it.optString(2), 13, Neon.CYAN, false);
            col.addView(l);
            col.addView(val);
            if (it.optInt(3) != 0) {
                row.addView(col, new LinearLayout.LayoutParams(0, -2, 1));
                row.addView(Neon.btn(a, "Vérifier", 12, false, x -> step(i, 1)), lp(-2, -2, 0));
            } else {
                TextView mi = Neon.btn(a, "◀", 13, false, x -> step(i, -1));
                TextView pl = Neon.btn(a, "▶", 13, false, x -> step(i, 1));
                row.addView(mi, new LinearLayout.LayoutParams(dp(44), dp(40)));
                col.setPadding(dp(10), 0, dp(10), 0);
                row.addView(col, new LinearLayout.LayoutParams(0, -2, 1));
                row.addView(pl, new LinearLayout.LayoutParams(dp(44), dp(40)));
            }
            box.addView(row);
        }
        checks(j.optJSONArray("el"), "el", "Éléments du lecteur");
        checks(j.optJSONArray("tb"), "tb", "Onglets affichés");
        JSONObject t = j.optJSONObject("t");
        if (t != null && t.length() > 0) {
            TextView h = Neon.glow(a, "Textes", 15, Neon.PINK);
            h.setPadding(dp(4), dp(16), 0, dp(6));
            box.addView(h);
            Iterator<String> ks = t.keys();
            while (ks.hasNext()) {
                final String key = ks.next();
                LinearLayout row = new LinearLayout(a);
                row.setGravity(Gravity.CENTER_VERTICAL);
                final EditText e = new EditText(a);
                e.setText(t.optString(key));
                e.setHint(key);
                e.setHintTextColor(Neon.FAINT);
                e.setTextColor(Neon.TEXT);
                e.setSingleLine(true);
                row.addView(Neon.text(a, key, 12, Neon.DIM, false), new LinearLayout.LayoutParams(dp(90), -2));
                row.addView(e, new LinearLayout.LayoutParams(0, -2, 1));
                row.addView(Neon.btn(a, "OK", 12, true, x -> Api.get("/api/set?k=" + Api.enc(key) + "&v=" + Api.enc(e.getText().toString()), r -> {
                    Toast.makeText(a, r != null ? "Enregistré" : "Refusé", Toast.LENGTH_SHORT).show(); load(); })), lp(-2, -2, 0));
                box.addView(row);
            }
        }
        scroll.post(() -> scroll.scrollTo(0, y));
    }

    private void checks(JSONArray arr, final String w, String title) {
        if (arr == null || arr.length() == 0) return;
        TextView h = Neon.glow(a, title, 15, Neon.PINK);
        h.setPadding(dp(4), dp(16), 0, dp(6));
        box.addView(h);
        for (int k = 0; k < arr.length(); k++) {
            JSONArray x = arr.optJSONArray(k);
            if (x == null) continue;
            final int i = k;
            android.widget.CheckBox cb = new android.widget.CheckBox(a);
            cb.setText(x.optString(0));
            cb.setTextColor(Neon.TEXT);
            cb.setButtonTintList(android.content.res.ColorStateList.valueOf(Neon.CYAN));
            cb.setChecked(x.optInt(1) != 0);
            cb.setEnabled(x.optInt(2) == 0);
            cb.setOnClickListener(vv -> Api.get("/api/check?w=" + w + "&i=" + i, r -> load()));
            box.addView(cb);
        }
    }

    private void step(int i, int d) { Api.get("/api/menu?i=" + i + "&d=" + d, r -> box.postDelayed(this::load, 200)); }
}

// ---------------------------------------------------------------- Cartes memoire virtuelles
class CardsTab extends Tab {
    static final int REQ_GCI = 8, REQ_RAW = 9;
    static String pendingCard;
    private LinearLayout box;
    private TextView status;
    CardsTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        status = UiKit.hint(a, "Cartes mémoire virtuelles (Pulse)");
        v.addView(status);
        ScrollView sc = new ScrollView(a);
        box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(0, 0, 0, dp(16));
        sc.addView(box);
        v.addView(sc, lp(-1, 0, 1));
        root = v;
        return v;
    }

    @Override void show() { load(); }

    void load() {
        Neon.set(status, "Lecture des cartes…");
        Api.get("/api/cards", r -> {
            if (r == null) { Neon.set(status, "Pas de réponse de la console"); return; }
            box.removeAllViews();
            try {
                JSONArray j = new JSONArray(r);
                Neon.set(status, j.length() == 0 ? "Aucune carte virtuelle (Pulse en crée une au premier jeu)" : "Toucher une sauvegarde : la récupérer (.gci) ou l'effacer");
                for (int k = 0; k < j.length(); k++) card(j.getJSONObject(k));
            } catch (Exception e) { Neon.set(status, "Réponse illisible"); }
        });
    }

    private void card(final JSONObject c) {
        final String f = c.optString("f");
        LinearLayout cd = UiKit.card(a);
        cd.addView(Neon.glow(a, f + (c.optInt("a") != 0 ? "  · Pulse" : ""), 15, Neon.PINK));
        LinearLayout row = new LinearLayout(a);
        row.setPadding(0, dp(8), 0, dp(6));
        if (c.optInt("x") != 0) row.addView(Neon.btn(a, "Télécharger", 12, false, x -> UiKit.download(a, "/api/cardfile/" + Uri.encode(f), f)), lp(0, -2, 1));
        if (c.optInt("ok") != 0 || c.optInt("bl") != 0) { row.addView(gap()); row.addView(Neon.btn(a, "+ Sauvegarde", 12, true, x -> pick(f, REQ_GCI)), lp(0, -2, 1)); }
        row.addView(gap());
        row.addView(Neon.btn(a, "Remplacer", 12, false, x -> NeonDialog.confirm(a, "Remplacer " + f + " ?", "Choisis une image de carte (.raw). L'ancienne est gardée en .bak sur la console.", "Choisir", () -> pick(f, REQ_RAW))), lp(0, -2, 1));
        cd.addView(row);
        if (c.optInt("ok") == 0) {
            if (!c.optString("w").isEmpty()) cd.addView(UiKit.hint(a, c.optString("w")));
        } else {
            JSONArray e = c.optJSONArray("e");
            cd.addView(UiKit.hint(a, (e != null ? e.length() : 0) + " sauvegarde(s), " + c.optInt("free") + " blocs libres sur " + c.optInt("b")));
            for (int i = 0; e != null && i < e.length(); i++) {
                final JSONArray s = e.optJSONArray(i);
                if (s == null) continue;
                final int idx = i;
                LinearLayout it = new LinearLayout(a);
                it.setOrientation(LinearLayout.VERTICAL);
                it.setBackground(new android.graphics.drawable.InsetDrawable(Neon.button(a, 12, false), 0, dp(3), 0, dp(3)));
                it.setPadding(dp(12), dp(10), dp(12), dp(10));
                it.addView(Neon.text(a, s.optString(0), 14, Neon.TEXT, true));
                it.addView(Neon.text(a, s.optString(2) + " · " + s.optInt(4) + " blocs" + (s.optString(1).isEmpty() ? "" : " · " + s.optString(1)), 12, Neon.DIM, false));
                it.setOnClickListener(x -> saveMenu(a, f, idx, s, this::load));
                cd.addView(it);
            }
        }
        box.addView(cd, UiKit.gapBelow(a, 12));
    }

    private View gap() { View g = new View(a); g.setLayoutParams(new LinearLayout.LayoutParams(dp(6), 1)); return g; }

    /** une sauvegarde : la recuperer (.gci) ou l'effacer de la carte virtuelle (confirmation) */
    static void saveMenu(final Activity a, final String card, final int idx, final JSONArray s, final Runnable reload) {
        final String nm = s.optString(3);
        new NeonDialog(a).title(s.optString(0)).message(s.optString(2) + " · " + s.optInt(4) + " blocs · " + card)
            .items(new String[] { "Télécharger (.gci)", "Effacer de la carte" }, new int[] { 1 }, w -> {
                if (w == 0) UiKit.download(a, "/api/gci/" + Uri.encode(card) + "/" + idx + "/" + Uri.encode(nm) + ".gci", nm + ".gci");
                else NeonDialog.confirm(a, "Effacer « " + s.optString(0) + " » ?", "La sauvegarde est retirée de " + card + ". Récupère-la d'abord (.gci) si tu veux la garder.", "Effacer", () ->
                    Api.get("/api/gcidel?c=" + Api.enc(card) + "&i=" + idx + "&n=" + Api.enc(nm), r -> {
                        String m = "Pas de réponse (console à mettre à jour ?)";
                        try { if (r != null) m = new JSONObject(r).optString("m", m); } catch (Exception e) { }
                        Toast.makeText(a, m, Toast.LENGTH_LONG).show();
                        if (reload != null) reload.run();
                    }));
            }).cancel("Fermer").show();
    }

    /** fiche du jeu : ses sauvegardes, sur ses cartes virtuelles (carte-A-<ID>.raw, carte-B-<ID>.raw) */
    static void gameSaves(final Activity a, final String id, final LinearLayout into) {
        into.removeAllViews();
        into.addView(UiKit.hint(a, "Lecture des cartes…"));
        Api.get("/api/cards", r -> {
            into.removeAllViews();
            int n = 0;
            try {
                JSONArray j = new JSONArray(r);
                for (int k = 0; k < j.length(); k++) {
                    JSONObject c = j.getJSONObject(k);
                    final String f = c.optString("f");
                    if (!f.toUpperCase(java.util.Locale.ROOT).contains(id.toUpperCase(java.util.Locale.ROOT))) continue;
                    JSONArray e = c.optJSONArray("e");
                    for (int i = 0; e != null && i < e.length(); i++) {
                        final JSONArray s = e.optJSONArray(i);
                        if (s == null) continue;
                        final int idx = i;
                        TextView t = Neon.text(a, s.optString(0) + "\n" + f + " · " + s.optInt(4) + " blocs" + (s.optString(1).isEmpty() ? "" : " · " + s.optString(1)), 13, Neon.TEXT, false);
                        t.setBackground(new android.graphics.drawable.InsetDrawable(Neon.button(a, 12, false), 0, Neon.dp(a, 3), 0, Neon.dp(a, 3)));
                        t.setPadding(Neon.dp(a, 12), Neon.dp(a, 10), Neon.dp(a, 12), Neon.dp(a, 10));
                        t.setOnClickListener(v -> saveMenu(a, f, idx, s, () -> gameSaves(a, id, into)));
                        into.addView(t);
                        n++;
                    }
                }
            } catch (Exception ex) { into.addView(UiKit.hint(a, r == null ? "Pas de réponse de la console" : "Réponse illisible")); return; }
            if (n == 0) into.addView(UiKit.hint(a, "Aucune sauvegarde de ce jeu sur une carte virtuelle"));
        });
    }

    private void pick(String card, int req) {
        pendingCard = card;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try { a.startActivityForResult(i, req); } catch (Exception e) { Toast.makeText(a, "Pas de sélecteur de fichiers", Toast.LENGTH_LONG).show(); }
    }
}

// ---------------------------------------------------------------- Salon
class SalonTab extends Tab {
    private LinearLayout box;
    private TextView status;
    private long last;
    SalonTab(Activity a) { super(a); }

    @Override View build() {
        LinearLayout v = UiKit.column(a);
        status = UiKit.hint(a, "Les autres GC Station de la maison");
        v.addView(status);
        ScrollView sc = new ScrollView(a);
        box = new LinearLayout(a);
        box.setOrientation(LinearLayout.VERTICAL);
        sc.addView(box);
        v.addView(sc, lp(-1, 0, 1));
        root = v;
        return v;
    }

    @Override void show() { load(); }
    @Override void state(JSONObject s) { if (root != null && root.isShown() && System.currentTimeMillis() - last > 4000) load(); }

    private void load() {
        last = System.currentTimeMillis();
        Api.get("/api/salon", r -> {
            if (r == null) { Neon.set(status, "Pas de réponse de la console"); return; }
            try {
                JSONObject j = new JSONObject(r);
                JSONArray c = j.optJSONArray("c");
                Neon.set(status, j.optInt("on") == 0 ? "Le Salon est coupé (START > Réglages > Réseau > Salon)" : c == null || c.length() == 0 ? "Aucune autre GC Station pour l'instant" : "Toucher « Écouter avec » pour la même musique sur ta télé");
                box.removeAllViews();
                for (int i = 0; c != null && i < c.length(); i++) {
                    JSONArray x = c.getJSONArray(i);
                    final int idx = i;
                    final String ip = x.optString(1);
                    LinearLayout cd = UiKit.card(a);
                    cd.addView(Neon.glow(a, x.optString(0), 16, Neon.CYAN));
                    cd.addView(Neon.text(a, "♪ " + (x.optString(2).isEmpty() ? "rien" : x.optString(2)), 13, Neon.TEXT, false));
                    cd.addView(UiKit.hint(a, ip));
                    LinearLayout row = new LinearLayout(a);
                    row.addView(Neon.btn(a, "Écouter avec", 13, true, v -> Api.get("/api/listen?i=" + idx, rr -> Neon.set(status, rr != null ? "Écoute lancée sur la télé" : "Refusé"))), lp(0, -2, 1));
                    View g = new View(a); row.addView(g, new LinearLayout.LayoutParams(dp(8), 1));
                    row.addView(Neon.btn(a, "Piloter celle-ci", 13, false, v -> ((MainActivity) a).switchTo(ip)), lp(0, -2, 1));
                    cd.addView(row);
                    box.addView(cd, UiKit.gapBelow(a, 12));
                }
            } catch (Exception e) { Neon.set(status, "Réponse illisible"); }
        });
    }
}

// ---------------------------------------------------------------- Outils : memoire, capture, rapport
class ToolsTab extends Tab {
    private TextView memTxt, bootTxt, journal, testTxt;
    private long lastTest;
    private Spark spark;
    private ImageView shot;
    private Bitmap shotBm;
    private long last;
    ToolsTab(Activity a) { super(a); }

    @Override View build() {
        ScrollView sc = new ScrollView(a);
        LinearLayout v = UiKit.column(a);
        v.setPadding(dp(16), dp(4), dp(16), dp(16));
        LinearLayout mem = UiKit.card(a);
        mem.addView(Neon.glow(a, "Mémoire de la console", 15, Neon.PINK));
        memTxt = Neon.text(a, "…", 13, Neon.TEXT, false);
        memTxt.setPadding(0, dp(6), 0, dp(6));
        mem.addView(memTxt);
        spark = new Spark(a);
        mem.addView(spark, new LinearLayout.LayoutParams(-1, dp(70)));
        bootTxt = Neon.text(a, "", 12, Neon.DIM, false);
        bootTxt.setPadding(0, dp(6), 0, 0);
        mem.addView(bootTxt);
        v.addView(mem, UiKit.gapBelow(a, 12));

        LinearLayout cap = UiKit.card(a);
        cap.addView(Neon.glow(a, "Capture de la télé", 15, Neon.PINK));
        LinearLayout row = new LinearLayout(a);
        row.setPadding(0, dp(8), 0, dp(8));
        row.addView(Neon.btn(a, "Capturer", 13, true, x -> capture()), lp(0, -2, 1));
        View g = new View(a); row.addView(g, new LinearLayout.LayoutParams(dp(8), 1));
        row.addView(Neon.btn(a, "Enregistrer", 13, false, x -> saveShot()), lp(0, -2, 1));
        cap.addView(row);
        shot = new ImageView(a);
        shot.setAdjustViewBounds(true);
        cap.addView(shot, new LinearLayout.LayoutParams(-1, -2));
        v.addView(cap, UiKit.gapBelow(a, 12));

        LinearLayout tst = UiKit.card(a);                   // (console build 764 : lancer un test de la console depuis ici)
        tst.addView(Neon.glow(a, "Test de la console", 15, Neon.PINK));
        testTxt = Neon.text(a, "…", 13, Neon.TEXT, false);
        testTxt.setPadding(0, dp(6), 0, dp(4));
        tst.addView(testTxt);
        String[][] tests = { { "Rapide", "0", "1 minute" }, { "Complet", "1", "environ 5 minutes" }, { "Total", "2", "environ 15 minutes : tout le matériel et tout GC Station" },
                             { "Touches", "3", "3 heures au plus : chaque touche dans chaque écran" } };
        LinearLayout tr = null;
        for (int i = 0; i < tests.length; i++) {
            if (i % 2 == 0) { tr = new LinearLayout(a); tr.setPadding(0, dp(4), 0, dp(4)); tst.addView(tr); }
            final String[] t = tests[i];
            if (i % 2 == 1) { View gg = new View(a); tr.addView(gg, new LinearLayout.LayoutParams(dp(8), 1)); }
            tr.addView(Neon.btn(a, t[0], 13, i == 3, x -> NeonDialog.confirm(a, "Test " + t[0].toLowerCase(java.util.Locale.FRANCE) + " ?",
                "Durée : " + t[2] + ". GC Station se manipule tout seul sur la télé ; ne touche pas la manette (B : arrêter). Rien n'est effacé et tes réglages sont remis à la fin.",
                "Lancer", () -> Api.get("/api/test?m=" + t[1], r -> testState(r)))), lp(0, -2, 1));
        }
        tst.addView(Neon.btn(a, "Voir le dernier rapport", 13, false, x -> Api.get("/test.txt", r -> journal.setText(r != null ? r : "Pas encore de rapport"))), lp(-1, -2, 0));
        v.addView(tst, UiKit.gapBelow(a, 12));

        LinearLayout rep = UiKit.card(a);
        rep.addView(Neon.glow(a, "Rapport de bug", 15, Neon.PINK));
        LinearLayout r2 = new LinearLayout(a);
        r2.setPadding(0, dp(8), 0, dp(8));
        r2.addView(Neon.btn(a, "Lire le journal", 13, false, x -> loadJournal()), lp(0, -2, 1));
        View g2 = new View(a); r2.addView(g2, new LinearLayout.LayoutParams(dp(8), 1));
        r2.addView(Neon.btn(a, "Envoyer le rapport", 13, true, x -> shareReport()), lp(0, -2, 1));
        rep.addView(r2);
        journal = Neon.text(a, "", 11, Neon.DIM, false);
        journal.setTypeface(android.graphics.Typeface.MONOSPACE);
        journal.setTextIsSelectable(true);
        rep.addView(journal);
        v.addView(rep);
        sc.addView(v);
        root = sc;
        return sc;
    }

    @Override void show() { mem(); test(); }
    @Override void state(JSONObject s) {
        if (root == null || !root.isShown()) return;
        if (System.currentTimeMillis() - last > 2500) mem();
        if (System.currentTimeMillis() - lastTest > 3000) test();
    }

    private void test() { lastTest = System.currentTimeMillis(); Api.get("/api/test", this::testState); }
    private void testState(String r) {
        if (testTxt == null) return;
        if (r == null) { Neon.set(testTxt, "La console ne répond pas (ou elle n'a pas encore le build 764)"); return; }
        try {
            JSONObject j = new JSONObject(r);
            String t = j.optString("txt");
            Neon.set(testTxt, j.optInt("run") != 0 ? "En cours : " + t : t.isEmpty() ? "Aucun test lancé depuis l'allumage" : t);
        } catch (Exception e) { }
    }

    private void mem() {
        last = System.currentTimeMillis();
        Api.get("/api/memoire", r -> {
            if (r == null) return;
            try {
                JSONObject j = new JSONObject(r);
                long free = j.optLong("free"), used = j.optLong("used"), big = j.optLong("big");
                Neon.set(memTxt, String.format(java.util.Locale.FRANCE, "Libre %.1f Mo · utilisée %.1f Mo%s\nBuild %d · allumée depuis %d min",
                        free / 1048576.0, used / 1048576.0, big > 0 ? String.format(java.util.Locale.FRANCE, " · plus grand bloc %.1f Mo", big / 1048576.0) : "",
                        j.optInt("build"), j.optInt("up") / 60));
                JSONArray h = j.optJSONArray("hist");
                if (h != null) { float[] f = new float[h.length()]; for (int i = 0; i < f.length; i++) f[i] = h.optInt(i); spark.set(f); }
                JSONArray st = j.optJSONArray("steps");
                String longest = ""; int lm = -1;
                for (int i = 0; st != null && i < st.length(); i++) { JSONArray x = st.optJSONArray(i); if (x != null && x.optInt(1) > lm) { lm = x.optInt(1); longest = x.optString(0); } }
                Neon.set(bootTxt, "Démarrage : " + String.format(java.util.Locale.FRANCE, "%.1f s", j.optInt("boot") / 1000.0) + (lm >= 0 ? "  (le plus long : " + longest + ", " + lm + " ms)" : ""));
            } catch (Exception e) { }
        });
    }

    private void capture() {
        Toast.makeText(a, "Capture…", Toast.LENGTH_SHORT).show();
        Api.bytes("/capture.bmp", b -> {
            Bitmap bm = b == null ? null : BitmapFactory.decodeByteArray(b, 0, b.length);
            if (bm == null) { Toast.makeText(a, "Capture impossible", Toast.LENGTH_LONG).show(); return; }
            shotBm = bm;
            shot.setImageBitmap(bm);
        });
    }

    private Uri saveShot() {
        if (shotBm == null) { Toast.makeText(a, "Capture d'abord", Toast.LENGTH_SHORT).show(); return null; }
        try {
            ContentValues cv = new ContentValues();
            cv.put(MediaStore.Images.Media.DISPLAY_NAME, "gc-station-" + System.currentTimeMillis() + ".png");
            cv.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
            if (android.os.Build.VERSION.SDK_INT >= 29) cv.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/GC Station");
            Uri u = a.getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, cv);
            try (OutputStream o = a.getContentResolver().openOutputStream(u)) { shotBm.compress(Bitmap.CompressFormat.PNG, 100, o); }
            Toast.makeText(a, "Enregistrée dans Images > GC Station", Toast.LENGTH_SHORT).show();
            return u;
        } catch (Exception e) { Toast.makeText(a, "Enregistrement impossible", Toast.LENGTH_LONG).show(); return null; }
    }

    private void loadJournal() {
        Api.get("/api/journal", r -> journal.setText(r != null ? r : "Pas de réponse de la console"));
    }

    private void shareReport() {
        Api.get("/api/journal", r -> {
            String txt = "Rapport GC Station\n\n" + (r != null ? r : "(journal illisible)");
            Intent i = new Intent(shotBm != null ? Intent.ACTION_SEND : Intent.ACTION_SEND);
            i.setType(shotBm != null ? "image/png" : "text/plain");
            i.putExtra(Intent.EXTRA_TEXT, txt);
            i.putExtra(Intent.EXTRA_SUBJECT, "Rapport GC Station");
            if (shotBm != null) { Uri u = saveShot(); if (u != null) { i.putExtra(Intent.EXTRA_STREAM, u); i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION); } else i.setType("text/plain"); }
            try { a.startActivity(Intent.createChooser(i, "Envoyer le rapport")); } catch (Exception e) { }
        });
    }
}

/** petite courbe neon (memoire libre des 2 dernieres minutes) */
class Spark extends View {
    private float[] v = new float[0];
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG), fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path p = new Path(), f = new Path();
    Spark(Context c) {
        super(c);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeWidth(Neon.dp(c, 2));
        line.setColor(Neon.CYAN);
        line.setShadowLayer(Neon.dp(c, 4), 0, 0, Neon.CYAN);
        setLayerType(LAYER_TYPE_SOFTWARE, null);               // (petite vue : le halo de la courbe)
    }
    void set(float[] x) { v = x; invalidate(); }
    @Override protected void onDraw(Canvas c) {
        if (v.length < 2) return;
        float mn = Float.MAX_VALUE, mx = 0;
        for (float x : v) { mn = Math.min(mn, x); mx = Math.max(mx, x); }
        if (mx - mn < 1) { mx += 1; mn -= 1; }
        int w = getWidth(), h = getHeight(), pad = Neon.dp(getContext(), 4);
        p.reset(); f.reset();
        for (int i = 0; i < v.length; i++) {
            float x = i * (w - 1f) / (v.length - 1), y = pad + (h - 2 * pad) * (1 - (v[i] - mn) / (mx - mn));
            if (i == 0) { p.moveTo(x, y); f.moveTo(x, h); f.lineTo(x, y); } else { p.lineTo(x, y); f.lineTo(x, y); }
        }
        f.lineTo(w, h); f.close();
        fill.setShader(new LinearGradient(0, 0, 0, h, Neon.alpha(Neon.PINK, 0x66), Neon.alpha(Neon.PINK, 0), Shader.TileMode.CLAMP));
        c.drawPath(f, fill);
        c.drawPath(p, line);
    }
}

// ---------------------------------------------------------------- Plus : les autres ecrans, en tuiles neon
class PlusTab extends Tab {
    private static final String[][] TILES = {
        { "▣", "Fichiers", "La carte SD", "5" }, { "◈", "Jeux", "Jaquettes, lancer", "6" },
        { "▧", "Photos", "Sur la télé", "7" }, { "☰", "File d'attente", "À suivre", "8" },
        { "⚙", "Réglages", "De GC Station", "9" }, { "▤", "Cartes mémoire", "Sauvegardes", "10" },
        { "⌂", "Salon", "Les autres consoles", "11" }, { "⚒", "Outils", "Mémoire, capture, rapport", "12" },
        { "◎", "Recherche", "Musique, radios, jeux", "13" }, { "▦", "Copies", "Cartes et réglages gardés", "14" },
        { "♠", "Jeu de cartes", "8 américain entre potes", "-1" } };
    PlusTab(Activity a) { super(a); }

    @Override View build() {
        ScrollView sc = new ScrollView(a);
        android.widget.GridLayout g = new android.widget.GridLayout(a);
        g.setColumnCount(2);
        g.setPadding(dp(12), dp(4), dp(12), dp(16));
        int[] col = { Neon.PINK, Neon.CYAN, Neon.VIOLET, Neon.BLUE };
        for (int i = 0; i < TILES.length; i++) {
            final String[] t = TILES[i];
            LinearLayout c = new LinearLayout(a);
            c.setOrientation(LinearLayout.VERTICAL);
            c.setGravity(Gravity.CENTER);
            c.setPadding(dp(10), dp(18), dp(10), dp(16));
            c.setBackground(Neon.button(a, 22, false));
            c.setClickable(true);
            int cc = col[i % 4];
            TextView ic = Neon.glow(a, t[0], 30, cc);
            ic.setGravity(Gravity.CENTER);
            c.addView(ic);
            TextView n = Neon.text(a, t[1], 15, Neon.TEXT, true);
            n.setGravity(Gravity.CENTER);
            n.setPadding(0, dp(8), 0, dp(2));
            c.addView(n);
            TextView d = Neon.text(a, t[2], 11, Neon.DIM, false);
            d.setGravity(Gravity.CENTER);
            c.addView(d);
            c.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); if (t[3].equals("-1")) CartesActivity.open(a); else ((MainActivity) a).select(Integer.parseInt(t[3])); });
            android.widget.GridLayout.LayoutParams p = new android.widget.GridLayout.LayoutParams(
                android.widget.GridLayout.spec(i / 2), android.widget.GridLayout.spec(i % 2, 1f));
            p.width = 0;
            p.setMargins(dp(6), dp(6), dp(6), dp(6));
            g.addView(c, p);
        }
        sc.addView(g);
        root = sc;
        return sc;
    }
}
