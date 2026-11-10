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

/** build 457 : tuiles des fonctions gardees par le proprietaire (absentes de la version publique) */
private val TUILES_PRIVEES = setOf("Salon", "Extraire d'un .iso", "Manette en jeu", "Regarder un ami", "Regarder en 3D", "Discord")   // (build 493 : RetroAchievements aussi dans la version publique, mode normal)   // (build 486 : le compagnon et la vue 3D aussi dans la version publique)
// build 492 : 21 cases ramenees a 11 : les cases voisines sont regroupees (une case de groupe ouvre ses cases a elle),
// Apparence et Mettre a jour passent dans le menu ⚙ en haut a droite (reglages de l'appli, pas de la console)
private val TOUTES get() = listOf(
    Tuile("Fichiers", "La carte SD", Ic.Dossier, C.Rose) { St.sous = "Fichiers" },
    Tuile("Photos", "Sur la télé", Ic.Photo, C.Cyan) { St.sous = "Photos" },
    Tuile("Réglages", "De GC Station", Ic.Reglages, C.Violet) { St.sous = "Réglages" },
    Tuile("Cartes mémoire", "Virtuelles, vraies, copies", Ic.Carte, C.Bleu) { St.sous = "Cartes mémoire" },
    Tuile("Succès", "GC Station, RetroAchievements, stats", Ic.Trophee, C.Orange) { St.sous = "Succès" },
    Tuile("En jeu", "Compagnon, codes, manette", Ic.Jeux, C.Vert) { St.sous = "En jeu" },
    Tuile("Mini-jeux", "Duels, jeu de cartes", Ic.Cartes, C.Cyan) { St.sous = "Mini-jeux" },
    Tuile("Recherche", "Musique, radios, jeux, globe", Ic.Chercher, C.Rose) { St.sous = "Recherche" },
    Tuile("Salon", "Les autres consoles", Ic.Salon, C.Cyan) { St.sous = "Salon" },
    Tuile("Outils", "Mémoire, capture, rapport", Ic.Outils, C.Violet) { St.sous = "Outils" }).filter { Prive.ACTIF || it.nom !in TUILES_PRIVEES }

// Build 504 (appli privee) : cet onglet s'appelle « Console » et ne garde que ce qui touche a la console (fichiers, photos,
// reglages, cartes memoire, outils, extraction). Ce qui sert pendant une partie et ce qui se fait entre amis passe dans
// l'onglet Pulse (EcranPulse, plus bas) ; la recherche s'ouvre par la loupe du haut de l'appli. Build 512 : publique aussi (sans l'extraction).
private val HORS_CONSOLE = setOf("Succès", "En jeu", "Mini-jeux", "Recherche", "Salon")
private val TUILES get() = if (!Rangee.ACTIF) TOUTES else TOUTES.filter { it.nom !in HORS_CONSOLE } +
    listOf(Tuile("Extraire d'un .iso", "main.dol d'un jeu du téléphone", Ic.Copies, C.Cyan) { St.sous = "Extraire d'un jeu du téléphone" }).filter { Prive.ACTIF }   // (build 512 : jamais dans la version publique)
/** les ecrans qui s'ouvrent dans l'onglet Pulse (appli privee) */
val SOUS_PULSE = setOf("Compagnon de jeu", "Chercheur de codes", "Regarder un ami", "Regarder en 3D", "Discord", "Salon",
    "Succès", "Succès GC Station", "RetroAchievements", "Statistiques de jeu", "Mini-jeux")

/** cases des groupes ; un ecran d'un groupe revient au groupe (fleche retour) */
private val GROUPES: Map<String, List<Tuile>> get() = mapOf(
    "Cartes mémoire" to listOf(
        Tuile("Cartes virtuelles", "Sauvegardes", Ic.Carte, C.Bleu) { St.sous = "Cartes virtuelles" },
        Tuile("Vraies cartes", "Slots A et B de la console", Ic.Carte, C.Cyan) { St.sous = "Vraies cartes mémoire" },
        Tuile("Copies", "Cartes et réglages gardés", Ic.Copies, C.Bleu) { St.sous = "Copies" }),
    "Succès" to listOf(
        Tuile("Succès GC Station", "Débloqués dans les jeux", Ic.Trophee, C.Orange) { a -> if (Prive.ACTIF) { St.sous = "Succès GC Station" } else { LastLaunch.showAch(a) } },   // (build 479 : liste par jeu)
        Tuile("RetroAchievements", "Succès du site, en jeu", Ic.Trophee, C.Orange) { St.sous = "RetroAchievements" },
        Tuile("Statistiques de jeu", "Temps par jeu et par semaine", Ic.Trophee, C.Orange) { St.sous = "Statistiques de jeu" }),
    "En jeu" to listOf(
        Tuile("Compagnon de jeu", "Jauges, carte, chrono en direct", Ic.Jeux, C.Vert) { St.sous = "Compagnon de jeu" },
        Tuile("Chercheur de codes", "Tes cheats et succès, en jeu", Ic.Chercher, C.Cyan) { St.sous = "Chercheur de codes" },
        Tuile("Manette en jeu", "Pulse : ports 2 à 4", Ic.Manette, C.Violet) { a -> a.manetteEnJeu() },
        Tuile("Extraire d'un .iso", "main.dol d'un jeu du téléphone", Ic.Copies, C.Cyan) { St.sous = "Extraire d'un jeu du téléphone" },
        Tuile("Regarder un ami", "Sa vue 3D en direct, par Internet", Ic.Tele, C.Rose) { St.sous = "Regarder un ami" },   // (build 495)
        Tuile("Regarder en 3D", "Un ami joue, ta caméra", Ic.Tele, C.Cyan) { St.sous = "Regarder en 3D" },   // (build 499)
        Tuile("Discord", "Ton jeu sur ton profil Discord", Ic.Jeux, C.Violet) { St.sous = "Discord" }),   // (build 498)
    "Mini-jeux" to listOf(
        Tuile("Mini-jeux", "Duels, téléphone = manette", Ic.Jeux, C.Cyan) { a -> MiniJeuxActivity.open(a) },
        Tuile("Jeu de cartes", "8 américain entre potes", Ic.Cartes, C.Rose) { a -> CartesActivity.open(a) }),
    "Recherche" to listOf(
        Tuile("Rechercher", "Musique, radios, jeux", Ic.Chercher, C.Rose) { St.sous = "Rechercher" },
        Tuile("Globe des radios", "Les radios du monde", Ic.Globe, C.Bleu) { a -> a.startActivity(android.content.Intent(a, GlobeActivity::class.java)) }))
    .mapValues { (_, l) -> l.filter { Prive.ACTIF || it.nom !in TUILES_PRIVEES } }
/** le groupe d'un ecran (fleche retour) ; "" : la grille de Plus */
fun parentPlus(sous: String): String = when (sous) {
    "Cartes virtuelles", "Vraies cartes mémoire", "Copies" -> "Cartes mémoire"
    "Succès GC Station", "RetroAchievements", "Statistiques de jeu" -> "Succès"
    "Compagnon de jeu", "Chercheur de codes", "Extraire d'un jeu du téléphone", "Regarder un ami", "Regarder en 3D", "Discord" -> if (Rangee.ACTIF) "" else "En jeu"   // (build 504 : plus de groupe « En jeu » dans l'appli privee)
    "Rechercher" -> "Recherche"
    else -> ""
}

@Composable
private fun Case(t: Tuile, act: StationActivity) {
    Column(Modifier.verre(RoundedCornerShape(24.dp)).appui { t.faire(act) }.padding(16.dp)) {
        Pastille(t.ic, t.couleur, 42.dp)
        Spacer(Modifier.height(12.dp))
        Text(t.nom, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Doux(t.desc, taille = 11, lignes = 1)
    }
}

/** Build 504 (appli privee) : l'onglet Pulse. En haut la carte de Pulse (logo anime, partie en cours et temps de jeu, ou
 *  « aucun jeu en cours ») ; puis « Pendant la partie » (compagnon, codes, manette, succes) et « Entre amis » (Salon,
 *  regarder un ami, Discord, mini-jeux). Les ecrans s'ouvrent dans cet onglet (SOUS_PULSE), fleche retour : ici. */
@Composable
fun EcranPulse() {
    val act = LocalContext.current as StationActivity
    val g = St.jeu
    @Suppress("UNUSED_VARIABLE") val tic = St.seconde          // (le temps de jeu avance)
    val enJeu = GROUPES["En jeu"] ?: emptyList()
    fun de(l: List<Tuile>, vararg noms: String) = noms.mapNotNull { n -> l.firstOrNull { it.nom == n } }
    val partie = de(enJeu, "Compagnon de jeu", "Chercheur de codes", "Manette en jeu") + de(TOUTES, "Succès")
    val amis = de(TOUTES, "Salon") + de(enJeu, "Regarder un ami", "Regarder en 3D", "Discord") + de(GROUPES["Mini-jeux"] ?: emptyList(), "Mini-jeux", "Jeu de cartes")
    val large: androidx.compose.foundation.lazy.grid.LazyGridItemSpanScope.() -> androidx.compose.foundation.lazy.grid.GridItemSpan = { androidx.compose.foundation.lazy.grid.GridItemSpan(maxLineSpan) }
    LazyVerticalGrid(GridCells.Adaptive(150.dp), Modifier.fillMaxSize().padding(horizontal = 14.dp), contentPadding = PaddingValues(bottom = 14.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item(span = large) {
            Column(Modifier.fillMaxWidth().verre(RoundedCornerShape(26.dp), C.Rose.copy(alpha = 0.40f)).padding(14.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Box(Modifier.size(104.dp), contentAlignment = Alignment.Center) { Prive.LogoPulse(Modifier.size(104.dp)) }
                        Text("Pulse", style = androidx.compose.ui.text.TextStyle(brush = androidx.compose.ui.graphics.Brush.verticalGradient(listOf(Color(255, 150, 215), Color(130, 150, 255)))),
                            fontSize = 22.sp, fontWeight = FontWeight.Black)
                    }
                    Spacer(Modifier.width(14.dp))
                    Column(Modifier.weight(1f)) {
                        Titre(if (g != null) "Partie en cours" else "Pulse")
                        Spacer(Modifier.height(4.dp))
                        Text(if (g != null) g.name.ifEmpty { g.id } else "Aucun jeu en cours", color = C.Texte, fontSize = 17.sp, fontWeight = FontWeight.Bold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        if (g != null) {
                            val t = g.playedSec()
                            Text((t / 3600).toString() + ":" + String.format(java.util.Locale.FRANCE, "%02d:%02d", t % 3600 / 60, t % 60),
                                style = androidx.compose.ui.text.TextStyle(brush = C.Neon), fontSize = 26.sp, fontWeight = FontWeight.Black)
                            Doux("temps de jeu · " + g.id, taille = 11)
                        } else Doux(if (St.connecte) "Lance un jeu depuis l'onglet Jeux : Pulse reste en mémoire pendant la partie et ses outils marchent ici."
                                    else "Console pas connectée.", taille = 12)
                    }
                }
                if (g != null && St.jeuMasque) {
                    Spacer(Modifier.height(12.dp))
                    Bouton("Écran de la partie", Modifier.fillMaxWidth(), plein = true, hauteur = 40.dp) { St.jeuMasque = false }
                }
                // build 509 (console v397) : le bandeau des succes sur la tele : essai, et succes RetroAchievements oui / non
                // (build 521 : aussi dans la version publique : la console v397+ publique a le bandeau)
                BandeauPulse(act)
            }
        }
        item(span = large) { Titre("Pendant la partie", Modifier.padding(start = 4.dp, top = 8.dp)) }
        items(partie) { t -> Case(t, act) }
        item(span = large) { Titre("Entre amis", Modifier.padding(start = 4.dp, top = 8.dp)) }
        items(amis) { t -> Case(t, act) }
    }
}

@Composable
private fun BandeauPulse(act: StationActivity) {
            run {
                val sc = androidx.compose.runtime.rememberCoroutineScope()
                var raTele by androidx.compose.runtime.remember { androidx.compose.runtime.mutableStateOf(Bandeau.raSurTele(act)) }
                Spacer(Modifier.height(12.dp))
                Bouton("Tester la notification sur la télé", Modifier.fillMaxWidth(), hauteur = 40.dp, icone = Ic.Trophee) {
                    sc.launch {
                        val err = withContext(Dispatchers.IO) { Bandeau.envoyer(3, "Ceci est un essai de GC Remote") }
                        Lang.toast(act, err ?: "Bandeau envoyé : regarde la télé", Toast.LENGTH_LONG).show()
                    }
                }
                Row(Modifier.fillMaxWidth().padding(top = 8.dp).appui { raTele = !raTele; Bandeau.raSurTele(act, raTele) }, verticalAlignment = Alignment.CenterVertically) {
                    Doux("Succès RetroAchievements sur la télé", Modifier.weight(1f), 13, C.Texte)
                    androidx.compose.material3.Switch(raTele, { raTele = it; Bandeau.raSurTele(act, it) },
                        colors = androidx.compose.material3.SwitchDefaults.colors(checkedTrackColor = C.Rose, checkedThumbColor = Color.White))
                }
                Doux("Le bandeau se coupe aussi sur la console : START > Jeux > « Succès sur la télé (Pulse) ».", taille = 11, couleur = C.Pale)
                Prive.NotifTeleReglages(act)                   // (build 522 : message et notifications du telephone sur la tele)
            }
}

@Composable
fun EcranPlus() {
    val act = LocalContext.current as StationActivity
    val sous = St.sous
    val groupe = GROUPES[sous]
    if (sous.isEmpty() || groupe != null) Column(Modifier.fillMaxSize()) {
        if (groupe != null) Row(Modifier.padding(start = 10.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).appui { St.sous = parentPlus(sous) }, contentAlignment = Alignment.Center) { Icon(Ic.Retour, null, tint = C.Texte) }
            Spacer(Modifier.width(6.dp))
            Text(sous, color = C.Texte, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        }
        LazyVerticalGrid(GridCells.Adaptive(150.dp), Modifier.fillMaxSize().padding(horizontal = 14.dp), contentPadding = PaddingValues(bottom = 14.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(groupe ?: TUILES) { t -> Case(t, act) }
        }
    }
    if (sous.isEmpty() || groupe != null) return
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.padding(start = 10.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).appui { St.sous = parentPlus(sous) }, contentAlignment = Alignment.Center) { Icon(Ic.Retour, null, tint = C.Texte) }
            Spacer(Modifier.width(6.dp))
            Text(sous, color = C.Texte, fontSize = 20.sp, fontWeight = FontWeight.Bold)
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (sous) {
                "Fichiers" -> EcranFichiers()
                "Apparence" -> EcranApparence()
                "Chercheur de codes" -> EcranChercheur()
                "RetroAchievements" -> EcranRetro()
                "Regarder un ami" -> EcranRegarder()
                "Regarder en 3D" -> if (BuildConfig.PRIVE) EcranRegarder3D()   // (build 499)
                "Statistiques de jeu" -> EcranStatsJeux()
                "Succès GC Station" -> EcranSucces()
                "Vraies cartes mémoire" -> EcranVraiesCartes()
                "Photos" -> EcranPhotos()
                "Réglages" -> Ancien(sous) { SettingsTab(it) }
                "Cartes virtuelles" -> Ancien(sous) { CardsTab(it) }
                "Salon" -> Ancien(sous) { SalonTab(it) }
                "Outils" -> Ancien(sous) { ToolsTab(it) }
                "Rechercher" -> Ancien(sous) { SearchTab(it) }
                "Copies" -> Ancien(sous) { CopiesTab(it) }
                else -> Prive.Ecran(sous)                                  // (build 457 : ecrans de la variante privee)
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
        val ctx = LocalContext.current
        val vs = rememberScrollState()
        ModalBottomSheet(onDismissRequest = { texte = null }, containerColor = Color(0xFF0E0618)) {
            Column(Modifier.padding(horizontal = 16.dp).padding(bottom = 20.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(nom, Modifier.weight(1f), color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Puce("Copier", true) {
                        val cm = ctx.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                        try { cm.setPrimaryClip(android.content.ClipData.newPlainText(nom, t)); Toast.makeText(ctx, "Texte copié", Toast.LENGTH_SHORT).show() }
                        catch (e: Exception) { Toast.makeText(ctx, "Texte trop long pour être copié", Toast.LENGTH_SHORT).show() }
                    }
                }
                Spacer(Modifier.height(8.dp))
                Box(Modifier.fillMaxWidth().heightIn(max = 520.dp).verre(RoundedCornerShape(14.dp)).verticalScroll(vs).padding(10.dp)) {
                    SelectionContainer { Text(t, color = C.Texte, fontSize = 12.sp, fontFamily = FontFamily.Monospace) }
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
    val scope = rememberCoroutineScope()
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
        .appui { scope.launch {                                   // (v308 : la reponse dit si c'est fait)
            val r = lire("/api/show?i=$i", 4000)
            dire(when {
                r == null -> "Pas de réponse de la console (ou photo introuvable : actualise la liste)"
                r.contains("\"ok\":1") -> "Sur la télé : " + x.optString(1)
                r.contains("onglet") -> "Photo pas affichée : l'onglet PHOTOS est caché sur la console (START > Onglets affichés)"
                else -> "Photo pas affichée"
            })
        } },
        contentAlignment = Alignment.Center) {
        val b = img
        if (b != null) Image(b, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        if (video) Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color.Transparent, Color(0x99000000)))), contentAlignment = Alignment.Center) {
            Icon(Ic.Lecture, null, tint = C.Cyan, modifier = Modifier.size(36.dp))
        }
    }
}
