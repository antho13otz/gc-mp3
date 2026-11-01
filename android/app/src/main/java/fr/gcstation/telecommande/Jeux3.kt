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
    // build 182 : les mini-jeux. mgNo = 501 + rang dans mgDataTbl (mgdata.inc : le type de chaque mini-jeu) ; les noms (anglais
    // officiels) suivent la liste de chaque type dans l'ordre de la table (m501 = Coney Island, verifie) : a confirmer en jeu.
    private val MG = arrayOfNulls<String>(80).also { t ->
        val p4 = intArrayOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 23, 62, 63, 64, 65, 75, 77)
        val n4 = listOf("Coney Island", "Ground Pound Down", "Chimp Chase", "Chomp Romp", "Pushy Penguins", "Leaf Leap", "Night Light Fright",
            "Pop-Star Piranhas", "Mazed & Confused", "Dinger Derby", "Hydrostars", "Later Skater", "Will Flower", "Triple Jump", "Hotel Goomba",
            "Coin Cache", "Vicious Vending", "Flower Shower", "Dodge Bomb", "Fish Upon a Star", "Rumble Fumble", "Frozen Frenzy", "Fish Sticks")
        val p13 = intArrayOf(16, 17, 18, 19, 21, 22, 24, 66, 67, 68, 69, 76)
        val n13 = listOf("Flatiator", "Squared Away", "Mario Mechs", "Revolving Fire", "Heat Stroke", "Beam Team", "Big Top Drop", "Quilt for Speed",
            "Tube It or Lose It", "Mathletes", "Fight Cards", "Curvy Curbs")
        val p22 = intArrayOf(20, 25, 26, 27, 28, 29, 30, 31, 32, 33, 73, 74)
        val n22 = listOf("Clock Stoppers", "Defuse or Lose", "ID UFO", "Mario Can-Can", "Handy Hoppers", "Berry Basket", "Bus Buffer", "Rumble Ready",
            "Submarathon", "Manic Mallets", "Panic Pinball", "Banking Coins")
        val nb = listOf("Astro-Logical", "Bill Blasters", "Tug-o-Dorrie", "Twist 'n' Out", "Lucky Lineup", "Random Ride")
        val nd = listOf("Shock Absorbers", "Countdown Pound", "Whomp Maze", "Shy Guy Showdown", "Button Mashers", "Get a Rope", "Pump 'n' Jump",
            "Head Waiter", "Blown Away", "Merry Poppings", "Pound Peril", "Piece Out", "Bound of Music", "Wind Wavers", "Sky Survivor")
        for (k in p4.indices) t[p4[k]] = n4[k] + " (4 joueurs)"
        for (k in p13.indices) t[p13[k]] = n13[k] + " (1 contre 3)"
        for (k in p22.indices) t[p22[k]] = n22[k] + " (2 contre 2)"
        for (k in nb.indices) t[34 + k] = nb[k] + " (bataille)"
        for (k in nd.indices) t[40 + k] = nd[k] + " (duel)"
        for ((k, n) in listOf("Rain of Fire", "Cage-in Cookin'", "Scaldin' Cauldron").withIndex()) t[58 + k] = "$n (Bowser)"
        for ((k, n) in listOf("Banana Punch", "Da Vine Climb", "Mass A-peel").withIndex()) t[70 + k] = "$n (DK)"
        t[61] = "Frightmare (dernier tour)"; t[78] = "Mini-jeu bonus"; t[79] = "Mini-jeu bonus (2 contre 2)"
    }
    private fun nomMg(mg: Int) = MG.getOrNull(mg - 501) ?: ("n° " + (mg - 500))
    // build 182 : l'historique des tours (garde sur le telephone pendant la partie) et les alertes
    private class Tour(val tour: Int, val mg: Int, val pieces: IntArray, val etoiles: IntArray)
    private val histo = ArrayList<Tour>()
    @Volatile private var hCle = ""; @Volatile private var hTour = -1
    private var hPieces = IntArray(4); private var hEtoiles = IntArray(4); private var hMg = -1
    private var alerte = ""; private var alerteT = 0L
    private var rangAvant = IntArray(4) { -1 }; private var procheAvant = BooleanArray(4)
    // build 185 : pieces au debut de chaque tour (graphique), etoiles au debut des resultats (bonus), parties gardees
    private var serie: Array<ArrayList<Int>> = Array(4) { ArrayList<Int>() }
    private var finEtoiles: IntArray? = null; private var finT = 0L; private var debut = 1
    private fun parties(): org.json.JSONArray = try { org.json.JSONArray(Compagnon.ctx?.getSharedPreferences("gcs", 0)?.getString("mp5_parties", "[]") ?: "[]") } catch (e: Exception) { org.json.JSONArray() }
    private fun garderPartie(p: org.json.JSONObject) {
        val c = Compagnon.ctx ?: return
        val a = parties(); val b = org.json.JSONArray()
        b.put(p)
        for (i in 0 until a.length()) { val o = a.optJSONObject(i) ?: continue; if (o.optLong("t") != p.optLong("t") && b.length() < 30) b.put(o) }
        c.getSharedPreferences("gcs", 0).edit().putString("mp5_parties", b.toString()).apply()
    }
    /** les parties gardees : la derniere en detail (classement, courbes), puis la liste */
    private fun mesParties(): List<Bloc> {
        val a = parties()
        if (a.length() == 0) return listOf(Bloc("Mes parties", texte = "Les parties finies avec le compagnon ouvert apparaîtront ici (classement, étoiles bonus, pièces tour par tour)."))
        val r = ArrayList<Bloc>()
        val df = java.text.SimpleDateFormat("d MMM HH:mm", java.util.Locale.FRANCE)
        fun joueurs(o: org.json.JSONObject) = (0 until (o.optJSONArray("joueurs")?.length() ?: 0)).mapNotNull { o.optJSONArray("joueurs")?.optJSONObject(it) }
        val d = a.getJSONObject(0)
        val jd = joueurs(d).sortedWith(compareBy({ -it.optInt("et") }, { -it.optInt("pi") }))
        r.add(Bloc("Dernière partie · " + d.optString("plateau") + " · " + df.format(java.util.Date(d.optLong("t"))),
            jd.mapIndexed { i, j -> Ligne((i + 1).toString() + ". " + j.optString("nom"), j.optInt("et").toString() + " ★ · " + j.optInt("pi") + " pièces" +
                (if (j.optInt("bonus") > 0) " (" + j.optInt("bonus") + " ★ bonus)" else "") + " · mini-jeux " + j.optInt("mg"), -1f, COUL_PERSO.getOrElse(j.optInt("perso")) { 0xFFFFFFFF }) },
            courbes = joueurs(d).map { j -> val s = j.optJSONArray("serie"); Courbe(j.optString("nom"), COUL_PERSO.getOrElse(j.optInt("perso")) { 0xFFFFFFFF },
                (0 until (s?.length() ?: 0)).map { s!!.optInt(it) }) }))
        val victoires = HashMap<String, Int>()
        for (i in 0 until a.length()) { val o = a.optJSONObject(i) ?: continue; joueurs(o).maxWithOrNull(compareBy({ it.optInt("et") }, { it.optInt("pi") }))?.let { victoires[it.optString("nom")] = (victoires[it.optString("nom")] ?: 0) + 1 } }
        r.add(Bloc("Mes parties (" + a.length() + ")", (0 until a.length()).mapNotNull { a.optJSONObject(it) }.map { o ->
            val g = joueurs(o).maxWithOrNull(compareBy({ it.optInt("et") }, { it.optInt("pi") }))
            Ligne(df.format(java.util.Date(o.optLong("t"))) + " · " + o.optString("plateau"), "gagnant : " + (g?.optString("nom") ?: "–") + " (" + (g?.optInt("et") ?: 0) + " ★)")
        }, texte = "Victoires : " + victoires.entries.sortedByDescending { it.value }.joinToString(" · ") { it.key + " " + it.value }))
        return r
    }

    override fun lire(l: Lecteur, id: String): Fiche? {
        val gp = joueurs(id); val sys = gp + 0x420
        val tour = l.u8(sys + 4) ?: return null; val tours = l.u8(sys + 5) ?: return null
        val plateau = (l.u8(sys + 8) ?: return null) and 31
        val partie = tours in 10..50 && tour in 1..tours + 1 && plateau in 0..8
        val joueur = l.u8(sys + 0xA)?.toByte()?.toInt() ?: -1
        val mg = l.u16(sys + 0x34) ?: 0xFFFF
        val banque = (l.u16(sys + 0x38) ?: 0).coerceAtMost(9999)
        val equipes = ((l.u8(sys) ?: 0) and 0x40) != 0
        class J(val n: Int, val nom: String, val pieces: Int, val etoiles: Int, val rang: Int, val perso: Int, val caseNo: Int, val ordi: Boolean, val mgTotal: Int,
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
                Fiche(id, "Mario Party 5", "Menus", 0xFFB82E2E, 0xFF2A0A0A, message = "Pas de partie en cours.", blocs = mesParties())
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
            js.add(J(k, nom, pieces, etoiles, rang, perso, l.s16(b + 0xC) ?: -1, ordi, totalMg, niveau, caps, reste, equipe, li))
        }
        if (!partie) return Fiche(id, "Mario Party 5", "Menus ou mini-jeux libres", 0xFFB82E2E, 0xFF2A0A0A, message = "Pas de partie sur un plateau en ce moment.",
            blocs = mesParties())
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
        if (mg in 501..580) lignes.add(Ligne("Mini-jeu", nomMg(mg)))
        lignes.add(Ligne("Banque Koopa", "$banque pièces"))
        if (equipes) lignes.add(Ligne("Mode", "Par équipes"))
        if (tours - tour in 0..4 && tour <= tours) lignes.add(Ligne("Derniers tours", "Oui (les 5 derniers)"))
        if (posees.isNotEmpty()) lignes.add(Ligne("Capsules posées sur le plateau", posees.size.toString()))
        // l'historique : a chaque nouveau tour, ce qui a change pendant le tour d'avant (pieces, etoiles, mini-jeu)
        val hc = "$id/$plateau/$tours"
        if (hc != hCle || tour < hTour) { hCle = hc; histo.clear(); hTour = tour; hPieces = IntArray(4) { js[it].pieces }; hEtoiles = IntArray(4) { js[it].etoiles }; hMg = -1; rangAvant = IntArray(4) { -1 }
            serie = Array(4) { k -> arrayListOf(js[k].pieces) }; finEtoiles = null; finT = 0L; debut = tour }
        if (mg in 501..580) hMg = mg
        if (tour > hTour) {
            histo.add(0, Tour(hTour, hMg, IntArray(4) { js[it].pieces - hPieces[it] }, IntArray(4) { js[it].etoiles - hEtoiles[it] }))
            if (histo.size > 50) histo.removeAt(histo.size - 1)
            hTour = tour; hPieces = IntArray(4) { js[it].pieces }; hEtoiles = IntArray(4) { js[it].etoiles }; hMg = -1
            if (tour <= tours) for (k in 0 until 4) serie[k].add(js[k].pieces)
        }
        // build 185 : la fin de partie (tour = nombre de tours + 1 : resultats et etoiles bonus) : gardee sur le telephone
        var finBloc: List<Bloc> = emptyList()
        if (tour == tours + 1) {
            if (finEtoiles == null) { finEtoiles = IntArray(4) { js[it].etoiles }; finT = System.currentTimeMillis() }
            val fe = finEtoiles!!
            val ordreFin = js.sortedWith(compareBy({ -it.etoiles }, { -it.pieces }))
            val meilleurMg = js.maxByOrNull { it.mgTotal }
            val p = org.json.JSONObject().put("t", finT).put("plateau", PLATEAUX[plateau]).put("tours", tours).put("debut", debut)
            val ja = org.json.JSONArray()
            for (j in js) ja.put(org.json.JSONObject().put("nom", j.nom).put("perso", j.perso).put("et", j.etoiles).put("pi", j.pieces)
                .put("bonus", j.etoiles - fe[j.n]).put("mg", j.mgTotal).put("ordi", j.ordi).put("serie", org.json.JSONArray(serie[j.n])))
            p.put("joueurs", ja)
            garderPartie(p)
            finBloc = listOf(Bloc("Fin de partie", ordreFin.mapIndexed { i, j ->
                Ligne((i + 1).toString() + ". " + j.nom, "${j.etoiles} ★ · ${j.pieces} pièces" + if (j.etoiles - fe[j.n] > 0) " (dont " + (j.etoiles - fe[j.n]) + " ★ bonus)" else "",
                    -1f, COUL_PERSO.getOrElse(j.perso) { 0xFFFFFFFF })
            } + listOfNotNull(meilleurMg?.let { Ligne("Meilleur aux mini-jeux", it.nom + " (" + it.mgTotal + " pièces gagnées)") }),
                texte = "Les étoiles bonus sont comptées à partir du moment où la fin de partie commence (elles arrivent pendant les résultats)."))
        }
        val courbes = Bloc("Pièces tour par tour", courbes = js.map { j -> Courbe(j.nom, COUL_PERSO.getOrElse(j.perso) { 0xFFFFFFFF }, serie[j.n] + j.pieces) },
            texte = if (debut > 1) "Depuis le tour $debut (l'écran a été ouvert en cours de partie)." else "")
        // les alertes : toi = le premier joueur humain (sinon J1)
        val moi = js.firstOrNull { !it.ordi } ?: js[0]
        val tms = System.currentTimeMillis()
        for (j in js) {
            if (j.n != moi.n && rangAvant[j.n] >= 0 && rangAvant[moi.n] >= 0 && rangAvant[j.n] >= rangAvant[moi.n] && j.rang < moi.rang) {
                alerte = "⚠ ${j.nom} passe devant toi (${j.rang + 1}e)"; alerteT = tms }
        }
        for (j in js) rangAvant[j.n] = j.rang
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
                val proche = d in 0..10
                if (proche && !procheAvant[j.n] && j.n != moi.n) { alerte = "⭐ ${j.nom} est à $d cases de l'étoile"; alerteT = tms }
                procheAvant[j.n] = proche
                Ligne("J" + (j.n + 1) + " " + j.nom, (if (ty >= 0) "case " + NOM_CASE[ty] + " n° " + j.caseNo else "–") +
                    (posees[i]?.let { " · capsule " + capsule(it) } ?: "") + (if (d >= 0) " · étoile à $d cases" else ""),
                    -1f, COUL_PERSO.getOrElse(j.perso) { 0xFFFFFFFF })
            }
            blocs.add(Bloc("Plateau", lignes = ou, plan = Plan(pl.pos, pl.types.map { COUL_CASE[it] }, pl.liens, pions,
                if (etoile >= 0) listOf(etoile) else pl.etoiles, petits = pl.types.indices.filter { pl.types[it] == 0 }.toSet(), marques = posees.keys),
                texte = "Cases : bleue, rouge, verte (?), jaune (étoile, entourée là où elle est), orange (VS), brune (DK), rouge foncé (Bowser) ; " +
                    "petit carré blanc : une capsule posée ; petits points gris : passages."))
        } else blocs.add(Bloc("Plateau", texte = "Lecture des cases du plateau…"))
        if (finBloc.isNotEmpty()) blocs.addAll(0, finBloc)
        blocs.add(courbes)
        if (histo.isNotEmpty()) blocs.add(Bloc("Historique des tours", histo.take(20).map { h ->
            val gagnant = (0 until 4).maxByOrNull { h.pieces[it] }
            Ligne("Tour " + h.tour + if (h.mg in 501..580) " · " + nomMg(h.mg).substringBefore(" (") else "",
                (0 until 4).joinToString("  ") { k -> js[k].nom.take(5) + " " + (if (h.pieces[k] >= 0) "+" else "") + h.pieces[k] + (if (h.etoiles[k] != 0) " " + (if (h.etoiles[k] > 0) "+" else "") + h.etoiles[k] + "★" else "") } +
                    (if (gagnant != null && h.pieces[gagnant] > 0) "  · meilleur : " + js[gagnant].nom else ""))
        }, texte = "Pièces et étoiles gagnées ou perdues à chaque tour (depuis que l'écran est ouvert)."))
        for (j in js) blocs.add(Bloc("J" + (j.n + 1) + " · " + j.nom + (if (j.ordi) " (ordi " + NIVEAUX[j.niveau] + ")" else "") + if (equipes) " · équipe " + (j.equipe + 1) else "", j.lignes))
        return Fiche(id, PLATEAUX[plateau], "Tour $tour sur $tours" + if (etoile >= 0) " · étoile en case n° " + (etoile + 1) else "", 0xFFB82E2E, 0xFF2A0A0A,
            grands = ordre.take(4).map { it.nom to "${it.etoiles}★ ${it.pieces}" }, blocs = blocs,
            message = if (alerte.isNotEmpty() && tms - alerteT < 30000) alerte else "")
    }
}

// ---------------------------------------------------------------- Star Fox Adventures (build 194)
// Structure de la partie en cours (SaveGame), d'apres les notes publiques de RenaKunisaki/StarFoxAdventures (notes/savedata.txt,
// et ses codes : la vie, la magie et les scarabs du jeu sont lus dans cette structure) : charState[2] (Krystal, Fox : 0xC
// chacun : vie en quarts de coeur 0, vie max 1, magie 4, magie max 6, scarabs 8, BafomDads 9 / 0xA), energie de Tricky 0x18,
// max 0x19, nom 0x1C, personnage 0x20, avancement 0x55D (/ 187), temps de jeu 0x560 (images). Pas de liste de symboles pour
// la version europeenne : l'adresse est cherchee une fois dans la memoire (jeu en pause, comme Ocarina of Time), puis gardee.
object JeuSFA : ModuleJeu("Star Fox Adventures", "GSAP01", "GSAE01", "GSAJ01") {
    @Volatile var progres = -1f
    @Volatile private var candidats: LongArray? = null
    @Volatile private var choisi = -1
    private var avant = IntArray(0)
    private fun cle(id: String) = "sfa_$id"

    private fun charger(c: android.content.Context?, id: String) {
        if (candidats != null || c == null) return
        val s = c.getSharedPreferences("gcs", 0).getString(cle(id), "") ?: ""
        candidats = s.split(',').mapNotNull { it.trim().toLongOrNull() }.toLongArray()
    }
    /** la signature d'une SaveGame vraisemblable (o : debut dans la photo de la memoire) */
    private fun vraisemblableMem(m: ByteArray, o: Int): Boolean {
        fun u8(k: Int) = m[o + k].toInt() and 255
        fun u16(k: Int) = ((m[o + k].toInt() and 255) shl 8) or (m[o + k + 1].toInt() and 255)
        for (c in 0 until 2) {
            val b = c * 0xC
            val vie = m[o + b].toInt(); val max = m[o + b + 1].toInt()
            if (max !in 4..100 || max % 4 != 0 || vie !in 0..max) return false
            val mg = u16(b + 4); val mgMax = u16(b + 6)
            if (mgMax > 400 || mg > mgMax) return false
            if (u8(b + 0xA) > 20 || u8(b + 9) > u8(b + 0xA)) return false
        }
        if (u8(0x19) > 100 || u8(0x18) > u8(0x19)) return false
        if (u8(0x20) > 1 || u8(0x55D) > 187) return false
        // le nom : 1 a 3 lettres puis 0
        var n = 0
        while (n < 4 && u8(0x1C + n) != 0) { val ch = u8(0x1C + n); if (ch !in 0x20..0x7E) return false; n++ }
        if (n == 0 || n == 4) return false
        val t = java.lang.Float.intBitsToFloat((u16(0x560) shl 16) or u16(0x562))
        return t.isFinite() && t > 60f && t < 60f * 3600f * 999f
    }
    private fun vraisemblable(l: Lecteur, a: Long): Boolean {
        for (c in 0 until 2) {
            val b = a + c * 0xCL
            val vie = l.u8(b)?.toByte()?.toInt() ?: return false; val max = l.u8(b + 1)?.toByte()?.toInt() ?: return false
            if (max !in 4..100 || max % 4 != 0 || vie !in 0..max) return false
            val mg = l.u16(b + 4) ?: return false; val mgMax = l.u16(b + 6) ?: return false
            if (mgMax > 400 || mg > mgMax) return false
        }
        val t = l.fini(l.f32(a + 0x560), 1e9f) ?: return false
        return (l.u8(a + 0x20) ?: 9) <= 1 && t > 60f
    }

    override fun agir(c: android.content.Context, id: String) {
        if (progres >= 0f) return
        progres = 0f
        Thread {
            Compagnon.pauseLecture = true
            try {
                Thread.sleep(400)
                if (MemoireJeu.demande('P', 1) != null) {
                    val mem = ByteArray(MemoireJeu.TAILLE)
                    val ok = MemoireJeu.lire(IntArray(MemoireJeu.FIN / MemoireJeu.BLK) { it }, mem, true, { progres = it * 0.95f }, { false })
                    MemoireJeu.demande('P', 0)
                    if (ok) {
                        val r = ArrayList<Long>()
                        var i = 0
                        val fin = MemoireJeu.FIN - 0x600
                        while (i < fin && r.size < 12) { if (vraisemblableMem(mem, i)) r.add(MemoireJeu.BASE + i); i += 4 }
                        candidats = r.toLongArray(); choisi = -1
                        c.getSharedPreferences("gcs", 0).edit().putString(cle(id), r.joinToString(",")).apply()
                    }
                }
            } catch (x: Exception) { }
            progres = -1f
            Compagnon.pauseLecture = false
        }.start()
    }

    override fun lire(l: Lecteur, id: String): Fiche? {
        charger(Compagnon.ctx, id)
        val pr = progres
        if (pr >= 0f) return Fiche(id, "Star Fox Adventures", "Recherche dans la mémoire…", 0xFF1E4E8F, 0xFF07162E,
            grands = listOf("lu" to ((pr * 100).toInt().toString() + " %")), message = "Le jeu est en pause pendant la recherche (une seule fois).")
        val ok = (candidats ?: LongArray(0)).filter { vraisemblable(l, it) }
        if (ok.isEmpty()) return Fiche(id, "Star Fox Adventures", if (candidats.isNullOrEmpty()) "Pas encore trouvé" else "Plus trouvé (autre partie ?)", 0xFF1E4E8F, 0xFF07162E,
            message = "Charge ta partie et joue quelques secondes, puis touche le bouton : l'appli cherche la partie en cours dans la mémoire " +
                "(jeu en pause une trentaine de secondes, une seule fois). Recommence si tu changes de fichier de sauvegarde.",
            bouton = "Trouver ma partie dans la mémoire")
        // la partie en cours : celle dont le temps de jeu avance (les copies des fichiers de la carte ne bougent pas)
        val temps = ok.map { (l.fini(l.f32(it + 0x560), 1e9f) ?: 0f).toInt() }.toIntArray()
        if (avant.size == temps.size) for (k in temps.indices) if (temps[k] != avant[k]) { choisi = k; break }
        avant = temps
        val a = ok[if (choisi in ok.indices) choisi else 0]
        val noms = arrayOf("Krystal", "Fox")
        val perso = (l.u8(a + 0x20) ?: 1).coerceIn(0, 1)
        val blocs = ArrayList<Bloc>()
        var vieP = 0; var maxP = 0; var sc = 0
        for (c in intArrayOf(perso, 1 - perso)) {
            val b = a + c * 0xCL
            val vie = (l.u8(b)?.toByte()?.toInt() ?: 0).coerceAtLeast(0); val max = (l.u8(b + 1)?.toByte()?.toInt() ?: 0).coerceIn(0, 100)
            val mg = l.u16(b + 4) ?: 0; val mgMax = l.u16(b + 6) ?: 0
            val scar = l.u8(b + 8) ?: 0; val bd = l.u8(b + 9) ?: 0; val bdMax = l.u8(b + 0xA) ?: 0
            if (c == perso) { vieP = vie; maxP = max; sc = scar }
            if (max == 0) continue
            blocs.add(Bloc(noms[c] + if (c == perso) " (en jeu)" else "", listOf(
                Ligne("Cœurs", String.format(java.util.Locale.FRANCE, "%.2f / %d", vie / 4f, max / 4), pct(vie, max), 0xFFFF3B5C),
                Ligne("Magie", if (mgMax > 0) "$mg / $mgMax" else "–", if (mgMax > 0) pct(mg, mgMax) else -1f, 0xFF4FA3FF),
                Ligne("Scarabs", scar.toString(), -1f, 0xFF3DDC84),
                Ligne("BafomDads", if (bdMax > 0) "$bd / $bdMax" else "–", if (bdMax > 0) pct(bd, bdMax) else -1f, 0xFFFFB300))))
        }
        val tr = l.u8(a + 0x18) ?: 0; val trMax = l.u8(a + 0x19) ?: 0
        val av = l.u8(a + 0x55D) ?: 0
        val t = l.fini(l.f32(a + 0x560), 1e9f) ?: 0f
        val nom = (l.texte(a + 0x1C, 4) ?: "").filter { it.code in 32..126 }
        blocs.add(Bloc("Partie", listOf(
            Ligne("Énergie de Tricky", if (trMax > 0) "$tr / $trMax" else "–", if (trMax > 0) pct(tr, trMax) else -1f, 0xFFFF8A3D),
            Ligne("Avancement", (av * 100 / 187).toString() + " %", pct(av, 187), 0xFF9C6CFF),
            Ligne("Temps de jeu", dureeS((t / 60f).toLong())),
            Ligne("Fichier", nom.ifEmpty { "–" }))))
        return Fiche(id, "Star Fox Adventures", noms[perso] + (if (nom.isNotEmpty()) " · fichier $nom" else ""), 0xFF1E4E8F, 0xFF07162E,
            grands = listOf("cœurs" to String.format(java.util.Locale.FRANCE, "%.1f", vieP / 4f), "scarabs" to sc.toString(), "avancement" to (av * 100 / 187).toString() + " %"),
            blocs = blocs, message = if (ok.size > 1 && choisi < 0) "Plusieurs endroits possibles : joue quelques secondes, l'appli garde celui dont le temps avance." else "")
    }
}
