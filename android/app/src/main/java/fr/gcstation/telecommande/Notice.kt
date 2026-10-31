package fr.gcstation.telecommande

// Appli : la notice d'un jeu (console v296 : /api/notice?id=<ID> -> {"n":pages}, &p=<n> -> la page en JPEG).
// Les pages sont rangees sur la carte SD de la console (SD:/gc-mp3/notices/<ID>/01.jpg...) ; l'appli les garde
// ensuite sur le telephone. Glisser : pages ; deux doigts : zoom ; double toucher : zoomer / revenir.

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

object Notices {
    /** nombre de pages de la notice du jeu (0 : pas de notice, ou console trop ancienne) */
    suspend fun pages(id: String): Int {
        val r = lire("/api/notice?id=$id") ?: return 0
        return try { JSONObject(r).optInt("n") } catch (e: Exception) { 0 }
    }
    /** une page : le telephone, sinon la console */
    suspend fun page(dir: File, id: String, p: Int): ImageBitmap? = withContext(Dispatchers.IO) {
        val f = File(dir, "notices/$id/" + String.format("%02d", p) + ".jpg")
        var b: ByteArray? = if (f.exists() && f.length() > 0) f.readBytes() else null
        if (b == null) {
            b = Api.getBytes("/api/notice?id=$id&p=$p", 15000)
            if (b != null && b.size > 100) try { f.parentFile?.mkdirs(); f.writeBytes(b) } catch (e: Exception) { }
        }
        if (b == null) null else BitmapFactory.decodeByteArray(b, 0, b.size)?.asImageBitmap()
    }
}

@Composable
fun NoticeEcran(act: StationActivity, id: String, nom: String, n: Int, fermer: () -> Unit) {
    val pager = rememberPagerState { n }
    var zoome by remember { mutableStateOf(false) }
    Dialog(onDismissRequest = fermer, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(C.Fond)) {
            HorizontalPager(pager, Modifier.fillMaxSize(), userScrollEnabled = !zoome, beyondViewportPageCount = 1) { p ->
                PageNotice(act, id, p + 1) { z -> if (p == pager.currentPage) zoome = z }
            }
            Row(Modifier.fillMaxWidth().statusBarsPadding().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Notice · $nom", color = C.Texte, fontSize = 17.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Doux(if (pager.currentPage == 0) "Couverture · " + n + " pages" else "Page " + (pager.currentPage + 1) + " / " + n, taille = 12)
                }
                Box(Modifier.size(44.dp).verre(CircleShape).appui(fermer), contentAlignment = Alignment.Center) { Icon(Ic.Fermer, null, tint = C.Texte) }
            }
            Doux("Glisser : pages · deux doigts : zoomer · double toucher : zoomer / revenir", Modifier.align(Alignment.BottomCenter).navigationBarsPadding().padding(16.dp), 11, C.Pale)
        }
    }
}

@Composable
private fun PageNotice(act: StationActivity, id: String, p: Int, surZoom: (Boolean) -> Unit) {
    var img by remember(id, p) { mutableStateOf<ImageBitmap?>(null) }
    var rate by remember(id, p) { mutableStateOf(false) }
    var sc by remember { mutableFloatStateOf(1f) }
    var off by remember { mutableStateOf(Offset.Zero) }
    LaunchedEffect(id, p) { img = Notices.page(act.cacheDir, id, p); if (img == null) rate = true }
    Box(Modifier.fillMaxSize().padding(top = 80.dp, bottom = 40.dp)
        .pointerInput(p) {
            detectTapGestures(onDoubleTap = { t ->
                if (sc > 1.05f) { sc = 1f; off = Offset.Zero } else { sc = 2.5f; off = (Offset(size.width / 2f, size.height / 2f) - t) * 1.5f }
                surZoom(sc > 1.05f)
            })
        }
        .pointerInput(p) {                                // (deux doigts, ou deja zoomee : zoom et deplacement ; sinon le glisser tourne les pages)
            awaitEachGesture {
                awaitFirstDown(requireUnconsumed = false)
                do {
                    val ev = awaitPointerEvent()
                    if (ev.changes.count { it.pressed } >= 2 || sc > 1.01f) {
                        sc = (sc * ev.calculateZoom()).coerceIn(1f, 5f)
                        off = if (sc <= 1.01f) Offset.Zero else off + ev.calculatePan()
                        ev.changes.forEach { if (it.positionChanged()) it.consume() }
                        surZoom(sc > 1.05f)
                    }
                } while (ev.changes.any { it.pressed })
            }
        }, contentAlignment = Alignment.Center) {
        val im = img
        if (im != null) Image(im, null, Modifier.fillMaxSize().graphicsLayer { scaleX = sc; scaleY = sc; translationX = off.x; translationY = off.y }, contentScale = ContentScale.Fit)
        else Doux(if (rate) "Page introuvable" else "Chargement de la page…", taille = 13)
    }
}
