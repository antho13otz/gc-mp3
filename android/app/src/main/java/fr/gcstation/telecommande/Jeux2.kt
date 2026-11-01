package fr.gcstation.telecommande

// Appli (v314) : ecran compagnon pour d'autres jeux, avec une « fiche » commune (sections de jauges, compteurs et
// listes) remplie par chaque jeu. Memes regles que Compagnon.kt : lecture en direct par Pulse (bloc de 1 Ko), au plus
// [Lecteur.budget] blocs par lecture, chaque valeur verifiee (bornes) ; une version du jeu inconnue n'affiche rien.
// Adresses : tables de symboles publiques des decompilations (seulement des adresses, aucun code repris) :
//  - zeldaret/tp (GZ2P01, GZ2E01) : g_dComIfG_gameInfo (dSv_info_c : mSavedata.mPlayer en 0 ; dSv_memory_c en 0x958 ;
//    dComIfG_play_c en 0xF38 : mStartStage 0x3EC8, mPlayerPtr 0x4E7C) ; fopAc_ac_c : current.pos 0x4D0, speedF 0x52C ;
//  - projectPiki/pikmin (GPIP01, GPIE01 rev 0 et 1) : gameflow (WorldClock en 0x2D8), playerState, GameStat, pikiInfMgr ;
//  - projectPiki/pikmin2 (GPVP01, GPVE01) : playData (PlayData), gameSystem (TimeMgr), GameStat, naviMgr (Navi.mHealth) ;
//  - doldecomp/melee (GALE01, 1.02, et ses mods comme Akaneia) : player_slots (StaticPlayer, 0xE90 par port) ;
//  - Mario Superstar Baseball (GYQE01) : adresses de Project Rio (ProjectRio/ProjectRio, MSB_StatTracker.h).

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** une ligne : nom, valeur ; part (0..1) : une jauge ; couleur de la valeur */
class Ligne(val nom: String, val valeur: String, val part: Float = -1f, val couleur: Long = 0)
/** une section : lignes, et/ou pastilles (texte, obtenu) */
class Bloc(val titre: String, val lignes: List<Ligne> = emptyList(), val pastilles: List<Pair<String, Boolean>> = emptyList(), val texte: String = "",
           val plan: Plan? = null)                                         // (build 175 : un plan vu de dessus)
/** build 175 : un plan vu de dessus (x, z du jeu) : des cases reliees, et des pions (texte court) */
class Plan(val cases: List<FloatArray>, val couleurs: List<Long>, val liens: List<IntArray>, val pions: List<Pion>, val etoiles: List<Int> = emptyList())
class Pion(val x: Float, val z: Float, val couleur: Long, val texte: String)
/** l'ecran d'un jeu : grands chiffres en haut, puis les sections */
class Fiche(val jeu: String, val titre: String, val sous: String, val haut: Long, val bas: Long,
            val grands: List<Pair<String, String>> = emptyList(), val blocs: List<Bloc> = emptyList(), val message: String = "",
            val bouton: String = "")                                       // (v314 : un bouton d'action du jeu, ModuleJeu.agir)

abstract class ModuleJeu(val nom: String, vararg val ids: String) {
    abstract fun lire(l: Lecteur, id: String): Fiche?
    /** le bouton de la fiche (Fiche.bouton) : par ex. la recherche unique du jeu dans la memoire */
    open fun agir(c: android.content.Context, id: String) { }
}

object ModulesJeux {
    val tous: List<ModuleJeu> = listOf(JeuTP, JeuPikmin1, JeuPikmin2, JeuMelee, JeuMSB, JeuTTYD, JeuOoT, JeuMP5)
    fun pour(id: String): ModuleJeu? = tous.firstOrNull { id in it.ids }
    fun noms(): Map<String, String> = tous.flatMap { m -> m.ids.map { it to m.nom } }.toMap()
}

internal fun pct(a: Int, b: Int) = if (b > 0) (a.toFloat() / b).coerceIn(0f, 1f) else -1f
internal fun heureJeu(h: Float) = String.format(java.util.Locale.FRANCE, "%d h %02d", h.toInt() % 24, ((h - h.toInt()) * 60).toInt().coerceIn(0, 59))
internal fun dureeS(s: Long) = if (s >= 3600) String.format(java.util.Locale.FRANCE, "%d h %02d", s / 3600, s / 60 % 60) else String.format(java.util.Locale.FRANCE, "%d min %02d", s / 60, s % 60)

// ---------------------------------------------------------------- Twilight Princess
object JeuTP : ModuleJeu("Twilight Princess", "GZ2P01", "GZ2E01") {
    private fun base(id: String) = if (id == "GZ2P01") 0x80408160L else 0x804061C0L   // g_dComIfG_gameInfo

    private val LIEUX = mapOf(
        "F_SP103" to "Village de Toal", "F_SP104" to "Source de Toal", "F_SP108" to "Bois de Firone", "F_SP109" to "Village Cocorico",
        "F_SP110" to "Mont du Péril", "F_SP111" to "Cimetière de Cocorico", "F_SP112" to "Rivière Zora", "F_SP113" to "Domaine Zora",
        "F_SP114" to "Pic Blanc", "F_SP115" to "Lac Hylia", "F_SP116" to "Bourg d'Hyrule", "F_SP117" to "Bois Sacré",
        "F_SP118" to "Camp Bulblin", "F_SP121" to "Plaine d'Hyrule", "F_SP122" to "Abords du Bourg d'Hyrule", "F_SP124" to "Désert Gerudo",
        "F_SP126" to "Source de la Rivière Zora", "F_SP127" to "Étang de pêche", "F_SP128" to "Village caché",
        "D_MN05" to "Temple Sylvestre", "D_MN04" to "Mine des Gorons", "D_MN01" to "Temple Abyssal", "D_MN10" to "Tour du Jugement",
        "D_MN11" to "Manoir de Glace", "D_MN06" to "Temple du Temps", "D_MN07" to "Cité Céleste", "D_MN08" to "Palais du Crépuscule",
        "D_MN09" to "Château d'Hyrule")
    fun lieu(s: String): String {
        LIEUX[s]?.let { return it }
        LIEUX[s.take(6)]?.let { return it + if (s.length > 6) " (boss)" else "" }
        return when { s.startsWith("D_SB") -> "Grotte"; s.startsWith("R_SP") -> "Intérieur"; else -> "" }
    }
    private val OBJETS = mapOf(
        0x40 to "Aérouage", 0x41 to "Toupie", 0x42 to "Boulet", 0x43 to "Arc", 0x44 to "Grappin", 0x45 to "Bottes de fer",
        0x46 to "Bâton Anima", 0x47 to "Double grappin", 0x48 to "Lanterne", 0x4A to "Canne à pêche", 0x4B to "Lance-pierre",
        0x3E to "Œil de faucon", 0x4F to "Sac de bombes", 0x50 to "Sac de bombes", 0x84 to "Herbe-sifflet", 0x80 to "Lettre",
        0x82 to "Statuette", 0x83 to "Pendentif d'Iria", 0x60 to "Flacon vide", 0x61 to "Potion rouge", 0x62 to "Potion verte",
        0x63 to "Potion bleue", 0x64 to "Lait", 0x65 to "Lait (moitié)", 0x66 to "Huile", 0x67 to "Eau", 0x6C to "Fée",
        0x73 to "Larme de fée", 0x74 to "Ver", 0x76 to "Larve d'abeille", 0x6A to "Soupe", 0x6B to "Eau de source chaude",
        0x5B to "Canne (abeille)", 0x5D to "Canne (ver)", 0x58 to "Canne à leurre", 0x59 to "Flèches-bombes", 0x5A to "Flèches de faucon",
        0x29 to "Épée de légende", 0x28 to "Épée d'Ordon", 0x4C to "Bâton Anima")

    override fun lire(l: Lecteur, id: String): Fiche? {
        val g = base(id)
        val vieMax = l.u16(g) ?: return null
        val vie = l.u16(g + 2) ?: return null
        val play = g + 0xF38
        val scene = (l.texte(play + 0x3EC8, 8) ?: return null).takeIf { s -> s.all { it.code in 32..126 } } ?: return null
        if (vieMax == 0 || vieMax > 100 || vie > vieMax / 5 * 4 + 4) return Fiche(id, "Twilight Princess", "Pas de partie chargée", 0xFF2E4A1E, 0xFF0E1A0A,
            message = "Écran titre ou choix du fichier.")
        val rubis = l.u16(g + 4) ?: 0
        val huileMax = l.u16(g + 6) ?: 0; val huile = l.u16(g + 8) ?: 0
        val bourse = when (l.u8(g + 0x19)) { 1 -> 600; 2 -> 1000; else -> 300 }
        val loup = (l.u8(g + 0x1E) ?: 0) == 1
        val heure = l.fini(l.f32(g + 0x28 + 0x0C), 1000f)?.takeIf { it in 0f..360f }
        val objets = l.plage(g + 0x9C, 24) ?: IntArray(0)
        val fleches = l.u8(g + 0xEC) ?: 0
        val bombes = l.plage(g + 0xED, 3) ?: IntArray(3)
        val lancePierre = l.u8(g + 0xF4) ?: 0
        val ombres = l.u8(g + 0x109) ?: 0; val miroir = l.u8(g + 0x10A) ?: 0; val ames = l.u8(g + 0x10C) ?: 0
        val larmes = l.plage(g + 0x114, 4) ?: IntArray(4)
        var poissons = 0
        for (k in 0 until 16) poissons += (l.u16(g + 0x16C + 2L * k) ?: 0).coerceAtMost(9999)
        val tempsTicks = l.u32(g + 0x1A8 + 4)?.let { lo -> ((l.u32(g + 0x1A8) ?: 0L) shl 32) or lo } ?: 0L
        val morts = l.u16(g + 0x1B2) ?: 0
        val nom = (l.texte(g + 0x1B4, 16) ?: "").filter { it.code in 32..255 }
        val cheval = (l.texte(g + 0x1C5, 16) ?: "").filter { it.code in 32..255 }
        val cles = l.u8(g + 0x958 + 0x1C) ?: 0
        val dj = l.u8(g + 0x958 + 0x1D) ?: 0
        val salle = l.u8(play + 0x3EC8 + 0xA)?.toByte()?.toInt() ?: -1
        var vitesse = -1f; var alt = Float.NaN
        l.ptr(play + 0x4E7C)?.let { p -> vitesse = Math.abs(l.fini(l.f32(p + 0x52C), 1000f) ?: 0f); alt = l.fini(l.f32(p + 0x4D0 + 4)) ?: Float.NaN }
        val lieu = lieu(scene).ifEmpty { scene }
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Link", listOf(
            Ligne("Cœurs", String.format(java.util.Locale.FRANCE, "%.2f / %d", vie / 4f, vieMax / 5), pct(vie, vieMax / 5 * 4), 0xFFFF3B5C),
            Ligne("Huile de la lanterne", "$huile / $huileMax", pct(huile, huileMax), 0xFFFFB300),
            Ligne("Rubis", "$rubis / $bourse", pct(rubis, bourse), 0xFF3DDC84),
            Ligne("Forme", if (loup) "Loup" else "Hylien"),
            Ligne("Flèches", fleches.toString()), Ligne("Bombes", bombes.filter { it in 1..99 }.joinToString(" + ").ifEmpty { "0" }),
            Ligne("Graines du lance-pierre", lancePierre.toString()))))
        blocs.add(Bloc("Quête", listOf(
            Ligne("Âmes d'Esprits", "$ames / 60", pct(ames, 60), 0xFF9C6CFF),
            Ligne("Ombres fusionnées", Integer.bitCount(ombres and 7).toString() + " / 3", pct(Integer.bitCount(ombres and 7), 3)),
            Ligne("Éclats du Miroir des Ombres", Integer.bitCount(miroir and 15).toString() + " / 4", pct(Integer.bitCount(miroir and 15), 4)),
            Ligne("Larmes de lumière", larmes.joinToString(" · ") { it.coerceAtMost(16).toString() }),
            Ligne("Poissons pêchés", poissons.toString()))))
        val inv = objets.filter { it != 0xFF && it != 0 }.map { OBJETS[it] ?: ("Objet " + Integer.toHexString(it)) }.distinct()
        if (inv.isNotEmpty()) blocs.add(Bloc("Inventaire", pastilles = inv.map { it to true }))
        if (scene.startsWith("D_MN")) blocs.add(Bloc("Donjon", listOf(Ligne("Petites clés", cles.toString())),
            pastilles = listOf("Carte" to (dj and 1 != 0), "Boussole" to (dj and 2 != 0), "Clé du boss" to (dj and 4 != 0), "Boss vaincu" to (dj and 8 != 0))))
        blocs.add(Bloc("Partie", listOf(
            Ligne("Joueur", nom.ifEmpty { "Link" }), Ligne("Cheval", cheval.ifEmpty { "Epona" }),
            Ligne("Heure du jeu", if (heure != null) heureJeu(heure / 15f) else "–"),
            Ligne("Temps de jeu (dernière sauvegarde)", if (tempsTicks in 1..(1L shl 60)) dureeS(tempsTicks / 40_500_000L) else "–"),
            Ligne("K.O.", morts.toString()),
            Ligne("Salle", if (salle >= 0) salle.toString() else "–"),
            Ligne("Vitesse", if (vitesse >= 0) vitesse.toInt().toString() else "–"),
            Ligne("Altitude", if (!alt.isNaN()) alt.toInt().toString() else "–"))))
        return Fiche(id, lieu, if (loup) "Link (loup)" else "Link", 0xFF3E6B22, 0xFF0F1D08,
            grands = listOf("cœurs" to (vie / 4).toString(), "rubis" to rubis.toString(), "âmes" to ames.toString()), blocs = blocs)
    }
}

// ---------------------------------------------------------------- Pikmin
private val COULEURS_P1 = listOf("Bleus" to 0xFF4F8DFFL, "Rouges" to 0xFFFF4D4DL, "Jaunes" to 0xFFFFD23FL)
object JeuPikmin1 : ModuleJeu("Pikmin", "GPIP01", "GPIE01") {
    private class Adr(val flow: Long, val inf: Long, val dead: Long, val form: Long, val work: Long, val cont: Long, val map: Long, val ps: Long)
    private val P = Adr(0x803A2638, 0x803D6C70, 0x803D6CD8, 0x803D6CF0, 0x803D6D08, 0x803D6D20, 0x803D6D44, 0x803ECB4C)
    private val E0 = Adr(0x8039D718, 0x803D1D50, 0x803D1DB8, 0x803D1DD0, 0x803D1DE8, 0x803D1E00, 0x803D1E24, 0x803E7BEC)
    private val E1 = Adr(0x8039D7B8, 0x803D1DF0, 0x803D1E58, 0x803D1E70, 0x803D1E88, 0x803D1EA0, 0x803D1EC4, 0x803E7C8C)

    override fun lire(l: Lecteur, id: String): Fiche? {
        val a = if (id == "GPIP01") P else if ((l.u8(0x80000007) ?: 0) >= 1) E1 else E0      // (US : revision du disque)
        fun tri(adr: Long): IntArray? { val r = IntArray(3); for (k in 0 until 3) r[k] = l.s32(adr + 4L * k)?.takeIf { it in 0..9999 } ?: return null; return r }
        val carte = tri(a.map) ?: return null
        val heure = l.fini(l.f32(a.flow + 0x2D8 + 0x18), 100f)
        val jour = l.s32(a.flow + 0x2D8 + 0x24)?.takeIf { it in 0..99 }
        val form = tri(a.form) ?: IntArray(3); val work = tri(a.work) ?: IntArray(3); val dead = tri(a.dead) ?: IntArray(3)
        val oignons = IntArray(3)
        for (c in 0 until 3) for (h in 0 until 3) oignons[c] += (l.s32(a.inf + 4L * (c * 3 + h)) ?: 0).coerceIn(0, 99999)
        val ps = l.ptr(a.ps)
        var pieces = -1; var piecesTot = 30; var nes = -1; var morts = -1; var vivants = -1
        if (ps != null) {
            pieces = l.s32(ps + 0x17C)?.takeIf { it in 0..30 } ?: -1
            piecesTot = l.s32(ps + 0x174)?.takeIf { it in 1..30 } ?: 30
            morts = l.s32(ps + 0x1A0)?.takeIf { it in 0..999999 } ?: -1
            nes = l.s32(ps + 0x1A4)?.takeIf { it in 0..999999 } ?: -1
            vivants = l.s32(ps + 0x1A8)?.takeIf { it in 0..999999 } ?: -1
        }
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Pikmin sur la carte", COULEURS_P1.mapIndexed { i, (n, c) -> Ligne(n, carte[i].toString(), pct(carte[i], 100), c) } +
            listOf(Ligne("Avec Olimar", form.sum().toString()), Ligne("Au travail", work.sum().toString()))))
        blocs.add(Bloc("Dans les Oignons", COULEURS_P1.mapIndexed { i, (n, c) -> Ligne(n, oignons[i].toString(), -1f, c) }))
        blocs.add(Bloc("Bilan", listOf(
            Ligne("Pièces du vaisseau", if (pieces >= 0) "$pieces / $piecesTot" else "–", if (pieces >= 0) pct(pieces, piecesTot) else -1f, 0xFF9AD0FF),
            Ligne("Pikmin nés", if (nes >= 0) nes.toString() else "–"), Ligne("Pikmin perdus", if (morts >= 0) morts.toString() else dead.sum().toString()),
            Ligne("Pikmin en vie (bilan)", if (vivants >= 0) vivants.toString() else "–"))))
        return Fiche(id, "Pikmin", "Jour " + (jour?.plus(1) ?: "?") + if (heure != null) " · " + heureJeu(heure) else "", 0xFF1E5A2E, 0xFF07170C,
            grands = listOf("sur la carte" to carte.sum().toString(), "dans les Oignons" to oignons.sum().toString(), "pièces" to (if (pieces >= 0) pieces.toString() else "–")),
            blocs = blocs)
    }
}

private val COULEURS_P2 = listOf("Bleus" to 0xFF4F8DFFL, "Rouges" to 0xFFFF4D4DL, "Jaunes" to 0xFFFFD23FL, "Violets" to 0xFF9C5CFFL,
    "Blancs" to 0xFFEDEDEDL, "Bulbmin" to 0xFF8A6D4DL, "Carottes" to 0xFFFF9A3CL)
object JeuPikmin2 : ModuleJeu("Pikmin 2", "GPVP01", "GPVE01") {
    private class Adr(val form: Long, val alive: Long, val navi: Long, val sys: Long, val play: Long)
    private val P = Adr(0x80512FEC, 0x805132EC, 0x80516660, 0x80516768, 0x80516810)
    private val E = Adr(0x805122EC, 0x805125EC, 0x80515960, 0x80515A68, 0x80515B10)
    @Volatile private var pasNavi = 0                                // (taille de Navi : trouvee une fois, voir plus bas)

    override fun lire(l: Lecteur, id: String): Fiche? {
        val a = if (id == "GPVP01") P else E
        fun compte(adr: Long): IntArray? { val r = IntArray(7); for (k in 0 until 7) r[k] = l.s32(adr + 4 + 4L * k)?.takeIf { it in 0..9999 } ?: return null; return r }
        val vivants = compte(a.alive) ?: return null
        val esc0 = compte(a.form) ?: IntArray(7); val esc1 = compte(a.form + 0x20) ?: IntArray(7)
        val pd = l.ptr(a.play)
        var pokos = -1; var pokosGrotte = 0; var tresors = -1; val spray = IntArray(2); val baies = IntArray(2); val vieMax = FloatArray(2) { 100f }
        val oignons = IntArray(5)
        if (pd != null) {
            pokos = l.s32(pd + 0xE8)?.takeIf { it in 0..9_999_999 } ?: -1
            pokosGrotte = l.s32(pd + 0xEC)?.takeIf { it in 0..9_999_999 } ?: 0
            tresors = l.s32(pd + 0xBC)?.takeIf { it in 0..999 } ?: -1
            for (k in 0 until 2) { spray[k] = l.s32(pd + 0xC0 + 4L * k)?.coerceIn(0, 999) ?: 0; baies[k] = l.s32(pd + 0xC8 + 4L * k)?.coerceIn(0, 999) ?: 0
                vieMax[k] = l.fini(l.f32(pd + 0x24 + 4L * k), 10000f)?.takeIf { it > 0f } ?: 100f }
            l.ptr(pd + 0xA8 + 4)?.let { c -> for (col in 0 until 5) for (h in 0 until 3) oignons[col] += (l.s32(c + 4L * (col * 3 + h)) ?: 0).coerceIn(0, 99999) }
        }
        var heure: Float? = null; var jour = -1; var grotte = false
        l.ptr(a.sys)?.let { gs ->
            grotte = (l.u8(gs + 0x48) ?: 0) != 0
            l.ptr(gs + 0x40)?.let { tm -> heure = l.fini(l.f32(tm + 0x20C), 100f); jour = l.s32(tm + 0x218)?.takeIf { it in 0..999 } ?: -1 }
        }
        // les capitaines : NaviMgr.mArray (0x28), Navi.mHealth (0x2A0) ; le 2e trouve par sa table virtuelle (meme que le 1er)
        val sante = FloatArray(2) { -1f }
        l.ptr(a.navi)?.let { nm ->
            l.ptr(nm + 0x28)?.let { n0 ->
                sante[0] = l.fini(l.f32(n0 + 0x2A0), 10000f) ?: -1f
                val vt = l.u32(n0)
                if (pasNavi == 0 && vt != null) for (s in 0x2E0..0x420 step 4) if (l.u32(n0 + s) == vt && l.u16(n0 + s + 0x2DC) == 1) { pasNavi = s; break }
                if (pasNavi > 0) sante[1] = l.fini(l.f32(n0 + pasNavi + 0x2A0), 10000f) ?: -1f
            }
        }
        val blocs = ArrayList<Bloc>()
        val noms = listOf("Olimar", "Louie")
        blocs.add(Bloc("Capitaines", (0 until 2).filter { sante[it] >= 0f }.map { k ->
            Ligne(noms[k], (sante[k]).toInt().toString() + " / " + vieMax[k].toInt(), (sante[k] / vieMax[k]).coerceIn(0f, 1f), if (sante[k] / vieMax[k] < 0.3f) 0xFFFF3B3B else 0xFF3DDC84)
        } + listOf(Ligne("Escouade d'Olimar", esc0.sum().toString()), Ligne("Escouade de Louie", esc1.sum().toString()))))
        blocs.add(Bloc("Pikmin en vie", COULEURS_P2.mapIndexed { i, (n, c) -> Ligne(n, vivants[i].toString(), pct(vivants[i], 100), c) }.filter { it.valeur != "0" || COULEURS_P2.indexOfFirst { p -> p.first == it.nom } < 5 }))
        blocs.add(Bloc("Dans les Oignons", COULEURS_P2.take(5).mapIndexed { i, (n, c) -> Ligne(n, oignons[i].toString(), -1f, c) }))
        blocs.add(Bloc("Expédition", listOf(
            Ligne("Pokos", if (pokos >= 0) pokos.toString() else "–", if (pokos >= 0) pct(pokos, 10100) else -1f, 0xFFFFD23F),
            Ligne("Pokos de la grotte en cours", pokosGrotte.toString()),
            Ligne("Trésors", if (tresors >= 0) "$tresors / 201" else "–", if (tresors >= 0) pct(tresors, 201) else -1f, 0xFFFF9A3C),
            Ligne("Spray piment", spray[0].toString() + " (baies : " + baies[0] + ")"),
            Ligne("Spray amer", spray[1].toString() + " (baies : " + baies[1] + ")"))))
        val h = heure
        return Fiche(id, "Pikmin 2", (if (grotte) "Dans une grotte · " else "") + "Jour " + (if (jour >= 0) jour + 1 else "?") + if (h != null && !grotte) " · " + heureJeu(h) else "",
            0xFF2B4E8F, 0xFF0A1530,
            grands = listOf("Pikmin" to vivants.sum().toString(), "pokos" to (if (pokos >= 0) pokos.toString() else "–"), "trésors" to (if (tresors >= 0) tresors.toString() else "–")),
            blocs = blocs)
    }
}

// ---------------------------------------------------------------- Super Smash Bros. Melee
object JeuMelee : ModuleJeu("Super Smash Bros. Melee", "GALE01") {
    private const val SLOTS = 0x80453080L                          // player_slots (StaticPlayer, 0xE90 chacun)
    val PERSOS = arrayOf("Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch", "Kirby", "Bowser", "Link", "Luigi", "Mario", "Marth",
        "Mewtwo", "Ness", "Peach", "Pikachu", "Ice Climbers", "Rondoudou", "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Link enfant",
        "Dr. Mario", "Roy", "Pichu", "Ganondorf", "Créa-Main", "Polygone homme", "Polygone femme", "Giga Bowser", "Dé-Main", "Sac de sable", "Popo")
    private val COUL = longArrayOf(0xFFFF4D4D, 0xFF4F8DFF, 0xFFFFD23F, 0xFF3DDC84)

    override fun lire(l: Lecteur, id: String): Fiche? {
        val blocs = ArrayList<Bloc>()
        val grands = ArrayList<Pair<String, String>>()
        var enJeu = 0
        for (i in 0 until 4) {
            val b = SLOTS + i * 0xE90L
            val etat = l.s32(b) ?: return null
            val perso = l.s32(b + 4) ?: return null
            val genre = l.s32(b + 8) ?: return null
            if (genre !in 0..2 || perso !in 0..0x7F) continue                       // (3 : pas de joueur)
            val pc = (l.s16(b + 0x60) ?: 0).coerceIn(0, 999)
            val vies = l.u8(b + 0x8E)?.toByte()?.toInt() ?: 0
            val chutes = (l.s32(b + 0x68) ?: 0).coerceIn(0, 999)
            val ko = (0 until 6).sumOf { k -> (l.s32(b + 0x70 + 4L * k) ?: 0).coerceIn(0, 999) }
            val sd = (l.u16(b + 0x8C) ?: 0).coerceAtMost(999)
            val pieces = (l.s32(b + 0x90) ?: 0).coerceIn(0, 99999)
            val niveau = l.u8(b + 0x49) ?: 0
            val equipe = l.u8(b + 0x47) ?: 0
            val nom = PERSOS.getOrNull(perso) ?: ("Personnage " + perso)
            if (etat == 2) enJeu++
            blocs.add(Bloc("J" + (i + 1) + " · " + nom + if (genre == 1) " (ordi niv. $niveau)" else "", listOf(
                Ligne("Dégâts", "$pc %", (pc / 200f).coerceIn(0f, 1f), if (pc >= 100) 0xFFFF3B3B else COUL[i]),
                Ligne("Vies", if (vies in 0..99) vies.toString() else "–"),
                Ligne("Éjections", ko.toString()), Ligne("Chutes", chutes.toString()), Ligne("Autodestructions", sd.toString()),
                Ligne("Pièces", pieces.toString()), Ligne("Équipe", listOf("rouge", "bleue", "verte").getOrNull(equipe) ?: "–"))))
            grands.add(("J" + (i + 1)) to "$pc %")
        }
        if (blocs.isEmpty()) return Fiche(id, "Super Smash Bros. Melee", "Menus", 0xFF5A1E1E, 0xFF170707, message = "Pas de combat en ce moment.")
        return Fiche(id, "Super Smash Bros. Melee", if (enJeu > 0) "Combat en cours" else "Menus ou résultats", 0xFF8F1E2B, 0xFF1E070B, grands = grands, blocs = blocs)
    }
}

// ---------------------------------------------------------------- Mario Superstar Baseball (Project Rio)
object JeuMSB : ModuleJeu("Mario Superstar Baseball", "GYQE01") {
    val PERSOS = arrayOf("Mario", "Luigi", "Donkey Kong", "Diddy Kong", "Peach", "Daisy", "Yoshi", "Bébé Mario", "Bébé Luigi", "Bowser",
        "Wario", "Waluigi", "Koopa vert", "Toad rouge", "Boo", "Toadette", "Maskass rouge", "Birdo", "Monty", "Bowser Jr", "Paratroopa rouge",
        "Pianta bleu", "Pianta rouge", "Pianta jaune", "Noki bleu", "Noki rouge", "Noki vert", "Frère Marto", "Papy Champi", "Toad bleu",
        "Toad jaune", "Toad vert", "Toad violet", "Magikoopa bleu", "Magikoopa rouge", "Magikoopa vert", "Magikoopa jaune", "Roi Boo",
        "Flora Piranha", "Dixie Kong", "Goomba", "Paragoomba", "Koopa rouge", "Paratroopa vert", "Maskass bleu", "Maskass jaune",
        "Maskass vert", "Maskass noir", "Skelerex gris", "Skelerex vert", "Skelerex rouge", "Skelerex bleu", "Frère Pyro", "Frère Boomerang")
    private val STADES = arrayOf("Stade Mario", "Château de Bowser", "Palais de Wario", "Parc Yoshi", "Jardin de Peach", "Jungle DK", "Terrain jouet")
    private fun perso(i: Int?) = if (i == null) "–" else PERSOS.getOrNull(i) ?: ("Joueur " + i)

    override fun lire(l: Lecteur, id: String): Fiche? {
        val ext = l.u16(0x808928A4) ?: return null
        val dom = l.u16(0x808928CA) ?: return null
        val manche = l.u8(0x808928A3) ?: return null
        if (ext > 999 || dom > 999 || manche > 30) return null
        val enJeu = (l.u8(0x8036F3A9) ?: 0) != 0 || manche > 0
        val demi = l.u8(0x8089294D) ?: 0
        val manches = l.u8(0x8089294A) ?: 9
        val balles = l.u8(0x8089296F) ?: 0; val prises = l.u8(0x8089296B) ?: 0; val retraits = l.u8(0x80892973) ?: 0
        val etoiles1 = l.u8(0x80892AD6) ?: 0; val etoiles2 = l.u8(0x80892AD7) ?: 0
        val stade = STADES.getOrNull(l.u8(0x800E8705) ?: -1) ?: ""
        val cap = listOf(l.u8(0x80353083), l.u8(0x80353087))
        val attaque = if (demi == 0) 0 else 1; val defense = 1 - attaque
        fun idPerso(team: Int, r: Int) = l.u8(0x80353C05 + (team * 9L + r) * 0xA0)
        val frappeur = l.u8(0x80890971)?.takeIf { it < 9 }
        val lanceur = l.u8(0x80890AD9)?.takeIf { it < 9 }
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Au marbre", listOf(
            Ligne("Manche", (if (demi == 0) "Haut de la " else "Bas de la ") + manche + "e / " + manches),
            Ligne("Balles", "$balles", pct(balles, 4), 0xFF3DDC84), Ligne("Prises", "$prises", pct(prises, 3), 0xFFFFB300),
            Ligne("Retraits", "$retraits", pct(retraits, 3), 0xFFFF3B3B),
            Ligne("Frappeur", if (frappeur != null) perso(idPerso(attaque, frappeur)) else "–"),
            Ligne("Lanceur", if (lanceur != null) perso(idPerso(defense, lanceur)) else "–"),
            Ligne("Endurance du lanceur", if (lanceur != null) (l.u16(0x803535D8 + (defense * 9L + lanceur) * 0x1E) ?: 0).toString() else "–"),
            Ligne("Vitesse du lancer", (l.u8(0x80890B0A) ?: 0).toString()),
            Ligne("Type de lancer", when (l.u8(0x80890B21)) { 0 -> "Courbe"; 1 -> "Chargé"; 2 -> "Changement de vitesse"; else -> "–" }),
            Ligne("Étoiles", "J1 : $etoiles1 · J2 : $etoiles2"))))
        // les bases (coureur : numero dans l'equipe, 0xFF = personne)
        val bases = (1..3).map { b -> (l.u8(0x8088EEF9 + b * 0x154L) ?: 0xFF) != 0xFF }
        val qui = (1..3).map { b -> l.u8(0x8088EEFB + b * 0x154L) }
        blocs.add(Bloc("Bases", pastilles = (0 until 3).map { k -> ((k + 1).toString() + "re base" + if (bases[k]) " : " + perso(qui[k]) else "") to bases[k] }))
        // score manche par manche
        val ms = (0 until minOf(manche, 18)).map { i -> (l.u16(0x808928A4 + 2L * (i + 1)) ?: 0) to (l.u16(0x808928CA + 2L * (i + 1)) ?: 0) }
        if (ms.isNotEmpty()) blocs.add(Bloc("Score par manche", listOf(
            Ligne("Extérieur", ms.joinToString(" ") { it.first.toString() } + "  =  $ext"),
            Ligne("Domicile", ms.joinToString(" ") { it.second.toString() } + "  =  $dom"))))
        // les frappeurs de l'equipe a la batte : coups surs, coups de circuit, points produits
        for (team in 0..1) {
            val lignes = (0 until 9).mapNotNull { r ->
                val o = (team * 9L + r) * 0x26
                val nom = perso(idPerso(team, r))
                val ab = l.u8(0x803537E8 + o) ?: return@mapNotNull null
                val h = l.u8(0x803537E9 + o) ?: 0; val hr = l.u8(0x803537ED + o) ?: 0; val rbi = l.u8(0x803537F4 + o) ?: 0; val so = l.u8(0x803537F1 + o) ?: 0
                Ligne(nom + if (team == attaque && r == frappeur) "  ⚾" else "", "$h/$ab · $hr CC · $rbi PP · $so RAP")
            }
            blocs.add(Bloc((if (team == 0) "Extérieur" else "Domicile") + " · capitaine " + perso(cap[team]), lignes))
        }
        return Fiche(id, if (enJeu) "Extérieur $ext – $dom Domicile" else "Mario Superstar Baseball", stade, 0xFF1E6FB8, 0xFF07203A,
            grands = listOf("extérieur" to ext.toString(), "domicile" to dom.toString(), "manche" to "$manche"),
            blocs = if (enJeu) blocs else emptyList(), message = if (enJeu) "CS : coups sûrs/passages · CC : coups de circuit · PP : points produits · RAP : retraits sur prises" else "Pas de match en ce moment.")
    }
}

// ---------------------------------------------------------------- Ocarina of Time (Collector's Edition, Multi Pack)
// Le jeu N64 tourne dans l'emulateur du disque : sa memoire est quelque part dans celle de la GameCube. Une recherche
// unique (jeu en pause, toute la memoire lue comme la photo du chercheur de codes) trouve la sauvegarde en cours
// (SaveContext : "ZELDAZ" en 0x1C, zeldaret/oot save.h) ; les endroits vraisemblables sont gardes sur le telephone.
object JeuOoT : ModuleJeu("Ocarina of Time (émulé)", "PZLP01", "D43P01", "PZLE01", "D43E01", "PZLJ01", "D43J01") {
    @Volatile var progres = -1f
    @Volatile private var candidats: LongArray? = null
    @Volatile private var choisi = -1
    private var dernierTemps = IntArray(0)

    private fun cle(id: String) = "oot_$id"
    private fun charger(c: android.content.Context?, id: String) {
        if (candidats != null || c == null) return
        val s = c.getSharedPreferences("gcs", 0).getString(cle(id), "") ?: ""
        candidats = s.split(',').mapNotNull { it.trim().toLongOrNull() }.toLongArray()
    }
    private fun vraisemblable(l: Lecteur, a: Long): Boolean {
        if (l.texte(a + 0x1C, 6) != "ZELDAZ") return false
        val cap = l.s16(a + 0x2E) ?: return false; val vie = l.s16(a + 0x30) ?: return false
        val age = l.s32(a + 4) ?: return false; val rubis = l.s16(a + 0x34) ?: return false
        return cap in 0x30..0x140 && vie in 0..cap && age in 0..1 && rubis in 0..999
    }
    private fun vraisemblableMem(m: ByteArray, o: Int): Boolean {
        fun s16(k: Int) = ((m[o + k].toInt() shl 8) or (m[o + k + 1].toInt() and 255)).toShort().toInt()
        fun s32(k: Int) = ((m[o + k].toInt() and 255) shl 24) or ((m[o + k + 1].toInt() and 255) shl 16) or ((m[o + k + 2].toInt() and 255) shl 8) or (m[o + k + 3].toInt() and 255)
        val cap = s16(0x2E); val vie = s16(0x30)
        return cap in 0x30..0x140 && vie in 0..cap && s32(4) in 0..1 && s16(0x34) in 0..999
    }

    override fun agir(c: android.content.Context, id: String) {
        if (progres >= 0f) return
        progres = 0f
        Thread {
            Compagnon.pauseLecture = true
            try {
                Thread.sleep(400)
                val e = MemoireJeu.demande('P', 1)
                if (e != null) {
                    val mem = ByteArray(MemoireJeu.TAILLE)
                    val ok = MemoireJeu.lire(IntArray(MemoireJeu.FIN / MemoireJeu.BLK) { it }, mem, true, { progres = it * 0.95f }, { false })
                    MemoireJeu.demande('P', 0)
                    if (ok) {
                        val z = "ZELDAZ".toByteArray(Charsets.ISO_8859_1)
                        val r = ArrayList<Long>()
                        var i = 0x1C
                        val fin = MemoireJeu.FIN - 0x40
                        while (i < fin && r.size < 8) {
                            if (mem[i] == z[0] && mem[i + 1] == z[1] && mem[i + 2] == z[2] && mem[i + 3] == z[3] && mem[i + 4] == z[4] && mem[i + 5] == z[5]
                                && vraisemblableMem(mem, i - 0x1C)) r.add(MemoireJeu.BASE + i - 0x1C)
                            i += 2
                        }
                        candidats = r.toLongArray(); choisi = -1
                        c.getSharedPreferences("gcs", 0).edit().putString(cle(id), r.joinToString(",")).apply()
                    }
                }
            } catch (x: Exception) { }
            progres = -1f
            Compagnon.pauseLecture = false
        }.start()
    }

    private val OBJETS = mapOf(0x00 to "Bâton Mojo", 0x01 to "Noix Mojo", 0x02 to "Bombes", 0x03 to "Arc des fées", 0x04 to "Flèche de feu",
        0x05 to "Feu de Din", 0x06 to "Lance-pierre", 0x07 to "Ocarina des fées", 0x08 to "Ocarina du Temps", 0x09 to "Missiles Teigneux",
        0x0A to "Grappin", 0x0B to "Super Grappin", 0x0C to "Flèche de glace", 0x0D to "Vent de Farore", 0x0E to "Boomerang",
        0x0F to "Monocle de Vérité", 0x10 to "Haricots magiques", 0x11 to "Masse des Titans", 0x12 to "Flèche de lumière", 0x13 to "Amour de Nayru",
        0x14 to "Flacon", 0x15 to "Potion rouge", 0x16 to "Potion verte", 0x17 to "Potion bleue", 0x18 to "Fée", 0x19 to "Poisson",
        0x1A to "Lait", 0x1B to "Lettre de Ruto", 0x1C to "Flamme bleue", 0x1D to "Insectes", 0x1E to "Âme errante", 0x1F to "Lait (moitié)",
        0x20 to "Esprit", 0x21 to "Œuf", 0x22 to "Poulet", 0x23 to "Lettre de Zelda")
    private val MEDAILLONS = listOf("Forêt", "Feu", "Eau", "Esprit", "Ombre", "Lumière")
    private val CHANTS = listOf("Menuet des bois", "Boléro du feu", "Sérénade de l'eau", "Requiem de l'esprit", "Nocturne de l'ombre", "Prélude de la lumière",
        "Berceuse de Zelda", "Chant d'Epona", "Chant de Saria", "Chant du soleil", "Chant du temps", "Chant des tempêtes")
    private val PIERRES = listOf("Émeraude Kokiri", "Rubis Goron", "Saphir Zora")

    override fun lire(l: Lecteur, id: String): Fiche? {
        charger(Compagnon.ctx, id)
        val pr = progres
        if (pr >= 0f) return Fiche(id, "Ocarina of Time", "Recherche dans l'émulateur…", 0xFF2E5A1E, 0xFF0B1A07,
            grands = listOf("lu" to ((pr * 100).toInt().toString() + " %")), message = "Le jeu est en pause pendant la recherche (une seule fois).")
        val c = candidats ?: LongArray(0)
        val ok = c.filter { vraisemblable(l, it) }
        if (ok.isEmpty()) return Fiche(id, "Ocarina of Time", if (c.isEmpty()) "Pas encore trouvé" else "Plus trouvé (nouvelle partie ?)", 0xFF2E5A1E, 0xFF0B1A07,
            message = "Lance Ocarina of Time et charge une partie, puis touche le bouton : l'appli cherche le jeu dans la mémoire de l'émulateur " +
                "(jeu en pause quelques dizaines de secondes, une seule fois). Majora's Mask et Zelda NES ne sont pas pris en charge.",
            bouton = "Trouver Ocarina of Time dans l'émulateur")
        // la vraie sauvegarde en cours : celle dont l'heure du jeu avance
        val temps = ok.map { l.u16(it + 0x0C) ?: 0 }.toIntArray()
        if (dernierTemps.size == temps.size) { for (k in temps.indices) if (temps[k] != dernierTemps[k]) { choisi = k; break } }
        dernierTemps = temps
        val a = ok[if (choisi in ok.indices) choisi else 0]
        fun s16(o: Int) = l.s16(a + o) ?: 0
        val cap = s16(0x2E); val vie = s16(0x30); val rubis = s16(0x34)
        val magieNiv = l.u8(a + 0x32)?.toByte()?.toInt() ?: 0; val magie = l.u8(a + 0x33) ?: 0
        val adulte = (l.s32(a + 4) ?: 1) == 0
        val heure = (l.u16(a + 0x0C) ?: 0) * 24f / 65536f
        val jours = l.s32(a + 0x14)?.coerceIn(0, 99999) ?: 0
        val morts = l.u16(a + 0x22) ?: 0
        val inv = a + 0x74
        val objets = l.plage(inv, 24) ?: IntArray(0)
        val munitions = l.plage(inv + 0x18, 16) ?: IntArray(16)
        val equip = l.u16(inv + 0x28) ?: 0
        val ameliorations = l.u32(inv + 0x2C) ?: 0L
        val quete = l.u32(inv + 0x30) ?: 0L
        val tokens = l.s16(inv + 0x5C) ?: 0
        val doubleDef = (l.u8(a + 0x3D) ?: 0) != 0
        fun bit(n: Int) = (quete shr n) and 1L != 0L
        val quarts = ((quete shr 28) and 0xF).toInt()
        val bourse = when ((ameliorations shr 12) and 3) { 1L -> 200; 2L -> 500; else -> 99 }
        val blocs = ArrayList<Bloc>()
        blocs.add(Bloc("Link", listOf(
            Ligne("Cœurs", String.format(java.util.Locale.FRANCE, "%.1f / %d", vie / 16f, cap / 16), pct(vie, cap), 0xFFFF3B5C),
            Ligne("Magie", if (magieNiv > 0) "$magie / " + (48 * magieNiv) else "–", if (magieNiv > 0) pct(magie, 48 * magieNiv) else -1f, 0xFF3DDC84),
            Ligne("Rubis", "$rubis / $bourse", pct(rubis, bourse), 0xFF3DDC84),
            Ligne("Âge", if (adulte) "Adulte" else "Enfant"), Ligne("Quarts de cœur", "$quarts / 4"),
            Ligne("Double défense", if (doubleDef) "Oui" else "Non"),
            Ligne("Skulltulas d'or", "$tokens / 100", pct(tokens, 100), 0xFFFFD23F))))
        blocs.add(Bloc("Médaillons et pierres", pastilles = MEDAILLONS.mapIndexed { k, n -> n to bit(k) } + PIERRES.mapIndexed { k, n -> n to bit(18 + k) } +
            listOf("Carte Gerudo" to bit(22), "Pierre de Souffrance" to bit(21))))
        blocs.add(Bloc("Chants", pastilles = CHANTS.mapIndexed { k, n -> n to bit(6 + k) }))
        val inventaire = objets.filter { it != 0xFF }.map { OBJETS[it] ?: ("Objet " + Integer.toHexString(it)) }
        if (inventaire.isNotEmpty()) blocs.add(Bloc("Inventaire", pastilles = inventaire.map { it to true }))
        val noms = mapOf(0 to "Bâtons Mojo", 1 to "Noix Mojo", 2 to "Bombes", 3 to "Flèches", 6 to "Graines", 8 to "Missiles", 14 to "Haricots")
        blocs.add(Bloc("Munitions", noms.filter { objets.getOrNull(it.key)?.let { o -> o != 0xFF } == true }.map { (k, n) -> Ligne(n, munitions[k].toString()) }))
        val epees = listOf("Épée Kokiri", "Épée de légende", "Épée de Biggoron").filterIndexed { k, _ -> equip and (1 shl k) != 0 }
        val boucliers = listOf("Bouclier Mojo", "Bouclier hylien", "Bouclier miroir").filterIndexed { k, _ -> equip and (1 shl (4 + k)) != 0 }
        val tuniques = listOf("Tunique Kokiri", "Tunique Goron", "Tunique Zora").filterIndexed { k, _ -> equip and (1 shl (8 + k)) != 0 }
        val bottes = listOf("Bottes Kokiri", "Bottes de plomb", "Bottes des airs").filterIndexed { k, _ -> equip and (1 shl (12 + k)) != 0 }
        blocs.add(Bloc("Équipement", pastilles = (epees + boucliers + tuniques + bottes).map { it to true }))
        blocs.add(Bloc("Partie", listOf(Ligne("Heure", heureJeu(heure)), Ligne("Jours", jours.toString()), Ligne("Morts", morts.toString()))))
        return Fiche(id, "Ocarina of Time", (if (adulte) "Link adulte" else "Link enfant") + " · " + heureJeu(heure), 0xFF3E7A26, 0xFF0B1E06,
            grands = listOf("cœurs" to (vie / 16).toString(), "rubis" to rubis.toString(), "skulltulas" to tokens.toString()), blocs = blocs)
    }
}

// ---------------------------------------------------------------- v314 : valeurs epinglees (tous les jeux)
/** les valeurs trouvees avec le chercheur de codes et epinglees au compagnon : par jeu, gardees sur le telephone */
object Epingles {
    class E(val adr: Long, val taille: Int, val nom: String, val max: Long)
    @Volatile var version = 0
    private fun cle(jeu: String) = "epingles_$jeu"
    fun liste(c: android.content.Context?, jeu: String): List<E> {
        if (c == null) return emptyList()
        return try {
            val a = org.json.JSONArray(c.getSharedPreferences("gcs", 0).getString(cle(jeu), "[]"))
            (0 until a.length()).map { val o = a.getJSONObject(it); E(o.getLong("a"), o.getInt("t"), o.getString("n"), o.optLong("m")) }
        } catch (x: Exception) { emptyList() }
    }
    private fun garder(c: android.content.Context, jeu: String, l: List<E>) {
        val a = org.json.JSONArray()
        for (e in l) a.put(org.json.JSONObject().put("a", e.adr).put("t", e.taille).put("n", e.nom).put("m", e.max))
        c.getSharedPreferences("gcs", 0).edit().putString(cle(jeu), a.toString()).apply()
        version++
    }
    fun ajouter(c: android.content.Context, jeu: String, e: E) = garder(c, jeu, liste(c, jeu).filter { it.adr != e.adr } + e)
    fun retirer(c: android.content.Context, jeu: String, adr: Long) = garder(c, jeu, liste(c, jeu).filter { it.adr != adr })

    /** les valeurs en direct (une section) ; null : rien d'epingle */
    fun bloc(l: Lecteur, c: android.content.Context?, jeu: String): Bloc? {
        val ep = liste(c, jeu)
        if (ep.isEmpty()) return null
        return Bloc("Mes valeurs (épinglées)", ep.map { e ->
            val v: Long? = when (e.taille) { 1 -> l.u8(e.adr)?.toLong(); 2 -> l.u16(e.adr)?.toLong(); else -> l.u32(e.adr) }
            Ligne(e.nom, v?.toString() ?: "–", if (e.max > 0 && v != null) (v.toFloat() / e.max).coerceIn(0f, 1f) else -1f, 0xFFFF8FE6)
        })
    }
    /** un jeu sans ecran dedie : la fiche de ses valeurs epinglees */
    fun fiche(l: Lecteur, c: android.content.Context?, jeu: String, nom: String): Fiche? {
        val b = bloc(l, c, jeu) ?: return null
        return Fiche(jeu, nom.ifEmpty { jeu }, "Tes valeurs, en direct", 0xFF5B2A86, 0xFF1A0B2A, blocs = listOf(b),
            message = "Ajoute d'autres valeurs avec le chercheur de codes : une adresse trouvée > Épingler au compagnon de jeu.")
    }
    /** une fiche existante + les valeurs epinglees */
    fun avec(f: Fiche, l: Lecteur, c: android.content.Context?): Fiche {
        val b = bloc(l, c, f.jeu) ?: return f
        return Fiche(f.jeu, f.titre, f.sous, f.haut, f.bas, f.grands, f.blocs + b, f.message)
    }
}

// ---------------------------------------------------------------- l'ecran commun
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CompagnonFiche(f: Fiche) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(22.dp)).background(Brush.verticalGradient(listOf(Color(f.haut), Color(f.bas)))).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(f.titre, color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Black)
        if (f.sous.isNotEmpty()) Doux(f.sous, taille = 12, couleur = Color(0xCCFFFFFF))
        if (f.grands.isNotEmpty()) Row(Modifier.fillMaxWidth().padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for ((n, v) in f.grands) Column(Modifier.weight(1f).clip(RoundedCornerShape(14.dp)).background(Color(0x22FFFFFF)).padding(10.dp)) {
                Text(v, color = Color.White, fontSize = 22.sp, fontWeight = FontWeight.Black, maxLines = 1)
                Doux(n, taille = 11, couleur = Color(0xCCFFFFFF), lignes = 1)
            }
        }
        if (f.message.isNotEmpty()) Doux(f.message, taille = 12, couleur = Color.White)
        if (f.bouton.isNotEmpty()) {
            val ctx = androidx.compose.ui.platform.LocalContext.current
            Bouton(f.bouton, Modifier.fillMaxWidth().padding(top = 6.dp), plein = true) { ModulesJeux.pour(f.jeu)?.agir(ctx.applicationContext, f.jeu) }
        }
    }
    for (b in f.blocs) Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
            Titre(b.titre)
            for (li in b.lignes) Column {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Doux(li.nom, taille = 13, modifier = Modifier.weight(1f))
                    Text(li.valeur, color = if (li.couleur != 0L) Color(li.couleur) else C.Texte, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                }
                if (li.part >= 0f) {
                    Spacer(Modifier.height(3.dp))
                    Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(C.Verre)) {
                        Box(Modifier.fillMaxWidth(li.part).height(6.dp).clip(RoundedCornerShape(3.dp)).background(if (li.couleur != 0L) Color(li.couleur) else C.Rose))
                    }
                }
            }
            if (b.pastilles.isNotEmpty()) FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                for ((t, ok) in b.pastilles) Box(Modifier.clip(RoundedCornerShape(10.dp)).background(if (ok) Color(0x553DDC84) else Color(0x18FFFFFF)).padding(horizontal = 10.dp, vertical = 5.dp)) {
                    Text(t, color = if (ok) Color.White else C.Pale, fontSize = 12.sp)
                }
            }
            if (b.texte.isNotEmpty()) Doux(b.texte, taille = 12)
            b.plan?.let { DessinPlan(it) }
        }
    }
    Spacer(Modifier.width(1.dp))
}

/** v314 : la liste des valeurs epinglees d'un jeu, pour en retirer */
@Composable
fun GererEpingles(jeu: String) {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val v = androidx.compose.runtime.remember { androidx.compose.runtime.mutableIntStateOf(0) }
    @Suppress("UNUSED_VARIABLE") val vv = v.intValue
    val ep = Epingles.liste(ctx, jeu)
    if (ep.isEmpty()) return
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Titre("Mes valeurs épinglées")
            for (e in ep) Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(e.nom, color = C.Texte, fontSize = 14.sp)
                    Doux(java.lang.Long.toHexString(e.adr).uppercase().padStart(8, '0') + " · " + (8 * e.taille) + " bits" + (if (e.max > 0) " · max " + e.max else ""), taille = 11)
                }
                Bouton("Retirer", hauteur = 34.dp) { Epingles.retirer(ctx, jeu, e.adr); v.intValue = v.intValue + 1 }
            }
        }
    }
}


/** build 175 : le plan d'un plateau (Mario Party) : cases colorees, chemins, etoile, pions des joueurs */
@Composable
fun DessinPlan(p: Plan) {
    if (p.cases.isEmpty()) return
    var x0 = Float.MAX_VALUE; var x1 = -Float.MAX_VALUE; var z0 = Float.MAX_VALUE; var z1 = -Float.MAX_VALUE
    for (c in p.cases) { x0 = minOf(x0, c[0]); x1 = maxOf(x1, c[0]); z0 = minOf(z0, c[1]); z1 = maxOf(z1, c[1]) }
    val lx = (x1 - x0).coerceAtLeast(1f); val lz = (z1 - z0).coerceAtLeast(1f)
    val ratio = (lx / lz).coerceIn(0.6f, 1.8f)
    androidx.compose.foundation.Canvas(Modifier.fillMaxWidth().aspectRatio(ratio)) {
        val m = 14.dp.toPx()
        val k = minOf((size.width - 2 * m) / lx, (size.height - 2 * m) / lz)
        val ox = (size.width - lx * k) / 2; val oz = (size.height - lz * k) / 2
        fun pt(x: Float, z: Float) = androidx.compose.ui.geometry.Offset(ox + (x - x0) * k, oz + (z - z0) * k)
        for (l in p.liens) {
            val a = p.cases.getOrNull(l[0]) ?: continue; val b = p.cases.getOrNull(l[1]) ?: continue
            drawLine(Color(0x55FFFFFF), pt(a[0], a[1]), pt(b[0], b[1]), strokeWidth = 2.dp.toPx())
        }
        val r = (4.5f).dp.toPx()
        for ((i, c) in p.cases.withIndex()) {
            drawCircle(Color(p.couleurs.getOrElse(i) { 0xFF9AA8C8 }), r, pt(c[0], c[1]))
            if (i in p.etoiles) drawCircle(Color(0xFFFFD23F), r * 2.2f, pt(c[0], c[1]), style = androidx.compose.ui.graphics.drawscope.Stroke(2.dp.toPx()))
        }
        // les pions : decales s'ils sont sur la meme case
        val vus = HashMap<Long, Int>()
        for (pi in p.pions) {
            val cle = (pi.x.toLong() shl 32) xor pi.z.toLong()
            val n = vus.getOrDefault(cle, 0); vus[cle] = n + 1
            val c = pt(pi.x, pi.z) + androidx.compose.ui.geometry.Offset(n * 9.dp.toPx(), -n * 9.dp.toPx())
            drawCircle(Color(pi.couleur), 9.dp.toPx(), c)
            drawCircle(Color.White, 9.dp.toPx(), c, style = androidx.compose.ui.graphics.drawscope.Stroke(1.5f.dp.toPx()))
            val paint = android.graphics.Paint().apply { color = 0xFF14101E.toInt(); textSize = 9.sp.toPx(); isFakeBoldText = true; textAlign = android.graphics.Paint.Align.CENTER; isAntiAlias = true }
            drawIntoCanvas { cv -> cv.nativeCanvas.drawText(pi.texte, c.x, c.y + 3.dp.toPx(), paint) }
        }
    }
}
