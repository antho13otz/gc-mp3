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
import androidx.compose.runtime.withFrameNanos
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableIntStateOf
import android.graphics.Rect
import android.graphics.Canvas
import android.graphics.Bitmap
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

/** une image de la tele et l'instant ou elle est arrivee */
private class Trame(val bmp: Bitmap, val t: Long) { @Volatile var champ: Bitmap? = null }

/** build 507 : ecart moyen de luminosite (0 a 255) entre deux images, sur une version 80x60 ; -1 : pas comparable */
private fun ecartImages(a: Bitmap?, b: Bitmap?): Float {
    if (a == null || b == null) return -1f
    return try {
        val pa = IntArray(80 * 60); val pb = IntArray(80 * 60)
        Bitmap.createScaledBitmap(a, 80, 60, true).getPixels(pa, 0, 80, 0, 0, 80, 60)
        Bitmap.createScaledBitmap(b, 80, 60, true).getPixels(pb, 0, 80, 0, 0, 80, 60)
        var s = 0L
        for (k in pa.indices) {
            val x = pa[k]; val y = pb[k]
            s += kotlin.math.abs(((x shr 16 and 255) + (x shr 8 and 255) * 2 + (x and 255)) - ((y shr 16 and 255) + (y shr 8 and 255) * 2 + (y and 255))) / 4
        }
        s.toFloat() / pa.size
    } catch (e: Exception) { -1f }
}   // (champ : mouvement depuis la trame d'avant)

/** reglages de l'image : Rapide (320x240), Net (640x480), Max (640x480, compression tres legere) */
private val QUALITES = arrayOf(Triple("Rapide", 320, 62), Triple("Net", 640, 80), Triple("Max", 640, 92))

/** le flux de la tele : la console n'envoie que les blocs qui ont change (console v294 et plus : /api/ecranz),
 *  recolles ici sur la "toile" ; consoles plus anciennes : image entiere a chaque fois (/api/ecran) */
private class Flux {
    var toile: Bitmap? = null
    var seq = 0L
    var partiel = true
    var web = false
    var msConsole = 0                                     // (temps de preparation de l'image par la console)
    var ko = 0                                            // (taille recue)
    var plein = false                                     // (build 506 : la derniere image etait entiere / combien de blocs)
    var blocs = 0
    /** 1 : nouvelle image (dans img), 0 : rien de change, -1 : pas de reponse */
    var img: Bitmap? = null
    /** build 507 : une image entiere (GET /api/ecran), sans toile : pendant que tout l'ecran bouge, en 320x240 (la console
     *  v396 la prepare pendant l'envoi de la precedente). 1 : nouvelle image, -1 : pas de reponse */
    fun entiere(w: Int, q: Int): Int {
        var c: HttpURLConnection? = null
        try {
            c = URL("http://" + Api.host + "/api/ecran?w=$w&q=$q").openConnection() as HttpURLConnection
            c.connectTimeout = 2500; c.readTimeout = 4000; c.useCaches = false
            if (c.responseCode != 200) return -1
            web = c.getHeaderField("X-Web") == "1"
            val b = c.inputStream.use { it.readBytes() }
            msConsole = c.getHeaderField("X-Ms")?.toIntOrNull() ?: 0; ko = (b.size + 512) / 1024
            img = BitmapFactory.decodeByteArray(b, 0, b.size) ?: return -1
            plein = true; blocs = 0
            return 1
        } catch (e: Exception) {
            return -1
        } finally {
            c?.disconnect()
        }
    }
    fun suivante(w: Int, q: Int): Int {
        var c: HttpURLConnection? = null
        try {
            val chemin = if (partiel) "/api/ecranz?w=$w&q=$q&s=$seq" else "/api/ecran?w=$w&q=$q"
            c = URL("http://" + Api.host + chemin).openConnection() as HttpURLConnection
            c.connectTimeout = 2500; c.readTimeout = 4000; c.useCaches = false
            val code = c.responseCode
            if (partiel && code == 404) { partiel = false; return 0 }
            web = c.getHeaderField("X-Web") == "1"
            if (code == 204) { seq = c.getHeaderField("X-Seq")?.toLongOrNull() ?: 0L; plein = false; blocs = 0; return 0 }
            if (code != 200) return -1
            val b = c.inputStream.use { it.readBytes() }
            msConsole = c.getHeaderField("X-Ms")?.toIntOrNull() ?: 0; ko = (b.size + 512) / 1024
            plein = !partiel || c.getHeaderField("X-Plein") == "1"; blocs = c.getHeaderField("X-N")?.toIntOrNull() ?: 0
            if (!partiel || c.getHeaderField("X-Plein") == "1") {
                val bm = BitmapFactory.decodeByteArray(b, 0, b.size) ?: return -1
                if (partiel) { toile = bm.copy(Bitmap.Config.ARGB_8888, true); seq = c.getHeaderField("X-Seq")?.toLongOrNull() ?: 0L }
                img = bm
                return 1
            }
            val n = c.getHeaderField("X-N")?.toIntOrNull() ?: return -1
            val t = toile
            val bande = BitmapFactory.decodeByteArray(b, 2 * n, b.size - 2 * n)
            if (t == null || bande == null) { seq = 0; return 0 }              // (rien a recoller : image entiere la prochaine fois)
            val cv = Canvas(t)
            val de = Rect(); val vers = Rect()
            for (k in 0 until n) {
                val sx = (k % 40) * 16; val sy = (k / 40) * 16
                val tx = (b[2 * k].toInt() and 255) * 16; val ty = (b[2 * k + 1].toInt() and 255) * 16
                de.set(sx, sy, sx + 16, sy + 16); vers.set(tx, ty, tx + 16, ty + 16)
                cv.drawBitmap(bande, de, vers, null)
            }
            seq = c.getHeaderField("X-Seq")?.toLongOrNull() ?: 0L
            img = t.copy(Bitmap.Config.ARGB_8888, false)
            return 1
        } catch (e: Exception) {
            return -1
        } finally {
            c?.disconnect()
        }
    }
}

@Composable
fun EcranTele() {
    val act = LocalContext.current as Activity
    val paysage = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val prefs = remember { act.getSharedPreferences("gcs", 0) }
    var qualite by remember { mutableIntStateOf(prefs.getInt("tele_qualite", 1).coerceIn(0, 2)) }
    var upscaling by remember { mutableStateOf(prefs.getBoolean("tele_upscaling", Upscale.possible)) }   // (agrandie par le telephone)
    var fluide by remember { mutableStateOf(prefs.getBoolean("tele_fluide", true)) }                    // (images intermediaires 120 Hz)
    var auto by remember { mutableStateOf(prefs.getBoolean("tele_auto", true)) }                       // (build 506 / 507 : image plus legere pendant le mouvement)
    var vite by remember { mutableStateOf(false) }                                                     // (en ce moment : qualite de mouvement)
    var enDirect by remember { mutableStateOf(Rangee.ACTIF && prefs.getBoolean("tele_direct", false)) }   // (l'image ne part que si on la demande ; build 505, appli privee : le dernier choix est garde)
    // build 505 (appli privee) : l'onglet Tele range. Sous l'image, une seule ligne : en direct / arrete, la qualite, puis a
    // droite les reglages (⚙ : upscaling, 120 Hz, tactile, details, aide) et le plein ecran. L'etiquette sur l'image ne montre
    // que les images par seconde (les details : en la touchant). L'aide des gestes se masque. La manette est dans un cadre.
    var details by remember { mutableStateOf(!Rangee.ACTIF || prefs.getBoolean("tele_details", false)) }
    var aide by remember { mutableStateOf(!Rangee.ACTIF || prefs.getBoolean("tele_aide", true)) }
    var reglages by remember { mutableStateOf(false) }
    LaunchedEffect(enDirect) { if (Rangee.ACTIF) prefs.edit().putBoolean("tele_direct", enDirect).apply() }
    var tactile by remember { mutableStateOf(prefs.getBoolean("tele_tactile", true)) }   // (v303 : toucher l'image = cliquer a cet endroit)
    // (les dernieres images recues ; affichees avec un leger retard regulier, pour lisser les arrivees irregulieres)
    val trames = remember { java.util.ArrayDeque<Trame>() }
    var version by remember { mutableIntStateOf(0) }
    var b by remember { mutableStateOf<Trame?>(null) }      // (derniere image)
    var intervalle by remember { mutableFloatStateOf(80f) } // (moyenne glissante entre deux images, ms)
    var diag by remember { mutableStateOf("") }
    var web by remember { mutableStateOf(false) }
    var ips by remember { mutableFloatStateOf(0f) }
    var rate by remember { mutableStateOf(false) }
    var pleinEcran by remember { mutableStateOf(false) }
    var maintenant by remember { mutableLongStateOf(0L) }
    // (forme de l'image : celle de la tele d'apres le reglage de la console « Format de l'ecran », 4:3 ou 16:9 ;
    //  pas celle des pixels envoyes, ex. 640 x 574 en PAL, qui etirait l'image en hauteur)
    var format by remember { mutableFloatStateOf(if (prefs.getBoolean("tele_169", false)) 16f / 9f else 4f / 3f) }
    LaunchedEffect(St.connecte) {
        while (isActive && St.connecte) {
            val r = lire("/api/settings", 5000)
            val m = try { org.json.JSONObject(r ?: "").optJSONArray("m") } catch (e: Exception) { null }
            if (m != null) for (i in 0 until m.length()) {
                val e = m.optJSONArray(i) ?: continue
                if (e.optString(1).startsWith("Format de l") || (e.optInt(0) == 32 && e.optString(2).contains(":"))) {
                    val l = e.optString(2).startsWith("16")
                    format = if (l) 16f / 9f else 4f / 3f
                    prefs.edit().putBoolean("tele_169", l).apply()
                    break
                }
            }
            delay(15000)
        }
    }
    val interpole = fluide && Upscale.possible && (upscaling || Rangee.ACTIF)   // (build 506, appli privee : aussi sans upscaling)

    // (on quitte l'onglet : l'orientation redevient libre)
    DisposableEffect(Unit) { onDispose { if (pleinEcran) act.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED } }

    LaunchedEffect(qualite, enDirect, St.connecte, interpole, auto) {
        if (!enDirect || !St.connecte) return@LaunchedEffect
        val flux = Flux()
        trames.clear()
        val (_, w, q) = QUALITES[qualite]
        var n = 0
        var t0 = SystemClock.uptimeMillis()
        // build 507 (appli privee) : quand tout l'ecran bouge (la console renvoie l'image entiere ou beaucoup de blocs), les
        // images suivantes sont demandees entieres en 320x240 : quatre fois moins de travail pour la console, quatre fois
        // moins a envoyer, et la console v396 prepare la suivante pendant l'envoi. Le telephone l'agrandit (et dessine les
        // images intermediaires). Trois images presque pareilles de suite, ou 4 s passees : retour aux images partielles
        // en 640, en commencant par une image entiere nette (s=0). (506 : seulement une qualite plus basse.)
        val qVite = minOf(q, 80)
        var calme = 0
        var depuis = 0L
        var avant: Bitmap? = null
        vite = false
        while (isActive) {
            if (!St.visible || St.jeu != null && St.horsLigne) { delay(500); continue }
            val r = withContext(Dispatchers.IO) { if (vite) flux.entiere(320, qVite) else flux.suivante(w, q) }
            if (Rangee.ACTIF && auto && w > 320) {
                if (!vite) {
                    if (r == 1 && (flux.plein || flux.blocs > 120)) { vite = true; calme = 0; avant = null; depuis = SystemClock.uptimeMillis() }
                } else if (r == 1) {
                    val e = ecartImages(avant, flux.img); avant = flux.img
                    calme = if (e in 0f..2f) calme + 1 else 0
                    if (calme >= 3 || SystemClock.uptimeMillis() - depuis > 4000) { vite = false; calme = 0; avant = null; flux.seq = 0 }
                }
            } else vite = false
            web = flux.web
            if (r < 0) { rate = true; delay(800); continue }
            rate = false
            if (r == 0) { delay(12); continue }                 // (rien n'a change sur la tele)
            val nb = Trame(flux.img ?: continue, SystemClock.uptimeMillis())
            val na = b
            b = nb
            if (na != null) intervalle = intervalle * 0.85f + (nb.t - na.t).coerceIn(10L, 400L) * 0.15f
            diag = "console " + flux.msConsole + " ms · " + flux.ko + " Ko"
            trames.addLast(nb)
            while (trames.size > 5) trames.removeFirst()
            version++
            if (interpole && na != null && na.bmp.width == nb.bmp.width && na.bmp.height == nb.bmp.height) {
                launch(Dispatchers.Default) {                    // (mouvement de l'image d'avant vers celle-ci, a cote)
                    val w2 = nb.bmp.width; val h2 = nb.bmp.height
                    val pa = IntArray(w2 * h2); val pb = IntArray(w2 * h2)
                    na.bmp.getPixels(pa, 0, w2, 0, 0, w2, h2); nb.bmp.getPixels(pb, 0, w2, 0, 0, w2, h2)
                    val m = Mouvement.estimer(pa, pb, w2, h2)
                    nb.champ = Bitmap.createBitmap(Mouvement.pixels(m), m.bw, m.bh, Bitmap.Config.ARGB_8888)
                }
            }
            n++
            val now = SystemClock.uptimeMillis()
            if (now - t0 >= 1000) { ips = n * 1000f / (now - t0); n = 0; t0 = now }
        }
    }
    // (images intermediaires : on redessine a chaque rafraichissement de l'ecran, 120 Hz sur ton telephone)
    LaunchedEffect(interpole, enDirect) {
        if (!interpole || !enDirect) return@LaunchedEffect
        while (isActive) { withFrameNanos { }; maintenant = SystemClock.uptimeMillis() }
    }

    val bb = b
    val ecran: @Composable (Modifier) -> Unit = { m ->
        Box(m.aspectRatio(format).clip(RoundedCornerShape(if (paysage) 10.dp else 18.dp))
            .background(Color.Black).border(1.dp, Brush.linearGradient(listOf(C.Rose, C.Cyan)), RoundedCornerShape(if (paysage) 10.dp else 18.dp))
            .then(if (enDirect) Modifier.gestesTele(web, tactile) else Modifier)) {
            if (bb != null) {
                @Suppress("UNUSED_VARIABLE") val vv = version
                // (on montre l'instant "maintenant - retard" : entre les deux images qui l'encadrent, avec leur mouvement)
                val vue = if (interpole && trames.size >= 2) {
                    val ici = maintenant - (intervalle * 1.3f).toLong().coerceIn(40L, 300L)
                    var av: Trame? = null; var ap: Trame? = null
                    for (tr in trames) { if (tr.t <= ici) av = tr else { ap = tr; break } }
                    when {
                        av == null -> Vue(null, trames.first.bmp, null, 1f)
                        ap == null -> Vue(null, av.bmp, null, 1f)
                        ap.champ == null -> Vue(null, if (ici - av.t > (ap.t - av.t) / 2) ap.bmp else av.bmp, null, 1f)
                        else -> Vue(av.bmp, ap.bmp, ap.champ, ((ici - av.t).toFloat() / (ap.t - av.t)).coerceIn(0f, 1f))
                    }
                } else Vue(null, bb.bmp, null, 1f)
                ImageTele(vue, upscaling, Modifier.fillMaxSize())
            }
            else Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
                if (!St.connecte) Doux("Pas connecté à la console", centre = true)
                else if (!enDirect) { }
                else if (rate) Doux("La console n'envoie pas son image : mets-la à jour (build 774 ou plus)", Modifier.padding(20.dp), centre = true)
                else CircularProgressIndicator(color = C.Rose)
            }
            if (bb != null) Row(Modifier.align(Alignment.TopStart).padding(8.dp).clip(RoundedCornerShape(8.dp)).background(Color(0x99000000))
                .then(if (Rangee.ACTIF) Modifier.appui { details = !details; prefs.edit().putBoolean("tele_details", details).apply() } else Modifier).padding(horizontal = 7.dp, vertical = 3.dp)) {
                Box(Modifier.align(Alignment.CenterVertically).size(7.dp).clip(CircleShape).background(if (rate) C.Rouge else C.Vert))
                Spacer(Modifier.width(5.dp))
                Text(String.format(java.util.Locale.FRANCE, "%.0f img/s", ips) + (if (interpole) " → fluide" else "") + (if (vite) " · mouvement" else "") + (if (!details) "" else (if (diag.isNotEmpty()) " · $diag" else "") + (if (upscaling && Upscale.possible) " · upscaling" else "") + if (web) " · pointeur" else ""), color = Color.White, fontSize = 10.sp)
            }
            if (!enDirect && St.connecte) Box(Modifier.fillMaxSize().background(Color(0x99000000)), contentAlignment = Alignment.Center) {
                Bouton("Afficher la télé", plein = true, icone = Ic.Lecture) { enDirect = true }
            }
        }
    }
    val pleinEcranBascule = {
        pleinEcran = !paysage
        act.requestedOrientation = if (paysage) ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED else ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
    }
    val barre: @Composable () -> Unit = {
        if (Rangee.ACTIF) Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Puce(if (enDirect) "● En direct" else "Arrêtée", enDirect) { enDirect = !enDirect }
            Puce(QUALITES[qualite].first, qualite > 0) { qualite = (qualite + 1) % 3; prefs.edit().putInt("tele_qualite", qualite).apply() }
            Spacer(Modifier.weight(1f))
            Box {
                Box(Modifier.size(40.dp).verre(CircleShape).appui { reglages = true }, contentAlignment = Alignment.Center) { Icon(Ic.Reglages, "Réglages de l'image", tint = C.Texte, modifier = Modifier.size(20.dp)) }
                androidx.compose.material3.DropdownMenu(expanded = reglages, onDismissRequest = { reglages = false }) {
                    @Composable fun choix(nom: String, on: Boolean, faire: () -> Unit) =
                        androidx.compose.material3.DropdownMenuItem(text = { Text((if (on) "☑  " else "☐  ") + nom) }, onClick = faire)
                    if (Upscale.possible) choix("Upscaling (image agrandie par le téléphone)", upscaling) { upscaling = !upscaling; prefs.edit().putBoolean("tele_upscaling", upscaling).apply() }
                    if (Upscale.possible) choix("Fluide (images intermédiaires, 120 Hz)", fluide) { fluide = !fluide; prefs.edit().putBoolean("tele_fluide", fluide).apply() }
                    choix("Image réduite pendant le mouvement (plus fluide)", auto) { auto = !auto; prefs.edit().putBoolean("tele_auto", auto).apply() }
                    choix("Tactile (toucher = cliquer à cet endroit)", tactile) { tactile = !tactile; prefs.edit().putBoolean("tele_tactile", tactile).apply() }
                    choix("Détails sur l'image (débit, délai)", details) { details = !details; prefs.edit().putBoolean("tele_details", details).apply() }
                    choix("Aide des gestes", aide) { aide = !aide; prefs.edit().putBoolean("tele_aide", aide).apply() }
                }
            }
            Box(Modifier.size(40.dp).verre(CircleShape, C.Rose.copy(alpha = 0.5f)).appui(pleinEcranBascule), contentAlignment = Alignment.Center) { Icon(Ic.PleinEcran, "Plein écran", tint = C.Texte, modifier = Modifier.size(20.dp)) }
        }
        else Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Puce(QUALITES[qualite].first, qualite > 0) { qualite = (qualite + 1) % 3; prefs.edit().putInt("tele_qualite", qualite).apply() }
            if (Upscale.possible) Puce("Upscaling", upscaling) { upscaling = !upscaling; prefs.edit().putBoolean("tele_upscaling", upscaling).apply() }
            if (Upscale.possible && upscaling) Puce("Fluide 120 Hz", fluide) { fluide = !fluide; prefs.edit().putBoolean("tele_fluide", fluide).apply() }
            Puce(if (enDirect) "En direct" else "Image arrêtée", enDirect) { enDirect = !enDirect }
            Puce("Tactile", tactile) { tactile = !tactile; prefs.edit().putBoolean("tele_tactile", tactile).apply() }
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
            val ratio = format
            val lImg = minOf(h * ratio, maxWidth * 0.58f)
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
                ecran(Modifier.size(width = lImg, height = lImg / ratio))   // (largeur et hauteur : la rangee ne l'etire plus)
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
            if (aide) Doux((if (tactile) "Sur l'image : toucher = cliquer à cet endroit, appui long = retour, glisser = déplacer la flèche."
                 else if (web) "Onglet WEB : glisse pour bouger le pointeur, touche pour cliquer."
                 else "Sur l'image : toucher = A, appui long = B, glisser = la croix.") + (if (Rangee.ACTIF) "  (toucher ce texte pour le masquer)" else ""),
                 if (Rangee.ACTIF) Modifier.appui { aide = false; prefs.edit().putBoolean("tele_aide", false).apply() } else Modifier, taille = 11, couleur = C.Pale)
            Spacer(Modifier.height(12.dp))
            Column(if (Rangee.ACTIF) Modifier.fillMaxWidth().verre(RoundedCornerShape(26.dp)).padding(horizontal = 14.dp, vertical = 14.dp) else Modifier.fillMaxWidth()) {
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
            }
            Spacer(Modifier.height(16.dp))
        }
    }
}

/** le doigt sur l'image : tactile (console v303 : on clique la ou on touche), sinon manette (A, B, croix) ou pointeur (WEB) */
private fun toucher(u: Float, v: Float, c: Int) = Api.pool {
    Api.get("/api/toucher?u=" + String.format(java.util.Locale.ROOT, "%.4f", u.coerceIn(0f, 1f)) + "&v=" + String.format(java.util.Locale.ROOT, "%.4f", v.coerceIn(0f, 1f)) + "&c=" + c, 1500)
}
private fun Modifier.gestesTele(pointeur: Boolean, tactile: Boolean): Modifier = if (tactile) this
    .pointerInput(true) {
        detectTapGestures(
            onTap = { p -> toucher(p.x / size.width, p.y / size.height, 1) },
            onLongPress = { p -> toucher(p.x / size.width, p.y / size.height, 2) })
    }
    .pointerInput(true) {
        var t = 0L
        detectDragGestures { ch, _ ->                            // (la fleche suit le doigt, sans cliquer)
            ch.consume()
            val n = android.os.SystemClock.uptimeMillis()
            if (n - t >= 40) { t = n; toucher(ch.position.x / size.width, ch.position.y / size.height, 0) }
        }
    }
else this
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

// (le clavier ouvert sur la tele : ClavierTele.kt, sur tous les onglets)

@Suppress("unused")
private fun centreTexte() = TextAlign.Center
