package fr.gcstation.telecommande

// Appli : le mur de jaquettes. Les jaquettes avant de tous les jeux affiches, en grand (3 par ligne), qui
// s'inclinent quand on bouge le telephone (capteur de gravite), avec un reflet qui glisse dessus. Toucher : la fiche
// du jeu. Jaquettes HD de GameTDB (gardees sur le telephone), sinon celles de la console.
// « En fond d'écran » : choisir le fond anime de la collection (FondAnime.kt).

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.app.WallpaperManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

object MurImages {
    val mem = object : LruCache<String, ImageBitmap>(48) { }
    /** jaquette avant en grand (environ 500 px de large) : HD de GameTDB, sinon celle de la console */
    suspend fun charger(dir: File, g: Jeu): ImageBitmap? {
        mem.get(g.id)?.let { return it }
        val b: Bitmap? = withContext(Dispatchers.IO) {
            val f = JaquettesHQ.fichier(dir, "coverHQ", g.id)
            if (f != null) {
                val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeFile(f.path, o)
                var e = 1
                while (o.outWidth / (e * 2) >= 480) e *= 2
                BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inSampleSize = e })
            } else null
        }
        val im = b?.asImageBitmap() ?: Jaquettes.charger(dir, g.id, g.i)
        im?.let { mem.put(g.id, it) }
        return im
    }
}

/** l'inclinaison du telephone (-1 a 1 sur x et y), lissee */
@Composable
private fun inclinaison(): Pair<Float, Float> {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    var ix by remember { mutableFloatStateOf(0f) }
    var iy by remember { mutableFloatStateOf(0f) }
    DisposableEffect(Unit) {
        val sm = ctx.getSystemService(Context.SENSOR_SERVICE) as SensorManager
        val s = sm.getDefaultSensor(Sensor.TYPE_GRAVITY) ?: sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        val l = object : SensorEventListener {
            override fun onSensorChanged(e: SensorEvent) {
                val gx = (e.values[0] / 9.81f).coerceIn(-1f, 1f); val gy = ((e.values[1] - 6.5f) / 6f).coerceIn(-1f, 1f)   // (tenu a ~40 degres : au repos)
                ix += (gx - ix) * 0.12f; iy += (gy - iy) * 0.12f
            }
            override fun onAccuracyChanged(s: Sensor?, a: Int) { }
        }
        if (s != null) sm.registerListener(l, s, SensorManager.SENSOR_DELAY_GAME)
        onDispose { sm.unregisterListener(l) }
    }
    return ix to iy
}

@Composable
fun MurEcran(act: StationActivity, jeux: List<Jeu>, fiche: (Jeu) -> Unit, fermer: () -> Unit) {
    val (ix, iy) = inclinaison()
    Dialog(onDismissRequest = fermer, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color(0xFF16072C), Color(0xFF05020C))))) {
            LazyVerticalGrid(GridCells.Fixed(3), Modifier.fillMaxSize().padding(horizontal = 10.dp),
                contentPadding = PaddingValues(top = 110.dp, bottom = 40.dp), horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                items(jeux, key = { it.id + "#" + it.i }) { g -> Jaquette(act, g, ix, iy) { fiche(g) } }
            }
            Column(Modifier.fillMaxWidth().background(Brush.verticalGradient(listOf(Color(0xF016072C), Color(0x0016072C)))).statusBarsPadding().padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("Mur de jaquettes", color = C.Texte, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                        Doux(jeux.size.toString() + " jeu" + (if (jeux.size > 1) "x" else "") + " · incline le téléphone · toucher : la fiche", taille = 12, lignes = 1)
                    }
                    Bouton("En fond d'écran", hauteur = 36.dp) { fondEcran(act) }
                    Spacer(Modifier.size(8.dp))
                    Box(Modifier.size(44.dp).verre(CircleShape).appui(fermer), contentAlignment = Alignment.Center) { Icon(Ic.Fermer, null, tint = C.Texte) }
                }
            }
        }
    }
}

/** ouvre le choix du fond d'ecran anime « GC Station : ma collection » */
fun fondEcran(c: Context) {
    val i = Intent(WallpaperManager.ACTION_CHANGE_LIVE_WALLPAPER)
        .putExtra(WallpaperManager.EXTRA_LIVE_WALLPAPER_COMPONENT, ComponentName(c, FondAnime::class.java))
    try { c.startActivity(i) } catch (e: Exception) {
        try { c.startActivity(Intent(WallpaperManager.ACTION_LIVE_WALLPAPER_CHOOSER)) } catch (e2: Exception) { }
    }
}

@Composable
private fun Jaquette(act: StationActivity, g: Jeu, ix: Float, iy: Float, onClick: () -> Unit) {
    var img by remember(g.id) { mutableStateOf(MurImages.mem.get(g.id)) }
    LaunchedEffect(g.id) { if (img == null) img = MurImages.charger(Hors.dir(act), g) }
    val sh = RoundedCornerShape(6.dp)
    Column {
        Box(Modifier.fillMaxWidth().aspectRatio(0.7f)
            .graphicsLayer { rotationY = ix * 14f; rotationX = iy * 10f; cameraDistance = 10f * density; shadowElevation = 14f; shape = sh; clip = true }
            .background(Color(0xFF2A1A44)).appui(onClick)
            .drawWithContent {
                drawContent()
                // (reflet qui glisse selon l'inclinaison)
                val cx = size.width * (0.5f - ix * 0.9f); val cy = size.height * (0.5f + iy * 0.9f)
                drawRect(Brush.linearGradient(listOf(Color.White.copy(alpha = 0f), Color.White.copy(alpha = 0.22f), Color.White.copy(alpha = 0f)),
                    start = Offset(cx - size.width * 0.6f, cy - size.height * 0.6f), end = Offset(cx + size.width * 0.6f, cy + size.height * 0.6f)))
            }, contentAlignment = Alignment.Center) {
            val im = img
            if (im != null) Image(im, g.nom, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            else Text(g.nom, color = C.Doux, fontSize = 11.sp, maxLines = 4, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(6.dp))
        }
        Spacer(Modifier.height(4.dp))
        Text(g.nom, color = C.Doux, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
