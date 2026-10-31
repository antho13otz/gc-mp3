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
        const val M_NOTICE = 6                                    // (la notice dans le couvercle, console v296)
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
    class Groupe(val premier: Int, val nombre: Int, val piece: Int, val mat: Int, val r: Float, val g: Float, val b: Float, val brillant: Float, val mode: Int)
    val v = ArrayList<Float>()
    val groupes = ArrayList<Groupe>()
    private var debut = 0
    private var piece = 0; private var mat = 0; private var col = 0; private var br = 1f; private var mode = 0
    /** mode (rendu) : 0 plastique brillant, 1 disque face gravee, 2 disque face imprimee, 3 film sur la jaquette, 4 plastique mat */
    fun commencer(piece: Int, mat: Int, col: Int = 0, brillant: Float = 1f, mode: Int = 0) { finir(); debut = v.size / 8; this.piece = piece; this.mat = mat; this.col = col; br = brillant; this.mode = mode }
    fun finir() {
        val n = v.size / 8 - debut
        if (n > 0) groupes.add(Groupe(debut, n, piece, mat, ((col shr 16) and 255) / 255f, ((col shr 8) and 255) / 255f, (col and 255) / 255f, br, mode))
        debut = v.size / 8
    }
    fun s(x: Float, y: Float, z: Float, nx: Float, ny: Float, nz: Float, u: Float, w: Float) { v.add(x); v.add(y); v.add(z); v.add(nx); v.add(ny); v.add(nz); v.add(u); v.add(w) }
    fun tri(a: FloatArray, b: FloatArray, c: FloatArray) { for (p in arrayOf(a, b, c)) s(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]) }
    fun quad(a: FloatArray, b: FloatArray, c: FloatArray, d: FloatArray) { tri(a, b, c); tri(a, c, d) }
}


/** le boitier (bac, couvercle), le disque et l'ombre ; full : jaquette complete ; avant : jaquette avant seule */
fun boitier(full: Boolean, avant: Boolean, notice: Float = 0f): Maillage {   // notice : largeur / hauteur de la notice (0 : pas de notice)
    val X = Geo.X; val Y = Geo.Y; val Z = Geo.Z; val T = Geo.T; val R = Geo.R; val SP = Geo.SP; val DR = Geo.DR
    val P_BAC = Geo.P_BAC; val P_COUV = Geo.P_COUV; val P_DISQUE = Geo.P_DISQUE; val P_SOL = Geo.P_SOL
    val M_COULEUR = Geo.M_COULEUR; val M_FULL = Geo.M_FULL; val M_AVANT = Geo.M_AVANT; val M_DISQUE = Geo.M_DISQUE; val M_DESSOUS = Geo.M_DESSOUS; val M_OMBRE = Geo.M_OMBRE
    fun contour(x: Float, y: Float, r: Float, n: Int = 16) = Geo.contour(x, y, r, n)

    val m = Maillage()
    val ub = (1 - SP) / 2; val uf = (1 + SP) / 2
    val noir = 0x141418; val gris = 0x4E4E54; val fond = 0x5C5C62; val bord = 0x38383D   // (interieur : gris moyen, comme le vrai)
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
    if (full) { m.commencer(P_BAC, M_FULL, 0, 1.8f, 3); plaque(ext, -Z, -1f) { x, y -> floatArrayOf(uDos(x), v(y)) } }
    else { m.commencer(P_BAC, M_COULEUR, noir, 1.4f); plaque(ext, -Z, -1f, null) }
    if (full) { m.commencer(P_BAC, M_FULL, 0, 1.6f, 3); paroi(ext, -Z, zi, true, true) }
    else { m.commencer(P_BAC, M_COULEUR, noir, 1.4f); paroi(ext, -Z, zi, true, true) }
    m.commencer(P_BAC, M_COULEUR, noir, 1.4f); paroi(ext, -Z, zi, true, false)
    m.commencer(P_BAC, M_COULEUR, bord, 0.8f, 4); anneau(ext, inn, zi, 1f)
    m.commencer(P_BAC, M_COULEUR, gris, 0.6f, 4); paroi(inn, zf, zi, false, false); paroi(inn, zf, zi, false, true)
    m.commencer(P_BAC, M_COULEUR, fond, 0.5f, 4); plaque(inn, zf, 1f, null)
    fun cercle(r: Float, n: Int = 96) = (0 until n).map { i -> val a = 2 * Math.PI * i / n; floatArrayOf((r * kotlin.math.cos(a)).toFloat(), (r * kotlin.math.sin(a)).toFloat(), kotlin.math.cos(a).toFloat(), kotlin.math.sin(a).toFloat()) }
    fun hexa(r: Float) = (0 until 6).map { i -> val a = Math.toRadians(60.0 * i); floatArrayOf((r * kotlin.math.cos(a)).toFloat(), (r * kotlin.math.sin(a)).toFloat(), kotlin.math.cos(a).toFloat(), kotlin.math.sin(a).toFloat()) }
    // pave tourne (cx, cy : centre ; a : angle ; l x e : taille)
    fun bloc(cx: Float, cy: Float, a: Double, l: Float, e: Float, z0: Float, z1: Float) {
        val ca = kotlin.math.cos(a).toFloat(); val sa = kotlin.math.sin(a).toFloat()
        val c = listOf(-l / 2 to -e / 2, l / 2 to -e / 2, l / 2 to e / 2, -l / 2 to e / 2).map { (u, w) -> floatArrayOf(cx + u * ca - w * sa, cy + u * sa + w * ca, 0f, 0f) }
        for (k in 0 until 4) { val j = (k + 1) % 4; val nx = (c[k][0] + c[j][0]) / 2 - cx; val ny = (c[k][1] + c[j][1]) / 2 - cy; val nl = kotlin.math.sqrt(nx * nx + ny * ny); c[k][2] = nx / nl; c[k][3] = ny / nl }
        for (k in 0 until 4) { val j = (k + 1) % 4; val nx = c[k][2]; val ny = c[k][3]
            m.quad(p(c[k][0], c[k][1], z0, nx, ny, 0f), p(c[j][0], c[j][1], z0, nx, ny, 0f), p(c[j][0], c[j][1], z1, nx, ny, 0f), p(c[k][0], c[k][1], z1, nx, ny, 0f)) }
        m.quad(p(c[0][0], c[0][1], z1, 0f, 0f, 1f), p(c[1][0], c[1][1], z1, 0f, 0f, 1f), p(c[2][0], c[2][1], z1, 0f, 0f, 1f), p(c[3][0], c[3][1], z1, 0f, 0f, 1f))
    }
    // bande en arc de cercle en relief (centre, rayons, angles en degres, hauteurs)
    fun arc(cx: Float, cy: Float, r0: Float, r1: Float, a0: Double, a1: Double, z0: Float, z1: Float, n: Int = 24) {
        for (i in 0 until n) {
            val t0 = Math.toRadians(a0 + (a1 - a0) * i / n); val t1 = Math.toRadians(a0 + (a1 - a0) * (i + 1) / n)
            val c0 = kotlin.math.cos(t0).toFloat(); val s0 = kotlin.math.sin(t0).toFloat(); val c1 = kotlin.math.cos(t1).toFloat(); val s1 = kotlin.math.sin(t1).toFloat()
            m.quad(p(cx + r0 * c0, cy + r0 * s0, z1, 0f, 0f, 1f), p(cx + r1 * c0, cy + r1 * s0, z1, 0f, 0f, 1f), p(cx + r1 * c1, cy + r1 * s1, z1, 0f, 0f, 1f), p(cx + r0 * c1, cy + r0 * s1, z1, 0f, 0f, 1f))
            m.quad(p(cx + r1 * c0, cy + r1 * s0, z0, c0, s0, 0f), p(cx + r1 * c1, cy + r1 * s1, z0, c1, s1, 0f), p(cx + r1 * c1, cy + r1 * s1, z1, c1, s1, 0f), p(cx + r1 * c0, cy + r1 * s0, z1, c0, s0, 0f))
            m.quad(p(cx + r0 * c1, cy + r0 * s1, z0, -c1, -s1, 0f), p(cx + r0 * c0, cy + r0 * s0, z0, -c0, -s0, 0f), p(cx + r0 * c0, cy + r0 * s0, z1, -c0, -s0, 0f), p(cx + r0 * c1, cy + r0 * s1, z1, -c1, -s1, 0f))
        }
    }
    // demi-disque (ou disque) en relief : centre, rayon, angles en degres, hauteurs (bouton du centre)
    fun lobe(cx: Float, cy: Float, r: Float, a0: Double, a1: Double, z0: Float, z1: Float, n: Int = 24) {
        val pts = (0..n).map { i -> val a = Math.toRadians(a0 + (a1 - a0) * i / n); floatArrayOf(cx + r * kotlin.math.cos(a).toFloat(), cy + r * kotlin.math.sin(a).toFloat(), kotlin.math.cos(a).toFloat(), kotlin.math.sin(a).toFloat()) }
        val c = p(cx, cy, z1, 0f, 0f, 1f)
        for (i in 0 until n) { val a = pts[i]; val b = pts[i + 1]
            m.tri(c, p(a[0], a[1], z1, 0f, 0f, 1f), p(b[0], b[1], z1, 0f, 0f, 1f))
            m.quad(p(a[0], a[1], z0, a[2], a[3], 0f), p(b[0], b[1], z0, b[2], b[3], 0f), p(b[0], b[1], z1, b[2], b[3], 0f), p(a[0], a[1], z1, a[2], a[3], 0f)) }
        if (a1 - a0 < 359.0) { val a = pts[n]; val b = pts[0]; val nx = -(b[1] - a[1]); val ny = b[0] - a[0]; val l = kotlin.math.sqrt(nx * nx + ny * ny)
            m.quad(p(a[0], a[1], z0, nx / l, ny / l, 0f), p(b[0], b[1], z0, nx / l, ny / l, 0f), p(b[0], b[1], z1, nx / l, ny / l, 0f), p(a[0], a[1], z1, nx / l, ny / l, 0f)) }
    }
    // support du disque, comme le vrai : grand hexagone a peine en relief, pointes en haut et en bas ; quatre pans clairs
    // inclines en diagonale (pour prendre le disque) ; anneau en relief ou le disque se pose ; un fin cercle a l'interieur ;
    // au centre, la fente sombre avec ses deux barres, le bouton en "S" (PUSH HERE) et sa base claire
    val RH = 0.64f; val apo = RH * 0.8660254f
    fun hexP(r: Float) = (0 until 6).map { i -> val a = Math.toRadians(90.0 + 60.0 * i); floatArrayOf((r * kotlin.math.cos(a)).toFloat(), (r * kotlin.math.sin(a)).toFloat(), kotlin.math.cos(a + Math.PI / 6).toFloat(), kotlin.math.sin(a + Math.PI / 6).toFloat()) }
    fun hexDist(t: Double): Float { val d = Math.toDegrees(t); val phi = Math.round((d - 0.0) / 60.0) * 60.0; return (apo / kotlin.math.cos(Math.toRadians(d - phi))).toFloat() }
    val zh = zf + 0.009f
    m.commencer(P_BAC, M_COULEUR, 0x66666E, 0.7f, 4)
    plaque(hexP(RH), zh, 1f, null)
    m.commencer(P_BAC, M_COULEUR, 0x82828A, 0.9f, 4)
    paroi(hexP(RH), zf, zh, true, false)
    m.commencer(P_BAC, M_COULEUR, 0x8E8E96, 0.9f, 4)                      // (pans clairs inclines)
    val rIn = DR + 0.032f
    for (c in doubleArrayOf(52.0, 128.0, 232.0, 308.0)) {
        val n = 8
        for (i in 0 until n) {
            val t0 = Math.toRadians(c - 15 + 30.0 * i / n); val t1 = Math.toRadians(c - 15 + 30.0 * (i + 1) / n)
            val o0 = hexDist(t0); val o1 = hexDist(t1)
            val c0 = kotlin.math.cos(t0).toFloat(); val s0 = kotlin.math.sin(t0).toFloat(); val c1 = kotlin.math.cos(t1).toFloat(); val s1 = kotlin.math.sin(t1).toFloat()
            val nz = 0.85f; val nr = -0.53f                             // (pente qui descend vers le disque)
            val tm = (t0 + t1) / 2; val cm = kotlin.math.cos(tm).toFloat(); val sm = kotlin.math.sin(tm).toFloat()
            m.quad(p(rIn * c0, rIn * s0, zh + 0.001f, -cm * nr, -sm * nr, nz), p(o0 * c0, o0 * s0, zh + 0.012f, -cm * nr, -sm * nr, nz),
                   p(o1 * c1, o1 * s1, zh + 0.012f, -cm * nr, -sm * nr, nz), p(rIn * c1, rIn * s1, zh + 0.001f, -cm * nr, -sm * nr, nz))
            m.quad(p(o0 * c0, o0 * s0, zh, cm, sm, 0f), p(o1 * c1, o1 * s1, zh, cm, sm, 0f), p(o1 * c1, o1 * s1, zh + 0.012f, cm, sm, 0f), p(o0 * c0, o0 * s0, zh + 0.012f, cm, sm, 0f))
        }
        for (e in doubleArrayOf(c - 15, c + 15)) {                      // (cotes du pan)
            val t = Math.toRadians(e); val o = hexDist(t); val cc = kotlin.math.cos(t).toFloat(); val ss = kotlin.math.sin(t).toFloat(); val sg = if (e < c) -1f else 1f
            m.tri(p(rIn * cc, rIn * ss, zh, -ss * sg, cc * sg, 0f), p(o * cc, o * ss, zh, -ss * sg, cc * sg, 0f), p(o * cc, o * ss, zh + 0.012f, -ss * sg, cc * sg, 0f))
        }
    }
    m.commencer(P_BAC, M_COULEUR, 0x86868E, 1.0f, 4)                      // (anneau en relief : le disque s'y pose)
    arc(0f, 0f, DR + 0.012f, DR + 0.032f, 0.0, 360.0, zh, zh + 0.008f, 120)
    m.commencer(P_BAC, M_COULEUR, 0x3E3E44, 0.6f, 4)                      // (son ombre a l'interieur, et le fin cercle)
    arc(0f, 0f, DR + 0.002f, DR + 0.012f, 0.0, 360.0, zh, zh + 0.0008f, 120)
    arc(0f, 0f, 0.168f, 0.174f, 0.0, 360.0, zh, zh + 0.0006f, 72)
    m.commencer(P_BAC, M_COULEUR, 0x2C2C31, 0.5f, 4)                      // (la fente du centre)
    pave(-0.075f, -0.15f, 0.075f, 0.15f, zh, zh + 0.0008f)
    m.commencer(P_BAC, M_COULEUR, 0x7C7268, 0.8f, 4)                      // (les deux barres, un peu usees)
    pave(-0.072f, -0.15f, -0.060f, 0.15f, zh, zh + 0.006f); pave(0.060f, -0.15f, 0.072f, 0.15f, zh, zh + 0.006f)
    m.commencer(P_BAC, M_COULEUR, 0x9497A0, 1.0f, 4)                      // (base claire du bouton)
    pave(-0.042f, -0.115f, 0.042f, -0.05f, zh, zh + 0.004f)
    m.commencer(P_BAC, M_COULEUR, 0x5E5E66, 1.1f)                         // (le bouton en "S" : deux demi-disques decales)
    lobe(-0.006f, 0.022f, 0.052f, 30.0, 210.0, zh, zh + 0.0085f); lobe(0.006f, -0.022f, 0.052f, 210.0, 390.0, zh, zh + 0.0085f)
    // rangement de carte memoire (en bas, cote charniere) : cadre fin ouvert en bas, deux languettes beiges, le texte en relief
    m.commencer(P_BAC, M_COULEUR, 0x45454B, 0.7f, 4)
    val ax0 = -X + 0.06f; val ax1 = -X + 0.50f; val ay0 = -Y + 0.20f; val ay1 = -Y + 0.50f
    pave(ax0, ay1 - 0.010f, ax1, ay1, zf, zf + 0.010f); pave(ax0, ay0, ax0 + 0.010f, ay1, zf, zf + 0.010f); pave(ax1 - 0.010f, ay0, ax1, ay1, zf, zf + 0.010f)
    m.commencer(P_BAC, M_COULEUR, 0x50505A, 0.6f, 4)
    for (k in 0 until 18) { val x = ax0 + 0.06f + k * 0.019f; pave(x, ay1 - 0.055f, x + 0.012f, ay1 - 0.035f, zf, zf + 0.0015f) }
    m.commencer(P_BAC, M_COULEUR, 0xC9C1B2, 1.0f, 4)
    pave(ax0 + 0.010f, ay0 + 0.03f, ax0 + 0.055f, ay0 + 0.18f, zf, zf + 0.012f); pave(ax1 - 0.055f, ay0 + 0.03f, ax1 - 0.010f, ay0 + 0.18f, zf, zf + 0.012f)
    // les deux dessins en creux (en bas a droite) : comment prendre le disque (bon : appuyer au centre / mauvais : tirer)
    m.commencer(P_BAC, M_COULEUR, 0x38383E, 0.6f, 4)
    for ((x0, x1) in listOf(-0.24f to 0.10f, 0.13f to 0.47f)) {
        val y0 = -Y + 0.07f; val y1 = -Y + 0.40f; val e = 0.008f
        pave(x0, y0, x1, y0 + e, zf, zf + 0.002f); pave(x0, y1 - e, x1, y1, zf, zf + 0.002f); pave(x0, y0, x0 + e, y1, zf, zf + 0.002f); pave(x1 - e, y0, x1, y1, zf, zf + 0.002f)
        val cx = (x0 + x1) / 2; val cy = y0 + 0.13f
        arc(cx, cy, 0.075f, 0.082f, 0.0, 360.0, zf, zf + 0.002f, 40); arc(cx, cy, 0.022f, 0.028f, 0.0, 360.0, zf, zf + 0.002f, 20)
    }
    bloc(-0.07f + 0.02f, -Y + 0.30f, Math.toRadians(70.0), 0.13f, 0.03f, zf, zf + 0.002f)                     // (bon : un doigt au centre)
    bloc(0.30f + 0.06f, -Y + 0.24f, Math.toRadians(-20.0), 0.14f, 0.03f, zf, zf + 0.002f)                     // (mauvais : la main qui tire)
    m.commencer(P_BAC, M_COULEUR, 0x2A2A2F, 0.6f, 4)                      // (le ✓ et le ✗ au-dessus)
    bloc(-0.085f, -Y + 0.435f, Math.toRadians(-45.0), 0.035f, 0.012f, zf, zf + 0.006f); bloc(-0.058f, -Y + 0.445f, Math.toRadians(58.0), 0.07f, 0.012f, zf, zf + 0.006f)
    bloc(0.30f, -Y + 0.44f, Math.toRadians(45.0), 0.06f, 0.012f, zf, zf + 0.006f); bloc(0.30f, -Y + 0.44f, Math.toRadians(-45.0), 0.06f, 0.012f, zf, zf + 0.006f)
    // fentes : une en haut a droite
    m.commencer(P_BAC, M_COULEUR, 0x202024, 0.5f, 4)
    pave(X - 0.075f, Y - 0.48f, X - 0.06f, Y - 0.26f, zf, zf + 0.002f)

    // ---- le couvercle (la jaquette sous le film transparent, l'interieur, les attaches de la notice) ----
    if (full) { m.commencer(P_COUV, M_FULL, 0, 2.2f, 3); plaque(ext, Z, 1f) { x, y -> floatArrayOf(uAvant(x), v(y)) } }
    else if (avant) { m.commencer(P_COUV, M_AVANT, 0, 2.2f, 3); plaque(ext, Z, 1f) { x, y -> floatArrayOf(uAvant(x), v(y)) } }
    else { m.commencer(P_COUV, M_COULEUR, 0x46288C, 1.8f, 3); plaque(ext, Z, 1f, null) }
    if (full) { m.commencer(P_COUV, M_FULL, 0, 1.6f, 3); paroi(ext, zi, Z, true, true) }
    else { m.commencer(P_COUV, M_COULEUR, noir, 1.4f); paroi(ext, zi, Z, true, true) }
    m.commencer(P_COUV, M_COULEUR, noir, 1.4f); paroi(ext, zi, Z, true, false)
    m.commencer(P_COUV, M_COULEUR, bord, 0.8f, 4); anneau(ext, inn, zi, -1f)
    m.commencer(P_COUV, M_COULEUR, gris, 0.6f, 4); paroi(inn, zi, zl, false, false); paroi(inn, zi, zl, false, true)
    m.commencer(P_COUV, M_COULEUR, fond, 0.5f, 4); plaque(inn, zl, -1f, null)
    // la notice, posee au fond du couvercle sous les attaches (cote qui s'ouvre) ; ouvert, le couvercle est retourne :
    // le cote x > 0 passe a gauche, la couverture se lit a l'endroit
    if (notice > 0f) {
        m.commencer(P_COUV, Geo.M_NOTICE, 0xFFFFFF, 0.3f, 4)
        var hh = 2 * (Y - 0.035f) * 0.94f; var w = hh * notice
        val wMax = 2 * (X - 0.035f) * 0.93f
        if (w > wMax) { w = wMax; hh = w / notice }
        val x1 = X - 0.055f; val x0 = x1 - w; val zn = zl - 0.003f
        m.quad(p(x0, -hh / 2, zn, 0f, 0f, -1f, 1f, 1f), p(x1, -hh / 2, zn, 0f, 0f, -1f, 0f, 1f), p(x1, hh / 2, zn, 0f, 0f, -1f, 0f, 0f), p(x0, hh / 2, zn, 0f, 0f, -1f, 1f, 0f))
    }
    // attaches de la notice, comme le vrai : un crochet courbe en "C" qui part du bord (en haut et en bas, du cote qui
    // s'ouvre) : une bande plate puis un demi-cercle qui redescend sur le fond
    m.commencer(P_COUV, M_COULEUR, 0xB4B5BB, 1.2f)
    for (sy in floatArrayOf(1f, -1f)) {
        val ya = sy * (Y - 0.26f); val yb = sy * (Y - 0.13f); val y0 = minOf(ya, yb); val y1 = maxOf(ya, yb)
        val xw = X - 0.035f; val cx = X - 0.22f; val r = 0.036f; val zt = zl - 2 * r; val cz = zl - r
        m.quad(p(xw, y0, zt, 0f, 0f, -1f), p(cx, y0, zt, 0f, 0f, -1f), p(cx, y1, zt, 0f, 0f, -1f), p(xw, y1, zt, 0f, 0f, -1f))
        val n = 16
        for (i in 0 until n) {
            val t0 = Math.toRadians(-90.0 + 180.0 * i / n); val t1 = Math.toRadians(-90.0 + 180.0 * (i + 1) / n)
            val x0 = cx - r * kotlin.math.cos(t0).toFloat(); val z0 = cz + r * kotlin.math.sin(t0).toFloat()
            val x1 = cx - r * kotlin.math.cos(t1).toFloat(); val z1 = cz + r * kotlin.math.sin(t1).toFloat()
            val nx0 = -kotlin.math.cos(t0).toFloat(); val nz0 = kotlin.math.sin(t0).toFloat(); val nx1 = -kotlin.math.cos(t1).toFloat(); val nz1 = kotlin.math.sin(t1).toFloat()
            m.quad(p(x0, y0, z0, nx0, 0f, nz0), p(x1, y0, z1, nx1, 0f, nz1), p(x1, y1, z1, nx1, 0f, nz1), p(x0, y1, z0, nx0, 0f, nz0))
        }
        pave(cx - 0.004f, y0, cx + 0.03f, y1, zl - 0.004f, zl)          // (le pied du crochet, sur le fond)
    }
    // encoches noires dans le rebord, du cote qui s'ouvre ; fentes pres de la charniere (en haut et en bas)
    m.commencer(P_COUV, M_COULEUR, 0x141417, 0.5f, 4)
    for ((ya, yb) in listOf(Y - 0.42f to Y - 0.30f, Y - 0.58f to Y - 0.46f, -Y + 0.30f to -Y + 0.42f, -Y + 0.12f to -Y + 0.24f))
        pave(X - 0.05f, ya, X - 0.035f, yb, zl - 0.03f, zl)
    pave(-X + 0.12f, Y - 0.40f, -X + 0.135f, Y - 0.22f, zl - 0.002f, zl)
    pave(-X + 0.12f, -Y + 0.22f, -X + 0.135f, -Y + 0.40f, zl - 0.002f, zl)

    // ---- le disque (dessus : l'image du jeu ; dessous : irise ; tranche argent) ; trou de 15 mm ----
    val ri = DR * 15f / 80f; val e = 0.006f
    m.commencer(P_DISQUE, M_DISQUE, 0, 2.4f, 2)
    val seg = 256
    for (i in 0 until seg) {
        val a0 = 2 * Math.PI * i / seg; val a1 = 2 * Math.PI * (i + 1) / seg
        val c0 = kotlin.math.cos(a0).toFloat(); val s0 = kotlin.math.sin(a0).toFloat(); val c1 = kotlin.math.cos(a1).toFloat(); val s1 = kotlin.math.sin(a1).toFloat()
        fun t(x: Float, y: Float) = floatArrayOf(0.5f + x / (2 * DR), 0.5f - y / (2 * DR))
        val o0 = t(DR * c0, DR * s0); val o1 = t(DR * c1, DR * s1); val i0 = t(ri * c0, ri * s0); val i1 = t(ri * c1, ri * s1)
        m.quad(p(ri * c0, ri * s0, e, 0f, 0f, 1f, i0[0], i0[1]), p(DR * c0, DR * s0, e, 0f, 0f, 1f, o0[0], o0[1]),
               p(DR * c1, DR * s1, e, 0f, 0f, 1f, o1[0], o1[1]), p(ri * c1, ri * s1, e, 0f, 0f, 1f, i1[0], i1[1]))
    }
    m.commencer(P_DISQUE, M_COULEUR, 0xC0C4CC, 3f, 1)
    for (i in 0 until seg) {
        val a0 = 2 * Math.PI * i / seg; val a1 = 2 * Math.PI * (i + 1) / seg
        val c0 = kotlin.math.cos(a0).toFloat(); val s0 = kotlin.math.sin(a0).toFloat(); val c1 = kotlin.math.cos(a1).toFloat(); val s1 = kotlin.math.sin(a1).toFloat()
        fun t(x: Float, y: Float) = floatArrayOf(0.5f + x / (2 * DR), 0.5f - y / (2 * DR))
        val o0 = t(DR * c0, DR * s0); val o1 = t(DR * c1, DR * s1); val i0 = t(ri * c0, ri * s0); val i1 = t(ri * c1, ri * s1)
        m.quad(p(ri * c1, ri * s1, -e, 0f, 0f, -1f, i1[0], i1[1]), p(DR * c1, DR * s1, -e, 0f, 0f, -1f, o1[0], o1[1]),
               p(DR * c0, DR * s0, -e, 0f, 0f, -1f, o0[0], o0[1]), p(ri * c0, ri * s0, -e, 0f, 0f, -1f, i0[0], i0[1]))
    }
    m.commencer(P_DISQUE, M_COULEUR, 0xB8BCC8, 2f)
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
