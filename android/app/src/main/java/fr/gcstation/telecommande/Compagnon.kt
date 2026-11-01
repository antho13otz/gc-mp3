package fr.gcstation.telecommande

// Appli (v312) : ecran compagnon et chrono de speedrun. Pendant une partie lancee avec Pulse (reglage de la console
// « Chercheur de codes (appli) »), l'appli lit quelques valeurs du jeu en direct par le Broadband Adapter (meme chemin
// que le chercheur de codes : UDP 51237, bloc de 1 Ko, jeu pas en pause) environ 5 fois par seconde.
// Jeux pris en charge (versions europeennes, adresses tirees des tables de symboles publiques des decompilations
// zeldaret/tww, doldecomp/sms et doldecomp/mkdd ; seulement des adresses, aucun code repris) :
//  - GZLP01 The Wind Waker : coeurs, rubis, magie, perles, Triforce, chants, fleches, bombes, lieu ; sur la Grande Mer,
//    la carte 7 x 7 avec la position de Link (la mer va de -350000 a +350000, cases de 100000 : d_com_inf_game.cpp) ;
//  - GMSP01 Super Mario Sunshine : soleils, pieces bleues, pieces, vies, sante, eau de JET, zone et episode ;
//  - GM4P01 Mario Kart Double Dash!! : place, tour, temps des tours (en ms, comme le jeu), course terminee.
// Les valeurs lues sont verifiees (bornes) : une autre version du jeu ou un ecran sans partie n'affiche rien de faux.
// Chrono : depart, etapes et fin automatiques (ou a la main), record et meilleurs segments gardes sur le telephone.

import android.content.Context
import android.os.SystemClock
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameMillis
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

/** lecture des valeurs du jeu pendant un passage : chaque bloc de 1 Ko n'est demande qu'une fois */
class Lecteur(private val max: Int = 64) {
    private val cache = HashMap<Long, ByteArray?>()
    var lus = 0                                                        // (blocs de 1 Ko demandes pendant ce passage)
    private fun octets(a: Long, n: Int): ByteArray? {
        if (a < MemoireJeu.BASE || a + n > MemoireJeu.BASE + MemoireJeu.FIN) return null
        val b = a and (MemoireJeu.BLK - 1).toLong().inv()
        if (a + n > b + MemoireJeu.BLK) return null               // (valeurs alignees : jamais sur deux blocs)
        val d = if (cache.containsKey(b)) cache[b] else {
            if (lus >= max) return null                                 // (pas plus de blocs par passage : le jeu n'est pas gene)
            lus++
            MemoireJeu.bloc(b).also { cache[b] = it }
        } ?: return null
        val o = (a - b).toInt()
        return d.copyOfRange(o, o + n)
    }
    fun u8(a: Long): Int? = octets(a, 1)?.let { it[0].toInt() and 255 }
    fun u16(a: Long): Int? = octets(a, 2)?.let { ((it[0].toInt() and 255) shl 8) or (it[1].toInt() and 255) }
    fun s16(a: Long): Int? = u16(a)?.toShort()?.toInt()
    fun u32(a: Long): Long? = octets(a, 4)?.let {
        ((it[0].toLong() and 255) shl 24) or ((it[1].toLong() and 255) shl 16) or ((it[2].toLong() and 255) shl 8) or (it[3].toLong() and 255)
    }
    fun s32(a: Long): Int? = u32(a)?.toInt()
    fun f32(a: Long): Float? = u32(a)?.let { java.lang.Float.intBitsToFloat(it.toInt()) }
    /** pointeur vers la memoire du jeu, sinon null */
    fun ptr(a: Long): Long? = u32(a)?.takeIf { it >= 0x80003100L && it < MemoireJeu.BASE + MemoireJeu.FIN }
    fun texte(a: Long, n: Int): String? = octets(a, n)?.let { b ->
        var k = 0
        while (k < n && b[k] != 0.toByte()) k++
        String(b, 0, k, Charsets.ISO_8859_1)
    }
}

// ---------------------------------------------------------------- The Wind Waker (GZLP01)
object JeuWW {
    const val ID = "GZLP01"
    private const val G = 0x803CC530L                              // g_dComIfG_gameInfo (dSv_info_c en tete)
    private const val SCENE = G + 0x12A0 + 0x3E94                  // play.mCurStage : nom (8), point, salle
    private const val LINK = G + 0x12A0 + 0x48AC                   // play.mpPlayerPtr[0] (2 : le bateau)

    class Etat(val partie: Boolean, val vie: Int, val vieMax: Int, val rubis: Int, val bourse: Int, val magie: Int, val magieMax: Int,
               val chants: Int, val triforce: Int, val perles: Int, val fleches: Int, val flechesMax: Int, val bombes: Int, val bombesMax: Int,
               val nom: String, val scene: String, val x: Float, val z: Float, val aPos: Boolean, val angle: Int,
               val epee: Int, val bouclier: Int, val objets: List<Int>, val heure: Float, val jour: Int, val vent: Int,
               val visitees: BooleanArray, val cles: Int, val donjon: Int, val vitesse: Float)

    // (lu toutes les 2 s : inventaire, cases visitees)
    private var objets: List<Int> = emptyList()
    private var visitees = BooleanArray(49)

    /** null : rien de lisible (pas ce jeu, ou Pulse muet) */
    fun lire(l: Lecteur, lent: Boolean): Etat? {
        val vm = l.u16(G) ?: return null
        val v = l.u16(G + 2) ?: return null
        val scene = l.texte(SCENE, 8) ?: return null
        if (scene.any { it.code < 32 || it.code > 126 }) return null
        if (vm == 0 || vm > 80 || v > vm) return Etat(false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", scene, 0f, 0f, false, 0,
            0xFF, 0xFF, emptyList(), 0f, 0, 0, BooleanArray(49), 0, 0, 0f)
        val rubis = l.u16(G + 4) ?: 0
        val mm = l.u8(G + 0x13) ?: 0
        val m = l.u8(G + 0x14) ?: 0
        val col = G + 0xB4                                             // mCollect : chants (+9), Triforce (+10), perles (+11)
        if (lent || objets.isEmpty()) {
            val o = ArrayList<Int>()
            for (i in 0 until 21) { val it = l.u8(G + 0x3C + i) ?: continue; if (it != 0xFF && it != 0) o.add(it) }   // mPlayerItem
            objets = o
            val vis = BooleanArray(49)
            for (i in 0 until 49) vis[i] = ((l.u8(G + 0xC4 + 0x40 + i) ?: 0) and 1) != 0   // mMap.mFmapBits : case deja atteinte
            visitees = vis
        }
        var x = 0f; var z = 0f; var a = false; var ang = 0; var vit = 0f
        val p = l.ptr(LINK)
        if (p != null) {
            val px = l.f32(p + 0x1F8); val pz = l.f32(p + 0x200)       // current.pos
            if (px != null && pz != null && px.isFinite() && pz.isFinite() && Math.abs(px) < 1e6f && Math.abs(pz) < 1e6f) { x = px; z = pz; a = true }
            ang = l.s16(p + 0x20E) ?: 0                                // shape_angle.y
        }
        if (scene == "sea") {
            val b = l.ptr(LINK + 8)                                    // le bateau : speedF
            val sp = if (b != null) l.f32(b + 0x254) else null
            if (sp != null && sp.isFinite() && Math.abs(sp) < 500f) vit = Math.abs(sp)
        }
        return Etat(true, v, vm, rubis.coerceAtMost(9999), l.u8(G + 0x12) ?: 0, m.coerceAtMost(mm), mm,
            l.u8(col + 9) ?: 0, l.u8(col + 10) ?: 0, l.u8(col + 11) ?: 0,
            l.u8(G + 0x69) ?: 0, l.u8(G + 0x6F) ?: 0, l.u8(G + 0x6A) ?: 0, l.u8(G + 0x70) ?: 0,
            (l.texte(G + 0x148 + 0x14, 17) ?: "").filter { it.code in 32..255 }, scene, x, z, a, ang,
            l.u8(G + 0xE) ?: 0xFF, l.u8(G + 0xF) ?: 0xFF, objets,
            (l.f32(G + 0x24) ?: 0f).let { if (it.isFinite()) (it / 15f).mod(24f) else 0f }, l.u16(G + 0x28) ?: 0, l.s16(G + 0x2C) ?: 0,
            visitees, l.u8(G + 0x798) ?: 0, l.u8(G + 0x799) ?: 0, vit)
    }

    /** cases de la carte marine : salle n de la mer = case ((n - 1) % 7, (n - 1) / 7) ; A1 en haut a gauche */
    val ILES = arrayOf(
        "Forteresse Maudite", "Île Étoilée", "Îlot Septentrional des Fées", "", "Île du Croissant", "Archipel Ursa Major", "",
        "Île de la Face Quatre", "Île Géminée", "Île des Binocles", "Mercantîle", "Pas de Chat", "Île du Dragon", "Tour de l'Envol",
        "Îlot Occidental des Fées", "Île de la Rocaille", "Île de Tingle", "Île Triangulaire du Nord", "Îlot Oriental des Fées", "Montagne de Feu", "Archipel d'Orion",
        "Île de la Face Trois", "Île du Poisson", "Île de la Face Une", "Île de la Face Six", "Tour des Dieux", "Île Triangulaire de l'Est", "Îlot Épineux des Fées",
        "Serrure de Pierre", "Canonière", "Île de la Tête de Pierre", "Île Triangulaire du Sud", "Île de Qui-Ça", "Île de la Bombe", "Île aux Nichoirs",
        "Île du Losange", "Île de la Face Cinq", "Île du Requin", "Îlot Méridional des Fées", "Île Gelée", "Île aux Forêts", "Saut de Roc",
        "Île du Fer à Cheval", "Île de l'Aurore", "Île de la Terre", "Île de la Face Deux", "Îlots Kif Kif", "Batodrome", "Archipel de Cassiopée")

    fun case(x: Float, z: Float): Int {
        val c = Math.floor((x + 350000.0) / 100000.0).toInt().coerceIn(0, 6)
        val r = Math.floor((z + 350000.0) / 100000.0).toInt().coerceIn(0, 6)
        return r * 7 + c
    }
    fun nomCase(i: Int): String = ('A' + i % 7).toString() + (i / 7 + 1)
    fun ile(i: Int): String = ILES[i].ifEmpty { "Île " + nomCase(i) }

    /** le lieu (donjons et grands lieux) d'apres le nom de la scene ; "" : autre */
    fun lieu(s: String): String = when {
        s == "sea" -> "Grande Mer"
        s == "sea_T" -> "Écran titre"
        s == "MajyuE" || s == "majroom" || s == "ma2room" || s == "ma3room" || s == "Mjtower" || s == "M2tower" -> "Forteresse Maudite"
        s == "M_NewD2" || s == "M_DragB" || s == "M_Dra09" -> "Caverne du Dragon"
        s.startsWith("kin") || s == "kindan" -> "Bois Défendus"
        s.startsWith("Siren") -> "Tour des Dieux"
        s.startsWith("M_Dai") -> "Temple de la Terre"
        s.startsWith("kaze") -> "Temple du Vent"
        s == "Hyrule" || s == "Hyroom" || s == "kenroom" -> "Château d'Hyrule"
        s == "GTower" || s.startsWith("Ganon") || s.startsWith("Xboss") -> "Tour de Ganon"
        else -> ""
    }
    fun estDonjon(s: String) = lieu(s).let { it.isNotEmpty() && it != "Grande Mer" && it != "Écran titre" }

    val CHANTS = arrayOf("Requiem du Vent", "Ballade des Rafales", "Chant de l'Emprise", "Chant de la Terre", "Chant du Vent", "Chant du Passage")

    fun nomEpee(i: Int): String = when (i) { 0x38 -> "Épée du Héros"; 0x39 -> "Épée de Légende (sans pouvoir)"; 0x3A -> "Épée de Légende (à moitié)"; 0x3E -> "Épée de Légende"; else -> "" }
    fun nomBouclier(i: Int): String = when (i) { 0x3B -> "Bouclier du Héros"; 0x3C -> "Bouclier Miroir"; else -> "" }
    /** objets de l'inventaire (dItemNo) */
    fun nomObjet(i: Int): String = when (i) {
        0x20 -> "Longue-vue"; 0x21 -> "Tingle Tuner"; 0x22 -> "Baguette du Vent"; 0x23 -> "Boîte à images"; 0x24 -> "Sac à butin"
        0x25 -> "Corde à grappin"; 0x26 -> "Boîte à images +"; 0x27 -> "Arc"; 0x28 -> "Bracelets de force"; 0x29 -> "Bottes de plomb"
        0x2A -> "Armure magique"; 0x2C -> "Sac à appâts"; 0x2D -> "Boomerang"; 0x2F -> "Grappin"; 0x30 -> "Sac postal"; 0x31 -> "Sac de bombes"
        0x33 -> "Marteau"; 0x34 -> "Feuille Mojo"; 0x35 -> "Arc (flèches magiques)"; 0x36 -> "Arc (flèches de lumière)"
        0x50 -> "Flacon vide"; 0x51 -> "Potion rouge"; 0x52 -> "Potion verte"; 0x53 -> "Potion bleue"; 0x54 -> "Soupe (moitié)"; 0x55 -> "Soupe"
        0x56 -> "Flacon d'eau"; 0x57 -> "Fée en flacon"; 0x58 -> "Luciole"; 0x59 -> "Eau de la forêt"
        else -> ""
    }
}

// ---------------------------------------------------------------- Super Mario Sunshine (GMSP01)
object JeuSMS {
    const val ID = "GMSP01"
    private const val FLAGS = 0x80405828L                          // TFlagManager::smInstance
    private const val APP = 0x803E10C0L                            // gpApplication (TApplication)
    private const val MARIO = 0x804057B0L                          // gpMarioOriginal
    private const val MARIO_POS = 0x804057D4L                      // gpMarioPos
    private val ZONE_DE = intArrayOf(                              // (StageUtil : numero de scene -> zone a soleils)
        0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 1, 1, 5, 6, 1, 8, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 3, 8,
        4, 4, 5, 5, 5, 5, 5, 5, 6, 5, 7, 7, 8, 8, 2, 2, 3, 3, 5, 6, 9, 1, 1, 2, 6, 8, 5, 3, 9)
    val ZONES = arrayOf("Aéroport Delfino", "Place Delfino", "Colline Bianco", "Port Ricco", "Plage Gelato", "Parc Pinna",
        "Plage Sirena", "Village Pianta", "Baie Noki", "Mont Corona")
    val COULEURS = longArrayOf(0xFF9E9E9E, 0xFFFFD54A, 0xFF7CCB5B, 0xFF4FA3FF, 0xFFFFC27A, 0xFFFF7AB6, 0xFF7FE0D0, 0xFFFF8A3D, 0xFF5BC0EB, 0xFFE04F4F)

    /** soleils de chaque zone (MSF_SHINE_*) : numeros des drapeaux ; les 8 episodes d'abord (dans l'ordre), puis les autres */
    private val SOLEILS: Array<IntArray> = arrayOf(
        intArrayOf(),
        (0x56..0x63).toList().toIntArray() + intArrayOf(0x6B, 0x74, 0x75, 0x76),
        (0x00..0x09).toList().toIntArray() + intArrayOf(0x64),
        (0x0A..0x13).toList().toIntArray() + intArrayOf(0x65),
        (0x14..0x1D).toList().toIntArray() + intArrayOf(0x66),
        (0x1E..0x27).toList().toIntArray() + intArrayOf(0x67),
        (0x28..0x31).toList().toIntArray() + intArrayOf(0x68),
        intArrayOf(0x3C, 0x41, 0x3E, 0x3D, 0x40, 0x3F, 0x42, 0x43, 0x44, 0x45, 0x6A),   // (Pianta : episodes dans le desordre)
        (0x32..0x3B).toList().toIntArray() + intArrayOf(0x69),
        intArrayOf(0x77))
    /** soleils des pieces bleues (echange au port de la Place Delfino) */
    private val SOLEILS_BLEUS = (0x46..0x55).toList().toIntArray() + (0x6C..0x73).toList().toIntArray()

    class Zone(val soleils: Int, val total: Int, val episodes: Int, val bleues: Int)
    class Etat(val enJeu: Boolean, val soleils: Int, val bleues: Int, val pieces: Int, val vies: Int, val sante: Int,
               val eau: Float, val scene: Int, val zone: Int, val episode: Int, val secret: Boolean,
               val zones: List<Zone>, val echange: Int, val rouges: Int, val buse: Int, val yoshi: Boolean, val altitude: Float)

    private var zones: List<Zone> = emptyList()
    private var echange = 0
    private var yoshi = false

    fun lire(l: Lecteur, lent: Boolean): Etat? {
        val f = l.ptr(FLAGS) ?: return null
        val soleils = l.s32(f + 0xD0) ?: return null                 // MSF_SHINE_COUNT (0x40000)
        val bleues = l.s32(f + 0xD4) ?: return null                  // MSF_BLUE_COIN_COUNT
        if (soleils !in 0..120 || bleues !in 0..240) return null
        val pieces = (l.s32(f + 0xD8) ?: 0).coerceIn(0, 999)         // MSF_GOLD_COIN_COUNT
        val vies = (l.s32(f + 0x7C) ?: 0).coerceIn(0, 99)            // MSF_LIFE_COUNT (0x20001)
        if (lent || zones.isEmpty()) {
            val bits = ByteArray(119)                                 // mCardBools : drapeaux 0x1xxxx
            for (i in 0 until 119) bits[i] = (l.u8(f + i) ?: 0).toByte()
            fun b(n: Int) = n shr 3 < 119 && (bits[n shr 3].toInt() shr (n and 7)) and 1 != 0
            zones = (0 until 10).map { z ->
                val s = SOLEILS[z]
                var ep = 0
                for (k in 0 until minOf(8, s.size)) if (b(s[k])) ep = ep or (1 shl k)
                val bl = if (z == 0) 0 else (0 until 50).count { b(0x78 + (z - 1) * 50 + it) }   // MSF_BLUE_COIN_BASE
                Zone(s.count { b(it) }, s.size, ep, bl)
            }
            echange = SOLEILS_BLEUS.count { b(it) }
            yoshi = b(0x38F)                                          // MSF_YOSHI_UNLOCKED
        }
        val etat = l.u8(APP + 8) ?: 0
        val scene = l.u8(APP + 0xE) ?: 0
        val ep = l.u8(APP + 0xF) ?: 0
        var sante = -1; var eau = -1f
        val m = l.ptr(MARIO)
        if (m != null) {
            sante = l.s16(m + 0x120)?.takeIf { it in 0..8 } ?: -1
            val g = l.ptr(m + 0x3E4)
            if (g != null) eau = ((l.s32(g + 0x1C80) ?: -1) / 10500f).takeIf { it in 0f..1.2f }?.coerceAtMost(1f) ?: -1f
        }
        val alt = l.ptr(MARIO_POS)?.let { l.f32(it + 4) }?.takeIf { it.isFinite() && Math.abs(it) < 1e6f } ?: 0f
        val zone = if (scene < ZONE_DE.size) ZONE_DE[scene] else 1
        return Etat(etat == 5, soleils, bleues, pieces, vies, sante, eau, scene, zone, ep, scene in 0x15..0x34,
            zones, echange, (l.s32(f + 0xF4) ?: 0).coerceIn(0, 8), (l.s32(f + 0xE0) ?: 0).coerceIn(0, 7), yoshi, alt)
    }

    fun nomBuse(i: Int): String = when (i) { 1 -> "Turbo-fusée"; 4 -> "Planeur"; 5 -> "Turbo"; else -> "" }
}

// ---------------------------------------------------------------- Mario Kart Double Dash!! (GM4P01)
object JeuMKDD {
    const val ID = "GM4P01"
    private const val RACE = 0x803D5628L                           // RaceMgr::sRaceManager
    const val PAS_DE_TEMPS = 5999999

    class Kart(val n: Int, val x: Float, val z: Float, val place: Int, val pilote: Int, val copilote: Int, val humain: Boolean,
               val tour: Int, val progres: Float, val fini: Boolean, val total: Int, val ballons: Int)
    class Etat(val enCourse: Boolean, val fini: Boolean, val place: Int, val tour: Int, val tours: Int, val progres: Float,
               val tempsTours: List<Int>, val total: Int, val karts: List<Kart> = emptyList(), val circuit: Int = 0,
               val mode: Int = 0, val coupe: Int = -1, val niveau: Int = -1, val miroir: Boolean = false, val manche: Int = 0,
               val recordTour: Int = PAS_DE_TEMPS, val recordTotal: Int = PAS_DE_TEMPS, val ecarts: Map<Int, Float> = emptyMap())

    val HORS_COURSE = Etat(false, false, 0, 0, 0, 0f, emptyList(), PAS_DE_TEMPS)

    /** noms des circuits (ECourseID) */
    fun nomCircuit(id: Int): String = when (id) {
        0x21 -> "Baby Park"; 0x22 -> "Plage Peach"; 0x23 -> "Croisière Daisy"; 0x24 -> "Circuit Luigi"; 0x25 -> "Circuit Mario"
        0x26 -> "Circuit Yoshi"; 0x28 -> "Pont Champignon"; 0x29 -> "Ville Champignon"; 0x2A -> "Stade Waluigi"; 0x2B -> "Colisée Wario"
        0x2C -> "Jungle Dino Dino"; 0x2D -> "Montagne DK"; 0x2F -> "Château de Bowser"; 0x31 -> "Route Arc-en-ciel"; 0x32 -> "Désert Sec Sec"
        0x33 -> "Royaume Sorbet"; else -> ""
    }
    val PILOTES = arrayOf("", "Bébé Mario", "Bébé Luigi", "Parakoopa", "Koopa", "Peach", "Daisy", "Mario", "Luigi", "Wario", "Waluigi",
        "Yoshi", "Birdo", "Donkey Kong", "Diddy Kong", "Bowser", "Bowser Jr.", "Toad", "Toadette", "Roi Boo", "Flora Piranha")
    val COULEURS = longArrayOf(0xFFB8C7FF, 0xFFE53935, 0xFF43A047, 0xFFE57373, 0xFF66BB6A, 0xFFF48FB1, 0xFFFFA726, 0xFFE53935, 0xFF43A047,
        0xFFFDD835, 0xFF8E24AA, 0xFF7CB342, 0xFFF06292, 0xFF8D6E63, 0xFFD84315, 0xFF9E9D24, 0xFFC0CA33, 0xFF42A5F5, 0xFFF06292, 0xFFECEFF1, 0xFF2E7D32)
    fun nomPilote(i: Int) = if (i in 1 until PILOTES.size) PILOTES[i] else ""
    fun couleur(i: Int) = if (i in 0 until COULEURS.size) COULEURS[i] else COULEURS[0]
    val MODES = arrayOf("", "Contre-la-montre", "Grand Prix", "Course libre", "Bataille de ballons", "Bataille Shine", "Bataille Bob-omb", "Bataille", "", "")
    val COUPES = arrayOf("Coupe Champignon", "Coupe Fleur", "Coupe Étoile", "Coupe Spéciale", "Toutes les coupes")
    val NIVEAUX = arrayOf("50cc", "100cc", "150cc", "Miroir")

    /** le trace du circuit : bords gauche et droit de chaque troncon (x1, z1, x2, z2), lu une fois par course */
    @Volatile var trace: List<FloatArray> = emptyList()
    @Volatile var depart = -1                                       // (troncon de la ligne d'arrivee dans trace)
    @Volatile private var dernierCircuit = -1
    @Volatile private var essai = 0L
    private val pilotes = HashMap<Long, Int>()                      // (SCharDB -> pilote : donnees fixes du jeu)
    private var progresAvant = -1f
    private var progresT = 0L
    private var vitesse = 0f                                        // (avancee du kart 1 par seconde, lissee)

    /** Course::Sector (0xB0 octets) : mID (0), mBPoint (8, SCheckPoint : cpStart, cpEnd), mNextSectors[0] (0x98) */
    private fun lireTrace(l: Lecteur, s0: Long) {
        val pts = ArrayList<FloatArray>()
        val ids = ArrayList<Int>()
        val ordre = ArrayList<Long>()
        var s: Long? = s0
        while (s != null && s !in ordre && ordre.size < 600) {
            ordre.add(s)
            val cp = l.ptr(s + 8) ?: return
            val v = floatArrayOf(l.f32(cp) ?: Float.NaN, l.f32(cp + 8) ?: Float.NaN, l.f32(cp + 12) ?: Float.NaN, l.f32(cp + 20) ?: Float.NaN)
            if (!v.all { it.isFinite() && Math.abs(it) < 1e6f }) return
            pts.add(v); ids.add(l.s16(s) ?: -1)
            s = l.ptr(s + 0x98)
        }
        if (s == null) return
        val boucle = ordre.indexOf(s)                                  // (la boucle se ferme : on garde le tour seulement)
        if (boucle < 0 || pts.size - boucle < 8) return
        trace = pts.subList(boucle, pts.size).toList(); depart = ids.subList(boucle, ids.size).indexOf(0)
    }

    private fun pilote(l: Lecteur, db: Long?): Int {
        if (db == null) return 0
        return pilotes.getOrPut(db) { (l.u16(db) ?: 0).takeIf { it in 1..20 } ?: 0 }
    }

    fun lire(l: Lecteur, lent: Boolean): Etat? {
        val rm = l.ptr(RACE) ?: return HORS_COURSE
        val kc = l.ptr(rm + 0x48) ?: return HORS_COURSE               // mKartChecker[0]
        val tours = l.s32(kc + 0x0C) ?: return null                   // mMaxLap
        if (tours !in 1..9) return HORS_COURSE
        val tour = l.s32(kc + 0x2C) ?: return null                    // mLap (-1 avant la ligne)
        val fini = (l.u8(kc + 0x29) ?: 0) != 0                        // mRaceEnd
        val enCourse = (l.u8(kc + 0x78) ?: 0) != 0                    // mIsInRace
        val place = (l.s32(kc + 0x88) ?: 0).takeIf { it in 1..8 } ?: 0
        val prog = (l.f32(kc + 0x48) ?: 0f).let { if (it.isFinite()) it.coerceIn(0f, 1f) else 0f }
        val faits = (if (fini) tours else tour).coerceIn(0, tours)
        val lt = ArrayList<Int>()
        val tp = l.ptr(kc + 0x14)                                     // mLapTimes (ms)
        if (tp != null) for (i in 0 until faits) { val t = l.s32(tp + 4L * i) ?: break; if (t in 1 until PAS_DE_TEMPS) lt.add(t) else break }
        val total = if (fini) (l.s32(kc + 0x84) ?: PAS_DE_TEMPS) else PAS_DE_TEMPS
        // RaceInfo : mode (8), coupe (C), niveau (10), manche (26), miroir (2C), KartInfo[8] (30, 0x18 chacun)
        val ri = l.ptr(rm + 0x38)
        // les karts (KartChecker : mLap 2C, mRaceEnd 29, mRaceProgression 54, mPos 5C, mTotalTime 84, mRank 88, mBalloonNum 90)
        val karts = ArrayList<Kart>()
        for (i in 0 until 8) {
            val k = l.ptr(rm + 0x48 + 4L * i) ?: continue
            val x = l.f32(k + 0x5C) ?: continue
            val z = l.f32(k + 0x64) ?: continue
            if (!x.isFinite() || !z.isFinite() || Math.abs(x) > 1e6f || Math.abs(z) > 1e6f || (x == 0f && z == 0f)) continue
            var pi = 0; var co = 0; var hu = i == 0
            if (ri != null) {
                val ki = ri + 0x30 + 0x18L * i
                pi = pilote(l, l.ptr(ki + 8)); co = pilote(l, l.ptr(ki + 0x10))
                hu = (l.u32(ki + 4) ?: 0L) != 0L
            }
            karts.add(Kart(i, x, z, (l.s32(k + 0x88) ?: 0).takeIf { it in 1..8 } ?: 0, pi, co, hu,
                l.s32(k + 0x2C) ?: 0, (l.f32(k + 0x54) ?: 0f).let { if (it.isFinite()) it else 0f }, (l.u8(k + 0x29) ?: 0) != 0,
                l.s32(k + 0x84) ?: PAS_DE_TEMPS, (l.s16(k + 0x90) ?: 0).coerceIn(0, 9)))
        }
        // ecart avec les autres (en secondes) : difference d'avancee / avancee du kart 1 par seconde
        val moi = karts.firstOrNull { it.n == 0 }
        val now = SystemClock.elapsedRealtime()
        if (moi != null && enCourse && !fini) {
            if (progresAvant >= 0f && now > progresT) {
                val v = (moi.progres - progresAvant) / ((now - progresT) / 1000f)
                if (v > 0f && v < 1f) vitesse = if (vitesse <= 0f) v else vitesse * 0.8f + v * 0.2f
            }
            progresAvant = moi.progres; progresT = now
        } else { progresAvant = -1f }
        val ecarts = HashMap<Int, Float>()
        if (moi != null && vitesse > 0f) for (k in karts) if (k.n != 0 && !k.fini) ecarts[k.n] = (k.progres - moi.progres) / vitesse
        val crs = l.ptr(rm + 0x44)?.let { l.s32(it) } ?: 0
        val s0 = l.ptr(kc + 0x44) ?: l.ptr(kc + 0x40)               // (troncon ou se trouve le kart 1)
        if (s0 != null && (crs != dernierCircuit || (trace.isEmpty() && now - essai > 3000))) {
            dernierCircuit = crs; essai = now; trace = emptyList(); lireTrace(l, s0)
        }
        fun temps(v: Int?) = if (v != null && v in 1 until PAS_DE_TEMPS) v else PAS_DE_TEMPS
        return Etat(enCourse || fini, fini, place, tour, tours, prog, lt, total, karts, crs,
            if (ri != null) l.s32(ri + 8) ?: 0 else 0, if (ri != null) l.s32(ri + 0xC) ?: -1 else -1,
            if (ri != null) l.s32(ri + 0x10) ?: -1 else -1, ri != null && (l.u8(ri + 0x2C) ?: 0) != 0,
            if (ri != null) l.s16(ri + 0x26) ?: 0 else 0, temps(l.s32(rm + 0x94)), temps(l.s32(rm + 0x98)), ecarts)
    }
}

// ---------------------------------------------------------------- la lecture en direct
object Compagnon {
    val PRIS = mapOf(JeuWW.ID to "The Wind Waker", JeuSMS.ID to "Super Mario Sunshine", JeuMKDD.ID to "Mario Kart Double Dash!!")
    var jeu by mutableStateOf("")                                     // (code du jeu lu par Pulse)
    var etat by mutableStateOf<Any?>(null)
    var statut by mutableStateOf("Connexion à Pulse…")
    var ecranOuvert = false
    private var job: Job? = null
    private val scope = MainScope()
    val traceWW = mutableStateListOf<Offset>()                        // (le sillage de Link sur la mer)

    fun pris(id: String?) = id != null && PRIS.containsKey(id)

    fun demarrer() {
        if (job?.isActive == true) return
        job = scope.launch {
            var muet = 0; var idLu = 0L; var dernierLent = 0L
            while (isActive) {
                if (!ecranOuvert && !Chrono.enCours) break
                if (jeu.isEmpty() || SystemClock.elapsedRealtime() - idLu > 4000) {
                    val e = withContext(Dispatchers.IO) { MemoireJeu.demande('I') }
                    idLu = SystemClock.elapsedRealtime()
                    if (e == null) {
                        muet++
                        statut = if (St.jeu?.mem == false) "Ce jeu a été lancé sans le chercheur de codes. Sur la console : START > Réglages > Chercheur de codes (appli) = Oui, puis relance le jeu."
                                 else "Pulse ne répond pas. Sur la console : START > Réglages > Chercheur de codes (appli) = Oui, puis relance le jeu (Broadband Adapter branché)."
                        if (muet > 20 && !Chrono.enCours) break
                        delay(1500); continue
                    }
                    muet = 0
                    if (e.jeu != jeu) { jeu = e.jeu; etat = null; traceWW.clear() }
                }
                val id = jeu
                if (!pris(id)) { statut = "Ce jeu ($id) n'a pas encore d'écran compagnon. Pris en charge : " + PRIS.values.joinToString(", ") + " (versions européennes)."; etat = null; delay(2000); continue }
                val t0 = SystemClock.elapsedRealtime()
                val lent = t0 - dernierLent > 2000                          // (valeurs qui bougent peu : toutes les 2 s)
                if (lent) dernierLent = t0
                val l = Lecteur()
                val r = withContext(Dispatchers.IO) {
                    when (id) { JeuWW.ID -> JeuWW.lire(l, lent); JeuSMS.ID -> JeuSMS.lire(l, lent); else -> JeuMKDD.lire(l, lent) }
                }
                if (r == null) statut = "Valeurs illisibles : est-ce bien la version européenne du jeu ($id) ?"
                else {
                    statut = ""
                    if (r is JeuWW.Etat && r.aPos && r.scene == "sea") {
                        val o = Offset(r.x, r.z)
                        val d = traceWW.lastOrNull()
                        if (d == null || (d - o).getDistance() > 1500f) { traceWW.add(o); if (traceWW.size > 400) traceWW.removeAt(0) }
                    }
                    Chrono.observer(etat, r)
                    etat = r
                }
                // (rythme : 5 lectures par seconde au plus, et jamais plus d'environ 30 blocs de 1 Ko par seconde en tout)
                val pris = SystemClock.elapsedRealtime() - t0
                delay((maxOf(200L, l.lus * 33L) - pris).coerceAtLeast(40))
            }
        }
    }
}

// ---------------------------------------------------------------- le chrono de speedrun
object Chrono {
    class Etape(val nom: String, val ms: Long)

    var enCours by mutableStateOf(false)
    var fini by mutableStateOf(false)
    var nouveauRecord by mutableStateOf(false)
    var auto by mutableStateOf(true)
    var coupes by mutableStateOf(1)                                    // (MKDD : 1 coupe = 4 courses, ou 4 coupes = 16)
    var jeu by mutableStateOf("")
    val etapes = mutableStateListOf<Etape>()
    var record by mutableStateOf<List<Etape>?>(null)
    var meilleurs by mutableStateOf<LongArray?>(null)                 // (meilleur temps de chaque segment)
    private var depart = 0L
    private var finMs = 0L
    // MKDD : temps du jeu (somme des courses, comme les speedruns de Mario Kart)
    private var cumul = 0L
    private var courseDepart = 0L
    private var enCourse = false
    private var ctx: Context? = null
    // The Wind Waker : lieux deja passes pendant la course
    private val vus = HashSet<String>()

    fun init(c: Context) { ctx = c.applicationContext }
    private fun cle() = "chrono_" + jeu + (if (jeu == JeuMKDD.ID) "_" + coupes else "")
    fun nbCourses() = coupes * 4

    fun temps(): Long = when {
        fini -> finMs
        !enCours -> 0L
        jeu == JeuMKDD.ID -> cumul + if (enCourse) SystemClock.elapsedRealtime() - courseDepart else 0L
        else -> SystemClock.elapsedRealtime() - depart
    }

    fun charger(id: String) {
        if (enCours || fini) { if (id == jeu) return }
        jeu = id
        record = null; meilleurs = null
        val p = ctx?.getSharedPreferences("gcs", 0) ?: return
        try {
            val j = JSONObject(p.getString(cle(), null) ?: return)
            val n = j.getJSONArray("noms"); val t = j.getJSONArray("temps")
            record = (0 until minOf(n.length(), t.length())).map { Etape(n.getString(it), t.getLong(it)) }
            val m = j.optJSONArray("meilleurs")
            if (m != null) meilleurs = LongArray(m.length()) { m.getLong(it) }
        } catch (e: Exception) { }
    }

    private fun sauver() {
        val p = ctx?.getSharedPreferences("gcs", 0) ?: return
        val j = JSONObject()
        val r = record
        if (r != null) { j.put("noms", JSONArray(r.map { it.nom })); j.put("temps", JSONArray(r.map { it.ms })) }
        else { j.put("noms", JSONArray()); j.put("temps", JSONArray()) }
        val m = meilleurs
        if (m != null) j.put("meilleurs", JSONArray(m.toList()))
        p.edit().putString(cle(), j.toString()).apply()
    }

    fun demarrer() {
        etapes.clear(); vus.clear(); fini = false; nouveauRecord = false; enCours = true
        depart = SystemClock.elapsedRealtime(); cumul = 0L; courseDepart = depart
        Compagnon.demarrer()
    }
    fun etape(nom: String) {
        if (!enCours) return
        val t = temps()
        val i = etapes.size
        val prec = if (i == 0) 0L else etapes[i - 1].ms
        val seg = t - prec
        val m = meilleurs
        if (m == null || i >= m.size) meilleurs = (m ?: LongArray(0)).copyOf(i + 1).also { it[i] = seg }
        else if (seg < m[i]) meilleurs = m.copyOf().also { it[i] = seg }
        etapes.add(Etape(nom, t))
        sauver()
    }
    fun terminer(nom: String = "Fin") {
        if (!enCours) return
        etape(nom)
        finMs = etapes.last().ms
        enCours = false; fini = true
        val r = record
        nouveauRecord = r == null || r.isEmpty() || etapes.size > r.size || (etapes.size == r.size && finMs < r.last().ms)
        if (nouveauRecord) { record = etapes.toList(); sauver() }
    }
    fun annuler() { enCours = false; fini = false; etapes.clear(); vus.clear() }
    fun effacerRecord() { record = null; meilleurs = null; sauver() }

    /** ecart avec le record a l'etape i (ms), null : pas de record pour cette etape */
    fun ecart(i: Int): Long? { val r = record ?: return null; return if (i < r.size && i < etapes.size) etapes[i].ms - r[i].ms else null }
    fun estMeilleur(i: Int): Boolean {
        val m = meilleurs ?: return false
        if (i >= etapes.size || i >= m.size) return false
        val seg = etapes[i].ms - (if (i == 0) 0L else etapes[i - 1].ms)
        return seg <= m[i]
    }

    /** chaque nouvelle lecture du jeu : depart, etapes et fin automatiques */
    fun observer(avant: Any?, apres: Any) {
        if (Compagnon.jeu != jeu && !enCours) charger(Compagnon.jeu)
        when (apres) {
            is JeuMKDD.Etat -> mkdd(avant as? JeuMKDD.Etat, apres)
            is JeuWW.Etat -> ww(avant as? JeuWW.Etat, apres)
            is JeuSMS.Etat -> sms(avant as? JeuSMS.Etat, apres)
        }
    }

    private fun mkdd(a: JeuMKDD.Etat?, b: JeuMKDD.Etat) {
        val roule = b.enCourse && !b.fini
        if (roule && (a == null || !a.enCourse || a.fini)) {           // (une course commence)
            if (!enCours && auto && !fini) demarrer()
            courseDepart = SystemClock.elapsedRealtime()
        }
        enCourse = roule
        if (b.fini && a != null && !a.fini && enCours) {                // (ligne d'arrivee)
            val t = if (b.total in 1 until JeuMKDD.PAS_DE_TEMPS) b.total.toLong() else SystemClock.elapsedRealtime() - courseDepart
            cumul += t
            val n = etapes.size + 1
            if (n >= nbCourses()) terminer("Course $n") else etape("Course $n")
        }
    }

    private fun ww(a: JeuWW.Etat?, b: JeuWW.Etat) {
        if (a == null) return
        if (!enCours && auto && !fini && a.scene == "sea_T" && b.scene != "sea_T" && b.scene.isNotEmpty()
            && b.partie && b.vieMax == 12 && b.triforce == 0 && b.perles == 0) { demarrer(); vus.add(JeuWW.lieu(b.scene)) }
        if (!enCours || !auto || !b.partie || !a.partie) return
        val l = JeuWW.lieu(b.scene)
        if (l.isNotEmpty() && l != "Grande Mer" && l != "Écran titre" && vus.add(l)) etape(l)
        val nouv = b.perles and a.perles.inv()
        if (nouv and 1 != 0) etape("Perle de Nayru")
        if (nouv and 2 != 0) etape("Perle de Din")
        if (nouv and 4 != 0) etape("Perle de Farore")
        val tri = Integer.bitCount(b.triforce)
        if (tri > Integer.bitCount(a.triforce)) etape(if (tri == 8) "Triforce complète" else "Fragment de Triforce $tri")
    }

    private fun sms(a: JeuSMS.Etat?, b: JeuSMS.Etat) {
        if (a == null) return
        if (!enCours && auto && !fini && b.enJeu && !a.enJeu && b.soleils == 0 && b.zone == 0) demarrer()
        if (!enCours || !auto) return
        if (b.soleils == a.soleils + 1) {
            val nom = JeuSMS.ZONES[a.zone] + if (a.zone in 2..9 && !a.secret) " · ép. " + (a.episode + 1) else if (a.secret) " · secret" else ""
            if (a.zone == 9) terminer(nom) else etape(nom)
        }
    }
}

// ---------------------------------------------------------------- l'ecran
fun dureeChrono(ms: Long, signe: Boolean = false): String {
    val neg = ms < 0
    val a = Math.abs(ms)
    val h = a / 3600000; val m = a / 60000 % 60; val s = a / 1000 % 60; val d = a / 100 % 10
    val t = if (h > 0) String.format(java.util.Locale.FRANCE, "%d:%02d:%02d.%d", h, m, s, d)
            else String.format(java.util.Locale.FRANCE, "%d:%02d.%d", m, s, d)
    return (if (neg) "−" else if (signe) "+" else "") + t
}

@Composable
fun EcranCompagnon() {
    val act = LocalContext.current
    var vue by remember { mutableStateOf(0) }
    LaunchedEffect(Unit) { Chrono.init(act); Compagnon.ecranOuvert = true; Compagnon.demarrer() }
    DisposableEffect(Unit) { onDispose { Compagnon.ecranOuvert = false } }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        val id = Compagnon.jeu
        Doux(if (id.isNotEmpty() && Compagnon.pris(id)) Compagnon.PRIS[id] + " (" + id + ")" else "Pendant une partie lancée avec Pulse, l'appli lit le jeu en direct : jauges, carte, chrono.", taille = 12)
        if (Compagnon.statut.isNotEmpty()) Doux(Compagnon.statut, taille = 12, couleur = C.Orange)
        RangeeDePuces {
            Puce("En jeu", vue == 0) { vue = 0 }
            Puce("Chrono", vue == 1) { vue = 1 }
        }
        if (vue == 0) when (val e = Compagnon.etat) {
            is JeuWW.Etat -> CompagnonWW(e)
            is JeuSMS.Etat -> CompagnonSMS(e)
            is JeuMKDD.Etat -> CompagnonMKDD(e)
            else -> {}
        } else EcranChrono()
        Spacer(Modifier.height(20.dp))
    }
}

@Composable
private fun Jauge(nom: String, valeur: String, couleur: Color, modifier: Modifier = Modifier) {
    Column(modifier.clip(RoundedCornerShape(16.dp)).background(C.Verre).padding(horizontal = 12.dp, vertical = 10.dp)) {
        Text(valeur, color = couleur, fontSize = 20.sp, fontWeight = FontWeight.Black, maxLines = 1)
        Doux(nom, taille = 11, lignes = 1)
    }
}

@Composable
private fun Barre(part: Float, couleur: Color, hauteur: Int = 10) {
    Box(Modifier.fillMaxWidth().height(hauteur.dp).clip(RoundedCornerShape((hauteur / 2).dp)).background(C.Verre)) {
        Box(Modifier.fillMaxWidth(part.coerceIn(0f, 1f)).height(hauteur.dp).clip(RoundedCornerShape((hauteur / 2).dp))
            .background(androidx.compose.ui.graphics.Brush.horizontalGradient(listOf(couleur.copy(alpha = 0.7f), couleur))))
    }
}

/** une pastille allumee ou eteinte (objets de donjon, chants...) */
@Composable
private fun Etiquette(t: String, allume: Boolean, couleur: Color) {
    Box(Modifier.clip(RoundedCornerShape(50)).background(if (allume) couleur.copy(alpha = 0.22f) else Color(0x0FFFFFFF))
        .padding(horizontal = 10.dp, vertical = 5.dp)) {
        Text(t, color = if (allume) couleur else C.Pale, fontSize = 12.sp, fontWeight = if (allume) FontWeight.SemiBold else FontWeight.Normal, maxLines = 1)
    }
}

/** une carte avec un bandeau de couleur en haut */
@Composable
private fun Volet(titre: String, couleur: Color, droite: String = "", content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(22.dp)).background(C.Verre)) {
        Box(Modifier.fillMaxWidth().background(androidx.compose.ui.graphics.Brush.horizontalGradient(listOf(couleur.copy(alpha = 0.35f), Color.Transparent)))
            .padding(horizontal = 16.dp, vertical = 9.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Titre(titre, couleur = couleur)
                Spacer(Modifier.weight(1f))
                if (droite.isNotEmpty()) Doux(droite, taille = 12, couleur = C.Texte)
            }
        }
        Column(Modifier.padding(start = 16.dp, end = 16.dp, top = 10.dp, bottom = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) { content() }
    }
}

private fun coeur(s: Float): Path = Path().apply {
    moveTo(s * 0.5f, s * 0.92f)
    cubicTo(s * 0.1f, s * 0.62f, s * 0.0f, s * 0.38f, s * 0.12f, s * 0.2f)
    cubicTo(s * 0.26f, s * 0.02f, s * 0.47f, s * 0.08f, s * 0.5f, s * 0.28f)
    cubicTo(s * 0.53f, s * 0.08f, s * 0.74f, s * 0.02f, s * 0.88f, s * 0.2f)
    cubicTo(s * 1.0f, s * 0.38f, s * 0.9f, s * 0.62f, s * 0.5f, s * 0.92f)
    close()
}

private fun hhmm(h: Float): String { val m = (h * 60).toInt(); return String.format(java.util.Locale.FRANCE, "%02d:%02d", m / 60 % 24, m % 60) }

// ---------------------------------------------------------------- Wind Waker
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CompagnonWW(e: JeuWW.Etat) {
    if (!e.partie) { Carte(Modifier.fillMaxWidth()) { Doux("Aucune partie chargée (écran titre ou choix du fichier).", couleur = C.Texte) }; return }
    val vert = Color(0xFF3DDC84); val or = Color(0xFFFFD54A)
    val lieu = JeuWW.lieu(e.scene)
    val nuit = e.heure < 6f || e.heure >= 18f
    Volet(if (e.nom.isNotEmpty()) e.nom else "Link", vert, "Jour " + (e.jour + 1) + " · " + hhmm(e.heure) + if (nuit) " ☾" else " ☀") {
        Text(when { lieu.isNotEmpty() -> lieu; else -> "Lieu : " + e.scene }, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
        val n = (e.vieMax + 3) / 4
        Canvas(Modifier.fillMaxWidth().height(if (n > 10) 60.dp else 30.dp)) {
            val pas = minOf(size.width / 10f, 30.dp.toPx())
            val s = pas * 0.9f
            for (i in 0 until n) {
                val q = (e.vie - i * 4).coerceIn(0, 4)
                val o = Offset((i % 10) * pas, (i / 10) * (pas + 2.dp.toPx()))
                val p = coeur(s); p.translate(o)
                drawPath(p, Color(0x26FFFFFF))
                if (q > 0) clipPath(p) { drawArc(Color(0xFFFF3B5C), -90f, 90f * q, true, o, Size(s, s)) }
                drawPath(p, Color(0xCCFFFFFF), style = Stroke(1.2.dp.toPx()))
            }
        }
        if (e.magieMax > 0) Barre(e.magie / e.magieMax.toFloat(), vert, 8)
        val cap = when (e.bourse) { 1 -> 1000; 2 -> 5000; else -> 200 }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Jauge("rubis / $cap", e.rubis.toString(), vert, Modifier.weight(1f))
            Jauge("flèches" + if (e.flechesMax > 0) " / " + e.flechesMax else "", e.fleches.toString(), C.Texte, Modifier.weight(1f))
            Jauge("bombes" + if (e.bombesMax > 0) " / " + e.bombesMax else "", e.bombes.toString(), C.Texte, Modifier.weight(1f))
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            JeuWW.nomEpee(e.epee).let { if (it.isNotEmpty()) Etiquette("⚔ $it", true, Color(0xFF9FD3FF)) }
            JeuWW.nomBouclier(e.bouclier).let { if (it.isNotEmpty()) Etiquette("🛡 $it", true, Color(0xFF9FD3FF)) }
        }
    }
    if (JeuWW.estDonjon(e.scene) && lieu != "Forteresse Maudite") Volet("Donjon", Color(0xFFB388FF), lieu) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Etiquette("🗝 " + e.cles + if (e.cles > 1) " petites clés" else " petite clé", e.cles > 0, or)
            Etiquette("Carte", e.donjon and 1 != 0, Color(0xFF9FD3FF))
            Etiquette("Boussole", e.donjon and 2 != 0, Color(0xFF9FD3FF))
            Etiquette("Grande clé", e.donjon and 4 != 0, or)
            Etiquette("Boss vaincu", e.donjon and 8 != 0, Color(0xFFFF6E6E))
        }
    }
    Volet("Quête", or, Integer.bitCount(e.triforce).toString() + "/8 Triforce") {
        Row(verticalAlignment = Alignment.CenterVertically) {
            for ((k, p) in listOf(Triple("Nayru", Color(0xFF4FA3FF), 1), Triple("Din", Color(0xFFFF4D4D), 2), Triple("Farore", Color(0xFF3DDC84), 4)).withIndex()) {
                val a = e.perles and p.third != 0
                Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                    Canvas(Modifier.size(30.dp)) {
                        val d = Path().apply { moveTo(size.width / 2, 0f); lineTo(size.width, size.height / 2); lineTo(size.width / 2, size.height); lineTo(0f, size.height / 2); close() }
                        drawPath(d, if (a) p.second else Color(0x1FFFFFFF))
                        drawPath(d, if (a) Color.White.copy(alpha = 0.8f) else Color(0x40FFFFFF), style = Stroke(1.5.dp.toPx()))
                    }
                    Doux("Perle de " + p.first, taille = 10, couleur = if (a) p.second else C.Pale)
                }
                if (k < 2) Spacer(Modifier.width(4.dp))
            }
        }
        Canvas(Modifier.fillMaxWidth().height(56.dp)) {                // (les 8 fragments : deux rangees de triangles)
            val w = minOf(size.width / 4.6f, 52.dp.toPx()); val h = size.height / 2
            val x0 = (size.width - w * 4) / 2
            for (i in 0 until 8) {
                val r = i / 4; val c = i % 4
                val haut = (c + r) % 2 == 0
                val x = x0 + c * w; val y = r * h
                val t = Path().apply {
                    if (haut) { moveTo(x + w / 2, y + 2); lineTo(x + w - 2, y + h - 2); lineTo(x + 2, y + h - 2) }
                    else { moveTo(x + 2, y + 2); lineTo(x + w - 2, y + 2); lineTo(x + w / 2, y + h - 2) }
                    close()
                }
                val a = e.triforce and (1 shl i) != 0
                drawPath(t, if (a) or else Color(0x14FFFFFF))
                drawPath(t, if (a) Color(0xFFFFF3B0) else Color(0x33FFFFFF), style = Stroke(1.dp.toPx()))
            }
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            for ((i, c) in JeuWW.CHANTS.withIndex()) Etiquette("♪ $c", e.chants and (1 shl i) != 0, Color(0xFFFFB3E6))
        }
    }
    val objets = e.objets.map { JeuWW.nomObjet(it) }.filter { it.isNotEmpty() }
    if (objets.isNotEmpty()) Volet("Inventaire", Color(0xFF9FD3FF), objets.size.toString() + " objets") {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            for (o in objets) Etiquette(o, true, if (o.startsWith("Flacon") || o.startsWith("Potion") || o.startsWith("Soupe") || o.startsWith("Fée") || o == "Luciole" || o.startsWith("Eau")) Color(0xFF7FE0D0) else C.Texte)
        }
    }
    CarteMarine(e)
}

@Composable
private fun CarteMarine(e: JeuWW.Etat) {
    val mer = e.scene == "sea"
    val ici = if (mer && e.aPos) JeuWW.case(e.x, e.z) else -1
    val vues = e.visitees.count { it }
    Volet("Carte marine", Color(0xFF4FC3F7), "$vues / 49 cases") {
        Text(if (ici >= 0) JeuWW.nomCase(ici) + " · " + JeuWW.ile(ici) else "Link n'est pas en mer", color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Bold)
        val trace = Compagnon.traceWW
        Canvas(Modifier.fillMaxWidth().aspectRatio(1f)) {
            val marge = 18.dp.toPx()
            val c = (size.width - marge) / 7f
            val paint = android.graphics.Paint().apply { isAntiAlias = true; textSize = 11.dp.toPx(); color = android.graphics.Color.argb(170, 255, 255, 255); textAlign = android.graphics.Paint.Align.CENTER }
            drawRoundRect(androidx.compose.ui.graphics.Brush.verticalGradient(listOf(Color(0xFF0E3B66), Color(0xFF082340)), marge, marge + 7 * c),
                Offset(marge, marge), Size(c * 7, c * 7), CornerRadius(10.dp.toPx()))
            for (i in 0 until 49) {
                val o = Offset(marge + (i % 7) * c, marge + (i / 7) * c)
                if (!e.visitees[i]) drawRect(Color(0x66000814), o, Size(c, c))            // (brouillard : case jamais atteinte)
                if (i == ici) drawRect(Color(0x55FF4FD8), o, Size(c, c))
                val centre = o + Offset(c / 2, c / 2)
                drawCircle(if (e.visitees[i]) Color(0xFF8FD18A) else Color(0x332E4D35), c * 0.11f, centre)
            }
            for (k in 0..7) {
                drawLine(Color(0x2EFFFFFF), Offset(marge + k * c, marge), Offset(marge + k * c, marge + 7 * c), 1f)
                drawLine(Color(0x2EFFFFFF), Offset(marge, marge + k * c), Offset(marge + 7 * c, marge + k * c), 1f)
            }
            drawIntoCanvas { cv ->
                for (k in 0 until 7) {
                    cv.nativeCanvas.drawText(('A' + k).toString(), marge + k * c + c / 2, marge - 5.dp.toPx(), paint)
                    cv.nativeCanvas.drawText((k + 1).toString(), marge / 2, marge + k * c + c / 2 + 4.dp.toPx(), paint)
                }
            }
            fun pt(o: Offset) = Offset(marge + (o.x + 350000f) / 100000f * c, marge + (o.y + 350000f) / 100000f * c)
            for (k in 1 until trace.size) drawLine(Color(0x99FFFFFF), pt(trace[k - 1]), pt(trace[k]), 2.dp.toPx())
            // le vent (rose des vents dans le coin)
            val va = e.vent / 65536.0 * 2 * Math.PI
            val vc = Offset(marge + 7 * c - 26.dp.toPx(), marge + 26.dp.toPx())
            drawCircle(Color(0x66000000), 20.dp.toPx(), vc)
            drawCircle(Color(0x55FFFFFF), 20.dp.toPx(), vc, style = Stroke(1.dp.toPx()))
            val vd = Offset(Math.sin(va).toFloat(), Math.cos(va).toFloat())
            drawLine(Color(0xFFFFE08A), vc - vd * 14.dp.toPx(), vc + vd * 14.dp.toPx(), 2.5.dp.toPx())
            drawCircle(Color(0xFFFFE08A), 3.5.dp.toPx(), vc + vd * 14.dp.toPx())
            if (mer && e.aPos) {                                         // (Link : une fleche dans le sens ou il va)
                val p = pt(Offset(e.x, e.z))
                val a = e.angle / 65536.0 * 2 * Math.PI
                val d = Offset(Math.sin(a).toFloat(), Math.cos(a).toFloat()); val n = Offset(-d.y, d.x)
                val r = 9.dp.toPx()
                drawCircle(Color(0x55FF4FD8), r * 1.9f, p)
                val f = Path().apply { val t = p + d * r * 1.3f; val g = p - d * r * 0.8f + n * r * 0.8f; val h = p - d * r * 0.8f - n * r * 0.8f
                    moveTo(t.x, t.y); lineTo(g.x, g.y); lineTo(h.x, h.y); close() }
                drawPath(f, Color(0xFF3DDC84)); drawPath(f, Color.White, style = Stroke(1.5.dp.toPx()))
            }
        }
        Doux("Assombries : cases jamais atteintes · flèche verte : Link · rose des vents : sens du vent", taille = 10)
    }
}

// ---------------------------------------------------------------- Sunshine
@Composable
private fun CompagnonSMS(e: JeuSMS.Etat) {
    val or = Color(0xFFFFD54A)
    val zc = Color(JeuSMS.COULEURS[e.zone])
    Volet(JeuSMS.ZONES[e.zone], zc, when { !e.enJeu -> "menu"; e.secret -> "niveau secret"; e.zone in 2..9 -> "épisode " + (e.episode + 1); else -> "" }) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Canvas(Modifier.size(64.dp)) {                                   // (le soleil)
                val c = Offset(size.width / 2, size.height / 2)
                for (k in 0 until 16) {
                    val a = Math.toRadians(k * 22.5)
                    val l = if (k % 2 == 0) 0.5f else 0.38f
                    drawLine(Color(0xFFFFB300), c, c + Offset((Math.cos(a) * size.width * l).toFloat(), (Math.sin(a) * size.width * l).toFloat()), 4.dp.toPx())
                }
                drawCircle(androidx.compose.ui.graphics.Brush.radialGradient(listOf(Color(0xFFFFF59D), or, Color(0xFFFFA000)), c, size.width * 0.32f), size.width * 0.32f, c)
                drawCircle(Color(0xFF5D4037), size.width * 0.035f, c + Offset(-size.width * 0.08f, -size.width * 0.03f))
                drawCircle(Color(0xFF5D4037), size.width * 0.035f, c + Offset(size.width * 0.08f, -size.width * 0.03f))
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(e.soleils.toString() + " / 120", style = TextStyle(brush = C.Neon), fontSize = 32.sp, fontWeight = FontWeight.Black)
                Barre(e.soleils / 120f, or, 8)
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Jauge("pièces", e.pieces.toString(), or, Modifier.weight(1f))
            Jauge("pièces bleues", e.bleues.toString() + "/240", Color(0xFF4FA3FF), Modifier.weight(1f))
            Jauge("vies", "× " + e.vies, C.Rouge, Modifier.weight(1f))
        }
        if (e.rouges > 0) Row(verticalAlignment = Alignment.CenterVertically) {
            Doux("Pièces rouges ", taille = 12)
            for (k in 0 until 8) Box(Modifier.padding(horizontal = 2.dp).size(14.dp).clip(RoundedCornerShape(7.dp)).background(if (k < e.rouges) Color(0xFFFF3B3B) else Color(0x1FFFFFFF)))
        }
    }
    Volet("Mario", Color(0xFFFF6E6E), if (e.yoshi) "Yoshi débloqué" else "") {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Canvas(Modifier.size(92.dp)) {                                   // (la jauge de vie : 8 parts, comme dans le jeu)
                val r = size.width / 2
                for (k in 0 until 8) {
                    val plein = k < e.sante
                    val coul = if (!plein) Color(0x1FFFFFFF) else if (e.sante <= 2) Color(0xFFFF3B3B) else if (e.sante <= 4) Color(0xFFFFB300) else Color(0xFF3DDC84)
                    drawArc(coul, -90f + k * 45f + 3f, 39f, true, Offset.Zero, Size(size.width, size.height))
                }
                drawCircle(Color(0xFF1A1030), r * 0.5f, Offset(r, r))
                drawIntoCanvas {
                    val p = android.graphics.Paint().apply { isAntiAlias = true; textSize = 20.dp.toPx(); color = android.graphics.Color.WHITE
                        textAlign = android.graphics.Paint.Align.CENTER; typeface = android.graphics.Typeface.DEFAULT_BOLD }
                    it.nativeCanvas.drawText(if (e.sante >= 0) e.sante.toString() else "–", r, r + 7.dp.toPx(), p)
                }
            }
            Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (e.eau >= 0f) { Doux("JET : " + (e.eau * 100).toInt() + " % d'eau", taille = 12, couleur = C.Texte); Barre(e.eau, Color(0xFF4FC3F7), 12) }
                val b = JeuSMS.nomBuse(e.buse)
                if (b.isNotEmpty()) Etiquette("Buse : $b", true, Color(0xFF9FD3FF))
                Doux("Altitude " + e.altitude.toInt(), taille = 11)
            }
        }
    }
    if (e.zones.size == 10) Volet("Les zones", or, e.echange.toString() + "/24 soleils des pièces bleues") {
        for (ligne in (1..9).chunked(2)) Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for (z in ligne) {
                val zo = e.zones[z]
                val c = Color(JeuSMS.COULEURS[z])
                Column(Modifier.weight(1f).clip(RoundedCornerShape(16.dp)).background(if (z == e.zone) c.copy(alpha = 0.18f) else Color(0x0DFFFFFF)).padding(10.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Canvas(Modifier.size(30.dp)) {
                            drawArc(Color(0x22FFFFFF), 0f, 360f, false, style = Stroke(4.dp.toPx()))
                            if (zo.total > 0) drawArc(c, -90f, 360f * zo.soleils / zo.total, false, style = Stroke(4.dp.toPx(), cap = androidx.compose.ui.graphics.StrokeCap.Round))
                        }
                        Spacer(Modifier.width(8.dp))
                        Column {
                            Text(JeuSMS.ZONES[z], color = C.Texte, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
                            Doux(zo.soleils.toString() + "/" + zo.total + " ☀" + if (zo.bleues > 0) " · " + zo.bleues + " 🔵" else "", taille = 11)
                        }
                    }
                    if (z in 2..8) Row(Modifier.padding(top = 6.dp)) {
                        for (k in 0 until 8) Box(Modifier.padding(end = 3.dp).size(9.dp).clip(RoundedCornerShape(5.dp)).background(if (zo.episodes and (1 shl k) != 0) c else Color(0x1FFFFFFF)))
                    }
                }
            }
            if (ligne.size == 1) Spacer(Modifier.weight(1f))
        }
        Doux("Pastilles : les 8 épisodes de la zone · 🔵 : pièces bleues trouvées", taille = 10)
    }
}

// ---------------------------------------------------------------- Mario Kart
@Composable
private fun CompagnonMKDD(e: JeuMKDD.Etat) {
    if (!e.enCourse) { Carte(Modifier.fillMaxWidth()) { Doux("Pas de course en ce moment (menus).", couleur = C.Texte) }; return }
    val moi = e.karts.firstOrNull { it.n == 0 }
    val nom = JeuMKDD.nomCircuit(e.circuit)
    val infos = listOfNotNull(JeuMKDD.MODES.getOrNull(e.mode)?.takeIf { it.isNotEmpty() },
        if (e.mode == 2) JeuMKDD.COUPES.getOrNull(e.coupe) else null,
        if (e.mode in 2..3) JeuMKDD.NIVEAUX.getOrNull(e.niveau) else null,
        if (e.mode == 2 && e.manche >= 0) "course " + (e.manche + 1) else null).joinToString(" · ")
    Volet(nom.ifEmpty { "Course" }, C.Rose, infos) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(if (e.place == 0) "–" else e.place.toString() + if (e.place == 1) "er" else "e",
                style = TextStyle(brush = C.Neon), fontSize = 58.sp, fontWeight = FontWeight.Black)
            Spacer(Modifier.width(14.dp))
            Column {
                if (moi != null && moi.pilote > 0) Text(JeuMKDD.nomPilote(moi.pilote) + if (moi.copilote > 0) " & " + JeuMKDD.nomPilote(moi.copilote) else "",
                    color = Color(JeuMKDD.couleur(moi.pilote)), fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
                Text(if (e.fini) "Arrivée !" else "Tour " + (e.tour + 1).coerceIn(1, e.tours) + " / " + e.tours, color = C.Texte, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                if (e.fini && e.total < JeuMKDD.PAS_DE_TEMPS) Doux("Temps total " + dureeMs(e.total.toLong()), taille = 13, couleur = C.Cyan)
            }
        }
        if (!e.fini) Barre(e.progres, C.Rose)
        val meilleur = e.tempsTours.minOrNull()
        if (e.tempsTours.isNotEmpty()) Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for ((i, t) in e.tempsTours.withIndex()) Column(Modifier.weight(1f).clip(RoundedCornerShape(12.dp)).background(Color(0x0FFFFFFF)).padding(8.dp)) {
                Doux("Tour " + (i + 1), taille = 10)
                Text(dureeMs(t.toLong()), color = if (t == meilleur && e.tempsTours.size > 1) C.Vert else C.Texte, fontSize = 13.sp, fontFamily = FontFamily.Monospace, maxLines = 1)
            }
        }
        if (e.mode == 1 && (e.recordTour < JeuMKDD.PAS_DE_TEMPS || e.recordTotal < JeuMKDD.PAS_DE_TEMPS))
            Doux("Records du circuit : " + listOfNotNull(if (e.recordTotal < JeuMKDD.PAS_DE_TEMPS) "course " + dureeMs(e.recordTotal.toLong()) else null,
                if (e.recordTour < JeuMKDD.PAS_DE_TEMPS) "tour " + dureeMs(e.recordTour.toLong()) else null).joinToString(" · "), taille = 12, couleur = Color(0xFFFFD54A))
    }
    if (JeuMKDD.trace.isNotEmpty() || e.karts.isNotEmpty()) Volet("Circuit", C.Cyan, "vu du dessus") { CarteCircuit(e) }
    if (e.karts.size > 1) Volet("Classement", Color(0xFFFFD54A), e.karts.size.toString() + " karts") {
        val bataille = e.mode in 4..7
        for (k in e.karts.sortedBy { if (it.place == 0) 99 else it.place }) Row(verticalAlignment = Alignment.CenterVertically) {
            Text(if (k.place == 0) "–" else k.place.toString(), color = if (k.place == 1) Color(0xFFFFD54A) else C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Black,
                modifier = Modifier.width(26.dp))
            Box(Modifier.size(12.dp).clip(RoundedCornerShape(6.dp)).background(Color(JeuMKDD.couleur(k.pilote))))
            Spacer(Modifier.width(8.dp))
            Column(Modifier.weight(1f)) {
                Text((JeuMKDD.nomPilote(k.pilote).ifEmpty { "Kart " + (k.n + 1) }) + if (k.copilote > 0) " & " + JeuMKDD.nomPilote(k.copilote) else "",
                    color = if (k.n == 0) C.Rose else C.Texte, fontSize = 13.sp, fontWeight = if (k.n == 0) FontWeight.Bold else FontWeight.Normal, maxLines = 1)
                if (k.n == 0 || k.humain) Doux(if (k.n == 0) "toi" else "joueur", taille = 10, couleur = C.Cyan)
            }
            val ec = e.ecarts[k.n]
            Text(when {
                bataille -> "🎈 " + k.ballons
                k.fini && k.total < JeuMKDD.PAS_DE_TEMPS -> dureeMs(k.total.toLong())
                k.n == 0 -> "tour " + (k.tour + 1).coerceIn(1, maxOf(1, e.tours))
                ec != null -> (if (ec >= 0) "+" else "−") + String.format(java.util.Locale.FRANCE, "%.1f s", Math.abs(ec))
                else -> "tour " + (k.tour + 1).coerceIn(1, maxOf(1, e.tours))
            }, color = when { ec == null || k.n == 0 -> C.Doux; ec > 0 -> C.Vert; else -> C.Rouge }, fontSize = 13.sp, fontFamily = FontFamily.Monospace)
        }
        if (!e.fini) Doux("Écart estimé en secondes : + devant toi, − derrière toi.", taille = 10)
    }
}

@Composable
private fun CarteCircuit(e: JeuMKDD.Etat) {
    val t = JeuMKDD.trace
    if (t.isEmpty()) { Doux("Lecture du circuit…", taille = 11); return }
    Canvas(Modifier.fillMaxWidth().aspectRatio(1f)) {
        var x0 = Float.MAX_VALUE; var x1 = -Float.MAX_VALUE; var z0 = Float.MAX_VALUE; var z1 = -Float.MAX_VALUE
        for (p in t) { x0 = minOf(x0, p[0], p[2]); x1 = maxOf(x1, p[0], p[2]); z0 = minOf(z0, p[1], p[3]); z1 = maxOf(z1, p[1], p[3]) }
        val marge = 14.dp.toPx()
        val k = minOf((size.width - 2 * marge) / (x1 - x0).coerceAtLeast(1f), (size.height - 2 * marge) / (z1 - z0).coerceAtLeast(1f))
        val ox = (size.width - (x1 - x0) * k) / 2; val oz = (size.height - (z1 - z0) * k) / 2
        fun pt(x: Float, z: Float) = Offset(ox + (x - x0) * k, oz + (z - z0) * k)
        val route = Path(); val bordG = Path(); val bordD = Path()
        for (i in t.indices) {
            val a = t[i]; val b = t[(i + 1) % t.size]
            val p1 = pt(a[0], a[1]); val p2 = pt(a[2], a[3]); val p3 = pt(b[2], b[3]); val p4 = pt(b[0], b[1])
            route.addPath(Path().apply { moveTo(p1.x, p1.y); lineTo(p2.x, p2.y); lineTo(p3.x, p3.y); lineTo(p4.x, p4.y); close() })
            if (i == 0) { bordG.moveTo(p1.x, p1.y); bordD.moveTo(p2.x, p2.y) } else { bordG.lineTo(p1.x, p1.y); bordD.lineTo(p2.x, p2.y) }
        }
        bordG.close(); bordD.close()
        drawPath(route, Color(0x33FF4FD8), style = Stroke(10.dp.toPx()))      // (halo)
        drawPath(route, Color(0xFF3A3F52))
        drawPath(bordG, Color(0xCCFFFFFF), style = Stroke(1.5.dp.toPx()))
        drawPath(bordD, Color(0xCCFFFFFF), style = Stroke(1.5.dp.toPx()))
        val dep = JeuMKDD.depart
        if (dep in t.indices) {                                           // (ligne d'arrivee en damier)
            val a = t[dep]; val g = pt(a[0], a[1]); val d = pt(a[2], a[3])
            for (j in 0 until 8) drawLine(if (j % 2 == 0) Color.White else Color.Black, g + (d - g) * (j / 8f), g + (d - g) * ((j + 1) / 8f), 5.dp.toPx())
        }
        val paint = android.graphics.Paint().apply { isAntiAlias = true; textSize = 10.dp.toPx(); color = android.graphics.Color.BLACK
            textAlign = android.graphics.Paint.Align.CENTER; typeface = android.graphics.Typeface.DEFAULT_BOLD }
        for (kt in e.karts.sortedByDescending { it.n }) {                 // (le kart 1, le joueur, dessine en dernier : au-dessus)
            val p = pt(kt.x, kt.z)
            val joueur = kt.n == 0
            val r = if (joueur) 9.dp.toPx() else 7.dp.toPx()
            if (joueur) drawCircle(Color(0x66FF4FD8), r * 1.9f, p)
            drawCircle(Color(JeuMKDD.couleur(kt.pilote)), r, p)
            drawCircle(if (joueur) Color(0xFFFF4FD8) else Color.White, r, p, style = Stroke((if (joueur) 2.5 else 1.2).dp.toPx()))
            if (kt.place > 0) drawIntoCanvas { it.nativeCanvas.drawText(kt.place.toString(), p.x, p.y + 3.5.dp.toPx(), paint) }
        }
    }
    Doux("Pastille : couleur du pilote, chiffre : place · contour rose : toi · damier : ligne d'arrivée", taille = 10)
}

/** temps de Mario Kart : 1:23.456 */
fun dureeMs(ms: Long): String = String.format(java.util.Locale.FRANCE, "%d:%02d.%03d", ms / 60000, ms / 1000 % 60, ms % 1000)

@Composable
private fun EcranChrono() {
    var image by remember { mutableLongStateOf(0L) }
    LaunchedEffect(Chrono.enCours) { while (Chrono.enCours) withFrameMillis { image = it } }
    val id = Compagnon.jeu
    LaunchedEffect(id) { if (id.isNotEmpty() && !Chrono.enCours) Chrono.charger(id) }
    Carte(Modifier.fillMaxWidth()) {
        Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            @Suppress("UNUSED_VARIABLE") val t = image
            Text(dureeChrono(Chrono.temps()), style = TextStyle(brush = C.Neon), fontSize = 52.sp, fontWeight = FontWeight.Black, fontFamily = FontFamily.Monospace)
            val r = Chrono.record
            Doux(when {
                Chrono.fini -> if (Chrono.nouveauRecord) "Nouveau record !" else "Terminé"
                Chrono.enCours -> if (id == JeuMKDD.ID) "Temps du jeu (somme des courses)" else "En cours"
                Chrono.auto -> "Prêt : le chrono part tout seul " + when (id) {
                    JeuWW.ID -> "en lançant un nouveau fichier"
                    JeuSMS.ID -> "à l'arrivée sur l'aéroport d'un nouveau fichier"
                    JeuMKDD.ID -> "au départ de la première course"
                    else -> "dans un jeu pris en charge"
                }
                else -> "Prêt (à la main)"
            } + if (r != null && !Chrono.enCours && !Chrono.fini) " · record " + dureeChrono(r.last().ms) else "", taille = 12, centre = true)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (!Chrono.enCours) Bouton(if (Chrono.fini) "Nouvelle course" else "Démarrer", plein = true, icone = Ic.Lecture) { Chrono.charger(id); Chrono.demarrer() }
                else {
                    Bouton("Étape", plein = true) { Chrono.etape("Étape " + (Chrono.etapes.size + 1)) }
                    Bouton("Fin", icone = Ic.Stop) { Chrono.terminer() }
                }
                if (Chrono.enCours || Chrono.fini) Bouton("", icone = Ic.Fermer) { Chrono.annuler() }
            }
        }
    }
    if (!Chrono.enCours) Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            RangeeDePuces {
                Puce("Automatique", Chrono.auto) { Chrono.auto = true }
                Puce("À la main", !Chrono.auto) { Chrono.auto = false }
            }
            if (id == JeuMKDD.ID) RangeeDePuces {
                Puce("1 coupe (4 courses)", Chrono.coupes == 1) { Chrono.coupes = 1; Chrono.charger(id) }
                Puce("16 courses", Chrono.coupes == 4) { Chrono.coupes = 4; Chrono.charger(id) }
            }
            Doux(when (id) {
                JeuWW.ID -> "Étapes : chaque grand lieu atteint pour la première fois (donjons, Château d'Hyrule, Tour de Ganon), chaque perle, chaque fragment de Triforce. Fin : bouton Fin."
                JeuSMS.ID -> "Étapes : chaque soleil (zone et épisode). Fin : le soleil du Mont Corona."
                JeuMKDD.ID -> "Étapes : chaque course terminée, avec les temps du jeu au millième. Fin : la dernière course."
                else -> "Lance The Wind Waker, Super Mario Sunshine ou Mario Kart Double Dash!! (versions européennes)."
            } + " Le téléphone lit le jeu 5 fois par seconde : les étapes automatiques sont précises à 0,2 s près.", taille = 11)
            if (Chrono.record != null) Bouton("Effacer le record", Modifier.fillMaxWidth(), hauteur = 38.dp) { Chrono.effacerRecord() }
        }
    }
    val et = Chrono.etapes
    val r = Chrono.record
    if (et.isNotEmpty() || r != null) Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Titre("Étapes")
            val n = maxOf(et.size, r?.size ?: 0)
            for (i in 0 until n) Row(verticalAlignment = Alignment.CenterVertically) {
                val fait = i < et.size
                Text((if (fait) et[i].nom else r!![i].nom), color = if (fait) C.Texte else C.Pale, fontSize = 13.sp, maxLines = 1, modifier = Modifier.weight(1f))
                if (fait) {
                    val d = Chrono.ecart(i)
                    if (d != null) Text(dureeChrono(d, true), color = if (Chrono.estMeilleur(i)) Color(0xFFFFD54A) else if (d <= 0) C.Vert else C.Rouge, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                    else if (Chrono.estMeilleur(i)) Text("★", color = Color(0xFFFFD54A), fontSize = 12.sp)
                    Spacer(Modifier.width(10.dp))
                }
                Text(dureeChrono(if (fait) et[i].ms else r!![i].ms), color = if (fait) C.Texte else C.Pale, fontSize = 13.sp, fontFamily = FontFamily.Monospace)
            }
            Doux("Vert : en avance sur le record · rouge : en retard · or : meilleur segment jamais fait.", taille = 10)
        }
    }
}
