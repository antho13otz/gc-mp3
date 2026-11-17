package fr.gcstation.telecommande

// Build 566 : sorti de src/prive/.../ExtrasRA.kt, pour les deux applis (privee et publique) : historique des succes
// RetroAchievements, redemarrage de GC Station et mise a jour de la console par le telephone. Le rapport de partie, les
// touches en direct et le mode hardcore restent dans l'appli privee (essais).

import android.content.Context
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject


object ExtrasCommun {
    private const val MAX_HISTO = 300

    // ---------- historique des succes ----------
    class Debloque(val t: Long, val jeu: String, val titre: String, val desc: String, val points: Int, val badge: String, val hardcore: Boolean)
    var rev by mutableIntStateOf(0)
    private fun pr(c: Context) = c.getSharedPreferences("gcs", 0)

    fun historique(c: Context): List<Debloque> = try {
        val a = JSONArray(pr(c).getString("ra_histo", "[]"))
        (0 until a.length()).mapNotNull { k -> a.optJSONObject(k)?.let {
            Debloque(it.optLong("t"), it.optString("j"), it.optString("n"), it.optString("d"), it.optInt("p"), it.optString("b"), it.optInt("h") != 0) } }
    } catch (e: Exception) { emptyList() }

    /** un succes vient d'etre debloque (evenement 1 de rcheevos) */
    fun succes(c: Context?, jeu: String, titre: String, desc: String, points: Int, badge: String, hardcore: Boolean) {
        val cc = c ?: return
        synchronized(this) {
            val a = try { JSONArray(pr(cc).getString("ra_histo", "[]")) } catch (e: Exception) { JSONArray() }
            val n = JSONArray()
            n.put(JSONObject().put("t", System.currentTimeMillis()).put("j", jeu).put("n", titre).put("d", desc).put("p", points).put("b", badge).put("h", if (hardcore) 1 else 0))
            for (k in 0 until minOf(a.length(), MAX_HISTO - 1)) n.put(a.opt(k))
            pr(cc).edit().putString("ra_histo", n.toString()).apply()
        }
        RetroSucces.ui.post { rev++ }
    }

    // ---------- badges (memoire + fichiers du cache) ----------
    private val badges = android.util.LruCache<String, ImageBitmap>(64)
    private fun badge(c: Context, url: String): ImageBitmap? {
        badges.get(url)?.let { return it }
        if (!url.startsWith("https://")) return null
        val f = java.io.File(c.cacheDir, "ra_badge_" + Integer.toHexString(url.hashCode()) + ".png")
        val b = try {
            if (f.length() > 0) android.graphics.BitmapFactory.decodeFile(f.path) else {
                val x = java.net.URL(url).openConnection() as java.net.HttpURLConnection
                x.connectTimeout = 4000; x.readTimeout = 4000; x.setRequestProperty("User-Agent", RetroNatif.agent)
                val o = x.inputStream.use { it.readBytes() }
                f.writeBytes(o); android.graphics.BitmapFactory.decodeByteArray(o, 0, o.size)
            }
        } catch (e: Exception) { null } ?: return null
        return b.asImageBitmap().also { badges.put(url, it) }
    }

    @Composable fun Historique() {
        val c = LocalContext.current
        var ouvert by remember { mutableStateOf(false) }
        @Suppress("UNUSED_VARIABLE") val r = rev
        val l = remember(rev) { historique(c) }
        Carte(Modifier.fillMaxWidth(), androidx.compose.foundation.layout.PaddingValues(12.dp)) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Row(Modifier.fillMaxWidth().appui { ouvert = !ouvert }, verticalAlignment = Alignment.CenterVertically) {
                    Titre("Historique des succès", Modifier.weight(1f))
                    Doux(l.size.toString() + " · " + l.sumOf { it.points } + " pts" + if (ouvert) "  ▾" else "  ▸", taille = 12)
                }
                if (ouvert) {
                    if (l.isEmpty()) Doux("Les succès RetroAchievements débloqués apparaîtront ici, avec la date.", taille = 12)
                    val fmt = java.text.SimpleDateFormat("d MMM yyyy · HH:mm", java.util.Locale.FRANCE)
                    var jeuPrec = ""
                    for (d in l.take(150)) {
                        if (d.jeu != jeuPrec) { jeuPrec = d.jeu; Text(d.jeu.ifEmpty { "Jeu inconnu" }, color = C.Cyan, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 4.dp)) }
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            var img by remember(d.badge) { mutableStateOf<ImageBitmap?>(badges.get(d.badge)) }
                            LaunchedEffect(d.badge) { if (img == null) img = withContext(Dispatchers.IO) { badge(c, d.badge) } }
                            Box(Modifier.size(40.dp).clip(RoundedCornerShape(8.dp)).background(Color(0x22FFFFFF))) {
                                img?.let { Image(it, null, Modifier.size(40.dp)) }
                            }
                            Spacer(Modifier.width(10.dp))
                            Column(Modifier.weight(1f)) {
                                Text("🏆 " + d.titre, color = C.Texte, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 2)
                                if (d.desc.isNotEmpty()) Doux(d.desc, taille = 11, lignes = 2)
                                Doux(fmt.format(java.util.Date(d.t)) + " · " + d.points + " pts" + if (d.hardcore) " · hardcore" else "", taille = 10, couleur = C.Pale)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------- redemarrage de GC Station ----------
    @Composable fun Redemarrer(act: android.app.Activity) {
        var demande by remember { mutableStateOf(false) }
        Spacer(Modifier.height(10.dp))
        Bouton("Redémarrer GC Station", Modifier.fillMaxWidth(), hauteur = 40.dp) { demande = true }
        if (demande) androidx.compose.material3.AlertDialog(
            onDismissRequest = { demande = false },
            title = { Text("Redémarrer GC Station ?") },
            text = { Text("Comme le bouton Reset de la console : GC Station enregistre ses réglages et se relance. Pas pendant une partie ni une mise à jour.") },
            confirmButton = {
                androidx.compose.material3.TextButton(onClick = {
                    demande = false
                    Api.pool {
                        val r = Api.get("/api/redemarrer", 3000)
                        act.runOnUiThread { Lang.toast(act, if (r != null && r.contains("ok")) "GC Station redémarre…" else "Pas de réponse (console v420 ou plus ?)", android.widget.Toast.LENGTH_LONG).show() }
                    }
                }) { Text("Redémarrer") }
            },
            dismissButton = { androidx.compose.material3.TextButton(onClick = { demande = false }) { Text("Annuler") } })
    }

    // ---------- build 563 (console v429) : mise a jour de la console par le telephone ----------
    // La console telecharge en HTTPS (dechiffre par son processeur) : des dizaines de minutes. Le telephone telecharge le
    // .dol en quelques secondes, l'envoie a la console sur le Wi-Fi (sans chiffrement, par morceaux), puis lui demande de
    // l'installer (/api/maj/installer) : meme verification et meme installation qu'un telechargement de la console.
    private var majTexte by mutableStateOf("")
    private var majEnCours by mutableStateOf(false)
    private fun majLancer(c: Context) {
        if (majEnCours) return
        majEnCours = true; majTexte = "Recherche de la dernière version…"
        Thread {
            var tmp: java.io.File? = null
            try {
                val m = Api.get("/api/maj", 4000)
                if (m == null) { majTexte = "La console ne répond pas (console v429 ou plus ?)"; return@Thread }
                val jm = org.json.JSONObject(m)
                val a = jm.optInt("build", 0)
                val rel = HttpGet.text(jm.optString("rel"), 15000)
                if (rel == null) { majTexte = "Impossible de lire la dernière version (GitLab)"; return@Thread }
                val build = Regex("build=(\\d+)").findAll(rel).maxOfOrNull { it.groupValues[1].toInt() } ?: 0
                val urls = Regex("\"browser_download_url\"\\s*:\\s*\"([^\"]+-b(\\d+)\\.dol)\"").findAll(rel).map { it.groupValues[1] to it.groupValues[2].toInt() }.toList()
                val choix = urls.firstOrNull { it.second == build } ?: urls.maxByOrNull { it.second }
                if (choix == null) { majTexte = "Pas encore de fichier publié (compilation en cours ?)"; return@Thread }
                val b = if (build > 0) build else choix.second
                if (a > 0 && b <= a) { majTexte = "La console est déjà à jour (build $a)"; return@Thread }
                // build 564 : la version compressee du meme build (2,3 Mo au lieu de 6 : envoi plus court, demarrage plus rapide),
                // un .dol complet qui se decompresse lui-meme ; la console l'installe pareil. Sinon le .dol classique
                val comp = Regex("\"browser_download_url\"\\s*:\\s*\"([^\"]+/gc-station-compresse-b$b\\.bin)\"").find(rel)?.groupValues?.get(1)
                val url = comp ?: choix.first
                majTexte = "Téléchargement du build $b${if (comp != null) " (compressé)" else ""}…"
                tmp = java.io.File(c.cacheDir, "maj-console.dol")
                var cx = java.net.URL(url).openConnection() as java.net.HttpURLConnection
                cx.connectTimeout = 10000; cx.readTimeout = 30000; cx.instanceFollowRedirects = true
                var red = 0
                while (cx.responseCode in 301..308 && red++ < 5) {     // (GitLab renvoie vers son stockage, parfois sur un autre domaine)
                    val loc = cx.getHeaderField("Location") ?: break
                    cx.disconnect(); cx = java.net.URL(java.net.URL(url), loc).openConnection() as java.net.HttpURLConnection
                    cx.connectTimeout = 10000; cx.readTimeout = 30000
                }
                if (cx.responseCode != 200) { majTexte = "Téléchargement refusé (code ${cx.responseCode})"; return@Thread }
                val total = cx.contentLengthLong
                cx.inputStream.use { i -> tmp.outputStream().use { o ->
                    val buf = ByteArray(65536); var n: Int; var lu = 0L
                    while (i.read(buf).also { n = it } > 0) { o.write(buf, 0, n); lu += n
                        if (total > 0) majTexte = "Téléchargement du build $b : ${lu * 100 / total} %" }
                } }
                cx.disconnect()
                val taille = tmp.length()
                if (taille < 0x100 || (total > 0 && taille != total)) { majTexte = "Téléchargement incomplet"; return@Thread }
                majTexte = "Envoi à la console…"
                val r = tmp.inputStream().use { Api.putChunks("maj", "nouveau.dol", it, taille) { d -> majTexte = "Envoi à la console : ${d * 100 / taille} %" } }
                if (r == null) { majTexte = "Envoi interrompu (réessaie)"; return@Thread }
                val ok = Api.get("/api/maj/installer?b=$b", 5000)
                majTexte = if (ok != null && ok.contains("\"ok\":1")) "Build $b envoyé : la console l'installe et se relance (regarde la télé)" else "La console a refusé l'installation"
            } catch (e: Exception) {
                majTexte = "Erreur : ${e.message ?: e.javaClass.simpleName}"
            } finally {
                try { tmp?.delete() } catch (e: Exception) { }
                majEnCours = false
            }
        }.start()
    }
    @Composable fun MajConsole(c: Context) {
        Spacer(Modifier.height(10.dp))
        Bouton(if (majEnCours) "Mise à jour en cours…" else "Mettre à jour la console par le téléphone", Modifier.fillMaxWidth(), hauteur = 40.dp, actif = !majEnCours) { majLancer(c.applicationContext) }
        if (majTexte.isNotEmpty()) Doux(majTexte, taille = 12, couleur = C.Texte)
        else Doux("Bien plus rapide que la mise à jour depuis la console : le téléphone télécharge, puis envoie sur le Wi-Fi.", taille = 11, couleur = C.Pale)
    }
}
