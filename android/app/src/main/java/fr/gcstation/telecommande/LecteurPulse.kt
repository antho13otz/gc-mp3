package fr.gcstation.telecommande

// build 486 : sorti de Compagnon.kt (code commun : la vue 3D de l'appli publique s'en sert)

/** lecture des valeurs du jeu pendant un passage : chaque bloc de 1 Ko n'est demande qu'une fois, et au plus [budget]
 *  blocs par passage (le jeu repond aux demandes pendant son image : peu de blocs = pas de ralentissement du jeu) */
class Lecteur(private val budget: Int = 24, private val pauseMs: Long = 3) {
    private val cache = HashMap<Long, ByteArray?>()
    var demandes = 0; private set
    private fun octets(a: Long, n: Int): ByteArray? {
        if (a < MemoireJeu.BASE || a + n > MemoireJeu.BASE + MemoireJeu.FIN) return null
        val b = a and (MemoireJeu.BLK - 1).toLong().inv()
        if (a + n > b + MemoireJeu.BLK) return null               // (valeurs alignees : jamais sur deux blocs)
        val d = if (cache.containsKey(b)) cache[b] else {
            if (demandes >= budget) return null
            demandes++
            if (demandes > 1 && pauseMs > 0) try { Thread.sleep(pauseMs) } catch (e: InterruptedException) { }   // (au plus ~1 bloc par image du jeu)
            MemoireJeu.bloc(b).also { cache[b] = it }
        } ?: return null
        val o = (a - b).toInt()
        return d.copyOfRange(o, o + n)
    }
    fun u8(a: Long): Int? = octets(a, 1)?.let { it[0].toInt() and 255 }
    fun u16(a: Long): Int? = octets(a, 2)?.let { ((it[0].toInt() and 255) shl 8) or (it[1].toInt() and 255) }
    fun s16(a: Long): Int? = u16(a)?.toShort()?.toInt()
    fun u32(a: Long): Long? = octets(a, 4)?.let {
        ((it[0].toLong() and 255) shl 24) or ((it[1].toLong() and 255) shl 16) or ((it[2].toLong() and 255) shl 8) or (it[3].toLong() and 255)
    }
    fun s32(a: Long): Int? = u32(a)?.toInt()
    fun f32(a: Long): Float? = u32(a)?.let { java.lang.Float.intBitsToFloat(it.toInt()) }
    /** pointeur vers la memoire du jeu, sinon null */
    fun ptr(a: Long): Long? = u32(a)?.takeIf { it >= 0x80003100L && it < MemoireJeu.BASE + MemoireJeu.FIN }
    fun texte(a: Long, n: Int): String? = octets(a, n)?.let { b ->
        var k = 0
        while (k < n && b[k] != 0.toByte()) k++
        String(b, 0, k, Charsets.ISO_8859_1)
    }
    /** n octets (chacun lu a part : une plage peut passer d'un bloc a l'autre) ; null si un seul manque */
    fun plage(a: Long, n: Int): IntArray? { val r = IntArray(n); for (i in 0 until n) r[i] = u8(a + i) ?: return null; return r }
    fun fini(f: Float?, max: Float = 1e6f): Float? = f?.takeIf { it.isFinite() && Math.abs(it) < max }
}
