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
class Lecteur {
    private val cache = HashMap<Long, ByteArray?>()
    private fun octets(a: Long, n: Int): ByteArray? {
        if (a < MemoireJeu.BASE || a + n > MemoireJeu.BASE + MemoireJeu.FIN) return null
        val b = a and (MemoireJeu.BLK - 1).toLong().inv()
        if (a + n > b + MemoireJeu.BLK) return null               // (valeurs alignees : jamais sur deux blocs)
        val d = cache.getOrPut(b) { MemoireJeu.bloc(b) } ?: return null
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
    private const val G = 0x803CC530L                              // g_dComIfG_gameInfo
    private const val SCENE = G + 0x12A0 + 0x3E94                  // play.mCurStage : nom (8), point, salle
    private const val LINK = G + 0x12A0 + 0x48AC                   // play.mpPlayerPtr[0]

    class Etat(val partie: Boolean, val vie: Int, val vieMax: Int, val rubis: Int, val magie: Int, val magieMax: Int,
               val chants: Int, val triforce: Int, val perles: Int, val fleches: Int, val bombes: Int, val nom: String,
               val scene: String, val x: Float, val z: Float, val aPos: Boolean)

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
        var x = 0f; var z = 0f; var a = false
        val p = l.ptr(LINK)
        if (p != null) {
            val px = l.f32(p + 0x1F8); val pz = l.f32(p + 0x200)       // current.pos
            if (px != null && pz != null && px.isFinite() && pz.isFinite() && Math.abs(px) < 1e6f && Math.abs(pz) < 1e6f) { x = px; z = pz; a = true }
        }
        return Etat(true, v, vm, rubis.coerceAtMost(9999), m.coerceAtMost(mm), mm,
            Integer.bitCount(l.u8(col + 9) ?: 0), l.u8(col + 10) ?: 0, l.u8(col + 11) ?: 0,
            l.u8(G + 0x69) ?: 0, l.u8(G + 0x6A) ?: 0, (l.texte(G + 0x148 + 0x14, 17) ?: "").filter { it.code in 32..255 },
            scene, x, z, a)
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

    class Etat(val enJeu: Boolean, val soleils: Int, val bleues: Int, val pieces: Int, val vies: Int, val sante: Int,
               val eau: Float, val scene: Int, val zone: Int, val episode: Int, val secret: Boolean)

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
        var sante = -1; var eau = -1f
        val m = l.ptr(MARIO)
        if (m != null) {
            sante = l.s16(m + 0x120)?.takeIf { it in 0..8 } ?: -1
            val g = l.ptr(m + 0x3E4)
            if (g != null) eau = ((l.s32(g + 0x1C80) ?: -1) / 10500f).takeIf { it in 0f..1.2f }?.coerceAtMost(1f) ?: -1f
        }
        val zone = if (scene < ZONE_DE.size) ZONE_DE[scene] else 1
        return Etat(etat == 5, soleils, bleues, pieces, vies, sante, eau, scene, zone, ep, scene in 0x15..0x34)
    }
}

// ---------------------------------------------------------------- Mario Kart Double Dash!! (GM4P01)
object JeuMKDD {
    const val ID = "GM4P01"
    private const val RACE = 0x803D5628L                           // RaceMgr::sRaceManager
    const val PAS_DE_TEMPS = 5999999

    class Etat(val enCourse: Boolean, val fini: Boolean, val place: Int, val tour: Int, val tours: Int, val progres: Float,
               val tempsTours: List<Int>, val total: Int)

    val HORS_COURSE = Etat(false, false, 0, 0, 0, 0f, emptyList(), PAS_DE_TEMPS)

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
        return Etat(enCourse || fini, fini, place, tour, tours, prog, lt, total)
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
                    val l = Lecteur()
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
                    Chrono.observer(etat, r)
                    etat = r
                }
                val pris = SystemClock.elapsedRealtime() - t0
                delay((200 - pris).coerceAtLeast(40))
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
        Text(valeur, color = couleur, fontSize = 22.sp, fontWeight = FontWeight.Black, maxLines = 1)
        Doux(nom, taille = 11, lignes = 1)
    }
}

@Composable
private fun Barre(part: Float, couleur: Color) {
    Box(Modifier.fillMaxWidth().height(10.dp).clip(RoundedCornerShape(5.dp)).background(C.Verre)) {
        Box(Modifier.fillMaxWidth(part.coerceIn(0f, 1f)).height(10.dp).clip(RoundedCornerShape(5.dp)).background(couleur))
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

@Composable
private fun CompagnonWW(e: JeuWW.Etat) {
    if (!e.partie) { Carte(Modifier.fillMaxWidth()) { Doux("Aucune partie chargée (écran titre ou choix du fichier).", couleur = C.Texte) }; return }
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Titre(if (e.nom.isNotEmpty()) e.nom else "Link", couleur = C.Vert)
                Spacer(Modifier.weight(1f))
                Doux((e.vie / 4f).let { if (it % 1f == 0f) it.toInt().toString() else String.format(java.util.Locale.FRANCE, "%.2f", it) } + " / " + e.vieMax / 4 + " cœurs", taille = 12)
            }
            // coeurs (par quarts), 10 par ligne
            val n = (e.vieMax + 3) / 4
            Canvas(Modifier.fillMaxWidth().height(if (n > 10) 58.dp else 28.dp)) {
                val s = minOf(size.width / 10f, 28.dp.toPx()) * 0.92f
                val pas = s / 0.92f
                for (i in 0 until n) {
                    val q = (e.vie - i * 4).coerceIn(0, 4)
                    val o = Offset((i % 10) * pas, (i / 10) * (pas + 2.dp.toPx()))
                    val p = coeur(s); p.translate(o)
                    drawPath(p, Color(0x33FFFFFF))
                    if (q > 0) clipPath(p) { drawArc(Color(0xFFFF3B5C), -90f, 90f * q, true, o, Size(s, s)) }
                    drawPath(p, Color(0xAAFFFFFF), style = Stroke(1.2.dp.toPx()))
                }
            }
            if (e.magieMax > 0) { Doux("Magie " + e.magie + " / " + e.magieMax, taille = 11); Barre(e.magie / e.magieMax.toFloat(), C.Vert) }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Jauge("rubis", e.rubis.toString(), C.Vert, Modifier.weight(1f))
                Jauge("flèches", e.fleches.toString(), C.Texte, Modifier.weight(1f))
                Jauge("bombes", e.bombes.toString(), C.Texte, Modifier.weight(1f))
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Doux("Perles ", taille = 12)
                for ((k, c) in listOf(Color(0xFF4FA3FF), Color(0xFFFF4D4D), Color(0xFF3DDC84)).withIndex()) {
                    val a = e.perles and (1 shl k) != 0
                    Box(Modifier.padding(horizontal = 3.dp).size(16.dp).clip(RoundedCornerShape(8.dp)).background(if (a) c else Color(0x22FFFFFF)))
                }
                Spacer(Modifier.width(14.dp))
                Doux("Triforce " + Integer.bitCount(e.triforce) + "/8", taille = 12, couleur = if (e.triforce == 255) Color(0xFFFFD54A) else C.Doux)
                Spacer(Modifier.width(14.dp))
                Doux("Chants " + e.chants + "/6", taille = 12)
            }
        }
    }
    // la carte marine
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            val l = JeuWW.lieu(e.scene)
            val mer = e.scene == "sea"
            val ici = if (mer && e.aPos) JeuWW.case(e.x, e.z) else -1
            Titre("Carte marine")
            Text(when {
                ici >= 0 -> JeuWW.nomCase(ici) + " · " + JeuWW.ile(ici)
                l.isNotEmpty() -> l
                else -> "Ailleurs (" + e.scene + ")"
            }, color = C.Texte, fontSize = 17.sp, fontWeight = FontWeight.Bold)
            val trace = Compagnon.traceWW
            Canvas(Modifier.fillMaxWidth().aspectRatio(1f)) {
                val marge = 18.dp.toPx()
                val c = (size.width - marge) / 7f
                val paint = android.graphics.Paint().apply { isAntiAlias = true; textSize = 11.dp.toPx(); color = android.graphics.Color.argb(170, 255, 255, 255); textAlign = android.graphics.Paint.Align.CENTER }
                drawRoundRect(Color(0xFF0B2A4A), Offset(marge, marge), Size(c * 7, c * 7), CornerRadius(8.dp.toPx()))
                for (i in 0 until 49) {
                    val o = Offset(marge + (i % 7) * c, marge + (i / 7) * c)
                    if (i == ici) drawRect(Color(0x55FF4FD8), o, Size(c, c))
                    drawCircle(if (i == ici) Color(0xFFFFE08A) else Color(0x5580C8FF), c * 0.09f, o + Offset(c / 2, c / 2))
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
                for (k in 1 until trace.size) drawLine(Color(0x99FFFFFF), pt(trace[k - 1]), pt(trace[k]), 2.dp.toPx())
                if (mer && e.aPos) {
                    val p = pt(Offset(e.x, e.z))
                    drawCircle(Color(0x66FF4FD8), 9.dp.toPx(), p)
                    drawCircle(Color(0xFF3DDC84), 5.dp.toPx(), p)
                    drawCircle(Color.White, 5.dp.toPx(), p, style = Stroke(1.5.dp.toPx()))
                }
            }
            if (!mer) Doux("La position s'affiche quand Link est sur la Grande Mer.", taille = 11)
        }
    }
}

@Composable
private fun CompagnonSMS(e: JeuSMS.Etat) {
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Titre(JeuSMS.ZONES[e.zone] + when { !e.enJeu -> ""; e.secret -> " · niveau secret"; e.zone in 2..9 -> " · épisode " + (e.episode + 1); else -> "" }, couleur = Color(0xFFFFD54A))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Canvas(Modifier.size(54.dp)) {                                   // (le soleil)
                    val c = Offset(size.width / 2, size.height / 2)
                    for (k in 0 until 12) {
                        val a = Math.toRadians(k * 30.0)
                        drawLine(Color(0xFFFFB300), c, c + Offset((Math.cos(a) * size.width * 0.5).toFloat(), (Math.sin(a) * size.width * 0.5).toFloat()), 4.dp.toPx())
                    }
                    drawCircle(Color(0xFFFFD54A), size.width * 0.32f, c)
                }
                Spacer(Modifier.width(12.dp))
                Column {
                    Text(e.soleils.toString() + " / 120", style = TextStyle(brush = C.Neon), fontSize = 34.sp, fontWeight = FontWeight.Black)
                    Doux("soleils", taille = 11)
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Jauge("pièces bleues / 240", e.bleues.toString(), Color(0xFF4FA3FF), Modifier.weight(1f))
                Jauge("pièces", e.pieces.toString(), Color(0xFFFFD54A), Modifier.weight(1f))
                Jauge("vies", e.vies.toString(), C.Rouge, Modifier.weight(1f))
            }
            if (e.sante >= 0) {
                Doux("Santé " + e.sante + " / 8", taille = 11)
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    for (k in 0 until 8) Box(Modifier.weight(1f).height(12.dp).clip(RoundedCornerShape(4.dp)).background(
                        if (k < e.sante) (if (e.sante <= 2) C.Rouge else Color(0xFFFFB300)) else C.Verre))
                }
            }
            if (e.eau >= 0f) { Doux("Eau de JET " + (e.eau * 100).toInt() + " %", taille = 11); Barre(e.eau, Color(0xFF4FC3F7)) }
            if (!e.enJeu) Doux("Menu ou cinématique.", taille = 11)
        }
    }
}

@Composable
private fun CompagnonMKDD(e: JeuMKDD.Etat) {
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            if (!e.enCourse) { Doux("Pas de course en ce moment (menus).", couleur = C.Texte); return@Column }
            Row(verticalAlignment = Alignment.Bottom) {
                Text(if (e.place == 0) "–" else e.place.toString() + if (e.place == 1) "er" else "e",
                    style = TextStyle(brush = C.Neon), fontSize = 56.sp, fontWeight = FontWeight.Black)
                Spacer(Modifier.width(14.dp))
                Column(Modifier.padding(bottom = 10.dp)) {
                    Text(if (e.fini) "Arrivée !" else "Tour " + (e.tour + 1).coerceIn(1, e.tours) + " / " + e.tours, color = C.Texte, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                    if (e.fini && e.total < JeuMKDD.PAS_DE_TEMPS) Doux("Temps total " + dureeMs(e.total.toLong()), taille = 13, couleur = C.Cyan)
                }
            }
            if (!e.fini) Barre(e.progres, C.Rose)
            val meilleur = e.tempsTours.minOrNull()
            for ((i, t) in e.tempsTours.withIndex()) Row {
                Doux("Tour " + (i + 1), taille = 13)
                Spacer(Modifier.weight(1f))
                Text(dureeMs(t.toLong()), color = if (t == meilleur && e.tempsTours.size > 1) C.Vert else C.Texte, fontSize = 14.sp, fontFamily = FontFamily.Monospace)
            }
        }
    }
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
