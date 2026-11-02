package fr.gcstation.telecommande

// Appli build 224 : un petit moteur 3D pour regarder un niveau pendant qu'on joue (Rayman 3 : les triangles de collision du
// pack de cartes). Dessin logiciel dans un Canvas : sommets projetes en perspective, triangles tries du plus loin au plus
// proche (peintre), couleur selon la hauteur et l'orientation. Un doigt : tourner autour de Rayman ; deux doigts : zoom ;
// double toucher : revenir derriere la camera du jeu.

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp

/** un maillage : 9 flottants par triangle (x y z, Z vers le haut) et une couleur par triangle */
class MaillageNiveau(val t: FloatArray, val couleurs: IntArray) {
    companion object {
        /** fichier du pack : nombre de triangles (u32 gros-boutiste) puis 9 flottants par triangle */
        fun lire(f: java.io.File): MaillageNiveau? = try {
            val b = java.nio.ByteBuffer.wrap(f.readBytes())
            val n = b.int
            if (n <= 0 || n > 200000) null else {
                val t = FloatArray(n * 9) { b.float }
                var z0 = Float.MAX_VALUE; var z1 = -Float.MAX_VALUE
                for (i in 0 until n * 3) { val z = t[i * 3 + 2]; if (z < z0) z0 = z; if (z > z1) z1 = z }
                val c = IntArray(n) { k ->
                    val o = k * 9
                    val ux = t[o + 3] - t[o]; val uy = t[o + 4] - t[o + 1]; val uz = t[o + 5] - t[o + 2]
                    val vx = t[o + 6] - t[o]; val vy = t[o + 7] - t[o + 1]; val vz = t[o + 8] - t[o + 2]
                    val nx = uy * vz - uz * vy; val ny = uz * vx - ux * vz; val nz = ux * vy - uy * vx
                    val l = Math.sqrt((nx * nx + ny * ny + nz * nz).toDouble()).toFloat().coerceAtLeast(1e-6f)
                    val sol = Math.abs(nz) / l
                    val h = (((t[o + 2] + t[o + 5] + t[o + 8]) / 3 - z0) / (z1 - z0 + 1e-3f)).coerceIn(0f, 1f)
                    val lum = 0.45f + 0.35f * sol + 0.2f * (nx / l * 0.5f + 0.5f)
                    val r = ((70 + 120 * h) * lum).toInt().coerceIn(0, 255); val g = ((80 + 100 * h) * lum).toInt().coerceIn(0, 255)
                    val bl = ((150 + 90 * (1 - h)) * lum).toInt().coerceIn(0, 255)
                    (0xFF shl 24) or (r shl 16) or (g shl 8) or bl
                }
                MaillageNiveau(t, c)
            }
        } catch (e: Exception) { null }
    }
}

/** ce que montre la vue : le maillage, la cible (Rayman), la camera du jeu (pour le point de vue par defaut), des reperes */
class Vue3D(val m: MaillageNiveau, val cible: FloatArray, val camera: FloatArray?, val chemin: List<FloatArray> = emptyList())

@Composable
fun DessinVue3D(v: Vue3D) {
    var lacet by remember { mutableFloatStateOf(Float.NaN) }   // (NaN : derriere la camera du jeu)
    var tangage by remember { mutableFloatStateOf(0.55f) }
    var dist by remember { mutableFloatStateOf(70f) }
    var libre by remember { mutableStateOf(false) }
    androidx.compose.foundation.Canvas(Modifier.fillMaxWidth().aspectRatio(1.1f)
        .pointerInput(Unit) {
            detectTransformGestures { _, pan, zoom, _ ->
                if (lacet.isNaN()) lacet = 0f
                libre = true
                lacet -= pan.x * 0.008f
                tangage = (tangage + pan.y * 0.006f).coerceIn(0.05f, 1.5f)
                dist = (dist / zoom).coerceIn(8f, 600f)
            }
        }
        .pointerInput(Unit) { detectTapGestures(onDoubleTap = { libre = false; lacet = Float.NaN; tangage = 0.55f; dist = 70f }) }) {
        val cx = v.cible[0]; val cy = v.cible[1]; val cz = v.cible[2]
        // lacet par defaut : celui de la camera du jeu (de la camera vers Rayman)
        val a = if (!libre || lacet.isNaN()) {
            val c = v.camera
            if (c != null) Math.atan2((cy - c[1]).toDouble(), (cx - c[0]).toDouble()).toFloat() else 0f
        } else lacet
        // position de l'oeil : derriere la cible, en hauteur
        val ex = cx - Math.cos(a.toDouble()).toFloat() * Math.cos(tangage.toDouble()).toFloat() * dist
        val ey = cy - Math.sin(a.toDouble()).toFloat() * Math.cos(tangage.toDouble()).toFloat() * dist
        val ez = cz + Math.sin(tangage.toDouble()).toFloat() * dist
        // repere de la camera : avant f, droite r, haut u
        var fx = cx - ex; var fy = cy - ey; var fz = cz - ez
        val fl = Math.sqrt((fx * fx + fy * fy + fz * fz).toDouble()).toFloat(); fx /= fl; fy /= fl; fz /= fl
        var rx = fy; var ry = -fx; val rl = Math.sqrt((rx * rx + ry * ry).toDouble()).toFloat().coerceAtLeast(1e-6f); rx /= rl; ry /= rl
        val ux = ry * fz; val uy = -rx * fz; val uz = rx * fy - ry * fx
        val foc = size.width * 0.9f; val mx = size.width / 2; val my = size.height / 2
        drawRect(Color(0xFF0E0A1E))
        val t = v.m.t; val n = t.size / 9
        val px = FloatArray(n * 3); val py = FloatArray(n * 3); val pz = FloatArray(n * 3)
        for (i in 0 until n * 3) {
            val dx = t[i * 3] - ex; val dy = t[i * 3 + 1] - ey; val dz = t[i * 3 + 2] - ez
            val z = dx * fx + dy * fy + dz * fz
            pz[i] = z
            if (z > 0.5f) { px[i] = mx + (dx * rx + dy * ry) / z * foc; py[i] = my - (dx * ux + dy * uy + dz * uz) / z * foc }
        }
        val ordre = ArrayList<Int>(n)
        for (k in 0 until n) {
            val i = k * 3
            if (pz[i] <= 0.5f || pz[i + 1] <= 0.5f || pz[i + 2] <= 0.5f) continue
            if (pz[i] > 900f) continue
            ordre.add(k)
        }
        ordre.sortByDescending { pz[it * 3] + pz[it * 3 + 1] + pz[it * 3 + 2] }
        val path = Path()
        for (k in ordre) {
            val i = k * 3
            path.reset()
            path.moveTo(px[i], py[i]); path.lineTo(px[i + 1], py[i + 1]); path.lineTo(px[i + 2], py[i + 2]); path.close()
            val z = (pz[i] + pz[i + 1] + pz[i + 2]) / 3
            val brume = (z / 500f).coerceIn(0f, 0.7f)
            val c = Color(v.m.couleurs[k])
            drawPath(path, Color(c.red * (1 - brume) + 0.055f * brume, c.green * (1 - brume) + 0.04f * brume, c.blue * (1 - brume) + 0.12f * brume, 1f))
        }
        fun proj(x: Float, y: Float, z: Float): Offset? {
            val dx = x - ex; val dy = y - ey; val dz = z - ez
            val zz = dx * fx + dy * fy + dz * fz
            return if (zz <= 0.5f) null else Offset(mx + (dx * rx + dy * ry) / zz * foc, my - (dx * ux + dy * uy + dz * uz) / zz * foc)
        }
        var av: Offset? = null
        for (p in v.chemin) { val q = proj(p[0], p[1], p[2]); if (av != null && q != null) drawLine(Color(0xCCFFC94A), av, q, 2.dp.toPx()); av = q }
        proj(cx, cy, cz + 1.5f)?.let { drawCircle(Color(0xFFFF3B3B), 7.dp.toPx(), it); drawCircle(Color.White, 7.dp.toPx(), it, style = androidx.compose.ui.graphics.drawscope.Stroke(2.dp.toPx())) }
    }
}
