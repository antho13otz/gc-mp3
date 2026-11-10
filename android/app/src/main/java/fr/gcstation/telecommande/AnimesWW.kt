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
    /** build 356 : pour le direct des objets : la peau de chaque modele (comme celle de Link) et sa signature dans le jeu
     *  (taille du fichier du modele, nombre d'os) ; pack : <lieu>_vif.r3p */
    class Peau(val taille: Long, val nj: Int, val j: ByteArray, val w: FloatArray, val l: FloatArray)
    var peaux: List<Peau?> = emptyList()
    /** signature (taille shl 16 or os) -> numero du modele */
    var sigs: Map<Long, Int> = emptyMap()
    /** (build 370) modeles secondaires, animes en boucle par l'appli ; et les signatures a suivre en direct (les autres) */
    var locaux: Set<Int> = emptySet()
    val sigsDirect: Map<Long, Int> get() = if (locaux.isEmpty()) sigs else sigs.filterValues { it !in locaux }
    /** positions (repere de l'appli) de tous les sommets du modele m, d'apres les matrices du monde de ses os (jeu) */
    fun poser(m: Int, mat: FloatArray, o: java.nio.FloatBuffer): Boolean {
        val pe = peaux.getOrNull(m) ?: return false
        val n = modeles[m].total
        if (pe.j.size < 4 * n || mat.size < pe.nj * 12) return false
        o.position(0)
        // (build 367 : un sommet relie a une matrice aberrante partait a des metres (une ligne jusqu'au ciel) : s'il sort de
        //  la taille du modele autour de son 1er os, il reste sur cet os ; plus de la moitie ainsi : objet pas dessine)
        val rx = mat[3]; val ry = mat[7]; val rz = mat[11]
        val lim = modeles[m].rayon * 2f + 150f; val lim2 = lim * lim
        var hors = 0
        for (k in 0 until n) {
            var x = 0f; var y = 0f; var z = 0f
            for (q in 0 until 4) {
                val ji = pe.j[4 * k + q].toInt() and 255
                if (ji == 255 || ji >= pe.nj) continue
                val p = pe.w[4 * k + q]; val b = ji * 12; val c = 12 * k + 3 * q
                val lx = pe.l[c]; val ly = pe.l[c + 1]; val lz = pe.l[c + 2]
                x += p * (mat[b] * lx + mat[b + 1] * ly + mat[b + 2] * lz + mat[b + 3])
                y += p * (mat[b + 4] * lx + mat[b + 5] * ly + mat[b + 6] * lz + mat[b + 7])
                z += p * (mat[b + 8] * lx + mat[b + 9] * ly + mat[b + 10] * lz + mat[b + 11])
            }
            val dx = x - rx; val dy = y - ry; val dz = z - rz
            if (!(dx * dx + dy * dy + dz * dz <= lim2)) { x = rx; y = ry; z = rz; hors++ }
            o.put(x * 0.01f).put(-z * 0.01f).put(y * 0.01f)
        }
        o.position(0); return hors * 2 <= n
    }
    class Groupe(val texture: Int, val mode: Int, val env: Int, val du: Float, val dv: Float, val n: Int, val uvc: ByteBuffer, val debut: Int) {
        /** (build 356 : porte les clignements et couleurs du groupe ; sert aussi a charger ses textures) */
        val stub = SceneR3.Groupe(texture, mode != 0, ByteBuffer.allocateDirect(0), 0, mode, env)
    }
    class Modele(val groupes: List<Groupe>, val images: Int, val ips: Float, val total: Int, val pos: List<ByteBuffer>) {
        /** (build 367) rayon du modele dans sa 1re image (unites du jeu) : sert a reperer les sommets aberrants du direct */
        val rayon: Float by lazy {
            val f = pos.firstOrNull()?.duplicate()?.order(ByteOrder.nativeOrder())?.asFloatBuffer() ?: return@lazy 1000f
            if (total == 0) return@lazy 1000f
            var cx = 0f; var cy = 0f; var cz = 0f
            for (k in 0 until total) { cx += f.get(3 * k); cy += f.get(3 * k + 1); cz += f.get(3 * k + 2) }
            cx /= total; cy /= total; cz /= total
            var r = 0f
            for (k in 0 until total) { val a = f.get(3 * k) - cx; val b = f.get(3 * k + 1) - cy; val c = f.get(3 * k + 2) - cz; r = maxOf(r, a * a + b * b + c * c) }
            Math.sqrt(r.toDouble()).toFloat() * 100f
        }
    }
    class Copie(val modele: Int, val mobile: Int, val phase: Float, val mat: FloatArray)

    /** des groupes « vides » pour que le moteur charge leurs textures avec celles du lieu */
    val groupesTex: List<SceneR3.Groupe> by lazy { modeles.flatMap { m -> m.groupes.map { it.stub } } }

    /** build 356 : <lieu>_vif.r3p : "WWV1", nombre de modeles ; par modele : taille du fichier dans le jeu, nombre d'os,
     *  longueur, puis pour chaque sommet 4 os (octets), 4 poids, 4 positions locales (unites du jeu) */
    fun lirePeaux(f: java.io.File) {
        if (!f.exists()) return
        try {
            val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
            if (b.int != 0x31565757) return                                   // "WWV1"
            val n = b.int; val ps = ArrayList<Peau?>(); val sg = HashMap<Long, Int>()
            for (m in 0 until n) {
                val taille = b.int.toLong() and 0xFFFFFFFFL; val nj = b.int; val lg = b.int
                val nv = lg / 68
                if (lg <= 0 || m >= modeles.size || nv != modeles[m].total) { b.position(b.position() + lg); ps.add(null); continue }
                val j = ByteArray(4 * nv); val w = FloatArray(4 * nv); val l = FloatArray(12 * nv)
                for (k in 0 until nv) { b.get(j, 4 * k, 4); for (q in 0 until 4) w[4 * k + q] = b.float; for (q in 0 until 12) l[12 * k + q] = b.float }
                ps.add(Peau(taille, nj, j, w, l))
                if (taille > 0) sg.putIfAbsent((taille shl 16) or nj.toLong(), m)
            }
            peaux = ps; sigs = sg
        } catch (e: Throwable) { }
    }

    /** build 458 : peaux de la bibliotheque d'equipement, pour les modeles ajoutes a partir de debut (les signatures du lieu
     *  restent prioritaires) */
    fun ajouterPeaux(f: java.io.File, debut: Int) {
        if (!f.exists()) return
        try {
            val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
            if (b.int != 0x31565757) return
            val n = b.int; val ps = ArrayList<Peau?>(peaux); val sg = HashMap(sigs)
            while (ps.size < debut) ps.add(null)
            for (k in 0 until n) {
                val m = debut + k
                val taille = b.int.toLong() and 0xFFFFFFFFL; val nj = b.int; val lg = b.int
                val nv = lg / 68
                if (lg <= 0 || m >= modeles.size || nv != modeles[m].total) { b.position(b.position() + lg); ps.add(null); continue }
                val j = ByteArray(4 * nv); val w = FloatArray(4 * nv); val l = FloatArray(12 * nv)
                for (q in 0 until nv) { b.get(j, 4 * q, 4); for (r in 0 until 4) w[4 * q + r] = b.float; for (r in 0 until 12) l[12 * q + r] = b.float }
                ps.add(Peau(taille, nj, j, w, l))
                if (taille > 0) sg.putIfAbsent((taille shl 16) or nj.toLong(), m)
            }
            peaux = ps; sigs = sg
        } catch (e: Throwable) { }
    }
    companion object {
        /** build 458 : la bibliotheque d'equipement de Link (pack : ww_equip_ani.r3a, ww_equip_vif.r3p, ww_equip_tNNN.png,
         *  fabriquee depuis Link.arc de la copie du jeu du proprietaire) : ses modeles sont ajoutes a ceux de chaque lieu
         *  (apres eux : les numeros du lieu ne changent pas), leurs textures sont numerotees a partir de 10000 */
        const val TEX_EQUIP = 10000
        private fun modelesDe(b: ByteBuffer): List<Modele> {
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
            return ms
        }
        private fun equipement(d: java.io.File): List<Modele> = try {
            val f = java.io.File(d, "ww_equip_ani.r3a")
            if (!f.exists()) emptyList() else { val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x31415757) emptyList() else modelesDe(b) }
        } catch (e: Throwable) { emptyList() }
        fun lire(d: java.io.File, nv: String): AnimesWW? = try {
            val f = java.io.File(d, nv + "_ani.r3a")
            val eq = if (nv.startsWith("ww_")) emptyList() else equipement(d)
            if (!f.exists()) (if (eq.isEmpty()) null else AnimesWW(eq, emptyList()).also { it.lirePeaux(java.io.File(d, "ww_equip_vif.r3p")) }) else {
                val b = ByteBuffer.wrap(f.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x31415757) null else {                         // "WWA1"
                    val ms = ArrayList<Modele>(modelesDe(b)); val nLieu = ms.size
                    val nc = b.int; val cs = ArrayList<Copie>()
                    for (k in 0 until nc) {
                        val m = b.int; val mo = b.int; val ph = b.float
                        val mat = FloatArray(16) { b.float }
                        if (m in ms.indices) cs.add(Copie(m, mo, ph, mat))
                    }
                    // build 356 : <lieu>_ani.txt : « modele t|c groupe ips n valeurs... » (clignements, couleurs)
                    java.io.File(d, nv + "_ani.txt").takeIf { it.exists() }?.readLines()?.forEach { li -> val p = li.trim().split(' ')
                        val m = ms.getOrNull(p.getOrNull(0)?.toIntOrNull() ?: return@forEach) ?: return@forEach
                        SceneR3.lireSuite(p, 1) { m.groupes.getOrNull(it)?.stub } }
                    // (build 356 : les modeles sans copie placee servent au direct ; le lieu en a s'il y a des modeles)
                    ms.addAll(eq)                                              // (build 458 : equipement de Link, apres ceux du lieu)
                    if (ms.isEmpty()) null else AnimesWW(ms, cs).also { it.lirePeaux(java.io.File(d, nv + "_vif.r3p"))
                        if (eq.isNotEmpty()) it.ajouterPeaux(java.io.File(d, "ww_equip_vif.r3p"), nLieu)
                        // (build 370 : <lieu>_local.txt : modeles secondaires (mouettes, animaux, bois flottant, palmiers) :
                        //  animes en boucle ici, jamais demandes au direct)
                        it.locaux = try { java.io.File(d, nv + "_local.txt").takeIf { f -> f.exists() }?.readText()?.trim()?.split(' ')
                            ?.mapNotNull { x -> x.toIntOrNull() }?.toSet() ?: emptySet() } catch (e: Exception) { emptySet() } }
                }
            }
        } catch (e: Throwable) { null }
    }
}
