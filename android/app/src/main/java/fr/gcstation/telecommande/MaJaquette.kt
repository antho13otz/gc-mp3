package fr.gcstation.telecommande

// Appli (v313) : « Ma jaquette » : la jaquette de TA boite (ta region, ta langue, en haute definition) a la place de celle
// de GameTDB. Pour l'avant, le dos et la tranche : une photo (galerie, appareil photo, capture d'une annonce...), les
// 4 coins poses sur ceux de la jaquette, l'appli redresse la photo (perspective : Matrix.setPolyToPoly) et assemble la
// jaquette complete aux proportions de GameTDB (dos 47,5 % | tranche 5 % | avant 47,5 %, boite de 135 x 190 mm).
// Une face pas donnee : celle de GameTDB. Garde sur le telephone : jaquettes/perso/<id>.jpg (et chaque face a part,
// pour en refaire une seule).

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.graphics.Paint
import android.graphics.Rect
import android.net.Uri
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.material3.Text
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

object MaJaquette {
    const val H = 1800                                             // (hauteur de la jaquette assemblee)
    const val W_FACE = 1280                                        // (une face : 135 x 190 mm)
    val W = Math.round(W_FACE / 0.475f)                            // (largeur totale : dos 47,5 % + tranche 5 % + avant 47,5 %)
    val W_TR = W - 2 * W_FACE
    val FACES = arrayOf("avant", "dos", "tranche")

    fun fichier(dir: File, id: String) = File(dir, "jaquettes/perso/$id.jpg")
    fun face(dir: File, id: String, k: Int) = File(dir, "jaquettes/perso/$id-" + FACES[k] + ".jpg")
    fun existe(dir: File, id: String) = fichier(dir, id).exists()

    fun charger(dir: File, id: String): Bitmap? {
        val f = fichier(dir, id)
        if (!f.exists()) return null
        return BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inPreferredConfig = Bitmap.Config.ARGB_8888 })
    }
    fun chargerFace(dir: File, id: String, k: Int): Bitmap? {
        val f = face(dir, id, k)
        return if (f.exists()) BitmapFactory.decodeFile(f.path) else null
    }
    fun effacer(dir: File, id: String) { fichier(dir, id).delete(); for (k in 0 until 3) face(dir, id, k).delete() }

    /** une photo choisie (dans le bon sens), 2400 px au plus ; null : illisible */
    fun lire(ctx: Context, uri: Uri, maxi: Int = 2400): Bitmap? = try {
        if (Build.VERSION.SDK_INT >= 28) {
            ImageDecoder.decodeBitmap(ImageDecoder.createSource(ctx.contentResolver, uri)) { d, info, _ ->
                val s = maxOf(info.size.width, info.size.height)
                if (s > maxi) { val k = maxi.toFloat() / s; d.setTargetSize((info.size.width * k).toInt().coerceAtLeast(1), (info.size.height * k).toInt().coerceAtLeast(1)) }
                d.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
            }
        } else {
            val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            ctx.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, o) }
            var s = 1; while (maxOf(o.outWidth, o.outHeight) / s > maxi) s *= 2
            ctx.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = s }) }
        }
    } catch (e: Throwable) { null }

    /** le quadrilatere q (haut-gauche, haut-droit, bas-droit, bas-gauche : 8 nombres, en points de b) redresse en w x h */
    fun redresser(b: Bitmap, q: FloatArray, w: Int, h: Int): Bitmap {
        val r = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val m = android.graphics.Matrix()
        m.setPolyToPoly(q, 0, floatArrayOf(0f, 0f, w.toFloat(), 0f, w.toFloat(), h.toFloat(), 0f, h.toFloat()), 0, 4)
        android.graphics.Canvas(r).drawBitmap(b, m, Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG or Paint.DITHER_FLAG))
        return r
    }

    /** la jaquette complete : les faces donnees, sinon GameTDB (ref : jaquette complete, ou seulement l'avant) */
    fun composer(dir: File, id: String, ref: Bitmap?, refComplete: Boolean): Bitmap {
        val r = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)
        val c = android.graphics.Canvas(r)
        val p = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)
        c.drawColor(0xFF1E1E26.toInt())
        if (ref != null) {
            if (refComplete) c.drawBitmap(ref, null, Rect(0, 0, W, H), p)
            else c.drawBitmap(ref, null, Rect(W - W_FACE, 0, W, H), p)
        }
        chargerFace(dir, id, 1)?.let { c.drawBitmap(it, null, Rect(0, 0, W_FACE, H), p) }
        chargerFace(dir, id, 2)?.let { c.drawBitmap(it, null, Rect(W_FACE, 0, W_FACE + W_TR, H), p) }
        chargerFace(dir, id, 0)?.let { c.drawBitmap(it, null, Rect(W - W_FACE, 0, W, H), p) }
        return r
    }
    fun enregistrer(dir: File, id: String, b: Bitmap, f: File) {
        f.parentFile?.mkdirs()
        f.outputStream().use { b.compress(Bitmap.CompressFormat.JPEG, 92, it) }
    }
}

/** l'ecran « Ma jaquette » ; fermer(true) : jaquette changee (la boite la recharge) */
@Composable
fun MaJaquetteEcran(act: StationActivity, id: String, nom: String, fermer: (Boolean) -> Unit) {
    val dir = Hors.dir(act)
    val scope = rememberCoroutineScope()
    var k by remember { mutableIntStateOf(0) }                       // (face en cours : 0 avant, 1 dos, 2 tranche)
    var photo by remember { mutableStateOf<Bitmap?>(null) }
    var coins by remember { mutableStateOf(floatArrayOf()) }          // (8 nombres, en points de la photo)
    var apercu by remember { mutableStateOf<Bitmap?>(null) }
    var faites by remember { mutableStateOf(BooleanArray(3) { MaJaquette.face(dir, id, it).exists() }) }
    var change by remember { mutableStateOf(false) }
    var msg by remember { mutableStateOf("") }
    fun apercuMaj() {
        val b = photo ?: return
        val q = coins
        if (q.size != 8) return
        val w = if (k == 2) 60 else 300; val h = if (k == 2) 806 else 422
        apercu = MaJaquette.redresser(b, q, w, h)
    }
    val choisir = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) scope.launch {
            msg = "Lecture de la photo…"
            val b = withContext(Dispatchers.IO) { MaJaquette.lire(act, uri) }
            msg = if (b == null) "Photo illisible" else ""
            if (b != null) {
                photo = b
                val mx = b.width * 0.08f; val my = b.height * 0.08f
                coins = floatArrayOf(mx, my, b.width - mx, my, b.width - mx, b.height - my, mx, b.height - my)
                apercuMaj()
            }
        }
    }
    Dialog(onDismissRequest = { fermer(change) }, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Column(Modifier.fillMaxSize().background(C.Fond).statusBarsPadding().navigationBarsPadding().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Ma jaquette", color = C.Texte, fontSize = 20.sp)
                    Doux(nom, taille = 12)
                }
                Bouton("Fermer") { fermer(change) }
            }
            Doux("La jaquette de ta boîte, dans ta langue et en haute définition. Pour chaque face : une photo bien éclairée, " +
                 "la jaquette entière dedans (même un peu de travers). Puis place les 4 points sur les 4 coins de la jaquette : " +
                 "l'appli la redresse. Une face pas donnée garde celle de GameTDB.", taille = 12)
            RangeeDePuces {
                for (f in 0 until 3) Puce((if (faites[f]) "✓ " else "") + arrayOf("Avant", "Dos", "Tranche")[f], k == f) {
                    k = f; photo = null; apercu = null; coins = floatArrayOf()
                }
            }
            Bouton(if (photo == null) "Choisir la photo " + arrayOf("de l'avant", "du dos", "de la tranche")[k] else "Changer de photo",
                Modifier.fillMaxWidth(), plein = photo == null) { choisir.launch("image/*") }
            if (msg.isNotEmpty()) Doux(msg, taille = 12, couleur = C.Orange)
            val b = photo
            if (b != null && coins.size == 8) {
                Doux("Fais glisser les 4 points sur les coins de la jaquette (" + (if (k == 2) "la tranche : en haut, le logo GameCube" else "dans le bon sens, le haut en haut") + ").", taille = 11)
                // la photo et les 4 coins (deplacables)
                Box(Modifier.fillMaxWidth().aspectRatio(b.width.toFloat() / b.height).clip(RoundedCornerShape(12.dp))) {
                    Image(b.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.FillBounds)
                    var tenu by remember { mutableIntStateOf(-1) }
                    Canvas(Modifier.fillMaxSize().pointerInput(b) {
                        detectDragGestures(
                            onDragStart = { o ->
                                val sx = size.width / b.width.toFloat(); val sy = size.height / b.height.toFloat()
                                var best = -1; var bd = Float.MAX_VALUE
                                val q = coins
                                for (i in 0 until 4) { val dx = q[2 * i] * sx - o.x; val dy = q[2 * i + 1] * sy - o.y; val d = dx * dx + dy * dy; if (d < bd) { bd = d; best = i } }
                                tenu = best
                            },
                            onDragEnd = { tenu = -1; apercuMaj() },
                            onDragCancel = { tenu = -1 },
                            onDrag = { ch, d ->
                                ch.consume()
                                val i = tenu
                                if (i >= 0) {
                                    val sx = size.width / b.width.toFloat(); val sy = size.height / b.height.toFloat()
                                    val q = coins.copyOf()
                                    q[2 * i] = (q[2 * i] + d.x / sx).coerceIn(0f, b.width.toFloat())
                                    q[2 * i + 1] = (q[2 * i + 1] + d.y / sy).coerceIn(0f, b.height.toFloat())
                                    coins = q
                                }
                            })
                    }) {
                        val sx = size.width / b.width; val sy = size.height / b.height
                        val q = coins
                        val pts = (0 until 4).map { Offset(q[2 * it] * sx, q[2 * it + 1] * sy) }
                        val cadre = Path().apply { moveTo(pts[0].x, pts[0].y); for (i in 1 until 4) lineTo(pts[i].x, pts[i].y); close() }
                        drawPath(cadre, Color(0x3300E5FF))
                        drawPath(cadre, Color(0xFF00E5FF), style = Stroke(2.dp.toPx()))
                        for ((i, p) in pts.withIndex()) {
                            drawCircle(Color(0x66000000), 16.dp.toPx(), p)
                            drawCircle(if (i == tenu) Color(0xFFFF4FD8) else Color.White, 9.dp.toPx(), p)
                            drawCircle(Color(0xFF00E5FF), 9.dp.toPx(), p, style = Stroke(2.dp.toPx()))
                        }
                    }
                }
                apercu?.let { a ->
                    Doux("Résultat :", taille = 11)
                    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                        Image(a.asImageBitmap(), null, Modifier.height(260.dp).aspectRatio(a.width.toFloat() / a.height).clip(RoundedCornerShape(6.dp)))
                    }
                }
                Bouton("Garder cette face", Modifier.fillMaxWidth(), plein = true) {
                    val q = coins; val ph = b; val f = k
                    scope.launch {
                        msg = "Redressement…"
                        withContext(Dispatchers.IO) {
                            val r = MaJaquette.redresser(ph, q, if (f == 2) MaJaquette.W_TR else MaJaquette.W_FACE, MaJaquette.H)
                            MaJaquette.enregistrer(dir, id, r, MaJaquette.face(dir, id, f))
                            val (ref, complete) = JaquettesHQ.reference(dir, id)
                            MaJaquette.enregistrer(dir, id, MaJaquette.composer(dir, id, ref, complete), MaJaquette.fichier(dir, id))
                        }
                        faites = BooleanArray(3) { MaJaquette.face(dir, id, it).exists() }
                        change = true; photo = null; apercu = null; coins = floatArrayOf()
                        msg = arrayOf("Avant", "Dos", "Tranche")[f] + " gardé" + (if (f == 2) "e" else "") + " : il apparaît sur la boîte en 3D."
                        if (f < 2 && !faites[f + 1]) k = f + 1
                    }
                }
            }
            if (faites.any { it }) {
                Spacer(Modifier.height(6.dp))
                Bouton("Revenir à la jaquette de GameTDB", Modifier.fillMaxWidth()) {
                    MaJaquette.effacer(dir, id); faites = BooleanArray(3); change = true; msg = "Jaquette de GameTDB remise."
                }
            }
            Spacer(Modifier.height(30.dp))
        }
    }
}
