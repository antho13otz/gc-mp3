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
import androidx.compose.runtime.DisposableEffect
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
    /** les iles de la Grande Mer (case n de la carte marine, de 1 a 49, rangee par rangee depuis le nord-ouest) : leurs
     *  noms dans le jeu francais (build 338 : lus dans les textes du jeu, data2/bmgres.arc, messages 12710 a 12758) */
    val ILES = listOf("Forteresse Maudite", "Île Étoilée", "Îlot Septentrional des Fées", "Île du Vent", "Île du Croissant", "Archipel Ursa Major", "Île aux Trois Tours", "Île de la Face Quatre", "Île Geminée", "Île des Binocles", "Mercantîle", "Pas de Chat", "Île du Dragon", "Tour de l'Envol", "Îlot Occidental des Fées", "Île de la Rocaille", "Île de Tingle", "Île Triangulaire du Nord", "Îlot Oriental des Fées", "Montagne de Feu", "Archipel d'Orion", "Île de la Face Trois", "Île du Poisson", "Île de la Face Une", "Île de la Face Six", "Tour des Dieux", "Île Triangulaire de l'Est", "Îlot Épineux des Fées", "Serrure de Pierre", "Canonnière", "Île de la Tête de Pierre", "Île Triangulaire du Sud", "Île de Qui-ça", "Île de la Bombe", "Île aux Nichoirs", "Île du Losange", "Île de la Face Cinq", "Île du Requin", "Îlot Méridional des Fées", "Île Gelée", "Île aux Forêts", "Saut de Roc", "Île du Fer à Cheval", "Île de l'Aurore", "Île de la Terre", "Île de la Face Deux", "Îlots Kif Kif", "Batodrome", "Archipel de Cassiopée")
    /** les autres lieux connus : etape du jeu (en minuscules) -> (rubrique, nom) */
    private val LIEUX = linkedMapOf(
        "m_newd2" to ("Donjons" to "Caverne du Dragon"), "m_dragb" to ("Donjons" to "Caverne du Dragon — boss"),
        "kindan" to ("Donjons" to "Bois Défendus"), "kinmb" to ("Donjons" to "Bois Défendus — mini-boss"), "kinboss" to ("Donjons" to "Bois Défendus — boss"),
        "siren" to ("Donjons" to "Tour des Dieux"), "sirenmb" to ("Donjons" to "Tour des Dieux — mini-boss"), "sirenb" to ("Donjons" to "Tour des Dieux — boss"),
        "majyue" to ("Donjons" to "Forteresse Maudite"), "mjtower" to ("Donjons" to "Forteresse Maudite — tour"),
        "majroom" to ("Donjons" to "Forteresse Maudite — salles"), "ma2room" to ("Donjons" to "Forteresse Maudite — salles 2"), "ma3room" to ("Donjons" to "Forteresse Maudite — salles 3"),
        "m_dai" to ("Donjons" to "Temple de la Terre"), "m_daimb" to ("Donjons" to "Temple de la Terre — mini-boss"), "m_daib" to ("Donjons" to "Temple de la Terre — boss"),
        "kaze" to ("Donjons" to "Temple du Vent"), "kazemb" to ("Donjons" to "Temple du Vent — mini-boss"), "kazeb" to ("Donjons" to "Temple du Vent — boss"),
        "gtower" to ("Donjons" to "Château de Ganon"), "ganona" to ("Donjons" to "Château de Ganon — A"), "ganonb" to ("Donjons" to "Château de Ganon — B"),
        "ganonc" to ("Donjons" to "Château de Ganon — C"), "ganond" to ("Donjons" to "Château de Ganon — D"), "ganone" to ("Donjons" to "Château de Ganon — E"),
        "ganonj" to ("Donjons" to "Château de Ganon — J"), "ganonk" to ("Donjons" to "Château de Ganon — K"), "ganonl" to ("Donjons" to "Château de Ganon — L"),
        "ganonm" to ("Donjons" to "Château de Ganon — M"), "ganonn" to ("Donjons" to "Château de Ganon — N"),
        "xboss0" to ("Donjons" to "Retour des boss — 1"), "xboss1" to ("Donjons" to "Retour des boss — 2"), "xboss2" to ("Donjons" to "Retour des boss — 3"), "xboss3" to ("Donjons" to "Retour des boss — 4"),
        "hyrule" to ("Hyrule" to "Château d'Hyrule"), "hyroom" to ("Hyrule" to "Château d'Hyrule — intérieur"), "kenroom" to ("Hyrule" to "Salle de l'Épée"),
        "linkrm" to ("Maisons et intérieurs" to "Maison de Link"), "linkug" to ("Maisons et intérieurs" to "Maison de Link — sous-sol"),
        "orichh" to ("Maisons et intérieurs" to "Maison d'Orca"), "omori" to ("Maisons et intérieurs" to "Île aux Forêts — intérieur"),
        "pjavdou" to ("Maisons et intérieurs" to "Grotte de Jabun"), "pship" to ("Bateaux" to "Bateau pirate"), "pship2" to ("Bateaux" to "Bateau pirate (2)"),
        "pship3" to ("Bateaux" to "Bateau pirate (3)"), "shipd" to ("Bateaux" to "Vaisseau Fantôme"), "tincle" to ("Maisons et intérieurs" to "Tour de Tingle"))
    fun nom(a: String): String {
        val k = a.lowercase()
        if (k == "sea") return "La Grande Mer (vue d'ensemble)"
        if (k.startsWith("sea_")) return k.removePrefix("sea_").toIntOrNull()?.let { ILES.getOrNull(it - 1) } ?: k
        LIEUX[k]?.let { return it.second }
        Regex("^cave(\\d+)$").find(k)?.let { return "Grotte " + it.groupValues[1].toInt() }
        Regex("^fairy(\\d+)$").find(k)?.let { return "Fontaine de fée " + it.groupValues[1].toInt() }
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
    // (build 358 : sans les fichiers des objets qui bougent (<lieu>_mob.r3v), qui apparaissaient comme des lieux)
    fun installes(): List<String> = dossier?.listFiles()?.filter { it.name.endsWith(".r3v") && !it.name.endsWith("_mob.r3v") }?.map { it.name.removeSuffix(".r3v") } ?: emptyList()
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
                // (build 343 : aussi Link : ww_link.ani (animations) et ww_link.peau (sa peau) ; avant, ignores a l'installation)
                // (build 353 : et les objets animes de chaque lieu : <lieu>_ani.r3a)
                // (build 364 : et les peaux des objets animes : <lieu>_vif.r3p (lues par AnimesWW.kt) ; avant, ignorees a l'installation)
                if (!(nom.endsWith(".png") || nom.endsWith(".txt") || nom.endsWith(".r3v") || nom.endsWith(".ani") || nom.endsWith(".peau") || nom.endsWith(".r3a") || nom.endsWith(".r3p"))) continue
                java.io.File(d, nom).outputStream().use { z.copyTo(it) }
                if (nom.endsWith(".r3v") && !nom.endsWith("_mob.r3v")) n++
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
    // build 338 : Link en 3D (pack : ww_link.ani) : son cap, sa vitesse ; sous l'eau : plus bas que la mer (hauteur 0)
    // sur la Grande Mer (nage)
    val nage = (e.scene == "sea" && e.y < -30f)
    // (build 340 : « 3e personne » : derriere Link, tourne comme lui ; « Vue du jeu » : la camera du jeu ; la vraie pose
    //  de Link lue en direct)
    val capApp = Math.atan2(-Math.cos(e.cap), Math.sin(e.cap)).toFloat()
    // (build 356 : les modeles du lieu reconnaissables en memoire : le direct cherche les objets et les suit)
    DisposableEffect(sc) { JeuWW.sigsPack = sc.animes?.sigsDirect ?: emptyMap(); onDispose { JeuWW.sigsPack = emptyMap() } }   // (build 370 : sans les objets secondaires)
    DessinVue3DGL(sc, p, cam, emptyList(), montrerRayman = true, distance = 22f, linkWW = floatArrayOf(e.cap.toFloat(), e.vitesse, if (nage) 1f else 0f),
        capPerso = capApp, poseWW = { JeuWW.poseLink() }, camJeu = { JeuWW.cameraJeu() },
        // (build 345 : le direct de Pulse : pose, position et camera a chaque image du jeu, rejouees sans a-coups)
        ecouteDirect = { JeuWW.ecouterDirect() },
        rejeu = { et ->
            val r = JeuWW.rejouerWW()
            et.directRejeu = r != null
            if (r != null) {
                val t = System.nanoTime()
                et.poseWW = r.first; et.tPoseWW = t
                et.cible = CartesWW.pos(r.second[0], r.second[1], r.second[2]); et.direct = true
                r.third?.let { JeuWW.vueDe(it) }?.let { et.camJeu = it; et.tCamJeu = t }
            }
            et.vifsWW = JeuWW.vifsRejoues; et.modelesVifs = if (et.vifsWW != null) JeuWW.modelesVifs else emptySet()
        })
    Doux("Link en 3D (pack Wind Waker) · carrés dorés : coffres.", taille = 10)
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

/** build 338 : Link en 3D (pack Wind Waker : ww_link.ani et ww_link_tNNN.png), son modele du jeu et ses animations cuites
 *  en images (attente, marche, course, nage, nage sur place) : voir linkww.py. Les u v et couleurs sont communs a toutes
 *  les images ; seules les positions changent. */
class LinkAnime(val groupes: List<Groupe>, val anims: Map<String, Anim>) {
    class Groupe(val texture: Int, val mode: Int, val n: Int, val uvc: java.nio.ByteBuffer)
    class Anim(val images: Int, val ips: Float, val pos: Array<Array<java.nio.FloatBuffer>>)   // [image][groupe]
    companion object {
        fun lire(f: java.io.File): LinkAnime? = try {
            if (!f.exists()) null else {
                val b = java.nio.ByteBuffer.wrap(f.readBytes()).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x314C5757) null else {                         // "WWL1"
                    val ng = b.int; val na = b.int
                    val gs = ArrayList<Groupe>()
                    for (g in 0 until ng) {
                        val t = b.int; val m = b.int; val n = b.int
                        val bb = java.nio.ByteBuffer.allocateDirect(n * 12).order(java.nio.ByteOrder.nativeOrder())
                        val src = ByteArray(n * 12); b.get(src); bb.put(src); bb.position(0)
                        gs.add(Groupe(t, m, n, bb))
                    }
                    val an = HashMap<String, Anim>()
                    for (a in 0 until na) {
                        val nb = ByteArray(16); b.get(nb); val nom = String(nb).trimEnd('\u0000')
                        val ni = b.int; val ips = b.float
                        val im = Array(ni) { Array(ng) { g ->
                            val fb = java.nio.ByteBuffer.allocateDirect(gs[g].n * 12).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer()
                            for (k in 0 until gs[g].n * 3) fb.put(b.float)
                            fb.position(0); fb } }
                        an[nom] = Anim(ni, ips, im)
                    }
                    LinkAnime(gs, an)
                }
            }
        } catch (e: Throwable) { null }
    }
}

/** build 340 : la peau de Link (pack : ww_link.peau, voir linkww.py) : pour chaque sommet, jusqu'a 4 articulations, leurs
 *  poids et la position du sommet dans chacune. Avec les 42 matrices du monde lues dans le jeu (modele de Link +0x8C), la
 *  position de chaque sommet = somme des poids x matrice x position locale ; puis repere de l'appli (x, -z, y) / 100. */
class PeauLink(private val j: Array<ByteArray>, private val w: Array<FloatArray>, private val l: Array<FloatArray>, private val sortie: Array<java.nio.FloatBuffer>) {
    fun poser(g: Int, m: FloatArray): java.nio.FloatBuffer {
        val jj = j[g]; val ww = w[g]; val ll = l[g]; val o = sortie[g]; val n = jj.size / 4
        o.position(0)
        for (k in 0 until n) {
            var x = 0f; var y = 0f; var z = 0f
            for (q in 0 until 4) {
                val ji = jj[4 * k + q].toInt() and 255
                if (ji == 255 || ji >= 42) continue
                val p = ww[4 * k + q]; val b = ji * 12; val c = 12 * k + 3 * q
                val lx = ll[c]; val ly = ll[c + 1]; val lz = ll[c + 2]
                x += p * (m[b] * lx + m[b + 1] * ly + m[b + 2] * lz + m[b + 3])
                y += p * (m[b + 4] * lx + m[b + 5] * ly + m[b + 6] * lz + m[b + 7])
                z += p * (m[b + 8] * lx + m[b + 9] * ly + m[b + 10] * lz + m[b + 11])
            }
            o.put(x * 0.01f).put(-z * 0.01f).put(y * 0.01f)
        }
        o.position(0); return o
    }
    companion object {
        fun lire(f: java.io.File, la: LinkAnime): PeauLink? = try {
            if (!f.exists()) null else {
                val b = java.nio.ByteBuffer.wrap(f.readBytes()).order(java.nio.ByteOrder.LITTLE_ENDIAN)
                if (b.int != 0x31505757) null else {                         // "WWP1"
                    val ng = b.int
                    if (ng != la.groupes.size) null else {
                        val J = arrayOfNulls<ByteArray>(ng); val W = arrayOfNulls<FloatArray>(ng); val L = arrayOfNulls<FloatArray>(ng)
                        for (g in 0 until ng) {
                            val n = b.int
                            if (n != la.groupes[g].n) throw IllegalStateException()
                            val jj = ByteArray(4 * n); val ww = FloatArray(4 * n); val ll = FloatArray(12 * n)
                            for (k in 0 until n) {
                                b.get(jj, 4 * k, 4)
                                for (q in 0 until 4) ww[4 * k + q] = b.float
                                for (q in 0 until 12) ll[12 * k + q] = b.float
                            }
                            J[g] = jj; W[g] = ww; L[g] = ll
                        }
                        PeauLink(Array(ng) { J[it]!! }, Array(ng) { W[it]!! }, Array(ng) { L[it]!! },
                            Array(ng) { java.nio.ByteBuffer.allocateDirect(la.groupes[it].n * 12).order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer() })
                    }
                }
            }
        } catch (e: Throwable) { null }
    }
}
