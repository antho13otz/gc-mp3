@file:OptIn(androidx.compose.ui.text.ExperimentalTextApi::class)

package fr.gcstation.telecommande

// Appli : la charpente de l'appli (en-tete, bandeaux, onglets, mini-lecteur, connexion, partie en cours).

import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.draw.blur
import android.graphics.BitmapFactory
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import android.content.res.Configuration
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private val ONGLETS = listOf("Lecture" to Ic.Lecture, "Musique" to Ic.Musique, "Jeux" to Ic.Jeux, "Télé" to Ic.Tele, "Plus" to Ic.Plus)

@Composable
fun App() {
    val act = LocalContext.current as StationActivity
    BackHandler(enabled = St.sous.isNotEmpty() || St.onglet != 0) {
        if (St.sous.isNotEmpty()) St.sous = "" else St.onglet = 0
    }
    // (ambiance « Couleurs du jeu » : celles du jeu lance, sinon de la derniere fiche ouverte, tirees de sa jaquette)
    val idCouleurs = St.jeu?.id?.takeIf { it.length == 6 } ?: C.idJeu
    LaunchedEffect(idCouleurs, C.ambiance) {
        if (C.ambiance != 3 || idCouleurs.isEmpty()) return@LaunchedEffect
        val b = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
            (JaquettesHQ.fichier(Hors.dir(act), "coverHQ", idCouleurs) ?: JaquettesHQ.fichier(Hors.dir(act), "cover", idCouleurs))
                ?.let { android.graphics.BitmapFactory.decodeFile(it.path, android.graphics.BitmapFactory.Options().apply { inSampleSize = 4 }) } ?: Jaquettes.bitmap(Hors.dir(act), idCouleurs)
        }
        if (b != null) Ambiances.duJeu(b)?.let { C.duJeu = it }
    }
    Box(Modifier.fillMaxSize()) {
        // un seul fond, du haut de l'ecran jusqu'en bas (derriere l'en-tete et la barre d'onglets aussi) : l'aurore qui
        // bouge, et sur l'onglet Lecture la pochette du morceau, floue, qui derive doucement
        FondAurore(actif = St.visible)
        FondPochette()
        Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding()) {
            // (Tele en paysage : plein ecran, sans en-tete ni barre d'onglets)
            val plein = St.onglet == 3 && LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
            if (!plein) EnTete(act)
            if (!plein) Bandeaux(act)
            Box(Modifier.weight(1f).fillMaxWidth()) {
                AnimatedContent(St.onglet, transitionSpec = {
                    // (dans le sens de l'onglet choisi, avec un leger ressort ; l'ancien glisse un peu et s'efface)
                    val d = if (targetState > initialState) 1 else -1
                    val r = androidx.compose.animation.core.spring<androidx.compose.ui.unit.IntOffset>(dampingRatio = 0.82f, stiffness = 420f)
                    (fadeIn(tween(180)) + androidx.compose.animation.slideInHorizontally(r) { it / 6 * d } + androidx.compose.animation.scaleIn(androidx.compose.animation.core.spring(0.8f, 420f), initialScale = 0.97f)) togetherWith
                        (fadeOut(tween(140)) + androidx.compose.animation.slideOutHorizontally(r) { -it / 10 * d })
                }, label = "onglet") { o ->
                    when (o) {
                        0 -> EcranLecture()
                        1 -> EcranMusique()
                        2 -> EcranJeux()
                        3 -> EcranTele()
                        else -> EcranPlus()
                    }
                }
            }
            if (St.onglet != 0 && St.onglet != 3) MiniLecteur()
            if (!plein) BarreOnglets()
        }
        PartieEnCours(act)
        ClavierPartout()                                         // (champ de texte ouvert sur la tele : le clavier du telephone, partout)
        if (St.panneau) Connexion(act)
    }
}

@Composable
private fun FondPochette() {
    val p = St.pochette
    val montre = St.onglet == 0 && p != null
    val a by androidx.compose.animation.core.animateFloatAsState(if (montre) 1f else 0f, tween(600), label = "fondPochette")
    if (a <= 0.01f || p == null) return
    val tr = androidx.compose.animation.core.rememberInfiniteTransition(label = "derive")
    val t by tr.animateFloat(0f, 1f, androidx.compose.animation.core.infiniteRepeatable(tween(22000), androidx.compose.animation.core.RepeatMode.Reverse), label = "t")
    Image(p, null, Modifier.fillMaxSize().graphicsLayer {
            val k = if (St.visible) t else 0.5f
            scaleX = 1.25f + 0.12f * k; scaleY = scaleX
            translationX = size.width * 0.06f * (k - 0.5f); translationY = size.height * 0.04f * (0.5f - k)
            alpha = 0.38f * a
        }.blur(60.dp), contentScale = ContentScale.Crop)
    // (un voile en degrade : les textes restent lisibles en haut et en bas)
    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(C.Fond.copy(alpha = 0.35f * a), Color.Transparent, C.Fond.copy(alpha = 0.45f * a)))))
}

@Composable
private fun EnTete(act: StationActivity) {
    var menu by remember { mutableStateOf(false) }
    Row(Modifier.fillMaxWidth().padding(start = 20.dp, end = 8.dp, top = 8.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text("GC REMOTE", style = androidx.compose.ui.text.TextStyle(brush = C.Neon), fontSize = 22.sp, fontWeight = FontWeight.Black, letterSpacing = 2.sp)
            Row(verticalAlignment = Alignment.CenterVertically) {
                val (couleur, txt) = when {
                    St.jeu != null -> C.Rose to "En jeu · " + (St.jeu?.name ?: "")
                    St.horsLigne -> C.Rouge to "Console injoignable · nouvel essai…"
                    St.connecte -> C.Cyan to "Connecté · " + (Api.host ?: "") + if (Live.pushing()) " · en direct" else ""
                    else -> C.Pale to "Pas connecté"
                }
                Box(Modifier.size(7.dp).clip(CircleShape).background(couleur))
                Spacer(Modifier.width(6.dp))
                Doux(txt, taille = 12, lignes = 1)
            }
        }
        Box {
            Box(Modifier.size(44.dp).appui { menu = true }, contentAlignment = Alignment.Center) { Icon(Ic.Reglages, null, tint = C.Doux) }
            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                val items = listOf(
                    "Actualiser les listes" to { act.actualiser() },
                    "Chercher la console à nouveau" to { act.search() },
                    "Saisir l'adresse de la console" to { act.panneau("Tape l'adresse affichée dans l'onglet TÉLÉPHONE de la console.", false); St.connecte = false },
                    "Mettre à jour l'appli (" + BuildConfig.VERSION_NAME + ")" to { AppUpdate.open(act) },
                    "Manette en jeu : réglages et disposition" to { ManetteActivity.openSetup(act) })
                for ((t, f) in items) DropdownMenuItem(text = { Text(t) }, onClick = { menu = false; f() })
            }
        }
    }
}

@Composable
private fun Bandeaux(act: StationActivity) {
    var cache by remember { mutableStateOf(false) }                     // (bandeau ferme : il revient a la prochaine coupure)
    LaunchedEffect(St.horsLigne) { if (!St.horsLigne) cache = false }
    AnimatedVisibility(St.horsLigne && St.jeu == null && !cache, enter = expandVertically() + fadeIn(), exit = shrinkVertically() + fadeOut()) {
        // (une seule ligne : la console ne repond pas, ce qui est garde reste la ; chercher, manette, fermer)
        Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 2.dp).verre(RoundedCornerShape(14.dp), C.Rouge.copy(alpha = 0.45f)).padding(start = 12.dp, end = 4.dp, top = 4.dp, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text("Hors-ligne · tes jeux gardés restent là", color = C.Texte, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            Box(Modifier.size(34.dp).clip(CircleShape).appui { act.search() }, contentAlignment = Alignment.Center) { Icon(Ic.Chercher, "Chercher la console", tint = C.Texte, modifier = Modifier.size(18.dp)) }
            Box(Modifier.size(34.dp).clip(CircleShape).appui { act.manetteEnJeu() }, contentAlignment = Alignment.Center) { Icon(Ic.Manette, "Manette dans le jeu", tint = C.Texte, modifier = Modifier.size(18.dp)) }
            Box(Modifier.size(34.dp).clip(CircleShape).appui { cache = true }, contentAlignment = Alignment.Center) { Icon(Ic.Fermer, "Masquer", tint = C.Pale, modifier = Modifier.size(16.dp)) }
        }
    }
    val e = St.envoi
    AnimatedVisibility(e != null, enter = expandVertically() + fadeIn(), exit = shrinkVertically() + fadeOut()) {
        if (e != null) Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 4.dp).verre(RoundedCornerShape(18.dp)).padding(horizontal = 14.dp, vertical = 10.dp)) {
            val t = if (e.fini) e.texte else (if (e.nb > 1) "Envoi " + (e.idx + 1) + "/" + e.nb + " : " else "Envoi : ") + e.nom + "  " + e.pct + " %" + if (e.debit.isEmpty()) "" else " · " + e.debit
            Text(t, color = C.Texte, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(6.dp))
            LinearProgressIndicator(progress = { e.pct / 100f }, modifier = Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)),
                color = C.Rose, trackColor = Color(0x22FFFFFF))
        }
    }
}

@Composable
private fun MiniLecteur() {
    val s = St.etat ?: return
    val t = s.optString("title", "")
    Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 4.dp).verre(RoundedCornerShape(20.dp))
        .clickable(remember { MutableInteractionSource() }, null) { St.onglet = 0 }.padding(start = 14.dp, end = 6.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(if (t.isEmpty()) "Rien en lecture" else t, color = C.Texte, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            val sub = s.optString("sub", "")
            if (sub.isNotEmpty()) Text(sub, color = C.Rose, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Rond(if (s.optInt("playing", 0) != 0) Ic.Pause else Ic.Lecture, 40.dp, plein = true) { Api.cmd("toggle", -1) }
        Spacer(Modifier.width(6.dp))
        Rond(Ic.Suivant, 40.dp) { Api.cmd("next", -1) }
    }
}

@Composable
private fun BarreOnglets() {
    Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp)
        .verre(RoundedCornerShape(26.dp), C.Rose.copy(alpha = 0.35f), Color(0x1AFFFFFF)).padding(vertical = 6.dp, horizontal = 4.dp)) {
        ONGLETS.forEachIndexed { i, (nom, ic) ->
            val on = St.onglet == i
            Column(Modifier.weight(1f).clip(RoundedCornerShape(20.dp)).appui { if (St.onglet == i) St.sous = "" else St.onglet = i }.padding(vertical = 6.dp),
                horizontalAlignment = Alignment.CenterHorizontally) {
                Box(Modifier.size(width = 48.dp, height = 28.dp).clip(RoundedCornerShape(14.dp))
                    .background(if (on) Brush.linearGradient(listOf(C.Rose.copy(alpha = 0.55f), C.Violet.copy(alpha = 0.45f))) else Brush.linearGradient(listOf(Color.Transparent, Color.Transparent))),
                    contentAlignment = Alignment.Center) {
                    Icon(ic, nom, tint = if (on) Color.White else C.Pale, modifier = Modifier.size(20.dp))
                }
                Spacer(Modifier.height(2.dp))
                Text(nom, color = if (on) C.Texte else C.Pale, fontSize = 11.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Normal)
            }
        }
    }
}

// ---------- connexion ----------
@Composable
private fun Connexion(act: StationActivity) {
    var ip by remember { mutableStateOf(Api.host ?: "") }
    Box(Modifier.fillMaxSize().background(C.Fond.copy(alpha = 0.92f)).clickable(remember { MutableInteractionSource() }, null) { }, contentAlignment = Alignment.Center) {
        Column(Modifier.padding(24.dp).fillMaxWidth().verre(RoundedCornerShape(28.dp)).padding(24.dp).verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally) {
            Text("GC REMOTE", style = androidx.compose.ui.text.TextStyle(brush = C.Neon), fontSize = 32.sp, fontWeight = FontWeight.Black, letterSpacing = 3.sp)
            Text("Télécommande", color = C.Cyan, fontSize = 14.sp)
            Spacer(Modifier.height(22.dp))
            if (St.cherche) CircularProgressIndicator(color = C.Rose) else Icon(Ic.Wifi, null, tint = C.Rose, modifier = Modifier.size(40.dp))
            Spacer(Modifier.height(14.dp))
            Text(St.panneauMsg, color = C.Texte, fontSize = 14.sp, textAlign = TextAlign.Center)
            for (c in St.choixConsoles) {
                Spacer(Modifier.height(8.dp))
                Ligne(c[1], c[0], Modifier.appui { act.connect(c[0]) }, gauche = { Pastille(Ic.Tele, C.Cyan, 34.dp) })
            }
            Spacer(Modifier.height(18.dp))
            Champ(ip, { ip = it }, "Adresse de la console (ex. 192.168.1.20)", clavier = KeyboardType.Uri)
            Spacer(Modifier.height(12.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Bouton("Se connecter", Modifier.weight(1f), plein = true) { act.adresse(ip) }
                Bouton("Chercher", Modifier.weight(1f)) { act.search() }
            }
            Spacer(Modifier.height(10.dp))
            Bouton("Manette dans le jeu", Modifier.fillMaxWidth(), icone = Ic.Manette) { if (ip.isNotBlank()) { Api.host = ip.trim() }; act.manetteEnJeu() }
            Spacer(Modifier.height(10.dp))
            Bouton("Utiliser hors-ligne", Modifier.fillMaxWidth(), icone = Ic.Jeux) { St.horsLigneMode = true; St.panneau = false; St.onglet = 2 }   // (sans la console : ce qui est garde)
            Doux("Tes jeux, jaquettes, boîtes 3D et notices gardés sur le téléphone, sans la console.", Modifier.padding(top = 6.dp), 11, C.Pale, centre = true)
        }
    }
}

// ---------- partie en cours ----------
@Composable
private fun PartieEnCours(act: StationActivity) {
    val g = St.jeu
    AnimatedVisibility(g != null && !St.jeuMasque && !St.panneau, enter = fadeIn(tween(250)), exit = fadeOut(tween(200))) {
        if (g == null) return@AnimatedVisibility
        @Suppress("UNUSED_VARIABLE") val tic = St.seconde
        Box(Modifier.fillMaxSize().background(C.Fond).clickable(remember { MutableInteractionSource() }, null) { }.statusBarsPadding().navigationBarsPadding(),
            contentAlignment = Alignment.Center) {
            FondAurore()
            Column(Modifier.padding(20.dp).fillMaxWidth().verre(RoundedCornerShape(30.dp), C.Rose.copy(alpha = 0.55f), Color(0xFF1C1030)).verticalScroll(rememberScrollState()).padding(22.dp),
                horizontalAlignment = Alignment.CenterHorizontally) {
                Titre("Partie en cours")
                Spacer(Modifier.height(14.dp))
                val bmp = remember(g.id) {
                    val f = File(Hors.dir(act), "jaquettes/" + g.id + ".png")
                    if (f.exists()) BitmapFactory.decodeFile(f.path) else null
                }
                if (bmp != null) Image(bmp.asImageBitmap(), null, Modifier.size(width = 120.dp, height = 168.dp).clip(RoundedCornerShape(14.dp)), contentScale = ContentScale.Crop)
                else Pastille(Ic.Jeux, C.Rose, 90.dp)
                Spacer(Modifier.height(12.dp))
                Text(g.name.ifEmpty { g.id }, color = C.Texte, fontSize = 20.sp, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
                Doux(g.id + (if (g.profile.isEmpty()) "" else " · profil " + g.profile) + " · lancé à " + SimpleDateFormat("HH:mm", Locale.FRANCE).format(Date(g.start)), taille = 12, centre = true)
                val t = g.playedSec()
                Spacer(Modifier.height(12.dp))
                Text((t / 3600).toString() + ":" + String.format(Locale.FRANCE, "%02d:%02d", t % 3600 / 60, t % 60),
                    style = androidx.compose.ui.text.TextStyle(brush = C.Neon), fontSize = 44.sp, fontWeight = FontWeight.Black)
                Doux("temps de jeu", taille = 11, couleur = C.Pale)
                val left = g.leftSec()
                if (left != Long.MIN_VALUE) {
                    Spacer(Modifier.height(10.dp))
                    if (left > 0) Text("Il reste " + Live.dur(left) + " de jeu aujourd'hui", color = if (left <= 300) C.Orange else C.Cyan, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                    else Text("Temps de jeu du jour écoulé", color = C.Rouge, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                }
                Spacer(Modifier.height(14.dp))
                Doux("Pour revenir à GC Station : A + Z + START sur la manette. L'appli se reconnecte toute seule au retour.", taille = 12, centre = true)
                Spacer(Modifier.height(16.dp))
                if (g.mem) {
                    val sc = rememberCoroutineScope()
                    Bouton(if (tacheChercheur().isEmpty()) "Capture de l'écran du jeu" else tacheChercheur(), Modifier.fillMaxWidth(), plein = true, icone = Ic.Photo) { capturerJeu(act, sc) }
                    Spacer(Modifier.height(8.dp))
                    Bouton("Chercheur de codes", Modifier.fillMaxWidth(), icone = Ic.Chercher) { St.jeuMasque = true; St.onglet = 4; St.sous = "Chercheur de codes" }
                    Spacer(Modifier.height(8.dp))
                    ApercuCapture(act)
                }
                if (g.padMask != 0) {
                    Bouton("Manette dans le jeu", Modifier.fillMaxWidth(), plein = true, icone = Ic.Manette) { ManetteActivity.open(act, g.padMask) }
                    Spacer(Modifier.height(8.dp))
                }
                Bouton("Masquer", Modifier.fillMaxWidth()) { St.jeuMasque = true }
            }
        }
    }
}
