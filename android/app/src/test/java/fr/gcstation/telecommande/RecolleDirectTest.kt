package fr.gcstation.telecommande

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

/**
 * build 532 : le vrai recollage de l'appli (RecolleDirect, celui de MemoireJeu) face a une console simulee comme Pulse
 * v410 (natres.c, ms_send : copies de toute l'image, images depuis le dernier envoi, anneau 2 / 7 / 15 / 31) et a un
 * Wi-Fi avec pertes en rafales, coupures, doubles et retards (paquets dans le desordre).
 * Lancer : gradle -p android testPriveDebugUnitTest
 */
class RecolleDirectTest {
    private val IMG_MS = 1000.0 / 59.94

    /** un paquet tel que Pulse l'envoie */
    private class Paquet(val t: Double, val octets: ByteArray)

    private fun p32(b: ByteArray, o: Int, v: Long) { b[o] = (v shr 24).toByte(); b[o + 1] = (v shr 16).toByte(); b[o + 2] = (v shr 8).toByte(); b[o + 3] = v.toByte() }

    /** donnees de l'image v (differentes a chaque image) */
    /** donnees de l'image v ; compression : presque tout fixe, quelques octets qui changent (comme la memoire d'un jeu) */
    private var peuDeChangements = false
    private fun donnees(v: Long, taille: Int) = if (peuDeChangements) ByteArray(taille) { i -> (if (i % 97 == 0 || i < 8) (v * 131 + i * 7) and 255 else (i * 13 + 5) and 255).toByte() }
        else ByteArray(taille) { i -> ((v * 131 + i * 7 + (v shr 8)) and 255).toByte() }

    /** comme Pulse v416 (ms_code) : image codee : en-tete, puis complete ou changements depuis la reference */
    private fun coder(v: Long, d: ByteArray, refImg: Long, ref: ByteArray?): ByteArray {
        val o = java.io.ByteArrayOutputStream()
        val h = ByteArray(16); var s1 = 0L; var s2 = 0L
        for (b in d) { s1 = (s1 + (b.toLong() and 255)) and 0xFFFFFFFFL; s2 = (s2 + s1) and 0xFFFFFFFFL }
        p32(h, 0, if (ref == null) v else refImg); p32(h, 4, d.size.toLong()); p32(h, 8, s1); p32(h, 12, s2)
        o.write(h)
        if (ref == null) { o.write(d); return o.toByteArray() }
        var pos = 0; var saut = 0
        while (pos < d.size) {
            if (d[pos] == ref[pos]) { saut++; pos++; continue }
            var nb = 0; while (pos + nb < d.size && d[pos + nb] != ref[pos + nb]) nb++
            o.write(saut shr 8); o.write(saut and 255); o.write(nb shr 8); o.write(nb and 255); o.write(d, pos, nb)
            pos += nb; saut = 0
        }
        return o.toByteArray()
    }

    /**
     * console comme Pulse v410 : taille octets par image, copies envois en plus de toute l'image (ms_dup), anneau (ms_ann),
     * pasEnvoyee(v) : image que la console n'envoie pas (bus pris par le jeu). Paquets de 1400 octets de donnees au plus.
     */
    private fun console(v0: Long, nImg: Int, taille: Int, copies: Int, anneau: Boolean, pasEnvoyee: (Long) -> Boolean = { false },
                        compresse: Boolean = false): List<Paquet> {
        if (compresse) return consoleCompresse(v0, nImg, taille, pasEnvoyee)
        // (comme Pulse v413 : toute l'image gardee dans sa case ; copies 2, 7, 15, 31 images plus tard si l'anneau les garde ;
        //  image pas envoyee (bus pris) : gardee, et envoyee des le prochain envoi)
        val out = ArrayList<Paquet>()
        val parts = (taille + 1399) / 1400
        val sz = (8 + parts * 18 + taille + 3) and 3.inv(); val n = if (anneau) minOf(32, RecolleDirect.ANNEAU / sz) else 0
        val ring = HashMap<Long, Pair<Long, List<ByteArray>>>()   // (case -> image, paquets)
        val enAttente = HashSet<Long>()
        var prev = v0 - 1
        for (v in v0 until v0 + nImg) {
            val muet = pasEnvoyee(v)
            var t = (v - v0) * IMG_MS
            fun rejoue(copiesAussi: Boolean) {
                for (sl in 0 until n.toLong()) {
                    val (iv, pk) = ring[sl] ?: continue
                    val age = v - iv
                    if (age <= 0 || age > 255 || (age >= n && iv !in enAttente)) continue
                    if (iv !in enAttente && (!copiesAussi || age !in listOf(2L, 7L, 15L, 31L))) continue
                    enAttente -= iv
                    for (b in pk) { out += Paquet(t, b.copyOf()); t += 0.12 }
                }
            }
            if (!muet && n >= 3) rejoue(false)               // (d'abord celles pas envoyees : la case de cette image peut etre la leur)
            val d = donnees(v, taille)
            val dv = minOf(255L, v - prev)
            val premiers = ArrayList<ByteArray>()
            for (pass in 0..(if (muet) 0 else copies)) for (part in 0 until parts) {
                val o = part * 1400; val k = minOf(1400, taille - o)
                val b = ByteArray(16 + k)
                b[0] = 'G'.code.toByte(); b[1] = 'C'.code.toByte(); b[2] = 'M'.code.toByte(); b[3] = 'S'.code.toByte()
                p32(b, 4, v); p32(b, 8, (part.toLong() shl 16) or parts.toLong()); p32(b, 12, taille.toLong() or (if (copies > 0) dv shl 24 else 0))
                System.arraycopy(d, o, b, 16, k)
                if (pass == 0) premiers += b
                if (!muet) { out += Paquet(t, b); t += 0.12 }
            }
            if (n >= 3) { ring[v % n] = v to premiers; if (muet) enAttente += v }
            if (muet) continue
            prev = v
            if (n >= 3) rejoue(true)
        }
        return out
    }

    /** console v416 avec compression : references toutes les 30 images, journal de 32 images, en attente renvoyees au prochain envoi */
    private fun consoleCompresse(v0: Long, nImg: Int, taille: Int, pasEnvoyee: (Long) -> Boolean): List<Paquet> {
        val out = ArrayList<Paquet>()
        val journal = HashMap<Long, ByteArray>(); val enAttente = HashSet<Long>()
        var refImg = -1L; var ref: ByteArray? = null
        fun emettre(v: Long, e: ByteArray, t0: Double): Double {
            var t = t0
            val parts = (e.size + 1399) / 1400
            for (q in 0 until parts) {
                val o = q * 1400; val k = minOf(1400, e.size - o)
                val b = ByteArray(16 + k)
                b[0] = 'G'.code.toByte(); b[1] = 'C'.code.toByte(); b[2] = 'M'.code.toByte(); b[3] = 'S'.code.toByte()
                p32(b, 4, v); p32(b, 8, (q.toLong() shl 16) or (5L shl 8) or 0x80L or parts.toLong()); p32(b, 12, e.size.toLong() or (1L shl 24))
                System.arraycopy(e, o, b, 16, k)
                out += Paquet(t, b); t += 0.12
            }
            return t
        }
        for (v in v0 until v0 + nImg) {
            val muet = pasEnvoyee(v)
            var t = (v - v0) * IMG_MS
            if (!muet) for (iv in enAttente.sorted()) { journal[iv]?.let { t = emettre(iv, it, t) } }
            if (!muet) enAttente.clear()
            val d = donnees(v, taille)
            val cle = ref == null || v - refImg >= 30 || !journal.containsKey(refImg)
            val e = if (cle) coder(v, d, v, null) else coder(v, d, refImg, ref)
            if (cle) { refImg = v; ref = d }
            journal[v] = e; journal.remove(v - 32)
            if (muet) { enAttente += v; continue }
            t = emettre(v, e, t)
            for (k in longArrayOf(2, 7, 15, 31)) journal[v - k]?.let { t = emettre(v - k, it, t) }
        }
        return out
    }

    /** Wi-Fi : pertes de Gilbert-Elliott dans le temps (rafales), coupures periodiques, doubles, retards (desordre) */
    private class WiFi(val rnd: Random, val pPerte: Double = 0.0, val rafaleMs: Double = 0.0, val coupureMs: Double = 0.0,
                       val coupuresParS: Int = 0, val pDouble: Double = 0.0, val retardMaxMs: Double = 0.0) {
        private var mauvais = false; private var tEtat = 0.0
        private val coupures = ArrayList<Double>()
        fun passe(p: List<Paquet>): List<Paquet> {
            val fin = (p.lastOrNull()?.t ?: 0.0) + 1000
            var t = 0.0
            while (coupuresParS > 0 && t < fin) { repeat(coupuresParS) { coupures += t + rnd.nextDouble() * 1000 }; t += 1000 }
            val r = ArrayList<Paquet>()
            for (x in p) {
                // (etat mauvais : rafales d'en moyenne rafaleMs ; pPerte du temps en tout)
                while (rafaleMs > 0 && tEtat < x.t) {
                    val moy = if (mauvais) rafaleMs else rafaleMs * (1 - pPerte) / pPerte
                    tEtat += -moy * Math.log(1 - rnd.nextDouble()); if (tEtat < x.t) mauvais = !mauvais
                }
                val perdu = (rafaleMs > 0 && mauvais) || (rafaleMs == 0.0 && rnd.nextDouble() < pPerte) ||
                    coupures.any { x.t >= it && x.t < it + coupureMs }
                if (perdu) continue
                val n = if (rnd.nextDouble() < pDouble) 2 else 1
                repeat(n) { r += Paquet(x.t + 1.0 + rnd.nextDouble() * retardMaxMs, x.octets.copyOf()) }
            }
            return r.sortedBy { it.t }
        }
    }

    private class Resultat(val images: List<Long>, val retard: Map<Long, Double>, val nonEnvoyees: Long)

    private fun recevoir(paquets: List<Paquet>, taille: Int, fen: Int, v0: Long, numero: Boolean = false): Resultat {
        val vues = ArrayList<Long>(); val retard = HashMap<Long, Double>()
        var tNow = 0.0
        val r = RecolleDirect { img, d, _ ->
            assertArrayEquals("donnees de l'image $img", donnees(img, taille), d)
            vues += img; retard[img] = tNow - (img - v0) * IMG_MS
        }
        r.fen = fen; r.avecNumero = numero
        for (p in paquets) { tNow = p.t; r.paquet(p.octets, p.octets.size) }
        return Resultat(vues, retard, r.nonEnvoyees)
    }

    private fun dansLOrdre(v: List<Long>) {
        for (i in 1 until v.size) assertTrue("image ${v[i]} donnee apres ${v[i - 1]}", v[i] > v[i - 1])
    }

    @Test fun fenetres() {
        assertEquals(3, RecolleDirect.fenetre(false, 3000))       // (sans anneau : 3 images de desordre)
        assertEquals(3, RecolleDirect.fenetre(true, 4096))        // (3 paquets, 3 cases : copie 2 images plus tard)
        assertEquals(32, RecolleDirect.fenetre(true, 4096, true, true))   // (v416, compression : jusqu'a 32 images)
        assertEquals(3, RecolleDirect.fenetre(true, 16000))       // (2 cases seulement : pas d'anneau)
        assertEquals(0, RecolleDirect.fenetre(false, 3000, false))  // (avec un ecran abonne : tout de suite)
        assertEquals(32, RecolleDirect.fenetre(true, 8))        // (32 cases : copies jusqu'a 31 images plus tard)
        assertEquals(8, RecolleDirect.fenetre(true, 1380))      // (11 cases : 2 et 7)
        assertEquals(16, RecolleDirect.fenetre(true, 600))      // (26 cases : 2, 7, 15)
    }

    @Test fun sansPerte_toutesLesImages_dansLOrdre() {
        for ((taille, ann) in listOf(8 to true, 300 to true, 1380 to true, 3000 to false, 4096 to true)) {
            val p = console(1000, 600, taille, 1, ann)
            val res = recevoir(p, taille, RecolleDirect.fenetre(ann, taille), 1000)
            dansLOrdre(res.images)
            // (les toutes dernieres peuvent attendre encore leurs copies)
            assertTrue("$taille octets : ${res.images.size}", res.images.size >= 600 - RecolleDirect.fenetre(ann, taille))
            assertEquals(1000L, res.images.first())
            assertEquals(0L, res.nonEnvoyees)
        }
    }

    @Test fun imagesPasEnvoyees_rattrapees() {
        // (console v413 : une image pas envoyee (bus pris) est gardee et repart au prochain envoi : aucune ne manque)
        val pas = { v: Long -> v % 50 in 10L..12L }
        for (taille in listOf(300, 4096)) {
            val res = recevoir(console(1000, 1000, taille, 1, true, pas), taille, RecolleDirect.fenetre(true, taille), 1000)
            dansLOrdre(res.images)
            val derniere = res.images.last()
            assertEquals("$taille octets", (derniere - 1000 + 1).toInt(), res.images.size)
        }
    }

    @Test fun imagesPasEnvoyees_sansAnneau_compteesAPart() {
        val pas = { v: Long -> v % 50 in 10L..12L }
        val p = console(1000, 1000, 3000, 1, false, pas)
        val res = recevoir(p, 3000, RecolleDirect.fenetre(false, 3000), 1000)
        dansLOrdre(res.images)
        val derniere = res.images.last()
        assertEquals((1001L..derniere).count { pas(it) }.toLong(), res.nonEnvoyees)
        assertTrue(res.images.none(pas))
    }

    @Test fun grosseImage_4Ko_pertes() {
        // (Wind Waker : ~4 Ko par image, 3 paquets ; console v413 : copie 2 images plus tard)
        val taille = 4096; val n = 60 * 120
        val w = WiFi(Random(13), pPerte = 0.005, rafaleMs = 4.0, coupureMs = 20.0, coupuresParS = 1, pDouble = 0.02, retardMaxMs = 6.0)
        val im = recevoir(w.passe(console(1, n, taille, 1, true)), taille, RecolleDirect.fenetre(true, taille), 1).images
        dansLOrdre(im)
        val taux = im.size.toDouble() / im.last()
        println("4 Ko par image, une coupure de 20 ms par seconde : %.3f %% recues".format(taux * 100))
        assertTrue("recues : %.3f %%".format(taux * 100), taux >= 0.99)
    }

    @Test fun doublesEtDesordre_uneSeuleFoisDansLOrdre() {
        val rnd = Random(7)
        for ((taille, ann) in listOf(300 to true, 1380 to true, 4000 to false)) {
            val w = WiFi(rnd, pDouble = 0.3, retardMaxMs = 30.0)    // (jusqu'a 30 ms : pres de 2 images de desordre)
            val res = recevoir(w.passe(console(5000, 1200, taille, 1, ann)), taille, RecolleDirect.fenetre(ann, taille), 5000)
            dansLOrdre(res.images)
            assertEquals(res.images.size, res.images.toSet().size)
            assertTrue("$taille octets : ${res.images.size} / 1200", res.images.size >= 1200 - RecolleDirect.fenetre(ann, taille))
        }
    }

    @Test fun pertesEnRafales_anneau() {
        // (3 coupures de 40 ms par seconde, plus 1 % du temps perdu en rafales de 5 ms, doubles, desordre de 6 ms)
        val rnd = Random(11)
        val taille = 300; val n = 60 * 120
        val w = WiFi(rnd, pPerte = 0.01, rafaleMs = 5.0, coupureMs = 40.0, coupuresParS = 3, pDouble = 0.02, retardMaxMs = 6.0)
        val fen = RecolleDirect.fenetre(true, taille)
        val res = recevoir(w.passe(console(1, n, taille, 1, true)), taille, fen, 1)
        dansLOrdre(res.images)
        val taux = res.images.size.toDouble() / (res.images.last() - 1 + 1)
        println("anneau, 300 octets, coupures de 40 ms : %.3f %% recues".format(taux * 100))
        assertTrue("recues : %.3f %%".format(taux * 100), taux >= 0.999)
        // (attente : jamais plus que la fenetre, plus le desordre du Wi-Fi)
        val pire = res.retard.values.maxOrNull() ?: 0.0
        assertTrue("attente %.0f ms".format(pire), pire <= (fen + 2) * IMG_MS)
    }

    @Test fun coupuresDe120ms_anneau() {
        val taille = 300; val n = 60 * 300
        val w = WiFi(Random(5), pPerte = 0.02, rafaleMs = 8.0, coupureMs = 120.0, coupuresParS = 1, pDouble = 0.02, retardMaxMs = 6.0)
        val im = recevoir(w.passe(console(1, n, taille, 1, true)), taille, RecolleDirect.fenetre(true, taille), 1).images
        dansLOrdre(im)
        val taux = im.size.toDouble() / im.last()
        println("anneau, 300 octets, une coupure de 120 ms par seconde : %.3f %% recues".format(taux * 100))
        assertTrue("recues : %.3f %%".format(taux * 100), taux >= 0.995)
    }

    @Test fun pertesEnRafales_sansAnneau_moinsBien() {
        val taille = 300; val n = 60 * 120
        fun essai(ann: Boolean): Double {
            val w = WiFi(Random(11), pPerte = 0.01, rafaleMs = 5.0, coupureMs = 40.0, coupuresParS = 3, pDouble = 0.02, retardMaxMs = 6.0)
            val fen = RecolleDirect.fenetre(ann, taille)
            val im = recevoir(w.passe(console(1, n, taille, if (ann) 1 else 2, ann)), taille, fen, 1).images
            return im.size.toDouble() / im.last()
        }
        val avec = essai(true); val sans = essai(false)
        println("avec anneau %.3f %%, copies seules %.3f %%".format(avec * 100, sans * 100))
        assertTrue(avec > sans)
    }

    @Test fun imageEnPlusieursPaquets_desordre() {
        val rnd = Random(3)
        val taille = 5000                                       // (4 paquets par image)
        // (melanges par groupes de 6 paquets : des morceaux de deux images voisines se croisent)
        val p = console(100, 300, taille, 1, false).chunked(6).flatMap { it.shuffled(rnd) }.mapIndexed { i, x -> Paquet(i.toDouble(), x.octets) }
        val res = recevoir(p, taille, RecolleDirect.fenetre(false, taille), 100)
        dansLOrdre(res.images)
        assertTrue("${res.images.size} / 300", res.images.size >= 300 - 3)
    }

    @Test fun jeuRelance_compteurRepart() {
        val a = console(9000, 100, 300, 1, true)
        val b = console(1, 100, 300, 1, true).map { Paquet(it.t + 5000, it.octets) }
        val res = recevoir(a + b, 300, RecolleDirect.fenetre(true, 300), 9000)
        val apres = res.images.filter { it < 9000 }
        assertTrue("images du jeu relance : ${apres.size}", apres.size >= 100 - 32)
        dansLOrdre(apres)
    }

    @Test fun numeroDAbonnement() {
        // (console v413, option bit 4 : morceau << 16 | numero << 8 | morceaux)
        var vu = -2
        val r = RecolleDirect { _, _, ab -> vu = ab }
        r.avecNumero = true
        val b = ByteArray(24); b[0] = 'G'.code.toByte(); p32(b, 4, 7); p32(b, 8, (0x2AL shl 8) or 1); p32(b, 12, 8); r.paquet(b, 24)
        assertEquals(0x2A, vu)
    }

    @Test fun compression_imagesJustes_pertesEtBusPris() {
        // (console v416 : references + changements ; Wi-Fi avec coupures, doubles, desordre ; bus pris 8 images d'affilee)
        peuDeChangements = true
        try {
            val taille = 2736; val n = 60 * 60
            val pas = { v: Long -> v % 400 in 100L..107L }
            val w = WiFi(Random(17), pPerte = 0.01, rafaleMs = 5.0, coupureMs = 40.0, coupuresParS = 1, pDouble = 0.02, retardMaxMs = 6.0)
            val res = recevoir(w.passe(console(1, n, taille, 0, true, pas, compresse = true)), taille, RecolleDirect.fenetre(true, taille, true, true), 1, numero = true)
            dansLOrdre(res.images)                                // (et chaque image donnee = la bonne : verifie dans recevoir)
            val taux = res.images.size.toDouble() / res.images.last()
            println("compression, 2 736 octets, coupures et bus pris : %.3f %% recues".format(taux * 100))
            assertTrue("recues : %.3f %%".format(taux * 100), taux >= 0.999)
        } finally { peuDeChangements = false }
    }

    @Test fun compression_tailleReduite() {
        peuDeChangements = true
        try {
            val p = console(1, 300, 2736, 0, true, compresse = true)
            // (taille de chaque image codee, une fois : mot 12 des paquets, bits 0-23)
            val tailles = p.groupBy { (it.octets[4].toLong() and 255 shl 24) or (it.octets[5].toLong() and 255 shl 16) or (it.octets[6].toLong() and 255 shl 8) or (it.octets[7].toLong() and 255) }
                .mapValues { (_, l) -> (l[0].octets[13].toInt() and 255 shl 16) or (l[0].octets[14].toInt() and 255 shl 8) or (l[0].octets[15].toInt() and 255) }
            val moy = tailles.values.average()
            println("compression : %.0f octets par image en moyenne au lieu de 2 736 (%.1f %%)".format(moy, moy * 100 / 2736))
            assertTrue(moy < 2736 / 4)
        } finally { peuDeChangements = false }
    }

    @Test fun compression_sommeFausse_refusee() {
        // (jamais une fausse valeur : un octet change dans les changements, la somme ne va plus : image refusee)
        val r = RecolleDirect { _, _, _ -> }
        val ref = ByteArray(100) { (it * 3).toByte() }
        assertTrue(r.decoder(10, coder(10, ref, 10, null)) != null)
        val d = ref.copyOf().also { it[40] = 99 }
        val e = coder(11, d, 10, ref)
        assertArrayEquals(d, r.decoder(11, e))
        val abime = e.copyOf().also { it[e.size - 1] = (it[e.size - 1] + 1).toByte() }
        assertEquals(null, r.decoder(11, abime))
        val sansRef = coder(12, d, 9, ref)                       // (reference 9 jamais recue)
        assertEquals(null, r.decoder(12, sansRef))
    }

    @Test fun paquetsAbimes_ignores() {
        val r = RecolleDirect { _, _, _ -> throw AssertionError("rien ne doit sortir") }
        r.paquet(ByteArray(8), 8)
        val b = ByteArray(24); p32(b, 4, 5); p32(b, 8, (3L shl 16) or 2); p32(b, 12, 8); r.paquet(b, 24)    // (morceau 3 sur 2)
        p32(b, 8, 1); p32(b, 12, 20); r.paquet(b, 24)                                                       // (taille plus grande que les donnees)
        p32(b, 8, 17); r.paquet(b, 24)                                                                      // (17 morceaux)
    }
}
