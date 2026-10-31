package fr.gcstation.telecommande

// Appli : estimation du mouvement entre deux images de la tele (pour les images intermediaires a 120 Hz).
// Par blocs : l'image est reduite en luminance (moitie si elle fait 640 de large), decoupee en blocs de 8x8 ;
// pour chaque bloc, le deplacement (jusqu'a 7 pixels de la version reduite, dans tous les sens) qui ressemble le
// plus a l'image suivante. Un deplacement n'est garde que s'il fait nettement mieux que "rien ne bouge" (sinon :
// 0, pour que le texte immobile reste net). Puis les vecteurs isoles (aucun voisin pareil) sont remplaces par le median 3x3.
// Resultat : un champ de vecteurs (en pixels de l'image d'origine), un par bloc. Kotlin pur : verifiable a part.

object Mouvement {
    const val BLOC = 8
    const val RAYON = 7

    class Champ(val bw: Int, val bh: Int, val dx: FloatArray, val dy: FloatArray, val pas: Int)   // (pas : pixels d'origine par bloc)

    /** luminance reduite : facteur 1 ou 2 */
    private fun luma(px: IntArray, w: Int, h: Int, f: Int): IntArray {
        val lw = w / f; val lh = h / f
        val o = IntArray(lw * lh)
        for (y in 0 until lh) for (x in 0 until lw) {
            var s = 0
            for (j in 0 until f) for (i in 0 until f) {
                val c = px[(y * f + j) * w + x * f + i]
                s += ((c shr 16 and 255) * 77 + (c shr 8 and 255) * 150 + (c and 255) * 29) shr 8
            }
            o[y * lw + x] = s / (f * f)
        }
        return o
    }

    /** a (avant) -> b (apres), pixels ARGB w x h */
    fun estimer(a: IntArray, b: IntArray, w: Int, h: Int): Champ {
        val f = if (w >= 512) 2 else 1
        val lw = w / f; val lh = h / f
        val la = luma(a, w, h, f); val lb = luma(b, w, h, f)
        val bw = lw / BLOC; val bh = lh / BLOC
        val vx = IntArray(bw * bh); val vy = IntArray(bw * bh)
        for (by in 0 until bh) for (bx in 0 until bw) {
            val x0 = bx * BLOC; val y0 = by * BLOC
            fun sad(dx: Int, dy: Int, limite: Int): Int {
                var s = 0
                for (y in 0 until BLOC) {
                    val ya = y0 + y; val yb = ya + dy
                    if (yb < 0 || yb >= lh) return Int.MAX_VALUE
                    val ra = ya * lw; val rb = yb * lw
                    for (x in 0 until BLOC) {
                        val xb = x0 + x + dx
                        if (xb < 0 || xb >= lw) return Int.MAX_VALUE
                        val e = la[ra + x0 + x] - lb[rb + xb]
                        s += if (e < 0) -e else e
                    }
                    if (s >= limite) return s
                }
                return s
            }
            val zero = sad(0, 0, Int.MAX_VALUE)
            var best = zero; var bdx = 0; var bdy = 0
            if (zero > BLOC * BLOC * 2) {                         // (bloc qui change : on cherche ou il est alle)
                for (dy in -RAYON..RAYON) for (dx in -RAYON..RAYON) {
                    if (dx == 0 && dy == 0) continue
                    val s = sad(dx, dy, best)
                    if (s < best || (s == best && dx * dx + dy * dy < bdx * bdx + bdy * bdy)) { best = s; bdx = dx; bdy = dy }
                }
                if (best * 10 > zero * 7) { bdx = 0; bdy = 0 }      // (pas nettement mieux : immobile)
            }
            vx[by * bw + bx] = bdx; vy[by * bw + bx] = bdy
        }
        // (deux passes : chaque bloc essaie les vecteurs de ses 4 voisins et prend le leur s'il va presque aussi bien :
        //  corrige les bords horizontaux ou le mouvement de cote ne se voit pas, et lisse le champ)
        for (passe in 0 until 2) for (by in 0 until bh) for (bx in 0 until bw) {
            val k = by * bw + bx
            val x0 = bx * BLOC; val y0 = by * BLOC
            fun sad2(dx: Int, dy: Int): Int {
                var s = 0
                for (y in 0 until BLOC) {
                    val yb = y0 + y + dy
                    if (yb < 0 || yb >= lh) return Int.MAX_VALUE
                    for (x in 0 until BLOC) {
                        val xb = x0 + x + dx
                        if (xb < 0 || xb >= lw) return Int.MAX_VALUE
                        val e = la[(y0 + y) * lw + x0 + x] - lb[yb * lw + xb]
                        s += if (e < 0) -e else e
                    }
                }
                return s
            }
            val propre = sad2(vx[k], vy[k])
            var bs = propre; var cx = vx[k]; var cy = vy[k]
            for ((nx, ny) in arrayOf(bx - 1 to by, bx + 1 to by, bx to by - 1, bx to by + 1)) {
                if (nx < 0 || ny < 0 || nx >= bw || ny >= bh) continue
                val n = ny * bw + nx
                if (vx[n] == cx && vy[n] == cy) continue
                val sn = sad2(vx[n], vy[n])
                if (sn.toLong() * 10 <= propre.toLong() * 11 && sn <= bs + propre / 10 && (vx[n] != 0 || vy[n] != 0)) { bs = sn; cx = vx[n]; cy = vy[n] }
            }
            vx[k] = cx; vy[k] = cy
        }
        // vecteurs isoles : median 3x3
        val ox = FloatArray(bw * bh); val oy = FloatArray(bw * bh)
        val tx = IntArray(9); val ty = IntArray(9)
        for (by in 0 until bh) for (bx in 0 until bw) {
            var n = 0
            for (j in -1..1) for (i in -1..1) {
                val x = (bx + i).coerceIn(0, bw - 1); val y = (by + j).coerceIn(0, bh - 1)
                tx[n] = vx[y * bw + x]; ty[n] = vy[y * bw + x]; n++
            }
            // (garde si au moins 2 voisins vont pareil (bords d'une zone qui bouge) ; sinon le median)
            val cx = vx[by * bw + bx]; val cy = vy[by * bw + bx]
            var pareil = 0
            for (k in 0 until 9) if (k != 4 && kotlin.math.abs(tx[k] - cx) <= 1 && kotlin.math.abs(ty[k] - cy) <= 1) pareil++
            tx.sort(); ty.sort()
            val gx = if (pareil >= 2) cx else tx[4]; val gy = if (pareil >= 2) cy else ty[4]
            ox[by * bw + bx] = (gx * f).toFloat(); oy[by * bw + bx] = (gy * f).toFloat()
        }
        return Champ(bw, bh, ox, oy, BLOC * f)
    }

    /** le champ en pixels ARGB (rouge : x, vert : y ; 128 = immobile, 4 niveaux par pixel), pour le shader */
    fun pixels(c: Champ): IntArray {
        val o = IntArray(c.bw * c.bh)
        for (k in o.indices) {
            val r = (128 + c.dx[k] * 4).toInt().coerceIn(0, 255); val g = (128 + c.dy[k] * 4).toInt().coerceIn(0, 255)
            o[k] = (0xFF shl 24) or (r shl 16) or (g shl 8)
        }
        return o
    }
}
