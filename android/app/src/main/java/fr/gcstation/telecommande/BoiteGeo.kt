package fr.gcstation.telecommande

// Appli : la geometrie de la boite 3D (boitier GameCube, disque, ombre), sans rien d'Android : verifiable a part.
// Boitier GameCube : 135 x 191 x 14 mm -> largeur 1,5 ; hauteur 2,12 ; epaisseur 0,156.

object Geo {
        const val X = 0.75f
        const val Y = 1.06f
        const val Z = 0.078f
        const val T = 0.024f                                      // (epaisseur du couvercle)
        const val R = 0.07f                                       // (coins arrondis)
        const val SP = 0.05f                                      // (jaquette complete : la tranche = 5 % au milieu)
        const val DR = 0.445f                                     // (disque de 8 cm)
        const val P_BAC = 0; const val P_COUV = 1; const val P_DISQUE = 2; const val P_SOL = 3
        const val M_COULEUR = 0; const val M_FULL = 1; const val M_AVANT = 2; const val M_DISQUE = 3; const val M_DESSOUS = 4; const val M_OMBRE = 5
    /** contour arrondi (sens trigo) : points {x, y, nx, ny} */
    fun contour(x: Float, y: Float, r: Float, n: Int = 10): List<FloatArray> {
            val l = ArrayList<FloatArray>()
            val cs = arrayOf(floatArrayOf(x - r, y - r, 0f), floatArrayOf(-x + r, y - r, 90f), floatArrayOf(-x + r, -y + r, 180f), floatArrayOf(x - r, -y + r, 270f))
            for (c in cs) for (i in 0..n) {
                val a = Math.toRadians((c[2] + 90.0 * i / n)).toFloat()
                val cx = kotlin.math.cos(a); val sy = kotlin.math.sin(a)
                l.add(floatArrayOf(c[0] + r * cx, c[1] + r * sy, cx, sy))
            }
            return l
        }
}

/** maillage : triangles [x y z nx ny nz u v], regroupes par piece (boitier, couvercle, disque, ombre) et matiere */
class Maillage {
    class Groupe(val premier: Int, val nombre: Int, val piece: Int, val mat: Int, val r: Float, val g: Float, val b: Float, val brillant: Float)
    val v = ArrayList<Float>()
    val groupes = ArrayList<Groupe>()
    private var debut = 0
    private var piece = 0; private var mat = 0; private var col = 0; private var br = 1f
    fun commencer(piece: Int, mat: Int, col: Int = 0, brillant: Float = 1f) { finir(); debut = v.size / 8; this.piece = piece; this.mat = mat; this.col = col; br = brillant }
    fun finir() {
        val n = v.size / 8 - debut
        if (n > 0) groupes.add(Groupe(debut, n, piece, mat, ((col shr 16) and 255) / 255f, ((col shr 8) and 255) / 255f, (col and 255) / 255f, br))
        debut = v.size / 8
    }
    fun s(x: Float, y: Float, z: Float, nx: Float, ny: Float, nz: Float, u: Float, w: Float) { v.add(x); v.add(y); v.add(z); v.add(nx); v.add(ny); v.add(nz); v.add(u); v.add(w) }
    fun tri(a: FloatArray, b: FloatArray, c: FloatArray) { for (p in arrayOf(a, b, c)) s(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]) }
    fun quad(a: FloatArray, b: FloatArray, c: FloatArray, d: FloatArray) { tri(a, b, c); tri(a, c, d) }
}


/** le boitier (bac, couvercle), le disque et l'ombre ; full : jaquette complete ; avant : jaquette avant seule */
fun boitier(full: Boolean, avant: Boolean): Maillage {
    val X = Geo.X; val Y = Geo.Y; val Z = Geo.Z; val T = Geo.T; val R = Geo.R; val SP = Geo.SP; val DR = Geo.DR
    val P_BAC = Geo.P_BAC; val P_COUV = Geo.P_COUV; val P_DISQUE = Geo.P_DISQUE; val P_SOL = Geo.P_SOL
    val M_COULEUR = Geo.M_COULEUR; val M_FULL = Geo.M_FULL; val M_AVANT = Geo.M_AVANT; val M_DISQUE = Geo.M_DISQUE; val M_DESSOUS = Geo.M_DESSOUS; val M_OMBRE = Geo.M_OMBRE
    fun contour(x: Float, y: Float, r: Float, n: Int = 10) = Geo.contour(x, y, r, n)

    val m = Maillage()
    val ub = (1 - SP) / 2; val uf = (1 + SP) / 2
    val noir = 0x141418; val gris = 0x2A2A2F; val fond = 0x232327; val bord = 0x1C1C20
    val zi = Z - T                                            // (plan d'ouverture)
    val zf = -Z + 0.016f                                      // (fond du bac)
    val zl = zi + 0.009f                                      // (fond du couvercle)
    val ext = contour(X, Y, R)
    val inn = contour(X - 0.035f, Y - 0.035f, R - 0.03f)
    val n = ext.size
    fun uAvant(x: Float) = if (full) uf + (x + X) / (2 * X) * (1 - uf) else (x + X) / (2 * X)
    fun uDos(x: Float) = (X - x) / (2 * X) * ub
    fun v(y: Float) = (Y - y) / (2 * Y)
    fun p(x: Float, y: Float, z: Float, nx: Float, ny: Float, nz: Float, u: Float = 0f, w: Float = 0f) = floatArrayOf(x, y, z, nx, ny, nz, u, w)

    // face plane remplie (contour convexe, en eventail depuis le centre)
    fun plaque(c: List<FloatArray>, z: Float, nz: Float, uv: ((Float, Float) -> FloatArray)?) {
        val ctr = p(0f, 0f, z, 0f, 0f, nz, uv?.invoke(0f, 0f)?.get(0) ?: 0f, uv?.invoke(0f, 0f)?.get(1) ?: 0f)
        for (i in c.indices) {
            val a = c[i]; val b = c[(i + 1) % c.size]
            val ua = uv?.invoke(a[0], a[1]); val ubb = uv?.invoke(b[0], b[1])
            val pa = p(a[0], a[1], z, 0f, 0f, nz, ua?.get(0) ?: 0f, ua?.get(1) ?: 0f)
            val pb = p(b[0], b[1], z, 0f, 0f, nz, ubb?.get(0) ?: 0f, ubb?.get(1) ?: 0f)
            if (nz > 0) m.tri(ctr, pa, pb) else m.tri(ctr, pb, pa)
        }
    }
    // anneau plat entre deux contours (le rebord)
    fun anneau(a: List<FloatArray>, b: List<FloatArray>, z: Float, nz: Float) {
        for (i in a.indices) { val j = (i + 1) % a.size
            m.quad(p(a[i][0], a[i][1], z, 0f, 0f, nz), p(a[j][0], a[j][1], z, 0f, 0f, nz), p(b[j][0], b[j][1], z, 0f, 0f, nz), p(b[i][0], b[i][1], z, 0f, 0f, nz)) }
    }
    // paroi entre z0 et z1 (dehors : normale vers l'exterieur ; dedans : vers l'interieur) ; la tranche (x = -X) a part
    fun paroi(c: List<FloatArray>, z0: Float, z1: Float, dehors: Boolean, tranche: Boolean) {
        val k = if (dehors) 1f else -1f
        for (i in c.indices) { val j = (i + 1) % c.size
            val a = c[i]; val b = c[j]
            val gauche = kotlin.math.abs(a[0] + X) < 1e-4f && kotlin.math.abs(b[0] + X) < 1e-4f
            if (gauche != tranche) continue
            val u0 = ub + (z0 + Z) / (2 * Z) * (uf - ub); val u1 = ub + (z1 + Z) / (2 * Z) * (uf - ub)
            m.quad(p(a[0], a[1], z0, a[2] * k, a[3] * k, 0f, u0, v(a[1])), p(b[0], b[1], z0, b[2] * k, b[3] * k, 0f, u0, v(b[1])),
                   p(b[0], b[1], z1, b[2] * k, b[3] * k, 0f, u1, v(b[1])), p(a[0], a[1], z1, a[2] * k, a[3] * k, 0f, u1, v(a[1])))
        }
    }
    // petit pave (attaches, ergots)
    fun pave(x0: Float, y0: Float, x1: Float, y1: Float, z0: Float, z1: Float) {
        m.quad(p(x0, y1, z1, 0f, 0f, 1f), p(x0, y0, z1, 0f, 0f, 1f), p(x1, y0, z1, 0f, 0f, 1f), p(x1, y1, z1, 0f, 0f, 1f))
        m.quad(p(x0, y0, z0, 0f, -1f, 0f), p(x1, y0, z0, 0f, -1f, 0f), p(x1, y0, z1, 0f, -1f, 0f), p(x0, y0, z1, 0f, -1f, 0f))
        m.quad(p(x1, y1, z0, 0f, 1f, 0f), p(x0, y1, z0, 0f, 1f, 0f), p(x0, y1, z1, 0f, 1f, 0f), p(x1, y1, z1, 0f, 1f, 0f))
        m.quad(p(x0, y1, z0, -1f, 0f, 0f), p(x0, y0, z0, -1f, 0f, 0f), p(x0, y0, z1, -1f, 0f, 0f), p(x0, y1, z1, -1f, 0f, 0f))
        m.quad(p(x1, y0, z0, 1f, 0f, 0f), p(x1, y1, z0, 1f, 0f, 0f), p(x1, y1, z1, 1f, 0f, 0f), p(x1, y0, z1, 1f, 0f, 0f))
    }

    // ---- le bac (le dos, la tranche, l'interieur) ----
    if (full) { m.commencer(P_BAC, M_FULL, 0, 1.8f); plaque(ext, -Z, -1f) { x, y -> floatArrayOf(uDos(x), v(y)) } }
    else { m.commencer(P_BAC, M_COULEUR, noir, 1.4f); plaque(ext, -Z, -1f, null) }
    if (full) { m.commencer(P_BAC, M_FULL, 0, 1.6f); paroi(ext, -Z, zi, true, true) }
    else { m.commencer(P_BAC, M_COULEUR, noir, 1.4f); paroi(ext, -Z, zi, true, true) }
    m.commencer(P_BAC, M_COULEUR, noir, 1.4f); paroi(ext, -Z, zi, true, false)
    m.commencer(P_BAC, M_COULEUR, bord, 0.8f); anneau(ext, inn, zi, 1f)
    m.commencer(P_BAC, M_COULEUR, gris, 0.6f); paroi(inn, zf, zi, false, false); paroi(inn, zf, zi, false, true)
    m.commencer(P_BAC, M_COULEUR, fond, 0.5f); plaque(inn, zf, 1f, null)
    // support du disque : plateau hexagonal, anneau, ergot central a dents
    m.commencer(P_BAC, M_COULEUR, 0x2E2E33, 0.7f)
    val hx = ArrayList<FloatArray>()
    for (i in 0 until 6) { val a = Math.toRadians(30.0 + 60.0 * i); hx.add(floatArrayOf((0.52 * kotlin.math.cos(a)).toFloat(), (0.52 * kotlin.math.sin(a)).toFloat(), kotlin.math.cos(a).toFloat(), kotlin.math.sin(a).toFloat())) }
    val zh = zf + 0.008f
    plaque(hx, zh, 1f, null); paroi(hx, zf, zh, true, false)
    m.commencer(P_BAC, M_COULEUR, 0x3A3A40, 0.9f)
    val rc = ArrayList<FloatArray>()
    for (i in 0 until 48) { val a = 2 * Math.PI * i / 48; rc.add(floatArrayOf((0.085 * kotlin.math.cos(a)).toFloat(), (0.085 * kotlin.math.sin(a)).toFloat(), kotlin.math.cos(a).toFloat(), kotlin.math.sin(a).toFloat())) }
    val zc = zh + 0.034f
    paroi(rc, zh, zc, true, false); plaque(rc, zc, 1f, null)
    m.commencer(P_BAC, M_COULEUR, 0x55555C, 1.2f)
    for (i in 0 until 6) {                                   // (les dents de l'ergot)
        val a = Math.PI * i / 3; val cx = (0.06 * kotlin.math.cos(a)).toFloat(); val cy = (0.06 * kotlin.math.sin(a)).toFloat()
        pave(cx - 0.012f, cy - 0.012f, cx + 0.012f, cy + 0.012f, zc, zc + 0.006f)
    }
    m.commencer(P_BAC, M_COULEUR, 0x2A2A2F, 0.6f)            // (petite encoche en bas, comme le vrai)
    pave(-0.16f, -Y + 0.12f, 0.02f, -Y + 0.2f, zf, zf + 0.012f)

    // ---- le couvercle (la jaquette sous le film transparent, l'interieur, les attaches de la notice) ----
    if (full) { m.commencer(P_COUV, M_FULL, 0, 2.2f); plaque(ext, Z, 1f) { x, y -> floatArrayOf(uAvant(x), v(y)) } }
    else if (avant) { m.commencer(P_COUV, M_AVANT, 0, 2.2f); plaque(ext, Z, 1f) { x, y -> floatArrayOf(uAvant(x), v(y)) } }
    else { m.commencer(P_COUV, M_COULEUR, 0x46288C, 1.8f); plaque(ext, Z, 1f, null) }
    if (full) { m.commencer(P_COUV, M_FULL, 0, 1.6f); paroi(ext, zi, Z, true, true) }
    else { m.commencer(P_COUV, M_COULEUR, noir, 1.4f); paroi(ext, zi, Z, true, true) }
    m.commencer(P_COUV, M_COULEUR, noir, 1.4f); paroi(ext, zi, Z, true, false)
    m.commencer(P_COUV, M_COULEUR, bord, 0.8f); anneau(ext, inn, zi, -1f)
    m.commencer(P_COUV, M_COULEUR, gris, 0.6f); paroi(inn, zi, zl, false, false); paroi(inn, zi, zl, false, true)
    m.commencer(P_COUV, M_COULEUR, fond, 0.5f); plaque(inn, zl, -1f, null)
    m.commencer(P_COUV, M_COULEUR, 0x3A3A40, 0.9f)
    for (sy in floatArrayOf(1f, -1f)) {                      // (attaches de la notice, du cote qui s'ouvre)
        val yc = sy * (Y - 0.16f)
        pave(X - 0.2f, yc - 0.035f, X - 0.06f, yc + 0.035f, zl - 0.012f, zl)
        pave(X - 0.2f, yc - 0.06f * sy - 0.012f, X - 0.17f, yc - 0.06f * sy + 0.012f, zl - 0.02f, zl)
    }

    // ---- le disque (dessus : l'image du jeu ; dessous : irise ; tranche argent) ; trou de 15 mm ----
    val ri = DR * 15f / 80f; val e = 0.006f
    m.commencer(P_DISQUE, M_DISQUE, 0, 2.4f)
    val seg = 128
    for (i in 0 until seg) {
        val a0 = 2 * Math.PI * i / seg; val a1 = 2 * Math.PI * (i + 1) / seg
        val c0 = kotlin.math.cos(a0).toFloat(); val s0 = kotlin.math.sin(a0).toFloat(); val c1 = kotlin.math.cos(a1).toFloat(); val s1 = kotlin.math.sin(a1).toFloat()
        fun t(x: Float, y: Float) = floatArrayOf(0.5f + x / (2 * DR), 0.5f - y / (2 * DR))
        val o0 = t(DR * c0, DR * s0); val o1 = t(DR * c1, DR * s1); val i0 = t(ri * c0, ri * s0); val i1 = t(ri * c1, ri * s1)
        m.quad(p(ri * c0, ri * s0, e, 0f, 0f, 1f, i0[0], i0[1]), p(DR * c0, DR * s0, e, 0f, 0f, 1f, o0[0], o0[1]),
               p(DR * c1, DR * s1, e, 0f, 0f, 1f, o1[0], o1[1]), p(ri * c1, ri * s1, e, 0f, 0f, 1f, i1[0], i1[1]))
    }
    m.commencer(P_DISQUE, M_DESSOUS, 0, 3f)
    for (i in 0 until seg) {
        val a0 = 2 * Math.PI * i / seg; val a1 = 2 * Math.PI * (i + 1) / seg
        val c0 = kotlin.math.cos(a0).toFloat(); val s0 = kotlin.math.sin(a0).toFloat(); val c1 = kotlin.math.cos(a1).toFloat(); val s1 = kotlin.math.sin(a1).toFloat()
        fun t(x: Float, y: Float) = floatArrayOf(0.5f + x / (2 * DR), 0.5f - y / (2 * DR))
        val o0 = t(DR * c0, DR * s0); val o1 = t(DR * c1, DR * s1); val i0 = t(ri * c0, ri * s0); val i1 = t(ri * c1, ri * s1)
        m.quad(p(ri * c1, ri * s1, -e, 0f, 0f, -1f, i1[0], i1[1]), p(DR * c1, DR * s1, -e, 0f, 0f, -1f, o1[0], o1[1]),
               p(DR * c0, DR * s0, -e, 0f, 0f, -1f, o0[0], o0[1]), p(ri * c0, ri * s0, -e, 0f, 0f, -1f, i0[0], i0[1]))
    }
    m.commencer(P_DISQUE, M_COULEUR, 0xC8CAD4, 2f)
    for (i in 0 until seg) {
        val a0 = 2 * Math.PI * i / seg; val a1 = 2 * Math.PI * (i + 1) / seg
        val c0 = kotlin.math.cos(a0).toFloat(); val s0 = kotlin.math.sin(a0).toFloat(); val c1 = kotlin.math.cos(a1).toFloat(); val s1 = kotlin.math.sin(a1).toFloat()
        m.quad(p(DR * c0, DR * s0, -e, c0, s0, 0f), p(DR * c1, DR * s1, -e, c1, s1, 0f), p(DR * c1, DR * s1, e, c1, s1, 0f), p(DR * c0, DR * s0, e, c0, s0, 0f))
        m.quad(p(ri * c1, ri * s1, -e, -c1, -s1, 0f), p(ri * c0, ri * s0, -e, -c0, -s0, 0f), p(ri * c0, ri * s0, e, -c0, -s0, 0f), p(ri * c1, ri * s1, e, -c1, -s1, 0f))
    }

    // ---- ombre au sol ----
    m.commencer(P_SOL, M_OMBRE, 0, 0f)
    val sy = -Y - 0.02f
    m.quad(p(-1.6f, sy, -0.9f, 0f, 1f, 0f, 0f, 0f), p(-1.6f, sy, 0.9f, 0f, 1f, 0f, 0f, 1f), p(1.6f, sy, 0.9f, 0f, 1f, 0f, 1f, 1f), p(1.6f, sy, -0.9f, 0f, 1f, 0f, 1f, 0f))
    m.finir()

    return m
}
