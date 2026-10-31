@file:OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class, androidx.compose.material3.ExperimentalMaterial3Api::class)

package fr.gcstation.telecommande

// Appli : Jeux. Grille des jaquettes (la console les envoie en CMPR, decodees par Cmpr, gardees sur le
// telephone), recherche, favoris, pastilles ; fiche du jeu : lancer (Pulse, patchs de secours, lanceur de secours),
// cheats (choisir, ajouter, modifier), pastille, reglages du jeu, sauvegardes, raccourci sur l'ecran d'accueil.

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.util.LruCache
import android.widget.LinearLayout
import android.widget.Toast
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.asCoroutineDispatcher
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.Executors

/** un jeu de la liste : [nom, id, lancements, minutes, favori, dernier lancement] ; i = sa place (jaquette, lancement) */
class Jeu(val x: JSONArray, val i: Int) {
    val nom: String get() = x.optString(0)
    val id: String get() = x.optString(1)
    val lancements: Int get() = x.optInt(2)
    val minutes: Int get() = x.optInt(3)
    val favori: Boolean get() = x.optInt(4) != 0
    val dernier: Long get() = x.optLong(5)
}

object Jaquettes {
    val mem = object : LruCache<String, ImageBitmap>(160) { }
    private val fil = Executors.newSingleThreadExecutor().asCoroutineDispatcher()   // (une a la fois : la console lit sa carte SD)
    /** jaquette d'un jeu : memoire, puis le telephone, puis la console */
    suspend fun charger(dir: File, id: String, i: Int): ImageBitmap? {
        mem.get(id)?.let { return it }
        return withContext(fil) {
            mem.get(id)?.let { return@withContext it }
            val disk = File(dir, "jaquettes/$id.png")
            var bm: Bitmap? = if (disk.exists()) BitmapFactory.decodeFile(disk.path) else null
            if (bm == null) {
                val got: Bitmap? = Cmpr.fetch(i)
                if (got != null) try { disk.parentFile?.mkdirs(); FileOutputStream(disk).use { o -> got.compress(Bitmap.CompressFormat.PNG, 100, o) } } catch (e: Exception) { }
                bm = got
            }
            val ib = bm?.asImageBitmap()
            if (ib != null) mem.put(id, ib)
            ib
        }
    }
    fun bitmap(dir: File, id: String): Bitmap? { val f = File(dir, "jaquettes/$id.png"); return if (f.exists()) BitmapFactory.decodeFile(f.path) else null }
}

private val CP_COL = listOf(Color(0x66FFFFFF), Color(0xFF46DC78), Color(0xFFFFBE3C), Color(0xFFFF783C), Color(0xFFFF4650))
private val CP_NOM = listOf("Aucune", "Jouable", "Marche mal", "Écran noir", "Plantage")

@Composable
fun EcranJeux() {
    val act = LocalContext.current as StationActivity
    var jeux by remember { mutableStateOf<List<Jeu>?>(null) }
    var msg by remember { mutableStateOf("Chargement des jeux (la console fait la liste)…") }
    var q by remember { mutableStateOf("") }
    var filtre by remember { mutableIntStateOf(0) }            // 0 tous, 1 favoris, 2 deja joues
    val compat = remember { mutableStateMapOf<String, Int>() }
    var fiche by remember { mutableStateOf<Jeu?>(null) }
    var recharge by remember { mutableIntStateOf(0) }
    val dernier = remember { LastLaunch(act) { id -> jeux?.firstOrNull { it.id == id }?.let { fiche = it } } }
    var garde by remember { mutableLongStateOf(0L) }           // (hors-ligne : date de la liste gardee ; 0 : liste de la console)
    var tout by remember { mutableStateOf<String?>(null) }    // (« Tout garder pour hors-ligne » en cours : ce qu'il fait)
    var toutAnnule by remember { mutableStateOf(false) }
    var etagere by remember { mutableStateOf(false) }       // (l'etagere de collection : Etagere.kt)
    var mur by remember { mutableStateOf(false) }           // (le mur de jaquettes : MurJaquettes.kt)
    var choixEnvoi by remember { mutableStateOf(false) }     // (v301 : jaquettes et disques envoyes a la console)
    var avecHd by remember { mutableStateOf(true) }
    var envoi by remember { mutableStateOf<String?>(null) }
    var envoiAnnule by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    LaunchedEffect(St.listes, recharge, St.connecte) {
        val r = if (St.connecte) lire("/api/games", 15000) else null
        val c = if (r != null) lire("/api/compat") else null
        val (rr, cc) = if (r != null) { Hors.garderJeux(act, r, c); garde = 0L; r to c }
                       else Hors.jeux(act)?.let { garde = it.third; it.first to it.second } ?: run { msg = if (St.connecte) "Pas de réponse de la console" else "Hors-ligne : aucune liste gardée (connecte-toi une fois à la console)"; return@LaunchedEffect }
        val l = ArrayList<Jeu>()
        try { val j = JSONArray(rr); for (k in 0 until j.length()) { val g = j.getJSONArray(k); if (g.optString(1).isNotEmpty()) l.add(Jeu(g, k)) } } catch (e: Exception) { }
        jeux = l
        if (cc != null) lireCompat(cc, compat)
    }
    val pj = St.etat?.optLong("pj") ?: -1L
    LaunchedEffect(pj) { dernier.refresh(); if (dernier.pj >= 0 && pj > 0 && pj != dernier.pj) recharge++ }
    // (fiche demandee ailleurs : Recherche, raccourci)
    val aOuvrir = St.jeuAOuvrir
    LaunchedEffect(aOuvrir, jeux) {
        if (aOuvrir != null && jeux != null) { val id = aOuvrir.optString(1); fiche = jeux?.firstOrNull { it.id == id }; St.jeuAOuvrir = null }
    }
    val l = jeux
    val lq = q.trim().lowercase()
    val vus = remember(l, lq, filtre) {
        l?.filter { (lq.isEmpty() || it.nom.lowercase().contains(lq) || it.id.lowercase().contains(lq)) && (filtre == 0 || (filtre == 1 && it.favori) || (filtre == 2 && it.minutes > 0)) }
            ?.let { if (filtre == 2) it.sortedByDescending { g -> g.dernier } else it } ?: emptyList()
    }
    LazyVerticalGrid(GridCells.Adaptive(112.dp), Modifier.fillMaxSize().padding(horizontal = 14.dp), contentPadding = PaddingValues(bottom = 14.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        item(span = { GridItemSpan(maxLineSpan) }) {
            Column {
                Champ(q, { q = it }, "Chercher un jeu", icone = Ic.Chercher)
                Spacer(Modifier.height(8.dp))
                RangeeDePuces {
                    Puce("Tous", filtre == 0) { filtre = 0 }
                    Puce("★ Favoris", filtre == 1) { filtre = 1 }
                    Puce("Déjà joués", filtre == 2) { filtre = 2 }
                }
                Spacer(Modifier.height(8.dp))
                AndroidView({ dernier.view.also { v -> (v.parent as? android.view.ViewGroup)?.removeView(v) } }, Modifier.fillMaxWidth())
                if (garde > 0L) Doux("Hors-ligne · liste gardée le " + java.text.SimpleDateFormat("d/M à HH:mm", java.util.Locale.FRANCE).format(java.util.Date(garde)) + " · jaquettes, boîtes et notices gardées",
                    Modifier.padding(start = 4.dp, top = 8.dp), 12, C.Cyan)
                if (vus.isNotEmpty()) Row(Modifier.fillMaxWidth().padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Bouton("Étagère 3D", Modifier.weight(1f), plein = true, hauteur = 40.dp) { etagere = true }
                    Bouton("Mur de jaquettes", Modifier.weight(1f), hauteur = 40.dp) { mur = true }
                }
                if (St.connecte && l != null && l.isNotEmpty()) Bouton("Tout garder pour hors-ligne", Modifier.fillMaxWidth().padding(top = 8.dp), hauteur = 40.dp) {
                    toutAnnule = false; tout = "Préparation…"
                    scope.launch {
                        val n = Hors.toutGarder(act, l, { toutAnnule }) { k, t, nom -> tout = if (nom.isEmpty()) null else "Jeu " + (k + 1) + " / " + t + " : " + nom }
                        tout = null
                        Toast.makeText(act, if (toutAnnule) "Arrêté : ce qui est déjà gardé reste" else "Tout est gardé pour hors-ligne (" + l.size + " jeux, " + n + " pages de notices)", Toast.LENGTH_LONG).show()
                    }
                }
                if (St.connecte && l != null && l.isNotEmpty()) Bouton("Envoyer jaquettes et disques à la console", Modifier.fillMaxWidth().padding(top = 8.dp), hauteur = 40.dp) { choixEnvoi = true }
                Doux(if (l == null) msg else if (l.isEmpty()) "Aucun jeu (onglet JEUX de la GameCube)" else vus.size.toString() + " jeu" + (if (vus.size > 1) "x" else "") + " · toucher pour la fiche",
                    Modifier.padding(start = 4.dp, top = 8.dp), 12)
            }
        }
        items(vus, key = { it.id + "#" + it.i }) { g -> CelluleJeu(act, g, compat[g.id] ?: 0) { fiche = g } }
    }
    if (etagere) EtagereEcran(act, vus, { g -> etagere = false; fiche = g }) { etagere = false }
    if (mur) MurEcran(act, vus, { g -> mur = false; fiche = g }) { mur = false }
    fiche?.let { g -> FicheJeu(act, g, compat, { fiche = null }) { recharge++ } }
    // (jaquettes et disques telecharges par le telephone sur GameTDB, envoyes a la console : seulement ce qui lui manque)
    if (choixEnvoi) AlertDialog(onDismissRequest = { choixEnvoi = false }, containerColor = Color(0xFF1A0E30),
        title = { Text("Envoyer à la console", color = C.Texte) },
        text = { Doux("Le téléphone télécharge sur GameTDB ce qui manque à la GameCube (jaquettes, jaquettes complètes de la boîte, disques) et le lui envoie. La HD (zoom de la boîte sur la console) pèse 1 à 2 Mo par jeu : plus long.", taille = 13) },
        confirmButton = { TextButton({
            choixEnvoi = false; envoiAnnule = false; envoi = "Préparation…"
            scope.launch {
                val b = JaquettesConsole.envoyer(Hors.dir(act), avecHd, { envoiAnnule }) { k, t, txt -> envoi = if (txt.isEmpty()) null else "Jeu " + (k + 1) + " / " + t + " : " + txt }
                envoi = null
                Toast.makeText(act, (if (envoiAnnule) "Arrêté. " else "") + JaquettesConsole.resume(b), Toast.LENGTH_LONG).show()
            }
        }) { Text("Envoyer", color = C.Rose) } },
        dismissButton = { TextButton({ avecHd = !avecHd }) { Text(if (avecHd) "☑ Avec la HD" else "☐ Avec la HD", color = C.Texte) } })
    envoi?.let { m ->
        AlertDialog(onDismissRequest = { }, containerColor = Color(0xFF1A0E30),
            title = { Text("Envoi à la console", color = C.Texte) },
            text = { Doux(m + "\n\nTéléchargées sur GameTDB par le téléphone, puis envoyées à la GameCube.", taille = 13) },
            confirmButton = { TextButton({ envoiAnnule = true }) { Text("Arrêter", color = C.Rose) } })
    }
    tout?.let { m ->
        AlertDialog(onDismissRequest = { }, containerColor = Color(0xFF1A0E30),
            title = { Text("Tout garder pour hors-ligne", color = C.Texte) },
            text = { Doux(m + "\n\nJaquettes (console), boîtes 3D et disques (GameTDB, par Internet), notices (console).", taille = 13) },
            confirmButton = { TextButton({ toutAnnule = true }) { Text("Arrêter", color = C.Rose) } })
    }
}

private fun lireCompat(r: String, into: MutableMap<String, Int>) {
    try { val j = JSONObject(r); into.clear(); for (k in j.keys()) into[k] = j.optInt(k) } catch (e: Exception) { }
}

@Composable
private fun CelluleJeu(act: Activity, g: Jeu, cp: Int, onClick: () -> Unit) {
    var img by remember(g.id) { mutableStateOf<ImageBitmap?>(Jaquettes.mem.get(g.id)) }
    LaunchedEffect(g.id) { if (img == null) img = Jaquettes.charger(Hors.dir(act), g.id, g.i) }
    Column(Modifier.appui(onClick)) {
        Box(Modifier.fillMaxWidth().aspectRatio(0.72f).clip(RoundedCornerShape(16.dp)).background(Brush.verticalGradient(listOf(Color(0xFF2A1450), Color(0xFF140A26))))
            .border(1.dp, if (g.favori) C.Rose.copy(alpha = 0.7f) else C.Bord, RoundedCornerShape(16.dp))) {
            val b = img
            if (b != null) Image(b, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            else Column(Modifier.fillMaxSize().padding(10.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
                Icon(Ic.Jeux, null, tint = C.Violet.copy(alpha = 0.7f), modifier = Modifier.size(34.dp))
                Spacer(Modifier.height(6.dp))
                Doux(g.nom, taille = 11, lignes = 3, centre = true)
            }
            val dr = pays(g.id).first                      // (drapeau du pays du jeu, en bas a gauche de la jaquette)
            if (dr.isNotEmpty()) Box(Modifier.align(Alignment.BottomStart).padding(6.dp).clip(RoundedCornerShape(6.dp)).background(Color(0x99000000)).padding(horizontal = 4.dp, vertical = 1.dp)) {
                Text(dr, fontSize = 14.sp)
            }
            if (cp in 1..4) Box(Modifier.align(Alignment.TopEnd).padding(7.dp).size(16.dp).clip(CircleShape).background(CP_COL[cp]).border(2.dp, Color(0xFF0C0418), CircleShape))
            if (g.favori) Icon(Ic.Etoile, null, tint = C.Rose, modifier = Modifier.align(Alignment.TopStart).padding(6.dp).size(18.dp))
        }
        Spacer(Modifier.height(6.dp))
        Text(g.nom, color = C.Texte, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis)
        Doux(pays(g.id).first.let { if (it.isEmpty()) "" else "$it " } + g.id + if (g.minutes > 0) " · " + duree(g.minutes) else "", taille = 11, lignes = 1)
    }
}

/** pays du jeu, d'apres la 4e lettre de son code (GTEP01 : P = Europe) : drapeau et nom */
fun pays(id: String): Pair<String, String> = when (id.getOrNull(3)) {
    'E' -> "🇺🇸" to "États-Unis"; 'J' -> "🇯🇵" to "Japon"; 'P', 'X', 'Y', 'Z' -> "🇪🇺" to "Europe"; 'F' -> "🇫🇷" to "France"
    'D' -> "🇩🇪" to "Allemagne"; 'S' -> "🇪🇸" to "Espagne"; 'I' -> "🇮🇹" to "Italie"; 'H' -> "🇳🇱" to "Pays-Bas"
    'U' -> "🇦🇺" to "Australie"; 'K' -> "🇰🇷" to "Corée"; 'W' -> "🇹🇼" to "Taïwan"; 'R' -> "🇷🇺" to "Russie"
    else -> "" to ""
}

private fun duree(min: Int) = if (min >= 60) (min / 60).toString() + " h " + (min % 60) + " min" else "$min min"

private val FX = listOf(
    Triple("T", "Pulse universel", "Attrape les accès au lecteur pendant le jeu (jeux qui se décompressent, plusieurs programmes : PSO…)"),
    Triple("S", "Lecteur lent (comme un vrai)", "Pour les jeux qui bloquent quand le disque répond trop vite"),
    Triple("F", "Lecteur rapide (normal)", "Vitesse habituelle de Pulse : pour un jeu réglé sur « lecteur lent » qui n'en a pas besoin"),
    Triple("C", "Sans carte mémoire virtuelle", "Le jeu utilise tes vraies cartes"),
    Triple("H", "Sans cheats", "Le jeu démarre sans ses cheats (ils restent choisis)"),
    Triple("W", "Sans 16:9 forcé", "Image d'origine du jeu"),
    Triple("V", "Vidéo d'origine", "Sans 480p / 60 Hz / sans flou / image large"))

private class Cheat(val nom: String, val actif: Boolean, val lignes: Int, val codes: String)

@Composable
private fun FicheJeu(act: StationActivity, g: Jeu, compat: MutableMap<String, Int>, fermer: () -> Unit, change: () -> Unit) {
    val scope = rememberCoroutineScope()
    val etat = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var fav by remember { mutableStateOf(g.favori) }
    var cheats by remember { mutableStateOf<List<Cheat>?>(null) }
    var cheatsMsg by remember { mutableStateOf("Chargement des cheats…") }
    var masque by remember { mutableStateOf("") }
    var masqueSale by remember { mutableStateOf(false) }
    var ouverts by remember { mutableStateOf(act.getSharedPreferences("gcs", 0).getBoolean("cheats_ouverts", false)) }
    var cfg by remember { mutableStateOf<JSONObject?>(null) }
    var cfgOuvert by remember { mutableStateOf(false) }
    var patchs by remember { mutableStateOf(false) }
    var edite by remember { mutableStateOf<Pair<Int, Cheat?>?>(null) }   // (-1 : nouveau)
    var menuCheat by remember { mutableIntStateOf(-1) }
    var renomme by remember { mutableIntStateOf(-1) }
    var sauvegardes by remember { mutableStateOf(false) }
    var boite by remember { mutableStateOf(false) }                     // (la boite en 3D, en plein ecran)
    var notice by remember { mutableIntStateOf(0) }                     // (pages de la notice sur la console ; 0 : pas de notice)
    var noticeOuverte by remember { mutableStateOf(false) }
    var noticeAjout by remember { mutableStateOf(false) }              // (v297 : ajouter une notice depuis le telephone)
    var noticeEnvoi by remember { mutableStateOf<String?>(null) }
    val choisirNotice = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) scope.launch {
            noticeEnvoi = "Préparation des pages…"
            val err = withContext(Dispatchers.IO) {
                val pages = try { Notices.preparer(act, uris) { m -> noticeEnvoi = m } } catch (e: Throwable) { null }
                if (pages.isNullOrEmpty()) "Aucune page lisible dans ces fichiers" else Notices.envoyer(Hors.dir(act), g.id, pages) { m -> noticeEnvoi = m }
            }
            noticeEnvoi = null
            Toast.makeText(act, err ?: "Notice installée sur la console", Toast.LENGTH_LONG).show()
            notice = Notices.pages(g.id)
        }
    }

    fun remplir(r: String?) {
        val j = try { if (r != null) JSONObject(r) else null } catch (e: Exception) { null }
        if (j == null) { cheats = emptyList(); cheatsMsg = "Cheats indisponibles (console à mettre à jour ?)"; return }
        if (j.optString("err").isNotEmpty()) Toast.makeText(act, j.optString("err"), Toast.LENGTH_LONG).show()
        val c = j.optJSONArray("c")
        val l = ArrayList<Cheat>()
        for (k in 0 until (c?.length() ?: 0)) { val e = c!!.optJSONArray(k) ?: continue; l.add(Cheat(e.optString(0), e.optInt(1) != 0, e.optInt(2), e.optString(3))) }
        cheats = l
        masque = l.joinToString("") { if (it.actif) "1" else "0" }
        masqueSale = false
        cheatsMsg = if (l.isEmpty()) (if (j.optInt("x") != 0) "Aucun cheat utilisable dans SD:/gc-mp3/cheats/" + g.id + ".txt" else "Pas encore de cheats pour ce jeu : + Ajouter (ou SD:/gc-mp3/cheats/" + g.id + ".txt)") else ""
    }
    /** le choix des cheats part sur la console (s'il a change) */
    suspend fun envoyerMasque() {
        if (!masqueSale) return
        masqueSale = false
        if (lire("/api/cheatset?id=" + g.id + "&s=" + masque) == null) Toast.makeText(act, "Cheats pas enregistrés", Toast.LENGTH_SHORT).show()
    }
    fun cheatOp(q: String) { scope.launch { envoyerMasque(); val r = lire("/api/cheatedit?id=" + g.id + "&" + q); if (r == null) Toast.makeText(act, "Pas de réponse (console à mettre à jour ?)", Toast.LENGTH_LONG).show() else remplir(r) } }
    fun lancer(m: Int, fx: String = "") {
        scope.launch {
            envoyerMasque()
            val r = lire("/api/launch?i=" + maxOf(0, g.i) + "&m=" + m + "&id=" + g.id + if (fx.isEmpty()) "" else "&fx=$fx")
            Toast.makeText(act, if (r != null) (if (m == 1) "Lancement avec Pulse" + (if (fx.isEmpty()) "" else " + " + fx.length + " patch" + (if (fx.length > 1) "s" else "")) else "Lanceur de secours") + " : " + g.nom else "Refusé", Toast.LENGTH_SHORT).show()
            fermer()
        }
    }
    fun chargerCfg(row: Int, d: Int) { scope.launch { val r = lire("/api/gamecfg?id=" + g.id + "&i=" + g.i + "&row=" + row + "&d=" + d); cfg = try { if (r != null) JSONObject(r) else JSONObject() } catch (e: Exception) { JSONObject() } } }

    LaunchedEffect(g.id) { remplir(lire("/api/cheats?id=" + g.id)) }
    LaunchedEffect(g.id) { notice = Notices.pages(g.id).let { if (it > 0) it else Hors.pagesNotice(act, g.id) } }   // (hors-ligne : les pages gardees)
    LaunchedEffect(masque, masqueSale) { if (masqueSale) { delay(600); envoyerMasque() } }

    ModalBottomSheet(onDismissRequest = { scope.launch { envoyerMasque() }; fermer() }, sheetState = etat, containerColor = Color(0xFF120824)) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp).padding(bottom = 30.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                var img by remember { mutableStateOf<ImageBitmap?>(Jaquettes.mem.get(g.id)) }
                LaunchedEffect(g.id) { if (img == null) img = Jaquettes.charger(Hors.dir(act), g.id, g.i) }
                Box(Modifier.size(width = 96.dp, height = 134.dp).clip(RoundedCornerShape(14.dp)).background(Color(0x22FFFFFF)).appui { boite = true }) {
                    img?.let { Image(it, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
                    Box(Modifier.align(Alignment.BottomEnd).padding(5.dp).clip(RoundedCornerShape(8.dp)).background(Color(0xAA000000)).padding(horizontal = 6.dp, vertical = 2.dp)) {
                        Text("3D", color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                    }
                }
                Spacer(Modifier.width(16.dp))
                Column(Modifier.weight(1f)) {
                    Text(g.nom, color = C.Texte, fontSize = 19.sp, fontWeight = FontWeight.Bold, maxLines = 3, overflow = TextOverflow.Ellipsis)
                    val (dr, pn) = pays(g.id)
                    Text((if (dr.isEmpty()) "" else "$dr ") + g.id + (if (pn.isEmpty()) "" else " · $pn"), color = C.Cyan, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                    Doux(if (g.minutes > 0) "Joué " + duree(g.minutes) else "Jamais joué avec Pulse", taille = 12)
                    if (g.lancements > 0) Doux("Lancé " + g.lancements + " fois" + (if (g.dernier > 0) " · dernière fois " + LastLaunch.ago(g.dernier) else ""), taille = 12)
                    val pr = LastLaunch.forGame(act, g.id)
                    if (pr != null) Text("Dernier essai Pulse : " + LastLaunch.verdict(pr[0]) + " (" + LastLaunch.ago(pr[1].toLong()) + ")", color = Color(LastLaunch.color(pr[0])), fontSize = 12.sp)
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Bouton(if (fav) "Favori" else "Favori", plein = fav, icone = if (fav) Ic.Etoile else Ic.EtoileVide, hauteur = 38.dp) {
                            scope.launch {
                                val r = lire("/api/gamefav?id=" + g.id + "&i=" + g.i)
                                val j = try { if (r != null) JSONObject(r) else null } catch (e: Exception) { null }
                                if (j == null) Toast.makeText(act, "Pas possible (console à mettre à jour ?)", Toast.LENGTH_SHORT).show()
                                else { fav = j.optInt("fav") != 0; try { g.x.put(4, if (fav) 1 else 0) } catch (e: Exception) { }; change() }
                            }
                        }
                        Bouton("Raccourci", icone = Ic.Salon, hauteur = 38.dp) { raccourci(act, g) }
                    }
                }
            }
            Spacer(Modifier.height(18.dp))
            Bouton("Lancer avec Pulse", Modifier.fillMaxWidth(), plein = true, icone = Ic.Lecture, hauteur = 52.dp) { lancer(1) }
            Spacer(Modifier.height(8.dp))
            Bouton("Voir la boîte en 3D", Modifier.fillMaxWidth(), icone = Ic.Jeux) { boite = true }
            if (notice > 0) { Spacer(Modifier.height(8.dp)); Bouton("Lire la notice ($notice pages)", Modifier.fillMaxWidth()) { noticeOuverte = true } }
            Spacer(Modifier.height(8.dp))
            Bouton(if (notice > 0) "Remplacer la notice…" else "Ajouter une notice…", Modifier.fillMaxWidth()) { noticeAjout = true }
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Bouton("Patchs de secours…", Modifier.weight(1f)) { patchs = true }
                Bouton("Lanceur de secours", Modifier.weight(1f)) { lancer(0) }
            }

            // ---- cheats ----
            Spacer(Modifier.height(22.dp))
            val cl = cheats
            Row(verticalAlignment = Alignment.CenterVertically) {
                Titre("Cheats", Modifier.weight(1f))
                if (cl != null && cl.size > 2) { Bouton(if (ouverts) "Replier" else "Afficher (" + cl.size + ")", hauteur = 34.dp) {
                    ouverts = !ouverts; act.getSharedPreferences("gcs", 0).edit().putBoolean("cheats_ouverts", ouverts).apply() }; Spacer(Modifier.width(8.dp)) }
                Bouton("Ajouter", icone = Ic.Ajouter, hauteur = 34.dp) { edite = -1 to null }
            }
            val nOn = masque.count { it == '1' }
            Doux(if (cheatsMsg.isNotEmpty()) cheatsMsg else if (nOn == 0) "Aucun cheat activé · appui long : modifier" else "$nOn cheat" + (if (nOn > 1) "s activés" else " activé") + " · Pulse les applique (sinon : lanceur de secours)",
                Modifier.padding(top = 6.dp), 12)
            if (cl != null && (ouverts || cl.size <= 2)) cl.forEachIndexed { k, c ->
                val on = k < masque.length && masque[k] == '1'
                Spacer(Modifier.height(8.dp))
                Row(Modifier.fillMaxWidth().verre(RoundedCornerShape(16.dp)).combinedClickable(
                        onClick = { if (k < masque.length) { val cs = masque.toCharArray(); cs[k] = if (cs[k] == '1') '0' else '1'; masque = String(cs); masqueSale = true } },
                        onLongClick = { menuCheat = k }).padding(horizontal = 14.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(c.nom, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                        Doux(c.lignes.toString() + " ligne" + (if (c.lignes > 1) "s" else "") + " de code · appui long : modifier", taille = 12, couleur = C.Pale)
                    }
                    Switch(on, null, colors = SwitchDefaults.colors(checkedTrackColor = C.Rose, checkedThumbColor = Color.White))
                }
            }

            // ---- pastille ----
            Spacer(Modifier.height(22.dp))
            Titre("Pastille")
            Spacer(Modifier.height(8.dp))
            val cv = compat[g.id] ?: 0
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                for (k in CP_COL.indices) {
                    Box(Modifier.size(32.dp).scale(if (k == cv) 1.15f else 1f).clip(CircleShape).background(if (k == 0) Color(0x22FFFFFF) else CP_COL[k])
                        .border(if (k == cv) 3.dp else 1.dp, if (k == cv) Color.White else Color(0x55FFFFFF), CircleShape)
                        .appui { scope.launch { val r = lire("/api/compatset?id=" + g.id + "&d=" + k); if (r == null) Toast.makeText(act, "Pas possible (console à mettre à jour ?)", Toast.LENGTH_SHORT).show() else { lireCompat(r, compat); change() } } })
                }
                Text(CP_NOM[if (cv < CP_NOM.size) cv else 0], color = if (cv == 0) C.Pale else CP_COL[cv], fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
            }

            // ---- reglages du jeu ----
            Spacer(Modifier.height(22.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Titre("Réglages du jeu", Modifier.weight(1f))
                Bouton(if (cfgOuvert) "Masquer" else "Afficher", hauteur = 34.dp) { cfgOuvert = !cfgOuvert; if (cfgOuvert) chargerCfg(-1, 0) }
            }
            val cj = cfg
            if (cfgOuvert && cj != null) {
                val rows = cj.optJSONArray("r")
                if (cj.optInt("busy") != 0) Doux("Les réglages de ce jeu sont ouverts sur la télé : ferme-les d'abord.", Modifier.padding(top = 6.dp), couleur = C.Rose)
                else if (rows == null) Doux("Indisponible (console à mettre à jour ?)", Modifier.padding(top = 6.dp))
                else {
                    for (k in 0 until rows.length()) {
                        val e = rows.optJSONArray(k) ?: continue
                        Spacer(Modifier.height(6.dp))
                        Row(Modifier.fillMaxWidth().verre(RoundedCornerShape(14.dp)).appui { Toast.makeText(act, e.optString(3), Toast.LENGTH_LONG).show() }
                            .padding(start = 12.dp, end = 6.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text(e.optString(0), color = C.Texte, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                                Text(e.optString(1), color = if (e.optInt(2) != 0) Color(0xFF7DF0A6) else C.Doux, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                            }
                            Box(Modifier.size(40.dp).verre(CircleShape).appui { chargerCfg(k, -1) }, contentAlignment = Alignment.Center) { Icon(Ic.Gauche, null, tint = C.Texte) }
                            Spacer(Modifier.width(6.dp))
                            Box(Modifier.size(40.dp).verre(CircleShape).appui { chargerCfg(k, 1) }, contentAlignment = Alignment.Center) { Icon(Ic.Droite, null, tint = C.Texte) }
                        }
                    }
                    Spacer(Modifier.height(10.dp))
                    Bouton("Tout comme la console", Modifier.fillMaxWidth()) { chargerCfg(rows.length(), 0) }
                }
            }

            // ---- sauvegardes (carte virtuelle) : l'ecran des cartes memoire, partage ----
            Spacer(Modifier.height(22.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Titre("Sauvegardes (carte virtuelle)", Modifier.weight(1f))
                Bouton(if (sauvegardes) "Masquer" else "Afficher", hauteur = 34.dp) { sauvegardes = !sauvegardes }
            }
            if (sauvegardes) AndroidView({ c -> LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; CardsTab.gameSaves(act, g.id, this) } }, Modifier.fillMaxWidth().padding(top = 6.dp))
        }
    }

    if (boite) Boite3DEcran(act, g.id, g.nom, { boite = false; lancer(1) }) { boite = false }
    if (noticeOuverte) Notice3DEcran(act, g.id, g.nom, notice) { noticeOuverte = false }
    if (noticeAjout) AlertDialog(onDismissRequest = { noticeAjout = false }, containerColor = Color(0xFF1A0E30),
        title = { Text(if (notice > 0) "Remplacer la notice" else "Ajouter une notice", color = C.Texte) },
        text = {
            Column {
                Doux("Choisis un PDF, ou les images des pages (rangées dans l'ordre de leurs noms). L'appli les met au bon format et les envoie à la console : la notice apparaît dans la boîte du jeu, sur la télé et ici.", taille = 13)
                Spacer(Modifier.height(8.dp))
                Doux("Pages seules ou planches d'impression (2 doubles pages par image, traits de coupe) : l'appli reconnaît et découpe les planches toute seule.", taille = 11, couleur = C.Pale)
            }
        },
        confirmButton = { TextButton({ noticeAjout = false; choisirNotice.launch(arrayOf("image/*", "application/pdf")) }) { Text("Choisir les fichiers", color = C.Rose) } },
        dismissButton = { TextButton({ noticeAjout = false }) { Text("Annuler", color = C.Doux) } })
    noticeEnvoi?.let { m ->
        AlertDialog(onDismissRequest = { }, containerColor = Color(0xFF1A0E30),
            title = { Text("Notice", color = C.Texte) }, text = { Doux(m, taille = 14) }, confirmButton = { })
    }

    if (patchs) {
        val choix = remember { mutableStateMapOf<Int, Boolean>() }
        AlertDialog(onDismissRequest = { patchs = false }, containerColor = Color(0xFF1A0E30),
            title = { Text("Patchs de secours", color = C.Texte) },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    Doux("Pour ce lancement seulement : les réglages du jeu ne changent pas.", taille = 12)
                    FX.forEachIndexed { k, f ->
                        Spacer(Modifier.height(8.dp))
                        Row(Modifier.fillMaxWidth().verre(RoundedCornerShape(14.dp)).appui {
                                val on = !(choix[k] ?: false)
                                choix[k] = on
                                if (on && (k == 1 || k == 2)) choix[if (k == 1) 2 else 1] = false
                            }.padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
                            Column(Modifier.weight(1f)) {
                                Text(f.second, color = C.Texte, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                                Doux(f.third, taille = 11, couleur = C.Pale)
                            }
                            Switch(choix[k] ?: false, null, colors = SwitchDefaults.colors(checkedTrackColor = C.Rose))
                        }
                    }
                }
            },
            confirmButton = { TextButton({ patchs = false; lancer(1, FX.indices.filter { choix[it] == true }.joinToString("") { FX[it].first }) }) { Text("Lancer avec Pulse", color = C.Rose) } },
            dismissButton = { TextButton({ patchs = false }) { Text("Annuler", color = C.Doux) } })
    }

    if (menuCheat >= 0) {
        val k = menuCheat
        val c = cheats?.getOrNull(k)
        if (c == null) menuCheat = -1 else AlertDialog(onDismissRequest = { menuCheat = -1 }, containerColor = Color(0xFF1A0E30),
            title = { Text(c.nom, color = C.Texte) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Bouton("Renommer", Modifier.fillMaxWidth()) { menuCheat = -1; renomme = k }
                    Bouton("Modifier les codes", Modifier.fillMaxWidth()) { menuCheat = -1; edite = k to c }
                    Bouton("Supprimer", Modifier.fillMaxWidth(), couleur = C.Rouge) { menuCheat = -1; cheatOp("op=d&n=$k") }
                }
            },
            confirmButton = { TextButton({ menuCheat = -1 }) { Text("Fermer", color = C.Doux) } })
    }

    if (renomme >= 0) {
        val k = renomme
        var nom by remember { mutableStateOf(cheats?.getOrNull(k)?.nom ?: "") }
        AlertDialog(onDismissRequest = { renomme = -1 }, containerColor = Color(0xFF1A0E30),
            title = { Text("Nouveau nom", color = C.Texte) },
            text = { Champ(nom, { nom = it }, "Nom du cheat") },
            confirmButton = { TextButton({ renomme = -1; if (nom.isNotBlank()) cheatOp("op=r&n=$k&name=" + Api.enc(nom.trim())) }) { Text("Enregistrer", color = C.Rose) } },
            dismissButton = { TextButton({ renomme = -1 }) { Text("Annuler", color = C.Doux) } })
    }

    edite?.let { (n, c) ->
        var nom by remember { mutableStateOf(c?.nom ?: "") }
        var codes by remember { mutableStateOf(c?.codes?.split(",")?.filter { it.length == 16 }?.joinToString("\n") { it.substring(0, 8) + " " + it.substring(8) } ?: "") }
        AlertDialog(onDismissRequest = { edite = null }, containerColor = Color(0xFF1A0E30),
            title = { Text(if (n < 0) "Nouveau cheat" else "Codes : " + (c?.nom ?: ""), color = C.Texte) },
            text = {
                Column {
                    if (n < 0) { Champ(nom, { nom = it }, "Nom du cheat (ex. Vies infinies)"); Spacer(Modifier.height(10.dp)) }
                    Champ(codes, { codes = it }, "04001234 00000063\n04005678 00000001", uneLigne = false, mono = true)
                    Doux("Codes Action Replay déchiffrés / Gecko : 8 + 8 chiffres hexadécimaux par ligne.", Modifier.padding(top = 8.dp), 12, C.Pale)
                }
            },
            confirmButton = { TextButton({
                val hex = codes.replace(Regex("[^0-9A-Fa-f]"), "")
                if (hex.isEmpty() || hex.length % 16 != 0) { Toast.makeText(act, "Chaque ligne doit avoir 16 chiffres (XXXXXXXX YYYYYYYY)", Toast.LENGTH_LONG).show(); return@TextButton }
                if (n < 0 && nom.isBlank()) { Toast.makeText(act, "Donne un nom au cheat", Toast.LENGTH_SHORT).show(); return@TextButton }
                val p = hex.chunked(16).joinToString(",")
                edite = null
                cheatOp(if (n < 0) "op=a&name=" + Api.enc(nom.trim()) + "&codes=" + p else "op=c&n=$n&codes=$p")
            }) { Text("Enregistrer", color = C.Rose) } },
            dismissButton = { TextButton({ edite = null }) { Text("Annuler", color = C.Doux) } })
    }
}

/** icone du jeu sur l'ecran d'accueil : la toucher lance le jeu avec Pulse */
private fun raccourci(a: Activity, g: Jeu) {
    val sm = a.getSystemService(android.content.pm.ShortcutManager::class.java)
    if (sm == null || !sm.isRequestPinShortcutSupported) { Toast.makeText(a, "Ton écran d'accueil n'accepte pas les raccourcis", Toast.LENGTH_LONG).show(); return }
    val it = Host.home(a).setAction(MainActivity.ACT_GAME).putExtra("id", g.id).putExtra("nom", g.nom)
        .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK or android.content.Intent.FLAG_ACTIVITY_CLEAR_TOP)
    val cover = Jaquettes.bitmap(Hors.dir(a), g.id)
    val icon = if (cover != null) {
        val ic = Bitmap.createBitmap(432, 432, Bitmap.Config.ARGB_8888)
        val cv = Canvas(ic)
        val bg = Paint()
        bg.shader = LinearGradient(0f, 0f, 432f, 432f, intArrayOf(Neon.BG_TOP, 0xFF2A0A4A.toInt(), Neon.BG_BOTTOM), null, Shader.TileMode.CLAMP)
        cv.drawRect(0f, 0f, 432f, 432f, bg)
        val h = 260f
        val w = h * cover.width / cover.height
        cv.drawBitmap(cover, null, RectF(216 - w / 2, 216 - h / 2, 216 + w / 2, 216 + h / 2), Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG))
        android.graphics.drawable.Icon.createWithAdaptiveBitmap(ic)
    } else android.graphics.drawable.Icon.createWithResource(a, R.mipmap.ic_launcher)
    val si = android.content.pm.ShortcutInfo.Builder(a, "jeu-" + g.id)
        .setShortLabel(if (g.nom.length > 24) g.nom.substring(0, 23) + "…" else g.nom).setLongLabel(g.nom)
        .setIcon(icon).setIntent(it).build()
    try { sm.requestPinShortcut(si, null) } catch (e: Exception) { Toast.makeText(a, "Raccourci impossible", Toast.LENGTH_SHORT).show() }
}
