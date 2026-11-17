package fr.gcstation.telecommande

// Appli build 565 (privee, GC Station v453) : les journaux de la console, sans passer par la carte SD.
// /api/journaux : la liste (nom, taille, date) ; /api/journaux/<nom> : le fichier (seulement ceux de la liste de la console :
// dernier lancement, journal du jeu, fluidite, demarrage, plantage, reparation). Voir, partager, ou garder dans Telechargements.

import android.content.ContentValues
import android.content.Intent
import android.os.Build
import android.provider.MediaStore
import android.widget.Toast
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private class Journal(val nom: String, val taille: Long, val quand: Long)

private val NOMS = mapOf(
    "dernier-lancement.txt" to "Dernier lancement d'un jeu",
    "journal-jeu.txt" to "Journal de la dernière partie",
    "fluidite.txt" to "Mesure de fluidité",
    "demarrage.txt" to "Démarrage de GC Station",
    "gc-mp3-crash.txt" to "Dernier plantage",
    "reparation.txt" to "Réparation d'un jeu")

private fun tailleTxt(sz: Long) = if (sz >= 1048576) String.format(Locale.FRANCE, "%.1f Mo", sz / 1048576.0) else maxOf(1L, (sz + 1023) / 1024).toString() + " Ko"

/** garde le texte dans Telechargements/GC Station ; rend le chemin montre, ou null */
private fun garder(act: StationActivity, nom: String, txt: String): String? = try {
    val base = nom.removeSuffix(".txt") + "-" + SimpleDateFormat("yyyyMMdd-HHmm", Locale.FRANCE).format(Date()) + ".txt"
    if (Build.VERSION.SDK_INT >= 29) {
        val v = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, base)
            put(MediaStore.MediaColumns.MIME_TYPE, "text/plain")
            put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/GC Station")
        }
        val u = act.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v) ?: throw Exception()
        act.contentResolver.openOutputStream(u)?.use { it.write(txt.toByteArray(Charsets.UTF_8)) } ?: throw Exception()
        "Téléchargements/GC Station/$base"
    } else {
        val d = java.io.File(act.getExternalFilesDir(null), "journaux").apply { mkdirs() }
        java.io.File(d, base).writeText(txt, Charsets.UTF_8)
        d.path + "/" + base
    }
} catch (e: Exception) { null }

@Composable
fun EcranJournaux() {
    val act = LocalContext.current as StationActivity
    val sc = rememberCoroutineScope()
    var liste by remember { mutableStateOf<List<Journal>?>(null) }
    var erreur by remember { mutableStateOf("") }
    var ouvert by remember { mutableStateOf<Journal?>(null) }
    var texte by remember { mutableStateOf<String?>(null) }
    var tour by remember { mutableStateOf(0) }
    LaunchedEffect(tour, St.connecte) {
        if (!St.connecte) { erreur = "Console pas connectée."; return@LaunchedEffect }
        val r = withContext(Dispatchers.IO) { Api.get("/api/journaux", 5000) }
        if (r == null) { erreur = "Pas de réponse de la console (GC Station v453 ou plus)."; liste = null; return@LaunchedEffect }
        try {
            val a = JSONArray(r)
            liste = (0 until a.length()).map { val o = a.getJSONObject(it); Journal(o.optString("n"), o.optLong("s"), o.optLong("t")) }
            erreur = ""
        } catch (e: Exception) { erreur = "Réponse illisible de la console." }
    }
    val j = ouvert
    if (j != null) {
        LaunchedEffect(j.nom) {
            texte = null
            texte = withContext(Dispatchers.IO) { Api.get("/api/journaux/" + j.nom, 20000) } ?: ""
        }
        Column(Modifier.fillMaxSize().padding(horizontal = 14.dp)) {
            Titre(NOMS[j.nom] ?: j.nom)
            Doux(j.nom + " · " + tailleTxt(j.taille), taille = 11)
            Spacer(Modifier.height(8.dp))
            val t = texte
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Bouton("Retour", Modifier.weight(1f), hauteur = 40.dp) { ouvert = null }
                Bouton("Partager", Modifier.weight(1f), plein = true, icone = Ic.Partager, hauteur = 40.dp, actif = !t.isNullOrEmpty()) {
                    if (t.isNullOrEmpty()) return@Bouton
                    if (t.length > 300_000) {                    // (trop long pour un partage en texte : garde-le d'abord)
                        val p = garder(act, j.nom, t)
                        Lang.toast(act, if (p != null) "Trop long pour un partage direct : gardé dans $p" else "Impossible de garder le fichier", Toast.LENGTH_LONG).show()
                        return@Bouton
                    }
                    val i = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_SUBJECT, j.nom).putExtra(Intent.EXTRA_TEXT, t)
                    act.startActivity(Intent.createChooser(i, "Partager " + j.nom))
                }
                Bouton("Garder", Modifier.weight(1f), icone = Ic.Dossier, hauteur = 40.dp, actif = !t.isNullOrEmpty()) {
                    if (t.isNullOrEmpty()) return@Bouton
                    sc.launch {
                        val p = withContext(Dispatchers.IO) { garder(act, j.nom, t) }
                        Lang.toast(act, if (p != null) "Gardé dans $p" else "Impossible de garder le fichier", Toast.LENGTH_LONG).show()
                    }
                }
            }
            Spacer(Modifier.height(10.dp))
            Column(Modifier.weight(1f).fillMaxWidth().verre(RoundedCornerShape(18.dp)).padding(12.dp)) {
                when {
                    t == null -> Doux("Lecture du journal…")
                    t.isEmpty() -> Doux("Journal introuvable ou vide.")
                    else -> SelectionContainer {
                        val v = rememberScrollState(); val h = rememberScrollState()
                        Text(if (t.length > 400_000) t.substring(0, 400_000) + "\n…(la suite : Partager ou Garder)" else t,
                            Modifier.fillMaxSize().verticalScroll(v).horizontalScroll(h),
                            color = C.Texte, fontSize = 11.sp, fontFamily = FontFamily.Monospace, softWrap = false)
                    }
                }
            }
            Spacer(Modifier.height(10.dp))
        }
        return
    }
    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item {
            Doux("Les journaux de la console, à lire ici ou à partager, sans retirer la carte SD.", taille = 12)
            Spacer(Modifier.height(4.dp))
            Bouton("Actualiser", Modifier.fillMaxWidth(), hauteur = 40.dp) { tour++ }
        }
        if (erreur.isNotEmpty()) item { Doux(erreur, couleur = C.Orange) }
        val l = liste
        if (l != null && l.isEmpty()) item { Doux("Aucun journal sur la carte pour l'instant.") }
        if (l != null) items(l) { x ->
            Column(Modifier.fillMaxWidth().verre(RoundedCornerShape(20.dp)).appui { ouvert = x }.padding(14.dp)) {
                Text(NOMS[x.nom] ?: x.nom, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.Bold)
                Doux(x.nom + " · " + tailleTxt(x.taille) + (if (x.quand > 0) " · " + SimpleDateFormat("d MMM HH:mm", Locale.FRANCE).format(Date(x.quand * 1000)) else ""), taille = 11)
            }
        }
        item { Spacer(Modifier.height(10.dp)) }
    }
}
