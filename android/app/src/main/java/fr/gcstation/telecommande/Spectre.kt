package fr.gcstation.telecommande

// Appli (GC Station v309) : le spectre de la console, comme sur la tele (style LED : cases empilees, crete, reflet ;
// rose -> violet -> bleu de gauche a droite, plus clair en haut). Les 55 barres viennent de la console (/api/viz,
// 0..255), environ 20 fois par seconde ; l'appli les lisse et redessine a chaque image de l'ecran (120 Hz).
// Console trop ancienne (pas de /api/viz) : rien n'est affiche.

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

private const val NB = 55

@Composable
fun SpectreConsole(actif: Boolean, modifier: Modifier = Modifier) {
    val cible = remember { FloatArray(NB) }
    val aff = remember { FloatArray(NB) }
    val crete = remember { FloatArray(NB) }
    var image by remember { mutableLongStateOf(0L) }
    var present by remember { mutableStateOf(true) }
    // (les barres de la console, tant que la musique joue et que l'ecran est la)
    LaunchedEffect(actif) {
        if (!actif) { cible.fill(0f); return@LaunchedEffect }
        var rates = 0
        while (isActive && rates < 6) {
            val b = withContext(Dispatchers.IO) { Api.getBytes("/api/viz", 900) }
            if (b != null && b.size >= NB) { for (i in 0 until NB) cible[i] = (b[i].toInt() and 255) / 255f; rates = 0; present = true }
            else rates++
            delay(45)
        }
        if (rates >= 6) { present = false; cible.fill(0f) }
    }
    // (redessin a chaque image tant que quelque chose bouge)
    LaunchedEffect(actif) {
        while (isActive) {
            withFrameNanos { image = it }
            if (!actif && aff.all { it < 0.005f } && crete.all { it < 0.005f }) break
        }
    }
    if (!present) return
    Canvas(modifier) {
        @Suppress("UNUSED_VARIABLE") val t = image
        for (i in 0 until NB) {
            val c = cible[i]
            aff[i] = if (c > aff[i]) aff[i] + (c - aff[i]) * 0.55f else aff[i] * 0.9f + c * 0.1f
            crete[i] = maxOf(aff[i], crete[i] - 0.008f)
        }
        val haut = size.height * 0.74f                       // (barres ; dessous : le reflet)
        val pas = 3.dp.toPx()
        val ep = 2.dp.toPx()
        val cases = (haut / pas).toInt().coerceAtLeast(4)
        val bw = size.width / NB
        val trou = minOf(1.6.dp.toPx(), bw * 0.3f)
        val rose = Color(0xFFFF4FD8); val violet = Color(0xFF9C3CFF); val bleu = Color(0xFF4682FF); val clair = Color(0xFFFFC4FF)
        for (i in 0 until NB) {
            val tc = i / (NB - 1f)
            val cb = if (tc < 0.5f) lerp(rose, violet, tc * 2f) else lerp(violet, bleu, (tc - 0.5f) * 2f)
            val x = i * bw
            val n = (1 + (aff[i] * cases).toInt()).coerceAtMost(cases)
            for (j in 0 until n) {
                drawRect(lerp(cb, clair, j / (cases + 2f)), Offset(x, haut - pas * (j + 1)), Size(bw - trou, ep))
            }
            val pj = (crete[i] * cases).toInt()
            if (pj >= n && pj < cases) drawRect(Color(0xDDFFE6FF), Offset(x, haut - pas * (pj + 1)), Size(bw - trou, ep))
            for (j in 0 until minOf(n, 7)) {                  // (reflet)
                val y = haut + 2.dp.toPx() + pas * j
                if (y + ep > size.height) break
                drawRect(cb.copy(alpha = (80 - j * 11) / 255f), Offset(x, y), Size(bw - trou, ep))
            }
        }
    }
}
