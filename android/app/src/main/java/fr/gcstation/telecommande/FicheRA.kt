package fr.gcstation.telecommande

// Appli build 534 : les succes RetroAchievements d'un jeu, dans sa fiche (avant : seulement pendant une partie, Pulse > Succes).
// - L'empreinte du jeu vient de la console (/api/rahash, console v411 : gardee, sinon ~4 Mo du disque lus, quelques s) ;
//   gardee aussi sur le telephone (ra_h_<id>).
// - Le telephone demande au site (comme rcheevos : dorequest.php) : gameid (empreinte -> jeu), patch (la liste des succes,
//   avec le compte connecte), unlocks (ceux obtenus : normal, et hardcore dans l'appli privee). Rien n'est demarre :
//   pas de session de jeu, la presence du compte ne change pas.

import android.content.Context
import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.foundation.Image
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject

object FicheRA {
    class Succes(val id: Long, val titre: String, val desc: String, val points: Int, val badge: String, val obtenu: Boolean, val hardcore: Boolean)
    class Jeu(val titre: String, val succes: List<Succes>)
    /** null : pas de resultat ; message : ce qu'il faut dire a la place */
    class Resultat(val jeu: Jeu?, val message: String)

    private const val SITE = "https://retroachievements.org/dorequest.php"
    private val memoire = HashMap<String, Resultat>()           // (fiche rouverte : tout de suite, jusqu'a la fin de l'appli)
    private val badges = LruCache<String, ImageBitmap>(120)

    private fun demande(corps: String): JSONObject? = try {
        val c = java.net.URL(SITE).openConnection() as java.net.HttpURLConnection
        c.connectTimeout = 15000; c.readTimeout = 20000
        c.setRequestProperty("User-Agent", RetroNatif.agent)
        c.requestMethod = "POST"; c.doOutput = true
        c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
        c.outputStream.use { it.write(corps.toByteArray(Charsets.UTF_8)) }
        val st = c.responseCode
        val t = (if (st >= 400) c.errorStream else c.inputStream)?.use { it.readBytes().toString(Charsets.UTF_8) }
        if (t == null) null else JSONObject(t)
    } catch (e: Exception) { null }
    private fun e(s: String) = java.net.URLEncoder.encode(s, "UTF-8")

    /** empreinte du jeu : gardee sur le telephone, sinon demandee a la console (quelques secondes la 1re fois) */
    suspend fun empreinte(c: Context, id: String, i: Int): String {
        val pr = c.getSharedPreferences("gcs", 0)
        pr.getString("ra_h_$id", null)?.takeIf { it.length == 32 }?.let { return it }
        val r = lire("/api/rahash?id=$id&i=${maxOf(0, i)}", 12000) ?: return ""
        val h = try { JSONObject(r).optString("h") } catch (e: Exception) { "" }
        if (h.length == 32) pr.edit().putString("ra_h_$id", h).apply()
        return h
    }

    suspend fun charger(c: Context, id: String, i: Int, encore: Boolean = false): Resultat {
        if (!encore) memoire[id]?.let { return it }
        val pr = c.getSharedPreferences("gcs", 0)
        val u = pr.getString("ra_user", "") ?: ""; val t = pr.getString("ra_token", "") ?: ""
        if (u.isEmpty() || t.isEmpty()) return Resultat(null, "Connecte-toi à RetroAchievements (Pulse > Succès) pour voir les succès de ce jeu.")
        val h = empreinte(c, id, i)
        if (h.isEmpty()) return Resultat(null, "Empreinte du jeu pas lue (console v411 et plus, allumée sur le même Wi-Fi).")
        val r = withContext(Dispatchers.IO) {
            val g = demande("r=gameid&m=" + e(h)) ?: return@withContext Resultat(null, "Pas de réponse de RetroAchievements (Internet ?)")
            val gid = g.optLong("GameID")
            if (gid <= 0) { garder(c, id, "0|0|0"); return@withContext Resultat(null, "Ce jeu (cette version du disque) n'a pas de succès sur RetroAchievements.") }
            val p = demande("r=patch&u=" + e(u) + "&t=" + e(t) + "&g=" + gid) ?: return@withContext Resultat(null, "Pas de réponse de RetroAchievements (Internet ?)")
            if (!p.optBoolean("Success", false)) return@withContext Resultat(null, "RetroAchievements : " + p.optString("Error", "refusé (reconnecte-toi dans Pulse > Succès)"))
            val pd = p.optJSONObject("PatchData") ?: return@withContext Resultat(null, "Réponse de RetroAchievements illisible")
            fun obtenus(hc: Int): Set<Long> {
                val o = demande("r=unlocks&u=" + e(u) + "&t=" + e(t) + "&g=" + gid + "&h=" + hc)?.optJSONArray("UserUnlocks") ?: return emptySet()
                return (0 until o.length()).map { o.optLong(it) }.toSet()
            }
            val normal = obtenus(0); val hard = if (Prive.ACTIF) obtenus(1) else emptySet()
            val a = pd.optJSONArray("Achievements")
            val l = ArrayList<Succes>()
            for (k in 0 until (a?.length() ?: 0)) {
                val x = a!!.optJSONObject(k) ?: continue
                if (x.optInt("Flags") != 3) continue                            // (3 : succes officiels ; 5 : pas encore officiels)
                val sid = x.optLong("ID")
                l += Succes(sid, x.optString("Title"), x.optString("Description"), x.optInt("Points"), x.optString("BadgeName"),
                    sid in normal || sid in hard, sid in hard)
            }
            if (l.isEmpty()) { garder(c, id, "0|0|0"); Resultat(null, "Pas encore de succès officiels pour ce jeu sur RetroAchievements.") }
            else { garder(c, id, "$gid|${l.size}|${l.count { it.obtenu }}"); Resultat(Jeu(pd.optString("Title"), l), "") }
        }
        if (r.jeu != null) memoire[id] = r
        return r
    }

    // ---------- build 553 (appli privee) : les jeux de la collection qui ont des succes, et ta progression ----------
    /** ce qu'on sait d'un jeu : null (pas encore regarde), ou (succes officiels, obtenus) ; 0 succes : pas de succes */
    fun connu(c: Context, id: String): Pair<Int, Int>? {
        val v = c.getSharedPreferences("gcs", 0).getString("ra_c_$id", null) ?: return null
        val t = v.split('|'); return if (t.size >= 3) (t[1].toIntOrNull() ?: 0) to (t[2].toIntOrNull() ?: 0) else null
    }
    private fun garder(c: Context, id: String, v: String) { c.getSharedPreferences("gcs", 0).edit().putString("ra_c_$id", v).apply() }
    /** regarde tous les jeux pas encore regardes (encore : tous), un par un ; rend (avec succes, regardes) */
    suspend fun verifierTous(c: Context, jeux: List<fr.gcstation.telecommande.Jeu>, encore: Boolean, arret: () -> Boolean, etape: (Int, Int, String) -> Unit): Pair<Int, Int> {
        var avec = 0; var vus = 0
        for ((k, g) in jeux.withIndex()) {
            if (arret()) break
            if (!encore && connu(c, g.id) != null) { if ((connu(c, g.id)?.first ?: 0) > 0) avec++; continue }
            etape(k, jeux.size, g.nom)
            val r = charger(c, g.id, g.i, encore = true)
            vus++
            if (r.jeu != null) avec++
            if (r.message.startsWith("Connecte-toi")) break
        }
        etape(jeux.size, jeux.size, "")
        return avec to vus
    }

    suspend fun badge(nom: String): ImageBitmap? {
        if (nom.isEmpty()) return null
        badges.get(nom)?.let { return it }
        return withContext(Dispatchers.IO) {
            try {
                val c = java.net.URL("https://media.retroachievements.org/Badge/$nom.png").openConnection() as java.net.HttpURLConnection
                c.connectTimeout = 10000; c.readTimeout = 15000
                c.setRequestProperty("User-Agent", RetroNatif.agent)
                val b = c.inputStream.use { BitmapFactory.decodeStream(it) }?.asImageBitmap()
                if (b != null) badges.put(nom, b)
                b
            } catch (e: Exception) { null }
        }
    }
}

/** fiche du jeu : ses succes RetroAchievements (ceux qui restent d'abord, badge grise ; les obtenus ensuite) */
@Composable
fun SuccesRADuJeu(c: Context, id: String, i: Int) {
    var res by remember(id) { mutableStateOf<FicheRA.Resultat?>(null) }
    var encore by remember(id) { mutableStateOf(0) }
    LaunchedEffect(id, encore) { res = FicheRA.charger(c, id, i, encore > 0) }
    Spacer(Modifier.height(18.dp))
    val r = res
    val j = r?.jeu
    if (j == null) {
        Titre("RetroAchievements")
        Doux(r?.message ?: "Lecture des succès RetroAchievements…", Modifier.padding(top = 6.dp), 12)
        if (r != null) { Spacer(Modifier.height(6.dp)); Bouton("Réessayer", Modifier.fillMaxWidth()) { res = null; encore++ } }
        return
    }
    val ob = j.succes.count { it.obtenu }
    val pts = j.succes.sumOf { it.points }; val ptsOb = j.succes.filter { it.obtenu }.sumOf { it.points }
    Titre("RetroAchievements · $ob / ${j.succes.size}")
    Doux("$ptsOb / $pts points" + (if (Prive.ACTIF && j.succes.any { it.hardcore }) " · " + j.succes.count { it.hardcore } + " en hardcore" else ""), Modifier.padding(top = 2.dp), 12)
    for (s in j.succes.sortedBy { it.obtenu }) {
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth().verre(RoundedCornerShape(16.dp), if (s.obtenu) C.Orange.copy(alpha = 0.55f) else C.Bord).padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically) {
            var img by remember(s.badge) { mutableStateOf<ImageBitmap?>(null) }
            LaunchedEffect(s.badge) { img = FicheRA.badge(s.badge) }
            Box(Modifier.size(44.dp).clip(RoundedCornerShape(8.dp))) {
                img?.let { Image(it, null, Modifier.size(44.dp).alpha(if (s.obtenu) 1f else 0.35f)) }
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text((if (s.obtenu) "🏆 " else "") + s.titre, color = if (s.obtenu) C.Texte else C.Doux, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                if (s.desc.isNotEmpty()) Doux(s.desc, taille = 12)
                if (s.hardcore) Doux("Obtenu en hardcore", taille = 11, couleur = C.Cyan)
            }
            Spacer(Modifier.width(8.dp))
            Doux("${s.points} pts", taille = 11)
        }
    }
}
