package fr.gcstation.telecommande

// Appli : estimation du mouvement entre deux images de la tele (pour les images intermediaires a 120 Hz).
// Par blocs : l'image est reduite en luminance (moitie si elle fait 640 de large), decoupee en blocs de 8x8 ;
// pour chaque bloc, le deplacement (jusqu'a 7 pixels de la version reduite, dans tous les sens) qui ressemble le
// plus a l'image suivante. Un deplacement n'est garde que s'il fait nettement mieux que "rien ne bouge" (sinon :
// 0, pour que le texte immobile reste net). Puis les vecteurs isoles (aucun voisin pareil) sont remplaces par le median 3x3.
// Resultat : un champ de vecteurs (en pixels de l'image d'origine), un par bloc. Kotlin pur : verifiable a part.

object Mouvement {
    const val BLOC = 8
    const val LARGE = 8          // (build 506 : recherche du niveau grossier, en pixels de l'image reduite)
    const val AFFINE = 2

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

    /** moyenne 2x2 (niveau grossier de la recherche) */
    private fun moitie(l: IntArray, w: Int, h: Int): IntArray {
        val cw = w / 2; val ch = h / 2
        val o = IntArray(cw * ch)
        for (y in 0 until ch) for (x in 0 until cw) {
            val k = 2 * y * w + 2 * x
            o[y * cw + x] = (l[k] + l[k + 1] + l[k + w] + l[k + w + 1]) shr 2
        }
        return o
    }

    /** ecart entre le bloc (x0, y0) de la et le meme bloc decale de (dx, dy) dans lb ; s'arrete des qu'il depasse limite */
    private fun sad(la: IntArray, lb: IntArray, lw: Int, lh: Int, x0: Int, y0: Int, dx: Int, dy: Int, limite: Int): Int {
        if (x0 + dx < 0 || y0 + dy < 0 || x0 + dx + BLOC > lw || y0 + dy + BLOC > lh) return Int.MAX_VALUE
        var s = 0
        for (y in 0 until BLOC) {
            val ra = (y0 + y) * lw + x0; val rb = (y0 + y + dy) * lw + x0 + dx
            for (x in 0 until BLOC) { val e = la[ra + x] - lb[rb + x]; s += if (e < 0) -e else e }
            if (s >= limite) return s
        }
        return s
    }

    /** a (avant) -> b (apres), pixels ARGB w x h.
     *  Build 506 : en deux niveaux. D'abord sur l'image reduite de moitie, recherche large (jusqu'a 32 pixels de l'image par
     *  image : le carrousel de la console, un panoramique) ; le mouvement d'ensemble le plus frequent est essaye partout.
     *  Puis sur l'image normale, chaque bloc part du meilleur de ces vecteurs et l'affine de 2 pixels. Avant : 14 pixels au
     *  plus, et un mouvement plus rapide restait un simple fondu. */
    fun estimer(a: IntArray, b: IntArray, w: Int, h: Int): Champ {
        val f = if (w >= 512) 2 else 1
        val lw = w / f; val lh = h / f
        val la = luma(a, w, h, f); val lb = luma(b, w, h, f)
        val bw = lw / BLOC; val bh = lh / BLOC
        val bouge = BLOC * BLOC * 2
        // ---- niveau grossier
        val cw = lw / 2; val ch = lh / 2
        val ca = moitie(la, lw, lh); val cb = moitie(lb, lw, lh)
        val cbw = cw / BLOC; val cbh = ch / BLOC
        val cvx = IntArray(cbw * cbh); val cvy = IntArray(cbw * cbh)
        val compte = HashMap<Int, Int>()
        for (by in 0 until cbh) for (bx in 0 until cbw) {
            val x0 = bx * BLOC; val y0 = by * BLOC
            val zero = sad(ca, cb, cw, ch, x0, y0, 0, 0, Int.MAX_VALUE)
            if (zero <= bouge) continue
            var best = zero; var bdx = 0; var bdy = 0
            for (dy in -LARGE..LARGE) for (dx in -LARGE..LARGE) {
                if (dx == 0 && dy == 0) continue
                val sv = sad(ca, cb, cw, ch, x0, y0, dx, dy, best)
                if (sv < best || (sv == best && dx * dx + dy * dy < bdx * bdx + bdy * bdy)) { best = sv; bdx = dx; bdy = dy }
            }
            if (best * 10 > zero * 7) continue
            cvx[by * cbw + bx] = bdx; cvy[by * cbw + bx] = bdy
            val cle = (bdx + 64) * 256 + (bdy + 64)
            compte[cle] = (compte[cle] ?: 0) + 1
        }
        val ens = compte.maxByOrNull { it.value }?.takeIf { it.value >= 3 }?.key
        val gx = if (ens != null) (ens / 256 - 64) * 2 else 0; val gy = if (ens != null) (ens % 256 - 64) * 2 else 0
        // ---- niveau normal
        val vx = IntArray(bw * bh); val vy = IntArray(bw * bh)
        val cand = IntArray(8)
        for (by in 0 until bh) for (bx in 0 until bw) {
            val x0 = bx * BLOC; val y0 = by * BLOC
            val zero = sad(la, lb, lw, lh, x0, y0, 0, 0, Int.MAX_VALUE)
            if (zero <= bouge) continue
            val ck = minOf(by / 2, cbh - 1).coerceAtLeast(0) * cbw + minOf(bx / 2, cbw - 1).coerceAtLeast(0)
            var nc = 0
            cand[nc++] = 0; cand[nc++] = 0
            if (cbw > 0 && cbh > 0) { cand[nc++] = cvx[ck] * 2; cand[nc++] = cvy[ck] * 2 }
            cand[nc++] = gx; cand[nc++] = gy
            var best = zero; var bdx = 0; var bdy = 0
            var c = 0
            while (c < nc) {
                val dx = cand[c]; val dy = cand[c + 1]; c += 2
                if (dx == 0 && dy == 0) continue
                val sv = sad(la, lb, lw, lh, x0, y0, dx, dy, best)
                if (sv < best) { best = sv; bdx = dx; bdy = dy }
            }
            val ox0 = bdx; val oy0 = bdy
            for (dy in -AFFINE..AFFINE) for (dx in -AFFINE..AFFINE) {
                if (dx == 0 && dy == 0) continue
                val tx = ox0 + dx; val ty = oy0 + dy
                val sv = sad(la, lb, lw, lh, x0, y0, tx, ty, best)
                if (sv < best || (sv == best && tx * tx + ty * ty < bdx * bdx + bdy * bdy)) { best = sv; bdx = tx; bdy = ty }
            }
            if (best * 10 > zero * 7) { bdx = 0; bdy = 0 }      // (pas nettement mieux : immobile)
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
            ox[by * bw + bx] = (gx * f).coerceIn(-31, 31).toFloat(); oy[by * bw + bx] = (gy * f).coerceIn(-31, 31).toFloat()
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
