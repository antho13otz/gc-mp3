package fr.gcstation.telecommande

// Appli build 479 (console v389) : les succes des jeux, jeu par jeu. La console (/api/succes) donne chaque succes
// [ID du jeu, titre, description, obtenu] (v389 : aussi ceux des jeux pas encore lances avec Pulse) ; les noms des jeux
// viennent de la liste des jeux (/api/games). Pour chaque jeu : obtenus / total, une jauge, et en le touchant la liste :
// ceux qui restent d'abord (avec ce qu'il faut faire), puis ceux deja obtenus.

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray

private class Succes(val titre: String, val desc: String, val obtenu: Boolean)
private class JeuSucces(val id: String, val nom: String, val liste: List<Succes>) {
    val obtenus get() = liste.count { it.obtenu }
}

/** noms des jeux qui ont des succes fournis par GC Station (si la console ne les a pas dans sa liste) */
private val NOMS_CONNUS = mapOf(
    "GZLP01" to "The Wind Waker", "GZLE01" to "The Wind Waker (USA)", "GZ2P01" to "Twilight Princess", "GZ2E01" to "Twilight Princess (USA)",
    "GPIP01" to "Pikmin", "GPVP01" to "Pikmin 2", "GPVE01" to "Pikmin 2 (USA)", "GMSP01" to "Super Mario Sunshine",
    "GMSE01" to "Super Mario Sunshine (USA)", "GP5P01" to "Mario Party 5", "GP5E01" to "Mario Party 5 (USA)", "GMPE01" to "Mario Party 4 (USA)")

@Composable
fun EcranSucces() {
    val ctx = LocalContext.current
    var jeux by remember { mutableStateOf<List<JeuSucces>?>(null) }
    var erreur by remember { mutableStateOf(false) }
    var ouvert by remember { mutableStateOf("") }
    var filtre by remember { mutableStateOf(0) }                          // (0 tous, 1 commences, 2 a finir)
    LaunchedEffect(Unit) {
        val r = withContext(Dispatchers.IO) { Api.get("/api/succes", 8000) }
        val g = withContext(Dispatchers.IO) { Api.get("/api/games", 9000) }
        val noms = HashMap<String, String>(NOMS_CONNUS)
        try { val a = JSONArray(g ?: ""); for (i in 0 until a.length()) a.optJSONArray(i)?.let { x -> if (x.optString(1).length >= 6) noms[x.optString(1).take(6)] = x.optString(0) } } catch (e: Exception) { }
        try {
            val a = JSONArray(r ?: "")
            if (r != null) Attente.garderSucces(ctx, r)
            val par = LinkedHashMap<String, ArrayList<Succes>>()
            for (i in 0 until a.length()) { val x = a.optJSONArray(i) ?: continue
                par.getOrPut(x.optString(0)) { ArrayList() }.add(Succes(x.optString(1), x.optString(2), x.optInt(3) != 0)) }
            jeux = par.map { (id, l) -> JeuSucces(id, noms[id] ?: id, l) }
                .sortedWith(compareByDescending<JeuSucces> { it.obtenus > 0 }.thenByDescending { it.obtenus.toFloat() / it.liste.size }.thenBy { it.nom })
        } catch (e: Exception) { erreur = true }
    }
    val js = jeux
    if (js == null) { Box(Modifier.fillMaxSize().padding(20.dp)) { Doux(if (erreur) "La console ne répond pas (GC Station v268 et plus)." else "Lecture des succès sur la console…", couleur = if (erreur) C.Orange else C.Doux) }; return }
    val total = js.sumOf { it.liste.size }; val obtenus = js.sumOf { it.obtenus }
    val vus = when (filtre) { 1 -> js.filter { it.obtenus > 0 }; 2 -> js.filter { it.obtenus < it.liste.size }; else -> js }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(start = 14.dp, end = 14.dp, bottom = 20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(18.dp)).background(Color(0x22FFFFFF)).padding(14.dp)) {
                Row(verticalAlignment = Alignment.Bottom) {
                    Text("$obtenus", color = Color(0xFFFFD54A), fontSize = 30.sp, fontWeight = FontWeight.Black)
                    Text(" / $total succès", color = C.Texte, fontSize = 16.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(bottom = 4.dp))
                }
                Spacer(Modifier.height(6.dp))
                Jauge(if (total > 0) obtenus.toFloat() / total else 0f, Color(0xFFFFD54A))
                Spacer(Modifier.height(6.dp))
                Doux("${js.size} jeux · ${js.count { it.obtenus == it.liste.size && it.liste.isNotEmpty() }} terminés à 100 %", taille = 12)
            }
        }
        item {
            RangeeDePuces {
                Puce("Tous", filtre == 0) { filtre = 0 }
                Puce("Commencés", filtre == 1) { filtre = 1 }
                Puce("À finir", filtre == 2) { filtre = 2 }
            }
        }
        if (vus.isEmpty()) item { Doux(if (js.isEmpty()) "Aucun succès sur la console : ajoute un fichier SD:/gc-mp3/succes/<ID du jeu>.txt." else "Rien ici pour ce filtre.", taille = 13) }
        items(vus, key = { it.id }) { j ->
            val fini = j.obtenus == j.liste.size
            val couleur = if (fini) Color(0xFFFFD54A) else C.Cyan
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Color(0x1AFFFFFF)).appui { ouvert = if (ouvert == j.id) "" else j.id }.padding(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(j.nom, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Doux(j.id + if (fini) " · 100 % !" else "", taille = 11, lignes = 1)
                    }
                    Text("${j.obtenus} / ${j.liste.size}", color = couleur, fontSize = 15.sp, fontWeight = FontWeight.Black)
                }
                Spacer(Modifier.height(6.dp))
                Jauge(j.obtenus.toFloat() / j.liste.size.coerceAtLeast(1), couleur)
                if (ouvert == j.id) {
                    Spacer(Modifier.height(8.dp))
                    val restants = j.liste.filter { !it.obtenu }; val faits = j.liste.filter { it.obtenu }
                    if (restants.isNotEmpty()) { Doux("À faire · ${restants.size}", taille = 12, couleur = C.Cyan); for (s in restants) LigneSucces(s) }
                    if (faits.isNotEmpty()) { Spacer(Modifier.height(4.dp)); Doux("Obtenus · ${faits.size}", taille = 12, couleur = Color(0xFFFFD54A)); for (s in faits) LigneSucces(s) }
                } else Doux("Toucher pour voir la liste", taille = 10, lignes = 1)
            }
        }
    }
}

@Composable
private fun Jauge(f: Float, c: Color) {
    Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(3.dp)).background(Color(0x26FFFFFF))) {
        Box(Modifier.fillMaxHeight().fillMaxWidth(f.coerceIn(0f, 1f)).clip(RoundedCornerShape(3.dp)).background(c))
    }
}

@Composable
private fun LigneSucces(s: Succes) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.Top) {
        Text(if (s.obtenu) "🏆" else "○", color = if (s.obtenu) Color(0xFFFFD54A) else C.Doux, fontSize = 15.sp, modifier = Modifier.width(26.dp))
        Column(Modifier.weight(1f)) {
            Text(s.titre, color = if (s.obtenu) C.Texte else C.Texte.copy(alpha = 0.85f), fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            if (s.desc.isNotEmpty()) Doux(s.desc, taille = 12)
        }
    }
}
