package fr.gcstation.telecommande

// Appli : la boite du jeu en 3D (fiche d'un jeu > jaquette ou "Boite 3D"), comme sur la console mais en vrai 3D
// (OpenGL ES 2 du telephone) : antialiasing 4x, textures en pleine definition avec mipmaps et filtrage anisotrope,
// lumiere, reflet du plastique, ombre, rendu a la frequence de l'ecran.
// La jaquette complete (dos | tranche | avant) vient de GameTDB en HQ, telechargee par le telephone (Internet), gardee
// dans le cache de l'appli ; sinon la jaquette avant (HQ de GameTDB, ou celle de la console).
// Doigt : tourner ; deux doigts : zoom ; double toucher : ouvrir / fermer le boitier (le disque est dedans).

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.graphics.SweepGradient
import android.graphics.Typeface
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.opengl.GLUtils
import android.opengl.Matrix
import android.os.SystemClock
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.opengles.GL10
import kotlin.math.max
import kotlin.math.min

// ------------------------------------------------------------------ jaquettes de GameTDB (HQ)
object JaquettesHQ {
    /** regions de GameTDB a essayer pour un ID (4e lettre), comme la console */
    private fun regions(id: String): List<String> = when (id.getOrNull(3)) {
        'E' -> listOf("US", "EN", "FR"); 'J' -> listOf("JA", "US", "EN"); 'D' -> listOf("DE", "EN", "US")
        'S' -> listOf("ES", "EN", "US"); 'I' -> listOf("IT", "EN", "US"); else -> listOf("FR", "EN", "US")
    }

    private fun telecharger(url: String, max: Int = 12 * 1024 * 1024): ByteArray? {
        var c: HttpURLConnection? = null
        return try {
            c = URL(url).openConnection() as HttpURLConnection
            c.connectTimeout = 6000; c.readTimeout = 15000
            c.setRequestProperty("User-Agent", "GC-Station-telecommande")
            if (c.responseCode != 200) null else c.inputStream.use { inp ->
                val o = java.io.ByteArrayOutputStream(); val b = ByteArray(65536); var n: Int
                while (inp.read(b).also { n = it } > 0) { o.write(b, 0, n); if (o.size() > max) return null }
                o.toByteArray()
            }
        } catch (e: Exception) { null } finally { c?.disconnect() }
    }

    /** une image de GameTDB (genre : coverfullHQ, coverfull, coverHQ), gardee sur le telephone ; null : pas trouvee */
    private fun image(dir: File, genre: String, id: String): Bitmap? {
        val d = File(dir, "jaquettes/$genre"); d.mkdirs()
        val f = File(d, "$id.png")
        val absent = File(d, "$id.absent")
        if (!f.exists()) {
            if (absent.exists() && System.currentTimeMillis() - absent.lastModified() < 7L * 86400000) return null
            var ok = false
            for (rg in regions(id)) {
                val b = telecharger("https://art.gametdb.com/wii/$genre/$rg/$id.png") ?: continue
                val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeByteArray(b, 0, b.size, o)
                if (o.outWidth > 0) {
                    try { f.writeBytes(b); ok = true } catch (e: Exception) { }
                    break
                }
            }
            if (!ok) { try { absent.writeText("") } catch (e: Exception) { }; return null }
        }
        return BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 })
    }

    /** {jaquette complete (dos|tranche|avant) ou null, jaquette avant ou null} */
    suspend fun charger(dir: File, id: String): Pair<Bitmap?, Bitmap?> = withContext(Dispatchers.IO) {
        val full = image(dir, "coverfullHQ", id) ?: image(dir, "coverfull", id)
        val avant = if (full != null) null else image(dir, "coverHQ", id) ?: Jaquettes.bitmap(dir, id)
        full to avant
    }
}

// ------------------------------------------------------------------ ecran : la boite en plein ecran
@Composable
fun Boite3DEcran(act: StationActivity, id: String, nom: String, lancer: (() -> Unit)?, fermer: () -> Unit) {
    var etat by remember { mutableStateOf("Recherche de la jaquette complète…") }
    var vue by remember { mutableStateOf<BoiteVue?>(null) }
    var ouvert by remember { mutableStateOf(false) }
    LaunchedEffect(id, vue) {
        val v = vue ?: return@LaunchedEffect
        val (full, avant) = JaquettesHQ.charger(act.cacheDir, id)
        etat = when {
            full != null -> "Jaquette complète " + full.width + "×" + full.height + " (GameTDB)"
            avant != null -> "Jaquette avant seulement (pas de jaquette complète pour ce jeu)"
            else -> "Pas de jaquette pour ce jeu"
        }
        v.textures(full, avant, nom, id)
    }
    Dialog(onDismissRequest = fermer, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(C.Fond)) {
            AndroidView({ c -> BoiteVue(c).also { b -> b.surOuvert = { o -> ouvert = o }; vue = b } }, Modifier.fillMaxSize())
            Column(Modifier.fillMaxWidth().statusBarsPadding().padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(nom, color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Doux(etat, taille = 11, lignes = 2)
                    }
                    Box(Modifier.size(44.dp).verre(CircleShape).appui(fermer), contentAlignment = Alignment.Center) { Icon(Ic.Fermer, null, tint = C.Texte) }
                }
            }
            Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().navigationBarsPadding().padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Doux("Glisse pour tourner · deux doigts pour zoomer · double toucher pour ouvrir", taille = 11, couleur = C.Pale, centre = true)
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Bouton(if (ouvert) "Fermer le boîtier" else "Ouvrir le boîtier", Modifier.weight(1f)) { vue?.basculer() }
                    if (lancer != null) Bouton("Jouer", Modifier.weight(1f), plein = true, icone = Ic.Lecture) { lancer() }
                }
            }
        }
    }
}

// ------------------------------------------------------------------ la vue OpenGL
class BoiteVue(ctx: Context) : GLSurfaceView(ctx) {
    private val r = Rendu()
    var surOuvert: ((Boolean) -> Unit)? = null
    private val pinch = ScaleGestureDetector(ctx, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(d: ScaleGestureDetector): Boolean { r.zoom = (r.zoom * d.scaleFactor).coerceIn(0.6f, 3f); r.touche(); return true }
    })
    private val gestes = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean { r.vYaw = 0f; r.vPitch = 0f; r.touche(); return true }
        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            if (e2.pointerCount > 1) return true
            val k = 160f / resources.displayMetrics.densityDpi
            r.yaw -= dx * 0.45f * k * 2; r.pitch = (r.pitch - dy * 0.35f * k * 2).coerceIn(-70f, 70f); r.touche(); return true
        }
        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            val k = 160f / resources.displayMetrics.densityDpi
            r.vYaw = vx * 0.45f * k * 2; r.vPitch = vy * 0.2f * k * 2; return true
        }
        override fun onDoubleTap(e: MotionEvent): Boolean { basculer(); return true }
    })

    init {
        setEGLContextClientVersion(2)
        setEGLConfigChooser(ChoixConfig())
        setRenderer(r)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun basculer() { r.ouvrirVers = if (r.ouvrirVers > 0.5f) 0f else 1f; r.touche(); val o = r.ouvrirVers > 0.5f; post { surOuvert?.invoke(o) } }

    fun textures(full: Bitmap?, avant: Bitmap?, nom: String, id: String) {
        val disque = Rendu.disque(nom, id)
        queueEvent { r.charger(full, avant, disque) }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        pinch.onTouchEvent(e)
        gestes.onTouchEvent(e)
        return true
    }
}

/** antialiasing 4x si possible, sinon 2x, sinon sans */
private class ChoixConfig : GLSurfaceView.EGLConfigChooser {
    override fun chooseConfig(egl: EGL10, d: EGLDisplay): EGLConfig {
        val base = intArrayOf(EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8, EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_DEPTH_SIZE, 24, 0x3040, 4)
        for ((dep, s) in listOf(24 to 4, 16 to 4, 24 to 2, 24 to 0, 16 to 0)) {
            val a = base.copyOf().also { it[7] = dep } + (if (s > 0) intArrayOf(EGL10.EGL_SAMPLE_BUFFERS, 1, EGL10.EGL_SAMPLES, s) else intArrayOf()) + intArrayOf(EGL10.EGL_NONE)
            val n = IntArray(1)
            val c = arrayOfNulls<EGLConfig>(1)
            if (egl.eglChooseConfig(d, a, c, 1, n) && n[0] > 0 && c[0] != null) return c[0]!!
        }
        throw IllegalArgumentException("pas de configuration OpenGL")
    }
}

private class Rendu : GLSurfaceView.Renderer {
    @Volatile var yaw = 24f
    @Volatile var pitch = 8f
    @Volatile var zoom = 1f
    @Volatile var vYaw = 0f
    @Volatile var vPitch = 0f
    @Volatile var ouvrirVers = 0f
    private var ouvert = 0f
    @Volatile private var toucheA = 0L
    fun touche() { toucheA = SystemClock.uptimeMillis() }

    private var prog = 0
    private var progFond = 0
    private var w = 1
    private var h = 1
    private var texFull = 0
    private var texAvant = 0
    private var texDisque = 0
    private var texOmbre = 0
    private var aniso = 1f
    private var maxTex = 2048
    private var dernier = 0L

    private val proj = FloatArray(16)
    private val vueM = FloatArray(16)
    private val mod = FloatArray(16)
    private val lid = FloatArray(16)
    private val tmp = FloatArray(16)
    private val mvp = FloatArray(16)

    // ---- geometrie : [x y z nx ny nz u v] par sommet, 4 sommets par face ----
    private class Face(val premier: Int, val mat: Int, val r: Float, val g: Float, val b: Float, val couvercle: Boolean)
    private val faces = ArrayList<Face>()
    private var buf: FloatBuffer? = null
    private var aFull = false

    companion object {
        const val X = 0.75f
        const val Y = 1.0f
        const val Z = 0.073f
        const val T = 0.022f                                      // (epaisseur du couvercle)
        const val SP = 0.05f                                      // (jaquette complete : la tranche = 5 % au milieu)
        const val M_FULL = 1; const val M_AVANT = 2; const val M_DISQUE = 3; const val M_OMBRE = 4; const val M_COULEUR = 0

        /** le mini-disque GameCube (8 cm) : argent iris, etiquette au nom du jeu */
        fun disque(nom: String, id: String): Bitmap {
            val s = 1024
            val b = Bitmap.createBitmap(s, s, Bitmap.Config.ARGB_8888)
            val c = Canvas(b)
            val p = Paint(Paint.ANTI_ALIAS_FLAG)
            val m = s / 2f
            p.shader = SweepGradient(m, m, intArrayOf(0xFFE8E8F0.toInt(), 0xFFB8D8FF.toInt(), 0xFFF0C8F8.toInt(), 0xFFD8F8E0.toInt(), 0xFFFFF0C0.toInt(), 0xFFE8E8F0.toInt()), null)
            c.drawCircle(m, m, m - 2, p)
            p.shader = RadialGradient(m, m, m, intArrayOf(0x00FFFFFF, 0x30FFFFFF, 0x00000000), floatArrayOf(0.3f, 0.7f, 1f), Shader.TileMode.CLAMP)
            c.drawCircle(m, m, m - 2, p)
            p.shader = null
            p.color = 0xFF2A1450.toInt()                                       // (etiquette)
            c.drawCircle(m, m, m * 0.62f, p)
            p.shader = RadialGradient(m, m * 0.7f, m * 0.62f, intArrayOf(0xFF6A3CC8.toInt(), 0xFF2A1450.toInt()), null, Shader.TileMode.CLAMP)
            c.drawCircle(m, m, m * 0.6f, p)
            p.shader = null
            p.color = 0xFFF4EEFF.toInt(); p.textAlign = Paint.Align.CENTER; p.typeface = Typeface.DEFAULT_BOLD
            var ts = 64f
            p.textSize = ts
            while (p.measureText(nom) > s * 0.62f && ts > 26) { ts -= 2; p.textSize = ts }
            c.drawText(nom, m, m - m * 0.33f, p)
            p.textSize = 40f; p.color = 0xFFFF2FD0.toInt()
            c.drawText(id, m, m + m * 0.42f, p)
            p.color = 0xFFC8C8D8.toInt()                                       // (centre transparent, anneau)
            c.drawCircle(m, m, m * 0.2f, p)
            p.xfermode = android.graphics.PorterDuffXfermode(android.graphics.PorterDuff.Mode.CLEAR)
            c.drawCircle(m, m, m * 0.12f, p)
            return b
        }
    }

    private fun construire(full: Boolean) {
        aFull = full
        faces.clear()
        val v = ArrayList<Float>()
        fun quad(p: Array<FloatArray>, n: FloatArray, uv: Array<FloatArray>, mat: Int, col: Int, couv: Boolean) {
            faces.add(Face(v.size / 8, mat, ((col shr 16) and 255) / 255f, ((col shr 8) and 255) / 255f, (col and 255) / 255f, couv))
            for (k in 0..3) { v.add(p[k][0]); v.add(p[k][1]); v.add(p[k][2]); v.add(n[0]); v.add(n[1]); v.add(n[2]); v.add(uv[k][0]); v.add(uv[k][1]) }
        }
        fun f(x: Float, y: Float, z: Float) = floatArrayOf(x, y, z)
        fun t(u: Float, w: Float) = floatArrayOf(u, w)
        val ub = (1 - SP) / 2; val uf = (1 + SP) / 2
        val plast = 0x1C1626; val bord = 0x2A2238
        val zi = Z - T                                                          // (fond du bac = interieur du couvercle)
        // bac
        if (full) quad(arrayOf(f(X, Y, -Z), f(-X, Y, -Z), f(-X, -Y, -Z), f(X, -Y, -Z)), f(0f, 0f, -1f), arrayOf(t(0f, 0f), t(ub, 0f), t(ub, 1f), t(0f, 1f)), M_FULL, 0, false)
        else quad(arrayOf(f(X, Y, -Z), f(-X, Y, -Z), f(-X, -Y, -Z), f(X, -Y, -Z)), f(0f, 0f, -1f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, 0x24183E, false)
        if (full) quad(arrayOf(f(-X, Y, -Z), f(-X, Y, Z), f(-X, -Y, Z), f(-X, -Y, -Z)), f(-1f, 0f, 0f), arrayOf(t(ub, 0f), t(uf, 0f), t(uf, 1f), t(ub, 1f)), M_FULL, 0, false)
        else quad(arrayOf(f(-X, Y, -Z), f(-X, Y, Z), f(-X, -Y, Z), f(-X, -Y, -Z)), f(-1f, 0f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, 0x6A2FA0, false)
        quad(arrayOf(f(X, Y, zi), f(X, Y, -Z), f(X, -Y, -Z), f(X, -Y, zi)), f(1f, 0f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, bord, false)
        quad(arrayOf(f(-X, Y, -Z), f(X, Y, -Z), f(X, Y, zi), f(-X, Y, zi)), f(0f, 1f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, bord, false)
        quad(arrayOf(f(-X, -Y, zi), f(X, -Y, zi), f(X, -Y, -Z), f(-X, -Y, -Z)), f(0f, -1f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, plast, false)
        quad(arrayOf(f(-X, Y, zi), f(X, Y, zi), f(X, -Y, zi), f(-X, -Y, zi)), f(0f, 0f, 1f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, 0x120E1A, false)
        // couvercle (pivote sur la tranche : x = -X, z = Z)
        if (full) quad(arrayOf(f(-X, Y, Z), f(X, Y, Z), f(X, -Y, Z), f(-X, -Y, Z)), f(0f, 0f, 1f), arrayOf(t(uf, 0f), t(1f, 0f), t(1f, 1f), t(uf, 1f)), M_FULL, 0, true)
        else quad(arrayOf(f(-X, Y, Z), f(X, Y, Z), f(X, -Y, Z), f(-X, -Y, Z)), f(0f, 0f, 1f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_AVANT, 0x46288C, true)
        quad(arrayOf(f(X, Y, zi), f(-X, Y, zi), f(-X, -Y, zi), f(X, -Y, zi)), f(0f, 0f, -1f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, 0x5C469C, true)
        quad(arrayOf(f(X, Y, Z), f(X, Y, zi), f(X, -Y, zi), f(X, -Y, Z)), f(1f, 0f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, bord, true)
        quad(arrayOf(f(-X, Y, zi), f(X, Y, zi), f(X, Y, Z), f(-X, Y, Z)), f(0f, 1f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, bord, true)
        quad(arrayOf(f(-X, -Y, Z), f(X, -Y, Z), f(X, -Y, zi), f(-X, -Y, zi)), f(0f, -1f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_COULEUR, plast, true)
        // disque (pose dans le bac) puis ombre au sol (dessines a part, avec transparence)
        val dr = 0.445f; val dz = zi + 0.004f; val dx = 0.06f
        quad(arrayOf(f(dx - dr, dr, dz), f(dx + dr, dr, dz), f(dx + dr, -dr, dz), f(dx - dr, -dr, dz)), f(0f, 0f, 1f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_DISQUE, 0, false)
        val sy = -Y - 0.02f
        quad(arrayOf(f(-1.5f, sy, -0.9f), f(1.5f, sy, -0.9f), f(1.5f, sy, 0.9f), f(-1.5f, sy, 0.9f)), f(0f, 1f, 0f), arrayOf(t(0f, 0f), t(1f, 0f), t(1f, 1f), t(0f, 1f)), M_OMBRE, 0, false)
        val fb = ByteBuffer.allocateDirect(v.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
        for (x in v) fb.put(x)
        fb.position(0)
        buf = fb
    }

    // ---- textures ----
    private fun pot(n: Int): Int { var p = 1; while (p < n) p = p shl 1; return min(p, maxTex) }
    private fun texture(b: Bitmap?, ancienne: Int): Int {
        if (ancienne != 0) GLES20.glDeleteTextures(1, intArrayOf(ancienne), 0)
        if (b == null) return 0
        // (taille en puissance de 2 : mipmaps en OpenGL ES 2 ; les coordonnees de texture ne changent pas)
        val bw = pot(b.width); val bh = pot(b.height)
        val s = if (bw != b.width || bh != b.height) Bitmap.createScaledBitmap(b, bw, bh, true) else b
        val id = IntArray(1)
        GLES20.glGenTextures(1, id, 0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, id[0])
        GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, s, 0)
        GLES20.glGenerateMipmap(GLES20.GL_TEXTURE_2D)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR_MIPMAP_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        if (aniso > 1f) GLES20.glTexParameterf(GLES20.GL_TEXTURE_2D, 0x84FE, aniso)       // (GL_TEXTURE_MAX_ANISOTROPY_EXT)
        if (s !== b) s.recycle()
        return id[0]
    }

    private var garde: Triple<Bitmap?, Bitmap?, Bitmap>? = null             // (pour recharger si la surface est recreee)
    fun charger(full: Bitmap?, avant: Bitmap?, disque: Bitmap) {
        garde = Triple(full, avant, disque)
        texFull = texture(full, texFull)
        texAvant = texture(avant, texAvant)
        texDisque = texture(disque, texDisque)
        construire(full != null)
    }

    private fun ombre(): Bitmap {
        val b = Bitmap.createBitmap(256, 256, Bitmap.Config.ARGB_8888)
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        p.shader = RadialGradient(128f, 128f, 128f, intArrayOf(0xAA000000.toInt(), 0x55000000, 0x00000000), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        Canvas(b).drawCircle(128f, 128f, 128f, p)
        return b
    }

    // ---- shaders ----
    private val VS = """
        uniform mat4 uMVP; uniform mat4 uM;
        attribute vec3 aP; attribute vec3 aN; attribute vec2 aT;
        varying vec3 vN; varying vec2 vT; varying vec3 vW;
        void main() { vec4 w = uM * vec4(aP, 1.0); vW = w.xyz; vN = (uM * vec4(aN, 0.0)).xyz; vT = aT; gl_Position = uMVP * vec4(aP, 1.0); }
    """
    private val FS = """
        precision highp float;
        uniform sampler2D uTex; uniform float uUseTex; uniform vec4 uCol; uniform vec3 uEye; uniform float uFlat; uniform float uGloss;
        varying vec3 vN; varying vec2 vT; varying vec3 vW;
        void main() {
            vec4 base = uUseTex > 0.5 ? texture2D(uTex, vT) : uCol;
            if (uFlat > 0.5) { gl_FragColor = base; return; }
            vec3 n = normalize(vN);
            vec3 V = normalize(uEye - vW);
            if (dot(n, V) < 0.0) n = -n;
            vec3 L1 = normalize(vec3(-0.45, 0.65, 1.0)), L2 = normalize(vec3(0.9, -0.15, 0.5));
            float d = max(dot(n, L1), 0.0) * 0.8 + max(dot(n, L2), 0.0) * 0.3;
            float s = pow(max(dot(n, normalize(L1 + V)), 0.0), 70.0) * 0.55 * uGloss + pow(max(dot(n, normalize(L2 + V)), 0.0), 40.0) * 0.15 * uGloss;
            float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.28;
            vec3 c = base.rgb * (0.30 + 0.85 * d) + vec3(s) + vec3(0.75, 0.45, 1.0) * rim;
            gl_FragColor = vec4(c, base.a);
        }
    """
    private val VS_FOND = "attribute vec2 aP; varying vec2 vP; void main() { vP = aP; gl_Position = vec4(aP, 0.999, 1.0); }"
    private val FS_FOND = """
        precision mediump float; varying vec2 vP;
        void main() {
            vec3 haut = vec3(0.071, 0.024, 0.141), bas = vec3(0.027, 0.008, 0.059);
            vec3 c = mix(bas, haut, vP.y * 0.5 + 0.5);
            float g = 1.0 - smoothstep(0.0, 1.1, length(vP - vec2(0.0, 0.15)));
            c += vec3(0.42, 0.14, 0.62) * g * 0.35;
            gl_FragColor = vec4(c, 1.0);
        }
    """
    private fun shader(t: Int, src: String): Int { val s = GLES20.glCreateShader(t); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s); return s }
    private fun programme(vs: String, fs: String): Int {
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, vs)); GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, fs))
        GLES20.glLinkProgram(p)
        return p
    }
    private val fond: FloatBuffer = ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f)); position(0) }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        prog = programme(VS, FS)
        progFond = programme(VS_FOND, FS_FOND)
        val ext = GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: ""
        if (ext.contains("GL_EXT_texture_filter_anisotropic")) { val a = FloatArray(1); GLES20.glGetFloatv(0x84FF, a, 0); aniso = max(1f, a[0]) }
        val m = IntArray(1); GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, m, 0); maxTex = min(8192, max(2048, m[0]))
        texFull = 0; texAvant = 0; texDisque = 0
        texOmbre = texture(ombre(), 0)
        construire(false)
        garde?.let { (a, b, c) -> charger(a, b, c) }
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        w = width; h = max(1, height)
        GLES20.glViewport(0, 0, w, h)
    }

    override fun onDrawFrame(gl: GL10?) {
        val now = SystemClock.uptimeMillis()
        val dt = if (dernier == 0L) 0.016f else min(0.05f, (now - dernier) / 1000f)
        dernier = now
        // inertie, rotation toute seule apres 3 s sans toucher, couvercle
        yaw += vYaw * dt; pitch = (pitch + vPitch * dt).coerceIn(-70f, 70f)
        vYaw *= (1 - min(1f, dt * 3.5f)); vPitch *= (1 - min(1f, dt * 3.5f))
        if (now - toucheA > 3000) { yaw += 14f * dt; pitch += (8f - pitch) * min(1f, dt * 0.8f) }
        ouvert += (ouvrirVers - ouvert) * min(1f, dt * 5f)

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        // fond
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(progFond)
        val ap = GLES20.glGetAttribLocation(progFond, "aP")
        GLES20.glEnableVertexAttribArray(ap)
        GLES20.glVertexAttribPointer(ap, 2, GLES20.GL_FLOAT, false, 8, fond)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(ap)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)

        val b = buf ?: return
        val aspect = w.toFloat() / h
        Matrix.perspectiveM(proj, 0, if (aspect < 1) 34f else 26f, aspect, 0.1f, 60f)
        val dist = (if (aspect < 1) 6.2f else 5.0f) * (1 + 0.4f * ouvert) / zoom      // (ouverte : plus large, on recule)
        val eye = floatArrayOf(0f, 0.35f, dist)
        Matrix.setLookAtM(vueM, 0, eye[0], eye[1], eye[2], 0f, -0.05f, 0f, 0f, 1f, 0f)
        Matrix.setIdentityM(mod, 0)
        Matrix.translateM(mod, 0, ouvert * 0.65f, 0f, 0f)                 // (ouverte : recentree)
        Matrix.rotateM(mod, 0, pitch, 1f, 0f, 0f)
        Matrix.rotateM(mod, 0, yaw, 0f, 1f, 0f)
        // couvercle : pivote autour de la charniere (x = -X, z = Z), jusqu'a 150 degres, comme un livre
        System.arraycopy(mod, 0, lid, 0, 16)
        Matrix.translateM(lid, 0, -X, 0f, Z)
        Matrix.rotateM(lid, 0, -150f * ouvert, 0f, 1f, 0f)
        Matrix.translateM(lid, 0, X, 0f, -Z)

        GLES20.glUseProgram(prog)
        val aP = GLES20.glGetAttribLocation(prog, "aP"); val aN = GLES20.glGetAttribLocation(prog, "aN"); val aT = GLES20.glGetAttribLocation(prog, "aT")
        b.position(0); GLES20.glVertexAttribPointer(aP, 3, GLES20.GL_FLOAT, false, 32, b); GLES20.glEnableVertexAttribArray(aP)
        b.position(3); GLES20.glVertexAttribPointer(aN, 3, GLES20.GL_FLOAT, false, 32, b); GLES20.glEnableVertexAttribArray(aN)
        b.position(6); GLES20.glVertexAttribPointer(aT, 2, GLES20.GL_FLOAT, false, 32, b); GLES20.glEnableVertexAttribArray(aT)
        b.position(0)
        val uMVP = GLES20.glGetUniformLocation(prog, "uMVP"); val uM = GLES20.glGetUniformLocation(prog, "uM")
        val uUse = GLES20.glGetUniformLocation(prog, "uUseTex"); val uCol = GLES20.glGetUniformLocation(prog, "uCol")
        val uEye = GLES20.glGetUniformLocation(prog, "uEye"); val uFlat = GLES20.glGetUniformLocation(prog, "uFlat")
        val uGloss = GLES20.glGetUniformLocation(prog, "uGloss")
        GLES20.glUniform1i(GLES20.glGetUniformLocation(prog, "uTex"), 0)
        GLES20.glUniform3f(uEye, eye[0], eye[1], eye[2])
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)

        fun matrices(m: FloatArray) {
            Matrix.multiplyMM(tmp, 0, vueM, 0, m, 0)
            Matrix.multiplyMM(mvp, 0, proj, 0, tmp, 0)
            GLES20.glUniformMatrix4fv(uMVP, 1, false, mvp, 0)
            GLES20.glUniformMatrix4fv(uM, 1, false, m, 0)
        }
        fun dessiner(f: Face) {
            val tex = when (f.mat) { M_FULL -> texFull; M_AVANT -> texAvant; M_DISQUE -> texDisque; M_OMBRE -> texOmbre; else -> 0 }
            if (tex != 0) { GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex); GLES20.glUniform1f(uUse, 1f) } else GLES20.glUniform1f(uUse, 0f)
            GLES20.glUniform4f(uCol, f.r, f.g, f.b, 1f)
            GLES20.glUniform1f(uGloss, if (f.mat == M_DISQUE) 1.6f else 1f)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_FAN, f.premier, 4)
        }
        // ombre au sol (sans rotation : elle reste sous la boite)
        val sol = FloatArray(16); Matrix.setIdentityM(sol, 0); Matrix.translateM(sol, 0, ouvert * 0.65f, 0f, 0f)
        GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA)
        GLES20.glDepthMask(false)
        matrices(sol); GLES20.glUniform1f(uFlat, 1f)
        for (f in faces) if (f.mat == M_OMBRE) dessiner(f)
        GLES20.glDepthMask(true); GLES20.glDisable(GLES20.GL_BLEND)
        GLES20.glUniform1f(uFlat, 0f)
        // la boite
        matrices(mod)
        for (f in faces) if (!f.couvercle && f.mat != M_DISQUE && f.mat != M_OMBRE) dessiner(f)
        matrices(lid)
        for (f in faces) if (f.couvercle) dessiner(f)
        // le disque, quand le boitier s'ouvre
        if (ouvert > 0.02f && texDisque != 0) {
            GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA)
            matrices(mod)
            for (f in faces) if (f.mat == M_DISQUE) dessiner(f)
            GLES20.glDisable(GLES20.GL_BLEND)
        }
        GLES20.glDisableVertexAttribArray(aP); GLES20.glDisableVertexAttribArray(aN); GLES20.glDisableVertexAttribArray(aT)
    }
}

@Suppress("unused")
private val degradeFond = Brush.verticalGradient(listOf(Color(0xFF120624), Color(0xFF07020F)))
@Suppress("unused")
private fun centre() = TextAlign.Center
@Suppress("unused")
private fun espace() = Modifier.width(1.dp)
