package fr.gcstation.telecommande

// Appli : hors-ligne (sans la console). Ce que l'appli a vu est garde sur le telephone (filesDir : Android ne l'efface
// pas comme le cache) : la liste des jeux (temps de jeu, favoris, pastilles), les jaquettes, les boites 3D et les disques
// (GameTDB), les pages des notices. « Tout garder pour hors-ligne » telecharge d'un coup tout ce qui manque.

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

object Hors {
    @Volatile private var pret = false
    /** le dossier du telephone ou tout est garde (la 1re fois : ce qui etait dans le cache y est deplace) */
    fun dir(c: Context): File {
        val d = File(c.filesDir, "hors-ligne")
        if (!pret) {
            pret = true
            d.mkdirs()
            for (s in listOf("jaquettes", "notices")) {
                val a = File(c.cacheDir, s); val b = File(d, s)
                if (a.isDirectory && !b.exists()) { if (!a.renameTo(b)) try { a.copyRecursively(b, overwrite = false); a.deleteRecursively() } catch (e: Exception) { } }
            }
        }
        return d
    }
    /** la liste des jeux (reponse de /api/games) et les pastilles, gardees pour hors-ligne */
    fun garderJeux(c: Context, jeux: String, compat: String?) {
        try { File(dir(c), "jeux.json").writeText(jeux); if (compat != null) File(dir(c), "compat.json").writeText(compat) } catch (e: Exception) { }
    }
    /** {liste des jeux, pastilles ou null, date (ms)} gardees ; null : jamais gardees */
    fun jeux(c: Context): Triple<String, String?, Long>? {
        val f = File(dir(c), "jeux.json")
        if (!f.exists()) return null
        val p = File(dir(c), "compat.json")
        return try { Triple(f.readText(), if (p.exists()) p.readText() else null, f.lastModified()) } catch (e: Exception) { null }
    }
    /** pages de la notice gardees sur le telephone */
    fun pagesNotice(c: Context, id: String): Int {
        var n = 0
        while (n < 99 && File(dir(c), "notices/$id/" + String.format("%02d", n + 1) + ".jpg").let { it.exists() && it.length() > 0 }) n++
        return n
    }
    /** tout garder pour hors-ligne : jaquette, boite 3D et disque, notice de chaque jeu ; etat(fait, total, texte) */
    suspend fun toutGarder(act: StationActivity, jeux: List<Jeu>, annule: () -> Boolean, etat: (Int, Int, String) -> Unit): Int = withContext(Dispatchers.IO) {
        val d = dir(act)
        var pages = 0
        for ((k, g) in jeux.withIndex()) {
            if (annule()) break
            etat(k, jeux.size, g.nom)
            Jaquettes.charger(d, g.id, g.i)                       // (la jaquette : depuis la console)
            JaquettesHQ.charger(d, g.id, act)                     // (boite 3D et disque : GameTDB, par Internet ; disque agrandi par IA)
            val n = Notices.pages(g.id)
            for (p in 1..n) { if (annule()) break; etat(k, jeux.size, g.nom + " · notice " + p + " / " + n); if (Notices.page(d, g.id, p) != null) pages++ }
        }
        etat(jeux.size, jeux.size, "")
        pages
    }
}
