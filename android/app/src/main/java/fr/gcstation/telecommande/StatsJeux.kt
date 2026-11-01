package fr.gcstation.telecommande

// Appli build 185 : statistiques de jeu. Deux sources :
//  - la console (/api/games : pour chaque jeu, lancements, minutes de jeu mesurees par Pulse, dernier lancement) : les
//    totaux depuis toujours ;
//  - le telephone (Live : chaque partie, du lancement au retour a GC Station, quand l'appli tournait) : le temps par
//    semaine et par jour, qui commence donc avec cette version.

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray

private class JeuStat(val nom: String, val id: String, val lancements: Int, val minutes: Int, val dernier: Long)
private class Session(val id: String, val nom: String, val debut: Long, val sec: Long)

private fun duree(min: Long): String = if (min >= 60) String.format(java.util.Locale.FRANCE, "%d h %02d", min / 60, min % 60) else "$min min"

@Composable
fun EcranStatsJeux() {
    val ctx = LocalContext.current
    var jeux by remember { mutableStateOf<List<JeuStat>?>(null) }
    var erreur by remember { mutableStateOf(false) }
    var sessions by remember { mutableStateOf<List<Session>>(emptyList()) }
    LaunchedEffect(Unit) {
        sessions = withContext(Dispatchers.IO) {
            try {
                val a = JSONArray(ctx.getSharedPreferences("gcs", 0).getString("sessions_jeu", "[]"))
                (0 until a.length()).mapNotNull { a.optJSONObject(it) }.map { Session(it.optString("id"), it.optString("nom"), it.optLong("t"), it.optLong("s")) }
            } catch (e: Exception) { emptyList() }
        }
        val r = withContext(Dispatchers.IO) { Api.get("/api/games", 9000) }
        try {
            val a = JSONArray(r ?: "")
            jeux = (0 until a.length()).mapNotNull { a.optJSONArray(it) }.map { g ->
                var t = g.optLong(5)
                if (t in 1 until 946684800L) t += 946684800L                   // (secondes depuis 2000 : horloge de la GameCube)
                JeuStat(g.optString(0), g.optString(1), g.optInt(2), g.optInt(3), t)
            }
        } catch (e: Exception) { erreur = true }
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        val js = jeux
        if (js == null && !erreur) Doux("Lecture de tes jeux sur la console…")
        if (erreur) Doux("La console ne répond pas : seulement les parties notées par le téléphone.", couleur = C.Orange)
        // les grands chiffres (console)
        if (js != null) {
            val joues = js.filter { it.lancements > 0 }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((v, n) in listOf(duree(js.sumOf { it.minutes.toLong() }) to "de jeu", joues.size.toString() to "jeux lancés", js.sumOf { it.lancements }.toString() to "lancements"))
                    Column(Modifier.weight(1f).clip(RoundedCornerShape(16.dp)).background(Color(0x22FFFFFF)).padding(12.dp)) {
                        Text(v, color = C.Texte, fontSize = 20.sp, fontWeight = FontWeight.Black, maxLines = 1)
                        Doux(n, taille = 11, lignes = 1)
                    }
            }
        }
        // par semaine (telephone)
        val maintenant = System.currentTimeMillis()
        val cal = java.util.Calendar.getInstance(java.util.Locale.FRANCE).apply { firstDayOfWeek = java.util.Calendar.MONDAY }
        fun debutSemaine(t: Long): Long { cal.timeInMillis = t; cal.set(java.util.Calendar.DAY_OF_WEEK, java.util.Calendar.MONDAY)
            cal.set(java.util.Calendar.HOUR_OF_DAY, 0); cal.set(java.util.Calendar.MINUTE, 0); cal.set(java.util.Calendar.SECOND, 0); cal.set(java.util.Calendar.MILLISECOND, 0)
            if (cal.timeInMillis > t) cal.add(java.util.Calendar.DAY_OF_YEAR, -7); return cal.timeInMillis }
        val cette = debutSemaine(maintenant)
        val semaines = (7 downTo 0).map { k -> cette - k * 7L * 86400000L }
        val parSemaine = semaines.map { d -> sessions.filter { debutSemaine(it.debut) == d }.sumOf { it.sec } / 60 }
        val df = java.text.SimpleDateFormat("d MMM", java.util.Locale.FRANCE)
        Carte(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Titre("Temps de jeu par semaine")
                if (sessions.isEmpty()) Doux("Les parties sont notées par le téléphone à partir de cette version (du lancement au retour à GC Station, appli ouverte ou en fond).", taille = 12)
                else {
                    val max = (parSemaine.maxOrNull() ?: 1L).coerceAtLeast(1L)
                    Row(Modifier.fillMaxWidth().height(120.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.Bottom) {
                        for ((k, m) in parSemaine.withIndex()) Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                            if (m > 0) Doux(if (m >= 60) (m / 60).toString() + "h" else m.toString(), taille = 9)
                            Box(Modifier.fillMaxWidth().height((96f * m / max).coerceAtLeast(2f).dp).clip(RoundedCornerShape(4.dp))
                                .background(if (k == parSemaine.size - 1) C.Rose else C.Violet.copy(alpha = 0.7f)))
                        }
                    }
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        for (d in semaines) Box(Modifier.weight(1f), contentAlignment = Alignment.Center) { Doux(df.format(java.util.Date(d)), taille = 8, lignes = 1) }
                    }
                    Doux("Cette semaine : " + duree(parSemaine.last()) + " · la semaine dernière : " + duree(parSemaine[parSemaine.size - 2]), taille = 12)
                }
            }
        }
        // cette semaine, par jeu (telephone)
        val semaine = sessions.filter { it.debut >= cette }.groupBy { it.id }.map { (_, l) -> l.first().nom to l.sumOf { it.sec } / 60 }.sortedByDescending { it.second }
        if (semaine.isNotEmpty()) Barres("Cette semaine", semaine.map { it.first to it.second }, C.Rose) { duree(it) }
        // depuis toujours (console)
        if (js != null) {
            val temps = js.filter { it.minutes > 0 }.sortedByDescending { it.minutes }.take(10)
            if (temps.isNotEmpty()) Barres("Les plus joués (temps, depuis toujours)", temps.map { it.nom to it.minutes.toLong() }, C.Violet) { duree(it) }
            val lances = js.filter { it.lancements > 0 }.sortedByDescending { it.lancements }.take(10)
            if (lances.isNotEmpty()) Barres("Les plus lancés", lances.map { it.nom to it.lancements.toLong() }, C.Cyan) { "$it fois" }
            val recents = js.filter { it.dernier > 0 }.sortedByDescending { it.dernier }.take(8)
            if (recents.isNotEmpty()) Carte(Modifier.fillMaxWidth()) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Titre("Joués dernièrement")
                    val dfl = java.text.SimpleDateFormat("EEEE d MMMM", java.util.Locale.FRANCE)
                    for (g in recents) Row(verticalAlignment = Alignment.CenterVertically) {
                        Doux(g.nom, taille = 13, modifier = Modifier.weight(1f), lignes = 1)
                        Doux(dfl.format(java.util.Date(g.dernier * 1000)), taille = 11)
                    }
                }
            }
            if (temps.isEmpty() && lances.isEmpty()) Doux("Pas encore de jeu lancé avec GC Station.")
        }
        // les dernieres parties notees par le telephone
        if (sessions.isNotEmpty()) Carte(Modifier.fillMaxWidth()) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Titre("Dernières parties")
                val dfp = java.text.SimpleDateFormat("EEE d MMM HH:mm", java.util.Locale.FRANCE)
                for (s in sessions.takeLast(12).reversed()) Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) { Text(s.nom, color = C.Texte, fontSize = 13.sp, maxLines = 1); Doux(dfp.format(java.util.Date(s.debut)), taille = 11) }
                    Text(duree(s.sec / 60), color = C.Rose, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                }
            }
        }
        Spacer(Modifier.height(10.dp))
    }
}

@Composable
private fun Barres(titre: String, l: List<Pair<String, Long>>, couleur: Color, texte: (Long) -> String) {
    val max = (l.maxOfOrNull { it.second } ?: 1L).coerceAtLeast(1L)
    Carte(Modifier.fillMaxWidth()) {
        Column(verticalArrangement = Arrangement.spacedBy(7.dp)) {
            Titre(titre)
            for ((n, v) in l) Column {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Doux(n, taille = 13, modifier = Modifier.weight(1f), lignes = 1)
                    Spacer(Modifier.width(8.dp))
                    Text(texte(v), color = C.Texte, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                }
                Spacer(Modifier.height(3.dp))
                Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(C.Verre)) {
                    Box(Modifier.fillMaxWidth((v.toFloat() / max).coerceIn(0.02f, 1f)).height(6.dp).clip(RoundedCornerShape(3.dp)).background(couleur))
                }
            }
        }
    }
}
