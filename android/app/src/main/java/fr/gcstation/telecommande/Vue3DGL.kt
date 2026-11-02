package fr.gcstation.telecommande

// Appli build 233 (build 242 : image calculee en plus haute resolution, nettete, halo lumineux, ombres adoucies) : le niveau en 3D avec ses vraies textures (OpenGL ES 2). Le pack de cartes (fabrique a part depuis les
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
import androidx.compose.foundation.layout.size
import kotlinx.coroutines.launch
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
class SceneR3(val dossier: java.io.File, val niveau: String, val groupes: List<Groupe>, val centre: FloatArray, val rayon: Float = 60f) {
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
                    val ech = ArrayList<Float>()   // (build 242 : points echantillons pour la taille du niveau)
                    for (g in 0 until ng) {
                        val tex = b.int; val md = b.int; val env = if (v2) b.int else 0; val n = b.int
                        val al = md != 0
                        val bb = ByteBuffer.allocateDirect(n * 24).order(ByteOrder.nativeOrder())
                        val src = ByteArray(n * 24); b.get(src)
                        // (les flottants du fichier sont petit-boutistes : comme le processeur du telephone)
                        bb.put(src); bb.position(0)
                        for (k in 0 until n step 17) { sx += bb.getFloat(k * 24); sy += bb.getFloat(k * 24 + 4); sz += bb.getFloat(k * 24 + 8); nn++
                            ech.add(bb.getFloat(k * 24)); ech.add(bb.getFloat(k * 24 + 4)) }
                        l.add(Groupe(tex, al, bb, n, if (v2) md else if (al) 2 else 0, env))
                    }
                    adoucirEclairage(l)
                    val ctr = if (nn > 0) floatArrayOf((sx / nn).toFloat(), (sy / nn).toFloat(), (sz / nn).toFloat()) else floatArrayOf(0f, 0f, 0f)
                    // rayon : 85 % des points du decor sont a moins de cette distance (au sol) du centre ; les objets tres lointains ne comptent pas
                    val ds = (0 until ech.size / 2).map { Math.hypot((ech[it * 2] - ctr[0]).toDouble(), (ech[it * 2 + 1] - ctr[1]).toDouble()).toFloat() }.sorted()
                    val ray = if (ds.isEmpty()) 60f else ds[(ds.size * 0.85).toInt().coerceAtMost(ds.size - 1)].coerceIn(15f, 900f)
                    SceneR3(d, niveau.lowercase(), l, ctr, ray)
                }
            }
        } catch (e: Throwable) { null }

        /** build 242 : les ombres du jeu sont une couleur par sommet ; un meme point du decor est souvent repete dans
         *  plusieurs groupes avec des couleurs un peu differentes, ce qui dessine des raccords. On rapproche chaque
         *  sommet (a moitie) de la moyenne des sommets au meme endroit. Les lumieres (additives) ne sont pas touchees. */
        private fun adoucirEclairage(l: List<Groupe>) {
            fun cle(b: ByteBuffer, o: Int): Long {
                val x = Math.round(b.getFloat(o) * 20f).toLong(); val y = Math.round(b.getFloat(o + 4) * 20f).toLong(); val z = Math.round(b.getFloat(o + 8) * 20f).toLong()
                return ((x and 0x1FFFFF) shl 42) or ((y and 0x1FFFFF) shl 21) or (z and 0x1FFFFF)
            }
            val somme = HashMap<Long, IntArray>()
            for (g in l) { if (g.mode == 3) continue
                for (k in 0 until g.n) { val o = k * 24
                    val s = somme.getOrPut(cle(g.sommets, o)) { IntArray(4) }
                    s[0] += g.sommets.get(o + 20).toInt() and 255; s[1] += g.sommets.get(o + 21).toInt() and 255; s[2] += g.sommets.get(o + 22).toInt() and 255; s[3]++ } }
            for (g in l) { if (g.mode == 3) continue
                for (k in 0 until g.n) { val o = k * 24
                    val s = somme[cle(g.sommets, o)] ?: continue
                    if (s[3] < 2) continue
                    for (c in 0..2) { val v = g.sommets.get(o + 20 + c).toInt() and 255; g.sommets.put(o + 20 + c, ((v + s[c] / s[3] + 1) / 2).toByte()) } } }
            for (g in l) g.sommets.position(0)
        }
    }
}

/** ce que la vue regarde (modifie par les doigts et par le jeu, lu par le fil OpenGL) */
class Etat3D {
    @Volatile var lacet = Float.NaN; @Volatile var tangage = 0.5f; @Volatile var dist = 70f
    @Volatile var cible = floatArrayOf(0f, 0f, 0f); @Volatile var camera: FloatArray? = null
    @Volatile var chemin: FloatArray = FloatArray(0); @Volatile var montrerRayman = true
    @Volatile var decal = floatArrayOf(0f, 0f, 0f)       // (build 236 : deplacement a deux doigts, ajoute a la cible)
    @Volatile var redessiner: () -> Unit = {}
    @Volatile var distBase = 70f                           // (build 242 : distance de depart, adaptee a la taille du niveau)
    fun recentrer() { lacet = Float.NaN; tangage = 0.5f; dist = distBase; decal = floatArrayOf(0f, 0f, 0f) }
    /** build 242 : une vue du dessus cadrant tout le niveau */
    fun dessus() { if (lacet.isNaN()) lacet = lacetActuel(); tangage = 1.5f; dist = maxOf(dist, distBase * 1.6f) }
    /** build 242 : zoom par boutons (facteur < 1 : rapprocher) */
    fun zoom(f: Float) { dist = (dist * f).coerceIn(2f, 2500f) }
    fun lacetActuel(): Float {
        val l = lacet
        if (!l.isNaN()) return l
        val c = cible; val cam = camera
        return if (cam != null) Math.atan2((c[1] - cam[1]).toDouble(), (c[0] - cam[0]).toDouble()).toFloat() else 0f
    }
}

/** build 242 : taille des textures agrandies par l'IA (le pack HD les donne en x4 ; on peut les reduire si le telephone peine) */
object QualiteR3 {
    var facteur by androidx.compose.runtime.mutableStateOf(4)     // (etat Compose : changer recree la vue)
    fun charger(ctx: android.content.Context) { facteur = ctx.getSharedPreferences("cartes-r3", 0).getInt("tex", 4).coerceIn(1, 4) }
    fun changer(ctx: android.content.Context, f: Int) { facteur = f; ctx.getSharedPreferences("cartes-r3", 0).edit().putInt("tex", f).apply() }
}

private class RenduR3(val scene: SceneR3, val etat: Etat3D, val facteur: Int) : GLSurfaceView.Renderer {
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
            #ifdef GL_FRAGMENT_PRECISION_HIGH
            precision highp float;
            #else
            precision mediump float;
            #endif
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
        val npot = (GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: "").contains("texture_npot")
        // pack HD : "<niveau>_hd4.txt" ; les textures y sont 4x plus grandes que dans le jeu
        val hd = java.io.File(scene.dossier, scene.niveau + "_hd4.txt").exists()
        val fac = if (facteur == 3 && !npot) 2 else facteur
        textures.clear()
        for (g in scene.groupes) {
            if (g.texture < 0 || textures.containsKey(g.texture)) continue
            val f = java.io.File(scene.dossier, scene.niveau + "_t" + String.format(java.util.Locale.ROOT, "%03d", g.texture) + ".png")
            val bmp = (if (hd && fac < 4) {
                // x2 et x1 : lecture directe en 1/2 ou 1/4 ; x3 : reduction filtree des 3/4
                val o = android.graphics.BitmapFactory.Options().apply { inSampleSize = if (fac == 1) 4 else if (fac == 2) 2 else 1 }
                val b0 = android.graphics.BitmapFactory.decodeFile(f.path, o)
                if (b0 != null && fac == 3) android.graphics.Bitmap.createScaledBitmap(b0, maxOf(1, b0.width * 3 / 4), maxOf(1, b0.height * 3 / 4), true).also { if (it !== b0) b0.recycle() } else b0
            } else android.graphics.BitmapFactory.decodeFile(f.path)) ?: continue
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
        // build 242 : post-traitement (image calculee plus grande que l'ecran, puis reduite, nettete, halo lumineux)
        val vPost = """
            attribute vec2 aP; varying vec2 v;
            void main() { gl_Position = vec4(aP, 0.0, 1.0); v = aP * 0.5 + 0.5; }"""
        progFin = programme(vPost, """
            precision mediump float;
            uniform sampler2D uS; uniform sampler2D uB; uniform vec2 uPx; uniform float uBloom;
            varying vec2 v;
            vec3 t(vec2 o) { return texture2D(uS, v + uPx * o).rgb; }
            void main() {
                vec3 c = (t(vec2(-0.5, -0.5)) + t(vec2(0.5, -0.5)) + t(vec2(-0.5, 0.5)) + t(vec2(0.5, 0.5))) * 0.25;
                vec3 f = (t(vec2(-2.0, 0.0)) + t(vec2(2.0, 0.0)) + t(vec2(0.0, -2.0)) + t(vec2(0.0, 2.0))) * 0.25;
                c += (c - f) * 0.35;
                c += texture2D(uB, v).rgb * uBloom;
                float l = dot(c, vec3(0.299, 0.587, 0.114));
                gl_FragColor = vec4(clamp(mix(vec3(l), c, 1.08), 0.0, 1.0), 1.0); }""")
        progClair = programme(vPost, """
            precision mediump float;
            uniform sampler2D uS; uniform vec2 uPx; varying vec2 v;
            void main() {
                vec3 c = (texture2D(uS, v + uPx * vec2(-1.0, -1.0)).rgb + texture2D(uS, v + uPx * vec2(1.0, -1.0)).rgb
                        + texture2D(uS, v + uPx * vec2(-1.0, 1.0)).rgb + texture2D(uS, v + uPx * vec2(1.0, 1.0)).rgb) * 0.25;
                gl_FragColor = vec4(max(c - vec3(0.6), 0.0) * 2.2, 1.0); }""")
        progFlou = programme(vPost, """
            precision mediump float;
            uniform sampler2D uS; uniform vec2 uPx; varying vec2 v;
            void main() {
                vec3 c = texture2D(uS, v).rgb * 0.227;
                c += (texture2D(uS, v + uPx * 1.385).rgb + texture2D(uS, v - uPx * 1.385).rgb) * 0.316;
                c += (texture2D(uS, v + uPx * 3.231).rgb + texture2D(uS, v - uPx * 3.231).rgb) * 0.070;
                gl_FragColor = vec4(c, 1.0); }""")
        val ext = GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: ""
        profond24 = ext.contains("GL_OES_depth24")
        GLES20.glGetIntegerv(GLES20.GL_MAX_TEXTURE_SIZE, maxTex, 0)
        cibles.clear()
    }

    // --- build 242 : cibles de rendu (image agrandie A, halo B et C a 1/4) ---
    private var progFin = 0; private var progClair = 0; private var progFlou = 0
    private var profond24 = false; private val maxTex = IntArray(1)
    private class Cible(val fbo: Int, val tex: Int, val rb: Int, val w: Int, val h: Int)
    private val cibles = ArrayList<Cible>()
    private var ecranW = 1; private var ecranH = 1; private var echelle = 1f
    private fun cible(w: Int, h: Int, profondeur: Boolean): Cible? {
        val f = IntArray(1); val t = IntArray(1); val r = IntArray(1)
        GLES20.glGenFramebuffers(1, f, 0); GLES20.glGenTextures(1, t, 0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t[0])
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, w, h, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null)
        for ((p, v) in listOf(GLES20.GL_TEXTURE_MIN_FILTER to GLES20.GL_LINEAR, GLES20.GL_TEXTURE_MAG_FILTER to GLES20.GL_LINEAR,
                GLES20.GL_TEXTURE_WRAP_S to GLES20.GL_CLAMP_TO_EDGE, GLES20.GL_TEXTURE_WRAP_T to GLES20.GL_CLAMP_TO_EDGE))
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, p, v)
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, f[0])
        GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0, GLES20.GL_TEXTURE_2D, t[0], 0)
        if (profondeur) {
            GLES20.glGenRenderbuffers(1, r, 0); GLES20.glBindRenderbuffer(GLES20.GL_RENDERBUFFER, r[0])
            GLES20.glRenderbufferStorage(GLES20.GL_RENDERBUFFER, if (profond24) 0x81A6 else GLES20.GL_DEPTH_COMPONENT16, w, h)   // (DEPTH_COMPONENT24_OES)
            GLES20.glFramebufferRenderbuffer(GLES20.GL_FRAMEBUFFER, GLES20.GL_DEPTH_ATTACHMENT, GLES20.GL_RENDERBUFFER, r[0])
        }
        val ok = GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER) == GLES20.GL_FRAMEBUFFER_COMPLETE
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
        val c = Cible(f[0], t[0], r[0], w, h)
        if (!ok) { liberer(c); return null }
        return c
    }
    private fun liberer(c: Cible) {
        GLES20.glDeleteFramebuffers(1, intArrayOf(c.fbo), 0); GLES20.glDeleteTextures(1, intArrayOf(c.tex), 0)
        if (c.rb != 0) GLES20.glDeleteRenderbuffers(1, intArrayOf(c.rb), 0)
    }
    private fun passePost(p: Int, src: Cible, px: Float, py: Float) {
        GLES20.glUseProgram(p)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, src.tex)
        GLES20.glUniform1i(GLES20.glGetUniformLocation(p, "uS"), 0)
        GLES20.glUniform2f(GLES20.glGetUniformLocation(p, "uPx"), px, py)
        val a = GLES20.glGetAttribLocation(p, "aP")
        GLES20.glVertexAttribPointer(a, 2, GLES20.GL_FLOAT, false, 8, quad); GLES20.glEnableVertexAttribArray(a)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(a)
    }

    override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
        ecranW = w.coerceAtLeast(1); ecranH = h.coerceAtLeast(1)
        GLES20.glViewport(0, 0, w, h)
        android.opengl.Matrix.perspectiveM(proj, 0, 60f, w.toFloat() / h.coerceAtLeast(1), 0.5f, 3000f)
        for (c in cibles) liberer(c); cibles.clear(); echelle = 1f
        // facteur : jusqu'a 2x l'ecran (4 pixels calcules pour 1 affiche), sans depasser 4096 ni la limite du GPU
        val lim = minOf(4096, if (maxTex[0] > 0) maxTex[0] else 2048)
        val k = minOf(2f, lim.toFloat() / maxOf(ecranW, ecranH))
        if (k < 1.2f) return
        val a = cible((ecranW * k).toInt(), (ecranH * k).toInt(), true) ?: return
        val b = cible(maxOf(1, ecranW / 4), maxOf(1, ecranH / 4), false)
        val c = cible(maxOf(1, ecranW / 4), maxOf(1, ecranH / 4), false)
        if (b == null || c == null) { liberer(a); b?.let { liberer(it) }; c?.let { liberer(it) }; return }
        cibles.add(a); cibles.add(b); cibles.add(c); echelle = a.w.toFloat() / ecranW
    }
    override fun onDrawFrame(gl: GL10?) {
        if (cibles.size == 3) {
            val (a, b, c) = cibles
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, a.fbo); GLES20.glViewport(0, 0, a.w, a.h)
            dessinerScene()
            GLES20.glDisable(GLES20.GL_DEPTH_TEST); GLES20.glDisable(GLES20.GL_BLEND)
            // halo : parties claires, a 1/4, floutees dans les deux sens
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, b.fbo); GLES20.glViewport(0, 0, b.w, b.h)
            passePost(progClair, a, 1f / a.w, 1f / a.h)
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, c.fbo)
            passePost(progFlou, b, 1f / b.w, 0f)
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, b.fbo)
            passePost(progFlou, c, 0f, 1f / c.h)
            // image finale : reduction de l'image agrandie + nettete + halo
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0); GLES20.glViewport(0, 0, ecranW, ecranH)
            GLES20.glUseProgram(progFin)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE1); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, b.tex)
            GLES20.glUniform1i(GLES20.glGetUniformLocation(progFin, "uB"), 1)
            GLES20.glUniform1f(GLES20.glGetUniformLocation(progFin, "uBloom"), 0.4f)
            passePost(progFin, a, 1f / a.w, 1f / a.h)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        } else dessinerScene()
    }
    private fun dessinerScene() {
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
            GLES20.glLineWidth(4f * echelle); GLES20.glDrawArrays(GLES20.GL_LINE_STRIP, 0, ch.size / 3)
        }
        if (etat.montrerRayman) {
            GLES20.glDisable(GLES20.GL_DEPTH_TEST)
            val p: FloatBuffer = ByteBuffer.allocateDirect(12).order(ByteOrder.nativeOrder()).asFloatBuffer().put(floatArrayOf(c0[0], c0[1], c0[2] + 1.5f)); p.position(0)
            GLES20.glVertexAttribPointer(lPos, 3, GLES20.GL_FLOAT, false, 12, p); GLES20.glEnableVertexAttribArray(lPos)
            GLES20.glUniform1f(lTaille, 30f * echelle); GLES20.glUniform4f(lCoul, 1f, 1f, 1f, 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
            GLES20.glUniform1f(lTaille, 22f * echelle); GLES20.glUniform4f(lCoul, 1f, 0.23f, 0.23f, 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
            GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        }
    }
}

/** la vue 3D texturee ; cible / camera / chemin : la position en direct (ou le centre du niveau, sans jouer) */
@Composable
fun DessinVue3DGL(scene: SceneR3, cible: FloatArray, camera: FloatArray?, chemin: List<FloatArray>, montrerRayman: Boolean = true) {
    // build 242 : un etat neuf par niveau ; sans jeu en cours, la vue de depart cadre tout le niveau
    val etat = remember(scene) { Etat3D().also { e -> if (!montrerRayman) { e.distBase = scene.rayon * 1.7f; e.dist = e.distBase; e.tangage = 0.75f } } }
    etat.cible = cible; etat.camera = camera; etat.montrerRayman = montrerRayman
    etat.chemin = FloatArray(chemin.size * 3).also { a -> for ((k, p) in chemin.withIndex()) { a[k * 3] = p[0]; a[k * 3 + 1] = p[1]; a[k * 3 + 2] = p[2] } }
    var plein by remember(scene) { androidx.compose.runtime.mutableStateOf(false) }
    Box(Modifier.fillMaxWidth().aspectRatio(1.1f)) {
        if (!plein) VueGL(scene, etat, Modifier.fillMaxSize())
        Row(Modifier.align(androidx.compose.ui.Alignment.TopEnd).padding(8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            PetitBouton("⟲") { etat.recentrer(); etat.redessiner() }
            PetitBouton("⛶ Plein écran") { plein = true }
        }
        BoutonsZoom(etat, Modifier.align(androidx.compose.ui.Alignment.BottomEnd).padding(8.dp))
    }
    if (plein) androidx.compose.ui.window.Dialog({ plein = false }, androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(Color(0xFF6B80AE))) {
            VueGL(scene, etat, Modifier.fillMaxSize())
            Row(Modifier.align(androidx.compose.ui.Alignment.TopEnd).padding(top = 40.dp, end = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                PetitBouton("⟲ Recentrer") { etat.recentrer(); etat.redessiner() }
                PetitBouton("⬇ De dessus") { etat.dessus(); etat.redessiner() }
                PetitBouton("✕") { plein = false }
            }
            BoutonsZoom(etat, Modifier.align(androidx.compose.ui.Alignment.CenterEnd).padding(end = 12.dp))
            Text("1 doigt : tourner · 2 doigts : déplacer, zoomer, pivoter · double toucher : recentrer", color = Color.White, fontSize = 12.sp,
                modifier = Modifier.align(androidx.compose.ui.Alignment.BottomCenter).padding(bottom = 28.dp).background(Color(0x88000000), RoundedCornerShape(10.dp)).padding(horizontal = 10.dp, vertical = 6.dp))
        }
    }
}

/** build 242 : zoom par boutons (maintenir pour zoomer en continu) */
@Composable
private fun BoutonsZoom(etat: Etat3D, modifier: Modifier) {
    val scope = androidx.compose.runtime.rememberCoroutineScope()
    androidx.compose.foundation.layout.Column(modifier, verticalArrangement = Arrangement.spacedBy(6.dp)) {
        for ((t, f) in listOf("+" to 0.97f, "−" to 1.03f)) {
            Text(t, color = Color.White, fontSize = 20.sp, textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                modifier = Modifier.size(42.dp).background(Color(0x99000000), RoundedCornerShape(21.dp))
                    .pointerInput(t) {
                        detectTapGestures(onPress = {
                            val job = scope.launch { while (true) { androidx.compose.runtime.withFrameNanos { }; etat.zoom(f); etat.redessiner() } }
                            tryAwaitRelease(); job.cancel()
                        })
                    }.padding(top = 6.dp))
        }
    }
}

@Composable
private fun PetitBouton(t: String, faire: () -> Unit) {
    Text(t, color = Color.White, fontSize = 13.sp, modifier = Modifier.background(Color(0x99000000), RoundedCornerShape(12.dp))
        .pointerInput(t) { detectTapGestures { faire() } }.padding(horizontal = 12.dp, vertical = 7.dp))
}

/** la surface OpenGL et les gestes : 1 doigt : tourner ; 2 doigts : pincer pour zoomer, glisser pour deplacer la vue,
 *  tourner les doigts pour pivoter ; double toucher : recentrer. Les gestes sont gardes (la page ne defile pas pendant
 *  qu'on manipule la vue). Build 242 : la surface est recreee quand on change de niveau (avant, l'ancien restait),
 *  vitesse proportionnelle a la taille de la vue, et elan quand on lache le doigt. */
@Composable
private fun VueGL(scene: SceneR3, etat: Etat3D, modifier: Modifier) {
    androidx.compose.runtime.key(scene, QualiteR3.facteur) {
    val vueRef = remember { arrayOfNulls<GLSurfaceView>(1) }
    val scope = androidx.compose.runtime.rememberCoroutineScope()
    vueRef[0]?.requestRender()
    DisposableEffect(Unit) {
        val rd = { vueRef[0]?.requestRender(); Unit }
        etat.redessiner = rd
        onDispose { vueRef[0]?.onPause(); vueRef[0] = null; if (etat.redessiner === rd) etat.redessiner = {} }
    }
    Box(modifier) {
        AndroidView({ ctx ->
            GLSurfaceView(ctx).apply {
                setEGLContextClientVersion(2)
                setEGLConfigChooser(ChoixMsaa())
                setRenderer(RenduR3(scene, etat, QualiteR3.facteur))
                renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
                vueRef[0] = this
            }
        }, Modifier.fillMaxSize(), update = { it.requestRender() })
        Box(Modifier.fillMaxSize()
            .pointerInput(Unit) {
                var dernierTap = 0L
                var elan: kotlinx.coroutines.Job? = null
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    elan?.cancel()
                    val t0 = System.currentTimeMillis(); var bouge = 0f; var doigts = 1
                    var avant: List<Offset>? = null
                    val hauteur = size.height.coerceAtLeast(1).toFloat(); val largeur = size.width.coerceAtLeast(1).toFloat()
                    var vL = 0f; var vT = 0f; var tPrec = System.nanoTime()   // vitesses (par seconde) pour l'elan
                    var vDx = 0f; var vDy = 0f
                    while (true) {
                        val ev = awaitPointerEvent()
                        val pts = ev.changes.filter { it.pressed }.map { it.position }
                        if (pts.isEmpty()) break
                        doigts = maxOf(doigts, pts.size)
                        val av = avant
                        val tn = System.nanoTime(); val dt = ((tn - tPrec) / 1e9f).coerceIn(0.004f, 0.1f); tPrec = tn
                        if (av != null && av.size == pts.size) {
                            if (etat.lacet.isNaN()) etat.lacet = etat.lacetActuel()
                            if (pts.size == 1) {
                                val dx = pts[0].x - av[0].x; val dy = pts[0].y - av[0].y
                                bouge += Math.abs(dx) + Math.abs(dy)
                                val dl = -dx / largeur * 3.6f; val dtg = dy / hauteur * 2.2f   // (toute la largeur : un peu plus d'un demi-tour)
                                etat.lacet += dl
                                etat.tangage = (etat.tangage + dtg).coerceIn(-0.25f, 1.55f)
                                vL = vL * 0.4f + dl / dt * 0.6f; vT = vT * 0.4f + dtg / dt * 0.6f; vDx = 0f; vDy = 0f
                            } else if (pts.size >= 2) {
                                val c1 = Offset((av[0].x + av[1].x) / 2, (av[0].y + av[1].y) / 2); val c2 = Offset((pts[0].x + pts[1].x) / 2, (pts[0].y + pts[1].y) / 2)
                                val d1 = (av[0] - av[1]).getDistance().coerceAtLeast(1f); val d2 = (pts[0] - pts[1]).getDistance().coerceAtLeast(1f)
                                etat.dist = (etat.dist * d1 / d2).coerceIn(2f, 2500f)
                                // pivoter : l'angle entre les deux doigts
                                val a1 = Math.atan2((av[1].y - av[0].y).toDouble(), (av[1].x - av[0].x).toDouble())
                                val a2 = Math.atan2((pts[1].y - pts[0].y).toDouble(), (pts[1].x - pts[0].x).toDouble())
                                var da = a2 - a1; if (da > Math.PI) da -= 2 * Math.PI; if (da < -Math.PI) da += 2 * Math.PI
                                etat.lacet += da.toFloat()
                                // deplacer : le sol glisse sous les doigts (unites du monde par pixel a la distance regardee)
                                val l = etat.lacet; val k = etat.dist * 1.155f / hauteur
                                val dx = c2.x - c1.x; val dy = c2.y - c1.y
                                val fx = Math.cos(l.toDouble()).toFloat(); val fy = Math.sin(l.toDouble()).toFloat()
                                val d = etat.decal
                                val mx = (-dx * fy + dy * fx) * k; val my = (dx * fx + dy * fy) * k
                                etat.decal = floatArrayOf(d[0] + mx, d[1] + my, d[2])   // (droite = (fy, -fx))
                                vDx = vDx * 0.4f + mx / dt * 0.6f; vDy = vDy * 0.4f + my / dt * 0.6f; vL = 0f; vT = 0f
                            }
                            vueRef[0]?.requestRender()
                        }
                        avant = pts
                        ev.changes.forEach { it.consume() }
                    }
                    // elan : la vue continue un peu et ralentit (sauf si le doigt s'etait arrete avant de lacher)
                    if ((System.nanoTime() - tPrec) / 1e6 < 60 && bouge >= 24f || doigts > 1) {
                        elan = scope.launch {
                            var t = System.nanoTime()
                            while (Math.abs(vL) + Math.abs(vT) > 0.05f || Math.hypot(vDx.toDouble(), vDy.toDouble()) > etat.dist * 0.01) {
                                androidx.compose.runtime.withFrameNanos { }
                                val n = System.nanoTime(); val dt = ((n - t) / 1e9f).coerceIn(0.001f, 0.05f); t = n
                                etat.lacet += vL * dt; etat.tangage = (etat.tangage + vT * dt).coerceIn(-0.25f, 1.55f)
                                val d = etat.decal; etat.decal = floatArrayOf(d[0] + vDx * dt, d[1] + vDy * dt, d[2])
                                val f = Math.exp(-5.0 * dt).toFloat(); vL *= f; vT *= f; vDx *= f; vDy *= f
                                vueRef[0]?.requestRender()
                            }
                        }
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
