package fr.gcstation.telecommande

// Appli build 213 : Rayman 3 (GRHP41 / GRHE41) : la progression lue dans la sauvegarde (« RAYMAN 3 Save Data », .gci) :
// sur la carte de la console (slot A ou B, hors partie) ou un .gci du telephone (Dolphin...). La sauvegarde GameCube
// n'est ni compressee ni brouillee. Disposition (decrite par BinarySerializer.OpenSpace, licence MIT : seulement les
// positions et tailles) : apres l'en-tete .gci (0x40) et les images (0x5040) : nombre de parties, parties presentes (10),
// noms (10 x 7), 1 octet, positions des parties (10 x 16 bits) ; une partie : total (cages, score), 9 mondes (cages,
// score), 16 octets, fin du jeu, monde atteint, options (12), 6 x 4 octets, niveau en cours (20), (20), bonus GameCube.
// Cages par monde : celles de la sauvegarde a 100 % (60 en tout) ; tampons : score du monde au-dessus du seuil.

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

object Rayman3 {
    val MONDES = arrayOf("Le Concile des Fées", "La Forêt de Claire-Feuille", "Les Marais Crapoteux", "La Lande aux Esprits Frappés",
        "Le Désert des Knaarens", "Le Grand Raccourci", "Le Sommet d'Outre-Nuées", "Le Quartier Général Hoodlums", "La Tour du Leptys")
    val CAGES_MAX = intArrayOf(0, 10, 9, 9, 12, 0, 3, 7, 10)
    val TAMPONS = intArrayOf(20820, 44500, 25900, 58000, 55500, 26888, 26700, 43700, 48000)

    class Partie(val nom: String, val cages: Int, val score: Long, val mondeCages: IntArray, val mondeScore: LongArray,
                 val fini: Boolean, val atteint: Int, val niveau: String, val cauchemar: Boolean, val folie: Boolean, val miniJeux: Int)

    private fun u32(b: ByteArray, o: Int): Long = if (o + 4 > b.size) 0 else
        ((b[o].toLong() and 255) shl 24) or ((b[o + 1].toLong() and 255) shl 16) or ((b[o + 2].toLong() and 255) shl 8) or (b[o + 3].toLong() and 255)
    private fun u16(b: ByteArray, o: Int): Int = ((b[o].toInt() and 255) shl 8) or (b[o + 1].toInt() and 255)
    private fun txt(b: ByteArray, o: Int, n: Int): String = String(b, o, minOf(n, b.size - o), Charsets.ISO_8859_1).substringBefore('\u0000').trim()

    /** les parties d'une sauvegarde .gci de Rayman 3 ; null si ce n'en est pas une */
    fun lire(g: ByteArray): List<Partie>? {
        if (g.size < 0x5100 || !txt(g, 0, 4).startsWith("GRH")) return null
        val b = 0x40 + 0x5040
        val l = ArrayList<Partie>()
        for (i in 0 until 10) {
            if (g[b + 1 + i].toInt() == 0) continue
            val s = b + u16(g, b + 82 + 2 * i)
            if (s + 200 > g.size) continue
            val mc = IntArray(9) { u32(g, s + 8 + 8 * it).toInt().coerceIn(0, 99) }
            val ms = LongArray(9) { u32(g, s + 12 + 8 * it) }
            l.add(Partie(txt(g, b + 11 + 7 * i, 7).ifEmpty { "Partie " + (i + 1) }, u32(g, s).toInt().coerceIn(0, 60), u32(g, s + 4), mc, ms,
                u32(g, s + 96) != 0L, u32(g, s + 100).toInt().coerceIn(0, 9), txt(g, s + 140, 20),
                u32(g, s + 180) != 0L, u32(g, s + 184) != 0L, u32(g, s + 188).toInt().coerceIn(0, 99)))
        }
        return l
    }
}

@Composable
fun EcranRayman3() {
    val act = LocalContext.current as StationActivity
    val portee = rememberCoroutineScope()
    var parties by remember { mutableStateOf<List<Rayman3.Partie>?>(null) }
    var message by remember { mutableStateOf("") }
    var occupe by remember { mutableStateOf(false) }
    fun montrer(b: ByteArray?, d: String) {
        val p = if (b == null) null else Rayman3.lire(b)
        parties = p
        message = when { b == null -> "Pas de réponse : GC Station à l'écran (hors partie), carte branchée ?"
            p == null -> "Ce n'est pas une sauvegarde de Rayman 3 GameCube"; p.isEmpty() -> "Aucune partie dans cette sauvegarde"; else -> d }
    }
    val choisir = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { u: Uri? ->
        if (u != null) portee.launch {
            val b = withContext(Dispatchers.IO) { try { act.contentResolver.openInputStream(u)?.use { it.readBytes() } } catch (e: Exception) { null } }
            montrer(b, "Lu depuis le téléphone")
        }
    }
    val pack = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { u: Uri? ->
        if (u != null) portee.launch {
            val n = withContext(Dispatchers.IO) { try { CartesR3.installer(act, u) } catch (e: Exception) { -1 } }
            message = if (n > 0) "Cartes installées : $n niveau(x). Elles s'affichent dans le compagnon de jeu." else "Pas de carte dans ce fichier"
        }
    }
        fun carte(slot: String) {
        occupe = true; message = "Lecture de la carte du slot $slot…"
        portee.launch {
            val b = withContext(Dispatchers.IO) {
                try {
                    val j = JSONObject(Api.get("/api/vraiecarte?s=$slot&op=liste", 35000) ?: return@withContext null)
                    val l = j.optJSONArray("l") ?: return@withContext null
                    val o = (0 until l.length()).map { l.getJSONObject(it) }.firstOrNull { it.optString("jeu").startsWith("GRH") && it.optString("n").contains("Save Data") }
                        ?: return@withContext ByteArray(0)
                    Api.getBytes("/api/vraiecarte?s=$slot&op=gci&i=" + o.getInt("i") + "&n=" + Api.enc(o.optString("n")), 35000)
                } catch (e: Exception) { null }
            }
            occupe = false
            if (b != null && b.isEmpty()) { parties = null; message = "Pas de sauvegarde Rayman 3 sur la carte du slot $slot" } else montrer(b, "Lu sur la carte du slot $slot")
        }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Doux("Ta progression lue dans la sauvegarde de Rayman 3 : sur la carte mémoire de la console (hors partie) ou un .gci du téléphone (Dolphin…).", taille = 12)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Bouton("Carte slot A", Modifier.weight(1f), plein = true, actif = !occupe) { carte("A") }
            Bouton("Carte slot B", Modifier.weight(1f), actif = !occupe) { carte("B") }
        }
        Bouton("Choisir un .gci du téléphone", Modifier.fillMaxWidth(), actif = !occupe) { choisir.launch(arrayOf("*/*")) }
        Bouton("Installer un pack de cartes des niveaux (.zip)", Modifier.fillMaxWidth(), actif = !occupe) { pack.launch(arrayOf("*/*")) }
        if (message.isNotEmpty()) Doux(message, taille = 12, couleur = C.Texte)
        for (p in parties.orEmpty()) PartieR3(p)
        Spacer(Modifier.height(14.dp))
    }
}

@Composable
private fun PartieR3(p: Rayman3.Partie) {
    val tampons = (0 until 9).count { p.mondeScore[it] > Rayman3.TAMPONS[it] }
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(p.nom, color = Color(0xFFFFC94A), fontSize = 20.sp, fontWeight = FontWeight.Black)
            Doux((if (p.fini) "Jeu fini" else "Monde atteint : " + Rayman3.MONDES[(p.atteint - 1).coerceIn(0, 8)]) +
                (if (p.niveau.isNotEmpty() && p.niveau != "endgame") " · reprise : " + p.niveau else ""), taille = 12)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((v, n) in listOf(p.cages.toString() + "/60" to "cages", "$tampons/9" to "tampons", String.format(java.util.Locale.FRANCE, "%,d", p.score) to "score"))
                    Column(Modifier.weight(1f)) { Text(v, color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold, maxLines = 1); Doux(n, taille = 11) }
            }
            Barre(p.cages / 60f, Color(0xFF3DDC84), hauteur = 6)
            Titre("Monde par monde", couleur = Color(0xFFFFC94A))
            for (k in 0 until 9) {
                val max = Rayman3.CAGES_MAX[k]
                val t = p.mondeScore[k] > Rayman3.TAMPONS[k]
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(Rayman3.MONDES[k], color = C.Texte, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
                        Doux("score " + String.format(java.util.Locale.FRANCE, "%,d", p.mondeScore[k]) +
                            if (t) " · tampon ✓" else " · tampon à " + String.format(java.util.Locale.FRANCE, "%,d", Rayman3.TAMPONS[k] + 1), taille = 11, lignes = 1)
                    }
                    Spacer(Modifier.width(8.dp))
                    Text(if (max == 0) "—" else p.mondeCages[k].toString() + "/" + max, color = if (max > 0 && p.mondeCages[k] >= max) C.Vert else C.Texte, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                }
            }
            val manque = (0 until 9).filter { Rayman3.CAGES_MAX[it] > p.mondeCages[it] }.map { Rayman3.MONDES[it] + " : " + (Rayman3.CAGES_MAX[it] - p.mondeCages[it]) + " cage(s)" } +
                (0 until 9).filter { p.mondeScore[it] <= Rayman3.TAMPONS[it] }.map { Rayman3.MONDES[it] + " : le tampon" }
            Titre("Pour finir à 100 %", couleur = C.Vert)
            if (manque.isEmpty()) Doux("Tout est fait : 60 cages et 9 tampons !", taille = 12, couleur = C.Vert)
            else for (m in manque) Doux("• $m", taille = 12)
            Doux("Bonus GameCube : niveaux 2D " + (if (p.cauchemar) "Cauchemar ✓" else "Cauchemar ✗") + " · " + (if (p.folie) "Folie ✓" else "Folie ✗") +
                " · mini-jeux débloqués : " + p.miniJeux, taille = 11)
        }
    }
}

/** build 217 : Rayman 3 en direct dans le compagnon de jeu (version europeenne GRHP41). Adresses trouvees en comparant
 *  3 photos de la memoire (Moor_00) : copie fixe de la position de Rayman (0x804AAC94, x y z, Z vers le haut), position
 *  de la camera (0x80496A10), nom du niveau en cours (0x803E712D). A verifier dans les autres mondes. */
object JeuRayman3 : ModuleJeu("Rayman 3", "GRHP41") {
    private const val POS = 0x804AAC94L
    private const val CAM = 0x80496A10L
    private const val NIVEAU = 0x803E712DL
    private val chemins = HashMap<String, ArrayList<FloatArray>>()
    private val MONDES = listOf("intro" to "Le Concile des Fées", "wood" to "La Forêt de Claire-Feuille", "knaar" to "Le Désert des Knaarens",
        "mount" to "Le Sommet d'Outre-Nuées", "lept" to "La Tour du Leptys", "tower" to "La Tour du Leptys", "menu" to "Menu")

    override fun lire(l: Lecteur, id: String): Fiche? {
        val niv = (l.texte(NIVEAU, 16) ?: "").substringBefore('\u0000').filter { it.code in 33..126 }
        if (niv.isEmpty() || niv.length > 15) return null
        val x = l.fini(l.f32(POS), 1e5f); val y = l.fini(l.f32(POS + 4), 1e5f); val z = l.fini(l.f32(POS + 8), 1e5f)
        val cx = l.fini(l.f32(CAM), 1e5f); val cy = l.fini(l.f32(CAM + 4), 1e5f)
        val monde = MONDES.firstOrNull { niv.lowercase().startsWith(it.first) }?.second
        val blocs = ArrayList<Bloc>()
        if (x != null && y != null) {
            val ch = chemins.getOrPut(niv) { ArrayList() }
            val d = ch.lastOrNull()
            if (d == null || (d[0] - x) * (d[0] - x) + (d[1] + y) * (d[1] + y) > 4f) { ch.add(floatArrayOf(x, -y)); if (ch.size > 800) ch.removeAt(0) }
            val pions = ArrayList<Pion>()
            if (cx != null && cy != null) pions.add(Pion(cx, -cy, 0xFF5FD3FF, "c"))
            pions.add(Pion(x, -y, 0xFFFF3B3B, "R"))
            val carte = CartesR3.carte(niv)
            val pts = ArrayList<FloatArray>(ch); for (p in pions) pts.add(floatArrayOf(p.x, p.z))
            val n = ch.size
            if (pts.size >= 2) blocs.add(Bloc("Plan du niveau ($niv)", plan = Plan(pts, List(pts.size) { if (it < n) 0xFFFFC94A else 0x00000000 },
                (1 until n).map { intArrayOf(it - 1, it) }, pions, petits = (0 until pts.size).toSet(),
                fond = carte?.first, fondBornes = carte?.second),
                texte = "Ton chemin dans ce niveau (il se dessine en avançant), R : Rayman, c : la caméra. Vu de dessus." +
                    if (carte == null) " Carte du niveau : installe le pack de cartes (Plus > Rayman 3)." else ""))
        }
        blocs.add(Bloc("Pour finir à 100 %", texte = "Cages, tampons et scores monde par monde : Plus > Rayman 3 (lus dans ta sauvegarde)."))
        return Fiche(id, monde ?: "Rayman 3", "Niveau $niv", 0xFFE0A030, 0xFF2A1A08,
            grands = listOfNotNull(if (x != null && y != null) "position" to (x.toInt().toString() + " ; " + y.toInt()) else null,
                if (z != null) "altitude" to z.toInt().toString() else null, "niveau" to niv), blocs = blocs)
    }
}


/** build 221 : les cartes des niveaux (pack .zip fabrique a part depuis les fichiers du jeu, installe sur le telephone :
 *  jamais dans l'appli) : rayman3/<niveau>.png et rayman3/<niveau>.txt (« x0 y0 x1 y1 » : bornes du monde de l'image) */
object CartesR3 {
    @Volatile var dossier: java.io.File? = null
    private val cache = HashMap<String, Pair<android.graphics.Bitmap, FloatArray>?>()
    fun carte(niveau: String): Pair<android.graphics.Bitmap, FloatArray>? {
        val k = niveau.lowercase()
        synchronized(cache) { if (cache.containsKey(k)) return cache[k] }
        val d = dossier ?: return null
        val r = try {
            val b = java.io.File(d, "$k.txt").readText().trim().split(Regex("\\s+")).map { it.toFloat() }
            val img = android.graphics.BitmapFactory.decodeFile(java.io.File(d, "$k.png").path)
            if (img == null || b.size < 4) null else img to floatArrayOf(b[0], -b[3], b[2], -b[1])   // (y vers le haut -> z du plan)
        } catch (e: Exception) { null }
        synchronized(cache) { cache[k] = r }
        return r
    }
    fun vider() { synchronized(cache) { cache.clear() } }
    /** installe un pack .zip (rayman3/*.png, *.txt) ; rend le nombre de cartes */
    fun installer(ctx: android.content.Context, u: android.net.Uri): Int {
        val d = java.io.File(ctx.filesDir, "cartes-r3"); d.mkdirs()
        var n = 0
        ctx.contentResolver.openInputStream(u)?.use { ins ->
            java.util.zip.ZipInputStream(ins).use { z ->
                while (true) {
                    val e = z.nextEntry ?: break
                    val nom = e.name.substringAfterLast('/').lowercase()
                    if (e.isDirectory || !(nom.endsWith(".png") || nom.endsWith(".txt")) || nom.contains("..")) continue
                    java.io.File(d, nom).outputStream().use { z.copyTo(it) }
                    if (nom.endsWith(".png")) n++
                }
            }
        }
        dossier = d; vider()
        return n
    }
}
