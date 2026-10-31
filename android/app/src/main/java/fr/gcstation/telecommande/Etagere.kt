package fr.gcstation.telecommande

// Appli : l'etagere de collection. Toutes les boites 3D des jeux, rangees cote a cote, la tranche vers nous, sur une
// planche devant un mur. Un doigt : faire defiler (avec elan, la boite la plus proche se cale au milieu) ; toucher une
// boite : elle sort de l'etagere et se tourne vers nous (jaquette avant) ; la retoucher : la boite s'ouvre (Boite3D.kt) ;
// toucher ailleurs : elle se range. Deux doigts : zoomer. Meme boite et meme rendu que la boite 3D (BoiteGeo.kt,
// Ombrage) ; les jaquettes des boites autour du milieu sont chargees au fur et a mesure (telephone, sinon GameTDB).

import android.content.Context
import android.graphics.Bitmap
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import fr.gcstation.telecommande.Geo.M_AVANT
import fr.gcstation.telecommande.Geo.M_FULL
import fr.gcstation.telecommande.Geo.P_BAC
import fr.gcstation.telecommande.Geo.P_COUV
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.ConcurrentHashMap
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** une place de l'etagere : un jeu (indice dans la liste), son nom et son rayon (lettre, editeur, annee, temps de jeu) */
class Place(val jeu: Int, val nom: String, val rayon: String = "")

private val TRIS = listOf("Nom", "Éditeur", "Année", "Temps de jeu")
private fun lettre(s: String): String {
    val c = java.text.Normalizer.normalize(s.trim(), java.text.Normalizer.Form.NFD).firstOrNull { it.isLetterOrDigit() }?.uppercaseChar() ?: '#'
    return if (c in 'A'..'Z') c.toString() else "#"
}
private fun tranche(m: Int) = when { m >= 1200 -> "20 h et plus"; m >= 300 -> "5 à 20 h"; m >= 60 -> "1 à 5 h"; m > 0 -> "Moins d'1 h"; else -> "Jamais joué" }
/** les places dans l'ordre du tri (le rayon de chaque jeu s'affiche en haut ; plus d'intercalaire dans l'etagere) */
fun places(jeux: List<Jeu>, tri: Int, fiches: Map<String, Fiches.Fiche>): List<Place> {
    val nom = compareBy<Int> { jeux[it].nom.lowercase() }
    val ordre = jeux.indices.sortedWith(when (tri) {
        1 -> compareBy<Int> { val e = fiches[jeux[it].id]?.editeur.orEmpty(); if (e.isEmpty()) "\uFFFF" else e.lowercase() }.then(nom)
        2 -> compareBy<Int> { fiches[jeux[it].id]?.annee?.takeIf { a -> a > 0 } ?: 9999 }.then(nom)
        3 -> compareByDescending<Int> { jeux[it].minutes }.then(nom)
        else -> nom
    })
    val l = ArrayList<Place>()
    for (k in ordre) {
        val g = jeux[k]
        val r = when (tri) {
            1 -> fiches[g.id]?.editeur?.takeIf { it.isNotEmpty() } ?: "Éditeur inconnu"
            2 -> fiches[g.id]?.annee?.takeIf { it > 0 }?.toString() ?: "Année inconnue"
            3 -> tranche(g.minutes)
            else -> lettre(g.nom)
        }
        l.add(Place(k, g.nom, r))
    }
    return l
}

@Composable
fun EtagereEcran(act: StationActivity, jeux: List<Jeu>, fiche: (Jeu) -> Unit, fermer: () -> Unit) {
    var vue by remember { mutableStateOf<EtagereVue?>(null) }
    var centre by remember { mutableIntStateOf(0) }
    var sortie by remember { mutableIntStateOf(-1) }
    var boite by remember { mutableStateOf<Jeu?>(null) }
    var tri by remember { mutableIntStateOf(try { act.getSharedPreferences("gcs", 0).getInt("etagere_tri", 0) } catch (e: Exception) { 0 }) }
    var fiches by remember { mutableStateOf<Map<String, Fiches.Fiche>>(emptyMap()) }
    LaunchedEffect(tri) { if (tri == 1 || tri == 2) fiches = Fiches.charger(Hors.dir(act)) }
    val pl = remember(jeux, tri, fiches) { places(jeux, tri, fiches) }
    // jaquettes des boites autour du milieu (les plus proches d'abord)
    LaunchedEffect(vue, centre) {
        val v = vue ?: return@LaunchedEffect
        val voulues = (centre - 14..centre + 14).filter { it in pl.indices && pl[it].jeu >= 0 }.sortedBy { abs(it - centre) }   // (tout ce qui se voit, et de la marge)
        v.garder(voulues.toSet())
        for (k in voulues) {
            if (v.aDeja(k)) continue
            val im = JaquettesHQ.chargerEtagere(Hors.dir(act), jeux[pl[k].jeu].id)
            // (reduites pour l'etagere : jaquette complete 1024 x 512, la tranche reste nette ; avant seule 512 x 1024)
            val f = im.full?.let { if (it.width > 1024 || it.height > 512) Bitmap.createScaledBitmap(it, 1024, 512, true) else it }
            val a = im.avant?.let { if (it.width > 512 || it.height > 1024) Bitmap.createScaledBitmap(it, 512, 1024, true) else it }
            v.images(k, f, a)
        }
    }
    val jeuDe = { k: Int -> pl.getOrNull(k)?.jeu?.takeIf { it >= 0 }?.let { jeux[it] } }
    Dialog(onDismissRequest = fermer, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(C.Fond)) {
            key(pl) {                                            // (nouveau tri : nouvelle etagere)
                AndroidView({ c -> EtagereVue(c, pl.map { if (it.jeu < 0) it.nom else null }).also { b ->
                    b.surCentre = { k -> centre = k }; b.surSortie = { k -> sortie = k }; b.surOuvrir = { k -> jeuDe(k)?.let { boite = it } }
                    vue = b; centre = 0; sortie = -1
                    } }, Modifier.fillMaxSize())
            }
            Column(Modifier.fillMaxWidth().statusBarsPadding().padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("Étagère", color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                        val rayon = pl.getOrNull(centre)?.rayon?.takeIf { it.isNotEmpty() }
                        Doux(jeux.size.toString() + " jeu" + (if (jeux.size > 1) "x" else "") + (if (rayon != null) " · rayon « $rayon »" else ""), taille = 12, lignes = 1)
                    }
                    Box(Modifier.size(44.dp).verre(CircleShape).appui(fermer), contentAlignment = Alignment.Center) { Icon(Ic.Fermer, null, tint = C.Texte) }
                }
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    TRIS.forEachIndexed { i, t -> Puce(t, tri == i) { tri = i; try { act.getSharedPreferences("gcs", 0).edit().putInt("etagere_tri", i).apply() } catch (e: Exception) { } } }
                }
                if ((tri == 1 || tri == 2) && fiches.isEmpty()) Doux("Fiches des jeux (GameTDB) : chargement…", Modifier.padding(top = 6.dp), 11, C.Pale)
            }
            Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().navigationBarsPadding().padding(16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                val p = pl.getOrNull(if (sortie >= 0) sortie else centre)
                if (p != null) Text(if (p.jeu < 0) "Rayon « " + p.nom + " »" else p.nom, color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                val fi = p?.takeIf { it.jeu >= 0 }?.let { fiches[jeux[it.jeu].id] }
                if (fi != null) Doux(listOf(fi.editeur, fi.annee.takeIf { it > 0 }?.toString().orEmpty(), fi.genre).filter { it.isNotEmpty() }.joinToString(" · "), taille = 12, lignes = 1)
                Spacer(Modifier.height(4.dp))
                Doux(if (sortie >= 0) "Toucher la boîte : l'ouvrir · toucher ailleurs : la ranger" else "Glisser : parcourir · toucher une boîte : la sortir · deux doigts : zoomer",
                    taille = 11, couleur = C.Pale, centre = true)
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    if (sortie < 0) {
                        Bouton("◀", Modifier.weight(1f)) { vue?.aller(centre - 1) }
                        Bouton("Sortir la boîte", Modifier.weight(2f), plein = true, actif = p != null && p.jeu >= 0) { vue?.sortir(centre) }
                        Bouton("▶", Modifier.weight(1f)) { vue?.aller(centre + 1) }
                    } else {
                        Bouton("Ranger", Modifier.weight(1f)) { vue?.ranger() }
                        Bouton("Ouvrir", Modifier.weight(1f), plein = true) { jeuDe(sortie)?.let { boite = it } }
                        Bouton("Fiche du jeu", Modifier.weight(1f)) { jeuDe(sortie)?.let { fiche(it) } }
                    }
                }
            }
        }
    }
    boite?.let { g -> Boite3DEcran(act, g.id, g.nom, null) { boite = null } }
}

class EtagereVue(ctx: Context, intercalaires: List<String?>) : GLSurfaceView(ctx) {
    private val r = RenduEtagere(intercalaires)
    var surCentre: ((Int) -> Unit)? = null
    var surSortie: ((Int) -> Unit)? = null
    var surOuvrir: ((Int) -> Unit)? = null
    private val charges = ConcurrentHashMap.newKeySet<Int>()
    private val pinch = ScaleGestureDetector(ctx, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(d: ScaleGestureDetector): Boolean { r.zoom = (r.zoom * d.scaleFactor).coerceIn(0.8f, 3.5f); return true }
    })
    private val gestes = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean { r.vScroll = 0f; r.tient = true; return true }
        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            if (e2.pointerCount > 1 || pinch.isInProgress) return true
            if (r.sortieVers >= 0) ranger()
            r.scroll = (r.scroll + dx * r.unite).coerceIn(-0.3f, (r.n - 1) * RenduEtagere.PAS + 0.3f); dire(); return true
        }
        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            if (e2.pointerCount > 1 || pinch.isInProgress) return true
            r.vScroll = -vx * r.unite; return true
        }
        override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
            val k = r.boiteSous(e.x, e.y)
            when {
                r.sortieVers >= 0 && k == r.sortieVers -> post { surOuvrir?.invoke(k) }
                r.sortieVers >= 0 -> ranger()
                k >= 0 && r.noms[k] != null -> aller(k)
                k >= 0 -> sortir(k)
            }
            return true
        }
        override fun onDoubleTap(e: MotionEvent): Boolean { if (r.sortieVers >= 0) post { surOuvrir?.invoke(r.sortieVers) } else { r.zoom = 1f }; return true }
    })

    init {
        setEGLContextClientVersion(2)
        setEGLConfigChooser(ChoixConfig())
        setRenderer(r)
        renderMode = RENDERMODE_CONTINUOUSLY
        r.surCentre = { k -> post { surCentre?.invoke(k) } }
    }

    private fun dire() { val k = r.sortieVers; post { surSortie?.invoke(k) } }
    fun aller(k: Int) { ranger(); r.cible = k.coerceIn(0, r.n - 1) }
    fun sortir(k: Int) { if (k !in 0 until r.n || r.noms[k] != null) return; r.cible = k; r.sortieVers = k; dire() }
    fun ranger() { if (r.sortieVers >= 0) { r.sortieVers = -1; dire() } }
    fun aDeja(k: Int) = charges.contains(k)
    fun images(k: Int, full: Bitmap?, avant: Bitmap?) { charges.add(k); queueEvent { r.images(k, full, avant) } }
    fun garder(s: Set<Int>) { charges.retainAll(s); queueEvent { r.garder(s) } }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        pinch.onTouchEvent(e)
        gestes.onTouchEvent(e)
        if (e.actionMasked == MotionEvent.ACTION_UP || e.actionMasked == MotionEvent.ACTION_CANCEL) r.lacher()
        return true
    }
}

private class RenduEtagere(val noms: List<String?>) : GLSurfaceView.Renderer {
    val n = noms.size
    private val texNom = HashMap<Int, Int>()                      // (l'etiquette de chaque intercalaire)
    private lateinit var mInter: Maillage
    companion object { const val PAS = 2 * Geo.Z + 0.014f }       // (une boite tous les 17 mm, comme sur une vraie etagere)
    @Volatile var scroll = 0f
    @Volatile var vScroll = 0f
    @Volatile var tient = false
    @Volatile var zoom = 1f
    @Volatile var unite = 0.004f
    @Volatile var cible = -1                                       // (boite a amener au milieu ; -1 : la plus proche)
    @Volatile var sortieVers = -1                                  // (boite sortie, -1 : aucune)
    var surCentre: ((Int) -> Unit)? = null
    private var dernierCentre = -1
    private var sortie = FloatArray(0)                             // (0 rangee, 1 sortie : par boite)

    fun lacher() { tient = false; if (abs(vScroll) < 0.05f && cible < 0) cible = Math.round(scroll / PAS).coerceIn(0, n - 1) }

    private var prog = 0
    private var progFond = 0
    private var w = 1
    private var h = 1
    private var aniso = 1f
    private var maxTex = 2048
    private var dernier = 0L
    private val texFull = HashMap<Int, Int>()
    private val texAvant = HashMap<Int, Int>()
    private val garde = HashMap<Int, Pair<Bitmap?, Bitmap?>>()
    private val proj = FloatArray(16)
    private val vueM = FloatArray(16)
    private val mod = FloatArray(16)
    private val tmp = FloatArray(16)
    private val mvp = FloatArray(16)
    private val inv = FloatArray(16)
    private lateinit var mFull: Maillage
    private lateinit var mAvant: Maillage
    private lateinit var mRien: Maillage
    private lateinit var mPlanche: Maillage
    private val bufs = HashMap<Maillage, FloatBuffer>()

    fun images(k: Int, full: Bitmap?, avant: Bitmap?) {
        garde[k] = full to avant
        texFull.remove(k)?.let { GLES20.glDeleteTextures(1, intArrayOf(it), 0) }
        texAvant.remove(k)?.let { GLES20.glDeleteTextures(1, intArrayOf(it), 0) }
        if (full != null) texFull[k] = texture(full) else if (avant != null) texAvant[k] = texture(avant)
    }
    fun garder(s: Set<Int>) {
        for (k in texFull.keys.toList()) if (k !in s) { GLES20.glDeleteTextures(1, intArrayOf(texFull[k]!!), 0); texFull.remove(k) }
        for (k in texAvant.keys.toList()) if (k !in s) { GLES20.glDeleteTextures(1, intArrayOf(texAvant[k]!!), 0); texAvant.remove(k) }
        garde.keys.retainAll(s)
    }

    private fun pot(k: Int): Int { var p = 1; while (p < k) p = p shl 1; return min(p, min(maxTex, 2048)) }
    private fun texture(b: Bitmap): Int {
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
        if (aniso > 1f) GLES20.glTexParameterf(GLES20.GL_TEXTURE_2D, 0x84FE, aniso)
        if (s !== b) s.recycle()
        return id[0]
    }

    /** un intercalaire de rayon : une carte fine qui depasse des boites, l'etiquette sur la tranche vers nous */
    private fun intercalaire(): Maillage {
        val m = Maillage()
        fun p(x: Float, y: Float, z: Float, nx: Float, ny: Float, nz: Float, u: Float = 0f, v: Float = 0f) = floatArrayOf(x, y, z, nx, ny, nz, u, v)
        val x0 = -PAS * 0.42f; val x1 = PAS * 0.42f; val y0 = -Geo.Y; val y1 = Geo.Y + 0.30f; val zA = Geo.X + 0.03f; val zB = -Geo.X
        m.commencer(P_BAC, M_AVANT, 0, 0.6f, 4)
        m.quad(p(x0, y0, zA, 0f, 0f, 1f, 0f, 1f), p(x1, y0, zA, 0f, 0f, 1f, 1f, 1f), p(x1, y1, zA, 0f, 0f, 1f, 1f, 0f), p(x0, y1, zA, 0f, 0f, 1f, 0f, 0f))
        m.commencer(P_BAC, Geo.M_COULEUR, 0xB04CD8, 0.7f, 4)
        m.quad(p(x0, y1, zA, 0f, 1f, 0f), p(x1, y1, zA, 0f, 1f, 0f), p(x1, y1, zB, 0f, 1f, 0f), p(x0, y1, zB, 0f, 1f, 0f))
        m.quad(p(x0, y0, zB, -1f, 0f, 0f), p(x0, y0, zA, -1f, 0f, 0f), p(x0, y1, zA, -1f, 0f, 0f), p(x0, y1, zB, -1f, 0f, 0f))
        m.quad(p(x1, y0, zA, 1f, 0f, 0f), p(x1, y0, zB, 1f, 0f, 0f), p(x1, y1, zB, 1f, 0f, 0f), p(x1, y1, zA, 1f, 0f, 0f))
        m.finir()
        return m
    }
    private fun etiquette(t: String): Bitmap {
        val w = 128; val h = 1024
        val b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val c = android.graphics.Canvas(b)
        val p = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
        p.shader = android.graphics.LinearGradient(0f, 0f, 0f, h.toFloat(), 0xFFFF4FD8.toInt(), 0xFF7A3CFF.toInt(), android.graphics.Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, w.toFloat(), h.toFloat(), p)
        p.shader = null; p.color = 0x33FFFFFF; c.drawRect(0f, 0f, w.toFloat(), 120f, p)
        p.color = 0xFFFFFFFF.toInt(); p.typeface = android.graphics.Typeface.DEFAULT_BOLD; p.textAlign = android.graphics.Paint.Align.CENTER
        if (t.length <= 2) { p.textSize = 96f; c.drawText(t, w / 2f, 100f, p) }
        else {                                                     // (le nom ecrit le long, de haut en bas, comme sur une tranche)
            c.save(); c.rotate(90f, w / 2f, w / 2f)
            var ts = 64f; p.textSize = ts; p.textAlign = android.graphics.Paint.Align.LEFT
            while (p.measureText(t) > h - 170 && ts > 26f) { ts -= 2f; p.textSize = ts }
            c.drawText(t, 150f, w / 2f + ts * 0.35f, p)
            c.restore()
        }
        return b
    }

    /** la planche (bois sombre) et le mur du fond, larges : ils ne defilent pas (uniformes, ca ne se voit pas) */
    private fun planche(): Maillage {
        val m = Maillage()
        fun p(x: Float, y: Float, z: Float, nx: Float, ny: Float, nz: Float) = floatArrayOf(x, y, z, nx, ny, nz, 0f, 0f)
        val y0 = -Geo.Y; val e = 0.09f; val zA = 0.98f; val zB = -0.86f; val L = 30f
        m.commencer(P_BAC, Geo.M_COULEUR, 0x4A3528, 0.6f, 4)
        m.quad(p(-L, y0, zA, 0f, 1f, 0f), p(L, y0, zA, 0f, 1f, 0f), p(L, y0, zB, 0f, 1f, 0f), p(-L, y0, zB, 0f, 1f, 0f))
        m.commencer(P_BAC, Geo.M_COULEUR, 0x5C4434, 0.7f, 4)
        m.quad(p(-L, y0 - e, zA, 0f, 0f, 1f), p(L, y0 - e, zA, 0f, 0f, 1f), p(L, y0, zA, 0f, 0f, 1f), p(-L, y0, zA, 0f, 0f, 1f))
        m.commencer(P_BAC, Geo.M_COULEUR, 0x1C1430, 0.4f, 4)
        m.quad(p(-L, y0, zB, 0f, 0f, 1f), p(L, y0, zB, 0f, 0f, 1f), p(L, 6f, zB, 0f, 0f, 1f), p(-L, 6f, zB, 0f, 0f, 1f))
        m.finir()
        return m
    }
    private fun tampon(m: Maillage): FloatBuffer = bufs.getOrPut(m) {
        val fb = ByteBuffer.allocateDirect(m.v.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
        for (x in m.v) fb.put(x)
        fb.position(0); fb
    }

    private fun shader(t: Int, src: String): Int { val s = GLES20.glCreateShader(t); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s); return s }
    private fun programme(vs: String, fs: String): Int {
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, vs)); GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, fs))
        GLES20.glLinkProgram(p)
        return p
    }
    private val fondBuf: FloatBuffer = ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f)); position(0) }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        prog = programme(Ombrage.VS, Ombrage.FS)
        progFond = programme(Ombrage.VS_FOND, Ombrage.FS_FOND)
        val ext = GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: ""
        if (ext.contains("GL_EXT_texture_filter_anisotropic")) { val a = FloatArray(1); GLES20.glGetFloatv(0x84FF, a, 0); aniso = max(1f, a[0]) }
        val mx = IntArray(1); GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, mx, 0); maxTex = max(2048, mx[0])
        if (!::mFull.isInitialized) { mFull = boitier(true, false); mAvant = boitier(false, true); mRien = boitier(false, false); mPlanche = planche(); mInter = intercalaire() }
        texNom.clear()
        bufs.clear()
        texFull.clear(); texAvant.clear()
        for ((k, im) in garde) { if (im.first != null) texFull[k] = texture(im.first!!) else if (im.second != null) texAvant[k] = texture(im.second!!) }
        if (sortie.size != n) sortie = FloatArray(n)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        w = width; h = max(1, height)
        GLES20.glViewport(0, 0, w, h)
    }

    /** la boite sous un point de l'ecran (a la hauteur des tranches) ; -1 : aucune */
    fun boiteSous(sx: Float, sy: Float): Int {
        // (rayon depuis l'oeil, coupe le plan des tranches z = 0,75)
        Matrix.multiplyMM(tmp, 0, proj, 0, vueM, 0)
        if (!Matrix.invertM(inv, 0, tmp, 0)) return -1
        val nx = 2f * sx / w - 1f; val ny = 1f - 2f * sy / h
        val a = FloatArray(4); val b = FloatArray(4)
        Matrix.multiplyMV(a, 0, inv, 0, floatArrayOf(nx, ny, -1f, 1f), 0); Matrix.multiplyMV(b, 0, inv, 0, floatArrayOf(nx, ny, 1f, 1f), 0)
        for (v in arrayOf(a, b)) { v[0] /= v[3]; v[1] /= v[3]; v[2] /= v[3] }
        val zt = Geo.X
        val t = (zt - a[2]) / (b[2] - a[2])
        val x = a[0] + (b[0] - a[0]) * t; val y = a[1] + (b[1] - a[1]) * t
        if (y < -Geo.Y - 0.1f || y > Geo.Y + 0.6f) return -1
        val k = Math.round((x + scroll) / PAS)
        return if (k in 0 until n) k else -1
    }

    override fun onDrawFrame(gl: GL10?) {
        val now = SystemClock.uptimeMillis()
        val dt = if (dernier == 0L) 0.016f else min(0.05f, (now - dernier) / 1000f)
        dernier = now
        if (!tient) {
            if (cible >= 0) {
                val c = cible * PAS
                scroll += (c - scroll) * min(1f, dt * 7f)
                if (abs(c - scroll) < 0.0005f) { scroll = c }
            } else if (abs(vScroll) > 0.01f) {
                scroll = (scroll + vScroll * dt).coerceIn(-0.3f, (n - 1) * PAS + 0.3f)
                vScroll *= (1 - min(1f, dt * 2.6f))
                if (abs(vScroll) < 0.3f) { vScroll = 0f; cible = Math.round(scroll / PAS).coerceIn(0, n - 1) }
            }
        } else cible = -1
        val c = Math.round(scroll / PAS).coerceIn(0, max(0, n - 1))
        if (c != dernierCentre) { dernierCentre = c; surCentre?.invoke(c) }
        for (k in sortie.indices) sortie[k] += ((if (k == sortieVers) 1f else 0f) - sortie[k]) * min(1f, dt * 5f)

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(progFond)
        val ap = GLES20.glGetAttribLocation(progFond, "aP")
        GLES20.glEnableVertexAttribArray(ap)
        GLES20.glVertexAttribPointer(ap, 2, GLES20.GL_FLOAT, false, 8, fondBuf)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(ap)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)

        val aspect = w.toFloat() / h
        val fov = if (aspect < 1) 40f else 30f
        Matrix.perspectiveM(proj, 0, fov, aspect, 0.1f, 80f)
        val tv = kotlin.math.tan(Math.toRadians(fov / 2.0)).toFloat()
        val dist = (if (aspect < 1) 0.85f / (tv * aspect) else 1.3f / tv) / zoom   // (portrait : une dizaine de tranches en largeur)
        unite = 2f * dist * tv / h
        val eye = floatArrayOf(0f, 0.55f, dist + Geo.X)
        Matrix.setLookAtM(vueM, 0, eye[0], eye[1], eye[2], 0f, -0.1f, 0f, 0f, 1f, 0f)

        GLES20.glUseProgram(prog)
        val aP = GLES20.glGetAttribLocation(prog, "aP"); val aN = GLES20.glGetAttribLocation(prog, "aN"); val aT = GLES20.glGetAttribLocation(prog, "aT")
        val uMVP = GLES20.glGetUniformLocation(prog, "uMVP"); val uM = GLES20.glGetUniformLocation(prog, "uM")
        val uUse = GLES20.glGetUniformLocation(prog, "uUseTex"); val uCol = GLES20.glGetUniformLocation(prog, "uCol")
        val uGloss = GLES20.glGetUniformLocation(prog, "uGloss"); val uMode = GLES20.glGetUniformLocation(prog, "uMode")
        GLES20.glUniform1i(GLES20.glGetUniformLocation(prog, "uTex"), 0)
        GLES20.glUniform3f(GLES20.glGetUniformLocation(prog, "uEye"), eye[0], eye[1], eye[2])
        GLES20.glUniform1f(GLES20.glGetUniformLocation(prog, "uFlat"), 0f)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glEnableVertexAttribArray(aP); GLES20.glEnableVertexAttribArray(aN); GLES20.glEnableVertexAttribArray(aT)
        fun lier(m: Maillage) {
            val b = tampon(m)
            b.position(0); GLES20.glVertexAttribPointer(aP, 3, GLES20.GL_FLOAT, false, 32, b)
            b.position(3); GLES20.glVertexAttribPointer(aN, 3, GLES20.GL_FLOAT, false, 32, b)
            b.position(6); GLES20.glVertexAttribPointer(aT, 2, GLES20.GL_FLOAT, false, 32, b)
            b.position(0)
        }
        fun matrices(m: FloatArray) {
            Matrix.multiplyMM(tmp, 0, vueM, 0, m, 0); Matrix.multiplyMM(mvp, 0, proj, 0, tmp, 0)
            GLES20.glUniformMatrix4fv(uMVP, 1, false, mvp, 0); GLES20.glUniformMatrix4fv(uM, 1, false, m, 0)
        }
        fun dessiner(m: Maillage, tf: Int, ta: Int) {
            for (g in m.groupes) {
                if (g.piece != P_BAC && g.piece != P_COUV) continue
                val t = when (g.mat) { M_FULL -> tf; M_AVANT -> ta; else -> 0 }
                if (t != 0) { GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t); GLES20.glUniform1f(uUse, 1f) } else GLES20.glUniform1f(uUse, 0f)
                GLES20.glUniform4f(uCol, g.r, g.g, g.b, 1f)
                GLES20.glUniform1f(uGloss, g.brillant); GLES20.glUniform1f(uMode, g.mode.toFloat())
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, g.premier, g.nombre)
            }
        }
        // la planche et le mur
        lier(mPlanche); Matrix.setIdentityM(mod, 0); matrices(mod); dessiner(mPlanche, 0, 0)
        // les boites visibles (tranche vers nous ; la boite sortie avance et se tourne, jaquette avant vers nous)
        val vis = ((dist + 2f) * tv * aspect / PAS).toInt() + 3
        var lie: Maillage? = null
        for (k in max(0, c - vis)..min(n - 1, c + vis)) {
            val nom = noms[k]
            if (nom != null) {                                   // (intercalaire de rayon)
                val t = texNom.getOrPut(k) { val b = etiquette(nom); val id = texture(b); b.recycle(); id }
                if (lie !== mInter) { lier(mInter); lie = mInter }
                Matrix.setIdentityM(mod, 0); Matrix.translateM(mod, 0, k * PAS - scroll, 0f, 0f)
                matrices(mod); dessiner(mInter, 0, t)
                continue
            }
            val tf = texFull[k] ?: 0; val ta = texAvant[k] ?: 0
            val m = if (tf != 0) mFull else if (ta != 0) mAvant else mRien
            if (m !== lie) { lier(m); lie = m }
            val s = sortie[k]
            val e = s * s * (3 - 2 * s)
            Matrix.setIdentityM(mod, 0)
            Matrix.translateM(mod, 0, k * PAS - scroll, 0.10f * e, 1.0f * e)
            Matrix.rotateM(mod, 0, 90f - 76f * e, 0f, 1f, 0f)
            Matrix.rotateM(mod, 0, -6f * e, 0f, 0f, 1f)
            matrices(mod)
            dessiner(m, tf, ta)
        }
        GLES20.glDisableVertexAttribArray(aP); GLES20.glDisableVertexAttribArray(aN); GLES20.glDisableVertexAttribArray(aT)
    }
}
