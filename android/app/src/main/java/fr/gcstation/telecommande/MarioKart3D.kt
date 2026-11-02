package fr.gcstation.telecommande

// Appli build 266 : Mario Kart Double Dash en 3D. Les circuits viennent d'un pack (fabrique a part depuis les fichiers du jeu,
// jamais dans l'appli) : mkdd/<circuit>.r3v (meme format que Rayman 3), <circuit>_tNNN.png, <circuit>_tex.txt,
// <circuit>_obj.txt (boites a objets « i », depart « d »), <circuit>_carte.png (mini-carte du jeu).
// Repere : celui du jeu (x, y vers le haut, z) devient (x, -z, y) a l'echelle 1/100.

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

object CartesMK {
    @Volatile var dossier: java.io.File? = null
    const val ECHELLE = 0.01f
    /** les circuits dans l'ordre des coupes, puis les arenes : (archive, nom francais) */
    val CIRCUITS = listOf(
        "Coupe Champignon" to listOf("luigi" to "Circuit Luigi", "peach" to "Plage Peach", "babyluigi" to "Baby Park", "desert" to "Désert Sec Sec"),
        "Coupe Fleur" to listOf("nokonoko" to "Pont Champignon", "mario" to "Circuit Mario", "daisy" to "Croisière Daisy", "waluigi" to "Stade Waluigi"),
        "Coupe Étoile" to listOf("snow" to "Royaume Sorbet", "patapata" to "Ville Champignon", "yoshi" to "Circuit Yoshi", "donkey" to "Montagne DK"),
        "Coupe Spéciale" to listOf("wario" to "Colisée Wario", "diddy" to "Jungle Dino Dino", "koopa" to "Château de Bowser", "rainbow" to "Route Arc-en-ciel"),
        "Arènes de bataille" to listOf("mini1" to "Manoir de Luigi", "mini2" to "Nintendo GameCube", "mini3" to "Block City", "mini5" to "Arène 5",
            "mini7" to "Cookie Land", "mini8" to "Arène 8"))
    fun nom(a: String) = CIRCUITS.flatMap { it.second }.firstOrNull { it.first == a }?.second ?: a
    /** numero de circuit du jeu (ECourseID, lu en course) -> archive */
    fun archive(id: Int): String? = when (id) {
        0x21 -> "babyluigi"; 0x22 -> "peach"; 0x23 -> "daisy"; 0x24 -> "luigi"; 0x25 -> "mario"; 0x26 -> "yoshi"; 0x28 -> "nokonoko"
        0x29 -> "patapata"; 0x2A -> "waluigi"; 0x2B -> "wario"; 0x2C -> "diddy"; 0x2D -> "donkey"; 0x2F -> "koopa"; 0x31 -> "rainbow"
        0x32 -> "desert"; 0x33 -> "snow"; else -> null
    }
    /** une position du jeu dans le repere de la vue 3D */
    fun pos(x: Float, y: Float, z: Float) = floatArrayOf(x * ECHELLE, -z * ECHELLE, y * ECHELLE)

    private val scenes = HashMap<String, SceneR3?>()
    fun scene(a: String): SceneR3? {
        synchronized(scenes) { if (scenes.containsKey(a)) return scenes[a] }
        val d = dossier ?: return null
        val sc = SceneR3.lire(d, a)
        synchronized(scenes) { scenes[a] = sc }
        return sc
    }
    fun installes(): List<String> = dossier?.listFiles()?.filter { it.name.endsWith(".r3v") }?.map { it.name.removeSuffix(".r3v") } ?: emptyList()
    /** installe un ou plusieurs packs (.zip, un zip de zips aussi) ; rend le nombre de circuits */
    fun installer(ctx: android.content.Context, u: Uri): Int {
        val d = java.io.File(ctx.filesDir, "cartes-mk"); d.mkdirs()
        var n = 0
        fun lire(z: java.util.zip.ZipInputStream, prof: Int) {
            while (true) {
                val e = z.nextEntry ?: break
                val nom = e.name.substringAfterLast('/').lowercase()
                if (e.isDirectory || nom.contains("..")) continue
                if (nom.endsWith(".zip")) { if (prof < 2) lire(java.util.zip.ZipInputStream(z), prof + 1); continue }
                if (!(nom.endsWith(".png") || nom.endsWith(".txt") || nom.endsWith(".r3v"))) continue
                java.io.File(d, nom).outputStream().use { z.copyTo(it) }
                if (nom.endsWith(".r3v")) n++
            }
        }
        ctx.contentResolver.openInputStream(u)?.use { ins -> java.util.zip.ZipInputStream(ins).use { z -> lire(z, 0) } }
        dossier = d; synchronized(scenes) { scenes.clear() }
        return n
    }
}

/** en course (compagnon de jeu) : le circuit en 3D, la camera derriere ton kart, les autres karts aux couleurs de leur pilote */
private var derniere: FloatArray? = null
@Composable
fun CircuitMK3D(e: JeuMKDD.Etat) {
    val a = CartesMK.archive(e.circuit) ?: return
    val sc = remember(a) { CartesMK.scene(a) } ?: run {
        Doux("Le circuit en 3D : installe le pack Mario Kart (fiche du jeu).", taille = 10); return
    }
    val moi = e.karts.firstOrNull { it.n == 0 } ?: return
    val p = CartesMK.pos(moi.x, moi.y, moi.z)
    // la camera : derriere le kart (d'apres la derniere position un peu eloignee)
    val d0 = derniere
    if (d0 == null || (d0[0] - p[0]) * (d0[0] - p[0]) + (d0[1] - p[1]) * (d0[1] - p[1]) > 2.5f * 2.5f) derniere = p
    val cam = derniere?.takeIf { it !== p } ?: d0
    val autres = e.karts.filter { it.n != 0 }
    val pions = FloatArray(autres.size * 4)
    for ((i, k) in autres.withIndex()) {
        val q = CartesMK.pos(k.x, k.y, k.z)
        pions[i * 4] = q[0]; pions[i * 4 + 1] = q[1]; pions[i * 4 + 2] = q[2]
        pions[i * 4 + 3] = java.lang.Float.intBitsToFloat((if (k.pilote > 0) JeuMKDD.PERSO_COUL[k.pilote] else 0xFFB8C7FFL).toInt())
    }
    Titre("En 3D")
    DessinVue3DGL(sc, p, cam, emptyList(), montrerRayman = true, pions = pions, distance = 28f)
    Doux("Point rouge : toi · les autres karts aux couleurs de leur pilote · boîtes arc-en-ciel : boîtes à objets.", taille = 10)
}

/** fiche du jeu : installer les packs, voir chaque circuit en 3D (hors ligne) */
@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
fun SectionMKDD(act: StationActivity) {
    if (CartesMK.dossier == null) CartesMK.dossier = java.io.File(act.filesDir, "cartes-mk")
    val portee = rememberCoroutineScope()
    var message by remember { mutableStateOf("") }
    var recharge by remember { mutableStateOf(0) }
    var occupe by remember { mutableStateOf(false) }
    val pack = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { us: List<Uri> ->
        if (us.isNotEmpty()) portee.launch {
            occupe = true; var total = 0
            for ((i, u) in us.withIndex()) {
                message = "Installation du pack ${i + 1} sur ${us.size}…"
                total += withContext(Dispatchers.IO) { try { CartesMK.installer(act, u) } catch (e: Exception) { 0 } }
            }
            occupe = false; recharge++
            message = if (total > 0) "Installé : $total circuit(s), ci-dessous et dans le compagnon de jeu (en course)." else "Pas de circuit dans ce(s) fichier(s)."
        }
    }
    val installes = remember(recharge) { CartesMK.installes().toSet() }
    var vu by remember { mutableStateOf<String?>(null) }
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Doux("Chaque circuit en 3D avec ses textures, ses boîtes à objets et la ligne de départ. En course, le compagnon de jeu montre les karts dessus.", taille = 12)
        val n = vu
        if (n != null) {
            val sc = remember(n) { CartesMK.scene(n) }
            if (sc != null) Carte(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        Text(CartesMK.nom(n), color = Color(0xFFFF5A5A), fontSize = 15.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                        Bouton("✕", hauteur = 32.dp) { vu = null }
                    }
                    DessinVue3DGL(sc, sc.centre, null, emptyList(), montrerRayman = false)
                }
            }
        }
        if (installes.isEmpty()) Doux("Aucun circuit installé.", taille = 12)
        for ((coupe, l) in CartesMK.CIRCUITS) {
            val ici = l.filter { it.first in installes }
            if (ici.isEmpty()) continue
            Text(coupe, color = C.Texte, fontSize = 14.sp, fontWeight = FontWeight.Bold)
            androidx.compose.foundation.layout.FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((a, nm) in ici) Bouton(nm, plein = a == vu, hauteur = 38.dp) { vu = if (vu == a) null else a }
            }
        }
        Bouton("Installer / mettre à jour des packs Mario Kart (.zip)", Modifier.fillMaxWidth(), actif = !occupe) { pack.launch(arrayOf("*/*")) }
        if (message.isNotEmpty()) Doux(message, taille = 12, couleur = C.Texte)
    }
}
