package fr.gcstation.telecommande

// Appli build 173 : d'autres jeux pour le compagnon (meme fiche que Jeux2.kt). Seulement des adresses et des tailles
// de structures, chaque valeur bornee ; une version inconnue n'affiche rien.

// ---------------------------------------------------------------- Mario Party 5
// mariopartyrd/marioparty5 (GP5E01) : GwPlayer (4 x 0x108 : personnage, cases, pieces, etoiles, statistiques ; build 178 : les champs
// verifies dans le main.dol europeen : GWPlayerCoinGet lit 0x20, la fonction des etoiles 0x34 -> case 0xC, compteurs 0x14, rang en 0x8),
// GwSystem (0x2BC : tour, nombre de tours, plateau, mini-jeu, banque) juste apres. Europeen (GP5P01) : le main.dol
// construit 8024 8DB0 744 fois, 8024 91D0 (+0x420) 545 fois et 8024 9490 (+0x6E0) 107 fois : les memes trois blocs.
object JeuMP5 : ModuleJeu("Mario Party 5", "GP5P01", "GP5E01") {
    private fun joueurs(id: String) = if (id == "GP5P01") 0x80248DB0L else 0x8022A070L
    // build 175 : les cases du plateau (masu.c : masuData[256], 0x70 octets : etoile presente 0x0 (bit 0x40), pos 0x4, type 0x2C,
    // linkNum 0x62, linkTbl 0x64 ; numeros de case a partir de 1). Europeen : 8026 C518 (suivi, comme en americain, de masuLight
    // +0x7000 et masuStarTbl +0x8200). Build 180 : capsules posees : GwSystem.masuCapsule[256] (0x3A, numero de case - 1).
    private fun cases(id: String) = if (id == "GP5P01") 0x8026C518L else 0x8024D5F8L
    private val COUL_CASE = longArrayOf(0xFF5A5A6E, 0xFF4F8DFF, 0xFFFF4D4D, 0xFF8B1E2E, 0xFF3DDC84, 0xFFFFD23F, 0xFF9AA8C8, 0xFFFF8A3D, 0xFFB5793F)
    private val NOM_CASE = arrayOf("passage", "bleue", "rouge", "Bowser", "?", "étoile", "? (vide)", "VS", "DK")
    private val COUL_PERSO = longArrayOf(0xFFFF4040, 0xFF2FD16B, 0xFFFF8FC8, 0xFF5BE05B, 0xFFFFD84A, 0xFFFFC94A, 0xFF9B6BFF, 0xFF4F8BFF, 0xFFE8E8F0, 0xFFF5A623, 0xFFFF5A5A, 0xFF4CD964, 0xFF4F8DFF)
    private val CAPSULES = mapOf(0 to "Champignon", 1 to "Super champignon", 2 to "Champignon poison", 3 to "Tuyau", 4 to "Klepto", 5 to "Bulle",
        6 to "Wiggler", 10 to "Frère Marto", 11 to "Bloc pièce", 12 to "Hériss", 13 to "Paratroopa", 14 to "Bill Ball", 15 to "Goomba",
        16 to "Bob-omb", 17 to "Banque Koopa", 20 to "Kamek", 21 to "Lanceur", 22 to "Plante Piranha", 23 to "Mini Kamek", 24 to "Ukiki",
        25 to "Lakitu", 30 to "Tornade", 31 to "Duel", 32 to "Chomp", 33 to "Skelerex", 34 to "Bowser", 35 to "Chance", 36 to "Miracle",
        37 to "DK", 38 to "VS")
    private fun capsule(n: Int) = CAPSULES[n] ?: ("Capsule $n")
    private class Plateau(val cle: String, val pos: List<FloatArray>, val types: IntArray, val liens: List<IntArray>, val etoiles: List<Int>)
    @Volatile private var memo: Plateau? = null
    @Volatile private var essaiPlateau = 0L
    @Volatile private var etoileCase = -1                              // (case ou est l'etoile en ce moment : relue toutes les 8 s)
    @Volatile private var etoileT = 0L

    /** lu une fois par plateau (une trentaine de blocs, au rythme d'un par image) */
    private fun lirePlateau(id: String, cle: String): Plateau? {
        val l = Lecteur(40, 12)
        val base = cases(id)
        val pos = ArrayList<FloatArray>(); val types = ArrayList<Int>(); val liens = ArrayList<IntArray>()
        for (i in 0 until 256) {
            val m = base + 0x70L * i
            val x = l.fini(l.f32(m + 4), 1e6f) ?: return null; val z = l.fini(l.f32(m + 12), 1e6f) ?: return null
            val t = l.u16(m + 0x2C) ?: return null; val n = l.u16(m + 0x62) ?: return null
            if (x == 0f && z == 0f && t == 0 && n == 0) break                       // (fin des cases lues : memset a 0)
            if (t > 8 || n > 5) return null
            pos.add(floatArrayOf(x, z)); types.add(t)
            for (k in 0 until n) { val v = l.u16(m + 0x64 + 2L * k) ?: 0; if (v in 1..256) liens.add(intArrayOf(i, v - 1)) }
        }
        if (pos.size < 10) return null
        return Plateau(cle, pos, types.toIntArray(), liens.filter { it[1] < pos.size }, types.indices.filter { types[it] == 5 })
    }
    /** l'etoile : la case etoile dont le drapeau « etoile presente » est mis (8 cases au plus) */
    private fun lireEtoile(id: String, pl: Plateau) {
        val l = Lecteur(10, 8)
        var c = -1
        for (i in pl.etoiles) { val f = l.u8(cases(id) + 0x70L * i) ?: return; if (f and 0x40 != 0) { c = i; break } }
        etoileCase = c
    }
    /** nombre de vraies cases (pas les passages) de a jusqu'a b en suivant les chemins ; -1 : pas de chemin */
    private fun distance(pl: Plateau, a: Int, b: Int): Int {
        if (a !in pl.pos.indices || b !in pl.pos.indices) return -1
        if (a == b) return 0
        val suiv = HashMap<Int, MutableList<Int>>()
        for (l in pl.liens) suiv.getOrPut(l[0]) { ArrayList() }.add(l[1])
        val d = IntArray(pl.pos.size) { -1 }
        val f = java.util.ArrayDeque<Int>()
        d[a] = 0; f.add(a)
        while (f.isNotEmpty()) {
            val x = f.poll()
            for (y in suiv[x] ?: continue) {
                if (d[y] >= 0) continue
                d[y] = d[x] + if (pl.types[y] != 0) 1 else 0
                if (y == b) return d[y]
                f.add(y)
            }
        }
        return -1
    }
    private val PERSOS = arrayOf("Mario", "Luigi", "Peach", "Yoshi", "Wario", "Daisy", "Waluigi", "Toad", "Boo", "Mini Bowser",
        "Mini Bowser rouge", "Mini Bowser vert", "Mini Bowser bleu")
    private val PLATEAUX = arrayOf("Rêve des jouets", "Rêve arc-en-ciel", "Rêve des pirates", "Rêve sous-marin", "Rêve du futur",
        "Rêve sucré", "Cauchemar de Bowser", "Tutoriel", "Circuit")
    private val COUL = longArrayOf(0xFF4F8DFF, 0xFFFF4D4D, 0xFFFFD23F, 0xFF3DDC84)
    private val NIVEAUX = arrayOf("facile", "normal", "difficile", "très difficile")

    override fun lire(l: Lecteur, id: String): Fiche? {
        val gp = joueurs(id); val sys = gp + 0x420
        val tour = l.u8(sys + 4) ?: return null; val tours = l.u8(sys + 5) ?: return null
        val plateau = (l.u8(sys + 8) ?: return null) and 31
        val partie = tours in 10..50 && tour in 1..tours + 1 && plateau in 0..8
        val joueur = l.u8(sys + 0xA)?.toByte()?.toInt() ?: -1
        val mg = l.u16(sys + 0x34) ?: 0xFFFF
        val banque = (l.u16(sys + 0x38) ?: 0).coerceAtMost(9999)
        val equipes = ((l.u8(sys) ?: 0) and 0x40) != 0
        class J(val n: Int, val nom: String, val pieces: Int, val etoiles: Int, val rang: Int, val perso: Int, val caseNo: Int, val ordi: Boolean,
                val niveau: Int, val caps: List<Int>, val reste: Int, val equipe: Int, val lignes: List<Ligne>)
        val js = ArrayList<J>()
        for (k in 0 until 4) {
            val b = gp + 0x108L * k
            val w0 = l.u16(b) ?: return null
            val perso = (w0 shr 9) and 15
            val ordi = (w0 shr 13) and 1 == 1
            val niveau = (w0 shr 14) and 3
            val pieces = l.s16(b + 0x20) ?: return null; val etoiles = l.s16(b + 0x34) ?: return null
            if (perso >= PERSOS.size || pieces !in 0..999 || etoiles !in 0..99) return if (partie) null else
                Fiche(id, "Mario Party 5", "Menus", 0xFFB82E2E, 0xFF2A0A0A, message = "Pas de partie en cours.")
            val w8 = l.u16(b + 8) ?: 0
            val rang = (w8 shr 5) and 3
            val equipe = (l.u8(b + 2) ?: 0) shr 7
            val max = (l.s16(b + 0x26) ?: 0).coerceIn(0, 999); val maxEt = (l.s16(b + 0x36) ?: 0).coerceIn(0, 99)
            val totalMg = (l.s16(b + 0x22) ?: 0).coerceIn(0, 9999); val total = (l.s16(b + 0x24) ?: 0).coerceIn(0, 9999)
            val combat = (l.s16(b + 0x28) ?: 0).coerceIn(0, 9999)
            val mgPieces = (l.s16(b + 0x2A) ?: 0).coerceIn(-999, 999)
            val caps = (l.plage(b + 5, 3) ?: IntArray(3)).filter { it in 0..42 }
            val reste = (l.u8(b + 0xA)?.toByte()?.toInt() ?: 0).coerceIn(0, 99)
            val cases = l.plage(b + 0x14, 11) ?: IntArray(11)
            fun c(i: Int) = cases[i].toByte().toInt().coerceIn(0, 127)
            val nom = PERSOS[perso]
            val li = arrayListOf(
                Ligne("Étoiles", etoiles.toString(), (etoiles / 10f).coerceIn(0f, 1f), 0xFFFFD23F),
                Ligne("Pièces", pieces.toString(), (pieces / 100f).coerceIn(0f, 1f), COUL[k]),
                Ligne("Classement", (rang + 1).toString() + if (rang == 0) "er" else "e"),
                Ligne("Capsules", if (caps.isEmpty()) "aucune" else caps.joinToString(", ") { capsule(it) }, caps.size / 3f, 0xFF9C6CFF),
                Ligne("Pièces gagnées aux mini-jeux", totalMg.toString()), Ligne("Pièces ramassées en tout", total.toString()),
                Ligne("Pièces gagnées en duel / combat", combat.toString()),
                Ligne("Record de pièces / d'étoiles", "$max / $maxEt"),
                Ligne("Cases bleues / rouges", "${c(0)} / ${c(1)}"), Ligne("Cases ? / capsule", "${c(3)} / ${c(2)}"),
                Ligne("Cases Bowser / DK / VS", "${c(4)} / ${c(10)} / ${c(6)}"), Ligne("Cases étoile", c(9).toString()))
            if (mgPieces != 0) li.add(2, Ligne("Dernier mini-jeu", (if (mgPieces > 0) "+" else "") + mgPieces + " pièces"))
            js.add(J(k, nom, pieces, etoiles, rang, perso, l.s16(b + 0xC) ?: -1, ordi, niveau, caps, reste, equipe, li))
        }
        if (!partie) return Fiche(id, "Mario Party 5", "Menus ou mini-jeux libres", 0xFFB82E2E, 0xFF2A0A0A, message = "Pas de partie sur un plateau en ce moment.")
        // le plateau : relu quand il change (et toutes les 30 s s'il n'a pas pu etre lu) ; l'etoile toutes les 8 s
        val cle = "$id/$plateau"
        val t = System.currentTimeMillis()
        if (memo?.cle != cle && t - essaiPlateau > 30000) { essaiPlateau = t; etoileCase = -1; lirePlateau(id, cle)?.let { memo = it } }
        val pl = memo?.takeIf { it.cle == cle }
        if (pl != null && t - etoileT > 8000) { etoileT = t; lireEtoile(id, pl) }
        val etoile = if (pl != null) etoileCase else -1
        // capsules posees sur le plateau (GwSystem.masuCapsule)
        val posees = HashMap<Int, Int>()
        if (pl != null) for (i in pl.pos.indices) { val v = l.s16(sys + 0x3A + 2L * i) ?: break; if (v in 0..42) posees[i] = v }
        val lignes = ArrayList<Ligne>()
        lignes.add(Ligne("Tour", "$tour / $tours", pct(tour, tours), 0xFFFF8A3D))
        if (joueur in 0..3) lignes.add(Ligne("À qui de jouer", js.getOrNull(joueur)?.let { it.nom + if (it.reste > 0) " (encore ${it.reste} cases)" else "" } ?: "–"))
        if (mg in 501..600) lignes.add(Ligne("Mini-jeu", "n° " + (mg - 500)))
        lignes.add(Ligne("Banque Koopa", "$banque pièces"))
        if (equipes) lignes.add(Ligne("Mode", "Par équipes"))
        if (tours - tour in 0..4 && tour <= tours) lignes.add(Ligne("Derniers tours", "Oui (les 5 derniers)"))
        if (posees.isNotEmpty()) lignes.add(Ligne("Capsules posées sur le plateau", posees.size.toString()))
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Partie", lignes))
        // le classement en un coup d'oeil
        val ordre = js.sortedWith(compareBy({ it.rang }, { -it.etoiles }, { -it.pieces }))
        blocs.add(Bloc("Classement", ordre.map { j -> Ligne((j.rang + 1).toString() + ". " + j.nom, "${j.etoiles} ★ · ${j.pieces} pièces",
            (j.etoiles * 100 + j.pieces).toFloat() / (ordre.maxOf { it.etoiles * 100 + it.pieces }.coerceAtLeast(1)), COUL_PERSO.getOrElse(j.perso) { 0xFFFFFFFF }) }))
        if (pl != null) {
            val pions = js.mapNotNull { j -> pl.pos.getOrNull(j.caseNo - 1)?.let { Pion(it[0], it[1], COUL_PERSO.getOrElse(j.perso) { 0xFFFFFFFF }, "J" + (j.n + 1)) } }
            val ou = js.map { j ->
                val i = j.caseNo - 1
                val ty = pl.types.getOrNull(i) ?: -1
                val d = if (etoile >= 0) distance(pl, i, etoile) else -1
                Ligne("J" + (j.n + 1) + " " + j.nom, (if (ty >= 0) "case " + NOM_CASE[ty] + " n° " + j.caseNo else "–") +
                    (posees[i]?.let { " · capsule " + capsule(it) } ?: "") + (if (d >= 0) " · étoile à $d cases" else ""),
                    -1f, COUL_PERSO.getOrElse(j.perso) { 0xFFFFFFFF })
            }
            blocs.add(Bloc("Plateau", lignes = ou, plan = Plan(pl.pos, pl.types.map { COUL_CASE[it] }, pl.liens, pions,
                if (etoile >= 0) listOf(etoile) else pl.etoiles, petits = pl.types.indices.filter { pl.types[it] == 0 }.toSet(), marques = posees.keys),
                texte = "Cases : bleue, rouge, verte (?), jaune (étoile, entourée là où elle est), orange (VS), brune (DK), rouge foncé (Bowser) ; " +
                    "petit carré blanc : une capsule posée ; petits points gris : passages."))
        } else blocs.add(Bloc("Plateau", texte = "Lecture des cases du plateau…"))
        for (j in js) blocs.add(Bloc("J" + (j.n + 1) + " · " + j.nom + (if (j.ordi) " (ordi " + NIVEAUX[j.niveau] + ")" else "") + if (equipes) " · équipe " + (j.equipe + 1) else "", j.lignes))
        return Fiche(id, PLATEAUX[plateau], "Tour $tour sur $tours" + if (etoile >= 0) " · étoile en case n° " + (etoile + 1) else "", 0xFFB82E2E, 0xFF2A0A0A,
            grands = ordre.take(4).map { it.nom to "${it.etoiles}★ ${it.pieces}" }, blocs = blocs)
    }
}
