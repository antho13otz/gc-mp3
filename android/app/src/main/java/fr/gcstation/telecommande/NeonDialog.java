package fr.gcstation.telecommande;

// Fenetres de l'appli au theme "Neon retro" (a la place des boites grises d'Android) : carte sombre en verre,
// bord degrade rose -> violet -> cyan, titre lumineux, fond flou derriere (Android 12 et plus), entree animee.
//   new NeonDialog(a).title("...").message("...").view(v).items(new String[]{...}, w -> ...)
//       .button("OK", true, () -> ...).cancel("Annuler").show();

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.os.Build;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.OvershootInterpolator;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

final class NeonDialog {
    interface Pick { void on(int which); }
    interface Text { void on(String v); }

    private final Activity a;
    private final Dialog d;
    private final LinearLayout card, body, buttons;
    private TextView titleV;
    private boolean stacked;

    NeonDialog(Activity a) {
        this.a = a;
        d = new Dialog(a);
        d.requestWindowFeature(Window.FEATURE_NO_TITLE);
        card = new LinearLayout(a);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(frame(a, 26));
        card.setPadding(dp(22), dp(20), dp(22), dp(16));
        card.setElevation(dp(12));
        body = new LinearLayout(a);
        body.setOrientation(LinearLayout.VERTICAL);
        ScrollView sc = new ScrollView(a);
        sc.setVerticalScrollBarEnabled(false);
        sc.setOverScrollMode(View.OVER_SCROLL_NEVER);
        sc.addView(body);
        card.addView(sc, new LinearLayout.LayoutParams(-1, 0, 1));
        buttons = new LinearLayout(a);
        buttons.setGravity(Gravity.END);
        LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(-1, -2);
        bl.topMargin = dp(16);
        card.addView(buttons, bl);
        buttons.setVisibility(View.GONE);
    }

    private int dp(float v) { return Neon.dp(a, v); }

    /** fond de carte : bord degrade neon (1,5 dp) autour d'un verre sombre */
    static Drawable frame(Activity a, float r) {
        GradientDrawable edge = new GradientDrawable(GradientDrawable.Orientation.TL_BR, new int[] { Neon.PINK, Neon.VIOLET, Neon.BLUE, Neon.CYAN });
        edge.setCornerRadius(Neon.dp(a, r));
        GradientDrawable in = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, new int[] { 0xF51A0B33, 0xF80A0416 });
        in.setCornerRadius(Neon.dp(a, r) - Neon.dp(a, 1.5f));
        LayerDrawable l = new LayerDrawable(new Drawable[] { edge, in });
        int e = Neon.dp(a, 1.5f);
        l.setLayerInset(1, e, e, e, e);
        return l;
    }

    NeonDialog title(String t) {
        titleV = Neon.glow(a, t, 20, Neon.PINK);
        titleV.setMaxLines(3);
        titleV.setEllipsize(TextUtils.TruncateAt.END);
        card.addView(titleV, 0);
        View bar = new View(a);
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] { Neon.PINK, Neon.CYAN, Neon.alpha(Neon.CYAN, 0) });
        g.setCornerRadius(dp(2));
        bar.setBackground(g);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(dp(90), dp(3));
        p.topMargin = dp(8); p.bottomMargin = dp(12);
        card.addView(bar, 1, p);
        return this;
    }

    NeonDialog message(CharSequence m) {
        TextView t = Neon.text(a, "", 15, Neon.DIM, false);
        t.setText(Lang.t(m));
        t.setLineSpacing(0, 1.15f);
        body.addView(t);
        return this;
    }

    NeonDialog view(View v) { body.addView(v, new LinearLayout.LayoutParams(-1, -2)); return this; }

    /** liste de choix : lignes en verre, la fenetre se ferme au choix */
    NeonDialog items(String[] items, Pick p) { return items(items, null, p); }
    NeonDialog items(String[] items, int[] danger, Pick p) {
        for (int i = 0; i < items.length; i++) {
            final int w = i;
            boolean bad = false;
            if (danger != null) for (int k : danger) if (k == i) bad = true;
            TextView r = Neon.text(a, items[i], 16, bad ? 0xFFFF6B8E : Neon.TEXT, true);
            r.setBackground(Neon.button(a, 14, false));
            r.setPadding(dp(16), dp(14), dp(16), dp(14));
            r.setClickable(true);
            r.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); d.dismiss(); p.on(w); });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
            lp.topMargin = i == 0 ? 0 : dp(8);
            body.addView(r, lp);
        }
        return this;
    }

    /** boutons du bas ; au-dela de deux, ils passent les uns sous les autres (pleine largeur) */
    NeonDialog button(String label, boolean primary, Runnable r) { return button(label, primary, true, r); }
    NeonDialog button(String label, boolean primary, boolean close, Runnable r) {
        buttons.setVisibility(View.VISIBLE);
        TextView b = Neon.btn(a, label, 14, primary, v -> { if (close) d.dismiss(); if (r != null) r.run(); });
        b.setMaxLines(1);
        b.setEllipsize(TextUtils.TruncateAt.END);
        buttons.addView(b, new LinearLayout.LayoutParams(-2, -2));
        relayout();
        return this;
    }
    NeonDialog cancel(String label) { return button(label, false, null); }

    private void relayout() {
        int n = buttons.getChildCount();
        stacked = n > 2;
        buttons.setOrientation(stacked ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
        for (int i = 0; i < n; i++) {
            LinearLayout.LayoutParams p = stacked ? new LinearLayout.LayoutParams(-1, -2) : new LinearLayout.LayoutParams(0, -2, 1);
            if (stacked) p.topMargin = i == 0 ? 0 : dp(8);
            else p.leftMargin = i == 0 ? 0 : dp(10);
            buttons.getChildAt(i).setLayoutParams(p);
        }
    }

    NeonDialog onDismiss(Runnable r) { d.setOnDismissListener(x -> r.run()); return this; }
    void dismiss() { d.dismiss(); }

    NeonDialog show() {
        if (stacked) {                                        // (empiles : le dernier ajoute, le principal, en haut)
            int n = buttons.getChildCount();
            View[] v = new View[n];
            for (int i = 0; i < n; i++) v[i] = buttons.getChildAt(i);
            buttons.removeAllViews();
            for (int i = n - 1; i >= 0; i--) buttons.addView(v[i]);
            for (int i = 0; i < n; i++) ((LinearLayout.LayoutParams) buttons.getChildAt(i).getLayoutParams()).topMargin = i == 0 ? 0 : dp(8);
        }
        LinearLayout outer = new LinearLayout(a);
        outer.setGravity(Gravity.CENTER);
        outer.setPadding(dp(20), dp(40), dp(20), dp(40));
        outer.addView(card, new LinearLayout.LayoutParams(Math.min(a.getResources().getDisplayMetrics().widthPixels - dp(40), dp(460)), -2));
        d.setContentView(outer, new ViewGroup.LayoutParams(-1, -1));
        Window w = d.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            w.setLayout(-1, -1);
            w.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            w.setDimAmount(0.62f);
            if (Build.VERSION.SDK_INT >= 31) {
                w.addFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND);
                w.getAttributes().setBlurBehindRadius(dp(18));
            }
            w.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        }
        outer.setOnClickListener(v -> d.dismiss());          // (toucher a cote = fermer)
        card.setClickable(true);
        card.setAlpha(0f); card.setScaleX(0.9f); card.setScaleY(0.9f); card.setTranslationY(dp(24));
        d.show();
        card.animate().alpha(1f).scaleX(1f).scaleY(1f).translationY(0).setDuration(240).setInterpolator(new OvershootInterpolator(1.1f)).start();
        return this;
    }

    // ---------- raccourcis ----------
    static void confirm(Activity a, String title, String msg, String yes, Runnable r) {
        new NeonDialog(a).title(title).message(msg == null ? "" : msg).cancel("Annuler").button(yes, true, r).show();
    }

    static void ask(Activity a, String title, String init, Text t) {
        final EditText f = new EditText(a);
        f.setText(init); // NOLANG
        f.setSingleLine(true);
        f.setTextColor(Neon.TEXT);
        f.setHintTextColor(Neon.FAINT);
        f.setTextSize(16);
        f.setBackground(Neon.glass(a, 14));
        f.setPadding(Neon.dp(a, 14), Neon.dp(a, 12), Neon.dp(a, 14), Neon.dp(a, 12));
        f.setSelection(f.getText().length());
        NeonDialog nd = new NeonDialog(a).title(title).view(f).cancel("Annuler").button("OK", true, () -> t.on(f.getText().toString().trim()));
        nd.show();
        f.requestFocus();
        Window w = nd.d.getWindow();
        if (w != null) w.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    }

    /** interrupteur neon (pilule) pour les listes a cocher */
    static View toggle(Activity a, boolean on) {
        View v = new View(a);
        paintToggle(a, v, on);
        return v;
    }
    static void paintToggle(Activity a, View v, boolean on) {
        GradientDrawable track = on ? Neon.neonFill(a, 14) : new GradientDrawable();
        if (!on) { track.setColor(0x22FFFFFF); track.setCornerRadius(Neon.dp(a, 14)); track.setStroke(Neon.dp(a, 1), Neon.alpha(Neon.DIM, 0x66)); }
        GradientDrawable knob = new GradientDrawable();
        knob.setShape(GradientDrawable.OVAL);
        knob.setColor(on ? Color.WHITE : Neon.DIM);
        LayerDrawable l = new LayerDrawable(new Drawable[] { track, knob });
        int s = Neon.dp(a, 4), k = Neon.dp(a, 20), wdt = Neon.dp(a, 48);
        l.setLayerInset(1, on ? wdt - k - s : s, s, on ? s : wdt - k - s, s);
        v.setBackground(l);
    }
}
