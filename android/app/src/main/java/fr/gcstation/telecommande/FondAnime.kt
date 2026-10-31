package fr.gcstation.telecommande

// Appli : fond d'ecran anime du telephone. Les jaquettes gardees sur le telephone (onglet Jeux, etagere, mur,
// « Tout garder pour hors-ligne ») defilent en 3D : quatre rangees a des vitesses differentes, celles du fond plus
// petites et plus sombres ; chaque jaquette pivote un peu selon sa place (comme un carrousel), avec un reflet. Glisser
// entre les ecrans d'accueil decale le tout (parallaxe). Dessin par la puce graphique (Canvas materiel, jaquettes en
// memoire graphique), a chaque rafraichissement de l'ecran (Choreographer) et en demandant 120 Hz ; rien quand il est cache.

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Camera
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.Surface
import android.service.wallpaper.WallpaperService
import android.view.SurfaceHolder
import java.io.File
import kotlin.math.abs
import kotlin.math.min

class FondAnime : WallpaperService() {
    override fun onCreateEngine(): Engine = Moteur()

    /** les jaquettes avant gardees sur le telephone (HD de GameTDB d'abord, sinon celles de la console) */
    private fun jaquettes(max: Int): List<Bitmap> {
        val d = Hors.dir(this)
        val vus = HashSet<String>()
        val fichiers = ArrayList<File>()
        for (s in listOf("jaquettes/coverHQ", "jaquettes")) {
            File(d, s).listFiles { f -> f.isFile && f.name.endsWith(".png") && f.length() > 0 }?.sortedBy { it.name }?.forEach {
                val id = it.name.removeSuffix(".png")
                if (id.length == 6 && vus.add(id)) fichiers.add(it)
            }
        }
        fichiers.shuffle()
        val l = ArrayList<Bitmap>()
        for (f in fichiers.take(max)) {
            try {
                val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeFile(f.path, o)
                var e = 1
                while (o.outWidth / (e * 2) >= 260) e *= 2
                BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inSampleSize = e })?.let { b ->
                    // (en memoire graphique : dessinee sans recopie a chaque image)
                    val hw = if (Build.VERSION.SDK_INT >= 26) try { b.copy(Bitmap.Config.HARDWARE, false) } catch (e2: Throwable) { null } else null
                    if (hw != null) { b.recycle(); l.add(hw) } else l.add(b)
                }
            } catch (e: Throwable) { }
        }
        return l
    }

    inner class Moteur : Engine() {
        private val h = Handler(Looper.getMainLooper())
        private var visible = false
        private var images: List<Bitmap> = emptyList()
        private var charge = false
        private var w = 1; private var hh = 1
        private var decal = 0f                                     // (ecran d'accueil : 0 a 1)
        private var t0 = SystemClock.uptimeMillis()
        private val cam = Camera()
        private val mat = Matrix()
        private val p = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        private val ombre = Paint(Paint.ANTI_ALIAS_FLAG)
        private val reflet = Paint(Paint.ANTI_ALIAS_FLAG)
        private val texte = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xCCFFFFFF.toInt(); textAlign = Paint.Align.CENTER }
        private val rect = android.graphics.RectF()
        private val fondH = Paint()
        private val voile = Paint()
        private var fondPour = 0                                   // (degrades refaits seulement si la taille change)
        // (a chaque rafraichissement de l'ecran, 120 Hz sur ton telephone)
        private val image = Choreographer.FrameCallback { if (visible) { dessiner(); suivante() } }
        private fun suivante() { if (visible) Choreographer.getInstance().postFrameCallback(image) }
        private val dessin = Runnable { Choreographer.getInstance().removeFrameCallback(image); suivante() }

        override fun onVisibilityChanged(v: Boolean) {
            visible = v
            h.removeCallbacks(dessin)
            if (v) {
                if (!charge) { charge = true; Thread { val l = jaquettes(36); h.post { images = l } }.start() }
                h.post(dessin)
            }
        }
        override fun onSurfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            w = width; hh = height
            if (Build.VERSION.SDK_INT >= 30) try { holder.surface.setFrameRate(120f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT) } catch (e: Exception) { }
            super.onSurfaceChanged(holder, format, width, height)
        }
        override fun onSurfaceDestroyed(holder: SurfaceHolder) { visible = false; h.removeCallbacks(dessin); Choreographer.getInstance().removeFrameCallback(image); super.onSurfaceDestroyed(holder) }
        override fun onOffsetsChanged(xo: Float, yo: Float, xs: Float, ys: Float, xp: Int, yp: Int) { decal = xo }
        override fun onDestroy() { h.removeCallbacks(dessin); Choreographer.getInstance().removeFrameCallback(image); super.onDestroy() }

        private fun dessiner() {
            val holder = surfaceHolder
            val c: Canvas = try { (if (Build.VERSION.SDK_INT >= 26) holder.lockHardwareCanvas() else holder.lockCanvas()) ?: return } catch (e: Exception) { return }
            try { scene(c) } finally { try { holder.unlockCanvasAndPost(c) } catch (e: Exception) { } }
        }

        private fun scene(c: Canvas) {
            val W = w.toFloat(); val H = hh.toFloat()
            if (fondPour != w * 7919 + hh) {
                fondPour = w * 7919 + hh
                fondH.shader = android.graphics.ComposeShader(
                    LinearGradient(0f, 0f, 0f, H, intArrayOf(0xFF140628.toInt(), 0xFF07020F.toInt()), null, Shader.TileMode.CLAMP),
                    RadialGradient(W / 2, H * 0.42f, W * 0.9f, intArrayOf(0x556A2CB0, 0x00000000), null, Shader.TileMode.CLAMP), android.graphics.PorterDuff.Mode.SRC_OVER)
                voile.shader = LinearGradient(0f, H * 0.7f, 0f, H, intArrayOf(0x00000000, 0xAA05010C.toInt()), null, Shader.TileMode.CLAMP)
                // (reflet : un seul degrade, sur une jaquette de 1 x 1, etire a chaque dessin)
                reflet.shader = LinearGradient(0f, 0f, 1f, 1f, intArrayOf(0x00FFFFFF, 0x30FFFFFF, 0x00FFFFFF), floatArrayOf(0.2f, 0.45f, 0.7f), Shader.TileMode.CLAMP)
            }
            c.drawRect(0f, 0f, W, H, fondH)
            val l = images
            if (l.isEmpty()) {
                texte.textSize = W * 0.04f
                c.drawText("Ouvre GC Station (onglet Jeux) pour garder des jaquettes", W / 2, H / 2, texte)
                return
            }
            val t = (SystemClock.uptimeMillis() - t0) / 1000f
            // rangees : du fond (petites, lentes, sombres) vers l'avant
            val rangs = 4
            for (r in 0 until rangs) {
                val prof = r.toFloat() / (rangs - 1)                     // (0 au fond, 1 devant)
                val ch = H * (0.15f + 0.09f * prof)                       // (hauteur d'une jaquette)
                val cw = ch * 0.7f
                val pas = cw * 1.18f
                val y = H * (0.12f + 0.24f * r)
                val vit = (14f + 30f * prof) * (if (r % 2 == 0) 1f else -1f)
                val dx = t * vit * W / 400f - decal * W * (0.15f + 0.35f * prof)
                val nb = (W / pas).toInt() + 3
                val debut = Math.floorDiv(-dx.toInt(), pas.toInt().coerceAtLeast(1))
                for (k in debut - 1..debut + nb) {
                    val x = k * pas + dx
                    val img = l[Math.floorMod(k * 7 + r * 11, l.size)]
                    val cx = x + cw / 2
                    val rel = ((cx - W / 2) / (W / 2)).coerceIn(-1.4f, 1.4f)  // (-1 a gauche, 1 a droite)
                    c.save()
                    cam.save(); cam.rotateY(-rel * 28f); cam.getMatrix(mat); cam.restore()
                    mat.preTranslate(-cw / 2, -ch / 2); mat.postTranslate(cx, y + ch / 2)
                    c.concat(mat)
                    ombre.color = 0x66000000; c.drawRect(cw * 0.04f, ch * 0.05f, cw * 1.04f, ch * 1.05f, ombre)
                    p.alpha = (110 + 145 * prof).toInt()
                    rect.set(0f, 0f, cw, ch)
                    c.drawBitmap(img, null, rect, p)
                    p.alpha = 255
                    reflet.alpha = (255 * (1 - abs(rel).coerceAtMost(1f))).toInt()
                    c.save(); c.scale(cw, ch); c.drawRect(0f, 0f, 1f, 1f, reflet); c.restore()
                    c.restore()
                }
            }
            // voile en bas (les icones restent lisibles)
            c.drawRect(0f, H * 0.7f, W, H, voile)
            if (min(W, H) <= 0f) return
        }
    }
}
