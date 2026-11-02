package fr.gcstation.telecommande

// Appli build 233 : le niveau en 3D avec ses vraies textures (OpenGL ES 2). Le pack de cartes (fabrique a part depuis les
// fichiers du jeu, jamais dans l'appli) donne, par niveau : <niveau>.r3v (groupes de triangles par texture : sommets x y z,
// u v, couleur r g b a) et les images <niveau>_tNN.png. Un doigt : tourner ; deux doigts : zoom ; double toucher : revenir
// derriere la camera du jeu. Rayman : point rouge ; ton chemin : trait jaune.

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.sp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.geometry.Offset
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.opengl.GLUtils
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.viewinterop.AndroidView
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/** une scene du pack : groupes de triangles (texture, transparence, sommets entrelaces) */
class SceneR3(val dossier: java.io.File, val niveau: String, val groupes: List<Groupe>, val centre: FloatArray) {
    class Groupe(val texture: Int, val alpha: Boolean, val sommets: ByteBuffer, val n: Int,
                 val mode: Int = if (alpha) 2 else 0, val enveloppe: Int = 0)   // (build 236 : 0 opaque, 1 decoupe, 2 transparent, 3 lumiere ; miroir U/V)
    companion object {
        /** fichier .r3v : "R3V1", nombre de groupes ; par groupe : texture (s32), alpha (u32), nombre de sommets (u32),
         *  puis les sommets (24 octets : 5 flottants + 4 octets de couleur), petit-boutiste */
        fun lire(d: java.io.File, niveau: String): SceneR3? = try {
            val f = java.io.File(d, niveau.lowercase() + ".r3v")
            if (!f.exists()) null else {
                val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
                val magie = b.int
                if (magie != 0x31563352 && magie != 0x32563352) null else {          // "R3V1" ou "R3V2"
                    val v2 = magie == 0x32563352
                    val ng = b.int
                    val l = ArrayList<Groupe>()
                    var sx = 0.0; var sy = 0.0; var sz = 0.0; var nn = 0L
                    for (g in 0 until ng) {
                        val tex = b.int; val md = b.int; val env = if (v2) b.int else 0; val n = b.int
                        val al = md != 0
                        val bb = ByteBuffer.allocateDirect(n * 24).order(ByteOrder.nativeOrder())
                        val src = ByteArray(n * 24); b.get(src)
                        // (les flottants du fichier sont petit-boutistes : comme le processeur du telephone)
                        bb.put(src); bb.position(0)
                        for (k in 0 until n step 17) { sx += bb.getFloat(k * 24); sy += bb.getFloat(k * 24 + 4); sz += bb.getFloat(k * 24 + 8); nn++ }
                        l.add(Groupe(tex, al, bb, n, if (v2) md else if (al) 2 else 0, env))
                    }
                    SceneR3(d, niveau.lowercase(), l, if (nn > 0) floatArrayOf((sx / nn).toFloat(), (sy / nn).toFloat(), (sz / nn).toFloat()) else floatArrayOf(0f, 0f, 0f))
                }
            }
        } catch (e: Throwable) { null }
    }
}

/** ce que la vue regarde (modifie par les doigts et par le jeu, lu par le fil OpenGL) */
class Etat3D {
    @Volatile var lacet = Float.NaN; @Volatile var tangage = 0.5f; @Volatile var dist = 70f
    @Volatile var cible = floatArrayOf(0f, 0f, 0f); @Volatile var camera: FloatArray? = null
    @Volatile var chemin: FloatArray = FloatArray(0); @Volatile var montrerRayman = true
    @Volatile var decal = floatArrayOf(0f, 0f, 0f)       // (build 236 : deplacement a deux doigts, ajoute a la cible)
    fun recentrer() { lacet = Float.NaN; tangage = 0.5f; dist = 70f; decal = floatArrayOf(0f, 0f, 0f) }
    fun lacetActuel(): Float {
        val l = lacet
        if (!l.isNaN()) return l
        val c = cible; val cam = camera
        return if (cam != null) Math.atan2((c[1] - cam[1]).toDouble(), (c[0] - cam[0]).toDouble()).toFloat() else 0f
    }
}

private class RenduR3(val scene: SceneR3, val etat: Etat3D) : GLSurfaceView.Renderer {
    private var prog = 0; private var progL = 0
    private var uSeuil = 0; private var progCiel = 0; private var cPos = 0
    private val quad: FloatBuffer = ByteBuffer.allocateDirect(32).order(ByteOrder.nativeOrder()).asFloatBuffer().put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f)).also { it.position(0) }
    private var aPos = 0; private var aUv = 0; private var aCol = 0; private var uMvp = 0; private var uTex = 0; private var uAvecTex = 0; private var uCam = 0
    private var lPos = 0; private var lMvp = 0; private var lCoul = 0; private var lTaille = 0
    private val textures = HashMap<Int, Int>()
    private val vbo = IntArray(256)
    private val proj = FloatArray(16); private val vue = FloatArray(16); private val mvp = FloatArray(16)
    private fun shader(t: Int, src: String): Int { val s = GLES20.glCreateShader(t); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s); return s }
    private fun programme(v: String, f: String): Int {
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, v)); GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, f)); GLES20.glLinkProgram(p)
        return p
    }
    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        prog = programme("""
            uniform mat4 uMvp; uniform vec3 uCam;
            attribute vec3 aPos; attribute vec2 aUv; attribute vec4 aCol;
            varying vec2 vUv; varying vec4 vCol; varying float vBrume;
            void main() { gl_Position = uMvp * vec4(aPos, 1.0); vUv = aUv; vCol = aCol;
                vBrume = clamp((distance(aPos, uCam) - 250.0) / 900.0, 0.0, 0.55); }""", """
            precision mediump float;
            uniform sampler2D uTex; uniform float uAvecTex; uniform float uSeuil;
            varying vec2 vUv; varying vec4 vCol; varying float vBrume;
            void main() { vec4 t = uAvecTex > 0.5 ? texture2D(uTex, vUv) : vec4(1.0);
                vec4 c = t * vec4(vCol.rgb * 2.0, vCol.a); if (c.a < uSeuil) discard;
                gl_FragColor = vec4(mix(c.rgb, vec3(0.42, 0.5, 0.68), vBrume), c.a); }""")
        aPos = GLES20.glGetAttribLocation(prog, "aPos"); aUv = GLES20.glGetAttribLocation(prog, "aUv"); aCol = GLES20.glGetAttribLocation(prog, "aCol")
        uMvp = GLES20.glGetUniformLocation(prog, "uMvp"); uTex = GLES20.glGetUniformLocation(prog, "uTex")
        uAvecTex = GLES20.glGetUniformLocation(prog, "uAvecTex"); uCam = GLES20.glGetUniformLocation(prog, "uCam")
        uSeuil = GLES20.glGetUniformLocation(prog, "uSeuil")
        // ciel : un degrade plein ecran dessine avant le niveau
        progCiel = programme("""
            attribute vec2 aP; varying float vY;
            void main() { gl_Position = vec4(aP, 0.999, 1.0); vY = aP.y * 0.5 + 0.5; }""", """
            precision mediump float; varying float vY;
            void main() { gl_FragColor = vec4(mix(vec3(0.62, 0.72, 0.86), vec3(0.16, 0.30, 0.62), smoothstep(0.35, 1.0, vY)), 1.0); }""")
        cPos = GLES20.glGetAttribLocation(progCiel, "aP")
        progL = programme("""
            uniform mat4 uMvp; uniform float uTaille; attribute vec3 aPos;
            void main() { gl_Position = uMvp * vec4(aPos, 1.0); gl_PointSize = uTaille; }""", """
            precision mediump float; uniform vec4 uCoul;
            void main() { gl_FragColor = uCoul; }""")
        lPos = GLES20.glGetAttribLocation(progL, "aPos"); lMvp = GLES20.glGetUniformLocation(progL, "uMvp")
        lCoul = GLES20.glGetUniformLocation(progL, "uCoul"); lTaille = GLES20.glGetUniformLocation(progL, "uTaille")
        // sommets
        val n = minOf(scene.groupes.size, vbo.size)
        GLES20.glGenBuffers(n, vbo, 0)
        for (i in 0 until n) {
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo[i])
            val g = scene.groupes[i]; g.sommets.position(0)
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, g.n * 24, g.sommets, GLES20.GL_STATIC_DRAW)
        }
        // textures
        val aniso = (GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: "").contains("texture_filter_anisotropic")
        textures.clear()
        for (g in scene.groupes) {
            if (g.texture < 0 || textures.containsKey(g.texture)) continue
            val f = java.io.File(scene.dossier, scene.niveau + "_t" + String.format(java.util.Locale.ROOT, "%03d", g.texture) + ".png")
            val bmp = android.graphics.BitmapFactory.decodeFile(f.path) ?: continue
            val t = IntArray(1); GLES20.glGenTextures(1, t, 0)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t[0])
            GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bmp, 0); bmp.recycle()
            val pot = { v: Int -> v > 0 && (v and (v - 1)) == 0 }
            GLES20.glGenerateMipmap(GLES20.GL_TEXTURE_2D)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR_MIPMAP_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, if (g.enveloppe and 1 != 0) GLES20.GL_MIRRORED_REPEAT else GLES20.GL_REPEAT)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, if (g.enveloppe and 2 != 0) GLES20.GL_MIRRORED_REPEAT else GLES20.GL_REPEAT)
            if (aniso) GLES20.glTexParameterf(GLES20.GL_TEXTURE_2D, 0x84FE, 16f)   // (GL_TEXTURE_MAX_ANISOTROPY_EXT : nettete de biais)
            textures[g.texture] = t[0]
        }
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        GLES20.glClearColor(0.42f, 0.5f, 0.68f, 1f)
    }
    override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
        GLES20.glViewport(0, 0, w, h)
        android.opengl.Matrix.perspectiveM(proj, 0, 60f, w.toFloat() / h.coerceAtLeast(1), 0.5f, 3000f)
    }
    override fun onDrawFrame(gl: GL10?) {
        val c0 = etat.cible; val dc = etat.decal
        val c = floatArrayOf(c0[0] + dc[0], c0[1] + dc[1], c0[2] + dc[2])
        val a = etat.lacetActuel()
        val tg = etat.tangage; val d = etat.dist
        val ex = c[0] - (Math.cos(a.toDouble()) * Math.cos(tg.toDouble()) * d).toFloat()
        val ey = c[1] - (Math.sin(a.toDouble()) * Math.cos(tg.toDouble()) * d).toFloat()
        val ez = c[2] + (Math.sin(tg.toDouble()) * d).toFloat()
        android.opengl.Matrix.setLookAtM(vue, 0, ex, ey, ez, c[0], c[1], c[2], 0f, 0f, 1f)
        android.opengl.Matrix.multiplyMM(mvp, 0, proj, 0, vue, 0)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(progCiel)
        GLES20.glVertexAttribPointer(cPos, 2, GLES20.GL_FLOAT, false, 8, quad); GLES20.glEnableVertexAttribArray(cPos)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(cPos)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(prog)
        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0)
        GLES20.glUniform3f(uCam, ex, ey, ez)
        GLES20.glUniform1i(uTex, 0)
        GLES20.glEnableVertexAttribArray(aPos); GLES20.glEnableVertexAttribArray(aUv); GLES20.glEnableVertexAttribArray(aCol)
        for (passe in 0..3) {                             // opaque, decoupe, transparent, lumiere (additive)
            when (passe) {
                0 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uSeuil, 0.02f) }
                1 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uSeuil, 0.5f) }
                2 -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA); GLES20.glDepthMask(false); GLES20.glUniform1f(uSeuil, 0.02f) }
                3 -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE); GLES20.glDepthMask(false); GLES20.glUniform1f(uSeuil, 0.02f) }
            }
            for ((i, g) in scene.groupes.withIndex()) {
                if (i >= vbo.size || g.mode != passe) continue
                val t = textures[g.texture]
                GLES20.glUniform1f(uAvecTex, if (t != null) 1f else 0f)
                if (t != null) { GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t) }
                GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo[i])
                GLES20.glVertexAttribPointer(aPos, 3, GLES20.GL_FLOAT, false, 24, 0)
                GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, 24, 12)
                GLES20.glVertexAttribPointer(aCol, 4, GLES20.GL_UNSIGNED_BYTE, true, 24, 20)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, g.n)
            }
        }
        GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
        GLES20.glDisableVertexAttribArray(aUv); GLES20.glDisableVertexAttribArray(aCol)
        // le chemin et Rayman (par-dessus)
        GLES20.glUseProgram(progL)
        GLES20.glUniformMatrix4fv(lMvp, 1, false, mvp, 0)
        val ch = etat.chemin
        if (ch.size >= 6) {
            val fb = ByteBuffer.allocateDirect(ch.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().put(ch); fb.position(0)
            GLES20.glUniform4f(lCoul, 1f, 0.79f, 0.29f, 1f); GLES20.glUniform1f(lTaille, 1f)
            GLES20.glVertexAttribPointer(lPos, 3, GLES20.GL_FLOAT, false, 12, fb); GLES20.glEnableVertexAttribArray(lPos)
            GLES20.glLineWidth(4f); GLES20.glDrawArrays(GLES20.GL_LINE_STRIP, 0, ch.size / 3)
        }
        if (etat.montrerRayman) {
            GLES20.glDisable(GLES20.GL_DEPTH_TEST)
            val p: FloatBuffer = ByteBuffer.allocateDirect(12).order(ByteOrder.nativeOrder()).asFloatBuffer().put(floatArrayOf(c0[0], c0[1], c0[2] + 1.5f)); p.position(0)
            GLES20.glVertexAttribPointer(lPos, 3, GLES20.GL_FLOAT, false, 12, p); GLES20.glEnableVertexAttribArray(lPos)
            GLES20.glUniform1f(lTaille, 30f); GLES20.glUniform4f(lCoul, 1f, 1f, 1f, 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
            GLES20.glUniform1f(lTaille, 22f); GLES20.glUniform4f(lCoul, 1f, 0.23f, 0.23f, 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
            GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        }
    }
}

/** la vue 3D texturee ; cible / camera / chemin : la position en direct (ou le centre du niveau, sans jouer) */
@Composable
fun DessinVue3DGL(scene: SceneR3, cible: FloatArray, camera: FloatArray?, chemin: List<FloatArray>, montrerRayman: Boolean = true) {
    val etat = remember(scene) { Etat3D() }
    etat.cible = cible; etat.camera = camera; etat.montrerRayman = montrerRayman
    etat.chemin = FloatArray(chemin.size * 3).also { a -> for ((k, p) in chemin.withIndex()) { a[k * 3] = p[0]; a[k * 3 + 1] = p[1]; a[k * 3 + 2] = p[2] } }
    var plein by androidx.compose.runtime.remember { androidx.compose.runtime.mutableStateOf(false) }
    Box(Modifier.fillMaxWidth().aspectRatio(1.1f)) {
        if (!plein) VueGL(scene, etat, Modifier.fillMaxSize())
        Row(Modifier.align(androidx.compose.ui.Alignment.TopEnd).padding(8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            PetitBouton("⟲") { etat.recentrer() }
            PetitBouton("⛶ Plein écran") { plein = true }
        }
    }
    if (plein) androidx.compose.ui.window.Dialog({ plein = false }, androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(Color(0xFF6B80AE))) {
            VueGL(scene, etat, Modifier.fillMaxSize())
            Row(Modifier.align(androidx.compose.ui.Alignment.TopEnd).padding(top = 40.dp, end = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                PetitBouton("⟲ Recentrer") { etat.recentrer() }
                PetitBouton("⬇ De dessus") { etat.tangage = 1.5f; etat.dist = maxOf(etat.dist, 160f) }
                PetitBouton("✕") { plein = false }
            }
            Text("1 doigt : tourner · 2 doigts : déplacer et zoomer · double toucher : recentrer", color = Color.White, fontSize = 12.sp,
                modifier = Modifier.align(androidx.compose.ui.Alignment.BottomCenter).padding(bottom = 28.dp).background(Color(0x88000000), RoundedCornerShape(10.dp)).padding(horizontal = 10.dp, vertical = 6.dp))
        }
    }
}

@Composable
private fun PetitBouton(t: String, faire: () -> Unit) {
    Text(t, color = Color.White, fontSize = 13.sp, modifier = Modifier.background(Color(0x99000000), RoundedCornerShape(12.dp))
        .pointerInput(t) { detectTapGestures { faire() } }.padding(horizontal = 12.dp, vertical = 7.dp))
}

/** la surface OpenGL et les gestes : 1 doigt : tourner ; 2 doigts : pincer pour zoomer, glisser pour deplacer la vue ;
 *  double toucher : recentrer. Les gestes sont gardes (la page ne defile pas pendant qu'on manipule la vue). */
@Composable
private fun VueGL(scene: SceneR3, etat: Etat3D, modifier: Modifier) {
    val vueRef = remember { arrayOfNulls<GLSurfaceView>(1) }
    vueRef[0]?.requestRender()
    DisposableEffect(scene) { onDispose { vueRef[0]?.onPause(); vueRef[0] = null } }
    Box(modifier) {
        AndroidView({ ctx ->
            GLSurfaceView(ctx).apply {
                setEGLContextClientVersion(2)
                setEGLConfigChooser(ChoixMsaa())
                setRenderer(RenduR3(scene, etat))
                renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
                vueRef[0] = this
            }
        }, Modifier.fillMaxSize(), update = { it.requestRender() })
        Box(Modifier.fillMaxSize()
            .pointerInput(scene) {
                var dernierTap = 0L
                awaitEachGesture {
                    val bas = awaitFirstDown(requireUnconsumed = false)
                    val t0 = System.currentTimeMillis(); var bouge = 0f; var doigts = 1
                    var avant: List<Offset>? = null
                    while (true) {
                        val ev = awaitPointerEvent()
                        val pts = ev.changes.filter { it.pressed }.map { it.position }
                        if (pts.isEmpty()) break
                        doigts = maxOf(doigts, pts.size)
                        val av = avant
                        if (av != null && av.size == pts.size) {
                            if (etat.lacet.isNaN()) etat.lacet = etat.lacetActuel()
                            if (pts.size == 1) {
                                val dx = pts[0].x - av[0].x; val dy = pts[0].y - av[0].y
                                bouge += Math.abs(dx) + Math.abs(dy)
                                etat.lacet -= dx * 0.006f
                                etat.tangage = (etat.tangage + dy * 0.005f).coerceIn(-0.3f, 1.55f)
                            } else {
                                val c1 = Offset((av[0].x + av[1].x) / 2, (av[0].y + av[1].y) / 2); val c2 = Offset((pts[0].x + pts[1].x) / 2, (pts[0].y + pts[1].y) / 2)
                                val d1 = (av[0] - av[1]).getDistance().coerceAtLeast(1f); val d2 = (pts[0] - pts[1]).getDistance().coerceAtLeast(1f)
                                etat.dist = (etat.dist * d1 / d2).coerceIn(3f, 1500f)
                                // deplacer : la cible glisse dans le plan du sol, sous les doigts
                                val l = etat.lacet; val k = etat.dist * 0.0022f
                                val dx = c2.x - c1.x; val dy = c2.y - c1.y
                                val fx = Math.cos(l.toDouble()).toFloat(); val fy = Math.sin(l.toDouble()).toFloat()
                                val d = etat.decal
                                etat.decal = floatArrayOf(d[0] + (-dx * fy + dy * fx) * k, d[1] + (dx * fx + dy * fy) * k, d[2])   // (droite = (fy, -fx))
                            }
                            vueRef[0]?.requestRender()
                        }
                        avant = pts
                        ev.changes.forEach { it.consume() }
                    }
                    // double toucher (deux touchers brefs sans bouger) : recentrer
                    val fin = System.currentTimeMillis()
                    if (doigts == 1 && bouge < 24f && fin - t0 < 300) {
                        if (fin - dernierTap < 350) { etat.recentrer(); vueRef[0]?.requestRender(); dernierTap = 0L } else dernierTap = fin
                    }
                }
            })
    }
}


/** build 236 : anticrenelage 4x (bords nets), sinon la configuration simple */
private class ChoixMsaa : GLSurfaceView.EGLConfigChooser {
    override fun chooseConfig(egl: javax.microedition.khronos.egl.EGL10, d: javax.microedition.khronos.egl.EGLDisplay): EGLConfig {
        fun essai(msaa: Boolean): EGLConfig? {
            val a = intArrayOf(0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3025, 24, 0x3040, 4) +   // (R G B A, profondeur 24, ES2)
                (if (msaa) intArrayOf(0x3032, 1, 0x3031, 4) else intArrayOf()) + intArrayOf(0x3038)
            val n = IntArray(1); val c = arrayOfNulls<EGLConfig>(1)
            return if (egl.eglChooseConfig(d, a, c, 1, n) && n[0] > 0) c[0] else null
        }
        return essai(true) ?: essai(false) ?: run {
            val n = IntArray(1); val c = arrayOfNulls<EGLConfig>(1)
            egl.eglChooseConfig(d, intArrayOf(0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3025, 16, 0x3040, 4, 0x3038), c, 1, n); c[0]!!
        }
    }
}
