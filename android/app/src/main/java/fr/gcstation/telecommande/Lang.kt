package fr.gcstation.telecommande

// Appli : la langue (Plus > Apparence > Langue : langue du telephone, Francais ou English). Les textes de l'appli sont
// ecrits en francais ; en anglais, chaque texte affiche passe par Lang.t, qui cherche dans assets/en.tsv :
//  1. le texte entier ;
//  2. un modele avec des valeurs ($nom, %d…) : « Lire la notice (12 pages) » -> « Read the manual (12 pages) » ;
//  3. sinon les morceaux connus, du plus long au plus court, en un seul passage (textes composes a l'execution :
//     « Lancé 3 fois · dernière fois hier »). Ce qui reste inconnu (noms de jeux, de fichiers…) ne change pas.
// Tous les Text de l'appli (wrapper plus bas), les toasts (Lang.toast) et les vues Java (Neon, setText) y passent.

import android.content.Context
import android.widget.Toast
import androidx.compose.material3.LocalTextStyle
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.TextUnit
import java.util.concurrent.ConcurrentHashMap

object Lang {
    /** "auto" (langue du telephone), "fr" ou "en" */
    var choix by mutableStateOf("auto")
    /** true : l'appli s'affiche en anglais */
    var anglais by mutableStateOf(false)
        private set

    private class Modele(val re: Regex, val en: String, val noms: List<String>)

    @Volatile private var exact: Map<String, String> = emptyMap()
    @Volatile private var modeles: List<Modele> = emptyList()
    @Volatile private var morceaux: Map<Char, List<Pair<String, String>>> = emptyMap()
    @Volatile private var charge = false
    private val cache = ConcurrentHashMap<String, String>()
    private val PLACE = Regex("""\$\{[^}]*\}|\$[A-Za-z_][A-Za-z0-9_.]*|%[,.\d]*[dsf]""")

    @JvmStatic fun init(c: Context) {
        if (charge) return
        synchronized(this) {
            if (charge) return
            val ctx = c.applicationContext ?: c
            choix = try { ctx.getSharedPreferences("gcs", 0).getString("langue", "auto") ?: "auto" } catch (e: Exception) { "auto" }
            appliquer()
            try { charger(ctx) } catch (e: Exception) { }
            charge = true
        }
    }

    fun choisir(c: Context, v: String) {
        choix = v
        try { c.getSharedPreferences("gcs", 0).edit().putString("langue", v).apply() } catch (e: Exception) { }
        appliquer()
    }

    private fun appliquer() {
        anglais = when (choix) {
            "en" -> true
            "fr" -> false
            else -> !java.util.Locale.getDefault().language.equals("fr", true)
        }
    }

    private fun charger(c: Context) {
        val ex = HashMap<String, String>()
        val mo = ArrayList<Modele>()
        val bouts = ArrayList<Pair<String, String>>()
        c.assets.open("en.tsv").bufferedReader(Charsets.UTF_8).useLines { lignes ->
            for (l in lignes) {
                if (l.isEmpty() || l.startsWith("#")) continue
                val i = l.indexOf('\t'); if (i <= 0) continue
                val fr = deco(l.substring(0, i)); var en = deco(l.substring(i + 1))
                if (en.isNotEmpty() && fr.endsWith(" ") && (en.last().isLetterOrDigit() || en.last() in ":·,.…")) en += " "    // (espace de fin : gardee)
                if (PLACE.containsMatchIn(fr)) {
                    // modele : les valeurs deviennent (.+?), retrouvees dans l'anglais par leur nom ou leur ordre
                    val noms = PLACE.findAll(fr).map { it.value }.toList()
                    val sb = StringBuilder("^"); var k = 0
                    for (m in PLACE.findAll(fr)) { sb.append(Regex.escape(fr.substring(k, m.range.first))); sb.append("(.+?)"); k = m.range.last + 1 }
                    sb.append(Regex.escape(fr.substring(k))).append("$")
                    mo.add(Modele(Regex(sb.toString(), RegexOption.DOT_MATCHES_ALL), en, noms))
                } else {
                    ex[fr] = en
                    if (fr.length >= 2 && fr.isNotBlank()) bouts.add(fr to en)
                }
            }
        }
        exact = ex
        modeles = mo
        morceaux = bouts.sortedByDescending { it.first.length }.groupBy { it.first[0] }
    }

    private fun deco(s: String) = Regex("""\\u([0-9A-Fa-f]{4})""").replace(s.replace("\\n", "\n").replace("\\t", "\t")) { it.groupValues[1].toInt(16).toChar().toString() }

    /** le texte dans la langue choisie */
    @JvmStatic fun t(s: String?): String {
        if (s == null) return ""
        if (!anglais || s.isEmpty() || !charge) return s
        cache[s]?.let { return it }
        val r = traduire(s, 0)
        if (cache.size > 4000) cache.clear()
        cache[s] = r
        return r
    }

    @JvmStatic fun t(s: CharSequence?): CharSequence? {
        if (s == null || !anglais) return s
        if (s is String) return t(s)
        if (s is android.text.Spanned && s.getSpans(0, s.length, Any::class.java).isNotEmpty()) return s
        return t(s.toString())
    }

    private fun traduire(s: String, prof: Int): String {
        exact[s]?.let { return it }
        // (espaces autour : gardes)
        val deb = s.length - s.trimStart().length; val fin = s.length - s.trimEnd().length
        if (deb + fin in 1 until s.length) exact[s.trim()]?.let { return s.substring(0, deb) + it + s.substring(s.length - fin) }
        if (prof < 2) for (m in modeles) {
            val r = m.re.find(s) ?: continue
            val vals = r.groupValues.drop(1).map { v -> if (v.any { it.isLetter() }) traduire(v, prof + 1) else v }
            var en = m.en
            // remplacement par nom ($pages…), sinon dans l'ordre (%d, %s…)
            val parNom = m.noms.all { it.startsWith("$") } && m.noms.toSet().size == m.noms.size
            if (parNom) for ((i, n) in m.noms.withIndex().sortedByDescending { it.value.length }) en = en.replace(n, vals[i])
            else { var i = 0; en = PLACE.replace(en) { vals.getOrElse(i++) { "" } } }
            return en
        }
        return morceaux(ILYA.replace(s, "$1 ago"))
    }

    private val ILYA = Regex("""\bil y a (\d+ (?:min|h))""")

    private fun lettre(c: Char) = c.isLetterOrDigit() || c == '\''

    private fun morceaux(s: String): String {
        val sb = StringBuilder(s.length + 16)
        var i = 0
        var change = false
        while (i < s.length) {
            var pris: Pair<String, String>? = null
            if (i == 0 || !lettre(s[i - 1]) || !lettre(s[i])) {
                val l = morceaux[s[i]]
                if (l != null) for (p in l) {
                    val f = p.first
                    if (!s.startsWith(f, i)) continue
                    val j = i + f.length
                    // (un mot entier : pas « Jeu » dans « Jeux »)
                    if (j < s.length && lettre(s[j]) && lettre(f.last())) continue
                    pris = p; break
                }
            }
            if (pris != null) { sb.append(pris.second); i += pris.first.length; change = true }
            else { sb.append(s[i]); i++ }
        }
        return if (change) sb.toString() else s
    }

    /** Toast.makeText, traduit */
    @JvmStatic fun toast(c: Context, s: CharSequence?, d: Int): Toast = Toast.makeText(c, t(s) ?: "", d)
}

/** Le Text de Compose, traduit (tous les ecrans Compose de l'appli l'utilisent a la place de material3.Text) */
@Composable
fun Text(
    text: String,
    modifier: Modifier = Modifier,
    color: Color = Color.Unspecified,
    fontSize: TextUnit = TextUnit.Unspecified,
    fontStyle: FontStyle? = null,
    fontWeight: FontWeight? = null,
    fontFamily: FontFamily? = null,
    letterSpacing: TextUnit = TextUnit.Unspecified,
    textDecoration: TextDecoration? = null,
    textAlign: TextAlign? = null,
    lineHeight: TextUnit = TextUnit.Unspecified,
    overflow: TextOverflow = TextOverflow.Clip,
    softWrap: Boolean = true,
    maxLines: Int = Int.MAX_VALUE,
    minLines: Int = 1,
    onTextLayout: ((TextLayoutResult) -> Unit)? = null,
    style: TextStyle = LocalTextStyle.current,
) {
    val en = Lang.anglais   // (lu ici : changer de langue redessine les textes)
    androidx.compose.material3.Text(text = if (en) Lang.t(text) else text, modifier = modifier, color = color, fontSize = fontSize,
        fontStyle = fontStyle, fontWeight = fontWeight, fontFamily = fontFamily, letterSpacing = letterSpacing,
        textDecoration = textDecoration, textAlign = textAlign, lineHeight = lineHeight, overflow = overflow, softWrap = softWrap,
        maxLines = maxLines, minLines = minLines, onTextLayout = onTextLayout ?: {}, style = style)
}
