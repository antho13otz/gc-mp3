package fr.gcstation.telecommande

// Appli : la notice d'un jeu (console v296 : /api/notice?id=<ID> -> {"n":pages}, &p=<n> -> la page en JPEG).
// Les pages sont rangees sur la carte SD de la console (SD:/gc-mp3/notices/<ID>/01.jpg...) ; l'appli les garde
// ensuite sur le telephone. Glisser : pages ; deux doigts : zoom ; double toucher : zoomer / revenir.

import android.content.Context
import android.graphics.Bitmap
import android.graphics.pdf.PdfRenderer
import android.net.Uri
import android.provider.OpenableColumns
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
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
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.runtime.Composable
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.graphics.TransformOrigin
import kotlinx.coroutines.launch
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
        if (!St.connecte) return 0                                      // (hors-ligne : Hors.pagesNotice)
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

    // ---------- v297 : ajouter une notice depuis le telephone (images ou PDF) ----------
    private const val PW = 480; private const val PH = 816              // (taille des pages, comme la console les lit)

    private fun nom(ctx: Context, u: Uri): String {
        try { ctx.contentResolver.query(u, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c -> if (c.moveToFirst()) return c.getString(0) ?: "" } } catch (e: Exception) { }
        return u.lastPathSegment ?: ""
    }
    /** ordre naturel des noms : "page 2" avant "page 10" */
    private fun cle(s: String): String = Regex("\\d+").replace(s.lowercase()) { it.value.padStart(6, '0') }

    private fun jpeg(b: Bitmap): ByteArray {
        val k = minOf(PW.toFloat() / b.width, PH.toFloat() / b.height, 1f)
        val w = maxOf(8, (b.width * k).toInt()); val h = maxOf(8, (b.height * k).toInt())
        val s = if (w != b.width || h != b.height) Bitmap.createScaledBitmap(b, w, h, true) else b
        val o = ByteArrayOutputStream(); s.compress(Bitmap.CompressFormat.JPEG, 88, o)
        if (s !== b) s.recycle()
        return o.toByteArray()
    }
    private fun lireImage(ctx: Context, u: Uri): Bitmap? {
        val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        ctx.contentResolver.openInputStream(u)?.use { BitmapFactory.decodeStream(it, null, o) }
        var ech = 1
        while (o.outWidth / (ech * 2) >= 1600 && o.outHeight / (ech * 2) >= 1600) ech *= 2   // (pas plus que necessaire : memoire)
        return ctx.contentResolver.openInputStream(u)?.use { BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = ech }) }
    }
    /** contenu (non blanc) d'une bande de l'image : [x0, y0, x1, y1] ou null ; part : fraction de points non blancs */
    private fun cadre(b: Bitmap, y0: Int, y1: Int): Pair<IntArray?, Float> {
        val w = b.width; val h = y1 - y0
        val col = IntArray(w); val lig = IntArray(h); var tout = 0
        val px = IntArray(w)
        for (y in 0 until h) {
            b.getPixels(px, 0, w, 0, y0 + y, w, 1)
            for (x in 0 until w) { val c = px[x]; if (((c shr 16) and 255) + ((c shr 8) and 255) + (c and 255) < 720) { col[x]++; lig[y]++; tout++ } }
        }
        val part = tout.toFloat() / (w * h)
        val xs = (0 until w).filter { col[it] > h / 2 }; val ys = (0 until h).filter { lig[it] > w / 2 }
        if (xs.size < w / 8 || ys.size < h / 8) return null to part
        return intArrayOf(xs.first(), y0 + ys.first(), xs.last() + 1, y0 + ys.last() + 1) to part
    }
    /** planches d'impression (2 doubles pages l'une sur l'autre, traits de coupe) -> pages dans l'ordre du livret :
     *  la 1re double page est le dos + la couverture (couverture en 1er, dos en dernier) */
    /** une planche d'impression ? (grandes marges blanches autour du contenu, traits de coupe) ; une page seule : non */
    private fun estPlanche(b: Bitmap): Boolean {
        val h2 = b.height / 2
        val ref = cadre(b, 0, h2).first ?: cadre(b, h2, b.height).first ?: return false
        return ref[0] > b.width * 0.06f && ref[2] < b.width * 0.94f && (ref[2] - ref[0]) > (ref[3] - ref[1])
    }
    private class Decoupe { val pages = ArrayList<ByteArray>(); var dos: ByteArray? = null; var premiere = true }
    /** une planche (traitee tout de suite : pas toutes les grandes images en memoire) */
    private fun decouper(b: Bitmap, z: Decoupe) {
        run {
            val h2 = b.height / 2
            val (ch, ph) = cadre(b, 0, h2); val (cb, pb) = cadre(b, h2, b.height)
            // (moitie sans cadre net, page presque blanche : meme place que l'autre moitie, decalee d'une demi-image)
            val haut = ch ?: cb?.let { intArrayOf(it[0], it[1] - h2, it[2], it[3] - h2) }
            val bas = cb ?: ch?.let { intArrayOf(it[0], it[1] + h2, it[2], it[3] + h2) }
            for ((c, part, ok) in listOf(Triple(haut, ph, ch != null), Triple(bas, pb, cb != null))) {
                if (c == null || (!ok && part < 0.005f)) continue           // (moitie vide)
                val x0 = c[0].coerceIn(0, b.width - 2); val x1 = c[2].coerceIn(x0 + 2, b.width)
                val y0 = c[1].coerceIn(0, b.height - 2); val y1 = c[3].coerceIn(y0 + 2, b.height)
                val mid = (x0 + x1) / 2
                val g = Bitmap.createBitmap(b, x0, y0, mid - x0, y1 - y0); val d = Bitmap.createBitmap(b, mid, y0, x1 - mid, y1 - y0)
                if (z.premiere) { z.pages.add(jpeg(d)); z.dos = jpeg(g); z.premiere = false } else { z.pages.add(jpeg(g)); z.pages.add(jpeg(d)) }
                g.recycle(); d.recycle()
            }
        }
    }
    /** pages a envoyer : un PDF (chaque page) ou des images (ordre des noms) ; les planches d'impression sont decoupees */
    fun preparer(ctx: Context, uris: List<Uri>, etat: (String) -> Unit): List<ByteArray> {   // (v299 : planches reconnues toutes seules)
        val l = uris.map { it to nom(ctx, it) }.sortedBy { cle(it.second) }
        val out = ArrayList<ByteArray>()
        val z = Decoupe()
        for ((u, n) in l) {
            val pdf = n.lowercase().endsWith(".pdf") || ctx.contentResolver.getType(u) == "application/pdf"
            if (pdf) {
                ctx.contentResolver.openFileDescriptor(u, "r")?.use { fd ->
                    PdfRenderer(fd).use { r ->
                        for (i in 0 until r.pageCount) {
                            etat("Lecture du PDF : page " + (i + 1) + " / " + r.pageCount)
                            r.openPage(i).use { pg ->
                                val k = 2f * PH / pg.height                             // (rendu net, puis reduit)
                                val b = Bitmap.createBitmap(maxOf(8, (pg.width * k).toInt()), maxOf(8, (pg.height * k).toInt()), Bitmap.Config.ARGB_8888)
                                b.eraseColor(android.graphics.Color.WHITE)
                                pg.render(b, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
                                if (estPlanche(b)) decouper(b, z) else out.add(jpeg(b))
                                b.recycle()
                            }
                        }
                    }
                }
            } else {
                etat("Lecture de " + n)
                val b = lireImage(ctx, u) ?: continue
                if (estPlanche(b)) decouper(b, z) else out.add(jpeg(b))
                b.recycle()
            }
        }
        if (z.pages.isNotEmpty()) { out.addAll(z.pages); z.dos?.let { out.add(it) } }   // (le dos de la notice a la fin)
        return out
    }
    /** envoie les pages a la console (remplace l'ancienne notice) ; null : tout est parti, sinon le message d'erreur */
    fun envoyer(dir: File, id: String, pages: List<ByteArray>, etat: (String) -> Unit): String? {
        if (Api.get("/api/notice?id=$id&del=1", 8000) == null) return "La console ne répond pas (build 790 ou plus récent ?)"
        File(dir, "notices/$id").deleteRecursively()                       // (les anciennes pages gardees sur le telephone)
        val ch = Api.canChunk()
        for ((i, b) in pages.withIndex()) {
            etat("Envoi à la console : page " + (i + 1) + " / " + pages.size)
            val n = String.format("%02d.jpg", i + 1)
            val r = if (ch) Api.putChunks("notice&nid=$id", n, ByteArrayInputStream(b), b.size.toLong(), null) else Api.put("notice&nid=$id", n, ByteArrayInputStream(b), b.size.toLong())
            if (r == null) return "Envoi interrompu à la page " + (i + 1)
        }
        Api.get("/api/notice?id=$id&fin=1", 5000)
        return null
    }
}

@Composable
fun NoticeEcran(act: StationActivity, id: String, nom: String, n: Int, fermer: () -> Unit) {
    val pager = rememberPagerState { n }
    var zoome by remember { mutableStateOf(false) }
    // (ouverture : la notice arrive de biais, comme sortie de la boite, et se pose a plat ; fermeture : l'inverse)
    val ouv = remember { Animatable(0f) }
    val scope = rememberCoroutineScope()
    LaunchedEffect(Unit) { ouv.animateTo(1f, tween(520, easing = FastOutSlowInEasing)) }
    val quitter: () -> Unit = { scope.launch { ouv.animateTo(0f, tween(380, easing = FastOutSlowInEasing)); fermer() } }
    Dialog(onDismissRequest = quitter, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(C.Fond.copy(alpha = ouv.value))) {
            HorizontalPager(pager, Modifier.fillMaxSize().graphicsLayer {
                    val a = ouv.value
                    alpha = a; scaleX = 0.5f + 0.5f * a; scaleY = 0.5f + 0.5f * a
                    rotationY = (1 - a) * 55f; cameraDistance = 14f * density
                    transformOrigin = TransformOrigin(0f, 0.5f); translationX = -(1 - a) * size.width * 0.25f
                }, userScrollEnabled = !zoome, beyondViewportPageCount = 1) { p ->
                // (une vraie page qui tourne : celle de droite se pose par-dessus en pivotant sur la reliure, a gauche ;
                //  celle de dessous ne bouge pas et s'assombrit)
                Box(Modifier.fillMaxSize().graphicsLayer {
                    val off = (p - pager.currentPage) - pager.currentPageOffsetFraction
                    translationX = -off * size.width
                    cameraDistance = 14f * density
                    if (off > 0f) {
                        transformOrigin = TransformOrigin(0f, 0.5f)
                        rotationY = -88f * off.coerceIn(0f, 1f)
                        alpha = if (off >= 0.99f) 0f else 1f
                    } else {
                        val sh = (-off).coerceIn(0f, 1f)
                        scaleX = 1f - 0.05f * sh; scaleY = 1f - 0.05f * sh; alpha = 1f - 0.6f * sh
                    }
                }) {
                    PageNotice(act, id, p + 1) { z -> if (p == pager.currentPage) zoome = z }
                }
            }
            Row(Modifier.fillMaxWidth().statusBarsPadding().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Notice · $nom", color = C.Texte, fontSize = 17.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Doux(if (pager.currentPage == 0) "Couverture · " + n + " pages" else "Page " + (pager.currentPage + 1) + " / " + n, taille = 12)
                }
                Box(Modifier.size(44.dp).verre(CircleShape).appui(quitter), contentAlignment = Alignment.Center) { Icon(Ic.Fermer, null, tint = C.Texte) }
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
    LaunchedEffect(id, p) { img = Notices.page(Hors.dir(act), id, p); if (img == null) rate = true }
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
