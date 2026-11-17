package fr.gcstation.telecommande

// Appli 572 : ecrans compagnon de Metroid Prime 1 et 2, Sonic Adventure 2 Battle, Sonic Adventure DX et Shrek Extra Large
// (appli privee seulement, en essai). Memes regles que Jeux2.kt : lecture en direct par Pulse, chaque valeur verifiee
// (bornes) ; une version du jeu inconnue n'affiche rien. Adresses : notes de code publiques de RetroAchievements (pages
// « Code Notes » de chaque jeu, envoyees par le proprietaire le 15 novembre 2026) ; seulement des adresses.

/** appli 573 : plan vu de dessus qui se dessine en jouant (un par niveau ou salle : cle) : le chemin parcouru et la position.
 *  Aucune carte du jeu (rien n'est extrait du disque). saut : au-dela de cette distance d'un coup (teleportation, nouveau
 *  niveau sans numero lisible), le chemin recommence. */
object Traces {
    private val chemins = HashMap<String, ArrayList<FloatArray>>()
    @Synchronized fun bloc(cle: String, titre: String, xl: Float?, zl: Float?, lettre: String, couleur: Long, saut: Float = Float.MAX_VALUE, texte: String = ""): Bloc? {
        if (xl == null || zl == null || !xl.isFinite() || !zl.isFinite() || Math.abs(xl) > 1e6f || Math.abs(zl) > 1e6f) return null
        val x: Float = xl; val z: Float = zl
        val ch = chemins.getOrPut(cle) { ArrayList() }
        val d = ch.lastOrNull()
        if (d != null) {
            val dist = Math.hypot((d[0] - x).toDouble(), (d[1] - z).toDouble()).toFloat()
            if (dist > saut) ch.clear()
        }
        var x0 = x; var x1 = x; var z0 = z; var z1 = z
        for (c in ch) { x0 = minOf(x0, c[0]); x1 = maxOf(x1, c[0]); z0 = minOf(z0, c[1]); z1 = maxOf(z1, c[1]) }
        val pas = maxOf(maxOf(x1 - x0, z1 - z0) / 150f, 0.05f)
        val e = ch.lastOrNull()
        if (e == null || Math.hypot((e[0] - x).toDouble(), (e[1] - z).toDouble()) > pas) { ch.add(floatArrayOf(x, z)); if (ch.size > 900) ch.removeAt(0) }
        if (chemins.size > 60) chemins.keys.firstOrNull { it != cle }?.let { chemins.remove(it) }
        val pts = ArrayList<FloatArray>(ch)
        if (pts.size < 2) return null
        val n = pts.size
        return Bloc(titre, plan = Plan(pts, List(n) { 0xFF7A9CC8 }, (1 until n).map { intArrayOf(it - 1, it) }, listOf(Pion(x, z, couleur, lettre)),
            petits = (0 until n).toSet()), texte = texte.ifEmpty { "Ton chemin ici (il se dessine en avançant), $lettre : ta position." })
    }
}

private fun f64(l: Lecteur, a: Long): Double? {
    val hi = l.u32(a) ?: return null; val lo = l.u32(a + 4) ?: return null
    return java.lang.Double.longBitsToDouble((hi shl 32) or lo).takeIf { it.isFinite() }
}

// ---------------------------------------------------------------- Metroid Prime
object JeuMP1 : ModuleJeu("Metroid Prime", "GM8P01", "GM8E01") {
    // adresses europeennes ; americaines : + 0x79180 (revision 2) ou + 0x78120 (revision 0), meme ecart pour tout le gestionnaire
    private fun d(l: Lecteur, id: String): Long = if (id == "GM8P01") 0L else if ((l.u8(0x80000007) ?: 0) == 0) 0x78120L else 0x79180L
    private val MONDES = mapOf(0x158EFE17L to "Frégate Orpheon", 0x39F2DE28L to "Surface de Tallon", 0x83F6FF6FL to "Ruines Chozo",
        0x3EF8237CL to "Cavernes de Magmoor", 0xA8BE6291L to "Monts Phendrana", 0xB1AC4D65L to "Mines de Phazon",
        0xC13B09D1L to "Cratère d'impact", 0x13D79165L to "Cinématique de fin")
    private val RAYONS = mapOf(0 to "Rayon de puissance", 1 to "Rayon de glace", 2 to "Rayon ondulatoire", 3 to "Rayon plasma", 4 to "Rayon Phazon", 0x1B to "Rayon Phazon")
    private val VISEURS = arrayOf("Combat", "Rayons X", "Scanner", "Thermique")
    private val COMBIS = arrayOf("Combinaison de puissance", "Combinaison gravité", "Combinaison Varia", "Combinaison Phazon",
        "Fusion", "Fusion gravité", "Fusion Varia", "Fusion Phazon")
    private val OBJETS = listOf(0x2C to "Rayon de puissance", 0x34 to "Rayon de glace", 0x3C to "Rayon ondulatoire", 0x44 to "Rayon plasma",
        0x54 to "Viseur scanner", 0x5C to "Bombes", 0x74 to "Viseur thermique", 0x7C to "Rayon charge", 0x84 to "Super missile",
        0x8C to "Rayon grappin", 0x94 to "Viseur à rayons X", 0x9C to "Ice Spreader", 0xA4 to "Bottes de saut spatial", 0xAC to "Boule morphing",
        0xBC to "Accélérateur", 0xC4 to "Boule araignée", 0xD4 to "Combinaison gravité", 0xDC to "Combinaison Varia", 0xE4 to "Combinaison Phazon",
        0x6C to "Lance-flammes", 0x10C to "Wavebuster")
    private val ARTEFACTS = arrayOf("Vérité", "Force", "Ancien", "Sauvage", "Vie", "Guerrier", "Chozo", "Nature", "Soleil", "Monde", "Esprit", "Nouveau-né")

    override fun lire(l: Lecteur, id: String): Fiche? {
        val dd = d(l, id)
        val pp = l.ptr(0x803E2940L + dd) ?: return Fiche(id, "Metroid Prime", "Pas de partie", 0xFF2A5C7A, 0xFF07131C, message = "Écran titre ou chargement.")
        val p = l.ptr(pp) ?: return null
        val vie = l.fini(l.f32(p + 0xC), 10000f) ?: return null
        val reservoirs = l.s32(p + 0xEC) ?: return null
        if (vie < 0f || reservoirs !in 0..14) return null
        fun s(o: Int) = l.s32(p + o) ?: 0
        val missiles = s(0x48); val missilesMax = s(0x4C); val pb = s(0x60); val pbMax = s(0x64)
        if (missilesMax !in 0..255 || pbMax !in 0..8) return null
        val rayon = s(0x8); val viseur = s(0x14); val combi = s(0x20)
        val ont = OBJETS.filter { s(it.first) != 0 }.map { it.second }
        val art = ARTEFACTS.indices.map { ARTEFACTS[it] to (s(0x114 + 8 * it) != 0) }
        val scans = s(0x180); val scansMax = s(0x184)
        // monde, salle, temps de jeu (etat de la partie)
        var monde = ""; var temps = -1.0
        l.ptr(0x803DF7ACL + dd)?.let { g -> monde = MONDES[l.u32(g + 0x84) ?: 0L] ?: ""; temps = f64(l, g + 0xA0)?.takeIf { it in 0.0..1e7 } ?: -1.0 }
        val salle = l.s32(0x803E2954L + dd)?.takeIf { it in 0..200 } ?: -1
        val fuite = l.fini(l.f32(0x803E2F94L + dd), 10000f)?.takeIf { it > 0f } ?: 0f
        // boss : numero dans la table des objets, sa vie (+ 0x268)
        var boss: Ligne? = null
        val bossMax = l.fini(l.f32(0x803E2FA4L + dd), 1e6f) ?: 0f
        val uid = l.u16(0x803E2FA0L + dd) ?: 0xFFFF
        if (bossMax > 0f && uid != 0xFFFF) l.ptr(0x803E2898L + dd)?.let { t -> l.ptr(t + (uid and 0x3FF) * 8L + 4)?.let { b ->
            l.fini(l.f32(b + 0x268), 1e6f)?.takeIf { it in 0f..bossMax * 1.01f }?.let { v -> boss = Ligne("Boss", "${v.toInt()} / ${bossMax.toInt()}", v / bossMax, 0xFFFF5A3C) } } }
        val pleins = ((vie.toInt() - 1) / 100).coerceIn(0, reservoirs)
        val blocs = ArrayList<Bloc>()
        val lignes = arrayListOf(
            Ligne("Énergie", vie.toInt().toString() + " (réservoirs " + pleins + " / " + reservoirs + ")", vie / (99f + 100f * reservoirs), 0xFFFFB300),
            Ligne("Missiles", "$missiles / $missilesMax", pct(missiles, missilesMax), 0xFFFF6F3C),
            Ligne("Bombes de puissance", "$pb / $pbMax", pct(pb, pbMax), 0xFFFFD23F),
            Ligne("Rayon", RAYONS[rayon] ?: "–"), Ligne("Viseur", VISEURS.getOrElse(viseur) { "–" }), Ligne("Combinaison", COMBIS.getOrElse(combi) { "–" }))
        boss?.let { lignes.add(0, it) }
        if (fuite > 0f) lignes.add(0, Ligne("Évacuation", String.format(java.util.Locale.FRANCE, "%d:%02d", fuite.toInt() / 60, fuite.toInt() % 60), -1f, 0xFFFF3B5C))
        blocs.add(Bloc("Samus", lignes))
        blocs.add(Bloc("Artefacts Chozo", pastilles = art))
        if (ont.isNotEmpty()) blocs.add(Bloc("Équipement", pastilles = OBJETS.map { it.second to (it.second in ont) }))
        val m = ArrayList<Ligne>()
        val nArt = art.count { it.second }
        if (reservoirs < 14) m.add(Ligne("Réservoirs d'énergie", "$reservoirs / 14", pct(reservoirs, 14), 0xFFFFB300))
        if (missilesMax < 250) m.add(Ligne("Missiles", "$missilesMax / 250 : encore ${(250 - missilesMax) / 5} extensions", pct(missilesMax, 250), 0xFFFF6F3C))
        if (pbMax < 8) m.add(Ligne("Bombes de puissance", "$pbMax / 8", pct(pbMax, 8)))
        if (nArt < 12) m.add(Ligne("Artefacts", "$nArt / 12", pct(nArt, 12), 0xFF9C6CFF))
        if (scansMax > 0) m.add(Ligne("Journal (scans)", "$scans / $scansMax", pct(scans, scansMax), 0xFF4F8DFF))
        if (m.isNotEmpty()) blocs.add(Bloc("Pour finir à 100 %", m))
        // appli 573 : Samus vue de dessus (objet du joueur : transformation en + 0x34, ligne par ligne : x en + 0x40, y en + 0x50 ;
        // z est la hauteur dans ce jeu)
        l.ptr(0x803E28D4L + dd)?.let { o -> Traces.bloc("mp1:$monde:$salle", "Plan de la salle", l.fini(l.f32(o + 0x40), 1e5f), l.fini(l.f32(o + 0x50), 1e5f), "S", 0xFFFF8F3C) }?.let { blocs.add(it) }
        blocs.add(Bloc("Partie", listOfNotNull(
            Ligne("Région", monde.ifEmpty { "–" }), if (salle >= 0) Ligne("Salle", salle.toString()) else null,
            if (temps >= 0) Ligne("Temps de jeu", dureeS(temps.toLong())) else null)))
        return Fiche(id, monde.ifEmpty { "Metroid Prime" }, "Samus", 0xFF2A5C7A, 0xFF07131C,
            grands = listOf("énergie" to vie.toInt().toString(), "missiles" to missiles.toString(), "artefacts" to "$nArt/12"), blocs = blocs)
    }
}

// ---------------------------------------------------------------- Metroid Prime 2 : Echoes
object JeuMP2 : ModuleJeu("Metroid Prime 2 : Echoes", "G2MP01", "G2ME01") {
    private fun d(id: String) = if (id == "G2MP01") 0L else -0x1220L         // (americain : 0x1220 plus bas)
    private val MONDES = mapOf(0x3BFA3EFFL to "Temple Grounds", 0x42B935E4L to "Agon Wastes", 0x3DFD2249L to "Torvus Bog",
        0x1BAA96C2L to "Sanctuary Fortress", 0x863FCD72L to "Grand Temple")
    private val MONDES_NOIRS = mapOf(0x3BFA3EFFL to "Sky Temple Grounds", 0x42B935E4L to "Dark Agon Wastes", 0x3DFD2249L to "Dark Torvus Bog",
        0x1BAA96C2L to "Ing Hive", 0x863FCD72L to "Sky Temple")
    private val RAYONS = arrayOf("Rayon de puissance", "Rayon des ténèbres", "Rayon de lumière", "Rayon annihilateur")
    private val VISEURS = arrayOf("Combat", "Écho", "Scanner", "Ténèbres")
    private val COMBIS = arrayOf("Varia", "Combinaison des ténèbres", "Combinaison de lumière")
    private val OBJETS = listOf(0x6C to "Rayon des ténèbres", 0x78 to "Rayon de lumière", 0x84 to "Rayon annihilateur", 0x90 to "Super missile",
        0x9C to "Darkburst", 0xA8 to "Sunburst", 0xB4 to "Sonic Boom", 0xD8 to "Viseur des ténèbres", 0xE4 to "Viseur écho",
        0xFC to "Combinaison des ténèbres", 0x108 to "Combinaison de lumière", 0x114 to "Boule morphing", 0x120 to "Accélérateur",
        0x12C to "Boule araignée", 0x138 to "Bombes", 0x168 to "Rayon charge", 0x174 to "Rayon grappin", 0x180 to "Bottes de saut spatial",
        0x18C to "Gravity Boost", 0x198 to "Lance-missiles chercheur", 0x1A4 to "Vrille", 0x4EC to "Traducteur violet", 0x4F8 to "Traducteur ambre",
        0x504 to "Traducteur émeraude", 0x510 to "Traducteur cobalt", 0x564 to "Module de transfert d'énergie")
    private val CLES_CIEL = intArrayOf(0x1BC, 0x1C8, 0x1D4, 0x51C, 0x528, 0x534, 0x540, 0x54C, 0x558)
    private val CLES = listOf("Agon" to intArrayOf(0x1E0, 0x1EC, 0x1F8), "Torvus" to intArrayOf(0x204, 0x210, 0x21C), "Ruche Ing" to intArrayOf(0x228, 0x234, 0x240))

    override fun lire(l: Lecteur, id: String): Fiche? {
        val dd = d(id)
        val p = l.ptr(0x803DDF5CL + dd) ?: return Fiche(id, "Metroid Prime 2", "Pas de partie", 0xFF3B2A6E, 0xFF0B0718, message = "Écran titre ou chargement.")
        val vie = l.fini(l.f32(p + 0x14), 10000f) ?: return null
        fun s(o: Int) = l.s32(p + o) ?: 0
        val reservoirs = s(0x258); val missiles = s(0x26C); val missilesMax = s(0x270)
        if (vie < 0f || reservoirs !in 0..14 || missilesMax !in 0..255) return null
        val pb = s(0x260); val pbMax = s(0x264)
        val noir = s(0x278); val noirMax = s(0x27C); val lum = s(0x284); val lumMax = s(0x288)
        val pctObj = s(0x290).coerceIn(0, 100)
        val ont = OBJETS.filter { s(it.first) > 0 }.map { it.second }
        val ciel = CLES_CIEL.count { s(it) > 0 }
        val scans = s(0x578); val scansMax = s(0x57C)
        var monde = 0L
        l.ptr(0x803DDF04L + dd)?.let { w -> monde = l.u32(w + 8) ?: 0L }
        val sombre = ((l.u8(0x803DF24CL + dd) ?: 0) and 1) != 0
        val nomMonde = (if (sombre) MONDES_NOIRS[monde] else MONDES[monde]) ?: ""
        val salle = l.s32(0x803DDFA0L + dd)?.takeIf { it in 0..200 } ?: -1
        var boss: Ligne? = null
        val bossMax = l.fini(l.f32(0x803DED44L + dd), 1e6f) ?: 0f
        val uid = l.u16(0x803DED40L + dd) ?: 0xFFFF
        if (bossMax > 0f && uid != 0xFFFF) l.ptr(0x803DD110L + dd)?.let { t -> l.ptr(t + (uid and 0x3FF) * 8L + 4)?.let { b ->
            l.fini(l.f32(b + 0x2D4), 1e6f)?.takeIf { it in 0f..bossMax * 1.01f }?.let { v -> boss = Ligne("Boss", "${v.toInt()} / ${bossMax.toInt()}", v / bossMax, 0xFFFF5A3C) } } }
        val blocs = ArrayList<Bloc>()
        val lignes = arrayListOf(
            Ligne("Énergie", vie.toInt().toString() + " (réservoirs " + ((vie.toInt() - 1) / 100).coerceIn(0, reservoirs) + " / " + reservoirs + ")", vie / (99f + 100f * reservoirs), 0xFFFFB300),
            Ligne("Missiles", "$missiles / $missilesMax", pct(missiles, missilesMax), 0xFFFF6F3C),
            Ligne("Munitions des ténèbres", "$noir / $noirMax", pct(noir, noirMax), 0xFF9C6CFF),
            Ligne("Munitions de lumière", "$lum / $lumMax", pct(lum, lumMax), 0xFFFFF59D),
            Ligne("Bombes de puissance", "$pb / $pbMax", pct(pb, pbMax), 0xFFFFD23F),
            Ligne("Rayon", RAYONS.getOrElse(s(0xC)) { "–" }), Ligne("Viseur", VISEURS.getOrElse(s(0x30)) { "–" }), Ligne("Combinaison", COMBIS.getOrElse(s(0x54)) { "–" }))
        boss?.let { lignes.add(0, it) }
        blocs.add(Bloc("Samus", lignes))
        blocs.add(Bloc("Clés", listOf(Ligne("Clés du Temple céleste", "$ciel / 9", pct(ciel, 9), 0xFF4FC3F7)) +
            CLES.map { (n, t) -> val k = t.count { s(it) > 0 }; Ligne("Clés de $n", "$k / 3", pct(k, 3)) }))
        if (ont.isNotEmpty()) blocs.add(Bloc("Équipement", pastilles = OBJETS.map { it.second to (it.second in ont) }))
        val m = ArrayList<Ligne>()
        m.add(Ligne("Objets", "$pctObj %", pct(pctObj, 100), 0xFF3DDC84))
        if (reservoirs < 14) m.add(Ligne("Réservoirs d'énergie", "$reservoirs / 14", pct(reservoirs, 14)))
        if (scansMax > 0) m.add(Ligne("Journal (scans)", "$scans / $scansMax", pct(scans, scansMax), 0xFF4F8DFF))
        blocs.add(Bloc("Pour finir à 100 %", m))
        l.ptr(0x803DDDFCL + dd)?.let { o -> Traces.bloc("mp2:$monde:$sombre:$salle", "Plan de la salle", l.fini(l.f32(o + 0x54), 1e5f), l.fini(l.f32(o + 0x58), 1e5f), "S", 0xFFFF8F3C) }?.let { blocs.add(it) }   // (appli 573 : z = hauteur)
        blocs.add(Bloc("Partie", listOfNotNull(Ligne("Région", nomMonde.ifEmpty { "–" }), Ligne("Monde", if (sombre) "Aether des ténèbres" else "Aether"),
            if (salle >= 0) Ligne("Salle", salle.toString()) else null)))
        return Fiche(id, nomMonde.ifEmpty { "Metroid Prime 2" }, if (sombre) "Aether des ténèbres" else "Samus", 0xFF3B2A6E, 0xFF0B0718,
            grands = listOf("énergie" to vie.toInt().toString(), "missiles" to missiles.toString(), "objets" to "$pctObj %"), blocs = blocs)
    }
}

// ---------------------------------------------------------------- Sonic Adventure 2 Battle (version americaine)
object JeuSA2B : ModuleJeu("Sonic Adventure 2 Battle", "GSNE8P") {
    private val NIVEAUX = listOf(0x1F2D6C to "City Escape", 0x1F2FB8 to "Wild Canyon", 0x1F2A5C to "Prison Lane", 0x1F2B20 to "Metal Harbor",
        0x1F25C4 to "Green Forest", 0x1F274C to "Pumpkin Hill", 0x1F307C to "Mission Street", 0x1F28D4 to "Aquatic Mine", 0x1F3514 to "Hidden Base",
        0x1F38E8 to "Pyramid Cave", 0x1F369C to "Death Chamber", 0x1F35D8 to "Eternal Engine", 0x1F3BF8 to "Meteor Herd", 0x1F3450 to "Crazy Gadget",
        0x1F3A70 to "Final Rush", 0x1F2BE4 to "Iron Gate", 0x1F3140 to "Dry Lagoon", 0x1F338C to "Sand Ocean", 0x1F2E30 to "Radical Highway",
        0x1F3760 to "Egg Quarters", 0x1F3824 to "Lost Colony", 0x1F2CA8 to "Weapons Bed", 0x1F2998 to "Security Hall", 0x1F2688 to "White Jungle",
        0x1F2810 to "Sky Rail", 0x1F4528 to "Mad Space", 0x1F4464 to "Cosmic Wall", 0x1F4218 to "Final Chase", 0x1F3D80 to "Cannon's Core")
    private val AMELIORATIONS = listOf(0xB4 to "Light Shoes", 0xB5 to "Ancient Light (Sonic)", 0xB6 to "Magic Gloves", 0xB7 to "Flame Ring (Sonic)",
        0xB8 to "Bounce Bracelet", 0xB9 to "Mystic Melody (Sonic)", 0xBA to "Booster", 0xBB to "Bazooka", 0xBC to "Laser Blaster (Tails)",
        0xBD to "Mystic Melody (Tails)", 0xBE to "Shovel Claws", 0xBF to "Sunglasses", 0xC0 to "Hammer Gloves", 0xC1 to "Air Necklace",
        0xC2 to "Mystic Melody (Knuckles)", 0xC4 to "Air Shoes", 0xC5 to "Ancient Light (Shadow)", 0xC6 to "Flame Ring (Shadow)",
        0xC7 to "Mystic Melody (Shadow)", 0xC8 to "Jet Engine", 0xC9 to "Large Cannon", 0xCA to "Laser Blaster (Eggman)", 0xCB to "Protective Armor",
        0xCC to "Mystic Melody (Eggman)", 0xCD to "Pick Nails", 0xCE to "Treasure Scope", 0xCF to "Iron Boots", 0xD0 to "Mystic Melody (Rouge)")
    private val MISSIONS = arrayOf("Histoire", "100 anneaux", "Chao caché", "Contre la montre", "Difficile")
    private val MODES = mapOf(0 to "Choix du niveau", 1 to "Histoire des héros", 2 to "Histoire sombre", 3 to "Dernière histoire",
        0xC to "Boss (héros)", 0xD to "Boss (sombre)", 0xE to "Tous les boss")
    private val RANGS = arrayOf("–", "E", "D", "C", "B", "A")

    override fun lire(l: Lecteur, id: String): Fiche? {
        val vies = l.s16(0x801CC1CCL) ?: return null
        val anneaux = l.s16(0x801CC1D0L) ?: return null
        val emblemes = l.u8(0x801F234DL) ?: return null
        if (vies !in 0..99 || anneaux !in 0..999 || emblemes > 180) return null
        val score = l.u32(0x801CC1F8L)?.takeIf { it <= 99_999_999L } ?: 0L
        val mn = l.u8(0x801CC183L) ?: 0; val sec = l.u8(0x801CC184L) ?: 0; val cs = l.u8(0x801CC185L) ?: 0
        val mission = l.u8(0x801CC18BL) ?: 0; val mode = l.u8(0x801E50D4L) ?: 0
        val heros = (l.u8(0x801F234AL) ?: 0).coerceAtMost(0x2D); val sombre = (l.u8(0x801F234BL) ?: 0).coerceAtMost(0x2B); val derniere = (l.u8(0x801F234CL) ?: 0).coerceAtMost(7)
        val total = l.u32(0x801CC204L)?.takeIf { it < 10_000_000L } ?: -1L
        val ameliorations = AMELIORATIONS.map { (o, n) -> n to ((l.u8(0x801E5000L + o) ?: 0) != 0) }
        var rival: Ligne? = null
        l.ptr(0x801E772CL)?.let { r -> l.fini(l.f32(r + 0x48), 1e5f)?.takeIf { it > 0f }?.let { rival = Ligne("Vie du rival / boss", it.toInt().toString(), -1f, 0xFFFF5A3C) } }
        var meca: Ligne? = null
        l.ptr(0x801E7768L)?.let { p -> l.fini(l.f32(p + 0x168), 1e4f)?.takeIf { it in 0.5f..1000f }?.let { meca = Ligne("Vie du robot", it.toInt().toString(), -1f, 0xFF3DDC84) } }
        val rangs = NIVEAUX.map { (a, n) -> n to (0 until 5).map { (l.u8(0x80000000L + a + it) ?: 0).coerceIn(0, 5) } }
        val nA = rangs.sumOf { r -> r.second.count { it == 5 } }; val faits = rangs.sumOf { r -> r.second.count { it > 0 } }
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Niveau en cours", listOfNotNull(
            Ligne("Anneaux", anneaux.toString(), -1f, 0xFFFFD23F), Ligne("Vies", vies.toString()), Ligne("Score", score.toString()),
            Ligne("Temps", String.format(java.util.Locale.FRANCE, "%d:%02d.%02d", mn, sec, cs)), Ligne("Mission", MISSIONS.getOrElse(mission) { "–" }),
            rival, meca)))
        blocs.add(Bloc("Histoire", listOf(
            Ligne("Héros", if (heros >= 0x2D) "terminée" else "$heros / 45", pct(heros, 0x2D), 0xFF4F8DFF),
            Ligne("Sombre", if (sombre >= 0x2B) "terminée" else "$sombre / 43", pct(sombre, 0x2B), 0xFFFF3B5C),
            Ligne("Dernière histoire", if (derniere >= 7) "terminée" else "$derniere / 7", pct(derniere, 7), 0xFFFFD23F),
            Ligne("Mode", MODES[mode] ?: "–"))))
        blocs.add(Bloc("Collection", listOfNotNull(
            Ligne("Emblèmes", "$emblemes / 180", pct(emblemes, 180), 0xFFFFD23F),
            Ligne("Rangs A", "$nA / ${rangs.size * 5}", pct(nA, rangs.size * 5), 0xFF3DDC84),
            Ligne("Missions réussies", "$faits / ${rangs.size * 5}", pct(faits, rangs.size * 5)),
            if (total >= 0) Ligne("Anneaux ramassés en tout", total.toString()) else null)))
        l.ptr(0x801E7768L)?.let { p -> Traces.bloc("sa2b", "Plan du niveau", l.fini(l.f32(p + 0x14), 1e6f), l.fini(l.f32(p + 0x1C), 1e6f), "S", 0xFF4F8DFF, saut = 4000f) }?.let { blocs.add(it) }   // (appli 573)
        blocs.add(Bloc("Améliorations", pastilles = ameliorations))
        blocs.add(Bloc("Rangs par niveau", rangs.map { (n, r) -> Ligne(n, r.joinToString(" ") { RANGS[it] }) },
            texte = "Les 5 missions de chaque niveau (– : pas encore réussie)."))
        return Fiche(id, "Sonic Adventure 2 Battle", MODES[mode] ?: "", 0xFF1E4FA8, 0xFF060F24,
            grands = listOf("anneaux" to anneaux.toString(), "vies" to vies.toString(), "emblèmes" to emblemes.toString()), blocs = blocs)
    }
}

// ---------------------------------------------------------------- Sonic Adventure DX (version americaine)
object JeuSADX : ModuleJeu("Sonic Adventure DX", "GXSE8P") {
    private val PERSOS = mapOf(0 to "Sonic", 2 to "Tails", 3 to "Knuckles", 5 to "Amy", 6 to "E-102 Gamma", 7 to "Big")
    private val CARTES = mapOf(0x00 to "Hedgehog Hammer", 0x01 to "Emerald Coast", 0x02 to "Windy Valley", 0x03 to "Twinkle Park", 0x04 to "Speed Highway",
        0x05 to "Red Mountain", 0x06 to "Sky Deck", 0x07 to "Lost World", 0x08 to "Icecap", 0x09 to "Casinopolis", 0x0A to "Final Egg", 0x0C to "Hot Shelter",
        0x0F to "Chaos 0", 0x10 to "Chaos 2", 0x11 to "Chaos 4", 0x12 to "Chaos 6", 0x13 to "Perfect Chaos", 0x14 to "Egg Hornet", 0x15 to "Egg Walker",
        0x16 to "Egg Viper", 0x17 to "Zero", 0x18 to "E-101 Beta", 0x19 to "E-101 mkII", 0x1A to "Station Square", 0x1D to "Egg Carrier (dehors)",
        0x20 to "Egg Carrier (dedans)", 0x21 to "Mystic Ruins", 0x22 to "Le passé", 0x23 to "Twinkle Circuit", 0x24 to "Sky Chase 1", 0x25 to "Sky Chase 2",
        0x26 to "Sand Hill", 0x27 to "Station Square", 0x28 to "Egg Carrier", 0x29 to "Mystic Ruins")
    private val DEBLOQUES = arrayOf("", "Sonic", "Tails", "Knuckles", "Amy", "Gamma", "Big", "Super Sonic")

    override fun lire(l: Lecteur, id: String): Fiche? {
        val anneaux = l.s16(0x8074C7A8L) ?: return null
        val vies = l.u8(0x8074C7ADL) ?: return null
        if (anneaux !in 0..999 || vies > 99) return null
        val perso = l.u8(0x8074A7ADL) ?: 0; val carte = l.u8(0x8074A7C7L) ?: 0xFF
        val score = l.u32(0x8074C79CL)?.takeIf { it <= 99_999_999L } ?: 0L
        val mn = l.u8(0x8074C7ACL) ?: 0; val sec = l.u8(0x8074C7ABL) ?: 0; val ms = l.u8(0x8074C7AAL) ?: 0
        val fichier = (l.u8(0x8079BD15L) ?: 0).coerceIn(0, 2)
        val base = 0x807A862CL + 0x570L * fichier
        var emblemes = 0
        for (k in 0 until 17) emblemes += Integer.bitCount((l.u8(base + k) ?: 0) and if (k == 16) 0x03 else 0xFF)
        val persos = l.u8(0x807A8654L + 0x570L * fichier) ?: 0
        var missions = 0
        for (k in 0 until 60) if ((l.u8(0x807A83ACL + k) ?: 0) and 0x80 != 0) missions++
        var boss: Ligne? = null
        val bMax = l.fini(l.f32(0x808429CCL), 1e5f) ?: 0f
        if (bMax > 0f) l.fini(l.f32(0x808429D4L), 1e5f)?.takeIf { it in 0f..bMax }?.let { boss = Ligne("Boss", "${it.toInt()} / ${bMax.toInt()}", it / bMax, 0xFFFF5A3C) }
        val nomCarte = CARTES[carte] ?: ""
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("En cours", listOfNotNull(boss,
            Ligne("Anneaux", anneaux.toString(), -1f, 0xFFFFD23F), Ligne("Vies", vies.toString()), Ligne("Score", score.toString()),
            Ligne("Temps", String.format(java.util.Locale.FRANCE, "%d:%02d.%02d", mn, sec, ms)),
            Ligne("Personnage", PERSOS[perso] ?: "–"), Ligne("Lieu", nomCarte.ifEmpty { "–" }))))
        val sous = l.u8(0x8074A7C5L) ?: 0
        l.u32(0x806B4450L)?.let { (it and 0x1FFFFFFFL) or 0x80000000L }?.takeIf { it in 0x80003100L..0x817FFFFFL }?.let { c ->
            Traces.bloc("sadx:$carte:$sous", "Plan du lieu", l.fini(l.f32(c + 0x20), 1e6f), l.fini(l.f32(c + 0x28), 1e6f), (PERSOS[perso] ?: "S").take(1), 0xFF4F8DFF, saut = 4000f) }?.let { blocs.add(it) }   // (appli 573)
        blocs.add(Bloc("Collection (fichier ${fichier + 1})", listOf(
            Ligne("Emblèmes", "$emblemes / 130", pct(emblemes, 130), 0xFFFFD23F),
            Ligne("Missions réussies", "$missions / 60", pct(missions, 60), 0xFF3DDC84))))
        blocs.add(Bloc("Personnages débloqués", pastilles = (1..7).map { DEBLOQUES[it] to (persos shr it and 1 != 0) }))
        return Fiche(id, nomCarte.ifEmpty { "Sonic Adventure DX" }, PERSOS[perso] ?: "", 0xFF1E6FC8, 0xFF061428,
            grands = listOf("anneaux" to anneaux.toString(), "vies" to vies.toString(), "emblèmes" to "$emblemes"), blocs = blocs)
    }
}

// ---------------------------------------------------------------- Shrek Extra Large (version americaine)
object JeuShrek : ModuleJeu("Shrek Extra Large", "GS9E6S") {
    private class Niveau(val nom: String, val ouvert: Long, val taches: List<Pair<String, Long>>)
    private val NIVEAUX = listOf(
        Niveau("Enchanted Forest", 0, listOf("Find Red's basket" to 0x8045B61CL, "Get the Kids across" to 0x8045B620L, "Blow up the door" to 0x8045B624L,
            "Collect the Goodies" to 0x8045B628L, "Capture the Evil Fairies" to 0x8045B62CL, "Find the hidden Easter Eggs" to 0x8045B630L)),
        Niveau("Mother Goose Land", 0x8045B633L, listOf("Toss the eggs into the nest" to 0x8045B635L, "Wake up the sleeping guards" to 0x8045B639L,
            "Get the cow to Old McDonald" to 0x8045B63DL, "Corral Bo Peep's sheep" to 0x8045B641L, "Capture the Evil Fairies" to 0x8045B645L, "Find the hidden Easter Eggs" to 0x8045B649L)),
        Niveau("Sweetsville Industrial Park", 0x8045B64CL, listOf("Flush the Slingshot Vandals" to 0x8045B64EL, "Shut Off the Factory Whistles" to 0x8045B652L,
            "Repair the Machine" to 0x8045B656L, "Defeat FrankenDrop" to 0x8045B65AL, "Capture the Evil Fairies" to 0x8045B65EL, "Find the hidden Easter Eggs" to 0x8045B662L)),
        Niveau("Molasses Sewers", 0x8045B665L, listOf("Defeat the ice-cream guards" to 0x8045B667L, "Escort the engineers" to 0x8045B66BL,
            "Open the safety valves" to 0x8045B66FL, "The Ice Queen" to 0x8045B673L, "Capture the Evil Fairies" to 0x8045B677L, "Find the hidden Easter Eggs" to 0x8045B67BL)),
        Niveau("Lonely Mountain", 0x8045B67EL, listOf("Help the Leprechaun" to 0x8045B680L, "Release the Wagon" to 0x8045B684L, "Get the Pot of Gold" to 0x8045B688L,
            "Knock out the Gnomes" to 0x8045B68CL, "Capture the Evil Fairies" to 0x8045B690L, "Find the hidden Easter Eggs" to 0x8045B694L)),
        Niveau("Creepy Crypt", 0x8045B697L, listOf("Get rid of the Skeletons" to 0x8045B699L, "Punch the witches" to 0x8045B69DL, "Fetch the Spider Kids" to 0x8045B6A1L,
            "Collect the Armor" to 0x8045B6A5L, "Capture the Evil Fairies" to 0x8045B6A9L, "Find the hidden Easter Eggs" to 0x8045B6ADL)),
        Niveau("Crown Prince's Castle", 0x8045B6B0L, listOf("Bring the Crown Prince upstairs" to 0x8045B6B2L, "Lock up the Keeper" to 0x8045B6B6L,
            "Free the Prisoners" to 0x8045B6BAL, "Entertain the Crown Prince" to 0x8045B6BEL, "Capture the Evil Fairies" to 0x8045B6C2L, "Find the hidden Easter Eggs" to 0x8045B6C6L)),
        Niveau("Blue Dragon Castle", 0x8045B6C9L, listOf("Save the Fishes" to 0x8045B6CBL, "Water the Baby Dragons" to 0x8045B6CFL, "Lure the Dragon with Fire" to 0x8045B6D3L,
            "Stop the Noise" to 0x8045B6D7L, "Capture the Evil Fairies" to 0x8045B6DBL, "Find the hidden Easter Eggs" to 0x8045B6DFL)),
        Niveau("Merlin's Dark Tower", 0x8045B6E2L, listOf("Defeat Merlin" to 0x8045B6E3L)))
    private val LIEUX = arrayOf("Enchanted Forest", "Mother Goose Land", "Sweetsville Industrial Park", "Molasses Sewers", "Lonely Mountain",
        "Creepy Crypt", "Crown Prince's Castle", "Blue Dragon Castle", "Shrek's Swamp", "Carte", "Merlin's Dark Tower", "Démo", "GameCube")
    private val TRUCS = arrayOf("Invulnérable", "Gaz infini", "Flamme infinie", "Super force", "Super flatulence", "Faible gravité", "\$ pour Merlin")

    override fun lire(l: Lecteur, id: String): Fiche? {
        val lieu = l.u32(0x803C8F80L) ?: return null
        if (lieu > 12) return null
        val argent = l.u32(0x8045B6ECL)?.takeIf { it <= 9_999_999L } ?: return null
        var vie = -1f; var gaz = -1f; var piment = -1f
        l.ptr(0x803D745CL)?.let { p ->
            vie = l.fini(l.f32(p + 0x1C0), 1000f) ?: -1f; gaz = l.fini(l.f32(p + 0x1B4), 1000f) ?: -1f; piment = l.fini(l.f32(p + 0x1BC), 1000f) ?: -1f
        }
        var oeufs: Ligne? = null
        l.ptr(0x8079564CL)?.let { p -> val n = l.s32(p + 0x78); val m = l.s32(p + 0x7C)
            if (n != null && m != null && m in 1..99 && n in 0..m) oeufs = Ligne("Œufs dorés", "$n / $m", pct(n, m), 0xFFFFD23F) }
        val faits = NIVEAUX.map { nv -> nv.taches.map { (n, a) -> n to ((l.u8(a) ?: 0) == 1) } }
        val total = faits.sumOf { it.size }; val ok = faits.sumOf { t -> t.count { it.second } }
        val trucs = TRUCS.indices.map { TRUCS[it] to ((l.u8(0x8045B6F0L + it) ?: 0) == 1) }
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Shrek", listOfNotNull(
            if (vie >= 0f) Ligne("Vie", vie.toInt().toString(), (vie / 100f).coerceIn(0f, 1f), 0xFF3DDC84) else null,
            if (gaz >= 0f) Ligne("Jauge de pets", gaz.toInt().toString(), (gaz / 100f).coerceIn(0f, 1f), 0xFF9CCC65) else null,
            if (piment >= 0f) Ligne("Piment", piment.toInt().toString(), (piment / 100f).coerceIn(0f, 1f), 0xFFFF5A3C) else null,
            Ligne("Argent", argent.toString(), -1f, 0xFFFFD23F), oeufs)))
        l.ptr(0x803D745CL)?.let { p -> Traces.bloc("shrek:$lieu", "Plan du niveau", l.fini(l.f32(p + 0x278), 1e6f), l.fini(l.f32(p + 0x280), 1e6f), "S", 0xFF9CCC65) }?.let { blocs.add(it) }   // (appli 573)
        blocs.add(Bloc("Missions", listOf(Ligne("Missions réussies", "$ok / $total", pct(ok, total), 0xFF3DDC84)) +
            NIVEAUX.indices.map { k -> val nv = NIVEAUX[k]; val f = faits[k]
                val ouvert = nv.ouvert == 0L || (l.u8(nv.ouvert) ?: 0) == 1
                Ligne(nv.nom, if (!ouvert) "fermé" else "${f.count { it.second }} / ${f.size}", if (ouvert) pct(f.count { it.second }, f.size) else -1f, 0xFF9CCC65) }))
        val ici = NIVEAUX.indexOfFirst { it.nom.startsWith(LIEUX[lieu.toInt()].take(12)) }
        if (ici >= 0) blocs.add(Bloc("Missions de ce niveau", pastilles = faits[ici]))
        blocs.add(Bloc("Codes débloqués", pastilles = trucs))
        return Fiche(id, LIEUX[lieu.toInt()], "Shrek", 0xFF4E7A1E, 0xFF101A06,
            grands = listOf("argent" to argent.toString(), "missions" to "$ok/$total"), blocs = blocs)
    }
}
