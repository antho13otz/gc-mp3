package fr.gcstation.telecommande

// Appli : la notice en 3D (OpenGL ES 2, antialiasing), qu'on manipule fermee comme ouverte. Forme : LivretGeo.kt.
// Un doigt : tourner la notice dans tous les sens (avec elan) ; deux doigts : zoomer a l'endroit pince et deplacer ;
// toucher a droite / a gauche : page suivante / precedente (la feuille tourne en se courbant) ; double toucher :
// recadrer (ou ouvrir / refermer si deja cadree). Les images des pages autour de celle ouverte sont chargees au fur et
// a mesure (telephone, sinon console) ; les autres restent du papier blanc (cachees sous les pages visibles).

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Brush
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
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.ConcurrentHashMap
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.max
import kotlin.math.min

@Composable
fun Notice3DEcran(act: StationActivity, id: String, nom: String, n: Int, fermer: () -> Unit) {
    var vue by remember { mutableStateOf<NoticeVue?>(null) }
    var feuille by remember { mutableIntStateOf(0) }
    var aPlat by remember { mutableStateOf(false) }
    val L = Livret.feuilles(n)
    // pages a avoir en image : couverture, dos, et celles autour de la feuille ouverte (les plus proches d'abord)
    LaunchedEffect(vue, feuille) {
        val v = vue ?: return@LaunchedEffect
        val c = 2 * feuille
        val voulues = (listOf(1, n) + ((c - 5)..(c + 6)).sortedBy { kotlin.math.abs(it - c - 0.5f) }).filter { it in 1..n }.distinct()
        v.garder(voulues.toSet())
        for (p in voulues) {
            if (v.aDeja(p)) continue
            Notices.page(Hors.dir(act), id, p)?.let { v.page(p, it.asAndroidBitmap()) }
        }
    }
    Dialog(onDismissRequest = fermer, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(C.Fond)) {
            AndroidView({ c -> NoticeVue(c, n).also { b -> b.surFeuille = { f -> feuille = f }; vue = b } }, Modifier.fillMaxSize())
            Row(Modifier.fillMaxWidth().background(Brush.verticalGradient(0f to C.Fond, 0.75f to C.Fond, 1f to Color.Transparent)).statusBarsPadding().padding(16.dp).padding(bottom = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Notice · $nom", color = C.Texte, fontSize = 17.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Doux(when {
                        feuille == 0 -> "Couverture · $n pages"
                        feuille >= L -> "Dos · $n pages"
                        else -> Livret.recto(n, feuille).let { r -> if (r > 0) "Pages " + Livret.verso(n, feuille - 1) + "–" + r + " / " + n else "Page " + Livret.verso(n, feuille - 1) + " / " + n }
                    }, taille = 12)
                }
                Box(Modifier.size(44.dp).verre(CircleShape).appui(fermer), contentAlignment = Alignment.Center) { Icon(Ic.Fermer, null, tint = C.Texte) }
            }
            Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().background(Brush.verticalGradient(0f to Color.Transparent, 0.15f to C.Fond, 1f to C.Fond)).navigationBarsPadding().padding(16.dp).padding(top = 14.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Doux("Un doigt : tourner la notice · deux doigts : zoomer et déplacer · toucher à droite / à gauche : tourner une page · double toucher : recadrer",
                    taille = 11, couleur = C.Pale, centre = true)
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally)) {
                    Puce("De face", false) { vue?.deFace() }
                    Puce("À plat", false) { aPlat = true }
                }
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    Bouton("◀", Modifier.weight(1f)) { vue?.tourner(-1) }
                    Bouton(if (feuille == 0) "Ouvrir" else "Refermer", Modifier.weight(2f), plein = true) { vue?.aller(if (feuille == 0) 1 else 0) }
                    Bouton("▶", Modifier.weight(1f)) { vue?.tourner(1) }
                }
            }
        }
    }
    if (aPlat) NoticeEcran(act, id, nom, n) { aPlat = false }
}

class NoticeVue(ctx: Context, n: Int) : GLSurfaceView(ctx) {
    private val r = RenduNotice(n)
    var surFeuille: ((Int) -> Unit)? = null
    private val charges = ConcurrentHashMap.newKeySet<Int>()
    private var fx0 = 0f
    private var fy0 = 0f
    private val pinch = ScaleGestureDetector(ctx, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScaleBegin(d: ScaleGestureDetector): Boolean { fx0 = d.focusX; fy0 = d.focusY; return true }
        override fun onScale(d: ScaleGestureDetector): Boolean {
            r.deplacer(d.focusX - fx0, d.focusY - fy0); fx0 = d.focusX; fy0 = d.focusY
            r.zoomer(d.scaleFactor, d.focusX - width / 2f, d.focusY - height / 2f); return true
        }
    })
    private val gestes = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean { r.vYaw = 0f; r.vPitch = 0f; r.tient = true; return true }
        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            if (e2.pointerCount > 1 || pinch.isInProgress) return true
            val k = 160f / resources.displayMetrics.densityDpi
            r.yaw -= dx * 0.6f * k; r.pitch = (r.pitch - dy * 0.6f * k).coerceIn(-89f, 89f); return true
        }
        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            if (e2.pointerCount > 1 || pinch.isInProgress) return true
            val k = 160f / resources.displayMetrics.densityDpi
            r.vYaw = vx * 0.6f * k; r.vPitch = vy * 0.6f * k; return true
        }
        override fun onSingleTapConfirmed(e: MotionEvent): Boolean { tourner(if (e.x > width / 2f) 1 else -1); return true }
        override fun onDoubleTap(e: MotionEvent): Boolean {
            if (r.zoom > 1.05f || r.decale() || r.penche()) deFace() else aller(if (r.posVers > 0.5f) 0 else 1)
            return true
        }
    })

    init {
        setEGLContextClientVersion(2)
        setEGLConfigChooser(ChoixConfig())
        setRenderer(r)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    fun aller(f: Int) { val k = f.coerceIn(0, Livret.feuilles(r.n)); r.posVers = k.toFloat(); post { surFeuille?.invoke(k) } }
    fun tourner(d: Int) = aller(Math.round(r.posVers) + d)
    fun deFace() { r.recadrer() }
    fun aDeja(p: Int) = charges.contains(p)
    fun page(p: Int, b: Bitmap) { charges.add(p); queueEvent { r.page(p, b) } }
    /** garde seulement ces pages en texture (memoire) */
    fun garder(s: Set<Int>) { charges.retainAll(s); queueEvent { r.garder(s) } }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        pinch.onTouchEvent(e)
        gestes.onTouchEvent(e)
        if (e.actionMasked == MotionEvent.ACTION_UP || e.actionMasked == MotionEvent.ACTION_CANCEL) r.tient = false
        return true
    }
}

private class RenduNotice(val n: Int) : GLSurfaceView.Renderer {
    @Volatile var yaw = 0f
    @Volatile var pitch = 0f
    @Volatile var vYaw = 0f
    @Volatile var vPitch = 0f
    @Volatile var tient = false
    @Volatile var zoom = 1f
    @Volatile var posVers = 0f
    @Volatile private var panX = 0f
    @Volatile private var panY = 0f
    @Volatile private var unite = 0.005f
    @Volatile private var auRepos = false                       // (en train de revenir de face)
    private var pos = 0f
    private var ratio = 480f / 816f

    fun zoomer(f: Float, dx: Float, dy: Float) {
        val z0 = zoom; val z1 = (zoom * f).coerceIn(0.5f, 8f)
        panX += dx * unite * (1 / z0 - 1 / z1); panY -= dy * unite * (1 / z0 - 1 / z1)
        zoom = z1
    }
    fun deplacer(dx: Float, dy: Float) { panX -= dx * unite / zoom; panY += dy * unite / zoom }
    fun decale() = kotlin.math.abs(panX) + kotlin.math.abs(panY) > 0.02f
    fun penche() = kotlin.math.abs(((yaw % 360f) + 540f) % 360f - 180f) > 3f || kotlin.math.abs(pitch) > 3f
    fun recadrer() { zoom = 1f; panX = 0f; panY = 0f; vYaw = 0f; vPitch = 0f; auRepos = true }

    private var prog = 0
    private var progFond = 0
    private var w = 1
    private var h = 1
    private var aniso = 1f
    private var maxTex = 2048
    private var dernier = 0L
    private val tex = HashMap<Int, Int>()
    private val bitmaps = HashMap<Int, Bitmap>()                // (pour recreer les textures si la surface est recreee)
    private val proj = FloatArray(16)
    private val vueM = FloatArray(16)
    private val mod = FloatArray(16)
    private val tmp = FloatArray(16)
    private val mvp = FloatArray(16)
    private var buf: FloatBuffer? = null

    fun page(p: Int, b: Bitmap) {
        bitmaps[p] = b
        tex[p]?.let { GLES20.glDeleteTextures(1, intArrayOf(it), 0) }
        tex[p] = texture(b)
        if (p == 1 && b.height > 0) ratio = b.width.toFloat() / b.height
    }
    fun garder(s: Set<Int>) {
        for (p in tex.keys.toList()) if (p !in s) { GLES20.glDeleteTextures(1, intArrayOf(tex[p]!!), 0); tex.remove(p) }
        bitmaps.keys.retainAll(s)
    }

    private fun pot(k: Int): Int { var p = 1; while (p < k) p = p shl 1; return min(p, maxTex) }
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

    private val VS = """
        uniform mat4 uMVP; uniform mat4 uM;
        attribute vec3 aP; attribute vec3 aN; attribute vec2 aT;
        varying vec3 vN; varying vec2 vT; varying vec3 vW;
        void main() { vec4 w = uM * vec4(aP, 1.0); vW = w.xyz; vN = (uM * vec4(aN, 0.0)).xyz; vT = aT; gl_Position = uMVP * vec4(aP, 1.0); }
    """
    // (papier legerement satine : deux lumieres de face, un peu de reflet ; les pages gardent leurs vraies couleurs)
    private val FS = """
        precision highp float;
        uniform sampler2D uTex; uniform float uUseTex; uniform vec4 uCol; uniform vec3 uEye;
        varying vec3 vN; varying vec2 vT; varying vec3 vW;
        void main() {
            vec3 b = uUseTex > 0.5 ? texture2D(uTex, vT).rgb : uCol.rgb;
            vec3 n = normalize(vN); vec3 V = normalize(uEye - vW);
            if (dot(n, V) < 0.0) n = -n;
            vec3 L1 = normalize(vec3(-0.3, 0.5, 0.8)), L2 = normalize(vec3(0.5, 0.2, 0.85));
            float d = 0.42 + 0.62 * (max(dot(n, L1), 0.0) * 0.75 + max(dot(n, L2), 0.0) * 0.35);
            float s = pow(max(dot(n, normalize(L1 + V)), 0.0), 40.0) * 0.10;
            gl_FragColor = vec4(min(b * d, vec3(1.0)) + vec3(s), 1.0);
        }
    """
    private val VS_FOND = "attribute vec2 aP; varying vec2 vP; void main() { vP = aP; gl_Position = vec4(aP, 0.999, 1.0); }"
    private val FS_FOND = """
        precision mediump float; varying vec2 vP;
        void main() {
            vec3 c = mix(vec3(0.027, 0.008, 0.059), vec3(0.071, 0.024, 0.141), vP.y * 0.5 + 0.5);
            c += vec3(0.42, 0.14, 0.62) * (1.0 - smoothstep(0.0, 1.1, length(vP - vec2(0.0, 0.15)))) * 0.35;
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
    private val fondBuf: FloatBuffer = ByteBuffer.allocateDirect(8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f)); position(0) }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        prog = programme(VS, FS)
        progFond = programme(VS_FOND, FS_FOND)
        val ext = GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: ""
        if (ext.contains("GL_EXT_texture_filter_anisotropic")) { val a = FloatArray(1); GLES20.glGetFloatv(0x84FF, a, 0); aniso = max(1f, a[0]) }
        val mx = IntArray(1); GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, mx, 0); maxTex = min(8192, max(2048, mx[0]))
        tex.clear()
        for ((p, b) in bitmaps) tex[p] = texture(b)
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
        // elan (freine peu a peu) ; "de face" : revient doucement a plat face a nous ; pages : la feuille tourne
        if (!tient) {
            yaw += vYaw * dt; pitch = (pitch + vPitch * dt).coerceIn(-89f, 89f)
            val fr = 1 - min(1f, dt * 2.2f); vYaw *= fr; vPitch *= fr
        } else auRepos = false
        if (auRepos) {
            val c = 360f * Math.round(yaw / 360f)
            yaw += (c - yaw) * min(1f, dt * 6f); pitch += (0f - pitch) * min(1f, dt * 6f)
            if (kotlin.math.abs(c - yaw) < 0.05f && kotlin.math.abs(pitch) < 0.05f) { yaw = c; pitch = 0f; auRepos = false }
        }
        val ecart = posVers - pos
        pos += if (kotlin.math.abs(ecart) < 0.002f) ecart else ecart * min(1f, dt * 5.5f)

        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(progFond)
        val ap = GLES20.glGetAttribLocation(progFond, "aP")
        GLES20.glEnableVertexAttribArray(ap)
        GLES20.glVertexAttribPointer(ap, 2, GLES20.GL_FLOAT, false, 8, fondBuf)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(ap)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)

        // la forme du moment (quelques milliers de sommets : refaite a chaque image)
        val m = Livret.construire(n, pos, ratio)
        var b = buf
        if (b == null || b.capacity() < m.n * 8) { b = ByteBuffer.allocateDirect(m.n * 8 * 4 * 2).order(ByteOrder.nativeOrder()).asFloatBuffer(); buf = b }
        b!!.position(0); b.put(m.v, 0, m.n * 8); b.position(0)

        val aspect = w.toFloat() / h
        val fov = 30f
        Matrix.perspectiveM(proj, 0, fov, aspect, 0.1f, 80f)
        val tv = kotlin.math.tan(Math.toRadians(fov / 2.0)).toFloat()
        val dist = max(1.08f / tv, Livret.demiLargeur(n, pos, ratio) * 1.08f / (tv * aspect)) * 1.12f / zoom
        unite = 2f * dist * zoom * tv / h
        val eye = floatArrayOf(panX, panY, dist)
        Matrix.setLookAtM(vueM, 0, eye[0], eye[1], eye[2], panX, panY, 0f, 0f, 1f, 0f)
        Matrix.setIdentityM(mod, 0)
        Matrix.rotateM(mod, 0, pitch, 1f, 0f, 0f)
        Matrix.rotateM(mod, 0, yaw, 0f, 1f, 0f)
        Matrix.translateM(mod, 0, -Livret.centre(n, pos, ratio), 0f, -(Livret.feuilles(n) - 1) * Livret.E / 2f)

        GLES20.glUseProgram(prog)
        val aP = GLES20.glGetAttribLocation(prog, "aP"); val aN = GLES20.glGetAttribLocation(prog, "aN"); val aT = GLES20.glGetAttribLocation(prog, "aT")
        b.position(0); GLES20.glVertexAttribPointer(aP, 3, GLES20.GL_FLOAT, false, 32, b); GLES20.glEnableVertexAttribArray(aP)
        b.position(3); GLES20.glVertexAttribPointer(aN, 3, GLES20.GL_FLOAT, false, 32, b); GLES20.glEnableVertexAttribArray(aN)
        b.position(6); GLES20.glVertexAttribPointer(aT, 2, GLES20.GL_FLOAT, false, 32, b); GLES20.glEnableVertexAttribArray(aT)
        b.position(0)
        Matrix.multiplyMM(tmp, 0, vueM, 0, mod, 0)
        Matrix.multiplyMM(mvp, 0, proj, 0, tmp, 0)
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(prog, "uMVP"), 1, false, mvp, 0)
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(prog, "uM"), 1, false, mod, 0)
        GLES20.glUniform3f(GLES20.glGetUniformLocation(prog, "uEye"), eye[0], eye[1], eye[2])
        GLES20.glUniform1i(GLES20.glGetUniformLocation(prog, "uTex"), 0)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        val uUse = GLES20.glGetUniformLocation(prog, "uUseTex"); val uCol = GLES20.glGetUniformLocation(prog, "uCol")
        for (g in m.groupes) {
            val t = if (g.page > 0) tex[g.page] ?: 0 else 0
            if (t != 0) { GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t); GLES20.glUniform1f(uUse, 1f) } else GLES20.glUniform1f(uUse, 0f)
            GLES20.glUniform4f(uCol, (g.col shr 16 and 255) / 255f, (g.col shr 8 and 255) / 255f, (g.col and 255) / 255f, 1f)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, g.premier, g.nombre)
        }
        GLES20.glDisableVertexAttribArray(aP); GLES20.glDisableVertexAttribArray(aN); GLES20.glDisableVertexAttribArray(aT)
    }
}
