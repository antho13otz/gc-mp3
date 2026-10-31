package fr.gcstation.telecommande

// Appli : agrandir proprement une petite image (l'image du disque de GameTDB, ~160 px) avant d'en faire une texture.
// 1. Les pixels transparents prennent la couleur de leurs voisins opaques (le fond ne deteint pas sur le bord).
// 2. Agrandissement bicubique (Catmull-Rom), et pour chaque point les bornes des 4 pixels d'origine qui l'entourent.
// 3. Plusieurs passes de netteté (renforcement local), chaque point restant entre ces bornes : les contours se
//    raidissent (bords nets, texte lisible) sans halo ni bruit dans les aplats.
// Une seule fois par jeu, sur le processeur. Kotlin pur (pixels ARGB) : verifiable a part.

object Agrandir {
    /** couleurs des pixels transparents : celles des voisins opaques */
    fun remplir(px: IntArray, w: Int, h: Int): IntArray {
        val o = px.copyOf()
        val ok = BooleanArray(w * h) { (px[it] ushr 24) >= 128 }
        if (ok.none { it }) return o
        var reste = ok.count { !it }
        while (reste > 0) {
            val nv = ok.copyOf()
            for (y in 0 until h) for (x in 0 until w) {
                val k = y * w + x
                if (ok[k]) continue
                var r = 0; var g = 0; var b = 0; var n = 0
                for (j in -1..1) for (i in -1..1) {
                    val xx = x + i; val yy = y + j
                    if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue
                    val q = yy * w + xx
                    if (!ok[q]) continue
                    r += o[q] shr 16 and 255; g += o[q] shr 8 and 255; b += o[q] and 255; n++
                }
                if (n > 0) { o[k] = (px[k] and 0xFF000000.toInt()) or ((r / n) shl 16) or ((g / n) shl 8) or (b / n); nv[k] = true; reste-- }
            }
            System.arraycopy(nv, 0, ok, 0, ok.size)
        }
        return o
    }

    private fun cr(t: Float): FloatArray {                    // (poids Catmull-Rom des 4 pixels, t entre 0 et 1)
        val t2 = t * t; val t3 = t2 * t
        return floatArrayOf((-t3 + 2 * t2 - t) * 0.5f, (3 * t3 - 5 * t2 + 2) * 0.5f, (-3 * t3 + 4 * t2 + t) * 0.5f, (t3 - t2) * 0.5f)
    }

    /** w x h -> W x H ; passes : nombre de passes de netteté ; force : leur force */
    fun agrandir(px0: IntArray, w: Int, h: Int, W: Int, H: Int, passes: Int = 6, force: Float = 0.8f): IntArray {
        val px = remplir(px0, w, h)
        val src = Array(4) { ch -> FloatArray(w * h) { (px[it] ushr (24 - 8 * ch) and 255).toFloat() } }
        val c = Array(4) { FloatArray(W * H) }
        val mn = Array(4) { FloatArray(W * H) }
        val mx = Array(4) { FloatArray(W * H) }
        for (Y in 0 until H) {
            val qy = (Y + 0.5f) * h / H - 0.5f
            val iy = kotlin.math.floor(qy).toInt(); val wy = cr(qy - iy)
            for (X in 0 until W) {
                val qx = (X + 0.5f) * w / W - 0.5f
                val ix = kotlin.math.floor(qx).toInt(); val wx = cr(qx - ix)
                val k = Y * W + X
                for (ch in 0 until 4) {
                    val a = src[ch]
                    var s = 0f
                    for (j in 0 until 4) {
                        val row = (iy + j - 1).coerceIn(0, h - 1) * w
                        var r = 0f
                        for (i in 0 until 4) r += a[row + (ix + i - 1).coerceIn(0, w - 1)] * wx[i]
                        s += r * wy[j]
                    }
                    val x0 = ix.coerceIn(0, w - 1); val x1 = (ix + 1).coerceIn(0, w - 1)
                    val y0 = iy.coerceIn(0, h - 1) * w; val y1 = (iy + 1).coerceIn(0, h - 1) * w
                    val p00 = a[y0 + x0]; val p01 = a[y0 + x1]; val p10 = a[y1 + x0]; val p11 = a[y1 + x1]
                    val lo = minOf(minOf(p00, p01), minOf(p10, p11)); val hi = maxOf(maxOf(p00, p01), maxOf(p10, p11))
                    mn[ch][k] = lo; mx[ch][k] = hi
                    c[ch][k] = s.coerceIn(lo, hi)
                }
            }
        }
        // netteté : c + force x (c - flou 3x3), borne ; repetee : les bords se raidissent
        val t = FloatArray(W * H)
        repeat(passes) {
            for (ch in 0 until 4) {
                val a = c[ch]
                for (Y in 0 until H) {
                    val ym = (if (Y > 0) Y - 1 else 0) * W; val y = Y * W; val yp = (if (Y < H - 1) Y + 1 else Y) * W
                    for (X in 0 until W) {
                        val xm = if (X > 0) X - 1 else 0; val xp = if (X < W - 1) X + 1 else X
                        val flou = (a[ym + xm] + a[ym + xp] + a[yp + xm] + a[yp + xp] + 2 * (a[ym + X] + a[yp + X] + a[y + xm] + a[y + xp]) + 4 * a[y + X]) / 16f
                        val k = y + X
                        t[k] = (a[k] + (a[k] - flou) * force).coerceIn(mn[ch][k], mx[ch][k])
                    }
                }
                System.arraycopy(t, 0, a, 0, t.size)
            }
        }
        val o = IntArray(W * H)
        for (k in o.indices) {
            var v = 0
            for (ch in 0 until 4) v = v or ((c[ch][k] + 0.5f).toInt().coerceIn(0, 255) shl (24 - 8 * ch))
            o[k] = v
        }
        return o
    }
}
