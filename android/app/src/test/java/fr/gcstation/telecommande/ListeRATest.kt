package fr.gcstation.telecommande

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

/**
 * build 553 : la liste de memoire demandee pour RetroAchievements (ListeRA, celle de l'appli).
 * Sunshine simule : des objets de 64 octets crees sans arret dans 4 pages de 1 Ko (rcheevos les lit par pointeurs : une
 * nouvelle zone a chaque objet), plus 30 zones fixes eparpillees. Mesure : combien de fois une zone lue n'etait pas encore
 * dans la liste (= une pause de la verification).
 */
class ListeRATest {
    private fun plages(z: Collection<Int>): IntArray {      // (tranches de 16 octets -> plages, comme nPlages(.., 0))
        val t = z.sorted(); val r = ArrayList<Int>()
        for (x in t) { val a = x * 16; if (r.size >= 2 && r[r.size - 2] + r[r.size - 1] == a) r[r.size - 1] += 16 else { r.add(a); r.add(16) } }
        return r.toIntArray()
    }
    private fun couvre(p: List<Pair<Int, Int>>, tranche: Int) = p.any { tranche * 16 >= it.first && tranche * 16 + 16 <= it.first + it.second }

    private fun simuler(avecChaudes: Boolean): Pair<Int, Int> {
        val rnd = Random(3)
        val fixes = (0 until 30).map { 0x100000 / 16 + it * 4096 }.toSet()
        val pages = listOf(0x400, 0x401, 0x402, 0x403)          // (pages de 1 Ko des objets)
        val vivants = ArrayDeque<Int>()
        val l = ListeRA()
        var liste = emptyList<Pair<Int, Int>>()
        var pauses = 0; var lectures = 0
        for (img in 0 until 3000) {
            if (img % 6 == 0) {                                     // (un objet nait ; le plus vieux disparait)
                val pg = pages[rnd.nextInt(pages.size)]
                vivants.addLast(pg * 64 + rnd.nextInt(16) * 4)
                if (vivants.size > 12) vivants.removeFirst()
            }
            val lues = fixes + vivants.flatMap { listOf(it, it + 1, it + 2, it + 3) }
            if (img >= 600) for (t in lues) { lectures++; if (!couvre(liste, t)) pauses++ }   // (apres 10 s : pages apprises)
            if (img % 3 == 0) {                                     // (l'appli renouvelle la liste)
                val (p, _) = if (avecChaudes) l.construire(plages(lues), 12288, 256) else ListeRA().also { it.construire(IntArray(0), 12288, 256) }.construire(plages(lues), 12288, 256)
                liste = ListeRA.morceaux(p, 12288, 256)
            }
        }
        return pauses to lectures
    }

    @Test fun pagesChaudes_moinsDePauses() {
        val (sans, n) = simuler(false)
        val (avec, _) = simuler(true)
        println("Sunshine simule, apres 10 s : zones pas encore dans la liste : %d sans pages chaudes, %d avec (sur %d lectures)".format(sans, avec, n))
        assertTrue("avec $avec, sans $sans", avec * 4 < sans || avec == 0)
    }

    @Test fun limites_respectees() {
        val l = ListeRA()
        val rnd = Random(9)
        repeat(50) {
            val z = (0 until 400).map { rnd.nextInt(0x180000) }.toSet()
            val (p, _) = l.construire(plages(z), 12288, 256)
            val m = ListeRA.morceaux(p, 12288, 256)
            assertTrue(m.size <= 256)
            assertTrue(m.sumOf { it.second } <= 12288)
            assertTrue(m.all { it.second in 1..4608 && it.first >= 0 && it.first + it.second <= 0x01800000 })
            for (i in 1 until m.size) assertTrue(m[i].first >= m[i - 1].first + m[i - 1].second)   // (dans l'ordre, sans chevauchement)
        }
    }

    @Test fun petitJeu_marge_sansPageChaude() {
        // (Wind Waker : ~20 zones fixes : marge de 64 octets, aucune page chaude, liste stable)
        val l = ListeRA()
        val z = (0 until 20).map { 0x200000 / 16 + it * 300 }.toSet()
        var p0 = IntArray(0)
        repeat(10) { p0 = l.construire(plages(z), 12288, 256).first }
        assertEquals(0, l.pagesChaudes)
        assertEquals(20 * (16 + 64), p0.indices.step(2).sumOf { p0[it + 1] })
    }
}
