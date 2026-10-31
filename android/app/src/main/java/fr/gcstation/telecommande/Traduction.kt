package fr.gcstation.telecommande

// Appli : traduction en francais sur le telephone (Google ML Kit, hors ligne : le texte ne part pas sur Internet).
// La langue du texte est reconnue (ML Kit language-id), puis il est traduit vers le francais ; le modele de la
// langue (~30 Mo) est telecharge une fois, a la premiere traduction. Les traductions sont gardees (traductions/).

import com.google.mlkit.common.model.DownloadConditions
import com.google.mlkit.nl.languageid.LanguageIdentification
import com.google.mlkit.nl.translate.TranslateLanguage
import com.google.mlkit.nl.translate.Translation
import com.google.mlkit.nl.translate.TranslatorOptions
import kotlinx.coroutines.suspendCancellableCoroutine
import java.io.File
import kotlin.coroutines.resume

object Traduction {
    class Resultat(val texte: String, val langue: String)
    private val NOMS = mapOf("en" to "l'anglais", "de" to "l'allemand", "es" to "l'espagnol", "it" to "l'italien", "ja" to "le japonais",
        "nl" to "le néerlandais", "pt" to "le portugais")
    fun nomLangue(code: String) = NOMS[code] ?: code

    private suspend fun <T> attendre(t: com.google.android.gms.tasks.Task<T>): T? = suspendCancellableCoroutine { c ->
        t.addOnSuccessListener { c.resume(it) }.addOnFailureListener { c.resume(null) }
    }

    /** le texte en francais (null : deja en francais, langue inconnue, ou traduction impossible) */
    suspend fun versFrancais(dir: File, cle: String, texte: String): Resultat? {
        if (texte.isBlank()) return null
        val f = File(dir, "traductions/$cle.txt")
        if (f.exists()) try { val l = f.readText(); val i = l.indexOf('\n'); if (i > 0) return Resultat(l.substring(i + 1), l.substring(0, i)) } catch (e: Exception) { }
        val code = attendre(LanguageIdentification.getClient().identifyLanguage(texte)) ?: return null
        if (code == "und" || code == "fr") return null
        val src = TranslateLanguage.fromLanguageTag(code) ?: return null
        val tr = Translation.getClient(TranslatorOptions.Builder().setSourceLanguage(src).setTargetLanguage(TranslateLanguage.FRENCH).build())
        try {
            attendre(tr.downloadModelIfNeeded(DownloadConditions.Builder().build()))   // (deja la : tout de suite)
            // (paragraphe par paragraphe : la mise en page est gardee)
            val parts = texte.split("\n")
            val out = StringBuilder()
            for ((k, p) in parts.withIndex()) {
                if (k > 0) out.append('\n')
                if (p.isBlank()) continue
                out.append(attendre(tr.translate(p)) ?: return null)
            }
            val r = out.toString()
            try { f.parentFile?.mkdirs(); f.writeText(code + "\n" + r) } catch (e: Exception) { }
            return Resultat(r, code)
        } finally { tr.close() }
    }
}
