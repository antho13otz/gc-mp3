package fr.gcstation.telecommande

// Appli build 567 : compagnon de The Simpsons: Hit & Run (GHQE7D, GHQP7D). Adresses : notes publiques du jeu GameCube sur
// RetroAchievements (« Code Notes », seulement des adresses et leur sens, version US). La feuille de progression du jeu (pieces,
// niveau, missions, cartes, guepes, gags, tenues) est rangee par niveau, 0x26C octets par niveau ; les cartes a part, 0x20
// par niveau. Elle est dans la memoire que le jeu se reserve au demarrage (pas une adresse fixe de main.dol) : pour la version
// europeenne, l'appli la cherche une fois (les 7 temps des paris de course valent FFFFFFFF ou un temps en secondes, les
// missions 0 ou 1...), d'abord au decalage attendu (fin du programme europeen 0x660 plus loin), puis autour.

object JeuSimpsons : ModuleJeu("The Simpsons: Hit & Run", "GHQE7D", "GHQP7D") {
    // ---- version US (notes RetroAchievements)
    private const val PIECES = 0x8069D83CL          // s32
    private const val NIVEAU = 0x8069D82BL          // u8 : 0..6
    private const val MISSIONS = 0x8069C7BCL        // niveau 1 : mission 0 (tutoriel) ; +0x20 par mission, +8 = passee
    private const val NIV = 0x26CL                  // un niveau
    private const val COURSES = 0x100L              // 3 courses de rue (+0x20)
    private const val BONUS = 0x160L                // mission bonus
    private const val PARI = 0x18CL                 // s32 : temps de la course a pari (FFFFFFFF : pas faite)
    private const val VOITURES = 0x197L             // u8 : voitures debloquees
    private const val TENUES = 0x19BL               // u8 : tenues achetees
    private const val GUEPES = 0x19FL               // u8 : cameras guepes detruites
    private const val GAGS = 0x1B3L                 // u8 : gags trouves
    private const val CARTES = 0x8069C65BL          // u8 par niveau, +0x20 ; total en 0x8069C71F
    private const val CARTES_TOT = 0x8069C71FL
    private const val ETAT = 0x807F5C77L            // u8 : etat du jeu
    private const val FUITE = 0x806BCE2CL           // f32 : compteur de delit de fuite (0..100)
    private const val VOITURE = 0x806BCE87L         // u8 : voiture
    private const val SCRIPT = 0x806B0FA0L          // 8 octets : « el01\m7l » (niveau, mission, l = commencee)

    private val PERSOS = listOf("Homer", "Bart", "Lisa", "Marge", "Apu", "Bart", "Homer")
    val MISSIONS_NOMS = listOf(
        listOf("The Cola Caper", "S-M-R-T", "Petty Theft Homer", "Office Spaced", "Blind Big Brother", "Flowers By Irene", "Bonestorm Storm", "The Fat and the Furious"),
        listOf("Detention Deficit Disorder", "Weapons of Mass Delinquency", "Vox Nerduli", "Bart 'n' Frink", "Better Than Beef", "Monkey See Monkey D'oh", "Cell-Outs"),
        listOf("Nerd Race Queen", "Clueless", "Bonfire of the Manatees", "Operation Hellfish", "Slithery Sleuthing", "Fishy Deals", "The Old Pirate and the Sea"),
        listOf("For A Few Donuts More", "Redneck Roundup", "Ketchup Logic", "Return of the Nearly-Dead", "Wolves Stole My Pills", "The Cola Wars", "From Outer Space"),
        listOf("Incriminating Caffeine", "...and Baby Makes 8", "Eight is Too Much", "This Little Piggy", "Never Trust a Snake", "Kwik Cash", "Curious Curator"),
        listOf("Going to the Lu'", "Getting Down with the Clown", "Lab Coat Caper", "Duff for Me, Duff for You", "Full Metal Jackass", "Set to Kill", "Kang and Kodos Strike Back"),
        listOf("Rigor Motors", "Long Black Probes", "Pocket Protector", "There's Something About Monty", "Alien Autotopsy Part I", "Alien Autotopsy Part II", "Alien Autotopsy Part III"))
    private val BONUS_NOMS = listOf("This Old Shanty", "Dial B for Blood", "Princi-pal", "Beached Love", "Kinky Frinky", "Milking the Pigs", "Flaming Tires")
    private val ETATS = mapOf(0 to "Démarrage", 1 to "Démarrage", 2 to "Menu principal", 5 to "Jeu bonus", 6 to "Jeu bonus : chargement",
        7 to "Jeu bonus : course", 8 to "Chargement", 9 to "En jeu", 10 to "Pause", 11 to "Sortie")
    private val VOITURES_NOMS = mapOf(0x00 to "Ferrini (rouge)", 0x01 to "Longhorn", 0x02 to "Bandit", 0x03 to "70's Sports Car", 0x04 to "Family Sedan",
        0x05 to "WWII Vehicle", 0x06 to "Pickup Truck", 0x07 to "Police Car", 0x09 to "Canyonero", 0x0C to "Mr. Burns' Limo", 0x10 to "Zombie Car",
        0x13 to "Surveillance Van", 0x14 to "Compact Car", 0x15 to "Kremlin", 0x16 to "Sedan", 0x17 to "Cola Truck", 0x18 to "Chase Sedan",
        0x19 to "Police Car", 0x1A to "Cell Phone Car A", 0x1B to "Cell Phone Car B", 0x1C to "Cell Phone Car C", 0x1D to "Cell Phone Car D",
        0x1E to "Minivan", 0x1F to "Pickup", 0x20 to "Taxi", 0x21 to "Sports Car A", 0x22 to "Sports Car B", 0x23 to "SUV", 0x24 to "Station Wagon",
        0x25 to "Hover Bike", 0x26 to "36 Stutz Bearcat", 0x27 to "Honor Roller", 0x28 to "Armored Truck", 0x29 to "Curator", 0x2A to "Hearse",
        0x2B to "Krusty's Limo", 0x2C to "Limo", 0x2D to "Nerd Car", 0x2E to "Hover Car", 0x2F to "Milk Truck", 0x30 to "Donut Truck",
        0x31 to "El Carro Loco", 0x32 to "Book Burning Van", 0x33 to "Car Built For Homer", 0x34 to "Electaurus", 0x35 to "Open Wheel Race Car",
        0x36 to "WWII Vehicle (fusée)", 0x37 to "Moe's Sedan", 0x38 to "Mr. Plow", 0x39 to "School Bus", 0x3A to "Plow King",
        0x3B to "Globex Super Villain Car", 0x3C to "Tractor", 0x3D to "Sedan A", 0x3E to "Sedan B", 0x3F to "Ferrini (noire)", 0x40 to "Cube Van",
        0x41 to "Duff Truck", 0x42 to "Nonuplets Minivan", 0x43 to "Malibu Stacy Car", 0x44 to "Clown Car", 0x45 to "Coffin Car",
        0x46 to "Hallo Hearse", 0x47 to "Ghost Ship", 0x48 to "Witch Broom", 0x49 to "Charred Husk", 0x4A to "ATV", 0x4B to "R/C Buggy",
        0x4C to "Planet Hype 50's Car", 0x4D to "Knight Boat", 0x4E to "Monorail Car", 0x4F to "Obliteratatron Big Wheel Truck",
        0x50 to "Speed Rocket", 0x51 to "Ambulance", 0x52 to "Burns Armored Truck", 0x53 to "Fish Van", 0x54 to "Garbage Truck",
        0x55 to "Ice Cream Truck", 0x56 to "Itchy and Scratchy Movie Truck", 0x57 to "Nuclear Waste Truck", 0x58 to "Pizza Van",
        0x59 to "Mini School Bus", 0x5A to "\"Vote Quimby\" Truck", 0x5B to "Glass Truck", 0x5C to "Fire Truck", 0x5D to "Bone Truck",
        0x5E to "Redbrick", 0x60 to "À pied")

    // ---- version europeenne : decalage de la feuille, cherche une fois
    @Volatile private var decalageEU: Long? = null
    @Volatile private var dernierEssaiEU = 0L

    /** la feuille de progression est-elle a sa place (decalage d) ? */
    private fun feuilleOk(l: Lecteur, d: Long): Boolean {
        if ((l.u8(NIVEAU + d) ?: return false) > 6) return false
        if ((l.s32(PIECES + d) ?: return false) !in 0..9_999_999) return false
        for (k in 0 until 7) {
            val t = l.u32(MISSIONS + NIV * k + PARI + d) ?: return false
            if (t != 0xFFFFFFFFL && t !in 1L..3600L) return false
        }
        for (i in 0 until 8) {
            if ((l.u8(MISSIONS + 0x20L * i + d) ?: return false) > 1) return false
            if ((l.u8(MISSIONS + 0x20L * i + 8 + d) ?: return false) > 1) return false
        }
        for (k in 0 until 7) if ((l.u8(CARTES + 0x20L * k + d) ?: return false) > 7) return false
        return true
    }

    private fun chercherEU(): Long? {
        val now = System.currentTimeMillis()
        if (now - dernierEssaiEU < 20_000L) return null
        dernierEssaiEU = now
        val l = Lecteur(budget = 160)
        val attendu = 0x660L
        if (feuilleOk(l, attendu)) return attendu
        var j = 4L
        while (j <= 0x10000L) {
            if (feuilleOk(l, attendu + j)) return attendu + j
            if (feuilleOk(l, attendu - j)) return attendu - j
            j += 4
        }
        return null
    }

    override fun lire(l: Lecteur, id: String): Fiche? {
        val d: Long = if (id == "GHQE7D") 0L else decalageEU ?: (chercherEU()?.also { decalageEU = it } ?: return Fiche(id, nom,
            "Version européenne", 0xFF2F6DB5, 0xFF0B1E3A, message = "Recherche de la progression dans la mémoire du jeu… (lance ou charge une partie)"))
        if (!feuilleOk(l, d)) {
            if (id != "GHQE7D") decalageEU = null                      // (partie rechargee, autre place : on recherchera)
            return Fiche(id, nom, "", 0xFF2F6DB5, 0xFF0B1E3A, message = "Pas de partie en cours (ou progression illisible).")
        }
        val pieces = l.s32(PIECES + d) ?: 0
        val niv = l.u8(NIVEAU + d) ?: 0
        // etat du jeu, compteur de fuite, voiture, mission en cours : autres zones (meme decalage, verifie)
        val etat = l.u8(ETAT + d)?.takeIf { it in ETATS }
        val fuite = l.fini(l.f32(FUITE + d), 101f)?.takeIf { it >= 0f }
        val voiture = l.u8(VOITURE + d)?.let { VOITURES_NOMS[it] }
        val script = l.texte(SCRIPT + d, 8)?.takeIf { it.length >= 7 && it.startsWith("el0") && it[4] == '\\' && it[5] == 'm' }
        val nivScript = script?.get(3)?.digitToIntOrNull()?.minus(1)?.takeIf { it == niv }   // (« el0X » : niveau X)
        val misEnCours = nivScript?.let { script!![6].digitToIntOrNull()?.let { m -> if (niv == 0) m else m - 1 } }   // (niveau 1 : m0 = tutoriel)
        val commencee = script?.getOrNull(7) == 'l'

        class Niv(val faites: Int, val missions: List<Pair<String, Boolean>>, val courses: Int, val bonus: Boolean, val pari: Long,
                  val voitures: Int, val tenues: Int, val guepes: Int, val gags: Int, val cartes: Int)
        fun niveau(k: Int): Niv {
            val b = MISSIONS + NIV * k + d
            val noms = MISSIONS_NOMS[k]
            val prem = if (k == 0) 1 else 0                              // (niveau 1 : la case 0 est le tutoriel)
            val ms = noms.mapIndexed { i, n -> n to ((l.u8(b + 0x20L * i) ?: 0) == 1) }
            val faites = (prem until noms.size).count { ms[it].second }
            val courses = (0 until 3).count { (l.u8(b + COURSES + 0x20L * it) ?: 0) == 1 }
            return Niv(faites, ms, courses, (l.u8(b + BONUS) ?: 0) == 1, l.u32(b + PARI) ?: 0xFFFFFFFFL,
                l.u8(b + VOITURES) ?: 0, l.u8(b + TENUES) ?: 0, l.u8(b + GUEPES) ?: 0, l.u8(b + GAGS) ?: 0,
                (l.u8(CARTES + 0x20L * k + d) ?: 0).coerceIn(0, 7))
        }
        val tous = (0 until 7).map { niveau(it) }
        val n = tous[niv]
        val cartesTot = (l.u8(CARTES_TOT + d) ?: tous.sumOf { it.cartes }).coerceIn(0, 49)
        val missionsTot = tous.sumOf { it.faites }

        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Partie", listOfNotNull(
            etat?.let { Ligne("État", ETATS[it] ?: "") },
            fuite?.let { Ligne("Délit de fuite", if (it >= 100f) "poursuivi par la police !" else "${it.toInt()} %", it / 100f,
                if (it >= 100f) 0xFFFF5A4A else if (it >= 50f) 0xFFFFB13D else 0xFF8BE08B) },
            voiture?.let { Ligne("Voiture", it) },
            misEnCours?.let { m -> MISSIONS_NOMS[niv].getOrNull(m)?.let { Ligne(if (commencee) "Mission en cours" else "Prochaine mission", it) } })))
        val num = if (niv == 0) 1 else 0
        blocs.add(Bloc("Niveau ${niv + 1} · ${PERSOS[niv]}", listOf(
            Ligne("Missions", "${n.faites} / 7", pct(n.faites, 7), 0xFFFFD23F),
            Ligne("Courses de rue", "${n.courses} / 3", pct(n.courses, 3), 0xFF4F8DFF),
            Ligne("Mission bonus", if (n.bonus) "faite (${BONUS_NOMS[niv]})" else BONUS_NOMS[niv]),
            Ligne("Course à pari", if (n.pari == 0xFFFFFFFFL) "pas gagnée" else "${n.pari / 60} min ${String.format(java.util.Locale.FRANCE, "%02d", n.pari % 60)}"),
            Ligne("Cartes", "${n.cartes} / 7", pct(n.cartes, 7), 0xFF9C5CFF),
            Ligne("Guêpes", "${n.guepes} / 20", pct(n.guepes, 20), 0xFFFF9A3C),
            Ligne("Gags", n.gags.toString()),
            Ligne("Tenues achetées", "${n.tenues} / 3", pct(n.tenues, 3)),
            Ligne("Voitures débloquées", n.voitures.toString())),
            pastilles = n.missions.drop(num).mapIndexed { i, (nm, ok) -> "${i + 1}. $nm" to ok }))
        blocs.add(Bloc("Tous les niveaux", tous.mapIndexed { k, t ->      // (build 569 : une ligne courte, le detail en dessous)
            val fait = t.faites + t.courses + (if (t.bonus) 1 else 0) + t.cartes + t.guepes
            val p = pct(fait, 7 + 3 + 1 + 7 + 20)
            listOf(Ligne("Niveau ${k + 1} · ${PERSOS[k]}", "${(p * 100).toInt()} %", p, if (k == niv) 0xFFFFD23F else 0L),
                Ligne("missions ${t.faites}/7 · cartes ${t.cartes}/7 · guêpes ${t.guepes}/20", ""))
        }.flatten()))
        val sous = "Niveau ${niv + 1} · ${PERSOS[niv]}" + (misEnCours?.let { m -> MISSIONS_NOMS[niv].getOrNull(m)?.let { " · $it" } } ?: "")
        return Fiche(id, nom, sous, 0xFF2F6DB5, 0xFF0B1E3A,
            grands = listOf("pièces" to pieces.toString(), "cartes" to "$cartesTot / 49", "missions" to "$missionsTot / 49"),
            blocs = blocs)
    }
}
