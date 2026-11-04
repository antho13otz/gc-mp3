package fr.gcstation.telecommande

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** build 353 : Wind Waker, les objets animes par le jeu (arbres qui se balancent, mouettes qui battent des ailes,
 *  personnages, plantes, machines...). Pack : <lieu>_ani.r3a (petit-boutiste) :
 *   "WWA1", nombre de modeles ; par modele : nombre de groupes, d'images, images par seconde ; par groupe : texture,
 *   mode, repetition miroir, defilement u v, nombre de sommets, puis u v (2 flottants) et couleur (4 octets) par sommet ;
 *   puis chaque image : les positions de tous les groupes a la suite (3 flottants par sommet, repere de l'appli).
 *   Ensuite le nombre de copies ; par copie : modele, objet mobile suivi (-1 : aucun ; sinon numero de la ligne r/o/c du
 *   _mob.txt), decalage dans l'animation (0 a 1), matrice 4 x 4 par colonnes.
 *  Les images sont cuites d'apres les animations du jeu (.bck), 30 par seconde au plus : rejouees telles quelles. */
class AnimesWW(val modeles: List<Modele>, val copies: List<Copie>) {
    class Groupe(val texture: Int, val mode: Int, val env: Int, val du: Float, val dv: Float, val n: Int, val uvc: ByteBuffer, val debut: Int) {
        /** (build 355 : porte les clignements et couleurs du groupe ; sert aussi a charger ses textures) */
        val stub = SceneR3.Groupe(texture, mode != 0, ByteBuffer.allocateDirect(0), 0, mode, env)
    }
    class Modele(val groupes: List<Groupe>, val images: Int, val ips: Float, val total: Int, val pos: List<ByteBuffer>)
    class Copie(val modele: Int, val mobile: Int, val phase: Float, val mat: FloatArray)

    /** des groupes « vides » pour que le moteur charge leurs textures avec celles du lieu */
    val groupesTex: List<SceneR3.Groupe> by lazy { modeles.flatMap { m -> m.groupes.map { it.stub } } }

    companion object {
        fun lire(d: java.io.File, nv: String): AnimesWW? = try {
            val f = java.io.File(d, nv + "_ani.r3a")
            if (!f.exists()) null else {
                val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x31415757) null else {                         // "WWA1"
                    val nm = b.int; val ms = ArrayList<Modele>()
                    for (m in 0 until nm) {
                        val ng = b.int; val ni = b.int; val ips = b.float
                        val gs = ArrayList<Groupe>(); var tot = 0
                        for (g in 0 until ng) {
                            val tex = b.int; val md = b.int; val env = b.int; val du = b.float; val dv = b.float; val n = b.int
                            val bb = ByteBuffer.allocateDirect(n * 12).order(ByteOrder.nativeOrder())
                            val src = ByteArray(n * 12); b.get(src); bb.put(src); bb.position(0)
                            gs.add(Groupe(tex, md, env, du, dv, n, bb, tot)); tot += n
                        }
                        val pos = ArrayList<ByteBuffer>()
                        for (i in 0 until ni) {
                            val bb = ByteBuffer.allocateDirect(tot * 12).order(ByteOrder.nativeOrder())
                            val src = ByteArray(tot * 12); b.get(src); bb.put(src); bb.position(0); pos.add(bb)
                        }
                        ms.add(Modele(gs, ni, ips, tot, pos))
                    }
                    val nc = b.int; val cs = ArrayList<Copie>()
                    for (k in 0 until nc) {
                        val m = b.int; val mo = b.int; val ph = b.float
                        val mat = FloatArray(16) { b.float }
                        if (m in ms.indices) cs.add(Copie(m, mo, ph, mat))
                    }
                    // build 355 : <lieu>_ani.txt : « modele t|c groupe ips n valeurs... » (clignements, couleurs)
                    java.io.File(d, nv + "_ani.txt").takeIf { it.exists() }?.readLines()?.forEach { li -> val p = li.trim().split(' ')
                        val m = ms.getOrNull(p.getOrNull(0)?.toIntOrNull() ?: return@forEach) ?: return@forEach
                        SceneR3.lireSuite(p, 1) { m.groupes.getOrNull(it)?.stub } }
                    if (cs.isEmpty()) null else AnimesWW(ms, cs)
                }
            }
        } catch (e: Throwable) { null }
    }
}
