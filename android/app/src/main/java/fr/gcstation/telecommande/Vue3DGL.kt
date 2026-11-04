package fr.gcstation.telecommande

// Appli build 233 (build 242 : image calculee en plus haute resolution, nettete, halo lumineux, ombres adoucies) : le niveau en 3D avec ses vraies textures (OpenGL ES 2). Le pack de cartes (fabrique a part depuis les
// fichiers du jeu, jamais dans l'appli) donne, par niveau : <niveau>.r3v (groupes de triangles par texture : sommets x y z,
// u v, couleur r g b a) et les images <niveau>_tNN.png. Un doigt : tourner ; deux doigts : zoom ; double toucher : revenir
// derriere la camera du jeu. Rayman : point rouge ; ton chemin : trait jaune.

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.background
import androidx.compose.foundation.border
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
import androidx.compose.foundation.horizontalScroll
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
/** build 245 : un objet du niveau (pack : <niveau>_obj.txt) ; type : c cage, f p'tit etre fantome, j lum jaune, r lum rouge, p pouvoir */
class Objet3D(val type: Char, val x: Float, val y: Float, val z: Float, val nom: String, val etat: Long = -1L) {
    @Volatile var pris = false          // build 256 : ramasse pendant la partie (lu en direct dans la memoire du jeu)
}

class SceneR3(val dossier: java.io.File, val niveau: String, val groupes: List<Groupe>, val centre: FloatArray, val rayon: Float = 60f,
              val objets: List<Objet3D> = emptyList()) {
    @Volatile var versionPris = 0       // build 256 : change quand un objet est ramasse (ou reapparait) ; la vue se refait
    /** build 332 : Mario Kart, les objets qui bougent (pack : <circuit>_mob.r3v et _mob.txt) */
    var mobiles: ObjetsMobiles? = null
    /** build 353 : Wind Waker, les objets animes par le jeu (pack : <lieu>_ani.r3a) */
    var animes: AnimesWW? = null
    /** build 349 : la hauteur de la mer (Wind Waker : un grand carre transparent sans texture, a plat) ; NaN : pas de mer */
    val niveauEau: Float by lazy {
        groupes.firstOrNull { g -> g.texture < 0 && g.mode == 2 && g.n == 6 && (0 until 6).all { k -> g.sommets.getFloat(k * g.pas + 8) == g.sommets.getFloat(8) } }
            ?.sommets?.getFloat(8) ?: Float.NaN
    }
    class Groupe(val texture: Int, val alpha: Boolean, val sommets: ByteBuffer, val n: Int,
                 val mode: Int = if (alpha) 2 else 0, val enveloppe: Int = 0,
                 val lumiere: Int = -1, val lumiereK: Float = 0f, val pas: Int = 24) {  // (R3V3 : ombres precalculees du jeu, 2e texture)
        var defil = FloatArray(2)   // (R3V3 : texture qui defile, unites par seconde : eau, lave, sable qui coule)  // (build 236 : 0 opaque, 1 decoupe, 2 transparent, 3 lumiere ; miroir U/V)
        var eau = 0          // build 245 : 1 eau, 2 lave (d'apres le nom de la texture : <niveau>_tex.txt) ; animees par le moteur
                             // build 276 : 3 lave peinte (texture claire que le jeu colore en rouge : la mer de lave de knaar_10)
        var chaud = false    // build 276 : sous la lumiere rouge-orange du jeu (zone du crane de knaar_10)
        var vent = false     // build 356 : herbe de Wind Waker : ondule (2e coordonnees : hauteur dans la touffe, phase)
        /** build 356 : clignements (suite de textures) et couleurs animees (multiplicateurs r g b a), images par seconde */
        var texSeq: IntArray? = null; var texIps = 30f; var coulSeq: FloatArray? = null; var coulIps = 30f
    }
    companion object {
        /** build 356 : une ligne de suite (clignement ou couleur) a partir du mot k : type, groupe, images par seconde,
         *  nombre, valeurs */
        fun lireSuite(p: List<String>, k: Int, groupe: (Int) -> Groupe?) {
            val g = groupe(p.getOrNull(k + 1)?.toIntOrNull() ?: return) ?: return
            val ips = p.getOrNull(k + 2)?.toFloatOrNull() ?: return; val n = p.getOrNull(k + 3)?.toIntOrNull() ?: return
            when (p[k]) {
                "t" -> { val v = IntArray(n) { p.getOrNull(k + 4 + it)?.toIntOrNull() ?: -1 }; if (n > 0 && v.all { it >= 0 }) { g.texSeq = v; g.texIps = ips } }
                "c" -> { val v = FloatArray(n * 4) { p.getOrNull(k + 4 + it)?.toFloatOrNull() ?: 1f }; if (n > 0) { g.coulSeq = v; g.coulIps = ips } }
            }
        }
        /** fichier .r3v : "R3V1", nombre de groupes ; par groupe : texture (s32), alpha (u32), nombre de sommets (u32),
         *  puis les sommets (24 octets : 5 flottants + 4 octets de couleur), petit-boutiste */
        fun lire(d: java.io.File, niveau: String): SceneR3? = try {
            val f = java.io.File(d, niveau.lowercase() + ".r3v")
            if (!f.exists()) null else {
                val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
                val magie = b.int
                if (magie != 0x31563352 && magie != 0x32563352 && magie != 0x33563352) null else {          // "R3V1", "R3V2" ou "R3V3"
                    val v3 = magie == 0x33563352; val v2 = magie == 0x32563352 || v3
                    val ng = b.int
                    val l = ArrayList<Groupe>()
                    var sx = 0.0; var sy = 0.0; var sz = 0.0; var nn = 0L
                    val ech = ArrayList<Float>()   // (build 242 : points echantillons pour la taille du niveau)
                    for (g in 0 until ng) {
                        // (R3V3 : + texture des ombres (s32, -1 : aucune) et son facteur (f32) ; sommets de 32 octets : + leurs coordonnees)
                        val tex = b.int; val md = b.int; val env = if (v2) b.int else 0
                        val lm = if (v3) b.int else -1; val lk = if (v3) b.float else 0f; val n = b.int
                        val pas = if (v3) 32 else 24
                        val al = md != 0
                        val bb = ByteBuffer.allocateDirect(n * pas).order(ByteOrder.nativeOrder())
                        val src = ByteArray(n * pas); b.get(src)
                        // (les flottants du fichier sont petit-boutistes : comme le processeur du telephone)
                        bb.put(src); bb.position(0)
                        for (k in 0 until n step 17) { sx += bb.getFloat(k * pas); sy += bb.getFloat(k * pas + 4); sz += bb.getFloat(k * pas + 8); nn++
                            ech.add(bb.getFloat(k * pas)); ech.add(bb.getFloat(k * pas + 4)) }
                        l.add(Groupe(tex, al, bb, n, if (v2) md else if (al) 2 else 0, env, lm, lk, pas))
                    }
                    if (!v3) { sansBleuSprites(l); adoucirEclairage(l) }   // (R3V3 : les couleurs du jeu telles quelles, ombres comprises)
                    val ctr = if (nn > 0) floatArrayOf((sx / nn).toFloat(), (sy / nn).toFloat(), (sz / nn).toFloat()) else floatArrayOf(0f, 0f, 0f)
                    // rayon : 85 % des points du decor sont a moins de cette distance (au sol) du centre ; les objets tres lointains ne comptent pas
                    val ds = (0 until ech.size / 2).map { Math.hypot((ech[it * 2] - ctr[0]).toDouble(), (ech[it * 2 + 1] - ctr[1]).toDouble()).toFloat() }.sorted()
                    val ray = if (ds.isEmpty()) 60f else ds[(ds.size * 0.85).toInt().coerceAtMost(ds.size - 1)].coerceIn(15f, 900f)
                    val nv = niveau.lowercase()
                    // build 245 : noms des textures -> eau / lave
                    val ft = java.io.File(d, nv + "_tex.txt")
                    if (ft.exists()) {
                        val eau = Regex("water|_eau|eau\\d|waterfx"); val lave = Regex("magma|fusion")
                        // build 276 : dans knaar_10, la « kna_waterfx » est verte dans le fichier mais le jeu l'eclaire en
                        // rouge : c'est la mer de lave (vu sur la console) ; la zone du crane (textures dead_*) est sous la meme lumiere
                        val lavePeinte = Regex("^kna_waterfx"); val chaud = if (nv.startsWith("knaar_10")) Regex("^dead_|^kna_waterfx") else null
                        val type = HashMap<Int, Int>(); val chauds = HashSet<Int>()
                        for (li in ft.readLines()) { val p = li.trim().split(' ', limit = 2); if (p.size < 2) continue
                            val k = p[0].toIntOrNull() ?: continue
                            type[k] = if (lavePeinte.containsMatchIn(p[1])) 3 else if (lave.containsMatchIn(p[1])) 2 else if (eau.containsMatchIn(p[1])) 1 else 0
                            if (chaud?.containsMatchIn(p[1]) == true) chauds.add(k) }
                        // build 332 : R3V3 : les teintes du jeu sont deja dans les couleurs des sommets (knaar_10 : os blancs, sol
                        // orange, d'apres une photo de la console) ; la teinte chaude n'est gardee que pour les anciens packs
                        for (g in l) { g.eau = type[g.texture] ?: 0; g.chaud = !v3 && g.texture in chauds }
                    }
                    // R3V3 : <niveau>_anim.txt : groupe, defilement u et v par seconde
                    java.io.File(d, nv + "_anim.txt").takeIf { it.exists() }?.readLines()?.forEach { li -> val p = li.trim().split(' ')
                        val gi = p.getOrNull(0)?.toIntOrNull() ?: return@forEach; val du = p.getOrNull(1)?.toFloatOrNull() ?: 0f; val dv = p.getOrNull(2)?.toFloatOrNull() ?: 0f
                        l.getOrNull(gi)?.defil = floatArrayOf(du, dv)
                        if (p.getOrNull(3) == "1") l.getOrNull(gi)?.vent = true }
                    // build 356 : <niveau>_coul.txt : « t groupe ips n textures... » et « c groupe ips n r g b a ... »
                    java.io.File(d, nv + "_coul.txt").takeIf { it.exists() }?.readLines()?.forEach { li -> lireSuite(li.trim().split(' '), 0) { l.getOrNull(it) } }
                    // build 245 : objets (cages, lums, pouvoirs)
                    val fo = java.io.File(d, nv + "_obj.txt")
                    val obj = if (!fo.exists()) emptyList() else fo.readLines().mapNotNull { li ->
                        val p = li.trim().split(' ')
                        if (p.size < 4 || p[0].length != 1) null else {
                            val x = p[1].toFloatOrNull(); val y = p[2].toFloatOrNull(); val z = p[3].toFloatOrNull()
                            if (x == null || y == null || z == null) null
                            else Objet3D(p[0][0], x, y, z, p.getOrElse(4) { "" }, p.getOrNull(5)?.toLongOrNull(16)?.takeIf { it > 0 } ?: -1L)
                        }
                    }
                    SceneR3(d, nv, l, ctr, ray, obj).also { it.mobiles = ObjetsMobiles.lire(d, nv); it.animes = AnimesWW.lire(d, nv) }
                }
            }
        } catch (e: Throwable) { null }

        /** build 242 : les ombres du jeu sont une couleur par sommet ; un meme point du decor est souvent repete dans
         *  plusieurs groupes avec des couleurs un peu differentes, ce qui dessine des raccords. On rapproche chaque
         *  sommet (a moitie) de la moyenne des sommets au meme endroit. Les lumieres (additives) ne sont pas touchees. */
        /** build 278 : les sprites generes par le moteur (herbes de la Lande : lan_spritegrass*, fumees) ont des couleurs de
         *  sommets non remplies (bleu pur) : le jeu ne s'en sert pas, l'appli les teintait en bleu nuit. On leur donne la
         *  lumiere moyenne du niveau (repere : rouge et vert presque nuls, bleu au milieu ; vu sur 4 groupes de 3 niveaux). */
        private fun sansBleuSprites(l: List<Groupe>) {
            val moy = IntArray(4)
            fun m(g: Groupe): IntArray { val r = IntArray(3); for (k in 0 until g.n) for (c in 0..2) r[c] += g.sommets.get(k * 24 + 20 + c).toInt() and 255
                return IntArray(3) { r[it] / maxOf(1, g.n) } }
            val bleus = l.filter { g -> g.n > 0 && m(g).let { it[0] < 45 && it[1] < 45 && it[2] > 100 } }
            if (bleus.isEmpty()) return
            for (g in l) if (g !in bleus && g.mode != 3) for (k in 0 until g.n) { for (c in 0..2) moy[c] += g.sommets.get(k * 24 + 20 + c).toInt() and 255; moy[3]++ }
            val r = IntArray(3) { if (moy[3] > 0) moy[it] / moy[3] else 110 }
            for (g in bleus) { for (k in 0 until g.n) for (c in 0..2) g.sommets.put(k * 24 + 20 + c, r[c].toByte()); g.sommets.position(0) }
        }
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

/** build 245 : l'ambiance de chaque monde (ciel, brouillard, teinte de l'eau, halo). Reglee a l'oeil d'apres le jeu :
 *  les couleurs du decor et de la lumiere viennent toujours des fichiers du jeu, seuls le fond et la brume sont choisis ici. */
class Ambiance(val cielHaut: FloatArray, val cielBas: FloatArray, val brume: FloatArray, val brumeDebut: Float, val brumeLong: Float,
               val brumeMax: Float, val eau: FloatArray, val bloom: Float) {
    companion object {
        private fun c(r: Int, g: Int, b: Int) = floatArrayOf(r / 255f, g / 255f, b / 255f)
        fun pour(niveau: String): Ambiance { val n = niveau.lowercase(); return when {
            // build 266 : Mario Kart Double Dash (circuits nommes comme leurs archives)
            n == "rainbow" -> Ambiance(c(4, 4, 22), c(30, 20, 70), c(18, 14, 46), 300f, 1200f, 0.5f, c(150, 160, 230), 0.5f)
            n == "koopa" -> Ambiance(c(20, 6, 6), c(90, 30, 18), c(60, 22, 14), 200f, 900f, 0.55f, c(150, 120, 110), 0.5f)
            n == "patapata" || n == "wario" || n == "waluigi" -> Ambiance(c(10, 14, 50), c(70, 60, 120), c(40, 40, 80), 300f, 1100f, 0.45f, c(130, 160, 210), 0.45f)
            n == "snow" -> Ambiance(c(80, 130, 210), c(220, 232, 245), c(205, 215, 230), 250f, 1000f, 0.5f, c(170, 210, 240), 0.35f)
            n == "desert" -> Ambiance(c(90, 150, 220), c(240, 215, 160), c(225, 200, 160), 250f, 1100f, 0.45f, c(120, 180, 200), 0.35f)
            n == "diddy" -> Ambiance(c(60, 110, 160), c(170, 200, 170), c(130, 160, 130), 200f, 900f, 0.55f, c(110, 160, 140), 0.35f)
            n.startsWith("intro") -> Ambiance(c(24, 36, 92), c(70, 92, 150), c(52, 66, 110), 160f, 700f, 0.55f, c(150, 190, 230), 0.45f)     // nuit des fees
            n.startsWith("wood") -> Ambiance(c(70, 130, 210), c(175, 205, 225), c(150, 175, 165), 200f, 800f, 0.5f, c(150, 200, 190), 0.35f)  // foret de jour
            n.startsWith("swamp") -> Ambiance(c(10, 10, 30), c(38, 34, 64), c(30, 32, 48), 60f, 420f, 0.78f, c(105, 110, 95), 0.22f)       // marais de nuit
            n.startsWith("moor") -> Ambiance(c(34, 24, 62), c(96, 78, 120), c(70, 60, 92), 110f, 600f, 0.65f, c(120, 140, 170), 0.3f)        // lande crepusculaire
            n.startsWith("knaar") -> Ambiance(c(26, 10, 6), c(92, 44, 24), c(64, 32, 20), 120f, 650f, 0.6f, c(130, 150, 110), 0.5f)            // grottes, lave
            n.startsWith("flash") || n.startsWith("sea") -> Ambiance(c(60, 120, 220), c(170, 210, 240), c(150, 190, 225), 220f, 900f, 0.5f, c(120, 190, 230), 0.35f)
            n.startsWith("mount") -> Ambiance(c(80, 120, 200), c(210, 225, 240), c(200, 210, 228), 160f, 700f, 0.6f, c(170, 200, 230), 0.4f)  // neige
            n.startsWith("fact") || n.startsWith("snipe") -> Ambiance(c(26, 22, 20), c(80, 70, 60), c(62, 56, 50), 120f, 600f, 0.55f, c(110, 140, 120), 0.45f)
            n.startsWith("tower") || n.startsWith("lept") -> Ambiance(c(36, 18, 60), c(120, 80, 140), c(84, 62, 110), 140f, 700f, 0.55f, c(130, 140, 190), 0.4f)
            else -> Ambiance(c(41, 77, 158), c(158, 184, 219), c(107, 128, 174), 250f, 900f, 0.55f, c(170, 190, 220), 0.4f)
        } }
    }
}

/** build 296 : un kart de Mario Kart a dessiner : modele de kart, pilote, passager (numeros du jeu), matrice du kart dans le
 *  jeu (3 lignes de 4 flottants : rotation, puis la position ; repere du jeu, y vers le haut) */
class KartVue(val kart: Int, val pilote: Int, val passager: Int, val mat: FloatArray, val joueur: Boolean, val n: Int = -1, val echange: Boolean = false)

/** ce que la vue regarde (modifie par les doigts et par le jeu, lu par le fil OpenGL) */
class Etat3D {
    @Volatile var lacet = Float.NaN; @Volatile var tangage = 0.5f; @Volatile var dist = 70f
    @Volatile var cible = floatArrayOf(0f, 0f, 0f); @Volatile var camera: FloatArray? = null
    @Volatile var chemin: FloatArray = FloatArray(0); @Volatile var montrerRayman = true
    @Volatile var decal = floatArrayOf(0f, 0f, 0f)       // (build 236 : deplacement a deux doigts, ajoute a la cible)
    @Volatile var redessiner: () -> Unit = {}
    @Volatile var voir = "cfjkvrpidtuq"
    /** build 356 : Wind Waker, les objets lus en direct (poses) et les modeles du pack qu'ils remplacent */
    @Volatile var vifsWW: List<JeuWW.VifPose>? = null; @Volatile var modelesVifs: Set<Int> = emptySet()
    /** build 266 : autres pions a montrer (Mario Kart : les autres karts) : x, y, z, couleur (ARGB en Float.fromBits) par pion */
    @Volatile var pions = FloatArray(0)
    /** build 269 : position lue en direct ~30 fois par seconde ; le moteur la lisse a chaque image (plus de sauts) */
    @Volatile var direct = false                          // build 245 : types d'objets affiches
    /** build 272 : la derniere pose de Rayman lue dans la memoire (squelette, objets, textures) */
    @Volatile var pose: PoseRayman? = null
    /** journal de bug (bouton « 🐞 Bug ») : la prochaine image est lue et rendue ici */
    @Volatile var captureCb: ((android.graphics.Bitmap?) -> Unit)? = null
    /** build 338 : Wind Waker, Link en 3D : cap (radians, angle du jeu), vitesse (unites du jeu par image), sous l'eau (0/1) */
    @Volatile var linkWW: FloatArray? = null
    /** build 340 : Wind Waker, la vraie pose de Link lue en direct : 42 matrices du monde (3 x 4, repere du jeu) et l'heure
     *  de lecture (System.nanoTime) */
    @Volatile var poseWW: FloatArray? = null; @Volatile var tPoseWW = 0L
    /** build 340 : la camera du jeu pour « Vue du jeu » hors Mario Kart : matrice de vue (3 x 4, repere du jeu, comme
     *  Mario Kart) puis l'angle de vue vertical (degres) ; et l'heure de lecture */
    @Volatile var camJeu: FloatArray? = null; @Volatile var tCamJeu = 0L; @Volatile var aCamJeu = false
    /** build 340 : direction du personnage (cap dans le repere de l'appli, radians ; NaN : inconnue) pour « 3e personne » */
    @Volatile var capPerso = Float.NaN
    /** build 345 : rejeu du direct, appele avant chaque image (sans reseau) : met a jour la pose, la position, la camera */
    @Volatile var rejeu: (() -> Unit)? = null
    @Volatile var directRejeu = false
    /** build 296 : Mario Kart, les karts a dessiner en 3D (le premier est le tien) */
    @Volatile var karts: List<KartVue> = emptyList()
    /** build 282 : 0 normal (la vue suit la position), 1 suivre (derriere Rayman, tournee comme lui), 2 libre (la vue ne bouge plus seule),
     *  3 (Mario Kart) 3e personne : juste derriere ton kart, a sa hauteur, tournee comme lui */
    @Volatile var mode = 0
    @Volatile var libre: FloatArray? = null
    /** build 332 : 5 visite (premiere personne) : la camera est a cette place et tourne sur elle-meme (lacet : cap,
     *  tangage : regard vers le bas) ; on avance ou l'on regarde. Plus de point central : on entre dans les salles. */
    @Volatile var oeil = floatArrayOf(0f, 0f, 0f)
    /** build 332 : derniere position de la camera dessinee (pour passer en visite sans saut) */
    @Volatile var camPos: FloatArray? = null
    /** build 332 : le regard (vecteur unite) en visite */
    fun regard(): FloatArray { val a = lacetActuel().toDouble(); val t = tangage.toDouble()
        return floatArrayOf((Math.cos(a) * Math.cos(t)).toFloat(), (Math.sin(a) * Math.cos(t)).toFloat(), (-Math.sin(t)).toFloat()) }
    /** build 332 : visite : avancer (ou reculer) de k unites dans le regard */
    fun avancer(k: Float) { val r = regard(); val o = oeil; oeil = floatArrayOf(o[0] + r[0] * k, o[1] + r[1] * k, o[2] + r[2] * k) }
    /** build 332 : visite : se decaler (droite / haut de l'ecran) */
    fun glisser(droite: Float, haut: Float) {
        val a = lacetActuel().toDouble(); val rx = Math.sin(a).toFloat(); val ry = -Math.cos(a).toFloat()
        val o = oeil; oeil = floatArrayOf(o[0] + rx * droite, o[1] + ry * droite, o[2] + haut)
    }
    /** build 332 : visite : passer en premiere personne a la place de la camera actuelle (meme cap, meme inclinaison) */
    fun entrerVisite() {
        vol?.cancel()
        if (lacet.isNaN()) lacet = lacetActuel()
        val c = camPos
        oeil = c?.copyOf() ?: run { val r = regard(); val b = cible; floatArrayOf(b[0] - r[0] * dist, b[1] - r[1] * dist, b[2] - r[2] * dist) }
        tangage = tangage.coerceIn(-1.3f, 1.3f); mode = 5
    }
    // build 247 : derniere projection dessinee (pour savoir quel objet est sous le doigt) et vol anime de la camera
    @Volatile var mvpVue: FloatArray? = null; @Volatile var vueW = 1; @Volatile var vueH = 1
    var vol: kotlinx.coroutines.Job? = null
    /** voler en douceur vers un point (coordonnees du monde), une distance, une inclinaison, un cap */
    fun voler(scope: kotlinx.coroutines.CoroutineScope, vers: FloatArray? = null, d: Float? = null, tg: Float? = null, l: Float? = null) {
        vol?.cancel()
        if (lacet.isNaN()) lacet = lacetActuel()
        if (mode == 5) {                                      // (build 332 : visite : l'oeil va se placer devant le point, a hauteur)
            if (vers == null) return
            val r = regard(); val rh = Math.hypot(r[0].toDouble(), r[1].toDouble()).toFloat().coerceAtLeast(0.2f)
            val recul = minOf(d ?: 10f, 12f)
            val o0 = oeil.copyOf(); val o1 = floatArrayOf(vers[0] - r[0] / rh * recul, vers[1] - r[1] / rh * recul, vers[2] + 2f)
            val t1 = 0.15f; val tg0 = tangage
            vol = scope.launch {
                val t0 = System.nanoTime()
                while (true) {
                    androidx.compose.runtime.withFrameNanos { }
                    val k = ((System.nanoTime() - t0) / 450e6).toFloat().coerceAtMost(1f); val e = 1f - (1f - k) * (1f - k) * (1f - k)
                    oeil = FloatArray(3) { o0[it] + (o1[it] - o0[it]) * e }; tangage = tg0 + (t1 - tg0) * e
                    redessiner(); if (k >= 1f) break
                }
            }
            return
        }
        val c = if (mode == 2) (libre ?: cible) else cible; val dc0 = decal.copyOf(); val dist0 = dist; val tg0 = tangage; val l0 = lacet   // (build 282 : en mode libre, par rapport au point fixe)
        val dc1 = if (vers != null) floatArrayOf(vers[0] - c[0], vers[1] - c[1], vers[2] - c[2]) else dc0
        var dl = (l ?: l0) - l0
        while (dl > Math.PI) dl -= (2 * Math.PI).toFloat(); while (dl < -Math.PI) dl += (2 * Math.PI).toFloat()
        vol = scope.launch {
            val t0 = System.nanoTime()
            while (true) {
                androidx.compose.runtime.withFrameNanos { }
                val k = ((System.nanoTime() - t0) / 450e6).toFloat().coerceAtMost(1f)
                val e = 1f - (1f - k) * (1f - k) * (1f - k)          // depart vif, arrivee douce
                decal = FloatArray(3) { dc0[it] + (dc1[it] - dc0[it]) * e }
                dist = dist0 + ((d ?: dist0) - dist0) * e; tangage = tg0 + ((tg ?: tg0) - tg0) * e; lacet = l0 + dl * e
                redessiner()
                if (k >= 1f) break
            }
        }
    }
    /** l'objet le plus proche du doigt (en pixels de la vue), parmi les sortes affichees ; null si rien a moins de 48 px */
    fun objetSous(objets: List<Objet3D>, x: Float, y: Float): Objet3D? {
        val m = mvpVue ?: return null; var best: Objet3D? = null; var bd = 48f * 48f
        for (o in objets) {
            if (o.type !in voir || o.pris) continue
            val z = o.z + (if (o.type == 'c') -2f else if (o.type == 'f') 1.2f else 0.6f)
            val cx = m[0] * o.x + m[4] * o.y + m[8] * z + m[12]; val cy = m[1] * o.x + m[5] * o.y + m[9] * z + m[13]
            val cw = m[3] * o.x + m[7] * o.y + m[11] * z + m[15]
            if (cw <= 0.1f) continue
            val sx = (cx / cw * 0.5f + 0.5f) * vueW; val sy = (1f - (cy / cw * 0.5f + 0.5f)) * vueH
            val dd = (sx - x) * (sx - x) + (sy - y) * (sy - y) - (if (o.type == 'c') 300f else 0f)   // (les cages gagnent a egalite)
            if (dd < bd) { bd = dd; best = o }
        }
        return best
    }
    @Volatile var distBase = 70f                           // (build 242 : distance de depart, adaptee a la taille du niveau)
    fun recentrer() { if (mode == 5) mode = 0; lacet = Float.NaN; tangage = if (montrerRayman) 0.5f else 0.75f; dist = distBase; decal = floatArrayOf(0f, 0f, 0f) }
    /** build 242 : une vue du dessus cadrant tout le niveau */
    fun dessus() { if (lacet.isNaN()) lacet = lacetActuel(); tangage = 1.5f; dist = maxOf(dist, distBase * 1.6f) }
    /** build 242 : zoom par boutons (facteur < 1 : rapprocher) */
    fun zoom(f: Float) { if (mode == 5) avancer((1f - f) * 20f) else dist = (dist * f).coerceIn(2f, 2500f) }   // (build 332 : visite : + avance, − recule)
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
    // build 245 : ambiance, eau animee, objets
    private val amb = Ambiance.pour(scene.niveau); private val t0 = System.nanoTime()
    private var uVentL = 0; private var uMulL = 0
    private var uChaudL = 0; private var uEauL = 0; private var uTL = 0; private var uBrumeL = 0; private var uBrumePL = 0; private var uTeinteL = 0
    private var progO = 0
    private val objBuf = HashMap<Char, FloatBuffer>()
    // build 249 : les vrais visuels du jeu (pack : fichiers communs r3_*) : gemme 3D au reflet dore, lum rouge, icones des pouvoirs
    private var progG = 0; private var progS = 0
    // build 253 : les trois gemmes du jeu (j jaune, k rouge, v verte) : modele + texture de reflet + positions
    private class Gemme(val mod: FloatBuffer, val n: Int, val tex: Int, val pos: ArrayList<FloatArray>)
    private val gemmesC = HashMap<Char, Gemme>()
    private var texRouge = 0
    // build 261 : la cage du jeu (modele 3D sous son ballon) : x y z u v nx ny nz par sommet, coordonnees du jeu (origine = l'objet)
    private var progM = 0; private var cageMod: FloatBuffer? = null; private var nCage = 0; private var texCage = 0
    private val cagesPos = ArrayList<FloatArray>()
    // build 261 : les boites de pouvoir du jeu (une par pouvoir : grappin, helico, poing, roulade, snipe), meme format que la cage
    private class Modele(val mod: FloatBuffer, val n: Int)
    private val boites = HashMap<String, Modele>(); private var texBoite = 0
    private var raymanMod: Modele? = null; private var texRayman = 0
    // build 272 : Rayman anime (pack : r3_rayman_anim.txt) ; deux poses gardees pour glisser de l'une a l'autre
    private var animMod: ModeleAnime? = null; private val texAnim = HashMap<String, Int>()
    private var poseA: PoseRayman? = null; private var poseB: PoseRayman? = null
    private var peauA = HashMap<Int, FloatArray>(); private var peauB = HashMap<Int, FloatArray>()
    private var tB = 0L; private var dtAB = 1L
    private var peauPrec = HashMap<Int, FloatArray>(); private var tPrec = 0L; private var dtPrec = 1L
    private var ouDebut: FloatArray? = null; private var ouFin: FloatArray? = null
    private val matJoue = FloatArray(PoseRayman.N * 15); private val peauJoue = HashMap<Int, FloatArray>()
    private var animAffichee: AnimA3D? = null; private var locAffichee: FloatArray? = null; private var locDepart: FloatArray? = null; private var tChange = 0L
    private val bufEl = HashMap<String, FloatBuffer>()
    private val boitesPos = HashMap<String, ArrayList<FloatArray>>()
    // build 288 : Mario Kart : la vraie boite a objets du jeu (pack : mk_boite.txt et ses textures) ; plusieurs materiaux
    private class Morceau(val tex: Int, val mode: Int, val mod: FloatBuffer, val n: Int)
    /** personnage avec sa peau (pack : mk_peau_<n>.txt) : pour chaque sommet, ses articulations, poids et positions locales */
    private class PeauMorceau(val tex: Int, val mode: Int, val n: Int, val uv: FloatArray, val nb: IntArray, val deb: IntArray,
                              val js: IntArray, val ws: FloatArray, val ps: FloatArray, val buf: FloatBuffer)
    private val mkPeaux = HashMap<Int, List<PeauMorceau>?>()
    private fun lirePeau(cid: Int): List<PeauMorceau>? = mkPeaux.getOrPut(cid) { try {
        val f = java.io.File(scene.dossier, if (cid == -1) "mk_chomp.txt" else "mk_peau_$cid.txt")
        if (!f.exists()) null else {
            val r = ArrayList<PeauMorceau>(); var tex = 0; var mode = 0
            val uv = ArrayList<Float>(); val nb = ArrayList<Int>(); val js = ArrayList<Int>(); val ws = ArrayList<Float>(); val ps = ArrayList<Float>()
            fun fin() { val n = nb.size; if (n >= 3 && tex != 0) {
                val deb = IntArray(n); var a = 0; for (i in 0 until n) { deb[i] = a; a += nb[i] }
                r.add(PeauMorceau(tex, mode, n - n % 3, uv.toFloatArray(), nb.toIntArray(), deb, js.toIntArray(), ws.toFloatArray(), ps.toFloatArray(),
                    ByteBuffer.allocateDirect(n * 32).order(ByteOrder.nativeOrder()).asFloatBuffer())) }
                uv.clear(); nb.clear(); js.clear(); ws.clear(); ps.clear() }
            for (l in f.readLines()) {
                val p = l.trim().split(' ').filter { it.isNotEmpty() }
                if (p.isEmpty() || p[0] == "j") continue
                if (p[0] == "t") { fin(); tex = if (p.size > 1 && p[1] != "-") texNom.getOrPut(p[1]) { chargerTex(p[1], repete = true) } else 0; mode = p.getOrNull(2)?.toIntOrNull() ?: 0; continue }
                val k = p.getOrNull(2)?.toIntOrNull() ?: continue
                if (p.size < 3 + 5 * k) continue
                uv.add(p[0].toFloat()); uv.add(p[1].toFloat()); nb.add(k)
                for (q in 0 until k) { js.add(p[3 + 5 * q].toInt()); ws.add(p[4 + 5 * q].toFloat()); ps.add(p[5 + 5 * q].toFloat()); ps.add(p[6 + 5 * q].toFloat()); ps.add(p[7 + 5 * q].toFloat()) }
            }
            fin(); r.sortBy { if (it.mode == 2) 1 else 0 }
            r.ifEmpty { null }
        }
    } catch (e: Throwable) { null } }
    /** la peau posee avec les articulations lues (relatives au kart, unites du jeu) -> sommets dans le repere du kart (x 0,01) */
    private fun poser(m: PeauMorceau, r: FloatArray, monde: Boolean = false) {
        val b = m.buf; b.clear(); val nj = r.size / 12
        val pos = FloatArray(9)
        var v = 0
        while (v + 2 < m.n) {
            for (c in 0..2) {
                val i = v + c; var x = 0f; var y = 0f; var z = 0f
                for (q in 0 until m.nb[i]) {
                    val a = m.deb[i] + q; val j = m.js[a].coerceIn(0, nj - 1) * 12; val w = m.ws[a]
                    val lx = m.ps[3 * a]; val ly = m.ps[3 * a + 1]; val lz = m.ps[3 * a + 2]
                    x += w * (r[j] * lx + r[j + 1] * ly + r[j + 2] * lz + r[j + 3])
                    y += w * (r[j + 4] * lx + r[j + 5] * ly + r[j + 6] * lz + r[j + 7])
                    z += w * (r[j + 8] * lx + r[j + 9] * ly + r[j + 10] * lz + r[j + 11])
                }
                if (monde) { pos[c * 3] = x * 0.01f; pos[c * 3 + 1] = -z * 0.01f; pos[c * 3 + 2] = y * 0.01f }     // (repere du jeu -> de l'appli)
                else { pos[c * 3] = x * 0.01f; pos[c * 3 + 1] = y * 0.01f; pos[c * 3 + 2] = z * 0.01f }
            }
            val ux = pos[3] - pos[0]; val uy = pos[4] - pos[1]; val uz = pos[5] - pos[2]
            val wx = pos[6] - pos[0]; val wy = pos[7] - pos[1]; val wz = pos[8] - pos[2]
            var nx = uy * wz - uz * wy; var ny = uz * wx - ux * wz; var nz = ux * wy - uy * wx
            val ln = Math.sqrt((nx * nx + ny * ny + nz * nz).toDouble()).toFloat().takeIf { it > 1e-9f } ?: 1f
            nx /= ln; ny /= ln; nz /= ln
            for (c in 0..2) { b.put(pos[c * 3]).put(pos[c * 3 + 1]).put(pos[c * 3 + 2]).put(m.uv[2 * (v + c)]).put(m.uv[2 * (v + c) + 1]).put(nx).put(ny).put(nz) }
            v += 3
        }
        b.position(0)
    }
    private var mkBoite: List<Morceau>? = null; private var progMB = 0
    // build 296 : karts, pilotes et passagers (pack : mk_kart_N.txt, mk_pilote_N.txt, mk_passager_N.txt, mk_karts.txt : sieges)
    private val mkSieges = HashMap<Int, FloatArray>(); private val mkModeles = HashMap<String, List<Morceau>?>(); private var progK = 0
    private val texNom = HashMap<String, Int>()
    private fun modeleMK(nom: String): List<Morceau>? = mkModeles.getOrPut(nom) { lireMulti(nom) }
    private val mkBoitesPos = ArrayList<FloatArray>()
    private fun lireMulti(nom: String): List<Morceau>? = try {
        val f = java.io.File(scene.dossier, nom)
        if (!f.exists()) null else {
            val r = ArrayList<Morceau>(); var tex = 0; var mode = 0; var v = ArrayList<FloatArray>()
            fun fin() { if (v.isNotEmpty() && tex != 0) { val fb = ByteBuffer.allocateDirect(v.size * 32).order(ByteOrder.nativeOrder()).asFloatBuffer()
                for (q in v) fb.put(q); fb.position(0); r.add(Morceau(tex, mode, fb, v.size)) }; v = ArrayList() }
            for (l in f.readLines()) {
                val p = l.trim().split(' ')
                if (p[0] == "t") { fin(); tex = if (p.size > 1 && p[1] != "-") texNom.getOrPut(p[1]) { chargerTex(p[1], repete = true) } else 0; mode = p.getOrNull(2)?.toIntOrNull() ?: 0 }
                else p.mapNotNull { it.toFloatOrNull() }.takeIf { it.size == 8 }?.let { v.add(it.toFloatArray()) }
            }
            fin(); r.sortBy { if (it.mode == 2) 0 else if (it.mode == 0) 1 else 2 }      // (le « ? » d'abord, puis la boite translucide, puis le reflet)
            r.ifEmpty { null }
        }
    } catch (e: Throwable) { null }
    private fun lireModele(nom: String): Modele? = try {
        val f = java.io.File(scene.dossier, nom)
        if (!f.exists()) null else {
            val v = f.readLines().filter { !it.startsWith("t ") }.mapNotNull { l -> l.trim().split(' ').mapNotNull { it.toFloatOrNull() }.takeIf { it.size == 8 } }
            if (v.isEmpty()) null else {
                val fb = ByteBuffer.allocateDirect(v.size * 32).order(ByteOrder.nativeOrder()).asFloatBuffer()
                for (q in v) for (x in q) fb.put(x)
                fb.position(0); Modele(fb, v.size)
            }
        }
    } catch (e: Throwable) { null }
    private val texPouvoir = HashMap<String, Int>()
    private val pouvoirBuf = HashMap<String, FloatBuffer>()
    private fun chargerTex(nom: String, repete: Boolean = false): Int {
        val f = java.io.File(scene.dossier, nom); if (!f.exists()) return 0
        val bmp = android.graphics.BitmapFactory.decodeFile(f.path) ?: return 0
        val t = IntArray(1); GLES20.glGenTextures(1, t, 0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t[0])
        GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bmp, 0); bmp.recycle()
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        val w = if (repete) GLES20.GL_REPEAT else GLES20.GL_CLAMP_TO_EDGE
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, w)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, w)
        return t[0]
    }
    private val quad: FloatBuffer = ByteBuffer.allocateDirect(32).order(ByteOrder.nativeOrder()).asFloatBuffer().put(floatArrayOf(-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f)).also { it.position(0) }
    private var aUvL = -1; private var uLm = -1; private var uLmK = -1; private var uDefil = -1
    private val lumieres = HashMap<Int, Int>()          // (R3V3 : textures des ombres, sans repetition)
    private var aPos = 0; private var aUv = 0; private var aCol = 0; private var uMvp = 0; private var uTex = 0; private var uAvecTex = 0; private var uCam = 0
    private var lPos = 0; private var lMvp = 0; private var lCoul = 0; private var lTaille = 0
    private val textures = HashMap<Int, Int>()
    private val vbo = IntArray(maxOf(256, scene.groupes.size))     // (build 336 : autant que de groupes : les lieux de Wind Waker en ont beaucoup)
    private val vboM = IntArray(maxOf(256, scene.mobiles?.groupes?.size ?: 0))                     // (build 332 : objets qui bougent)
    private val mMob = FloatArray(16); private val mvpMob = FloatArray(16)
    // build 353 : objets animes (Wind Waker) : un tampon u v couleur par groupe, un tampon de positions par image
    private var vboAniUv = arrayOf<IntArray>(); private var vboAniPos = arrayOf<IntArray>()
    private val mAni = FloatArray(16); private val mAniInv = FloatArray(16); private val mvpAni = FloatArray(16)
    // build 356 : objets en direct : positions calculees (peau) une fois par image, dans des tampons reutilises
    private var vboVif = IntArray(0); private var bufVif = arrayOfNulls<java.nio.FloatBuffer>(0); private var imgVif = LongArray(0); private var noImage = 0L
    private fun tamponVif(i: Int, n: Int): Pair<Int, java.nio.FloatBuffer> {
        if (i >= vboVif.size) {
            val t = IntArray(i + 8); System.arraycopy(vboVif, 0, t, 0, vboVif.size); GLES20.glGenBuffers(t.size - vboVif.size, t, vboVif.size); vboVif = t
            bufVif = bufVif.copyOf(i + 8); imgVif = imgVif.copyOf(i + 8).also { a -> for (k in vboVif.size - 8 until a.size) a[k] = -1L }
        }
        val b = bufVif[i]?.takeIf { it.capacity() >= n * 3 } ?: java.nio.ByteBuffer.allocateDirect(n * 12).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer().also { bufVif[i] = it; imgVif[i] = -1L }
        return vboVif[i] to b
    }
    /** matrice 3 x 4 du jeu (y vers le haut, par lignes) -> matrice 4 x 4 de l'appli (x, -z, y ; 1/100), par colonnes */
    private fun matriceAppli(g: FloatArray, o: Int, m: FloatArray) {
        val q = arrayOf(intArrayOf(0, 1), intArrayOf(2, -1), intArrayOf(1, 1))     // (ligne i de Q : 1 en colonne q[i][0], signe q[i][1])
        for (i in 0..2) for (j in 0..2) m[j * 4 + i] = g[o + q[i][0] * 4 + q[j][0]] * q[i][1] * q[j][1]
        for (i in 0..2) m[12 + i] = 0.01f * g[o + q[i][0] * 4 + 3] * q[i][1]
        m[3] = 0f; m[7] = 0f; m[11] = 0f; m[15] = 1f
    }
    private val proj = FloatArray(16); private val vue = FloatArray(16); private val mvp = FloatArray(16); private val projJeu = FloatArray(16)
    private fun shader(t: Int, src: String): Int { val s = GLES20.glCreateShader(t); GLES20.glShaderSource(s, src); GLES20.glCompileShader(s); return s }
    private fun programme(v: String, f: String): Int {
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, v)); GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, f)); GLES20.glLinkProgram(p)
        return p
    }
    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        prog = programme("""
            uniform mat4 uMvp; uniform vec3 uCam; uniform float uT; uniform float uEau; uniform vec2 uDefil; uniform float uVent;
            attribute vec3 aPos; attribute vec2 aUv; attribute vec4 aCol; attribute vec2 aUvL;
            varying vec2 vUv; varying vec2 vUv2; varying vec4 vCol; varying vec3 vD; varying vec2 vUvL;
            void main() { vec3 p = aPos;
                // build 356 : herbe : le haut de la touffe ondule au vent (2e coordonnees : hauteur relative, phase)
                if (uVent > 0.5) { float o = sin(uT * 1.9 + aUvL.y) * 0.07 * aUvL.x * aUvL.x; p.x += o; p.y += o * 0.7; }
                gl_Position = uMvp * vec4(p, 1.0); vCol = aCol; vUvL = aUvL;
                // eau / lave : deux couches de la texture qui glissent en sens differents (ondulation) ; R3V3 : le defilement du jeu
                vec2 v = uEau > 0.5 ? vec2(uT * 0.035, uT * 0.021) : vec2(0.0);
                vUv = aUv + v + uDefil; vUv2 = aUv * 1.37 + vec2(-uT * 0.027, uT * 0.031);
                // build 347 : la brume est calculee a chaque pixel (la mer de Wind Waker, un seul grand carre dont les coins sont
                // a 500 m, etait toute embrumee a la couleur du ciel : on ne la voyait plus)
                vD = p - uCam; }""", """
            #ifdef GL_FRAGMENT_PRECISION_HIGH
            precision highp float;
            #else
            precision mediump float;
            #endif
            uniform sampler2D uTex; uniform float uAvecTex; uniform float uSeuil; uniform float uEau;
            uniform vec3 uBrume; uniform vec3 uTeinteEau; uniform float uChaud; uniform sampler2D uLm; uniform float uLmK; uniform vec3 uBrumeP; uniform vec4 uMul;
            varying vec2 vUv; varying vec2 vUv2; varying vec4 vCol; varying vec3 vD; varying vec2 vUvL;
            void main() { float vBrume = clamp((length(vD) - uBrumeP.x) / uBrumeP.y, 0.0, uBrumeP.z);
                vec4 t = uAvecTex > 0.5 ? texture2D(uTex, vUv) : vec4(1.0);
                if (uEau > 0.5 && uAvecTex > 0.5) t = mix(t, texture2D(uTex, vUv2), 0.5);
                vec4 c = t * vec4(vCol.rgb * 2.0, vCol.a) * uMul;      // (build 356 : couleurs animees du jeu)
                if (uLmK > 0.0) c.rgb = min(c.rgb * texture2D(uLm, vUvL).rgb * uLmK, vec3(1.0));   // (ombres precalculees du jeu)
                if (uEau > 0.5 && uEau < 1.5) { c.rgb *= uTeinteEau; c.a = min(1.0, c.a * 1.15 + 0.1); }
                if (uEau > 1.5 && uEau < 2.5) c.rgb *= 1.3;
                if (uEau > 2.5) { float l = dot(t.rgb, vec3(0.3, 0.59, 0.11));
                    c = vec4(vec3(1.0, 0.32, 0.07) * (0.25 + 1.9 * l) + vec3(0.35, 0.18, 0.0) * l * l, 1.0); }
                if (uChaud > 0.5 && uEau < 2.5) c.rgb *= vec3(1.45, 0.95, 0.62);
                if (c.a < uSeuil) discard;
                gl_FragColor = vec4(mix(c.rgb, uBrume, vBrume), c.a); }""")
        aPos = GLES20.glGetAttribLocation(prog, "aPos"); aUv = GLES20.glGetAttribLocation(prog, "aUv"); aCol = GLES20.glGetAttribLocation(prog, "aCol")
        aUvL = GLES20.glGetAttribLocation(prog, "aUvL"); uLm = GLES20.glGetUniformLocation(prog, "uLm"); uLmK = GLES20.glGetUniformLocation(prog, "uLmK")
        uDefil = GLES20.glGetUniformLocation(prog, "uDefil")
        uMvp = GLES20.glGetUniformLocation(prog, "uMvp"); uTex = GLES20.glGetUniformLocation(prog, "uTex")
        uAvecTex = GLES20.glGetUniformLocation(prog, "uAvecTex"); uCam = GLES20.glGetUniformLocation(prog, "uCam")
        uSeuil = GLES20.glGetUniformLocation(prog, "uSeuil")
        uEauL = GLES20.glGetUniformLocation(prog, "uEau"); uTL = GLES20.glGetUniformLocation(prog, "uT")
        uVentL = GLES20.glGetUniformLocation(prog, "uVent"); uMulL = GLES20.glGetUniformLocation(prog, "uMul")
        uBrumeL = GLES20.glGetUniformLocation(prog, "uBrume"); uBrumePL = GLES20.glGetUniformLocation(prog, "uBrumeP"); uTeinteL = GLES20.glGetUniformLocation(prog, "uTeinteEau"); uChaudL = GLES20.glGetUniformLocation(prog, "uChaud")
        // ciel : un degrade plein ecran dessine avant le niveau
        progCiel = programme("""
            attribute vec2 aP; varying float vY;
            void main() { gl_Position = vec4(aP, 0.999, 1.0); vY = aP.y * 0.5 + 0.5; }""", """
            precision mediump float; varying float vY; uniform vec3 uHaut; uniform vec3 uBas;
            void main() { gl_FragColor = vec4(mix(uBas, uHaut, smoothstep(0.35, 1.0, vY)), 1.0); }""")
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
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, g.n * g.pas, g.sommets, GLES20.GL_STATIC_DRAW)
        }
        val gm = scene.mobiles?.groupes ?: emptyList()
        val nm = minOf(gm.size, vboM.size)
        if (nm > 0) GLES20.glGenBuffers(nm, vboM, 0)
        for (i in 0 until nm) {
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vboM[i])
            val g = gm[i]; g.sommets.position(0)
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, g.n * g.pas, g.sommets, GLES20.GL_STATIC_DRAW)
        }
        val an = scene.animes
        if (an != null) {
            vboAniUv = Array(an.modeles.size) { m -> IntArray(an.modeles[m].groupes.size).also { v ->
                if (v.isNotEmpty()) GLES20.glGenBuffers(v.size, v, 0)
                for ((i, g) in an.modeles[m].groupes.withIndex()) { GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, v[i]); g.uvc.position(0)
                    GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, g.n * 12, g.uvc, GLES20.GL_STATIC_DRAW) } } }
            vboAniPos = Array(an.modeles.size) { m -> IntArray(an.modeles[m].images).also { v ->
                if (v.isNotEmpty()) GLES20.glGenBuffers(v.size, v, 0)
                for ((i, pb) in an.modeles[m].pos.withIndex()) { GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, v[i]); pb.position(0)
                    GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, an.modeles[m].total * 12, pb, GLES20.GL_STATIC_DRAW) } } }
        }
        // textures
        val aniso = (GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: "").contains("texture_filter_anisotropic")
        val npot = (GLES20.glGetString(GLES20.GL_EXTENSIONS) ?: "").contains("texture_npot")
        // pack HD : "<niveau>_hd4.txt" ; les textures y sont 4x plus grandes que dans le jeu
        val hd = java.io.File(scene.dossier, scene.niveau + "_hd4.txt").exists()
        val fac = if (facteur == 3 && !npot) 2 else facteur
        textures.clear()
        // (build 356 : aussi les textures des clignements)
        val aCharger = (scene.groupes + (scene.mobiles?.groupes ?: emptyList()) + (scene.animes?.groupesTex ?: emptyList()))
            .flatMap { g -> listOf(g.texture to g.enveloppe) + (g.texSeq?.map { it to g.enveloppe } ?: emptyList()) }
        // (build 358 : les textures des seuls objets animes et du direct (personnages : des centaines par ile) sont lues a
        //  la taille du jeu : en x4, elles remplissaient la memoire graphique et la vue restait parfois vide)
        val texDecor = HashSet<Int>().apply { for (g in scene.groupes + (scene.mobiles?.groupes ?: emptyList())) { add(g.texture); g.texSeq?.forEach { add(it) } } }
        for ((gTexture, gEnveloppe) in aCharger) {
            if (gTexture < 0 || textures.containsKey(gTexture)) continue
            val f = java.io.File(scene.dossier, scene.niveau + "_t" + String.format(java.util.Locale.ROOT, "%03d", gTexture) + ".png")
            val petite = hd && gTexture !in texDecor
            val bmp = (if (hd && (fac < 4 || petite)) {
                // x2 et x1 : lecture directe en 1/2 ou 1/4 ; x3 : reduction filtree des 3/4
                val o = android.graphics.BitmapFactory.Options().apply { inSampleSize = if (fac == 1 || petite) 4 else if (fac == 2) 2 else 1 }
                val b0 = android.graphics.BitmapFactory.decodeFile(f.path, o)
                if (b0 != null && fac == 3 && !petite) android.graphics.Bitmap.createScaledBitmap(b0, maxOf(1, b0.width * 3 / 4), maxOf(1, b0.height * 3 / 4), true).also { if (it !== b0) b0.recycle() } else b0
            } else android.graphics.BitmapFactory.decodeFile(f.path)) ?: continue
            val t = IntArray(1); GLES20.glGenTextures(1, t, 0)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t[0])
            GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bmp, 0); bmp.recycle()
            val pot = { v: Int -> v > 0 && (v and (v - 1)) == 0 }
            GLES20.glGenerateMipmap(GLES20.GL_TEXTURE_2D)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR_MIPMAP_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, if (gEnveloppe and 1 != 0) GLES20.GL_MIRRORED_REPEAT else GLES20.GL_REPEAT)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, if (gEnveloppe and 2 != 0) GLES20.GL_MIRRORED_REPEAT else GLES20.GL_REPEAT)
            if (aniso) GLES20.glTexParameterf(GLES20.GL_TEXTURE_2D, 0x84FE, 16f)   // (GL_TEXTURE_MAX_ANISOTROPY_EXT : nettete de biais)
            textures[gTexture] = t[0]
        }
        // R3V3 : les ombres precalculees (texture grise du jeu, une fois sur tout le circuit : pas de repetition)
        lumieres.clear()
        for (g in scene.groupes + (scene.mobiles?.groupes ?: emptyList())) {
            if (g.lumiere < 0 || lumieres.containsKey(g.lumiere)) continue
            val f = java.io.File(scene.dossier, scene.niveau + "_t" + String.format(java.util.Locale.ROOT, "%03d", g.lumiere) + ".png")
            val bmp = android.graphics.BitmapFactory.decodeFile(f.path) ?: continue
            val t = IntArray(1); GLES20.glGenTextures(1, t, 0)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t[0])
            GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bmp, 0); bmp.recycle()
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
            lumieres[g.lumiere] = t[0]
        }
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        GLES20.glClearColor(amb.brume[0], amb.brume[1], amb.brume[2], 1f)
        // objets : des pastilles (rondes ; cages : carre a barreaux) dessinees par-dessus le decor
        progO = programme("""
            uniform mat4 uMvp; uniform float uTaille; attribute vec3 aPos;
            void main() { gl_Position = uMvp * vec4(aPos, 1.0); gl_PointSize = uTaille; }""", """
            precision mediump float; uniform vec4 uCoul; uniform float uForme;
            void main() { vec2 q = gl_PointCoord * 2.0 - 1.0;
                if (uForme < 0.5) { float r = length(q); if (r > 1.0) discard;
                    gl_FragColor = vec4(r > 0.7 ? vec3(0.06) : uCoul.rgb * (1.2 - 0.4 * r), uCoul.a); }
                else if (uForme < 1.5) { vec2 a = abs(q); if (max(a.x, a.y) > 1.0) discard;
                    bool bord = max(a.x, a.y) > 0.78; bool barreau = fract((q.x + 1.0) * 2.5) < 0.3;
                    gl_FragColor = vec4(bord ? vec3(0.06) : (barreau ? uCoul.rgb * 0.5 : uCoul.rgb), uCoul.a); }
                else if (uForme < 2.5) { vec2 a = abs(q); if (max(a.x, a.y) > 1.0) discard;    // (boite a objets : arc-en-ciel, bord blanc)
                    float h = (q.x + q.y) * 0.25 + 0.5;
                    vec3 arc = clamp(abs(fract(h + vec3(0.0, 0.66, 0.33)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
                    gl_FragColor = vec4(max(a.x, a.y) > 0.8 ? vec3(1.0) : arc * 0.9 + 0.1, uCoul.a * 0.9); }
                else { vec2 a = abs(q); if (max(a.x, a.y) > 1.0) discard;                       // (depart : damier)
                    bool d = mod(floor((q.x + 1.0) * 3.0) + floor((q.y + 1.0) * 3.0), 2.0) < 0.5;
                    gl_FragColor = vec4(max(a.x, a.y) > 0.85 ? vec3(0.06) : (d ? vec3(1.0) : vec3(0.06)), uCoul.a); } }""")
        // gemme : modele 3D (x y z nx ny nz par sommet), reflet : la normale vue de la camera choisit le point de la texture
        progG = programme("""
            uniform mat4 uMvp; uniform mat4 uVue; uniform vec3 uPos; uniform float uAng; uniform float uEch;
            attribute vec3 aPos; attribute vec3 aN; varying vec2 vUv;
            void main() { float c = cos(uAng); float s = sin(uAng);
                vec3 p = vec3(aPos.x * c - aPos.y * s, aPos.x * s + aPos.y * c, aPos.z);
                vec3 n = vec3(aN.x * c - aN.y * s, aN.x * s + aN.y * c, aN.z);
                vec3 nv = normalize((uVue * vec4(n, 0.0)).xyz); vUv = nv.xy * 0.5 + 0.5;
                gl_Position = uMvp * vec4(p * uEch + uPos, 1.0); }""", """
            precision mediump float; uniform sampler2D uTex; uniform float uA; varying vec2 vUv;
            void main() { gl_FragColor = vec4(texture2D(uTex, vUv).rgb * 1.15, uA); }""")
        // images plates du jeu (lum rouge, icones des pouvoirs), toujours face a la camera
        progS = programme("""
            uniform mat4 uMvp; uniform float uTaille; attribute vec3 aPos;
            void main() { gl_Position = uMvp * vec4(aPos, 1.0); gl_PointSize = uTaille; }""", """
            precision mediump float; uniform sampler2D uTex; uniform float uA;
            void main() { vec4 t = texture2D(uTex, gl_PointCoord); gl_FragColor = vec4(t.rgb, t.a * uA); }""")
        gemmesC.clear()
        for ((t, mod, tx) in listOf(Triple('j', "r3_gemme.txt", "r3_reflet_or.png"), Triple('k', "r3_gemme_rouge.txt", "r3_reflet_rouge.png"),
                Triple('v', "r3_gemme_verte.txt", "r3_reflet_vert.png"))) {
            try {
                val f = java.io.File(scene.dossier, mod)
                if (!f.exists()) continue
                val v = f.readLines().mapNotNull { l -> l.trim().split(' ').mapNotNull { it.toFloatOrNull() }.takeIf { it.size == 6 } }
                if (v.isEmpty()) continue
                val te = chargerTex(tx); if (te == 0) continue
                val fb = ByteBuffer.allocateDirect(v.size * 24).order(ByteOrder.nativeOrder()).asFloatBuffer()
                for (q in v) for (x in q) fb.put(x)
                fb.position(0)
                // les grosses (bigpick, superpick…) deux fois plus grandes ; chaque gemme a sa phase (elles ne tournent pas ensemble)
                val pos = ArrayList<FloatArray>()
                for (o in scene.objets) if (o.type == t) {
                    val gros = Regex("big|super|gros", RegexOption.IGNORE_CASE).containsMatchIn(o.nom) && t != 'v'
                    pos.add(floatArrayOf(o.x, o.y, o.z + 1.0f, (o.x * 0.37f + o.y * 0.11f) % 6.28f, if (gros) 2f else 1f))
                }
                gemmesC[t] = Gemme(fb, v.size, te, pos)
            } catch (e: Throwable) { }
        }
        texRouge = chargerTex("r3_lum_rouge.png")
        progM = programme("""
            uniform mat4 uMvp; uniform vec3 uPos; uniform float uAng;
            attribute vec3 aPos; attribute vec2 aUv; attribute vec3 aN; varying vec2 vUv; varying float vL;
            void main() { float c = cos(uAng); float s = sin(uAng);
                vec3 p = vec3(aPos.x * c - aPos.y * s, aPos.x * s + aPos.y * c, aPos.z);
                vec3 n = normalize(vec3(aN.x * c - aN.y * s, aN.x * s + aN.y * c, aN.z));
                vL = 0.55 + 0.45 * max(0.0, dot(n, normalize(vec3(0.4, -0.5, 0.75))));
                vUv = aUv; gl_Position = uMvp * vec4(p + uPos, 1.0); }""", """
            precision mediump float; uniform sampler2D uTex; varying vec2 vUv; varying float vL;
            void main() { vec4 t = texture2D(uTex, vUv); if (t.a < 0.5) discard; gl_FragColor = vec4(t.rgb * vL * 1.1, 1.0); }""")
        cageMod = null; nCage = 0
        try {
            val f = java.io.File(scene.dossier, "r3_cage.txt")
            if (f.exists()) {
                val v = f.readLines().filter { !it.startsWith("t ") }.mapNotNull { l -> l.trim().split(' ').mapNotNull { it.toFloatOrNull() }.takeIf { it.size == 8 } }
                texCage = chargerTex("r3_cage.png", repete = true)
                if (v.isNotEmpty() && texCage != 0) {
                    val fb = ByteBuffer.allocateDirect(v.size * 32).order(ByteOrder.nativeOrder()).asFloatBuffer()
                    for (q in v) for (x in q) fb.put(x)
                    fb.position(0); cageMod = fb; nCage = v.size
                }
            }
        } catch (e: Throwable) { cageMod = null }
        raymanMod = if (scene.dossier.name == "cartes-r3") lireModele("r3_rayman_3d.txt") else null
        texRayman = if (raymanMod != null) chargerTex("r3_rayman.png", repete = true) else 0
        if (texRayman == 0) raymanMod = null
        animMod = if (scene.dossier.name == "cartes-r3") ModeleAnime.pour(scene.dossier) else null
        texAnim.clear(); bufEl.clear(); poseA = null; poseB = null; texNom.clear()
        boites.clear(); texBoite = chargerTex("r3_boite.png", repete = true)
        mkBoite = if (scene.dossier.name == "cartes-mk") lireMulti("mk_boite.txt") else null
        mkSieges.clear(); mkModeles.clear(); mkPeaux.clear()
        if (scene.dossier.name == "cartes-mk") try {
            java.io.File(scene.dossier, "mk_karts.txt").takeIf { it.exists() }?.forEachLine { l -> val p = l.trim().split(' ')
                if (p.size >= 4 && p[0] == "kart") p[1].toIntOrNull()?.let { mkSieges[it] = floatArrayOf(p[2].toFloat(), p[3].toFloat()) } }
        } catch (e: Throwable) { }
        // les effets de Mario Kart (fumee, etincelles, flammes, etoile) : des points doux, leur taille suit la distance
        progP = programme("""
            uniform mat4 uMvp; uniform float uEch; attribute vec3 aPos; attribute vec4 aCol; attribute float aT; varying vec4 vCol;
            void main() { gl_Position = uMvp * vec4(aPos, 1.0); gl_PointSize = clamp(aT * uEch / max(gl_Position.w, 0.05), 1.0, 200.0); vCol = aCol; }""", """
            precision mediump float; varying vec4 vCol;
            void main() { vec2 q = gl_PointCoord * 2.0 - 1.0; float r = dot(q, q); if (r > 1.0) discard;
                float a = 1.0 - r; gl_FragColor = vec4(vCol.rgb, vCol.a * a * a); }""")
        progK = programme("""
            uniform mat4 uMvp; uniform mat4 uModele;
            attribute vec3 aPos; attribute vec2 aUv; attribute vec3 aN; varying vec2 vUv; varying float vL;
            void main() { vec3 n = normalize(mat3(uModele) * aN);
                vL = 0.6 + 0.4 * abs(dot(n, normalize(vec3(0.4, -0.5, 0.75))));
                vUv = aUv; gl_Position = uMvp * (uModele * vec4(aPos, 1.0)); }""", """
            precision mediump float; uniform sampler2D uTex; uniform float uAlpha; uniform float uSeuil; varying vec2 vUv; varying float vL;
            void main() { vec4 t = texture2D(uTex, vUv); if (t.a < uSeuil) discard; gl_FragColor = vec4(t.rgb * vL * 1.1, t.a * uAlpha); }""")
        // (la boite tourne sur elle-meme (uAng) et bascule (uInc), comme dans le jeu ; uEch : elle repousse apres un choc)
        progMB = programme("""
            uniform mat4 uMvp; uniform vec3 uPos; uniform float uAng; uniform float uInc; uniform float uEch; uniform vec2 uDefil;
            attribute vec3 aPos; attribute vec2 aUv; attribute vec3 aN; varying vec2 vUv; varying float vL;
            void main() { float c = cos(uAng); float s = sin(uAng); float ci = cos(uInc); float si = sin(uInc);
                vec3 p0 = vec3(aPos.x, aPos.y * ci - aPos.z * si, aPos.y * si + aPos.z * ci);
                vec3 n0 = vec3(aN.x, aN.y * ci - aN.z * si, aN.y * si + aN.z * ci);
                vec3 p = vec3(p0.x * c - p0.y * s, p0.x * s + p0.y * c, p0.z) * uEch;
                vec3 n = normalize(vec3(n0.x * c - n0.y * s, n0.x * s + n0.y * c, n0.z));
                vL = 0.6 + 0.4 * max(0.0, dot(n, normalize(vec3(0.4, -0.5, 0.75))));
                vUv = aUv + uDefil; gl_Position = uMvp * vec4(p + uPos, 1.0); }""", """
            precision mediump float; uniform sampler2D uTex; uniform float uAlpha; uniform float uSeuil; varying vec2 vUv; varying float vL;
            void main() { vec4 t = texture2D(uTex, vUv); if (t.a < uSeuil) discard; gl_FragColor = vec4(t.rgb * vL * 1.15, t.a * uAlpha); }""")
        if (texBoite != 0) for (k in listOf("grappin", "helico", "poing", "roulade", "snipe")) lireModele("r3_boite_$k.txt")?.let { boites[k] = it }
        texPouvoir.clear()
        for (k in listOf("grappin", "helico", "poing", "roulade", "snipe")) chargerTex("r3_pouvoir_$k.png").let { if (it != 0) texPouvoir[k] = it }
        pouvoirBuf.clear()
        for ((k, l) in scene.objets.filter { it.type == 'p' }.groupBy { genrePouvoir(it.nom) }) {
            val fb = ByteBuffer.allocateDirect(l.size * 12).order(ByteOrder.nativeOrder()).asFloatBuffer()
            for (o in l) fb.put(o.x).put(o.y).put(o.z + 0.8f)
            fb.position(0); pouvoirBuf[k] = fb
        }
        reconstruireObjets()
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

    /** build 256 : listes des objets a dessiner, sans ceux deja ramasses (refaites quand scene.versionPris change) */
    private var versionVue = -1
    private fun reconstruireObjets() {
        versionVue = scene.versionPris
        val la = scene.objets.filter { !it.pris }
        objBuf.clear()
        for ((t, l) in la.groupBy { it.type }) {
            val fb = ByteBuffer.allocateDirect(l.size * 12).order(ByteOrder.nativeOrder()).asFloatBuffer()
            for (o in l) fb.put(o.x).put(o.y).put(o.z + (if (t == 'c' || t == 'f') 1.2f else 0.6f))
            fb.position(0); objBuf[t] = fb
        }
        pouvoirBuf.clear()
        for ((k, l) in la.filter { it.type == 'p' }.groupBy { genrePouvoir(it.nom) }) {
            val fb = ByteBuffer.allocateDirect(l.size * 12).order(ByteOrder.nativeOrder()).asFloatBuffer()
            for (o in l) fb.put(o.x).put(o.y).put(o.z + 0.8f)
            fb.position(0); pouvoirBuf[k] = fb
        }
        mkBoitesPos.clear()
        for (o in la) if (o.type == 'i') mkBoitesPos.add(floatArrayOf(o.x, o.y, o.z + 0.9f, (o.x * 0.23f + o.y * 0.41f) % 6.28f))
        boitesPos.clear()
        for (o in la) if (o.type == 'p') boitesPos.getOrPut(genrePouvoir(o.nom).ifEmpty { "grappin" }) { ArrayList() }.add(floatArrayOf(o.x, o.y, o.z, (o.x * 0.31f + o.y * 0.13f) % 6.28f))
        cagesPos.clear()
        for (o in la) if (o.type == 'c') cagesPos.add(floatArrayOf(o.x, o.y, o.z, (o.x * 0.29f + o.y * 0.17f) % 6.28f))
        for ((t, gm) in gemmesC) {
            gm.pos.clear()
            for (o in la) if (o.type == t) {
                val gros = Regex("big|super|gros", RegexOption.IGNORE_CASE).containsMatchIn(o.nom) && t != 'v'
                gm.pos.add(floatArrayOf(o.x, o.y, o.z + 1.0f, (o.x * 0.37f + o.y * 0.11f) % 6.28f, if (gros) 2f else 1f))
            }
        }
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
        android.opengl.Matrix.perspectiveM(proj, 0, 60f, w.toFloat() / h.coerceAtLeast(1), 0.5f, 9000f)   // (build 269 : grands circuits)
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
    /** build 338 : Link de Wind Waker (pack : ww_link.ani) : a la position lue, tourne selon son cap, l'animation choisie
     *  d'apres sa vitesse ; rend false sans pack (le point rouge reste) */
    private var linkAni: LinkAnime? = null; private var linkCharge = false
    private val linkTex = HashMap<Int, Int>(); private var linkVbo = IntArray(0)
    private val mLink = FloatArray(16); private val mvpLink = FloatArray(16)
    private var animLink = ""; private var tAnim = 0L
    private var peauLink: PeauLink? = null; private var peauCharge = false
    private fun dessinerLinkWW(mvp: FloatArray, c0: FloatArray): Boolean {
        val l = etat.linkWW ?: return false
        if (!linkCharge) {
            linkCharge = true
            linkAni = LinkAnime.lire(java.io.File(scene.dossier, "ww_link.ani"))
            val la = linkAni ?: return false
            for (g in la.groupes) if (g.texture >= 0 && !linkTex.containsKey(g.texture))
                linkTex[g.texture] = chargerTex("ww_link_t" + String.format(java.util.Locale.ROOT, "%03d", g.texture) + ".png")
            linkVbo = IntArray(la.groupes.size * 2); GLES20.glGenBuffers(linkVbo.size, linkVbo, 0)
            for ((i, g) in la.groupes.withIndex()) {
                GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, linkVbo[2 * i]); g.uvc.position(0)
                GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, g.n * 12, g.uvc, GLES20.GL_STATIC_DRAW)
            }
        }
        val la = linkAni ?: return false
        // build 340 : la vraie pose lue dans le jeu (42 matrices) : chaque sommet suit ses articulations (pack : ww_link.peau)
        val pw = etat.poseWW?.takeIf { System.nanoTime() - etat.tPoseWW < 1_000_000_000L && it.size >= 42 * 12 }
        if (pw != null && !peauCharge) { peauCharge = true; peauLink = PeauLink.lire(java.io.File(scene.dossier, "ww_link.peau"), la) }
        val pl = peauLink
        if (pw != null && pl != null) {
            GLES20.glUseProgram(prog)
            GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0)                   // (positions deja dans le monde)
            GLES20.glUniform1f(uEauL, 0f); GLES20.glUniform1f(uChaudL, 0f); GLES20.glUniform1f(uLmK, 0f); GLES20.glUniform2f(uDefil, 0f, 0f)
            GLES20.glUniform4f(uMulL, 1f, 1f, 1f, 1f); GLES20.glUniform1f(uVentL, 0f)
            GLES20.glUniform1f(uSeuil, 0.5f); GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glEnable(GLES20.GL_DEPTH_TEST)
            GLES20.glEnableVertexAttribArray(aPos); GLES20.glEnableVertexAttribArray(aUv); GLES20.glEnableVertexAttribArray(aCol)
            if (aUvL >= 0) { GLES20.glDisableVertexAttribArray(aUvL); GLES20.glVertexAttrib2f(aUvL, 0f, 0f) }
            for ((i, g) in la.groupes.withIndex()) {
                val t = linkTex[g.texture]
                GLES20.glUniform1f(uAvecTex, if (t != null && t != 0) 1f else 0f)
                if (t != null && t != 0) { GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t) }
                GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, linkVbo[2 * i])
                GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, 12, 0)
                GLES20.glVertexAttribPointer(aCol, 4, GLES20.GL_UNSIGNED_BYTE, true, 12, 8)
                val pb = pl.poser(i, pw)
                GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, linkVbo[2 * i + 1])
                GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, g.n * 12, pb, GLES20.GL_STREAM_DRAW)
                GLES20.glVertexAttribPointer(aPos, 3, GLES20.GL_FLOAT, false, 12, 0)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, g.n)
            }
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
            GLES20.glDisableVertexAttribArray(aUv); GLES20.glDisableVertexAttribArray(aCol)
            GLES20.glUseProgram(progL)
            return true
        }
        // l'animation : nage (sous l'eau), sinon d'apres la vitesse
        val v = l[1]
        val nom = if (l.size > 2 && l[2] > 0.5f) (if (v > 2f) "nage" else "nage_attente") else if (v < 1f) "attente" else if (v < 12f) "marche" else "course"
        val an = la.anims[nom] ?: la.anims["attente"] ?: return false
        if (nom != animLink) { animLink = nom; tAnim = System.nanoTime() }
        val img = (((System.nanoTime() - tAnim) / 1e9 * an.ips).toLong() % an.images).toInt()
        // matrice : position, puis rotation du cap autour de la verticale (le modele regarde vers -y)
        android.opengl.Matrix.setRotateM(mLink, 0, Math.toDegrees(l[0].toDouble()).toFloat(), 0f, 0f, 1f)
        mLink[12] = c0[0]; mLink[13] = c0[1]; mLink[14] = c0[2]
        android.opengl.Matrix.multiplyMM(mvpLink, 0, mvp, 0, mLink, 0)
        GLES20.glUseProgram(prog)
        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvpLink, 0)
        GLES20.glUniform1f(uEauL, 0f); GLES20.glUniform1f(uChaudL, 0f); GLES20.glUniform1f(uLmK, 0f); GLES20.glUniform2f(uDefil, 0f, 0f)
        GLES20.glUniform4f(uMulL, 1f, 1f, 1f, 1f); GLES20.glUniform1f(uVentL, 0f)
        GLES20.glUniform1f(uSeuil, 0.5f); GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        GLES20.glEnableVertexAttribArray(aPos); GLES20.glEnableVertexAttribArray(aUv); GLES20.glEnableVertexAttribArray(aCol)
        if (aUvL >= 0) { GLES20.glDisableVertexAttribArray(aUvL); GLES20.glVertexAttrib2f(aUvL, 0f, 0f) }
        for ((i, g) in la.groupes.withIndex()) {
            val t = linkTex[g.texture]
            GLES20.glUniform1f(uAvecTex, if (t != null && t != 0) 1f else 0f)
            if (t != null && t != 0) { GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t) }
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, linkVbo[2 * i])
            GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, 12, 0)
            GLES20.glVertexAttribPointer(aCol, 4, GLES20.GL_UNSIGNED_BYTE, true, 12, 8)
            val pb = an.pos[img][i]; pb.position(0)
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, linkVbo[2 * i + 1])
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, g.n * 12, pb, GLES20.GL_STREAM_DRAW)
            GLES20.glVertexAttribPointer(aPos, 3, GLES20.GL_FLOAT, false, 12, 0)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, g.n)
        }
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
        GLES20.glDisableVertexAttribArray(aUv); GLES20.glDisableVertexAttribArray(aCol)
        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0)
        GLES20.glUseProgram(progL)
        return true
    }

    /** un groupe du decor (ou d'un objet qui bouge) : textures, ombres, defilement, sommets (build 332 : sorti de la boucle) */
    /** build 356 : texture du moment (clignements) et couleur animee d'un groupe ; phase : decalage (0 a 1) de la copie */
    private fun texDuMoment(g: SceneR3.Groupe, tSec: Double, phase: Float = 0f): Int {
        val sq = g.texSeq ?: return g.texture
        return sq[((tSec * g.texIps + phase * sq.size).toLong() % sq.size).toInt()]
    }
    private fun couleurDuMoment(g: SceneR3.Groupe, tSec: Double, phase: Float = 0f) {
        val sq = g.coulSeq
        if (sq == null) { GLES20.glUniform4f(uMulL, 1f, 1f, 1f, 1f); return }
        val n = sq.size / 4; val i = ((tSec * g.coulIps + phase * n).toLong() % n).toInt() * 4
        GLES20.glUniform4f(uMulL, sq[i], sq[i + 1], sq[i + 2], sq[i + 3])
    }
    private fun dessinerGroupe(g: SceneR3.Groupe, buf: Int, tSec: Double) {
        GLES20.glUniform1f(uEauL, g.eau.toFloat()); GLES20.glUniform1f(uChaudL, if (g.chaud) 1f else 0f)
        GLES20.glUniform1f(uVentL, if (g.vent) 1f else 0f); couleurDuMoment(g, tSec)
        val t = textures[texDuMoment(g, tSec)]
        GLES20.glUniform1f(uAvecTex, if (t != null) 1f else 0f)
        if (t != null) { GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t) }
        // R3V3 : ombres precalculees (2e texture, ses propres coordonnees) et defilement de la texture
        val lt = if (g.lumiere >= 0) lumieres[g.lumiere] else null
        GLES20.glUniform1f(uLmK, if (lt != null) g.lumiereK else 0f)
        if (lt != null) { GLES20.glActiveTexture(GLES20.GL_TEXTURE1); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, lt); GLES20.glActiveTexture(GLES20.GL_TEXTURE0) }
        val du = g.defil[0]; val dv = g.defil[1]
        GLES20.glUniform2f(uDefil, if (du != 0f) ((tSec * du) % 1.0).toFloat() else 0f, if (dv != 0f) ((tSec * dv) % 1.0).toFloat() else 0f)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, buf)
        GLES20.glVertexAttribPointer(aPos, 3, GLES20.GL_FLOAT, false, g.pas, 0)
        GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, g.pas, 12)
        GLES20.glVertexAttribPointer(aCol, 4, GLES20.GL_UNSIGNED_BYTE, true, g.pas, 20)
        if (aUvL >= 0) { if (g.pas >= 32) { GLES20.glEnableVertexAttribArray(aUvL); GLES20.glVertexAttribPointer(aUvL, 2, GLES20.GL_FLOAT, false, g.pas, 24) }
            else { GLES20.glDisableVertexAttribArray(aUvL); GLES20.glVertexAttrib2f(aUvL, 0f, 0f) } }
        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, g.n)
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
            GLES20.glUniform1f(GLES20.glGetUniformLocation(progFin, "uBloom"), amb.bloom)
            passePost(progFin, a, 1f / a.w, 1f / a.h)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        } else dessinerScene()
        etat.captureCb?.let { cb -> etat.captureCb = null; cb(try { lireEcran() } catch (e: Throwable) { null }) }
    }
    /** l'image affichee (journal de bug) : glReadPixels, retournee (OpenGL lit de bas en haut) */
    private fun lireEcran(): android.graphics.Bitmap {
        val w = ecranW; val h = ecranH
        val b = ByteBuffer.allocateDirect(w * h * 4).order(ByteOrder.nativeOrder())
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
        GLES20.glReadPixels(0, 0, w, h, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, b)
        val px = IntArray(w * h); b.position(0)
        for (y in 0 until h) for (x in 0 until w) {
            val r = b.get().toInt() and 255; val g = b.get().toInt() and 255; val bl = b.get().toInt() and 255; b.get()
            px[(h - 1 - y) * w + x] = (0xFF shl 24) or (r shl 16) or (g shl 8) or bl
        }
        return android.graphics.Bitmap.createBitmap(px, w, h, android.graphics.Bitmap.Config.ARGB_8888)
    }
    /** build 249 : gemmes 3D qui tournent, lums rouges et pouvoirs avec les images du jeu. Comme les pastilles :
     *  d'abord estompes a travers le decor, puis nets la ou on les voit. */
    private fun dessinerVraisObjets(voir: String) {
        val t = ((System.nanoTime() - t0) / 1e9).toFloat()
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        for (passe in 0..1) {
            if (passe == 0) { GLES20.glDisable(GLES20.GL_DEPTH_TEST); GLES20.glDepthMask(false) } else { GLES20.glEnable(GLES20.GL_DEPTH_TEST) }
            val a = if (passe == 0) 0.3f else 1f
            for ((gt, gm) in gemmesC) {
                if (gt !in voir || gm.pos.isEmpty()) continue
                val g = gm.mod
                if (passe == 1) GLES20.glDepthMask(true)
                GLES20.glUseProgram(progG)
                GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progG, "uMvp"), 1, false, mvp, 0)
                GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progG, "uVue"), 1, false, vue, 0)
                GLES20.glUniform1f(GLES20.glGetUniformLocation(progG, "uA"), a)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, gm.tex); GLES20.glUniform1i(GLES20.glGetUniformLocation(progG, "uTex"), 0)
                val gp = GLES20.glGetAttribLocation(progG, "aPos"); val gn = GLES20.glGetAttribLocation(progG, "aN")
                g.position(0); GLES20.glVertexAttribPointer(gp, 3, GLES20.GL_FLOAT, false, 24, g)
                g.position(3); GLES20.glVertexAttribPointer(gn, 3, GLES20.GL_FLOAT, false, 24, g); g.position(0)
                GLES20.glEnableVertexAttribArray(gp); GLES20.glEnableVertexAttribArray(gn)
                val uP = GLES20.glGetUniformLocation(progG, "uPos"); val uA = GLES20.glGetUniformLocation(progG, "uAng")
                val uE = GLES20.glGetUniformLocation(progG, "uEch")
                for (q in gm.pos) { GLES20.glUniform3f(uP, q[0], q[1], q[2] + 0.15f * Math.sin((t * 2f + q[3]).toDouble()).toFloat())
                    GLES20.glUniform1f(uA, t * 1.6f + q[3]); GLES20.glUniform1f(uE, q[4]); GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, gm.n) }
                GLES20.glDisableVertexAttribArray(gp); GLES20.glDisableVertexAttribArray(gn)
                GLES20.glDepthMask(false)
            }
            val cm = cageMod
            if (passe == 1 && 'c' in voir && cm != null && cagesPos.isNotEmpty()) {
                GLES20.glDepthMask(true); GLES20.glDisable(GLES20.GL_BLEND)
                GLES20.glUseProgram(progM)
                GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progM, "uMvp"), 1, false, mvp, 0)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texCage); GLES20.glUniform1i(GLES20.glGetUniformLocation(progM, "uTex"), 0)
                val mp = GLES20.glGetAttribLocation(progM, "aPos"); val mu = GLES20.glGetAttribLocation(progM, "aUv"); val mn = GLES20.glGetAttribLocation(progM, "aN")
                cm.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, cm)
                cm.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, cm)
                cm.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, cm); cm.position(0)
                GLES20.glEnableVertexAttribArray(mp); GLES20.glEnableVertexAttribArray(mu); GLES20.glEnableVertexAttribArray(mn)
                val uP = GLES20.glGetUniformLocation(progM, "uPos"); val uA = GLES20.glGetUniformLocation(progM, "uAng")
                for (q in cagesPos) {     // (le ballon se balance doucement)
                    GLES20.glUniform3f(uP, q[0], q[1], q[2] + 0.12f * Math.sin((t * 1.3f + q[3]).toDouble()).toFloat())
                    GLES20.glUniform1f(uA, 0.12f * Math.sin((t * 0.7f + q[3]).toDouble()).toFloat() + q[3])
                    GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, nCage)
                }
                GLES20.glDisableVertexAttribArray(mp); GLES20.glDisableVertexAttribArray(mu); GLES20.glDisableVertexAttribArray(mn)
                GLES20.glEnable(GLES20.GL_BLEND); GLES20.glDepthMask(false)
            }
            val mb = mkBoite
            if (passe == 1 && 'i' in voir && mb != null && mkBoitesPos.isNotEmpty()) {     // build 288 : boites a objets de Mario Kart
                GLES20.glUseProgram(progMB)
                GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progMB, "uMvp"), 1, false, mvp, 0)
                GLES20.glUniform1i(GLES20.glGetUniformLocation(progMB, "uTex"), 0)
                val mp = GLES20.glGetAttribLocation(progMB, "aPos"); val mu = GLES20.glGetAttribLocation(progMB, "aUv"); val mn = GLES20.glGetAttribLocation(progMB, "aN")
                val uP = GLES20.glGetUniformLocation(progMB, "uPos"); val uA = GLES20.glGetUniformLocation(progMB, "uAng")
                val uD = GLES20.glGetUniformLocation(progMB, "uDefil"); val uAl = GLES20.glGetUniformLocation(progMB, "uAlpha"); val uS = GLES20.glGetUniformLocation(progMB, "uSeuil")
                val uI = GLES20.glGetUniformLocation(progMB, "uInc"); val uE = GLES20.glGetUniformLocation(progMB, "uEch")
                // (en course : les boites lues dans le jeu, cachees quand un kart les casse, qui repoussent ; sinon celles du circuit)
                val vives = JeuMKDD.boites()
                if (vives.isNotEmpty()) for (bx in vives) {
                    val av = boiteEch.put(bx.id, bx.ech)
                    if (av != null && av > 0.6f && bx.ech < 0.3f) eclats.add(floatArrayOf(bx.x * 0.01f, -bx.z * 0.01f, bx.y * 0.01f + 0.9f))
                }
                GLES20.glEnableVertexAttribArray(mp); GLES20.glEnableVertexAttribArray(mu); GLES20.glEnableVertexAttribArray(mn)
                for (m in mb) {
                    when (m.mode) {
                        2 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.5f); GLES20.glUniform2f(uD, 0f, 0f) }
                        0 -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA); GLES20.glDepthMask(false)
                               GLES20.glUniform1f(uAl, 0.55f); GLES20.glUniform1f(uS, 0f); GLES20.glUniform2f(uD, 0f, t * 0.35f) }      // (arc-en-ciel qui defile, comme dans le jeu)
                        else -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE); GLES20.glDepthMask(false)
                               GLES20.glUniform1f(uAl, 0.8f); GLES20.glUniform1f(uS, 0f); GLES20.glUniform2f(uD, 0f, 0f) }
                    }
                    GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, m.tex)
                    val b = m.mod
                    b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
                    b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
                    b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
                    if (vives.isEmpty()) for (q in mkBoitesPos) {
                        GLES20.glUniform3f(uP, q[0], q[1], q[2]); GLES20.glUniform1f(uA, if (m.mode == 2) 0f else t * 1.2f + q[3])
                        GLES20.glUniform1f(uI, if (m.mode == 2) 0f else 0.5f * Math.sin((t * 0.9f + q[3]).toDouble()).toFloat()); GLES20.glUniform1f(uE, 1f)
                        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
                    }
                    else for (bx in vives) {
                        if (bx.ech < 0.02f) continue                                   // (cassee : rien tant qu'elle n'a pas repousse)
                        val ph = ((bx.id / 0x240L) % 97L).toFloat() * 0.37f
                        GLES20.glUniform3f(uP, bx.x * 0.01f, -bx.z * 0.01f, bx.y * 0.01f + 0.9f)
                        val ang = if (m.mode == 2) 0f else t * 1.2f + ph; val inc = if (m.mode == 2) 0f else 0.5f * Math.sin((t * 0.9f + ph).toDouble()).toFloat()
                        GLES20.glUniform1f(uA, ang); GLES20.glUniform1f(uI, inc); GLES20.glUniform1f(uE, bx.ech)
                        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
                        if (bx.double && m.mode != 2) {                                // (double boite : une seconde boite dedans, a contre-sens)
                            GLES20.glUniform1f(uA, -ang * 1.3f); GLES20.glUniform1f(uI, -inc); GLES20.glUniform1f(uE, bx.ech * 0.62f)
                            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
                        }
                    }
                }
                GLES20.glDisableVertexAttribArray(mp); GLES20.glDisableVertexAttribArray(mu); GLES20.glDisableVertexAttribArray(mn)
                GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA); GLES20.glDepthMask(false)
            }
            if (passe == 1 && 'p' in voir && boites.isNotEmpty() && boitesPos.isNotEmpty()) {
                GLES20.glDepthMask(true); GLES20.glDisable(GLES20.GL_BLEND)
                GLES20.glUseProgram(progM)
                GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progM, "uMvp"), 1, false, mvp, 0)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texBoite); GLES20.glUniform1i(GLES20.glGetUniformLocation(progM, "uTex"), 0)
                val mp = GLES20.glGetAttribLocation(progM, "aPos"); val mu = GLES20.glGetAttribLocation(progM, "aUv"); val mn = GLES20.glGetAttribLocation(progM, "aN")
                val uP = GLES20.glGetUniformLocation(progM, "uPos"); val uA = GLES20.glGetUniformLocation(progM, "uAng")
                GLES20.glEnableVertexAttribArray(mp); GLES20.glEnableVertexAttribArray(mu); GLES20.glEnableVertexAttribArray(mn)
                for ((k, l) in boitesPos) {
                    val md = boites[k] ?: boites.values.first(); val b = md.mod
                    b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
                    b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
                    b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
                    for (q in l) {        // (la boite tourne lentement sur elle-meme)
                        GLES20.glUniform3f(uP, q[0], q[1], q[2]); GLES20.glUniform1f(uA, t * 0.9f + q[3])
                        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, md.n)
                    }
                }
                GLES20.glDisableVertexAttribArray(mp); GLES20.glDisableVertexAttribArray(mu); GLES20.glDisableVertexAttribArray(mn)
                GLES20.glEnable(GLES20.GL_BLEND); GLES20.glDepthMask(false)
            }
            GLES20.glUseProgram(progS)
            GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progS, "uMvp"), 1, false, mvp, 0)
            GLES20.glUniform1i(GLES20.glGetUniformLocation(progS, "uTex"), 0)
            GLES20.glUniform1f(GLES20.glGetUniformLocation(progS, "uA"), a)
            val sp = GLES20.glGetAttribLocation(progS, "aPos"); val uT = GLES20.glGetUniformLocation(progS, "uTaille")
            GLES20.glEnableVertexAttribArray(sp)
            val rb = objBuf['r']
            if ('r' in voir && rb != null && texRouge != 0) {
                GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE)            // (lumiere : additif)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texRouge); GLES20.glUniform1f(uT, 34f * echelle)
                GLES20.glVertexAttribPointer(sp, 3, GLES20.GL_FLOAT, false, 12, rb); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, rb.capacity() / 3)
                GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA)
            }
            if ('p' in voir && !(passe == 1 && boites.isNotEmpty())) for ((k, fb) in pouvoirBuf) {
                val tx = texPouvoir[k] ?: texPouvoir.values.firstOrNull() ?: continue
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tx); GLES20.glUniform1f(uT, 30f * echelle)
                GLES20.glVertexAttribPointer(sp, 3, GLES20.GL_FLOAT, false, 12, fb); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, fb.capacity() / 3)
            }
            GLES20.glDisableVertexAttribArray(sp)
        }
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
    }

    /** build 296 : les karts de Mario Kart en 3D, chacun avec son pilote et son passager, tournes comme dans le jeu.
     *  Le tien est place a la position lissee (fluide), les autres a leur derniere position lue. */
    private val mModele = FloatArray(16)
    private val ancres = HashMap<Int, FloatArray>()
    private var journalTour = 0
    // ---------------------------------------------------------------- kart en pieces (roues en direct)
    private val rotRoue = HashMap<Int, Float>(); private val rayonRoue = HashMap<String, Float>()
    private val posKart = HashMap<Int, FloatArray>(); private val vitKart = HashMap<Int, Float>()
    private var dtKarts = 0f; private var karts_t = 0L
    /** rayon d'une roue (unites de l'appli) : distance maximale a son axe (x) */
    private fun rayon(mod: List<Morceau>): Float { var r = 0f
        for (m in mod) { val b = m.mod; for (i in 0 until m.n) { val y = b.get(i * 8 + 1); val z = b.get(i * 8 + 2); r = maxOf(r, Math.sqrt((y * y + z * z).toDouble()).toFloat()) } }
        return if (r > 0.01f) r else 0.3f }
    /** vitesse d'un kart vers l'avant (unites de l'appli par seconde), d'apres sa position dessinee d'une image a l'autre */
    private fun vitesseKart(n: Int, x: Float, y: Float, z: Float, ra: FloatArray): Float {
        val av = posKart[n]; posKart[n] = floatArrayOf(x, y, z)
        if (av == null || dtKarts <= 0f) return vitKart[n] ?: 0f
        val fn = Math.sqrt((ra[2] * ra[2] + ra[5] * ra[5] + ra[8] * ra[8]).toDouble()).toFloat().coerceAtLeast(1e-4f)
        val vv = ((x - av[0]) * ra[2] + (y - av[1]) * ra[5] + (z - av[2]) * ra[8]) / fn / dtKarts
        val v = (vitKart[n] ?: 0f) * 0.75f + vv.coerceIn(-80f, 80f) * 0.25f; vitKart[n] = v; return v
    }
    /** mModele = kart dessine (ra, t) x piece i (relative, unites du jeu) ; roue : sa rotation remplacee par th (radians) */
    private fun posePiece(ra: FloatArray, tx: Float, ty: Float, tz: Float, pc: FloatArray, i: Int, th: Float) {
        val o = i * 12
        val c0 = floatArrayOf(pc[o], pc[o + 4], pc[o + 8]); var c1 = floatArrayOf(pc[o + 1], pc[o + 5], pc[o + 9]); var c2 = floatArrayOf(pc[o + 2], pc[o + 6], pc[o + 10])
        if (i > 0) {
            // (la rotation de la roue lue dans le jeu est enlevee : c2 a plat, c1 vers le haut ; puis celle de l'appli, reguliere)
            val f = Math.atan2(c2[1].toDouble(), c1[1].toDouble()); val cf = Math.cos(f).toFloat(); val sf = Math.sin(f).toFloat()
            var n1 = FloatArray(3) { cf * c1[it] + sf * c2[it] }; var n2 = FloatArray(3) { -sf * c1[it] + cf * c2[it] }
            if (n1[1] < 0f) { n1 = FloatArray(3) { -n1[it] }; n2 = FloatArray(3) { -n2[it] } }
            val t = if (n2[2] < 0f) -th else th; val ct = Math.cos(t.toDouble()).toFloat(); val st = Math.sin(t.toDouble()).toFloat()
            c1 = FloatArray(3) { ct * n1[it] + st * n2[it] }; c2 = FloatArray(3) { -st * n1[it] + ct * n2[it] }
        }
        val cs = arrayOf(c0, c1, c2)
        for (c in 0..2) { for (r in 0..2) mModele[c * 4 + r] = ra[r * 3] * cs[c][0] + ra[r * 3 + 1] * cs[c][1] + ra[r * 3 + 2] * cs[c][2]; mModele[c * 4 + 3] = 0f }
        val px = pc[o + 3] * 0.01f; val py = pc[o + 7] * 0.01f; val pz = pc[o + 11] * 0.01f
        mModele[12] = tx + ra[0] * px + ra[1] * py + ra[2] * pz; mModele[13] = ty + ra[3] * px + ra[4] * py + ra[5] * pz
        mModele[14] = tz + ra[6] * px + ra[7] * py + ra[8] * pz; mModele[15] = 1f
    }
    private fun morceauK(m: Morceau, uAl: Int, uS: Int, mp: Int, mu: Int, mn: Int) {
        when (m.mode) {
            0 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.02f) }
            1 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.5f) }
            2 -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA); GLES20.glDepthMask(false); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.02f) }
            else -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE); GLES20.glDepthMask(false); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.02f) }
        }
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, m.tex)
        val b = m.mod
        b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
        b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
        b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
    }
    private val feux = ArrayList<FloatArray>()
    private val boiteEch = HashMap<Long, Float>()         // (derniere echelle vue de chaque boite : pour voir quand elle casse)
    private val eclats = ArrayList<FloatArray>()          // (boites qui viennent de casser : eclats a lancer)            // (boules de feu de la course : position dans l'appli)       // (repere de chaque kart tel qu'il est dessine : ra puis position)
    private fun dessinerKarts(mvp: FloatArray, c0: FloatArray): Boolean {
        val ks = etat.karts
        if (ks.isEmpty() || mkSieges.isEmpty() || progK == 0) {     // (sans le pack des karts : les objets en points)
            ancres.clear(); feux.clear()
            objetsPoints.clear(); for ((g, p) in JeuMKDD.objets()) objetsPoints.add(floatArrayOf(p[0] * 0.01f, -p[2] * 0.01f, p[1] * 0.01f, g.toFloat()))
            return false
        }
        GLES20.glUseProgram(progK)
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progK, "uMvp"), 1, false, mvp, 0)
        GLES20.glUniform1i(GLES20.glGetUniformLocation(progK, "uTex"), 0); GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        val uM = GLES20.glGetUniformLocation(progK, "uModele"); val uAl = GLES20.glGetUniformLocation(progK, "uAlpha"); val uS = GLES20.glGetUniformLocation(progK, "uSeuil")
        val mp = GLES20.glGetAttribLocation(progK, "aPos"); val mu = GLES20.glGetAttribLocation(progK, "aUv"); val mn = GLES20.glGetAttribLocation(progK, "aN")
        GLES20.glEnableVertexAttribArray(mp); GLES20.glEnableVertexAttribArray(mu); GLES20.glEnableVertexAttribArray(mn)
        var fait = false; ancres.clear(); journalTour++
        if (JeuMKDD.direct()) JeuMKDD.rejouer()                   // (le direct rejoue au rythme du jeu, juste avant ce dessin)
        run { val now = System.nanoTime(); dtKarts = if (karts_t == 0L) 0f else ((now - karts_t) / 1e9f).coerceIn(0f, 0.1f); karts_t = now }
        for (k in ks) {
            // (le tien : rotation lue en direct ; les autres aussi, lus a part et glisses entre deux lectures)
            val g = (if (k.joueur) JeuMKDD.matJoueurAff ?: JeuMKDD.matJoueur else if (k.n > 0) JeuMKDD.matKart(k.n) else null) ?: k.mat; if (g.size < 12) continue
            val siege = mkSieges[k.kart] ?: continue
            // rotation du jeu (lignes) -> repere de l'appli (x, -z, y) : lignes 0, -2, 1
            val ra = floatArrayOf(g[0], g[1], g[2], -g[8], -g[9], -g[10], g[4], g[5], g[6])
            val tx: Float; val ty: Float; val tz: Float
            val gj = if (k.joueur) JeuMKDD.matJoueur else null                    // (le tien : position lue en direct + ecart de la carrosserie)
            if (JeuMKDD.direct()) { tx = g[3] * 0.01f; ty = -g[11] * 0.01f; tz = g[7] * 0.01f }   // (rejeu au rythme du jeu : la vraie place, deja fluide)
            else if (k.joueur && etat.direct && gj != null) { tx = c0[0] + (g[3] - gj[3]) * 0.01f; ty = c0[1] - (g[11] - gj[11]) * 0.01f; tz = c0[2] + (g[7] - gj[7]) * 0.01f }
            else if (k.joueur && etat.direct) { tx = c0[0]; ty = c0[1]; tz = c0[2] } else { tx = g[3] * 0.01f; ty = -g[11] * 0.01f; tz = g[7] * 0.01f }
            ancres[k.n] = floatArrayOf(ra[0], ra[1], ra[2], ra[3], ra[4], ra[5], ra[6], ra[7], ra[8], tx, ty, tz)
            if (k.joueur || journalTour % 20 == 0) Journal3D.note("K${k.n} kart=${k.kart} pilote=${k.pilote} passager=${k.passager} echange=${k.echange} pos=${Journal3D.v(tx, ty, tz)} g=${Journal3D.m(g)}")
            val parts = listOf("mk_kart_${k.kart}.txt" to 0f, "mk_pilote_${k.pilote}.txt" to 1f, "mk_passager_${k.passager}.txt" to 1f)
            for ((rangPart, ps) in parts.withIndex()) { val (nom, s) = ps
                // personnage anime (peau + articulations lues en direct) : positions deja relatives au kart, sans le siege
                if (rangPart > 0 && k.n >= 0) {
                    val cid = if (rangPart == 1) k.pilote else k.passager
                    // rang du squelette : 1 = personnage « pilote » de l'equipe, 0 = « passager » (echanges : l'inverse)
                    val rg = if ((rangPart == 1) != k.echange) 1 else 0
                    val pz = JeuMKDD.pose(k.n, rg); val pe = if (pz != null && cid > 0) lirePeau(cid) else null
                    if (k.joueur) Journal3D.note("S${k.n}.$rg perso=$cid pose=${pz?.size ?: 0} peau=${pe != null} j0=${Journal3D.m(pz, 0)}")
                    if (pz != null && pe != null) {
                        for (c in 0..2) { mModele[c * 4] = ra[c]; mModele[c * 4 + 1] = ra[3 + c]; mModele[c * 4 + 2] = ra[6 + c]; mModele[c * 4 + 3] = 0f }
                        mModele[12] = tx; mModele[13] = ty; mModele[14] = tz; mModele[15] = 1f
                        GLES20.glUniformMatrix4fv(uM, 1, false, mModele, 0)
                        for (m in pe) {
                            poser(m, pz)
                            if (m.mode >= 2) { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, if (m.mode == 2) GLES20.GL_ONE_MINUS_SRC_ALPHA else GLES20.GL_ONE); GLES20.glDepthMask(false) }
                            else { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true) }
                            GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, if (m.mode == 1) 0.5f else 0.02f)
                            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, m.tex)
                            val b = m.buf
                            b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
                            b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
                            b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
                            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
                        }
                        fait = true; continue
                    }
                }
                // le kart en pieces (pack mkdd-roues : carrosserie et 4 roues) placees comme dans le jeu : les roues braquent,
                // tournent avec la vitesse, la carrosserie se penche sur sa suspension
                if (rangPart == 0 && k.n >= 0) {
                    val pc = JeuMKDD.pieces(k.n); val corps = if (pc != null) modeleMK("mk_corps_${k.kart}.txt") else null
                    if (k.joueur) Journal3D.note("P${k.n} pieces=${pc != null} corps=${corps != null} repos=${JeuMKDD.repos(k.n) != null} c=${Journal3D.m(pc, 0)} r1=${Journal3D.m(pc, 12)} r3=${Journal3D.m(pc, 36)}")
                    if (pc != null && corps != null) {
                        val v = vitesseKart(k.n, tx, ty, tz, ra)
                        posePiece(ra, tx, ty, tz, pc, 0, 0f); GLES20.glUniformMatrix4fv(uM, 1, false, mModele, 0)
                        for (m in corps) morceauK(m, uAl, uS, mp, mu, mn)
                        // (roues : d'apres le kart au repos, accrochees a la carrosserie ; le telephone les fait tourner lui-meme ;
                        //  tes roues avant braquent comme dans le jeu, lues en direct)
                        val rp = JeuMKDD.repos(k.n); val rq = rp?.copyOf() ?: pc
                        if (rp != null && k.joueur) for (i in 1..2) {
                            val o = i * 12
                            val pl = Math.atan2(-pc[o + 8].toDouble(), pc[o].toDouble()); val pr = Math.atan2(-rp[o + 8].toDouble(), rp[o].toDouble())
                            var dl = pl - pr; while (dl > Math.PI) dl -= 2 * Math.PI; while (dl < -Math.PI) dl += 2 * Math.PI
                            val c = Math.cos(dl.coerceIn(-0.7, 0.7)).toFloat(); val sn = Math.sin(dl.coerceIn(-0.7, 0.7)).toFloat()
                            for (cc in 0..2) { val x = rp[o + cc]; val z = rp[o + 8 + cc]; rq[o + cc] = c * x + sn * z; rq[o + 8 + cc] = -sn * x + c * z }
                        }
                        for (i in 1..4) { val rm = modeleMK("mk_roue_${k.kart}_${if (i % 2 == 1) 'g' else 'd'}.txt") ?: continue
                            val cle = k.n * 4 + i
                            val ray = rayonRoue.getOrPut("${k.kart}") { rayon(rm) }
                            val sc = Math.sqrt((rq[i * 12 + 1] * rq[i * 12 + 1] + rq[i * 12 + 5] * rq[i * 12 + 5] + rq[i * 12 + 9] * rq[i * 12 + 9]).toDouble()).toFloat()
                            val th = ((rotRoue[cle] ?: 0f) + v * dtKarts / (ray * sc).coerceAtLeast(0.05f)) % (2f * Math.PI.toFloat())
                            rotRoue[cle] = th
                            posePiece(ra, tx, ty, tz, rq, i, th); GLES20.glUniformMatrix4fv(uM, 1, false, mModele, 0)
                            for (m in rm) morceauK(m, uAl, uS, mp, mu, mn) }
                        fait = true; continue
                    }
                }
                val mod = modeleMK(nom) ?: continue
                // decalage du siege (repere du kart : y vers le haut, z vers l'avant)
                val sy = siege[0] * s; val sz = siege[1] * s
                val ox = ra[1] * sy + ra[2] * sz; val oy = ra[4] * sy + ra[5] * sz; val oz = ra[7] * sy + ra[8] * sz
                for (c in 0..2) { mModele[c * 4] = ra[c]; mModele[c * 4 + 1] = ra[3 + c]; mModele[c * 4 + 2] = ra[6 + c]; mModele[c * 4 + 3] = 0f }
                mModele[12] = tx + ox; mModele[13] = ty + oy; mModele[14] = tz + oz; mModele[15] = 1f
                GLES20.glUniformMatrix4fv(uM, 1, false, mModele, 0)
                for (m in mod) {
                    when (m.mode) {
                        0 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.02f) }
                        1 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.5f) }
                        2 -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA); GLES20.glDepthMask(false); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.02f) }
                        else -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE); GLES20.glDepthMask(false); GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, 0.02f) }
                    }
                    GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, m.tex)
                    val b = m.mod
                    b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
                    b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
                    b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
                    GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
                }
                fait = true
            }
        }
        // le Chomp enchaine (pack : mk_chomp.txt, sa peau ; mk_chomp_maillon.txt) : articulations et maillons lus en direct
        JeuMKDD.chomp()?.let { ch ->
            fun dessiner(m: Morceau) {
                if (m.mode >= 2) { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, if (m.mode == 2) GLES20.GL_ONE_MINUS_SRC_ALPHA else GLES20.GL_ONE); GLES20.glDepthMask(false) }
                else { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true) }
                GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, if (m.mode == 1) 0.5f else 0.02f)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, m.tex)
                val b = m.mod
                b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
                b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
                b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
            }
            lirePeau(-1)?.let { pe ->
                mModele.fill(0f); mModele[0] = 1f; mModele[5] = 1f; mModele[10] = 1f; mModele[15] = 1f
                GLES20.glUniformMatrix4fv(uM, 1, false, mModele, 0)
                val r = ch.copyOf(24)
                for (m in pe) {
                    poser(m, r, monde = true)
                    dessiner(Morceau(m.tex, m.mode, m.buf, m.n))
                }
            }
            modeleMK("mk_chomp_maillon.txt")?.let { mod ->
                // maillon : modele deja dans le repere de l'appli (x, -z, y) / 100 ; sa matrice du jeu R -> P R P^-1, position (x, -z, y) / 100
                for (i in 0 until 18) {
                    val g = 24 + 12 * i
                    val rr = floatArrayOf(ch[g], ch[g + 1], ch[g + 2], ch[g + 4], ch[g + 5], ch[g + 6], ch[g + 8], ch[g + 9], ch[g + 10])
                    // P = [[1,0,0],[0,0,-1],[0,1,0]] : K = P R P^T
                    val k = floatArrayOf(rr[0], -rr[2], rr[1], -rr[6], rr[8], -rr[7], rr[3], -rr[5], rr[4])
                    for (c in 0..2) { mModele[c * 4] = k[c]; mModele[c * 4 + 1] = k[3 + c]; mModele[c * 4 + 2] = k[6 + c]; mModele[c * 4 + 3] = 0f }
                    mModele[12] = ch[g + 3] * 0.01f; mModele[13] = -ch[g + 11] * 0.01f; mModele[14] = ch[g + 7] * 0.01f; mModele[15] = 1f
                    GLES20.glUniformMatrix4fv(uM, 1, false, mModele, 0)
                    for (m in mod) dessiner(m)
                }
            }
        }
        // les objets de la course : leur modele (pack : mk_objet_<sorte>.txt), sinon un point de leur couleur (plus bas)
        val obs = JeuMKDD.objetsAncres(); objetsPoints.clear(); feux.clear()
        for (ob in obs) { val genre = ob.genre; val p = ob.p
            val mod = if (mkObjetAbsent.contains(genre)) null else modeleMK("mk_objet_$genre.txt").also { if (it == null) mkObjetAbsent.add(genre) }
            // (objet tenu : place dans le repere de son kart tel qu'il est dessine ; il reste colle au kart)
            val an = if (ob.rel != null) ancres[ob.kart] else null; val rl = ob.rel
            val ax: Float; val ay: Float; val az: Float
            if (an != null && rl != null) {
                ax = an[9] + (an[0] * rl[0] + an[1] * rl[1] + an[2] * rl[2]) * 0.01f
                ay = an[10] + (an[3] * rl[0] + an[4] * rl[1] + an[5] * rl[2]) * 0.01f
                az = an[11] + (an[6] * rl[0] + an[7] * rl[1] + an[8] * rl[2]) * 0.01f
            } else { ax = p[0] * 0.01f; ay = -p[2] * 0.01f; az = p[1] * 0.01f }
            if (genre == 9 || genre == 20 || genre == 21) feux.add(floatArrayOf(ax, ay, az))
            if (journalTour % 5 == 0) Journal3D.note("O genre=$genre kart=${ob.kart} tenu=${an != null && rl != null} p=${Journal3D.v(ax, ay, az)}")
            if (mod == null) { objetsPoints.add(floatArrayOf(ax, ay, az, genre.toFloat())); continue }
            // (debout ; les carapaces tournent sur elles-memes, comme dans le jeu)
            val ang = if (genre in 0..2 || genre == 13) ((System.nanoTime() / 1e9 * 6.0) % (2 * Math.PI)).toFloat() else 0f
            val co = Math.cos(ang.toDouble()).toFloat(); val si = Math.sin(ang.toDouble()).toFloat()
            mModele.fill(0f); mModele[0] = co; mModele[1] = si; mModele[4] = -si; mModele[5] = co; mModele[10] = 1f
            mModele[12] = ax; mModele[13] = ay; mModele[14] = az; mModele[15] = 1f
            GLES20.glUniformMatrix4fv(uM, 1, false, mModele, 0)
            for (m in mod) {
                if (m.mode >= 2) { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, if (m.mode == 2) GLES20.GL_ONE_MINUS_SRC_ALPHA else GLES20.GL_ONE); GLES20.glDepthMask(false) }
                else { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true) }
                GLES20.glUniform1f(uAl, 1f); GLES20.glUniform1f(uS, if (m.mode == 1) 0.5f else 0.02f)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, m.tex)
                val b = m.mod
                b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
                b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
                b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, m.n)
            }
        }
        GLES20.glDisableVertexAttribArray(mp); GLES20.glDisableVertexAttribArray(mu); GLES20.glDisableVertexAttribArray(mn)
        GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true)
        GLES20.glUseProgram(progL)
        return fait
    }
    private val objetsPoints = ArrayList<FloatArray>()
    // ---------------------------------------------------------------- effets de Mario Kart
    /** une particule : x y z, vitesse, age, duree, taille au debut et a la fin, couleur (r g b a), mode (0 fumee, 1 lumiere), gravite */
    private val PN = 16; private val PMAX = 900
    private val pts = FloatArray(PN * PMAX); private var nParts = 0
    private var progP = 0; private var effetsT = 0L
    private val alea = java.util.Random()
    private val pBuf: FloatBuffer = ByteBuffer.allocateDirect(PMAX * 8 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
    private fun emettre(x: Float, y: Float, z: Float, vx: Float, vy: Float, vz: Float, vie: Float, t0: Float, t1: Float,
                        r: Float, g: Float, b: Float, a: Float, lum: Boolean, grav: Float = 0f) {
        if (nParts >= PMAX) return
        val o = nParts++ * PN
        pts[o] = x; pts[o + 1] = y; pts[o + 2] = z; pts[o + 3] = vx; pts[o + 4] = vy; pts[o + 5] = vz
        pts[o + 6] = 0f; pts[o + 7] = vie; pts[o + 8] = t0; pts[o + 9] = t1
        pts[o + 10] = r; pts[o + 11] = g; pts[o + 12] = b; pts[o + 13] = a; pts[o + 14] = if (lum) 1f else 0f; pts[o + 15] = grav
    }
    private fun hasard(): Float = alea.nextFloat() * 2f - 1f
    /** nombre a emettre cette image pour un debit par seconde (la partie decimale au hasard) */
    private fun combien(debit: Float, dt: Float): Int { val v = debit * dt; val n = v.toInt(); return n + if (alea.nextFloat() < v - n) 1 else 0 }
    /** point du repere d'un kart (unites du jeu) -> appli, avec son repere tel qu'il est dessine (an : ra puis position) */
    private fun versAppli(an: FloatArray, lx: Float, ly: Float, lz: Float, o: FloatArray) {
        o[0] = an[9] + (an[0] * lx + an[1] * ly + an[2] * lz) * 0.01f
        o[1] = an[10] + (an[3] * lx + an[4] * ly + an[5] * lz) * 0.01f
        o[2] = an[11] + (an[6] * lx + an[7] * ly + an[8] * lz) * 0.01f
    }
    private val pt = FloatArray(3)
    /** les effets lus dans le jeu (KartBody : derapage, niveau du mini-turbo, turbo, etoile ; objets : boules de feu),
     *  refaits ici avec des particules : fumee des roues arriere au derapage, etincelles (oranges puis bleues quand le
     *  mini-turbo monte), flamme du turbo (bleue pour le mini-turbo), etincelles arc-en-ciel de l'etoile, traine des boules de feu */
    private fun dessinerEffets(mvp: FloatArray) {
        if (progP == 0) return
        val now = System.nanoTime(); val dt = if (effetsT == 0L) 0f else ((now - effetsT) / 1e9f).coerceIn(0f, 0.1f); effetsT = now
        // (vieillir et deplacer)
        var w = 0
        for (i in 0 until nParts) { val o = i * PN
            val age = pts[o + 6] + dt; if (age >= pts[o + 7]) continue
            if (w != i) System.arraycopy(pts, o, pts, w * PN, PN)
            val q = w * PN; pts[q + 6] = age
            pts[q + 5] += pts[q + 15] * dt
            pts[q] += pts[q + 3] * dt; pts[q + 1] += pts[q + 4] * dt; pts[q + 2] += pts[q + 5] * dt
            w++ }
        nParts = w
        // (emettre)
        if (dt > 0f) {
            for ((n, an) in ancres) {
                val e = JeuMKDD.effet(n) ?: continue
                val ro = JeuMKDD.rouesArriere(n) ?: floatArrayOf(-55f, 50f, -55f, 55f, 50f, -55f)
                val arX = -an[2]; val arY = -an[5]; val arZ = -an[8]                       // (vers l'arriere du kart, dans l'appli)
                if (e.derape) for (k in 0..1) {
                    versAppli(an, ro[k * 3], ro[k * 3 + 1] - 25f, ro[k * 3 + 2], pt)
                    repeat(combien(28f, dt)) { emettre(pt[0] + hasard() * 0.08f, pt[1] + hasard() * 0.08f, pt[2],
                        arX * 0.8f + hasard() * 0.3f, arY * 0.8f + hasard() * 0.3f, 0.5f + alea.nextFloat() * 0.4f,
                        0.7f, 0.35f, 1.2f, 0.9f, 0.9f, 0.9f, 0.45f, false) }
                    if (e.niveau >= 1) { val bleu = e.niveau >= 2
                        repeat(combien(45f, dt)) { emettre(pt[0], pt[1], pt[2] + 0.05f,
                            arX * 2.5f + hasard() * 1.2f, arY * 2.5f + hasard() * 1.2f, 1.2f + alea.nextFloat() * 1.5f, 0.28f, 0.2f, 0.05f,
                            if (bleu) 0.35f else 1f, if (bleu) 0.65f else 0.62f, if (bleu) 1f else 0.15f, 1f, true, -7f) } }
                }
                val turbo = e.turbo > 0; val mini = !turbo && e.mini > 0 && !e.derape
                if (turbo || mini) {
                    versAppli(an, 0f, (ro[1] + ro[4]) * 0.5f - 5f, minOf(ro[2], ro[5]) - 20f, pt)
                    repeat(combien(70f, dt)) { val c = alea.nextFloat()
                        emettre(pt[0] + hasard() * 0.1f, pt[1] + hasard() * 0.1f, pt[2] + hasard() * 0.06f,
                            arX * 3f + hasard() * 0.4f, arY * 3f + hasard() * 0.4f, arZ * 3f + 0.3f, 0.2f, 0.55f, 0.12f,
                            if (mini) 0.4f + 0.5f * c else 1f, if (mini) 0.7f + 0.3f * c else 0.45f + 0.5f * c, if (mini) 1f else 0.1f + 0.3f * c, 0.9f, true) }
                }
                if (e.etoile) {
                    versAppli(an, 0f, (ro[1] + ro[4]) * 0.5f + 20f, 0f, pt)
                    repeat(combien(45f, dt)) { val h = ((now / 1e9 * 1.5 + alea.nextFloat() * 0.3) % 1.0).toFloat()
                        val r = (Math.abs(h * 6f - 3f) - 1f).coerceIn(0f, 1f); val g = (2f - Math.abs(h * 6f - 2f)).coerceIn(0f, 1f); val b = (2f - Math.abs(h * 6f - 4f)).coerceIn(0f, 1f)
                        emettre(pt[0] + hasard() * 0.9f, pt[1] + hasard() * 0.9f, pt[2] + hasard() * 0.6f, 0f, 0f, 0.6f, 0.4f, 0.3f, 0.02f,
                            0.3f + 0.7f * r, 0.3f + 0.7f * g, 0.3f + 0.7f * b, 1f, true) }
                }
            }
            for (b in eclats) {                                                   // (la boite casse : eclats arc-en-ciel et un eclair blanc)
                emettre(b[0], b[1], b[2], 0f, 0f, 0f, 0.25f, 2.2f, 0.4f, 1f, 1f, 1f, 0.9f, true)
                repeat(40) { val h = alea.nextFloat()
                    val r = (Math.abs(h * 6f - 3f) - 1f).coerceIn(0f, 1f); val g = (2f - Math.abs(h * 6f - 2f)).coerceIn(0f, 1f); val bl = (2f - Math.abs(h * 6f - 4f)).coerceIn(0f, 1f)
                    emettre(b[0], b[1], b[2], hasard() * 4f, hasard() * 4f, 1.5f + alea.nextFloat() * 3f, 0.6f + alea.nextFloat() * 0.3f, 0.3f, 0.12f,
                        0.3f + 0.7f * r, 0.3f + 0.7f * g, 0.3f + 0.7f * bl, 1f, true, -9f) }
            }
            eclats.clear()
            for (f in feux) repeat(combien(50f, dt)) { val c = alea.nextFloat()
                emettre(f[0] + hasard() * 0.12f, f[1] + hasard() * 0.12f, f[2] + 0.25f + hasard() * 0.1f, hasard() * 0.3f, hasard() * 0.3f, 0.8f,
                    0.35f, 0.7f, 0.1f, 1f, 0.35f + 0.5f * c, 0.05f, 0.9f, true) }
        }
        if (nParts == 0) return
        // (dessin : la fumee d'abord, melangee ; les lumieres ensuite, ajoutees)
        GLES20.glUseProgram(progP)
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progP, "uMvp"), 1, false, mvp, 0)
        GLES20.glUniform1f(GLES20.glGetUniformLocation(progP, "uEch"), ecranH * 0.5f * proj[5])
        val aP = GLES20.glGetAttribLocation(progP, "aPos"); val aC = GLES20.glGetAttribLocation(progP, "aCol"); val aT = GLES20.glGetAttribLocation(progP, "aT")
        GLES20.glEnable(GLES20.GL_BLEND); GLES20.glDepthMask(false)
        for (lum in 0..1) {
            pBuf.clear(); var k = 0
            for (i in 0 until nParts) { val o = i * PN; if (pts[o + 14].toInt() != lum) continue
                val u = pts[o + 6] / pts[o + 7]
                pBuf.put(pts[o]).put(pts[o + 1]).put(pts[o + 2])
                pBuf.put(pts[o + 10]).put(pts[o + 11]).put(pts[o + 12]).put(pts[o + 13] * (1f - u))
                pBuf.put(pts[o + 8] + (pts[o + 9] - pts[o + 8]) * u); k++ }
            if (k == 0) continue
            GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, if (lum == 1) GLES20.GL_ONE else GLES20.GL_ONE_MINUS_SRC_ALPHA)
            pBuf.position(0); GLES20.glVertexAttribPointer(aP, 3, GLES20.GL_FLOAT, false, 32, pBuf); GLES20.glEnableVertexAttribArray(aP)
            pBuf.position(3); GLES20.glVertexAttribPointer(aC, 4, GLES20.GL_FLOAT, false, 32, pBuf); GLES20.glEnableVertexAttribArray(aC)
            pBuf.position(7); GLES20.glVertexAttribPointer(aT, 1, GLES20.GL_FLOAT, false, 32, pBuf); GLES20.glEnableVertexAttribArray(aT)
            GLES20.glDrawArrays(GLES20.GL_POINTS, 0, k)
            GLES20.glDisableVertexAttribArray(aP); GLES20.glDisableVertexAttribArray(aC); GLES20.glDisableVertexAttribArray(aT)
        }
        GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true)
        GLES20.glUseProgram(progL)
    }
    private val mkObjetAbsent = HashSet<Int>()
    /** couleur du point d'un objet sans modele (sorte d'objet du jeu) */
    private fun couleurObjet(g: Int): FloatArray = when (g) {
        0, 1 -> floatArrayOf(0.2f, 0.85f, 0.3f); 2 -> floatArrayOf(0.95f, 0.15f, 0.15f); 3, 4 -> floatArrayOf(1f, 0.88f, 0.15f)
        5, 12 -> floatArrayOf(1f, 0.55f, 0.15f); 6 -> floatArrayOf(1f, 1f, 0.4f); 7 -> floatArrayOf(0.15f, 0.15f, 0.18f)
        8 -> floatArrayOf(0.1f, 0.1f, 0.12f); 9, 20, 21 -> floatArrayOf(1f, 0.4f, 0.05f); 10 -> floatArrayOf(0.5f, 0.9f, 1f)
        11 -> floatArrayOf(0.6f, 1f, 0.6f); 13 -> floatArrayOf(0.2f, 0.4f, 1f); 14 -> floatArrayOf(1f, 0.45f, 0.7f)
        else -> floatArrayOf(1f, 1f, 1f) }

    /** build 272 : Rayman tel qu'il est dans le jeu (squelette, objets et textures lus en memoire) ; false : pas de pose recente */
    private fun dessinerRaymanAnime(mvp: FloatArray, c0: FloatArray): Boolean {
        val mod = animMod ?: return false
        val p = etat.pose ?: return false
        if (System.nanoTime() - p.t > 1_500_000_000L) return false
        if (p !== poseB) {
            val h = HashMap<Int, FloatArray>()
            for ((g, s) in mod.morceaux(p)) { val geo = mod.geos[g] ?: continue; val o = FloatArray(geo.nv * 6); mod.peau(geo, p, s, o); h[g * 32 + s] = o }
            val avant = poseB
            poseA = avant; peauA = peauB; poseB = p; peauB = h
            // build 276 : duree du glissement = ecart moyen entre deux poses (lisse : un ecart irregulier ne fait plus d'a-coups)
            val ecart = if (avant != null) (p.t - avant.t).coerceIn(10_000_000L, 300_000_000L) else 0L
            dtAB = if (ecart == 0L) 1L else if (dtAB <= 1L) ecart else (dtAB * 0.75 + ecart * 0.25).toLong(); tB = System.nanoTime()
            // (on part de la pose affichee a cet instant, pas de l'ancienne : pas de saut si la nouvelle arrive en avance)
            val al0 = if (avant == null) 1f else ((tB - tPrec) / dtPrec.toFloat()).coerceIn(0f, 1f)
            if (al0 < 1f && peauPrec.isNotEmpty()) for ((cle, o) in peauA) { val q = peauPrec[cle] ?: continue
                if (q.size == o.size) for (i in o.indices) o[i] = q[i] + (o[i] - q[i]) * al0 }
            peauPrec = peauA; tPrec = tB; dtPrec = dtAB
            // build 278 : la position du modele glisse avec la pose (meme lecture) : les pieds ne patinent plus
            val ouB = p.ou; val d0 = ouDebut; val f0 = ouFin
            ouDebut = if (d0 != null && f0 != null) FloatArray(3) { d0[it] + (f0[it] - d0[it]) * al0 } else ouB?.copyOf()
            ouFin = ouB?.copyOf()
            if (ouB != null && d0 != null && (ouB[0] - d0[0]).let { it * it } + (ouB[1] - d0[1]).let { it * it } > 400f) ouDebut = ouB.copyOf()   // (teleporte)
        }
        // build 284 : animation connue : l'appli la rejoue a chaque image de l'ecran (image du jeu + vitesse x temps ecoule)
        val an = p.anim; val pb = p.base; val par = p.parents; val ro = p.rot
        if (an != null && pb != null && par != null && ro != null) {
            val ecoule = ((System.nanoTime() - p.t) / 1e9f).coerceIn(0f, 0.25f)
            val loc = pb.copyOf(); an.locales(p.image + an.vitesse * ecoule, loc)
            // build 294 : quand Rayman change d'action, on passe d'une animation a l'autre en douceur (~0,15 s), comme le jeu
            val maintenant = System.nanoTime()
            if (an !== animAffichee) { locAffichee?.let { locDepart = it.copyOf(); tChange = maintenant }; animAffichee = an }
            val ld = locDepart
            if (ld != null && ld.size == loc.size) {
                val k = ((maintenant - tChange) / 150_000_000f).coerceIn(0f, 1f)
                val e = k * k * (3 - 2 * k)
                if (k >= 1f) locDepart = null else for (i in loc.indices) loc[i] = ld[i] + (loc[i] - ld[i]) * e
            }
            locAffichee = loc
            val q = PoseRayman(PoseRayman.composer(loc, par, ro, matJoue), p.types, p.data, p.fix4, p.pos, p.textures)
            val h = HashMap<Int, FloatArray>()
            for ((g, s) in mod.morceaux(q)) { val geo = mod.geos[g] ?: continue
                val cle = g * 32 + s; val o = peauJoue[cle]?.takeIf { it.size == geo.nv * 6 } ?: FloatArray(geo.nv * 6).also { peauJoue[cle] = it }
                mod.peau(geo, q, s, o); h[cle] = o }
            peauB = h; peauA = HashMap()
        }
        if (peauB.isEmpty()) return false
        val al = if (poseA == null) 1f else ((System.nanoTime() - tB).toFloat() / dtAB).coerceIn(0f, 1f)
        GLES20.glUseProgram(progM)
        GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progM, "uMvp"), 1, false, mvp, 0)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glUniform1i(GLES20.glGetUniformLocation(progM, "uTex"), 0)
        val od = ouDebut; val of = ouFin
        val ici = if (od != null && of != null && etat.direct) FloatArray(3) { od[it] + (of[it] - od[it]) * al } else c0
        GLES20.glUniform3f(GLES20.glGetUniformLocation(progM, "uPos"), ici[0], ici[1], ici[2])
        GLES20.glUniform1f(GLES20.glGetUniformLocation(progM, "uAng"), 0f)
        val mp = GLES20.glGetAttribLocation(progM, "aPos"); val mu = GLES20.glGetAttribLocation(progM, "aUv"); val mn = GLES20.glGetAttribLocation(progM, "aN")
        GLES20.glEnableVertexAttribArray(mp); GLES20.glEnableVertexAttribArray(mu); GLES20.glEnableVertexAttribArray(mn)
        for ((cle, o) in peauB) {
            val geo = mod.geos[cle / 32] ?: continue
            val a = peauA[cle]?.takeIf { it.size == o.size }
            for (e in geo.el) {
                val nom = poseB?.textures?.get(e.eo) ?: e.png
                val tex = texAnim.getOrPut(nom) { chargerTex(nom, repete = true) }.takeIf { it != 0 }
                    ?: texAnim.getOrPut(e.png) { chargerTex(e.png, repete = true) }.takeIf { it != 0 } ?: continue
                val n = e.coins.size
                val fb = bufEl.getOrPut("$cle/${e.eo}") { ByteBuffer.allocateDirect(n * 32).order(ByteOrder.nativeOrder()).asFloatBuffer() }
                fb.clear()
                for (k in 0 until n) {
                    val i = e.coins[k] * 6
                    if (a != null) { for (q in 0..2) fb.put(a[i + q] + (o[i + q] - a[i + q]) * al) } else { fb.put(o[i]); fb.put(o[i + 1]); fb.put(o[i + 2]) }
                    fb.put(e.uv[2 * k]); fb.put(e.uv[2 * k + 1])
                    fb.put(o[i + 3]); fb.put(o[i + 4]); fb.put(o[i + 5])
                }
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex)
                // build 300 : l'anneau de charge du poing est une lumiere (additive), le reste est opaque
                val lumiere = e.png.contains("ringofpower")
                if (lumiere) { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE); GLES20.glDepthMask(false) }
                else { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true) }
                fb.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, fb)
                fb.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, fb)
                fb.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, fb); fb.position(0)
                GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, n)
            }
        }
        GLES20.glDisableVertexAttribArray(mp); GLES20.glDisableVertexAttribArray(mu); GLES20.glDisableVertexAttribArray(mn)
        GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true)
        GLES20.glUseProgram(progL)
        return true
    }

    // build 269 : position affichee = position lue, lissee a chaque image ; cap de Rayman d'apres son deplacement
    private var tSuivre = 0L
    private var lisse: FloatArray? = null; private var tLisse = 0L; private var cap = 0f; private var capVise = 0f
    private fun cibleLissee(): FloatArray {
        val v = etat.cible; val l = lisse; val n = System.nanoTime()
        if (l == null || !etat.direct) { lisse = v.copyOf(); tLisse = n; return v }
        val dt = ((n - tLisse) / 1e9f).coerceIn(0f, 0.1f); tLisse = n
        val dx = v[0] - l[0]; val dy = v[1] - l[1]; val dz = v[2] - l[2]
        if (dx * dx + dy * dy + dz * dz > 400f) { lisse = v.copyOf(); return v }       // (teleporte : pas de glissade)
        val k = 1f - Math.exp(-18.0 * dt).toFloat()                                      // (rattrape ~95 % en 1/6 s)
        val m = floatArrayOf(l[0] + dx * k, l[1] + dy * k, l[2] + dz * k)
        val vx = m[0] - l[0]; val vy = m[1] - l[1]
        if (vx * vx + vy * vy > 1e-5f) capVise = Math.atan2(vy.toDouble(), vx.toDouble()).toFloat()
        var dc = capVise - cap; while (dc > Math.PI) dc -= (2 * Math.PI).toFloat(); while (dc < -Math.PI) dc += (2 * Math.PI).toFloat()
        cap += dc * (1f - Math.exp(-10.0 * dt).toFloat())
        lisse = m; return m
    }

    private fun dessinerScene() {
        noImage++                                             // (build 356 : peaux des objets en direct recalculees une fois)
        etat.rejeu?.invoke()                                  // (build 345 : le direct, rejoue a chaque image)
        val c0 = cibleLissee(); val dc = etat.decal
        // build 282 : mode libre : le centre reste ou il etait (seuls les doigts le deplacent)
        val base = if (etat.mode == 2) (etat.libre ?: c0.copyOf().also { etat.libre = it }) else c0
        val c = floatArrayOf(base[0] + dc[0], base[1] + dc[1], base[2] + dc[2] + if (etat.mode == 3) 0.7f else 0f)   // (3e personne : on vise un peu au-dessus du kart)
        // build 282 : mode suivre : la camera se place doucement derriere Rayman, dans sa direction
        val nt = System.nanoTime(); val dtS = ((nt - tSuivre) / 1e9f).coerceIn(0f, 0.1f); tSuivre = nt
        // 3e personne (Mario Kart) : la direction de ton kart (sa matrice lue en direct : l'avant du kart, z du jeu)
        val mj = if (etat.mode == 3) JeuMKDD.matJoueur else null
        if (etat.mode == 3 && mj != null && mj.size >= 12 && mj[2] * mj[2] + mj[10] * mj[10] > 0.05f) {
            val l0 = etat.lacetActuel()
            val vise = Math.atan2(-mj[10].toDouble(), mj[2].toDouble()).toFloat()        // (repere de l'appli : x, -z, y)
            var dl = vise - l0; while (dl > Math.PI) dl -= (2 * Math.PI).toFloat(); while (dl < -Math.PI) dl += (2 * Math.PI).toFloat()
            etat.lacet = l0 + dl * (1f - Math.exp(-7.0 * dtS).toFloat())
        } else if (etat.mode == 3 && !etat.capPerso.isNaN()) {        // (build 340 : Link de Wind Waker)
            val l0 = etat.lacetActuel()
            var dl = etat.capPerso - l0; while (dl > Math.PI) dl -= (2 * Math.PI).toFloat(); while (dl < -Math.PI) dl += (2 * Math.PI).toFloat()
            etat.lacet = l0 + dl * (1f - Math.exp(-5.0 * dtS).toFloat())
        } else if (etat.mode == 1 || etat.mode == 3) {
            val l0 = etat.lacetActuel()
            // (build 290 : la vraie direction de Rayman, lue avec sa pose : son modele regarde vers -y ; sinon d'apres son deplacement)
            val ro = etat.pose?.takeIf { System.nanoTime() - it.t < 1_000_000_000L }?.rot
            val vise = if (ro != null && ro[4] * ro[4] + ro[5] * ro[5] > 0.05f) Math.atan2(-ro[5].toDouble(), -ro[4].toDouble()).toFloat() else cap
            var dl = vise - l0; while (dl > Math.PI) dl -= (2 * Math.PI).toFloat(); while (dl < -Math.PI) dl += (2 * Math.PI).toFloat()
            etat.lacet = l0 + dl * (1f - Math.exp(-3.0 * dtS).toFloat())
        }
        val a = etat.lacetActuel()
        val tg = etat.tangage; val d = etat.dist
        var ex = c[0] - (Math.cos(a.toDouble()) * Math.cos(tg.toDouble()) * d).toFloat()
        var ey = c[1] - (Math.sin(a.toDouble()) * Math.cos(tg.toDouble()) * d).toFloat()
        var ez = c[2] + (Math.sin(tg.toDouble()) * d).toFloat()
        var vx = c[0]; var vy = c[1]; var vz = c[2]
        if (etat.mode == 5) {                             // build 332 : visite : la camera est a l'oeil et regarde devant elle
            val o = etat.oeil; val r = etat.regard()
            ex = o[0]; ey = o[1]; ez = o[2]; vx = ex + r[0]; vy = ey + r[1]; vz = ez + r[2]
        }
        // build 349 : la camera reste au-dessus de la mer (Link nage 80 cm sous la surface : en 3e personne, la camera passait
        // sous l'eau et ne voyait plus la mer que par en dessous)
        // build 351 : seulement quand la camera suit Link pres de la surface ; en visite, en mode libre, ou si ce qu'on regarde
        // est nettement sous la mer, la camera peut descendre sous l'eau
        val ne = scene.niveauEau
        if (!ne.isNaN() && etat.mode != 5 && etat.mode != 2 && c[2] > ne - 2f && ez < ne + 0.4f) { val dz = ne + 0.4f - ez; ez += dz; vz += dz * 0.5f }
        etat.camPos = floatArrayOf(ex, ey, ez)
        android.opengl.Matrix.setLookAtM(vue, 0, ex, ey, ez, vx, vy, vz, 0f, 0f, 1f)
        android.opengl.Matrix.multiplyMM(mvp, 0, proj, 0, vue, 0)
        // vue du jeu (Mario Kart) : la camera du jeu elle-meme (matrice de vue et angle lus en direct, rejoues comme le reste)
        val cj = if (etat.mode == 4 && JeuMKDD.direct()) { JeuMKDD.rejouer(); JeuMKDD.camJeu }   // (sans direct : la vue de derriere)
                 else if (etat.mode == 4) etat.camJeu?.takeIf { System.nanoTime() - etat.tCamJeu < 1_500_000_000L } else null   // (build 340 : autres jeux)
        if (cj != null) {
            // (repere du jeu -> appli : x, -z, y ; vue = R P^T, position x 0,01)
            vue[0] = cj[0]; vue[1] = cj[4]; vue[2] = cj[8]; vue[3] = 0f
            vue[4] = -cj[2]; vue[5] = -cj[6]; vue[6] = -cj[10]; vue[7] = 0f
            vue[8] = cj[1]; vue[9] = cj[5]; vue[10] = cj[9]; vue[11] = 0f
            vue[12] = cj[3] * 0.01f; vue[13] = cj[7] * 0.01f; vue[14] = cj[11] * 0.01f; vue[15] = 1f
            val asp = ecranW.toFloat() / ecranH.coerceAtLeast(1)
            // (meme largeur de vue que le jeu en 4/3 ; en portrait, l'angle du jeu en hauteur, au plus 100 degres)
            val th = Math.tan(Math.toRadians(cj[12] / 2.0)) * 4.0 / 3.0
            val fy = if (asp >= 4f / 3f) cj[12].toDouble() else minOf(2 * Math.toDegrees(Math.atan(th / asp)), 100.0)
            android.opengl.Matrix.perspectiveM(projJeu, 0, fy.toFloat(), asp, 0.5f, 9000f)
            android.opengl.Matrix.multiplyMM(mvp, 0, projJeu, 0, vue, 0)
        }
        etat.mvpVue = mvp.copyOf(); etat.vueW = ecranW; etat.vueH = ecranH
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glDisable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(progCiel)
        GLES20.glUniform3fv(GLES20.glGetUniformLocation(progCiel, "uHaut"), 1, amb.cielHaut, 0)
        GLES20.glUniform3fv(GLES20.glGetUniformLocation(progCiel, "uBas"), 1, amb.cielBas, 0)
        GLES20.glVertexAttribPointer(cPos, 2, GLES20.GL_FLOAT, false, 8, quad); GLES20.glEnableVertexAttribArray(cPos)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(cPos)
        GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        GLES20.glUseProgram(prog)
        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0)
        GLES20.glUniform3f(uCam, ex, ey, ez)
        GLES20.glUniform1i(uTex, 0); GLES20.glUniform1i(uLm, 1)
        GLES20.glUniform4f(uMulL, 1f, 1f, 1f, 1f); GLES20.glUniform1f(uVentL, 0f)      // (build 356 : valeurs neutres)
        val tSec = ((System.nanoTime() - t0) / 1e9 % 2000.0)
        GLES20.glUniform1f(uTL, ((System.nanoTime() - t0) / 1e9 % 2000.0).toFloat())
        GLES20.glUniform3fv(uBrumeL, 1, amb.brume, 0); GLES20.glUniform3f(uBrumePL, amb.brumeDebut, amb.brumeLong, amb.brumeMax)
        GLES20.glUniform3fv(uTeinteL, 1, amb.eau, 0)
        GLES20.glEnableVertexAttribArray(aPos); GLES20.glEnableVertexAttribArray(aUv); GLES20.glEnableVertexAttribArray(aCol)
        var linkFait = false
        for (passe in 0..3) {                             // opaque, decoupe, transparent, lumiere (additive)
            // build 349 : Link de Wind Waker avant l'eau et les transparents : sous la surface, on le voit a travers la mer
            if (passe == 2 && etat.montrerRayman && etat.linkWW != null) {
                linkFait = dessinerLinkWW(mvp, c0)
                GLES20.glUseProgram(prog); GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0)
                GLES20.glEnableVertexAttribArray(aPos); GLES20.glEnableVertexAttribArray(aUv); GLES20.glEnableVertexAttribArray(aCol)
            }
            when (passe) {
                0 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uSeuil, 0.02f) }
                1 -> { GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glUniform1f(uSeuil, 0.5f) }
                2 -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA); GLES20.glDepthMask(false); GLES20.glUniform1f(uSeuil, 0.02f) }
                3 -> { GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE); GLES20.glDepthMask(false); GLES20.glUniform1f(uSeuil, 0.02f) }
            }
            for ((i, g) in scene.groupes.withIndex()) {
                if (i >= vbo.size || g.mode != passe) continue
                dessinerGroupe(g, vbo[i], tSec)
            }
            // build 332 : les objets qui bougent (Mario Kart) : leur matrice a ce moment, puis leurs groupes
            val mo = scene.mobiles
            if (mo != null) for (o in mo.objets) {
                var pret = false
                for (gi in o.g0 until o.g0 + o.nb) {
                    val g = mo.groupes.getOrNull(gi) ?: continue
                    if (gi >= vboM.size || g.mode != passe) continue
                    if (!pret) {
                        mo.matrice(o, tSec, mMob)
                        android.opengl.Matrix.multiplyMM(mvpMob, 0, mvp, 0, mMob, 0)
                        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvpMob, 0)
                        // (brume : la camera dans le repere de l'objet, la partie 3 x 3 etant une rotation)
                        val qx = ex - mMob[12]; val qy = ey - mMob[13]; val qz = ez - mMob[14]
                        GLES20.glUniform3f(uCam, mMob[0] * qx + mMob[1] * qy + mMob[2] * qz, mMob[4] * qx + mMob[5] * qy + mMob[6] * qz, mMob[8] * qx + mMob[9] * qy + mMob[10] * qz)
                        pret = true
                    }
                    dessinerGroupe(g, vboM[gi], tSec)
                }
                if (pret) { GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0); GLES20.glUniform3f(uCam, ex, ey, ez) }
            }
            // build 353 : objets animes par le jeu (Wind Waker) : l'image de leur animation a ce moment, a leur place
            val ani = scene.animes
            if (ani != null && vboAniPos.size == ani.modeles.size) {
                val cachees = etat.modelesVifs
                for (cp in ani.copies) {
                    if (cp.modele in cachees) continue                          // (build 356 : montre par le direct)
                    val md = ani.modeles[cp.modele]
                    if (md.groupes.none { it.mode == passe } || md.images == 0) continue
                    val img = (((tSec * md.ips) + cp.phase * md.images).toLong() % md.images).toInt()
                    val ob = if (cp.mobile >= 0) mo?.objets?.getOrNull(cp.mobile) else null
                    if (ob != null) { mo!!.matrice(ob, tSec, mMob); android.opengl.Matrix.multiplyMM(mAni, 0, mMob, 0, cp.mat, 0) }
                    else System.arraycopy(cp.mat, 0, mAni, 0, 16)
                    android.opengl.Matrix.multiplyMM(mvpAni, 0, mvp, 0, mAni, 0)
                    GLES20.glUniformMatrix4fv(uMvp, 1, false, mvpAni, 0)
                    if (android.opengl.Matrix.invertM(mAniInv, 0, mAni, 0)) {       // (brume : la camera dans le repere de l'objet)
                        GLES20.glUniform3f(uCam, mAniInv[0] * ex + mAniInv[4] * ey + mAniInv[8] * ez + mAniInv[12],
                            mAniInv[1] * ex + mAniInv[5] * ey + mAniInv[9] * ez + mAniInv[13], mAniInv[2] * ex + mAniInv[6] * ey + mAniInv[10] * ez + mAniInv[14]) }
                    GLES20.glUniform1f(uEauL, 0f); GLES20.glUniform1f(uChaudL, 0f); GLES20.glUniform1f(uLmK, 0f)
                    if (aUvL >= 0) { GLES20.glDisableVertexAttribArray(aUvL); GLES20.glVertexAttrib2f(aUvL, 0f, 0f) }
                    for ((gi, g) in md.groupes.withIndex()) {
                        if (g.mode != passe) continue
                        val t = textures[texDuMoment(g.stub, tSec, cp.phase)]; couleurDuMoment(g.stub, tSec, cp.phase)
                        GLES20.glUniform1f(uVentL, 0f)
                        GLES20.glUniform1f(uAvecTex, if (t != null) 1f else 0f)
                        if (t != null) { GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t) }
                        GLES20.glUniform2f(uDefil, if (g.du != 0f) ((tSec * g.du) % 1.0).toFloat() else 0f, if (g.dv != 0f) ((tSec * g.dv) % 1.0).toFloat() else 0f)
                        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vboAniUv[cp.modele][gi])
                        GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, 12, 0)
                        GLES20.glVertexAttribPointer(aCol, 4, GLES20.GL_UNSIGNED_BYTE, true, 12, 8)
                        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vboAniPos[cp.modele][img])
                        GLES20.glVertexAttribPointer(aPos, 3, GLES20.GL_FLOAT, false, 12, g.debut * 12)
                        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, g.n)
                    }
                }
                // build 356 : les objets lus en direct : leur peau posee sur les os du jeu, ou leur image de repos a la place
                // donnee par le jeu (objets plus loin)
                val vp = etat.vifsWW
                if (vp != null) for ((vi, v) in vp.withIndex()) {
                    val md = ani.modeles.getOrNull(v.modele) ?: continue
                    if (md.groupes.none { it.mode == passe } || md.images == 0) continue
                    var posBuf = vboAniPos[v.modele][0]
                    if (v.plein) {
                        val (vb, fb) = tamponVif(vi, md.total)
                        if (imgVif[vi] != noImage) {
                            if (!ani.poser(v.modele, v.m, fb)) continue
                            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vb)
                            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, md.total * 12, fb, GLES20.GL_STREAM_DRAW); imgVif[vi] = noImage
                        }
                        posBuf = vb
                        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0); GLES20.glUniform3f(uCam, ex, ey, ez)
                    } else {
                        matriceAppli(v.m, 0, mAni)
                        android.opengl.Matrix.multiplyMM(mvpAni, 0, mvp, 0, mAni, 0)
                        GLES20.glUniformMatrix4fv(uMvp, 1, false, mvpAni, 0)
                        if (android.opengl.Matrix.invertM(mAniInv, 0, mAni, 0))
                            GLES20.glUniform3f(uCam, mAniInv[0] * ex + mAniInv[4] * ey + mAniInv[8] * ez + mAniInv[12],
                                mAniInv[1] * ex + mAniInv[5] * ey + mAniInv[9] * ez + mAniInv[13], mAniInv[2] * ex + mAniInv[6] * ey + mAniInv[10] * ez + mAniInv[14])
                    }
                    GLES20.glUniform1f(uEauL, 0f); GLES20.glUniform1f(uChaudL, 0f); GLES20.glUniform1f(uLmK, 0f); GLES20.glUniform1f(uVentL, 0f)
                    if (aUvL >= 0) { GLES20.glDisableVertexAttribArray(aUvL); GLES20.glVertexAttrib2f(aUvL, 0f, 0f) }
                    for ((gi, g) in md.groupes.withIndex()) {
                        if (g.mode != passe) continue
                        val t = textures[texDuMoment(g.stub, tSec)]; couleurDuMoment(g.stub, tSec)
                        GLES20.glUniform1f(uAvecTex, if (t != null) 1f else 0f)
                        if (t != null) { GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, t) }
                        GLES20.glUniform2f(uDefil, if (g.du != 0f) ((tSec * g.du) % 1.0).toFloat() else 0f, if (g.dv != 0f) ((tSec * g.dv) % 1.0).toFloat() else 0f)
                        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vboAniUv[v.modele][gi])
                        GLES20.glVertexAttribPointer(aUv, 2, GLES20.GL_FLOAT, false, 12, 0)
                        GLES20.glVertexAttribPointer(aCol, 4, GLES20.GL_UNSIGNED_BYTE, true, 12, 8)
                        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, posBuf)
                        GLES20.glVertexAttribPointer(aPos, 3, GLES20.GL_FLOAT, false, 12, g.debut * 12)
                        GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, g.n)
                    }
                }
                GLES20.glUniformMatrix4fv(uMvp, 1, false, mvp, 0); GLES20.glUniform3f(uCam, ex, ey, ez)
            }
        }
        GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true)
        GLES20.glUniform4f(uMulL, 1f, 1f, 1f, 1f); GLES20.glUniform1f(uVentL, 0f)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
        GLES20.glDisableVertexAttribArray(aUv); GLES20.glDisableVertexAttribArray(aCol); if (aUvL >= 0) GLES20.glDisableVertexAttribArray(aUvL)
        GLES20.glUniform1f(uLmK, 0f); GLES20.glUniform2f(uDefil, 0f, 0f)
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
        if (scene.versionPris != versionVue) reconstruireObjets()
        // build 245 : objets. D'abord en transparence a travers le decor (on les voit derriere les murs), puis nets la ou ils sont visibles
        if (objBuf.isNotEmpty()) {
            GLES20.glUseProgram(progO)
            GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progO, "uMvp"), 1, false, mvp, 0)
            val oPos = GLES20.glGetAttribLocation(progO, "aPos"); val oT = GLES20.glGetUniformLocation(progO, "uTaille")
            val oC = GLES20.glGetUniformLocation(progO, "uCoul"); val oF = GLES20.glGetUniformLocation(progO, "uForme")
            GLES20.glEnable(GLES20.GL_BLEND); GLES20.glBlendFunc(GLES20.GL_SRC_ALPHA, GLES20.GL_ONE_MINUS_SRC_ALPHA); GLES20.glDepthMask(false)
            GLES20.glEnableVertexAttribArray(oPos)
            val voir = etat.voir
            for (passe in 0..1) {
                if (passe == 0) GLES20.glDisable(GLES20.GL_DEPTH_TEST) else GLES20.glEnable(GLES20.GL_DEPTH_TEST)
                for (t in "uqtidjkvrpfc") {                          // les cages en dernier (par-dessus)
                    val fb = objBuf[t] ?: continue
                    if (t !in voir) continue
                    if (gemmesC.containsKey(t)) continue                         // (build 249 : vraies gemmes, plus bas)
                    if (t == 'c' && passe == 1 && cageMod != null) continue        // (build 261 : la cage est dessinee en 3D)
                    if (t == 'r' && texRouge != 0) continue
                    if (t == 'p' && texPouvoir.isNotEmpty()) continue
                    if (t == 'i' && passe == 1 && mkBoite != null) continue          // (build 288 : la vraie boite, dessinee en 3D)
                    val (taille, coul) = when (t) {
                        'c' -> 30f to floatArrayOf(1f, 0.74f, 0.25f); 'f' -> 26f to floatArrayOf(0.62f, 0.95f, 1f)
                        'p' -> 18f to floatArrayOf(0.62f, 0.55f, 1f); 'r' -> 11f to floatArrayOf(1f, 0.25f, 0.22f)
                        'k' -> 12f to floatArrayOf(0.95f, 0.1f, 0.3f); 'v' -> 15f to floatArrayOf(0.2f, 0.9f, 0.35f)
                        'i' -> 20f to floatArrayOf(1f, 1f, 1f); 'd' -> 26f to floatArrayOf(1f, 1f, 1f)
                        't' -> 22f to floatArrayOf(0.9f, 0.62f, 0.2f)                  // (build 336 : coffres de Wind Waker)
                        'u' -> 12f to floatArrayOf(0.3f, 1f, 0.45f); 'q' -> 18f to floatArrayOf(1f, 0.35f, 0.5f)   // (build 338 : rubis, quarts de coeur)
                        else -> 11f to floatArrayOf(1f, 0.92f, 0.28f) }
                    GLES20.glUniform1f(oT, taille * echelle); GLES20.glUniform1f(oF, if (t == 'c' || t == 'f' || t == 't') 1f else if (t == 'i') 2f else if (t == 'd') 3f else 0f)
                    GLES20.glUniform4f(oC, coul[0], coul[1], coul[2], if (passe == 0) 0.35f else 1f)
                    GLES20.glVertexAttribPointer(oPos, 3, GLES20.GL_FLOAT, false, 12, fb)
                    GLES20.glDrawArrays(GLES20.GL_POINTS, 0, fb.capacity() / 3)
                }
            }
            GLES20.glDisableVertexAttribArray(oPos)
            dessinerVraisObjets(voir)
            GLES20.glDisable(GLES20.GL_BLEND); GLES20.glDepthMask(true); GLES20.glEnable(GLES20.GL_DEPTH_TEST)
            GLES20.glUseProgram(progL)
        }
        val kartsDessines = dessinerKarts(mvp, c0)
        dessinerEffets(mvp)
        if (objetsPoints.isNotEmpty()) {                        // objets sans modele : un point de leur couleur (cache par le decor)
            val fb: FloatBuffer = ByteBuffer.allocateDirect(12).order(ByteOrder.nativeOrder()).asFloatBuffer()
            GLES20.glUseProgram(progL); GLES20.glUniformMatrix4fv(lMvp, 1, false, mvp, 0)
            for (o in objetsPoints) {
                fb.clear(); fb.put(o[0]).put(o[1]).put(o[2] + 0.35f); fb.position(0)
                val cc = couleurObjet(o[3].toInt())
                GLES20.glVertexAttribPointer(lPos, 3, GLES20.GL_FLOAT, false, 12, fb); GLES20.glEnableVertexAttribArray(lPos)
                GLES20.glUniform1f(lTaille, 13f * echelle); GLES20.glUniform4f(lCoul, 1f, 1f, 1f, 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
                GLES20.glUniform1f(lTaille, 9f * echelle); GLES20.glUniform4f(lCoul, cc[0], cc[1], cc[2], 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
            }
        }
        val pi = if (kartsDessines) FloatArray(0) else etat.pions
        if (pi.size >= 4) {
            GLES20.glDisable(GLES20.GL_DEPTH_TEST)
            val fb: FloatBuffer = ByteBuffer.allocateDirect(12).order(ByteOrder.nativeOrder()).asFloatBuffer()
            for (k in 0 until pi.size / 4) {
                fb.clear(); fb.put(pi[k * 4]).put(pi[k * 4 + 1]).put(pi[k * 4 + 2] + 1.2f); fb.position(0)
                val c = pi[k * 4 + 3].toRawBits()
                GLES20.glVertexAttribPointer(lPos, 3, GLES20.GL_FLOAT, false, 12, fb); GLES20.glEnableVertexAttribArray(lPos)
                GLES20.glUniform1f(lTaille, 22f * echelle); GLES20.glUniform4f(lCoul, 1f, 1f, 1f, 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
                GLES20.glUniform1f(lTaille, 16f * echelle)
                GLES20.glUniform4f(lCoul, (c shr 16 and 255) / 255f, (c shr 8 and 255) / 255f, (c and 255) / 255f, 1f); GLES20.glDrawArrays(GLES20.GL_POINTS, 0, 1)
            }
            GLES20.glEnable(GLES20.GL_DEPTH_TEST)
        }
        val rm = raymanMod
        if (etat.montrerRayman && dessinerRaymanAnime(mvp, c0)) {
            // (build 272 : Rayman anime d'apres sa pose en memoire)
        } else if (etat.montrerRayman && (linkFait || dessinerLinkWW(mvp, c0))) {
            // (build 338 : Link de Wind Waker, son vrai modele anime)
        } else if (etat.montrerRayman && rm != null) {          // build 269 : Rayman en 3D, tourne vers ou il va
            GLES20.glUseProgram(progM)
            GLES20.glUniformMatrix4fv(GLES20.glGetUniformLocation(progM, "uMvp"), 1, false, mvp, 0)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0); GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texRayman)
            GLES20.glUniform1i(GLES20.glGetUniformLocation(progM, "uTex"), 0)
            val mp = GLES20.glGetAttribLocation(progM, "aPos"); val mu = GLES20.glGetAttribLocation(progM, "aUv"); val mn = GLES20.glGetAttribLocation(progM, "aN")
            val b = rm.mod
            b.position(0); GLES20.glVertexAttribPointer(mp, 3, GLES20.GL_FLOAT, false, 32, b)
            b.position(3); GLES20.glVertexAttribPointer(mu, 2, GLES20.GL_FLOAT, false, 32, b)
            b.position(5); GLES20.glVertexAttribPointer(mn, 3, GLES20.GL_FLOAT, false, 32, b); b.position(0)
            GLES20.glEnableVertexAttribArray(mp); GLES20.glEnableVertexAttribArray(mu); GLES20.glEnableVertexAttribArray(mn)
            GLES20.glUniform3f(GLES20.glGetUniformLocation(progM, "uPos"), c0[0], c0[1], c0[2])
            GLES20.glUniform1f(GLES20.glGetUniformLocation(progM, "uAng"), cap + (Math.PI / 2).toFloat())   // (le modele regarde vers -y)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLES, 0, rm.n)
            GLES20.glDisableVertexAttribArray(mp); GLES20.glDisableVertexAttribArray(mu); GLES20.glDisableVertexAttribArray(mn)
            GLES20.glUseProgram(progL)
        } else if (etat.montrerRayman && !kartsDessines) {
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
fun DessinVue3DGL(scene: SceneR3, cible: FloatArray, camera: FloatArray?, chemin: List<FloatArray>, montrerRayman: Boolean = true,
                  pions: FloatArray? = null, distance: Float? = null, direct: (() -> FloatArray?)? = null,
                  pose: (() -> PoseRayman?)? = null, karts: List<KartVue>? = null, linkWW: FloatArray? = null,
                  capPerso: Float = Float.NaN, poseWW: (() -> FloatArray?)? = null, camJeu: (() -> FloatArray?)? = null,
                  ecouteDirect: (() -> Unit)? = null, rejeu: ((Etat3D) -> Unit)? = null) {
    // build 242 : un etat neuf par niveau ; sans jeu en cours, la vue de depart cadre tout le niveau
    val etat = remember(scene) { Etat3D().also { e -> if (!montrerRayman) { e.distBase = scene.rayon * 1.7f; e.dist = e.distBase; e.tangage = 0.75f } } }
    // build 269 : en direct, la position vient de la lecture rapide (la valeur passee ici, plus lente, ne sert qu'au depart)
    if (direct == null || !etat.direct) etat.cible = cible
    etat.camera = camera; etat.montrerRayman = montrerRayman
    if (pions != null) etat.pions = pions
    if (karts != null) etat.karts = karts
    etat.linkWW = linkWW; etat.capPerso = capPerso; etat.aCamJeu = camJeu != null
    etat.rejeu = rejeu?.let { r -> { r(etat) } }
    // build 345 : l'ecoute du direct (reseau), en continu tant que la vue est affichee
    if (ecouteDirect != null) androidx.compose.runtime.LaunchedEffect(scene) {
        try { while (true) { kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { try { ecouteDirect() } catch (e: Exception) { } }; kotlinx.coroutines.delay(1) } }
        finally { etat.rejeu = null }
    }
    // build 340 : pose de Link et camera du jeu, lues en direct (~20 fois par seconde chacune, a part de l'ecran)
    if (poseWW != null || camJeu != null) androidx.compose.runtime.LaunchedEffect(scene) {
        try {
            while (true) {
                kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                    if (!etat.directRejeu) {                         // (build 345 : sans direct seulement)
                        try { poseWW?.invoke()?.let { etat.poseWW = it; etat.tPoseWW = System.nanoTime() } } catch (e: Exception) { }
                        try { if (etat.mode == 4) camJeu?.invoke()?.let { etat.camJeu = it; etat.tCamJeu = System.nanoTime() } } catch (e: Exception) { }
                    }
                }
                kotlinx.coroutines.delay(15)
            }
        } finally { etat.poseWW = null }
    }
    if (direct != null) androidx.compose.runtime.LaunchedEffect(scene) {
        etat.direct = true
        try {
            while (true) {
                val p = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { try { direct() } catch (e: Exception) { null } }
                if (p != null && p.all { it.isFinite() }) { etat.cible = p }
                kotlinx.coroutines.delay(30)
            }
        } finally { etat.direct = false }
    }
    // build 272 : la pose de Rayman (squelette), lue a part : ~10 blocs par pose, le moteur glisse d'une pose a l'autre
    if (pose != null) androidx.compose.runtime.LaunchedEffect(scene) {
        try {
            while (true) {
                val p = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { try { pose() } catch (e: Exception) { null } }
                if (p != null) etat.pose = p
                kotlinx.coroutines.delay(if (p != null) 5 else 1000)
            }
        } finally { etat.pose = null }
    }
    if (distance != null) remember(scene) { etat.distBase = distance; etat.dist = distance; 0 }
    etat.chemin = FloatArray(chemin.size * 3).also { a -> for ((k, p) in chemin.withIndex()) { a[k * 3] = p[0]; a[k * 3 + 1] = p[1]; a[k * 3 + 2] = p[2] } }
    var plein by remember(scene) { androidx.compose.runtime.mutableStateOf(false) }
    var sansAth by remember(scene) { androidx.compose.runtime.mutableStateOf(false) }     // build 332 : plein ecran sans boutons
    var modeVue by remember(scene) { androidx.compose.runtime.mutableStateOf(etat.mode) }   // (pour l'aide des gestes)
    androidx.compose.runtime.LaunchedEffect(etat) { while (true) { kotlinx.coroutines.delay(300); if (modeVue != etat.mode) modeVue = etat.mode } }
    // build 247 : toucher un objet -> la camera y vole et son nom s'affiche ; double toucher ailleurs -> zoom
    val scope = androidx.compose.runtime.rememberCoroutineScope()
    var etiquette by remember(scene) { androidx.compose.runtime.mutableStateOf<String?>(null) }
    val toucher: (Float, Float, Boolean) -> Unit = { x, y, double ->
        val o = etat.objetSous(scene.objets, x, y)
        if (o != null) { etiquette = nomObjet(scene, o); allerA(scope, etat, o) }
        else if (double) etat.voler(scope, d = (etat.dist * 0.5f).coerceAtLeast(4f))
    }
    androidx.compose.runtime.LaunchedEffect(etiquette) { if (etiquette != null) { kotlinx.coroutines.delay(3500); etiquette = null } }
    @Composable fun Etiquette(m: Modifier) { val e = etiquette ?: return
        Text(e, color = Color.White, fontSize = 13.sp, fontWeight = androidx.compose.ui.text.font.FontWeight.Bold,
            modifier = m.background(Color(0xCC1A1530), RoundedCornerShape(10.dp)).padding(horizontal = 10.dp, vertical = 6.dp)) }
    Box(Modifier.fillMaxWidth().aspectRatio(1.1f)) {
        if (!plein) VueGL(scene, etat, Modifier.fillMaxSize(), toucher)
        Row(Modifier.align(androidx.compose.ui.Alignment.TopEnd).padding(8.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            PetitBouton("⟲") { etat.voler(scope, vers = etat.cible, d = etat.distBase, tg = if (etat.montrerRayman) 0.5f else 0.75f) }
            PetitBouton("⛶ Plein écran") { plein = true }
        }
        Etiquette(Modifier.align(androidx.compose.ui.Alignment.TopStart).padding(8.dp))
        BoutonsZoom(etat, Modifier.align(androidx.compose.ui.Alignment.BottomEnd).padding(8.dp))
        BoutonsVue(scope, etat, Modifier.align(androidx.compose.ui.Alignment.BottomStart).padding(8.dp))
    }
    ChoixObjets(scene, etat, Modifier.padding(top = 6.dp)) { o -> etiquette = nomObjet(scene, o); allerA(scope, etat, o) }
    val ctxBug = androidx.compose.ui.platform.LocalContext.current
    if (plein) androidx.compose.ui.window.Dialog({ plein = false }, androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(Color(0xFF6B80AE))) {
            VueGL(scene, etat, Modifier.fillMaxSize(), toucher)
            // build 332 : sans ATH : seule la vue reste (un petit oeil discret, en haut a droite, ramene les boutons)
            if (sansAth) Text("👁", fontSize = 18.sp, modifier = Modifier.align(androidx.compose.ui.Alignment.TopEnd).padding(top = 40.dp, end = 12.dp)
                .background(Color(0x33000000), RoundedCornerShape(16.dp)).pointerInput(Unit) { detectTapGestures { sansAth = false } }.padding(horizontal = 10.dp, vertical = 6.dp))
            else Box(Modifier.fillMaxSize()) {
                Row(Modifier.align(androidx.compose.ui.Alignment.TopEnd).padding(top = 40.dp, end = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    PetitBouton("🐞 Bug") { journalBug(ctxBug, scene, etat) }
                    PetitBouton("⟲ Recentrer") { if (etat.mode == 5) etat.mode = 0; etat.voler(scope, vers = etat.cible, d = etat.distBase, tg = if (etat.montrerRayman) 0.5f else 0.75f) }
                    PetitBouton("👁 Sans ATH") { sansAth = true }
                    PetitBouton("✕") { plein = false }
                }
                Etiquette(Modifier.align(androidx.compose.ui.Alignment.TopCenter).padding(top = 90.dp))
                if (scene.dossier.name == "cartes-mk") InterfaceMK(Modifier.fillMaxSize().padding(top = 96.dp, bottom = 120.dp, start = 12.dp, end = 12.dp))
                BoutonsZoom(etat, Modifier.align(androidx.compose.ui.Alignment.CenterEnd).padding(end = 12.dp))
                BoutonsVue(scope, etat, Modifier.align(androidx.compose.ui.Alignment.CenterStart).padding(start = 12.dp))
                ChoixObjets(scene, etat, Modifier.align(androidx.compose.ui.Alignment.BottomCenter).padding(bottom = 70.dp, start = 12.dp, end = 12.dp)) { o ->
                    etiquette = nomObjet(scene, o); allerA(scope, etat, o) }
                Text(if (modeVue == 5) "Visite · 1 doigt : regarder · 2 doigts : écarter pour avancer, glisser pour se décaler, monter, descendre · + / − : avancer, reculer"
                    else "1 doigt : tourner · 2 doigts : déplacer, zoomer, pivoter · toucher un objet : y aller · double toucher : zoom", color = Color.White, fontSize = 11.sp,
                    modifier = Modifier.align(androidx.compose.ui.Alignment.BottomCenter).padding(bottom = 28.dp, start = 12.dp, end = 12.dp).background(Color(0x88000000), RoundedCornerShape(10.dp)).padding(horizontal = 10.dp, vertical = 6.dp))
            }
        }
    }
}

/** l'interface du jeu sur la vue 3D (Mario Kart) : tour, place, les objets de ton equipe (pilote et passager) et le temps,
 *  comme a l'ecran du jeu ; relue 10 fois par seconde dans la course que lit l'ecran Mario Kart */
@Composable
private fun InterfaceMK(modifier: Modifier) {
    val e by androidx.compose.runtime.produceState<JeuMKDD.Etat?>(JeuMKDD.courseHud) {
        while (true) { value = JeuMKDD.courseHud; kotlinx.coroutines.delay(100) }
    }
    val c = e ?: return
    if (!c.enCourse) return
    val moi = c.karts.firstOrNull { it.n == 0 }
    val ombre = androidx.compose.ui.graphics.Shadow(Color(0xAA000000), Offset(2f, 3f), 4f)
    Box(modifier) {
        // tour (en haut a gauche, comme dans le jeu)
        if (c.tours > 0) Text("TOUR ${c.tour.coerceIn(1, c.tours)}/${c.tours}", color = Color.White, fontSize = 22.sp,
            fontWeight = androidx.compose.ui.text.font.FontWeight.Black, fontStyle = androidx.compose.ui.text.font.FontStyle.Italic,
            style = androidx.compose.ui.text.TextStyle(shadow = ombre), modifier = Modifier.align(androidx.compose.ui.Alignment.TopStart))
        // temps (en haut a droite)
        val chrono = if (c.fini && c.total in 1 until JeuMKDD.PAS_DE_TEMPS) chronoMK(c.total)
                     else c.tempsTours.lastOrNull()?.let { "tour préc. " + chronoMK(it) }
        if (chrono != null) Text(chrono, color = Color(0xFFFFE066), fontSize = 18.sp,
            fontWeight = androidx.compose.ui.text.font.FontWeight.Bold, style = androidx.compose.ui.text.TextStyle(shadow = ombre),
            modifier = Modifier.align(androidx.compose.ui.Alignment.TopEnd))
        // les deux objets (pilote, passager), en haut au milieu
        if (moi != null) Row(Modifier.align(androidx.compose.ui.Alignment.TopCenter).padding(top = 30.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (o in listOf(moi.objet1, moi.objet2)) {
                val ob = JeuMKDD.objet(o)
                Box(Modifier.size(54.dp).background(Color(0x99101030), RoundedCornerShape(12.dp))
                    .border(2.dp, Color(0xCCFFFFFF), RoundedCornerShape(12.dp)), contentAlignment = androidx.compose.ui.Alignment.Center) {
                    if (ob != null) Text(ob.first, fontSize = 30.sp)
                }
            }
        }
        // la place (en bas a droite, en grand)
        if (c.place in 1..8) {
            val suf = if (c.place == 1) "er" else "e"
            val coul = when (c.place) { 1 -> Color(0xFFFFD84A); 2 -> Color(0xFFDDE3F0); 3 -> Color(0xFFE0A060); else -> Color(0xFF8FD3FF) }
            Text("${c.place}$suf", color = coul, fontSize = 52.sp, fontWeight = androidx.compose.ui.text.font.FontWeight.Black,
                fontStyle = androidx.compose.ui.text.font.FontStyle.Italic, style = androidx.compose.ui.text.TextStyle(shadow = ombre),
                modifier = Modifier.align(androidx.compose.ui.Alignment.BottomEnd))
        }
    }
}
/** temps de course (millisecondes) : 1'23"456 */
internal fun chronoMK(ms: Int): String = "%d'%02d\"%03d".format(ms / 60000, ms / 1000 % 60, ms % 1000)

/** journal de bug de la vue 3D : capture de la prochaine image, en-tete (appli, scene, camera, packs), lignes du journal ;
 *  le .zip va dans Telechargements/GC Remote, puis le partage s'ouvre (pour me l'envoyer) */
internal fun journalBug(ctx: android.content.Context, scene: SceneR3, etat: Etat3D) {
    android.widget.Toast.makeText(ctx, "Journal de bug : enregistrement…", android.widget.Toast.LENGTH_SHORT).show()
    val principal = android.os.Handler(android.os.Looper.getMainLooper())
    etat.captureCb = { bmp ->
        Thread {
            val d = scene.dossier
            val packs = (d.list() ?: emptyArray()).filter { it.endsWith(".txt") }.groupBy { it.substringBefore('_', it) + "_" + it.removePrefix(it.substringBefore('_', it) + "_").substringBefore('_') }
                .map { (k, v) -> "$k x${v.size}" }.sorted().joinToString(", ")
            val e = StringBuilder()
            e.append("GC Remote ").append(BuildConfig.VERSION_NAME).append(" - journal de bug de la vue 3D - ")
                .append(java.text.SimpleDateFormat("dd/MM/yyyy HH:mm:ss", java.util.Locale.FRANCE).format(java.util.Date())).append('\n')
            e.append("scene : ").append(d.name).append(" / ").append(scene.niveau).append(" (").append(scene.groupes.size).append(" groupes, ").append(scene.objets.size).append(" objets)\n")
            e.append("camera : lacet ").append(etat.lacet).append(" tangage ").append(etat.tangage).append(" distance ").append(etat.dist)
                .append(" cible ").append(Journal3D.v(*etat.cible)).append(" decalage ").append(Journal3D.v(*etat.decal)).append(" direct ").append(etat.direct).append('\n')
            e.append("Mario Kart : direct de Pulse ").append(JeuMKDD.direct()).append(", karts ").append(JeuMKDD.kbs.size).append('\n')
            e.append("packs (fichiers du dossier) : ").append(packs).append('\n')
            val u = Journal3D.enregistrer(ctx, bmp, e.toString())
            principal.post {
                if (u == null) { android.widget.Toast.makeText(ctx, "Journal de bug : enregistrement impossible", android.widget.Toast.LENGTH_LONG).show(); return@post }
                android.widget.Toast.makeText(ctx, "Journal de bug enregistré (Téléchargements/GC Remote)", android.widget.Toast.LENGTH_LONG).show()
                try {
                    ctx.startActivity(android.content.Intent.createChooser(android.content.Intent(android.content.Intent.ACTION_SEND).setType("application/zip")
                        .putExtra(android.content.Intent.EXTRA_STREAM, u).addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION), "Envoyer le journal de bug")
                        .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK))
                } catch (x: Exception) { }
            }
        }.start()
    }
    etat.redessiner()
}

/** build 249 : la sorte de pouvoir d'apres le nom de l'objet dans le jeu */
internal fun genrePouvoir(nom: String): String { val n = nom.lowercase(); return when {
    "rouade" in n || "roulade" in n || "tornado" in n || "whirl" in n -> "roulade"; "grappin" in n -> "grappin"
    "helico" in n -> "helico"; "force" in n || "fist" in n || "poing" in n -> "poing"; "snipe" in n -> "snipe"; else -> "" } }

/** build 247 : nom lisible d'un objet ("Cage 2 sur 4") */
private fun nomObjet(scene: SceneR3, o: Objet3D): String {
    val meme = scene.objets.filter { it.type == o.type }
    val n = meme.indexOf(o) + 1
    return when (o.type) { 'c' -> "Cage $n sur ${meme.size}"; 'f' -> "P'tit être $n sur ${meme.size}"; 'j' -> "Gemme jaune"
        'k' -> "Gemme rouge"; 'v' -> "Gemme verte"; 'r' -> "Lum rouge"; 'i' -> "Boîte à objets $n sur ${meme.size}"; 'd' -> "Ligne de départ"; 't' -> "Coffre $n sur ${meme.size}"
        'u' -> "Rubis " + o.nom; 'q' -> if (o.nom == "receptacle") "Réceptacle de cœur" else "Quart de cœur"
        'p' -> "Pouvoir" + when { "rouade" in o.nom.lowercase() -> " : poing-roulade"; "grappin" in o.nom.lowercase() -> " : grappin"
            "force" in o.nom.lowercase() -> " : poing d'acier"; "snipe" in o.nom.lowercase() -> " : lockjaw"; "helico" in o.nom.lowercase() -> " : hélico"; else -> "" }
        else -> "" }
}

/** build 247 : la camera vole jusqu'a l'objet et le regarde d'un peu au-dessus */
private fun allerA(scope: kotlinx.coroutines.CoroutineScope, etat: Etat3D, o: Objet3D) =
    etat.voler(scope, vers = floatArrayOf(o.x, o.y, if (o.type == 'c') o.z - 2f else o.z), d = if (o.type == 'c' || o.type == 'f') 22f else 16f, tg = 0.5f)

/** build 247 : points de vue tout faits (au sol comme Rayman, de biais, du dessus) */
@Composable
private fun BoutonsVue(scope: kotlinx.coroutines.CoroutineScope, etat: Etat3D, modifier: Modifier) {
    androidx.compose.foundation.layout.Column(modifier, verticalArrangement = Arrangement.spacedBy(6.dp)) {
        PetitBouton("🚶 Au sol") { etat.voler(scope, d = 12f, tg = 0.12f) }
        PetitBouton("↗ Biais") { etat.voler(scope, d = maxOf(etat.dist, 40f), tg = 0.6f) }
        PetitBouton("⬇ Dessus") { etat.voler(scope, d = maxOf(etat.dist, etat.distBase * 1.6f), tg = 1.5f) }
        // build 282 : suivre Rayman (position et direction) ; mode libre (la vue ne suit plus rien)
        var mode by remember(etat) { androidx.compose.runtime.mutableStateOf(etat.mode) }
        if (etat.montrerRayman) PetitBouton("🎯 Suivre", actif = mode == 1) {
            mode = if (mode == 1) 0 else 1; etat.mode = mode; etat.libre = null
            if (mode == 1) { etat.vol?.cancel(); etat.decal = floatArrayOf(0f, 0f, 0f); etat.voler(scope, d = minOf(etat.dist, etat.distBase), tg = 0.35f) }
        }
        // 3e personne : comme dans le jeu, juste derriere ton kart (Mario Kart)
        // (build 340 : dans tous les jeux en direct, derriere le personnage, tourne comme lui)
        if (etat.karts.isNotEmpty() || (etat.montrerRayman && (etat.direct || etat.pose != null || !etat.capPerso.isNaN() || etat.linkWW != null)) || mode == 3) PetitBouton("🎥 3e personne", actif = mode == 3) {
            mode = if (mode == 3) 0 else 3; etat.mode = mode; etat.libre = null
            if (mode == 3) { etat.vol?.cancel(); etat.decal = floatArrayOf(0f, 0f, 0f); etat.voler(scope, d = 7f, tg = 0.16f) }
        }
        // vue du jeu : la camera du jeu elle-meme (Mario Kart, avec le direct de Pulse)
        if (etat.karts.isNotEmpty() || etat.aCamJeu || mode == 4) PetitBouton("🎮 Vue du jeu", actif = mode == 4) {
            mode = if (mode == 4) 0 else 4; etat.mode = mode; etat.libre = null
            if (mode == 4) { etat.vol?.cancel(); etat.decal = floatArrayOf(0f, 0f, 0f) }
        }
        androidx.compose.runtime.LaunchedEffect(etat) { while (true) { kotlinx.coroutines.delay(300); if (mode != etat.mode) mode = etat.mode } }
        // build 332 : visite : premiere personne, sans point central (pour entrer dans les salles)
        PetitBouton("🧭 Visite", actif = mode == 5) {
            if (mode == 5) { mode = 0; etat.mode = 0; etat.recentrer() } else { etat.entrerVisite(); mode = 5 }
            etat.redessiner()
        }
        PetitBouton("✋ Libre", actif = mode == 2) {
            if (mode == 2) { mode = 0; etat.mode = 0; etat.libre = null; etat.decal = floatArrayOf(0f, 0f, 0f) }
            else {
                val c = etat.cible; val d = etat.decal
                etat.vol?.cancel(); etat.libre = floatArrayOf(c[0] + d[0], c[1] + d[1], c[2] + d[2]); etat.decal = floatArrayOf(0f, 0f, 0f)
                mode = 2; etat.mode = 2
            }
        }
    }
}

/** build 245 : afficher / cacher chaque sorte d'objet (avec le nombre dans ce niveau) */
@Composable
private fun ChoixObjets(scene: SceneR3, etat: Etat3D, modifier: Modifier, aller: (Objet3D) -> Unit = {}) {
    if (scene.objets.isEmpty()) return
    // build 247 : passer d'une cage a l'autre
    val cages = remember(scene) { scene.objets.filter { it.type == 'c' }.ifEmpty { scene.objets.filter { it.type == 'f' } } }
    var ic by remember(scene) { androidx.compose.runtime.mutableStateOf(-1) }
    val n = remember(scene) { scene.objets.groupingBy { it.type }.eachCount() }
    // build 256 : combien il en reste (les objets ramasses en jouant disparaissent)
    var version by remember(scene) { androidx.compose.runtime.mutableStateOf(scene.versionPris) }
    androidx.compose.runtime.LaunchedEffect(scene) { while (true) { kotlinx.coroutines.delay(500); version = scene.versionPris } }
    val reste = remember(scene, version) { scene.objets.filter { !it.pris }.groupingBy { it.type }.eachCount() }
    var voir by remember(scene) { androidx.compose.runtime.mutableStateOf(etat.voir) }
    androidx.compose.foundation.layout.Column(modifier, verticalArrangement = Arrangement.spacedBy(4.dp)) {
        if (cages.isNotEmpty()) Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
            val nomC = if (cages[0].type == 'c') "Cage" else "P'tit être"
            PetitBouton("◀") { ic = if (ic <= 0) cages.size - 1 else ic - 1; aller(cages[ic]) }
            Text(if (ic < 0) "$nomC : ${cages.size}" else "$nomC ${ic + 1} / ${cages.size}", color = Color(0xFFFFBD40), fontSize = 13.sp,
                fontWeight = androidx.compose.ui.text.font.FontWeight.Bold)
            PetitBouton("▶") { ic = (ic + 1) % cages.size; aller(cages[ic]) }
        }
        Row(Modifier.horizontalScroll(androidx.compose.foundation.rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for ((t, nom, coul) in listOf(Triple('c', "Cages", 0xFFFFBD40), Triple('f', "P'tits êtres", 0xFF9EF2FF), Triple('j', "Gemmes jaunes", 0xFFFFEB47), Triple('k', "Gemmes rouges", 0xFFFF3B6B), Triple('v', "Gemmes vertes", 0xFF3DDC84),
                    Triple('r', "Lums rouges", 0xFFFF4038), Triple('p', "Pouvoirs", 0xFF9E8CFF),
                    Triple('i', "Boîtes à objets", 0xFFFFD54A), Triple('d', "Départ", 0xFFFFFFFF), Triple('t', "Coffres", 0xFFE6A040), Triple('u', "Rubis", 0xFF4DFF73), Triple('q', "Quarts de cœur", 0xFFFF5A80))) {
                val k = n[t] ?: continue
                val actif = t in voir
                val r = reste[t] ?: 0
                Text(if (r == k) "● $nom $k" else "● $nom $r / $k", color = if (actif) Color(coul) else Color(0x99FFFFFF), fontSize = 12.sp,
                    modifier = Modifier.background(if (actif) Color(0xCC1A1530) else Color(0x66000000), RoundedCornerShape(12.dp))
                        .pointerInput(t) { detectTapGestures {
                            voir = if (t in voir) voir.replace(t.toString(), "") else voir + t
                            etat.voir = voir; etat.redessiner() } }
                        .padding(horizontal = 10.dp, vertical = 6.dp))
            }
        }
        if ((n['f'] ?: 0) > 0) Text("Lande : le jeu range ses p'tits êtres en cage comme les autres fantômes ; ils sont tous montrés.",
            color = Color(0x99FFFFFF), fontSize = 10.sp)
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
                            etat.vol?.cancel()
                            val job = scope.launch { while (true) { androidx.compose.runtime.withFrameNanos { }; etat.zoom(f); etat.redessiner() } }
                            tryAwaitRelease(); job.cancel()
                        })
                    }.padding(top = 6.dp))
        }
    }
}

@Composable
private fun PetitBouton(t: String, actif: Boolean = false, faire: () -> Unit) {
    Text(t, color = if (actif) Color.Black else Color.White, fontSize = 13.sp, modifier = Modifier.background(if (actif) Color(0xFFFFBD40) else Color(0x99000000), RoundedCornerShape(12.dp))
        .pointerInput(t) { detectTapGestures { faire() } }.padding(horizontal = 12.dp, vertical = 7.dp))
}

/** la surface OpenGL et les gestes : 1 doigt : tourner ; 2 doigts : pincer pour zoomer, glisser pour deplacer la vue,
 *  tourner les doigts pour pivoter ; double toucher : recentrer. Les gestes sont gardes (la page ne defile pas pendant
 *  qu'on manipule la vue). Build 242 : la surface est recreee quand on change de niveau (avant, l'ancien restait),
 *  vitesse proportionnelle a la taille de la vue, et elan quand on lache le doigt. */
@Composable
private fun VueGL(scene: SceneR3, etat: Etat3D, modifier: Modifier, toucher: (Float, Float, Boolean) -> Unit = { _, _, _ -> }) {
    androidx.compose.runtime.key(scene, QualiteR3.facteur) {
    val vueRef = remember { arrayOfNulls<GLSurfaceView>(1) }
    val scope = androidx.compose.runtime.rememberCoroutineScope()
    vueRef[0]?.requestRender()
    // build 245 : l'eau et la lave bougent : on redessine environ 30 fois par seconde tant que la vue est affichee
    // (build 358 : la vue est redessinee meme quand rien ne bouge, 4 fois par seconde : apres un defilement de la page ou un
    //  retour dans l'appli, la surface recreee restait vide jusqu'au prochain geste)
    val anime = scene.groupes.any { it.eau > 0 || it.defil[0] != 0f || it.defil[1] != 0f || it.vent || it.texSeq != null || it.coulSeq != null } ||
        scene.objets.any { it.type in "jkvpc" } || scene.animes != null || scene.mobiles != null
    androidx.compose.runtime.LaunchedEffect(Unit) {
        while (true) {
            if (etat.direct) androidx.compose.runtime.withFrameNanos { } else kotlinx.coroutines.delay(if (anime || etat.montrerRayman) 33 else 250)    // (build 269 : en direct, chaque image)
            vueRef[0]?.requestRender()
        }
    }
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
                    val bas = awaitFirstDown(requireUnconsumed = false)
                    elan?.cancel(); etat.vol?.cancel()
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
                            if (etat.mode == 5) {
                                // build 332 : visite : 1 doigt : regarder autour (on tire le decor) ; 2 doigts : pincer pour
                                // avancer / reculer, glisser pour se decaler (cote, haut, bas), tourner les doigts pour pivoter
                                if (pts.size == 1) {
                                    val dx = pts[0].x - av[0].x; val dy = pts[0].y - av[0].y
                                    bouge += Math.abs(dx) + Math.abs(dy)
                                    val dl = dx / largeur * 2.4f; val dtg = -dy / hauteur * 1.8f
                                    etat.lacet += dl; etat.tangage = (etat.tangage + dtg).coerceIn(-1.45f, 1.45f)
                                    vL = vL * 0.4f + dl / dt * 0.6f; vT = vT * 0.4f + dtg / dt * 0.6f; vDx = 0f; vDy = 0f
                                } else if (pts.size >= 2) {
                                    val c1 = Offset((av[0].x + av[1].x) / 2, (av[0].y + av[1].y) / 2); val c2 = Offset((pts[0].x + pts[1].x) / 2, (pts[0].y + pts[1].y) / 2)
                                    val d1 = (av[0] - av[1]).getDistance().coerceAtLeast(1f); val d2 = (pts[0] - pts[1]).getDistance().coerceAtLeast(1f)
                                    etat.avancer((d2 - d1) / hauteur * 40f)
                                    val a1 = Math.atan2((av[1].y - av[0].y).toDouble(), (av[1].x - av[0].x).toDouble())
                                    val a2 = Math.atan2((pts[1].y - pts[0].y).toDouble(), (pts[1].x - pts[0].x).toDouble())
                                    var da = a2 - a1; if (da > Math.PI) da -= 2 * Math.PI; if (da < -Math.PI) da += 2 * Math.PI
                                    etat.lacet += da.toFloat()
                                    etat.glisser(-(c2.x - c1.x) / hauteur * 25f, (c2.y - c1.y) / hauteur * 25f)
                                    vL = 0f; vT = 0f; vDx = 0f; vDy = 0f
                                }
                            } else if (pts.size == 1) {
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
                                etat.lacet += vL * dt; etat.tangage = if (etat.mode == 5) (etat.tangage + vT * dt).coerceIn(-1.45f, 1.45f) else (etat.tangage + vT * dt).coerceIn(-0.25f, 1.55f)
                                val d = etat.decal; etat.decal = floatArrayOf(d[0] + vDx * dt, d[1] + vDy * dt, d[2])
                                val f = Math.exp(-5.0 * dt).toFloat(); vL *= f; vT *= f; vDx *= f; vDy *= f
                                vueRef[0]?.requestRender()
                            }
                        }
                    }
                    // double toucher (deux touchers brefs sans bouger) : recentrer
                    val fin = System.currentTimeMillis()
                    if (doigts == 1 && bouge < 24f && fin - t0 < 300) {
                        val dbl = fin - dernierTap < 350
                        toucher(bas.position.x, bas.position.y, dbl)
                        dernierTap = if (dbl) 0L else fin
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
