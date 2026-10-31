package fr.gcstation.telecommande

// Appli : la tele sur le telephone (second ecran). La console envoie ce qu'elle affiche en JPEG
// (GET /api/ecran : petit = 320x240, net = 640x480) ; deux demandes en meme temps pour enchainer les images.
// Le doigt sur l'image : toucher = A, appui long = B, glisser = la croix (un cran tous les 56 px) ;
// sur l'onglet WEB de la console (Cube Browser), le doigt devient le pointeur (toucher = clic).
// Dessous : la manette (croix, A B X Y, L Z R, START) et le clavier quand la tele en ouvre un. En paysage : plein ecran.

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.BitmapFactory
import android.os.SystemClock
import android.speech.RecognizerIntent
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.compose.foundation.layout.Column
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.atomic.AtomicLong
import kotlin.math.abs

private class Image1(val bmp: ImageBitmap, val w: Int, val h: Int, val web: Boolean, val ms: Int)

/** une image de la tele (sur le fil reseau) ; null = pas de reponse */
private fun imageTele(net: Boolean): Image1? {
    var c: HttpURLConnection? = null
    try {
        c = URL("http://" + Api.host + "/api/ecran?w=" + (if (net) 640 else 320) + "&q=" + (if (net) 72 else 58)).openConnection() as HttpURLConnection
        c.connectTimeout = 2500
        c.readTimeout = 4000
        c.useCaches = false
        if (c.responseCode != 200) return null
        val b = c.inputStream.use { it.readBytes() }
        val bm = BitmapFactory.decodeByteArray(b, 0, b.size) ?: return null
        val web = c.getHeaderField("X-Web") == "1"
        val ms = c.getHeaderField("X-Ms")?.toIntOrNull() ?: 0
        return Image1(bm.asImageBitmap(), bm.width, bm.height, web, ms)
    } catch (e: Exception) {
        return null
    } finally {
        c?.disconnect()
    }
}

@Composable
fun EcranTele() {
    val act = LocalContext.current as Activity
    val paysage = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val prefs = remember { act.getSharedPreferences("gcs", 0) }
    var net by remember { mutableStateOf(prefs.getBoolean("tele_nette", false)) }
    var upscaling by remember { mutableStateOf(prefs.getBoolean("tele_upscaling", Upscale.possible)) }   // (agrandie par le telephone)
    var enDirect by remember { mutableStateOf(false) }      // (l'image ne part que si on la demande : bouton Afficher la tele)
    var img by remember { mutableStateOf<Image1?>(null) }
    var ips by remember { mutableFloatStateOf(0f) }
    var rate by remember { mutableStateOf(false) }
    var pleinEcran by remember { mutableStateOf(false) }

    // (on quitte l'onglet : l'orientation redevient libre)
    DisposableEffect(Unit) { onDispose { if (pleinEcran) act.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED } }

    LaunchedEffect(net, enDirect, St.connecte) {
        if (!enDirect || !St.connecte) return@LaunchedEffect
        val seq = AtomicLong(0)
        val vue = AtomicLong(0)
        var n = 0
        var t0 = SystemClock.uptimeMillis()
        coroutineScope {
            repeat(2) {
                launch {
                    while (isActive) {
                        if (!St.visible || St.jeu != null && St.horsLigne) { delay(500); continue }
                        val moi = seq.incrementAndGet()
                        val r = withContext(Dispatchers.IO) { imageTele(net) }
                        if (r == null) { rate = true; delay(800); continue }
                        rate = false
                        if (moi > vue.get()) {                    // (une image plus ancienne que celle affichee : jetee)
                            vue.set(moi)
                            img = r
                            n++
                            val now = SystemClock.uptimeMillis()
                            if (now - t0 >= 1000) { ips = n * 1000f / (now - t0); n = 0; t0 = now }
                        }
                    }
                }
            }
        }
    }

    val i = img
    val ecran: @Composable (Modifier) -> Unit = { m ->
        Box(m.aspectRatio(if (i != null) i.w.toFloat() / i.h else 4f / 3f).clip(RoundedCornerShape(if (paysage) 10.dp else 18.dp))
            .background(Color.Black).border(1.dp, Brush.linearGradient(listOf(C.Rose, C.Cyan)), RoundedCornerShape(if (paysage) 10.dp else 18.dp))
            .then(if (enDirect) Modifier.gestesTele(i?.web == true) else Modifier)) {
            if (i != null) ImageTele(i.bmp, upscaling, Modifier.fillMaxSize())
            else Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
                if (!St.connecte) Doux("Pas connecté à la console", centre = true)
                else if (!enDirect) { }
                else if (rate) Doux("La console n'envoie pas son image : mets-la à jour (build 774 ou plus)", Modifier.padding(20.dp), centre = true)
                else CircularProgressIndicator(color = C.Rose)
            }
            if (i != null) Row(Modifier.align(Alignment.TopStart).padding(8.dp).clip(RoundedCornerShape(8.dp)).background(Color(0x99000000)).padding(horizontal = 7.dp, vertical = 3.dp)) {
                Box(Modifier.align(Alignment.CenterVertically).size(7.dp).clip(CircleShape).background(if (rate) C.Rouge else C.Vert))
                Spacer(Modifier.width(5.dp))
                Text(String.format(java.util.Locale.FRANCE, "%.0f img/s", ips) + (if (upscaling && Upscale.possible) " · upscaling" else "") + if (i.web) " · pointeur" else "", color = Color.White, fontSize = 10.sp)
            }
            if (!enDirect && St.connecte) Box(Modifier.fillMaxSize().background(Color(0x99000000)), contentAlignment = Alignment.Center) {
                Bouton("Afficher la télé", plein = true, icone = Ic.Lecture) { enDirect = true }
            }
        }
    }
    val barre: @Composable () -> Unit = {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Puce(if (net) "Net" else "Rapide", net) { net = !net; prefs.edit().putBoolean("tele_nette", net).apply() }
            if (Upscale.possible) Puce("Upscaling", upscaling) { upscaling = !upscaling; prefs.edit().putBoolean("tele_upscaling", upscaling).apply() }
            Puce(if (enDirect) "En direct" else "Image arrêtée", enDirect) { enDirect = !enDirect }
            Puce(if (paysage) "Portrait" else "Plein écran", false) {
                pleinEcran = !paysage
                act.requestedOrientation = if (paysage) ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED else ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
            }
        }
    }

    if (paysage) {
        // plein ecran : l'image prend toute la hauteur, les touches a cote sont a la taille de ce qui reste
        // (l'en-tete, la barre d'onglets et les barres d'Android sont caches : App.kt et ci-dessous)
        val vue = LocalView.current
        DisposableEffect(Unit) {
            val w = act.window
            val c = WindowCompat.getInsetsController(w, vue)
            c.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            c.hide(WindowInsetsCompat.Type.systemBars())
            onDispose { c.show(WindowInsetsCompat.Type.systemBars()) }
        }
        BoxWithConstraints(Modifier.fillMaxSize().displayCutoutPadding().padding(horizontal = 6.dp, vertical = 6.dp)) {
            val h = maxHeight
            val lImg = minOf(h * (if (i != null) i.w.toFloat() / i.h else 4f / 3f), maxWidth * 0.58f)
            val cote = (maxWidth - lImg) / 2 - 8.dp
            val sCroix = minOf(64.dp, (h - 62.dp) / 3, cote / 3).coerceAtLeast(30.dp)
            val k = minOf(1.2f, (h - 112.dp) / 150.dp, cote / 172.dp).coerceAtLeast(0.45f)
            val g = minOf(64.dp, (cote - 8.dp) / 2)
            Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Touche("L", "l", Modifier.size(width = g, height = 40.dp), C.Violet); Touche("Z", "z", Modifier.size(width = g, height = 40.dp), C.Violet)
                    }
                    Spacer(Modifier.height(14.dp))
                    Croix(sCroix)
                }
                ecran(Modifier.width(lImg))
                Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                        Touche("R", "r", Modifier.size(width = g, height = 40.dp), C.Violet)
                        Box(Modifier.size(40.dp).verre(CircleShape).appui {
                            pleinEcran = false; act.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
                        }, contentAlignment = Alignment.Center) { Icon(Ic.Fermer, null, tint = C.Texte, modifier = Modifier.size(18.dp)) }
                    }
                    Spacer(Modifier.height(8.dp))
                    Boutons(k)
                    Spacer(Modifier.height(8.dp))
                    Touche("START", "start", Modifier.size(width = 84.dp, height = 34.dp), C.Doux)
                }
            }
        }
    } else {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp)) {
            ecran(Modifier.fillMaxWidth())
            Spacer(Modifier.height(10.dp))
            barre()
            Spacer(Modifier.height(6.dp))
            Doux(if (i?.web == true) "Onglet WEB : glisse pour bouger le pointeur, touche pour cliquer."
                 else "Sur l'image : toucher = A, appui long = B, glisser = la croix.", taille = 11, couleur = C.Pale)
            Clavier()
            Spacer(Modifier.height(12.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Gachette("L", "l", Modifier.weight(1f)); Gachette("Z", "z", Modifier.weight(1f)); Gachette("R", "r", Modifier.weight(1f))
            }
            Spacer(Modifier.height(14.dp))
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
                Croix(50.dp)
                Boutons(0.92f)
            }
            Spacer(Modifier.height(10.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) { Touche("START", "start", Modifier.size(width = 120.dp, height = 40.dp), C.Doux) }
            Spacer(Modifier.height(16.dp))
        }
    }
}

/** le doigt sur l'image : manette (A, B, croix) ou pointeur (onglet WEB) */
private fun Modifier.gestesTele(pointeur: Boolean): Modifier = this
    .pointerInput(pointeur) {
        detectTapGestures(
            onTap = { if (pointeur) Api.mouse(0, 0, 0, 1) else Api.key("a") },
            onLongPress = { if (!pointeur) Api.key("b") })
    }
    .pointerInput(pointeur) {
        var acc = Offset.Zero
        var mx = 0f
        var my = 0f
        detectDragGestures(onDragStart = { acc = Offset.Zero; mx = 0f; my = 0f }) { ch, d ->
            ch.consume()
            if (pointeur) {
                // (la tele fait 640 points de large : un glissement sur toute l'image = toute la largeur de la tele)
                val k = 640f / size.width
                mx += d.x * k; my += d.y * k
                if (abs(mx) + abs(my) >= 3f) { Api.mouse(mx.toInt(), my.toInt(), 0, 0); mx -= mx.toInt(); my -= my.toInt() }
            } else {
                acc += d
                val pas = 56.dp.toPx()
                while (abs(acc.x) >= pas || abs(acc.y) >= pas) {
                    if (abs(acc.x) >= abs(acc.y)) { Api.key(if (acc.x > 0) "right" else "left"); acc = Offset(acc.x - (if (acc.x > 0) pas else -pas), 0f) }
                    else { Api.key(if (acc.y > 0) "down" else "up"); acc = Offset(0f, acc.y - (if (acc.y > 0) pas else -pas)) }
                }
            }
        }
    }

/** une touche de la manette : envoyee des l'appui */
@Composable
fun Touche(t: String, k: String, modifier: Modifier, couleur: Color, rond: Boolean = false, grand: Boolean = false) {
    val v = LocalView.current
    var appuye by remember { mutableStateOf(false) }
    val sh = if (rond) CircleShape else RoundedCornerShape(14.dp)
    Box(modifier.scale(if (appuye) 0.9f else 1f).clip(sh)
        .background(Brush.radialGradient(listOf(couleur.copy(alpha = if (appuye) 0.75f else 0.42f), couleur.copy(alpha = 0.12f))))
        .border(if (rond) 2.dp else 1.dp, couleur.copy(alpha = 0.85f), sh)
        .pointerInput(k) {
            detectTapGestures(onPress = {
                appuye = true
                v.performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY)
                Api.key(k)
                tryAwaitRelease()
                appuye = false
            })
        }, contentAlignment = Alignment.Center) {
        Text(t, color = Color.White, fontSize = if (grand) 24.sp else if (t.length > 2) 13.sp else 17.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun Gachette(t: String, k: String, modifier: Modifier = Modifier) = Touche(t, k, modifier.height(42.dp).let { if (modifier == Modifier) it.width(70.dp) else it }, C.Violet)

@Composable
private fun Croix(s: Dp) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Touche("▲", "up", Modifier.size(s), C.Cyan)
        Row {
            Touche("◀", "left", Modifier.size(s), C.Cyan)
            Box(Modifier.size(s).padding(s / 4).clip(CircleShape).background(Color(0x22FFFFFF)))
            Touche("▶", "right", Modifier.size(s), C.Cyan)
        }
        Touche("▼", "down", Modifier.size(s), C.Cyan)
    }
}

/** A (gros, vert), B (rouge), X et Y autour, comme sur la manette GameCube */
@Composable
private fun Boutons(k: Float) {
    Box(Modifier.size(width = (170 * k).dp, height = (150 * k).dp)) {
        Touche("A", "a", Modifier.padding(start = (46 * k).dp, top = (48 * k).dp).size((72 * k).dp), C.Vert, rond = true, grand = true)
        Touche("B", "b", Modifier.padding(start = 0.dp, top = (100 * k).dp).size((46 * k).dp), C.Rouge, rond = true)
        Touche("X", "x", Modifier.padding(start = (122 * k).dp, top = (34 * k).dp).size((46 * k).dp), C.Cyan, rond = true)
        Touche("Y", "y", Modifier.padding(start = (50 * k).dp, top = 0.dp).size((46 * k).dp), C.Violet, rond = true)
    }
}

/** clavier ouvert sur la tele : le texte part en direct (et la dictee) */
@Composable
private fun Clavier() {
    val ctx = LocalContext.current
    val s = St.etat
    val kb = (s?.optInt("kb", 0) ?: 0) != 0
    var txt by remember { mutableStateOf("") }
    var tape by remember { mutableStateOf(false) }
    val dictee = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { r ->
        val l = r.data?.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS)
        if (!l.isNullOrEmpty()) { txt = if (txt.isEmpty()) l[0] else txt + (if (txt.endsWith(" ")) "" else " ") + l[0]; tape = true }
    }
    LaunchedEffect(kb, s?.optString("kbt", "")) { if (kb && !tape) txt = s?.optString("kbt", "") ?: "" ; if (!kb) tape = false }
    LaunchedEffect(txt, tape) { if (tape) { delay(250); lire("/api/type?go=0&t=" + Api.enc(txt), 3000) } }
    if (!kb) return
    Spacer(Modifier.height(10.dp))
    Column(Modifier.fillMaxWidth().verre(RoundedCornerShape(20.dp), C.Cyan.copy(alpha = 0.6f)).padding(14.dp)) {
        val h = s?.optString("kbh", "") ?: ""
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(Ic.Clavier, null, tint = C.Cyan, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
            Text(h.ifEmpty { "Clavier ouvert sur la télé" }, color = C.Cyan, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        }
        Spacer(Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f)) { Champ(txt, { txt = it; tape = true }, "Écris ici : le texte part sur la télé") }
            Spacer(Modifier.width(8.dp))
            Box(Modifier.size(46.dp).verre(CircleShape).appui {
                val it = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH)
                    .putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
                    .putExtra(RecognizerIntent.EXTRA_LANGUAGE, java.util.Locale.getDefault().toLanguageTag())
                    .putExtra(RecognizerIntent.EXTRA_PROMPT, "Parle : le texte s'écrit sur la télé")
                try { dictee.launch(it) } catch (e: Exception) { Toast.makeText(ctx, "Pas de reconnaissance vocale sur ce téléphone", Toast.LENGTH_LONG).show() }
            }, contentAlignment = Alignment.Center) { Icon(Ic.Micro, null, tint = C.Texte) }
            Spacer(Modifier.width(8.dp))
            Bouton("OK", plein = true) { tape = false; val t = txt; Api.pool { Api.get("/api/type?go=1&t=" + Api.enc(t), 3000) } }
        }
    }
}

@Suppress("unused")
private fun centreTexte() = TextAlign.Center
