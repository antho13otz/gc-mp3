package fr.gcstation.telecommande

// Appli (v312) : ecran compagnon et chrono de speedrun. Pendant une partie lancee avec Pulse (reglage de la console
// « Chercheur de codes (appli) »), l'appli lit quelques valeurs du jeu en direct par le Broadband Adapter (meme chemin
// que le chercheur de codes : UDP 51237, bloc de 1 Ko, jeu pas en pause) environ 3 fois par seconde.
// v313 : beaucoup plus de valeurs, et toujours sans gener le jeu : au plus 28 blocs par lecture, demandes une par une
// (~1 bloc par image du jeu, Pulse en envoie 2 au plus par image), le trace d'un circuit a 1 bloc toutes les 15 ms.
// Jeux pris en charge (versions europeennes, adresses tirees des tables de symboles publiques des decompilations
// zeldaret/tww, doldecomp/sms et doldecomp/mkdd ; seulement des adresses, aucun code repris) :
//  - GZLP01 The Wind Waker : coeurs, rubis, magie, perles, Triforce, chants, fleches, bombes, lieu ; sur la Grande Mer,
//    la carte 7 x 7 avec la position de Link (la mer va de -350000 a +350000, cases de 100000 : d_com_inf_game.cpp) ;
//    v313 : inventaire (21 emplacements, flacons), epee, bouclier, bracelets, bourse, maximum de fleches et de bombes,
//    sacs (butin, appats, courrier), heure et jour, K.O., iles deja sur la carte (mFmapBits), cles et objets du donjon,
//    cap et vitesse de Link, vent en cours (g_env_light.mWind) ;
//  - GMSP01 Super Mario Sunshine : soleils, pieces bleues, pieces, vies, sante, eau de JET, zone et episode ;
//    v313 : chaque soleil de chaque zone (drapeaux de TFlagManager), pieces bleues et record de pieces par zone,
//    buse en cours et buses debloquees, Yoshi, position, vitesse et altitude de Mario ;
//  - GM4P01 Mario Kart Double Dash!! : place, tour, temps des tours (en ms, comme le jeu), course terminee ;
//    v313 : mode, cylindree, coupe, miroir (RaceInfo), pilotes et kart de chacun, objets tenus (ItemObjMgr), classement
//    et progression de chaque kart, ballons et points des batailles.
// Les valeurs lues sont verifiees (bornes) : une autre version du jeu ou un ecran sans partie n'affiche rien de faux.
// Chrono : depart, etapes et fin automatiques (ou a la main), record et meilleurs segments gardes sur le telephone.

import android.content.Context
import android.os.SystemClock
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.gestures.detectTapGestures
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


/** lecture des valeurs du jeu pendant un passage : chaque bloc de 1 Ko n'est demande qu'une fois, et au plus [budget]
 *  blocs par passage (le jeu repond aux demandes pendant son image : peu de blocs = pas de ralentissement du jeu) */
class Lecteur(private val budget: Int = 24, private val pauseMs: Long = 3) {
    private val cache = HashMap<Long, ByteArray?>()
    var demandes = 0; private set
    private fun octets(a: Long, n: Int): ByteArray? {
        if (a < MemoireJeu.BASE || a + n > MemoireJeu.BASE + MemoireJeu.FIN) return null
        val b = a and (MemoireJeu.BLK - 1).toLong().inv()
        if (a + n > b + MemoireJeu.BLK) return null               // (valeurs alignees : jamais sur deux blocs)
        val d = if (cache.containsKey(b)) cache[b] else {
            if (demandes >= budget) return null
            demandes++
            if (demandes > 1 && pauseMs > 0) try { Thread.sleep(pauseMs) } catch (e: InterruptedException) { }   // (au plus ~1 bloc par image du jeu)
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
    /** n octets (chacun lu a part : une plage peut passer d'un bloc a l'autre) ; null si un seul manque */
    fun plage(a: Long, n: Int): IntArray? { val r = IntArray(n); for (i in 0 until n) r[i] = u8(a + i) ?: return null; return r }
    fun fini(f: Float?, max: Float = 1e6f): Float? = f?.takeIf { it.isFinite() && Math.abs(it) < max }
}

/** angle du jeu (s16 : 65536 = un tour) -> radians */
fun angleJeu(v: Int): Double = v / 65536.0 * 2 * Math.PI

// ---------------------------------------------------------------- The Wind Waker (GZLP01)
object JeuWW {
    const val ID = "GZLP01"
    private const val G = 0x803CC530L                              // g_dComIfG_gameInfo (dSv_info_c en tete)
    private const val SCENE = G + 0x12A0 + 0x3E94                  // play.mCurStage : nom (8), point, salle
    private const val LINK = G + 0x12A0 + 0x48AC                   // play.mpPlayerPtr[0]
    private const val VENT = 0x803EC3FCL + 0x97C                   // g_env_light.mWind : mWindVec (x, y, z), puissance (0x1C)

    class Etat(val partie: Boolean, val vie: Int, val vieMax: Int, val rubis: Int, val magie: Int, val magieMax: Int,
               val chants: Int, val triforce: Int, val perles: Int, val fleches: Int, val bombes: Int, val nom: String,
               val scene: String, val x: Float, val z: Float, val aPos: Boolean,
               // v313 : tout le reste du fichier de sauvegarde, et Link en direct
               val objets: IntArray = IntArray(0),                  // 21 emplacements (numero d'objet, 255 : vide)
               val epee: Int = 255, val bouclier: Int = 255, val bracelet: Int = 255,
               val bourse: Int = 0, val flechesMax: Int = 0, val bombesMax: Int = 0, val photos: Int = 0,
               val butin: List<IntArray> = emptyList(), val appats: List<IntArray> = emptyList(), val courrier: List<IntArray> = emptyList(),   // (objet, nombre)
               val heure: Float = -1f, val jour: Int = 0, val morts: Int = 0,
               val cartes: IntArray = IntArray(0),                  // 49 iles : bits de la carte marine
               val cles: Int = -1, val donjon: Int = 0,             // salle en cours : petites cles, objets du donjon (bits)
               val cap: Double = 0.0, val vitesse: Float = 0f, val y: Float = 0f, val salle: Int = -1,
               val ventX: Float = 0f, val ventZ: Float = 0f, val ventOk: Boolean = false,
               val variante: Int = 0)                               // (v313 : mRandomSalvagePoint : images de la carte)

    /** null : rien de lisible (pas ce jeu, ou Pulse muet) */
    fun lire(l: Lecteur): Etat? {
        val vm = l.u16(G) ?: return null
        val v = l.u16(G + 2) ?: return null
        val scene = l.texte(SCENE, 8) ?: return null
        if (scene.any { it.code < 32 || it.code > 126 }) return null
        if (vm == 0 || vm > 80 || v > vm) return Etat(false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", scene, 0f, 0f, false)
        val rubis = l.u16(G + 4) ?: 0
        val mm = l.u8(G + 0x13) ?: 0
        val m = l.u8(G + 0x14) ?: 0
        val col = G + 0xB4                                             // mCollect : chants, Triforce, perles
        var x = 0f; var z = 0f; var y = 0f; var a = false; var cap = 0.0; var vit = 0f; var salle = -1
        val p = l.ptr(LINK)
        if (p != null) {
            val px = l.fini(l.f32(p + 0x1F8)); val pz = l.fini(l.f32(p + 0x200))   // current.pos
            if (px != null && pz != null) { x = px; z = pz; a = true; y = l.fini(l.f32(p + 0x1FC)) ?: 0f }
            cap = angleJeu(l.s16(p + 0x20E) ?: 0)                                  // shape_angle.y
            vit = (l.fini(l.f32(p + 0x254), 1000f) ?: 0f).let { Math.abs(it) }   // speedF
            salle = l.u8(p + 0x20A)?.toByte()?.toInt() ?: -1                      // current.roomNo
        }
        // le sac : objets (0x76 : butin, appats, courrier, 8 chacun) et leur nombre (0x9C)
        fun sac(o: Int): List<IntArray> {
            val ob = l.plage(G + 0x76 + o, 8) ?: return emptyList()
            val n = l.plage(G + 0x9C + o, 8) ?: IntArray(8)
            return (0 until 8).filter { k -> ob[k] != 0xFF && ob[k] != 0 }.map { k -> intArrayOf(ob[k], n[k]) }
        }
        val eq = l.plage(G + 0x0E, 4) ?: IntArray(4) { 255 }
        val heure = l.fini(l.f32(G + 0x24), 1000f)?.takeIf { it in 0f..360f } ?: -1f      // mPlayerStatusB.mTime (degres)
        var vx = 0f; var vz = 0f; var vok = false
        val wx = l.fini(l.f32(VENT), 10f); val wz = l.fini(l.f32(VENT + 8), 10f)
        if (wx != null && wz != null && (wx * wx + wz * wz) > 0.01f) { vx = wx; vz = wz; vok = true }
        val dj = l.u8(G + 0x778 + 0x21) ?: 0
        return Etat(true, v, vm, rubis.coerceAtMost(9999), m.coerceAtMost(mm), mm,
            Integer.bitCount(l.u8(col + 9) ?: 0), l.u8(col + 10) ?: 0, l.u8(col + 11) ?: 0,
            l.u8(G + 0x69) ?: 0, l.u8(G + 0x6A) ?: 0, (l.texte(G + 0x148 + 0x14, 17) ?: "").filter { it.code in 32..255 },
            scene, x, z, a,
            objets = l.plage(G + 0x3C, 21) ?: IntArray(0),
            epee = eq[0], bouclier = eq[1], bracelet = eq[2], bourse = l.u8(G + 0x12) ?: 0,
            flechesMax = l.u8(G + 0x6F) ?: 0, bombesMax = l.u8(G + 0x70) ?: 0, photos = l.u8(G + 0x68) ?: 0,
            butin = sac(0), appats = sac(8), courrier = sac(16),
            heure = heure, jour = l.u16(G + 0x28) ?: 0, morts = l.u16(G + 0x148 + 0x12) ?: 0,
            cartes = l.plage(G + 0xC4 + 0x40, 49) ?: IntArray(0),
            cles = l.u8(G + 0x778 + 0x20) ?: -1, donjon = dj,
            cap = cap, vitesse = vit, y = y, salle = salle, ventX = vx, ventZ = vz, ventOk = vok,
            variante = l.u8(G + 0x148 + 0x59) ?: 0)
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
    fun donjon(s: String): Boolean = lieu(s).let { it.isNotEmpty() && it != "Grande Mer" && it != "Écran titre" && it != "Château d'Hyrule" }

    /** emplacements de l'inventaire (dInventorySlot_e) : symbole, nom quand l'emplacement est vide */
    val CASES = arrayOf("🔭", "⛵", "🎼", "🪝", "👝", "🪃", "🍃", "📟", "📷", "🥾", "🛡", "👝", "🏹", "💣", "🍶", "🍶", "🍶", "🍶", "✉", "⛓", "🔨")
    val CASES_NOM = arrayOf("Longue-vue", "Voile", "Baguette du Vent", "Grappin", "Sac à butin", "Boomerang", "Feuille Mojo", "Tingle Tuner",
        "Boîte à images", "Bottes de plomb", "Cape magique", "Sac à appâts", "Arc", "Bombes", "Flacon", "Flacon", "Flacon", "Flacon",
        "Sac à courrier", "Lance-chaîne", "Marteau")

    /** noms des objets (numeros dItemNo_e) */
    fun objet(n: Int): String = when (n) {
        0x1F -> "Pendentif de joie"; 0x20 -> "Longue-vue"; 0x21 -> "Tingle Tuner"; 0x22 -> "Baguette du Vent"; 0x23 -> "Boîte à images"
        0x24 -> "Sac à butin"; 0x25 -> "Grappin"; 0x26 -> "Boîte à images de luxe"; 0x27 -> "Arc du héros"; 0x28 -> "Bracelets de force"
        0x29 -> "Bottes de plomb"; 0x2A -> "Cape magique"; 0x2C -> "Sac à appâts"; 0x2D -> "Boomerang"; 0x2F -> "Lance-chaîne"
        0x30 -> "Sac à courrier"; 0x31 -> "Bombes"; 0x33 -> "Marteau"; 0x34 -> "Feuille Mojo"; 0x35 -> "Arc (flèches de feu et de glace)"
        0x36 -> "Arc (flèches de lumière)"; 0x38 -> "Épée du héros"; 0x39 -> "Épée de légende (sans pouvoir)"; 0x3A -> "Épée de légende (à moitié réveillée)"
        0x3E -> "Épée de légende (réveillée)"; 0x3B -> "Bouclier du héros"; 0x3C -> "Bouclier miroir"
        0x45 -> "Collier de crâne"; 0x46 -> "Graine de Boko Baba"; 0x47 -> "Plume dorée"; 0x48 -> "Blason de chevalier"
        0x49 -> "Gelée rouge"; 0x4A -> "Gelée verte"; 0x4B -> "Gelée bleue"
        0x50 -> "Flacon vide"; 0x51 -> "Potion rouge"; 0x52 -> "Potion verte"; 0x53 -> "Potion bleue"; 0x54 -> "Demi-soupe"; 0x55 -> "Soupe d'Élixir"
        0x56 -> "Eau"; 0x57 -> "Fée"; 0x58 -> "Luciole"; 0x59 -> "Eau de la Forêt"; 0x78 -> "Voile"
        0x82 -> "Appât à oiseaux"; 0x83 -> "Poire Hyoi"; in 0x84..0x88 -> "Appât"; 0x89 -> "Haricot magique"
        0x8C -> "Fleur de la ville"; 0x8D -> "Fleur de la mer"; 0x8E -> "Fleur exotique"; 0x8F -> "Drapeau du héros"; 0x90 -> "Drapeau de la grosse prise"
        0x91 -> "Drapeau des soldes"; 0x92 -> "Moulin à vent"; 0x93 -> "Drapeau du croissant"; 0x94 -> "Idole de la tour"; 0x95 -> "Idole de la fontaine"
        0x96 -> "Statue du facteur"; 0x97 -> "Statue du gourou"; 0x98 -> "Lettre du père"; 0x99 -> "Mot pour maman"; 0x9A -> "Lettre de Maggie"
        0x9B -> "Lettre du Moblin"; 0x9C -> "Titre de la cabane"; 0x9D -> "Carte de membre"; 0x9E -> "Bon de remplissage"
        else -> "Objet " + Integer.toHexString(n)
    }
    /** contenu d'un flacon : symbole */
    fun flacon(n: Int): String = when (n) {
        0x50 -> "🍶"; 0x51 -> "❤"; 0x52 -> "💚"; 0x53 -> "💙"; 0x54, 0x55 -> "🍲"; 0x56, 0x59 -> "💧"; 0x57 -> "🧚"; 0x58 -> "✨"; else -> "🍶"
    }
    fun rubisMax(bourse: Int): Int = when (bourse) { 1 -> 1000; 2 -> 5000; else -> 200 }
    /** heure du jeu (0-360 degres) -> "14:05" */
    fun heureTexte(t: Float): String { val mn = (t / 360f * 24f * 60f).toInt().coerceIn(0, 1439); return String.format(java.util.Locale.FRANCE, "%02d:%02d", mn / 60, mn % 60) }
    /** direction (x, z du jeu ; nord = -z) -> nom */
    fun direction(dx: Double, dz: Double): String {
        val a = (Math.toDegrees(Math.atan2(dx, -dz)) + 360.0 + 22.5) % 360.0
        return arrayOf("nord", "nord-est", "est", "sud-est", "sud", "sud-ouest", "ouest", "nord-ouest")[(a / 45.0).toInt().coerceIn(0, 7)]
    }
}

// ---------------------------------------------------------------- Super Mario Sunshine (GMSP01)
object JeuSMS {
    const val ID = "GMSP01"
    private const val FLAGS = 0x80405828L                          // TFlagManager::smInstance
    private const val APP = 0x803E10C0L                            // gpApplication (TApplication)
    private const val MARIO = 0x804057B0L                          // gpMarioOriginal
    private val ZONE_DE = intArrayOf(                              // (StageUtil : numero de scene -> zone a soleils)
        0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 1, 1, 5, 6, 1, 8, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 3, 8,
        4, 4, 5, 5, 5, 5, 5, 5, 6, 5, 7, 7, 8, 8, 2, 2, 3, 3, 5, 6, 9, 1, 1, 2, 6, 8, 5, 3, 9)
    val ZONES = arrayOf("Aéroport Delfino", "Place Delfino", "Colline Bianco", "Port Ricco", "Plage Gelato", "Parc Pinna",
        "Plage Sirena", "Village Pianta", "Baie Noki", "Mont Corona")

    /** v313 : les soleils de chaque zone (numeros des drapeaux MSF_SHINE_*, FlagManagerFlags.hpp) : 8 episodes, puis
     *  les soleils en plus (pieces rouges, secrets, 100 pieces) */
    class ZoneSoleils(val nom: String, val couleur: Long, val soleils: IntArray, val bleues: Int, val bleuesMax: Int, val record: Int)
    private val SOL_ZONE = arrayOf(
        intArrayOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x64), intArrayOf(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 0x65),
        intArrayOf(20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 0x66), intArrayOf(30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 0x67),
        intArrayOf(40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 0x68), intArrayOf(60, 65, 62, 61, 64, 63, 66, 67, 68, 69, 0x6A),
        intArrayOf(50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 0x69))
    private val ZONE_NOMS = arrayOf("Colline Bianco", "Port Ricco", "Plage Gelato", "Parc Pinna", "Plage Sirena", "Village Pianta", "Baie Noki")
    private val ZONE_COUL = longArrayOf(0xFF7CD957, 0xFFFFB04A, 0xFF5FD3FF, 0xFFFF6FA8, 0xFFFFE066, 0xFFFF7A4A, 0xFF4FA3FF)
    private val BLEUE_ZONE = intArrayOf(50, 100, 150, 200, 250, 300, 350)   // (pieces bleues : 120 + ce rang ; 30 par zone, Delfino 0-19, Corona 420-429)
    private val RECORD_ZONE = intArrayOf(0x8C + 8, 0x8C + 12, 0x8C + 16, 0x8C + 20, 0x8C + 24, 0x8C + 28, 0x8C + 32)   // (MSF_COIN_RECORD_*, mCardInts)
    // Delfino : soleils de la place, de l'aeroport, des pieces bleues (24)
    private val SOL_DELFINO = intArrayOf(0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x74, 0x75, 0x76, 0x6B)
    private const val SOL_CORONA = 0x77
    private val SOL_BLEUES = (0x46..0x55).toList().plus(0x6C..0x73).toIntArray()
    val BUSES = arrayOf("Arrosage", "Fusée", "Sniper", "Yoshi", "Planeur", "Turbo")

    class Etat(val enJeu: Boolean, val soleils: Int, val bleues: Int, val pieces: Int, val vies: Int, val sante: Int,
               val eau: Float, val scene: Int, val zone: Int, val episode: Int, val secret: Boolean,
               val zones: List<ZoneSoleils> = emptyList(), val delfino: ZoneSoleils? = null, val achats: ZoneSoleils? = null, val corona: ZoneSoleils? = null,
               val buse: Int = -1, val buse2: Int = -1, val yoshi: Int = -1, val yoshiDebloque: Boolean = false,
               val fusee: Boolean = false, val turbo: Boolean = false, val rouges: Int = -1,
               val x: Float = 0f, val y: Float = 0f, val z: Float = 0f, val cap: Double = 0.0, val vitesse: Float = 0f, val aPos: Boolean = false)

    private fun bit(d: IntArray, n: Int): Boolean = n >= 0 && n / 8 < d.size && (d[n / 8] shr (n and 7)) and 1 != 0

    fun lire(l: Lecteur): Etat? {
        val f = l.ptr(FLAGS) ?: return null
        val soleils = l.s32(f + 0xD0) ?: return null                 // MSF_SHINE_COUNT (0x40000)
        val bleues = l.s32(f + 0xD4) ?: return null                  // MSF_BLUE_COIN_COUNT
        if (soleils !in 0..120 || bleues !in 0..240) return null
        val pieces = (l.s32(f + 0xD8) ?: 0).coerceIn(0, 999)         // MSF_GOLD_COIN_COUNT
        val vies = (l.s32(f + 0x7C) ?: 0).coerceIn(0, 99)            // MSF_LIFE_COUNT (0x20001)
        val etat = l.u8(APP + 8) ?: 0
        val scene = l.u8(APP + 0xE) ?: 0
        val ep = l.u8(APP + 0xF) ?: 0
        var sante = -1; var eau = -1f; var buse = -1; var buse2 = -1; var yo = -1
        var x = 0f; var y = 0f; var z = 0f; var cap = 0.0; var vit = 0f; var aPos = false
        val m = l.ptr(MARIO)
        if (m != null) {
            sante = l.s16(m + 0x120)?.takeIf { it in 0..8 } ?: -1
            val px = l.fini(l.f32(m + 0x10)); val py = l.fini(l.f32(m + 0x14)); val pz = l.fini(l.f32(m + 0x18))   // (JDrama::TPlacement : mPosition)
            if (px != null && py != null && pz != null) { x = px; y = py; z = pz; aPos = true }
            cap = angleJeu(l.s16(m + 0x96) ?: 0)                       // mFaceAngle.y
            vit = Math.abs(l.fini(l.f32(m + 0xB0), 1000f) ?: 0f)      // mForwardVel
            val g = l.ptr(m + 0x3E4)                                   // mWaterGun
            if (g != null) {
                eau = ((l.s32(g + 0x1C80) ?: -1) / 10500f).takeIf { it in 0f..1.2f }?.coerceAtMost(1f) ?: -1f
                buse = l.u8(g + 0x1C84)?.takeIf { it in 0..5 } ?: -1
                buse2 = l.u8(g + 0x1C85)?.takeIf { it in 0..5 } ?: -1
            }
            val yp = l.ptr(m + 0x3F0)                                  // mYoshi
            if (yp != null) yo = l.u8(yp)?.takeIf { it in 0..8 } ?: -1
        }
        // les drapeaux de la carte : 119 octets (soleils : bits 0 a 119 ; pieces bleues : 120 + 50 x bloc)
        val d = l.plage(f, 119)
        var zones: List<ZoneSoleils> = emptyList(); var delf: ZoneSoleils? = null; var ach: ZoneSoleils? = null; var cor: ZoneSoleils? = null
        var fusee = false; var turbo = false; var yd = false
        if (d != null) {
            fun nb(de: Int, max: Int): Int { var n = 0; for (k in 0 until max) if (bit(d, 120 + de + k)) n++; return n }
            fun sols(t: IntArray) = t.map { if (bit(d, it)) 1 else 0 }.toIntArray()
            zones = (0 until 7).map { k ->
                ZoneSoleils(ZONE_NOMS[k], ZONE_COUL[k], sols(SOL_ZONE[k]), nb(BLEUE_ZONE[k], 30), 30, (l.s32(f + RECORD_ZONE[k]) ?: 0).coerceIn(0, 999))
            }
            delf = ZoneSoleils("Place Delfino", 0xFFFFD54A, sols(SOL_DELFINO), nb(0, 20), 20, (l.s32(f + 0x8C + 4) ?: 0).coerceIn(0, 999))
            cor = ZoneSoleils("Mont Corona", 0xFFFF5A3C, sols(intArrayOf(SOL_CORONA)), nb(420, 10), 10, (l.s32(f + 0x8C + 36) ?: 0).coerceIn(0, 999))
            ach = ZoneSoleils("Pièces bleues échangées", 0xFF4FA3FF, sols(SOL_BLEUES), 0, 0, 0)
            for (k in 0 until 9) { if (bit(d, 0x366 + 2 * k)) fusee = true; if (bit(d, 0x367 + 2 * k)) turbo = true }   // (MSF_NOZZLE_ROCKET/TURBO_*)
            yd = bit(d, 0x38F)                                          // MSF_YOSHI_UNLOCKED
        }
        val zone = if (scene < ZONE_DE.size) ZONE_DE[scene] else 1
        return Etat(etat == 5, soleils, bleues, pieces, vies, sante, eau, scene, zone, ep, scene in 0x15..0x34,
            zones, delf, ach, cor, buse, buse2, yo, yd, fusee, turbo, (l.s32(f + 0xF4) ?: -1).takeIf { it in 0..8 } ?: -1,   // (MSF_RED_COIN_COUNT : mStageInts[0])
            x, y, z, cap, vit, aPos)
    }
}

// ---------------------------------------------------------------- Mario Kart Double Dash!! (GM4P01)
object JeuMKDD {
    const val ID = "GM4P01"
    private const val RACE = 0x803D5628L                           // RaceMgr::sRaceManager
    private const val OBJETS = 0x803D5D80L                         // ItemObjMgr::gItemMgr
    const val PAS_DE_TEMPS = 5999999

    class Kart(val n: Int, val x: Float, val z: Float, val place: Int,
               // v313 : pilotes, kart, objets, progression, bataille
               val pilote: Int = 0, val passager: Int = 0, val modele: Int = -1, val joueur: Boolean = false,
               val objet1: Int = 16, val objet2: Int = 16, val progres: Float = -1f, val tour: Int = 0, val fini: Boolean = false,
               val ballons: Int = -1, val points: Int = -1)
    class Etat(val enCourse: Boolean, val fini: Boolean, val place: Int, val tour: Int, val tours: Int, val progres: Float,
               val tempsTours: List<Int>, val total: Int, val karts: List<Kart> = emptyList(), val circuit: Int = 0,
               val mode: Int = 0, val niveau: Int = -1, val coupe: Int = -1, val miroir: Boolean = false, val nbKarts: Int = 0, val meilleurTour: Int = -1,
               val manche: Int = 0, val recordTour: Int = PAS_DE_TEMPS, val recordTotal: Int = PAS_DE_TEMPS, val ecarts: Map<Int, Float> = emptyMap())

    val HORS_COURSE = Etat(false, false, 0, 0, 0, 0f, emptyList(), PAS_DE_TEMPS)

    /** noms des circuits (ECourseID) */
    fun nomCircuit(id: Int): String = when (id) {
        0x21 -> "Baby Park"; 0x22 -> "Plage Peach"; 0x23 -> "Croisière Daisy"; 0x24 -> "Circuit Luigi"; 0x25 -> "Circuit Mario"
        0x26 -> "Circuit Yoshi"; 0x28 -> "Pont Champignon"; 0x29 -> "Ville Champignon"; 0x2A -> "Stade Waluigi"; 0x2B -> "Colisée Wario"
        0x2C -> "Jungle Dino Dino"; 0x2D -> "Montagne DK"; 0x2F -> "Château de Bowser"; 0x31 -> "Route Arc-en-ciel"; 0x32 -> "Désert Sec Sec"
        0x33 -> "Royaume Sorbet"; else -> ""
    }
    /** personnages (ECharID) : nom, couleur */
    val PERSOS = arrayOf("", "Bébé Mario", "Bébé Luigi", "Para-Koopa", "Koopa", "Peach", "Daisy", "Mario", "Luigi", "Wario", "Waluigi",
        "Yoshi", "Birdo", "Donkey Kong", "Diddy Kong", "Bowser", "Bowser Jr.", "Toad", "Toadette", "Roi Boo", "Petey Piranha")
    val PERSO_COUL = longArrayOf(0xFFB8C7FF, 0xFFFF5A5A, 0xFF4CD964, 0xFFFF9F40, 0xFF4CD964, 0xFFFF8FC8, 0xFFFFC94A, 0xFFFF4040, 0xFF2FD16B,
        0xFFFFD84A, 0xFF9B6BFF, 0xFF5BE05B, 0xFFFF7AB8, 0xFFB5793F, 0xFFE0573F, 0xFFFFB347, 0xFFF5A623, 0xFF4F8BFF, 0xFFFF9ED2, 0xFFE8E8F0, 0xFF3FBF6F)
    val KARTS = arrayOf("Bolide Rouge", "DK Jumbo", "Turbo Yoshi", "Koopa Dasher", "Carrosse Cœur", "Goo-Goo Buggy", "Wario Car", "Koopa King",
        "Bolide Vert", "Barrel Train", "Turbo Birdo", "Para-Wing", "Bloom Coach", "Rattle Buggy", "Waluigi Racer", "Bullet Blaster",
        "Toad Kart", "Toadette Kart", "Boo Pipes", "Piranha Pipes", "Kart de la parade")
    /** objets (numeros de ItemObjMgr) : symbole, nom ; 16 : aucun */
    fun objet(k: Int): Pair<String, String>? = when (k) {
        0 -> "🟢" to "Carapace verte"; 1 -> "🟢" to "Carapace verte géante"; 2 -> "🔴" to "Carapace rouge"; 3 -> "🍌" to "Peau de banane"
        4 -> "🍌" to "Banane géante"; 5 -> "🍄" to "Champignon"; 6 -> "⭐" to "Étoile"; 7 -> "⛓" to "Chomp"; 8 -> "💣" to "Bob-omb"
        9 -> "🔥" to "Boule de feu"; 10 -> "⚡" to "Éclair"; 11 -> "🥚" to "Œuf de Yoshi"; 12 -> "🌟" to "Champignon doré"
        13 -> "🔵" to "Carapace à épines"; 14 -> "❤" to "Cœurs"; 15 -> "❓" to "Fausse boîte"; 17 -> "🟢" to "3 carapaces vertes"
        18 -> "🍄" to "3 champignons"; 19 -> "🔴" to "3 carapaces rouges"; 20, 21 -> "🔥" to "Boules de feu"
        else -> null
    }
    val MODES = arrayOf("", "Contre-la-montre", "Grand Prix", "Course libre", "Bataille de ballons", "Voleur de Soleil", "Bataille de Bob-ombs",
        "Bataille", "Remise des prix", "Générique")
    val NIVEAUX = arrayOf("50cc", "100cc", "150cc", "Miroir")
    val COUPES = arrayOf("Coupe Champignon", "Coupe Fleur", "Coupe Étoile", "Coupe Spéciale", "Toutes les coupes")

    /** le trace du circuit : bords gauche et droit de chaque troncon (x1, z1, x2, z2), lu une fois par course */
    @Volatile var trace: List<FloatArray> = emptyList()
    @Volatile var depart = -1                                       // (troncon de la ligne d'arrivee dans trace)
    @Volatile private var dernierCircuit = -1
    @Volatile private var essai = 0L
    // ecart en secondes avec les autres : difference d'avancee / avancee du kart 1 par seconde (lissee)
    private var progresAvant = -1f
    private var progresT = 0L
    private var avance = 0f
    // pilotes et karts : lus une fois par course (ils ne changent pas pendant la course)
    @Volatile private var equipes: List<IntArray> = emptyList()      // (pilote, passager, kart, joueur)
    @Volatile private var equipesCle = -1L
    @Volatile private var equipesT = 0L

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
        val tour = pts.subList(boucle, pts.size).toList()
        trace = tour; depart = ids.subList(boucle, ids.size).indexOf(0)
    }

    /** RaceInfo.mKartInfo[i] (0x30 + 0x18 i) : mKartDB (SKartDB : id), mKartCharacters[2] (mKartGamePad, mCharDB : id) */
    private fun lireEquipes(l: Lecteur, ri: Long, n: Int): List<IntArray>? {
        val r = ArrayList<IntArray>()
        for (i in 0 until n) {
            val k = ri + 0x30 + 0x18L * i
            val db = l.ptr(k)
            val modele = if (db != null) (l.s32(db) ?: -1).takeIf { it in 0..20 } ?: -1 else -1
            val c1 = l.ptr(k + 8)?.let { l.u16(it) }?.takeIf { it in 1..20 } ?: 0
            val c2 = l.ptr(k + 0x10)?.let { l.u16(it) }?.takeIf { it in 1..20 } ?: 0
            val pad = (l.u32(k + 4) ?: 0L) != 0L || (l.u32(k + 0xC) ?: 0L) != 0L
            if (c1 == 0 && c2 == 0 && modele < 0) return null
            r.add(intArrayOf(c1, c2, modele, if (pad) 1 else 0))
        }
        return r
    }

    fun lire(l: Lecteur): Etat? {
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
        // la course : RaceInfo (mode 8, coupe 0xC, niveau 0x10, nombre de karts 0x1C, miroir 0x2C)
        val ri = l.ptr(rm + 0x38)
        val mode = ri?.let { l.s32(it + 8) }?.takeIf { it in 1..9 } ?: 0
        val coupe = ri?.let { l.s32(it + 0xC) }?.takeIf { it in 0..4 } ?: -1
        val niveau = ri?.let { l.s32(it + 0x10) }?.takeIf { it in 0..3 } ?: -1
        val nb = ri?.let { l.s16(it + 0x1C) }?.takeIf { it in 1..8 } ?: 8
        val miroir = ri?.let { l.u8(it + 0x2C) } == 1
        val crs = l.ptr(rm + 0x44)?.let { l.s32(it) } ?: 0
        val maintenant = SystemClock.elapsedRealtime()
        // pilotes : relus seulement quand la course change (ou toutes les 10 s)
        val cle = (crs.toLong() shl 16) or (mode.toLong() shl 8) or nb.toLong()
        if (ri != null && (cle != equipesCle || equipes.isEmpty() || maintenant - equipesT > 10000)) {
            val e = lireEquipes(l, ri, nb)
            if (e != null) { equipes = e; equipesCle = cle; equipesT = maintenant }
        }
        val eqs = equipes
        val im = l.ptr(OBJETS)
        // les karts (KartChecker : mLap 0x2C, mRaceEnd 0x29, mRaceProgression 0x54, mPos 0x5C, mRank 0x88, mBalloonNum 0x90,
        // mBombPoint 0xA6) ; leurs objets (ItemObjMgr : mStockItem[8][2] 0x24C, mEquipItem[8][2] 0x2CC -> mItemKind 0x7C)
        val karts = ArrayList<Kart>()
        for (i in 0 until nb) {
            val k = l.ptr(rm + 0x48 + 4L * i) ?: continue
            val x = l.fini(l.f32(k + 0x5C)) ?: continue
            val z = l.fini(l.f32(k + 0x64)) ?: continue
            if (x == 0f && z == 0f) continue
            fun objetDe(d: Int): Int {
                if (im == null) return 16
                val eq = l.ptr(im + 0x2CC + 8L * i + 4L * d)
                if (eq != null) { val kd = l.s32(eq + 0x7C); if (kd != null && kd in 0..21) return kd }
                return (l.s32(im + 0x24C + 16L * i + 8L * d) ?: 16).takeIf { it in 0..21 } ?: 16
            }
            val e = eqs.getOrNull(i)
            karts.add(Kart(i, x, z, (l.s32(k + 0x88) ?: 0).takeIf { it in 1..8 } ?: 0,
                pilote = e?.get(0) ?: 0, passager = e?.get(1) ?: 0, modele = e?.get(2) ?: -1, joueur = (e?.get(3) ?: 0) == 1 || i == 0,
                objet1 = objetDe(0), objet2 = objetDe(1),
                progres = l.fini(l.f32(k + 0x54), 1000f) ?: -1f, tour = l.s32(k + 0x2C) ?: 0, fini = (l.u8(k + 0x29) ?: 0) != 0,
                ballons = if (mode == 4) (l.s16(k + 0x90) ?: -1).takeIf { it in 0..9 } ?: -1 else -1,
                points = if (mode == 6) (l.s16(k + 0xA6) ?: -1).takeIf { it in 0..99 } ?: -1 else -1))
        }
        val s0 = l.ptr(kc + 0x44) ?: l.ptr(kc + 0x40)               // (troncon ou se trouve le kart 1)
        if (s0 != null && (crs != dernierCircuit || (trace.isEmpty() && maintenant - essai > 3000))) {
            dernierCircuit = crs; essai = maintenant; trace = emptyList(); lireTrace(Lecteur(700, 15), s0)   // (une fois par course, au rythme d'un bloc par image)
        }
        val best = l.s32(kc + 0x10)?.takeIf { it in 0 until lt.size } ?: -1
        val moi = karts.firstOrNull { it.n == 0 }
        if (moi != null && moi.progres >= 0f && enCourse && !fini) {
            if (progresAvant >= 0f && maintenant > progresT) {
                val v = (moi.progres - progresAvant) / ((maintenant - progresT) / 1000f)
                if (v > 0f && v < 1f) avance = if (avance <= 0f) v else avance * 0.8f + v * 0.2f
            }
            progresAvant = moi.progres; progresT = maintenant
        } else progresAvant = -1f
        val ecarts = HashMap<Int, Float>()
        if (moi != null && avance > 0f && e2(mode)) for (k in karts) if (k.n != 0 && !k.fini && k.progres >= 0f) ecarts[k.n] = (k.progres - moi.progres) / avance
        fun temps(v: Int?) = if (v != null && v in 1 until PAS_DE_TEMPS) v else PAS_DE_TEMPS
        return Etat(enCourse || fini, fini, place, tour, tours, prog, lt, total, karts, crs, mode, niveau, coupe, miroir, nb, best,
            ri?.let { l.s16(it + 0x26) }?.takeIf { it in 0..15 } ?: 0,                // (mGpStageNo : course de la coupe)
            temps(l.s32(rm + 0x94)), temps(l.s32(rm + 0x98)), ecarts)                   // (mBestLapTime, mBestTotalTimes[0])
    }
    private fun e2(mode: Int) = mode in 1..3                       // (courses : pas les batailles)
}

// ---------------------------------------------------------------- la lecture en direct
object Compagnon {
    val PRIS = mapOf(JeuWW.ID to "The Wind Waker", JeuSMS.ID to "Super Mario Sunshine", JeuMKDD.ID to "Mario Kart Double Dash!!")
    var jeu by mutableStateOf("")                                     // (code du jeu lu par Pulse)
    var etat by mutableStateOf<Any?>(null)
    var statut by mutableStateOf("Connexion à Pulse…")
    var ecranOuvert = false
    var ctx: Context? = null
    private var job: Job? = null
    private val scope = MainScope()
    val traceWW = mutableStateListOf<Offset>()                        // (le sillage de Link sur la mer)

    fun pris(id: String?) = id != null && PRIS.containsKey(id)

    fun demarrer() {
        if (job?.isActive == true) return
        job = scope.launch {
            var muet = 0; var idLu = 0L
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
                val r = withContext(Dispatchers.IO) {
                    val l = Lecteur(28)
                    when (id) { JeuWW.ID -> JeuWW.lire(l); JeuSMS.ID -> JeuSMS.lire(l); else -> JeuMKDD.lire(l) }
                }
                if (r == null) statut = "Valeurs illisibles : est-ce bien la version européenne du jeu ($id) ?"
                else {
                    statut = ""
                    if (r is JeuWW.Etat && r.aPos && r.scene == "sea") {
                        val o = Offset(r.x, r.z)
                        val d = traceWW.lastOrNull()
                        if (d == null || (d - o).getDistance() > 1500f) { traceWW.add(o); if (traceWW.size > 400) traceWW.removeAt(0) }
                    }
                    if (r is JeuWW.Etat && r.partie && r.aPos && r.scene != "sea") PlanWW.ajouter(ctx, r.scene, r.x, r.y, r.z, r.salle)   // (v313 : plan des interieurs)
                    Chrono.observer(etat, r)
                    etat = r
                }
                val pris = SystemClock.elapsedRealtime() - t0
                delay((300 - pris).coerceAtLeast(80))                       // (environ 3 lectures par seconde : le jeu ne le sent pas)
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
    LaunchedEffect(Unit) {
        Chrono.init(act); Compagnon.ctx = act.applicationContext; Compagnon.ecranOuvert = true; Compagnon.demarrer()
        CarteMarineWW.telecharger(act)                                   // (v313 : hors partie seulement)
        withContext(Dispatchers.Default) { CarteMarineWW.charger(act) }
    }
    DisposableEffect(Unit) { onDispose { Compagnon.ecranOuvert = false; PlanWW.enregistrer(Compagnon.ctx) } }
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
        Text(valeur, color = couleur, fontSize = 22.sp, fontWeight = FontWeight.Black, maxLines = 1)
        Doux(nom, taille = 11, lignes = 1)
    }
}

@Composable
private fun Barre(part: Float, couleur: Color, hauteur: Int = 10) {
    Box(Modifier.fillMaxWidth().height(hauteur.dp).clip(RoundedCornerShape((hauteur / 2).dp)).background(C.Verre)) {
        Box(Modifier.fillMaxWidth(part.coerceIn(0f, 1f)).height(hauteur.dp).clip(RoundedCornerShape((hauteur / 2).dp))
            .background(Brush.horizontalGradient(listOf(couleur.copy(alpha = 0.65f), couleur))))
    }
}

/** une carte avec un fond en degrade (couleurs du jeu) */
@Composable
private fun CarteJeu(haut: Color, bas: Color, content: @Composable () -> Unit) {
    Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(22.dp))
        .background(Brush.verticalGradient(listOf(haut, bas))).padding(16.dp)) { content() }
}

/** une case d'inventaire : symbole, nom en dessous ; pale si vide */
@Composable
private fun Case(sym: String, nom: String, actif: Boolean, modifier: Modifier = Modifier, nombre: String = "") {
    Column(modifier.clip(RoundedCornerShape(12.dp)).background(if (actif) Color(0x2EFFFFFF) else Color(0x0FFFFFFF)).padding(vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Box(contentAlignment = Alignment.BottomEnd) {
            Text(sym, fontSize = 22.sp, color = if (actif) Color.White else Color(0x40FFFFFF), modifier = Modifier.padding(horizontal = 4.dp))
            if (nombre.isNotEmpty()) Text(nombre, color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.Black,
                modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(Color(0xCC000000)).padding(horizontal = 3.dp))
        }
        Text(nom, color = if (actif) C.Texte else C.Pale, fontSize = 9.sp, maxLines = 1, modifier = Modifier.padding(horizontal = 2.dp))
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

/** forme d'ile (toujours la meme pour une case) autour de c, rayon r */
private fun ile(c: Offset, r: Float, graine: Int): Path = Path().apply {
    val rnd = java.util.Random(graine * 7919L + 13)
    val n = 9
    val pts = (0 until n).map { k ->
        val a = k * 2 * Math.PI / n
        val rr = r * (0.62f + rnd.nextFloat() * 0.45f)
        Offset(c.x + (Math.cos(a) * rr).toFloat(), c.y + (Math.sin(a) * rr).toFloat())
    }
    moveTo((pts[0].x + pts[n - 1].x) / 2, (pts[0].y + pts[n - 1].y) / 2)
    for (k in 0 until n) { val p = pts[k]; val q = pts[(k + 1) % n]; quadraticBezierTo(p.x, p.y, (p.x + q.x) / 2, (p.y + q.y) / 2) }
    close()
}

/** fleche (pointe vers l'angle a : 0 = vers le bas de l'ecran, comme +z dans le jeu) */
private fun DrawScope.fleche(c: Offset, a: Double, l: Float, couleur: Color) {
    val dx = Math.sin(a).toFloat(); val dz = Math.cos(a).toFloat()
    val pointe = Offset(c.x + dx * l, c.y + dz * l)
    val g = Offset(c.x - dz * l * 0.45f - dx * l * 0.35f, c.y + dx * l * 0.45f - dz * l * 0.35f)
    val d = Offset(c.x + dz * l * 0.45f - dx * l * 0.35f, c.y - dx * l * 0.45f - dz * l * 0.35f)
    val p = Path().apply { moveTo(pointe.x, pointe.y); lineTo(g.x, g.y); lineTo(c.x - dx * l * 0.1f, c.y - dz * l * 0.1f); lineTo(d.x, d.y); close() }
    drawPath(p, couleur)
    drawPath(p, Color.White, style = Stroke(1.2f))
}

// ---------------------------------------------------------------- The Wind Waker
@Composable
private fun CompagnonWW(e: JeuWW.Etat) {
    if (!e.partie) { Carte(Modifier.fillMaxWidth()) { Doux("Aucune partie chargée (écran titre ou choix du fichier).", couleur = C.Texte) }; return }
    // Link : coeurs, magie, rubis, munitions
    CarteJeu(Color(0xFF0E5B3A), Color(0xFF0A2E3F)) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(if (e.nom.isNotEmpty()) e.nom else "Link", color = Color(0xFFB9FF8A), fontSize = 24.sp, fontWeight = FontWeight.Black)
                Spacer(Modifier.weight(1f))
                if (e.heure >= 0f) Column(horizontalAlignment = Alignment.End) {
                    Text(JeuWW.heureTexte(e.heure), color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                    Doux("jour " + (e.jour + 1), taille = 10)
                }
            }
            val n = (e.vieMax + 3) / 4
            Canvas(Modifier.fillMaxWidth().height(if (n > 10) 60.dp else 30.dp)) {
                val s = minOf(size.width / 10f, 30.dp.toPx()) * 0.9f
                val pas = s / 0.9f
                for (i in 0 until n) {
                    val q = (e.vie - i * 4).coerceIn(0, 4)
                    val o = Offset((i % 10) * pas, (i / 10) * (pas + 2.dp.toPx()))
                    val p = coeur(s); p.translate(o)
                    drawPath(p, Color(0x40000000))
                    if (q > 0) clipPath(p) { drawArc(Brush.verticalGradient(listOf(Color(0xFFFF7A8A), Color(0xFFE0103A)), o.y, o.y + s), -90f, 90f * q, true, o, Size(s, s)) }
                    drawPath(p, Color(0xDDFFFFFF), style = Stroke(1.4.dp.toPx()))
                }
            }
            val quarts = e.vieMax % 4
            Doux((e.vie / 4f).let { if (it % 1f == 0f) it.toInt().toString() else String.format(java.util.Locale.FRANCE, "%.2f", it) } + " / " + e.vieMax / 4 + " cœurs"
                + (if (quarts > 0) " · $quarts quart" + (if (quarts > 1) "s" else "") + " de cœur" else "") + (if (e.morts > 0) " · " + e.morts + " K.O." else ""), taille = 11, couleur = Color(0xCCFFFFFF))
            if (e.magieMax > 0) { Doux("Magie " + e.magie + " / " + e.magieMax, taille = 11, couleur = Color(0xCCFFFFFF)); Barre(e.magie / e.magieMax.toFloat(), Color(0xFF6BFF6B)) }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Jauge("rubis / " + JeuWW.rubisMax(e.bourse), e.rubis.toString(), Color(0xFF6BFF8A), Modifier.weight(1f))
                Jauge("flèches" + if (e.flechesMax > 0) " / " + e.flechesMax else "", e.fleches.toString(), Color.White, Modifier.weight(1f))
                Jauge("bombes" + if (e.bombesMax > 0) " / " + e.bombesMax else "", e.bombes.toString(), Color.White, Modifier.weight(1f))
            }
            // equipement : epee, bouclier, bracelets
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Case("🗡", if (e.epee != 255) JeuWW.objet(e.epee).replace("Épée de légende", "Ép. légende") else "Épée", e.epee != 255, Modifier.weight(1.3f))
                Case(if (e.bouclier == 0x3C) "🪞" else "🛡", if (e.bouclier != 255) JeuWW.objet(e.bouclier) else "Bouclier", e.bouclier != 255, Modifier.weight(1f))
                Case("💪", "Bracelets", e.bracelet != 255, Modifier.weight(1f))
            }
        }
    }
    // l'inventaire : 21 emplacements (3 rangees de 7)
    if (e.objets.size == 21) Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Titre("Inventaire", couleur = Color(0xFFB9FF8A))
            for (r in 0 until 3) Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                for (c in 0 until 7) {
                    val k = r * 7 + c
                    val ob = e.objets[k]
                    val a = ob != 0xFF && ob != 0
                    val sym = if (k in 14..17 && a) JeuWW.flacon(ob) else JeuWW.CASES[k]
                    val nom = if (a) JeuWW.objet(ob) else JeuWW.CASES_NOM[k]
                    Case(sym, nom, a, Modifier.weight(1f), nombre = when { !a -> ""; k == 12 -> e.fleches.toString(); k == 13 -> e.bombes.toString(); k == 8 && e.photos > 0 -> e.photos.toString(); else -> "" })
                }
            }
            for ((nom, sac) in listOf("Sac à butin" to e.butin, "Sac à appâts" to e.appats, "Sac à courrier" to e.courrier)) if (sac.isNotEmpty())
                Doux(nom + " : " + sac.joinToString(" · ") { JeuWW.objet(it[0]) + if (it[1] > 1) " ×" + it[1] else "" }, taille = 11)
        }
    }
    // la quete : perles, Triforce, chants
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Titre("Quête", couleur = Color(0xFFFFD54A))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Canvas(Modifier.size(110.dp, 96.dp)) {                   // la Triforce : 8 fragments (4 par triangle bas, 2 x 2 en haut)
                    val w = size.width; val h = size.height
                    fun tri(a: Offset, b: Offset, c: Offset, on: Boolean) {
                        val p = Path().apply { moveTo(a.x, a.y); lineTo(b.x, b.y); lineTo(c.x, c.y); close() }
                        drawPath(p, if (on) Color(0xFFFFD54A) else Color(0x22FFFFFF))
                        drawPath(p, Color(0x88FFE9A0), style = Stroke(1.dp.toPx()))
                    }
                    val t = e.triforce
                    val s0 = Offset(w / 2, 0f); val g0 = Offset(0f, h); val d0 = Offset(w, h)
                    val m1 = Offset(w / 4, h / 2); val m2 = Offset(3 * w / 4, h / 2); val m3 = Offset(w / 2, h)
                    // chaque grand triangle coupe en deux : 2 fragments
                    tri(s0, m1, Offset(w / 2, h / 2), t and 1 != 0); tri(s0, Offset(w / 2, h / 2), m2, t and 2 != 0)
                    tri(m1, g0, Offset(w / 4, h), t and 4 != 0); tri(m1, Offset(w / 4, h), m3, t and 8 != 0)
                    tri(m2, m3, Offset(3 * w / 4, h), t and 16 != 0); tri(m2, Offset(3 * w / 4, h), d0, t and 32 != 0)
                    tri(m1, m2, m3, t and 64 != 0); tri(Offset(w / 2, h / 2), m1, m2, t and 128 != 0)
                }
                Spacer(Modifier.width(14.dp))
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("Triforce " + Integer.bitCount(e.triforce) + " / 8", color = if (e.triforce == 255) Color(0xFFFFD54A) else C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold)
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        for ((k, c) in listOf(Color(0xFF4FA3FF), Color(0xFFFF4D4D), Color(0xFF3DDC84)).withIndex()) {
                            val a = e.perles and (1 shl k) != 0
                            Box(Modifier.padding(end = 6.dp).size(18.dp).clip(RoundedCornerShape(9.dp))
                                .background(if (a) Brush.radialGradient(listOf(Color.White, c)) else Brush.linearGradient(listOf(Color(0x22FFFFFF), Color(0x22FFFFFF)))))
                        }
                        Doux("perles", taille = 11)
                    }
                    Row { for (k in 0 until 6) Text("♪", color = if (k < e.chants) Color(0xFFB9FF8A) else Color(0x33FFFFFF), fontSize = 18.sp, fontWeight = FontWeight.Black) }
                    Doux("chants " + e.chants + " / 6", taille = 11)
                }
            }
        }
    }
    // le donjon en cours
    if (JeuWW.donjon(e.scene) && e.cles >= 0) Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Titre(JeuWW.lieu(e.scene), couleur = Color(0xFFFF9F6B))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Case("🗝", "petites clés", e.cles > 0, Modifier.weight(1f), nombre = if (e.cles > 0) e.cles.toString() else "")
                Case("🗺", "carte", e.donjon and 1 != 0, Modifier.weight(1f))
                Case("🧭", "boussole", e.donjon and 2 != 0, Modifier.weight(1f))
                Case("🔑", "grande clé", e.donjon and 4 != 0, Modifier.weight(1f))
                Case("👹", "boss vaincu", e.donjon and 8 != 0, Modifier.weight(1f))
            }
        }
    }
    // v313 : a l'interieur (donjon, maison, ile) : le plan qui se dessine au fil des pas de Link
    if (e.scene != "sea" && e.scene != "sea_T" && e.aPos) PlanInterieurWW(e)
    // la carte marine (v313 : les images du vrai jeu quand l'appli les a ; toucher une case : son nom)
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            val l = JeuWW.lieu(e.scene)
            val mer = e.scene == "sea"
            val ici = if (mer && e.aPos) JeuWW.case(e.x, e.z) else -1
            var touchee by remember { mutableStateOf(-1) }
            Titre("Carte marine", couleur = Color(0xFF7FD4FF))
            Text(when {
                ici >= 0 -> JeuWW.nomCase(ici) + " · " + JeuWW.ile(ici)
                l.isNotEmpty() -> l
                else -> "Ailleurs (" + e.scene + ")"
            }, color = C.Texte, fontSize = 17.sp, fontWeight = FontWeight.Bold)
            val connues = e.cartes.count { it != 0 }
            if (e.cartes.size == 49) Doux("$connues îles sur 49 sur la carte" + if (mer && e.vitesse > 1f) " · vitesse " + e.vitesse.toInt() else "", taille = 11)
            val trace = Compagnon.traceWW
            val vraie = CarteMarineWW.tuiles.isNotEmpty()
            Canvas(Modifier.fillMaxWidth().aspectRatio(1f).pointerInput(Unit) {
                detectTapGestures { o ->
                    val marge = 18.dp.toPx()
                    val c = (size.width - marge) / 7f
                    val cx = ((o.x - marge) / c).toInt(); val cy = ((o.y - marge) / c).toInt()
                    touchee = if (o.x >= marge && o.y >= marge && cx in 0..6 && cy in 0..6) { val k = cy * 7 + cx; if (touchee == k) -1 else k } else -1
                }
            }) {
                val marge = 18.dp.toPx()
                val c = (size.width - marge) / 7f
                val paint = android.graphics.Paint().apply { isAntiAlias = true; textSize = 11.dp.toPx(); color = android.graphics.Color.argb(200, 90, 60, 30); textAlign = android.graphics.Paint.Align.CENTER; typeface = android.graphics.Typeface.SERIF }
                // parchemin et mer
                drawRoundRect(Brush.radialGradient(listOf(Color(0xFFF3E2B8), Color(0xFFD9BE86)), Offset(size.width / 2, size.height / 2), size.width * 0.75f), Offset.Zero, size, CornerRadius(12.dp.toPx()))
                drawRoundRect(Brush.radialGradient(listOf(Color(0xFF3D9BD1), Color(0xFF1D5E8E)), Offset(marge + 3.5f * c, marge + 3.5f * c), c * 5.5f), Offset(marge, marge), Size(c * 7, c * 7), CornerRadius(6.dp.toPx()))
                // les iles : l'image du jeu (case connue), sinon dessinees ; pas encore sur la carte : contour
                for (i in 0 until 49) {
                    val coin = Offset(marge + (i % 7) * c, marge + (i / 7) * c)
                    val o = coin + Offset(c / 2, c / 2)
                    val connue = e.cartes.size == 49 && e.cartes[i] != 0
                    val img = if (connue) CarteMarineWW.tuile(i, e.variante) else null
                    if (img != null) drawImage(img, IntOffset.Zero, IntSize(img.width, img.height), IntOffset(coin.x.toInt(), coin.y.toInt()), IntSize(c.toInt() + 1, c.toInt() + 1))
                    else {
                        val p = ile(o, c * 0.26f, i)
                        if (connue) { drawPath(p, Brush.radialGradient(listOf(Color(0xFF9BE07A), Color(0xFF4F9A3E)), o, c * 0.3f)); drawPath(p, Color(0xFFEFD9A0), style = Stroke(1.5.dp.toPx())) }
                        else drawPath(p, Color(0x33FFFFFF), style = Stroke(1.dp.toPx()))
                    }
                    if (i == ici) drawRect(Color(0x40FFE08A), coin, Size(c, c))
                    if (i == touchee) drawRect(Color(0xFFFF4FD8), coin, Size(c, c), style = Stroke(2.5.dp.toPx()))
                }
                for (k in 0..7) {
                    drawLine(Color(0x33FFFFFF), Offset(marge + k * c, marge), Offset(marge + k * c, marge + 7 * c), 1f)
                    drawLine(Color(0x33FFFFFF), Offset(marge, marge + k * c), Offset(marge + 7 * c, marge + k * c), 1f)
                }
                drawIntoCanvas { cv ->
                    for (k in 0 until 7) {
                        cv.nativeCanvas.drawText(('A' + k).toString(), marge + k * c + c / 2, marge - 5.dp.toPx(), paint)
                        cv.nativeCanvas.drawText((k + 1).toString(), marge / 2, marge + k * c + c / 2 + 4.dp.toPx(), paint)
                    }
                }
                fun pt(o: Offset) = Offset(marge + (o.x + 350000f) / 100000f * c, marge + (o.y + 350000f) / 100000f * c)
                // le sillage (de plus en plus net)
                for (k in 1 until trace.size) drawLine(Color.White.copy(alpha = 0.15f + 0.65f * k / trace.size), pt(trace[k - 1]), pt(trace[k]), 2.dp.toPx(), cap = StrokeCap.Round)
                if (mer && e.aPos) {
                    val p = pt(Offset(e.x, e.z))
                    drawCircle(Color(0x55FF4FD8), 13.dp.toPx(), p)
                    fleche(p, e.cap, 11.dp.toPx(), Color(0xFF3DDC84))
                }
            }
            if (touchee in 0..48) Text(JeuWW.nomCase(touchee) + " · " + JeuWW.ile(touchee) +
                (if (e.cartes.size == 49 && e.cartes[touchee] == 0) " (pas encore sur ta carte)" else ""), color = Color(0xFFFF8FE6), fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            else Doux("Touche une case pour voir le nom de l'île.", taille = 11)
            // la rose des vents, sous la carte (v313 : ne cache plus de case)
            if (e.ventOk) Row(verticalAlignment = Alignment.CenterVertically) {
                Canvas(Modifier.size(34.dp)) {
                    val rc = Offset(size.width / 2, size.height / 2)
                    drawCircle(Color(0xCCF3E2B8), size.width / 2, rc)
                    drawCircle(Color(0xFF8B5A2B), size.width / 2, rc, style = Stroke(1.5.dp.toPx()))
                    fleche(rc, Math.atan2(e.ventX.toDouble(), e.ventZ.toDouble()), size.width * 0.36f, Color(0xFF2F8FD8))
                }
                Spacer(Modifier.width(10.dp))
                Doux("Le vent souffle vers le " + JeuWW.direction(e.ventX.toDouble(), e.ventZ.toDouble()) + ".", taille = 12)
            }
            if (!vraie && CarteMarineWW.etat.isNotEmpty()) Doux(CarteMarineWW.etat, taille = 10)
            if (!mer) Doux("La position s'affiche quand Link est sur la Grande Mer.", taille = 11)
        }
    }
}

/** v313 : le plan d'un interieur (donjon, maison, ile) : le chemin de Link garde par scene, vu du dessus */
@Composable
private fun PlanInterieurWW(e: JeuWW.Etat) {
    val ctx = LocalContext.current
    @Suppress("UNUSED_VARIABLE") val v = PlanWW.version
    val pts = PlanWW.points(ctx.applicationContext, e.scene)
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Titre("Plan", couleur = Color(0xFFFFD27A))
            Text(JeuWW.lieu(e.scene).ifEmpty { "Intérieur" } + if (e.salle >= 0) " · salle " + e.salle else "", color = C.Texte, fontSize = 17.sp, fontWeight = FontWeight.Bold)
            Canvas(Modifier.fillMaxWidth().aspectRatio(1f)) {
                drawRoundRect(Color(0xFF14102A), Offset.Zero, size, CornerRadius(12.dp.toPx()))
                var x0 = e.x; var x1 = e.x; var z0 = e.z; var z1 = e.z
                for (p in pts) { x0 = minOf(x0, p[0]); x1 = maxOf(x1, p[0]); z0 = minOf(z0, p[2]); z1 = maxOf(z1, p[2]) }
                val marge = 22.dp.toPx()
                val etendue = maxOf(x1 - x0, z1 - z0, 1500f)
                val k = (size.width - 2 * marge) / etendue
                val ox = size.width / 2 - (x0 + x1) / 2 * k; val oz = size.height / 2 - (z0 + z1) / 2 * k
                fun pt(x: Float, z: Float) = Offset(ox + x * k, oz + z * k)
                val r = maxOf(60f * k, 3.dp.toPx())
                // les autres etages (hauteur eloignee) en pale, puis l'etage en cours, la salle en cours en rose
                for (p in pts) if (Math.abs(p[1] - e.y) > 350f) drawCircle(Color(0x223A8BFF), r, pt(p[0], p[2]))
                for (p in pts) if (Math.abs(p[1] - e.y) <= 350f) drawCircle(if (p[3].toInt() == e.salle) Color(0x99FF4FD8) else Color(0x8857B6FF), r, pt(p[0], p[2]))
                val ici = pt(e.x, e.z)
                drawCircle(Color(0x55FFFFFF), 13.dp.toPx(), ici)
                fleche(ici, e.cap, 11.dp.toPx(), Color(0xFF3DDC84))
            }
            Doux(if (pts.size < 5) "Le plan se dessine au fil de tes pas : explore, il se remplit (gardé sur le téléphone)."
                 else "Ton chemin, gardé sur le téléphone · rose : la salle où tu es · pâle : les autres étages.", taille = 10)
            if (pts.size >= 5) Bouton("Effacer ce plan", Modifier.fillMaxWidth(), hauteur = 36.dp) { PlanWW.effacer(ctx.applicationContext, e.scene) }
        }
    }
}

// ---------------------------------------------------------------- Super Mario Sunshine
@Composable
private fun CompagnonSMS(e: JeuSMS.Etat) {
    CarteJeu(Color(0xFF1C8FD8), Color(0xFF0B3C72)) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(JeuSMS.ZONES[e.zone] + when { !e.enJeu -> ""; e.secret -> " · niveau secret"; e.zone in 2..9 -> " · épisode " + (e.episode + 1); else -> "" },
                color = Color(0xFFFFE27A), fontSize = 18.sp, fontWeight = FontWeight.Black)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Canvas(Modifier.size(62.dp)) {                                   // le soleil
                    val c = Offset(size.width / 2, size.height / 2)
                    for (k in 0 until 16) {
                        val a = Math.toRadians(k * 22.5)
                        val r = size.width * (if (k % 2 == 0) 0.5f else 0.4f)
                        drawLine(Color(0xFFFFB300), c, c + Offset((Math.cos(a) * r).toFloat(), (Math.sin(a) * r).toFloat()), 4.dp.toPx(), cap = StrokeCap.Round)
                    }
                    drawCircle(Brush.radialGradient(listOf(Color(0xFFFFF6B0), Color(0xFFFFC21A)), c, size.width * 0.32f), size.width * 0.32f, c)
                    drawCircle(Color(0xFF3A2A00), size.width * 0.04f, c + Offset(-size.width * 0.09f, -size.width * 0.04f))
                    drawCircle(Color(0xFF3A2A00), size.width * 0.04f, c + Offset(size.width * 0.09f, -size.width * 0.04f))
                }
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(e.soleils.toString() + " / 120", color = Color.White, fontSize = 34.sp, fontWeight = FontWeight.Black)
                    Doux("soleils", taille = 11, couleur = Color(0xCCFFFFFF))
                }
                if (e.sante >= 0) Canvas(Modifier.size(64.dp)) {                // la jauge de vie du jeu : 8 parts
                    val c = Offset(size.width / 2, size.height / 2)
                    val r = size.width / 2
                    for (k in 0 until 8) {
                        val on = k < e.sante
                        val col = when { !on -> Color(0x33FFFFFF); e.sante <= 2 -> Color(0xFFFF3B3B); e.sante <= 4 -> Color(0xFFFFB300); else -> Color(0xFF3DDC84) }
                        drawArc(col, -90f + k * 45f + 2f, 41f, true, Offset(c.x - r, c.y - r), Size(2 * r, 2 * r))
                    }
                    drawCircle(Color(0xFF0B3C72), r * 0.45f, c)
                    drawIntoCanvas { cv -> cv.nativeCanvas.drawText(e.sante.toString(), c.x, c.y + 7.dp.toPx(), android.graphics.Paint().apply {
                        isAntiAlias = true; textSize = 18.dp.toPx(); color = android.graphics.Color.WHITE; textAlign = android.graphics.Paint.Align.CENTER; typeface = android.graphics.Typeface.DEFAULT_BOLD }) }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Jauge("pièces bleues / 240", e.bleues.toString(), Color(0xFF7FD4FF), Modifier.weight(1f))
                Jauge("pièces", e.pieces.toString(), Color(0xFFFFD54A), Modifier.weight(1f))
                Jauge("vies", e.vies.toString(), Color(0xFFFF8A8A), Modifier.weight(1f))
            }
            if (e.enJeu && e.rouges > 0) Row(verticalAlignment = Alignment.CenterVertically) {
                for (k in 0 until 8) Box(Modifier.padding(end = 4.dp).size(14.dp).clip(RoundedCornerShape(7.dp)).background(if (k < e.rouges) Color(0xFFFF3B3B) else Color(0x33FFFFFF)))
                Doux(" pièces rouges " + e.rouges + " / 8", taille = 11, couleur = Color(0xCCFFFFFF))
            }
            if (!e.enJeu) Doux("Menu ou cinématique.", taille = 11, couleur = Color(0xCCFFFFFF))
        }
    }
    // JET : le reservoir, les buses, Yoshi
    if (e.eau >= 0f || e.buse >= 0) Carte(Modifier.fillMaxWidth()) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Canvas(Modifier.size(54.dp, 96.dp)) {                                 // le reservoir
                val w = size.width; val h = size.height
                drawRoundRect(Color(0x22FFFFFF), Offset.Zero, size, CornerRadius(14.dp.toPx()))
                val niv = e.eau.coerceIn(0f, 1f)
                clipPath(Path().apply { addRoundRect(androidx.compose.ui.geometry.RoundRect(0f, 0f, w, h, CornerRadius(14.dp.toPx()))) }) {
                    drawRect(Brush.verticalGradient(listOf(Color(0xFF7FE3FF), Color(0xFF1E88E5))), Offset(0f, h * (1 - niv)), Size(w, h * niv))
                }
                drawRoundRect(Color(0x99FFFFFF), Offset.Zero, size, CornerRadius(14.dp.toPx()), style = Stroke(1.5.dp.toPx()))
            }
            Spacer(Modifier.width(14.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Titre("JET", couleur = Color(0xFF7FD4FF))
                if (e.eau >= 0f) Doux("Eau " + (e.eau * 100).toInt() + " %", taille = 12, couleur = C.Texte)
                Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                    val buses = listOf(0 to "💦", 4 to "🚁", 1 to "🚀", 5 to "💨")
                    for ((b, sym) in buses) {
                        val dispo = b == 0 || b == e.buse2 || (b == 1 && e.fusee) || (b == 5 && e.turbo) || b == 4
                        val active = b == e.buse || (e.buse < 0 && b == 0)
                        Case(sym, JeuSMS.BUSES[b], dispo, Modifier.weight(1f).then(if (active) Modifier.background(Color(0x553DDC84), RoundedCornerShape(12.dp)) else Modifier))
                    }
                }
                Doux(when {
                    e.yoshi == 8 -> "🦖 Sur le dos de Yoshi"
                    e.yoshi in 0..7 -> "🥚 Yoshi est là"
                    e.yoshiDebloque -> "🦖 Yoshi débloqué"
                    else -> ""
                } + if (e.aPos && e.vitesse > 0.5f) (if (e.yoshi >= 0 || e.yoshiDebloque) " · " else "") + "vitesse " + e.vitesse.toInt() + " · altitude " + e.y.toInt() else "", taille = 11)
            }
        }
    }
    // les soleils de chaque zone
    if (e.zones.isNotEmpty()) Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Titre("Soleils par zone", couleur = Color(0xFFFFD54A))
            val toutes = e.zones + listOfNotNull(e.delfino, e.corona)
            for (z in toutes) Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(z.nom, color = Color(z.couleur), fontSize = 13.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                    Doux(z.soleils.sum().toString() + "/" + z.soleils.size + (if (z.bleuesMax > 0) " · 🔵 " + z.bleues + "/" + z.bleuesMax else "") + (if (z.record > 0) " · 🪙 " + z.record else ""), taille = 11)
                }
                Canvas(Modifier.fillMaxWidth().height(20.dp)) {
                    val n = z.soleils.size
                    val pas = minOf(size.width / n.coerceAtLeast(1), 26.dp.toPx())
                    val r = pas * 0.36f
                    for (k in 0 until n) {
                        val c = Offset(k * pas + pas / 2, size.height / 2)
                        if (z.soleils[k] != 0) {
                            for (j in 0 until 8) { val a = Math.toRadians(j * 45.0); drawLine(Color(0xFFFFB300), c, c + Offset((Math.cos(a) * r * 1.35f).toFloat(), (Math.sin(a) * r * 1.35f).toFloat()), 2.dp.toPx()) }
                            drawCircle(Color(0xFFFFD54A), r, c)
                        } else drawCircle(Color(0x33FFFFFF), r, c, style = Stroke(1.5.dp.toPx()))
                    }
                }
                if (z.bleuesMax > 0) Barre(z.bleues / z.bleuesMax.toFloat(), Color(0xFF4FA3FF), hauteur = 4)
            }
            e.achats?.let { a -> Doux(a.nom + " : " + a.soleils.sum() + " / " + a.soleils.size, taille = 11) }
            Doux("🔵 pièces bleues trouvées · 🪙 record de pièces (100 pièces = 1 soleil)", taille = 10)
        }
    }
}

// ---------------------------------------------------------------- Mario Kart Double Dash!!
private fun couleurPlace(p: Int): Color = when (p) { 1 -> Color(0xFFFFD54A); 2 -> Color(0xFFD8E0EA); 3 -> Color(0xFFE09A5A); else -> Color(0xFF9AA8C8) }

@Composable
private fun CompagnonMKDD(e: JeuMKDD.Etat) {
    CarteJeu(Color(0xFFB0121E), Color(0xFF2A0A3A)) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            if (!e.enCourse) { Doux("Pas de course en ce moment (menus).", couleur = Color.White); return@Column }
            val infos = listOfNotNull(JeuMKDD.MODES.getOrNull(e.mode)?.takeIf { it.isNotEmpty() }, JeuMKDD.NIVEAUX.getOrNull(e.niveau)?.takeIf { !e.miroir || e.niveau == 3 },
                if (e.miroir && e.niveau != 3) "Miroir" else null, if (e.mode == 2) JeuMKDD.COUPES.getOrNull(e.coupe) else null)
            if (infos.isNotEmpty()) Doux(infos.joinToString(" · "), taille = 12, couleur = Color(0xFFFFD0D0))
            Row(verticalAlignment = Alignment.Bottom) {
                Text(if (e.place == 0) "–" else e.place.toString(), color = couleurPlace(e.place), fontSize = 64.sp, fontWeight = FontWeight.Black)
                Text(if (e.place == 1) "er" else if (e.place > 1) "e" else "", color = couleurPlace(e.place), fontSize = 22.sp, fontWeight = FontWeight.Black, modifier = Modifier.padding(bottom = 14.dp))
                Spacer(Modifier.width(14.dp))
                Column(Modifier.padding(bottom = 12.dp)) {
                    Text(if (e.fini) "Arrivée !" else "Tour " + (e.tour + 1).coerceIn(1, e.tours) + " / " + e.tours, color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                    if (e.fini && e.total < JeuMKDD.PAS_DE_TEMPS) Doux("Temps total " + dureeMs(e.total.toLong()), taille = 13, couleur = Color(0xFFFFE27A))
                    val moi = e.karts.firstOrNull { it.n == 0 }
                    if (moi != null && moi.pilote > 0) Doux(JeuMKDD.PERSOS[moi.pilote] + " & " + JeuMKDD.PERSOS[moi.passager] + (if (moi.modele >= 0) " · " + JeuMKDD.KARTS[moi.modele] else ""), taille = 11, couleur = Color(0xDDFFFFFF))
                }
                Spacer(Modifier.weight(1f))
                val moi = e.karts.firstOrNull { it.n == 0 }
                if (moi != null) Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(bottom = 10.dp)) {
                    Row { for (o in listOf(moi.objet1, moi.objet2)) Box(Modifier.padding(2.dp).size(40.dp).clip(RoundedCornerShape(10.dp)).background(Color(0x44000000)), contentAlignment = Alignment.Center) {
                        Text(JeuMKDD.objet(o)?.first ?: "", fontSize = 22.sp)
                    } }
                    Doux("objets", taille = 9, couleur = Color(0xAAFFFFFF))
                }
            }
            if (!e.fini) Barre(e.progres, Color(0xFFFFD54A))
        }
    }
    if (!e.enCourse) return
    // le classement : chaque kart, ses pilotes et ses objets
    if (e.karts.size > 1) Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Titre("Classement")
            val maxProg = e.karts.maxOf { it.progres }.coerceAtLeast(0.01f)
            for (k in e.karts.sortedBy { if (it.place == 0) 9 else it.place }) Row(verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(if (k.n == 0) Color(0x33FF4FD8) else Color(0x0DFFFFFF)).padding(horizontal = 8.dp, vertical = 6.dp)) {
                Box(Modifier.size(28.dp).clip(RoundedCornerShape(14.dp)).background(couleurPlace(k.place)), contentAlignment = Alignment.Center) {
                    Text(if (k.place > 0) k.place.toString() else "–", color = Color(0xFF1A1A2E), fontSize = 14.sp, fontWeight = FontWeight.Black)
                }
                Spacer(Modifier.width(8.dp))
                Column(Modifier.weight(1f)) {
                    Text(if (k.pilote > 0) JeuMKDD.PERSOS[k.pilote] + " & " + JeuMKDD.PERSOS[k.passager] else "Kart " + (k.n + 1),
                        color = if (k.pilote > 0) Color(JeuMKDD.PERSO_COUL[k.pilote]) else C.Texte, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1)
                    val det = listOfNotNull(if (k.modele >= 0) JeuMKDD.KARTS[k.modele] else null, if (k.joueur) "joueur" else null,
                        if (k.ballons >= 0) "🎈 " + k.ballons else null, if (k.points >= 0) "💣 " + k.points + " pts" else null, if (k.fini) "arrivé" else null)
                    if (det.isNotEmpty()) Doux(det.joinToString(" · "), taille = 10, lignes = 1)
                    if (k.progres >= 0f && e.mode in 1..3) Barre(k.progres / maxProg, Color(if (k.pilote > 0) JeuMKDD.PERSO_COUL[k.pilote] else 0xFF9AA8C8), hauteur = 3)
                }
                Text((JeuMKDD.objet(k.objet1)?.first ?: "") + (JeuMKDD.objet(k.objet2)?.first ?: ""), fontSize = 18.sp)
            }
        }
    }
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            CarteCircuit(e)
            val meilleur = e.tempsTours.minOrNull()
            if (e.tempsTours.isNotEmpty()) Titre("Tours")
            for ((i, t) in e.tempsTours.withIndex()) Row(verticalAlignment = Alignment.CenterVertically) {
                Doux("Tour " + (i + 1), taille = 13)
                Spacer(Modifier.width(10.dp))
                if (meilleur != null && e.tempsTours.size > 1) Box(Modifier.weight(1f).height(6.dp).clip(RoundedCornerShape(3.dp)).background(C.Verre)) {
                    Box(Modifier.fillMaxWidth((meilleur.toFloat() / t).coerceIn(0.05f, 1f)).height(6.dp).background(if (t == meilleur) C.Vert else C.Cyan.copy(alpha = 0.6f)))
                } else Spacer(Modifier.weight(1f))
                Spacer(Modifier.width(10.dp))
                Text(dureeMs(t.toLong()), color = if (t == meilleur && e.tempsTours.size > 1) C.Vert else C.Texte, fontSize = 14.sp, fontFamily = FontFamily.Monospace)
                if (meilleur != null && t != meilleur) Text(" +" + String.format(java.util.Locale.FRANCE, "%.3f", (t - meilleur) / 1000.0), color = C.Rouge, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
            }
        }
    }
}

@Composable
private fun CarteCircuit(e: JeuMKDD.Etat) {
    val t = JeuMKDD.trace
    val nom = JeuMKDD.nomCircuit(e.circuit)
    if (nom.isNotEmpty()) Titre(nom)
    if (t.isEmpty()) { if (e.karts.isNotEmpty()) Doux("Lecture du circuit…", taille = 11); return }
    Canvas(Modifier.fillMaxWidth().aspectRatio(1f)) {
        var x0 = Float.MAX_VALUE; var x1 = -Float.MAX_VALUE; var z0 = Float.MAX_VALUE; var z1 = -Float.MAX_VALUE
        for (p in t) { x0 = minOf(x0, p[0], p[2]); x1 = maxOf(x1, p[0], p[2]); z0 = minOf(z0, p[1], p[3]); z1 = maxOf(z1, p[1], p[3]) }
        val marge = 16.dp.toPx()
        val k = minOf((size.width - 2 * marge) / (x1 - x0).coerceAtLeast(1f), (size.height - 2 * marge) / (z1 - z0).coerceAtLeast(1f))
        val ox = (size.width - (x1 - x0) * k) / 2; val oz = (size.height - (z1 - z0) * k) / 2
        fun pt(x: Float, z: Float) = Offset(ox + (x - x0) * k, oz + (z - z0) * k)
        drawRoundRect(Brush.radialGradient(listOf(Color(0xFF2E7D32), Color(0xFF123A1A)), Offset(size.width / 2, size.height / 2), size.width * 0.7f), Offset.Zero, size, CornerRadius(14.dp.toPx()))
        val route = Path(); val bordG = Path(); val bordD = Path(); val milieu = Path()
        for (i in t.indices) {
            val a = t[i]; val b = t[(i + 1) % t.size]
            val q = Path().apply {
                val p1 = pt(a[0], a[1]); val p2 = pt(a[2], a[3]); val p3 = pt(b[2], b[3]); val p4 = pt(b[0], b[1])
                moveTo(p1.x, p1.y); lineTo(p2.x, p2.y); lineTo(p3.x, p3.y); lineTo(p4.x, p4.y); close()
            }
            route.addPath(q)
            val g = pt(a[0], a[1]); val d = pt(a[2], a[3]); val m = Offset((g.x + d.x) / 2, (g.y + d.y) / 2)
            if (i == 0) { bordG.moveTo(g.x, g.y); bordD.moveTo(d.x, d.y); milieu.moveTo(m.x, m.y) } else { bordG.lineTo(g.x, g.y); bordD.lineTo(d.x, d.y); milieu.lineTo(m.x, m.y) }
        }
        bordG.close(); bordD.close(); milieu.close()
        drawPath(route, Color(0xFF4A4F63))
        drawPath(bordG, Color(0xFFFF5252), style = Stroke(2.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 6.dp.toPx()))))
        drawPath(bordG, Color.White, style = Stroke(2.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 6.dp.toPx()), 6.dp.toPx())))
        drawPath(bordD, Color(0xFFFF5252), style = Stroke(2.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 6.dp.toPx()))))
        drawPath(bordD, Color.White, style = Stroke(2.5.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 6.dp.toPx()), 6.dp.toPx())))
        drawPath(milieu, Color(0x22FFFFFF), style = Stroke(1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(4.dp.toPx(), 8.dp.toPx()))))
        val dep = JeuMKDD.depart
        if (dep in t.indices) {                                         // ligne d'arrivee en damier
            val a = t[dep]; val g = pt(a[0], a[1]); val d = pt(a[2], a[3])
            val n = 8
            for (j in 0 until n) drawLine(if (j % 2 == 0) Color.White else Color.Black, g + (d - g) * (j / n.toFloat()), g + (d - g) * ((j + 1) / n.toFloat()), 5.dp.toPx())
        }
        val paint = android.graphics.Paint().apply { isAntiAlias = true; textSize = 10.dp.toPx(); color = android.graphics.Color.BLACK
            textAlign = android.graphics.Paint.Align.CENTER; typeface = android.graphics.Typeface.DEFAULT_BOLD }
        for (kt in e.karts.sortedByDescending { it.n }) {               // (le kart 1, le joueur, dessine en dernier : au-dessus)
            val p = pt(kt.x, kt.z)
            val joueur = kt.n == 0
            val r = if (joueur) 10.dp.toPx() else 7.5.dp.toPx()
            val col = if (kt.pilote > 0) Color(JeuMKDD.PERSO_COUL[kt.pilote]) else Color(0xFFB8C7FF)
            if (joueur) drawCircle(Color(0x66FF4FD8), r * 1.9f, p)
            drawCircle(Color(0x66000000), r, p + Offset(1.5.dp.toPx(), 1.5.dp.toPx()))
            drawCircle(col, r, p)
            drawCircle(Color.White, r, p, style = Stroke((if (joueur) 2.5 else 1.2).dp.toPx()))
            if (kt.place > 0) drawIntoCanvas { it.nativeCanvas.drawText(kt.place.toString(), p.x, p.y + 3.5.dp.toPx(), paint) }
        }
    }
    Doux("Vu du dessus · couleur : le pilote · chiffre : place · cercle rose : toi · damier : ligne d'arrivée", taille = 10)
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
            } + " Le téléphone lit le jeu 3 fois par seconde : les étapes automatiques sont précises à 0,3 s près.", taille = 11)
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
