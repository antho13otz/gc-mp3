package fr.gcstation.telecommande

// Appli build 456 : « Rapport du direct » plus clair. Au lieu du long tableau : trois petits graphiques (temps pris au jeu
// par Pulse, Ko par image, images par seconde du telephone) avec leur moyenne ; « Garder » enregistre le rapport sur le
// telephone (10 au plus) et « Comparer deux rapports » montre le gain d'un coup d'oeil. L'export texte d'avant reste
// (« Partager le texte »).

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** un rapport : heure, mode du direct, build de l'appli, et les series (une valeur par seconde) */
class RapportDirect(val t: Long, val mode: String, val build: Int, val ms: FloatArray, val ko: FloatArray, val ips: FloatArray) {
    fun moy(a: FloatArray) = if (a.isEmpty()) Float.NaN else a.average().toFloat()
    val titre: String get() = java.text.SimpleDateFormat("d MMM HH:mm", java.util.Locale.FRANCE).format(java.util.Date(t)) + " · " + mode
    fun json(): org.json.JSONObject = org.json.JSONObject().put("t", t).put("mode", mode).put("build", build)
        .put("ms", org.json.JSONArray(ms.map { it.toDouble() })).put("ko", org.json.JSONArray(ko.map { it.toDouble() })).put("ips", org.json.JSONArray(ips.map { it.toDouble() }))
    companion object {
        private fun fa(a: org.json.JSONArray?) = FloatArray(a?.length() ?: 0) { a!!.getDouble(it).toFloat() }
        fun de(j: org.json.JSONObject) = RapportDirect(j.getLong("t"), j.optString("mode"), j.optInt("build"), fa(j.optJSONArray("ms")), fa(j.optJSONArray("ko")), fa(j.optJSONArray("ips")))
        /** le rapport de maintenant (mesures des 10 dernieres minutes) */
        fun maintenant(): RapportDirect {
            val l = synchronized(MemoireJeu.mesures) { MemoireJeu.mesures.toList() }
            val v = synchronized(FluiditeR3.mesures) { FluiditeR3.mesures.toList() }
            return RapportDirect(System.currentTimeMillis(), Direct3D.nomMode(Direct3D.mode), BuildConfig.VERSION_CODE,
                FloatArray(l.size) { l[it].second[0] / 1000f }, FloatArray(l.size) { l[it].second[3] / 1024f }, FloatArray(v.size) { v[it].second })
        }
        private fun dossier(ctx: android.content.Context) = java.io.File(ctx.filesDir, "rapports-direct").also { it.mkdirs() }
        fun garder(ctx: android.content.Context, r: RapportDirect) {
            java.io.File(dossier(ctx), "${r.t}.json").writeText(r.json().toString())
            dossier(ctx).listFiles()?.sortedByDescending { it.name }?.drop(10)?.forEach { it.delete() }
        }
        fun gardes(ctx: android.content.Context): List<RapportDirect> = dossier(ctx).listFiles()?.sortedByDescending { it.name }
            ?.mapNotNull { try { de(org.json.JSONObject(it.readText())) } catch (e: Exception) { null } } ?: emptyList()
    }
}

private val COUL_A = Color(0xFF8FA3C0); private val COUL_B = Color(0xFFFF5FA2)

/** petit graphique : une ou deux series, leur moyenne en pointilles */
@Composable
private fun Graphe(nom: String, unite: String, a: FloatArray, b: FloatArray? = null, plusBasMieux: Boolean = true) {
    val ma = if (a.isEmpty()) Float.NaN else a.average().toFloat(); val mb = b?.let { if (it.isEmpty()) Float.NaN else it.average().toFloat() }
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        val txt = StringBuilder(nom).append(" : ")
        if (mb == null) txt.append(fmt(ma)).append(' ').append(unite).append(" en moyenne")
        else {
            txt.append(fmt(ma)).append(" → ").append(fmt(mb)).append(' ').append(unite)
            if (ma.isFinite() && mb.isFinite() && ma != 0f) { val g = (mb - ma) / ma * 100f
                txt.append(String.format(java.util.Locale.FRANCE, " (%+.0f %%%s)", g, if (Math.abs(g) < 3f) "" else if ((g < 0) == plusBasMieux) ", mieux" else ", moins bien")) }
        }
        Text(txt.toString(), color = Color.White, fontSize = 12.sp)
        val tout = a + (b ?: FloatArray(0))
        val max = (tout.filter { it.isFinite() }.maxOrNull() ?: 1f).coerceAtLeast(0.001f) * 1.1f
        Canvas(Modifier.fillMaxWidth().height(70.dp).background(Color(0x22FFFFFF), RoundedCornerShape(8.dp))) {
            fun serie(s: FloatArray, c: Color, m: Float) {
                if (s.size >= 2) { val p = Path()
                    for ((i, v) in s.withIndex()) { val x = size.width * i / (s.size - 1); val y = size.height * (1f - (if (v.isFinite()) v else 0f) / max)
                        if (i == 0) p.moveTo(x, y) else p.lineTo(x, y) }
                    drawPath(p, c, style = Stroke(width = 2.dp.toPx())) }
                if (m.isFinite()) { val y = size.height * (1f - m / max)
                    drawLine(c.copy(alpha = 0.7f), Offset(0f, y), Offset(size.width, y), strokeWidth = 1.dp.toPx(),
                        pathEffect = androidx.compose.ui.graphics.PathEffect.dashPathEffect(floatArrayOf(8f, 6f))) }
            }
            serie(a, if (b == null) COUL_B else COUL_A, ma)
            if (b != null) serie(b, COUL_B, mb ?: Float.NaN)
        }
    }
}
private fun fmt(v: Float) = if (v.isFinite()) String.format(java.util.Locale.FRANCE, if (v < 10f) "%.2f" else "%.1f", v) else "-"

/** la fenetre du rapport (et de la comparaison) */
@Composable
fun FenetreRapportDirect(onFermer: () -> Unit) {
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val r = remember { RapportDirect.maintenant() }
    var comparer by remember { mutableStateOf(false) }
    var choixA by remember { mutableStateOf<RapportDirect?>(null) }
    var choixB by remember { mutableStateOf<RapportDirect?>(null) }
    var garde by remember { mutableStateOf(false) }
    androidx.compose.ui.window.Dialog(onDismissRequest = onFermer) {
        Column(Modifier.fillMaxWidth().background(Color(0xFF15122A), RoundedCornerShape(16.dp)).padding(14.dp).verticalScroll(rememberScrollState())) {
            if (!comparer) {
                Text("Rapport du direct", color = C.Cyan, fontSize = 15.sp)
                Text("Mode ${r.mode} · appli ${r.build} · ${r.ms.size} s de mesures", color = C.Pale, fontSize = 11.sp)
                if (r.ms.isEmpty()) Text("Aucune mesure : console en v370 ou plus, vue 3D en direct ouverte.", color = C.Pale, fontSize = 12.sp)
                Graphe("Temps pris au jeu", "ms par image", r.ms)
                Graphe("Données", "Ko par image", r.ko)
                Graphe("Téléphone", "images par seconde", r.ips, plusBasMieux = false)
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    TextButton(onClick = { RapportDirect.garder(ctx, r); garde = true }, enabled = !garde && r.ms.isNotEmpty()) {
                        Text(if (garde) "Gardé" else "Garder", color = C.Rose, fontSize = 12.sp) }
                    TextButton(onClick = { comparer = true }) { Text("Comparer deux rapports", color = C.Rose, fontSize = 12.sp) }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    TextButton(onClick = { rapportDirect(ctx) }) { Text("Partager le texte", color = C.Rose, fontSize = 12.sp) }
                    TextButton(onClick = onFermer) { Text("Fermer", color = C.Pale, fontSize = 12.sp) }
                }
            } else {
                Text("Comparer deux rapports", color = C.Cyan, fontSize = 15.sp)
                val l = remember(garde) { listOf(r) + RapportDirect.gardes(ctx).filter { it.t != r.t } }
                Text("Avant (gris) puis après (rose) : touche deux rapports. « Maintenant » : celui de cette fenêtre.", color = C.Pale, fontSize = 11.sp)
                for (x in l) {
                    val nom = (if (x === r) "Maintenant · " + x.mode else x.titre) + String.format(java.util.Locale.FRANCE, " · %s ms, %s Ko", fmt(x.moy(x.ms)), fmt(x.moy(x.ko)))
                    val sel = if (x === choixA) "① " else if (x === choixB) "② " else ""
                    TextButton(onClick = {
                        if (choixA == null || (choixA != null && choixB != null)) { choixA = x; choixB = null } else if (x !== choixA) choixB = x
                    }) { Text(sel + nom, color = if (sel.isEmpty()) C.Pale else C.Rose, fontSize = 12.sp) }
                }
                val a = choixA; val b = choixB
                if (a != null && b != null) {
                    Text("${a.titre}  →  ${b.titre}", color = Color.White, fontSize = 11.sp)
                    Graphe("Temps pris au jeu", "ms", a.ms, b.ms)
                    Graphe("Données", "Ko par image", a.ko, b.ko)
                    Graphe("Téléphone", "images/s", a.ips, b.ips, plusBasMieux = false)
                }
                Row { TextButton(onClick = { comparer = false }) { Text("Retour", color = C.Rose, fontSize = 12.sp) }
                    TextButton(onClick = onFermer) { Text("Fermer", color = C.Pale, fontSize = 12.sp) } }
            }
        }
    }
}
