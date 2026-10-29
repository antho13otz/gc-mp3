package fr.gcstation.telecommande;

// Fond anime : nuit violacee et rubans d'aurore neon (rose, violet, bleu, cyan) qui ondulent lentement,
// comme l'Aurore de GC Station. Dessin simple (degrades + flou), ~30 images/s seulement quand l'appli est visible.

import android.content.Context;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Shader;
import android.view.View;

class AuroraView extends View {
    private final Paint bg = new Paint();
    private final Paint ribbon = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint star = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final long t0 = System.currentTimeMillis();
    private final float[] sx = new float[60], sy = new float[60], ss = new float[60];
    private boolean running = true;

    AuroraView(Context c) {
        super(c);
        setLayerType(LAYER_TYPE_SOFTWARE, null);             // (flou des rubans)
        ribbon.setStyle(Paint.Style.STROKE);
        ribbon.setStrokeCap(Paint.Cap.ROUND);
        java.util.Random r = new java.util.Random(7);
        for (int i = 0; i < sx.length; i++) { sx[i] = r.nextFloat(); sy[i] = r.nextFloat() * 0.75f; ss[i] = 0.6f + r.nextFloat() * 1.6f; }
    }

    void setRunning(boolean on) { running = on; if (on) invalidate(); }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        bg.setShader(new LinearGradient(0, 0, 0, h, Neon.BG_TOP, Neon.BG_BOTTOM, Shader.TileMode.CLAMP));
        ribbon.setMaskFilter(new BlurMaskFilter(Math.max(8, w / 18f), BlurMaskFilter.Blur.NORMAL));
    }

    @Override
    protected void onDraw(Canvas c) {
        int w = getWidth(), h = getHeight();
        float t = (System.currentTimeMillis() - t0) / 1000f;
        c.drawRect(0, 0, w, h, bg);
        for (int i = 0; i < sx.length; i++) {                // etoiles qui scintillent
            float a = 0.35f + 0.35f * (float) Math.sin(t * 1.3f + i * 2.1f);
            star.setColor(Neon.alpha(0xFFFFFFFF, (int) (a * 160)));
            c.drawCircle(sx[i] * w, sy[i] * h, ss[i] * getResources().getDisplayMetrics().density * 0.6f, star);
        }
        for (int k = 0; k < 4; k++) {                         // 4 rubans, du rose au cyan
            float base = h * (0.12f + k * 0.09f);
            path.reset();
            for (int x = -40; x <= w + 40; x += 24) {
                float y = base + (float) (Math.sin(x / (float) w * 5.2f + t * (0.35f + k * 0.07f) + k) * h * 0.035f
                                        + Math.sin(x / (float) w * 2.1f - t * 0.22f + k * 1.7f) * h * 0.05f);
                if (x == -40) path.moveTo(x, y); else path.lineTo(x, y);
            }
            int col = Neon.AURORA[k];
            ribbon.setShader(new LinearGradient(0, 0, w, 0, new int[] { Neon.alpha(col, 0), Neon.alpha(col, 150), Neon.alpha(Neon.AURORA[(k + 1) % 4], 110), Neon.alpha(col, 0) },
                                                null, Shader.TileMode.CLAMP));
            ribbon.setStrokeWidth(h * (0.05f + 0.02f * (float) Math.sin(t * 0.5f + k)));
            c.drawPath(path, ribbon);
        }
        if (running) postInvalidateDelayed(33);
    }
}
