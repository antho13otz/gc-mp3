package fr.gcstation.telecommande

// build 486 : sorti de ZeldaWW3D.kt (code commun : la vue 3D de l'appli publique s'en sert)

/** build 338 : Link en 3D (pack Wind Waker : ww_link.ani et ww_link_tNNN.png), son modele du jeu et ses animations cuites
 *  en images (attente, marche, course, nage, nage sur place) : voir linkww.py. Les u v et couleurs sont communs a toutes
 *  les images ; seules les positions changent. */
class LinkAnime(val groupes: List<Groupe>, val anims: Map<String, Anim>) {
    class Groupe(val texture: Int, val mode: Int, val n: Int, val uvc: java.nio.ByteBuffer)
    class Anim(val images: Int, val ips: Float, val pos: Array<Array<java.nio.FloatBuffer>>)   // [image][groupe]
    companion object {
        fun lire(f: java.io.File): LinkAnime? = try {
            if (!f.exists()) null else {
                val b = java.nio.ByteBuffer.wrap(f.readBytes()).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x314C5757) null else {                         // "WWL1"
                    val ng = b.int; val na = b.int
                    val gs = ArrayList<Groupe>()
                    for (g in 0 until ng) {
                        val t = b.int; val m = b.int; val n = b.int
                        val bb = java.nio.ByteBuffer.allocateDirect(n * 12).order(java.nio.ByteOrder.nativeOrder())
                        val src = ByteArray(n * 12); b.get(src); bb.put(src); bb.position(0)
                        gs.add(Groupe(t, m, n, bb))
                    }
                    val an = HashMap<String, Anim>()
                    for (a in 0 until na) {
                        val nb = ByteArray(16); b.get(nb); val nom = String(nb).trimEnd('\u0000')
                        val ni = b.int; val ips = b.float
                        val im = Array(ni) { Array(ng) { g ->
                            val fb = java.nio.ByteBuffer.allocateDirect(gs[g].n * 12).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer()
                            for (k in 0 until gs[g].n * 3) fb.put(b.float)
                            fb.position(0); fb } }
                        an[nom] = Anim(ni, ips, im)
                    }
                    LinkAnime(gs, an)
                }
            }
        } catch (e: Throwable) { null }
    }
}

/** build 340 : la peau de Link (pack : ww_link.peau, voir linkww.py) : pour chaque sommet, jusqu'a 4 articulations, leurs
 *  poids et la position du sommet dans chacune. Avec les 42 matrices du monde lues dans le jeu (modele de Link +0x8C), la
 *  position de chaque sommet = somme des poids x matrice x position locale ; puis repere de l'appli (x, -z, y) / 100. */
class PeauLink(private val j: Array<ByteArray>, private val w: Array<FloatArray>, private val l: Array<FloatArray>, private val sortie: Array<java.nio.FloatBuffer>) {
    fun poser(g: Int, m: FloatArray): java.nio.FloatBuffer {
        val jj = j[g]; val ww = w[g]; val ll = l[g]; val o = sortie[g]; val n = jj.size / 4
        o.position(0)
        for (k in 0 until n) {
            var x = 0f; var y = 0f; var z = 0f
            for (q in 0 until 4) {
                val ji = jj[4 * k + q].toInt() and 255
                if (ji == 255 || ji >= 42) continue
                val p = ww[4 * k + q]; val b = ji * 12; val c = 12 * k + 3 * q
                val lx = ll[c]; val ly = ll[c + 1]; val lz = ll[c + 2]
                x += p * (m[b] * lx + m[b + 1] * ly + m[b + 2] * lz + m[b + 3])
                y += p * (m[b + 4] * lx + m[b + 5] * ly + m[b + 6] * lz + m[b + 7])
                z += p * (m[b + 8] * lx + m[b + 9] * ly + m[b + 10] * lz + m[b + 11])
            }
            o.put(x * 0.01f).put(-z * 0.01f).put(y * 0.01f)
        }
        o.position(0); return o
    }
    companion object {
        fun lire(f: java.io.File, la: LinkAnime): PeauLink? = try {
            if (!f.exists()) null else {
                val b = java.nio.ByteBuffer.wrap(f.readBytes()).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x31505757) null else {                         // "WWP1"
                    val ng = b.int
                    if (ng != la.groupes.size) null else {
                        val J = arrayOfNulls<ByteArray>(ng); val W = arrayOfNulls<FloatArray>(ng); val L = arrayOfNulls<FloatArray>(ng)
                        for (g in 0 until ng) {
                            val n = b.int
                            if (n != la.groupes[g].n) throw IllegalStateException()
                            val jj = ByteArray(4 * n); val ww = FloatArray(4 * n); val ll = FloatArray(12 * n)
                            for (k in 0 until n) {
                                b.get(jj, 4 * k, 4)
                                for (q in 0 until 4) ww[4 * k + q] = b.float
                                for (q in 0 until 12) ll[12 * k + q] = b.float
                            }
                            J[g] = jj; W[g] = ww; L[g] = ll
                        }
                        PeauLink(Array(ng) { J[it]!! }, Array(ng) { W[it]!! }, Array(ng) { L[it]!! },
                            Array(ng) { java.nio.ByteBuffer.allocateDirect(la.groupes[it].n * 12).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer() })
                    }
                }
            }
        } catch (e: Throwable) { null }
    }
}
