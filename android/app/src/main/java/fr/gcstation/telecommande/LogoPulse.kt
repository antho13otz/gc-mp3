package fr.gcstation.telecommande

// Build 513 : le logo de Pulse (meme dessin que la console, source/pulse_logo.c), sorti de PulseLancement.kt (appli privee)
// pour que la version publique ait le vrai logo anime. Un dessin seulement : rien de prive ici.

import android.graphics.BitmapFactory
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.sin
import kotlin.math.sqrt

// ---------------------------------------------------------------- le logo (meme dessin que la console)
private val STOPS = arrayOf(intArrayOf(255, 70, 170), intArrayOf(160, 80, 255), intArrayOf(70, 110, 255), intArrayOf(40, 210, 255))
private fun grad(t0: Float, a: Float, dim: Float = 1f): Color {
    val t = t0.coerceIn(0f, 1f); val s = t * 3; val i = minOf(s.toInt(), 2); val f = s - i
    fun c(k: Int) = (STOPS[i][k] + (STOPS[i + 1][k] - STOPS[i][k]) * f) * dim / 255f
    return Color(c(0).coerceIn(0f, 1f), c(1).coerceIn(0f, 1f), c(2).coerceIn(0f, 1f), a.coerceIn(0f, 1f))
}
private fun ouverture(y: Float, ly: Float, r: Float): Float {
    val dy = abs(y - ly); val gh = r * 0.073f; val fd = r * 0.173f
    if (dy <= gh) return 0f
    val f = (dy - gh) / fd
    return if (f >= 1f) 1f else f * sqrt(f)
}
private fun DrawScope.hexOuvert(cx: Float, cy: Float, r: Float, hr: Float, w: Float, a: Float, dim: Float, ly: Float) {
    val sub = 8; val ww = maxOf(w, 1f)
    fun dcol(x: Float, y: Float, al: Float) = grad(((x - cx) + (y - cy)) / (2.5f * r) + 0.5f, al, dim)
    for (i in 0 until 6) {
        val a0 = (-Math.PI / 2 + i * Math.PI / 3).toFloat(); val a1 = a0 + (Math.PI / 3).toFloat()
        val x0 = cx + hr * cos(a0); val y0 = cy + hr * sin(a0); val x1 = cx + hr * cos(a1); val y1 = cy + hr * sin(a1)
        for (k in 0 until sub) {
            val u0 = k.toFloat() / sub; val u1 = (k + 1).toFloat() / sub
            val px = x0 + (x1 - x0) * u0; val py = y0 + (y1 - y0) * u0; val qx = x0 + (x1 - x0) * u1; val qy = y0 + (y1 - y0) * u1
            val o0 = ouverture(py, ly, r); val o1 = ouverture(qy, ly, r)
            if (o0 <= 0f && o1 <= 0f) continue
            drawLine(Brush.linearGradient(listOf(dcol(px, py, a * o0), dcol(qx, qy, a * o1)), Offset(px, py), Offset(qx, qy)),
                Offset(px, py), Offset(qx, qy), ww, StrokeCap.Round)
        }
    }
}
private fun DrawScope.cube(cx: Float, cy: Float, s: Float, g: Float) {
    val k = 0.866f
    fun face(c: Color, vararg p: Float) { val pa = Path(); pa.moveTo(p[0], p[1]); var i = 2; while (i < p.size) { pa.lineTo(p[i], p[i + 1]); i += 2 }; pa.close(); drawPath(pa, c) }
    face(Color(255, 150, 220), cx, cy - s - g, cx + s * k, cy - s * 0.5f - g, cx, cy - g, cx - s * k, cy - s * 0.5f - g)
    val lx = -g * 0.87f; val ry = g * 0.5f
    face(Color(150, 90, 255), cx - s * k + lx, cy - s * 0.5f + ry, cx + lx, cy + ry, cx + lx, cy + s + ry, cx - s * k + lx, cy + s * 0.5f + ry)
    face(Color(60, 100, 230), cx - lx, cy + ry, cx + s * k - lx, cy - s * 0.5f + ry, cx + s * k - lx, cy + s * 0.5f + ry, cx - lx, cy + s + ry)
}
private val BAT = arrayOf(floatArrayOf(-1.42f, 0f), floatArrayOf(-0.5f, 0f), floatArrayOf(-0.44f, -0.08f), floatArrayOf(-0.38f, 0f), floatArrayOf(-0.2f, 0f),
    floatArrayOf(-0.15f, 0.08f), floatArrayOf(-0.07f, -0.66f), floatArrayOf(0.01f, 0.3f), floatArrayOf(0.06f, 0f), floatArrayOf(0.18f, 0f),
    floatArrayOf(0.26f, -0.14f), floatArrayOf(0.34f, 0f), floatArrayOf(1.42f, 0f))
private fun DrawScope.battement(cx: Float, ly: Float, r: Float, lueur: Float) {
    val x0 = cx - 1.42f * r; val x1 = cx + 1.42f * r
    for (passe in 0..1) {
        val w = maxOf(if (passe == 1) r * 0.047f else r * 0.17f, 1f)
        for (i in 0 until 12) {
            val ax = cx + BAT[i][0] * (if (i == 0) r else r * 1.05f); val ay = ly + BAT[i][1] * r * 0.55f
            val bx = cx + BAT[i + 1][0] * (if (i == 11) r else r * 1.05f); val by = ly + BAT[i + 1][1] * r * 0.55f
            val n = minOf(((abs(bx - ax) + abs(by - ay)) / (r * 0.08f)).toInt() + 1, 16)
            for (k in 0 until n) {
                val u0 = k.toFloat() / n; val u1 = (k + 1).toFloat() / n
                val p = arrayOf(floatArrayOf(ax + (bx - ax) * u0, ay + (by - ay) * u0), floatArrayOf(ax + (bx - ax) * u1, ay + (by - ay) * u1))
                val c = Array(2) { j ->
                    val dx = abs(p[j][0] - cx)
                    val fa = ((1.42f * r - dx) / (0.5f * r)).coerceIn(0f, 1f)               // (s'efface vers les bords)
                    val g2 = grad((p[j][0] - x0) / (x1 - x0), 1f)
                    if (passe == 1) {
                        val wv = ((dx - 0.48f * r) / (0.43f * r)).coerceIn(0f, 1f)          // (blanc au centre)
                        Color(1f + (g2.red - 1f) * wv, 1f + (g2.green - 1f) * wv, 1f + (g2.blue - 1f) * wv, fa)
                    } else g2.copy(alpha = (60f + 70f * lueur) / 255f * fa)
                }
                drawLine(Brush.linearGradient(listOf(c[0], c[1]), Offset(p[0][0], p[0][1]), Offset(p[1][0], p[1][1])),
                    Offset(p[0][0], p[0][1]), Offset(p[1][0], p[1][1]), w, StrokeCap.Round)
            }
        }
    }
}
/** le logo de Pulse centre en (cx, cy), r : rayon de l'hexagone, t : temps (s) */
internal fun DrawScope.logoPulse(cx: Float, cy: Float, r: Float, t: Float) {
    val ph = (t % 1.1f) / 1.1f                              // (un battement toutes les 1,1 s)
    val bat = exp(-ph * 9f)                                 // (s'allume d'un coup, puis revient)
    val ly = cy + r * 0.013f
    hexOuvert(cx, cy, r, r * 1.29f, r * 0.017f, 90 / 255f, 0.5f, ly)
    hexOuvert(cx, cy, r, r * 1.15f, r * 0.027f, 150 / 255f, 0.6f, ly)
    hexOuvert(cx, cy, r, r * (1.05f + 0.45f * ph), r * 0.04f, 150 / 255f * (1 - ph), 1f, ly)
    drawCircle(Brush.radialGradient(listOf(Color(255, 120, 220).copy(alpha = (110 + 70 * bat) / 255f), Color(255, 120, 220, 0)), Offset(cx, cy), r * 0.62f), r * 0.62f, Offset(cx, cy))
    cube(cx, cy, r * 0.552f * (1f + 0.03f * bat), r * 0.067f)
    battement(cx, ly, r, bat)
    hexOuvert(cx, cy, r, r, r * 0.22f, 35 / 255f, 1f, ly)
    hexOuvert(cx, cy, r, r, r * 0.13f, 80 / 255f, 1f, ly)
    hexOuvert(cx, cy, r, r, r * 0.073f, 1f, 1f, ly)
}

/** build 503 (build 513 : aussi dans la version publique) : le logo de Pulse anime, a la taille donnee (carte Pulse de l'onglet Jeux) */
@Composable
fun LogoPulseAnime(m: Modifier) {
    var t by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(Unit) {
        val t0 = System.nanoTime()
        while (true) withFrameNanos { n -> t = (n - t0) / 1e9f }
    }
    Canvas(m) { logoPulse(size.width / 2, size.height / 2, size.minDimension * 0.33f, t) }
}

