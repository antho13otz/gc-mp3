package fr.gcstation.telecommande;

// Theme "Neon retro" de GC Station (themes/index.txt) : rose fluo et cyan, aurore rose -> violet -> bleu -> cyan,
// fond presque noir violace. Petites fabriques de vues : cartes en verre, boutons neon, textes lumineux.

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.graphics.drawable.StateListDrawable;
import android.content.res.ColorStateList;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.widget.TextView;

final class Neon {
    static final int PINK = 0xFFFF2FD0;
    static final int VIOLET = 0xFFB03CFF;
    static final int BLUE = 0xFF5A5CFF;
    static final int CYAN = 0xFF21E8FF;
    static final int BG_TOP = 0xFF0C0418;
    static final int BG_BOTTOM = 0xFF04010A;
    static final int TEXT = 0xFFF4EEFF;
    static final int DIM = 0xFFB7A8D8;
    static final int FAINT = 0xFF6E6290;
    static final int GLASS = 0x1CFFFFFF;
    static final int GLASS_EDGE = 0x33FFFFFF;
    static final int[] AURORA = { PINK, VIOLET, BLUE, CYAN };

    private Neon() { }

    static int dp(Context c, float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, c.getResources().getDisplayMetrics());
    }

    static int alpha(int color, int a) { return (color & 0x00FFFFFF) | (a << 24); }

    /** texte change seulement s'il est different (sinon Android refait la mise en page pour rien) */
    static void set(TextView t, CharSequence s) { if (!android.text.TextUtils.equals(t.getText(), s)) t.setText(s); }

    /** carte en verre : fond blanc tres leger, bord fin */
    static Drawable glass(Context c, float radius) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(GLASS);
        g.setCornerRadius(dp(c, radius));
        g.setStroke(dp(c, 1), GLASS_EDGE);
        return g;
    }

    /** degrade rose -> cyan (barre active, boutons principaux) */
    static GradientDrawable neonFill(Context c, float radius) {
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, new int[] { PINK, VIOLET, CYAN });
        g.setCornerRadius(dp(c, radius));
        return g;
    }

    /** bouton : verre au repos, rose lumineux quand on appuie (et ondulation) */
    static Drawable button(Context c, float radius, boolean primary) {
        GradientDrawable idle;
        if (primary) idle = neonFill(c, radius);
        else {
            idle = new GradientDrawable();
            idle.setColor(GLASS);
            idle.setCornerRadius(dp(c, radius));
            idle.setStroke(dp(c, 1.2f), alpha(PINK, 0x88));
        }
        GradientDrawable down = new GradientDrawable();
        down.setColor(alpha(PINK, 0x66));
        down.setCornerRadius(dp(c, radius));
        down.setStroke(dp(c, 1.5f), CYAN);
        StateListDrawable s = new StateListDrawable();
        s.addState(new int[] { android.R.attr.state_pressed }, down);
        s.addState(new int[] { }, idle);
        GradientDrawable mask = new GradientDrawable();
        mask.setColor(Color.WHITE);
        mask.setCornerRadius(dp(c, radius));
        return new RippleDrawable(ColorStateList.valueOf(alpha(CYAN, 0x55)), s, mask);
    }

    static TextView text(Context c, String s, float sp, int color, boolean bold) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        return t;
    }

    /** titre avec halo neon */
    static TextView glow(Context c, String s, float sp, int color) {
        TextView t = text(c, s, sp, color, true);
        t.setShadowLayer(dp(c, 10), 0, 0, color);
        t.setLetterSpacing(0.06f);
        return t;
    }

    /** bouton texte (ou symbole) neon, avec vibration courte */
    static TextView btn(Context c, String label, float sp, boolean primary, View.OnClickListener l) {
        TextView b = text(c, label, sp, primary ? Color.WHITE : TEXT, true);
        b.setGravity(Gravity.CENTER);
        b.setBackground(button(c, 16, primary));
        b.setPadding(dp(c, 14), dp(c, 10), dp(c, 14), dp(c, 10));
        b.setClickable(true);
        b.setOnClickListener(v -> { v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); if (l != null) l.onClick(v); });
        return b;
    }
}
