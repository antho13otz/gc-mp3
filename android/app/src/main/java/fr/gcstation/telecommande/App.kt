@file:OptIn(androidx.compose.ui.text.ExperimentalTextApi::class)

package fr.gcstation.telecommande

// Appli : la charpente de l'appli (en-tete, bandeaux, onglets, mini-lecteur, connexion, partie en cours).

import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.animation.togetherWith
import androidx.compose.animation.core.animateFloat
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.ui.draw.blur
import android.graphics.BitmapFactory
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.animateFloat
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
import androidx.compose.foundation.layout.offset
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

// Build 504 (appli privee) : l'appli rangee par usage. La barre devient Jeux, Pulse, Musique, Tele, Console. Les numeros des
// ecrans (St.onglet) ne changent pas (0 Lecture, 1 Musique, 2 Jeux, 3 Tele, 4 Plus / Console) ; 5 : le nouvel onglet Pulse.
// Lecture n'a plus d'onglet a elle : c'est l'ecran du lecteur, ouvert en touchant le lecteur du bas ; il compte dans Musique.
// Build 512 : aussi dans la version publique (Rangee.ACTIF).
/** build 512 : l'appli rangee par usage (504), l'onglet Tele range et plus fluide (505-507) : aussi dans la version
 *  publique. Les ecrans prives (manette, Salon, Discord, extraction, regarder un ami) gardent leur porte Prive.ACTIF. */
object Rangee { const val ACTIF = true }
private class Onglet(val i: Int, val nom: String, val ic: androidx.compose.ui.graphics.vector.ImageVector)
private val ONGLETS: List<Onglet> get() = if (Rangee.ACTIF)
    listOf(Onglet(2, "Jeux", Ic.Jeux), Onglet(5, "Pulse", Ic.Pulse), Onglet(1, "Musique", Ic.Musique), Onglet(3, "Télé", Ic.Tele), Onglet(4, "Console", Ic.Plus))
    else listOf(Onglet(0, "Lecture", Ic.Lecture), Onglet(1, "Musique", Ic.Musique), Onglet(2, "Jeux", Ic.Jeux), Onglet(3, "Télé", Ic.Tele), Onglet(4, "Plus", Ic.Plus))
/** l'onglet d'accueil (celui ou ramene la fleche retour) */
private val ACCUEIL get() = if (Rangee.ACTIF) 2 else 0
/** la place d'un ecran dans la barre (sens du glissement) */
private fun rang(o: Int): Int = ONGLETS.indexOfFirst { it.i == o || (Rangee.ACTIF && o == 0 && it.i == 1) }
private var accueilFait = false

@Composable
fun App() {
    val act = LocalContext.current as StationActivity
    if (Prive.accueilConsoles()) return                          // (build 501, appli privee : choix GameCube / Switch d'abord)
    BackHandler(enabled = St.sous.isNotEmpty() || St.onglet != ACCUEIL || Prive.ACTIF) {
        if (St.sous.isNotEmpty()) St.sous = parentPlus(St.sous) else if (St.onglet != ACCUEIL) St.onglet = ACCUEIL else Prive.changerConsole()
    }
    if (Rangee.ACTIF) {
        // (build 504 : l'appli s'ouvre sur Jeux ; un ecran de Pulse demande ailleurs s'ouvre dans l'onglet Pulse, les autres dans Console)
        LaunchedEffect(Unit) { if (!accueilFait) { accueilFait = true; if (St.onglet == 0 && St.sous.isEmpty()) St.onglet = 2 } }
        LaunchedEffect(St.onglet, St.sous) {
            if (St.onglet == 4 && St.sous in SOUS_PULSE) St.onglet = 5
            else if (St.onglet == 5 && St.sous.isNotEmpty() && St.sous !in SOUS_PULSE) St.onglet = 4
        }
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
                    val d = if (rang(targetState) > rang(initialState)) 1 else -1
                    sensOnglet = d
                    // (build 520, style liquide : le passage est fait par Liquefie, ci-dessous : rien ici)
                    if (Liquide.actif) return@AnimatedContent androidx.compose.animation.EnterTransition.None togetherWith androidx.compose.animation.ExitTransition.None
                    val r = androidx.compose.animation.core.spring<androidx.compose.ui.unit.IntOffset>(dampingRatio = 0.82f, stiffness = 420f)
                    (fadeIn(tween(180)) + androidx.compose.animation.slideInHorizontally(r) { it / 6 * d } + androidx.compose.animation.scaleIn(androidx.compose.animation.core.spring(0.8f, 420f), initialScale = 0.97f)) togetherWith
                        (fadeOut(tween(140)) + androidx.compose.animation.slideOutHorizontally(r) { -it / 10 * d })
                }, label = "onglet") { o ->
                    Liquefie(Liquide.actif) {
                    when (o) {
                        0 -> EcranLecture()
                        1 -> EcranMusique()
                        2 -> EcranJeux()
                        3 -> EcranTele()
                        5 -> if (St.sous.isEmpty()) EcranPulse() else EcranPlus()   // (build 504)
                        else -> EcranPlus()
                    }
                    }
                }
            }
            if (St.onglet != 0 && St.onglet != 3) MiniLecteur()
            if (!plein) BarreOnglets()
        }
        PartieEnCours(act)
        Prive.lancementPulse()                                   // (build 502, appli privee : ecran de chargement de Pulse au lancement d'un jeu)
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
    // (build 521 : le vrai verre recalcule ce fond chez lui : la pochette en tout petit, sa force et sa derive)
    androidx.compose.runtime.LaunchedEffect(p) {
        Liquide.pochette = try { val b = p.asAndroidBitmap(); android.graphics.Bitmap.createScaledBitmap(b.copy(android.graphics.Bitmap.Config.ARGB_8888, false), 16, (16f * b.height / b.width.coerceAtLeast(1)).toInt().coerceIn(4, 64), true) } catch (e: Exception) { null }
    }
    androidx.compose.runtime.SideEffect { Liquide.pochA = a; Liquide.pochK = if (St.visible) t else 0.5f }
    androidx.compose.runtime.DisposableEffect(Unit) { onDispose { Liquide.pochA = 0f } }
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
        // (build 504, appli privee : la recherche partout, par la loupe ; avant : une case de Plus)
        if (Rangee.ACTIF) Box(Modifier.size(44.dp).appui { St.onglet = 4; St.sous = "Recherche" }, contentAlignment = Alignment.Center) { Icon(Ic.Chercher, "Rechercher", tint = C.Doux) }
        Box {
            Box(Modifier.size(44.dp).appui { menu = true }, contentAlignment = Alignment.Center) { Icon(Ic.Reglages, null, tint = C.Doux) }
            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                val items = listOf(
                    "Changer de console" to { Prive.changerConsole() },           // (build 501 : appli privee)
                    "Actualiser les listes" to { act.actualiser() },
                    "Chercher la console à nouveau" to { act.search() },
                    "Saisir l'adresse de la console" to { act.panneau("Tape l'adresse affichée dans l'onglet TÉLÉPHONE de la console.", false); St.connecte = false },
                    "Apparence de l'appli" to { St.onglet = 4; St.sous = "Apparence" },   // (build 492 : avant, une case de Plus)
                    "Mettre à jour l'appli (" + BuildConfig.VERSION_NAME + ")" to { AppUpdate.open(act) },
                    "Exporter les réglages de l'appli" to { Reglages.exporter(act) },      // (build 452)
                    "Importer des réglages de l'appli" to { Reglages.choisir(act) },
                    "Manette en jeu : réglages et disposition" to { Prive.manetteReglages(act) }).filter { Prive.ACTIF || (!it.first.startsWith("Manette en jeu") && it.first != "Changer de console") }   // (build 457 : variante privee seulement)
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

/** build 510 : barre d'onglets en verre liquide : une bulle de verre glisse sous l'onglet choisi. Ses deux bords suivent deux
 *  ressorts de vitesses differentes (l'avant part vite, l'arriere suit) : en chemin elle s'etire, puis se ramasse en arrivant,
 *  comme une goutte. */
/** sens du dernier changement d'onglet (1 : vers la droite) */
private var sensOnglet = 1

/**
 * build 520 (style liquide) : passage d'un onglet a l'autre. L'ecran qui arrive glisse avec un ressort, grossit un peu et
 * ondule comme une surface d'eau qui se calme (Ondule, LiquideGL.kt) ; celui qui part glisse a l'oppose, se floute et
 * s'efface. Tout est dans le calque (graphicsLayer) : rien n'est remis en page pendant le passage.
 */
@Composable
private fun androidx.compose.animation.AnimatedVisibilityScope.Liquefie(actif: Boolean, contenu: @Composable () -> Unit) {
    if (!actif) { contenu(); return }
    val p by transition.animateFloat(transitionSpec = {
        if (targetState == androidx.compose.animation.EnterExitState.Visible) androidx.compose.animation.core.spring(dampingRatio = 0.74f, stiffness = 170f)
        else androidx.compose.animation.core.tween(260)
    }, label = "liquefie") { if (it == androidx.compose.animation.EnterExitState.Visible) 1f else 0f }
    val part = transition.targetState == androidx.compose.animation.EnterExitState.PostExit
    val d = sensOnglet
    val dens = androidx.compose.ui.platform.LocalDensity.current.density
    Box(Modifier.fillMaxSize().graphicsLayer {
        val q = (1f - p).coerceIn(0f, 1.2f)
        translationX = (if (part) -d else d) * size.width * (if (part) 0.10f else 0.16f) * (1f - p)
        val k = 1f - (if (part) 0.03f else 0.06f) * q
        scaleX = k; scaleY = k
        alpha = if (part) p.coerceIn(0f, 1f) else (p * 1.8f).coerceIn(0f, 1f)
        renderEffect = Ondule.effet(if (part) 0f else q.coerceAtMost(1f), Liquide.temps, dens / 2.75f, if (part) q * 18f * dens / 2.75f else 0f)
    }) { contenu() }
}

@Composable
private fun BarreLiquide() {
    val n = ONGLETS.size
    val ici = ONGLETS.indexOfFirst { it.i == St.onglet || (St.onglet == 0 && it.i == 1) }.coerceAtLeast(0).toFloat()
    // build 514 : la bulle se tire au doigt (glisse : position sous le doigt, en onglets) et choisit l'onglet ou on la lache ;
    // les icones grossissent sous elle comme sous une loupe pendant qu'elle bouge ; chaque toucher lance une onde dans le verre
    var glisse by androidx.compose.runtime.remember { androidx.compose.runtime.mutableStateOf<Float?>(null) }
    val onde = androidx.compose.runtime.remember { Liquide.Onde() }
    val gl = Liquide.actif
    val cible = glisse ?: ici
    // (build 519 : plus lente et plus elastique : la bulle sautait en 2-3 images, on ne la voyait ni s'etirer ni gonfler)
    val vite by androidx.compose.animation.core.animateFloatAsState(cible, androidx.compose.animation.core.spring(dampingRatio = 0.58f, stiffness = if (glisse != null) 900f else 300f), label = "avant")
    val lent by androidx.compose.animation.core.animateFloatAsState(cible, androidx.compose.animation.core.spring(dampingRatio = 0.45f, stiffness = if (glisse != null) 380f else 120f), label = "arriere")
    val leve by androidx.compose.animation.core.animateFloatAsState(if (glisse != null) 1.08f else 1f, androidx.compose.animation.core.spring(dampingRatio = 0.45f, stiffness = 500f), label = "leve")
    val dens = androidx.compose.ui.platform.LocalDensity.current.density
    fun choisir(j: Int) {
        val o = ONGLETS[j.coerceIn(0, n - 1)]; val i = o.i
        val lecteur = Rangee.ACTIF && i == 1 && St.onglet == 0
        if (lecteur) St.onglet = 1 else if (St.onglet == i) St.sous = "" else { St.onglet = i; if (Rangee.ACTIF) St.sous = "" }
    }
    // build 517 (d'apres la galerie de MagicOS) : le verre de la barre est un calque a part, sous la bulle et les onglets : la
    // bulle n'est plus decoupee par la barre et peut en deborder quand elle gonfle en chemin
    Box(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp)) {
    Box(Modifier.matchParentSize().then(if (gl) Modifier.verreGL(RoundedCornerShape(32.dp), force = 14f, onde = onde) else Modifier.verre(RoundedCornerShape(32.dp))))
    androidx.compose.foundation.layout.BoxWithConstraints(Modifier.fillMaxWidth().padding(vertical = 6.dp, horizontal = 6.dp)) {
        val w = maxWidth / n
        val wpx = constraints.maxWidth.toFloat() / n
        // (build 511 : les ressorts rebondissent un peu au-dela des onglets du bout : positions bornees a la barre, et bulle
        //  placee par decalage, pas par une marge, qui ne peut pas etre negative : la 510 plantait la)
        val g = minOf(vite, lent).coerceIn(0f, (n - 1).toFloat()); val d = maxOf(vite, lent).coerceIn(g, (n - 1).toFloat())
        val etire = (d - g).coerceAtMost(1.2f)
        val centre = (vite + lent) / 2f
        // (build 517 : en chemin, la bulle gonfle et deborde de la barre, comme une goutte soulevee, puis se repose ; verre clair,
        //  pas teinte, avec son fil de lumiere : la couleur passe a l'onglet choisi)
        val gonfle = if (glisse != null) 1f else (kotlin.math.abs(vite - lent) * 2.5f).coerceIn(0f, 1f)
        val bulle = Modifier.offset(x = w * g + 2.dp, y = 2.dp).size(width = (w * (d - g + 1f) - 4.dp).coerceAtLeast(0.dp), height = 52.dp)
            .graphicsLayer { scaleY = (1f - 0.06f * etire + 0.38f * gonfle) * leve; scaleX = (1f + 0.06f * gonfle) * leve }
        Box(if (gl) bulle.verreGL(RoundedCornerShape(26.dp), Color.White.copy(alpha = 0.10f), force = 18f) else bulle.verreTeinte(RoundedCornerShape(26.dp)))
        Row(Modifier.fillMaxWidth().pointerInput(n) {
                detectHorizontalDragGestures(
                    onDragStart = { o -> glisse = (o.x / wpx - 0.5f).coerceIn(0f, (n - 1).toFloat()) },
                    onDragEnd = { val j = kotlin.math.round(glisse ?: ici).toInt(); glisse = null; choisir(j) },
                    onDragCancel = { glisse = null }
                ) { ch, dx -> ch.consume(); glisse = ((glisse ?: ici) + dx / wpx).coerceIn(-0.25f, n - 0.75f) }
            }) {
            ONGLETS.forEachIndexed { j, o ->
                val i = o.i
                val lecteur = Rangee.ACTIF && i == 1 && St.onglet == 0
                val on = St.onglet == i || lecteur
                // (build 515 : seule l'icone grossit, sans etre coupee : avant, toute la case grossissait a l'interieur de sa
                //  decoupe, le haut de l'icone et le bas du mot etaient coupes et le mot sautait)
                // (build 516 : la loupe suit l'ecart entre les deux bords de la bulle : elle s'eteint en douceur quand la bulle se pose ;
                //  avant, elle restait pleine jusqu'a l'arret des ressorts puis retombait d'un coup : l'icone tremblait)
                val m = if (glisse != null) 1f else (kotlin.math.abs(vite - lent) * 2.5f).coerceIn(0f, 1f)
                val loupe = 1f + 0.30f * m * (1f - kotlin.math.abs(centre - j)).coerceIn(0f, 1f)
                val teinteOff = C.Doux.copy(alpha = 0.92f)
                val teinteOn = if (gl) C.Cyan else Color.White   // (build 517 : l'onglet choisi prend la couleur, comme MagicOS)
                // build 518 : l'icone s'anime quand la bulle passe dessus (une fois par passage) et quand elle devient l'onglet
                // choisi : chacune a son geste (manette qui se dandine, hexagone de Pulse qui tourne, note qui saute, tele qui
                // s'ecrase, grille qui gonfle). an : de 0 a 1 en 650 ms, le geste s'amortit vers la fin.
                val an = androidx.compose.runtime.remember { androidx.compose.animation.core.Animatable(1f) }
                // (build 519 : le geste part quand la bulle arrive sur l'onglet choisi (avant : a chaque passage, trop court pour
                //  se voir) ; en glissant au doigt, a chaque onglet survole ; 900 ms)
                val dessous = gl && glisse != null && kotlin.math.abs(centre - j) < 0.45f
                val geste = androidx.compose.animation.core.tween<Float>(900, easing = androidx.compose.animation.core.LinearEasing)
                // (build 523 : le geste part dans sa propre portee : il va toujours au bout. Avant, il etait lie a « onglet
                //  choisi » : en changeant vite d'onglet, il etait arrete net et l'icone restait figee au milieu du geste)
                val portee = androidx.compose.runtime.rememberCoroutineScope()
                fun lancer() { portee.launch { an.snapTo(0f); an.animateTo(1f, geste) } }
                androidx.compose.runtime.LaunchedEffect(dessous) { if (dessous) lancer() }
                androidx.compose.runtime.LaunchedEffect(on, gl) {         // (une seule fois, au premier contact de la bulle, meme si elle rebondit)
                    if (on && gl) {
                        androidx.compose.runtime.snapshotFlow { kotlin.math.abs(vite - j) < 0.35f }.first { it }
                        lancer()
                    }
                }
                val srcO = androidx.compose.runtime.remember { androidx.compose.foundation.interaction.MutableInteractionSource() }
                val buzz = vibre()
                // (build 516 : pas d'effet gelee sur les onglets : c'est la bulle qui bouge ; la case qui rebondissait faisait trembler l'icone)
                Column(Modifier.weight(1f).height(56.dp).clip(RoundedCornerShape(26.dp)).clickable(interactionSource = srcO, indication = null) {
                        buzz()
                        onde.lancer(6f * dens + wpx * (j + 0.5f), (6f + 28f) * dens)   // (centre de l'onglet, dans le verre de la barre)
                        choisir(j)
                    }, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                    Icon(o.ic, o.nom, tint = if (on) teinteOn else teinteOff, modifier = Modifier.size(21.dp)
                        .graphicsLayer {
                            val t = an.value; val am = (1f - t) * (1f - t)            // (amorti)
                            val pi = Math.PI.toFloat()
                            val sinA = kotlin.math.sin(t * 4f * pi)
                            var sx = loupe; var sy = loupe
                            when (o.i) {
                                2 -> { rotationZ = 26f * sinA * am; translationY = -3f * density * kotlin.math.sin(t * pi) }   // (Jeux : la manette se dandine)
                                5 -> { rotationZ = 360f * (1f - (1f - t) * (1f - t) * (1f - t)); val k = 1f + 0.25f * kotlin.math.sin(t * pi); sx *= k; sy *= k }   // (Pulse : un tour complet, gonfle au milieu)
                                1, 0 -> {                                                          // (Musique : la note saute et s'ecrase en retombant)
                                    val vol = (t / 0.6f).coerceAtMost(1f)
                                    translationY = -16f * density * kotlin.math.sin(vol * pi)
                                    if (t > 0.55f) { val e = kotlin.math.sin(((t - 0.55f) / 0.45f) * pi) * 0.25f * (1f - t); sx *= 1f + e * 2f; sy *= 1f - e * 2f }
                                    rotationZ = -12f * kotlin.math.sin(vol * pi)
                                }
                                3 -> { sx *= 1f + 0.32f * sinA * am; sy *= 1f - 0.28f * sinA * am } // (Tele : s'ecrase et rebondit)
                                else -> { val k = 1f + 0.42f * kotlin.math.sin(t * pi) * (1f - 0.5f * t) + 0.08f * sinA * am; sx *= k; sy *= k; rotationZ = 90f * (1f - am) }   // (Console : la grille gonfle et fait un quart de tour)
                            }
                            scaleX = sx; scaleY = sy; transformOrigin = androidx.compose.ui.graphics.TransformOrigin(0.5f, 0.6f)
                        })
                    Spacer(Modifier.height(3.dp))
                    Text(o.nom, color = if (on) teinteOn else teinteOff, fontSize = 11.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Normal, maxLines = 1, softWrap = false,
                        modifier = Modifier.graphicsLayer { if (on && gl) { val t = an.value; val k = 1f + 0.12f * kotlin.math.sin(t * Math.PI.toFloat()) * (1f - t); scaleX = k; scaleY = k } })   // (build 519 : le mot rebondit avec)
                }
            }
        }
    }
    }
}

@Composable
private fun BarreOnglets() {
    if (C.liquide) { BarreLiquide(); return }
    Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp)
        .verre(RoundedCornerShape(26.dp), C.Rose.copy(alpha = 0.35f), Color(0x1AFFFFFF)).padding(vertical = 6.dp, horizontal = 4.dp)) {
        ONGLETS.forEach { o ->
            val i = o.i; val nom = o.nom; val ic = o.ic
            val lecteur = Rangee.ACTIF && i == 1 && St.onglet == 0          // (l'ecran du lecteur compte dans Musique)
            val on = St.onglet == i || lecteur
            Column(Modifier.weight(1f).clip(RoundedCornerShape(20.dp)).appui {
                    if (lecteur) St.onglet = 1 else if (St.onglet == i) St.sous = "" else { St.onglet = i; if (Rangee.ACTIF) St.sous = "" }
                }.padding(vertical = 6.dp),
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
                    if (true) {                                       // (v314 : tous les jeux : valeurs épinglées)
                        if (Prive.ACTIF) Bouton("Compagnon de jeu et chrono", Modifier.fillMaxWidth(), plein = true, icone = Ic.Jeux) { St.jeuMasque = true; St.onglet = 4; St.sous = "Compagnon de jeu" }
                        Spacer(Modifier.height(8.dp))
                    }
                    Bouton("Chercheur de codes", Modifier.fillMaxWidth(), icone = Ic.Chercher) { St.jeuMasque = true; St.onglet = 4; St.sous = "Chercheur de codes" }
                    Spacer(Modifier.height(8.dp))
                    ApercuCapture(act)
                }
                if (g.padMask != 0 && Prive.ACTIF) {
                    Bouton("Manette dans le jeu", Modifier.fillMaxWidth(), plein = true, icone = Ic.Manette) { Prive.manetteOuvrir(act, g.padMask) }
                    Spacer(Modifier.height(8.dp))
                    Bouton("Bongos (Donkey Konga)", Modifier.fillMaxWidth(), icone = Ic.Manette) { Prive.bongosOuvrir(act, g.padMask) }   // (build 476 : manquaient dans
                    Spacer(Modifier.height(8.dp))                                                                                            //  cet ecran, seulement dans
                    Bouton("Clavier (Phantasy Star Online)", Modifier.fillMaxWidth(), icone = Ic.Manette) { Prive.clavierOuvrir(act, g.padMask) }   //  l'ancien)
                    Spacer(Modifier.height(8.dp))
                }
                Bouton("Masquer", Modifier.fillMaxWidth()) { St.jeuMasque = true }
            }
        }
    }
}
