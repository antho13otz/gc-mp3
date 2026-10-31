@file:OptIn(androidx.compose.ui.text.ExperimentalTextApi::class)

package fr.gcstation.telecommande

// Appli : la charpente de l'appli (en-tete, bandeaux, onglets, mini-lecteur, connexion, partie en cours).

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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
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
    Box(Modifier.fillMaxSize()) {
        FondAurore(actif = St.visible)
        Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().imePadding()) {
            // (Tele en paysage : plein ecran, sans en-tete ni barre d'onglets)
            val plein = St.onglet == 3 && LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
            if (!plein) EnTete(act)
            if (!plein) Bandeaux(act)
            Box(Modifier.weight(1f).fillMaxWidth()) {
                AnimatedContent(St.onglet, transitionSpec = {
                    (fadeIn(tween(220)) + slideInVertically(tween(220)) { it / 24 }) togetherWith fadeOut(tween(120))
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
        if (St.panneau) Connexion(act)
    }
}

@Composable
private fun EnTete(act: StationActivity) {
    var menu by remember { mutableStateOf(false) }
    Row(Modifier.fillMaxWidth().padding(start = 20.dp, end = 8.dp, top = 8.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text("GC STATION", style = androidx.compose.ui.text.TextStyle(brush = C.Neon), fontSize = 22.sp, fontWeight = FontWeight.Black, letterSpacing = 2.sp)
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
    AnimatedVisibility(St.horsLigne && St.jeu == null, enter = expandVertically() + fadeIn(), exit = shrinkVertically() + fadeOut()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 4.dp).verre(RoundedCornerShape(18.dp), C.Rouge.copy(alpha = 0.5f)).padding(12.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Doux("La console ne répond pas (" + (Api.host ?: "") + "). Éteinte, en jeu ou hors du Wi-Fi ? L'appli se reconnecte toute seule. Jeux : ce qui est gardé reste disponible.", Modifier.weight(1f), 12, C.Texte)
            Spacer(Modifier.width(8.dp))
            Bouton("Chercher", hauteur = 38.dp) { act.search() }
            Spacer(Modifier.width(6.dp))
            Bouton("", icone = Ic.Manette, plein = true, hauteur = 38.dp) { act.manetteEnJeu() }
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
            Text("GC STATION", style = androidx.compose.ui.text.TextStyle(brush = C.Neon), fontSize = 32.sp, fontWeight = FontWeight.Black, letterSpacing = 3.sp)
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
            Column(Modifier.padding(20.dp).fillMaxWidth().verre(RoundedCornerShape(30.dp), C.Rose.copy(alpha = 0.55f), Color(0xFF1C1030)).padding(22.dp),
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
                if (g.padMask != 0) {
                    Bouton("Manette dans le jeu", Modifier.fillMaxWidth(), plein = true, icone = Ic.Manette) { ManetteActivity.open(act, g.padMask) }
                    Spacer(Modifier.height(8.dp))
                }
                Bouton("Masquer", Modifier.fillMaxWidth()) { St.jeuMasque = true }
            }
        }
    }
}
