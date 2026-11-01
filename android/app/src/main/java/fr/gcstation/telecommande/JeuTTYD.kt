package fr.gcstation.telecommande

// Appli build 173 : Paper Mario : La Porte Millenaire (G8MP01 europeen, G8ME01 americain, G8MJ01 japonais).
// Seulement des adresses et des tailles de structures (aucun code repris) :
//  - doldecomp/ttyd (G8MJ01) : PouchData (mario_pouch.h), MarioWork (mario.h : flags 0, motion 0x2E, personnage 0x3C,
//    position 0x8C), GlobalWork (mariost.h : inBattle 0x14, renderTime 0x38, carte 0x12C, zone 0x13C, GSW0 0x174,
//    saveCount 0x11F0), BattleWork (battle.h : tour 0x0, units[64] 0x20), BattleUnit (battle_unit.h : type 0x8,
//    camp 0xC, PV max 0x108, PV 0x10C, PF max 0x10E, PF 0x112), listes des objets et des combattants ;
//  - PistonMiner/ttyd-tools (ttyd.us.lst, ttyd.eu.lst, ttyd.jp.lst) : adresses de marioGetPtr et _swByteGet ;
//  - le main.dol europeen (G8MP01) : pouchGetPtr en 800D6CAC lit le pointeur en 8042B420 (240 lectures, toutes dans
//    mario_pouch) ; marioGetPtr lit 8042B220 ; _swByteGet lit gp en 80422810 ; le pointeur du combat est en 8042B4B0
//    (38 lectures suivies de BattleWork.flags 0xEF4 / 0x420), soit celui de l'inventaire + 0x90, comme dans la version
//    japonaise (mpp 804188E0, _battleWorkPointer 80418970).
// Europeen : adresses fixes. Americain / japonais : pointeurs lus dans le code (instruction « lwz rX, d(r13) » au debut
// de la fonction), l'inventaire cherche une fois comme avant. Chaque valeur est bornee ; rien n'est affiche sinon.
object JeuTTYD : ModuleJeu("Paper Mario : La Porte Millénaire", "G8MP01", "G8ME01", "G8MJ01") {
    private val PARTENAIRES = arrayOf("", "Goombella", "Koops", "Bombart", "Yoshi", "Cumulia", "Vivian", "Mme Gonzales")
    @Volatile private var emplacement = 0L                           // pointeur vers PouchData
    @Volatile private var ptrMario = 0L
    @Volatile private var ptrGp = 0L
    @Volatile private var pour = ""
    @Volatile private var essai = 0L

    private class Fn(val pouch: Long, val mario: Long, val sw: Long)
    private fun fn(id: String) = when (id) {
        "G8MJ01" -> Fn(0x800D1C74, 0x8005D214, 0x800847B8)
        "G8ME01" -> Fn(0x800D5EB0, 0x8005E0C0, 0x80085744)
        else -> Fn(0x800D6CAC, 0x8005E1F8, 0x80086A98)
    }

    private fun r13(l: Lecteur): Long? {
        var a = 0x80003100L
        while (a < 0x80005000L) {
            val w = l.u32(a) ?: return null
            if (w ushr 16 == 0x3DA0L) {                              // lis r13, hi
                for (k in 1..4) {
                    val w2 = l.u32(a + 4L * k) ?: break
                    val hi = (w and 0xFFFF) shl 16
                    if (w2 ushr 16 == 0x61ADL) return hi or (w2 and 0xFFFF)
                    if (w2 ushr 16 == 0x39ADL) return (hi + (w2 and 0xFFFF).toShort().toLong()) and 0xFFFFFFFFL
                }
            }
            a += 4
        }
        return null
    }
    /** « lwz rX, d(r13) » a l'adresse a : l'adresse lue (r13 + d) */
    private fun slot(l: Lecteur, r: Long, a: Long): Long {
        val w = l.u32(a) ?: return 0L
        if (w ushr 26 != 32L || (w ushr 16) and 31 != 13L) return 0L
        return (r + (w and 0xFFFF).toShort().toLong()) and 0xFFFFFFFFL
    }
    private fun vraisemblable(l: Lecteur, p: Long): Boolean {
        val pv = l.s16(p + 0x70) ?: return false; val pvMax = l.s16(p + 0x72) ?: return false
        val pf = l.s16(p + 0x74) ?: return false; val pfMax = l.s16(p + 0x76) ?: return false
        val pieces = l.s16(p + 0x78) ?: return false; val niveau = l.s16(p + 0x8A) ?: return false
        return pvMax in 1..999 && pv in 0..pvMax && pfMax in 0..999 && pf in 0..pfMax && pieces in 0..999 && niveau in 1..99
    }
    private fun trouver(l: Lecteur, id: String) {
        if (id == "G8MP01") {                                        // europeen : connu (main.dol)
            if (l.u32(0x800D6CAC) == 0x806D1CC0L && l.u32(0x800D6CB0) == 0x4E800020L) { emplacement = 0x8042B420L; ptrMario = 0x8042B220L; ptrGp = 0x80422810L }
            return
        }
        val r = r13(l) ?: return
        val f = fn(id)
        val s = slot(l, r, f.pouch)
        if (s != 0L && l.u32(f.pouch + 4) == 0x4E800020L) l.ptr(s)?.let { if (vraisemblable(l, it)) emplacement = s }
        ptrMario = slot(l, r, f.mario)
        ptrGp = slot(l, r, f.sw)
    }

    // zones (les 3 lettres du nom de la carte) : chapitre et nom
    private val ZONES = mapOf(
        "aaa" to (0 to "Maison de Mario"), "gor" to (0 to "Port (Rogueport)"), "tik" to (0 to "Égouts du port"),
        "hei" to (1 to "Prairie (Petal Meadows)"), "nok" to (1 to "Petalburg"), "gon" to (1 to "Château d'Hooktail"),
        "win" to (2 to "Bois (Boggly Woods)"), "mri" to (2 to "Le Grand Arbre"), "tou" to (3 to "Glitzville"),
        "usu" to (4 to "Twilight Town"), "gra" to (4 to "Sentier du crépuscule"), "jin" to (4 to "Clocher (Creepy Steeple)"),
        "muj" to (5 to "Île de Keelhaul"), "dou" to (5 to "Grotte des pirates"), "rsh" to (6 to "Excess Express"),
        "eki" to (6 to "Gare de Riverside"), "hom" to (6 to "Gare de Poshley"), "pik" to (6 to "Poshley Heights"),
        "bom" to (7 to "Fahr Outpost"), "moo" to (7 to "La Lune"), "aji" to (7 to "Forteresse X-Naut"),
        "las" to (8 to "Palais des Ombres"), "jon" to (-1 to "Puits des 100 épreuves"), "kpa" to (-1 to "Bowser"),
        "yuu" to (-1 to "Salle de jeux des Piantas"), "end" to (-1 to "Fin"), "dmo" to (-1 to "Démonstration"), "title" to (-1 to "Écran titre"))
    private val PERSO = arrayOf("Mario", "Peach", "Bowser")
    private val OBJETS = mapOf(
        0x80 to "Éclair", 0x81 to "Foudre", 0x82 to "Étoile filante", 0x83 to "Tempête de glace", 0x84 to "Fleur de feu",
        0x85 to "Séisme", 0x86 to "Drap de Boo", 0x87 to "Champi volt", 0x88 to "Cape répulsive", 0x89 to "Poudre ruine",
        0x8A to "Mouton dormeur", 0x8B to "Bloc POW", 0x8C to "Chrono", 0x8D to "Cadran vertige", 0x8E to "Coup de poing",
        0x8F to "Carapace courage", 0x90 to "Vole-PV", 0x91 to "Échange", 0x92 to "Mini Mr. Mini", 0x93 to "Ramollisseur",
        0x94 to "Champignon", 0x95 to "Super champignon", 0x96 to "Ultra champignon", 0x97 to "Champi vie", 0x98 to "Champi séché",
        0x99 to "Tonique", 0x9A to "Sirop de miel", 0x9B to "Sirop d'érable", 0x9C to "Gelée", 0x9D to "Champi lent",
        0x9E to "Sirop graduel", 0x9F to "Hot-dog", 0xA0 to "Gâteau", 0xA1 to "Inversion", 0xA2 to "Masque effrayant",
        0xA3 to "Mystère", 0xA4 to "Bon d'auberge", 0xA5 to "Bosse Whacka", 0xA6 to "Noix de coco", 0xA7 to "Bouquet séché",
        0xA8 to "Œuf mystique", 0xA9 to "Feuille dorée", 0xAA to "Mangue", 0xAB to "Pâtes fraîches", 0xAC to "Préparation à gâteau",
        0xAD to "Sauce piquante", 0xAE to "Feuille de tortue", 0xAF to "Prêle", 0xB0 to "Pêche", 0xB1 to "Sac rancune",
        0xB2 to "Malédiction Koopa", 0xB3 to "Champi frit", 0xB4 to "Champi rôti", 0xB5 to "Steak de champi", 0xB6 to "Raté",
        0xB7 to "Champi au miel", 0xB8 to "Champi à l'érable", 0xB9 to "Champi en gelée", 0xBA to "Super au miel",
        0xBB to "Super à l'érable", 0xBC to "Super en gelée", 0xBD to "Ultra au miel", 0xBE to "Ultra à l'érable",
        0xBF to "Ultra en gelée", 0xC0 to "Soupe épicée", 0xC1 to "Dîner de Zess", 0xC2 to "Spécial de Zess",
        0xC3 to "Deluxe de Zess", 0xC4 to "Dynamite de Zess", 0xC5 to "Thé de Zess", 0xC6 to "Repas spatial",
        0xC7 to "Glace à l'eau", 0xC8 to "Frappé de Zess", 0xC9 to "Lapin des neiges", 0xCA to "Bombe coco",
        0xCB to "Repas courage", 0xCC to "Gâteau champi", 0xCD to "Crêpe champi", 0xCE to "Gâteau mousse", 0xCF to "Œuf au plat",
        0xD0 to "Parfait aux fruits", 0xD1 to "Œuf bombe", 0xD2 to "Pâtes à l'encre", 0xD3 to "Spaghetti", 0xD4 to "Bouillon champi",
        0xD5 to "Champi poison", 0xD6 to "Gâteau au chocolat", 0xD7 to "Délice mangue", 0xD8 to "Pudding d'amour",
        0xD9 to "Repas météore", 0xDA to "Ragoût d'épreuve", 0xDB to "Gâteau des amoureux", 0xDC to "Sauce à l'encre",
        0xDD to "Omelette", 0xDE to "Thé Koopa", 0xDF to "Koopâtes", 0xE0 to "Pâtes épicées", 0xE1 to "Gâteau du cœur",
        0xE2 to "Tarte à la pêche", 0xE3 to "Sucette électrique", 0xE4 to "Sucette de feu", 0xE5 to "Bonbon au miel",
        0xE6 to "Bonbon coco", 0xE7 to "Bonbon en gelée", 0xE8 to "Biscuit de Zess", 0xE9 to "Salade", 0xEA to "Pain Koopa",
        0xEB to "Jus frais")
    private val BADGES = mapOf(
        0xF0 to "Saut puissant", 0xF1 to "Multirebond", 0xF2 to "Rebond puissant", 0xF3 to "Saut tornade", 0xF4 to "Saut rétrécissant",
        0xF5 to "Saut dodo", 0xF6 to "Saut ramollissant", 0xF7 to "Coup puissant", 0xF8 to "Marteau séisme", 0xF9 to "Lancer de marteau",
        0xFA to "Coup perçant", 0xFB to "Coup étourdissant", 0xFC to "Marteau de feu", 0xFD to "Marteau de glace", 0xFE to "Double dose",
        0x100 to "Charge", 0x102 to "Super appel", 0x104 to "Attaque +", 0x106 to "Att.+ Déf.-", 0x108 to "Tout ou rien",
        0x10A to "Méga rush", 0x10C to "Rush puissant", 0x10E to "Att.- Déf.+", 0x110 to "Dernier recours", 0x112 to "Défense +",
        0x114 to "Esquive", 0x116 to "PV +", 0x118 to "PF +", 0x119 to "Économie de fleurs", 0x11B to "Pouvoir glacé",
        0x11C to "Bouclier à pics", 0x11D to "Bien-être", 0x11F to "Électrochoc", 0x120 to "Double peine", 0x121 to "Sauteur",
        0x122 to "Marteleur", 0x123 to "Retour à l'envoyeur", 0x124 to "Cœur joyeux", 0x126 to "Fleur joyeuse", 0x127 to "Vole-PV",
        0x129 to "Vole-PF", 0x12B to "Esquive in extremis", 0x12D to "Chanceux", 0x12F to "Jour de chance", 0x131 to "Remboursement",
        0x132 to "Fleur de pitié", 0x134 to "Changement rapide", 0x135 to "Coucou", 0x136 to "Tuteur", 0x137 to "Chercheur de cœurs",
        0x138 to "Chercheur de fleurs", 0x139 to "Argent argent", 0x13A to "Accro aux objets", 0x140 to "Calme", 0x141 to "Première attaque",
        0x142 to "Attaque éclair", 0x143 to "Lenteur", 0x144 to "Simplificateur", 0x145 to "Complexificateur", 0x146 to "Départ chanceux",
        0x147 to "Emblème L", 0x148 to "Emblème W", 0x149 to "Triple dose", 0x14C to "Méga saut", 0x14D to "Méga coup", 0x14E to "Méga séisme")
    private val ETOILES = listOf(0x72 to "Diamant", 0x73 to "Émeraude", 0x74 to "Or", 0x75 to "Rubis", 0x76 to "Saphir", 0x77 to "Grenat", 0x78 to "Cristal")
    private val FR = mapOf(0xDE to "Mario", 0xE0 to "Goombella", 0xE1 to "Koops", 0xE2 to "Yoshi", 0xE3 to "Cumulia", 0xE4 to "Vivian", 0xE5 to "Bombart", 0xE6 to "Mme Gonzales")
    // (noms des combattants : liste publique de la decompilation, en anglais)
    private val NOMS = "Null|Goomba|Paragoomba|Spiky Goomba|Spinia|Spania|Lord Crump (prologue)|Gus|Blooper|Blooper Left Tentacle|Blooper Right Tentacle|Koopatrol|Magikoopa|Magikoopa Clone|Koopa Troopa|Paratroopa|Fuzzy|Dull Bones|Bald Cleft|Bristle|Gold Fuzzy|Fuzzy Horde|Red Bones|Hooktail|Dark Puff|Pale Piranha|Cleft|Pider|X Naut|Yux|Mini Yux|Beldam Chapter2|Marilyn Chapter2|Vivian Chapter2|Magnus|X Fist|Goomba Glitzville|Kp Koopa|Kp Paratroopa|Pokey|Lakitu|Spiny|Hyper Bald Cleft|Bob Omb|Bandit|Big Bandit|Red Spiky Buzzy|Shady Koopa|Shady Paratroopa|Red Magikoopa|Red Magikoopa Clone|White Magikoopa|White Magikoopa Clone|Green Magikoopa|Green Magikoopa Clone|Dark Craw|Hammer Bro|Boomerang Bro|Fire Bro|Red Chomp|Dark Koopatrol|Iron Cleft Red|Iron Cleft Green|Bowser Chapter3|Rawk Hawk|Macho Grubba|Hyper Goomba|Hyper Paragoomba|Hyper Spiky Goomba|Crazee Dayzee|Amazy Dayzee|Hyper Cleft|Buzzy Beetle|Spike Top|Swooper|Boo|Atomic Boo|Doopliss Chapter4 Fight1|Doopliss Chapter4 Invincible|Doopliss Chapter4 Fight2|Goombella Chapter4|Koops Chapter4|Yoshi Chapter4|Flurrie Chapter4|Ember|Lava Bubble|Green Fuzzy|Flower Fuzzy|Putrid Piranha|Parabuzzy|Bill Blaster|Bullet Bill|Bulky Bob Omb|Cortez|Cortez Bone Pile|Cortez Sword|Cortez Hook|Cortez Rapier|Cortez Saber|Lord Crump Chapter5|X Nauts Crump Formation1|X Nauts Crump Formation2|X Nauts Crump Formation3|Ruff Puff|Poison Pokey|Spiky Parabuzzy|Dark Boo|Smorg|Smorg Miasma Tentacle A|Smorg Miasma Tentacle B|Smorg Miasma Tentacle C|Smorg Miasma Claw|Ice Puff|Frost Piranha|Moon Cleft|Z Yux|Mini Z Yux|X Yux|Mini X Yux|X Naut Phd|Elite X Naut|Magnus2 0|X Punch|Swoopula|Phantom Ember|Bombshell Bill Blaster|Bombshell Bill|Chain Chomp|Dark Wizzerd|Dark Wizzerd Clone|Dry Bones|Dark Bones|Gloomtail|Beldam Chapter8|Marilyn Chapter8|Doopliss Chapter8|Doopliss Chapter8 Mario|Doopliss Chapter8 Goombella|Doopliss Chapter8 Koops|Doopliss Chapter8 Yoshi|Doopliss Chapter8 Flurrie|Doopliss Chapter8 Vivian|Doopliss Chapter8 Bobbery|Doopliss Chapter8 Ms Mowz|Bowser Chapter8|Kammy Koopa|Grodus|Grodus X|Shadow Queen Peach|Shadow Queen Invincible|Shadow Queen Demon|Shadow Queen Left Right Hand|Shadow Queen Dead Hands|Gloomba|Paragloomba|Spiky Gloomba|Dark Koopa|Dark Paratroopa|Badge Bandit|Dark Lakitu|Sky Blue Spiny|Wizzerd|Piranha Plant|Spunia|Arantula|Dark Bristle|Poison Puff|Swampire|Bob Ulk|Elite Wizzerd|Elite Wizzerd Clone|Bonetail|Red Buzzy|Red Parabuzzy|Red Spiky Parabuzzy|Hyper Bob Omb|Ultra Bob Omb|Tutorial Goombella|Tutorial Frankly B2|Tutorial Frankly B3|Tutorial Frankly B4|Epilogue Doopliss Mario|Epilogue Flurrie|Epilogue Boo|Epilogue Atomic Boo|Epilogue Male Toad|Epilogue Female Toad|Test|Crump2|Beldam2|Marilyn2|Vivian2|Beldam3|Marilyn3|Mecha Kuri|Mecha Kame|Okorl|Yowarl|Tuyonarl|Wanawana|Minarai Kamec|Shy Guy|Groove Guy|Pyro Guy|Spy Guy|Anti Guy|Bzzap|Mini Bzzap|Ufo|Pennington|Fighter|Zess T|Master|Reporter|Hotdog Master|Flavio|Tree|Switch|Testnpc|Bomb Squad Bomb|System|Prologue Goombella|Mario|Shell Shield|Goombella|Koops|Yoshi|Flurrie|Vivian|Bobbery|Ms. Mowz".split('|')

    private fun nomObjet(i: Int) = OBJETS[i] ?: BADGES[i] ?: ("Objet " + i)
    private fun nomBadge(i: Int) = BADGES[i] ?: BADGES[i - 1]?.let { "$it (P)" } ?: ("Badge " + i)

    override fun lire(l: Lecteur, id: String): Fiche? {
        if (pour != id) { pour = id; emplacement = 0L; ptrMario = 0L; ptrGp = 0L }
        if (emplacement == 0L) {
            val t = System.currentTimeMillis()
            if (t - essai < 4000) return Fiche(id, "Paper Mario", "Recherche de l'inventaire…", 0xFF8F1E5E, 0xFF22071A, message = "Une partie doit être chargée (pas l'écran titre).")
            essai = t
            trouver(Lecteur(80), id)
            if (emplacement == 0L) return Fiche(id, "Paper Mario", if (id == "G8MP01") "Version du jeu inconnue" else "Inventaire pas encore trouvé", 0xFF8F1E5E, 0xFF22071A,
                message = if (id == "G8MP01") "Le programme du jeu ne correspond pas à la version européenne connue : rien n'est affiché."
                          else "Charge une partie : l'appli cherche toute seule où le jeu range l'inventaire (rien n'est affiché tant que ce n'est pas sûr).")
        }
        val p = l.ptr(emplacement) ?: return null
        if (!vraisemblable(l, p)) return Fiche(id, "Paper Mario", "Pas de partie chargée", 0xFF8F1E5E, 0xFF22071A, message = "Écran titre ou chargement.")
        fun s(o: Int) = l.s16(p + o) ?: 0
        val pv = s(0x70); val pvMax = s(0x72); val pf = s(0x74); val pfMax = s(0x76); val pieces = s(0x78)
        val ps = s(0x7A).coerceAtLeast(0); val psMax = s(0x7C).coerceIn(0, 800); val rang = s(0x88); val niveau = s(0x8A); val pouvoirs = s(0x8C)
        val pb = s(0x92); val pbMax = s(0x94); val points = s(0x96).coerceIn(0, 99)
        val saut = (l.u8(p + 0x98) ?: 0).coerceIn(0, 2); val marteau = (l.u8(p + 0x99) ?: 0).coerceIn(0, 2)
        val eclats = s(0x9A).coerceIn(0, 999); val soleils = s(0x9C).coerceIn(0, 999); val record = s(0x9E).coerceIn(0, 100)
        val public = l.fini(l.f32(p + 0x84), 1e6f)?.toInt()?.coerceIn(0, 200) ?: 0
        fun liste(o: Int, n: Int): List<Int> { val r = ArrayList<Int>(); for (k in 0 until n) { val v = l.s16(p + o + 2L * k) ?: 0; if (v in 1..0x200) r.add(v) }; return r }
        val tenus = liste(0x192, 20); val reserve = liste(0x1BA, 32); val badges = liste(0x1FA, 200); val equipes = liste(0x38A, 200)
        val cles = liste(0xA0, 121)
        val yoshi = (l.texte(p + 0x5C0, 16) ?: "").filter { it.code in 32..255 }
        val etoiles = ETOILES.map { (n, nom) -> nom to (n in cles) }
        val equipe = ArrayList<Ligne>()
        for (k in 1 until 8) {
            val o = p + 0xEL * k
            val fl = l.u16(o) ?: 0
            if (fl and 1 == 0) continue
            val hp = (l.s16(o + 6) ?: 0).coerceAtLeast(0); val hpMax = (l.s16(o + 2) ?: 0).coerceIn(0, 999)
            val rangP = ((l.s16(o + 8) ?: 0) + 1).coerceIn(1, 3)
            equipe.add(Ligne(if (k == 4 && yoshi.isNotEmpty()) "$yoshi (Yoshi)" else PARTENAIRES[k], "$hp / $hpMax PV · " + arrayOf("", "normal", "Super", "Ultra")[rangP], pct(hp, hpMax), 0xFF3DDC84))
        }
        // GlobalWork : carte, chapitre, combat, temps
        var carte = ""; var zone = ""; var combat = false; var gsw0 = -1; var temps = -1L; var sauvegardes = -1
        l.ptr(ptrGp)?.let { g ->
            carte = (l.texte(g + 0x12C, 16) ?: "").takeIf { it.isNotEmpty() && it.all { c -> c.code in 32..126 } } ?: ""
            zone = (l.texte(g + 0x13C, 32) ?: "").takeIf { it.all { c -> c.code in 32..126 } } ?: ""
            combat = (l.s32(g + 0x14) ?: 0) != 0
            gsw0 = l.s32(g + 0x174)?.takeIf { it in 0..1000 } ?: -1
            temps = (l.u32(g + 0x38 + 4)?.let { lo -> ((l.u32(g + 0x38) ?: 0L) shl 32) or lo } ?: -1L).takeIf { it in 1..(1L shl 58) } ?: -1L
            sauvegardes = l.s32(g + 0x11F0)?.takeIf { it in 0..99999 } ?: -1
        }
        val z = ZONES[carte.take(3)] ?: ZONES[zone.take(3)] ?: ZONES[carte.substringBefore('_')]
        // Mario sur la carte
        var pos: FloatArray? = null; var perso = -1; var motion = -1
        l.ptr(ptrMario)?.let { m ->
            val x = l.fini(l.f32(m + 0x8C), 1e5f); val y = l.fini(l.f32(m + 0x90), 1e5f); val zz = l.fini(l.f32(m + 0x94), 1e5f)
            if (x != null && y != null && zz != null) pos = floatArrayOf(x, y, zz)
            perso = (l.u8(m + 0x3C) ?: 255).let { if (it in 0..2) it else -1 }
            motion = l.u16(m + 0x2E)?.takeIf { it in 0..0x28 } ?: -1
        }
        val rangs = arrayOf("Débutant", "Étoile montante", "Étoile", "Superstar")
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Mario", listOf(
            Ligne("Points de vie", "$pv / $pvMax", pct(pv, pvMax), 0xFFFF3B5C),
            Ligne("Points de fleur", "$pf / $pfMax", pct(pf, pfMax), 0xFF4F8DFF),
            Ligne("Pouvoir étoile", (ps / 100).toString() + " / " + (psMax / 100), pct(ps, psMax), 0xFFFFD23F),
            Ligne("Points de badge libres", "$pb / $pbMax", pct(pb, pbMax), 0xFF9C6CFF),
            Ligne("Points étoile (prochain niveau)", "$points / 100", pct(points, 100), 0xFFFFB300),
            Ligne("Niveau", niveau.toString()), Ligne("Rang", rangs.getOrNull(rang) ?: rang.toString()),
            Ligne("Bottes", arrayOf("Bottes", "Super bottes", "Ultra bottes")[saut]),
            Ligne("Marteau", arrayOf("Marteau", "Super marteau", "Ultra marteau")[marteau]),
            Ligne("Pouvoirs étoile", Integer.bitCount(pouvoirs and 0xFF).toString() + " / 8"),
            Ligne("Public (dernier combat)", public.toString()))))
        if (equipe.isNotEmpty()) blocs.add(Bloc("Équipe", equipe))
        blocs.add(Bloc("Étoiles de cristal", pastilles = etoiles))
        // combat en cours : les combattants (BattleWork : pointeur de l'inventaire + 0x90)
        if (combat) l.ptr(emplacement + 0x90)?.let { bw -> combatBloc(l, bw)?.let { blocs.add(it) } }
        blocs.add(Bloc("Collection", listOf(
            Ligne("Pièces", "$pieces / 999", pct(pieces, 999), 0xFFFFD23F),
            Ligne("Éclats d'étoile", "$eclats / 100", pct(eclats, 100), 0xFF9AD0FF), Ligne("Soleils", "$soleils / 41", pct(soleils, 41), 0xFFFFB300),
            Ligne("Objets", "${tenus.size} / 20 (réserve : ${reserve.size} / 32)"), Ligne("Badges", "${badges.size} (équipés : ${equipes.size})"),
            Ligne("Objets importants", cles.size.toString()), Ligne("Record du Puits des 100 épreuves", if (record > 0) "étage $record" else "–"))))
        if (tenus.isNotEmpty()) blocs.add(Bloc("Objets sur Mario", pastilles = tenus.map { nomObjet(it) to true }))
        if (equipes.isNotEmpty()) blocs.add(Bloc("Badges équipés", pastilles = equipes.map { nomBadge(it) to true }))
        val lignesPartie = ArrayList<Ligne>()
        if (z != null) lignesPartie.add(Ligne("Chapitre", when (z.first) { 0 -> "Port / prologue"; -1 -> "–"; else -> z.first.toString() + " / 8" }))
        if (carte.isNotEmpty()) lignesPartie.add(Ligne("Carte", carte))
        if (gsw0 >= 0) lignesPartie.add(Ligne("Avancée de l'histoire", "$gsw0 / 405", pct(gsw0, 405), 0xFFB8327A))
        if (perso >= 0) lignesPartie.add(Ligne("Personnage", PERSO[perso]))
        pos?.let { lignesPartie.add(Ligne("Position", String.format(java.util.Locale.FRANCE, "x %.0f · y %.0f · z %.0f", it[0], it[1], it[2]))) }
        if (temps > 0) lignesPartie.add(Ligne("Temps de jeu (depuis l'allumage, hors menus)", dureeS(temps / 40_500_000L)))
        if (sauvegardes >= 0) lignesPartie.add(Ligne("Sauvegardes", sauvegardes.toString()))
        if (lignesPartie.isNotEmpty()) blocs.add(Bloc("Partie", lignesPartie))
        val titre = z?.second ?: "Paper Mario"
        return Fiche(id, titre, (if (combat) "En combat · " else "") + "Niveau $niveau · " + (rangs.getOrNull(rang) ?: ""), 0xFFB8327A, 0xFF2A0A1C,
            grands = listOf("PV" to "$pv/$pvMax", "PF" to "$pf/$pfMax", "pièces" to pieces.toString()), blocs = blocs)
    }

    private fun combatBloc(l: Lecteur, bw: Long): Bloc? {
        val tour = l.s16(bw)?.takeIf { it in 0..999 } ?: return null
        val lignes = ArrayList<Ligne>()
        lignes.add(Ligne("Tour", tour.toString()))
        var n = 0
        for (k in 0 until 64) {
            val u = l.ptr(bw + 0x20 + 4L * k) ?: continue
            val type = l.s32(u + 8) ?: continue
            if (type !in 1..0xE6 || type == 0xDC || type == 0xDB || type == 0xDF) continue
            val pvMax = l.s16(u + 0x108) ?: continue; val pv = l.s16(u + 0x10C) ?: continue
            if (pvMax !in 1..9999 || pv !in 0..pvMax) continue
            val nom = FR[type] ?: NOMS.getOrNull(type)?.takeIf { it.isNotEmpty() } ?: ("Ennemi n° $type")
            val equipeMario = type in 0xDD..0xE6
            lignes.add(Ligne(nom, "$pv / $pvMax PV", pct(pv, pvMax), if (equipeMario) 0xFF3DDC84 else 0xFFFF3B3B))
            if (++n >= 12) break
        }
        if (n == 0) return null
        return Bloc("Combat", lignes)
    }
}
