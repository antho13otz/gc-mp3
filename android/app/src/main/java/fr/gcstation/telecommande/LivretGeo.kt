package fr.gcstation.telecommande

// Appli : la forme de la notice en 3D (un livret de feuilles reliees a gauche), recalculee a chaque image.
// Feuille i : recto = page 2i+1, verso = page 2i+2 (la couverture est le recto de la 1re feuille). pos (0 a nombre
// de feuilles) : les feuilles d'indice < pos sont tournees a gauche ; une feuille en train de tourner se courbe (son
// bord libre suit avec du retard ; couverture et dos plus rigides). Les feuilles sont empilees (epaisseur : la tranche
// se voit). Reliure en x = 0, pages de hauteur 2 (y de -1 a 1), largeur 2 x ratio ; on lit depuis +z.
// Sommets : x, y, z, normale, u, v (8 flottants). Kotlin pur : verifiable a part.

object Livret {
    const val E = 0.0030f                                     // (ecart entre deux feuilles)
    const val PAPIER = -1                                     // (groupe sans image : tranche ou page absente)

    class Groupe(val premier: Int, val nombre: Int, val page: Int, val col: Int)
    class Maille(val v: FloatArray, val n: Int, val groupes: List<Groupe>)

    fun feuilles(n: Int) = maxOf(1, (n + 1) / 2)
    /** pages d'une feuille (0 : papier blanc) ; nombre impair de pages : la derniere est le dos (verso de la derniere feuille) */
    fun recto(n: Int, i: Int) = if (n % 2 == 1 && n > 1 && i == feuilles(n) - 1) 0 else 2 * i + 1
    fun verso(n: Int, i: Int) = if (n % 2 == 1 && n > 1 && i == feuilles(n) - 1) n else 2 * i + 2

    /** angle (radians) le long d'une feuille qui tourne : u de 0 (reliure) a 1 (bord libre), f : avancement (0 a 1) */
    fun angle(f: Float, u: Float, souple: Float): Float {
        val a = Math.PI.toFloat() * f
        return a - souple * kotlin.math.sin(a) * Math.pow(u.toDouble(), 1.6).toFloat()
    }

    private class Tampon {
        var v = FloatArray(8 * 4096); var n = 0
        val groupes = ArrayList<Groupe>()
        private var debut = 0; private var page = 0; private var col = 0
        fun commencer(p: Int, c: Int) { finir(); debut = n; page = p; col = c }
        fun finir() { if (n > debut) groupes.add(Groupe(debut, n - debut, page, col)); debut = n }
        fun s(x: Float, y: Float, z: Float, nx: Float, ny: Float, nz: Float, u: Float, t: Float) {
            if ((n + 1) * 8 > v.size) v = v.copyOf(v.size * 2)
            val k = n * 8
            v[k] = x; v[k + 1] = y; v[k + 2] = z; v[k + 3] = nx; v[k + 4] = ny; v[k + 5] = nz; v[k + 6] = u; v[k + 7] = t; n++
        }
    }

    /** n : nombre de pages ; pos : feuilles tournees (continu) ; ratio : largeur / hauteur d'une page */
    fun construire(n: Int, pos: Float, ratio: Float): Maille {
        val L = feuilles(n)
        val W = 2f * ratio
        val t = Tampon()
        val ep = E * 0.45f                                     // (demi-epaisseur d'une feuille)
        for (i in 0 until L) {
            val f = (pos - i).coerceIn(0f, 1f)
            val zr = (L - 1 - i) * E; val zl = i * E
            val z0 = zr + (zl - zr) * f + (if (f > 0f && f < 1f) E * 2f * kotlin.math.sin(Math.PI * f).toFloat() else 0f)
            val souple = if (i == 0 || i == L - 1) 0.18f else 0.6f
            val S = if (f > 0f && f < 1f) 28 else 1
            // ligne moyenne de la feuille (dans le plan xz) : points, et normale du recto
            val px = FloatArray(S + 1); val pz = FloatArray(S + 1); val nx = FloatArray(S + 1); val nz = FloatArray(S + 1)
            px[0] = 0f; pz[0] = z0
            for (j in 0..S) {
                val u = j.toFloat() / S
                val a = angle(f, u, souple)
                nx[j] = -kotlin.math.sin(a); nz[j] = kotlin.math.cos(a)
                if (j < S) {
                    val am = angle(f, (j + 0.5f) / S, souple)
                    px[j + 1] = px[j] + kotlin.math.cos(am) * W / S; pz[j + 1] = pz[j] + kotlin.math.sin(am) * W / S
                }
            }
            // recto (page 2i+1) puis verso (page 2i+2, image retournee : la reliure est a sa droite quand on la voit)
            for (cote in 0..1) {
                val pg = if (cote == 0) recto(n, i) else verso(n, i)
                val sg = if (cote == 0) 1f else -1f
                t.commencer(if (pg in 1..n) pg else PAPIER, 0xF4F1EA)
                for (j in 0 until S) {
                    val u0 = j.toFloat() / S; val u1 = (j + 1f) / S
                    val tu0 = if (cote == 0) u0 else 1f - u0; val tu1 = if (cote == 0) u1 else 1f - u1
                    val x0 = px[j] + nx[j] * ep * sg; val z0j = pz[j] + nz[j] * ep * sg
                    val x1 = px[j + 1] + nx[j + 1] * ep * sg; val z1j = pz[j + 1] + nz[j + 1] * ep * sg
                    val ax = nx[j] * sg; val az = nz[j] * sg; val bx = nx[j + 1] * sg; val bz = nz[j + 1] * sg
                    t.s(x0, 1f, z0j, ax, 0f, az, tu0, 0f); t.s(x0, -1f, z0j, ax, 0f, az, tu0, 1f); t.s(x1, -1f, z1j, bx, 0f, bz, tu1, 1f)
                    t.s(x0, 1f, z0j, ax, 0f, az, tu0, 0f); t.s(x1, -1f, z1j, bx, 0f, bz, tu1, 1f); t.s(x1, 1f, z1j, bx, 0f, bz, tu1, 0f)
                }
            }
            // tranches : haut, bas (le long de la feuille) et bord libre ; une feuille sur deux un peu plus sombre
            t.commencer(PAPIER, if (i % 2 == 0) 0xE6E1D6 else 0xD8D2C6)
            for (y in floatArrayOf(1f, -1f)) for (j in 0 until S) {
                val ax = px[j] + nx[j] * ep; val az = pz[j] + nz[j] * ep; val bx = px[j] - nx[j] * ep; val bz = pz[j] - nz[j] * ep
                val cx = px[j + 1] - nx[j + 1] * ep; val cz = pz[j + 1] - nz[j + 1] * ep; val dx = px[j + 1] + nx[j + 1] * ep; val dz = pz[j + 1] + nz[j + 1] * ep
                t.s(ax, y, az, 0f, y, 0f, 0f, 0f); t.s(bx, y, bz, 0f, y, 0f, 0f, 0f); t.s(cx, y, cz, 0f, y, 0f, 0f, 0f)
                t.s(ax, y, az, 0f, y, 0f, 0f, 0f); t.s(cx, y, cz, 0f, y, 0f, 0f, 0f); t.s(dx, y, dz, 0f, y, 0f, 0f, 0f)
            }
            run {
                val a = angle(f, 1f, souple); val ox = kotlin.math.cos(a); val oz = kotlin.math.sin(a)
                val ex = px[S]; val ez = pz[S]
                val fx = ex + nx[S] * ep; val fz = ez + nz[S] * ep; val gx = ex - nx[S] * ep; val gz = ez - nz[S] * ep
                t.s(fx, 1f, fz, ox, 0f, oz, 0f, 0f); t.s(gx, 1f, gz, ox, 0f, oz, 0f, 0f); t.s(gx, -1f, gz, ox, 0f, oz, 0f, 0f)
                t.s(fx, 1f, fz, ox, 0f, oz, 0f, 0f); t.s(gx, -1f, gz, ox, 0f, oz, 0f, 0f); t.s(fx, -1f, fz, ox, 0f, oz, 0f, 0f)
            }
        }
        // dos de la reliure (les agrafes : un trait sombre sur la tranche de gauche)
        t.commencer(PAPIER, 0x9A958C)
        val zb = (L - 1) * E + ep; val zh = -ep
        t.s(-0.002f, 1f, zh, -1f, 0f, 0f, 0f, 0f); t.s(-0.002f, -1f, zh, -1f, 0f, 0f, 0f, 0f); t.s(-0.002f, -1f, zb, -1f, 0f, 0f, 0f, 0f)
        t.s(-0.002f, 1f, zh, -1f, 0f, 0f, 0f, 0f); t.s(-0.002f, -1f, zb, -1f, 0f, 0f, 0f, 0f); t.s(-0.002f, 1f, zb, -1f, 0f, 0f, 0f, 0f)
        t.finir()
        return Maille(t.v, t.n, t.groupes)
    }

    /** centre a viser (x) et demi-largeur visible, selon l'ouverture : fermee, la page seule ; ouverte, les deux */
    fun centre(n: Int, pos: Float, ratio: Float): Float {
        val L = feuilles(n); val W = 2f * ratio
        return when {
            pos < 1f -> W / 2f * (1f - pos)
            pos > L - 1f -> -W / 2f * (pos - (L - 1f))
            else -> 0f
        }
    }
    fun demiLargeur(n: Int, pos: Float, ratio: Float): Float {
        val L = feuilles(n); val W = 2f * ratio
        val o = minOf(pos.coerceIn(0f, 1f), (L - pos).coerceIn(0f, 1f))
        return W / 2f * (1f + o)
    }
}
