package fr.gcstation.telecommande

// Appli (v315, GC Station v315) : les vraies cartes memoire branchees sur la console (slots A et B), depuis le
// telephone : la liste des sauvegardes (jeu, titre, blocs), une sauvegarde telechargee en .gci (Telechargements/GC
// Remote, puis partagee), un .gci du telephone ajoute sur la carte, une sauvegarde copiee sur la carte virtuelle de
// Pulse, ou effacee. GC Station fait le travail (/api/vraiecarte, sa boucle : les memes fonctions que START > Outils >
// Cartes memoire) : seulement hors partie (pendant un jeu, GC Station n'est pas la et la carte est au jeu).

import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

private class SauvegardeCarte(val i: Int, val blocs: Int, val jeu: String, val fichier: String, val nom: String, val titre: String, val sous: String)

@Composable
fun EcranVraiesCartes() {
    val act = LocalContext.current as StationActivity
    var slot by remember { mutableStateOf("A") }
    var liste by remember { mutableStateOf<List<SauvegardeCarte>?>(null) }
    var libre by remember { mutableStateOf(-1) }
    var message by remember { mutableStateOf("") }
    var charge by remember { mutableStateOf(0) }
    var choisie by remember { mutableStateOf<SauvegardeCarte?>(null) }
    var effacer by remember { mutableStateOf<SauvegardeCarte?>(null) }
    var occupe by remember { mutableStateOf(false) }

    fun relire() { charge++ }
    fun op(q: String, fini: (JSONObject?) -> Unit) {
        occupe = true
        Thread {
            val r = Api.get("/api/vraiecarte?s=$slot&$q", 35000)
            val j = try { if (r != null) JSONObject(r) else null } catch (e: Exception) { null }
            act.runOnUiThread { occupe = false; fini(j) }
        }.start()
    }
    val ajouter = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { u: Uri? ->
        if (u == null) return@rememberLauncherForActivityResult
        occupe = true
        Thread {
            var m = "Envoi impossible"
            try {
                val nom = "carte-" + System.currentTimeMillis() + ".gci"
                val taille = act.contentResolver.openAssetFileDescriptor(u, "r")?.use { it.length } ?: -1L
                val rep = act.contentResolver.openInputStream(u)?.use { Api.put("gci", nom, it, taille) }
                if (rep != null) {
                    val r = Api.get("/api/vraiecarte?s=$slot&op=imp&n=" + Api.enc(rep.trim().ifEmpty { nom }), 35000)
                    m = try { JSONObject(r ?: "").optString("m", m) } catch (e: Exception) { m }
                }
            } catch (e: Exception) { }
            act.runOnUiThread { occupe = false; Lang.toast(act, m, Toast.LENGTH_LONG).show(); relire() }
        }.start()
    }

    LaunchedEffect(slot, charge) {
        liste = null; message = "Lecture de la carte du slot $slot…"
        val r = withContext(Dispatchers.IO) { Api.get("/api/vraiecarte?s=$slot&op=liste", 35000) }
        try {
            val j = JSONObject(r ?: "")
            if (j.optInt("ok") == 0) { message = j.optString("m", "Carte illisible"); liste = emptyList(); return@LaunchedEffect }
            val l = j.getJSONArray("l")
            liste = (0 until l.length()).map { k -> val o = l.getJSONObject(k)
                SauvegardeCarte(o.getInt("i"), o.optInt("b"), o.optString("jeu"), o.optString("f"), o.optString("n"), o.optString("t"), o.optString("s")) }
            libre = j.optInt("libre", -1); message = ""
        } catch (e: Exception) {
            liste = emptyList()
            message = if (r == null) "Pas de réponse : GC Station v315 à l'écran (hors partie)." else "Réponse illisible (GC Station v315 ?)"
        }
    }

    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        item {
            Doux("Les cartes mémoire branchées sur la console. Hors partie seulement (pendant un jeu, la carte est au jeu).", taille = 12)
            Spacer(Modifier.height(8.dp))
            RangeeDePuces {
                Puce("Slot A", slot == "A") { slot = "A" }
                Puce("Slot B", slot == "B") { slot = "B" }
                Puce("Relire", false) { relire() }
            }
        }
        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Doux(when { message.isNotEmpty() -> message; liste == null -> ""; else -> (liste?.size ?: 0).toString() + " sauvegarde(s) · " + (if (libre >= 0) "$libre blocs libres" else "") },
                    taille = 12, couleur = if (message.isNotEmpty() && liste != null) C.Orange else C.Doux, modifier = Modifier.weight(1f))
            }
            Spacer(Modifier.height(6.dp))
            Bouton("Ajouter un .gci du téléphone sur cette carte", Modifier.fillMaxWidth(), icone = Ic.Ajouter, actif = !occupe) { ajouter.launch(arrayOf("*/*")) }
        }
        items(liste ?: emptyList()) { s ->
            Carte(Modifier.fillMaxWidth()) {
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.appui { choisie = s }) {
                    Pastille(Ic.Carte, C.Bleu, 36.dp)
                    Spacer(Modifier.height(1.dp).padding(start = 10.dp))
                    Column(Modifier.weight(1f).padding(start = 10.dp)) {
                        Text(s.titre.ifEmpty { s.fichier }, color = C.Texte, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
                        if (s.sous.isNotEmpty()) Doux(s.sous, taille = 12, lignes = 1)
                        Doux(s.jeu + " · " + s.blocs + (if (s.blocs > 1) " blocs" else " bloc"), taille = 11, couleur = C.Pale)
                    }
                }
            }
        }
        item { Spacer(Modifier.height(20.dp)) }
    }

    val c = choisie
    if (c != null) AlertDialog(onDismissRequest = { choisie = null }, containerColor = C.Fond2,
        title = { Text(c.titre.ifEmpty { c.fichier }, color = C.Texte) },
        text = { Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Doux(c.jeu + " · " + c.fichier + " · " + c.blocs + " bloc(s) · slot $slot", taille = 12)
            Bouton("Télécharger sur le téléphone (.gci)", Modifier.fillMaxWidth(), actif = !occupe) {
                choisie = null
                Lang.toast(act, "Lecture de la sauvegarde…", Toast.LENGTH_SHORT).show()
                Thread {
                    val b = Api.getBytes("/api/vraiecarte?s=$slot&op=gci&i=" + c.i + "&n=" + Api.enc(c.nom), 35000)
                    act.runOnUiThread { enregistrerGci(act, c.nom + ".gci", b) }
                }.start()
            }
            Bouton("Copier sur la carte virtuelle de Pulse", Modifier.fillMaxWidth(), actif = !occupe) {
                choisie = null
                op("op=vc&i=" + c.i + "&n=" + Api.enc(c.nom)) { j -> Lang.toast(act, j?.optString("m") ?: "Pas de réponse", Toast.LENGTH_LONG).show() }
            }
            Bouton("Effacer de la carte…", Modifier.fillMaxWidth(), couleur = C.Rouge, actif = !occupe) { choisie = null; effacer = c }
        } },
        confirmButton = { TextButton({ choisie = null }) { Text("Fermer", color = C.Doux) } })

    val e = effacer
    if (e != null) AlertDialog(onDismissRequest = { effacer = null }, containerColor = C.Fond2,
        title = { Text("Effacer cette sauvegarde ?", color = C.Texte) },
        text = { Doux((e.titre.ifEmpty { e.fichier }) + " (" + e.jeu + ") sera effacée de la vraie carte du slot $slot. Pense à la télécharger avant si tu veux la garder.", taille = 13) },
        confirmButton = { TextButton({
            effacer = null
            op("op=del&i=" + e.i + "&n=" + Api.enc(e.nom)) { j -> Lang.toast(act, j?.optString("m") ?: "Pas de réponse", Toast.LENGTH_LONG).show(); relire() }
        }) { Text("Effacer", color = C.Rouge) } },
        dismissButton = { TextButton({ effacer = null }) { Text("Annuler", color = C.Doux) } })
}

/** un .gci recu : Telechargements/GC Remote, puis le partage d'Android */
private fun enregistrerGci(a: android.app.Activity, nom: String, b: ByteArray?) {
    if (b == null || b.size < 64) { Lang.toast(a, "Sauvegarde pas reçue (GC Station v315, hors partie ?)", Toast.LENGTH_LONG).show(); return }
    if (android.os.Build.VERSION.SDK_INT < 29) { Lang.toast(a, "Android 10 ou plus est nécessaire", Toast.LENGTH_LONG).show(); return }
    var u: Uri? = null
    try {
        val cv = android.content.ContentValues()
        cv.put(android.provider.MediaStore.Downloads.DISPLAY_NAME, nom)
        cv.put(android.provider.MediaStore.Downloads.MIME_TYPE, "application/octet-stream")
        cv.put(android.provider.MediaStore.Downloads.RELATIVE_PATH, android.os.Environment.DIRECTORY_DOWNLOADS + "/GC Remote")
        u = a.contentResolver.insert(android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv)
        if (u != null) a.contentResolver.openOutputStream(u)?.use { it.write(b) }
    } catch (e: Exception) { u = null }
    if (u == null) { Lang.toast(a, "Impossible d'enregistrer le fichier", Toast.LENGTH_LONG).show(); return }
    Lang.toast(a, "Enregistrée : Téléchargements/GC Remote/$nom", Toast.LENGTH_LONG).show()
    val i = android.content.Intent(android.content.Intent.ACTION_SEND).setType("application/octet-stream")
        .putExtra(android.content.Intent.EXTRA_STREAM, u).addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
    try { a.startActivity(android.content.Intent.createChooser(i, "Envoyer $nom")) } catch (e: Exception) { }
}
