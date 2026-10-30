@file:OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class, androidx.compose.ui.text.ExperimentalTextApi::class,
    androidx.compose.material3.ExperimentalMaterial3Api::class)

package fr.gcstation.telecommande

// Appli : Lecture (pochette, paroles synchronisees, progression, commandes, volume) et
// Musique (morceaux, radios, file d'attente).

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.SystemClock
import android.widget.Toast
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

fun horloge(s0: Int): String { val s = maxOf(0, s0); return (s / 60).toString() + ":" + (if (s % 60 < 10) "0" else "") + (s % 60) }

/** un appel a la console depuis une coroutine (fil reseau), null = pas de reponse */
suspend fun lire(path: String, ms: Int = 4000): String? = withContext(Dispatchers.IO) { Api.get(path, ms) }
suspend fun lireOctets(path: String, ms: Int = 4000): ByteArray? = withContext(Dispatchers.IO) { Api.getBytes(path, ms) }

// ------------------------------------------------------------------ Lecture
@Composable
fun EcranLecture() {
    val s = St.etat
    val titre = s?.optString("title", "") ?: ""
    val sub = s?.optString("sub", "") ?: ""
    val joue = (s?.optInt("playing", 0) ?: 0) != 0
    val el = s?.optInt("el", 0) ?: 0
    val dur = s?.optInt("dur", 0) ?: 0
    val cle = if ((s?.optInt("cov", 0) ?: 0) != 0) "$titre|$sub" else ""
    var pochette by remember { mutableStateOf<ImageBitmap?>(null) }
    LaunchedEffect(cle) {
        if (cle.isEmpty()) { pochette = null; return@LaunchedEffect }
        val b = lireOctets("/cover.bmp?k=" + Api.enc(cle))
        val bm: Bitmap? = if (b == null) null else withContext(Dispatchers.Default) { BitmapFactory.decodeByteArray(b, 0, b.size) }
        pochette = bm?.asImageBitmap()
    }
    // paroles : [ms, texte], position = pos + temps passe (si la lecture tourne)
    var paroles by remember { mutableStateOf<List<Pair<Long, String>>>(emptyList()) }
    var pos by remember { mutableLongStateOf(0L) }
    var posAt by remember { mutableLongStateOf(0L) }
    var parolesJoue by remember { mutableStateOf(false) }
    var maintenant by remember { mutableLongStateOf(0L) }
    var toutes by remember { mutableStateOf(false) }
    LaunchedEffect("$titre|$sub", joue) {
        while (true) {
            val t0 = SystemClock.uptimeMillis()
            val r = lire("/api/lyrics")
            val j = try { if (r != null) JSONObject(r) else null } catch (e: Exception) { null }
            val l = j?.optJSONArray("l")
            if (l == null || l.length() == 0) paroles = emptyList()
            else {
                if (l.length() != paroles.size) paroles = (0 until l.length()).mapNotNull { k -> l.optJSONArray(k)?.let { it.optLong(0) to it.optString(1) } }
                val now = SystemClock.uptimeMillis()
                pos = j.optLong("pos") + (now - t0) / 2
                posAt = now
                parolesJoue = j.optInt("play") != 0
            }
            delay(if (paroles.isEmpty()) 20000L else 10000L)
        }
    }
    LaunchedEffect(paroles.isNotEmpty()) { while (paroles.isNotEmpty()) { maintenant = SystemClock.uptimeMillis(); delay(100) } }
    val ms = pos + (if (parolesJoue) maintenant - posAt else 0L) + 150
    var cur = -1
    for (i in paroles.indices) if (paroles[i].first <= ms) cur = i else break

    BoxWithConstraints(Modifier.fillMaxSize()) {
        val largeur = maxWidth
        val hauteur = maxHeight
        val p = pochette
        if (p != null) Image(p, null, Modifier.fillMaxSize().blur(60.dp).alpha(0.35f), contentScale = ContentScale.Crop)
        Column(Modifier.fillMaxSize().padding(horizontal = 24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            val taille = minOf(largeur - 48.dp, hauteur * 0.42f)
            Spacer(Modifier.height(8.dp))
            Box(Modifier.size(taille).clip(RoundedCornerShape(28.dp)).background(C.Neon).padding(3.dp)) {
                Box(Modifier.fillMaxSize().clip(RoundedCornerShape(25.dp)).background(Color(0xFF140A26)), contentAlignment = Alignment.Center) {
                    if (p != null) Image(p, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                    else Icon(Ic.Musique, null, tint = C.Rose.copy(alpha = 0.6f), modifier = Modifier.size(taille * 0.4f))
                }
            }
            Spacer(Modifier.height(18.dp))
            Text(titre.ifEmpty { "Rien en lecture" }, color = C.Texte, fontSize = 22.sp, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis)
            if (sub.isNotEmpty()) Text(sub, color = C.Rose, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            val info = s?.optString("info", "") ?: ""
            if (info.isNotEmpty()) Doux(info, taille = 12, lignes = 1)
            // paroles : ligne d'avant, en cours, d'apres (toucher : toutes)
            Column(Modifier.fillMaxWidth().height(70.dp).appui { if (paroles.isNotEmpty()) toutes = true }, horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center) {
                if (paroles.isNotEmpty()) {
                    Doux(if (cur > 0) paroles[cur - 1].second else "", taille = 12, couleur = C.Pale, lignes = 1, centre = true)
                    Text(if (cur >= 0) paroles[cur].second else "♪", style = TextStyle(brush = Brush.linearGradient(listOf(C.Cyan, C.Rose))), fontSize = 17.sp,
                        fontWeight = FontWeight.Bold, textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    Doux(if (cur + 1 < paroles.size) paroles[cur + 1].second else "", taille = 12, couleur = C.Pale, lignes = 1, centre = true)
                } else {
                    val lyr = s?.optString("lyr", "") ?: ""
                    if (lyr.isNotEmpty()) Text(lyr, color = C.Cyan, fontSize = 15.sp, textAlign = TextAlign.Center, maxLines = 2)
                }
            }
            // progression
            val f = if (dur > 0) minOf(1f, el / dur.toFloat()) else 0f
            val fa by animateFloatAsState(f, tween(if (f < 0.01f) 0 else 950, easing = LinearEasing), label = "pos")
            Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(Color(0x26FFFFFF))) {
                Box(Modifier.fillMaxHeight().fillMaxWidth(fa).clip(RoundedCornerShape(3.dp)).background(C.Neon))
            }
            Row(Modifier.fillMaxWidth().padding(top = 4.dp)) {
                Doux(horloge(el), Modifier.weight(1f), 12)
                Doux(if (dur > 0) horloge(dur) else "", taille = 12)
            }
            Spacer(Modifier.weight(1f))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                Rond(Ic.Precedent, 56.dp) { Api.cmd("prev", -1) }
                Rond(if (joue) Ic.Pause else Ic.Lecture, 80.dp, plein = true) { Api.cmd("toggle", -1) }
                Rond(Ic.Suivant, 56.dp) { Api.cmd("next", -1) }
                Rond(Ic.Stop, 44.dp) { Api.cmd("stop", -1) }
            }
            Spacer(Modifier.height(14.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(44.dp).verre(RoundedCornerShape(22.dp)).appui { if (!PlaybackService.adjust(-1)) Api.vol(false) }, contentAlignment = Alignment.Center) {
                    Text("−", color = C.Texte, fontSize = 22.sp) }
                Row(Modifier.width(130.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                    Icon(Ic.Volume, null, tint = C.Cyan, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text((s?.optInt("vol", 0) ?: 0).toString() + " %", color = C.Cyan, fontSize = 16.sp, fontWeight = FontWeight.Bold)
                }
                Box(Modifier.size(44.dp).verre(RoundedCornerShape(22.dp)).appui { if (!PlaybackService.adjust(1)) Api.vol(true) }, contentAlignment = Alignment.Center) {
                    Text("+", color = C.Texte, fontSize = 22.sp) }
            }
            Spacer(Modifier.height(14.dp))
        }
    }
    if (toutes) {
        ModalBottomSheet(onDismissRequest = { toutes = false }, containerColor = Color(0xFF140A26)) {
            val st = rememberLazyListState()
            LaunchedEffect(cur) { if (cur >= 0) st.animateScrollToItem(maxOf(0, cur - 3)) }
            Text(titre, color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(horizontal = 24.dp))
            LazyColumn(Modifier.fillMaxWidth().height(460.dp), st, contentPadding = PaddingValues(24.dp)) {
                itemsIndexed(paroles) { i, l ->
                    Text(l.second, Modifier.fillMaxWidth().padding(vertical = 7.dp), textAlign = TextAlign.Center, fontSize = 18.sp,
                        fontWeight = if (i == cur) FontWeight.Bold else FontWeight.Normal,
                        color = if (i == cur) C.Cyan else if (i < cur) C.Pale else C.Doux)
                }
            }
        }
    }
}

// ------------------------------------------------------------------ Musique / Radio / File d'attente
@Composable
fun EcranMusique() {
    var vue by remember { mutableIntStateOf(0) }               // 0 morceaux, 1 radios, 2 file
    Column(Modifier.fillMaxSize().padding(horizontal = 16.dp)) {
        RangeeDePuces {
            Puce("Morceaux", vue == 0) { vue = 0 }
            Puce("Radios", vue == 1) { vue = 1 }
            Puce("À suivre", vue == 2) { vue = 2 }
        }
        Spacer(Modifier.height(10.dp))
        when (vue) { 0 -> Liste(false); 1 -> Liste(true); else -> FileAttente() }
    }
}

private object Cache { var morceaux: List<Array<String>>? = null; var radios: List<Array<String>>? = null; var version = -1 }

@Composable
private fun Liste(radio: Boolean) {
    val ctx = LocalContext.current
    var tout by remember { mutableStateOf(if (radio) Cache.radios else Cache.morceaux) }
    var msg by remember { mutableStateOf("Chargement…") }
    var q by remember { mutableStateOf("") }
    var menu by remember { mutableStateOf(-1) }
    LaunchedEffect(radio, St.listes) {
        if (Cache.version != St.listes) { Cache.morceaux = null; Cache.radios = null; Cache.version = St.listes }
        if ((if (radio) Cache.radios else Cache.morceaux) != null) { tout = if (radio) Cache.radios else Cache.morceaux; return@LaunchedEffect }
        val r = lire(if (radio) "/api/stations" else "/api/tracks", 8000)
        if (r == null) { msg = "Pas de réponse de la console"; return@LaunchedEffect }
        val l = ArrayList<Array<String>>()
        try {
            val j = JSONArray(r)
            for (i in 0 until j.length()) {
                if (radio) l.add(arrayOf(j.optString(i, ""), ""))
                else { val e = j.optJSONArray(i); l.add(arrayOf(e?.optString(0, "") ?: "", e?.optString(1, "") ?: "")) }
            }
        } catch (e: Exception) { }
        if (radio) Cache.radios = l else { Cache.morceaux = l; QueueTab.tracks = l.toMutableList() }
        tout = l
    }
    val l = tout
    val lq = q.trim().lowercase()
    val vus = remember(l, lq) { if (l == null) emptyList() else l.indices.filter { lq.isEmpty() || l[it][0].lowercase().contains(lq) || l[it][1].lowercase().contains(lq) } }
    Champ(q, { q = it }, if (radio) "Chercher une radio" else "Chercher un morceau, un artiste", icone = Ic.Chercher)
    if (radio) {
        Spacer(Modifier.height(8.dp))
        Bouton("Globe des radios du monde", Modifier.fillMaxWidth(), plein = true, icone = Ic.Globe) { ctx.startActivity(Intent(ctx, GlobeActivity::class.java)) }
    }
    Doux(if (l == null) msg else vus.size.toString() + (if (radio) " radio" else " morceau") + (if (vus.size > 1) (if (radio) "s" else "x") else "") + if (radio) "" else " · appui long : file d'attente",
        Modifier.padding(start = 6.dp, top = 8.dp, bottom = 6.dp), 12)
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        itemsIndexed(vus, key = { _, k -> k }) { _, k ->
            val it = l!![k]
            Ligne(it[0], it[1], Modifier.combinedClickable(
                onClick = {
                    Api.cmd(if (radio) "radio" else "play", k)
                    Toast.makeText(ctx, (if (radio) "Radio : " else "Lecture : ") + it[0], Toast.LENGTH_SHORT).show()
                },
                onLongClick = { if (!radio) menu = k }),
                gauche = { Pastille(if (radio) Ic.Radio else Ic.Musique, if (radio) C.Cyan else C.Rose, 36.dp) })
        }
    }
    if (menu >= 0 && l != null && menu < l.size) {
        val k = menu
        ModalBottomSheet(onDismissRequest = { menu = -1 }, containerColor = Color(0xFF140A26)) {
            Column(Modifier.padding(horizontal = 20.dp).padding(bottom = 28.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(l[k][0], color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                if (l[k][1].isNotEmpty()) Doux(l[k][1])
                Bouton("Lire maintenant", Modifier.fillMaxWidth(), plein = true, icone = Ic.Lecture) { menu = -1; Api.cmd("play", k) }
                Bouton("Lire juste après", Modifier.fillMaxWidth(), icone = Ic.Suivant) { menu = -1; fileAjout(ctx, k, true) }
                Bouton("Ajouter à la fin de la file", Modifier.fillMaxWidth(), icone = Ic.File) { menu = -1; fileAjout(ctx, k, false) }
            }
        }
    }
}

private fun fileAjout(ctx: android.content.Context, k: Int, ensuite: Boolean) {
    Api.get("/api/queueadd?i=$k&d=" + (if (ensuite) 1 else 0)) { r ->
        Toast.makeText(ctx, if (r != null && r.contains("\"ok\":1")) (if (ensuite) "Joué juste après" else "Ajouté à la file") else "Pas possible (console à mettre à jour ?)", Toast.LENGTH_SHORT).show()
    }
}

@Composable
private fun FileAttente() {
    var file by remember { mutableStateOf<List<Int>?>(null) }
    var noms by remember { mutableStateOf(Cache.morceaux) }
    LaunchedEffect(Unit) {
        if (noms == null) {
            val r = lire("/api/tracks", 8000)
            if (r != null) try {
                val j = JSONArray(r)
                val l = (0 until j.length()).map { val e = j.optJSONArray(it); arrayOf(e?.optString(0) ?: "", e?.optString(1) ?: "") }
                Cache.morceaux = l; noms = l
            } catch (e: Exception) { }
        }
        while (true) {
            val r = lire("/api/queue")
            if (r != null) try { val j = JSONArray(r); file = (0 until j.length()).map { j.getInt(it) } } catch (e: Exception) { }
            delay(3500)
        }
    }
    val f = file
    Doux(if (f == null) "Chargement…" else if (f.isEmpty()) "Fin de la file (rien à suivre)" else f.size.toString() + " morceau" + (if (f.size > 1) "x" else "") + " à suivre · toucher pour le jouer",
        Modifier.padding(start = 6.dp, bottom = 6.dp), 12)
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        itemsIndexed(f ?: emptyList()) { i, k ->
            val n = noms
            val t = if (n != null && k >= 0 && k < n.size) n[k] else arrayOf("Morceau $k", "")
            Ligne(t[0], t[1], Modifier.appui { Api.cmd("play", k) }, gauche = {
                Box(Modifier.size(36.dp), contentAlignment = Alignment.Center) { Text((i + 1).toString(), color = C.Cyan, fontSize = 16.sp, fontWeight = FontWeight.Bold) }
            })
        }
    }
}

/** carre aux bords arrondis, pour les grilles */
fun Modifier.carre(): Modifier = this.aspectRatio(1f)
