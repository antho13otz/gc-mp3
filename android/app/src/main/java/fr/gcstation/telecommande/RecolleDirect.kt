package fr.gcstation.telecommande

/**
 * build 532 : recollage et remise en ordre du direct de Pulse (paquets "GCMS"), sorti de MemoireJeu sans rien
 * d'Android pour etre essaye sur le PC (android/app/src/test : pertes, doubles, retards, anneau de la console).
 *
 * Paquet : {"GCMS", image, morceau << 16 | morceaux, taille (bits 0-23) | images depuis le dernier envoi (24-31, v408)},
 * puis les donnees. Une image n'est donnee qu'une fois, dans l'ordre, jamais apres une plus recente ; une image complete
 * attend qu'une plus ancienne arrive (copies retardees de l'anneau, console v409+) au plus `fen` images.
 */
class RecolleDirect(private val donner: (Long, ByteArray, Int) -> Unit) {
    /** build 539 (console v413, option bit 4) : les paquets portent le numero de l'abonnement (bits 8-15 du mot des morceaux) ;
     *  donne avec l'image (-1 : sans numero) */
    @Volatile var avecNumero = false
    /** build 545 : une image compressee est arrivee (console v416) */
    @Volatile var codeVu = false
    /** build 545 : images compressees refusees (image de reference absente, ou somme fausse : jamais une fausse valeur) */
    @Volatile var refusees = 0L
    /** build 545 : octets recus pour les images compressees, et octets des images refaites (taux de compression) */
    @Volatile var octetsCodes = 0L
    @Volatile var octetsBruts = 0L
    private val refs = LinkedHashMap<Long, ByteArray>()

    /**
     * build 545 (console v416) : refait une image compressee. En-tete (16 octets) : image de reference (elle-meme : image
     * complete), octets de l'image, sommes s1 (des octets) et s2 (des s1) ; puis l'image complete, ou des changements depuis
     * l'image de reference (sauter, nombre sur 2 octets chacun, puis les octets). null : impossible ou somme fausse.
     */
    fun decoder(img: Long, e: ByteArray): ByteArray? {
        if (e.size < 16) return null
        val ri = g32(e, 0); val n = g32(e, 4).toInt()
        if (n < 0 || n > 65536) return null
        val raw: ByteArray
        if (ri == img) {
            if (e.size != 16 + n) return null
            raw = e.copyOfRange(16, 16 + n)
        } else {
            val base = refs[ri] ?: return null
            if (base.size != n) return null
            raw = base.copyOf()
            var o = 16; var pos = 0
            while (o < e.size) {
                if (o + 4 > e.size) return null
                val saut = (e[o].toInt() and 255 shl 8) or (e[o + 1].toInt() and 255); val nb = (e[o + 2].toInt() and 255 shl 8) or (e[o + 3].toInt() and 255)
                o += 4; pos += saut
                if (pos + nb > n || o + nb > e.size) return null
                System.arraycopy(e, o, raw, pos, nb); o += nb; pos += nb
            }
        }
        var s1 = 0L; var s2 = 0L
        for (b in raw) { s1 = (s1 + (b.toLong() and 255)) and 0xFFFFFFFFL; s2 = (s2 + s1) and 0xFFFFFFFFL }
        if (s1 != g32(e, 8) || s2 != g32(e, 12)) return null
        if (ri == img) { refs[img] = raw; while (refs.size > 4) refs.remove(refs.keys.first()) }
        octetsCodes += e.size; octetsBruts += n
        return raw
    }

    /** build 541 : un paquet avec un numero d'abonnement est arrive : la console est v413 ou plus (anneau pour toutes les images) */
    @Volatile var numeroVu = false
    // build 525 : quelques images en cours de recollage (un paquet en retard ou son double arrive apres le debut de la suivante)
    private val enCours = LinkedHashMap<Long, Array<ByteArray?>>()
    private var fait = -1L
    // build 529 : images completes en attente, donnees quand elles suivent la derniere donnee, ou au plus tard fen images apres
    private class Pret(val d: ByteArray, val dv: Int, val ab: Int, val code: Boolean)
    private val pretes = java.util.TreeMap<Long, Pret>()
    /** attente la plus longue (images) ; 0 : sans anneau, chaque image complete est donnee tout de suite */
    @Volatile var fen = 0
    /** build 528 (console v408) : images que la console n'a pas envoyees du tout (bus pris par le jeu), a ne pas compter
     *  comme perdues en route ; remis a 0 par RetroSucces au debut d'une partie */
    @Volatile var nonEnvoyees = 0L
    /** derniere image donnee (-1 : aucune) */
    val derniere: Long get() = fait

    private fun g32(b: ByteArray, o: Int) = (b[o].toLong() and 255 shl 24) or (b[o + 1].toLong() and 255 shl 16) or (b[o + 2].toLong() and 255 shl 8) or (b[o + 3].toLong() and 255)

    /** un paquet "GCMS" de n octets (en-tete compris) */
    fun paquet(buf: ByteArray, n: Int) {
        if (n < 16) return
        val img = g32(buf, 4); val pp = g32(buf, 8); val part = (pp shr 16).toInt(); val parts = (pp and (if (avecNumero) 0x7F else 0xFFFF)).toInt()
        val ab = if (avecNumero) (pp shr 8 and 0xFF).toInt() else -1
        val code = avecNumero && (pp and 0x80L) != 0L                     // (build 545, console v416 : image compressee)
        if (code) codeVu = true
        if (ab > 0) numeroVu = true
        val tt = g32(buf, 12); val tot = (tt and 0xFFFFFF).toInt(); val dv = (tt ushr 24).toInt()   // (v408 : images depuis le dernier envoi)
        if (parts !in 1..16 || part >= parts || tot > 65536) return
        if (fait >= 0 && img < fait - 600) { fait = -1L; enCours.clear(); pretes.clear(); refs.clear() }   // (le compteur d'images est reparti : jeu relance)
        if (img <= fait || pretes.containsKey(img)) return                    // (double, ou image deja donnee / deja complete)
        var rParts = enCours[img]
        if (rParts == null || rParts.size != parts) {
            rParts = arrayOfNulls(parts); enCours[img] = rParts
            while (enCours.size > 40) enCours.remove(enCours.keys.first())   // (build 539 : copies de l'anneau en plusieurs paquets)
        }
        if (rParts[part] != null) return
        rParts[part] = buf.copyOfRange(16, n)
        if (rParts.any { it == null }) return
        val d = ByteArray(tot); var o = 0
        for (q in rParts) { val x = q!!; val k = minOf(x.size, tot - o); System.arraycopy(x, 0, d, o, k); o += k }
        enCours.remove(img)
        if (o < tot) return
        pretes[img] = Pret(d, dv, ab, code)
        sortir()
    }

    private fun sortir() {
        if (pretes.isEmpty()) return
        val plusRecente = pretes.lastKey()
        while (pretes.isNotEmpty()) {
            val k = pretes.firstKey()
            if (fait >= 0 && k != fait + 1 && plusRecente - k < fen) break      // (une plus ancienne peut encore arriver)
            val pr = pretes.pollFirstEntry()!!.value
            if (fait >= 0 && pr.dv in 2..254) nonEnvoyees += minOf(k - fait - 1, (pr.dv - 1).toLong()).coerceAtLeast(0L)
            fait = k
            enCours.keys.removeAll { it <= k }
            val dd = if (pr.code) decoder(k, pr.d) else pr.d
            if (dd != null) donner(k, dd, pr.ab) else refusees++
        }
    }

    companion object {
        /** build 539 : cases de l'anneau de la console v413 pour une image de ces octets (moins de 3 : pas d'anneau) */
        /** octets de l'anneau de la console (v415 : 40 Ko ; v410-v414 : 16 Ko : une console plus ancienne garde moins d'images,
         *  l'appli attend alors un peu plus que necessaire : sans effet sur ce qui est recu) */
        const val ANNEAU = 16384                                   // (v416 : 16 Ko de nouveau, images compressees)
        fun cases(octets: Int): Int { val np = (octets + 1399) / 1400; return minOf(32, ANNEAU / ((8 + np * 18 + octets + 3) and 3.inv())) }
        /** build 531 (console v410) : attente pour l'anneau : la copie la plus tardive que la console garde (2, 7, 15 ou 31
         *  images plus tard, cases de NATRES_MSR_BYTES = 16 Ko a la taille de l'image, 32 au plus), +1.
         *  build 532 : sans anneau, 3 images (50 ms) si seul RetroAchievements lit (essai sur le PC : une image de plusieurs
         *  paquets doublee arrivait parfois apres la suivante, et etait perdue) ; 0 si un ecran s'abonne aussi (tout de suite) */
        fun fenetre(anneau: Boolean, octets: Int, seulRA: Boolean = true, compresse: Boolean = false): Int {
            if (anneau && compresse) return 32                // (build 545, console v416 : le journal garde jusqu'a 32 images)
            // (build 539, console v413 : toutes les images vont dans l'anneau : case = 8 + paquets x 18 + octets, 3 cases au moins)
            val n = if (!anneau) 0 else cases(octets)
            if (n < 3) return if (seulRA) 3 else 0
            return (listOf(2, 7, 15, 31).filter { it < n }.maxOrNull() ?: 0) + 1
        }
    }
}
