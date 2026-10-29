package fr.gcstation.telecommande;

// Fond anime : nuit violacee et rubans d'aurore neon (rose, violet, bleu, cyan) qui ondulent lentement, sur tout l'ecran
// (6 rubans de haut en bas ; halo flou + coeur net, dessines en demi-resolution).
// Rapide (120 images/s) : les rubans flous sont dessines UNE fois dans de petites images (le flou coute cher) ;
// ensuite la puce graphique les deplace, les etire et les fait respirer a chaque image (presque gratuit).
// L'animation suit l'ecran (postInvalidateOnAnimation : 120 Hz sur un ecran 120 Hz), et s'arrete appli cachee.

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Shader;
import android.view.View;

class AuroraView extends View {
    private static final int SCALE = 2;                     // rubans dessines en demi-resolution (halo flou + coeur net)
    private static final int N = 6;                         // rubans repartis sur tout l'ecran
    private final Paint bg = new Paint();
    private final Paint img = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint star = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Matrix m = new Matrix();
    private final Bitmap[] ribbons = new Bitmap[N];
    private final long t0 = System.nanoTime();
    private final float[] sx = new float[48], sy = new float[48], ss = new float[48];
    private boolean running = true;

    AuroraView(Context c) {
        super(c);
        java.util.Random r = new java.util.Random(7);
        for (int i = 0; i < sx.length; i++) { sx[i] = r.nextFloat(); sy[i] = r.nextFloat(); ss[i] = 0.6f + r.nextFloat() * 1.4f; }
    }

    void setRunning(boolean on) { running = on; if (on) postInvalidateOnAnimation(); }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        if (w <= 0 || h <= 0) return;
        bg.setShader(new LinearGradient(0, 0, 0, h, Neon.BG_TOP, Neon.BG_BOTTOM, Shader.TileMode.CLAMP));
        int bw = Math.max(64, w * 2 / SCALE), bh = Math.max(32, h * 3 / 10 / SCALE);   // (2 fois la largeur : il glisse)
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setStrokeWidth(bh * 0.22f);
        p.setMaskFilter(new BlurMaskFilter(bh * 0.11f, BlurMaskFilter.Blur.NORMAL));
        Paint core = new Paint(Paint.ANTI_ALIAS_FLAG);       // coeur net du ruban (sans flou)
        core.setStyle(Paint.Style.STROKE);
        core.setStrokeCap(Paint.Cap.ROUND);
        core.setStrokeWidth(Math.max(1.5f, bh * 0.03f));
        Path path = new Path();
        for (int k = 0; k < N; k++) {
            if (ribbons[k] != null) ribbons[k].recycle();
            Bitmap b = Bitmap.createBitmap(bw, bh, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(b);
            path.reset();
            for (int x = 0; x <= bw; x += 4) {                // vague fixe ; le mouvement vient du deplacement
                float y = bh * 0.5f + (float) (Math.sin(x / (float) bw * 9.0 + k) * bh * 0.16 + Math.sin(x / (float) bw * 3.4 + k * 1.7) * bh * 0.12);
                if (x == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            int col = Neon.AURORA[k % 4], nxt = Neon.AURORA[(k + 1) % 4];
            p.setShader(new LinearGradient(0, 0, bw, 0,
                    new int[] { Neon.alpha(col, 0), Neon.alpha(col, 200), Neon.alpha(nxt, 160), Neon.alpha(col, 190), Neon.alpha(nxt, 0) },
                    null, Shader.TileMode.CLAMP));
            c.drawPath(path, p);
            int lc = light(col), ln = light(nxt);
            core.setShader(new LinearGradient(0, 0, bw, 0,
                    new int[] { Neon.alpha(lc, 0), Neon.alpha(lc, 235), Neon.alpha(ln, 200), Neon.alpha(lc, 230), Neon.alpha(ln, 0) },
                    null, Shader.TileMode.CLAMP));
            c.drawPath(path, core);
            ribbons[k] = b;
        }
    }

    /** la couleur, eclaircie vers le blanc (coeur lumineux du ruban) */
    private static int light(int c) {
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        return 0xFF000000 | ((r + (255 - r) * 2 / 5) << 16) | ((g + (255 - g) * 2 / 5) << 8) | (b + (255 - b) * 2 / 5);
    }

    @Override
    protected void onDraw(Canvas c) {
        int w = getWidth(), h = getHeight();
        float t = (System.nanoTime() - t0) / 1e9f;
        c.drawRect(0, 0, w, h, bg);
        float d = getResources().getDisplayMetrics().density;
        for (int i = 0; i < sx.length; i++) {                // etoiles qui scintillent
            float a = 0.35f + 0.35f * (float) Math.sin(t * 1.3f + i * 2.1f);   // (sur tout l'ecran)
            star.setColor(Neon.alpha(0xFFFFFFFF, (int) (a * 150)));
            c.drawCircle(sx[i] * w, sy[i] * h, ss[i] * d * 0.6f, star);
        }
        for (int k = 0; k < N; k++) {
            Bitmap b = ribbons[k];
            if (b == null) continue;
            float bw = b.getWidth() * SCALE, bh = b.getHeight() * SCALE;
            float sY = 1f + 0.22f * (float) Math.sin(t * 0.45f + k * 1.1f);                 // il respire
            float x = -bw * 0.25f + (float) Math.sin(t * (0.10f + k * 0.025f) + k * 1.3f) * bw * 0.22f;   // il glisse
            float y = h * (-0.10f + k * 0.17f) + (float) Math.sin(t * (0.33f + k * 0.05f) + k) * h * 0.03f - bh * (sY - 1) / 2;   // (tout l'ecran)
            m.setScale(SCALE, SCALE * sY);
            m.postTranslate(x, y);
            img.setAlpha((int) (165 + 60 * Math.sin(t * 0.6f + k * 2.0f)));
            c.drawBitmap(b, m, img);
        }
        if (running) postInvalidateOnAnimation();
    }
}
