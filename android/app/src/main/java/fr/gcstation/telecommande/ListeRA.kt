package fr.gcstation.telecommande

/**
 * build 553 : la liste de memoire demandee a la console pour RetroAchievements (console v416+ : compression), sans rien
 * d'Android (essayee sur le PC : ListeRATest).
 *
 * - plages lues par rcheevos (adresse, longueur), 64 octets de marge apres chacune tant que la liste reste aux 3/4 du maximum
 *   (une fois coupee, pour la partie) ;
 * - « pages chaudes » (Super Mario Sunshine) : les pages de 1 Ko ou de nouvelles zones apparaissent sans arret (objets que le jeu
 *   cree et detruit, suivis par pointeurs) sont demandees entieres : les objets suivants y sont deja, plus de pause a chaque
 *   nouvel objet. Une page devient chaude a sa 3e nouvelle zone ; au plus 6 Ko de pages, et la liste aux 3/4 du maximum ;
 * - plages reunies en commencant par les plus proches s'il y a plus de morceaux que la console n'en accepte (morceaux de
 *   4 608 octets au plus : limite de Pulse).
 */
class ListeRA {
    private val vues = HashSet<Int>()                 // (tranches de 16 octets deja vues)
    private val chaleur = HashMap<Int, Int>()         // (page de 1 Ko -> nouvelles tranches apparues)
    private var premiere = true
    var margeCoupee = false; private set
    /** pages demandees entieres a la derniere construction */
    var pagesChaudes = 0; private set

    fun raz() { vues.clear(); chaleur.clear(); premiere = true; margeCoupee = false; pagesChaudes = 0 }

    /** rend les plages a demander (adresse, longueur, a la suite) et les octets voulus (avant la coupe a maxTot) */
    fun construire(q: IntArray, maxTot: Int, maxMorc: Int): Pair<IntArray, Int> {
        val zones = q.size / 2
        val bruts = q.indices.step(2).sumOf { q[it + 1] }
        // (apprendre ou apparaissent les nouvelles zones)
        var k = 0
        while (k + 1 < q.size) {
            var t = q[k] / 16; val tf = (q[k] + q[k + 1] - 1) / 16
            while (t <= tf) { if (vues.add(t) && !premiere) chaleur[t / 64] = (chaleur[t / 64] ?: 0) + 1; t++ }
            k += 2
        }
        premiere = false
        if (margeCoupee || bruts + zones * 64 > maxTot * 3 / 4) margeCoupee = true
        val marge = if (margeCoupee) 0 else 64
        val r = ArrayList<IntArray>()
        k = 0
        while (k + 1 < q.size) { r.add(intArrayOf(q[k], minOf(q[k] + q[k + 1] + marge, 0x01800000) - q[k])); k += 2 }
        // (pages chaudes : les plus chaudes d'abord, dans le budget)
        var budget = minOf(6144, maxTot * 3 / 4 - bruts - (if (marge > 0) zones * marge else 0))
        pagesChaudes = 0
        for ((pg, _) in chaleur.entries.filter { it.value >= 3 }.sortedByDescending { it.value }) {
            if (budget < 1024) break
            val a = pg * 1024
            if (a + 1024 > 0x01800000) continue
            r.add(intArrayOf(a, 1024)); budget -= 1024; pagesChaudes++
        }
        // (dans l'ordre, reunies si elles se touchent)
        r.sortBy { it[0] }
        val a = ArrayList<IntArray>()
        for (x in r) {
            if (a.isNotEmpty() && a.last()[0] + a.last()[1] >= x[0]) a.last()[1] = maxOf(a.last()[1], x[0] + x[1] - a.last()[0])
            else a.add(intArrayOf(x[0], x[1]))
        }
        fun morceaux() = a.sumOf { (it[1] + 4607) / 4608 }
        while (a.size > 1 && morceaux() > maxMorc) {
            var m = 0; var mg = Int.MAX_VALUE
            for (j in 0 until a.size - 1) { val gap = a[j + 1][0] - (a[j][0] + a[j][1]); if (gap < mg) { mg = gap; m = j } }
            a[m] = intArrayOf(a[m][0], a[m + 1][0] + a[m + 1][1] - a[m][0]); a.removeAt(m + 1)
        }
        return IntArray(a.size * 2) { if (it % 2 == 0) a[it / 2][0] else a[it / 2][1] } to a.sumOf { it[1] }
    }

    companion object {
        /** coupe en morceaux de 4 608 octets au plus, maxMorc morceaux et maxTot octets au plus : (adresse, longueur) */
        fun morceaux(p: IntArray, maxTot: Int, maxMorc: Int): List<Pair<Int, Int>> {
            val l = ArrayList<Pair<Int, Int>>(); var tot = 0; var i = 0
            dehors@ while (i + 1 < p.size) {
                var a = p[i]; var reste = minOf(p[i + 1], maxTot - tot)
                while (reste > 0) {
                    if (l.size >= maxMorc) break@dehors
                    val n = minOf(reste, 4608)
                    l.add(a to n); tot += n; a += n; reste -= n
                }
                if (tot >= maxTot) break
                i += 2
            }
            return l
        }
    }
}
