package fr.gcstation.telecommande

// Appli : la boite du jeu en 3D (fiche d'un jeu > jaquette ou "Boite 3D"), comme sur la console mais en vrai 3D
// (OpenGL ES 2 du telephone) : antialiasing 4x, textures en pleine definition avec mipmaps et filtrage anisotrope,
// lumiere, reflet du plastique, ombre, rendu a la frequence de l'ecran.
// La jaquette complete (dos | tranche | avant) vient de GameTDB en HQ, telechargee par le telephone (Internet), gardee
// dans le cache de l'appli ; sinon la jaquette avant (HQ de GameTDB, ou celle de la console).
// Boitier comme le vrai : plastique noir a coins arrondis, jaquette sous film transparent, support hexagonal et ergot
// du disque, attaches de la notice ; le disque du jeu (image de GameTDB) sur l'ergot, qu'on peut sortir.
// Doigt : tourner (elle revient sur la face la plus proche) ; boutons Avant / Tranche / Dos ; double toucher : ouvrir.

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.BitmapShader
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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asAndroidBitmap
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
import fr.gcstation.telecommande.Geo.X
import fr.gcstation.telecommande.Geo.Z
import fr.gcstation.telecommande.Geo.P_BAC
import fr.gcstation.telecommande.Geo.P_COUV
import fr.gcstation.telecommande.Geo.P_DISQUE
import fr.gcstation.telecommande.Geo.P_SOL
import fr.gcstation.telecommande.Geo.M_FULL
import fr.gcstation.telecommande.Geo.M_AVANT
import fr.gcstation.telecommande.Geo.M_DISQUE
import fr.gcstation.telecommande.Geo.M_DESSOUS
import fr.gcstation.telecommande.Geo.M_OMBRE
import fr.gcstation.telecommande.Geo.M_NOTICE

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
        val f = fichier(dir, genre, id) ?: return null
        return BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 })
    }
    /** le fichier PNG d'origine de GameTDB, telecharge une fois et garde sur le telephone ; null : pas trouve */
    fun fichier(dir: File, genre: String, id: String): File? {
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
        return f
    }

    /** {jaquette complete (dos|tranche|avant) ou null, jaquette avant ou null} */
    /** jaquette complete (dos|tranche|avant), jaquette avant (sans complete), image du disque : null si pas trouvee */
    class Images(val full: Bitmap?, val avant: Bitmap?, val disque: Bitmap?)
    suspend fun charger(dir: File, id: String, ctx: Context? = null): Images = withContext(Dispatchers.IO) {
        val full = image(dir, "coverfullHQ", id) ?: image(dir, "coverfull", id)?.let { netIA(dir, "coverfull", id, it, ctx) }
        val avant = if (full != null) null else image(dir, "coverHQ", id) ?: Jaquettes.bitmap(dir, id)?.let { netIA(dir, "cover", id, it, ctx) }
        Images(full, avant, disqueNet(dir, id, ctx))
    }

    /** pour l'etagere : jaquette complete (HD si gardee, sinon normale, ou deja agrandie par IA), sinon jaquette avant ;
     *  pas de disque ; (l'IA n'y est pas lancee : trop long pour toute une etagere, seulement ce qu'une boite a deja fait) */
    suspend fun chargerEtagere(dir: File, id: String): Images = withContext(Dispatchers.IO) {
        val full = image(dir, "coverfullHQ", id) ?: image(dir, "coverfull", id)?.let { netIA(dir, "coverfull", id, it, null) }
        val avant = if (full != null) null else image(dir, "coverHQ", id) ?: Jaquettes.bitmap(dir, id)?.let { netIA(dir, "cover", id, it, null) }
        Images(full, avant, null)
    }

    /** v308 : jaquette en basse definition (pas de HD chez GameTDB) agrandie par IA (x4, puis 1024 px au plus), gardee
     *  sur le telephone (jaquettes/<genre>ia) ; ctx null : seulement si deja faite ; sinon (ou echec) : celle d'origine */
    private fun netIA(dir: File, genre: String, id: String, b: Bitmap, ctx: Context?): Bitmap {
        if (maxOf(b.width, b.height) > 600) return b                  // (deja assez grande)
        val f = File(dir, "jaquettes/${genre}ia/$id.png")
        if (f.exists()) BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 })?.let { return it }
        if (ctx == null) return b
        return try {
            val px = IntArray(b.width * b.height); b.getPixels(px, 0, b.width, 0, 0, b.width, b.height)
            val ia = AgrandirIA.x4(ctx, px, b.width, b.height) ?: return b
            val k = minOf(1f, 1024f / maxOf(ia.width, ia.height))
            val r = if (k < 1f) Bitmap.createScaledBitmap(ia, (ia.width * k).toInt(), (ia.height * k).toInt(), true).also { if (it !== ia) ia.recycle() } else ia
            try { f.parentFile?.mkdirs(); f.outputStream().use { r.compress(Bitmap.CompressFormat.PNG, 100, it) } } catch (e: Exception) { }
            r
        } catch (e: Throwable) { b }                                  // (pas assez de memoire : celle d'origine)
    }

    /** l'image du disque agrandie a 1024 px, gardee sur le telephone (calculee une fois) : par IA (AgrandirIA.kt,
     *  x4 puis 1024), sinon proprement (Agrandir.kt) */
    private fun disqueNet(dir: File, id: String, ctx: Context?): Bitmap? {
        val f = File(dir, "jaquettes/" + (if (ctx != null) "disc1024ia" else "disc1024") + "/$id.png")
        if (f.exists()) BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 })?.let { return it }
        val b = image(dir, "disc", id) ?: return null
        if (b.width >= 1024) return b
        return try {
            val px = IntArray(b.width * b.height); b.getPixels(px, 0, b.width, 0, 0, b.width, b.height)
            val ia = if (ctx != null && b.width <= 400 && b.height <= 400) AgrandirIA.x4(ctx, px, b.width, b.height) else null
            val r = if (ia != null) {
                if (ia.width == 1024 && ia.height == 1024) ia else Bitmap.createScaledBitmap(ia, 1024, 1024, true).also { if (it !== ia) ia.recycle() }
            } else Bitmap.createBitmap(Agrandir.agrandir(px, b.width, b.height, 1024, 1024), 1024, 1024, Bitmap.Config.ARGB_8888)
            try { f.parentFile?.mkdirs(); f.outputStream().use { r.compress(Bitmap.CompressFormat.PNG, 100, it) } } catch (e: Exception) { }
            r
        } catch (e: Throwable) { b }                           // (pas assez de memoire : l'image d'origine)
    }
}

// ------------------------------------------------------------------ ecran : la boite en plein ecran
@Composable
fun Boite3DEcran(act: StationActivity, id: String, nom: String, lancer: (() -> Unit)?, fermer: () -> Unit) {
    var etat by remember { mutableStateOf("Recherche de la jaquette complète et du disque…") }
    var vue by remember { mutableStateOf<BoiteVue?>(null) }
    var ouvert by remember { mutableStateOf(false) }
    var sorti by remember { mutableStateOf(false) }
    var pagesNotice by remember { mutableIntStateOf(0) }
    var noticeOuverte by remember { mutableStateOf(false) }
    LaunchedEffect(id, vue) {
        val v = vue ?: return@LaunchedEffect
        val im = JaquettesHQ.charger(Hors.dir(act), id, act)
        etat = (when {
            im.full != null -> "Jaquette complète " + im.full.width + "×" + im.full.height
            im.avant != null -> "Jaquette avant seulement"
            else -> "Pas de jaquette"
        }) + " · " + (if (im.disque != null) "disque du jeu" else "pas d'image du disque") + " (GameTDB)"
        v.textures(im, nom, id)
        val n = Notices.pages(id).let { if (it > 0) it else Hors.pagesNotice(act, id) }   // (la notice du jeu, si la console en a une, ou gardee : dans le couvercle)
        if (n > 0) { Notices.page(Hors.dir(act), id, 1)?.let { v.notice(it.asAndroidBitmap()) }; pagesNotice = n }
    }
    Dialog(onDismissRequest = fermer, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(C.Fond)) {
            AndroidView({ c -> BoiteVue(c).also { b -> b.surOuvert = { o, s -> ouvert = o; sorti = s }; vue = b } }, Modifier.fillMaxSize())
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
                Doux(if (sorti) "Un doigt : faire tourner le disque · deux doigts : zoomer où tu veux et déplacer" else "Un doigt : tourner · deux doigts : zoomer où tu veux et déplacer · double toucher : ouvrir (zoomée : recadrer)",
                    taille = 11, couleur = C.Pale, centre = true)
                Spacer(Modifier.height(10.dp))
                if (!ouvert) Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally)) {
                    Puce("Avant", false) { vue?.face(0) }
                    Puce("Tranche", false) { vue?.face(1) }
                    Puce("Dos", false) { vue?.face(2) }
                }
                if (ouvert && pagesNotice > 0) { Spacer(Modifier.height(10.dp)); Bouton("Lire la notice ($pagesNotice pages)", Modifier.fillMaxWidth()) { noticeOuverte = true } }
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Bouton(if (ouvert) "Fermer" else "Ouvrir le boîtier", Modifier.weight(1f)) { vue?.basculer() }
                    if (ouvert) Bouton(if (sorti) "Ranger le disque" else "Sortir le disque", Modifier.weight(1f)) { vue?.disque() }
                    if (lancer != null) Bouton("Jouer", Modifier.weight(1f), plein = true, icone = Ic.Lecture) { lancer() }
                }
            }
        }
    }
    if (noticeOuverte) Notice3DEcran(act, id, nom, pagesNotice) { noticeOuverte = false }
}

// ------------------------------------------------------------------ la vue OpenGL
class BoiteVue(ctx: Context) : GLSurfaceView(ctx) {
    private val r = Rendu()
    var surOuvert: ((Boolean, Boolean) -> Unit)? = null
    // deux doigts : zoom a l'endroit pince, et la vue suit les doigts (dans tous les sens)
    private var fx0 = 0f
    private var fy0 = 0f
    private val pinch = ScaleGestureDetector(ctx, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScaleBegin(d: ScaleGestureDetector): Boolean { fx0 = d.focusX; fy0 = d.focusY; r.tient = true; return true }
        override fun onScale(d: ScaleGestureDetector): Boolean {
            r.deplacer(d.focusX - fx0, d.focusY - fy0); fx0 = d.focusX; fy0 = d.focusY
            r.zoomer(d.scaleFactor, d.focusX - width / 2f, d.focusY - height / 2f)
            r.touche(); return true
        }
    })
    private val gestes = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean { r.tient = true; r.vYaw = 0f; r.touche(); return true }
        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            if (e2.pointerCount > 1 || pinch.isInProgress) return true
            val k = 160f / resources.displayMetrics.densityDpi
            if (r.sortirVers > 0.5f) { r.tourne -= dx * 0.9f * k; return true }           // (disque sorti : on le fait tourner)
            r.yaw -= dx * 0.8f * k; r.pitch = (r.pitch - dy * 0.35f * k).coerceIn(-28f, 28f); r.touche(); return true
        }
        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            val k = 160f / resources.displayMetrics.densityDpi
            if (r.sortirVers > 0.5f) r.vTourne = -vx * 0.9f * k * 0.6f else r.vYaw = vx * 0.8f * k   // (disque sorti : c'est lui qui part, pas la boite)
            return true
        }
        override fun onDoubleTap(e: MotionEvent): Boolean { if (r.zoom > 1.05f || r.decale()) r.recadrer() else basculer(); return true }
    })

    init {
        setEGLContextClientVersion(2)
        setEGLConfigChooser(ChoixConfig())
        setRenderer(r)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    private fun dire() { val o = r.ouvrirVers > 0.5f; val s = r.sortirVers > 0.5f; post { surOuvert?.invoke(o, s) } }
    fun basculer() {
        if (r.ouvrirVers > 0.5f) { r.ouvrirVers = 0f; r.sortirVers = 0f } else { r.ouvrirVers = 1f; r.vers(0f) }
        r.touche(); dire()
    }
    fun disque() { if (r.ouvrirVers > 0.5f) { r.sortirVers = if (r.sortirVers > 0.5f) 0f else 1f; r.touche(); dire() } }

    fun textures(im: JaquettesHQ.Images, nom: String, id: String) {
        val face = Rendu.disque(nom, id, im.disque)
        val dessous = Rendu.dessousDisque()
        queueEvent { r.charger(im.full, im.avant, face, dessous) }
    }
    fun notice(b: Bitmap?) { queueEvent { r.notice(b) } }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        pinch.onTouchEvent(e)
        gestes.onTouchEvent(e)
        if (e.actionMasked == MotionEvent.ACTION_UP || e.actionMasked == MotionEvent.ACTION_CANCEL) r.lacher()
        return true
    }

    /** montrer une face : 0 avant, 1 tranche, 2 dos */
    fun face(k: Int) { r.vers(Rendu.VUES[k]); }
}

/** antialiasing 4x si possible, sinon 2x, sinon sans */
internal class ChoixConfig : GLSurfaceView.EGLConfigChooser {
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
    @Volatile private var panX = 0f
    @Volatile private var panY = 0f
    @Volatile private var unite = 0.005f                         // (unites 3D par pixel, a zoom 1 : calculee a chaque image)
    /** zoom autour d'un point de l'ecran (dx, dy : pixels depuis le centre) : ce point reste sous les doigts */
    fun zoomer(f: Float, dx: Float, dy: Float) {
        val z0 = zoom; val z1 = (zoom * f).coerceIn(0.6f, 6f)
        panX += dx * unite * (1 / z0 - 1 / z1); panY -= dy * unite * (1 / z0 - 1 / z1)
        zoom = z1
    }
    fun deplacer(dx: Float, dy: Float) { panX -= dx * unite / zoom; panY += dy * unite / zoom }
    fun decale() = kotlin.math.abs(panX) + kotlin.math.abs(panY) > 0.02f
    fun recadrer() { zoom = 1f; panX = 0f; panY = 0f }
    @Volatile var vYaw = 0f
    @Volatile var vPitch = 0f
    @Volatile var ouvrirVers = 0f
    @Volatile var sortirVers = 0f
    @Volatile var tient = false                                   // (un doigt sur l'ecran)
    @Volatile private var cible = 14f                             // (angle ou la boite va d'elle-meme ; NaN : nulle part)
    private var ouvert = 0f
    private var sorti = 0f
    @Volatile var tourne = 0f
    @Volatile var vTourne = 0f                                // (elan du disque lance du doigt)
    /** le doigt se leve : la boite va sur la face la plus proche (en tenant compte de l'elan) */
    fun lacher() {
        tient = false
        if (sortirVers > 0.5f) { vYaw = 0f; return }          // (disque sorti : la boite ne bouge pas)
        val p = yaw + vYaw * 0.3f
        vYaw = 0f
        var best = p; var d = 1e9f
        for (a in VUES) { val base = a + 360f * Math.round((p - a) / 360f); val e = kotlin.math.abs(p - base); if (e < d) { d = e; best = base } }
        cible = best
    }
    fun vers(a: Float) { cible = a + 360f * Math.round((yaw - a) / 360f); touche() }
    @Volatile private var toucheA = 0L
    fun touche() { toucheA = SystemClock.uptimeMillis() }

    private var prog = 0
    private var progFond = 0
    private var w = 1
    private var h = 1
    private var texFull = 0
    private var texAvant = 0
    private var texDisque = 0
    private var texDessous = 0
    private var texNotice = 0
    private var ratioNotice = 0f
    private var fullDernier = false
    private var gardeNotice: Bitmap? = null
    private var texOmbre = 0
    private var aniso = 1f
    private var maxTex = 2048
    private var dernier = 0L

    private val proj = FloatArray(16)
    private val vueM = FloatArray(16)
    private val mod = FloatArray(16)
    private val lid = FloatArray(16)
    private val dsq = FloatArray(16)
    private val sol = FloatArray(16)
    private val tmp = FloatArray(16)
    private val mvp = FloatArray(16)

    private var maillage = Maillage()
    private var buf: FloatBuffer? = null

    companion object {
        val VUES = floatArrayOf(14f, 90f, 194f, 270f)              // (avant de trois quarts, tranche, dos, cote)

        /** la face du disque : l'image du disque de GameTDB, sinon une etiquette au nom du jeu */
        fun disque(nom: String, id: String, art: Bitmap?): Bitmap {
            val s = 1024
            val b = Bitmap.createBitmap(s, s, Bitmap.Config.ARGB_8888)
            val c = Canvas(b)
            val p = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
            val m = s / 2f
            p.shader = SweepGradient(m, m, intArrayOf(0xFFDADAE2.toInt(), 0xFFC8D8F0.toInt(), 0xFFE8D0EC.toInt(), 0xFFD0ECD8.toInt(), 0xFFF0E6C8.toInt(), 0xFFDADAE2.toInt()), null)
            c.drawCircle(m, m, m, p)
            p.shader = null
            if (art != null) {                                     // (bord du disque net : l'image en motif d'un cercle lisse)
                val bs = BitmapShader(art, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
                bs.setLocalMatrix(android.graphics.Matrix().apply { setScale(s.toFloat() / art.width, s.toFloat() / art.height) })
                p.shader = bs
                c.drawCircle(m, m, m - 1f, p)
                p.shader = null
            } else {
                p.shader = RadialGradient(m, m * 0.7f, m * 0.95f, intArrayOf(0xFF6A3CC8.toInt(), 0xFF2A1450.toInt()), null, Shader.TileMode.CLAMP)
                c.drawCircle(m, m, m * 0.96f, p)
                p.shader = null
                p.color = 0xFFF4EEFF.toInt(); p.textAlign = Paint.Align.CENTER; p.typeface = Typeface.DEFAULT_BOLD
                var ts = 70f
                p.textSize = ts
                while (p.measureText(nom) > s * 0.7f && ts > 26) { ts -= 2; p.textSize = ts }
                c.drawText(nom, m, m - m * 0.42f, p)
                p.textSize = 44f; p.color = 0xFFFF2FD0.toInt()
                c.drawText(id, m, m + m * 0.55f, p)
            }
            p.color = 0xFFBFC2CC.toInt(); p.style = Paint.Style.FILL               // (anneau transparent du centre, comme un vrai disque)
            c.drawCircle(m, m, m * 0.30f, p)
            p.color = 0xFF9EA2AE.toInt()
            c.drawCircle(m, m, m * 0.24f, p)
            p.style = Paint.Style.STROKE                                         // (aretes nettes de l'anneau et du trou)
            p.strokeWidth = 3f; p.color = 0xFF7E828E.toInt(); c.drawCircle(m, m, m * 0.30f - 1.5f, p)
            p.strokeWidth = 2f; p.color = 0xFFD8DBE4.toInt(); c.drawCircle(m, m, m * 0.24f - 1f, p)
            p.color = 0xFF6E727E.toInt(); c.drawCircle(m, m, m * 0.1875f + 1f, p)
            p.style = Paint.Style.FILL
            return b
        }

        /** le dessous du disque : la face gravee, irisee */
        fun dessousDisque(): Bitmap {
            val s = 512
            val b = Bitmap.createBitmap(s, s, Bitmap.Config.ARGB_8888)
            val c = Canvas(b)
            val p = Paint(Paint.ANTI_ALIAS_FLAG)
            val m = s / 2f
            p.shader = SweepGradient(m, m, intArrayOf(0xFFB8C0D8.toInt(), 0xFF98E0F0.toInt(), 0xFFE0A8F0.toInt(), 0xFFF0E098.toInt(), 0xFFA8F0B8.toInt(), 0xFFB8C0D8.toInt()), null)
            c.drawCircle(m, m, m, p)
            p.shader = RadialGradient(m, m, m, intArrayOf(0x40FFFFFF, 0x00FFFFFF, 0x30000000), floatArrayOf(0.3f, 0.6f, 1f), Shader.TileMode.CLAMP)
            c.drawCircle(m, m, m, p)
            return b
        }

    }

    private fun construire(full: Boolean) {
        fullDernier = full
        val m = boitier(full, texAvant != 0, if (texNotice != 0) ratioNotice else 0f)
        val fb = ByteBuffer.allocateDirect(m.v.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
        for (x in m.v) fb.put(x)
        fb.position(0)
        maillage = m
        buf = fb
    }
    /** hauteur du disque pose sur l'ergot, dans le bac */
    private val zDisque get() = -Z + 0.016f + 0.008f + 0.012f

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

    private var garde: Array<Bitmap?>? = null                     // (pour recharger si la surface est recreee)
    fun charger(full: Bitmap?, avant: Bitmap?, disque: Bitmap, dessous: Bitmap) {
        garde = arrayOf(full, avant, disque, dessous)
        texFull = texture(full, texFull)
        texAvant = texture(avant, texAvant)
        texDisque = texture(disque, texDisque)
        texDessous = texture(dessous, texDessous)
        construire(full != null)
    }
    /** la notice (sa couverture) dans le couvercle ; null : pas de notice */
    fun notice(b: Bitmap?) {
        gardeNotice = b
        texNotice = texture(b, texNotice)
        ratioNotice = if (b != null && b.height > 0) b.width.toFloat() / b.height else 0f
        construire(fullDernier)
    }

    private fun ombre(): Bitmap {
        val b = Bitmap.createBitmap(256, 256, Bitmap.Config.ARGB_8888)
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        p.shader = RadialGradient(128f, 128f, 128f, intArrayOf(0xAA000000.toInt(), 0x55000000, 0x00000000), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        Canvas(b).drawCircle(128f, 128f, 128f, p)
        return b
    }

    // ---- shaders ----
    private val VS = Ombrage.VS
    private val FS = Ombrage.FS
    private val VS_FOND = Ombrage.VS_FOND
    private val FS_FOND = Ombrage.FS_FOND
    private fun shader(t: Int, src: String): Int { val s = GLES20.glCreateShader(t); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s); return s }
    private fun programme(vs: String, fs: String): Int {
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, vs)); GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, fs))
        GLES20.glLinkProgram(p)
        return p
    }
    private val fondBuf: FloatBuffer = ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f)); position(0) }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        prog = programme(VS, FS)
        progFond = programme(VS_FOND, FS_FOND)
        val ext = GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: ""
        if (ext.contains("GL_EXT_texture_filter_anisotropic")) { val a = FloatArray(1); GLES20.glGetFloatv(0x84FF, a, 0); aniso = max(1f, a[0]) }
        val mx = IntArray(1); GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, mx, 0); maxTex = min(8192, max(2048, mx[0]))
        texFull = 0; texAvant = 0; texDisque = 0; texDessous = 0; texNotice = 0
        texOmbre = texture(ombre(), 0)
        construire(false)
        garde?.let { g -> g[2]?.let { d -> g[3]?.let { u -> charger(g[0], g[1], d, u) } } }
        gardeNotice?.let { notice(it) }
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
        // inertie, rotation toute seule apres 3 s sans toucher, couvercle, disque
        // (lachee : elle va doucement sur la face choisie, et se remet droite ; ouverte : bien de face)
        if (!tient) {
            val c = cible
            if (!c.isNaN()) yaw += (c - yaw) * min(1f, dt * 6f)
            pitch += ((if (ouvrirVers > 0.5f) 4f else 8f) - pitch) * min(1f, dt * 4f)
        }
        ouvert += (ouvrirVers - ouvert) * min(1f, dt * 4f)
        sorti += (sortirVers - sorti) * min(1f, dt * 3.5f)
        tourne += dt * vTourne                                 // (sorti : il reste immobile ; seulement lance du doigt)
        vTourne *= (1 - min(1f, dt * 1.5f))

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(progFond)
        val ap = GLES20.glGetAttribLocation(progFond, "aP")
        GLES20.glEnableVertexAttribArray(ap)
        GLES20.glVertexAttribPointer(ap, 2, GLES20.GL_FLOAT, false, 8, fondBuf)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(ap)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)

        val b = buf ?: return
        val aspect = w.toFloat() / h
        Matrix.perspectiveM(proj, 0, if (aspect < 1) 34f else 26f, aspect, 0.1f, 60f)
        val dist = (if (aspect < 1) 6.6f else 5.2f) * (1 + 0.62f * ouvert * (1 - sorti * 0.6f)) / zoom
        val fov = if (aspect < 1) 34f else 26f
        unite = 2f * dist * zoom * kotlin.math.tan(Math.toRadians(fov / 2.0)).toFloat() / h
        val eye = floatArrayOf(panX, 0.35f + panY, dist)
        Matrix.setLookAtM(vueM, 0, eye[0], eye[1], eye[2], panX, -0.05f + panY, 0f, 0f, 1f, 0f)
        // boitier : ouvert a plat (175 degres, comme un livre), recentre
        Matrix.setIdentityM(mod, 0)
        Matrix.translateM(mod, 0, ouvert * 0.74f, 0f, 0f)
        Matrix.rotateM(mod, 0, pitch, 1f, 0f, 0f)
        Matrix.rotateM(mod, 0, yaw, 0f, 1f, 0f)
        System.arraycopy(mod, 0, lid, 0, 16)
        Matrix.translateM(lid, 0, -X, 0f, Z)
        Matrix.rotateM(lid, 0, -175f * ouvert, 0f, 1f, 0f)
        Matrix.translateM(lid, 0, X, 0f, -Z)
        // disque : pose sur l'ergot, ou sorti devant nous (immobile, sauf si on le lance du doigt)
        val pos = FloatArray(4)
        Matrix.multiplyMV(pos, 0, mod, 0, floatArrayOf(0f, 0f, zDisque, 1f), 0)
        val fx = 0f; val fy = 0.1f; val fz = dist * 0.42f
        Matrix.setIdentityM(dsq, 0)
        Matrix.translateM(dsq, 0, pos[0] + (fx - pos[0]) * sorti, pos[1] + (fy - pos[1]) * sorti, pos[2] + (fz - pos[2]) * sorti)
        Matrix.rotateM(dsq, 0, pitch * (1 - sorti) + 12f * sorti, 1f, 0f, 0f)
        Matrix.rotateM(dsq, 0, yaw * (1 - sorti) + (tourne % 360f) * sorti, 0f, 1f, 0f)
        Matrix.setIdentityM(sol, 0); Matrix.translateM(sol, 0, ouvert * 0.74f, 0f, 0f)

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
        val uMode = GLES20.glGetUniformLocation(prog, "uMode")
        GLES20.glUniform1i(GLES20.glGetUniformLocation(prog, "uTex"), 0)
        GLES20.glUniform3f(uEye, eye[0], eye[1], eye[2])
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        fun matrices(m: FloatArray) {
            Matrix.multiplyMM(tmp, 0, vueM, 0, m, 0)
            Matrix.multiplyMM(mvp, 0, proj, 0, tmp, 0)
            GLES20.glUniformMatrix4fv(uMVP, 1, false, mvp, 0)
            GLES20.glUniformMatrix4fv(uM, 1, false, m, 0)
        }
        fun dessiner(piece: Int) {
            for (g in maillage.groupes) {
                if (g.piece != piece) continue
                val tex = when (g.mat) { M_FULL -> texFull; M_AVANT -> texAvant; M_DISQUE -> texDisque; M_DESSOUS -> texDessous; M_OMBRE -> texOmbre; M_NOTICE -> texNotice; else -> 0 }
                if (tex != 0) { GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex); GLES20.glUniform1f(uUse, 1f) } else GLES20.glUniform1f(uUse, 0f)
                GLES20.glUniform4f(uCol, g.r, g.g, g.b, 1f)
                GLES20.glUniform1f(uGloss, g.brillant)
                GLES20.glUniform1f(uMode, g.mode.toFloat())
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, g.premier, g.nombre)
            }
        }
        // ombre au sol (elle reste sous la boite)
        GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA)
        GLES20.glDepthMask(false)
        matrices(sol); GLES20.glUniform1f(uFlat, 1f); dessiner(P_SOL)
        GLES20.glDepthMask(true); GLES20.glDisable(GLES20.GL_BLEND)
        GLES20.glUniform1f(uFlat, 0f)
        matrices(mod); dessiner(P_BAC)
        matrices(lid); dessiner(P_COUV)
        if (ouvert > 0.02f) {
            GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA)
            matrices(dsq); dessiner(P_DISQUE)
            GLES20.glDisable(GLES20.GL_BLEND)
        }
        GLES20.glDisableVertexAttribArray(aP); GLES20.glDisableVertexAttribArray(aN); GLES20.glDisableVertexAttribArray(aT)
    }
}

/** les shaders de la boite 3D (plastique, disque, film, fond), partages avec l'etagere (Etagere.kt) */
internal object Ombrage {
    val VS = """
        uniform mat4 uMVP; uniform mat4 uM;
        attribute vec3 aP; attribute vec3 aN; attribute vec2 aT;
        varying vec3 vN; varying vec2 vT; varying vec3 vW; varying vec3 vO;
        void main() { vec4 w = uM * vec4(aP, 1.0); vW = w.xyz; vO = aP; vN = (uM * vec4(aN, 0.0)).xyz; vT = aT; gl_Position = uMVP * vec4(aP, 1.0); }
    """
    // (rendu : lumiere de studio, reflets de l'environnement (deux boites a lumiere, sol, ciel violet) avec Fresnel ;
    //  modes : 0 plastique noir brillant, 1 disque face gravee (miroir + diffraction arc-en-ciel selon l'angle,
    //  centre en plastique transparent, anneau d'empilage, bande miroir gravee), 2 disque face imprimee,
    //  3 film transparent sur la jaquette, 4 plastique mat grene)
    val FS = """
        precision highp float;
        uniform sampler2D uTex; uniform float uUseTex; uniform vec4 uCol; uniform vec3 uEye; uniform float uFlat; uniform float uGloss; uniform float uMode;
        uniform mat4 uM;
        varying vec3 vN; varying vec2 vT; varying vec3 vW; varying vec3 vO;
        vec3 teinte(float x) { return clamp(abs(fract(x + vec3(0.0, 0.6667, 0.3333)) * 6.0 - 3.0) - 1.0, 0.0, 1.0); }
        float boite(vec3 r, vec3 d, float a, float b) { return smoothstep(a, b, dot(r, normalize(d))); }
        vec3 env(vec3 r) {
            vec3 c = mix(vec3(0.05, 0.03, 0.09), vec3(0.34, 0.24, 0.52), smoothstep(-0.1, 0.9, r.y));
            c += vec3(1.0, 0.98, 1.0) * boite(r, vec3(-0.45, 0.75, 0.55), 0.90, 0.965) * 2.2;
            c += vec3(0.85, 0.85, 1.0) * boite(r, vec3(0.75, 0.25, 0.65), 0.93, 0.975) * 1.3;
            c += vec3(1.0, 0.55, 0.95) * boite(r, vec3(0.0, -0.1, 1.0), 0.97, 0.99) * 0.5;
            return mix(c, vec3(0.015, 0.01, 0.02), smoothstep(-0.05, -0.35, r.y));
        }
        float grain(vec3 p) { return fract(sin(dot(floor(p * 900.0), vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
        void main() {
            vec4 base = uUseTex > 0.5 ? texture2D(uTex, vT) : uCol;
            if (uFlat > 0.5) { gl_FragColor = base; return; }
            vec3 n = normalize(vN);
            vec3 V = normalize(uEye - vW);
            if (dot(n, V) < 0.0) n = -n;
            vec3 R = reflect(-V, n);
            float cv = max(dot(n, V), 0.0);
            float F = 0.04 + 0.96 * pow(1.0 - cv, 5.0);
            vec3 L1 = normalize(vec3(-0.45, 0.75, 0.55)), L2 = normalize(vec3(0.75, 0.25, 0.65));
            float d = max(dot(n, L1), 0.0) * 0.72 + max(dot(n, L2), 0.0) * 0.32 + 0.14;
            vec3 c; float a = 1.0;
            if (uMode > 0.5 && uMode < 2.5) {
                vec2 q = vT - 0.5; float r = length(q) * 2.0;
                if (r < 0.1875) discard;
                vec3 t = normalize((uM * vec4(q.y, q.x, 0.0, 0.0)).xyz + vec3(1e-6));
                vec3 mir = env(R);
                if (r < 0.335) {                                   // (plastique transparent, anneau d'empilage)
                    float anneau = smoothstep(0.262, 0.268, r) * (1.0 - smoothstep(0.292, 0.298, r));
                    c = vec3(0.70, 0.74, 0.82) * 0.18 + mir * (0.35 + F) + vec3(0.25) * anneau;
                    a = 0.30 + 0.35 * anneau + 0.3 * F;
                } else if (r > 0.988) {                            // (bord transparent)
                    c = mir * 0.7 + vec3(0.15); a = 0.55;
                } else if (uMode < 1.5) {                          // (face gravee : miroir et arc-en-ciel)
                    // (diffraction des sillons, pas de 0,74 um : couleur seulement quand |sin a - sin b| = lambda / pas,
                    //  soit de 0,54 (violet) a 0,95 (rouge) ; ailleurs : le miroir argente)
                    float g1 = abs(dot(t, L1) + dot(t, V)), g2 = abs(dot(t, L2) + dot(t, V));
                    float k1 = (g1 - 0.54) / 0.41, k2 = (g2 - 0.54) / 0.41;
                    float f1 = smoothstep(0.0, 0.08, k1) * (1.0 - smoothstep(0.92, 1.0, k1));
                    float f2 = smoothstep(0.0, 0.08, k2) * (1.0 - smoothstep(0.92, 1.0, k2));
                    float w1 = pow(max(dot(n, normalize(L1 + V)), 0.0), 1.5), w2 = pow(max(dot(n, normalize(L2 + V)), 0.0), 1.5);
                    vec3 irid = teinte(0.78 - clamp(k1, 0.0, 1.0) * 0.78) * f1 * w1 * 1.3 + teinte(0.78 - clamp(k2, 0.0, 1.0) * 0.78) * f2 * w2 * 0.8;
                    vec3 argent = vec3(0.66, 0.68, 0.74);
                    if (r < 0.40) {                                // (bande miroir, texte grave)
                        c = mix(argent, mir, 0.75) * 1.05;
                        float ang = atan(q.y, q.x);
                        if (abs(r - 0.372) < 0.007 && fract(ang * 46.0) > 0.45) c *= 0.55;
                        if (abs(r - 0.40) < 0.004) c *= 0.6;
                    } else c = mix(argent * 0.95, mir, 0.45) + vec3(0.06) + irid * smoothstep(0.40, 0.44, r);
                } else {                                           // (face imprimee : l'image du jeu, encre brillante)
                    c = base.rgb * d + env(R) * F * 0.9 + vec3(pow(max(dot(n, normalize(L1 + V)), 0.0), 80.0) * 0.35);
                }
            } else {
                vec3 diff = base.rgb * d;
                float sp = pow(max(dot(n, normalize(L1 + V)), 0.0), 20.0 + 70.0 * uGloss) * 0.4 * uGloss;
                float refl = F * min(uGloss, 1.6);
                vec3 e = env(R);
                if (uMode > 3.5) {                                 // (plastique mat grene, eclaire un peu plus : l'interieur se voit bien)
                    diff = base.rgb * (0.30 + 0.85 * d) * (0.96 + 0.08 * grain(vO));
                    e = mix(vec3(dot(e, vec3(0.333))), e, 0.25) * 0.5; refl *= 0.6; sp *= 0.3;
                } else if (uMode > 2.5) {                          // (film transparent : reflet net des lumieres par-dessus la jaquette)
                    refl = 0.02 + 0.6 * pow(1.0 - cv, 5.0); sp *= 1.3; e = max(e - vec3(0.3, 0.22, 0.45), 0.0) * 1.3;
                    diff = base.rgb * (0.5 + 0.6 * d);                   // (la jaquette garde ses vraies couleurs)
                } else refl = max(refl, 0.06);
                c = diff + e * refl * 1.3 + vec3(sp);
            }
            gl_FragColor = vec4(c, a);
        }
    """
    val VS_FOND = "attribute vec2 aP; varying vec2 vP; void main() { vP = aP; gl_Position = vec4(aP, 0.999, 1.0); }"
    val FS_FOND = """
        precision mediump float; varying vec2 vP;
        void main() {
            vec3 haut = vec3(0.071, 0.024, 0.141), bas = vec3(0.027, 0.008, 0.059);
            vec3 c = mix(bas, haut, vP.y * 0.5 + 0.5);
            float g = 1.0 - smoothstep(0.0, 1.1, length(vP - vec2(0.0, 0.15)));
            c += vec3(0.42, 0.14, 0.62) * g * 0.35;
            gl_FragColor = vec4(c, 1.0);
        }
    """
}
