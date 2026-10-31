package fr.gcstation.telecommande

// Appli : envoyer a la GameCube les jaquettes et les images des disques, telechargees par le telephone sur GameTDB
// (la console n'a rien a telecharger). Console v301 : /api/jaqliste dit, pour chaque jeu, ce qu'elle a deja
// (jaquette de la liste, jaquette complete, complete HD, disque) ; seul ce qui manque est envoye
// (PUT /upload?dir=jaquette&nid=<ID>&kind=c|f|h|d), en PNG d'origine : la console le convertit a la 1re vue.

import android.graphics.BitmapFactory
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import java.io.File
import java.io.FileInputStream

object JaquettesConsole {
    class Bilan(var envoyees: IntArray = IntArray(4), var introuvables: Int = 0, var ratees: Int = 0, var deja: Int = 0, var erreur: String? = null)

    private val GENRES = arrayOf("cover", "coverfull", "coverfullHQ", "disc")       // (c, f, h, d)
    private val KINDS = "cfhd"

    /** hd : aussi la jaquette complete HD (zoom de la boite sur la console : 1 a 2 Mo par jeu) ; etat(fait, total, texte) */
    suspend fun envoyer(dir: File, hd: Boolean, annule: () -> Boolean, etat: (Int, Int, String) -> Unit): Bilan = withContext(Dispatchers.IO) {
        val b = Bilan()
        val r = Api.get("/api/jaqliste", 25000)
        if (r == null) { b.erreur = "La console ne répond pas, ou n'est pas à jour (il faut la v301 ou plus récente)"; return@withContext b }
        val l = try { JSONArray(r) } catch (e: Exception) { b.erreur = "Réponse de la console illisible"; return@withContext b }
        val ch = Api.canChunk()
        for (k in 0 until l.length()) {
            if (annule()) break
            val j = l.optJSONArray(k) ?: continue
            val id = j.optString(0)
            if (id.length != 6) continue
            etat(k, l.length(), id)
            for (t in 0 until 4) {
                if (annule()) break
                if (t == 2 && !hd) continue
                if (j.optInt(t + 1) != 0) { b.deja++; continue }
                val f = JaquettesHQ.fichier(dir, GENRES[t], id)
                if (f == null || f.length() <= 0) { b.introuvables++; continue }
                if (t == 2) {                                   // (la console veut la vraie HD : 1024 x 680 au moins)
                    val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    BitmapFactory.decodeFile(f.path, o)
                    if (o.outHeight < 680) { b.introuvables++; continue }
                }
                etat(k, l.length(), id + " · " + arrayOf("jaquette", "jaquette complète", "jaquette HD", "disque")[t])
                val q = "jaquette&nid=$id&kind=" + KINDS[t]
                val ok = FileInputStream(f).use { inp -> if (ch) Api.putChunks(q, "$id.png", inp, f.length(), null) else Api.put(q, "$id.png", inp, f.length()) } != null
                if (ok) b.envoyees[t]++ else b.ratees++
            }
        }
        etat(l.length(), l.length(), "")
        b
    }

    fun resume(b: Bilan): String {
        b.erreur?.let { return it }
        val n = b.envoyees.sum()
        val parts = listOf("jaquette" to b.envoyees[0], "complète" to b.envoyees[1], "HD" to b.envoyees[2], "disque" to b.envoyees[3])
            .filter { it.second > 0 }.joinToString(", ") { it.second.toString() + " " + it.first + (if (it.second > 1 && it.first != "HD") "s" else "") }
        return (if (n == 0) "Rien à envoyer : la console a déjà tout" + (if (b.introuvables > 0) " (sauf ce que GameTDB n'a pas)" else "") else "$n image" + (if (n > 1) "s" else "") + " envoyée" + (if (n > 1) "s" else "") + " : " + parts) +
            (if (b.introuvables > 0 && n > 0) " · " + b.introuvables + " absente" + (if (b.introuvables > 1) "s" else "") + " de GameTDB" else "") +
            (if (b.ratees > 0) " · " + b.ratees + " envoi" + (if (b.ratees > 1) "s" else "") + " raté" + (if (b.ratees > 1) "s" else "") else "")
    }
}
