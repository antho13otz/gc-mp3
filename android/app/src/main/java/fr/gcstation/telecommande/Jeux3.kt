package fr.gcstation.telecommande

// Appli build 173 : d'autres jeux pour le compagnon (meme fiche que Jeux2.kt). Seulement des adresses et des tailles
// de structures, chaque valeur bornee ; une version inconnue n'affiche rien.

// ---------------------------------------------------------------- Mario Party 5
// mariopartyrd/marioparty5 (GP5E01) : GwPlayer (4 x 0x108 : personnage, cases, pieces, etoiles, statistiques),
// GwSystem (0x2BC : tour, nombre de tours, plateau, mini-jeu, banque) juste apres. Europeen (GP5P01) : le main.dol
// construit 8024 8DB0 744 fois, 8024 91D0 (+0x420) 545 fois et 8024 9490 (+0x6E0) 107 fois : les memes trois blocs.
object JeuMP5 : ModuleJeu("Mario Party 5", "GP5P01", "GP5E01") {
    private fun joueurs(id: String) = if (id == "GP5P01") 0x80248DB0L else 0x8022A070L
    private val PERSOS = arrayOf("Mario", "Luigi", "Peach", "Yoshi", "Wario", "Daisy", "Waluigi", "Toad", "Boo", "Mini Bowser",
        "Mini Bowser rouge", "Mini Bowser vert", "Mini Bowser bleu")
    private val PLATEAUX = arrayOf("Rêve des jouets", "Rêve arc-en-ciel", "Rêve des pirates", "Rêve sous-marin", "Rêve du futur",
        "Rêve sucré", "Cauchemar de Bowser", "Tutoriel", "Circuit")
    private val COUL = longArrayOf(0xFF4F8DFF, 0xFFFF4D4D, 0xFFFFD23F, 0xFF3DDC84)

    override fun lire(l: Lecteur, id: String): Fiche? {
        val gp = joueurs(id); val sys = gp + 0x420
        val tour = l.u8(sys + 4) ?: return null; val tours = l.u8(sys + 5) ?: return null
        val plateau = (l.u8(sys + 8) ?: return null) and 31
        val partie = tours in 10..50 && tour in 1..tours + 1 && plateau in 0..8
        val joueur = l.u8(sys + 0xA)?.toByte()?.toInt() ?: -1
        val mg = l.u16(sys + 0x34) ?: 0xFFFF
        val banque = (l.u16(sys + 0x38) ?: 0).coerceAtMost(9999)
        val equipes = ((l.u8(sys) ?: 0) and 0x40) != 0
        val blocs = ArrayList<Bloc>(); val grands = ArrayList<Pair<String, String>>()
        class J(val n: Int, val nom: String, val pieces: Int, val etoiles: Int, val rang: Int)
        val js = ArrayList<J>()
        for (k in 0 until 4) {
            val b = gp + 0x108L * k
            val w0 = l.u16(b) ?: return null
            val perso = (w0 shr 9) and 15
            val ordi = (w0 shr 13) and 1 == 1
            val niveau = (w0 shr 14) and 3
            val pieces = l.s16(b + 0x22) ?: return null; val etoiles = l.s16(b + 0x34) ?: return null
            if (perso >= PERSOS.size || pieces !in 0..999 || etoiles !in 0..99) return if (partie) null else
                Fiche(id, "Mario Party 5", "Menus", 0xFFB82E2E, 0xFF2A0A0A, message = "Pas de partie en cours.")
            val rang = ((l.u16(b + 0xA) ?: 0) shr 5) and 3
            val max = (l.s16(b + 0x28) ?: 0).coerceIn(0, 999); val maxEt = (l.s16(b + 0x36) ?: 0).coerceIn(0, 99)
            val totalMg = (l.s16(b + 0x24) ?: 0).coerceIn(0, 9999); val total = (l.s16(b + 0x26) ?: 0).coerceIn(0, 9999)
            val combat = (l.s16(b + 0x2A) ?: 0).coerceIn(0, 9999)
            val capsules = (l.plage(b + 6, 3) ?: IntArray(3)).count { it != 0xFF && it != 0 }
            val cases = l.plage(b + 0x16, 11) ?: IntArray(11)
            fun c(i: Int) = cases[i].toByte().toInt().coerceIn(0, 127)
            val nom = PERSOS[perso]
            js.add(J(k, nom, pieces, etoiles, rang))
            blocs.add(Bloc("J" + (k + 1) + " · " + nom + if (ordi) " (ordi " + arrayOf("facile", "normal", "difficile", "très difficile")[niveau] + ")" else "", listOf(
                Ligne("Étoiles", etoiles.toString(), (etoiles / 10f).coerceIn(0f, 1f), 0xFFFFD23F),
                Ligne("Pièces", pieces.toString(), (pieces / 200f).coerceIn(0f, 1f), COUL[k]),
                Ligne("Classement", (rang + 1).toString() + if (rang == 0) "er" else "e"),
                Ligne("Capsules", "$capsules / 3"),
                Ligne("Pièces gagnées (mini-jeux / en tout)", "$totalMg / $total"),
                Ligne("Pièces de combat", combat.toString()),
                Ligne("Record de pièces / d'étoiles", "$max / $maxEt"),
                Ligne("Cases", "bleues ${c(0)} · rouges ${c(1)} · capsules ${c(2)} · ? ${c(3)} · Bowser ${c(4)} · DK ${c(10)} · étoile ${c(9)}"))))
        }
        if (!partie) return Fiche(id, "Mario Party 5", "Menus ou mini-jeux libres", 0xFFB82E2E, 0xFF2A0A0A, message = "Pas de partie sur un plateau en ce moment.")
        for (j in js.sortedBy { it.rang }) grands.add(j.nom to "${j.etoiles}★ ${j.pieces}")
        val lignes = ArrayList<Ligne>()
        lignes.add(Ligne("Tour", "$tour / $tours", pct(tour, tours), 0xFFFF8A3D))
        if (joueur in 0..3) lignes.add(Ligne("À qui de jouer", js.getOrNull(joueur)?.nom ?: "–"))
        if (mg in 501..600) lignes.add(Ligne("Mini-jeu", "n° " + (mg - 500)))
        lignes.add(Ligne("Banque", "$banque pièces"))
        if (equipes) lignes.add(Ligne("Mode", "Par équipes"))
        if (tours - tour in 0..4 && tour <= tours) lignes.add(Ligne("Derniers tours", "Oui (les 5 derniers)"))
        return Fiche(id, PLATEAUX[plateau], "Tour $tour sur $tours", 0xFFB82E2E, 0xFF2A0A0A,
            grands = grands.take(4), blocs = listOf(Bloc("Partie", lignes)) + blocs)
    }
}
