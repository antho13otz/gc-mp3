package fr.gcstation.telecommande

// Appli build 336 : Zelda The Wind Waker en 3D. Les lieux viennent d'un pack (fabrique a part depuis les fichiers du jeu,
// jamais dans l'appli) : <lieu>.r3v (meme format que Rayman 3 et Mario Kart), <lieu>_tNNN.png, <lieu>_tex.txt,
// <lieu>_anim.txt, <lieu>_obj.txt (coffres « t »). Un lieu par etape du jeu (toutes ses salles, a leur place) ; la Grande
// Mer : une scene par ile (sea_NN, numero de la case de la carte marine) et la vue d'ensemble (sea).
// Repere : celui du jeu (x, y vers le haut, z) devient (x, -z, y) a l'echelle 1/100, comme Mario Kart.

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

object CartesWW {
    @Volatile var dossier: java.io.File? = null
    /** les iles de la Grande Mer (case n de la carte marine, de 1 a 49, rangee par rangee depuis le nord-ouest) */
    val ILES = listOf("Forteresse Maudite", "Île de l'Étoile", "Île de la fée du Nord", "Île des Bourrasques", "Île du Croissant de lune",
        "Îles des Sept Étoiles", "Île du Belvédère", "Récif des Quatre Yeux", "Île de la Mère et l'Enfant", "Île Spectacle",
        "Mercantîle", "Île de l'Empreinte", "Île du Dragon", "Plateforme de vol", "Île de la fée de l'Ouest",
        "Pic Rocheux", "Île de Tingle", "Triangle du Nord", "Île de la fée de l'Est", "Mont Feu",
        "Archipel de la Ceinture d'étoiles", "Récif des Trois Yeux", "Île du Grand Poisson", "Récif du Cyclope", "Récif des Six Yeux",
        "Tour des Dieux", "Triangle de l'Est", "Île de la fée aux Épines", "Rocher Aiguille", "Îlot d'Acier",
        "Île du Gardien de pierre", "Triangle du Sud", "Oasis privée", "Île aux Bombes", "Rocher du Pic aux oiseaux",
        "Steppe du Diamant", "Récif des Cinq Yeux", "Île du Requin", "Île de la fée du Sud", "Île de l'Anneau de glace",
        "Île du Bois Sacré", "Plateau de la Falaise", "Île du Fer à cheval", "Île de l'Aurore", "Île de la Pierre tombale",
        "Récif des Deux Yeux", "Îles Angulaires", "Parcours nautique", "Îles des Cinq Étoiles")
    /** les autres lieux connus : etape du jeu (en minuscules) -> (rubrique, nom) */
    private val LIEUX = linkedMapOf(
        "m_newd2" to ("Donjons" to "Caverne du Dragon"), "m_dragb" to ("Donjons" to "Caverne du Dragon — boss"),
        "kindan" to ("Donjons" to "Bois Défendus"), "kinmb" to ("Donjons" to "Bois Défendus — mini-boss"), "kinboss" to ("Donjons" to "Bois Défendus — boss"),
        "siren" to ("Donjons" to "Tour des Dieux"), "sirenmb" to ("Donjons" to "Tour des Dieux — mini-boss"), "sirenb" to ("Donjons" to "Tour des Dieux — boss"),
        "majyue" to ("Donjons" to "Forteresse Maudite"), "mjtower" to ("Donjons" to "Forteresse Maudite — tour"),
        "majroom" to ("Donjons" to "Forteresse Maudite — salles"), "ma2room" to ("Donjons" to "Forteresse Maudite — salles 2"), "ma3room" to ("Donjons" to "Forteresse Maudite — salles 3"),
        "m_dai" to ("Donjons" to "Temple de la Terre"), "m_daimb" to ("Donjons" to "Temple de la Terre — mini-boss"), "m_daib" to ("Donjons" to "Temple de la Terre — boss"),
        "kaze" to ("Donjons" to "Temple du Vent"), "kazemb" to ("Donjons" to "Temple du Vent — mini-boss"), "kazeb" to ("Donjons" to "Temple du Vent — boss"),
        "gtower" to ("Donjons" to "Tour de Ganon"), "ganona" to ("Donjons" to "Tour de Ganon — A"), "ganonb" to ("Donjons" to "Tour de Ganon — B"),
        "ganonc" to ("Donjons" to "Tour de Ganon — C"), "ganond" to ("Donjons" to "Tour de Ganon — D"), "ganone" to ("Donjons" to "Tour de Ganon — E"),
        "ganonj" to ("Donjons" to "Tour de Ganon — J"), "ganonk" to ("Donjons" to "Tour de Ganon — K"), "ganonl" to ("Donjons" to "Tour de Ganon — L"),
        "ganonm" to ("Donjons" to "Tour de Ganon — M"), "ganonn" to ("Donjons" to "Tour de Ganon — N"),
        "xboss0" to ("Donjons" to "Retour des boss — 1"), "xboss1" to ("Donjons" to "Retour des boss — 2"), "xboss2" to ("Donjons" to "Retour des boss — 3"), "xboss3" to ("Donjons" to "Retour des boss — 4"),
        "hyrule" to ("Hyrule" to "Château d'Hyrule"), "hyroom" to ("Hyrule" to "Château d'Hyrule — intérieur"), "kenroom" to ("Hyrule" to "Salle de l'Épée"),
        "linkrm" to ("Maisons et intérieurs" to "Maison de Link"), "linkug" to ("Maisons et intérieurs" to "Maison de Link — sous-sol"),
        "orichh" to ("Maisons et intérieurs" to "Maison d'Orca"), "omori" to ("Maisons et intérieurs" to "Bois Sacré — intérieur"),
        "pjavdou" to ("Maisons et intérieurs" to "Grotte de Jabun"), "pship" to ("Bateaux" to "Bateau pirate"), "pship2" to ("Bateaux" to "Bateau pirate (2)"),
        "pship3" to ("Bateaux" to "Bateau pirate (3)"), "shipd" to ("Bateaux" to "Vaisseau fantôme"), "tincle" to ("Maisons et intérieurs" to "Île de Tingle — tour"))
    fun nom(a: String): String {
        val k = a.lowercase()
        if (k == "sea") return "La Grande Mer (vue d'ensemble)"
        if (k.startsWith("sea_")) return k.removePrefix("sea_").toIntOrNull()?.let { ILES.getOrNull(it - 1) } ?: k
        LIEUX[k]?.let { return it.second }
        Regex("^cave(\\d+)$").find(k)?.let { return "Grotte " + it.groupValues[1].toInt() }
        Regex("^fairy(\\d+)$").find(k)?.let { return "Fontaine des fées " + it.groupValues[1].toInt() }
        Regex("^tf_(\\d+)$").find(k)?.let { return "Grotte du Triforce " + it.groupValues[1].toInt() }
        return a
    }
    fun rubrique(a: String): String {
        val k = a.lowercase()
        if (k == "sea" || k.startsWith("sea_")) return "La Grande Mer"
        LIEUX[k]?.let { return it.first }
        if (k.startsWith("cave") || k.startsWith("fairy") || k.startsWith("tf_")) return "Grottes et fontaines"
        return "Autres lieux (nom du jeu)"
    }
    val ORDRE = listOf("La Grande Mer", "Donjons", "Hyrule", "Maisons et intérieurs", "Bateaux", "Grottes et fontaines", "Autres lieux (nom du jeu)")
    /** le lieu du pack pour l'etape et la salle lues dans le jeu (la mer : une scene par case) */
    fun lieu(scene: String, salle: Int): String = if (scene == "sea") (if (salle in 1..49) "sea_%02d".format(salle) else "sea") else scene.lowercase()
    /** une position du jeu dans le repere de la vue 3D */
    fun pos(x: Float, y: Float, z: Float) = floatArrayOf(x * 0.01f, -z * 0.01f, y * 0.01f)

    private val scenes = HashMap<String, SceneR3?>()
    fun scene(a: String): SceneR3? {
        synchronized(scenes) { if (scenes.containsKey(a)) return scenes[a] }
        val d = dossier ?: return null
        val sc = SceneR3.lire(d, a)
        synchronized(scenes) { scenes[a] = sc }
        return sc
    }
    fun installes(): List<String> = dossier?.listFiles()?.filter { it.name.endsWith(".r3v") }?.map { it.name.removeSuffix(".r3v") } ?: emptyList()
    /** installe un ou plusieurs packs (.zip, un zip de zips aussi) ; rend le nombre de lieux */
    fun installer(ctx: android.content.Context, u: Uri): Int {
        val d = java.io.File(ctx.filesDir, "cartes-ww"); d.mkdirs()
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

/** en jeu (compagnon) : le lieu en 3D, Link dessus (sa position lue en direct) */
private var derniereWW: FloatArray? = null
@Composable
fun LieuWW3D(e: JeuWW.Etat) {
    if (!e.aPos) return
    val a = CartesWW.lieu(e.scene, e.salle)
    val sc = remember(a) { CartesWW.scene(a) } ?: run {
        Doux("Le lieu en 3D : installe le pack Wind Waker (fiche du jeu).", taille = 10); return
    }
    val p = CartesWW.pos(e.x, e.y, e.z)
    val d0 = derniereWW
    if (d0 == null || (d0[0] - p[0]) * (d0[0] - p[0]) + (d0[1] - p[1]) * (d0[1] - p[1]) > 2.5f * 2.5f) derniereWW = p
    val cam = derniereWW?.takeIf { it !== p } ?: d0
    Titre("En 3D : " + CartesWW.nom(a))
    DessinVue3DGL(sc, p, cam, emptyList(), montrerRayman = true, distance = 22f)
    Doux("Point rouge : Link · carrés dorés : coffres.", taille = 10)
}

/** fiche du jeu : installer les packs, voir chaque lieu en 3D (hors ligne) */
@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
fun SectionWW(act: StationActivity) {
    if (CartesWW.dossier == null) CartesWW.dossier = java.io.File(act.filesDir, "cartes-ww")
    val portee = rememberCoroutineScope()
    var message by remember { mutableStateOf("") }
    var recharge by remember { mutableStateOf(0) }
    var occupe by remember { mutableStateOf(false) }
    val pack = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { us: List<Uri> ->
        if (us.isNotEmpty()) portee.launch {
            occupe = true; var total = 0
            for ((i, u) in us.withIndex()) {
                message = "Installation du pack ${i + 1} sur ${us.size}…"
                total += withContext(Dispatchers.IO) { try { CartesWW.installer(act, u) } catch (e: Exception) { 0 } }
            }
            occupe = false; recharge++
            message = if (total > 0) "Installé : $total lieu(x), ci-dessous et dans le compagnon de jeu." else "Pas de lieu dans ce(s) fichier(s)."
        }
    }
    val installes = remember(recharge) { CartesWW.installes() }
    var vu by remember { mutableStateOf<String?>(null) }
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Doux("Chaque lieu du jeu en 3D avec ses textures et ses coffres : les îles de la Grande Mer, les donjons, les maisons. En jeu, le compagnon montre Link dessus.", taille = 12)
        val n = vu
        if (n != null) {
            val sc = remember(n) { CartesWW.scene(n) }
            if (sc != null) Carte(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                        Text(CartesWW.nom(n), color = Color(0xFF7DF0A6), fontSize = 15.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                        Bouton("✕", hauteur = 32.dp) { vu = null }
                    }
                    DessinVue3DGL(sc, sc.centre, null, emptyList(), montrerRayman = false)
                }
            }
        }
        if (installes.isEmpty()) Doux("Aucun lieu installé.", taille = 12)
        val parRub = installes.groupBy { CartesWW.rubrique(it) }
        for (r in CartesWW.ORDRE) {
            val l = parRub[r] ?: continue
            Text(r, color = C.Texte, fontSize = 14.sp, fontWeight = FontWeight.Bold)
            // (la mer : vue d'ensemble puis les iles dans l'ordre de la carte ; ailleurs : par nom)
            val tri = if (r == "La Grande Mer") l.sortedBy { if (it == "sea") -1 else it.removePrefix("sea_").toIntOrNull() ?: 99 } else l.sortedBy { CartesWW.nom(it) }
            androidx.compose.foundation.layout.FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                for (a in tri) Bouton(CartesWW.nom(a), plein = a == vu, hauteur = 38.dp) { vu = if (vu == a) null else a }
            }
        }
        Bouton("Installer / mettre à jour des packs Wind Waker (.zip)", Modifier.fillMaxWidth(), actif = !occupe) { pack.launch(arrayOf("*/*")) }
        if (message.isNotEmpty()) Doux(message, taille = 12, couleur = C.Texte)
    }
}
