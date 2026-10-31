package fr.gcstation.telecommande

// Appli : les fiches des jeux GameCube (GameTDB : editeur, annee, genre...), la meme base que la console (release
// « fiches » du depot, fiches-gc.txt : ID, titre, genre, joueurs, developpeur, editeur, annee, classification, resume ;
// separes par des tabulations). Telechargee par le telephone, gardee 30 jours. Sert au tri de l'etagere.

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.net.HttpURLConnection
import java.net.URL

object Fiches {
    class Fiche(val genre: String, val developpeur: String, val editeur: String, val annee: Int,
                val joueurs: String = "", val classification: String = "", val resume: String = "")
    @Volatile private var memo: Map<String, Fiche>? = null
    private const val URL_FICHES = "https://github.com/antho13otz/gc-mp3/releases/download/fiches/fiches-gc.txt"

    /** les fiches (vide si pas d'Internet et rien de garde) */
    suspend fun charger(dir: File): Map<String, Fiche> = withContext(Dispatchers.IO) {
        memo?.let { return@withContext it }
        val f = File(dir, "fiches-gc.txt")
        if (!f.exists() || System.currentTimeMillis() - f.lastModified() > 30L * 86400000) telecharger(f)
        val m = HashMap<String, Fiche>()
        if (f.exists()) try {
            f.forEachLine(Charsets.UTF_8) { l ->
                val c = l.split('\t')
                if (c.size >= 7 && c[0].length == 6) {
                    val an = Regex("\\d{4}").find(c[6])?.value?.toIntOrNull() ?: 0
                    m[c[0]] = Fiche(c[2].trim(), c[4].trim(), c[5].trim(), an, c[3].trim(), c.getOrNull(7)?.trim() ?: "",
                        (c.getOrNull(8) ?: "").replace("\\n", "\n").trim())
                }
            }
        } catch (e: Exception) { }
        if (m.isNotEmpty()) memo = m
        m
    }

    private fun telecharger(f: File) {
        var c: HttpURLConnection? = null
        try {
            var u = URL(URL_FICHES)
            for (k in 0 until 5) {                                   // (GitHub redirige vers son stockage)
                c = u.openConnection() as HttpURLConnection
                c.instanceFollowRedirects = false
                c.connectTimeout = 8000; c.readTimeout = 30000
                c.setRequestProperty("User-Agent", "GC-Station-telecommande")
                val code = c.responseCode
                if (code in 300..399) { u = URL(u, c.getHeaderField("Location") ?: return); c.disconnect(); continue }
                if (code != 200) return
                val t = File(f.path + ".part")
                c.inputStream.use { inp -> t.outputStream().use { inp.copyTo(it) } }
                if (t.length() > 1000) { f.delete(); t.renameTo(f) } else t.delete()
                return
            }
        } catch (e: Exception) { } finally { c?.disconnect() }
    }
}
