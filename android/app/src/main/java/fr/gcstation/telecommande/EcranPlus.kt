@file:OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class, androidx.compose.material3.ExperimentalMaterial3Api::class)

package fr.gcstation.telecommande

// Appli : Plus (grille), Fichiers et Photos (Compose), et les ecrans partages avec l'ancien ecran
// (Reglages, Cartes memoire, Salon, Outils, Recherche, Copies : le code Java, affiche tel quel dans l'appli).

import android.app.Activity
import android.graphics.BitmapFactory
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.horizontalScroll
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

private class Tuile(val nom: String, val desc: String, val ic: ImageVector, val couleur: Color, val faire: (StationActivity) -> Unit)

private val TUILES get() = listOf(
    Tuile("Fichiers", "La carte SD", Ic.Dossier, C.Rose) { St.sous = "Fichiers" },
    Tuile("Photos", "Sur la télé", Ic.Photo, C.Cyan) { St.sous = "Photos" },
    Tuile("Réglages", "De GC Station", Ic.Reglages, C.Violet) { St.sous = "Réglages" },
    Tuile("Apparence", "Ambiance, langue, texte, 120 Hz", Ic.Photo, C.Rose) { St.sous = "Apparence" },
    Tuile("Cartes mémoire", "Sauvegardes", Ic.Carte, C.Bleu) { St.sous = "Cartes mémoire" },
    Tuile("Recherche", "Musique, radios, jeux", Ic.Chercher, C.Rose) { St.sous = "Recherche" },
    Tuile("Salon", "Les autres consoles", Ic.Salon, C.Cyan) { St.sous = "Salon" },
    Tuile("Outils", "Mémoire, capture, rapport", Ic.Outils, C.Violet) { St.sous = "Outils" },
    Tuile("Copies", "Cartes et réglages gardés", Ic.Copies, C.Bleu) { St.sous = "Copies" },
    Tuile("Succès", "Débloqués dans les jeux", Ic.Trophee, C.Orange) { a -> LastLaunch.showAch(a) },
    Tuile("Chercheur de codes", "Tes cheats et succès, en jeu", Ic.Chercher, C.Cyan) { St.sous = "Chercheur de codes" },
    Tuile("Jeu de cartes", "8 américain entre potes", Ic.Cartes, C.Rose) { a -> CartesActivity.open(a) },
    Tuile("Mini-jeux", "Duels, téléphone = manette", Ic.Jeux, C.Cyan) { a -> MiniJeuxActivity.open(a) },
    Tuile("Manette en jeu", "Pulse : ports 2 à 4", Ic.Manette, C.Violet) { a -> a.manetteEnJeu() },
    Tuile("Globe des radios", "Les radios du monde", Ic.Globe, C.Bleu) { a -> a.startActivity(android.content.Intent(a, GlobeActivity::class.java)) },
    Tuile("Mettre à jour", "Nouvelle version de l'appli", Ic.Maj, C.Rose) { a -> AppUpdate.open(a) })

@Composable
fun EcranPlus() {
    val act = LocalContext.current as StationActivity
    val sous = St.sous
    if (sous.isEmpty()) {
        LazyVerticalGrid(GridCells.Adaptive(150.dp), Modifier.fillMaxSize().padding(horizontal = 14.dp), contentPadding = PaddingValues(bottom = 14.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(TUILES) { t ->
                Column(Modifier.verre(RoundedCornerShape(24.dp)).appui { t.faire(act) }.padding(16.dp)) {
                    Pastille(t.ic, t.couleur, 42.dp)
                    Spacer(Modifier.height(12.dp))
                    Text(t.nom, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Doux(t.desc, taille = 11, lignes = 1)
                }
            }
        }
        return
    }
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.padding(start = 10.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).appui { St.sous = "" }, contentAlignment = Alignment.Center) { Icon(Ic.Retour, null, tint = C.Texte) }
            Spacer(Modifier.width(6.dp))
            Text(sous, color = C.Texte, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (sous) {
                "Fichiers" -> EcranFichiers()
                "Apparence" -> EcranApparence()
                "Chercheur de codes" -> EcranChercheur()
                "Photos" -> EcranPhotos()
                "Réglages" -> Ancien(sous) { SettingsTab(it) }
                "Cartes mémoire" -> Ancien(sous) { CardsTab(it) }
                "Salon" -> Ancien(sous) { SalonTab(it) }
                "Outils" -> Ancien(sous) { ToolsTab(it) }
                "Recherche" -> Ancien(sous) { SearchTab(it) }
                "Copies" -> Ancien(sous) { CopiesTab(it) }
            }
        }
    }
}

/** un ecran de l'ancienne appli (Java), garde tant que l'appli tourne */
private object Anciens {
    var act: Activity? = null
    val tabs = HashMap<String, Tab>()
    val vues = HashMap<String, View>()
}

@Composable
private fun Ancien(nom: String, fabrique: (Activity) -> Tab) {
    val act = LocalContext.current as Activity
    if (Anciens.act !== act) { Anciens.act = act; Anciens.tabs.clear(); Anciens.vues.clear() }
    val tab = Anciens.tabs.getOrPut(nom) { fabrique(act) }
    AndroidView({ Anciens.vues.getOrPut(nom) { tab.build() }.also { v -> (v.parent as? ViewGroup)?.removeView(v) } }, Modifier.fillMaxSize())
    LaunchedEffect(nom, St.connecte) { if (St.connecte) tab.show() }
    val s = St.etat
    LaunchedEffect(s) { if (s != null) tab.state(s) }
}

// ------------------------------------------------------------------ Fichiers (carte SD de la console)
private class Entree(val nom: String, val dossier: Boolean, val taille: Long)
private val TEXTE = listOf(".txt", ".log", ".ini", ".cfg", ".m3u", ".m3u8", ".lrc", ".json", ".md", ".xml", ".csv", ".nfo", ".srt", ".pls", ".htm", ".html", ".css", ".js", ".c", ".h", ".py", ".sh", ".yml", ".yaml", ".conf")
private fun estTexte(n: String) = TEXTE.any { n.lowercase().endsWith(it) }
private fun taille(sz: Long) = if (sz >= 1048576) String.format(java.util.Locale.FRANCE, "%.1f Mo", sz / 1048576.0) else maxOf(1L, (sz + 1023) / 1024).toString() + " Ko"

@Composable
fun EcranFichiers() {
    val act = LocalContext.current as StationActivity
    val scope = rememberCoroutineScope()
    var h by remember { mutableStateOf("") }
    var uh by remember { mutableStateOf<String?>(null) }
    var chemin by remember { mutableStateOf("sd:") }
    var entrees by remember { mutableStateOf<List<Entree>>(emptyList()) }
    var msg by remember { mutableStateOf("Lecture du dossier…") }
    var coupe by remember { mutableStateOf<Triple<String, Int, String>?>(null) }
    var actions by remember { mutableIntStateOf(-1) }
    var demande by remember { mutableStateOf<Triple<String, String, (String) -> Unit>?>(null) }
    var texte by remember { mutableStateOf<Pair<String, String>?>(null) }
    var supprime by remember { mutableIntStateOf(-1) }

    fun ouvrir(dir: String, i: Int) {
        msg = "Lecture du dossier…"
        scope.launch {
            val r = lire("/api/files?h=" + dir + if (i >= 0) "&i=$i" else "", 8000)
            if (r == null) { msg = "Pas de réponse de la console"; return@launch }
            try {
                val j = JSONObject(r)
                h = j.optString("h", "")
                uh = if (j.isNull("uh")) null else j.optString("uh", "")
                chemin = j.optString("d", "sd:")
                val e = j.optJSONArray("e")
                entrees = (0 until (e?.length() ?: 0)).map { k -> val x = e!!.getJSONArray(k); Entree(x.optString(0, ""), x.optInt(1, 0) != 0, x.optLong(2, 0)) }
                msg = if (entrees.isEmpty()) "(dossier vide)" else entrees.size.toString() + " élément" + (if (entrees.size > 1) "s" else "") + " · appui long : renommer, couper, supprimer"
            } catch (ex: Exception) { msg = "Réponse illisible" }
        }
    }
    fun fsop(q: String) {
        msg = "…"
        scope.launch {
            val r = lire("/api/fsop?$q", 15000)
            msg = try { if (r != null) JSONObject(r).optString("m", "Fait") else "Pas de réponse de la console" } catch (e: Exception) { "Fait" }
            ouvrir(h, -1)
        }
    }
    fun telecharger(i: Int, nom: String) = UiKit.download(act, "/api/get?h=$h&i=$i", nom)
    fun lireTexte(i: Int, e: Entree) {
        if (e.taille > 4L * 1024 * 1024) { Lang.toast(act, "Fichier trop gros pour être affiché : téléchargé à la place", Toast.LENGTH_LONG).show(); telecharger(i, e.nom); return }
        texte = e.nom to "Chargement…"
        scope.launch {
            val b = lireOctets("/api/get?h=$h&i=$i", 8000)
            val s = if (b == null) "Lecture impossible (pas de réponse de la console)" else withContext(Dispatchers.Default) {
                var t = try { java.nio.charset.StandardCharsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT).decode(java.nio.ByteBuffer.wrap(b)).toString() }
                        catch (x: Exception) { String(b, java.nio.charset.StandardCharsets.ISO_8859_1) }
                if (t.startsWith("﻿")) t = t.substring(1)
                t = t.replace("\r\n", "\n").replace('\r', '\n')
                if (t.length > 200000) t.substring(0, 200000) + "\n\n… (la suite : Télécharger)" else if (t.isEmpty()) "(fichier vide)" else t
            }
            texte = e.nom to s
        }
    }
    val choisir = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { l -> if (l.isNotEmpty()) act.envoyerChoix(l) }

    LaunchedEffect(St.listes) { ouvrir(h, -1) }

    Column(Modifier.fillMaxSize().padding(horizontal = 14.dp)) {
        Text(chemin, color = C.Cyan, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Bouton("", icone = Ic.Haut, hauteur = 40.dp, actif = uh != null) { uh?.let { ouvrir(it, -1) } }
            Bouton("Dossier", icone = Ic.Ajouter, hauteur = 40.dp) { demande = Triple("Nom du nouveau dossier", "") { v: String -> if (v.isNotBlank()) fsop("op=mkdir&h=$h&n=" + Api.enc(v.trim())) } }
            coupe?.let { c -> Bouton("Coller : " + c.third, plein = true, hauteur = 40.dp) { coupe = null; fsop("op=mv&h=" + c.first + "&i=" + c.second + "&x=" + Api.enc(c.third) + "&t=" + h) } }
            Bouton("Envoyer", icone = Ic.Envoyer, hauteur = 40.dp) { choisir.launch(arrayOf("*/*")) }
            Bouton("", icone = Ic.Actualiser, hauteur = 40.dp) { msg = "Actualisation…"; ouvrir(h, -1) }
        }
        Doux(msg, Modifier.padding(start = 4.dp, top = 8.dp, bottom = 6.dp), 12)
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            itemsIndexed(entrees) { i, e ->
                Ligne(e.nom, if (e.dossier) "" else taille(e.taille), Modifier.combinedClickable(
                    onClick = { if (e.dossier) ouvrir(h, i) else if (estTexte(e.nom)) lireTexte(i, e) else telecharger(i, e.nom) },
                    onLongClick = { actions = i }),
                    gauche = { Pastille(if (e.dossier) Ic.Dossier else Ic.Fichier, if (e.dossier) C.Cyan else C.Rose, 34.dp) })
            }
        }
    }

    if (actions >= 0 && actions < entrees.size) {
        val i = actions
        val e = entrees[i]
        val q = "&h=$h&i=$i&x=" + Api.enc(e.nom)
        ModalBottomSheet(onDismissRequest = { actions = -1 }, containerColor = Color(0xFF140A26)) {
            Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 28.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(e.nom, color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                Bouton("Renommer", Modifier.fillMaxWidth()) { actions = -1; demande = Triple("Nouveau nom", e.nom) { v: String -> if (v.isNotBlank() && v != e.nom) fsop("op=ren$q&n=" + Api.enc(v.trim())) } }
                Bouton("Couper (pour déplacer)", Modifier.fillMaxWidth()) { actions = -1; coupe = Triple(h, i, e.nom); msg = "Ouvre le dossier de destination, puis Coller" }
                if (!e.dossier) {
                    Bouton("Lire comme texte", Modifier.fillMaxWidth()) { actions = -1; lireTexte(i, e) }
                    Bouton("Télécharger", Modifier.fillMaxWidth(), icone = Ic.Maj) { actions = -1; telecharger(i, e.nom) }
                }
                Bouton("Supprimer", Modifier.fillMaxWidth(), couleur = C.Rouge) { actions = -1; supprime = i }
            }
        }
    }
    if (supprime >= 0 && supprime < entrees.size) {
        val i = supprime
        val e = entrees[i]
        AlertDialog(onDismissRequest = { supprime = -1 }, containerColor = Color(0xFF1A0E30),
            title = { Text("Supprimer " + e.nom + (if (e.dossier) " et tout son contenu" else "") + " ?", color = C.Texte) },
            text = { Doux("Ça ne peut pas être annulé.") },
            confirmButton = { TextButton({ supprime = -1; fsop("op=del&h=$h&i=$i&x=" + Api.enc(e.nom)) }) { Text("Supprimer", color = C.Rouge) } },
            dismissButton = { TextButton({ supprime = -1 }) { Text("Annuler", color = C.Doux) } })
    }
    demande?.let { (titre, init, fait) ->
        var v by remember(titre, init) { mutableStateOf(init) }
        AlertDialog(onDismissRequest = { demande = null }, containerColor = Color(0xFF1A0E30),
            title = { Text(titre, color = C.Texte) },
            text = { Champ(v, { v = it }, titre) },
            confirmButton = { TextButton({ demande = null; fait(v) }) { Text("OK", color = C.Rose) } },
            dismissButton = { TextButton({ demande = null }) { Text("Annuler", color = C.Doux) } })
    }
    texte?.let { (nom, t) ->
        var retour by remember { mutableStateOf(true) }
        val vs = rememberScrollState()
        val hs = rememberScrollState()
        ModalBottomSheet(onDismissRequest = { texte = null }, containerColor = Color(0xFF0E0618)) {
            Column(Modifier.padding(horizontal = 16.dp).padding(bottom = 20.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(nom, Modifier.weight(1f), color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Puce(if (retour) "Retour à la ligne" else "Sans retour", retour) { retour = !retour }
                }
                Spacer(Modifier.height(8.dp))
                Box(Modifier.fillMaxWidth().heightIn(max = 520.dp).verre(RoundedCornerShape(14.dp)).verticalScroll(vs)
                    .then(if (retour) Modifier else Modifier.horizontalScroll(hs)).padding(10.dp)) {
                    SelectionContainer { Text(t, color = C.Texte, fontSize = 12.sp, fontFamily = FontFamily.Monospace, softWrap = retour) }
                }
            }
        }
    }
}

// ------------------------------------------------------------------ Photos
private object Vignettes { val mem = android.util.LruCache<Int, ImageBitmap>(200) }

@Composable
fun EcranPhotos() {
    var photos by remember { mutableStateOf<List<JSONArray>?>(null) }
    var msg by remember { mutableStateOf("Chargement…") }
    LaunchedEffect(St.listes) {
        val r = lire("/api/photos", 8000)
        if (r == null) { msg = "Pas de réponse de la console"; return@LaunchedEffect }
        photos = try { val j = JSONArray(r); (0 until j.length()).map { j.getJSONArray(it) } } catch (e: Exception) { emptyList() }
        val n = photos?.size ?: 0
        msg = if (n == 0) "Aucune photo (SD:/photos)" else "$n photo" + (if (n > 1) "s" else "") + " · toucher pour l'afficher sur la télé"
    }
    Column(Modifier.fillMaxSize().padding(horizontal = 14.dp)) {
        Doux(msg, Modifier.padding(start = 4.dp, bottom = 8.dp), 12)
        LazyVerticalGrid(GridCells.Adaptive(104.dp), Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 14.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(photos ?: emptyList(), key = { it.optInt(0) }) { x -> Vignette(x) { m -> msg = m } }
        }
    }
}

@Composable
private fun Vignette(x: JSONArray, dire: (String) -> Unit) {
    val i = x.optInt(0)
    val video = x.optInt(2) != 0
    var img by remember(i) { mutableStateOf<ImageBitmap?>(Vignettes.mem.get(i)) }
    LaunchedEffect(i) {
        if (img != null || video) return@LaunchedEffect
        val d = lireOctets("/api/photo?i=$i", 4000) ?: return@LaunchedEffect
        img = withContext(Dispatchers.Default) {
            val o = BitmapFactory.Options()
            o.inJustDecodeBounds = true
            BitmapFactory.decodeByteArray(d, 0, d.size, o)
            var ss = 1
            while (maxOf(o.outWidth, o.outHeight) / (ss * 2) >= 320) ss *= 2
            val o2 = BitmapFactory.Options()
            o2.inSampleSize = ss
            BitmapFactory.decodeByteArray(d, 0, d.size, o2)?.asImageBitmap()
        }
        img?.let { Vignettes.mem.put(i, it) }
    }
    Box(Modifier.aspectRatio(1f).clip(RoundedCornerShape(16.dp)).background(Color(0xFF1A0E30)).border(1.dp, C.Rose.copy(alpha = 0.3f), RoundedCornerShape(16.dp))
        .appui { Api.get("/api/show?i=$i") { r -> dire(if (r != null) "Sur la télé : " + x.optString(1) else "Pas de réponse de la console") } },
        contentAlignment = Alignment.Center) {
        val b = img
        if (b != null) Image(b, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        if (video) Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color.Transparent, Color(0x99000000)))), contentAlignment = Alignment.Center) {
            Icon(Ic.Lecture, null, tint = C.Cyan, modifier = Modifier.size(36.dp))
        }
    }
}
