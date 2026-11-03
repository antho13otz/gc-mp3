package fr.gcstation.telecommande

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** build 332 : Mario Kart, les objets qui bougent (voitures, Thwomps, barres de feu, dirigeables, papillons, Nessie…).
 *  Pack : <circuit>_mob.r3v (les modeles, autour de leur point d'attache, memes textures que le circuit) et
 *  <circuit>_mob.txt, une ligne par trajet ou par objet :
 *   R <trajet> <ferme 0/1> x y z x y z ...                       points d'un trajet du jeu (repere de l'appli)
 *   r <groupe> <nb> x y z ax ay az <degres/s>                     tourne autour de l'axe passant par x y z
 *   o <groupe> <nb> x y z ax ay az <ampleur> <periode>            monte et descend le long de l'axe
 *   c <groupe> <nb> <trajet> <point de depart> <vitesse/s> <degres/s>   suit le trajet (ferme : en boucle ; sinon aller-retour)
 *  Les mouvements sont rejoues par l'appli (vitesses reglees d'apres le jeu), hors partie comme en partie. */
class ObjetsMobiles(val groupes: List<SceneR3.Groupe>, val objets: List<Objet>, private val trajets: Map<Int, Trajet>) {
    class Objet(val type: Char, val g0: Int, val nb: Int, val p: FloatArray)
    class Trajet(val pts: FloatArray, val ferme: Boolean) {
        val n = pts.size / 3
        /** distance cumulee au debut de chaque segment (et longueur totale en dernier) */
        val cum = FloatArray(n + 1).also { c ->
            val m = if (ferme) n else n - 1
            for (k in 0 until m) { val a = k * 3; val b = ((k + 1) % n) * 3
                c[k + 1] = c[k] + Math.sqrt(((pts[b] - pts[a]) * (pts[b] - pts[a]) + (pts[b + 1] - pts[a + 1]) * (pts[b + 1] - pts[a + 1]) + (pts[b + 2] - pts[a + 2]) * (pts[b + 2] - pts[a + 2])).toDouble()).toFloat() }
            for (k in m + 1..n) c[k] = c[m]
        }
        val longueur = cum[if (ferme) n else n - 1]
    }

    /** matrice (OpenGL, par colonnes) de l'objet au temps t (secondes) ; la partie 3 x 3 est toujours une rotation */
    fun matrice(o: Objet, t: Double, m: FloatArray) {
        val p = o.p
        when (o.type) {
            'r' -> {
                android.opengl.Matrix.setRotateM(m, 0, ((p[6] * t) % 360.0).toFloat(), p[3], p[4], p[5])
                m[12] = p[0]; m[13] = p[1]; m[14] = p[2]
            }
            'o' -> {
                val l = Math.sqrt((p[3] * p[3] + p[4] * p[4] + p[5] * p[5]).toDouble()).toFloat().coerceAtLeast(1e-6f)
                val k = (p[6] * (0.5 - 0.5 * Math.cos(2 * Math.PI * t / p[7].coerceAtLeast(0.1f)))).toFloat() / l
                android.opengl.Matrix.setIdentityM(m, 0)
                m[12] = p[0] + p[3] * k; m[13] = p[1] + p[4] * k; m[14] = p[2] + p[5] * k
            }
            else -> {
                val tr = trajets[p[0].toInt()]
                if (tr == null || tr.n < 2 || tr.longueur <= 0f) { android.opengl.Matrix.setIdentityM(m, 0); return }
                val d0 = tr.cum[p[1].toInt().coerceIn(0, tr.n - 1)]
                var d = ((d0 + p[2] * t) % (if (tr.ferme) tr.longueur.toDouble() else 2.0 * tr.longueur)).toFloat()
                var sens = 1f
                if (!tr.ferme && d > tr.longueur) { d = 2 * tr.longueur - d; sens = -1f }      // (trajet ouvert : aller-retour)
                val m1 = if (tr.ferme) tr.n else tr.n - 1
                var k = 0
                while (k < m1 - 1 && tr.cum[k + 1] < d) k++
                val a = k * 3; val b = ((k + 1) % tr.n) * 3
                val seg = (tr.cum[k + 1] - tr.cum[k]).coerceAtLeast(1e-6f); val f = ((d - tr.cum[k]) / seg).coerceIn(0f, 1f)
                val x = tr.pts[a] + (tr.pts[b] - tr.pts[a]) * f; val y = tr.pts[a + 1] + (tr.pts[b + 1] - tr.pts[a + 1]) * f; val z = tr.pts[a + 2] + (tr.pts[b + 2] - tr.pts[a + 2]) * f
                // l'avant (sens du deplacement) ; le modele regarde vers -y de l'appli (+z du jeu), le haut vers +z
                var fx = (tr.pts[b] - tr.pts[a]) * sens; var fy = (tr.pts[b + 1] - tr.pts[a + 1]) * sens; var fz = (tr.pts[b + 2] - tr.pts[a + 2]) * sens
                var l = Math.sqrt((fx * fx + fy * fy + fz * fz).toDouble()).toFloat()
                if (l < 1e-6f) { fx = 0f; fy = -1f; fz = 0f; l = 1f }
                fx /= l; fy /= l; fz /= l
                var rx = -fy; var ry = fx; var rz = 0f                 // (droite = haut x avant, haut = (0, 0, 1))
                val lr = Math.sqrt((rx * rx + ry * ry).toDouble()).toFloat()
                if (lr < 1e-4f) { rx = 1f; ry = 0f } else { rx /= lr; ry /= lr }
                val ux = fy * rz - fz * ry; val uy = fz * rx - fx * rz; val uz = fx * ry - fy * rx
                m[0] = rx; m[1] = ry; m[2] = rz; m[3] = 0f
                m[4] = -fx; m[5] = -fy; m[6] = -fz; m[7] = 0f
                m[8] = ux; m[9] = uy; m[10] = uz; m[11] = 0f
                m[12] = x; m[13] = y; m[14] = z; m[15] = 1f
                if (p[3] != 0f) { val r = FloatArray(16); val q = FloatArray(16)      // (tornade : tourne aussi sur elle-meme)
                    android.opengl.Matrix.setRotateM(r, 0, ((p[3] * t) % 360.0).toFloat(), 0f, 0f, 1f)
                    android.opengl.Matrix.multiplyMM(q, 0, m, 0, r, 0); System.arraycopy(q, 0, m, 0, 16) }
            }
        }
    }

    companion object {
        fun lire(d: java.io.File, nv: String): ObjetsMobiles? = try {
            val fm = java.io.File(d, nv + "_mob.r3v"); val ft = java.io.File(d, nv + "_mob.txt")
            if (!fm.exists() || !ft.exists()) null else {
                val b = ByteBuffer.wrap(fm.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x33563352) null else {                            // "R3V3"
                    val ng = b.int; val gs = ArrayList<SceneR3.Groupe>()
                    for (g in 0 until ng) {
                        val tex = b.int; val md = b.int; val env = b.int; val lm = b.int; val lk = b.float; val n = b.int
                        val bb = ByteBuffer.allocateDirect(n * 32).order(ByteOrder.nativeOrder())
                        val src = ByteArray(n * 32); b.get(src); bb.put(src); bb.position(0)
                        gs.add(SceneR3.Groupe(tex, md != 0, bb, n, md, env, lm, lk, 32))
                    }
                    val tr = HashMap<Int, Trajet>(); val ob = ArrayList<Objet>()
                    for (li in ft.readLines()) {
                        val p = li.trim().split(' ').filter { it.isNotEmpty() }
                        if (p.size < 3) continue
                        if (p[0] == "R") {
                            val v = p.drop(3).mapNotNull { it.toFloatOrNull() }
                            if (v.size >= 6) tr[p[1].toInt()] = Trajet(v.subList(0, v.size / 3 * 3).toFloatArray(), p[2] == "1")
                        } else if (p[0].length == 1 && p[0][0] in "roc") {
                            val g0 = p[1].toIntOrNull() ?: continue; val nb = p[2].toIntOrNull() ?: continue
                            val v = p.drop(3).map { it.toFloatOrNull() ?: 0f }.toFloatArray()
                            if (v.size >= (if (p[0] == "c") 4 else if (p[0] == "r") 7 else 8)) ob.add(Objet(p[0][0], g0, nb, v))
                        }
                    }
                    if (ob.isEmpty()) null else ObjetsMobiles(gs, ob, tr)
                }
            }
        } catch (e: Throwable) { null }
    }
}
